/*
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014 Sandia Corporation. All rights reserved.
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
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>

#include "ods.h"

const char *short_options = "s:h?";

struct option long_options[] = {
	{"store",  required_argument,  0,  's'},
	{"help",   no_argument,        0,  '?'},
	{0,        0,                  0,  0}
};

const char *ods_path = NULL;

void usage()
{
	printf("Usage: ods_verify -s ODS_PATH\n");
	_exit(-1);
}

void handle_args(int argc, char **argv)
{
	char o;
loop:
	o = getopt_long(argc, argv, short_options, long_options, NULL);
	switch (o) {
	case 's':
		ods_path = optarg;
		break;
	case -1:
		goto out;
	case '?':
	default:
		usage();
	}
	goto loop;
out:
	return;
}

int main(int argc, char **argv)
{
	int rc;
	ods_t ods;
	handle_args(argc, argv);
	if (!ods_path) {
		fprintf(stderr, "ERROR: No ODS_PATH specified\n");
		usage();
	}
	ods = ods_open(ods_path, O_RDWR);
	if (!ods) {
		fprintf(stderr, "ERROR: Cannot open ods: %s\n", ods_path);
		_exit(-1);
	}
	rc = ods_verify(ods);
	if (rc) {
		printf("Failed\n");
		_exit(-1);
	} else {
		printf("Success\n");
	}
	return 0;
}
