/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011-2025 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2011-2025 Open Grid Computing, Inc. All rights reserved.
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
 * \file curated_metrics.c
 * \brief /proc/meminfo data provider
 * \brief /proc/loadavg data provider
 * \brief /proc/stat data provider
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

#include <jansson.h>
#include <assert.h>

#define PROC_MEMINFO "/proc/meminfo"
#define PROC_LOADAVG "/proc/loadavg"
#define PROC_STAT "/proc/stat"
#define SAMP "curated_metrics"
static char *procmeminfo = PROC_MEMINFO;
static char *procloadavg = PROC_LOADAVG;
//static char *procstat = PROC_STAT;
static ldms_set_t set = NULL;
//meminfo
static FILE *mf = 0;
//load avg
static FILE *ml = 0;
//proc stat
//static FILE *ms = 0;
static int metric_offset;
static base_data_t base;
static ovis_log_t mylog;


uint64_t memfree;
uint64_t memtotal;
uint64_t dirty;
uint64_t active;
uint64_t inactive;
uint64_t cached;
uint64_t buffers;
uint64_t writeback;

typedef double (*metric_cb)();

/* available metrics */
typedef struct curated_metrics_s {
	//cur_metric_e code_no;
	char *name;
	int collect;
	char *file_path;
	enum ldms_value_type vtype;
	metric_cb cb;
} *curated_metrics_t;

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a))/sizeof((a)[0]))
#endif
#define CMLEN 8

union la_value {
	double d;
	uint64_t u;
};

void __try_set_u64(ldms_set_t set, int midx , uint64_t val, enum ldms_value_type vtype) {
	if (vtype == LDMS_V_U64)
		ldms_metric_set_u64(set, midx, val);
}
void __try_set_dbl(ldms_set_t set, int midx, double val, enum ldms_value_type vtype) {
	union ldms_value v;
	if (vtype == LDMS_V_D64) {
		v.v_d = val;
		ldms_metric_set(set, midx, &v);
	}
}

double free_cb() {
	return (double)memfree / (double)memtotal;
}

double buff_cb() {
	return (double)buffers / (double)memtotal;
}

double cached_cb() {
	return (double)cached / (double)memtotal;
}

double active_cb() {
	return (double)active / (double)memtotal;
}

double inactive_cb() {
	return (double)inactive / (double)memtotal;
}

double dirty_cb() {
	return (double)dirty / (double)memtotal;
}

double writeback_cb() {
	return (double)writeback / (double)memtotal;
}

double memtotal_cb() {
	return (double)memtotal;
}

struct curated_metrics_s metric_list[] = {
	{ "free",     1, PROC_MEMINFO, LDMS_V_D64, free_cb },
	{ "buff",     1, PROC_MEMINFO, LDMS_V_D64, buff_cb },
	{ "cached",   1, PROC_MEMINFO, LDMS_V_D64, cached_cb },
	{ "inactive", 1, PROC_MEMINFO, LDMS_V_D64, inactive_cb },
	{ "active",   1, PROC_MEMINFO, LDMS_V_D64, active_cb },
	{ "dirty",    1, PROC_MEMINFO, LDMS_V_D64, dirty_cb },
	{ "writeback", 1, PROC_MEMINFO, LDMS_V_D64, writeback_cb },
	{ "phys_mem", 1, PROC_MEMINFO, LDMS_V_D64, memtotal_cb },
	{ "load1min", 1, PROC_LOADAVG, LDMS_V_D64, NULL},
	{ "load5min", 1, PROC_LOADAVG, LDMS_V_D64, NULL },
	{ "load15min", 1, PROC_LOADAVG, LDMS_V_D64, NULL },
	{ "runnable", 1, PROC_LOADAVG, LDMS_V_U64, NULL },
	{ "scheduling_entities", 1, PROC_LOADAVG, LDMS_V_D64, NULL },
	{ "newest_pid", 1, PROC_LOADAVG, LDMS_V_D64, NULL },
};

static int minfo_handler(ldms_set_t set, int metric_no);

/* loadavg vars */
static unsigned force_integer = 0;

struct use_met {
	const char *name;
	int collect;
	enum ldms_value_type vtype;
	union la_value v;
};

