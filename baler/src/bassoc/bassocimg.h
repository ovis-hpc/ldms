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
 * \file bassocimg.h
 * \author Narate Taerat (narate at ogc dot us)
 *
 * \brief Baler association rule mining image facility.
 *
 * This is an implementation of Baler Association Rule Mining (bassoc) Image.
 * It basically a wrapper of ::bmvec_char, with necessary interface functions.
 * A ::bassocimg is an array of ::bassocimg_pixel with a header at the beginning
 * of the ::bmvec. The pixels in the array are sorted by <sec, comp_id>.
 */
#ifndef __BASSOCIMG_H
#define __BASSOCIMG_H

#include <assert.h>
#include <stdarg.h>
#include <limits.h>
#include "baler/bmvec.h"
#include "baler/bmhash.h"
#include "baler/bmapper.h"

typedef struct bassocimg_cache {
	char path[PATH_MAX];
	struct bmhash *ev2img;
	struct bmvec_char *img_hdr;
	struct bmvec_char *img_seg;
} *bassocimg_cache_t;

typedef struct bassocimg_pixel {
	uint64_t comp_id;
	uint64_t sec;
	uint64_t count;
} *bassocimg_pixel_t;

typedef struct bassocimg_cache_iter {
	struct bmhash_iter bmh_iter;
	bassocimg_cache_t cache;
} *bassocimg_cache_iter_t;

#define BASSOCIMG_NAME_MAX 255

typedef struct bassocimg {
	uint64_t hdr_ref; /* reference to self */
	bassocimg_cache_t cache; /* handle to cache */
} *bassocimg_t;

typedef struct bassocimg_hdr {
	uint64_t count; /* total count of occurrences */
	uint64_t len; /* number of pixels */
	uint64_t seg_count;
	uint64_t first_seg_ref; /* ref to first segment */
	uint64_t curr_seg_ref; /* ref to current segment */
	uint64_t last_seg_ref; /* last to last segment */
	struct bstr name; /* img name */
	char _name_buff[BASSOCIMG_NAME_MAX+1]; /* buffer for the name */
} *bassocimg_hdr_t;

#define BASSOCIMG_SEGMENT_SZ 4096
typedef struct bassocimg_seg {
	union {
		struct {
			uint64_t prev_seg_ref; /* ref to previous segment */
			uint64_t next_seg_ref; /* ref to next segment */
			uint64_t len; /* number of occupied pxl in the segment */
			struct bassocimg_pixel pxl[0];
		};
		char ___[BASSOCIMG_SEGMENT_SZ];
	};
} *bassocimg_seg_t;

static const uint64_t BASSOCIMG_SEGMENT_PXL = (
		(BASSOCIMG_SEGMENT_SZ - offsetof(struct bassocimg_seg, pxl))
			/ sizeof(struct bassocimg_pixel)
	);

/**
 * iterator over image pixels.
 */
typedef struct bassocimg_iter {
	bassocimg_t img; /* image handle */
	uint64_t seg_ref; /* segment reference */
	uint64_t seg_idx; /* index in segment */
	bassocimg_seg_t seg_ptr; /* pointer to segment */
} *bassocimg_iter_t;

static inline
bassocimg_hdr_t BASSOCIMG_HDR(bassocimg_t img)
{
	bassocimg_hdr_t hdr = bmvec_generic_get(img->cache->img_hdr,
						img->hdr_ref, sizeof(*hdr));
	return hdr;
}

/**
 * Open/create ::bassocimg_cache.
 *
 * \param path Path of the image cache.
 * \param create A boolean flag to create the cache if it does not exist.
 *
 * \retval NULL if there is an error, \c errno is also set.
 * \retval handle if the image cache is opened successfully.
 */
bassocimg_cache_t bassocimg_cache_open(const char *path, int create);

/**
 * Close the ::bassocimg_cache, and free associated memory.
 *
 * \param cache The cache to be closed.
 */
void bassocimg_cache_close_free(bassocimg_cache_t cache);

static inline
bassocimg_seg_t bassocimg_cache_seg_ref2ptr(bassocimg_cache_t cache,
					    uint64_t seg_ref)
{
	if (!seg_ref)
		return NULL;
	return bmvec_generic_get(cache->img_seg, seg_ref,
						sizeof(struct bassocimg_seg));
}

static inline
bassocimg_hdr_t bassocimg_cache_hdr_ref2ptr(bassocimg_cache_t cache,
					    uint64_t hdr_ref)
{
	if (!hdr_ref)
		return NULL;
	return bmvec_generic_get(cache->img_hdr, hdr_ref,
						sizeof(struct bassocimg_hdr));
}

static inline
bassocimg_seg_t bassocimg_seg_ref2ptr(bassocimg_t img, uint64_t seg_ref)
{
	if (!seg_ref)
		return NULL;
	return bmvec_generic_get(img->cache->img_seg, seg_ref,
						sizeof(struct bassocimg_seg));
}

/**
 * Get or create an image in the cache.
 *
 * \param cache the cache handle.
 * \param name the image name.
 * \param create a boolean option, 1 to create new image (if not existed) or 0
 *                   to not create the image.
 *
 * \retval NULL if the image is not found or the image creation is failed.
 *              \c errno is set to describe the error.
 *
 * \retval img the image handle, if image retrieval or creation is a success.
 */
bassocimg_t bassocimg_cache_get_img(bassocimg_cache_t cache,
					const struct bstr *name, int create);

