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
/**
 * \file bmvec.h
 * \author Narate Taerat (narate@ogc.us)
 *
 */
#ifndef __BMVEC_H
#define __BMVEC_H
#include <malloc.h>
#include <string.h>
#include <pthread.h>
#include "bcommon.h"
#include "butils.h"
#include "btypes.h"
#include "bmem.h"

/**
 * \defgroup bmvec Baler Memory Map for Vector
 * \{
 * \ingroup bmem
 *
 * This module is about Baler Memory Map that contains only a vector bvec.
 * It contains definition for bmvec* and functions to work with it.
 */

/**
 * \brief Baler Memory-mapped Vector increment size.
 * This value is used in bvec appending.
 */
#define BMVEC_INC 128*1024

/**
 * A macro for defining a new bmvec type.
 * \param name The name of newly defined bmvec type.
 * \param bvec_type The type of bvec to be mapped.
 */
#define BMVEC_DEF(name, bvec_type) \
	struct name { \
		pthread_mutex_t mutex; \
		struct bmem *mem; \
		bvec_type *bvec; \
	};

/**
 * Definition for bmvec_char.
 */
BMVEC_DEF(bmvec_char, struct bvec_char);

/**
 * Definition for bmvec_u64.
 */
BMVEC_DEF(bmvec_u64, struct bvec_u64);

/**
 * \brief Append function for ::bmvec_u64.
 * \param vec The ::bmvec_u64
 * \param x An element (of type uint64_t) to append.
 */
static
int bmvec_u64_append(struct bmvec_u64 *vec, uint64_t x)
{
	pthread_mutex_lock(&vec->mutex);
	struct bvec_u64 *v = vec->bvec;
	if (v->len == v->alloc_len) {
		/* Used len == allocated len, allocate more */
		if (!bmem_alloc(vec->mem, sizeof(x)*BMVEC_INC)) {
			berror("Cannot append");
			return -1;
		}
		/* Also update bvec because bmem_alloc can remap vec->mem */
		v = vec->bvec = (typeof(v)) vec->mem->ptr;

		/* Then, update the alloc_len */
		v->alloc_len += BMVEC_INC;
	}
	v->data[v->len++] = x;
	pthread_mutex_unlock(&vec->mutex);
	return 0;
}

/**
 * Get value
 */
static
uint64_t bmvec_u64_get(struct bmvec_u64 *vec, uint32_t idx)
{
	return vec->bvec->data[idx];
}

/**
 * \brief Set function for ::bmvec_u64.
 * \param vec The ::bmvec_u64
 * \param idx The index to set value to.
 * \param x An element (of type uint64_t) to set at \a idx
 */
static
int bmvec_u64_set(struct bmvec_u64 *vec, uint32_t idx, uint64_t x)
{
	pthread_mutex_lock(&vec->mutex);
	struct bvec_u64 *v = vec->bvec;
	if (idx >= v->alloc_len) {
		uint64_t alen = ((idx - v->alloc_len) | (2*BMVEC_INC-1))+1;
		/* Used len == allocated len, allocate more */
		if (!bmem_alloc(vec->mem, sizeof(x)*alen)) {
			berror("bmem_alloc");
			return -1;
		}
		v = vec->bvec = (typeof(v)) vec->mem->ptr;
		v->alloc_len += alen;
	}
	v->data[idx] = x;
	if (idx >= v->len)
		v->len = idx+1;
	pthread_mutex_unlock(&vec->mutex);
	return 0;
}

/**
 * A function for setting value of generic bmvec at \a idx, treating data as a
 * group of char of size \a elm_size.
 * \param _vec The pointer to any bmvec.
 * \param elm_size The size of an element in the \a _vec
 * \param idx The index.
 * \param elm The pointer to an element to be inserted.
 */
static inline
int bmvec_generic_set(void *_vec, uint32_t idx,
		void* elm, uint32_t elm_size)
{
	struct bmvec_char *vec = (typeof(vec)) _vec;
	pthread_mutex_lock(&vec->mutex);
	struct bvec_char *v = vec->bvec;
	if (idx >= v->alloc_len) {
		uint64_t alen = ((idx - v->alloc_len) | (2*BMVEC_INC-1))+1;
		/* Used len == allocated len, allocate more */
		if (!bmem_alloc(vec->mem, elm_size*alen)) {
			berror("bmem_alloc");
			return -1;
		}
		v = vec->bvec = (typeof(v)) vec->mem->ptr;
		v->alloc_len += alen;
	}

	memcpy(v->data + elm_size*idx, elm, elm_size);
	if (idx >= v->len)
		v->len = idx+1;
	pthread_mutex_unlock(&vec->mutex);
	return 0;

}

