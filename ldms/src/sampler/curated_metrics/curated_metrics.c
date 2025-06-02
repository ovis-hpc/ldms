/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011-2026 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2011-2026 Open Grid Computing, Inc. All rights reserved.
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
int smpl_minfo = 0;
static char *procloadavg = PROC_LOADAVG;
int smpl_loadavg = 0;
static char *procstat = PROC_STAT;
int smpl_procstat = 0;
static int metric_offset;
time_t prev_time;
time_t curr_time;

// meminfo vars
uint64_t memfree;
uint64_t memtotal;
uint64_t dirty;
uint64_t active;
uint64_t inactive;
uint64_t cached;
uint64_t buffers;
uint64_t writeback;

// procstat vars
typedef struct cpu_data {
	uint64_t user;
	uint64_t nice;
	uint64_t sys;
	uint64_t idle;
	uint64_t iowait;
	uint64_t irq;
	uint64_t softirq;
	uint64_t steal;
	uint64_t guest;
	uint64_t guest_nice;
} cpu_t;

cpu_t prev_cpu, curr_cpu;
uint64_t prev_ctxt, curr_ctxt;
uint64_t prev_intr, curr_intr;
uint64_t prev_fork, curr_fork;
double prev_cpu_sum, curr_cpu_sum;
double cpu_sum;
double cpu_idle;
double cpu_sys;
double cpu_user;
double cpu_iowait;
double cpu_stolen;
double cpu_intr;   // per second
double cpu_steal;
double cpu_ctxt; // per second
double cpu_fork;
double cpu_core_no;

#define LBUFSZ 256
#define PROCBUFSZ 65536

typedef double (*metric_cb)();

/* available metrics */
typedef struct cm_s {
	char *name;
	int collect;
	char *file_path;
	enum ldms_value_type vtype;
	metric_cb cb;
} *cm_t;

//call getpid() to get process id for ldmsd stats

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a))/sizeof((a)[0]))
#endif
#define CMLEN 8

union la_value {
	double d;
	uint64_t u;
};

