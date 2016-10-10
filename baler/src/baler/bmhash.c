/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
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
 * \file bmhash.c
 * \author Narate Taerat (narate at ogc dot us)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "bmhash.h"
#include "butils.h"

#include "fnv_hash.h"
#include "murmur_hash.h"

#define BMHASH_VER "BMHASH_0.1"

static inline
struct bmhash_header *bmhash_get_header(struct bmhash *bmh)
{
	return bmh->mem->ptr;
}

static inline
struct bvec_u64 *bmhash_get_table(struct bmhash *bmh)
{
	return bmhash_get_header(bmh)->table;
}

static inline
int __bmhash_initialized(struct bmhash *bmh)
{
	return (bmh->mem->hdr->ulen >= sizeof(struct bmhash_header)) &&
		(memcmp((bmhash_get_header(bmh)->ver), BMHASH_VER, sizeof(BMHASH_VER))==0);
}

static
int __bmhash_init(struct bmhash *bmh, va_list ap)
{
	int rc = 0;
	uint64_t offset;
	uint32_t i;
	uint32_t hash_size = va_arg(ap, typeof(hash_size));
	uint32_t fn_enum = va_arg(ap, typeof(fn_enum));
	uint32_t seed = va_arg(ap, typeof(seed));
	struct bmhash_header *hdr;

	offset = bmem_alloc(bmh->mem, sizeof(*hdr) + sizeof(struct bvec_u64) +
					hash_size*sizeof(uint64_t));
	if (!offset)
		return ENOMEM;
	hdr = BMPTR(bmh->mem, offset);
	memcpy(hdr->ver, BMHASH_VER, sizeof(BMHASH_VER));
	hdr->count = 0;
	hdr->hash_size = hash_size;
	hdr->fn_enum = fn_enum;
	hdr->seed = seed;
	hdr->table->alloc_len = hdr->hash_size;
	hdr->table->len = hdr->hash_size;

	for (i = 0; i < hdr->hash_size; i++) {
		hdr->table->data[i] = 0;
	}
	return 0;
}

struct bmhash *bmhash_open(const char *path, int flag, ...)
{
	struct bmhash *bmh = NULL;
	va_list ap;
	struct stat st;
	struct bmhash_header *hdr;
	int rc;

	rc = stat(path, &st);

	if (rc && errno == ENOENT && !(flag & O_CREAT)) {
		return NULL;
	}

	bmh = malloc(sizeof(*bmh));
	if (!bmh)
		return NULL;

	snprintf(bmh->path, sizeof(bmh->path), "%s", path);
	bmh->mem = bmem_open(path);
	if (!bmh->mem) {
		free(bmh);
		return NULL;
	}

	if (!__bmhash_initialized(bmh)) {
		if (!(flag & O_CREAT)) {
			bmhash_close_free(bmh);
			errno = EINVAL;
			return NULL;
		}
		va_start(ap, flag);
		rc = __bmhash_init(bmh, ap);
		va_end(ap);
		if (rc) {
			bmhash_close_free(bmh);
			errno = rc;
			return NULL;
		}
	}

	hdr = bmhash_get_header(bmh);

	switch (hdr->fn_enum) {
	case BMHASH_FN_FNV:
		bmh->hash_fn = fnv_hash_a1_32;
		break;
	case BMHASH_FN_MURMUR:
		bmh->hash_fn = (void*)MurmurHash3_x86_32;
		break;

	default:
		bmhash_close_free(bmh);
		bmh = NULL;
		errno = EINVAL;
	}

	return bmh;
}

int bmhash_unlink(const char *path)
{
	return bmem_unlink(path);
}

void bmhash_close_free(struct bmhash *bmh)
{
	bmem_close_free(bmh->mem);
	bmh->mem = NULL;
	free(bmh);
}

static
struct bmhash_entry *__bmhash_entry_find(struct bmhash *bmh,
				const struct bstr *key, uint32_t *hv)
{
	uint32_t __hv;
	struct bmhash_header *hdr = bmhash_get_header(bmh);
	struct bmhash_entry *ent = NULL;
	__hv = bmh->hash_fn(key->cstr, key->blen, hdr->seed);
	__hv %= hdr->table->len;

	if (hv)
		*hv = __hv;

	BMLIST_FOREACH(ent, hdr->table->data[__hv], link, bmh->mem) {
		if (0 == bstr_cmp(key, &ent->key)) {
			/* Found it */
			return ent;
		}
	}

	return NULL; /* Not found */
}

struct bmhash_entry *bmhash_entry_set(struct bmhash *bmh,
				      const struct bstr *key, uint64_t value)
{
	uint32_t hv;
	struct bmhash_entry *ent = __bmhash_entry_find(bmh, key, &hv);
	if (!ent) {
		struct bmhash_header *hdr = bmhash_get_header(bmh);
		uint64_t off = bmem_alloc(bmh->mem, sizeof(*ent) + key->blen);
		if (!off) {
			errno = ENOMEM;
			return NULL;
		}
		hdr = bmhash_get_header(bmh);
		ent = BMPTR(bmh->mem, off);
		memcpy(ent->key.cstr, key->cstr, key->blen);
		ent->key.blen = key->blen;
		BMLIST_INSERT_HEAD(hdr->table->data[hv], ent, link, bmh->mem);
	}
	ent->value = value;
	return ent;
}

struct bmhash_entry *bmhash_entry_get(struct bmhash *bmh,
				      const struct bstr *key)
{
	return __bmhash_entry_find(bmh, key, NULL);
}

void bmhash_iter_init(struct bmhash_iter *iter, struct bmhash *bmh)
{
	iter->bmh = bmh;
	iter->ent = NULL;
	iter->idx = 0;
}

struct bmhash_iter *bmhash_iter_new(struct bmhash *bmh)
{
	struct bmhash_iter *iter = malloc(sizeof(*iter));
	if (!iter)
		return NULL;
	bmhash_iter_init(iter, bmh);
	return iter;
}

void bmhash_iter_free(struct bmhash_iter *iter)
{
	free(iter);
}

struct bmhash_entry *bmhash_iter_entry(struct bmhash_iter *iter)
{
	return iter->ent;
}

int bmhash_iter_begin(struct bmhash_iter *iter)
{
	struct bmhash_header *hdr = bmhash_get_header(iter->bmh);
	struct bvec_u64 *table = hdr->table;
	int i;
	for (i = 0; i < table->len; i++) {
		if (table->data[i]) {
			iter->idx = i;
			iter->ent = BMPTR(iter->bmh->mem, table->data[i]);
			return 0;
		}
	}
	return ENOENT;
}

int bmhash_iter_next(struct bmhash_iter *iter)
{
	if (!iter->ent)
		return ENOENT;

	iter->ent = BMLIST_NEXT(iter->ent, link, iter->bmh->mem);
	if (iter->ent)
		return 0;

	/* Moving iter to the next list */
	struct bmhash_header *hdr = bmhash_get_header(iter->bmh);
	struct bvec_u64 *table = hdr->table;
	iter->idx++;
	while (iter->idx < table->len) {
		if (table->data[iter->idx]) {
			iter->ent = BMPTR(iter->bmh->mem,
						table->data[iter->idx]);
			return 0;
		}
		iter->idx++;
	}

	/* Reaching here == no more list */
	return ENOENT;
}
