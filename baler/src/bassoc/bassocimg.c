/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-16 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-16 Sandia Corporation. All rights reserved.
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
 * \file bassocimg.c
 * \author Narate Taerat (narate at ogc dot us)
 */

#include <errno.h>

#include "baler/butils.h"
#include "bassocimg.h"

#define BASSOCIMG_SEG_FOREACH(img, seg) \
	for ((seg) = bassocimg_seg_ref2ptr((img), BASSOCIMG_HDR(img)->first_seg_ref); \
		(seg); \
		(seg) = bassocimg_seg_ref2ptr((img), (seg)->next_seg_ref))

static const struct bassocimg_hdr __empty_hdr = {0};
static const struct bassocimg_seg __empty_seg = {0};

bassocimg_cache_t bassocimg_cache_open(const char *path, int create)
{
	int plen, pplen, rc;
	struct stat st;
	bassocimg_cache_t cache = calloc(1, sizeof(*cache));
	if (!cache)
		goto err0;

	plen = snprintf(cache->path, sizeof(cache->path), "%s", path);
	if (plen >= sizeof(cache->path)) {
		errno = EINVAL;
		goto err1;
	}

	/* check and optionally create cache dir */
	rc = bis_dir(cache->path);
	if (rc == 0) {
		if (errno != ENOENT)
			goto err1;

		/* no entry, check `create` */
		if (!create)
			goto err1;

		/* creating new cache dir */
		rc = bmkdir_p(cache->path, 0755);
		if (rc) {
			errno = rc;
			goto err1;
		}
	}

	/* bmap */
	pplen = snprintf(cache->path + plen, sizeof(cache->path) - plen,
			 "/ev2img");
	if (pplen >= (sizeof(cache->path) - plen)) {
		errno = ENAMETOOLONG;
		goto err1;
	}
	cache->ev2img = bmhash_open(cache->path, O_RDWR|O_CREAT, 65599,
							BMHASH_FN_FNV, 7);
	if (!cache->ev2img) {
		goto err1;
	}

	/* bmvec (headers) */
	pplen = snprintf(cache->path + plen, sizeof(cache->path) - plen,
			 "/img_hdr");
	if (pplen >= (sizeof(cache->path) - plen)) {
		errno = ENAMETOOLONG;
		goto err1;
	}
	cache->img_hdr = bmvec_generic_open(cache->path);
	if (!cache->img_hdr)
		goto err1;

	/* bmvec (segments) */
	pplen = snprintf(cache->path + plen, sizeof(cache->path) - plen,
			 "/img_seg");
	if (pplen >= (sizeof(cache->path) - plen)) {
		errno = ENAMETOOLONG;
		goto err1;
	}
	cache->img_seg = bmvec_generic_open(cache->path);
	if (!cache->img_seg)
		goto err1;

	return cache;

	/* error handling */
err1:
	bassocimg_cache_close_free(cache);
err0:
	return NULL;
}

void bassocimg_cache_close_free(bassocimg_cache_t cache)
{
	if (cache->ev2img)
		bmhash_close_free(cache->ev2img);
	if (cache->img_hdr)
		bmvec_generic_close_free(cache->img_hdr);
	if (cache->img_seg)
		bmvec_generic_close_free(cache->img_seg);
	free(cache);
}

static
uint64_t  __bassocimg_cache_alloc_img(bassocimg_cache_t cache)
{
	int rc;
	uint64_t ref = bmvec_generic_get_len(cache->img_hdr);
	if (ref == 0)
		ref = 1;
	rc = bmvec_generic_set(cache->img_hdr, ref,
				&__empty_hdr, sizeof(__empty_hdr));
	if (rc) {
		errno = rc;
		return 0;
	}
	return ref;
}

bassocimg_t __bassocimg_cache_get_img_by_ref(bassocimg_cache_t cache,
								uint64_t ref)
{
	if (!ref)
		return NULL;
	bassocimg_t ihandle = calloc(1, sizeof(*ihandle));
	if (!ihandle)
		return NULL;
	ihandle->hdr_ref = ref;
	ihandle->cache = cache;
	return ihandle;
}

bassocimg_t bassocimg_cache_get_img(bassocimg_cache_t cache,
					const struct bstr *name, int create)
{
	struct bmhash_entry *hent;
	bassocimg_t img = NULL;
	bassocimg_hdr_t hdr = NULL;
	bassocimg_t ihandle = NULL;

	if (name->blen > BASSOCIMG_NAME_MAX) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	hent = bmhash_entry_get(cache->ev2img, name);
	if (!hent) {
		if (!create) {
			errno = ENOENT;
			return NULL;
		}
		uint64_t hdr_ref = __bassocimg_cache_alloc_img(cache);
		/* then, put into hash */
		hent = bmhash_entry_set(cache->ev2img, name, hdr_ref);
		if (!hent)
			return NULL;
		/* set the name */
		hdr = bassocimg_cache_hdr_ref2ptr(cache, hent->value);
		hdr->name.blen = name->blen;
		memcpy(hdr->name.cstr, name->cstr, name->blen);
	}

	return __bassocimg_cache_get_img_by_ref(cache, hent->value);
}

