/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
 *
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
/**
 * \file bheap.h
 * \author Narate Taerat (narate at ogc dot us)
 *
 * \brief Basic min-heap container implementation for generic usage.
 *
 * \note If max-heap is needed, just change the \c cmp function.
 *
 * \note bheap is not thread-safe.
 */
#ifndef __BHEAP_H
#define __BHEAP_H

#include "btypes.h"

struct bheap {
	int (*cmp)(void *a0, void *a1);
	int len;
	int alloc_len;
	void **array;
};

/**
 * Create ::bheap container with \c cmp compare function and \c alloc_len
 * length array.
 *
 * \retval NULL if error. \c errno is also set.
 * \retval ptr the ::bheap handle, if success.
 */
struct bheap *bheap_new(int alloc_len, int (*cmp)(void*,void*));

/**
 * Free the heap and associated resources.
 */
void bheap_free(struct bheap *h);

/**
 * Insert \c elm into the heap \c h.
 *
 * \retval 0 if success.
 * \retval errno if error.
 */
int bheap_insert(struct bheap *h, void *elm);

/**
 * Remove the top element from the heap \c h.
 *
 * \retval NULL if there is no element to remove.
 * \retval ptr the pointer to the top element.
 */
void* bheap_remove_top(struct bheap *h);

/**
 * \retval NULL if there is no element.
 * \retval ptr the pointer to the top element of the heap.
 */
void* bheap_get_top(struct bheap *h);

/**
 * Percolate the top-element to fix the heap.
 */
void bheap_percolate_top(struct bheap *h);

/**
 * Verify the heap \c h.
 *
 * \retval 0 if it is verified.
 * \retval 1 if the heap is not verified.
 */
int bheap_verify(struct bheap *h);

#endif