/**
 * The same as ::bassocimg_cache_get_img(), but return cache reference instead.
 *
 * \param cache the cache handle.
 * \param name the image name.
 * \param create a boolean option, 1 to create new image (if not existed) or 0
 *                   to not create the image.
 *
 * \retval 0 if the image is not found or the image creation is failed.
 *              \c errno is set to describe the error.
 *
 * \retval ref the image reference, if image retrieval or creation is a success.
 */
uint64_t bassocimg_cache_get_img_ref(bassocimg_cache_t cache,
					const struct bstr *name, int create);

/**
 * Initialize image iterator \c iter.
 */
void bassocimg_iter_init(bassocimg_iter_t iter, bassocimg_t img);

/**
 * Seek iterator to the first element.
 */
bassocimg_pixel_t bassocimg_iter_first(bassocimg_iter_t iter);

/**
 * Seek iterator to the next element.
 */
bassocimg_pixel_t bassocimg_iter_next(bassocimg_iter_t iter);

/**
 * Free the in-memory \c img.
 */
void bassocimg_put(struct bassocimg *img);

/**
 * \retval n The number of pixels in the image \c img.
 */
static inline
int bassocimg_get_pixel_len(struct bassocimg *img)
{
	return BASSOCIMG_HDR(img)->len;
}

/**
 * Reset image to BLANK.
 */
void bassocimg_reset(struct bassocimg *img);

static inline
bassocimg_pixel_t bassocimg_get_last_pixel(struct bassocimg *img)
{
	bassocimg_seg_t last_seg = bassocimg_seg_ref2ptr(img, BASSOCIMG_HDR(img)->last_seg_ref);
	if (last_seg && last_seg->len) {
		return &last_seg->pxl[last_seg->len-1];
	}
	return NULL;
}

static inline
int bassocimg_pixel_cmp(const struct bassocimg_pixel *p0,
			const struct bassocimg_pixel *p1)
{
	if (p0->sec < p1->sec)
		return -1;
	if (p0->sec > p1->sec)
		return 1;
	if (p0->comp_id < p1->comp_id)
		return -1;
	if (p0->comp_id > p1->comp_id)
		return 1;
	return 0;
}

#define BASSOCIMG_PIXEL_CMP(p0, p1, op) (bassocimg_pixel_cmp(p0, p1) op 0)

/**
 * Append a pixel \c p to the image \c img.
 *
 * \retval 0 if no error.
 * \retval errno if error.
 */
int bassocimg_append_pixel(struct bassocimg *img, struct bassocimg_pixel *p);

/**
 * Similar to ::bassocimg_append_pixel, but if the pixel \c p is at the same
 * location as the last pixel in the image, it will just increment the count.
 *
 * \retval 0 if no error.
 * \retval errno if error.
 */
static inline
int bassocimg_add_count(struct bassocimg *img, struct bassocimg_pixel *p)
{
	int rc;
	int cmp;
	bassocimg_hdr_t hdr = BASSOCIMG_HDR(img);
	struct bassocimg_pixel *lp = bassocimg_get_last_pixel(img);
	if (!lp) {
		goto new_pixel;
	}
	cmp = bassocimg_pixel_cmp(lp, p);
	if (cmp > 0) {
		bwarn("Appending pixel out of order");
		assert(0);
		return EINVAL;
	}
	if (cmp == 0) {
		lp->count += p->count;
		hdr->count += p->count;
		goto out;
	}

new_pixel:
	rc = bassocimg_append_pixel(img, p);
	if (rc)
		return rc;
out:
	return 0;
}

/**
 * Perform an image intersection of \c img0 and \c img1, and store the result
 * into \c result.
 *
 * \retval 0 if no error.
 * \retval errno if error.
 */
int bassocimg_intersect(struct bassocimg *img0, struct bassocimg *img1,
						struct bassocimg *result);

/**
 * Produce \c result image from \c img0, each pixel of which shifted by \c sec.
 *
 * \retval 0 if no error.
 * \retval errno if error.
 */
int bassocimg_shift_ts(struct bassocimg *img, int sec,
					struct bassocimg *result);

/**
 * Initialize the image cache iterator, the memory \c iter pointed at must be
 * valid.
 *
 * \param iter the pointer to ::bassocimg_cache_iter structure.
 * \param cache the handle to image cache.
 */
void bassocimg_cache_iter_init(bassocimg_cache_iter_t iter,
					bassocimg_cache_t cache);

/**
 * Start the iterator, returning the first image in the cache.
 *
 * \param iter the iterator handle.
 * \retval img the handle to the first image, if exists.
 * \retval NULL if there is no image.
 *
 * \note The returned \c img should be put back by calling \c bassocimg_put()
 *       when done.
 */
bassocimg_t bassocimg_cache_iter_first(bassocimg_cache_iter_t iter);

/**
 * Iterate to the next image to the cache.
 *
 * \param iter the iterator handle.
 * \retval img the handle to the next image, if exists.
 * \retval NULL if there is no more image.
 *
 * \note The returned \c img should be put back by calling \c bassocimg_put()
 *       when done.
 */
bassocimg_t bassocimg_cache_iter_next(bassocimg_cache_iter_t iter);

/**
 * Dump image information through \c stdout.
 *
 * \param img the image handle.
 * \param print_seg set this to 1 to also print image segment info.
 * \param print_pixel set this to 1 to also print pixel info.
 */
void bassocimg_dump(bassocimg_t img, int print_seg, int print_pixel);

#endif