void bassocimg_iter_init(bassocimg_iter_t iter, bassocimg_t img)
{
	iter->img = img;
	iter->seg_ref = BASSOCIMG_HDR(img)->first_seg_ref;
	iter->seg_idx = 0;
}

bassocimg_pixel_t bassocimg_iter_first(bassocimg_iter_t iter)
{
	bassocimg_hdr_t hdr = BASSOCIMG_HDR(iter->img);
	iter->seg_idx = 0;
	iter->seg_ref = hdr->first_seg_ref;
	iter->seg_ptr = bassocimg_seg_ref2ptr(iter->img, iter->seg_ref);
	if (!iter->seg_ptr)
		return NULL;
	if (iter->seg_idx >= iter->seg_ptr->len)
		return NULL;
	return &iter->seg_ptr->pxl[iter->seg_idx];
}

bassocimg_pixel_t bassocimg_iter_next(bassocimg_iter_t iter)
{
	iter->seg_idx++;
again:
	if (iter->seg_idx < iter->seg_ptr->len) {
		/* good */
		return &iter->seg_ptr->pxl[iter->seg_idx];
	}

	/* else, end of the segment */
	iter->seg_idx = 0;
	iter->seg_ref = iter->seg_ptr->next_seg_ref;
	iter->seg_ptr = bassocimg_seg_ref2ptr(iter->img, iter->seg_ref);
	if (!iter->seg_ref)
		return NULL;
	goto again;
}

bassocimg_seg_t bassocimg_seg_alloc(bassocimg_t img)
{
	bassocimg_t _img;
	bassocimg_hdr_t hdr;
	bassocimg_seg_t seg;
	bassocimg_seg_t last_seg;
	uint64_t seg_ref;
	int rc;
	struct bmvec_char *img_seg = img->cache->img_seg;
	seg_ref = bmvec_generic_get_len(img_seg);
	if (seg_ref == 0)
		seg_ref = 1;
	rc = bmvec_generic_set(img_seg, seg_ref,
		&__empty_seg, sizeof(__empty_seg));
	if (rc) {
		errno = rc;
		return NULL;
	}
	hdr = BASSOCIMG_HDR(img);
	if (!hdr->first_seg_ref)
		hdr->first_seg_ref = seg_ref;
	seg = bmvec_generic_get(img_seg, seg_ref, sizeof(*seg));
	seg->prev_seg_ref = hdr->last_seg_ref;
	last_seg = bassocimg_seg_ref2ptr(img, hdr->last_seg_ref);
	if (last_seg) {
		last_seg->next_seg_ref = seg_ref;
	}
	hdr->last_seg_ref = seg_ref;
	hdr->curr_seg_ref = seg_ref;
	hdr->seg_count++;
	return seg;
}

int bassocimg_append_pixel(bassocimg_t img, bassocimg_pixel_t p)
{
	int rc;
	bassocimg_hdr_t hdr;
	bassocimg_seg_t seg;
	struct bassocimg_pixel *lp = bassocimg_get_last_pixel(img);
	if (lp && BASSOCIMG_PIXEL_CMP(lp, p, >=)) {
		berr("Appending pixel out of order");
		return EINVAL;
	}

again:
	hdr = BASSOCIMG_HDR(img);
	seg = bassocimg_seg_ref2ptr(img, hdr->curr_seg_ref);
	if (!seg) {
		seg = bassocimg_seg_alloc(img);
		if (!seg)
			return errno;
	}
	if (seg->len == BASSOCIMG_SEGMENT_PXL) {
		/* current segment full, use next segment */
		hdr->curr_seg_ref = seg->next_seg_ref;
		goto again;
	}
	assert(seg->len < BASSOCIMG_SEGMENT_PXL);
	/* spaces available in current segment */
	seg->pxl[seg->len] = *p;
	seg->len++;
	hdr->len++;
	hdr->count += p->count;
	return 0;

}

void bassocimg_reset(bassocimg_t img)
{
	bassocimg_seg_t seg;
	bassocimg_hdr_t hdr = BASSOCIMG_HDR(img);
	hdr->len = 0;
	hdr->count = 0;
	hdr->curr_seg_ref = hdr->first_seg_ref;
	/* resetting all segments */
	BASSOCIMG_SEG_FOREACH(img, seg) {
		seg->len = 0;
	}
}

void bassocimg_put(bassocimg_t img)
{
	free(img);
}

