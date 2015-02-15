/* -*- c-basic-offset: 8 -*-
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
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include <errno.h>

#include "ldmsd.h"
#include <sos/sos.h>
#if 0
#include "../store/store_sos.h"

const char *short_options = "s:t:u:g:m:f?";
struct option long_options[] = {
	{"store",  required_argument,  0,  's'},
	{"type",   required_argument,  0,  't'},
	{"user",   required_argument,  0,  'u'},
	{"group",  required_argument,  0,  'g'},
	{"mode",   required_argument,  0,  'm'},
	{"force",  no_argument,        0,  'f'},
	{"help",   no_argument,        0,  '?'},
	{0,        0,                  0,  0},
};

void usage()
{
	printf(
"	Usage: ldmsd_sos_init [OPTIONS]\n"
"	OPTIONS:\n"
"	    -s,--store <STORE_PATH>\n"
"		path to the metric store\n"
"		Default: None (required)\n"
"	    -t,--type <STORE_TYPE>\n"
"		Type of the store (INT32, INT64, UINT32, UINT64, DOUBLE)\n"
"		Default: None (required)\n"
"	    -u,--user <USER_NAME|USER_ID>\n"
"		User name or ID\n"
"		Default: UID of the caller\n"
"	    -g,--group <GROUP_NAME|GROUP_ID>\n"
"		Group name or ID\n"
"		Default: GID of the caller\n"
"	    -m,--mode <OCTAL_PERMISSION>\n"
"		Store permission\n"
"		Default: 660\n"
"	    -f,--force\n"
"		Force store init even the store existed.\n"
"		Default: do not force\n"
"	    -?,-h,--help\n"
"		Print this message\n"
	      );
	_exit(-1);
}

const char *sos_path = NULL;
enum sos_type_e sos_type = SOS_TYPE_UNKNOWN;
uid_t uid = 0;
gid_t gid = 0;
mode_t mode = 0660;
int force = 0;

const char *sos_type_str[] = {
	[SOS_TYPE_INT32] = "INT32",
	[SOS_TYPE_INT64] = "INT64",
	[SOS_TYPE_UINT32] = "UINT32",
	[SOS_TYPE_UINT64] = "UINT64",
	[SOS_TYPE_DOUBLE] = "DOUBLE",
};

struct sos_class_s *sos_class[] = {
	[SOS_TYPE_INT32] = &ovis_metric_class_int32,
	[SOS_TYPE_INT64] = &ovis_metric_class_int64,
	[SOS_TYPE_UINT32] = &ovis_metric_class_uint32,
	[SOS_TYPE_UINT64] = &ovis_metric_class_int64,
	[SOS_TYPE_DOUBLE] = &ovis_metric_class_double,
};

const char *sos_suffix[] = {
	"_sos.OBJ",
	"_sos.PG",
	"_tv_sec.OBJ",
	"_tv_sec.PG",
	"_metric_id.OBJ",
	"_metric_id.PG",
};

enum sos_type_e get_type(const char *str)
{
	int i;
	for (i = 0; i < SOS_TYPE_UNKNOWN; i++) {
		if (sos_type_str[i] && strcasecmp(sos_type_str[i], str)==0) {
			return i;
		}
	}
	return SOS_TYPE_UNKNOWN;
}

int all_num(const char *x)
{
	while (*x) {
		if (!isdigit(*x))
			return 0;
		x++;
	}
	return 1;
}

typedef void (*cbfn_t)(const char *path, void *arg);

void foreach_file(cbfn_t fn, void *arg)
{
	char buff[4096];
	int i, rc;
	for (i = 0; i < sizeof(sos_suffix)/sizeof(*sos_suffix); i++) {
		sprintf(buff, "%s%s", sos_path, sos_suffix[i]);
		fn(buff, arg);
	}
}

void file_exists(const char *path, void *arg)
{
	int *out = arg;
	int rc;
	if (*out)
		return;

	struct stat _stat;
	rc = stat(path, &_stat);
	if (rc == 0)
		*out = 1;
}

void remove_file(const char *path, void *arg)
{
	int *out = arg;
	int rc;
	if (*out)
		return;
	rc = unlink(path);
	if (rc && errno != ENOENT)
		*out = -1;
}

struct chmod_arg {
	mode_t mode;
	int rc;
};

void chmod_file(const char *path, void *arg)
{
	struct chmod_arg *_arg = arg;
	if (_arg->rc)
		return;
	_arg->rc = chmod(path, _arg->mode);
	if (_arg->rc) {
		fprintf(stderr, "ERROR: Cannot chmod %s, errno: %d\n",
				path, errno);
	}
}

struct chown_arg {
	uid_t uid;
	gid_t gid;
	int rc;
};

void chown_file(const char *path, void *arg)
{
	struct chown_arg *_arg = arg;
	if (_arg->rc)
		return;
	_arg->rc = chown(path, _arg->uid, _arg->gid);
	if (_arg->rc) {
		fprintf(stderr, "ERROR: Cannot chown %s, errno: %d\n",
				path, errno);
	}
}
#endif

int main(int argc, char **argv)
{
#if 0
	char c;
	int rc;
	struct passwd passwd;
	struct passwd *passwd_r;
	struct group group;
	struct group *group_r;
	char buff[4096];
	char *str;
	struct stat stat;
	int existed = 0;

	uid = getuid();
	gid = getgid();

arg_loop:
	c = getopt_long(argc, argv, short_options, long_options, NULL);
	switch (c) {
	case -1:
		goto arg_out;
	case 's':
		sos_path = optarg;
		break;
	case 't':
		sos_type = get_type(optarg);
		if (sos_type == SOS_TYPE_UNKNOWN) {
			fprintf(stderr, "ERROR: Unknown type: %s\n", optarg);
			_exit(-1);
		}
		break;
	case 'u':
		getpwnam_r(optarg, &passwd, buff, sizeof(buff), &passwd_r);
		if (passwd_r) {
			uid = passwd.pw_uid;
			if (!gid)
				gid = passwd.pw_gid;
			break;
		}
		if (all_num(optarg)) {
			uid = atoi(optarg);
			if (!gid)
				gid = passwd.pw_gid;
			getpwuid_r(uid, &passwd, buff, sizeof(buff), &passwd_r);
			if (passwd_r)
				break;
		}
		fprintf(stderr, "ERROR: Unknown user: %s\n", optarg);
		_exit(-1);
		break;
	case 'g':
		getgrnam_r(optarg, &group, buff, sizeof(buff), &group_r);
		if (group_r) {
			gid = group.gr_gid;
			break;
		}
		if (all_num(optarg)) {
			gid = atoi(optarg);
			getgrgid_r(gid, &group, buff, sizeof(buff), &group_r);
			if (group_r)
				break;
		}
		fprintf(stderr, "ERROR: Unknown group: %s\n", optarg);
		_exit(-1);
		break;
	case 'm':
		rc = sscanf(optarg, "%o", &mode);
		if (rc != 1) {
			fprintf(stderr, "ERROR: wrong mode: %s\n", optarg);
			_exit(-1);
		}
		break;
	case 'f':
		force = 1;
		break;
	case '?':
	default:
		usage();
	}
	goto arg_loop;
arg_out:

	if (!sos_path) {
		fprintf(stderr, "ERROR: store path is not specified\n");
		_exit(-1);
	}

	if (sos_type == SOS_TYPE_UNKNOWN) {
		fprintf(stderr, "ERROR: sos type is not specified\n");
		_exit(-1);
	}

	foreach_file(file_exists, &existed);

	if (existed) {
		if (!force) {
			fprintf(stderr, "ERROR: Store existed\n");
			_exit(-1);
		}

		rc = 0;
		foreach_file(remove_file, &rc);
		if (rc) {
			fprintf(stderr, "ERROR: Cannot remove existing store, "
					"errno: %d\n", errno);
			_exit(-1);
		}
	}

	sos_t sos = sos_open(sos_path, O_CREAT|O_RDWR, mode,
			sos_class[sos_type]);

	sos_close(sos, ODS_COMMIT_SYNC);

	struct chmod_arg mod_arg = {.mode = mode, .rc = 0};
	foreach_file(chmod_file, &mod_arg);
	if (mod_arg.rc)
		return -1;

	struct chown_arg own_arg = {.uid = uid, .gid = gid, .rc = 0};
	foreach_file(chown_file, &own_arg);
	if (own_arg.rc)
		return -1;
#endif
	return 0;
}
