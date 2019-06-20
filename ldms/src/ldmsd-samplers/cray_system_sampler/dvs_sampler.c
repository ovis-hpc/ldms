/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017-2019 Open Grid Computing, Inc. All rights reserved.
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
#include <assert.h>
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
#include <coll/rbt.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>


#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define DVS_DIR "/proc/fs/dvs/mounts"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
			  ##__VA_ARGS__)

#define _SNPRINTF(inst, s, slen, fmt, ...) \
		snprintf(s, slen, "%s: " fmt, INST(inst)->inst_name, \
			  ##__VA_ARGS__)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*x))
#endif

typedef struct dvs_mount_s {
	char *mnt_pt;	 /* The DVS local mount point */
	char *dir;	 /* The path to the stat file */
	FILE *stats_f;	 /* <mount-no>/stats has the dvs statistics */
	FILE *mount_f;   /* <mount-no>/mount has the local mount point */
	ldms_set_t set;
	struct rbn rbn;
	ldms_schema_t schema;
	int* use_m; /* Indicator if this metric is to be used or not */
} *dvs_mount_t;

typedef struct dvs_sampler_inst_s *dvs_sampler_inst_t;
struct dvs_sampler_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */

	DIR *dir;

	int configured;
	char **cfgmetrics;  /* downselect of metrics from user config */
	int num_cfgmetrics;


	dvs_mount_t dvsm; /* for constructing schema + metric set */

	struct rbt mount_tree; /* tracking each mount */
};

