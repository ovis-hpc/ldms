/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2021 National Technology & Engineering Solutionsb
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2021 Open Grid Computing, Inc. All rights reserved.
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
#include "ldms_heap.h"
#include "rrbt.h"

#define LDMS_HEAP_SIGNATURE "BOHEAP0"

RRBN_DEF(addr, sizeof(uint64_t));
RRBN_DEF(size, sizeof(uint64_t));
struct mm_free {
	struct rrbn_addr addr_node;
	struct rrbn_size size_node;
};

struct mm_alloc {
	uint32_t count;
};

static int compare_size(void *node_key, const void *val_key)
{
	/* The key value is the number of grains. */
	uint64_t a = *(uint64_t *)node_key;
	uint64_t b = *(uint64_t *)val_key;
	if (a > b)
		return 1;
	if (a < b)
		return -1;
	return 0;
}

static int compare_addr(void *node_key, const void *val_key)
{
	/* The key value is the offset from the heap base. */
	uint64_t a = *(uint64_t *)node_key;
	uint64_t b = *(uint64_t *)val_key;
	if (a > b)
		return 1;
	if (a < b)
		return -1;
	return 0;
}

/* NB: only works for power of two r */
#define MMR_ROUNDUP(s,r)	((s + (r - 1)) & ~(r - 1))

static int heap_stat(struct rrbn *rbn, void *fn_data, int level)
{
	ldms_heap_info_t info = fn_data;
	struct mm_free *mm =
		container_of(rbn, struct mm_free, addr_node);
	info->free_chunks++;
	info->free_bytes += mm->size_node.key_u64[0];
	if (mm->size_node.key_u64[0] < info->smallest)
		info->smallest = mm->size_node.key_u64[0];
	if (mm->size_node.key_u64[0] > info->largest)
		info->largest = mm->size_node.key_u64[0];
	return 0;
}

uint64_t ldms_heap_off(ldms_heap_t h, void *p)
{
	uint64_t off;
	if (!p)
		return 0;
	off = (uint64_t)p - (uint64_t)h->base;
	assert(off < h->data->size);
	return off;
}

void *ldms_heap_ptr(ldms_heap_t h, uint64_t off)
{
	if (!off)
		return NULL;
	assert(off < h->data->size);
	return (void *)((uint64_t)h->base + off);
}

void ldms_heap_get_info(ldms_heap_t heap, ldms_heap_info_t info)
{
	memset(info, 0, sizeof(*info));

	pthread_mutex_lock(&heap->lock);

	info->grain = heap->data->grain;
	info->grain_bits = heap->data->grain_bits;
	info->size = heap->data->size;
	info->start = heap->data;

	info->smallest = heap->data->size + 1;
	rrbt_traverse(heap->addr_tree, heap_stat, info);

	pthread_mutex_unlock(&heap->lock);
}

__attribute__((unused))
static void get_pow2(size_t n, size_t *pow2, size_t *bits)
{
	size_t _pow2;
	size_t _bits;
	for (_pow2 = 1, _bits=0; _pow2 < n; _pow2 <<= 1, _bits++);
	*pow2 = _pow2;
	*bits = _bits;
}

size_t ldms_heap_grain_size(size_t grain_sz)
{
	return ((grain_sz > sizeof(struct mm_free)) ? grain_sz : sizeof(struct mm_free));
}

void ldms_heap_init(struct ldms_heap *heap, void *base, size_t size, size_t grain)
{
	uint64_t count;
	struct ldms_heap_base *hbase = base;

	if (size == 0) {
		heap->size = 0;
		return;
	}
	size = MMR_ROUNDUP(size, LDMS_HEAP_MIN_SIZE);

	for (heap->grain = 1, heap->grain_bits = 0;
	     heap->grain < ldms_heap_grain_size(grain);
	     heap->grain <<= 1, heap->grain_bits++);

	heap->size = size;
	heap->gn = 0;

	/* Inialize the size and address r-b trees */
	rrbt_init(&heap->size_tree);
	rrbt_init(&heap->addr_tree);

	/* Sign the heap */
	strcpy(hbase->signature, LDMS_HEAP_SIGNATURE);

	/* Initialize the prefix */
	struct mm_free *pfx = (typeof(pfx))hbase->start;
	assert(((uint64_t)pfx & 7) == 0);

	/* Insert the chunk into the r-b trees */
	struct rrbt_instance inst;
	rrbt_t tree = rrbt_get(&inst, &heap->size_tree.root, base, compare_size);
	count = size / heap->grain;
	rrbn_init(RRBN(pfx->size_node), &count, sizeof(count));
	rrbt_ins(tree, RRBN(pfx->size_node));

	tree = rrbt_get(&inst, &heap->addr_tree.root, base, compare_addr);
	count = (uint64_t)pfx - (uint64_t)base;
	rrbn_init(RRBN(pfx->addr_node), &count, sizeof(uint64_t));
	rrbt_ins(tree, RRBN(pfx->addr_node));
}

