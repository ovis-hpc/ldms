/*
 * Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
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

#include "ovis-ldms-config.h"
#include "heap.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * NOTE: min heap
 */
struct ovis_heap {
	ovis_heap_comp comp;
	int sz;
	int alloc_sz;
	ovis_heap_node_t heap[OVIS_FLEX];
};

ovis_heap_t ovis_heap_create(int heapsize, ovis_heap_comp comp)
{
	ovis_heap_t heap = malloc(sizeof(*heap) + sizeof(void*)*heapsize);
	if (!heap)
		return NULL;
	heap->alloc_sz = heapsize;
	heap->sz = 0;
	heap->comp = comp;
	return heap;
}

void ovis_heap_free(ovis_heap_t heap)
{
	free(heap);
}

static
void ovis_heap_float_up(ovis_heap_t heap, ovis_heap_node_t node);
static
void ovis_heap_sink_down(ovis_heap_t heap, ovis_heap_node_t node);

int ovis_heap_insert(ovis_heap_t heap, ovis_heap_node_t node)
{
	if (heap->sz == heap->alloc_sz)
		return ENOMEM;
	heap->heap[heap->sz] = node;
	heap->heap[heap->sz]->idx = heap->sz;
	heap->sz++;
	ovis_heap_float_up(heap, node);
	return 0;
}

ovis_heap_node_t ovis_heap_pop(ovis_heap_t heap)
{
	ovis_heap_node_t node = NULL;
	if (heap->sz == 0)
		return NULL;
	node = heap->heap[0];
	heap->heap[0] = heap->heap[--heap->sz];
	heap->heap[0]->idx = 0;
	ovis_heap_sink_down(heap, heap->heap[0]);
	node->idx = -1;
	return node;
}

static
void ovis_heap_float_up(ovis_heap_t heap, ovis_heap_node_t node)
{
	int c, p;
	int rc;
	c = node->idx;
	while (c > 0) {
		p = c / 2;
		rc = heap->comp(node, heap->heap[p]);
		if (rc >= 0)
			break;
		heap->heap[c] = heap->heap[p];
		heap->heap[c]->idx = c;
		c = p;
	}
	heap->heap[c] = node;
	heap->heap[c]->idx = c;
}

static
void ovis_heap_sink_down(ovis_heap_t heap, ovis_heap_node_t node)
{
	int c, l, r, x, rc;
	c = node->idx;
	l = c*2+1;
	r = l+1;
	while (l < heap->sz) {
		if (r >= heap->sz) {
			x = l;
			goto cmp;
		}

		rc = heap->comp(heap->heap[l], heap->heap[r]);
		if (rc < 0) {
			x = l;
		} else {
			x = r;
		}

	cmp:
		rc = heap->comp(heap->heap[x], node);
		if (rc < 0) {
			heap->heap[c] = heap->heap[x];
			heap->heap[c]->idx = c;
			c = x;
		} else {
			break;
		}
		l = c*2+1;
		r = l+1;
	}
	heap->heap[c] = node;
	heap->heap[c]->idx = c;
}

void ovis_heap_remove(ovis_heap_t heap, ovis_heap_node_t node)
{
	/* move last node to replace the removed node */
	heap->heap[node->idx] = heap->heap[--heap->sz];
	heap->heap[node->idx]->idx = node->idx;

	/* then update heap */
	ovis_heap_update(heap, heap->heap[node->idx]);

	/* also reset node index */
	node->idx = -1;
}

void ovis_heap_update(ovis_heap_t heap, ovis_heap_node_t node)
{
	int old_idx = node->idx;
	ovis_heap_float_up(heap, node);
	if (node->idx == old_idx) /* if index does not change, it may need sink down */
		ovis_heap_sink_down(heap, node);
}

ovis_heap_node_t ovis_heap_top(ovis_heap_t heap)
{
	if (heap->sz)
		return heap->heap[0];
	return NULL;
}

int ovis_heap_size(ovis_heap_t heap)
{
	return heap->sz;
}
