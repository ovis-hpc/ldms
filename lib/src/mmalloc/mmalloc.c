/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2016 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include "mmalloc.h"
#include "../coll/rbt.h"
#include "ovis-test/test.h"
#ifdef OVIS_LIB_MM_DEBUG
#define MM_DEBUG OVIS_LIB_MM_DEBUG
#endif

static int mm_is_disable_mm_free = 0;


struct mm_prefix {
	struct rbn addr_node;
	struct rbn size_node;
	size_t count;
	struct mm_prefix *pfx;
};

typedef struct mm_region {
	size_t grain;		/* minimum allocation size and alignment */
	size_t grain_bits;
	size_t size;
	void *start;
	pthread_mutex_t lock;
	struct rbt size_tree;
	struct rbt addr_tree;
} *mm_region_t;

static int compare_count(void *node_key, const void *val_key)
{
	return (int)(*(size_t *)node_key) - (*(size_t *)val_key);
}

static int compare_addr(void *node_key, const void *val_key)
{
	return (int)(*(char **)node_key - *(char **)val_key);
}

/* NB: only works for power of two r */
#define MMR_ROUNDUP(s,r)	((s + (r - 1)) & ~(r - 1))

static mm_region_t mmr;

#ifdef MM_DEBUG
static int mm_mmap = 1;

/* if mm_mmap is set to 0, then mmr and nommr are used to manage a
	list of pointers and sizes shoe-horned into mm_prefix structures.
- destroy searches are linear.
- mmr->start is returned NULL in info queries, which should prevent
	any code that requires start to be valid and point to mmapped RAM
	from functioning correctly.
*/
static struct mm_prefix nommr;
/**  nommr.size_node.color is count updated with allocs and frees. */
#define NOMM_COUNT nommr.size_node.color

/**  nommr.count is byte count updated with allocs and frees.
 mmr->size is the limit set in init. */
#define NOMM_SIZE nommr.count
#define NOMM_SIZE_MAX mmr->size

/** nommr->pfx is the first element of a list of mm_prefix which use pfx as the next pointer. */
#define NOMM_LIST (nommr.pfx)

/** mm_prefix->pfx is next pointer. */
#define NOMM_NEXT(p) ((p)->pfx)

/** mm_prefix->addr_node.key is the allocated memory. */
#define NOMM_DATA(p) (p->addr_node.key)

/** mm_prefix->count is the size of the allocation with that list element. */
#define NOMM_ALLOCSIZE(p) (p)->count

void mm_enable_debug()
{
	if (mmr)
		return;
	mm_mmap = 0;
}
#endif

void mm_get_info(struct mm_info *mmi)
{
	if (!mmr)
		return;
#ifdef MM_DEBUG
	if (mm_mmap) {
#endif
		mmi->grain = mmr->grain;
		mmi->grain_bits = mmr->grain_bits;
		mmi->size = mmr->size;
		mmi->start = mmr->start;
		return;
#ifdef MM_DEBUG
	}
	if (!mm_mmap) {
		mmi->grain = mmr->grain;
		mmi->grain_bits = mmr->grain_bits;
		mmi->size = mmr->size;
		mmi->start = NULL;
		return;
	}
#endif
}

static void get_pow2(size_t n, size_t *pow2, size_t *bits)
{
	size_t _pow2;
	size_t _bits;
	for (_pow2 = 1, _bits=0; _pow2 < n; _pow2 <<= 1, _bits++);
	*pow2 = _pow2;
	*bits = _bits;
}

