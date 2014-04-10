/*
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
#include "mds.h"

char *root_path;
char tmp_path[PATH_MAX];
int ts_convert_disable = 0;
int value_only = 0;

#define FMT "t:c:p:DTCqZV"
void usage(int argc, char *argv[])
{
	printf("usage: %s [OPTION]... {COMP_TYPE:METRIC}...\n"
	       "        -p <path>      - Path to files\n"
	       "        -c <comp_id>   - Specify component\n"
	       "        -t <time>      - Specify time\n"
	       "        -T             - Show results by Time\n"
	       "        -C             - Show results by Component\n"
	       "        -q             - Quiet output (no table headers and total report)\n"
	       "        -Z             - Disable timestamp conversion\n"
	       "        -V             - Print values only\n",
	       argv[0]);
	exit(1);
}

int records = 0;

void print_record(FILE *fp, sos_t sos, sos_obj_t obj, int delete)
{
	uint32_t tv_sec;
	uint32_t tv_usec;
	char t_s[128];
	char tv_s[128];
	struct tm *tm_p;
	time_t t;
	uint64_t metric_id;

	int32_t v32;
	int64_t v64;
	uint32_t vu32;
	uint64_t vu64;
	double vd;

	SOS_OBJ_ATTR_GET(tv_sec, sos, MDS_TV_SEC, obj);
	SOS_OBJ_ATTR_GET(tv_usec, sos, MDS_TV_USEC, obj);

	/* Format the time as a string */
	t = tv_sec;
	if (ts_convert_disable) {
		sprintf(tv_s, "%lu.%06u", t, tv_usec);
	} else {
		tm_p = localtime(&t);
		strftime(t_s, sizeof(t_s), "%D %T", tm_p);
		sprintf(tv_s, "%s.%06u", t_s, tv_usec);
	}

	SOS_OBJ_ATTR_GET(metric_id, sos, MDS_COMP_ID, obj);

	enum sos_type_e vtype = sos_get_attr_type(sos, MDS_VALUE);
	switch (vtype) {
	case SOS_TYPE_INT32:
		SOS_OBJ_ATTR_GET(v32, sos, MDS_VALUE, obj);
		if (value_only)
			fprintf(fp, "%" PRIi32 "\n", v32);
		else
			fprintf(fp, "%-24s %12"PRIi64" %16" PRIi32 " %p\n",
					tv_s, metric_id, v32, obj);
		break;
	case SOS_TYPE_INT64:
		SOS_OBJ_ATTR_GET(v64, sos, MDS_VALUE, obj);
		if (value_only)
			fprintf(fp, "%" PRIi64 "\n", v64);
		else
			fprintf(fp, "%-24s %12"PRIu64" %16" PRIi64 " %p\n",
					tv_s, metric_id, v64, obj);
		break;
	case SOS_TYPE_UINT32:
		SOS_OBJ_ATTR_GET(vu32, sos, MDS_VALUE, obj);
		if (value_only)
			fprintf(fp, "%" PRIu32 "\n", vu32);
		else
			fprintf(fp, "%-24s %12"PRIu64" %16" PRIu32 " %p\n",
					tv_s, metric_id, vu32, obj);
		break;
	case SOS_TYPE_UINT64:
		SOS_OBJ_ATTR_GET(vu64, sos, MDS_VALUE, obj);
		if (value_only)
			fprintf(fp, "%" PRIu64 "\n", vu64);
		else
			fprintf(fp, "%-24s %12"PRIu64" %16" PRIu64 " %p\n",
					tv_s, metric_id, vu64, obj);
		break;
	case SOS_TYPE_DOUBLE:
		SOS_OBJ_ATTR_GET(vd, sos, MDS_VALUE, obj);
		if (value_only)
			fprintf(fp, "%f\n", vd);
		else
			fprintf(fp, "%-24s %12"PRIu64" %16f %p\n",
					tv_s, metric_id, vd, obj);
		break;
	default:
		fprintf(stderr, "Unsupported type '%s'\n",
			sos_type_to_str(vtype));
		break;
	}

	if (delete) {
		sos_obj_remove(sos, obj);
		sos_obj_delete(sos, obj);
	}
}

