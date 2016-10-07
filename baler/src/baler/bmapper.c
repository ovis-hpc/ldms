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
#include "bmapper.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>

#include "fnv_hash.h"
#include "butils.h"
#include "bmvec.h"

/**
 * Hash function interface, for uint32_t.
 * The function interface is defined in this file becuase it is intended to be
 * used only locally.
 */
typedef uint32_t (*hash_u32_fn_t)(const char *str, int len, uint32_t seed);

/**
 * Hash function interface, for uint64_t.
 * The function interface is defined in this file becuase it is intended to be
 * used only locally.
 */
typedef uint64_t (*hash_u64_fn_t)(const char *str, int len, uint64_t seed);

/**
 * Hash function for Baler Mapper.
 */
hash_u64_fn_t bhash = fnv_hash_a1_64;

static
struct bstr* special_bstr[] = {
	[BMAP_ID_BEGIN - 1] = NULL, /* Last special bstr */
};

/**
 * Localized convenient macro for special_bstr initialization.
 */
#define ___SPECIAL_BSTR(ID, STR) do { \
	special_bstr[ID] = bstr_alloc_init_cstr(STR); \
	assert(special_bstr[ID]); \
} while(0);

void __special_bstr_init() __attribute__((constructor));
void __special_bstr_init()
{
	static int initialized = 0;
	if (initialized)
		return;
	initialized = 1;
	const char *starenv = getenv("BALER_STAR");
	if (starenv) {
		___SPECIAL_BSTR(BMAP_ID_STAR, starenv);
	} else {
		___SPECIAL_BSTR(BMAP_ID_STAR, BMAP_STAR_TEXT);
	}
}

struct bmap* bmap_open(const char *path)
{
	/* The function calls at err* labels can change errno,
	 * hence _errno is used to save the current errno before
	 * error hadling */
	int _errno = 0;
	static char ptmp[PATH_MAX]; /* temporary path */
	struct bmap *b = (typeof(b)) malloc(sizeof(*b));
	if (!b)
		goto errX;
	strcpy(b->path, path);
	pthread_mutex_init(&b->mutex, NULL);

	if (!bfile_exists(path)) {
		if (bmkdir_p(path, 0755) != 0) {
			_errno = errno;
			goto err0;
		}
	}

	if (!bis_dir(path)) {
		_errno = EINVAL;
		goto err0;
	}

	sprintf(ptmp, "%s/%s", path, "bmhash.mmap");
	if ((b->bmhash = bmvec_u64_open(ptmp)) == NULL) {
		_errno = errno;
		goto err0;
	}

	sprintf(ptmp, "%s/%s", path, "mlist.mmap");
	if ((b->mlist = bmem_open(ptmp)) == NULL) {
		_errno = errno;
		goto err1;
	}

	sprintf(ptmp, "%s/%s", path, "mstr.mmap");
	if ((b->mstr = bmem_open(ptmp)) == NULL) {
		_errno = errno;
		goto err2;
	}

	sprintf(ptmp, "%s/%s", path, "bmstr_idx.mmap");
	if ((b->bmstr_idx = bmvec_u64_open(ptmp)) == NULL) {
		_errno = errno;
		goto err3;
	}

	sprintf(ptmp, "%s/%s", path, "mhdr.mmap");
	if ((b->mhdr = bmem_open(ptmp)) == NULL) {
		_errno = errno;
		goto err4;
	}
	/* mhdr should contain only hdr, hence it is safe to extract the pointer
	 * this way */
	b->hdr = b->mhdr->ptr;

	struct bmem *hmem = b->bmhash->mem;
	if (hmem->hdr->ulen == sizeof(*hmem->hdr)) {
		char *env_hash_sz = getenv("BMAP_HASH_SZ");
		long hash_sz = 0;

		if (env_hash_sz) {
			hash_sz = strtol(env_hash_sz, NULL, 0);
		}

		if (!hash_sz) {
			hash_sz = 8388619;
		}

		if (bmap_init(b, hash_sz)==-1) {
			// map init fail
			_errno = errno;
			goto err5;
		}
	}

	return b;
err5:
	bdebug("bmap_open, err5\n");
	bmem_close_free(b->mhdr);
err4:
	bdebug("bmap_open, err4\n");
	bmvec_u64_close_free(b->bmstr_idx);
err3:
	bdebug("bmap_open, err3\n");
	bmem_close_free(b->mstr);
err2:
	bdebug("bmap_open, err2\n");
	bmem_close_free(b->mlist);
err1:
	bdebug("bmap_open, err1\n");
	bmvec_u64_close_free(b->bmhash);
err0:
	bdebug("bmap_open, err0\n");
	free(b);
	if (_errno)
		errno = _errno;
errX:
	return NULL;
}

