/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013,2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013,2015-2016 Sandia Corporation. All rights reserved.
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
 * \file bptn.c
 * \author Narate Taerat (narate@ogc.us)
 */

#include "bptn.h"
#include "butils.h"
#include "stdlib.h"
#include "string.h"
#include "bmlist.h"
#include <linux/limits.h>
#include <sys/fcntl.h>
#include <assert.h>

void battrarray_free(struct barray *a)
{
	int i;
	for (i=0; i<a->len; i++) {
		struct bptn_attr *attr;
		if (barray_get(a, i, &attr)) {
			bptn_attr_free(attr);
		}
	}
	barray_free(a);
}

struct bptn_attr* bptn_attr_alloc(uint32_t argc)
{
	struct bptn_attr *attr = (typeof(attr)) calloc(1,
			sizeof(*attr) + argc*sizeof(*attr->arg));
	if (!attr)
		goto err0;
	attr->argc = argc;
	int i;
	for (i=0; i<argc; i++) {
		if (bset_u32_init(&attr->arg[i], 0) == -1)
			goto err1;
	}
	return attr;
err1:
	for (i=0; i<argc; i++) {
		bset_u32_clear(&attr->arg[i]);
	}
	free(attr);
err0:
	return NULL;
}

void bptn_attr_free(struct bptn_attr *attr)
{
	int i;
	for (i=0; i<attr->argc; i++) {
		bset_u32_clear(&attr->arg[i]);
	}
	free(attr);
}

struct bptn_store* bptn_store_open(const char *path, int flag)
{
	int create = flag & O_CREAT;
	int acc_mode = flag & O_ACCMODE;
	if (!bfile_exists(path)) {
		if (!create) {
			errno = ENOENT;
			goto err0;
		}
		if (bmkdir_p(path, 0755) != 0)
			goto err0;
	}
	if (!bis_dir(path)) {
		errno = EINVAL;
		goto err0;
	}
	char tmp[PATH_MAX];
	struct bptn_store *store = calloc(1, sizeof(*store));
	pthread_mutex_init(&store->mutex, NULL);
	if (!store)
		goto err0;
	store->path = strdup(path);
	if (!store->path)
		goto err1;
	sprintf(tmp, "%s/map.map", path);
	store->map = bmap_open(tmp);
	if (!store->map)
		goto err1;
	sprintf(tmp, "%s/marg.map", path);
	store->marg = bmem_open(tmp);
	if (!store->marg)
		goto err1;
	sprintf(tmp, "%s/mattr.map", path);
	store->mattr = bmem_open(tmp);
	if (!store->mattr)
		goto err1;
	sprintf(tmp, "%s/attr_idx.map", path);
	store->attr_idx = bmvec_u64_open(tmp);
	if (!store->attr_idx)
		goto err1;
	uint32_t ptn_len = store->attr_idx->bvec->len;
	if (!ptn_len) {
		/* This store is just created, it needs initialization */
		int rc = bmvec_u64_init(store->attr_idx, 65536, 0);
		if (rc) {
			errno = rc;
			goto err1;
		}
	}

	if (acc_mode == O_RDONLY)
		/* skip internal attr sets if O_RDONLY */
		goto out;

	store->aattr = barray_alloc(sizeof(void*), ptn_len);
	if (!store->aattr)
		goto err1;
	int i, j;
	struct bvec_u64 *attr_bvec = store->attr_idx->bvec;
	int len = store->attr_idx->bvec->len;
	for (i=0; i<len; i++) {
		/* For each pattern */
		uint64_t attr_off = attr_bvec->data[i];
		struct bptn_attrM *attrM = BMPTR(store->mattr, attr_off);
		if (!attrM) /* attrM can be null */
			continue;
		struct bptn_attr *attr;
		attr = bptn_attr_alloc(attrM->argc);
		if (!attr)
			goto err1;
		barray_set(store->aattr, i, &attr);
		for (j=0; j<attrM->argc; j++) {
			/* For each argument set (implemented as list
			 * in mmapped file)*/
			struct bmlnode_u32 *node;
			BMLIST_FOREACH(node, attrM->arg_off[j], link,
					store->marg) {
				/* Add element into the in-memory set. */
				if (bset_u32_insert(&attr->arg[j], node->data)
						== BSET_INSERT_ERR)
					goto err1;
			}
		}
	}
out:
	return store;

err1:
	bptn_store_close_free(store);
err0:
	return NULL;
}

void bptn_store_close_free(struct bptn_store *store)
{
	/* Clear the in-memory stuffs first. */
	if (store->aattr)
		barray_free(store->aattr);
	if (store->path)
		free(store->path);
	/* Then clear the mmapped stuffs. */
	if (store->attr_idx)
		bmvec_u64_close_free(store->attr_idx);
	if (store->mattr)
		bmem_close_free(store->mattr);
	if (store->marg)
		bmem_close_free(store->marg);
	if (store->map)
		bmap_close_free(store->map);
	free(store);
}

