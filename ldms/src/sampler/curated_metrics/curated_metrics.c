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
 * \brief Curated subset of /proc/meminfo, /proc/loadavg, /proc/stat and
 *        /proc/net/dev, reported as percentages and per-second rates.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/queue.h>
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

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof((a))/sizeof((a)[0]))
#endif

enum cm_src {
	SRC_MEMINFO,
	SRC_LOADAVG,
	SRC_STAT,
	SRC_NETDEV,
	SRC_COUNT
};

static const char *src_path[SRC_COUNT] = {
	[SRC_MEMINFO] = PROC_MEMINFO,
	[SRC_LOADAVG] = PROC_LOADAVG,
	[SRC_STAT]    = PROC_STAT,
	[SRC_NETDEV]  = NET_DEV,
};

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

/* loadavg vars */
typedef struct loadavg_data {
	double load1;
	double load5;
	double load15;
	uint64_t runnable;
	uint64_t scheduling_entities;
	uint64_t new_pid;
} load_t;

/* per-cpu jiffies */
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

/*
 * /proc/stat values handed to the metric callbacks. cpu_data holds the
 * per-interval delta of the CPU currently being written; intr, ctxt_sw and
 * fork hold per-interval deltas.
 */
typedef struct stat_data {
	cpu_t cpu_data;
	uint64_t ctxt_sw;
	uint64_t intr;
	uint64_t fork;
	uint64_t procs_running;
	uint64_t procs_blocked;
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
} netdev_t;

/*
 * One CPU or one interface: its previous raw counters and its record
 * instance in the set (NULL until the first interval is written).
 */
typedef struct cm_ent {
	TAILQ_ENTRY(cm_ent) entry;
	char name[IFNAMSIZ];
	uint64_t gen;		/* last sample generation it was seen in */
	int primed;		/* prev holds a valid reading */
	ldms_mval_t rec;
	union {
		cpu_t cpu;
		netdev_t net;
	} prev;
} *cm_ent_t;
TAILQ_HEAD(cm_ent_list, cm_ent);

/* A selected metric: its metric_list index and its set/record metric id */
struct cm_sel {
	int midx;
	int id;
};

#define NUM_METRICS 32

typedef struct curated_metrics_s {
	FILE *fp[SRC_COUNT];
	int sample_src[SRC_COUNT];
	int collect[NUM_METRICS];

	/* Selected scalar metrics, per source, in schema order */
	struct cm_sel scalar[SRC_COUNT][NUM_METRICS];
	int n_scalar[SRC_COUNT];

	/* Selected record fields (name excluded), per source */
	struct cm_sel field[SRC_COUNT][NUM_METRICS];
	int n_field[SRC_COUNT];
	int name_id[SRC_COUNT];		/* record index of the name field */
	int rec_idx[SRC_COUNT];		/* record definition metric id */
	int list_mid[SRC_COUNT];	/* list metric id, -1 if none */
	int heap_full_logged[SRC_COUNT];

	/* Known CPUs / interfaces, in /proc order */
	struct cm_ent_list ents[SRC_COUNT];
	cm_ent_t hint[SRC_COUNT];
	uint64_t gen;

	/* Optional interface filter and set heap sizing */
	char **ifaces;
	int n_ifaces;
	int max_ifaces;

	char *lbuf;
	size_t lbuf_sz;

	ovis_log_t log;
	base_data_t base;
	int skip_first;

	mem_t minfo;
	load_t loadavg;
	stat_t stat;
	netdev_t net;		/* delta of the interface being written */
	uint64_t prev_intr;
	uint64_t prev_ctxt;
	uint64_t prev_fork;

	struct timespec prev_ts;
	double dtime;		/* seconds since the previous sample */
} *curated_metrics_t;

typedef void (*metric_cb)(curated_metrics_t mi, ldms_mval_t v);

/* available metrics */
typedef struct cm_s {
	const char *name;
	int not_rec;
	enum cm_src src;
	enum ldms_value_type vtype;
	metric_cb cb;
	const char unit[16];
} *cm_t;

static inline uint64_t dlt(uint64_t cur, uint64_t prev)
{
	/* Treat a counter that went backwards (reset, 32-bit wrap,
	 * iowait jitter) as zero instead of a huge unsigned value. */
	return cur >= prev ? cur - prev : 0;
}

static inline double pct(uint64_t part, uint64_t whole)
{
	return whole ? ((double)part / (double)whole) * 100.0 : 0.0;
}

static inline double rate(curated_metrics_t mi, uint64_t count)
{
	return mi->dtime > 0 ? (double)count / mi->dtime : 0.0;
}

