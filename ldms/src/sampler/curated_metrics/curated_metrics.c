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
#define NET_DEV "/proc/net/dev"
#define SAMP "curated_metrics"
static char *procmeminfo = PROC_MEMINFO;
int smpl_minfo = 0;
static char *procloadavg = PROC_LOADAVG;
int smpl_loadavg = 0;
static char *procstat = PROC_STAT;
int smpl_procstat = 0;
static char *netdev = NET_DEV;
int smpl_netdev = 0;

struct rec_metric_info {
        int mid;
        const char *name;
        const char *unit;
        enum ldms_value_type type;
        int array_len;
};

#define MAXIFACE 256
#ifndef IFNAMSIZ
/* from "linux/if.h" */
#define IFNAMSIZ 16
#endif

// meminfo vars
typedef struct meminfo_data {
	uint64_t memfree;
	uint64_t memtotal;
	uint64_t dirty;
	uint64_t active;
	uint64_t inactive;
	uint64_t cached;
	uint64_t buffers;
	uint64_t writeback;
} mem_t;

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
	uint64_t ctxt;
	uint64_t intr;
	uint64_t fork;
	uint64_t cpu_sum;
	int sbool;
	time_t dtime;
} cpu_t;

// netdev vars

typedef struct netdev_data {
	uint64_t rx_bytes;
	uint64_t rx_packets;
	uint64_t rx_errs;
	uint64_t rx_drop;
	uint64_t rx_fifo;
	uint64_t rx_frame;
	uint64_t rx_cmprsd;
	uint64_t rx_multicast;
	uint64_t tx_bytes;
	uint64_t tx_packets;
	uint64_t tx_errs;
	uint64_t tx_drop;
	uint64_t tx_fifo;
	uint64_t tx_colls;
	uint64_t tx_carrier;
	uint64_t tx_cmprsd;
	time_t dtime;
} netdev_t;

typedef struct iface_ent {
	char name[IFNAMSIZ];
	netdev_t data;
	TAILQ_ENTRY(iface_ent) entry;
} iface_ent_t;

#define LBUFSZ 256
#define PROCBUFSZ 65536

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a))/sizeof((a)[0]))
#endif
#define CMLEN 8

struct ldms_metric_template_s rec_metrics[] = {
{ "name"          , 0,    LDMS_V_CHAR_ARRAY , ""        , IFNAMSIZ } ,
{ "rx_bytes"      , 0,    LDMS_V_D64        , "bytes per sample interval"   , 1  } ,
{ "rx_packets"    , 0,    LDMS_V_D64        , "packets per sample interval" , 1  } ,
{ "rx_errs"       , 0,    LDMS_V_D64        , "packets per sample interval" , 1  } ,
{ "rx_drop"       , 0,    LDMS_V_D64        , "packets per sample interval" , 1  } ,
{ "tx_bytes"      , 0,    LDMS_V_D64        , "bytes per sample interval"   , 1  } ,
{ "tx_packets"    , 0,    LDMS_V_D64        , "packets per sample interval" , 1  } ,
{ "tx_errs"       , 0,    LDMS_V_D64        , "packets per sample interval" , 1  } ,
{0},
};

#define REC_METRICS_LEN (ARRAY_LEN(rec_metrics) - 1)
typedef struct curated_metrics_s {
	/* meminfo */
	FILE *mf;
	/* load avg */
	FILE *lf;
	/* proc stat */
	FILE *sf;
	//netdev
	FILE *nf;
	int rec_def_idx;
	int rec_metric_ids[REC_METRICS_LEN];
	size_t rec_heap_sz;
	size_t heap_sz;

	int netdev_list_mid;
	int ifcount;
	char iface[MAXIFACE][20];
	int excount;
	char exclude[MAXIFACE][20];
	/* end netdev*/

	ldms_set_t set;
	ovis_log_t log;
	int metric_offset;
	base_data_t base;
	int skip_first;

	cpu_t cpu;
	mem_t minfo;
	TAILQ_HEAD(iface_list, iface_ent) netdevq;
	struct iface_ent *cur_iface;

	time_t prev_time;
	time_t curr_time;
} *curated_metrics_t;

