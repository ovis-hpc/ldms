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

#include <stdio.h>
#include <stdlib.h>
#include "bassoc.h"

void usage()
{
	printf("Usage: bassocimg_dump CACHE IMG_NAME\n");
}

int main(int argc, char **argv)
{
	int i, n, rc;
	uint64_t idx;
	struct bassocimg_cache *cache;
	struct bassocimg *img;
	struct bassocimg_pixel *first_pxl;
	struct bassocimg_pixel *pxl;
	struct bassocimg_hdr *imghdr;
	struct bassocimg_iter itr;
	struct bstr *name;
	if (argc != 3) {
		usage();
		exit(-1);
	}

	if (!bis_dir(argv[1])) {
		berr("'%s' not found or not a cache dir.", argv[1]);
		exit(-1);
	}

	cache = bassocimg_cache_open(argv[1], 0);
	if (!cache) {
		berr("Cannot open cache %s, error(%d): %m", argv[1], errno);
		exit(-1);
	}

	name = bstr_alloc_init_cstr(argv[2]);
	img = bassocimg_cache_get_img(cache, name, 0);
	if (!img) {
		berr("Image not found: %s", argv[2]);
		exit(-1);
	}

	printf("Total count: %lu\n", BASSOCIMG_HDR(img)->count);
	printf("# of pixels: %lu\n", BASSOCIMG_HDR(img)->len);
	bassocimg_iter_init(&itr, img);
	for (pxl = bassocimg_iter_first(&itr);
			pxl; pxl = bassocimg_iter_next(&itr)) {
		printf("[%lu,%lu,%lu]\n", pxl->sec, pxl->comp_id, pxl->count);
	}
	return 0;
}