static void free_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = pct(mi->minfo.memfree, mi->minfo.memtotal);
}
static void buff_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = pct(mi->minfo.buffers, mi->minfo.memtotal);
}
static void cached_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = pct(mi->minfo.cached, mi->minfo.memtotal);
}
static void active_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = pct(mi->minfo.active, mi->minfo.memtotal);
}
static void inactive_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = pct(mi->minfo.inactive, mi->minfo.memtotal);
}
static void dirty_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = pct(mi->minfo.dirty, mi->minfo.memtotal);
}
static void writeback_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = pct(mi->minfo.writeback, mi->minfo.memtotal);
}
static void memtotal_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_u64 = mi->minfo.memtotal;
}
static void load1_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = mi->loadavg.load1;
}
static void load5_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = mi->loadavg.load5;
}
static void load15_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = mi->loadavg.load15;
}
static void run_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_u64 = mi->loadavg.runnable;
}
static void sched_ent_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_u64 = mi->loadavg.scheduling_entities;
}
static void new_pid_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_u64 = mi->loadavg.new_pid;
}
static void idle_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = pct(mi->stat.cpu_data.idle, mi->stat.cpu_data.cpu_sum);
}
static void sys_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = pct(mi->stat.cpu_data.sys, mi->stat.cpu_data.cpu_sum);
}
static void user_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = pct(mi->stat.cpu_data.user, mi->stat.cpu_data.cpu_sum);
}
static void iowait_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = pct(mi->stat.cpu_data.iowait, mi->stat.cpu_data.cpu_sum);
}
static void intr_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = rate(mi, mi->stat.intr);
}
static void ctxt_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = rate(mi, mi->stat.ctxt_sw);
}
static void fork_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = rate(mi, mi->stat.fork);
}
static void procs_run_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_u64 = mi->stat.procs_running;
}
static void procs_block_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_u64 = mi->stat.procs_blocked;
}
static void rxb_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = rate(mi, mi->net.rx_bytes);
}
static void rx_packets_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = rate(mi, mi->net.rx_packets);
}
static void rx_errs_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = rate(mi, mi->net.rx_errs);
}
static void rx_drop_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = rate(mi, mi->net.rx_drop);
}
static void txb_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = rate(mi, mi->net.tx_bytes);
}
static void txp_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = rate(mi, mi->net.tx_packets);
}
static void tx_errs_cb(curated_metrics_t mi, ldms_mval_t v) {
	v->v_d = rate(mi, mi->net.tx_errs);
}

/*
 * Schema order follows this table. Within a record source, the name field
 * (LDMS_V_CHAR_ARRAY) must come first; it is added automatically whenever
 * any other field of that record is selected.
 */
static const struct cm_s metric_list[] = {
	{ "free",     1, SRC_MEMINFO, LDMS_V_D64, free_cb, "%" },
	{ "buff",     1, SRC_MEMINFO, LDMS_V_D64, buff_cb, "%" },
	{ "cached",   1, SRC_MEMINFO, LDMS_V_D64, cached_cb, "%" },
	{ "inactive", 1, SRC_MEMINFO, LDMS_V_D64, inactive_cb, "%" },
	{ "active",   1, SRC_MEMINFO, LDMS_V_D64, active_cb, "%" },
	{ "dirty",    1, SRC_MEMINFO, LDMS_V_D64, dirty_cb, "%" },
	{ "writeback", 1, SRC_MEMINFO, LDMS_V_D64, writeback_cb, "%" },
	{ "MemTotal", 1, SRC_MEMINFO, LDMS_V_U64, memtotal_cb, "kB" },
	{ "load1min", 1, SRC_LOADAVG, LDMS_V_D64, load1_cb, "avg/1min" },
	{ "load5min", 1, SRC_LOADAVG, LDMS_V_D64, load5_cb, "avg/5min" },
	{ "load15min", 1, SRC_LOADAVG, LDMS_V_D64, load15_cb, "avg/15min" },
	{ "runnable", 1, SRC_LOADAVG, LDMS_V_U64, run_cb, "count" },
	{ "scheduling_entities", 1, SRC_LOADAVG, LDMS_V_U64, sched_ent_cb, "count" },
	{ "newest_pid", 1, SRC_LOADAVG, LDMS_V_U64, new_pid_cb, "pid" },
	{ "cpu_name", 0, SRC_STAT, LDMS_V_CHAR_ARRAY, NULL, "" },
	{ "idle", 0, SRC_STAT, LDMS_V_D64, idle_cb, "%" },
	{ "sys", 0, SRC_STAT, LDMS_V_D64, sys_cb, "%" },
	{ "user", 0, SRC_STAT, LDMS_V_D64, user_cb, "%" },
	{ "iowait", 0, SRC_STAT, LDMS_V_D64, iowait_cb, "%" },
	{ "interrupts", 1, SRC_STAT, LDMS_V_D64, intr_cb, "intr/sec" },
	{ "context_sw", 1, SRC_STAT, LDMS_V_D64, ctxt_cb, "ctxt_sw/sec" },
	{ "fork", 1, SRC_STAT, LDMS_V_D64, fork_cb, "forks/sec" },
	{ "procs_running", 1, SRC_STAT, LDMS_V_U64, procs_run_cb, "count" },
	{ "procs_blocked", 1, SRC_STAT, LDMS_V_U64, procs_block_cb, "count" },
	{ "iface_name", 0, SRC_NETDEV, LDMS_V_CHAR_ARRAY, NULL, "" },
	{ "rx_bytes", 0, SRC_NETDEV, LDMS_V_D64, rxb_cb, "rx_bytes/sec" },
	{ "rx_packets", 0, SRC_NETDEV, LDMS_V_D64, rx_packets_cb, "rx_packets/sec" },
	{ "rx_errs", 0, SRC_NETDEV, LDMS_V_D64, rx_errs_cb, "rx_errs/sec" },
	{ "rx_drop", 0, SRC_NETDEV, LDMS_V_D64, rx_drop_cb, "rx_drop/sec" },
	{ "tx_bytes", 0, SRC_NETDEV, LDMS_V_D64, txb_cb, "tx_bytes/sec" },
	{ "tx_packets", 0, SRC_NETDEV, LDMS_V_D64, txp_cb, "tx_packets/sec" },
	{ "tx_errs", 0, SRC_NETDEV, LDMS_V_D64, tx_errs_cb, "tx_errs/sec" }
};