static int _line2basename(char* s, char* name)
{
	int cnt;
	int i;

	cnt = sscanf(s, "%80[^:]:", name);

	if (cnt < 1) {
		ldmsd_log(LDMSD_LERROR, "invalid dvs stats file format.\n");
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

static int handle_req_ops(ldms_schema_t schema, char *line,
			  char **u64_names, size_t u64_cnt,
			  char **dbl_names, size_t dbl_cnt)
{
	int i, rc;
	char name[80];
	char metric_name[128];
	int metric_count = 0;

	rc = _line2basename(line, name);
	if (rc != 0) {
		goto err;
	}

	for (i = 0; i < u64_cnt; i++) {
		if (u64_names[i][0] == '\0')
			sprintf(metric_name, "%s", name);
		else
			sprintf(metric_name, "%s_%s", name, u64_names[i]);
		rc = ldms_schema_metric_add(schema, metric_name,
					    LDMS_V_U64, "");
		if (rc < 0)
			goto err;
		metric_count++;
	}
	for (i = 0; i < dbl_cnt; i++) {
		if (dbl_names[i][0] == '\0')
			sprintf(metric_name, "%s", name);
		else
			sprintf(metric_name, "%s_%s", name, dbl_names[i]);
		rc = ldms_schema_metric_add(schema, metric_name,
					    LDMS_V_D64, "");
		if (rc < 0)
			goto err;
		metric_count++;
	}
	return 0;
 err:
	return EINVAL;
}

/* ============== Sampler Plugin APIs ================= */

static
int dvs_sampler_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	dvs_sampler_inst_t inst = (void*)pi;
	dvs_mount_t dvsm = inst->dvsm;

	static char *RQ_u64_names[] = { "clnt_cnt", "clnt_err", "srvr_cnt",
					"srvr_err"};
	static char *RQ_dbl_names[] = { "last_dur", "max_dur" };
	static char *fs_u64_names[] = { "cnt", "err" };
	static char *fs_dbl_names[] = { "last_dur", "max_dur" };
	static char *mm_u64_names[] = { "min", "max" };
	static char *cnt_u64_names[] = { "" };

	static struct {
		char **u64_names;
		int u64_num;
		char **dbl_names;
		int dbl_num;
	} *ent, ents[] = {
		/* state 0: RQ_* metrics */
		{ RQ_u64_names, ARRAY_SIZE(RQ_u64_names),
		  RQ_dbl_names, ARRAY_SIZE(RQ_dbl_names) },
		/* state 1: fs metrics */
		{ fs_u64_names, ARRAY_SIZE(fs_u64_names),
		  fs_dbl_names, ARRAY_SIZE(fs_dbl_names) },
		/* state 2: min/max metircs */
		{ mm_u64_names, ARRAY_SIZE(mm_u64_names), NULL, 0 },
		/* state 3: IPC metrics */
		{ fs_u64_names, ARRAY_SIZE(fs_u64_names), NULL, 0 },
		/* state 4: Open/Inodes counters */
		{ cnt_u64_names, ARRAY_SIZE(cnt_u64_names), NULL, 0 },
	};

	int rc;
	char *s;
	char lbuf[256];
	char basename[80];
	int state = 0;
	int line_no = 0;
	int mnt_pt;

	mnt_pt = ldms_schema_metric_array_add(schema, "mountpt",
				LDMS_V_CHAR_ARRAY, "", strlen(dvsm->mnt_pt)+1);

	line_no = 0;
	fseek(dvsm->stats_f, 0, SEEK_SET);
	while ((s = fgets(lbuf, sizeof(lbuf), dvsm->stats_f))) {
		line_no++;
	}
	dvsm->use_m = calloc(line_no, sizeof(int));
	if (!dvsm->use_m)
		return ENOMEM;
	line_no = 0;
	fseek(dvsm->stats_f, 0, SEEK_SET);
	while ((s = fgets(lbuf, sizeof(lbuf), dvsm->stats_f))) {
		/* NOTE
		 * state 0: RQ_* metrics
		 * state 1: fs metrics
		 * state 2: min/max metrics
		 * state 3: IPC metrics
		 * state 4: Open files / Inode counters
		 */
		switch (state) {
		case 0:
			if (s[0] != 'R')
				state ++;
			break;
		case 1:
			if (s[11] == 'x') /* from "read_min_max" */
				state ++;
			break;
		case 2:
			if (s[0] == 'I') /* from "IPC .." */
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
		if (rc != 0)
			goto err;
		if (inst->num_cfgmetrics) {
			keepmetric = !!bsearch(basename, inst->cfgmetrics,
						inst->num_cfgmetrics,
						sizeof(char*), (void*)strcmp);
		} else {
			keepmetric = 1;
		}
		dvsm->use_m[line_no++] = keepmetric;
		if (!keepmetric){
			INST_LOG(inst, LDMSD_LDEBUG,
				 "config not including metrics from '%s'.\n",
				 lbuf);
			continue;
		}  else {
			INST_LOG(inst, LDMSD_LDEBUG,
				 "config WILL include metrics from '%s'.\n",
				 lbuf);
		}

		ent = &ents[state];
		rc = handle_req_ops(schema, s,
				    ent->u64_names, ent->u64_num,
				    ent->dbl_names, ent->dbl_num);
		if (rc)
			goto err;
	}
	return 0;

err:
	return rc;
}

static
int dvs_sampler_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	dvs_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	dvs_mount_t dvsm = ctxt;
	int cnt, state = 0;
	uint64_t clnt_cnt, clnt_err, srvr_cnt, srvr_err;
	double last_dur, max_dur;
	char *s;
	char lbuf[256];
	int line_no;
	int metric_no;

	fseek(dvsm->stats_f, 0, SEEK_SET);
	line_no = 0;
	/* samp->fist_idx is `mountpt` */
	metric_no = samp->first_idx + 1;
	while ((s = fgets(lbuf, sizeof(lbuf), dvsm->stats_f))) {
		/* NOTE
		 * state 0: RQ_* metrics
		 * state 1: fs metrics
		 * state 2: min/max metrics
		 * state 3: IPC metrics
		 * state 4: Open files / Inode counters
		 */
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
			cnt = sscanf(lbuf, "%*[^:]: %ld %ld %ld %ld %lf %lf",
					 &clnt_cnt, &clnt_err, &srvr_cnt,
					 &srvr_err, &last_dur, &max_dur);
			if (cnt != 6) {
				INST_LOG(inst, LDMSD_LERROR,
					 "Error parsing '%s' at line %d, "
					 "expecting 6 values\n",
					 lbuf, line_no);
				return EINVAL;
			}
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_cnt);
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_err);
			ldms_metric_set_u64(dvsm->set, metric_no++, srvr_cnt);
			ldms_metric_set_u64(dvsm->set, metric_no++, srvr_err);
			ldms_metric_set_double(dvsm->set, metric_no++, last_dur);
			ldms_metric_set_double(dvsm->set, metric_no++, max_dur);
			break;
		case 1:
			cnt = sscanf(lbuf, "%*[^:]: %ld %ld %lf %lf",
				     &clnt_cnt, &clnt_err, &last_dur, &max_dur);
			if (cnt != 4) {
				INST_LOG(inst, LDMSD_LERROR,
					 "Error parsing '%s' at line %d, "
					 "expecting four values\n",
					 lbuf, line_no);
				return EINVAL;
			}
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_cnt);
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_err);
			ldms_metric_set_double(dvsm->set, metric_no++, last_dur);
			ldms_metric_set_double(dvsm->set, metric_no++, max_dur);
			break;
		case 2:
		case 3:
			cnt = sscanf(lbuf, "%*[^:]: %ld %ld",
				     &clnt_cnt, &clnt_err);
			if (cnt != 2) {
				INST_LOG(inst, LDMSD_LERROR,
					 "Error parsing '%s' at line %d, "
					 "expecting 2 values\n",
					 lbuf, line_no);
				return EINVAL;
			}
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_cnt);
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_err);
			break;
		case 4:
			cnt = sscanf(lbuf, "%*[^:]: %ld", &clnt_cnt);
			if (cnt != 1) {
				INST_LOG(inst, LDMSD_LERROR,
					 "Error parsing '%s' at line %d, "
					 "expecting 1 values\n",
					 lbuf, line_no);
				return EINVAL;
			}
			ldms_metric_set_u64(dvsm->set, metric_no++, clnt_cnt);
		}
		line_no++;
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *dvs_sampler_desc(ldmsd_plugin_inst_t pi)
{
	return "dvs_sampler - dvs_sampler sampler plugin";
}

