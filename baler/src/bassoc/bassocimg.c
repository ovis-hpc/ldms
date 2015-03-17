/**
 * \file bassocimg.c
 * \author Narate Taerat (narate at ogc dot us)
 */

#include <errno.h>

#include "baler/butils.h"
#include "bassocimg.h"

#define BASSOCIMG_HDR_SZ 64

struct bassocimg *bassocimg_open(const char *path, int create)
{
	int rc;
	struct bassocimg *img = NULL;
	if (!bfile_exists(path) && !create) {
		errno = ENOENT;
		return NULL;
	}
	img = (void*)bmvec_generic_open(path);
	if (!img)
		return NULL;

	if (!img->bmvec.bvec->len) {
		/* This is an uninitialized file */
		struct bassocimg_pixel p = {0};
		rc = bmvec_generic_set(&img->bmvec, PIXEL_IDX_OFFSET - 1,
				&p, sizeof(p));
		if (rc) {
			errno = rc;
			bassocimg_close_free(img);
			return NULL;
		}
		struct bassocimg_hdr *hdr = bassocimg_get_hdr(img);
		hdr->count = 0;
	}

	return img;
}

void bassocimg_close_free(struct bassocimg *img)
{
	bmvec_generic_close_free(&img->bmvec);
}

int bassocimg_intersect(struct bassocimg *img0, struct bassocimg *img1,
						struct bassocimg *result)
{
	int i, j, m, n, rc;
	struct bassocimg_pixel *p0, *p1;
	struct bassocimg_pixel p;
	m = bassocimg_get_pixel_len(img0);
	n = bassocimg_get_pixel_len(img1);
	i = 0;
	j = 0;
	bassocimg_reset(result);
	while (i < m && j < n) {
		p0 = bassocimg_get_pixel(img0, i);
		p1 = bassocimg_get_pixel(img1, j);
		if (p0->sec_comp_id < p1->sec_comp_id) {
			i++;
			continue;
		}

		if (p0->sec_comp_id > p1->sec_comp_id) {
			j++;
			continue;
		}
		p.sec_comp_id = p0->sec_comp_id;
		p.count = (p0->count < p1->count)?(p0->count):(p1->count);
		rc = bassocimg_append_pixel(result, &p);
		if (rc)
			return rc;
		i++;
		j++;
	}
	return 0;
}

int bassocimg_shift_ts(struct bassocimg *img, int sec, struct bassocimg *result)
{
	struct bassocimg_pixel p;
	struct bassocimg_pixel *p0;
	int n = bassocimg_get_pixel_len(img);
	int rc;
	int i;
	for (i = 0; i < n; i++) {
		p0 = bassocimg_get_pixel(img, i);
		p.sec = p0->sec + sec;
		p.comp_id = p0->comp_id;
		p.count = p0->count;
		rc = bassocimg_append_pixel(result, &p);
		if (rc)
			return rc;
	}
	return 0;
}