_Static_assert(ARRAY_LEN(metric_list) == NUM_METRICS,
	       "NUM_METRICS must match the size of metric_list");

static int metric_find(const char *name)
{
	int i;
	for (i = 0; i < NUM_METRICS; i++) {
		if (0 == strcmp(name, metric_list[i].name))
			return i;
	}
	return -1;
}

static void free_ifaces(curated_metrics_t mi)
{
	int i;
	if (!mi->ifaces)
		return;
	for (i = 0; i < mi->n_ifaces; i++)
		free(mi->ifaces[i]);
	free(mi->ifaces);
	mi->ifaces = NULL;
	mi->n_ifaces = 0;
}

static int iface_wanted(curated_metrics_t mi, const char *name)
{
	int i;
	if (!mi->ifaces)
		return 1;
	for (i = 0; i < mi->n_ifaces; i++) {
		if (0 == strcmp(mi->ifaces[i], name))
			return 1;
	}
	return 0;
}

static int parse_cmetrics(curated_metrics_t mi, const char *path)
{
	json_t *cfp, *jm, *ji, *v;
	json_error_t jerr;
	size_t j;
	int i, rc = 0;

	cfp = json_load_file(path, 0, &jerr);
	if (!cfp) {
		ovis_log(mi->log, OVIS_LERROR,
			 "Error parsing '%s' configuration file.\n", path);
		ovis_log(mi->log, OVIS_LERROR,
			 "%s: Line: %d, Column: %d, %s\n",
			 path, jerr.line, jerr.column, jerr.text);
		return EINVAL;
	}

	jm = json_object_get(cfp, "metrics");
	if (!jm || !json_is_array(jm)) {
		ovis_log(mi->log, OVIS_LERROR,
			 "%s: 'metrics' must be present and be a list of "
			 "metric names.\n", path);
		rc = EINVAL;
		goto out;
	}
	memset(mi->collect, 0, sizeof(mi->collect));
	json_array_foreach(jm, j, v) {
		const char *mname = json_string_value(v);
		if (!mname) {
			ovis_log(mi->log, OVIS_LERROR,
				 "%s: metrics[%zu] is not a string.\n", path, j);
			rc = EINVAL;
			goto out;
		}
		i = metric_find(mname);
		if (i < 0) {
			ovis_log(mi->log, OVIS_LERROR,
				 "%s: unknown metric '%s'.\n", path, mname);
			rc = EINVAL;
			goto out;
		}
		mi->collect[i] = 1;
	}

	ji = json_object_get(cfp, "ifaces");
	if (ji) {
		if (!json_is_array(ji)) {
			ovis_log(mi->log, OVIS_LERROR,
				 "%s: 'ifaces' must be a list of interface "
				 "names.\n", path);
			rc = EINVAL;
			goto out;
		}
		mi->ifaces = calloc(json_array_size(ji) + 1, sizeof(char *));
		if (!mi->ifaces) {
			rc = ENOMEM;
			goto out;
		}
		json_array_foreach(ji, j, v) {
			const char *iname = json_string_value(v);
			if (!iname || strlen(iname) >= IFNAMSIZ) {
				ovis_log(mi->log, OVIS_LERROR,
					 "%s: ifaces[%zu] must be an interface "
					 "name shorter than %d characters.\n",
					 path, j, IFNAMSIZ);
				rc = EINVAL;
				goto out;
			}
			mi->ifaces[mi->n_ifaces] = strdup(iname);
			if (!mi->ifaces[mi->n_ifaces]) {
				rc = ENOMEM;
				goto out;
			}
			mi->n_ifaces++;
		}
	}
out:
	json_decref(cfp);
	return rc;
}

/*
 * Decide which sources are sampled, and add the name field to any record
 * that has other fields selected (record code relies on it).
 */
static int select_sources(curated_metrics_t mi)
{
	int i, any = 0;
	int has_field[SRC_COUNT] = {0};

	for (i = 0; i < NUM_METRICS; i++) {
		if (mi->collect[i] && !metric_list[i].not_rec &&
		    metric_list[i].vtype != LDMS_V_CHAR_ARRAY)
			has_field[metric_list[i].src] = 1;
	}
	for (i = 0; i < NUM_METRICS; i++) {
		if (metric_list[i].vtype == LDMS_V_CHAR_ARRAY &&
		    has_field[metric_list[i].src])
			mi->collect[i] = 1;
	}
	memset(mi->sample_src, 0, sizeof(mi->sample_src));
	for (i = 0; i < NUM_METRICS; i++) {
		if (mi->collect[i]) {
			mi->sample_src[metric_list[i].src] = 1;
			any = 1;
		}
	}
	return any ? 0 : EINVAL;
}

