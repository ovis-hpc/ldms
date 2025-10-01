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
 * \brief /proc/net/dev data provider
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
#include <net/if.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

#include <jansson.h>
#include <assert.h>

#define PROC_MEMINFO "/proc/meminfo"
#define PROC_LOADAVG "/proc/loadavg"
#define PROC_STAT "/proc/stat"
#define NET_DEV "/proc/net/dev"
static char *procmeminfo = PROC_MEMINFO;
static char *procloadavg = PROC_LOADAVG;
static char *procstat = PROC_STAT;
static char *netdev = NET_DEV;

struct rec_metric_info {
	int mid;
	const char *name;
	const char *unit;
	enum ldms_value_type type;
	int array_len;
};

#define MAXIFACE 256

/* meminfo vars */
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

typedef struct loadavg_data {
	double load1;
	double load5;
	double load15;
	uint64_t runnable;
	uint64_t scheduling_entities;
	uint64_t new_pid;
} load_t;

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
	uint64_t cpu_sum;
} cpu_t;

/* procstat vars */
typedef struct stat_data {
	cpu_t cpu_data;
	uint64_t ctxt_sw;
	uint64_t intr;
	uint64_t fork;
	uint64_t procs_blocked;
	int sbool;
	time_t dtime;
} stat_t;

/* netdev vars */
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

typedef struct collect_metrics_s  {
	int collect;
} *col_m_t;

typedef struct curated_metrics_s {
	/* meminfo */
	FILE *mf;
	/* load avg */
	FILE *lf;
	/* proc stat */
	FILE *sf;
	/* netdev */
	FILE *nf;

	int sample_meminfo;
	int sample_stat;
	int sample_loadavg;
	int sample_netdev;

	struct collect_metrics_s collect_list[31];

	/* proc stat */
	int stat_rec_idx;
	int stat_metric_len;
	size_t stat_rec_heap_sz;
	size_t stat_heap_sz;
	int stat_list_mid;

	/* netdev */
	int rec_def_idx;
	int rec_metric_len;
	size_t rec_heap_sz;
	size_t heap_sz;
	int netdev_list_mid;

	ldms_set_t set;
	ovis_log_t log;
	int metric_offset;
	base_data_t base;
	int skip_first;

	stat_t stat;
	mem_t minfo;
	load_t loadavg;
	TAILQ_HEAD(iface_list, iface_ent) netdevq;
	struct iface_ent *cur_iface;
	/* struct cpu_ent *curr_cpu; */

	time_t prev_time;
	time_t curr_time;
} *curated_metrics_t;

typedef double (*metric_cb)(curated_metrics_t mi);