int main(int argc, char *argv[])
{
	char comp_type[128];
	char metric_name[128];
	int quiet = 0;
	int cnt;
	int op;
	int delete = 0;
	int show_by_time = 1;
	uint32_t comp_id = -1;
	uint32_t tv_sec = -1;
	extern int optind;
	extern char *optarg;
	obj_key_t tv_key = obj_key_new(16);
	obj_key_t comp_key = obj_key_new(16);

	opterr = 0;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'q':
			quiet = 1;
			break;
		case 'p':
			root_path = strdup(optarg);
			break;
		case 'c':
			comp_id = strtol(optarg, NULL, 0);
			/* select the comp_id index */
			show_by_time = 0;
			break;
		case 't':
			tv_sec = strtol(optarg, NULL, 0);
			break;
		case 'C':
			show_by_time = 0;
			break;
		case 'T':
			show_by_time = 1;
			break;
		case 'D':
			delete = 1;
			break;
		case 'Z':
			ts_convert_disable = 1;
			break;
		case 'V':
			value_only = 1;
			break;
		case '?':
		default:
			usage(argc, argv);
		}
	}
	if (optind >= argc)
		usage(argc, argv);

	for (op = optind; op < argc; op++) {
		sos_t sos;
		sos_iter_t iter;
		sos_iter_t tv_iter;
		sos_iter_t comp_iter;

		cnt = sscanf(argv[op], "%128[^:]:%128s",
			     comp_type, metric_name);
		if (cnt != 2)
			usage(argc, argv);

		if (!quiet) {
			printf("COMP_TYPE: %s METRIC_NAME: %s\n\n",
						 comp_type, metric_name);
			printf("%-24s %-12s %-16s\n", "Timestamp", "Component", "Value");
			printf("------------------------ ------------ ----------------\n");
		}
		if (root_path)
			sprintf(tmp_path, "%s/%s/%s", root_path, comp_type, metric_name);
		else
			sprintf(tmp_path, "%s/%s", comp_type, metric_name);
		sos = sos_open(tmp_path, O_RDWR);
		if (!sos) {
			printf("Could not open SOS '%s'\n", tmp_path);
			continue;
		}

		tv_iter = sos_iter_new(sos, MDS_TV_SEC);
		comp_iter = sos_iter_new(sos, MDS_COMP_ID);

		int rc;
		if (tv_sec != -1) {
			obj_key_set(tv_key, &tv_sec, sizeof(tv_sec));
			rc = sos_iter_seek(tv_iter, tv_key);
		} else
			rc = sos_iter_begin(tv_iter);

		if (comp_id != -1) {
			obj_key_set(comp_key, &comp_id, sizeof(comp_id));
			rc = sos_iter_seek(comp_iter, comp_key);
		} else
			rc = sos_iter_begin(comp_iter);

		sos_obj_t obj;
		if (show_by_time)
			iter = tv_iter;
		else
			iter = comp_iter;
		for (; !rc; rc = sos_iter_next(iter)) {
			obj = sos_iter_obj(iter);
			obj_key_t key = sos_iter_key(iter);
			/*
			 * If the user specified a key on the index
			 * we need to stop when the iterator passes the key.
			 */
			if (tv_sec != -1 && sos_iter_key_cmp(tv_iter, key, tv_key)) {
				if (iter == tv_iter)
					break;
				else
					continue;
			}
			if (comp_id != -1 && sos_iter_key_cmp(comp_iter, key, comp_key)) {
				if (iter == comp_iter)
					break;
				else
					continue;
			}
			records ++;
			print_record(stdout, sos, obj, delete);
		}
		if (!quiet) {
			printf("------------------------ ------------ ----------------\n");
			printf("%d records\n", records);
		}
	}
	return 0;
}
