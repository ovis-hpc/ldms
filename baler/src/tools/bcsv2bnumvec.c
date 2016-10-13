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
/**
 * \file bcsv2bnumvec.c
 * \author Narate Taerat (narate at ogc dot us)
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>

#include "baler/btypes.h"
#include "baler/butils.h"
#include "baler/bmvec.h"

#include "bnum.h"

const char *out_dir = NULL;

const char *short_opt = "d:?";
struct option long_opt[] = {
	{"out-dir",  1,  0,  'd'},
	{0,      0,  0,  0},
};

void usage()
{
	printf("\nUsage: bcsv2bnumvec -d OUTPUT_DIR < INPUT_CSV\n\n");
}

void handle_args(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case -1:
		goto out;
	case 'd':
		out_dir = optarg;
		break;
	case '?':
	default:
		usage();
		exit(-1);
	}
	goto loop;
out:
	return;
}

void prep_out_dir(const char *dir)
{
	int rc;
	rc = bmkdir_p(dir, 0755);
	if (rc && rc != EEXIST) {
		berr("bmkdir_p() rc(%d): %s", rc, brcstr(rc));
		exit(-1);
	}
}

int process_csv_header(const char *str, struct bstr_list_head *h, int *count)
{
	int rc = 0;
	const char *s0, *s1;
	struct bstr_list_entry tail = {0};
	*count = 0;
	/* dummy tail */
	LIST_INSERT_HEAD(h, &tail, link);
	s0 = str;
	while (0 == (rc = bcsv_get_cell(s0, &s1))) {
		/* skip leading spaces */
		while (isspace(*s0) && s0 < s1)
			s0++;

		struct bstr_list_entry *s = bstr_list_entry_alloci(s1 - s0, s0);
		if (!s) {
			rc = ENOMEM;
			goto out;
		}
		(*count)++;
		LIST_INSERT_BEFORE(&tail, s, link);
		s0 = s1;
		if (*s0 == ',')
			s0++;
	}
	if (rc == ENOENT && (*count))
		rc = 0;
out:
	/* remove dummy tail */
	LIST_REMOVE(&tail, link);
	return rc;
}

void process_input()
{
	struct bdstr *bdstr = bdstr_new(4096);
	struct bstr_list_head hdr = {0};
	struct bstr_list_entry *bsent;
	char *ptr;
	const char *s0, *s1;
	int rc, count, i;
	struct bmvec_char **numvec;
	uint64_t line;

	if (!bdstr) {
		berror("bdstr_new()");
		exit(-1);
	}

	/* Expect header first */
	rc = bgetline(stdin, bdstr);
	if (rc) {
		berrorrc("bgetline()", rc);
		exit(-1);
	}

	rc = process_csv_header(bdstr->str, &hdr, &count);
	if (rc) {
		berrorrc("process_csv_header()", rc);
		exit(-1);
	}

	numvec = calloc(count, sizeof(*numvec));
	if (!numvec) {
		berror("calloc()");
		exit(-1);
	}

	i = 0 ;

	LIST_FOREACH(bsent, &hdr, link) {
		bdstr_reset(bdstr);
		rc = bdstr_append_printf(bdstr, "%s/%.*s",
						out_dir,
						bsent->str.blen,
						bsent->str.cstr);
		if (rc) {
			berrorrc("bdstr_append_printf()", rc);
			exit(-1);
		}
		numvec[i] = bmvec_generic_open(bdstr->str);
		if (!numvec[i]) {
			berror("bmvec_generic_open()");
			exit(-1);
		}
		i++;
	}

	line = 1;
	while (0 == (rc = bgetline(stdin, bdstr))) {
		s0 = bdstr->str;
		i = 0;
		while (0 == (rc = bcsv_get_cell(s0, &s1))) {
			struct bmvec_char *bmvec = numvec[i];
			struct bnum num;
			sscanf(s0, "%ld", &num.i64);
			sscanf(s0, "%lf", &num.d);
			rc = bmvec_generic_append(bmvec, &num, sizeof(num));
			if (rc) {
				berrorrc("bmvec_generic_append()", rc);
				exit(-1);
			}
			s0 = s1;
			if (*s0 == ',')
				s0++;
			i++;
		}
		if (i != count) {
			berr("Input error line %d: "
				"expecting %d columns, "
				"but got only %d columns",
						line, count, i);
			exit(-1);
		}
		line++;
	}
	if (rc != ENOENT) {
		berrorrc("bgetline()", rc);
		exit(-1);
	}
}

int main(int argc, char **argv)
{
	handle_args(argc, argv);
	if (!out_dir) {
		berr("Output director is not specified");
		usage();
		exit(-1);
	}
	prep_out_dir(out_dir);
	process_input();
}
