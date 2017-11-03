/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2017 Sandia Corporation. All rights reserved.
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
 * \file dvs_sampler.c
 * \brief Sample data from /proc/fs/dvs/[mount-id]/stats
 *
 * This sampler produces multiple metric sets, one for each mount
 * in /proc/fs/dvs/[mount-id].
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <libgen.h>
#include <ftw.h>
#include <coll/rbt.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

static ldmsd_msg_log_f log_fn;
static char *procfile = "/proc/fs/dvs";
#define SAMP "dvs_client"
static int metric_offset;	/* first non-base metric */
static base_data_t cfg_base;	/* global base for all sets */

typedef struct dvs_mount_s {
	base_data_t cfg; /* The configuration data */
	char *mnt_pt;	 /* The DVS local mount point */
	char *dir;	 /* The path to the stat file */
	FILE *stats_f;	 /* <mount-no>/stats has the dvs statistics */
	FILE *mount_f;   /* <mount-no>/mount has the local mount point */
	ldms_set_t set;
	struct rbn rbn;
} *dvs_mount_t;

struct rbt mount_tree;

static int handle_req_ops(dvs_mount_t mount, char *line,
			  char **u64_names, size_t u64_cnt,
			  char **dbl_names, size_t dbl_cnt,
			  int *metric_no)
{
	int i, rc;
	char *base_name;
	char name[80];
	char metric_name[128];
	int cnt = sscanf(line, "%80[^:]:", name);
	int metric_count = 0;
	if (cnt < 1) {
		log_fn(LDMSD_LERROR, "invalid dvs stats file format.\n");
		goto err;
	}
	cnt = strlen(name);
	if (cnt && name[cnt-1] == '\n')
		name[cnt-1] = '\0';
	for (i = 0; i < cnt; i++) {
		if (name[i] == ' ') {
			name[i] = '_';
			continue;
		}
	}
	for (i = 0; i < u64_cnt; i++) {
		if (u64_names[i][0] == '\0')
			sprintf(metric_name, "%s", name);
		else
			sprintf(metric_name, "%s_%s", name, u64_names[i]);
		rc = ldms_schema_metric_add(mount->cfg->schema, metric_name, LDMS_V_U64);
		if (rc < 0)
			goto err;
		metric_count++;
	}
	for (i = 0; i < dbl_cnt; i++) {
		if (dbl_names[i][0] == '\0')
			sprintf(metric_name, "%s", name);
		else
			sprintf(metric_name, "%s_%s", name, dbl_names[i]);
		rc = ldms_schema_metric_add(mount->cfg->schema, metric_name, LDMS_V_D64);
		if (rc < 0)
			goto err;
		metric_count++;
	}
	*metric_no = *metric_no + metric_count;
	return 0;
 err:
	return EINVAL;
}

base_data_t dup_cfg(base_data_t base, char *mnt_pt)
{
	char inst[PATH_MAX];
	base_data_t newb = calloc(1, sizeof(*base));
	*newb = *base;
	newb->producer_name = strdup(base->producer_name);
	sprintf(inst, "%s%s", newb->producer_name, mnt_pt);
	newb->instance_name = strdup(inst);
	newb->schema_name = strdup(base->schema_name);
	return newb;
}

