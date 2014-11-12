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
/**
 * \file sos_archive.c
 * \author Narate Taerat (narate at ogc dot us)
 * \brief Remove indices and pack main ODS of the given SOS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>

#include "sos.h"
#include "ods.h"
#include "sos_priv.h"
#include "ods_priv.h"

const char *short_options = "s:h?";

struct option long_options[] = {
	{"store",  required_argument,  0,  's'},
	{"help",   no_argument,        0,  '?'},
	{0,        0,                  0,  0}
};

const char *sos_path = NULL;

const char *usage_str = "\n\
Synopsis: Remove indices and pack the main ODS of the given SOS.\n\
\n\
Usage: sos_archive -s <SOS_PATH>\n\
\n\
";

void usage()
{
	printf("%s\n", usage_str);
	_exit(-1);
}

void handle_args(int argc, char **argv)
{
	char o;
loop:
	o = getopt_long(argc, argv, short_options, long_options, NULL);
	switch (o) {
	case -1:
		goto out;
	case 's':
		sos_path = optarg;
		break;
	case '?':
	default:
		usage();
	}
	goto loop;
out:
	return;
}

char path[PATH_MAX];

int main(int argc, char **argv)
{
	ods_t ods;
	sos_meta_t meta;
	size_t meta_sz;
	int i, rc;
	handle_args(argc, argv);
	if (!sos_path)
		usage();
	snprintf(path, sizeof(path), "%s_sos", sos_path);
	ods = ods_open(path, O_RDWR);
	if (!ods) {
		fprintf(stderr, "ERROR: Could not open the sos store '%s'. "
				"The given path might not be a SOS store path.\n",
				sos_path);
		_exit(-1);
	}
	meta = ods_get_user_data(ods, &meta_sz);
	for (i = 0; i < meta->attr_cnt; i++) {
		snprintf(path, sizeof(path), "%s_%s", sos_path,
							meta->attrs[i].name);
		rc = ods_unlink(path);
		if (rc == ENOENT)
			continue; /* ignore no entry error */
		if (rc) {
			fprintf(stderr, "ERROR: ods_unlink: %d\n", rc);
			_exit(-1);
		}
	}

	rc = ods_pack(ods);
	if (rc) {
		fprintf(stderr, "ERROR: ods_pack: %d\n", rc);
		_exit(-1);
	}

	return 0;
}