static
char *_help = "\
dvs_sampler configuration synopsis:\n\
    config name=INST [COMMON_OPTIONS] [conffile=<cfgfile>]\n\
\n\
Option descriptions:\n\
    conffile     Optional file of metricnames for downselection\n\
";

static
const char *dvs_sampler_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

int __downselect(dvs_sampler_inst_t inst, const char *cfgpath,
		 char *ebuf, int ebufsz)
{
	FILE *f;
	char lbuf[256], *s;
	char tmpname[256];
	int rc, count, i;

	f = fopen(cfgpath, "r");
	if (!f) {
		_SNPRINTF(inst, ebuf, ebufsz, "cannot open conffile `%s`, "
			  "errno: %d\n", cfgpath, errno);
		return errno;
	}

	count = 0;
	while ((s = fgets(lbuf, sizeof(lbuf), f))) {
		rc = sscanf(lbuf," %s", tmpname);
		if (rc == 1 && (strlen(tmpname) > 0) && (tmpname[0] != '#')) {
			count++;
		}
	}

	inst->cfgmetrics = calloc(count, sizeof(char*));
	if (!inst->cfgmetrics) {
		_SNPRINTF(inst, ebuf, ebufsz, "not enough memory.\n");
		rc = ENOMEM;
		goto out;
	}
	i = 0;
	fseek(f, 0, SEEK_SET);
	while ((s = fgets(lbuf, sizeof(lbuf), f))) {
		rc = sscanf(lbuf," %s", tmpname);
		if (rc == 1 && (strlen(tmpname) > 0) && (tmpname[0] != '#')) {
			assert(i < count);
			if (i >= count) {
				_SNPRINTF(inst, ebuf, ebufsz,
					  "config file changed while "
					  "processing.\n");
				rc = EINVAL;
				goto out;
			}
			inst->cfgmetrics[i] = strdup(tmpname);
			if (!inst->cfgmetrics[i]) {
				_SNPRINTF(inst, ebuf, ebufsz,
					  "not enough memory.\n");
				rc = ENOMEM;
				goto out;
			}
			i++;
		}
	}
	assert(i == count);
	if (i != count) {
		_SNPRINTF(inst, ebuf, ebufsz,
			  "config file changed while processing.\n");
		rc = EINVAL;
		goto out;
	}
	inst->num_cfgmetrics = count;
	qsort(inst->cfgmetrics, count, sizeof(char *), (void*)strcmp);
out:
	fclose(f);
	return rc;
}

static
int dvs_sampler_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	dvs_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int rc;
	const char *value;

	if (inst->configured) {
		_SNPRINTF(inst, ebuf, ebufsz, "already configured\n");
		return EALREADY;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	value = json_attr_find_str(json, "conffile");
	if (value) {
		rc = __downselect(inst, value, ebuf, ebufsz);
		return rc;
	}

	/* set is created in dvs_sampler_sample() when a new mountpoint is
	 * detected. */
	inst->configured = 1;

	return 0;
}

dvs_mount_t find_dvs(dvs_sampler_inst_t inst, const char *dir)
{
	struct rbn *rbn = rbt_find(&inst->mount_tree, dir);
	if (rbn)
		return container_of(rbn, struct dvs_mount_s, rbn);
	return NULL;
}