#define ARRAY_SIZE(_a_)	(sizeof(_a_)/ sizeof(_a_[0]))
static int create_metric_set(dvs_mount_t dvsm)
{
	char *tmp;
	static char *RQ_u64_names[] = { "clnt_cnt", "clnt_err", "srvr_cnt", "srvr_err"};
	static char *RQ_dbl_names[] = { "last_dur", "max_dur" };
	static char *fs_u64_names[] = { "cnt", "err" };
	static char *fs_dbl_names[] = { "last_dur", "max_dur" };
	static char *mm_u64_names[] = { "min", "max" };
	static char *cnt_u64_names[] = { "" };

	ldms_schema_t schema;
	int rc, i, metric_no;
	uint64_t metric_value;
	union ldms_value v;
	char *s;
	char lbuf[256];
	int state = 0;

	dvsm->cfg = dup_cfg(cfg_base, dvsm->mnt_pt);
	schema = base_schema_new(dvsm->cfg);
	if (!schema) {
		log_fn(LDMSD_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, cfg_base->schema_name, errno);
		goto err;
	}

	/* Location of first metric from proc/meminfo file */
	metric_offset = ldms_schema_metric_count_get(schema);

	/*
	 * Process the file to define all the metrics.
	 */
	metric_no = metric_offset;
	fseek(dvsm->stats_f, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), dvsm->stats_f);
		if (!s)
			break;
		switch (state) {
		case 0:
			if (s[0] != 'R')
				state ++;
			break;
		case 1:
			if (s[11] == 'x')
				state ++;
			break;
		case 2:
			if (s[0] == 'I')
				state ++;
			break;
		case 3:
			if (s[0] != 'I')
				state ++;
			break;
		}
		switch (state) {
		case 0:
			rc = handle_req_ops(dvsm, s,
					    RQ_u64_names, ARRAY_SIZE(RQ_u64_names),
					    RQ_dbl_names, ARRAY_SIZE(RQ_dbl_names),
					    &metric_no
					    );
			break;
		case 1:
			rc = handle_req_ops(dvsm, s,
					    fs_u64_names, ARRAY_SIZE(fs_u64_names),
					    fs_dbl_names, ARRAY_SIZE(fs_dbl_names),
					    &metric_no);
			break;
		case 2:		/* min-max */
			rc = handle_req_ops(dvsm, strtok_r(s, "_", &tmp),
					    mm_u64_names, ARRAY_SIZE(mm_u64_names),
					    NULL, 0,
					    &metric_no
					    );
			break;
		case 3:
			rc = handle_req_ops(dvsm, s,
					    fs_u64_names, ARRAY_SIZE(fs_u64_names),
					    NULL, 0,
					    &metric_no
					    );
			break;
		case 4:
			rc = handle_req_ops(dvsm, s,
					    cnt_u64_names, ARRAY_SIZE(cnt_u64_names),
					    NULL, 0,
					    &metric_no
					    );
			break;
		default:
			;
		}
		if (rc)
			goto err;
	} while (s);

	dvsm->set = base_set_new(dvsm->cfg);
	if (!dvsm->set) {
		rc = errno;
		goto err;
	}

	return 0;

 err:
	return rc;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP BASE_CONFIG_USAGE;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	cfg_base = base_config(avl, SAMP, SAMP, log_fn);
	if (!cfg_base)
		return EINVAL;
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

dvs_mount_t find_dvs(const char *dir)
{
	struct rbn *rbn = rbt_find(&mount_tree, dir);
	if (rbn)
		return container_of(rbn, struct dvs_mount_s, rbn);
	return NULL;
}

int unmount_dvs(dvs_mount_t dvsm)
{
	fclose(dvsm->stats_f);
	fclose(dvsm->mount_f);
	if (dvsm->mnt_pt)
		free(dvsm->mnt_pt);
	if (dvsm->dir)
		free(dvsm->dir);
	if (dvsm->set)
		ldms_set_delete(dvsm->set);
	rbt_del(&mount_tree, &dvsm->rbn);
	free(dvsm);
}