static void close_files(curated_metrics_t mi)
{
	int src;
	for (src = 0; src < SRC_COUNT; src++) {
		if (mi->fp[src]) {
			fclose(mi->fp[src]);
			mi->fp[src] = NULL;
		}
	}
}

static void free_ents(curated_metrics_t mi)
{
	int src;
	cm_ent_t e;
	for (src = 0; src < SRC_COUNT; src++) {
		while ((e = TAILQ_FIRST(&mi->ents[src]))) {
			TAILQ_REMOVE(&mi->ents[src], e, entry);
			free(e);
		}
		mi->hint[src] = NULL;
	}
}

static void reset_selection(curated_metrics_t mi)
{
	int src;
	close_files(mi);
	free_ents(mi);
	free_ifaces(mi);
	memset(mi->collect, 0, sizeof(mi->collect));
	memset(mi->sample_src, 0, sizeof(mi->sample_src));
	memset(mi->n_scalar, 0, sizeof(mi->n_scalar));
	memset(mi->n_field, 0, sizeof(mi->n_field));
	for (src = 0; src < SRC_COUNT; src++) {
		mi->list_mid[src] = -1;
		mi->rec_idx[src] = -1;
		mi->name_id[src] = -1;
	}
}

/* Rewind (or open) the file for src. */
static FILE *src_file(curated_metrics_t mi, enum cm_src src, int *rc)
{
	if (mi->fp[src]) {
		rewind(mi->fp[src]);
		return mi->fp[src];
	}
	mi->fp[src] = fopen(src_path[src], "r");
	if (!mi->fp[src]) {
		*rc = errno;
		ovis_log(mi->log, OVIS_LERROR, "Error %d opening '%s'.\n",
			 *rc, src_path[src]);
	}
	return mi->fp[src];
}

/* Record slots for CPUs: every configured CPU plus the aggregate line. */
static size_t cpu_capacity(void)
{
	long n = sysconf(_SC_NPROCESSORS_CONF);
	if (n < 1)
		n = 1;
	return (size_t)n + 1;
}

/* Record slots for interfaces. */
static size_t netdev_capacity(curated_metrics_t mi)
{
	FILE *f;
	char *line = NULL;
	size_t sz = 0, n = 0;

	if (mi->max_ifaces)
		return mi->max_ifaces;
	if (mi->ifaces)
		return mi->n_ifaces;
	f = fopen(NET_DEV, "r");
	if (f) {
		while (getline(&line, &sz, f) > 0) {
			if (strchr(line, ':'))
				n++;
		}
		free(line);
		fclose(f);
	}
	/* Leave room for interfaces created after config */
	return 2 * n + 8;
}