dvs_mount_t mount_dvs(dvs_sampler_inst_t inst, const char *dir)
{
	int rc = ENOMEM;
	char path[PATH_MAX];
	char lbuf[128];
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	dvs_mount_t dvsm;
	ldms_schema_t schema;
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
	inst->dvsm = dvsm;
	schema = samp->create_schema((void*)inst);
	inst->dvsm = NULL;
	snprintf(path, sizeof(path), "%s%s", samp->producer_name, mnt_pt);
	dvsm->set = samp->create_set((void*)inst, path, schema, dvsm);
	ldms_schema_delete(schema);
	if (!dvsm->set) {
		rc = errno;
		goto err_5;
	}

	ldms_metric_array_set_str(dvsm->set, samp->first_idx, mnt_pt);

	rbn_init(&dvsm->rbn, dvsm->dir);
	rbt_ins(&inst->mount_tree, &dvsm->rbn);

	fclose(dvsm->mount_f);
	dvsm->mount_f = NULL;

	return dvsm;

 err_5:
	if (dvsm->use_m)
		free(dvsm->use_m);
	free(dvsm->mnt_pt);
 err_4:
	fclose(dvsm->stats_f);
 err_3:
	fclose(dvsm->mount_f);
	if (!dvsm->stats_f || !dvsm->mount_f)
		INST_LOG(inst, LDMSD_LERROR,
			 "Error %d opening '%s'\n", errno, path);
 err_2:
	free(dvsm->dir);
 err_1:
	free(dvsm);
 err_0:
	if (rc == ENOMEM)
		INST_LOG(inst, LDMSD_LERROR,
			 "Memory allocation failure in mount_dvs()\n");
	return NULL;
}

/*
 * Unmount and free dvsm
 */
void unmount_dvs(dvs_sampler_inst_t inst, dvs_mount_t dvsm)
{
	rbt_del(&inst->mount_tree, &dvsm->rbn);
	if (dvsm->mnt_pt) {
		free(dvsm->mnt_pt);
		dvsm->mnt_pt = NULL;
	}
	if (dvsm->stats_f) {
		fclose(dvsm->stats_f);
		dvsm->stats_f = NULL;
	}
	if (dvsm->mount_f) {
		fclose(dvsm->mount_f);
		dvsm->mount_f = NULL;
	}
	if (dvsm->set) {
		ldms_set_unpublish(dvsm->set);
		ldms_set_delete(dvsm->set);
		dvsm->set = NULL;
	}
	if (dvsm->dir) {
		free(dvsm->dir);
	}
	free(dvsm);
}

/*
 * stale `local mount` >>> update mnt, delete set, create set
 *
 * REUSE dvsm (dir doesn't change)
 */
int remount_dvs(dvs_sampler_inst_t inst, dvs_mount_t dvsm)
{
	char path[PATH_MAX];
	FILE *f = NULL;
	int rc;
	ldms_schema_t schema;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;

	/* cleanup old stuff */
	if (dvsm->mnt_pt) {
		free(dvsm->mnt_pt);
		dvsm->mnt_pt = NULL;
	}
	if (dvsm->stats_f) {
		fclose(dvsm->stats_f);
		dvsm->stats_f = NULL;
	}
	if (dvsm->mount_f) {
		fclose(dvsm->mount_f);
		dvsm->mount_f = NULL;
	}
	if (dvsm->set) {
		ldms_set_unpublish(dvsm->set);
		ldms_set_delete(dvsm->set);
		dvsm->set = NULL;
	}

	/* reopen stats */
	snprintf(path, sizeof(path), "%s/stats", dvsm->dir);
	dvsm->stats_f = fopen(path, "r");
	if (!dvsm->stats_f)
		return errno;
	/* refresh mnt_pt */
	snprintf(path, sizeof(path), "%s/mount", dvsm->dir);
	f = fopen(path, "r");
	if (!f)
		return errno;
	rc = fscanf(f, "%*s %ms", &dvsm->mnt_pt);
	fclose(f);
	if (rc < 1)
		return EINVAL;
	/* schema + set */
	inst->dvsm = dvsm;
	schema = samp->create_schema((void*)inst);
	if (!schema)
		return errno;
	snprintf(path, sizeof(path), "%s%s", samp->producer_name, dvsm->mnt_pt);
	dvsm->set = samp->create_set((void*)inst, path, schema, dvsm);
	ldms_schema_delete(schema);
	if (!dvsm->set)
		return errno;
	return 0;
}

/*
 * returns ENOENT if the stat file disappeared
 * returns ESTALE if the local mount point does not match the key
 * returns 0      if everything seems to be OK.
 */