ldms_heap_t ldms_heap_get(ldms_heap_t h, struct ldms_heap *heap, void *base)
{
	h->data = heap;
	h->base = base;
	h->size_tree = rrbt_get(&h->size_inst, &heap->size_tree.root, base, compare_size);
	h->addr_tree = rrbt_get(&h->addr_inst, &heap->addr_tree.root, base, compare_addr);
	pthread_mutex_init(&h->lock, NULL);
	if (strncmp(h->base->signature, LDMS_HEAP_SIGNATURE, sizeof(h->base->signature))) {
		assert(0 == "heap signature mismatch");
		return NULL;
	}
	return h;
}

size_t ldms_heap_alloc_size(size_t grain_sz, size_t data_sz)
{
	/* Contains the size prefix needed to free the memory */
	data_sz += sizeof(struct mm_alloc);
	/* The memory chunk must hold mm_free */
	if (data_sz < sizeof(struct mm_free))
		data_sz = sizeof(struct mm_free);
	return MMR_ROUNDUP(data_sz, ldms_heap_grain_size(grain_sz));
}

void *ldms_heap_alloc(ldms_heap_t heap, size_t size)
{
	struct mm_free *p, *n;
	struct mm_alloc *a;
	struct rrbn *rbn;
	uint64_t count;
	uint64_t remainder;


	size = ldms_heap_alloc_size(heap->data->grain, size);
	count = size >> heap->data->grain_bits;

#if LDMS_HEAP_DEBUG
	printf("------- %p: heap_alloc(%ld) -- start\n", heap, count);
	printf("                         ---size tree ----\n");
	rrbt_verify(heap->size_tree);
	rrbt_print(heap->size_tree);
	printf("                         ---addr tree ----\n");
	rrbt_verify(heap->addr_tree);
	rrbt_print(heap->addr_tree);
#endif /* LDMS_HEAP_DEBUG */

	pthread_mutex_lock(&heap->lock);
	rbn = rrbt_find_lub(heap->size_tree, &count);
	if (!rbn) {
		pthread_mutex_unlock(&heap->lock);
		return NULL;
	}

	p = container_of(rbn, struct mm_free, size_node);

	/* Remove the node from the size and address trees */
	rrbt_del(heap->size_tree, RRBN(p->size_node));
	rrbt_del(heap->addr_tree, RRBN(p->addr_node));

	/* Create a new node from the remainder of p if any */
	remainder = p->size_node.key_u64[0] - count;
	if (remainder) {
		n = (struct mm_free *)
			((unsigned char *)p + (count << heap->data->grain_bits));
		uint64_t offset = ldms_heap_off(heap, n);
		rrbn_init(RRBN(n->size_node), &remainder, sizeof(remainder));
		rrbn_init(RRBN(n->addr_node), &offset, sizeof(offset));

		rrbt_ins(heap->size_tree, RRBN(n->size_node));
		rrbt_ins(heap->addr_tree, RRBN(n->addr_node));
	}
	a = (struct mm_alloc *)p;
	a->count = count;
	a += 1;
#if LDMS_HEAP_DEBUG
	printf("---%lx (size:%lx[%p], addr:%lx[%p], end:%lx) ---- heap_alloc(%ld) -- end\n",
			ldms_heap_off(heap, a),
			rrbt_off(heap->size_tree, rbn),
			rbn,
			rrbt_off(heap->addr_tree, RRBN(p->addr_node)),
			&p->addr_node,
			rrbt_off(heap->addr_tree, RRBN(p->addr_node)) + size,  count);
	printf("                         ---size tree ----\n");
	rrbt_verify(heap->size_tree);
	rrbt_print(heap->size_tree);
	printf("                         ---addr tree ----\n");
	rrbt_verify(heap->addr_tree);
	rrbt_print(heap->addr_tree);
	printf("------------------------------------------\n");
#endif /* LDMS_HEAP_DEBUG */
	heap->data->gn++;
	pthread_mutex_unlock(&heap->lock);
	return a;
}