static int create_metric_set(curated_metrics_t mi)
{
	ldms_schema_t schema;
	ldms_record_t rec[SRC_COUNT] = { NULL };
	size_t heap_sz = 0;
	int i, rc, src, id;
	FILE *f;

	schema = base_schema_new(mi->base);
	if (!schema) {
		rc = errno ? errno : ENOMEM;
		ovis_log(mi->log, OVIS_LERROR,
			 "The schema '%s' could not be created, errno=%d.\n",
			 mi->base->schema_name, rc);
		return rc;
	}

	for (i = 0; i < NUM_METRICS; i++) {
		const struct cm_s *m = &metric_list[i];
		if (!mi->collect[i])
			continue;
		src = m->src;
		if (m->not_rec) {
			id = ldms_schema_metric_add_with_unit(schema, m->name,
							      m->unit, m->vtype);
			if (id < 0) {
				rc = -id;
				goto err;
			}
			mi->scalar[src][mi->n_scalar[src]++] =
				(struct cm_sel){ .midx = i, .id = id };
			continue;
		}
		if (!rec[src]) {
			rec[src] = ldms_record_create(src == SRC_STAT ?
						      "cpu" : "netdev");
			if (!rec[src]) {
				rc = errno ? errno : ENOMEM;
				goto err;
			}
		}
		if (m->vtype == LDMS_V_CHAR_ARRAY) {
			id = ldms_record_metric_add(rec[src], m->name, m->name,
						    m->vtype, IFNAMSIZ);
			if (id >= 0)
				mi->name_id[src] = id;
		} else {
			id = ldms_record_metric_add(rec[src], m->name, m->unit,
						    m->vtype, 0);
			if (id >= 0)
				mi->field[src][mi->n_field[src]++] =
					(struct cm_sel){ .midx = i, .id = id };
		}
		if (id < 0) {
			rc = -id;
			goto err;
		}
	}

	for (src = SRC_STAT; src <= SRC_NETDEV; src++) {
		size_t sz;
		if (!rec[src])
			continue;
		sz = (src == SRC_STAT ? cpu_capacity() : netdev_capacity(mi))
			* ldms_record_heap_size_get(rec[src]);
		mi->rec_idx[src] = ldms_schema_record_add(schema, rec[src]);
		if (mi->rec_idx[src] < 0) {
			rc = -mi->rec_idx[src];
			goto err;
		}
		rec[src] = NULL; /* owned by the schema now */
		mi->list_mid[src] = ldms_schema_metric_list_add(schema,
				src == SRC_STAT ? "cpu_list" : "netdev_list",
				NULL, sz);
		if (mi->list_mid[src] < 0) {
			rc = -mi->list_mid[src];
			goto err;
		}
		heap_sz += sz;
	}

	rc = 0;
	for (src = 0; src < SRC_COUNT; src++) {
		if (!mi->sample_src[src])
			continue;
		f = src_file(mi, src, &rc);
		if (!f)
			goto err;
	}

	if (!base_set_new_heap(mi->base, heap_sz)) {
		rc = errno ? errno : ENOMEM;
		ovis_log(mi->log, OVIS_LERROR,
			 "Failed to create set, error %d.\n", rc);
		goto err;
	}
	return 0;

err:
	for (src = 0; src < SRC_COUNT; src++)
		ldms_record_delete(rec[src]);
	close_files(mi);
	/* The schema itself is released with mi->base by the caller */
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=CFG-NAME plugin=curated_metrics [path=PATH] [max_ifaces=N]\n"
		"    PATH to a JSON configuration file containing a dictionary\n"
		"         as follows:\n"
		"              {\n"
		"                  \"metrics\" : [ <name>, <name>, ... ],\n"
		"                  \"ifaces\"  : [ <iface>, <iface>, ... ]\n"
		"              }\n"
		"         \"ifaces\" is optional; when present only those\n"
		"         interfaces are reported.\n"
		"    max_ifaces Number of interface records to reserve in the\n"
		"         set (default: the configured ifaces, or twice the\n"
		"         interfaces present at config time plus 8).\n"
		BASE_CONFIG_USAGE;
}

