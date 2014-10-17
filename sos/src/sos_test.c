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

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include <getopt.h>

#include "sos.h"
#include "obj_idx.h"

SOS_OBJ_BEGIN(ovis_metric_class, "OvisMetric")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("metric_id", SOS_TYPE_UINT64),
	SOS_OBJ_ATTR("value", SOS_TYPE_UINT64)
SOS_OBJ_END(4);

const char *short_options = "b:s:l:rRi?";

struct option long_options[] = {
	{"store",       required_argument,  0,  's'},
	{"remove",      no_argument,        0,  'r'},
	{"rotate",      no_argument,        0,  'R'},
	{"keep-index",  no_argument,        0,  'i'},
	{"limit",       required_argument,  0,  'l'},
	{"backups",     required_argument,  0,  'l'},
	{"help",        no_argument,        0,  '?'},
	{0,             0,                  0,  0}
};

void usage()
{
	printf("\n\
Usage: sos_test [OPTIONS]\n\
\n\
OPTIONS: \n\
    -s,--store <PATH>\n\
	Path to the store (default: store/store)\n\
\n\
    -R,--rotate \n\
	Enable store rotation\n\
\n\
    -i,--keep-index \n\
	SOS rotation keep indices\n\
\n\
    -r,--remove \n\
	Enable old data removal (disabled if -R is given)\n\
\n\
    -l,--limit <TIME_LIMIT>\n\
	Time limitation in seconds for store rotation or data removal\n\
	(default: 60 (sec))\n\
\n\
    -b,--backups <BACKUPS_LIMIT>\n\
	The number of backups for store rotation (default: unlimited)\n\
"
	);
	_exit(-1);
}

void remove_data(sos_t sos, uint32_t sec, uint32_t limit)
{
	sos_obj_t obj;
	uint32_t t;
	sos_iter_t iter;
	int rc;

	iter = sos_iter_new(sos, 0);
	assert(iter);
	rc = sos_iter_begin(iter);
	if (rc)
		goto exit;
loop:
	obj = sos_iter_obj(iter);
	if (!obj)
		goto exit; /* no more object */
	t = sos_obj_attr_get_uint32(sos, 0, obj);
	if (sec - t < limit)
		goto exit;
	sos_iter_obj_remove(iter);
	sos_obj_delete(sos, obj);
	goto loop;
exit:
	sos_iter_free(iter);
}

int main(int argc, char **argv)
{
	char buf[BUFSIZ];
	uint32_t sec, usec, t, sec0;
	uint64_t metric_id, value;
	int n;
	int rc;
	int limit = 60;
	char o;
	const char *path = "store/store";
	char *s;
	int remove = 0;
	int rotate = 0;
	int backups = 0;
	int keep_index = 0;
	sos_iter_t iter;
	sos_t sos;
	sos_obj_t obj;
	pid_t p;
	umask(0);

arg_loop:
	o = getopt_long(argc, argv, short_options, long_options, NULL);
	switch (o) {
	case -1:
		goto arg_out;
	case 's':
		path = optarg;
		break;
	case 'r':
		remove = 1;
		break;
	case 'R':
		rotate = 1;
		break;
	case 'i':
		keep_index = 1;
		break;
	case 'l':
		limit = atoi(optarg);
		break;
	case 'b':
		backups = atoi(optarg);
		break;
	case '?':
	default:
		usage();
	}
	goto arg_loop;
arg_out:

	if (rotate)
		remove = 0;

	sec0 = 0;

	sos = sos_open(path, O_RDWR|O_CREAT, 0660, &ovis_metric_class);
	assert(sos);

	while ((s = fgets(buf, sizeof(buf), stdin)) != NULL) {
		n = sscanf(buf, "%"PRIu32".%"PRIu32" %"PRIu64" %"PRIu64,
				&sec, &usec, &metric_id, &value);

		if (!sec0)
			sec0 = sec;

		if (n != 4)
			break;

		obj = sos_obj_new(sos);
		assert(obj);

		sos_obj_attr_set_uint32(sos, 0, obj, sec);
		sos_obj_attr_set_uint32(sos, 1, obj, usec);
		sos_obj_attr_set_uint64(sos, 2, obj, metric_id);
		sos_obj_attr_set_uint64(sos, 3, obj, value);

		/* Add it to the indexes */
		rc = sos_obj_add(sos, obj);
		assert(rc == 0);
		if (remove)
			remove_data(sos, sec, limit);
		if (rotate && (sec - sec0) >= limit) {
			sos_t new_sos;
			if (keep_index)
				new_sos = sos_rotate_i(sos, backups);
			else
				new_sos = sos_rotate(sos, backups);
			assert(new_sos);
			sos = new_sos;
			sec0 = sec;
			p = sos_post_rotation(sos, "SOS_TEST_POSTROTATE");
		}
	}

	sos_close(sos, ODS_COMMIT_SYNC);

	return 0;
}