/* available metrics */
typedef struct cm_s {
	char *name;
	int not_rec;
	char *file_path;
	enum ldms_value_type vtype;
	metric_cb cb;
	const char unit[16];
} *cm_t;

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
	return ((double)mi->minfo.memfree / (double)mi->minfo.memtotal) * 100.0;
}
double buff_cb(curated_metrics_t mi) {
	return ((double)mi->minfo.buffers / (double)mi->minfo.memtotal) * 100.0;
}
double cached_cb(curated_metrics_t mi) {
	return ((double)mi->minfo.cached / (double)mi->minfo.memtotal) * 100.0;
}
double active_cb(curated_metrics_t mi) {
	return ((double)mi->minfo.active / (double)mi->minfo.memtotal) * 100.0;
}
double inactive_cb(curated_metrics_t mi) {
	return ((double)mi->minfo.inactive / (double)mi->minfo.memtotal) * 100.0;
}
double dirty_cb(curated_metrics_t mi) {
	return ((double)mi->minfo.dirty / (double)mi->minfo.memtotal) * 100.0;
}
double writeback_cb(curated_metrics_t mi) {
	return ((double)mi->minfo.writeback / (double)mi->minfo.memtotal) * 100.0;
}
double memtotal_cb(curated_metrics_t mi) {
	return (double)mi->minfo.memtotal;
}
double load1_cb(curated_metrics_t mi) {
	return mi->loadavg.load1;
}
double load5_cb(curated_metrics_t mi) {
	return mi->loadavg.load5;
}
double load15_cb(curated_metrics_t mi) {
	return mi->loadavg.load15;
}
double run_cb(curated_metrics_t mi) {
	return (double)mi->loadavg.runnable;
}
double sched_ent_cb(curated_metrics_t mi) {
	return (double)mi->loadavg.scheduling_entities;
}
double new_pid_cb(curated_metrics_t mi) {
	return (double)mi->loadavg.new_pid;
}
double idle_cb(curated_metrics_t mi) {
	return ((double)mi->stat.cpu_data.idle / (double)mi->stat.cpu_data.cpu_sum) * 100.0;
}
double sys_cb(curated_metrics_t mi) {
	return ((double)mi->stat.cpu_data.sys / (double)mi->stat.cpu_data.cpu_sum) * 100.0;
}
double user_cb(curated_metrics_t mi) {
	return ((double)mi->stat.cpu_data.user / (double)mi->stat.cpu_data.cpu_sum) * 100.0;
}
double iowait_cb(curated_metrics_t mi) {
	return ((double)mi->stat.cpu_data.iowait / (double)mi->stat.cpu_data.cpu_sum) * 100.0;
}
double intr_cb(curated_metrics_t mi) {
	return (double)mi->stat.intr / (double)mi->stat.dtime;
}
double ctxt_cb(curated_metrics_t mi) {
	return (double)mi->stat.ctxt_sw / ((double)mi->stat.dtime);
}
double fork_cb(curated_metrics_t mi) {
	return (double)mi->stat.fork / (double)mi->stat.dtime;
}
double procs_block_cb(curated_metrics_t mi) {
	return (double)mi->stat.procs_blocked;
}
double rxb_cb(curated_metrics_t mi) {
	return (double)mi->cur_iface->data.rx_bytes / ((double)mi->curr_time - (double)mi->prev_time);
}
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
	{ "free",     1, PROC_MEMINFO, LDMS_V_D64, free_cb, "%" },
	{ "buff",     1, PROC_MEMINFO, LDMS_V_D64, buff_cb, "%" },
	{ "cached",   1, PROC_MEMINFO, LDMS_V_D64, cached_cb, "%" },
	{ "inactive", 1, PROC_MEMINFO, LDMS_V_D64, inactive_cb, "%" },
	{ "active",   1, PROC_MEMINFO, LDMS_V_D64, active_cb, "%" },
	{ "dirty",    1, PROC_MEMINFO, LDMS_V_D64, dirty_cb, "%" },
	{ "writeback", 1, PROC_MEMINFO, LDMS_V_D64, writeback_cb, "%" },
	{ "phys_mem", 1, PROC_MEMINFO, LDMS_V_D64, memtotal_cb, "kB" },
	{ "load1min", 1, PROC_LOADAVG, LDMS_V_D64, load1_cb, "avg/1min" },
	{ "load5min", 1, PROC_LOADAVG, LDMS_V_D64, load5_cb, "avg/5min" },
	{ "load15min", 1, PROC_LOADAVG, LDMS_V_D64, load15_cb, "avg/15min" },
	{ "runnable", 1, PROC_LOADAVG, LDMS_V_D64, run_cb, "count" },
	{ "scheduling_entities", 1, PROC_LOADAVG, LDMS_V_D64, sched_ent_cb, "count" },
	{ "newest_pid", 1, PROC_LOADAVG, LDMS_V_D64, new_pid_cb, "pid" },
	{ "cpu_name", 0, PROC_STAT, LDMS_V_CHAR_ARRAY, NULL, "" },
	{ "idle", 0, PROC_STAT, LDMS_V_D64, idle_cb, "%" },
	{ "sys", 0, PROC_STAT, LDMS_V_D64, sys_cb, "%" },
	{ "user", 0, PROC_STAT, LDMS_V_D64, user_cb, "%" },
	{ "iowait", 0, PROC_STAT, LDMS_V_D64, iowait_cb, "%" },
	{ "interrupts", 1, PROC_STAT, LDMS_V_D64, intr_cb, "intr/sec" },
	{ "context_sw", 1, PROC_STAT, LDMS_V_D64, ctxt_cb, "ctxt_sw/sec" },
	{ "fork", 1, PROC_STAT, LDMS_V_D64, fork_cb, "forks/sec" },
	{ "procs_blocked", 1, PROC_STAT, LDMS_V_D64, procs_block_cb, "count" },
	{ "iface_name", 0, NET_DEV, LDMS_V_CHAR_ARRAY, NULL, "" },
	{ "rx_bytes", 0, NET_DEV, LDMS_V_D64, rxb_cb, "rx_bytes/sec" },
	{ "rx_packets", 0, NET_DEV, LDMS_V_D64, rx_packets_cb, "rx_packets/sec" },
	{ "rx_errs", 0, NET_DEV, LDMS_V_D64, rx_errs_cb, "rx_errs/sec" },
	{ "rx_drop", 0, NET_DEV, LDMS_V_D64, rx_drop_cb, "rx_drop/sec" },
	{ "tx_bytes", 0, NET_DEV, LDMS_V_D64, txb_cb, "tx_bytes/sec" },
	{ "tx_packets", 0, NET_DEV, LDMS_V_D64, txp_cb, "tx_packets/sec" },
	{ "tx_errs", 0, NET_DEV, LDMS_V_D64, tx_errs_cb, "tx_errs/sec" }
};

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
	return strncmp(key, ent->prefix, strlen(ent->prefix));
}