static inline
void *bmvec_generic_get(void *_vec, uint32_t idx, uint32_t elm_size)
{
	void *ptr;
	struct bmvec_char *vec = (typeof(vec)) _vec;
	struct bvec_char *v = vec->bvec;
	if (idx >= v->len)
		return NULL;
	return v->data + elm_size*idx;
}

/**
 * Element pointer to index in the vector.
 */
static inline
uint32_t bmvec_generic_get_index(void *_vec, void *elm, uint32_t elm_size)
{
	return (elm - (void*)((struct bmvec_char*)_vec)->bvec->data)/elm_size;
}

/**
 * Similar to ::bmvec_generic_set, but this one does only appending.
 * \param vec The pointer to the generic bmvec structure (::bmvec_char).
 * \param elm The pointer to element to be inserted.
 * \param elm_size The size (in bytes) of an element.
 */
static
int bmvec_generic_append(struct bmvec_char *vec, void *elm, uint32_t elm_size)
{
	pthread_mutex_lock(&vec->mutex);
	struct bvec_char *v = vec->bvec;
	if (v->len == v->alloc_len) {
		/* Used len == allocated len, allocate more */
		if (!bmem_alloc(vec->mem, elm_size*BMVEC_INC)) {
			berror("Cannot append");
			return -1;
		}
		/* Also update bvec because bmem_alloc can remap vec->mem */
		v = vec->bvec = (typeof(v)) vec->mem->ptr;

		/* Then, update the alloc_len */
		v->alloc_len += BMVEC_INC;
	}
	memcpy(v->data + elm_size*v->len, elm, elm_size);
	v->len++;
	pthread_mutex_unlock(&vec->mutex);
	return 0;
}

/**
 * Open \a path and map into ::bmvec_u64 structure.
 * \note This function only open the file and set the pointers in
 * 	::bmvec_u64 accordingly. It does not initialize the ::bmvec_u64.
 * 	To initialize a ::bmvec_u64, please see ::bmvec_u64_init().
 * \param path The file path for the memory mapping.
 * \return A pointer to ::bmvec_u64, if success.
 * \return NULL, if not success.
 */
struct bmvec_u64* bmvec_u64_open(char *path);

/**
 * Open \a path and map into ::bmvec_char structure, which can later be
 * casted to any bmvec structure.
 * \return NULL on error.
 * \return A void pointer to ::bmvec_char structure (can be cast to any bmvec*).
 */
void* bmvec_generic_open(char *path);

/**
 * Initialize \a vec.
 * \param vec The ::bmvec_u64 to be initialized.
 * \param size The number of initial elements.
 * \param value The initial value. If \a size is 0, \a value is ignored.
 * \return 0 on success.
 * \return -1 on error.
 */
int bmvec_u64_init(struct bmvec_u64 *vec, uint32_t size, uint64_t value);

/**
 * Initialize \a vec with \a elm.
 * \param vec The bmvec to be initialized.
 * \param size The requested initial size of the vector.
 * \param elm The pointer to an element used for initialization.
 * \param elm_size The size of an element.
 * \return 0 on success.
 * \return -1 on error.
 */
int bmvec_generic_init(void *vec, uint32_t size, void *elm, uint32_t elm_size);

/**
 * Close the mapped file and free \a vec (along with its objects).
 * \param vec The ::bmvec_u64 to be closed and freed.
 */
void bmvec_u64_close_free(struct bmvec_u64 *vec);

/**
 * Close the mapped file and free \a _vec.
 * \param _vec The generic bmvec structure.
 */
void bmvec_generic_close_free(void *_vec);

/**
 * \brief Unlink the bmvec of given \c path from the file system.
 *
 * \retval 0 if success.
 * \retval errno if error.
 */
int bmvec_unlink(const char *path);

int bmvec_generic_refresh(struct bmvec_char *vec);

/**\}*/ // bmvec

#endif /* __BMVEC_H */