int mm_init(size_t size, size_t grain)
{
	if (mmr)
		return ENOMEM;
	mmr = calloc(1, sizeof (*mmr));
	if (!mmr)
		return ENOMEM;
	pthread_mutex_init(&mmr->lock, NULL);
	size = MMR_ROUNDUP(size, 4096);
#ifdef MM_DEBUG
	if (mm_mmap) {
#endif
		mmr->start = mmap(NULL, size, PROT_READ | PROT_WRITE,
				  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (MAP_FAILED == mmr->start)
			goto out;

		memset(mmr->start, 0XAA, size);
#ifdef MM_DEBUG
	}
#endif

	get_pow2(grain, &mmr->grain, &mmr->grain_bits);
	mmr->size = size;

#ifdef MM_DEBUG
	if (mm_mmap) {
#endif
		/* Inialize the size and address r-b trees */
		rbt_init(&mmr->size_tree, compare_count);
		rbt_init(&mmr->addr_tree, compare_addr);

		/* Initialize the prefix */
		struct mm_prefix *pfx = mmr->start;
		pfx->count = size / mmr->grain;
		pfx->pfx = pfx;
		rbn_init(&pfx->size_node, &pfx->count);
		rbn_init(&pfx->addr_node, &pfx->pfx);

		/* Insert the chunk into the r-b trees */
		rbt_ins(&mmr->size_tree, &pfx->size_node);
		rbt_ins(&mmr->addr_tree, &pfx->addr_node);
#ifdef MM_DEBUG
	}
#endif
	const char *tmp = getenv("MMALLOC_DISABLE_MM_FREE");
	if (tmp)
		mm_is_disable_mm_free = atoi(tmp);

	return 0;
 out:
	free(mmr);
	return errno;
}

#ifdef MM_DEBUG
int mm_final()
{
	if (!mmr)
		return 0;
	if (mm_mmap) {
		size_t x = 0;
		struct rbn *rn;
		RBT_FOREACH(rn, &mmr->size_tree) {
			x++;
		}
		if (x == 1) {
			pthread_mutex_lock(&mmr->lock);
			munmap(mmr->start, mmr->size);
			memset(mmr, 0, sizeof(*mmr));
			pthread_mutex_unlock(&mmr->lock);
			goto out;
		} else {
			return EINVAL;
		}
	}
	if (NOMM_COUNT != 0) {
		return EINVAL;
	}
out:
	pthread_mutex_destroy(&mmr->lock);
	free(mmr);
	mmr = NULL;
	return 0;
}
#endif

void *mm_alloc(size_t size)
{
#ifdef MM_DEBUG
	if (mm_mmap) {
#endif
		struct mm_prefix *p, *n;
		struct rbn *rbn;
		uint64_t count;
		uint64_t remainder;

		size += sizeof(*p);
		size = MMR_ROUNDUP(size, mmr->grain);
		count = size >> mmr->grain_bits;

		pthread_mutex_lock(&mmr->lock);
		rbn = rbt_find_lub(&mmr->size_tree, &count);
		if (!rbn) {
			pthread_mutex_unlock(&mmr->lock);
			return NULL;
		}

		p = container_of(rbn, struct mm_prefix, size_node);

		/* Remove the node from the size and address trees */
		rbt_del(&mmr->size_tree, &p->size_node);
		rbt_del(&mmr->addr_tree, &p->addr_node);

		/* Create a new node from the remainder of p if any */
		remainder = p->count - count;
		if (remainder) {
			n = (struct mm_prefix *)
				((unsigned char *)p + (count << mmr->grain_bits));
			n->count = remainder;
			n->pfx = n;
			rbn_init(&n->size_node, &n->count);
			rbn_init(&n->addr_node, &n->pfx);

			rbt_ins(&mmr->size_tree, &n->size_node);
			rbt_ins(&mmr->addr_tree, &n->addr_node);
		}
		p->count = count;
		p->pfx = p;
		pthread_mutex_unlock(&mmr->lock);
		return ++p;
#ifdef MM_DEBUG
	}
	size = MMR_ROUNDUP(size, mmr->grain);
	void *d = NULL;
	int pmerr = posix_memalign(&d, mmr->grain, size);
	if (pmerr)
		d = NULL;
	struct mm_prefix *newelt = calloc(1, sizeof(*newelt));
	if (d && newelt) {
		memset(d, 0XAA, size);
		NOMM_ALLOCSIZE(newelt) = size;
		pthread_mutex_lock(&mmr->lock);
		NOMM_SIZE += size;
		if (NOMM_SIZE <= NOMM_SIZE_MAX) {
			NOMM_COUNT++;
			NOMM_DATA(newelt) = d;
			NOMM_NEXT(newelt) = NOMM_LIST;
			NOMM_LIST = newelt;
		} else {
			NOMM_SIZE -= size;
			free(d);
			free(newelt);
			d = NULL;
			newelt = NULL;
		}
		pthread_mutex_unlock(&mmr->lock);
	} else {
		free(d);
		free(newelt);
		d = NULL;
	}
	return d;
#endif
}

void mm_free(void *d)
{
	if (mm_is_disable_mm_free)
		return;

	if (!d)
		return;

#ifdef MM_DEBUG
	if (mm_mmap) {
#endif
		struct mm_prefix *p = d;
		struct mm_prefix *q, *r;
		struct rbn *rbn;

		p --;

		pthread_mutex_lock(&mmr->lock);
		/* See if we can coalesce with our lesser sibling */
		rbn = rbt_find_glb(&mmr->addr_tree, &p->pfx);
		if (rbn) {
			q = container_of(rbn, struct mm_prefix, addr_node);

			/* See if q is contiguous with us */
			r = (struct mm_prefix *)
				((unsigned char *)q + (q->count << mmr->grain_bits));
			if (r == p) {
				/* Remove the sibling from the tree and coelesce */
				rbt_del(&mmr->size_tree, &q->size_node);
				rbt_del(&mmr->addr_tree, &q->addr_node);

				q->count += p->count;
				p = q;
			}
		}

		/* See if we can coalesce with our greater sibling */
		rbn = rbt_find_lub(&mmr->addr_tree, &p->pfx);
		if (rbn) {
			q = container_of(rbn, struct mm_prefix, addr_node);

			/* See if q is contiguous with us */
			r = (struct mm_prefix *)
				((unsigned char *)p + (p->count << mmr->grain_bits));
			if (r == q) {
				/* Remove the sibling from the tree and coelesce */
				rbt_del(&mmr->size_tree, &q->size_node);
				rbt_del(&mmr->addr_tree, &q->addr_node);

				p->count += q->count;
			}
		}
		/* Fix-up our nodes' key in case we coelesced */
		p->pfx = p;
		rbn_init(&p->size_node, &p->count);
		rbn_init(&p->addr_node, &p->pfx);

		memset(p+1, 0xff, (p->count << mmr->grain_bits) - sizeof(*p));

		/* Put 'p' back in the trees */
		rbt_ins(&mmr->size_tree, &p->size_node);
		rbt_ins(&mmr->addr_tree, &p->addr_node);
		pthread_mutex_unlock(&mmr->lock);
		return;
#ifdef MM_DEBUG
	}
	if (!NOMM_LIST)
		return;
	pthread_mutex_lock(&mmr->lock);
	struct mm_prefix *oldelt = NOMM_LIST;
	struct mm_prefix *prev = NULL;
	while (oldelt && NOMM_DATA(oldelt) != d) {
		prev = oldelt;
		oldelt = NOMM_NEXT(oldelt);
	}
	if (oldelt) {
		if (prev)
			NOMM_NEXT(prev) = NOMM_NEXT(oldelt);
		else
			NOMM_LIST = NOMM_NEXT(oldelt);
		NOMM_SIZE -= NOMM_ALLOCSIZE(oldelt);
		NOMM_COUNT--;
		free(NOMM_DATA(oldelt));
		free(oldelt);
	}
	pthread_mutex_unlock(&mmr->lock);
#endif
}

static int heap_stat(struct rbn *rbn, void *fn_data, int level)
{
	struct mm_stat *s = fn_data;
	struct mm_prefix *mm =
		container_of(rbn, struct mm_prefix, addr_node);
	s->chunks++;
	s->bytes += mm->count;
	if (mm->count < s->smallest)
		s->smallest = mm->count;
	if (mm->count > s->largest)
		s->largest = mm->count;
	return 0;
}

void mm_stats(struct mm_stat *s)
{
	if (!s)
		return;
	memset(s,0,sizeof(*s));
	if (!mmr)
		return;
	s->size = mmr->size;
	s->grain = mmr->grain;
#ifdef MM_DEBUG
	if (mm_mmap) {
#endif
		s->smallest = s->size + 1;
		rbt_traverse(&mmr->addr_tree, heap_stat, s);
		return;
#ifdef MM_DEBUG
	}
	s->bytes = NOMM_SIZE;
	s->chunks = NOMM_COUNT;
	struct mm_prefix *oldelt = NOMM_LIST;
	while (oldelt != NULL) {
		if (s->smallest > NOMM_ALLOCSIZE(oldelt))
			s->smallest = NOMM_ALLOCSIZE(oldelt);
		if (s->largest < NOMM_ALLOCSIZE(oldelt))
			s->largest = NOMM_ALLOCSIZE(oldelt);
		oldelt = NOMM_NEXT(oldelt);
	}
#endif
}

int mm_format_stats(struct mm_stat *s, char *buf, size_t buflen)
{
	if (!buf || !buflen)
		return 0;
	if (!s) {
		return snprintf(buf, buflen, "mm_stat: null\n");
	}
#ifdef MM_DEBUG
	if (mm_mmap) {
#endif
		size_t used = s->size - s->grain*s->largest;
		return snprintf(buf, buflen,
			"mm_stat: size=%zu grain=%zu chunks_free=%zu grains_free=%zu grains_largest=%zu grains_smallest=%zu bytes_free=%zu bytes_largest=%zu bytes_smallest=%zu bytes_used+holes=%zu\n",
		s->size, s->grain, s->chunks, s->bytes, s->largest, s->smallest,
		s->grain*s->bytes, s->grain*s->largest, s->grain*s->smallest, used);
#ifdef MM_DEBUG
	} else {
		return snprintf(buf, buflen,
			"mm_stat_debug: size=%zu grain=%zu chunks_used=%zu bytes_used=%zu chunk_largest=%zu chunk_smallest=%zu bytes_free=%zu bytes_largest=na bytes_smallest=na bytes_used+holes=%zu\n",
		s->size, s->grain, s->chunks, s->bytes, s->largest, s->smallest,
		s->size - s->bytes, s->bytes);
	}
#endif
}

#ifdef MMR_TEST
int node_count;
int heap_print(struct rbn *rbn, void *fn_data, int level)
{
	struct mm_prefix *mm =
		container_of(rbn, struct mm_prefix, addr_node);
	printf("#%*p[%zu]\n", 80 - (level * 20), mm, mm->count);
	node_count++;
	return 0;
}

void print_mm_stats(struct mm_stat *s) {
#define MMBUFSZ 512
	char buf[MMBUFSZ];
	mm_format_stats(s, buf, MMBUFSZ);
	printf("%s", buf);
}


int main(int argc, char *argv[])
{
	void *b[6];
	int i;
	struct mm_stat s;
	/*
	 * After init, there is a single free block in the heap.
	 */
	if (mm_init(1024 * 1024 * 16, 64)) {
		printf("Error initializing memory region!\n");
		perror("mm_create: ");
		exit(1);
	}
	/* +-----~~---------------~~------------+
	 * |               Heap                 |
	 * +---------~~------~~--------~~-------+
	 */
	node_count = 0;
	rbt_traverse(&mmr->addr_tree, heap_print, NULL);
	TEST_ASSERT((node_count == 1),
		    "There is only a single node in the heap after mm_init.\n");
	mm_stats(&s);
	print_mm_stats(&s);

	/*
	 * Allocate six blocks without intervening frees.
	 */
	for (i = 0; i < 6; i++)
		b[i] = mm_alloc(2048);
	TEST_ASSERT(((b[0] < b[1])
		     && (b[1] < b[2])
		     && (b[2] < b[3])
		     && (b[3] < b[4])
		     && (b[4] < b[5])),
		    "The six allocations are in address-wise "
		    "increasing order.\n");
	mm_stats(&s);
	print_mm_stats(&s);

	/*
	 * +---++---++---++---++---++---++--~~--+
	 * | 1 || 2 || 3 || 4 || 5 || 6 || Heap |
	 * +---++---+|---++---++---++---++--~~~-+
	 */
	node_count = 0;
	rbt_traverse(&mmr->addr_tree, heap_print, NULL);
	TEST_ASSERT((node_count == 1),
		    "There is only a single node in the heap "
		    "after six allocations.\n");
	mm_stats(&s);
	print_mm_stats(&s);
	/*
	 * free 1,3 and 5. Because there are allocated blocks between
	 * each of these newly freed blocks, they can't be coelesced.
	 */
	mm_free(b[0]);
	mm_free(b[2]);
	mm_free(b[4]);
	/*
	 * +---++---++---++---++---++---++--~~--+
	 * |1H || 2 ||3H || 4 ||5H || 6 || Heap |
	 * +---++---+|---++---++---++---++--~~~-+
	 */
	node_count = 0;
	rbt_traverse(&mmr->addr_tree, heap_print, NULL);
	TEST_ASSERT((node_count == 4),
		    "There are four nodes in the heap "
		    "after three discontiguous frees.\n");
	mm_stats(&s);
	print_mm_stats(&s);
	/*
	 * Then free 2. 1F, 2 and 3F should get coelesced.
	 */
	mm_free(b[1]);
	/*
	 * +-------------++---++---++---++--~~--+
	 * |  1,2,3H     || 4 ||5H || 6 || Heap |
	 * +-------------++---++---++---++--~~--+
	 */
	node_count = 0;
	rbt_traverse(&mmr->addr_tree, heap_print, NULL);
	TEST_ASSERT((node_count == 3),
		    "There are three nodes in the heap "
		    "after a contiguous free coelesces a block.\n");
	mm_stats(&s);
	print_mm_stats(&s);
	/*
	 * Then free 6, and 5H, 6 and the remainder of the
	 * heap should get coelesced.
	 */
	mm_free(b[5]);
	/*
	 * +-------------++---++---~~---~~--~~--+
	 * |  1,2,3H     || 4 ||   Heap         |
	 * +-------------++---++---~~---~~--~~--+
	 */
	node_count = 0;
	rbt_traverse(&mmr->addr_tree, heap_print, NULL);
	TEST_ASSERT((node_count == 2),
		    "There are two nodes in the heap "
		    "after a contiguous free coelesces another block.\n");
	mm_stats(&s);
	print_mm_stats(&s);
#ifdef MM_DEBUG
	int ferr = mm_final();
	TEST_ASSERT((ferr != 0), "Final does not work on active data\n");
#endif
	/*
	 * Then free 4, and the heap should have a single
	 * free block in it.
	 */
	mm_free(b[3]);
	/*
	 * +-----~~---------------~~------------+
	 * |               Heap                 |
	 * +---------~~------~~--------~~-------+
	 */
	node_count = 0;
	rbt_traverse(&mmr->addr_tree, heap_print, NULL);
	TEST_ASSERT((node_count == 1),
		    "There is one node in the heap "
		    "after a contiguous free coelesces the "
		    "remaining block.\n");
	mm_stats(&s);
	print_mm_stats(&s);
#ifdef MM_DEBUG
	ferr = mm_final();
	TEST_ASSERT((ferr == 0), "mm_final works on empty allocation list.\n");
	/* test nommap verison */
	mm_enable_debug();
	TEST_ASSERT((mm_mmap == 0), "No mmap enabled.\n");
	/*
	 * After init, there is a single free block in the heap.
	 */
	if (mm_init(1024 * 1024 * 16, 64)) {
		printf("Error initializing memory region!\n");
		perror("mm_create: ");
		exit(1);
	}
	/* +-----~~---------------~~------------+
	 * |               Heap                 |
	 * +---------~~------~~--------~~-------+
	 */
	node_count = 0;
	mm_stats(&s);
	print_mm_stats(&s);

	/*
	 * Allocate six blocks without intervening frees.
	 */
	for (i = 0; i < 6; i++) {
		b[i] = mm_alloc(2048);
		printf("alloc %d at %p\n", i, b[i]);
	}
	mm_stats(&s);
	print_mm_stats(&s);
	ferr = mm_final();
	TEST_ASSERT((ferr != 0), "Final debug does not work on active data\n");
	for (i = 0; i < 6; i++)
		mm_free(b[i]);
	mm_stats(&s);
	print_mm_stats(&s);
	ferr = mm_final();
	TEST_ASSERT((ferr == 0), "mm_final works on empty debug allocation list.\n");
#endif
	return 0;
}
#endif
