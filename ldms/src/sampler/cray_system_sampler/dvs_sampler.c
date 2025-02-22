/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017-2018 Open Grid Computing, Inc. All rights reserved.
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

static ovis_log_t mylog;

static char *procfile = "/proc/fs/dvs";
#define SAMP "cray_dvs_sampler"
static int metric_offset;	/* first non-base metric */
static base_data_t cfg_base;	/* global base for all sets */
static char **cfgmetrics;   /* downselect of metrics from user config */
static int num_cfgmetrics = 0;

typedef struct dvs_mount_s {
	base_data_t cfg; /* The configuration data */
	char *mnt_pt;	 /* The DVS local mount point */
	char *dir;	 /* The path to the stat file */
	FILE *stats_f;	 /* <mount-no>/stats has the dvs statistics */
	FILE *mount_f;   /* <mount-no>/mount has the local mount point */
	ldms_set_t set;
	struct rbn rbn;
	int* use_m; /* Indicator if this metric is to be used or not */
} *dvs_mount_t;

struct rbt mount_tree;

static int _line2basename(char* s, char* name){
	int cnt;
	int i;

	cnt = sscanf(s, "%80[^:]:", name);

	if (cnt < 1) {
		ovis_log(mylog, OVIS_LERROR, "invalid dvs stats file format.\n");
		return EINVAL;
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

	return 0;
}


static int handle_req_ops(dvs_mount_t mount, char *line,
			  char **u64_names, size_t u64_cnt,
			  char **dbl_names, size_t dbl_cnt,
			  int *metric_no)
{
	int i, rc;
	char *base_name;
	char name[80];
	char metric_name[128];
	int metric_count = 0;

	rc = _line2basename(line, name);
	if (rc != 0){
		goto err;
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
	char basename[80];
	int state = 0;
	int line_no = 0;
	int mnt_pt;

	dvsm->cfg = dup_cfg(cfg_base, dvsm->mnt_pt);
	schema = base_schema_new(dvsm->cfg);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, cfg_base->schema_name, errno);
		goto err;
	}
	mnt_pt = ldms_schema_meta_array_add(schema, "mountpt", LDMS_V_CHAR_ARRAY, strlen(dvsm->mnt_pt)+1);

	/*
	 * First pass to determine number of possible metrics
	 */
	line_no = 0;
	fseek(dvsm->stats_f, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), dvsm->stats_f);
		if (!s)
			break;
		line_no++;
	} while (s);

	dvsm->use_m = calloc(line_no, sizeof(int));
	if (!dvsm->use_m){
		rc = ENOMEM;
		goto err;
	}

	/*
	 * Process the file to define all the metrics.
	 */

	/* Location of first metric from dvs file */
	metric_offset = ldms_schema_metric_count_get(schema);

	metric_no = metric_offset;
	line_no = 0;
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
		/*
		 * Check to see if this metric (basename) should be used.
		 * Note: this is in terms of the input line number, not the
		 * metrics since there is more than 1 metric per line
		 */
		int keepmetric = 0;
		rc = _line2basename(s, basename);
		if (rc != 0){
			goto err;
		}
		if (!num_cfgmetrics)
			keepmetric = 1;
		for (i = 0; i < num_cfgmetrics; i++){
			//NOTE: that the following DEBUG is verbose
			ovis_log(mylog, OVIS_LDEBUG,
			       SAMP "will be comparing basename '%s' to keepname '%s'.\n",
			       basename, cfgmetrics[i]);
			if (strcmp(basename,cfgmetrics[i]) == 0){
				keepmetric = 1;
				break;
			}
		}
		dvsm->use_m[line_no++] = keepmetric;
		if (!keepmetric){
			ovis_log(mylog, OVIS_LDEBUG,
			       SAMP "config not including metrics from '%s'.\n", lbuf);
			continue;
		}  else {
			ovis_log(mylog, OVIS_LDEBUG,
			       SAMP "config WILL include metrics from '%s'.\n", lbuf);
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
	ldms_metric_array_set_str(dvsm->set, mnt_pt, dvsm->mnt_pt);

	return 0;

 err:
	return rc;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP "producer=<name> instance=<name> [component_id=<int>] [schema=<name>] [conffile=<cfgfile>]\n"
		"                [job_set=<name> job_id=<name> app_id=<name> job_start=<name> job_end=<name>]\n"
		"    producer     A unique name for the host providing the data\n"
		"    instance     A unique name for the metric set\n"
		"    component_id A unique number for the component being monitored, Defaults to zero.\n"
		"    schema       The name of the metric set schema, Defaults to the sampler name\n"
		"    conffile     Optional file of metricnames for downselection\n"
		"    job_set      The instance name of the set containing the job data, default is 'job_info'\n"
		"    job_id       The name of the metric containing the Job Id, default is 'job_id'\n"
		"    app_id       The name of the metric containing the Application Id, default is 'app_id'\n"
		"    job_start    The name of the metric containing the Job start time, default is 'job_start'\n"
		"    job_end      The name of the metric containing the Job end time, default is 'job_end'\n";

}

