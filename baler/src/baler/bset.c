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
#include "bset.h"
#include "butils.h"
#include <stdlib.h>

#include "fnv_hash.h"

struct bset_u32* bset_u32_alloc(int hsize)
{
	struct bset_u32 *set = (typeof(set)) calloc(1, sizeof(*set));
	if (!set)
		goto err0;
	if (bset_u32_init(set, hsize) == -1)
		goto err1;
	return set;
err1:
	free(set);
err0:
	return NULL;
}

int bset_u32_init(struct bset_u32 *set, int hsize)
{
	if (!set)
		return -1;
	if (!hsize)
		hsize = BSET_DEFAULT_HSIZE;
	set->hash = (typeof(set->hash)) calloc(hsize, sizeof(*set->hash));
	if (!set->hash)
		return -1;
	set->hsize = hsize;
	set->count = 0;
	return 0;
}

void bset_u32_clear(struct bset_u32 *set)
{
	if (!set)
		return;
	int i;
	for (i=0; i<set->hsize; i++) {
		struct blist_u32 *elm;
		while ((elm = LIST_FIRST(&set->hash[i]))) {
			LIST_REMOVE(elm, link);
			free(elm);
		}
	}
	free(set->hash);
}

void bset_u32_free(struct bset_u32 *set)
{
	bset_u32_clear(set);
	free(set);
}

int bset_u32_exist(struct bset_u32 *set, uint32_t val)
{
	uint32_t idx = val % set->hsize;
	struct blist_u32_head *lh = &set->hash[idx];
	struct blist_u32 *elm;
	LIST_FOREACH(elm, lh, link) {
		if (elm->data == val)
			return 1;
	}
	return 0;
}

/**
 * The same as ::bset_u32_exist, but also export index \a idx of the \a val.
 * \param set The pointer to ::bset_u32.
 * \param val The value to be inserted.
 * \param[out] idx The (output) index of \a val.
 */
int bset_u32_exist_idx(struct bset_u32 *set, uint32_t val, uint32_t *idx)
{
	*idx = val % set->hsize;
	struct blist_u32_head *lh = &set->hash[*idx];
	struct blist_u32 *elm;
	LIST_FOREACH(elm, lh, link) {
		if (elm->data == val)
			return 1;
	}
	return 0;
}

int bset_u32_insert(struct bset_u32 *set, uint32_t val)
{
	uint32_t idx;
	if (bset_u32_exist_idx(set, val, &idx))
		return EEXIST;
	struct blist_u32 *elm = (typeof(elm)) malloc(sizeof(*elm));
	if (!elm)
		return ENOMEM;
	elm->data = val;
	LIST_INSERT_HEAD(&set->hash[idx], elm, link);
	set->count++;
	return 0;
}

int bset_u32_remove(struct bset_u32 *set, uint32_t val)
{
	int idx = val % set->hsize;
	struct blist_u32 *elm;
	LIST_FOREACH(elm, &set->hash[idx], link) {
		if (elm->data != val)
			continue;
		LIST_REMOVE(elm, link);
		free(elm);
		return 0;
	}
	return ENOENT;
}

struct bset_u32_iter* bset_u32_iter_new(struct bset_u32 *set)
{
	struct bset_u32_iter *iter = calloc(1, sizeof(*iter));
	if (!iter)
		return NULL;
	iter->set = set;
	return iter;
}

int bset_u32_iter_next(struct bset_u32_iter *iter, uint32_t *out)
{
	while (!iter->elem && iter->next_idx < iter->set->hsize) {
		iter->elem = LIST_FIRST(&iter->set->hash[iter->next_idx++]);
	}

	if (!iter->elem)
		return ENOENT;

	*out = iter->elem->data;
	iter->elem = LIST_NEXT(iter->elem, link);
	return 0;
}

int bset_u32_iter_reset(struct bset_u32_iter *iter)
{
	iter->next_idx = 0;
	iter->elem = NULL;
	return 0;
}

int uint32_t_cmp(const void *p1, const void *p2)
{
	/* 0 - 4294967295 = 1, so *p1 - *p2 is unsafe */
	if (*(uint32_t*)p1 < *(uint32_t*)p2)
		return -1;
	if (*(uint32_t*)p1 > *(uint32_t*)p2)
		return 1;
	return 0;
}