#define MET1 "load1min"
#define MET2 "load5min"
#define MET3 "load15min"
#define MET4 "runnable"
#define MET5 "scheduling_entities"
#define MET6 "newest_pid"
struct use_met metname[] = {
	{ MET1, 1, LDMS_V_D64, {0} },
	{ MET2, 1, LDMS_V_D64, {0} },
	{ MET3, 1, LDMS_V_D64, {0} },
	{ MET4, 1, LDMS_V_D64, {0} },
	{ MET5, 1, LDMS_V_D64, {0} },
	{ MET6, 1, LDMS_V_D64, {0} }
};

#define METLEN 6

#ifdef LOADAVG_CONFIG_DEBUG
static void dump_metrics() {
	int i;
	for (i = 0; i < METLEN; i++) {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": %s %d %d\n",
			metname[i].name, metname[i].collect, metname[i].vtype);
	}
}
#endif

static int parse_cmetrics(char *path) {
	int i;
	int rc = 0;
	json_t *cfp, *cfg_metrics;
	json_error_t jerr;
	cfp = json_load_file(path, 0, &jerr);
	if (!cfp) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Could not open json file %s : exiting sampler\n", path);
		errno = ENOENT;
		goto err_1;
	}
	cfg_metrics = json_object_get(cfp, "metrics");
	if (!cfg_metrics) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Key 'metrics' is not present in json file %s\n", path);
		errno = ENOENT;
		goto err_1;
	}
	for (i = 0; i < ARRAY_LEN(metric_list); i++) {
		metric_list[i].collect = 0;
	}
	if (cfg_metrics && json_is_array(cfg_metrics)) {
		int num_metrics = 0;
		int j;
		num_metrics = json_array_size(cfg_metrics);
		for (j = 0; j < num_metrics; j++) {
			const char *mname;
			mname = json_string_value(json_array_get(cfg_metrics, j));
			for (i = 0; i < ARRAY_LEN(metric_list); i++) {
				rc = strcmp(mname, metric_list[i].name);
				if (!rc) {
					metric_list[i].collect = 1;
					//metric_list[i].fn = handler_tbl[i];
					break;
				}
			}
		}
	}
	return 0;
err_1:
	json_decref(cfp);
	return rc;
}

/* end loadavg */
static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	int rc, i;

	mf = fopen(procmeminfo, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open the " SAMP " file "
				"'%s'...exiting sampler\n", procmeminfo);
		return ENOENT;
	}

	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/* Location of first metric from proc/meminfo file */
	metric_offset = ldms_schema_metric_count_get(schema);

	// Derive Free idle mem %
	int metric_list_sz = sizeof(metric_list) / sizeof(metric_list[0]);
	for (i = 0; i < metric_list_sz; i++) {
		if (metric_list[i].collect) {
			rc = ldms_schema_metric_add(schema, metric_list[i].name, metric_list[i].vtype);
			if (rc < 0)
				goto err;
		}
	}
	// End system_memory metrics
	// Begin loadavg metrics
	ml = fopen(procloadavg, "r");
	if (!ml) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Could not open " PROC_LOADAVG
				" : exiting sampler\n");
		return ENOENT;
	}
	fclose(ml);
	ml = NULL;

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

 err:
	if (mf)
		fclose(mf);
	mf = NULL;
	return rc;
}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(mylog, OVIS_LERROR, "config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " " BASE_CONFIG_USAGE;
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, NULL);
	if (rc != 0)
		return rc;

	char *path = av_value(avl, "path");
	if (path) {
		rc = parse_cmetrics(path);
	}

	// loadavg config
	int force = av_idx_of(kwl, "force_integer");
	if (force != -1) {
		force_integer = 1;
	}
#ifdef LOADAVG_CONFIG_DEBUG
	dump_metrics();
	ovis_log(mylog, OVIS_LDEBUG, SAMP ": force_integer=%d.\n", force_integer);
#endif
	// end loadavg config
	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base) {
		rc = errno;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}
	return 0;
err:
	if (base)
		base_del(base);
	return rc;
}

static int logdisappear = 1;