static int local_config(struct attr_value_list *avl,
                        const char *name, const char *def_schema)
{
	char lbuf[256];
	char tmpname[256];
	char* value;
	char* s;
	int count = 0;
	int rc=0;
	FILE *cf;

	value = av_value(avl, "conffile");
	if (!value){
		ovis_log(mylog, OVIS_LDEBUG, "no conffile. This is ok.\n");
		return 0;
	}

	/* read the file of downselected metrics. One pass to count, second pass to fill */
	cf = fopen(value, "r");
	if (!cf){
		ovis_log(mylog, OVIS_LERROR, "conffile '%s' cannot be opened\n", value);
		rc = errno;     /* probably EPERM */
		return rc;
	}

	do {
		s = fgets(lbuf, sizeof(lbuf), cf);
		if (!s)
			break;
		count++;
	} while(s);

	cfgmetrics = calloc(count, sizeof(char*));
	if (!cfgmetrics){
		fclose(cf);
		return ENOMEM;
	}

	fseek(cf, 0, SEEK_SET);
	count = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), cf);
		if (!s)
			break;

		rc = sscanf(lbuf," %s", tmpname);
		if ((strlen(tmpname) > 0) && (tmpname[0] != '#')){
			cfgmetrics[count] = strdup(tmpname);
			ovis_log(mylog, OVIS_LDEBUG, "conffile will be keeping metric '%s'\n", tmpname);
		}
		count++;
	} while(s);

	num_cfgmetrics = count;

	fclose(cf);

	return 0;

}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	cfg_base = base_config(avl, self->inst_name, SAMP, mylog);
	if (!cfg_base)
		return EINVAL;
	rc = local_config(avl, SAMP, SAMP);

	return rc;

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
	if (dvsm->use_m)
		free(dvsm->use_m);
	rbt_del(&mount_tree, &dvsm->rbn);
	free(dvsm);
}

int mount_dvs(const char *dir)
{
	int rc = ENOMEM;
	char path[PATH_MAX];
	char lbuf[PATH_MAX];
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
	if (dvsm->use_m)
		free(dvsm->use_m);
	free(dvsm->mnt_pt);
 err_4:
	fclose(dvsm->stats_f);
 err_3:
	fclose(dvsm->mount_f);
	if (!dvsm->stats_f || !dvsm->mount_f)
		ovis_log(mylog, OVIS_LERROR, "Error %d opening '%s'\n", errno, path);
 err_2:
	free(dvsm->dir);
 err_1:
	free(dvsm);
 err_0:
	if (rc == ENOMEM)
		ovis_log(mylog, OVIS_LERROR, "Memory allocation failure in mount_dvs()\n");
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
	char lbuf[PATH_MAX];
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

		if (!dvsm->use_m[line_no]){
			line_no++;
			continue;
		}
		switch (state) {
		case 0:
			cnt = sscanf(lbuf, "%127[^:]: %lld %lld %lld %lld %lf %lf",
					 metric_name,
					 &clnt_cnt, &clnt_err, &srvr_cnt, &srvr_err,
					 &last_dur, &max_dur);
			if (cnt != 7) {
				ovis_log(mylog, OVIS_LERROR, "Error parsing '%s' at line %d, expecting 6 values\n",
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
				ovis_log(mylog, OVIS_LERROR, "Error parsing '%s' at line %d, expecting four values\n",
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
				ovis_log(mylog, OVIS_LERROR, "Error parsing '%s' at line %d, expecting 2 values\n",
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
				ovis_log(mylog, OVIS_LERROR, "Error parsing '%s' at line %d, expecting 1 values\n",
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
		ovis_log(mylog, OVIS_LERROR, "error walking the dvs procfs stats file");
		return EINVAL;
	}
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	int i;

	/* TODO: Run through the tree and blow everything away */
	if (cfg_base)
		base_del(cfg_base);
	for (i = 0; i <num_cfgmetrics; i++){
		free(cfgmetrics[i]);
	}
	free(cfgmetrics);
	cfgmetrics = NULL;
	num_cfgmetrics = 0;
	if (mylog)
		ovis_log_destroy(mylog);
}

static struct ldmsd_sampler dvs_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.sample = sample,
};

static int mount_comparator(void *a, const void *b)
{
	return strcmp(a, b);
}

struct ldmsd_plugin *get_plugin()
{
	int rc;
	mylog = ovis_log_register("sampler."SAMP, "The log subsystem of the " SAMP " plugin");
	if (!mylog) {
		rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the subsystem "
				"of '" SAMP "' plugin. Error %d\n", rc);
	}
	rbt_init(&mount_tree, mount_comparator);
	return &dvs_plugin.base;
}
