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
#include "bassocimg.h"
#include "baler/butils.h"

void usage()
{
	printf("bassocimg_intersect_test CACHE NAME1 NAME2 OUT_CACHE NAME_OUT\n");
}

int main(int argc, char **argv)
{
	if (argc != 6) {
		usage();
		exit(-1);
	}

	const char *in_cache_path = argv[1];
	const char *out_cache_path = argv[4];
	struct bstr *in_name_1 = bstr_alloc_init_cstr(argv[2]);
	struct bstr *in_name_2 = bstr_alloc_init_cstr(argv[3]);
	struct bstr *out_name = bstr_alloc_init_cstr(argv[5]);

	struct bassocimg_cache *in_cache, *out_cache;

	in_cache = bassocimg_cache_open(in_cache_path, 0);
	if (!in_cache) {
		berr("Cannot open image cache %s", in_cache_path);
		exit(-1);
	}
	out_cache = bassocimg_cache_open(argv[4], 1);
	if (!out_cache) {
		berr("Cannot open image cache %s", out_cache_path);
		exit(-1);
	}

	bassocimg_t img1, img2;
	img1 = bassocimg_cache_get_img(in_cache, in_name_1, 0);
	if (!img1) {
		berr("Input image '%.*s' not found",  in_name_1->blen, in_name_1->cstr);
		exit(-1);
	}
	img2 = bassocimg_cache_get_img(in_cache, in_name_2, 0);
	if (!img1) {
		berr("Input image '%.*s' not found",  in_name_2->blen, in_name_2->cstr);
		exit(-1);
	}

	bassocimg_t out;
	out = bassocimg_cache_get_img(out_cache, out_name, 1);
	bassocimg_reset(out);

	bassocimg_intersect(img1, img2, out);

	return 0;
}