static int parse_cmetrics(curated_metrics_t mi, char *path) {
	int i;
	int rc = 0;
	json_t *cfp, *cfg_metrics;
	json_error_t jerr;

	cfp = json_load_file(path, 0, &jerr);
	if (!cfp) {
		ovis_log(mi->log, OVIS_LERROR,
			 "Error parsing '%s' configuration file.\n",
			 path);
		ovis_log(mi->log, OVIS_LERROR,
			 "%s: Line: %d, Column: %d, %s\n",
			 path, jerr.line, jerr.column, jerr.text);
		errno = EINVAL;
		goto err_1;
	}
	cfg_metrics = json_object_get(cfp, "metrics");
	if (!cfg_metrics) {
		ovis_log(mi->log, OVIS_LERROR,
			 "Key 'metrics' is not present in json file %s\n",
			 path);
		errno = ENOENT;
		goto err_1;
	}
	mi->sample_meminfo = 0;
	mi->sample_stat = 0;
	mi->sample_loadavg = 0;
	mi->sample_netdev = 0;
	for (i = 0; i < ARRAY_LEN(metric_list); i++) {
		mi->collect_list[i].collect = 0;
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
					mi->collect_list[i].collect = 1;
					if (!strcmp(metric_list[i].file_path, PROC_STAT)) {
						mi->sample_stat = 1;
						continue;
					}
					if (!strcmp(metric_list[i].file_path, PROC_MEMINFO)) {
						mi->sample_meminfo = 1;
						continue;
					}
					if (!strcmp(metric_list[i].file_path, PROC_LOADAVG)) {
						mi->sample_loadavg = 1;
						continue;
					}
					if (!strcmp(metric_list[i].file_path, NET_DEV)) {
						mi->sample_netdev = 1;
						continue;
					}
				}
			}
		}
	}
	return 0;
err_1:
	json_decref(cfp);
	return rc;
}