void bmap_close_free(struct bmap* m)
{
	bmvec_generic_close_free((void*)m->bmstr_idx);
	bmem_close_free(m->mstr);
	bmem_close_free(m->mlist);
	bmvec_generic_close_free((void*)m->bmhash);
	bmem_close_free(m->mhdr);
	free(m);
}

int bmap_init(struct bmap *map, int nmemb)
{
	int i;
	struct bmem_hdr *hash_hdr = map->bmhash->mem->hdr;
	struct bmem_hdr *idx_hdr = map->bmstr_idx->mem->hdr;
	if (hash_hdr->ulen != sizeof(*hash_hdr)
			|| idx_hdr->ulen != sizeof(*idx_hdr)) {
		/* The map has already been initialized */
		berr("map has been initialized\n");
		errno = EINVAL;
		return -1;
	}

	/* Initialize map header */
	int64_t hdr_off = bmem_alloc(map->mhdr, sizeof(struct bmap_hdr));
	if (!hdr_off) {
		berr("cannot allocate from bmem\n");
		errno = ENOMEM;
		return -1;
	}
	map->hdr = BMPTR(map->mhdr, hdr_off);
	map->hdr->next_id = BMAP_ID_BEGIN;
	map->hdr->count = 0;

	/* Initialization for map->mhash */
	if (bmvec_u64_init(map->bmhash, nmemb, 0) == -1) {
		berr("hash init failed\n");
		return -1; /* errno should be set already */
	}

	/* Initialization for map->mstr_idx */
	if (bmvec_u64_init(map->bmstr_idx, 0, 0) == -1) {
		berr("bmstr_idx init failed\n");
		return -1;
	}

	/* Don't have to do anything with map->mlist and map->mstr */
	return 0;
}

int bmap_rehash(struct bmap *map, int nmemb)
{
	pthread_mutex_lock(&map->mutex);
	/* Resize bmhash, by setting a value at the designated index.
	 * bmvec will automatically resize itself. */
	struct bmvec_u64 *h = map->bmhash;
	bmvec_u64_set(h, nmemb, 0);
	/* Then, discard the existing information. */
	int i;
	uint64_t *hdata = h->bvec->data;
	for (i=0; i<nmemb; i++) {
		if (hdata[i])
			hdata[i] = 0;
	}
	/* Then,  we can iterate through the nodes in mlist, discarding their
	 * old links and create new ones according to the new hash. */
	struct bmlnode_mapper *node = map->mlist->ptr;
	uint64_t node_off = sizeof(map->mlist->hdr);
	uint64_t len = map->mlist->hdr->ulen;

	uint64_t *stridx = map->bmstr_idx->bvec->data;
	struct bmem *mstr = map->mstr;

	while (node_off < len) {
		struct bstr *str = BMPTR(mstr, node->str_off);
		/* NOTE: We can speed up the re-hashing by remembering the hash
		 * key before modulo. bhash will always return the same value
		 * given the same input. This is mark as IMPROVE LATER.
		 */
		uint64_t key = bhash(str->cstr, str->blen, 0) % nmemb;
		node->link.next = hdata[key];
		hdata[key] = node_off;
		/* Go to next node. */
		node++;
		node_off += sizeof(*node);
	}
	pthread_mutex_unlock(&map->mutex);
	return 0;
}


/* caller must have map->mutex locked. */
static
uint32_t __bmap_get_id_plus64(struct bmap *map,
		const struct bstr *str, uint64_t *ohidx)
{
	struct bvec_u64 *hvec = map->bmhash->bvec;
	uint64_t key = bhash(str->cstr, str->blen, 0);
	uint64_t hidx = key % hvec->len;

	int64_t lhead = hvec->data[hidx];

	struct bmem *mlist = map->mlist;
	struct bmem *mstr = map->mstr;
	struct bvec_u64 *str_idx = map->bmstr_idx->bvec;
	struct bmlnode_mapper *node;
	int id = BMAP_ID_NOTFOUND;
	BMLIST_FOREACH(node, lhead, link, mlist) {
		if (!node->id) {
			/* This is not supposed to happen */
			berr("node->data is NULL");
			id = BMAP_ID_ERR;
			goto out;
		}
		struct bstr *_str = BMPTR(mstr, node->str_off);
		if (_str->blen != str->blen)
			continue;
		if (memcmp(_str->cstr, str->cstr, str->blen) == 0) {
			id = node->id;
			break;
		}
	}
	if (ohidx)
		*ohidx = hidx;
out:
	return id;
}