static int minfo_handler(ldms_set_t set, int metric_no) {
	int rc, i;
	char lbuf[256];
	char metric_name[128];
	//union la_value v;
	char *s;

	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s", metric_name);
		if (rc !=1) {
			rc = EINVAL;
			goto out;
		}
                /* Strip the colon from metric name if present */
                i = strlen(metric_name);
                if (i && metric_name[i-1] == ':')
                        metric_name[i-1] = '\0';

		if (strcmp(metric_name, "Active") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &active);
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "MemFree") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &memfree);
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "MemTotal") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &memtotal);
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Dirty") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &dirty);
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "InActive") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &inactive);
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Cached") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &cached);
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Buffers") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &buffers);
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Writeback") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &writeback);
		}
	} while (s);
	double dval;
	uint64_t val;
	for (i = 0; i < ARRAY_LEN(metric_list); i++) {
		if (metric_list[i].collect && strcmp(metric_list[i].file_path, PROC_MEMINFO) == 0) {
			if (metric_list[i].vtype == LDMS_V_D64) {
				dval = metric_list[i].cb();
				__try_set_dbl(set, metric_no, dval, metric_list[i].vtype);
			} else {
				val = metric_list[i].cb();
				__try_set_u64(set, metric_no, val, metric_list[i].vtype);
			}
			metric_no++;
		}
	}
	return 0;
out:
	return rc;
}

int loadavg_handler(ldms_set_t set, int metric_no) {
	int rc, i;
	char lbuf[256];
	char *s;
	if (!ml) {
		ml = fopen(procloadavg, "r");
		if (!ml)
			return 0;
	}

	rc = fseek(ml, 0, SEEK_SET);
	if (rc) {
		if (logdisappear) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": %s disappeared.\n",
				procloadavg);
			logdisappear = 0;
		}
		fclose(ml);
		ml = NULL;
		return 0;
	} else {
		logdisappear = 1;
	}

	s = fgets(lbuf, sizeof(lbuf), ml);
	if (!s) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": fgets failed.\n");
		rc = ENOENT;
		return rc;
	}

#define LINE_FMT "%lg %lg %lg %" SCNu64 "/%" SCNu64 " %" SCNu64 "\n"
	for (i = 0; i < METLEN; i++) {
		metname[0].v.u = 0;
	}
	rc = sscanf(lbuf, LINE_FMT, &metname[0].v.d, &metname[1].v.d, &metname[2].v.d,
		&metname[3].v.u, &metname[4].v.u, &metname[5].v.u);
	if (rc < METLEN) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": fail " PROC_LOADAVG "\n");
		rc = EINVAL;
		return rc;
	}
	int j = 0;
	rc = 0;
	for (i = 0; i < METLEN; i++) {
		if (metname[i].collect) {
			switch (metname[i].vtype) {
			case LDMS_V_D64:
				if (force_integer)
					ldms_metric_set_u64(set, (j + metric_no),
						(uint64_t)(100*metname[i].v.d));
				else
					ldms_metric_set_double(set, (j + metric_no),
						metname[i].v.d);
				break;
			case LDMS_V_U64:
				ldms_metric_set_u64(set, (j + metric_no),
					metname[i].v.u);
				break;
			default:
				ovis_log(mylog, OVIS_LCRITICAL, SAMP ": sample() memory corruption detected.\n");
				rc = EINVAL;
				return rc;
			}
			j++;
		}
	}
	return 0;
}

static int sample(ldmsd_plug_handle_t handle)
{
	int rc;
	int metric_no;

	// loadavg values

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, SAMP "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	metric_no = metric_offset;
	rc = minfo_handler(set, metric_no);
	if (rc)
		goto out;
	rc = loadavg_handler(set, metric_no);
	if (rc)
		goto out;
 out:
	base_sample_end(base);
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	int rc;
	mylog = ldmsd_plug_log_get(handle);
	set = NULL;
	if (!mylog) {
		rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the subsystem "
				"of '" SAMP "' plugin. Error %d\n", rc);
	}
	return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	if (mf)
		fclose(mf);
	mf = NULL;
	if (ml)
		fclose(ml);
	ml = NULL;
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.flags = LDMSD_PLUGIN_MULTI_INSTANCE,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