void __try_set_u64(ldms_set_t set, int *midx , uint64_t val, enum ldms_value_type vtype) {
	if (vtype == LDMS_V_U64)
		ldms_metric_set_u64(set, *midx, val);
}
void __try_set_dbl(ldms_set_t set, int *midx, double val, enum ldms_value_type vtype) {
	union ldms_value v;
	if (vtype == LDMS_V_D64) {
		v.v_d = val;
		ldms_metric_set(set, *midx, &v);
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

double idle_cb() {
	return (double)cpu_idle / (double)cpu_sum;
}
double sys_cb() {
	return (double)cpu_sys / (double)cpu_sum;
}
double user_cb() {
	return (double)cpu_user / (double)cpu_sum;
}
double iowait_cb() {
	return (double)cpu_iowait / (double)cpu_sum;
}
double stolen_cb() {
	return (double)cpu_stolen / (double)cpu_sum;
}
double intr_cb() {
	return cpu_intr / ((double)curr_time - (double)prev_time);
}
double ctxt_cb() {
	return cpu_ctxt / ((double)curr_time - (double)prev_time);
}
double fork_cb() {
	return cpu_fork / ((double)curr_time - (double)prev_time);
}

typedef struct curated_metrics_s {
	//meminfo
	FILE *mf;
	//load avg
	FILE *lf;
	//proc stat
	FILE *sf;
	ldms_set_t set;
	ovis_log_t log;
	int metrics_offset;
	base_data_t base;
} *curated_metrics_t;

struct cm_s metric_list[] = {
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
	{ "idle", 1, PROC_STAT, LDMS_V_D64, idle_cb },
	{ "sys", 1, PROC_STAT, LDMS_V_D64, sys_cb },
	{ "user", 1, PROC_STAT, LDMS_V_D64, user_cb },
	{ "iowait", 1, PROC_STAT, LDMS_V_D64, iowait_cb },
	{ "stolen", 1, PROC_STAT, LDMS_V_D64, stolen_cb },
	{ "interrupts", 1, PROC_STAT, LDMS_V_D64, intr_cb },
	{ "context_sw", 1, PROC_STAT, LDMS_V_D64, ctxt_cb },
	{ "fork", 1, PROC_STAT, LDMS_V_D64, fork_cb }
};

static int minfo_handler(curated_metrics_t mi, int *metric_no);

static int cpu_handler(curated_metrics_t mi, int *metric_no);

/* loadavg vars */
static unsigned force_integer = 0;

struct use_loadavg {
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
struct use_loadavg metname[] = {
	{ MET1, 1, LDMS_V_D64, {0} },
	{ MET2, 1, LDMS_V_D64, {0} },
	{ MET3, 1, LDMS_V_D64, {0} },
	{ MET4, 1, LDMS_V_D64, {0} },
	{ MET5, 1, LDMS_V_D64, {0} },
	{ MET6, 1, LDMS_V_D64, {0} }
};

#define METLEN 6

#ifdef LOADAVG_CONFIG_DEBUG
static void dump_metrics(curated_metrics_t mi) {
	int i;
	for (i = 0; i < METLEN; i++) {
		ovis_log(mi->log, OVIS_LDEBUG, SAMP ": %s %d %d\n",
			metname[i].name, metname[i].collect, metname[i].vtype);
	}
}
#endif

enum stat_row {
	STAT_CPU = 1,
	STAT_CTXT,
	STAT_BTIME,
	STAT_PROCESSES,
	STAT_PROCS_RUNNING,
	STAT_PROCS_BLOCKED,
	STAT_INTR,
	STAT_SOFTIRQ,
};

struct stat_row_ent {
	const char *prefix;
	enum stat_row type;
};

struct stat_row_ent stat_row_ents[] = {
	/* sorted by prefix for bsearch */
	{ "btime",         STAT_BTIME         },
	{ "cpu",           STAT_CPU           },
	{ "ctxt",          STAT_CTXT          },
	{ "intr",          STAT_INTR          },
	{ "processes",     STAT_PROCESSES     },
	{ "procs_blocked", STAT_PROCS_BLOCKED },
	{ "procs_running", STAT_PROCS_RUNNING },
	{ "softirq",       STAT_SOFTIRQ       },
};

int stat_row_cmp(const void *key, const void *_ent)
{
	const struct stat_row_ent *ent = _ent;
	return strcmp(key, ent->prefix);
}

/* end procstat */

static int parse_cmetrics(curated_metrics_t mi, char *path) {
	int i;
	int rc = 0;
	json_t *cfp, *cfg_metrics;
	json_error_t jerr;
	cfp = json_load_file(path, 0, &jerr);
	if (!cfp) {
		ovis_log(mi->log, OVIS_LERROR, SAMP ": Could not open json file %s : exiting sampler\n", path);
		errno = ENOENT;
		goto err_1;
	}
	cfg_metrics = json_object_get(cfp, "metrics");
	if (!cfg_metrics) {
		ovis_log(mi->log, OVIS_LERROR, SAMP ": Key 'metrics' is not present in json file %s\n", path);
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
static int create_metric_set(curated_metrics_t mi)
{
	ldms_schema_t schema;
	int rc, i;
	//uint64_t metric_value;
	//char *s;
	//char lbuf[LBUFSZ];
	//char metric_name[LBUFSZ];

	mi->mf = fopen(procmeminfo, "r");
	if (!mi->mf) {
		ovis_log(mi->log, OVIS_LERROR, "Could not open the " SAMP " file "
				"'%s'...exiting sampler\n", procmeminfo);
		return ENOENT;
	}

	schema = base_schema_new(mi->base);
	if (!schema) {
		ovis_log(mi->log, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, mi->base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/* Location of first metric from proc/meminfo file */
	metric_offset = ldms_schema_metric_count_get(schema);

	/*
	 * system_memory metrics.
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		rc = sscanf(lbuf, "%s %" PRIu64,
			    metric_name, &metric_value);
		if (rc < 2)
			break;

	} while (s);
	 */
	// Derive Free idle mem %
	//
	int metric_list_sz = sizeof(metric_list) / sizeof(metric_list[0]);
	for (i = 0; i < metric_list_sz; i++) {
		if (metric_list[i].collect) {
			rc = ldms_schema_metric_add(schema, metric_list[i].name, metric_list[i].vtype);
			if (rc < 0)
				goto err;
		}
	}
	/*
	*/
	// End system_memory metrics
	// Being loadavg metrics
	mi->lf = fopen(procloadavg, "r");
	if (!mi->lf) {
		ovis_log(mi->log, OVIS_LERROR, SAMP ": Could not open " PROC_LOADAVG
				" : exiting sampler\n");
		return ENOENT;
	}
	fclose(mi->lf);
	mi->lf = NULL;

	mi->sf = fopen(procstat, "r");
	if (!mi->sf) {
		ovis_log(mi->log, OVIS_LERROR, SAMP ": Could not open " PROC_STAT
				" : exiting sampler\n");
		return ENOENT;
	}
	fclose(mi->sf);
	mi->sf = NULL;
	mi->set = base_set_new(mi->base);
	if (!mi->set) {
		rc = errno;
		goto err;
	}

	return 0;

 err:
	if (mi->mf)
		fclose(mi->mf);
	mi->mf = NULL;
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " " BASE_CONFIG_USAGE;
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	curated_metrics_t mi = ldmsd_plug_ctxt_get(handle);
	int rc;

	mi->log = ldmsd_plug_log_get(handle);
	if (mi->set) {
		ovis_log(mi->log, OVIS_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}


	char *path = av_value(avl, "path");
	if (path) {
		rc = parse_cmetrics(mi, path);
	}

	// loadavg config
	int force = av_idx_of(kwl, "force_integer");
	if (force != -1) {
		force_integer = 1;
	}
#ifdef LOADAVG_CONFIG_DEBUG
	dump_metrics(mi);
	ovis_log(mi->log, OVIS_LDEBUG, SAMP ": force_integer=%d.\n", force_integer);
#endif
	// end loadavg config
	mi->base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mi->log);
	if (!mi->base) {
		rc = errno;
		goto err;
	}

	rc = create_metric_set(mi);
	if (rc) {
		ovis_log(mi->log, OVIS_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}
	return 0;
err:
	if (mi->base)
		base_del(mi->base);
	return rc;
}

// Remove for curated_metrics
static int logdisappear = 1;

static int minfo_handler(curated_metrics_t mi, int *metric_no) {
	int rc, i;
	char lbuf[256];
	char metric_name[128];
	//union la_value v;
	char *s;
	fseek(mi->mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mi->mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s", metric_name);
		if (rc !=1) {
			rc = EINVAL;
			goto out;
		}
		rc = 0;
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
				__try_set_dbl(mi->set, metric_no, dval, metric_list[i].vtype);
			} else {
				val = metric_list[i].cb();
				__try_set_u64(mi->set, metric_no, val, metric_list[i].vtype);
			}
			(*metric_no)++;
		}
	}
out:
	return rc;
}

int loadavg_handler(curated_metrics_t mi, int *metric_no) {
	int rc, i;
	char lbuf[256];
	char *s;
	if (!mi->lf) {
		mi->lf = fopen(procloadavg, "r");
		if (!mi->lf)
			return 0;
	}

	rc = fseek(mi->lf, 0, SEEK_SET);
	if (rc) {
		if (logdisappear) {
			ovis_log(mi->log, OVIS_LERROR, SAMP ": %s disappeared.\n",
				procloadavg);
			logdisappear = 0;
		}
		fclose(mi->lf);
		mi->lf = NULL;
		return 0;
	} else {
		logdisappear = 1;
	}

	s = fgets(lbuf, sizeof(lbuf), mi->lf);
	if (!s) {
		ovis_log(mi->log, OVIS_LERROR, SAMP ": fgets failed.\n");
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
		ovis_log(mi->log, OVIS_LERROR, SAMP ": fail " PROC_LOADAVG "\n");
		rc = EINVAL;
		return rc;
	}
	rc = 0;
	for (i = 0; i < METLEN; i++) {
		if (metname[i].collect) {
			switch (metname[i].vtype) {
			case LDMS_V_D64:
				if (force_integer)
					ldms_metric_set_u64(mi->set, *metric_no,
						(uint64_t)(100*metname[i].v.d));
				else
					ldms_metric_set_double(mi->set, *metric_no,
						metname[i].v.d);
				break;
			case LDMS_V_U64:
				ldms_metric_set_u64(mi->set, *metric_no,
					metname[i].v.u);
				break;
			default:
				ovis_log(mi->log, OVIS_LCRITICAL, SAMP ": sample() memory corruption detected.\n");
				rc = EINVAL;
				return rc;
			}
			(*metric_no)++;
		}
	}
	return rc;
}

static int cpu_handler(curated_metrics_t mi, int *metric_no) {
	int i, rc;
	char tok[128];
	int n;
	struct stat_row_ent *ent;
	int cpu_total = 0;

	if (!mi->set) {
		ovis_log(mi->log, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}
	if (!prev_time)
		prev_time = time(NULL);
	mi->sf = fopen(procstat, "r");
	while (1 == fscanf(mi->sf, "%s", tok)) {
		printf("%s\n", tok);
		ent = bsearch(tok, stat_row_ents, ARRAY_LEN(stat_row_ents),
				sizeof(stat_row_ents[0]), stat_row_cmp);
		if (!ent) {
			ovis_log(mi->log, OVIS_LDEBUG, "unknown key: %s\n", tok);
			continue;
		}
		switch(ent->type) {
		case STAT_CPU:
			if (cpu_total < 1) {
				if (!prev_cpu_sum) {
					n = fscanf(mi->sf,	" %"PRIu64 " %"PRIu64 " %"PRIu64
								" %"PRIu64 " %"PRIu64 " %"PRIu64
								" %"PRIu64 " %"PRIu64 " %"PRIu64
								" %"PRIu64,
						&prev_cpu.user, &prev_cpu.nice, &prev_cpu.sys, &prev_cpu.idle,
						&prev_cpu.iowait, &prev_cpu.irq, &prev_cpu.softirq, &prev_cpu.steal,
						&prev_cpu.guest, &prev_cpu.guest_nice);
					if (n != 10) {
						rc = EINVAL;
						goto out;
					}
					prev_cpu_sum = prev_cpu.user + prev_cpu.nice + prev_cpu.sys + prev_cpu.idle +
						prev_cpu.iowait + prev_cpu.irq + prev_cpu.softirq + prev_cpu.steal;
					break;
				}
				if (prev_cpu_sum) {
					n = fscanf(mi->sf,	" %"PRIu64 " %"PRIu64 " %"PRIu64
								" %"PRIu64 " %"PRIu64 " %"PRIu64
								" %"PRIu64 " %"PRIu64 " %"PRIu64
								" %"PRIu64,
						&curr_cpu.user, &curr_cpu.nice, &curr_cpu.sys, &curr_cpu.idle,
						&curr_cpu.iowait, &curr_cpu.irq, &curr_cpu.softirq, &curr_cpu.steal,
						&curr_cpu.guest, &curr_cpu.guest_nice);
					if (n != 10) {
						rc = EINVAL;
						goto out;
					}
					curr_cpu_sum = curr_cpu.user + curr_cpu.nice + curr_cpu.sys + curr_cpu.idle +
						curr_cpu.iowait + curr_cpu.irq + curr_cpu.softirq + curr_cpu.steal;
					cpu_user = curr_cpu.user - prev_cpu.user;
					cpu_sys = curr_cpu.sys - prev_cpu.sys;
					cpu_iowait = curr_cpu.iowait - prev_cpu.iowait;
					cpu_idle = curr_cpu.idle - prev_cpu.idle;
					cpu_steal = curr_cpu.steal - prev_cpu.steal;
					cpu_sum = curr_cpu_sum - prev_cpu_sum;
				}
			}
			cpu_total += 1;
			break;
		case STAT_INTR:
			if (prev_intr) {
				n = fscanf(mi->sf, " %"PRIu64, &curr_intr);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				cpu_intr = curr_intr - prev_intr;
			}
			if (!prev_intr) {
				n = fscanf(mi->sf, " %"PRIu64, &prev_intr);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
			}
			break;
		/*
		case STAT_SOFTIRQ:
			n = fscanf(mi->sf,  " %"PRIu64 " %"PRIu64 " %"PRIu64
					" %"PRIu64 " %"PRIu64 " %"PRIu64
					" %"PRIu64 " %"PRIu64 " %"PRIu64
					" %"PRIu64 " %"PRIu64,
				&data[0], &data[1], &data[2], &data[3],
				&data[4], &data[5], &data[6], &data[7],
				&data[8], &data[9], &data[10]);
			if (n != 11) {
				rc = EINVAL;
				goto out;
			}
			break;
		*/
		case STAT_CTXT:
			if (!prev_ctxt) {
				n = fscanf(mi->sf, " %"PRIu64, &prev_ctxt);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				break;
			}
			if (prev_ctxt) {
				n = fscanf(mi->sf, " %"PRIu64, &curr_ctxt);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				cpu_ctxt = curr_ctxt - prev_ctxt;
			}
			break;
		case STAT_BTIME:
		case STAT_PROCESSES:
			if (!prev_fork) {
				n = fscanf(mi->sf, " %"PRIu64, &prev_fork);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				break;
			}
			if (prev_fork) {
				n = fscanf(mi->sf, " %"PRIu64, &curr_fork);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				cpu_fork = curr_fork - prev_fork;
			}
		case STAT_PROCS_RUNNING:
		case STAT_PROCS_BLOCKED:
			/*
			n = fscanf(mi->sf, "%"PRIu64 , &u64);
			if (n != 1) {
				rc = ENODATA;
				goto out;
			}
			ldms_metric_set_u64(mi->set, sch_metric_ids[ent->type], u64);
			*/
		default:
			if (curr_cpu_sum && curr_ctxt) {
				curr_time = time(NULL);
				double dval;
				for (i = 0; i < ARRAY_LEN(metric_list); i++) {
					if (metric_list[i].collect && strcmp(metric_list[i].file_path, PROC_STAT) == 0) {
						dval = metric_list[i].cb();
						__try_set_dbl(mi->set, metric_no, dval, metric_list[i].vtype);
						(*metric_no)++;
					}
				}
				prev_cpu_sum = 0;
				curr_cpu_sum = 0;
				prev_intr = 0;
				curr_intr = 0;
				prev_fork = 0;
				curr_fork = 0;
				prev_ctxt = 0;
				curr_ctxt = 0;
			}
			break;
		}
	}
	fclose(mi->sf);
	rc = 0;
out:
	return rc;

}

static int sample(ldmsd_plug_handle_t handle)
{
	int rc;
	int metric_cnt;
	int *metric_no = &metric_cnt;

	curated_metrics_t mi = ldmsd_plug_ctxt_get(handle);
	if (!mi->base->set) {
		ovis_log(mi->log, OVIS_LDEBUG, SAMP "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(mi->base);
	*metric_no = metric_offset;
	rc = minfo_handler(mi, metric_no);
	if (rc) {
		ovis_log(mi->log, OVIS_LDEBUG, "meminfo metrics not initialized\n");
	}
	rc = loadavg_handler(mi, metric_no);
	if (rc) {
		ovis_log(mi->log, OVIS_LDEBUG, "loadavg metrics not initialized\n");
	}
	rc = cpu_handler(mi, metric_no);
	if (rc) {
		ovis_log(mi->log, OVIS_LDEBUG, "cpu metrics not initialized\n");
	}

	base_sample_end(mi->base);
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	curated_metrics_t mi = calloc(1, sizeof(*mi));
	if (mi) {
		ldmsd_plug_ctxt_set(handle, mi);
		return 0;
	}
	return ENOMEM;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	curated_metrics_t mi = ldmsd_plug_ctxt_get(handle);
	if (mi->mf)
		fclose(mi->mf);
	if (mi->lf)
		fclose(mi->lf);
	if (mi->sf)
		fclose(mi->sf);
	if (mi->set)
		base_set_delete(mi->base);
	if (mi->base)
		base_del(mi->base);
	free(mi);
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