/**
 * Internal get_id, similar to ::bmap_get_id(), but also return hash key.
 * \param map The pointer to ::bmap structure
 * \param str The poitner to ::bstr structure
 * \param[out] ohidx Hash index output
 * \return id
 */
uint32_t bmap_get_id_plus64(struct bmap *map,
		const struct bstr *str, uint64_t *ohidx)
{
	uint32_t id;
	pthread_mutex_lock(&map->mutex);
	id = __bmap_get_id_plus64(map, str, ohidx);
	pthread_mutex_unlock(&map->mutex);
	return id;
}

uint32_t bmap_get_id(struct bmap *map, const struct bstr *s)
{
	/* Lazy implementation ... */
	return bmap_get_id_plus64(map, s, 0);
}

/*
 * caller must have map->mutex locked.
 */
static
const struct bstr* __bmap_get_bstr(struct bmap *map, uint32_t id)
{
	const struct bstr *bstr = NULL;
	struct bvec_u64 *str_idx = map->bmstr_idx->bvec;
	if (str_idx->len <= id) /* out of range */
		goto out;
	if (id < BMAP_ID_BEGIN) { /* special ID */
		bstr = special_bstr[id];
		goto out;
	}
	/* Normal ID */
	int64_t str_off = str_idx->data[id];
	bstr = BMPTR(map->mstr, str_off);
out:
	return bstr;
}

const struct bstr* bmap_get_bstr(struct bmap *map, uint32_t id)
{
	pthread_mutex_lock(&map->mutex);
	const struct bstr *bstr = __bmap_get_bstr(map, id);
	pthread_mutex_unlock(&map->mutex);
	return bstr;
}

/**
 * \note On error, the allocated memory in ::bmem will not be freed.
 * 	Note as to do later.
 */
uint32_t bmap_insert(struct bmap *bm, const struct bstr *s)
{
	return bmap_insert_with_id(bm, s, 0);
}

uint32_t bmap_insert_with_id(struct bmap *bm, const struct bstr *s, uint32_t _id)
{
	uint32_t id;
	uint64_t hidx;
	const struct bstr *prev_bstr = NULL;

	pthread_mutex_lock(&bm->mutex);

	/* Check first if s exists in the map. */
	if ((id=__bmap_get_id_plus64(bm, s, &hidx)) != BMAP_ID_NOTFOUND) {
		if (!_id || id == _id)
			goto out;
		/* trying to assign same STR to different IDs */
		id = BMAP_ID_INVAL;
		goto out;
	}

	/* Also check if id exists in the map */
	prev_bstr = __bmap_get_bstr(bm, _id);
	/* Let through, allowing str aliasing */

	/* If s does not exist, allocate space for new bstr, and copy it */
	int64_t str_off = bmem_alloc(bm->mstr, sizeof(*s)+s->blen);
	if (!str_off) {
		berror("bmem_alloc");
		id = BMAP_ID_ERR;
		goto out;
	}
	struct bstr *str = BMPTR(bm->mstr, str_off);
	memcpy(str, s, sizeof(*s) + s->blen);

	/* Then, assign ID (which is also used as an index) */
	id = (_id)?(_id):(bm->hdr->next_id);
	if (id >= bm->hdr->next_id) {
		bm->hdr->next_id = id + 1;
	}

	if (!prev_bstr) {
		/* and set an index to it if this is the first of the alias */
		if (bmvec_u64_set(bm->bmstr_idx, id, str_off)) {
			berror("bmvec_u64_set");
			id = BMAP_ID_ERR;
			goto out;
		}
	}

	/* Allocate a node in linked list */
	struct bmlnode_mapper *node;
	struct bmem *mlist = bm->mlist;
	int64_t node_off = bmem_alloc(mlist, sizeof(*node));
	if (!node_off) {
		berror("bmem_alloc");
		id = BMAP_ID_ERR;
		goto out;
	}
	node = BMPTR(mlist, node_off);
	node->id = id;
	node->str_off = str_off;

	/* And insert it into list head (a cell in hash table) */
	uint64_t *hdata = bm->bmhash->bvec->data;
	BMLIST_INSERT_HEAD(hdata[hidx], node, link, mlist);

	bm->hdr->count++;
out:
	pthread_mutex_unlock(&bm->mutex);
	return id;
}

