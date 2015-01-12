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

#include "bmvec.h"

struct bmvec_u64* bmvec_u64_open(char *path)
{
	struct bmvec_u64 *vec = (typeof(vec)) malloc(sizeof(*vec));
	if (!vec) {
		berror("malloc");
		goto err0;
	}
	pthread_mutex_init(&vec->mutex, NULL);
	vec->mem = bmem_open(path);
	if (!vec->mem) {
		berror("bmem_open");
		goto err1;
	}
	vec->bvec = (typeof(vec->bvec)) vec->mem->ptr;
	return vec;
err2:
	bmem_close_free(vec->mem);
err1:
	free(vec);
err0:
	return NULL;
}

void* bmvec_generic_open(char *path)
{
	struct bmvec_char *vec = (typeof(vec)) malloc(sizeof(*vec));
	if (!vec) {
		berror("malloc");
		goto err0;
	}
	pthread_mutex_init(&vec->mutex, NULL);
	vec->mem = bmem_open(path);
	if (!vec->mem) {
		berror("bmem_open");
		goto err1;
	}
	vec->bvec = (typeof(vec->bvec)) vec->mem->ptr;
	return vec;
err2:
	bmem_close_free(vec->mem);
err1:
	free(vec);
err0:
	return NULL;
}

int bmvec_u64_init(struct bmvec_u64 *vec, uint32_t size, uint64_t value)
{
	int __size = (size|(BMVEC_INC-1))+1;
	int init_size = sizeof(vec->bvec)+__size*sizeof(uint64_t);
	int64_t off = bmem_alloc(vec->mem, init_size);
	if (off == -1) {
		berror("bmem_alloc");
		return -1;
	}
	vec->bvec = (typeof(vec->bvec)) vec->mem->ptr;
	vec->bvec->alloc_len = __size;
	vec->bvec->len = size;
	int i;
	uint64_t *data = vec->bvec->data;
	for (i=0; i<size; i++) {
		data[i] = value;
	}
	return 0;
}

int bmvec_generic_init(void *_vec, uint32_t size, void *elm, uint32_t elm_size)
{
	struct bmvec_char *vec = (typeof(vec)) _vec;
	int __size = (size|(BMVEC_INC-1))+1;
	int init_size = sizeof(vec->bvec)+__size*elm_size;
	int64_t off = bmem_alloc(vec->mem, init_size);
	if (off == -1) {
		berror("bmem_alloc");
		return -1;
	}
	vec->bvec = (typeof(vec->bvec)) vec->mem->ptr;
	vec->bvec->alloc_len = __size;
	vec->bvec->len = size;
	int i;
	char *data = vec->bvec->data;
	for (i=0; i<size; i++) {
		memcpy(data+elm_size*i, elm, elm_size);
	}
	return 0;
}

void bmvec_u64_close_free(struct bmvec_u64 *vec)
{
	bmem_close_free(vec->mem);
	free(vec);
}

void bmvec_generic_close_free(void *_vec)
{
	struct bmvec_char *vec = (typeof(vec)) _vec;
	bmem_close_free(vec->mem);
	free(vec);
}

int bmvec_unlink(const char *path)
{
	return bmem_unlink(path);
}