static int config(ldmsd_plug_handle_t handle,
		  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	curated_metrics_t mi = ldmsd_plug_ctxt_get(handle);
	char *path, *value;
	int rc, i;

	if (mi->base) {
		ovis_log(mi->log, OVIS_LERROR,
			 "'%s' has already been created.\n",
			 ldmsd_plug_cfg_name_get(handle));
		return EINVAL;
	}
	reset_selection(mi);

	path = av_value(avl, "path");
	if (path) {
		rc = parse_cmetrics(mi, path);
		if (rc)
			goto err;
	} else {
		for (i = 0; i < NUM_METRICS; i++)
			mi->collect[i] = 1;
	}

	value = av_value(avl, "max_ifaces");
	if (value) {
		char *end;
		long n = strtol(value, &end, 0);
		if (*end || n < 1 || n > 65536) {
			ovis_log(mi->log, OVIS_LERROR,
				 "max_ifaces '%s' is not a valid count.\n",
				 value);
			rc = EINVAL;
			goto err;
		}
		mi->max_ifaces = n;
	}

	rc = select_sources(mi);
	if (rc) {
		ovis_log(mi->log, OVIS_LERROR, "No metrics selected.\n");
		goto err;
	}

	mi->base = base_config(avl,
			       ldmsd_plug_cfg_name_get(handle),
			       ldmsd_plug_cfg_name_get(handle),
			       mi->log);
	if (!mi->base) {
		rc = errno ? errno : EINVAL;
		goto err;
	}
	rc = create_metric_set(mi);
	if (rc) {
		ovis_log(mi->log, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}
	mi->skip_first = 1;
	return 0;
err:
	base_del(mi->base);
	mi->base = NULL;
	reset_selection(mi);
	return rc;
}

static void write_scalars(curated_metrics_t mi, enum cm_src src)
{
	union ldms_value val;
	int k;

	for (k = 0; k < mi->n_scalar[src]; k++) {
		metric_list[mi->scalar[src][k].midx].cb(mi, &val);
		ldms_metric_set(mi->base->set, mi->scalar[src][k].id, &val);
	}
}

/* Find the entry for name; /proc order is stable, so try the hint first. */
static cm_ent_t ent_lookup(curated_metrics_t mi, enum cm_src src,
			   const char *name)
{
	cm_ent_t e = mi->hint[src];

	if (!e || strcmp(e->name, name)) {
		TAILQ_FOREACH(e, &mi->ents[src], entry) {
			if (0 == strcmp(e->name, name))
				break;
		}
	}
	if (!e) {
		e = calloc(1, sizeof(*e));
		if (!e)
			return NULL;
		snprintf(e->name, sizeof(e->name), "%s", name);
		TAILQ_INSERT_TAIL(&mi->ents[src], e, entry);
	}
	mi->hint[src] = TAILQ_NEXT(e, entry);
	e->gen = mi->gen;
	return e;
}

/* Drop CPUs / interfaces that were not in this sample. */
static void ent_sweep(curated_metrics_t mi, enum cm_src src, ldms_mval_t lh)
{
	cm_ent_t e, next;

	for (e = TAILQ_FIRST(&mi->ents[src]); e; e = next) {
		next = TAILQ_NEXT(e, entry);
		if (e->gen == mi->gen)
			continue;
		if (e->rec)
			ldms_list_remove_item(mi->base->set, lh, e->rec);
		TAILQ_REMOVE(&mi->ents[src], e, entry);
		free(e);
	}
	mi->hint[src] = TAILQ_FIRST(&mi->ents[src]);
}

/* Write the selected fields for e, allocating its record on first use. */
static void write_record(curated_metrics_t mi, enum cm_src src,
			 ldms_mval_t lh, cm_ent_t e)
{
	union ldms_value val;
	int k;

	if (!e->rec) {
		ldms_mval_t name;
		e->rec = ldms_record_alloc(mi->base->set, mi->rec_idx[src]);
		if (!e->rec) {
			if (!mi->heap_full_logged[src]) {
				ovis_log(mi->log, OVIS_LWARNING,
					 "No room in the set for '%s'%s.\n",
					 e->name, src == SRC_NETDEV ?
					 "; increase max_ifaces" : "");
				mi->heap_full_logged[src] = 1;
			}
			return;
		}
		name = ldms_record_metric_get(e->rec, mi->name_id[src]);
		snprintf(name->a_char, IFNAMSIZ, "%s", e->name);
		ldms_list_append_record(mi->base->set, lh, e->rec);
	}
	for (k = 0; k < mi->n_field[src]; k++) {
		const struct cm_s *m = &metric_list[mi->field[src][k].midx];
		m->cb(mi, &val);
		if (m->vtype == LDMS_V_D64)
			ldms_record_set_double(e->rec, mi->field[src][k].id, val.v_d);
		else
			ldms_record_set_u64(e->rec, mi->field[src][k].id, val.v_u64);
	}
}

static const struct {
	const char *key;
	size_t off;
} minfo_keys[] = {
	{ "MemTotal",  offsetof(mem_t, memtotal) },
	{ "MemFree",   offsetof(mem_t, memfree) },
	{ "Buffers",   offsetof(mem_t, buffers) },
	{ "Cached",    offsetof(mem_t, cached) },
	{ "Active",    offsetof(mem_t, active) },
	{ "Inactive",  offsetof(mem_t, inactive) },
	{ "Dirty",     offsetof(mem_t, dirty) },
	{ "Writeback", offsetof(mem_t, writeback) },
};

static int minfo_handler(curated_metrics_t mi, int prime)
{
	FILE *f;
	int rc = 0, k, found = 0;
	char key[64];
	uint64_t val;

	if (!mi->sample_src[SRC_MEMINFO] || prime)
		return 0;
	f = src_file(mi, SRC_MEMINFO, &rc);
	if (!f)
		return rc;
	memset(&mi->minfo, 0, sizeof(mi->minfo));
	while (found < (int)ARRAY_LEN(minfo_keys) &&
	       getline(&mi->lbuf, &mi->lbuf_sz, f) > 0) {
		if (2 != sscanf(mi->lbuf, "%63[^:]: %" SCNu64, key, &val))
			continue;
		for (k = 0; k < (int)ARRAY_LEN(minfo_keys); k++) {
			if (0 == strcmp(key, minfo_keys[k].key)) {
				*(uint64_t *)((char *)&mi->minfo +
					      minfo_keys[k].off) = val;
				found++;
				break;
			}
		}
	}
	if (!mi->minfo.memtotal) {
		ovis_log(mi->log, OVIS_LERROR,
			 "MemTotal not found in %s.\n", PROC_MEMINFO);
		return ENODATA;
	}
	write_scalars(mi, SRC_MEMINFO);
	return 0;
}

static int loadavg_handler(curated_metrics_t mi, int prime)
{
	FILE *f;
	int rc = 0;

	if (!mi->sample_src[SRC_LOADAVG] || prime)
		return 0;
	f = src_file(mi, SRC_LOADAVG, &rc);
	if (!f)
		return rc;
	if (getline(&mi->lbuf, &mi->lbuf_sz, f) <= 0) {
		ovis_log(mi->log, OVIS_LERROR,
			 "Error %d reading '%s'.\n", errno, PROC_LOADAVG);
		return EIO;
	}
	rc = sscanf(mi->lbuf, "%lf %lf %lf %" SCNu64 "/%" SCNu64 " %" SCNu64,
		    &mi->loadavg.load1,
		    &mi->loadavg.load5,
		    &mi->loadavg.load15,
		    &mi->loadavg.runnable,
		    &mi->loadavg.scheduling_entities,
		    &mi->loadavg.new_pid);
	if (rc != 6) {
		ovis_log(mi->log, OVIS_LERROR,
			 "Error scanning '%s' line '%s'.\n",
			 PROC_LOADAVG, mi->lbuf);
		return EINVAL;
	}
	write_scalars(mi, SRC_LOADAVG);
	return 0;
}

static int stat_handler(curated_metrics_t mi, int prime)
{
	FILE *f;
	int rc = 0, n;
	int have_list = mi->list_mid[SRC_STAT] >= 0;
	ldms_mval_t lh = NULL;
	uint64_t intr = 0, ctxt = 0, forks = 0;
	int got_intr = 0, got_ctxt = 0, got_fork = 0;
	char name[IFNAMSIZ];
	char *line;

	if (!mi->sample_src[SRC_STAT])
		return 0;
	f = src_file(mi, SRC_STAT, &rc);
	if (!f)
		return rc;
	if (have_list) {
		lh = ldms_metric_get(mi->base->set, mi->list_mid[SRC_STAT]);
		mi->hint[SRC_STAT] = TAILQ_FIRST(&mi->ents[SRC_STAT]);
	}

	while (getline(&mi->lbuf, &mi->lbuf_sz, f) > 0) {
		line = mi->lbuf;
		if (0 == strncmp(line, "cpu", 3)) {
			cpu_t cur = { 0 }, *prev;
			cm_ent_t e;

			if (!have_list)
				continue;
			n = sscanf(line, "%15s %" SCNu64 " %" SCNu64 " %" SCNu64
				   " %" SCNu64 " %" SCNu64 " %" SCNu64
				   " %" SCNu64 " %" SCNu64 " %" SCNu64
				   " %" SCNu64,
				   name, &cur.user, &cur.nice, &cur.sys,
				   &cur.idle, &cur.iowait, &cur.irq,
				   &cur.softirq, &cur.steal, &cur.guest,
				   &cur.guest_nice);
			if (n < 5) {
				ovis_log(mi->log, OVIS_LERROR,
					 "Bad cpu line in %s: '%s'\n",
					 PROC_STAT, line);
				rc = EINVAL;
				continue;
			}
			/* guest/guest_nice are already included in user/nice */
			cur.cpu_sum = cur.user + cur.nice + cur.sys + cur.idle
				+ cur.iowait + cur.irq + cur.softirq + cur.steal;
			e = ent_lookup(mi, SRC_STAT, name);
			if (!e) {
				rc = ENOMEM;
				break;
			}
			prev = &e->prev.cpu;
			if (!prime && e->primed) {
				cpu_t *d = &mi->stat.cpu_data;
				d->user = dlt(cur.user, prev->user);
				d->nice = dlt(cur.nice, prev->nice);
				d->sys = dlt(cur.sys, prev->sys);
				d->idle = dlt(cur.idle, prev->idle);
				d->iowait = dlt(cur.iowait, prev->iowait);
				d->irq = dlt(cur.irq, prev->irq);
				d->softirq = dlt(cur.softirq, prev->softirq);
				d->steal = dlt(cur.steal, prev->steal);
				d->guest = dlt(cur.guest, prev->guest);
				d->guest_nice = dlt(cur.guest_nice, prev->guest_nice);
				d->cpu_sum = d->user + d->nice + d->sys + d->idle
					+ d->iowait + d->irq + d->softirq + d->steal;
				write_record(mi, SRC_STAT, lh, e);
			}
			*prev = cur;
			e->primed = 1;
		} else if (0 == strncmp(line, "intr ", 5)) {
			got_intr = (1 == sscanf(line + 5, "%" SCNu64, &intr));
		} else if (0 == strncmp(line, "ctxt ", 5)) {
			got_ctxt = (1 == sscanf(line + 5, "%" SCNu64, &ctxt));
		} else if (0 == strncmp(line, "processes ", 10)) {
			got_fork = (1 == sscanf(line + 10, "%" SCNu64, &forks));
		} else if (0 == strncmp(line, "procs_running ", 14)) {
			sscanf(line + 14, "%" SCNu64, &mi->stat.procs_running);
		} else if (0 == strncmp(line, "procs_blocked ", 14)) {
			sscanf(line + 14, "%" SCNu64, &mi->stat.procs_blocked);
		}
	}
	if (have_list && !rc)
		ent_sweep(mi, SRC_STAT, lh);
	if (!got_intr || !got_ctxt || !got_fork) {
		ovis_log(mi->log, OVIS_LERROR,
			 "intr/ctxt/processes missing from %s.\n", PROC_STAT);
		rc = ENODATA;
	}

	if (!prime) {
		mi->stat.intr = got_intr ? dlt(intr, mi->prev_intr) : 0;
		mi->stat.ctxt_sw = got_ctxt ? dlt(ctxt, mi->prev_ctxt) : 0;
		mi->stat.fork = got_fork ? dlt(forks, mi->prev_fork) : 0;
		write_scalars(mi, SRC_STAT);
	}
	if (got_intr)
		mi->prev_intr = intr;
	if (got_ctxt)
		mi->prev_ctxt = ctxt;
	if (got_fork)
		mi->prev_fork = forks;
	return rc;
}

static int netdev_handler(curated_metrics_t mi, int prime)
{
	FILE *f;
	int rc = 0, n;
	ldms_mval_t lh;
	char name[IFNAMSIZ];
	char *line, *colon;
	netdev_t cur;
	cm_ent_t e;

	if (!mi->sample_src[SRC_NETDEV])
		return 0;
	f = src_file(mi, SRC_NETDEV, &rc);
	if (!f)
		return rc;
	lh = ldms_metric_get(mi->base->set, mi->list_mid[SRC_NETDEV]);
	mi->hint[SRC_NETDEV] = TAILQ_FIRST(&mi->ents[SRC_NETDEV]);

	while (getline(&mi->lbuf, &mi->lbuf_sz, f) > 0) {
		line = mi->lbuf;
		/* Skips the two header lines too: they have no ':' */
		colon = strchr(line, ':');
		if (!colon)
			continue;
		*colon = ' ';
		n = sscanf(line, " %15s %" SCNu64 " %" SCNu64 " %" SCNu64
			   " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
			   " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
			   " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
			   " %" SCNu64,
			   name,
			   &cur.rx_bytes, &cur.rx_packets, &cur.rx_errs,
			   &cur.rx_drop, &cur.rx_fifo, &cur.rx_frame,
			   &cur.rx_cmprsd, &cur.rx_multicast,
			   &cur.tx_bytes, &cur.tx_packets, &cur.tx_errs,
			   &cur.tx_drop, &cur.tx_fifo, &cur.tx_colls,
			   &cur.tx_carrier, &cur.tx_cmprsd);
		if (n != 17) {
			ovis_log(mi->log, OVIS_LDEBUG,
				 "wrong number of fields in %s line '%s'\n",
				 NET_DEV, line);
			continue;
		}
		if (!iface_wanted(mi, name))
			continue;
		e = ent_lookup(mi, SRC_NETDEV, name);
		if (!e) {
			rc = ENOMEM;
			break;
		}
		if (!prime && e->primed) {
			netdev_t *p = &e->prev.net;
			mi->net.rx_bytes = dlt(cur.rx_bytes, p->rx_bytes);
			mi->net.rx_packets = dlt(cur.rx_packets, p->rx_packets);
			mi->net.rx_errs = dlt(cur.rx_errs, p->rx_errs);
			mi->net.rx_drop = dlt(cur.rx_drop, p->rx_drop);
			mi->net.tx_bytes = dlt(cur.tx_bytes, p->tx_bytes);
			mi->net.tx_packets = dlt(cur.tx_packets, p->tx_packets);
			mi->net.tx_errs = dlt(cur.tx_errs, p->tx_errs);
			write_record(mi, SRC_NETDEV, lh, e);
		}
		e->prev.net = cur;
		e->primed = 1;
	}
	if (!rc)
		ent_sweep(mi, SRC_NETDEV, lh);
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
	curated_metrics_t mi = ldmsd_plug_ctxt_get(handle);
	struct timespec now;
	int rc, first_rc = 0, prime;

	if (!mi->base || !mi->base->set)
		return EINVAL;

	clock_gettime(CLOCK_MONOTONIC, &now);
	prime = mi->skip_first;
	if (!prime) {
		mi->dtime = (double)(now.tv_sec - mi->prev_ts.tv_sec)
			+ (double)(now.tv_nsec - mi->prev_ts.tv_nsec) / 1e9;
		base_sample_begin(mi->base);
	}
	mi->gen++;

	rc = minfo_handler(mi, prime);
	if (rc) {
		ovis_log(mi->log, OVIS_LERROR, "Error %d in meminfo handler\n", rc);
		first_rc = first_rc ? first_rc : rc;
	}
	rc = loadavg_handler(mi, prime);
	if (rc) {
		ovis_log(mi->log, OVIS_LERROR, "Error %d in loadavg handler\n", rc);
		first_rc = first_rc ? first_rc : rc;
	}
	rc = stat_handler(mi, prime);
	if (rc) {
		ovis_log(mi->log, OVIS_LERROR, "Error %d in cpu handler\n", rc);
		first_rc = first_rc ? first_rc : rc;
	}
	rc = netdev_handler(mi, prime);
	if (rc) {
		ovis_log(mi->log, OVIS_LERROR, "Error %d in netdev handler\n", rc);
		first_rc = first_rc ? first_rc : rc;
	}

	if (!prime)
		base_sample_end(mi->base);	/* always close the transaction */
	mi->skip_first = 0;
	mi->prev_ts = now;
	return first_rc;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	int src;
	curated_metrics_t mi = calloc(1, sizeof(*mi));
	if (!mi)
		return ENOMEM;
	ldmsd_plug_ctxt_set(handle, mi);
	for (src = 0; src < SRC_COUNT; src++) {
		TAILQ_INIT(&mi->ents[src]);
		mi->list_mid[src] = -1;
		mi->rec_idx[src] = -1;
		mi->name_id[src] = -1;
	}
	mi->log = ldmsd_plug_log_get(handle);
	/* The first sample only primes the delta counters */
	mi->skip_first = 1;
	return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	curated_metrics_t mi = ldmsd_plug_ctxt_get(handle);
	if (!mi)
		return;
	if (mi->base) {
		base_set_delete(mi->base);
		base_del(mi->base);
		mi->base = NULL;
	}
	reset_selection(mi);
	free(mi->lbuf);
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