typedef double (*metric_cb)(curated_metrics_t mi);

/* available metrics */
typedef struct cm_s {
	char *name;
	int collect;
	char *file_path;
	enum ldms_value_type vtype;
	metric_cb cb;
} *cm_t;

//call getpid() to get process id for ldmsd stats

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

double free_cb(curated_metrics_t mi) {
	return (double)mi->minfo.memfree / (double)mi->minfo.memtotal;
}

double buff_cb(curated_metrics_t mi) {
	return (double)mi->minfo.buffers / (double)mi->minfo.memtotal;
}

double cached_cb(curated_metrics_t mi) {
	return (double)mi->minfo.cached / (double)mi->minfo.memtotal;
}

double active_cb(curated_metrics_t mi) {
	return (double)mi->minfo.active / (double)mi->minfo.memtotal;
}

double inactive_cb(curated_metrics_t mi) {
	return (double)mi->minfo.inactive / (double)mi->minfo.memtotal;
}

double dirty_cb(curated_metrics_t mi) {
	return (double)mi->minfo.dirty / (double)mi->minfo.memtotal;
}

double writeback_cb(curated_metrics_t mi) {
	return (double)mi->minfo.writeback / (double)mi->minfo.memtotal;
}

double memtotal_cb(curated_metrics_t mi) {
	return (double)mi->minfo.memtotal;
}

double idle_cb(curated_metrics_t mi) {
	return (double)mi->cpu.idle / (double)mi->cpu.cpu_sum;
}
double sys_cb(curated_metrics_t mi) {
	return (double)mi->cpu.sys / (double)mi->cpu.cpu_sum;
}
double user_cb(curated_metrics_t mi) {
	return (double)mi->cpu.user / (double)mi->cpu.cpu_sum;
}
double iowait_cb(curated_metrics_t mi) {
	return (double)mi->cpu.iowait / (double)mi->cpu.cpu_sum;
}
/*
double stolen_cb(curated_metrics_t mi) {
	return (double)mi->cpu.stolen / (double)mi->cpu.cpu_sum;
}
*/
double intr_cb(curated_metrics_t mi) {
	return (double)mi->cpu.intr / (double)mi->cpu.dtime;
}
double ctxt_cb(curated_metrics_t mi) {
	return (double)mi->cpu.ctxt / ((double)mi->cpu.dtime);
}
double fork_cb(curated_metrics_t mi) {
	return (double)mi->cpu.fork / (double)mi->cpu.dtime;
}
double rxb_cb(curated_metrics_t mi) {
	return (double)mi->cur_iface->data.rx_bytes / ((double)mi->curr_time - (double)mi->prev_time);
}
/* netdev must be adjusted to use dtime */
double rx_packets_cb(curated_metrics_t mi) {
	return (double)mi->cur_iface->data.rx_packets / ((double)mi->curr_time - (double)mi->prev_time);
}
double rx_errs_cb(curated_metrics_t mi) {
	return (double)mi->cur_iface->data.rx_errs / ((double)mi->curr_time - (double)mi->prev_time);
}
double rx_drop_cb(curated_metrics_t mi) {
	return (double)mi->cur_iface->data.rx_drop / ((double)mi->curr_time - (double)mi->prev_time);
}
double txb_cb(curated_metrics_t mi) {
	return (double)mi->cur_iface->data.tx_bytes / ((double)mi->curr_time - (double)mi->prev_time);
}
double txp_cb(curated_metrics_t mi) {
	return (double)mi->cur_iface->data.tx_packets / ((double)mi->curr_time - (double)mi->prev_time);
}
double tx_errs_cb(curated_metrics_t mi) {
	return (double)mi->cur_iface->data.tx_errs / ((double)mi->curr_time - (double)mi->prev_time);
}

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
	//{ "stolen", 1, PROC_STAT, LDMS_V_D64, stolen_cb },
	{ "interrupts", 1, PROC_STAT, LDMS_V_D64, intr_cb },
	{ "context_sw", 1, PROC_STAT, LDMS_V_D64, ctxt_cb },
	{ "fork", 1, PROC_STAT, LDMS_V_D64, fork_cb }
};
struct cm_s netdev_metric_list[] = {
	{ "rx_bytes", 1, NET_DEV, LDMS_V_D64, rxb_cb },
	{ "rx_packets", 1, NET_DEV, LDMS_V_D64, rx_packets_cb },
	{ "rx_errs", 1, NET_DEV, LDMS_V_D64, rx_errs_cb },
	{ "rx_drop", 1, NET_DEV, LDMS_V_D64, rx_drop_cb },
	{ "tx_bytes", 1, NET_DEV, LDMS_V_D64, txb_cb },
	{ "tx_packets", 1, NET_DEV, LDMS_V_D64, txp_cb },
	{ "tx_errs", 1, NET_DEV, LDMS_V_D64, tx_errs_cb }
};