int bset_u32_to_brange_u32(struct bset_u32 *set, struct brange_u32_head *head)
{
	int rc = 0;
	uint32_t *data = malloc(sizeof(*data) * set->count);
	if (!data) {
		rc = ENOENT;
		goto err1;
	}
	struct bset_u32_iter *iter = bset_u32_iter_new(set);
	if (!iter) {
		rc = errno;
		goto err2;
	}
	uint32_t v;
	int i = 0;
	while (bset_u32_iter_next(iter, &v) == 0) {
		data[i++] = v;
	}
	free(iter); /* iter is not needed anymore */
	iter = NULL;
	qsort(data, set->count, sizeof(data[0]), uint32_t_cmp);
	struct brange_u32 *pr; /* previous range */
	struct brange_u32 *r; /* current range */
	/* init the first range */
	r = calloc(1, sizeof(*r));
	if (!r) {
		rc = errno;
		goto err3;
	}
	r->a = r->b = data[0];
	LIST_INSERT_HEAD(head, r, link);

	for (i=1; i<set->count; i++) {
		if (data[i] - r->b == 1) {
			r->b = data[i];
			continue;
		}
		/* else create new range */
		pr = r;
		r = calloc(1, sizeof(*r));
		if (!r) {
			rc = errno;
			goto err4;
		}
		r->a = r->b = data[i];
		LIST_INSERT_AFTER(pr, r, link);
	}
	free(data);
	data = NULL;

	return 0;

err4:
	while ((r = LIST_FIRST(head))) {
		LIST_REMOVE(r, link);
		free(r);
	}
err3:
	free(iter);
err2:
	free(data);
err1:
	free(head);
err0:
	return rc;
}

struct bset_u32 *bset_u32_from_numlist(const char *num_lst, int hsize)
{
	int rc = 0;
	int tn, sn;
	int a, b, i;
	const char *s = num_lst;
	struct bset_u32 *set = bset_u32_alloc(hsize);
	if (!set) {
		goto err0;
	}

	while (*s) {
		tn = sscanf(s, "%d%n - %d%n", &a, &sn, &b, &sn);
		switch (tn) {
		case 1:
			b = a;
			break;
		case 2:
			/* do nothing */
			break;
		default:
			/* Parse error */
			errno = EINVAL;
			goto err1;
		}
		s += sn;
		for (i = a; i <= b; i++) {
			rc = bset_u32_insert(set, i);
			if (rc && rc != EEXIST) {
				errno = rc;
				goto err1;
			}
		}
		while (*s && *s == ',')
			s++;
	}

	return set;
err1:
	bset_u32_free(set);
err0:
	return NULL;
}

struct brange_u32_iter *brange_u32_iter_new(struct brange_u32 *first)
{
        struct brange_u32_iter *itr = calloc(1, sizeof(*itr));
        if (!itr)
                return NULL;
        itr->first_range = first;
        itr->current_range = first;
        itr->current_value = first->a;
        return itr;
}

void brange_u32_iter_free(struct brange_u32_iter *itr)
{
	free(itr);
}

int brange_u32_iter_get_value(struct brange_u32_iter *itr, uint32_t *v)
{
	if (itr->current_range) {
		*v = itr->current_value;
		return 0;
	}
	return ENOENT;
}

int brange_u32_iter_next(struct brange_u32_iter *itr, uint32_t *v)
{
again:
	if (!itr->current_range)
		return ENOENT;
	if (itr->current_value == itr->current_range->b) {
		itr->current_range = LIST_NEXT(itr->current_range, link);
		goto again;
	}

	if (itr->current_value < itr->current_range->a) {
		*v = itr->current_value = itr->current_range->a;
		return 0;
	}

	*v = ++itr->current_value;

	return 0;
}

int brange_u32_iter_begin(struct brange_u32_iter *itr, uint32_t *v)
{
	if (!itr) {
		*v = 0;
		return 0;
	}
	itr->current_range = itr->first_range;
	*v = itr->current_value = itr->current_range->a;
	return 0;
}

int brange_u32_iter_fwd_seek(struct brange_u32_iter *itr, uint32_t *v)
{
	struct brange_u32 *r;
	int rc;
	if (*v < itr->current_value)
		return EINVAL;
	r = itr->current_range;
	while (r) {
		rc = brange_u32_cmp(r, *v);
		if (rc > 0)
			goto next;
		/* found a valid range */
		itr->current_range = r;
		itr->current_value = (rc)?(r->a):(*v);
		*v = itr->current_value;
		return 0;
	next:
		r = LIST_NEXT(r, link);
	}
	return ENOENT;
}