int check_dvs(dvs_sampler_inst_t inst, dvs_mount_t dvsm)
{
	char path[256];
	int rc;
	char *mnt = NULL;
	snprintf(path, sizeof(path), "%s/mount", dvsm->dir);
	dvsm->mount_f = fopen(path, "r");
	if (!dvsm->mount_f)
		return ENOENT;
	rc = fscanf(dvsm->mount_f, "%*s %ms", &mnt);
	fclose(dvsm->mount_f);
	dvsm->mount_f = NULL;
	if (rc < 1)
		return ESTALE;
	rc = strcmp(dvsm->mnt_pt, mnt);
	if (mnt)
		free(mnt);
	if (rc)
		return ESTALE;
	return 0;
}

int __dir_filter(const struct dirent *dent)
{
	if (dent->d_name[0] == '.')
		return 0;
	return 1;
}

int dvs_sampler_sample(ldmsd_plugin_inst_t pi)
{
	dvs_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	struct dirent *dent;
	char path[512];
	struct stat st;
	int i, n, rc;
	char *s;
	dvs_mount_t dvsm;
	ldmsd_set_entry_t sent, next;
	struct dirent **dlist = NULL;

	inst->dir = opendir(DVS_DIR);
	if (!inst->dir)
		return errno;

	/* detecting new mounts */
	n = scandir(DVS_DIR, &dlist, __dir_filter, alphasort);
	for (i = 0; i < n; i++) {
		dent = dlist[i];
		snprintf(path, sizeof(path), "%s/%s/stats",
			 DVS_DIR, dent->d_name);
		rc = lstat(path, &st);
		if (rc)
			continue;
		s = strrchr(path, '/');
		*s = 0;
		dvsm = find_dvs(inst, path);
		if (dvsm)
			continue; /* dvsm existed */
		dvsm = mount_dvs(inst, path);
		if (!dvsm) {
			INST_LOG(inst, LDMSD_LERROR,
				 "mount_dvs() failed, errno: %d\n", errno);
			rc = errno;
			goto out;
		}
	}

	/* remount stale mounts / removing deleted mounts */
	sent = LIST_FIRST(&samp->set_list);
	while (sent) {
		dvsm = sent->ctxt;
		next = LIST_NEXT(sent, entry); /* so that sent can be removed */
		switch (check_dvs(inst, dvsm)) {
		case ENOENT:
			LIST_REMOVE(sent, entry);
			/* remove mount; which also delete set */
			unmount_dvs(inst, dvsm);
			break;
		case ESTALE:
			LIST_REMOVE(sent, entry);
			/* remount also delete stale set. New set is inserted
			 * at the HEAD of the list, hence we won't process the
			 * same list twice. The new set context is the old
			 * dvsm. */
			rc = remount_dvs(inst, dvsm);
			if (rc)
				goto out;
			/* let through */
		case 0:
			ldms_transaction_begin(dvsm->set);
			samp->base_update_set(pi, dvsm->set);
			samp->update_set(pi, dvsm->set, dvsm);
			ldms_transaction_end(dvsm->set);
			break;
		}
		sent = next;
	}
	rc = 0;
 out:
	if (dlist) {
		for (i = 0; i < n; i++)
			free(dlist[i]);
		free(dlist);
	}
	closedir(inst->dir);
	inst->dir = NULL;
	return rc;
}

static
int dvs_sampler_init(ldmsd_plugin_inst_t pi)
{
	dvs_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = dvs_sampler_update_schema;
	samp->update_set = dvs_sampler_update_set;

	/* also override sample() */
	samp->sample = dvs_sampler_sample;

	/* NOTE More initialization code here if needed */
	rbt_init(&inst->mount_tree, (void*)strcmp);
	return 0;
}

static
void dvs_sampler_del(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	dvs_sampler_inst_t inst = (void*)pi;
	ldmsd_set_entry_t sent;
	dvs_mount_t dvsm;

	/* destroy set entries and mounts */
	while ((sent = LIST_FIRST(&samp->set_list))) {
		dvsm = sent->ctxt;
		LIST_REMOVE(sent, entry);
		/* unmount deletes the set */
		unmount_dvs(inst, dvsm);
	}
}

static
struct dvs_sampler_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "dvs_sampler",

                /* Common Plugin APIs */
		.desc   = dvs_sampler_desc,
		.help   = dvs_sampler_help,
		.init   = dvs_sampler_init,
		.del    = dvs_sampler_del,
		.config = dvs_sampler_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	dvs_sampler_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
