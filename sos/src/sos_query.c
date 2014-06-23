/*
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
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

/*
 * Author: Tom Tucker tom at ogc dot us
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <time.h>
#include <endian.h>

#include "sos.h"

#define FMT "s:k:v:i"
void usage(int argc, char *argv[])
{
	printf("usage: %s -s <path> [-k <key_column>] [-k <key_value>]\n"
	       "        -s <path>          - The path to the object store\n"
	       "        -k <key_column>    - The column index for the key (default 0)\n"
	       "        -v <key value>     - The key value.\n"
	       "        -i                 - Show the object store meta data.\n",
	       argv[0]);
	exit(1);
}

int records = 0;

void print_record(FILE *fp, sos_t sos, sos_obj_t obj, int col_count, int *width)
{
	uint32_t vu32;
	uint64_t vu64;
	int32_t v32;
	int64_t v64;
	double vd;
	char *s;
	int col;
	int n;
	sos_blob_obj_t blob;

	for (col = 0; col < col_count; col++) {
		enum sos_type_e vtype = sos_get_attr_type(sos, col);
		switch (vtype) {
		case SOS_TYPE_INT32:
			SOS_OBJ_ATTR_GET(v32, sos, col, obj);
			fprintf(fp, "%*" PRIi32 "", width[col], v32);
			break;
		case SOS_TYPE_INT64:
			SOS_OBJ_ATTR_GET(v64, sos, col, obj);
			fprintf(fp, "%*" PRIi64 "", width[col], v64);
			break;
		case SOS_TYPE_UINT32:
			SOS_OBJ_ATTR_GET(vu32, sos, col, obj);
			fprintf(fp, "%*u", width[col], vu32);
			break;
		case SOS_TYPE_UINT64:
			SOS_OBJ_ATTR_GET(vu64, sos, col, obj);
			fprintf(fp, "%*" PRIu64 "", width[col], vu64);
			break;
		case SOS_TYPE_STRING:
			s = sos_obj_attr_get(sos, col, obj);
			fprintf(fp, "%*s", width[col], s);
			break;
		case SOS_TYPE_BLOB:
			blob = sos_obj_attr_get(sos, col, obj);
			n = fprintf(fp, " %d:", blob->len);
			fprintf(fp, "%*s", width[col] - n, blob->data);
			break;
		case SOS_TYPE_DOUBLE:
			SOS_OBJ_ATTR_GET(vd, sos, col, obj);
			fprintf(fp, "%*f", width[col], vd);
			break;
		default:
			fprintf(stderr, "Unsupported type '%s'\n",
				sos_type_to_str(vtype));
			break;
		}
	}
	printf("\n");
}

void *get_key_value(sos_t sos, int col, char *sz)
{
	static uint32_t vu32;
	static uint64_t vu64;
	static int32_t v32;
	static int64_t v64;
	static double vd;

	enum sos_type_e vtype = sos_get_attr_type(sos, col);
	switch (vtype) {
	case SOS_TYPE_INT32:
		sscanf(sz, "%d", &v32);
		return &v32;
	case SOS_TYPE_INT64:
		sscanf(sz, "%" PRIi64 "", &v64);
		return &v64;
	case SOS_TYPE_UINT32:
		sscanf(sz, "%" PRIu32 "", &vu32);
		return &vu32;
	case SOS_TYPE_UINT64:
		sscanf(sz, "%" PRIu64 "", &vu64);
		return &vu64;
	case SOS_TYPE_DOUBLE:
		sscanf(sz, "%lf", &vd);
		return &vd;
	default:
		vu64 = -1;
		return &vu64;
	}
}


const char *type_name(enum sos_type_e vtype)
{
	switch (vtype) {
	case SOS_TYPE_INT32:
		return "INT32";
	case SOS_TYPE_INT64:
		return "INT64";
	case SOS_TYPE_UINT32:
		return "UINT32";
	case SOS_TYPE_UINT64:
		return "UINT64";
	case SOS_TYPE_DOUBLE:
		return "DOUBLE";
	case SOS_TYPE_BLOB:
		return "BLOB";
	case SOS_TYPE_USER:
		return "USER";
	case SOS_TYPE_UNKNOWN:
	default:
		return "UNKNOWN";
	}
}

void print_meta_data(sos_t sos)
{
	int col;
	int col_count = sos_get_attr_count(sos);
	printf("%-24s %-8s %-5s\n", "Name", "Type", "Index");
	printf("------------------------ -------- -----\n");
	for (col = 0; col < col_count; col++)
		printf("%-24s %-8s %5s\n",
		       sos_get_attr_name(sos, col),
		       type_name(sos_get_attr_type(sos, col)),
		       (sos_obj_attr_index(sos, col) ? "Yes" : "No"));
	printf("\n");
}

int main(int argc, char *argv[])
{
	extern int optind;
	extern char *optarg;
	obj_key_t key = obj_key_new(1024);
	sos_obj_t obj;
	sos_t sos;
	sos_iter_t iter;
	int key_col = 0;
	char *key_val = NULL;
	char *path = NULL;
	int op;
	int col;
	int col_count;
	int *col_width;
	const char **col_name;
	int meta_data = 0;
	int rc;

	opterr = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 's':
			path = strdup(optarg);
			break;
		case 'k':
			key_col = atoi(optarg);
			break;
		case 'v':
			key_val = strdup(optarg);
			break;
		case 'i':
			meta_data = 1;
			break;
		case '?':
		default:
			usage(argc, argv);
		}
	}
	if (!path || key_col < 0)
		usage(argc, argv);

	sos = sos_open(path, O_RDWR);
	if (!sos) {
		printf("Could not open the specified object store.\n");
		usage(argc, argv);
	}
	if (meta_data)
		print_meta_data(sos);

	col_count = sos_get_attr_count(sos);
	col_width = calloc(col_count, sizeof(int));
	col_name = calloc(col_count, sizeof(char *));
	for (col = 0; col < col_count; col++) {
		enum sos_type_e vtype = sos_get_attr_type(sos, col);
		const char *name = sos_get_attr_name(sos, col);
		int min;
		col_width[col] = strlen(name) + 1;
		switch (vtype) {
		case SOS_TYPE_INT32:
		case SOS_TYPE_UINT32:
			min = 12;
			break;
		case SOS_TYPE_INT64:
		case SOS_TYPE_UINT64:
			min = 24;
			break;
		case SOS_TYPE_DOUBLE:
			min = 16;
			break;
		default:
			min = 12;
			break;
		}
		if (col_width[col] < min)
			col_width[col] = min;
		col_name[col] = name;
		printf("%*s", col_width[col], name);
	}
	printf("\n");
	char heading[120];
	heading[0] = '\0';
	for (col = 0; col < col_count; col++) {
		int w;
		strcat(heading, " ");
		for (w = 0; w < col_width[col]-1; w++)
			strcat(heading, "-");
	}
	printf("%s\n", heading);

	iter = sos_iter_new(sos, key_col);
	if (!iter) {
		fprintf(stderr, "There is no index on column %d.\n", key_col);
		exit(3);
	}
	if (key_val) {
		sos_key_from_str(iter, key, key_val);
		rc = sos_iter_seek(iter, key);
		if (rc) {
			printf("The key '%s' was not found.\n", key_val);
			exit(4);
		}
	} else
		rc = sos_iter_begin(iter);
	for (; !rc; rc = sos_iter_next(iter)) {
		obj = sos_iter_obj(iter);
		obj_key_t iter_key = sos_iter_key(iter);
		if (key_val && sos_iter_key_cmp(iter, iter_key, key))
			break;
		records ++;
		print_record(stdout, sos, obj, col_count, col_width);
	}
	printf("%s\n", heading);
	printf("%d record(s).\n", records);
	return 0;
}