static int create_metric_set(curated_metrics_t mi)
{
	ldms_schema_t schema;
	int rc, i, n_cpu;
	rc = 0;
	n_cpu = 0;

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
		ovis_log(mi->log, OVIS_LERROR,
			 "Could not open the file '%s'...exiting sampler: %s\n",
			 procmeminfo, strerror(errno));
		return ENOENT;
	}

	/* Location of first metric from the schema */
	mi->metric_offset = ldms_schema_metric_count_get(schema);

	/*  Derive Free idle mem % */
	int metric_list_sz = sizeof(metric_list) / sizeof(metric_list[0]);
	ldms_record_t rec_def;
	ldms_record_t stat_rec;
	mi->rec_metric_len = 0;
	for (i = 0; i < metric_list_sz; i++) {
		if (mi->collect_list[i].collect) {
			if (metric_list[i].not_rec) {
				rc = ldms_schema_metric_add_with_unit(schema,
								      metric_list[i].name,
								      metric_list[i].unit,
								      metric_list[i].vtype);
				if (rc < 0)
					goto err2;
			} else {
				if (!strcmp(metric_list[i].file_path, PROC_STAT)) {
					if (!mi->stat_metric_len) {
						stat_rec = ldms_record_create("cpu");
						if (!stat_rec) {
							rc = errno;
							goto err2;
						}
					}
					if (metric_list[i].vtype == LDMS_V_CHAR_ARRAY) {
						rc = ldms_record_metric_add(stat_rec, metric_list[i].name,
							"cpu_name", metric_list[i].vtype, IFNAMSIZ);
					} else {
						rc = ldms_record_metric_add(stat_rec, metric_list[i].name,
							metric_list[i].unit, metric_list[i].vtype, 0);
					}
					mi->stat_metric_len++;
				} else {
					if (!mi->rec_metric_len) {
						rec_def = ldms_record_create("netdev");
						if (!rec_def) {
							rc = errno;
							goto err2;
						}
					}
					if (metric_list[i].vtype == LDMS_V_CHAR_ARRAY) {
						rc = ldms_record_metric_add(rec_def, metric_list[i].name,
							"iface_name", metric_list[i].vtype, IFNAMSIZ);
					} else {
						rc = ldms_record_metric_add(rec_def, metric_list[i].name,
							metric_list[i].unit, metric_list[i].vtype, 0);
					}
					mi->rec_metric_len++;
				}
			}
		}
	}
	/* Begin loadavg metrics */
	if (mi->sample_loadavg) {
		mi->lf = fopen(procloadavg, "r");
		if (!mi->lf) {
			ovis_log(mi->log, OVIS_LERROR, "Could not open " PROC_LOADAVG
					" : continuing\n");
			rc = errno;
			goto err;
		}
	}
	if (mi->sample_stat) {
		mi->sf = fopen(procstat, "r");
		if (!mi->sf) {
			ovis_log(mi->log, OVIS_LERROR,
				 "Error %d opening the '%s' file.",
				 errno, procstat);
			rc = errno;
			goto err;
		}
		char *s;
		char lbuf[IFNAMSIZ];
		do {
			s = fgets(lbuf, sizeof(lbuf), mi->sf);
			if (!s)
				break;
			if (0 == strncmp(s, "cpu", 3)) {
				n_cpu++;
			}
		} while(s);

		mi->stat_rec_heap_sz = ldms_record_heap_size_get(stat_rec);
		mi->stat_heap_sz = n_cpu * ldms_record_heap_size_get(stat_rec);
		mi->stat_rec_idx = ldms_schema_record_add(schema, stat_rec);
		if (mi->stat_rec_idx < 0) {
			rc = -mi->stat_rec_idx;
			goto err3;
		}
		mi->stat_list_mid = ldms_schema_metric_list_add(schema,
								"cpu_list",
								NULL, mi->stat_heap_sz);
		if (mi->stat_list_mid < 0) {
			rc = -mi->stat_list_mid;
			goto err3;
		}
	}
	/* procnetdev metrics */
	if (mi->sample_netdev) {
		mi->nf = fopen(netdev, "r");
		if (!mi->nf) {
			ovis_log(mi->log, OVIS_LERROR,
				 "Error %d opening '%s'\n",
				 errno, netdev);
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
		mi->netdev_list_mid =
			ldms_schema_metric_list_add(schema,
						    "netdev_list",
						    NULL, mi->heap_sz);
		if (mi->netdev_list_mid < 0) {
			rc = -mi->netdev_list_mid;
			goto err3;
		}
	}
	mi->heap_sz = mi->stat_heap_sz + mi->heap_sz;
	mi->set = base_set_new_heap(mi->base, mi->heap_sz);
	if (!mi->set) {
		rc = errno;
		goto err2;
	}
	return 0;

err3:
	if (stat_rec)
		ldms_record_delete(stat_rec);
	if (rec_def)
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
	return  "config name=CFG-NAME plugin=curated_metrics path=PATH\n"
		"    PATH to a JSON configuration file containing a dictionary\n"
		"         as follows:\n"
		"              {\n"
		"                  \"metrics\" : [ <name>, <name>, ... ],\n"
		"                  \"ifaces\"  : [ <iface>, <iface>, ... ]\n"
		"              }\n"
		BASE_CONFIG_USAGE;
	 }

