/*
 * Copyright (c) 2013-14 Open Grid Computing, Inc. All rights reserved.
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

#define FMT "s:k:e:m:M:i"

void usage(int argc, char *argv[])
{
	printf(
"usage: %s -s <path> [-k <key_column>] [-e <exact_value>] [-m <min_value>] \n"
"                    [-M <max_value>] ...\n"
"        -s <path>          - The path to the object store\n"
"        -k <key_column>    - The column index for the key (default 0)\n"
"        -e <exact_value>   - Query with exact value of the last key.\n"
"        -m <min_value>     - Query with minimum value of the last key.\n"
"        -M <max_value>     - Query with maximum value of the last key.\n"
"        -i                 - Show the object store meta data.\n"
"\n"
"    The -k -e -m -M query conditions can be repeated for multiple attributes.\n"
"    The query results will satisfy all query conditions. The first attribute\n"
"    is used to create iterator. The other specified attributes and conditions\n"
"    are used for condition checking only.\n"
"\n",
	       argv[0]);
	exit(1);
}

typedef struct sos_cond_s {
	sos_attr_t attr;
	obj_key_t min;
	obj_key_t max;
	TAILQ_ENTRY(sos_cond_s) entry;
} *sos_cond_t;

TAILQ_HEAD(sos_cond_head, sos_cond_s);

obj_key_t key_buff = NULL;
int bufflen= 0;

struct sos_cond_head cond_head = TAILQ_HEAD_INITIALIZER(cond_head);

static int __sos_cond_single_test(sos_t sos, obj_ref_t ref, sos_cond_t cond)
{
	sos_attr_t attr = cond->attr;
	obj_key_t k = key_buff;
	size_t attr_sz;
	attr_sz = sos_obj_attr_size(sos, attr->id, sos_ref_to_obj(sos, ref));
	if (bufflen < attr_sz) {
		k = obj_key_new(attr_sz);
		if (!k)
			return ENOMEM;
		bufflen = attr_sz;
		obj_key_delete(key_buff);
		key_buff = k;
	}
	sos_attr_key(attr, sos_ref_to_obj(sos, ref), k);

	if (cond->min && sos_attr_key_cmp(attr, k, cond->min) < 0)
		return EINVAL;

	if (cond->max && sos_attr_key_cmp(attr, cond->max, k) < 0)
		return EINVAL;

	return 0;
}

static int sos_cond_test(sos_t sos, obj_ref_t ref, struct sos_cond_head h)
{
	int rc;
	sos_cond_t cond;
	TAILQ_FOREACH(cond, &h, entry) {
		rc = __sos_cond_single_test(sos, ref, cond);
		if (rc)
			return rc;
	}
	return 0;
}

int records = 0;

void print_blob(FILE *fp, int width, size_t len, unsigned char *blob)
{
	int i;
	for (i = 0; 0 < width && i < len; i++) {
		fprintf(fp, " %02X", blob[i]);
		width -= 2;
	}
}

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
			n = fprintf(fp, " %lu:", blob->len);
			print_blob(fp, width[col] - n, blob->len, blob->data);
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

void check_sos(sos_t sos)
{
	if (sos)
		return;
	fprintf(stderr, "ERROR: Please specify SOS first\n");
	_exit(-1);
}

#define SOS_QKSIZE 1024

int main(int argc, char *argv[])
{
	extern int optind;
	extern char *optarg;
	obj_key_t key = obj_key_new(1024);
	obj_ref_t ref;
	sos_t sos = NULL;
	sos_iter_t iter = NULL;
	int key_col = 0;
	char *path = NULL;
	sos_attr_t attr;
	sos_cond_t cond = NULL;
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
			path = optarg;
			sos = sos_open(path, O_RDWR);
			if (!sos) {
				fprintf(stderr, "ERROR: Cannot open sos: %s\n",
						path);
				_exit(-1);
			}
			break;
		case 'k':
			check_sos(sos);
			key_col = atoi(optarg);
			attr = sos_obj_attr_by_id(sos, key_col);
			if (!attr) {
				fprintf(stderr, "ERROR: Invalid attribute: %d\n",
						key_col);
				_exit(-1);
			}
			if (!attr->has_idx) {
				fprintf(stderr, "ERROR: attribute '%d' "
						"is not indexed.\n",
						key_col);
				_exit(-1);
			}
			cond = calloc(1, sizeof(*cond));
			if (!cond) {
				fprintf(stderr, "ERROR: Not enough memory\n");
				_exit(-1);
			}
			cond->attr = attr;
			TAILQ_INSERT_TAIL(&cond_head, cond, entry);
			break;
		case 'm':
		case 'M':
		case 'e':
			check_sos(sos);
			key = obj_key_new(SOS_QKSIZE);
			if (!key) {
				fprintf(stderr, "ERROR: Not enough memory\n");
				_exit(-1);
			}
			sos_attr_key_from_str(cond->attr, key, optarg);
			if (op != 'm') {
				cond->max = key;
			}
			if (op != 'M') {
				cond->min = key;
			}
			break;
		case 'i':
			meta_data = 1;
			break;
		case '?':
		default:
			usage(argc, argv);
		}
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

	cond = TAILQ_FIRST(&cond_head);
	if (cond) {
		iter = sos_iter_new(sos, cond->attr->id);
	} else {
		for (col = 0; col < col_count; col++) {
			attr = sos_obj_attr_by_id(sos, col);
			if (!attr->has_idx)
				continue;
			iter = sos_iter_new(sos, col);
			break;
		}
	}

	if (!iter) {
		fprintf(stderr, "There is no index on column %d.\n", key_col);
		exit(3);
	}

	if (cond && cond->min) {
		rc = sos_iter_seek_inf(iter, cond->min);
		if (rc) /* no inf */
			rc = sos_iter_begin(iter);
	} else {
		rc = sos_iter_begin(iter);
	}

	for (; !rc; rc = sos_iter_next(iter)) {
		ref = sos_iter_ref(iter);
		if (cond && cond->max && sos_iter_key_cmp(iter, cond->max) > 0)
			break;
		if (cond && sos_cond_test(sos, ref, cond_head) != 0)
			continue;
		records ++;
		print_record(stdout, sos, sos_ref_to_obj(sos, ref),
						col_count, col_width);
	}

	printf("%s\n", heading);
	printf("%d record(s).\n", records);
	return 0;
}
