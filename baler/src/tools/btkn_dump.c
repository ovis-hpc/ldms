/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
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
#include <getopt.h>
#include <string.h>

#include "baler/butils.h"
#include "baler/btkn.h"

const char *store_path = NULL;

const char *short_opt = "s:?";
struct option long_opt[] = {
	{"store",  1,  0,  'o'},
	{"help",   0,  0,  '?'},
	{0,        0,  0,  0}
};

void usage()
{
	printf("SYNOPSIS btkn_dump -s BTKN_DIR\n");
}

void handle_args(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case -1:
		goto out;
	case 's':
		store_path = optarg;
		break;
	case '?':
	default:
		usage();
		exit(-1);
	}
out:
	return;
}

static
int cb(uint32_t tkn_id, const struct bstr *bstr, const struct btkn_attr *attr)
{
	printf("%10u ", tkn_id);

	printf("%10s ", btkn_attr_type_str(attr->type));

	printf("%.*s ", bstr->blen, bstr->cstr);

	printf("\n");

	return 0;
}

int main(int argc, char **argv)
{
	handle_args(argc, argv);
	if (!store_path) {
		berr("-s PATH is needed");
		exit(-1);
	}
	if (!bfile_exists(store_path)) {
		berr("File not found: %s", store_path);
		exit(-1);
	}
	struct btkn_store *tkn_store = btkn_store_open(store_path, 0);
	if (!tkn_store) {
		berror("btkn_store_open()");
		exit(-1);
	}

	btkn_store_iterate(tkn_store, cb);

	return 0;
}
