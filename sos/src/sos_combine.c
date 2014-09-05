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
 * \file sos_combine.c
 * \author Narate Taerat (narate at ogc dot us)
 * \brief Combine given SOSes into the destination SOS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <fcntl.h>

#include "sos.h"
#include "ods.h"
#include "sos_priv.h"
#include "ods_priv.h"

const char *short_options = "s:d:h?";

struct option long_options[] = {
	{"src",   required_argument,  0,  's'},
	{"dest",  required_argument,  0,  'd'},
	{"help",  no_argument,        0,  '?'},
	{0,       0,                  0,  0}
};

typedef struct src_list {
	const char *src_path;
	TAILQ_ENTRY(src_list) entry;
} *src_list_t;

int src_count = 0;
TAILQ_HEAD(, src_list) src_head = TAILQ_HEAD_INITIALIZER(src_head);
const char *dest_path = NULL;

const char *usage_str = "\n\
Synopsis: Combine give source SOSes into the destination SOS.\n\
\n\
Usage: sos_combine [-s] <SRC_SOS1> [<SRC_SOS2> ...] -d <DEST_SOS>\n\
\n\
The option '-s' can be given to make it consistent to other sos commands.\n\
";

void usage()
{
	printf("%s\n", usage_str);
	_exit(-1);
}

void handle_args(int argc, char **argv)
{
	char o;
	struct src_list *src_list;
	int i;
loop:
	o = getopt_long(argc, argv, short_options, long_options, NULL);
	switch (o) {
	case -1:
		goto out;
	case 'd':
		dest_path = optarg;
		argv[optind-2] = NULL;
		argv[optind-1] = NULL;
		break;
	case 's':
		/* treat default as src SOS */
		src_list = calloc(1, sizeof(*src_list));
		if (!src_list) {
			perror("calloc");
			_exit(-1);
		}
		src_list->src_path = optarg;
		TAILQ_INSERT_TAIL(&src_head, src_list, entry);
		argv[optind-2] = NULL;
		argv[optind-1] = NULL;
		break;
	case '?':
	case 'h':
	default:
		usage();
	}
	goto loop;
out:
	for (i = 1; i < argc; i++) {
		if (!argv[i])
			continue;
		src_list = calloc(1, sizeof(*src_list));
		if (!src_list) {
			perror("calloc");
			_exit(-1);
		}
		src_list->src_path = argv[i];
		TAILQ_INSERT_TAIL(&src_head, src_list, entry);
	}
	return;
}

char path[PATH_MAX];

struct sos_combine_ods_iter_cb_arg {
	int rc;
	sos_t sos;
};

void sos_combine_ods_iter_cb(ods_t ods, void *ptr, size_t sz, void *arg)
{
	struct sos_combine_ods_iter_cb_arg *sarg = arg;
	sos_t sos;
	sos_obj_t sobj;
	sos_obj_t dobj;
	sos_obj_t aobj;
	int id;
	void *valuep;
	enum sos_type_e attr_type;
	obj_ref_t ref;
	sos_attr_t attr;

	if (sarg->rc)
		return ;
	sobj = ptr;
	if (sobj->type != SOS_OBJ_TYPE_OBJ)
		return;

	sos = sarg->sos;

	dobj = sos_obj_new(sos);
	if (!dobj) {
		sarg->rc = ENOMEM;
		return;
	}
	for (id = 0; id < sos->classp->count; id++) {
		attr = sos_obj_attr_by_id(sos, id);
		attr_type = sos_get_attr_type(sos, id);
		valuep = &sobj->data[attr->data];
		if (attr_type == SOS_TYPE_BLOB
				|| attr_type == SOS_TYPE_STRING) {
			/* follow ref, in case of variable-length attr */
			ref = *(obj_ref_t*)valuep;
			aobj = ods_obj_ref_to_ptr(ods, ref);
			valuep = aobj->data;
		}
		sos_obj_attr_set(sos, id, dobj, valuep);
	}
	sarg->rc = sos_obj_add(sos, dobj);
}

int sos_combine_ods(sos_t sos, ods_t ods)
{
	int rc = 0;
	struct sos_combine_ods_iter_cb_arg sarg;
	sos_class_t oclass;
	oclass = sos_class_from_ods(ods);
	if (!oclass)
		return ENOMEM;
	if (sos_class_cmp(sos->classp, oclass)) {
		rc = EINVAL;
		goto out;
	}
	sarg.rc = 0;
	sarg.sos = sos;
	ods_iter(ods, sos_combine_ods_iter_cb, &sarg);
	rc = sarg.rc;
out:
	sos_class_free(oclass);
	return rc;
}

int main(int argc, char **argv)
{
	struct stat st;
	src_list_t src;
	sos_class_t classp = NULL;
	sos_class_t classp1;
	sos_t sos;
	ods_t ods;
	sos_meta_t meta;
	size_t meta_sz;
	int i, rc;

	umask(0);

	handle_args(argc, argv);

	if (!dest_path) {
		fprintf(stderr, "ERROR: option -d is needed.\n");
		_exit(-1);
	}

	/* Check input classes */
	TAILQ_FOREACH(src, &src_head, entry) {
		snprintf(path, PATH_MAX, "%s_sos", src->src_path);
		ods = ods_open(path, O_RDWR);
		if (!ods) {
			fprintf(stderr, "ERROR: cannot open ods: %s\n", path);
			_exit(-1);
		}
		classp1 = sos_class_from_ods(ods);

		if (!classp) {
			classp = classp1;
			goto skip;
		}

		if (sos_class_cmp(classp, classp1)) {
			fprintf(stderr, "ERROR: Input SOS(es) are not of the"
					" same class.\n");
			_exit(-1);
		}

		sos_class_free(classp1);
	skip:
		ods_close(ods, ODS_COMMIT_ASYNC);
	}

	snprintf(path, PATH_MAX, "%s_sos.OBJ", dest_path);
	rc = stat(path, &st);
	if (rc) {
		/* destination does not exist, creat it */
		sos = sos_open(dest_path, O_RDWR|O_CREAT, 0660, classp);
	} else {
		sos = sos_open(dest_path, O_RDWR);
	}

	if (!sos) {
		fprintf(stderr, "ERROR: Cannot open sos: %s\n", dest_path);
		_exit(-1);
	}

	if (sos_class_cmp(classp, sos->classp)) {
		fprintf(stderr, "ERROR: Destination SOS class does not"
				" match the input SOSes.\n");
		_exit(-1);
	}

	TAILQ_FOREACH(src, &src_head, entry) {
		printf("combining: %s ... ", src->src_path);
		snprintf(path, PATH_MAX, "%s_sos", src->src_path);
		ods = ods_open(path, O_RDWR);
		if (!ods) {
			fprintf(stderr, "ERROR: cannot open ods: %s\n", path);
			_exit(-1);
		}
		rc = sos_combine_ods(sos, ods);
		if (rc) {
			fprintf(stderr, "ERROR: sos_combine_ods() rc: %d\n", rc);
			_exit(-1);
		}
		printf("DONE\n");
	}

	return 0;
}