static
void __bstr_print_escape(const struct bstr *bstr)
{
	int i;
	for (i = 0; i < bstr->blen; i++) {
		if (isgraph(bstr->cstr[i])) {
			printf("%c", bstr->cstr[i]);
		} else {
			printf("\\x%02hhx", (unsigned char)bstr->cstr[i]);
		}
	}
}

static
void __bstr_print_hex(const struct bstr *bstr)
{
	int i;
	printf("blen: %d", bstr->blen);
	printf(", hex:");
	i = 0;
	while (i < bstr->blen) {
		if (i % 4 == 0)
			printf(" ");
		printf("%02hhx", bstr->cstr[i]);
		i++;
	}
}

void bmap_dump(struct bmap *bmap, int hex)
{
	uint32_t max_id = bmap->hdr->next_id;
	uint32_t id;
	const struct bstr *bstr;
	for (id = BMAP_ID_BEGIN; id < max_id; id++) {
		bstr = bmap_get_bstr(bmap, id);
		if (!bstr)
			continue;
		printf("%10u ", id);
		if (hex)
			__bstr_print_hex(bstr);
		else
			__bstr_print_escape(bstr);
		printf("\n");
	}
}

void bmap_dump_inverse(struct bmap *map, int hex)
{
	uint64_t idx;
	uint64_t *hdata = map->bmhash->bvec->data;
	uint64_t hlen = map->bmhash->bvec->len;
	struct bmem *mlist = map->mlist;
	struct bmem *mstr = map->mstr;
	struct bvec_u64 *str_idx = map->bmstr_idx->bvec;
	struct bmlnode_mapper *node;

	for (idx = 0; idx < hlen; idx++) {
		BMLIST_FOREACH(node, hdata[idx], link, mlist) {
			struct bstr *_str = BMPTR(mstr, node->str_off);
			if (hex)
				__bstr_print_hex(_str);
			else
				__bstr_print_escape(_str);
			printf(" %u\n", node->id);
		}
	}
}

void* bmap_get_ucontext(struct bmap *bmap)
{
	return bmap->ucontext;
}

void bmap_set_ucontext(struct bmap *bmap, void *ucontext)
{
	bmap->ucontext = ucontext;
}

void bmap_set_event_cb(struct bmap *bmap, bmap_ev_cb_fn ev_cb)
{
	bmap->ev_cb = ev_cb;
}

int bmap_unlink(const char *path)
{
	char *buff = malloc(PATH_MAX);
	int rc;
	int plen;
	if (!buff) {
		rc = ENOMEM;
		goto out;
	}
	plen = snprintf(buff, PATH_MAX, "%s", path);

	snprintf(buff + plen, PATH_MAX - plen, "bmhash.mmap");
	rc = bmvec_unlink(buff);
	if (rc && rc != ENOENT)
		goto out;

	snprintf(buff + plen, PATH_MAX - plen, "mlist.mmap");
	rc = bmem_unlink(buff);
	if (rc && rc != ENOENT)
		goto out;

	snprintf(buff + plen, PATH_MAX - plen, "mstr.mmap");
	rc = bmem_unlink(buff);
	if (rc && rc != ENOENT)
		goto out;

	snprintf(buff + plen, PATH_MAX - plen, "bmstr_idx.mmap");
	rc = bmvec_unlink(buff);
	if (rc && rc != ENOENT)
		goto out;

	snprintf(buff + plen, PATH_MAX - plen, "mhdr.mmap");
	rc = bmem_unlink(buff);
	if (rc && rc != ENOENT)
		goto out;

out:
	free(buff);
	return 0;
}

int bmap_refresh(struct bmap *bmap)
{
	int rc;
	int i, n;
	struct bmem *_bmem[] = {
		bmap->mhdr,
		bmap->mstr,
		bmap->mlist,
	};
	struct bmvec_u64 *_bmvec[] = {
		bmap->bmhash,
		bmap->bmstr_idx,
	};

	n = sizeof(_bmem)/sizeof(*_bmem);
	for (i = 0; i < n; i++) {
		rc = bmem_refresh(_bmem[i]);
		if (rc)
			return rc;
	}

	n = sizeof(_bmvec)/sizeof(*_bmvec);
	for (i = 0; i < n; i++) {
		rc = bmvec_generic_refresh((void*)_bmvec[i]);
		if (rc)
			return rc;
	}

	return 0;
}