static int minfo_handler(curated_metrics_t mi, int *metric_no);

static int cpu_handler(curated_metrics_t mi, int *metric_no);

static int netdev_handler(curated_metrics_t mi, int *metric_no);

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

/* procnetdev
struct cm_s netdev_metrics[] = {
	{ "rx_bytes"      , 1, NET_DEV,    LDMS_V_D64, rxb_cb        } ,
	{ "rx_drop"       , 1, NET_DEV,    LDMS_V_D64, rx_drop_cb    } ,
	{ "rx_errs"       , 1, NET_DEV,    LDMS_V_D64, rx_errs_cb    } ,
	{ "rx_packets"    , 1, NET_DEV,    LDMS_V_D64, rx_packets_cb } ,
	{ "tx_bytes"      , 1, NET_DEV,    LDMS_V_D64, txb_cb        } ,
	{ "tx_errs"       , 1, NET_DEV,    LDMS_V_D64, tx_errs_cb    } ,
	{ "tx_packets"    , 1, NET_DEV,    LDMS_V_D64, tx_packets_cb } ,
{0},
};

typedef struct procnetdev2_s {
	ovis_log_t mylog;

	int rec_def_idx;
	int rec_metric_ids[REC_METRICS_LEN];
	size_t rec_heap_sz;
	size_t heap_sz;

	int netdev_list_mid;

	int ifcount;
	char iface[MAXIFACE][20];
	int excount;
	char exclude[MAXIFACE][20];

	FILE *mf;
	base_data_t base;
} *procnetdev2_t;

json_t *ifaces;
*/
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
	/*
	ifaces = json_object_get(cfp, "ifaces");
	if (ifaces && json_is_array(ifaces)) {
		mi->nf = fopen(
	}
	*/
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
	//int incr_heap_sz;
	//procstat
	//int n_cpu;
	//char *s;
	rc = 0;

	schema = base_schema_new(mi->base);
	if (!schema) {
		ovis_log(mi->log, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, mi->base->schema_name, errno);
		rc = errno;
		goto err;
	}

	mi->mf = fopen(procmeminfo, "r");
	if (!mi->mf) {
		ovis_log(mi->log, OVIS_LERROR, "Could not open the " SAMP " file "
				"'%s'...exiting sampler\n", procmeminfo);
		return ENOENT;
	}

	/* Location of first metric from proc/meminfo file */
	mi->metric_offset = ldms_schema_metric_count_get(schema);

	// Derive Free idle mem %
	//
	int metric_list_sz = sizeof(metric_list) / sizeof(metric_list[0]);
	for (i = 0; i < metric_list_sz; i++) {
		if (metric_list[i].collect) {
			if (strcmp(metric_list[i].file_path, NET_DEV) != 0) {
				rc = ldms_schema_metric_add(schema, metric_list[i].name, metric_list[i].vtype);
				if (rc < 0)
					goto err;
			/*} else {
				mi->nf = fopen(netdev, "r");
				int i = 0;
				char tok[30];
				char mname[128];
				while(1 == fscanf(mi->nf, "%s", tok)) {
					if (i < 2)
						continue;
					if (ifaces) {
						int j = 0;
						for (j=0; j < ARRAY_LEN(ifaces); j++) {
							if (strcmp(
						}
					sprintf(mname, "%s.%s", tok, metric_list[i].name);
					rc = ldms_schema_metric_add(schema, mname, metric_list[i].vtype);
					if (rc < 0)
						goto err;
				}
				fclose(mi->nf);
				mi->nf = NULL;
			*/
			}
		}
	}
	/*
	*/
	// End system_memory metrics
	// Begin loadavg metrics
	mi->lf = fopen(procloadavg, "r");
	if (!mi->lf) {
		ovis_log(mi->log, OVIS_LERROR, SAMP ": Could not open " PROC_LOADAVG
				" : continuing\n");
	}
	fclose(mi->lf);
	mi->lf = NULL;

	mi->sf = fopen(procstat, "r");
	if (!mi->sf) {
		ovis_log(mi->log, OVIS_LERROR, SAMP ": Could not open " PROC_STAT
				" : continuing\n");
	}
	fclose(mi->sf);
	mi->sf = NULL;
	/* procnetdev metrics */
	mi->nf = fopen(netdev, "r");
	if (!mi->nf) {
		ovis_log(mi->log, OVIS_LERROR, SAMP ": Could not open " NET_DEV
				" : continuing\n");
		rc = ENOENT;
		goto err;
	}
	ldms_record_t rec_def;
	rec_def = ldms_record_from_template("netdev", rec_metrics, mi->rec_metric_ids);
	if (!rec_def) {
		rc = errno;
		goto err;
	}
	mi->rec_heap_sz = ldms_record_heap_size_get(rec_def);
	mi->heap_sz = MAXIFACE * ldms_record_heap_size_get(rec_def);

	mi->rec_def_idx = ldms_schema_record_add(schema, rec_def);
	if (mi->rec_def_idx < 0) {
		rc = -mi->rec_def_idx;
		goto err3;
	}

	mi->netdev_list_mid = ldms_schema_metric_list_add(schema, "netdev_list", NULL, mi->heap_sz);
	if (mi->netdev_list_mid < 0) {
		rc = -mi->netdev_list_mid;
		goto err2;
	}
	fclose(mi->nf);
	mi->set = base_set_new_heap(mi->base, mi->heap_sz);
	if (!mi->set) {
		rc = errno;
		goto err2;
	}

	return 0;