static int config(ldmsd_plug_handle_t handle,
		  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	curated_metrics_t mi = ldmsd_plug_ctxt_get(handle);
	int rc;

	/*
	 * load name=ci1 plugin=curated_metrics
	 * configure name=ci1 ...
	 * load name=ci2 plugin=curated_metrics
	 * configure name=ci1 ...
	 */
	if (mi->set) {
		ovis_log(mi->log, OVIS_LERROR,
			 "'%s' has already been configured.\n",
			 ldmsd_plug_cfg_name_get(handle));
		return EINVAL;
	}

	char *path = av_value(avl, "path");
	if (path) {
		rc = parse_cmetrics(mi, path);
	} else {
		int i;
		for (i = 0; i < ARRAY_LEN(metric_list); i++) {
			mi->collect_list[i].collect = 1;
		}
	}

	mi->base = base_config(avl,
			       ldmsd_plug_cfg_name_get(handle),
			       ldmsd_plug_cfg_name_get(handle),
			       mi->log);
	if (!mi->base) {
		rc = errno;
		goto err;
	}
	rc = create_metric_set(mi);
	if (rc) {
		ovis_log(mi->log, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}
	return 0;
err:
	if (mi->base)
		base_del(mi->base);
	return rc;
}

static int minfo_handler(curated_metrics_t mi, int *metric_no) {
	if (!mi->sample_meminfo)
		return 0;
	int rc, i;
	char lbuf[256];
	char metric_name[128];
	char *s;

	rewind(mi->mf);
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
			rc = sscanf(lbuf, "%s %"PRIu64,
				    metric_name, &(mi->minfo.active));
			if (rc != 2) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "MemFree") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64,
				    metric_name, &(mi->minfo.memfree));
			if (rc != 2) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "MemTotal") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64,
				    metric_name, &(mi->minfo.memtotal));
			if (rc != 2) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Dirty") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.dirty));
			if (rc != 2) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Inactive") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.inactive));
			if (rc != 2) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Cached") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.cached));
			if (rc != 2) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Buffers") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.buffers));
			if (rc != 2) {
				rc = EINVAL;
				goto out;
			}
			continue;
		}
		if (strcmp(metric_name, "Writeback") == 0) {
			rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &(mi->minfo.writeback));
			if (rc != 2) {
				rc = EINVAL;
				goto out;
			}
		}
	} while (s);
	double dval;
	uint64_t val;
	for (i = 0; i < ARRAY_LEN(metric_list); i++) {
		if (mi->collect_list[i].collect && strcmp(metric_list[i].file_path,
							  PROC_MEMINFO) == 0) {
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

int loadavg_handler(curated_metrics_t mi, int *metric_no)
{
	if (!mi->sample_loadavg)
		return 0;
	int rc, i;
	char lbuf[256];
	char *s;

	assert(mi->lf);
	rewind(mi->lf);
	s = fgets(lbuf, sizeof(lbuf), mi->lf);
	if (!s) {
		ovis_log(mi->log, OVIS_LERROR,
			 "Error %d reading '%s' file.\n",
			 errno, procloadavg);
		return errno;
	}

#define LINE_FMT "%lg %lg %lg %" SCNu64 "/%" SCNu64 " %" SCNu64 "\n"
	rc = sscanf(lbuf, LINE_FMT,
		    &mi->loadavg.load1,
		    &mi->loadavg.load5,
		    &mi->loadavg.load15,
		    &mi->loadavg.runnable,
		    &mi->loadavg.scheduling_entities,
		    &mi->loadavg.new_pid);
	if (rc < 6) {
		ovis_log(mi->log, OVIS_LERROR,
			 "Error %d scanning input line '%s'.\n",
			 rc, lbuf);
		return EINVAL;
	}
	double dval;
	for (i = 0; i < ARRAY_LEN(mi->collect_list); i++) {
		if (!mi->collect_list[i].collect)
			continue;
		if (0 == strcmp(metric_list[i].file_path, PROC_LOADAVG)) {
			dval = metric_list[i].cb(mi);
			__try_set_dbl(mi->set, metric_no,
				      dval, metric_list[i].vtype);
			(*metric_no)++;
		}
	}
	return 0;
}

static int cpu_handler(curated_metrics_t mi, int *metric_no)
{
	if (!mi->sample_stat)
		return 0;
	int i, rc;
	char tok[128];
	int n;
	struct stat_row_ent *ent;
	cpu_t curr_cpu;
	ldms_mval_t lh, rec_inst, name_mval;
	time_t curr_time;
	uint64_t intr;
	uint64_t ctxt_sw;
	uint64_t fork;

	assert(mi->set);
	assert(mi->sf);
	curr_time = time(NULL);
	lh = ldms_metric_get(mi->base->set, mi->stat_list_mid);
	rewind(mi->sf);
	rec_inst = ldms_list_first(mi->base->set, lh, NULL, NULL);
	while (1 == fscanf(mi->sf, "%s", tok)) {
		ent = bsearch(tok, stat_row_ents, ARRAY_LEN(stat_row_ents),
			      sizeof(stat_row_ents[0]), stat_row_cmp);
		if (!ent) {
			ovis_log(mi->log, OVIS_LDEBUG, "unknown key: %s\n", tok);
			continue;
		}
		switch(ent->type) {
		case STAT_CPU:
			if (mi->stat.sbool) {
				n = fscanf(mi->sf,
					   " %"PRIu64 " %"PRIu64 " %"PRIu64
					   " %"PRIu64 " %"PRIu64 " %"PRIu64
					   " %"PRIu64 " %"PRIu64 " %"PRIu64
					   " %"PRIu64,
					   &curr_cpu.user,
					   &curr_cpu.nice,
					   &curr_cpu.sys,
					   &curr_cpu.idle,
					   &curr_cpu.iowait,
					   &curr_cpu.irq,
					   &curr_cpu.softirq,
					   &curr_cpu.steal,
					   &curr_cpu.guest,
					   &curr_cpu.guest_nice);
				if (n != 10) {
					rc = EINVAL;
					goto out;
				}
				curr_cpu.cpu_sum = curr_cpu.user + curr_cpu.nice
					+ curr_cpu.sys + curr_cpu.idle
					+ curr_cpu.iowait + curr_cpu.irq
					+ curr_cpu.softirq + curr_cpu.steal;
				mi->stat.cpu_data = curr_cpu;
				if (rec_inst == NULL) {
					rec_inst = ldms_record_alloc(mi->base->set, mi->stat_rec_idx);
					if (rec_inst == NULL)
						continue;
					name_mval = ldms_record_metric_get(rec_inst, 0);
					int j = 0;
					j = snprintf(name_mval->a_char, IFNAMSIZ, "%.7s", tok);
					if (j < 0) {
						ovis_log(mi->log, OVIS_LERROR,
							 "String %s exceeds buffer size\n",
							 tok);
					}
				}
				ldms_list_append_record(mi->base->set, lh, rec_inst);
				/* lh is the mval that refers to the cpu list in the set */
				int rec_metric_id = 0;
				for (i = 0; i < ARRAY_LEN(metric_list); i++) {
					if (mi->collect_list[i].collect
					    && strcmp(metric_list[i].file_path, PROC_STAT) == 0 && !metric_list[i].not_rec) {
						if (!strcmp(metric_list[i].name, "cpu_name")) {
							rec_metric_id++;
							continue;
						}
						double dval;
						mi->stat.dtime = curr_time - mi->stat.dtime;
						dval = metric_list[i].cb(mi);
						ldms_record_set_double(rec_inst, rec_metric_id, dval);
						rec_metric_id++;
					}
				}
				mi->stat.cpu_data = curr_cpu;
				rec_inst = ldms_list_next(mi->base->set, rec_inst, NULL, NULL);
			}
			break;
		case STAT_INTR:
			if (mi->stat.sbool) {
				n = fscanf(mi->sf, " %"PRIu64, &intr);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				mi->stat.intr = intr - mi->stat.intr;
			} else {
				n = fscanf(mi->sf, " %"PRIu64, &(mi->stat.intr));
				if (n != 1) {
					ovis_log(mi->log, OVIS_LERROR,
						 "No data reading intr from %s\n", PROC_STAT);
					rc = EINVAL;
					goto out;
				}
			}
			break;
		case STAT_CTXT:
			if (mi->stat.sbool) {
				n = fscanf(mi->sf, " %"PRIu64, &ctxt_sw);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				mi->stat.ctxt_sw = ctxt_sw - mi->stat.ctxt_sw;
			} else {
				n = fscanf(mi->sf, " %"PRIu64, &(mi->stat.ctxt_sw));
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
			}
			break;
		case STAT_BTIME:
			break;
		case STAT_PROCESSES:
			if (mi->stat.sbool) {
				n = fscanf(mi->sf, " %"PRIu64, &fork);
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
				mi->stat.fork = fork - mi->stat.fork;
			} else {
				n = fscanf(mi->sf, " %"PRIu64, &(mi->stat.fork));
				if (n != 1) {
					rc = EINVAL;
					goto out;
				}
			}
			break;
		case STAT_PROCS_BLOCKED:
			n = fscanf(mi->sf, "%"PRIu64, &mi->stat.procs_blocked);
			if (n != 1) {
				rc = ENODATA;
				ovis_log(mi->log, OVIS_LERROR,
					 "No data reading procs_blocked from %s\n", PROC_STAT);
				goto out;
			}
			break;
		default:
			rc = 0;
		}
	}
	double dval;
	if (mi->stat.sbool) {
		for (i = 0; i < ARRAY_LEN(metric_list); i++) {
			if (mi->collect_list[i].collect
			    && strcmp(metric_list[i].file_path, PROC_STAT) == 0 && metric_list[i].not_rec) {
				switch(metric_list[i].vtype) {
				case LDMS_V_D64:
					mi->stat.dtime = curr_time - mi->stat.dtime;
					dval = metric_list[i].cb(mi);
					__try_set_dbl(mi->set, metric_no,
						      dval, metric_list[i].vtype);
					break;
				case LDMS_V_U64:
					__try_set_u64(mi->set, metric_no,
						      mi->stat.procs_blocked,
						      metric_list[i].vtype);
					break;
				default:
					ovis_log(mi->log, OVIS_LCRITICAL,
						 "sample() memory corruption detected.\n");
					rc = EINVAL;
					return rc;
				}
				(*metric_no)++;
			}
		}
	}
	mi->stat.intr = intr;
	mi->stat.fork = fork;
	mi->stat.ctxt_sw = ctxt_sw;
	if (!mi->stat.sbool) {
		mi->stat.sbool = 1;
	}
	mi->stat.dtime = curr_time;
	rc = 0;
out:
	return rc;

}

static int netdev_handler(curated_metrics_t mi, int *metric_no)
{
	if (!mi->sample_netdev)
		return 0;
	int rc = 0;
	char lbuf[256];
	char *s;
	char curriface[IFNAMSIZ + 1];
	netdev_t curr_netdev;
	ldms_mval_t lh, rec_inst, name_mval;

	if (!mi->base->set) {
		ovis_log(mi->log, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}
	if (!mi->nf) {
		mi->nf = fopen(netdev, "r");
		if (!mi->nf) {
			ovis_log(mi->log, OVIS_LDEBUG,
				 "Error %d opening file '%s'.",
				 errno, netdev);
			return errno;
		}
	}
	lh = ldms_metric_get(mi->base->set, mi->netdev_list_mid);

	rewind(mi->nf);
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
				" %" PRIu64 "\n",
				curriface,
				&curr_netdev.rx_bytes,
				&curr_netdev.rx_packets,
				&curr_netdev.rx_errs,
				&curr_netdev.rx_drop,
				&curr_netdev.rx_fifo,
				&curr_netdev.rx_frame,
				&curr_netdev.rx_cmprsd,
				&curr_netdev.rx_multicast,
				&curr_netdev.tx_bytes,
				&curr_netdev.tx_packets,
				&curr_netdev.tx_errs,
				&curr_netdev.tx_drop,
				&curr_netdev.tx_fifo,
				&curr_netdev.tx_colls,
				&curr_netdev.tx_carrier,
				&curr_netdev.tx_cmprsd);
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
			mi->cur_iface->data.rx_bytes =
				curr_netdev.rx_bytes - mi->cur_iface->data.rx_bytes;
			mi->cur_iface->data.rx_packets =
				curr_netdev.rx_packets - mi->cur_iface->data.rx_packets;
			mi->cur_iface->data.rx_errs =
				curr_netdev.rx_errs - mi->cur_iface->data.rx_errs;
			mi->cur_iface->data.rx_drop =
				curr_netdev.rx_drop - mi->cur_iface->data.rx_drop;
			mi->cur_iface->data.tx_bytes =
				curr_netdev.tx_bytes - mi->cur_iface->data.tx_bytes;
			mi->cur_iface->data.tx_packets =
				curr_netdev.tx_packets - mi->cur_iface->data.tx_packets;
			mi->cur_iface->data.tx_errs =
				curr_netdev.tx_errs - mi->cur_iface->data.tx_errs;
			mi->cur_iface->data.dtime =
				curr_netdev.dtime - mi->cur_iface->data.dtime;

			enum ldms_value_type typ;
			size_t cnt;
			for (rec_inst = ldms_list_first(mi->base->set, lh, &typ, &cnt);
					rec_inst;
					rec_inst = ldms_list_next(mi->base->set, rec_inst, &typ, &cnt)) {
				name_mval = ldms_record_metric_get(rec_inst, 0);
				if (0 == strcmp(name_mval->a_char, curriface))
					break;
			}
			/* If we didn't find an instance for this interface, add one */
			if (rec_inst == NULL) {
				rec_inst = ldms_record_alloc(mi->base->set, mi->rec_def_idx);
				if (rec_inst == NULL)
					continue;
				name_mval = ldms_record_metric_get(rec_inst, 0);
				int j = 0;
				j = snprintf(name_mval->a_char, IFNAMSIZ, "%s", curriface);
				if (j < 0) {
					ovis_log(mi->log, OVIS_LERROR,
						 "interface name %s exceeds buffer size\n",
						 curriface);
				}
				/* lh is the mval that refers to the netdev list in the set */
				ldms_list_append_record(mi->base->set, lh, rec_inst);
			}

			int rec_metric_id = 0;
			int i = 0;
			for (i = 0; i < ARRAY_LEN(mi->collect_list); i++) {
				if (metric_list[i].not_rec)
					continue;
				if (strcmp(metric_list[i].file_path, NET_DEV) != 0)
					continue;
				if (!mi->collect_list[i].collect)
					continue;
				if (!strcmp(metric_list[i].name, "iface_name")) {
					rec_metric_id++;
					continue;
				}
				double dval = metric_list[i].cb(mi);
				ldms_record_set_double(rec_inst, rec_metric_id, dval);
				rec_metric_id++;
			}
			strcpy(mi->cur_iface->name, curriface);
			mi->cur_iface->data = curr_netdev;
		}
	} while(s);
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
		ovis_log(mi->log, OVIS_LDEBUG,
			 "plugin '%s' not initialized\n",
			 ldmsd_plug_cfg_name_get(handle));
		return EINVAL;
	}

	base_sample_begin(mi->base);
	mi->curr_time = time(NULL);
	metric_cnt = mi->metric_offset;
	if (!mi->skip_first) {
		rc = minfo_handler(mi, metric_no);
		if (rc) {
			ovis_log(mi->log, OVIS_LDEBUG,
				 "meminfo metrics not initialized\n");
		}
		rc = loadavg_handler(mi, metric_no);
		if (rc) {
			ovis_log(mi->log, OVIS_LDEBUG,
				 "loadavg metrics not initialized\n");
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
	mi->prev_time = mi->curr_time;
	base_sample_end(mi->base);
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	curated_metrics_t mi = calloc(1, sizeof(*mi));
	if (mi) {
		ldmsd_plug_ctxt_set(handle, mi);
		TAILQ_INIT(&mi->netdevq);
		mi->log = ldmsd_plug_log_get(handle);
		mi->skip_first = 1;
		mi->stat.sbool = 0;
		mi->sample_meminfo = 1;
		mi->sample_stat = 1;
		mi->sample_loadavg = 1;
		mi->sample_netdev = 1;
		return 0;
	}
	return ENOMEM;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	curated_metrics_t mi = ldmsd_plug_ctxt_get(handle);
	struct iface_ent *e1, *e2;
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
	if (mi->nf)
		fclose(mi->nf);
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