int mount_dvs(const char *dir)
{
	int rc = ENOMEM;
	char path[PATH_MAX];
	char lbuf[128];
	dvs_mount_t dvsm;
	dvsm = calloc(1, sizeof(*dvsm));
	if (!dvsm)
		goto err_0;
	dvsm->dir = strdup(dir);
	if (!dvsm->dir)
		goto err_1;
	snprintf(path, sizeof(path), "%s/%s", dir, "mount");
	dvsm->mount_f = fopen(path, "r");
	if (!dvsm->mount_f) {
		rc = errno;	/* probably EPERM */
		goto err_2;
	}
	snprintf(path, sizeof(path), "%s/%s", dir, "stats");
	dvsm->stats_f = fopen(path, "r");
	if (!dvsm->stats_f) {
		rc = errno;	/* probably EPERM */
		goto err_3;
	}

	/* Read the local mount point and store it in dvsm */
	char *s = fgets(lbuf, sizeof(lbuf), dvsm->mount_f);
	if (!s)
		goto err_4;
	char *mnt_pt, *tmp;
	mnt_pt = strtok_r(s, " ", &tmp);
	mnt_pt = strtok_r(NULL, " ", &tmp);
	if (!mnt_pt)
		goto err_4;
	rc = strlen(mnt_pt);
	if (rc && mnt_pt[rc-1] == '\n')
		mnt_pt[rc-1] = '\0';
	dvsm->mnt_pt = strdup(mnt_pt);
	if (!dvsm->mnt_pt) {
		rc = ENOMEM;
		goto err_4;
	}
	rc = create_metric_set(dvsm);
	if (rc)
		goto err_5;

	rbn_init(&dvsm->rbn, dvsm->dir);
	rbt_ins(&mount_tree, &dvsm->rbn);
	return 0;
 err_5:
	free(dvsm->mnt_pt);
 err_4:
	fclose(dvsm->stats_f);
 err_3:
	fclose(dvsm->mount_f);
	if (!dvsm->stats_f || !dvsm->mount_f)
		log_fn(LDMSD_LERROR, SAMP ": Error %d opening '%s'\n", errno, path);
 err_2:
	free(dvsm->dir);
 err_1:
	free(dvsm);
 err_0:
	if (rc == ENOMEM)
		log_fn(LDMSD_LERROR, SAMP ": Memory allocation failure in mount_dvs()\n");
	return rc;
}

int remount_dvs(dvs_mount_t dvsm, const char *path)
{
	unmount_dvs(dvsm);
	return mount_dvs(path);
}

