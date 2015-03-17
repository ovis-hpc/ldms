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
#include "baler/bmvec.h"
#include "baler/bmapper.h"

struct bassocimg_pixel {
	union {
		struct {
			uint32_t comp_id;
			uint32_t sec;
		};
		uint64_t sec_comp_id;
	};
	uint32_t count;
};

struct bassocimg_hdr {
	union {
		struct {
			uint64_t count;
		};
		char _space_[256];
	};
};

struct bassocimg {
	struct bmvec_char bmvec;
};

static
const size_t PIXEL_IDX_OFFSET = 1 +
		sizeof(struct bassocimg_hdr)/sizeof(struct bassocimg_pixel);

/**
 * Open/create ::bassocimg.
 *
 * \param path Path of the image.
 * \param create A boolean determining if the file should be created, if it is
 *               not exist.
 *
 * \note If \c create is not 0, but the file exists, this function will just
 * open the image file.
 *
 * \retval NULL if error, \c errno is also set properly.
 * \retval ptr The pointer to ::bassocimg.
 */
struct bassocimg *bassocimg_open(const char *path, int create);

/**
 * Close and free \c img.
 */
void bassocimg_close_free(struct bassocimg *img);

/**
 * \retval n The number of pixels in the image \c img.
 */
static inline
int bassocimg_get_pixel_len(struct bassocimg *img)
{
	return img->bmvec.bvec->len - PIXEL_IDX_OFFSET;
}

static inline
struct bassocimg_hdr *bassocimg_get_hdr(struct bassocimg *img)
{
	/* pixel 0 to pixel PIXEL_IDX_OFFSET - 1 are memory area for header */
	return bmvec_generic_get(&img->bmvec, 0, sizeof(struct bassocimg_pixel));
}

static inline
void bassocimg_reset(struct bassocimg *img)
{
	struct bassocimg_hdr *hdr = bassocimg_get_hdr(img);
	hdr->count = 0;
	img->bmvec.bvec->len = PIXEL_IDX_OFFSET;
}

/**
 * Get ::bassocimg_pixel reference from given \c img and pixel index \c idx.
 *
 * \param img The image handle.
 * \param idx The pixel index.
 *
 * \retval ptr The reference to the pixel, if the given \c idx is valid.
 * \retval NULL if the given \c idx is invalid.
 */
static inline
struct bassocimg_pixel *bassocimg_get_pixel(struct bassocimg *img, int idx)
{
	return bmvec_generic_get(&img->bmvec, idx + PIXEL_IDX_OFFSET,
					sizeof(struct bassocimg_pixel));
}

static inline
struct bassocimg_pixel *bassocimg_get_last_pixel(struct bassocimg *img)
{
	if (img->bmvec.bvec->len > PIXEL_IDX_OFFSET)
		return bmvec_generic_get(&img->bmvec, img->bmvec.bvec->len - 1,
						sizeof(struct bassocimg_pixel));
	return NULL;
}

/**
 * Append a pixel \c p to the image \c img.
 *
 * \retval 0 if no error.
 * \retval errno if error.
 */
static inline
int bassocimg_append_pixel(struct bassocimg *img, struct bassocimg_pixel *p)
{
	int rc;
	struct bassocimg_pixel *lp = bassocimg_get_last_pixel(img);
	if (lp && lp->sec_comp_id >= p->sec_comp_id) {
		bwarn("Appending pixel out of order");
		assert(0);
		return EINVAL;
	}
	rc = bmvec_generic_set(&img->bmvec, img->bmvec.bvec->len, p,
								sizeof(*p));
	if (rc)
		return rc;
	struct bassocimg_hdr *hdr = bassocimg_get_hdr(img);
	hdr->count += p->count;
	return 0;
}

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
	struct bassocimg_pixel *lp = bassocimg_get_last_pixel(img);
	struct bassocimg_hdr *hdr;
	if (!lp) {
		goto new_pixel;
	}
	if (lp->sec_comp_id > p->sec_comp_id) {
		bwarn("Appending pixel out of order");
		assert(0);
		return EINVAL;
	}
	if (lp->sec_comp_id == p->sec_comp_id) {
		lp->count += p->count;
		goto update_count;
	}

new_pixel:
	rc = bmvec_generic_set(&img->bmvec, img->bmvec.bvec->len, p, sizeof(*p));
	if (rc)
		return rc;
update_count:
	hdr = bassocimg_get_hdr(img);
	hdr->count += p->count;
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

static inline
const char *bassocimg_get_path(struct bassocimg *img)
{
	return img->bmvec.mem->path;
}

#endif