err3:
	ldms_record_delete(rec_def);
err2:
	base_schema_delete(mi->base);
	mi->base = NULL;
err:
	if (mi->mf) {
		fclose(mi->mf);
		mi->mf = NULL;
	}
	if (mi->lf) {
		fclose(mi->lf);
		mi->lf = NULL;
	}
	if (mi->sf) {
		fclose(mi->sf);
		mi->sf = NULL;
	}
	if (mi->nf) {
		fclose(mi->nf);
		mi->nf = NULL;
	}
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " path=<config_filepath>\n" BASE_CONFIG_USAGE \
		"	<config_filepath>	A path to a json configuration file containing a dictionary with two available keywords.\n"
		"				The 'metrics' keyword value is a list of metrics in the curated_metrics sampler to include.\n"
		"				The 'ifaces' keyword value is a list of interfaces to include.\n";
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
	mi->cpu.sbool = 0;
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
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.active));
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "MemFree") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.memfree));
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "MemTotal") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.memtotal));
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Dirty") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.dirty));
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "InActive") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.inactive));
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Cached") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.cached));
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Buffers") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.buffers));
			if (rc != 2 && rc != 3) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Writeback") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.writeback));
		}
	} while (s);
	double dval;
	uint64_t val;
	for (i = 0; i < ARRAY_LEN(metric_list); i++) {
		if (metric_list[i].collect && strcmp(metric_list[i].file_path, PROC_MEMINFO) == 0) {
			if (metric_list[i].vtype == LDMS_V_D64) {
				dval = metric_list[i].cb(mi);
				__try_set_dbl(mi->set, metric_no, dval, metric_list[i].vtype);
			} else {
				val = metric_list[i].cb(mi);
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
	(*metric_no)--;
	for (i = 0; i < METLEN; i++) {
		if (metname[i].collect) {
			switch (metname[i].vtype) {
			case LDMS_V_D64:
				if (force_integer)
					ldms_metric_set_u64(mi->set, (*metric_no)++,
						(uint64_t)(100*metname[i].v.d));
				else
					ldms_metric_set_double(mi->set, (*metric_no)++,
						metname[i].v.d);
				break;
			case LDMS_V_U64:
				ldms_metric_set_u64(mi->set, (*metric_no)++,
					metname[i].v.u);
				break;
			default:
				ovis_log(mi->log, OVIS_LCRITICAL, SAMP ": sample() memory corruption detected.\n");
				rc = EINVAL;
				return rc;
			}
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
	cpu_t curr_cpu;

	if (!mi->set) {
		ovis_log(mi->log, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}
	mi->sf = fopen(procstat, "r");
        if (!mi->sf) {
                ovis_log(mi->log, OVIS_LERROR, "Could not open the " SAMP " file "
                                "'%s'...exiting sampler\n", procstat);
                rc = ENOENT;
		goto out;
        }

	fseek(mi->sf, 0, SEEK_SET);
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
				if (!mi->cpu.sbool) {
					n = fscanf(mi->sf,	" %"PRIu64 " %"PRIu64 " %"PRIu64
								" %"PRIu64 " %"PRIu64 " %"PRIu64
								" %"PRIu64 " %"PRIu64 " %"PRIu64
								" %"PRIu64,
						&(mi->cpu.user), &(mi->cpu.nice), &(mi->cpu.sys), &(mi->cpu.idle),
						&(mi->cpu.iowait), &(mi->cpu.irq), &(mi->cpu.softirq), &(mi->cpu.steal),
						&(mi->cpu.guest), &(mi->cpu.guest_nice));
					if (n != 10) {
						rc = EINVAL;
						goto out;
					}
					mi->cpu.cpu_sum = mi->cpu.user + mi->cpu.nice + mi->cpu.sys + mi->cpu.idle +
						mi->cpu.iowait + mi->cpu.irq + mi->cpu.softirq + mi->cpu.steal;
					break;
				} else {
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
					curr_cpu.cpu_sum = curr_cpu.user + curr_cpu.nice + curr_cpu.sys + curr_cpu.idle +
						curr_cpu.iowait + curr_cpu.irq + curr_cpu.softirq + curr_cpu.steal;
					mi->cpu.user = curr_cpu.user - mi->cpu.user;
					mi->cpu.sys = curr_cpu.sys - mi->cpu.sys;
					mi->cpu.iowait = curr_cpu.iowait - mi->cpu.iowait;
					mi->cpu.idle = curr_cpu.idle - mi->cpu.idle;
					mi->cpu.steal = curr_cpu.steal - mi->cpu.steal;
					mi->cpu.cpu_sum = curr_cpu.cpu_sum - mi->cpu.cpu_sum;
				}
			}
			cpu_total += 1;
			break;
		case STAT_INTR:
			if (mi->cpu.sbool) {
				n = fscanf(mi->sf, " %"PRIu64, &curr_cpu.intr);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				mi->cpu.intr = curr_cpu.intr - mi->cpu.intr;
			} else {
				n = fscanf(mi->sf, " %"PRIu64, &(mi->cpu.intr));
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
			}
			break;
		case STAT_CTXT:
			if (!mi->cpu.sbool) {
				n = fscanf(mi->sf, " %"PRIu64, &(mi->cpu.ctxt));
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				break;
			} else {
				n = fscanf(mi->sf, " %"PRIu64, &curr_cpu.ctxt);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				mi->cpu.ctxt = curr_cpu.ctxt - mi->cpu.ctxt;
			}
			break;
		case STAT_BTIME:
		case STAT_PROCESSES:
			if (!mi->cpu.sbool) {
				n = fscanf(mi->sf, " %"PRIu64, &(mi->cpu.fork));
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				break;
			} else {
				n = fscanf(mi->sf, " %"PRIu64, &curr_cpu.fork);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				mi->cpu.fork = curr_cpu.fork - mi->cpu.fork;
			}
			break;
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
			break;
		default:
			rc = EINVAL;
			goto out;
		}
	}
	for (i = 0; i < ARRAY_LEN(metric_list); i++) {
		if (metric_list[i].collect && strcmp(metric_list[i].file_path, PROC_STAT) == 0) {
			if (mi->cpu.sbool) {
				double dval;
				mi->cpu.dtime = curr_cpu.dtime - mi->cpu.dtime;
				dval = metric_list[i].cb(mi);
				__try_set_dbl(mi->set, metric_no, dval, metric_list[i].vtype);
			}
			(*metric_no)++;
		}
	}
	if (mi->cpu.sbool) {
		mi->cpu = curr_cpu;
	} else {
		mi->cpu.sbool = 1;
		mi->cpu.dtime = curr_cpu.dtime;
	}
	fclose(mi->sf);
	mi->sf = NULL;
	rc = 0;
out:
	return rc;

}

static int netdev_handler(curated_metrics_t mi, int *metric_no)
{
	int rc = 0;
	char lbuf[256];
	char *s;
	char curriface[IFNAMSIZ];
	netdev_t curr_netdev;
	ldms_mval_t lh, rec_inst, name_mval;

	if (!mi->base->set) {
		ovis_log(mi->log, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}
	mi->nf = fopen(netdev, "r");
	if (!mi->nf) {
		ovis_log(mi->log, OVIS_LDEBUG, "Could not open /proc/net/dev file ");
		return ENOENT;
	}
	lh = ldms_metric_get(mi->base->set, mi->netdev_list_mid);

	fseek(mi->nf, 0, SEEK_SET);
	s = fgets(lbuf, sizeof(lbuf), mi->nf);
	s = fgets(lbuf, sizeof(lbuf), mi->nf);

	do {
		s = fgets(lbuf, sizeof(lbuf), mi->nf);
		if (!s)
			break;

		char *pch = strchr(lbuf, ':');
		if (pch != NULL) {
			*pch = ' ';
		}

		curr_netdev.dtime = time(NULL);
		int rc = sscanf(lbuf, "%s %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 "\n", curriface, &curr_netdev.rx_bytes,
				&curr_netdev.rx_packets, &curr_netdev.rx_errs, &curr_netdev.rx_drop, &curr_netdev.rx_fifo,
				&curr_netdev.rx_frame, &curr_netdev.rx_cmprsd, &curr_netdev.rx_multicast,
				&curr_netdev.tx_bytes, &curr_netdev.tx_packets, &curr_netdev.tx_errs,
				&curr_netdev.tx_drop, &curr_netdev.tx_fifo, &curr_netdev.tx_colls,
				&curr_netdev.tx_carrier, &curr_netdev.tx_cmprsd);
		if (rc != 17) {
			ovis_log(mi->log, OVIS_LINFO,
				"wrong number of fields in procnetdev sscanf\n");
			continue;
		}

		/* Find the mi->cur_iface in the mi->netdevq that matches the curiface name */
		struct iface_ent *ci = NULL;
		TAILQ_FOREACH(ci, &mi->netdevq, entry) {
			if (0 == strcmp(ci->name, curriface))
				break;
		}
		if (ci == NULL) {
			struct iface_ent *cur_iface = malloc(sizeof(struct iface_ent));
			if (cur_iface == NULL) {
				rc = ENOMEM;
				goto out;
			}
			strcpy(cur_iface->name, curriface);
			cur_iface->data = curr_netdev;
			TAILQ_INSERT_TAIL(&mi->netdevq, cur_iface, entry);
		} else {
			if (ci == NULL)
				break;
			mi->cur_iface = ci;
			mi->cur_iface->data.rx_bytes = curr_netdev.rx_bytes - mi->cur_iface->data.rx_bytes;
			mi->cur_iface->data.rx_packets = curr_netdev.rx_packets - mi->cur_iface->data.rx_packets;
			mi->cur_iface->data.rx_errs = curr_netdev.rx_errs - mi->cur_iface->data.rx_errs;
			mi->cur_iface->data.rx_drop = curr_netdev.rx_drop - mi->cur_iface->data.rx_drop;
			mi->cur_iface->data.tx_bytes = curr_netdev.tx_bytes - mi->cur_iface->data.tx_bytes;
			mi->cur_iface->data.tx_packets = curr_netdev.tx_packets - mi->cur_iface->data.tx_packets;
			mi->cur_iface->data.tx_errs = curr_netdev.tx_errs - mi->cur_iface->data.tx_errs;
			mi->cur_iface->data.dtime = curr_netdev.dtime - mi->cur_iface->data.dtime;

			enum ldms_value_type typ;
			size_t cnt;
			for (rec_inst = ldms_list_first(mi->base->set, lh, &typ, &cnt);
					rec_inst; rec_inst = ldms_list_next(mi->base->set, rec_inst, &typ, &cnt)) {
				name_mval = ldms_record_metric_get(rec_inst, mi->rec_metric_ids[0]);
				if (0 == strcmp(name_mval->a_char, curriface))
					break;
			}
			/* If we didn't find an instance for this interface, add one */
			if (rec_inst == NULL) {
				rec_inst = ldms_record_alloc(mi->base->set, mi->rec_def_idx);
				if (rec_inst == NULL)
					continue;
				name_mval = ldms_record_metric_get(rec_inst, mi->rec_metric_ids[0]);
				snprintf(name_mval->a_char, IFNAMSIZ, "%s", curriface);
				/* lh is the mval that refers to the netdev list in the set */
				ldms_list_append_record(mi->base->set, lh, rec_inst);
			}

			int rec_metric_id, nd_metric_id;
			for (rec_metric_id = 0; rec_metric_id < ldms_record_card(rec_inst); rec_metric_id++) {
				for (nd_metric_id = 0; nd_metric_id < ARRAY_LEN(netdev_metric_list); nd_metric_id++) {
					if (strcmp(mi->cur_iface->name, ldms_record_metric_name_get(rec_inst, rec_metric_id)))
						continue;
					if (netdev_metric_list[nd_metric_id].collect) {
						double dval = netdev_metric_list[nd_metric_id].cb(mi);
						ldms_record_set_double(rec_inst, nd_metric_id, dval);
					}
					break;
				}
			}
			strcpy(mi->cur_iface->name, curriface);
			mi->cur_iface->data = curr_netdev;
		}
	} while(s);
	(*metric_no)++;
	return rc;
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
	if (!mi->prev_time) {
		mi->prev_time = mi->curr_time = time(NULL);
	} else {
		mi->prev_time = mi->curr_time;
		mi->curr_time = time(NULL);
	}
	metric_cnt = mi->metric_offset;
	if (!mi->skip_first) {
		rc = minfo_handler(mi, metric_no);
		if (rc) {
			ovis_log(mi->log, OVIS_LDEBUG, "meminfo metrics not initialized\n");
		}
		rc = loadavg_handler(mi, metric_no);
		if (rc) {
			ovis_log(mi->log, OVIS_LDEBUG, "loadavg metrics not initialized\n");
		}
	}
	rc = cpu_handler(mi, metric_no);
	if (rc) {
		ovis_log(mi->log, OVIS_LDEBUG, "cpu metrics not initialized\n");
	}
	rc = netdev_handler(mi, metric_no);
	if (rc)
		ovis_log(mi->log, OVIS_LDEBUG, "net dev metrics not initialized\n");
	mi->skip_first = 0;
	base_sample_end(mi->base);
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	curated_metrics_t mi = calloc(1, sizeof(*mi));
	if (mi) {
		ldmsd_plug_ctxt_set(handle, mi);
		TAILQ_INIT(&mi->netdevq);
		mi->skip_first = 1;
		return 0;
	}
	return ENOMEM;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	curated_metrics_t mi = ldmsd_plug_ctxt_get(handle);
	struct iface_ent *e1, *e2;
	TAILQ_INIT(&mi->netdevq);
	e1 = TAILQ_FIRST(&mi->netdevq);
	while (e1 != NULL) {
		e2 = TAILQ_NEXT(e1, entry);
		free(e1);
		e1 = e2;
	}
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