static int handle_old_mount(dvs_mount_t dvsm, const char *path)
{
	int rc;
	int metric_no, line_no;
	char *s;
	char lbuf[256];
	char metric_name[128];
	union ldms_value v;

	/*
	 * Make certain that our dvs_mount data is for this mount,
	 * i.e. the mount point hasn't been reused
	 */
	fseek(dvsm->mount_f, 0, SEEK_SET);
	s = fgets(lbuf, sizeof(lbuf), dvsm->mount_f);
	if (!s) {
		rc = remount_dvs(dvsm, path);
		return rc;
	} else {
		char *mnt_pt, *tmp;
		fseek(dvsm->mount_f, 0, SEEK_SET);
		s = fgets(lbuf, sizeof(lbuf), dvsm->mount_f);
		mnt_pt = strtok_r(s, " ", &tmp);
		mnt_pt = strtok_r(NULL, " ", &tmp);
		rc = strlen(mnt_pt);
		if (rc && mnt_pt[rc-1] == '\n')
			mnt_pt[rc-1] = '\0';
		if (strcmp(mnt_pt, dvsm->mnt_pt)) {
			/* our mount data is stale */
			rc = remount_dvs(dvsm, path);
			return rc;
		}
	}

	base_sample_begin(dvsm->cfg);
	metric_no = metric_offset;
	line_no = 0;
	fseek(dvsm->stats_f, 0, SEEK_SET);
	int cnt, state = 0;
	uint64_t clnt_cnt, clnt_err, srvr_cnt, srvr_err;
	double last_dur, max_dur;
	do {
		s = fgets(lbuf, sizeof(lbuf), dvsm->stats_f);
		if (!s)
			break;
		switch (state) {
		case 0:
			if (s[0] != 'R')
				state ++;
			break;
		case 1:
			if (s[11] == 'x')
				state ++;
			break;
		case 2:
			if (s[0] == 'I')
				state ++;
			break;
		case 3:
			if (s[0] != 'I')
				state ++;
			break;
		}
		switch (state) {
		case 0:
			cnt = sscanf(lbuf, "%127[^:]: %lld %lld %lld %lld %lf %lf",
					 metric_name,
					 &clnt_cnt, &clnt_err, &srvr_cnt, &srvr_err,
					 &last_dur, &max_dur);
			if (cnt != 7) {
				log_fn(LDMSD_LERROR, SAMP ": Error parsing '%s' at line %d, expecting 6 values\n",
				       lbuf, line_no);
				rc = EINVAL;
				break;
			}
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_cnt);
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_err);
			ldms_metric_set_u64(dvsm->set, metric_no++, srvr_cnt);
			ldms_metric_set_u64(dvsm->set, metric_no++, srvr_err);
			ldms_metric_set_double(dvsm->set, metric_no++, last_dur);
			ldms_metric_set_double(dvsm->set, metric_no++, max_dur);
			break;
		case 1:
			cnt = sscanf(lbuf, "%127[^:]: %lld %lld %lf %lf",
				     metric_name,
				     &clnt_cnt, &clnt_err,
				     &last_dur, &max_dur);
			if (cnt != 5) {
				log_fn(LDMSD_LERROR, SAMP ": Error parsing '%s' at line %d, expecting four values\n",
				       lbuf, line_no);
				rc = EINVAL;
				break;
			}
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_cnt);
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_err);
			ldms_metric_set_double(dvsm->set, metric_no++, last_dur);
			ldms_metric_set_double(dvsm->set, metric_no++, max_dur);
			break;
		case 2:
		case 3:
			cnt = sscanf(lbuf, "%127[^:]: %lld %lld",
				     metric_name, &clnt_cnt, &clnt_err);
			if (cnt != 3) {
				log_fn(LDMSD_LERROR, SAMP ": Error parsing '%s' at line %d, expecting 2 values\n",
				       lbuf, line_no);
				rc = EINVAL;
				break;
			}
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_cnt);
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_err);
			break;
		case 4:
			cnt = sscanf(lbuf, "%127[^:]: %lld", metric_name, &clnt_cnt);
			if (cnt != 2) {
				log_fn(LDMSD_LERROR, SAMP ": Error parsing '%s' at line %d, expecting 1 values\n",
				       lbuf, line_no);
				rc = EINVAL;
				break;
			}
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_cnt);
		}
		line_no++;
	} while (s);
	base_sample_end(dvsm->cfg);
	return 0;
}

static int handle_new_mount(const char *dir)
{
	int rc = mount_dvs(dir);
	if (rc)
		return rc;
	dvs_mount_t dvsm = find_dvs(dir);
	if (dvsm)
		return handle_old_mount(dvsm, dir);
}

static int handle_mount(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
	dvs_mount_t dvsm;
	char path[PATH_MAX];
	char *dir;
	if (strcmp(&fpath[ftwbuf->base], "stats"))
		return 0;

	strncpy(path, fpath, PATH_MAX-1);
	dir = dirname(path);

	/* Check the tree to see if we have this already */
	dvsm = find_dvs(dir);
	if (!dvsm)
		return handle_new_mount(dir);
	return handle_old_mount(dvsm, dir);
}

static int sample(struct ldmsd_sampler *self)
{
	if (nftw("/proc/fs/dvs/mounts", handle_mount, 2, FTW_PHYS) < 0) {
		log_fn(LDMSD_LERROR, SAMP ": error walking the dvs procfs stats file");
		return EINVAL;
	}
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	/* TODO: Run through the tree and blow everything away */
	if (cfg_base)
		base_del(cfg_base);
}

static struct ldmsd_sampler meminfo_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

static int mount_comparator(void *a, const void *b)
{
	return strcmp(a, b);
}

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	log_fn = pf;
	rbt_init(&mount_tree, mount_comparator);
	return &meminfo_plugin.base;
}
