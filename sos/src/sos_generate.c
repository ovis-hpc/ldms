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

/*
 * Author: Narate Taerat (narate at ogc dot net)
 */

#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "sos.h"
#include "sos_priv.h"
#include "ods.h"
#include "obj_idx.h"

#include "ovis_util/util.h"

const char *__short_opt = "c:s:?";
struct option __long_opt[] = {
	{"class", required_argument, 0, 'c'},
	{"store", required_argument, 0, 's'},
	{"help", no_argument, 0, '?'},
	{0, 0, 0, 0},
};

const char *class_path = NULL;
const char *store_path = NULL;

void usage()
{
	printf("\
Synopsis: sos_generate [-c CLASS_FILE] -s STORE\n\
\n\
Description:\n\
	sos_generate is a utility to add data into an SOS (STORE).\n\
	If the given STORE does not exist, sos_generate will create it\n\
	with the supplied CLASS_FILE. The CLASS_FILE is a text file,\n\
	each line of which contain a class distribute definition (name:type).\n\
	\n\
	CLASS EXAMPLE:\n\
		tv_sec*:UINT32\n\
		tv_usec:UINT32\n\
		metric_id*:UINT64\n\
		value:DOUBLE\n\
	\n\
	The supported are: INT32, UINT32, INT64, UINT64, DOUBLE, STRING.\n\
	The attribute names tagged with '*' will be indexed.\n\
	\n\
	If STORE does not exist and CLASS_FILE is not given, the program will\n\
	exit with an error.\n\
	\n\
	After successfully open or create a store, sos_generate will read\n\
	STDIN and add the data into the store accordingly. An example of\n\
	input in the STDIN is as follows:\n\
	\n\
	INPUT EXAMPLE (corresponding to CLASS EXAMPLE above):\n\
		1412192013,0,0x100000001,10.0\n\
		1412192013,0,0x200000001,11.0\n\
		1412192013,0,0x300000001,9.0\n\
	\n\
	Once the EOF is reached, the program will close the store and exit.\n\
\n"
	      );
	_exit(-1);
}

const char *__SOS_ATTR_STR[] = {
	[SOS_TYPE_INT32]    =  "INT32",
	[SOS_TYPE_INT64]    =  "INT64",
	[SOS_TYPE_UINT32]   =  "UINT32",
	[SOS_TYPE_UINT64]   =  "UINT64",
	[SOS_TYPE_DOUBLE]   =  "DOUBLE",
	[SOS_TYPE_STRING]   =  "STRING",
};

enum sos_type_e get_sos_type(const char *s)
{
	int i;
	for (i = SOS_TYPE_INT32; i <= SOS_TYPE_STRING; i++) {
		if (0 == strcasecmp(__SOS_ATTR_STR[i], s)) {
			return i;
		}
	}
	return SOS_TYPE_UNKNOWN;
}

struct sos_attr_s sos_attr[] = {
	[SOS_TYPE_INT32] = SOS_OBJ_ATTR("SOS_TYPE_INT32", SOS_TYPE_INT32),
	[SOS_TYPE_INT64] = SOS_OBJ_ATTR("SOS_TYPE_INT64", SOS_TYPE_INT64),
	[SOS_TYPE_UINT32] = SOS_OBJ_ATTR("SOS_TYPE_UINT32", SOS_TYPE_UINT32),
	[SOS_TYPE_UINT64] = SOS_OBJ_ATTR("SOS_TYPE_UINT64", SOS_TYPE_UINT64),
	[SOS_TYPE_DOUBLE] = SOS_OBJ_ATTR("SOS_TYPE_DOUBLE", SOS_TYPE_DOUBLE),
	[SOS_TYPE_STRING] = SOS_OBJ_ATTR("SOS_TYPE_STRING", SOS_TYPE_STRING),
};

sos_attr_t get_sos_attr(enum sos_type_e type)
{
	if (type < SOS_TYPE_INT32 || SOS_TYPE_STRING < type)
		return NULL;
	return &sos_attr[type];
}

struct sos_attr_s sos_attr_k[] = {
	[SOS_TYPE_INT32] = SOS_OBJ_ATTR_WITH_KEY("SOS_TYPE_INT32", SOS_TYPE_INT32),
	[SOS_TYPE_INT64] = SOS_OBJ_ATTR_WITH_KEY("SOS_TYPE_INT64", SOS_TYPE_INT64),
	[SOS_TYPE_UINT32] = SOS_OBJ_ATTR_WITH_KEY("SOS_TYPE_UINT32", SOS_TYPE_UINT32),
	[SOS_TYPE_UINT64] = SOS_OBJ_ATTR_WITH_KEY("SOS_TYPE_UINT64", SOS_TYPE_UINT64),
	[SOS_TYPE_DOUBLE] = SOS_OBJ_ATTR_WITH_KEY("SOS_TYPE_DOUBLE", SOS_TYPE_DOUBLE),
	[SOS_TYPE_STRING] = SOS_OBJ_ATTR_WITH_KEY("SOS_TYPE_STRING", SOS_TYPE_STRING),
};