int bassocimg_intersect(struct bassocimg *img0, struct bassocimg *img1,
						struct bassocimg *result)
{
	int rc, cmp;
	struct bassocimg_iter i0, i1;
	struct bassocimg_pixel *p0, *p1;
	struct bassocimg_pixel p;

	bassocimg_iter_init(&i0, img0);
	bassocimg_iter_init(&i1, img1);

	bassocimg_reset(result);
	p0 = bassocimg_iter_first(&i0);
	p1 = bassocimg_iter_first(&i1);
	while (p0 && p1) {
		cmp = bassocimg_pixel_cmp(p0, p1);
		if (cmp < 0) {
			p0 = bassocimg_iter_next(&i0);
			continue;
		}

		if (cmp > 0) {
			p1 = bassocimg_iter_next(&i1);
			continue;
		}
		p.sec = p0->sec;
		p.comp_id = p0->comp_id;
		p.count = (p0->count < p1->count)?(p0->count):(p1->count);
		rc = bassocimg_append_pixel(result, &p);
		if (rc)
			return rc;
		p0 = bassocimg_iter_next(&i0);
		p1 = bassocimg_iter_next(&i1);
	}
	return 0;
}

int bassocimg_shift_ts(struct bassocimg *img, int sec, struct bassocimg *result)
{
	struct bassocimg_pixel p;
	struct bassocimg_iter i0;
	struct bassocimg_pixel *p0;
	int n = bassocimg_get_pixel_len(img);
	int rc;
	int i;
	bassocimg_reset(result);
	bassocimg_iter_init(&i0, img);

	for (p0 = bassocimg_iter_first(&i0);
			p0; p0 = bassocimg_iter_next(&i0)) {
		p.sec = p0->sec + sec;
		p.comp_id = p0->comp_id;
		p.count = p0->count;
		rc = bassocimg_append_pixel(result, &p);
		if (rc)
			return rc;
	}
	return 0;
}

void bassocimg_cache_iter_init(bassocimg_cache_iter_t iter,
					bassocimg_cache_t cache)
{
	iter->cache = cache;
	bmhash_iter_init(&iter->bmh_iter, cache->ev2img);
}

bassocimg_t bassocimg_cache_iter_first(bassocimg_cache_iter_t iter)
{
	int rc;
	bassocimg_t img;
	struct bmhash_entry *hent;
	rc = bmhash_iter_begin(&iter->bmh_iter);
	if (rc) {
		errno = rc;
		return NULL;
	}
	hent = bmhash_iter_entry(&iter->bmh_iter);
	img = __bassocimg_cache_get_img_by_ref(iter->cache, hent->value);
	return img;
}

bassocimg_t bassocimg_cache_iter_next(bassocimg_cache_iter_t iter)
{
	int rc;
	bassocimg_t img;
	struct bmhash_entry *hent;
	rc = bmhash_iter_next(&iter->bmh_iter);
	if (rc) {
		errno = rc;
		return NULL;
	}
	hent = bmhash_iter_entry(&iter->bmh_iter);
	img = __bassocimg_cache_get_img_by_ref(iter->cache, hent->value);
	return img;
}

void bassocimg_dump(bassocimg_t img, int print_seg, int print_pixel)
{
	bassocimg_hdr_t hdr = BASSOCIMG_HDR(img);
	printf("--- %.*s ---\n", hdr->name.blen, hdr->name.cstr);
	printf("first_seg_ref: %lu\n", hdr->first_seg_ref);
	printf("last_seg_ref: %lu\n", hdr->last_seg_ref);
	printf("curr_seg_ref: %lu\n", hdr->curr_seg_ref);
	printf("seg_count: %lu\n", hdr->seg_count);
	printf("len: %lu\n", hdr->len);
	printf("count: %lu\n", hdr->len);

	if (print_seg) {
		uint64_t segref = hdr->first_seg_ref;
		int seg_count = 0;
		bassocimg_seg_t seg;
		while (segref) {
			seg_count++;
			seg = bmvec_generic_get(img->cache->img_seg,
						segref, sizeof(*seg));
			printf("    %lu(%lu, %lu): %lu\n", segref,
						seg->prev_seg_ref,
						seg->next_seg_ref,
						seg->len);
			segref = seg->next_seg_ref;
		}
		assert(hdr->seg_count == seg_count);
	}

	if (print_pixel) {
		struct bassocimg_iter itr;
		bassocimg_pixel_t pxl;
		bassocimg_iter_init(&itr, img);
		for (pxl = bassocimg_iter_first(&itr);
				pxl;
				pxl = bassocimg_iter_next(&itr)) {
			printf("[%lu, %lu, %lu]\n", pxl->sec,
						pxl->comp_id, pxl->count);
		}
	}
skip:
	printf("---------------\n");
}