void ldms_heap_free(ldms_heap_t heap, void *d)
{
	uint64_t count;
	struct mm_alloc *a = d;
	struct mm_free *p;
	struct mm_free *q;
	struct rrbn *rbn;
	uint64_t offset;
	uint64_t end;

	a--;
	p = (void *)a;
	pthread_mutex_lock(&heap->lock);
	offset = ldms_heap_off(heap, p);
	count = a->count;
#if LDMS_HEAP_DEBUG
	printf("------- %p: heap_free(%lx, %ld) -- start\n",
			heap,
			ldms_heap_off(heap, d),
			count);
	rbn = rrbt_find_glb(heap->addr_tree, &offset);
	if (rbn) {
		q = container_of(rbn, struct mm_free, addr_node);
		uint64_t end = q->addr_node.key_u64[0] + (q->size_node.key_u64[0] << heap->data->grain_bits);
		if (offset >= q->addr_node.key_u64[0] && offset < end) {
			printf("Double free detected. The memory in question is already in the heap.\n");
			assert(0);
		}
	}
	printf("                         ---size tree ----\n");
	rrbt_verify(heap->size_tree);
	rrbt_print(heap->size_tree);
	printf("                         ---addr tree ----\n");
	rrbt_verify(heap->addr_tree);
	rrbt_print(heap->addr_tree);
#endif /* LDMS_HEAP_DEBUG */
	rrbn_init(RRBN(p->size_node), &count, sizeof(count));
	rrbn_init(RRBN(p->addr_node), &offset, sizeof(offset));

	/* See if we can coalesce with our lesser sibling */
	rbn = rrbt_find_glb(heap->addr_tree, &offset);
	if (rbn) {
		q = container_of(rbn, struct mm_free, addr_node);

		/* See if q is contiguous with p */
		end = q->addr_node.key_u64[0] + (q->size_node.key_u64[0] << heap->data->grain_bits);
		if (end == p->addr_node.key_u64[0]) {
			/* Remove the left sibling from the tree and coelesce */
			rrbt_del(heap->size_tree, RRBN(q->size_node));
			rrbt_del(heap->addr_tree, RRBN(q->addr_node));
			q->size_node.key_u64[0] += p->size_node.key_u64[0];
			p = q;
		}
	}
	/* See if we can coalesce with our greater sibling */
	offset = ldms_heap_off(heap, p);
	rbn = rrbt_find_lub(heap->addr_tree, &offset);
	if (rbn) {
		q = container_of(rbn, struct mm_free, addr_node);

		end = p->addr_node.key_u64[0] + (p->size_node.key_u64[0] << heap->data->grain_bits);
		if (end == q->addr_node.key_u64[0]) {
			/* Remove the right sibling from the tree and coelesce */
			rrbt_del(heap->size_tree, RRBN(q->size_node));
			rrbt_del(heap->addr_tree, RRBN(q->addr_node));
			p->size_node.key_u64[0] += q->size_node.key_u64[0];
		}
	}
	offset = ldms_heap_off(heap, p);
	rrbn_init(RRBN(p->size_node), p->size_node.key_u64, sizeof(uint64_t));
	rrbn_init(RRBN(p->addr_node), p->addr_node.key_u64, sizeof(uint64_t));

	/* Put 'p' back in the trees */
	rrbt_ins(heap->size_tree, RRBN(p->size_node));
	rrbt_ins(heap->addr_tree, RRBN(p->addr_node));

	/* Modify generation nubmer */
	heap->data->gn++;

#if LDMS_HEAP_DEBUG
	printf("------- heap_free(%lx, %ld) -- end\n", ldms_heap_off(heap, d), count);
	printf("                         ---size tree ----\n");
	rrbt_verify(heap->size_tree);
	rrbt_print(heap->size_tree);
	printf("                         ---addr tree ----\n");
	rrbt_verify(heap->addr_tree);
	rrbt_print(heap->addr_tree);
	printf("------------------------------------------\n");
#endif /* LDMS_HEAP_DEBUG */
	pthread_mutex_unlock(&heap->lock);
}

size_t ldms_heap_size(size_t size)
{
	return MMR_ROUNDUP(size, LDMS_HEAP_MIN_SIZE);
}


#ifdef LDMS_HEAP_TEST
int node_count;
int heap_print(struct rrbn *rbn, void *fn_data, int level)
{
	struct mm_free *mm =
		container_of(rbn, struct mm_free, addr_node);
	printf("#%*p[%zu]\n", 80 - (level * 20), mm, mm->size_node.key_u64[0]);
	node_count++;
	return 0;
}

void print_mm_stats(struct mm_stat *s) {
	if (!s) {
		printf("mm_stat: null\n");
		return;
	}
	printf("mm_stat: size=%zu grain=%zu chunks_free=%zu grains_free=%zu grains_largest=%zu grains_smallest=%zu bytes_free=%zu bytes_largest=%zu bytes_smallest=%zu\n",
	s->size, s->grain, s->chunks, s->bytes, s->largest, s->smallest,
	s->grain*s->bytes, s->grain*s->largest, s->grain*s->smallest);
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
	rrbt_traverse(&mmr->addr_tree, heap_print, NULL);
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
	rrbt_traverse(&mmr->addr_tree, heap_print, NULL);
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
	rrbt_traverse(&mmr->addr_tree, heap_print, NULL);
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
	rrbt_traverse(&mmr->addr_tree, heap_print, NULL);
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
	rrbt_traverse(&mmr->addr_tree, heap_print, NULL);
	TEST_ASSERT((node_count == 2),
		    "There are two nodes in the heap "
		    "after a contiguous free coelesces another block.\n");
	mm_stats(&s);
	print_mm_stats(&s);
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
	rrbt_traverse(&mmr->addr_tree, heap_print, NULL);
	TEST_ASSERT((node_count == 1),
		    "There is one node in the heap "
		    "after a contiguous free coelesces the "
		    "remaining block.\n");
	mm_stats(&s);
	print_mm_stats(&s);
	return 0;
}
#endif
