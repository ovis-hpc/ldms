/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2016 Sandia Corporation. All rights reserved.
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
#include "btkn.h"
#include <ctype.h>

const char *btkn_type_str[] = {
	BTKN_TYPE__LIST(, BENUM_STR)
};

btkn_type_t btkn_type(const char *str)
{
	return bget_str_idx(btkn_type_str, BTKN_TYPE_LAST, str);
}

struct btkn_store* btkn_store_open(const char *path, int flag)
{
	struct btkn_store *ts = NULL;
	int create = flag & O_CREAT;
	int acc_mode = flag & O_ACCMODE;
	char *tmp = malloc(PATH_MAX);
	if (!tmp)
		return NULL;
	if (!bfile_exists(path)) {
		if (!create) {
			errno = ENOENT;
			goto err0;
		}
		if (bmkdir_p(path, 0755) != 0) {
			goto err0;
		}
	}
	if (!bis_dir(path)) {
		errno = EINVAL;
		goto err0;
	}
	ts = calloc(1, sizeof(*ts));
	if (!ts) {
		goto err0;
	}
	ts->path = strdup(path);
	if (!ts->path) {
		goto err1;
	}
	snprintf(tmp, PATH_MAX, "%s/tkn_attr.bmvec", path);
	ts->attr = (void*)bmvec_generic_open(tmp);
	if (!ts->attr) {
		goto err1;
	}
	snprintf(tmp, PATH_MAX, "%s/tkn.map", path);
	ts->map = bmap_open(tmp);
	if (!ts->map) {
		goto err1;
	}
	goto cleanup;

err1:
	btkn_store_close_free(ts);
err0:
cleanup:
	free(tmp);
	return ts;
}

void btkn_store_close_free(struct btkn_store *s)
{
	if (s->path)
		free(s->path);
	if (s->attr)
		bmvec_generic_close_free((void*)s->attr);
	if (s->map)
		bmap_close_free(s->map);
	free(s);
}

int btkn_store_id2str(struct btkn_store *store, uint32_t id,
		      char *dest, int len)
{
	dest[0] = '\0'; /* first set dest to empty string */
	const struct bstr *bstr = bmap_get_bstr(store->map, id);
	if (!bstr)
		return ENOENT;
	if (len <= bstr->blen)
		return ENOMEM;
	strncpy(dest, bstr->cstr, bstr->blen);
	dest[bstr->blen] = '\0'; /* null-terminate the string */
	return 0;
}

static const char *__hex = "0123456789abcdef";

int btkn_store_id2str_esc(struct btkn_store *store, uint32_t id,
		      char *dest, int len)
{
	int i = 0, o = 0;
	dest[0] = '\0'; /* first set dest to empty string */
	const struct bstr *bstr = bmap_get_bstr(store->map, id);
	if (!bstr)
		return ENOENT;
	if (len <= bstr->blen)
		return ENOMEM;
	while (i < bstr->blen) {
		if (isgraph(bstr->cstr[i])) {
			if (o >= len - 1)
				break;
			dest[o++] = bstr->cstr[i];
		} else {
			if (o >= len - 4)
				break;
			dest[o++] = '\\';
			dest[o++] = 'x';
			dest[o++] = __hex[((unsigned char)bstr->cstr[i])>>4];
			dest[o++] = __hex[((unsigned char)bstr->cstr[i])&0xF];
		}
		i++;
	}
	if (i < bstr->blen)
		return ENOMEM;
	dest[o] = '\0';
	return 0;
}


uint32_t btkn_store_insert_cstr(struct btkn_store *store, const char *str,
							btkn_type_t type)
{
	int blen = strlen(str);
	if (!blen) {
		errno = EINVAL;
		return BMAP_ID_ERR;
	}
	struct bstr *bstr = malloc(sizeof(*bstr) + blen);
	bstr->blen = blen;
	memcpy(bstr->cstr, str, blen);
	uint32_t id = btkn_store_insert(store, bstr);
	if (id == BMAP_ID_ERR)
		return id;
	struct btkn_attr attr;
	attr.type = type;
	btkn_store_set_attr(store, id, attr);
	return id;
}

int btkn_store_char_insert(struct btkn_store *store, const char *cstr,
							btkn_type_t type)
{
	uint32_t buf[4];
	struct bstr *bs = (void*)buf;
	uint64_t tkn_id;
	bs->blen = 1;
	const char *c = cstr;
	struct btkn_attr attr;
	attr.type = type;
	while (*c) {
		bs->cstr[0] = *c;
		tkn_id = btkn_store_insert(store, bs);
		if (tkn_id == BMAP_ID_ERR)
			return errno;
		btkn_store_set_attr(store, tkn_id, attr);
		c++;
	}
	return 0;
}

int btkn_store_refresh(struct btkn_store *store)
{
	int rc;
	rc = bmvec_generic_refresh((void*)store->attr);
	if (rc)
		return rc;
	rc = bmap_refresh(store->map);
	return rc;
}

void btkn_store_iterate(struct btkn_store *btkn_store,
			int (*cb)(uint32_t id, const struct bstr *bstr,
					const struct btkn_attr *attr))
{
	uint32_t last = btkn_store->map->hdr->next_id - 1;
	uint32_t i;
	int rc;
	for (i = BMAP_ID_BEGIN; i <= last; i++) {
		struct btkn_attr attr;
		const struct bstr *bstr;
		attr = btkn_store_get_attr(btkn_store, i);
		bstr = btkn_store_get_bstr(btkn_store, i);
		if (!bstr)
			continue;
		rc = cb(i, bstr, &attr);
		if (rc)
			break;
	}
}
