/*
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
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
 * \file bpt_verify.c
 * \author Narate Taerat (narate at ogc dot us)
 * \brief Verify a given index and exit (exit code will tell the health of the
 * ree).
 */

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "bpt.h"

#define FMT "hs:v"

void usage()
{
	printf(
"Usage: bpt_verify [-v] -s <INDEX> \n"
"    where <INDEX> is the path to the index file (without .OBJ or .PG)\n"
"    The -v option is for verbose output.\n"
	      );
}

void check_file(const char *path)
{
	struct stat s;
	int rc;
	rc = stat(path, &s);
	if (rc) {
		printf("ERROR: Cannot stat file %s, errno: %d\n",
				path, errno);
		_exit(-1);
	}
}

int main(int argc, char **argv)
{
	char c;
	const char *path = NULL;
	char buff[4096];
	int verbose = 0;
	int rc;

	while ((c = getopt(argc, argv, FMT)) > -1) {
		switch (c) {
		case 's':
			path = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			_exit(-1);
		}
	}

	if (!path) {
		usage();
		_exit(-1);
	}

	/* check file existence */
	snprintf(buff, sizeof(buff), "%s.OBJ", path);
	check_file(buff);
	snprintf(buff, sizeof(buff), "%s.PG", path);
	check_file(buff);

	obj_idx_t idx = obj_idx_open(path);
	if (!idx) {
		printf("ERROR: Cannot open index: %s\n", path);
		_exit(-1);
	}

	rc = bpt_verify(idx, verbose);
	if (rc) {
		printf("ERROR: B+ Tree corrupted\n");
		_exit(-1);
	}

	return 0;
}
