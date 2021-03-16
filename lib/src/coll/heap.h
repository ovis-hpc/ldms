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
#ifndef __OVIS_HEAP_H
#define __OVIS_HEAP_H

#define OVIS_HEAP_NODE_INIT(n) ((n)->idx = -1)

typedef struct ovis_heap_node {
	int idx; /* for ovis_heap internal use */
} *ovis_heap_node_t;

/**
 * OVIS heap comparator signature.
 *
 * The function must return 0 if \c a and \c b are equal,
 * -1 if \c a is less than \c b, and 1 if \c a is greater than \c b.
 */
typedef int (*ovis_heap_comp)(ovis_heap_node_t a, ovis_heap_node_t b);

typedef struct ovis_heap *ovis_heap_t;

/**
 * Create OVIS heap.
 *
 * \param heapsize The maximum number of elements in the heap.
 * \param comp The object comparator function.
 *
 * \retval heap OVIS heap handle.
 * \retval NULL if the creation is not success. \c errno is also set.
 */
ovis_heap_t ovis_heap_create(int heapsize, ovis_heap_comp comp);

/**
 * Deallocate \c heap. Ignores NULL input.
 */
void ovis_heap_free(ovis_heap_t heap);

/**
 * Insert an object into the heap.
 *
 * \param heap The OVIS heap handle.
 * \param obj The object to insert into the heap.
 *
 * \retval 0 if OK.
 * \retval ENOMEM if the heap is full.
 *
 * \note \c heap now owns \c node.
 */
int ovis_heap_insert(ovis_heap_t heap, ovis_heap_node_t node);

/**
 * Pop the top element out of the heap.
 *
 * \param heap the heap handle.
 *
 * \retval node pointer to the heap node.
 * \retval NULL if there is no more element.
 *
 * \note The caller owns the returned \c node.
 */
ovis_heap_node_t ovis_heap_pop(ovis_heap_t heap);

/**
 * Peek at the top element of the heap. No object is removed.
 *
 * \param heap the heap handle.
 *
 * \retval obj the top-of-the-heap object.
 * \retval NULL if the heap is empty.
 *
 * \note The caller <b>does not</b> own the returned \c node.
 */
ovis_heap_node_t ovis_heap_top(ovis_heap_t heap);

/**
 * Remove \c node from \c heap.
 *
 * \param heap the heap handle.
 * \param node the node handle. The giving node must be in the heap.
 *
 * \note after removed, \c heap does not own \c node anymore. The application is
 * responsible for freeing \c node.
 */
void ovis_heap_remove(ovis_heap_t heap, ovis_heap_node_t node);

/**
 * Node update.
 */
void ovis_heap_update(ovis_heap_t heap, ovis_heap_node_t node);

/**
 * Get heap size (number of entries).
 * \retval n the number of heap entries.
 */
int ovis_heap_size(ovis_heap_t heap);

#endif
