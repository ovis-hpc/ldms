/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
#include <assert.h>
#include "mmalloc.h"
#include "../coll/rbt.h"
#include "ovis-test/test.h"

struct mm_prefix {
	struct rbn addr_node;
	struct rbn size_node;
	size_t count;
	uint64_t addr;
};

typedef struct mm_region {
	size_t grain;		/* minimum allocation size and alignment */
	size_t grain_bits;
	size_t size;
	size_t in_use;
	void *start;
	struct rbt size_tree;
	struct rbt addr_tree;
} *mm_region_t;

static int compare_count(void *node_key, void *val_key)
{
	return (int)(*(uint64_t *)node_key) - (*(uint64_t *)val_key);
}

static int compare_addr(void *node_key, void *val_key)
{
	return (int)(*(char **)node_key - *(char **)val_key);
}

/* NB: only works for power of two r */
#define MMR_ROUNDUP(s,r)	((s + (r - 1)) & ~(r - 1))

pthread_mutex_t mmr_lock = PTHREAD_MUTEX_INITIALIZER;
static mm_region_t mmr;

void mm_get_info(struct mm_info *mmi)
{
	mmi->grain = mmr->grain;
	mmi->grain_bits = mmr->grain_bits;
	mmi->size = mmr->size;
	mmi->start = mmr->start;
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
	mmr = calloc(1, sizeof (*mmr));
	if (!mmr)
		return ENOMEM;

	size = MMR_ROUNDUP(size, 4096);
	mmr->start = mmap(NULL, size, PROT_READ | PROT_WRITE,
			  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (MAP_FAILED == mmr->start)
		goto out;

	get_pow2(grain, &mmr->grain, &mmr->grain_bits);
	mmr->size = size;

	/* Inialize the size and address r-b trees */
	rbt_init(&mmr->size_tree, compare_count);
	rbt_init(&mmr->addr_tree, compare_addr);

	/* Initialize the prefix */
	struct mm_prefix *pfx = mmr->start;
	pfx->count = size / mmr->grain;
	pfx->addr = (unsigned long)pfx;
	rbn_init(&pfx->size_node, &pfx->count);
	rbn_init(&pfx->addr_node, &pfx->addr);

	/* Insert the chunk into the r-b trees */
	rbt_ins(&mmr->size_tree, &pfx->size_node);
	rbt_ins(&mmr->addr_tree, &pfx->addr_node);
	return 0;
 out:
	free(mmr);
	return errno;
}

#define MM_DEBUG 1
#ifdef MM_DEBUG
static uint64_t max_addr = 0;
#endif

void *mm_alloc(size_t size)
{
	struct mm_prefix *p, *n;
	struct rbn *rbn;
	uint64_t count;
	uint64_t remainder;

	size += sizeof(*p);
	size = MMR_ROUNDUP(size, mmr->grain);
	mmr->in_use += size;
	count = size >> mmr->grain_bits;

	pthread_mutex_lock(&mmr_lock);
	rbn = rbt_find_least_gt_or_eq(&mmr->size_tree, &count);
	if (!rbn)
		goto err;

	p = container_of(rbn, struct mm_prefix, size_node);
#ifdef MM_DEBUG
	if (p > max_addr)
		max_addr = p;
#endif

	/* Remove the node from the size and address trees */
	rbt_del(&mmr->size_tree, &p->size_node);
	rbt_del(&mmr->addr_tree, &p->addr_node);

	/* Create a new node from the remainder of p if any */
	remainder = p->count - count;
	if (remainder) {
		n = (struct mm_prefix *)
			((unsigned char *)p + (count << mmr->grain_bits));
		n->count = remainder;
		n->addr = (unsigned long)n;
		rbn_init(&n->size_node, &n->count);
		rbn_init(&n->addr_node, &n->addr);

		rbt_ins(&mmr->size_tree, &n->size_node);
		rbt_ins(&mmr->addr_tree, &n->addr_node);
	}
	p->count = count;
	p->addr = (unsigned long)p;
	pthread_mutex_unlock(&mmr_lock);
	return ++p;
 err:
	pthread_mutex_unlock(&mmr_lock);
	return NULL;
}

void mm_free(void *d)
{
	struct mm_prefix *p = d;
	struct mm_prefix *q, *r;
	struct rbn *rbn;
	p --;

	pthread_mutex_lock(&mmr_lock);

#ifdef MM_DEBUG
	/* Assert that the address is not already in the tree */
	rbn  = rbt_find(&mmr->addr_tree, &p->addr);
	assert(NULL == rbn);
#endif
	mmr->in_use -= p->count << mmr->grain_bits;
	/* See if we can coalesce with our lesser sibling */
	rbn = rbt_find_greatest_lt_or_eq(&mmr->addr_tree, &p->addr);
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
	rbn = rbt_find_least_gt_or_eq(&mmr->addr_tree, &p->addr);
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
	p->addr = (unsigned long)p;
	rbn_init(&p->size_node, &p->count);
	rbn_init(&p->addr_node, &p->addr);

	/* Put 'p' back in the trees */
	rbt_ins(&mmr->size_tree, &p->size_node);
	rbt_ins(&mmr->addr_tree, &p->addr_node);
	pthread_mutex_unlock(&mmr_lock);
}

static int node_count;
static void heap_print(struct rbn *rbn, void *fn_data, int level)
{
	struct mm_prefix *mm =
		container_of(rbn, struct mm_prefix, addr_node);
	printf("#%*p[%d]\n", 80 - (level * 20), mm, mm->count);
	node_count++;
}
void mm_dump()
{
	printf("in_use %ld\n", mmr->in_use);
	printf("max_addr %p\n", (void *)(unsigned long)max_addr);
	rbt_traverse(&mmr->addr_tree, heap_print, NULL);
}
#ifdef MMR_TEST
int main(int argc, char *argv[])
{
	void *b[6];
	int i;
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
}
#endif
