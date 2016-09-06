/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
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
 * \file bassocimg_cache_dump.c
 * \author Narate Taerat (narate at ogc dot us)
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include "bassoc.h"

int print_seg = 0;
int print_pixel = 0;
const char *cache_path = NULL;

const char *short_opt = "c:sp";
struct option long_opt[] = {
	{"cache",  1,  0,  'c'},
	{"seg",    0,  0,  's'},
	{"pixel",  0,  0,  'p'},
	{0,        0,  0,  0}
};

void usage()
{
	printf("Usage: bassocimg_cache_dump -c CACHE [-s] [-p]\n");
}

void handle_args(int argc, char **argv)
{
	int c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case -1:
		return;
	case 's':
		print_seg = 1;
		break;
	case 'p':
		print_pixel = 1;
		break;
	case 'c':
		cache_path = optarg;
		break;
	default:
		usage();
		exit(-1);
	}
	goto loop;
}

int main(int argc, char **argv)
{
	int i, n, rc;
	uint64_t idx;
	struct bassocimg_cache *cache;
	struct bassocimg *img;
	struct bstr *name;

	handle_args(argc, argv);

	cache = bassocimg_cache_open(cache_path, 0);
	if (!cache) {
		berr("Cannot open cache %s, error(%d): %m", cache_path, errno);
		exit(-1);
	}

	struct bassocimg_cache_iter iter;

	bassocimg_cache_iter_init(&iter, cache);

	for (img = bassocimg_cache_iter_first(&iter);
			img;
			img = bassocimg_cache_iter_next(&iter)) {
		bassocimg_dump(img, print_seg, print_pixel);
		bassocimg_put(img);
	}

	return 0;
}