int bptn_store_addmsg(struct bptn_store *store, struct timeval *tv,
					uint32_t comp_id, struct bmsg *msg)
{
	int rc = 0;
	pthread_mutex_lock(&store->mutex);
	struct bptn_attr *attr;
	if (!barray_get(store->aattr, msg->ptn_id, &attr)) {
		attr = NULL;
	}
	if (!attr) {
		/* First message for the pattern */
		attr = bptn_attr_alloc(msg->argc);
		if (!attr) {
			rc = ENOMEM;
			goto err0;
		}
		if (barray_set(store->aattr, msg->ptn_id, &attr)) {
			rc = errno;
			goto err1;
		}
	}
	uint64_t attrM_off = bmvec_u64_get(store->attr_idx, msg->ptn_id);
	struct bptn_attrM *attrM = BMPTR(store->mattr, attrM_off);
	if (!attrM) {
		/* First message for the pattern */
		attrM_off = bmem_alloc(store->mattr, sizeof(*attrM) +
				msg->argc*sizeof(typeof(attrM->arg_off[0])));
		if (!attrM_off) {
			rc = ENOMEM;
			goto err2;
		}
		attrM = BMPTR(store->mattr, attrM_off);
		attrM->count = 0;
		attrM->first_seen = attrM->last_seen = *tv;
		attrM->argc = msg->argc;
		bmvec_u64_set(store->attr_idx, msg->ptn_id, attrM_off);
	}

	/* should not happen, but better safe than sorry */
	assert(attr->argc == msg->argc && attrM->argc == msg->argc);

	attrM->count++;
	if (timercmp(tv, &attrM->first_seen, <))
		attrM->first_seen = *tv;
	if (timercmp(tv, &attrM->last_seen, >))
		attrM->last_seen = *tv;

	goto out;
err2:
	/* Unset pattern attribute */
	do {
		void *tmp = NULL;
		barray_set(store->aattr, msg->ptn_id, &tmp);
	} while (0);
err1:
	bptn_attr_free(attr);
err0:
out:
	pthread_mutex_unlock(&store->mutex);
	return rc;
}

int bptn_store_id2str(struct bptn_store *ptns, struct btkn_store *tkns,
		      uint32_t ptn_id, char *dest, int len)
{
	if (!ptns || !tkns || !dest)
		return EINVAL;
	char *s = dest;
	int slen = len;
	const struct bstr *ptn = bmap_get_bstr(ptns->map, ptn_id);
	if (!ptn)
		return ENOENT;
	int i;
	int rc;
	int l;
	const uint32_t *c;
	for (i=0,c=ptn->u32str; i<ptn->blen; c++,i+=sizeof(*c)) {
		rc = btkn_store_id2str(tkns, *c, s, slen);
		if (rc)
			return rc;
		l = strlen(s);
		s += l;
		slen -= l;
	}
	return 0;
}

int bptn_store_id2str_esc(struct bptn_store *ptns, struct btkn_store *tkns,
		      uint32_t ptn_id, char *dest, int len)
{
	if (!ptns || !tkns || !dest)
		return EINVAL;
	char *s = dest;
	int slen = len;
	const struct bstr *ptn = bmap_get_bstr(ptns->map, ptn_id);
	if (!ptn)
		return ENOENT;
	int i;
	int rc;
	int l;
	const uint32_t *c;
	for (i=0,c=ptn->u32str; i<ptn->blen; c++,i+=sizeof(*c)) {
		rc = btkn_store_id2str_esc(tkns, *c, s, slen);
		if (rc)
			return rc;
		l = strlen(s);
		s += l;
		slen -= l;
	}
	return 0;
}

int bptn_store_ptn2str(struct bptn_store *ptns, struct btkn_store *tkns,
			const struct bstr *ptn, char *dest, int len)
{
	int slen = len;
	int l;
	int i, rc;
	char *s;
	const uint32_t *c;
	for (i=0,c=ptn->u32str; i<ptn->blen; c++,i+=sizeof(*c)) {
		rc = btkn_store_id2str(tkns, *c, s, slen);
		if (rc)
			return rc;
		l = strlen(s);
		s += l;
		slen -= l;
	}
	return 0;
}

int bptn_store_ptn2str_esc(struct bptn_store *ptns, struct btkn_store *tkns,
			const struct bstr *ptn, char *dest, int len)
{
	int slen = len;
	int l;
	int i, rc;
	char *s;
	const uint32_t *c;
	for (i=0,c=ptn->u32str; i<ptn->blen; c++,i+=sizeof(*c)) {
		rc = btkn_store_id2str_esc(tkns, *c, s, slen);
		if (rc)
			return rc;
		l = strlen(s);
		s += l;
		slen -= l;
	}
	return 0;
}

uint32_t bptn_store_last_id(struct bptn_store *ptns)
{
	return ptns->map->hdr->next_id - 1;
}

uint32_t bptn_store_first_id(struct bptn_store *ptns)
{
	return BMAP_ID_BEGIN;
}

int bptn_store_refresh(struct bptn_store *ptns)
{
	int rc;
	rc = bmap_refresh(ptns->map);
	if (rc)
		return rc;
	rc = bmem_refresh(ptns->mattr);
	if (rc)
		return rc;
	rc = bmem_refresh(ptns->marg);
	if (rc)
		return rc;
	return rc;
}

const struct bptn_attrM *bptn_store_get_attrM(struct bptn_store *ptns, uint32_t id)
{
	uint64_t off = bmvec_u64_get(ptns->attr_idx, id);
	struct bptn_attrM *attrM = BMPTR(ptns->mattr, off);
	return attrM;
}