sos_attr_t get_sos_attr_k(enum sos_type_e type)
{
	if (type < SOS_TYPE_INT32 || SOS_TYPE_STRING < type)
		return NULL;
	return &sos_attr_k[type];
}

typedef struct value_s {
	enum sos_type_e type;
	union {
		int32_t i32;
		int64_t i64;
		uint32_t u32;
		uint64_t u64;
		double d;
		char *str;
	};
} *value_t;

void str2value(const char *str, enum sos_type_e type, void *out)
{
	switch (type) {
	case SOS_TYPE_INT32:
		*(int32_t*)out = strtol(str, NULL, 0);
		break;
	case SOS_TYPE_INT64:
		*(int64_t*)out = strtoll(str, NULL, 0);
		break;
	case SOS_TYPE_UINT32:
		*(uint32_t*)out = strtoul(str, NULL, 0);
		break;
	case SOS_TYPE_UINT64:
		*(uint64_t*)out = strtoull(str, NULL, 0);
		break;
	case SOS_TYPE_DOUBLE:
		*(double*)out = strtod(str, NULL);
		break;
	default:
		printf("UNSUPPORTED TYPE: %s\n",
				sos_type_to_str(type));
		_exit(-1);
	}
}

char buff[4096];

void chomp(char *str)
{
	char *x = str + strlen(str) - 1;
	while (*x && !isalnum(*x)) {
		x--;
	}
	*++x = 0;
}

sos_class_t get_sos_class(const char *path)
{
	FILE *f = fopen(path, "r");
	char *attr_name, *type_name, *tmp;
	enum sos_type_e attr_type;
	int is_indexed;
	sos_class_t class = calloc(1, sizeof(*class) +
					256 * sizeof(struct sos_attr_s));
	if (!class) {
		perror("calloc");
		goto out;
	}

	class->name = basename(path);

	while (fgets(buff, sizeof(buff), f)) {
		attr_name = buff;
		type_name = strchr(buff, ':');
		if (!type_name) {
			printf("ERROR: expecting NAME:TYPE format,"
					" but got: %s\n", buff);
			goto cleanup;
		}
		*type_name++ = 0;
		chomp(type_name);

		attr_type = get_sos_type(type_name);
		if (attr_type == SOS_TYPE_UNKNOWN) {
			printf("ERROR: Unknown type: %s\n", type_name);
			goto cleanup;
		}

		if ((tmp = strchr(attr_name, '*'))) {
			*tmp = 0;
			is_indexed = 1;
		} else {
			is_indexed = 0;
		}

		if (is_indexed) {
			class->attrs[class->count] = *get_sos_attr_k(attr_type);
		} else {
			class->attrs[class->count] = *get_sos_attr(attr_type);
		}
		tmp = strdup(attr_name);
		if (!tmp) {
			perror("strdup");
			goto cleanup;
		}
		class->attrs[class->count].name = tmp;
		class->count++;
	}

	goto out;

cleanup:
	free(class);
	class = NULL;

out:
	return class;
}

int main(int argc, char **argv)
{
	sos_t sos;
	sos_class_t class;
	char c;
arg_loop:
	c = getopt_long(argc, argv, __short_opt, __long_opt, NULL);
	switch (c) {
	case -1:
		goto arg_out;
	case 'c':
		class_path = optarg;
		break;
	case 's':
		store_path = optarg;
		break;
	case '?':
	default:
		usage();
	}
	goto arg_loop;

arg_out:
	if (!store_path) {
		printf("ERROR: --store option is needed.\n");
		_exit(-1);
	}

	sos = sos_open(store_path, O_RDWR);
	if (!sos) {
		printf("INFO: store '%s' does not exist\n", store_path);
		/* Try creating it */
		if (!class_path) {
			printf("ERROR: --class option is needed.\n");
			_exit(-1);
		}
		class = get_sos_class(class_path);
		if (!class) {
			printf("ERROR: class definition error.\n");
			_exit(-1);
		}

		sos = sos_open(store_path, O_RDWR|O_CREAT, 0660, class);
		if (!sos) {
			printf("ERROR: Cannot create store: %s\n", store_path);
			_exit(-1);
		}
		printf("INFO: store '%s' created.\n", store_path);
	}

	int attr_id;
	sos_obj_t obj;
	char *tmp, *tok;
	sos_attr_t attr;
	uint64_t vbuf[2];
	void *v;

	while (fgets(buff, sizeof(buff), stdin)) {
		obj = sos_obj_new(sos);
		if (!obj) {
			printf("ERROR: Cannot create new sos object.\n");
			_exit(-1);
		}
		tok = strtok_r(buff, ",", &tmp);
		attr_id = 0;
		while (tok) {
			attr = sos_obj_attr_by_id(sos, attr_id);
			if (attr->type == SOS_TYPE_STRING) {
				v = tok;
			} else {
				str2value(tok, attr->type, vbuf);
				v = vbuf;
			}
			sos_attr_set(attr, obj, v);
			tok = strtok_r(NULL, ",", &tmp);
			attr_id++;
		}
		sos_obj_add(sos, obj);
	}

	printf("INFO: DONE\n");

	return 0;
}
