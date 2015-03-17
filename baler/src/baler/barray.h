/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-15 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-15 Sandia Corporation. All rights reserved.
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
 * \file barray.h
 * \author Narate Taerat (narate@ogc.us)
 * \defgroup barray Baler In-memory Dynamic Array
 * \{
 */
#ifndef __BARRAY_H
#define __BARRAY_H
#include "btypes.h"

/**
 * The number of bits for a chunk.
 */
#define BARRAY_CHUNK_BITS 12

/**
 * Number of element per chunk.
 */
#define BARRAY_CHUNK_SIZE (0x1<<BARRAY_CHUNK_BITS)

/**
 * Bitmask for a a chunk.
 */
#define BARRAY_CHUNK_MASK (BARRAY_CHUNK_SIZE - 1)

/**
 * Default ptr_len in ::barray.
 */
#define BARRAY_DEFAULT_PTR_LEN 65536

/**
 * In-memory dynamic array.
 */
struct barray {
	uint32_t len; /**< Current length of the array. */
	uint32_t ptr_len; /**< The length of ptr. */
	uint32_t elm_size; /**< Size of an element. */
	void** ptr; /**< Array of pointers to allocated chunks. */
};

/**
 * Allocation function for ::barray.
 * \param elm_size The size of an element.
 * \param hint_len Hint length (number of elements). If it is 0,
 * 	only one chunk will be pre-allocated.
 */
static
struct barray* barray_alloc(uint32_t elm_size, uint32_t hint_len)
{
	struct barray *a = (typeof(a)) malloc(sizeof(*a));
	uint32_t ptr_len = hint_len >> BARRAY_CHUNK_BITS;
	if (ptr_len < BARRAY_DEFAULT_PTR_LEN)
		ptr_len = BARRAY_DEFAULT_PTR_LEN;
	if(!a)
		goto err0;
	a->len = 0;
	a->ptr_len = ptr_len;
	a->elm_size = elm_size;
	a->ptr = (typeof(a->ptr)) calloc(ptr_len, sizeof(*a->ptr));
	if (!a->ptr)
		goto err1;
	/* a->ptr[i] is (void*) */
	a->ptr[0] = calloc(BARRAY_CHUNK_SIZE, elm_size);
	if (!a->ptr[0])
		goto err2;
	return a;
err2:
	free(a->ptr);
err1:
	free(a);
err0:
	return NULL;
}

/**
 * Free an array \a a.
 * \param a The pointer to ::barray to be freed.
 */
static
void barray_free(struct barray *a)
{
	int i;
	for (i=0; i<a->ptr_len; i++) {
		if (a->ptr[i])
			free(a->ptr[i]);
	}
	free(a->ptr);
	free(a);
}

/**
 * Get element from \a a and put into \a var.
 * \param a The pointer to ::barray.
 * \param idx The index.
 * \param[out] var The pointer to the output variable, this can be null.
 * \return NULL if such element does not exist.
 * \return A pointer to the element if it is in allocated memory (does not
 * 	mean that the element has been set before).
 */
static
const void* barray_get(struct barray *a, uint32_t idx, void *var)
{
	uint32_t chunk = idx >> BARRAY_CHUNK_BITS;
	if (chunk >= a->ptr_len || !a->ptr[chunk])
		return NULL;
	void *ret = a->ptr[chunk] + (idx & BARRAY_CHUNK_MASK)*a->elm_size;
	if (var)
		memcpy(var, ret, a->elm_size);
	return ret;
}

/**
 * \a a[ \a idx ] := \a *data
 * \param a The array.
 * \param idx The index.
 * \param data The pointer to the data.
 * \return 0 on success.
 * \return -1 on failure, with errno set.
 */
static
int barray_set(struct barray *a, uint32_t idx, void *data)
{
	uint32_t chunk = idx >> BARRAY_CHUNK_BITS;
	if (chunk >= a->ptr_len) {
		/* expand ptr as necessary */
		uint32_t new_len = (chunk|(BARRAY_DEFAULT_PTR_LEN-1))+1;
		void **new_ptr = (void**) realloc(a->ptr, new_len * sizeof(void*));
		if (!new_ptr)
			return -1; /* errno = ENOMEM should be set already */
		/* Initialize the new part of the memory. */
		bzero(new_ptr+a->ptr_len, (new_len-a->ptr_len)*sizeof(void*));

		a->ptr = new_ptr;
		a->ptr_len = new_len;
	}
	if (!a->ptr[chunk]) {
		a->ptr[chunk] = calloc(BARRAY_CHUNK_SIZE, a->elm_size);
		if (!a->ptr[chunk])
			return -1;
	}
	memcpy(a->ptr[chunk] + (idx & BARRAY_CHUNK_MASK) * a->elm_size,
		data, a->elm_size);
	if (a->len <= idx) {
		a->len = idx + 1;
	}
	return 0;
}

static inline
size_t barray_get_len(struct barray *a)
{
	return a->len;
}

#endif
/**\}*/
