/*
 * Gather top-level ZFS pool and resilver/scan statistics and print using
 * influxdb line protocol
 * usage: [options] [pool_name]
 * where options are:
 *   --execd, -e           run in telegraf execd input plugin mode, [CR] on
 *                         stdin causes a sample to be printed and wait for
 *                         the next [CR]
 *   --no-histograms, -n   don't print histogram data (reduces cardinality
 *                         if you don't care about histograms)
 *   --sum-histogram-buckets, -s sum histogram bucket values
 *
 * To integrate into telegraf use one of:
 * 1. the `inputs.execd` plugin with the `--execd` option
 * 2. the `inputs.exec` plugin to simply run with no options
 *
 * NOTE: libzfs is an unstable interface. YMMV.
 *
 * The design goals of this software include:
 * + be as lightweight as possible
 * + reduce the number of external dependencies as far as possible, hence
 *   there is no dependency on a client library for managing the metric
 *   collection -- info is printed, KISS
 * + broken pools or kernel bugs can cause this process to hang in an
 *   unkillable state. For this reason, it is best to keep the damage limited
 *   to a small process like zpool_influxdb rather than a larger collector.
 *
 * Copyright 2018-2020 Richard Elling
 *
 * This software is dual-licensed MIT and CDDL.
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License Version 1.0 (CDDL-1.0).
 * You can obtain a copy of the license from the top-level file
 * "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
 * You may not use this file except in compliance with the license.
 *
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * CDDL HEADER END
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include "zpool_zfs.h"


/* Function prototypes */

int
get_stats(zpool_handle_t *zhp, void *data);

#define	POOL_MEASUREMENT	 "zpool_stats"
#define	SCAN_MEASUREMENT	 "zpool_scan_stats"
#define	VDEV_MEASUREMENT	 "zpool_vdev_stats"
#define	POOL_LATENCY_MEASUREMENT "zpool_latency"
#define	POOL_QUEUE_MEASUREMENT	 "zpool_vdev_queue"
#define	MIN_LAT_INDEX	10  /* minimum latency index 10 = 1024ns */
#define	POOL_IO_SIZE_MEASUREMENT	"zpool_io_size"
#define	MIN_SIZE_INDEX	9  /* minimum size index 9 = 512 bytes */

/* global options */
int execd_mode = 0;
int no_histograms = 1;
int sum_histogram_buckets = 0;
char metric_data_type = 'u';
uint64_t metric_value_mask = UINT64_MAX;
uint64_t timestamp = 0;
int complained_about_sync = 0;
char *tags = "";

typedef int (*stat_printer_f)(nvlist_t *, const char *, const char *);

/*
 * influxdb line protocol rules for escaping are important because the
 * zpool name can include characters that need to be escaped
 *
 * caller is responsible for freeing result
 */
static char *
escape_string(char *s)
{
	char *c, *d;
	char *t = (char *)malloc(ZFS_MAX_DATASET_NAME_LEN * 2);
	if (t == NULL) {
		fprintf(stderr, "error: cannot allocate memory\n");
		exit(1);
	}

	for (c = s, d = t; *c != '\0'; c++, d++) {
		switch (*c) {
		case ' ':
		case ',':
		case '=':
		case '\\':
			*d++ = '\\';
			fallthrough;
		default:
			*d = *c;
		}
	}
	*d = '\0';
	return (t);
}

/*
 * print key=value where value is a uint64_t
 */
static void
print_kv(char *key, uint64_t value)
{
	printf("%s : %llu\n", key,
	    (u_longlong_t)value & metric_value_mask);
}

/*
 * print_scan_status() prints the details as often seen in the "zpool status"
 * output. However, unlike the zpool command, which is intended for humans,
 * this output is suitable for long-term tracking in influxdb.
 * TODO: update to include issued scan data
 */
static int
print_scan_status(nvlist_t *nvroot, const char *pool_name)
{
	uint_t c;
	int64_t elapsed;
	uint64_t examined, pass_exam, paused_time, paused_ts, rate;
	uint64_t remaining_time;
	pool_scan_stat_t *ps = NULL;
	double pct_done;
	char *state[DSS_NUM_STATES] = {
	    "none", "scanning", "finished", "canceled"};
	char *func;

	(void) nvlist_lookup_uint64_array(nvroot,
	    ZPOOL_CONFIG_SCAN_STATS,
	    (uint64_t **)&ps, &c);

	/*
	 * ignore if there are no stats
	 */
	if (ps == NULL)
		return (0);

	/*
	 * return error if state is bogus
	 */
	if (ps->pss_state >= DSS_NUM_STATES ||
	    ps->pss_func >= POOL_SCAN_FUNCS) {
		if (complained_about_sync % 1000 == 0) {
			fprintf(stderr, "error: cannot decode scan stats: "
			    "ZFS is out of sync with compiled zpool_influxdb");
			complained_about_sync++;
		}
		return (1);
	}

	switch (ps->pss_func) {
	case POOL_SCAN_NONE:
		func = "none_requested";
		break;
	case POOL_SCAN_SCRUB:
		func = "scrub";
		break;
	case POOL_SCAN_RESILVER:
		func = "resilver";
		break;
#ifdef POOL_SCAN_REBUILD
	case POOL_SCAN_REBUILD:
		func = "rebuild";
		break;
#endif
	default:
		func = "scan";
	}

	/* overall progress */
	examined = ps->pss_examined ? ps->pss_examined : 1;
	pct_done = 0.0;
	if (ps->pss_to_examine > 0)
		pct_done = 100.0 * examined / ps->pss_to_examine;

#ifdef EZFS_SCRUB_PAUSED
	paused_ts = ps->pss_pass_scrub_pause;
	paused_time = ps->pss_pass_scrub_spent_paused;
#else
	paused_ts = 0;
	paused_time = 0;
#endif

	/* calculations for this pass */
	if (ps->pss_state == DSS_SCANNING) {
		elapsed = (int64_t)time(NULL) - (int64_t)ps->pss_pass_start -
		    (int64_t)paused_time;
		elapsed = (elapsed > 0) ? elapsed : 1;
		pass_exam = ps->pss_pass_exam ? ps->pss_pass_exam : 1;
		rate = pass_exam / elapsed;
		rate = (rate > 0) ? rate : 1;
		remaining_time = ps->pss_to_examine - examined / rate;
	} else {
		elapsed =
		    (int64_t)ps->pss_end_time - (int64_t)ps->pss_pass_start -
		    (int64_t)paused_time;
		elapsed = (elapsed > 0) ? elapsed : 1;
		pass_exam = ps->pss_pass_exam ? ps->pss_pass_exam : 1;
		rate = pass_exam / elapsed;
		remaining_time = 0;
	}
	rate = rate ? rate : 1;

	/* influxdb line protocol format: "tags metrics timestamp" */
	printf("%s%s,function=%s,name=%s,state=%s ",
	    SCAN_MEASUREMENT, tags, func, pool_name, state[ps->pss_state]);
	print_kv("end_ts", ps->pss_end_time);
	print_kv(",errors", ps->pss_errors);
	print_kv(",examined", examined);
	print_kv(",issued", ps->pss_issued);
	print_kv(",pass_examined", pass_exam);
	print_kv(",pass_issued", ps->pss_pass_issued);
	print_kv(",paused_ts", paused_ts);
	print_kv(",paused_t", paused_time);
	printf(",pct_done=%.2f", pct_done);
	print_kv(",processed", ps->pss_processed);
	print_kv(",rate", rate);
	print_kv(",remaining_t", remaining_time);
	print_kv(",start_ts", ps->pss_start_time);
	print_kv(",to_examine", ps->pss_to_examine);
	print_kv(",to_process", ps->pss_to_process);
/*	printf(" %llu\n\n\n", (u_longlong_t)timestamp);*/
	printf(" \n\n\n");
	return (0);
}

/*
 * get a vdev name that corresponds to the top-level vdev names
 * printed by `zpool status`
 */
static char *
get_vdev_name(nvlist_t *nvroot, const char *parent_name)
{
	static char vdev_name[256];
	char *vdev_type = NULL;
	uint64_t vdev_id = 0;

	if (nvlist_lookup_string(nvroot, ZPOOL_CONFIG_TYPE,
	    &vdev_type) != 0) {
		vdev_type = "unknown";
	}
	if (nvlist_lookup_uint64(
	    nvroot, ZPOOL_CONFIG_ID, &vdev_id) != 0) {
		vdev_id = UINT64_MAX;
	}
	if (parent_name == NULL) {
		(void) snprintf(vdev_name, sizeof (vdev_name), "%s",
		    vdev_type);
	} else {
		(void) snprintf(vdev_name, sizeof (vdev_name),
		    "%s/%s-%llu",
		    parent_name, vdev_type, (u_longlong_t)vdev_id);
	}
	return (vdev_name);
}

/*
 * get a string suitable for an influxdb tag that describes this vdev
 *
 * By default only the vdev hierarchical name is shown, separated by '/'
 * If the vdev has an associated path, which is typical of leaf vdevs,
 * then the path is added.
 * It would be nice to have the devid instead of the path, but under
 * Linux we cannot be sure a devid will exist and we'd rather have
 * something than nothing, so we'll use path instead.
 */
static char *
get_vdev_desc(nvlist_t *nvroot, const char *parent_name)
{
	static char vdev_desc[2 * MAXPATHLEN];
	char *vdev_type = NULL;
	uint64_t vdev_id = 0;
	char vdev_value[MAXPATHLEN];
	char *vdev_path = NULL;
	char *s, *t;

	if (nvlist_lookup_string(nvroot, ZPOOL_CONFIG_TYPE, &vdev_type) != 0) {
		vdev_type = "unknown";
	}
	if (nvlist_lookup_uint64(nvroot, ZPOOL_CONFIG_ID, &vdev_id) != 0) {
		vdev_id = UINT64_MAX;
	}
	if (nvlist_lookup_string(
	    nvroot, ZPOOL_CONFIG_PATH, &vdev_path) != 0) {
		vdev_path = NULL;
	}

	if (parent_name == NULL) {
		s = escape_string(vdev_type);
		(void) snprintf(vdev_value, sizeof (vdev_value), "vdev=%s", s);
		free(s);
	} else {
		s = escape_string((char *)parent_name);
		t = escape_string(vdev_type);
		(void) snprintf(vdev_value, sizeof (vdev_value),
		    "vdev=%s/%s-%llu", s, t, (u_longlong_t)vdev_id);
		free(s);
		free(t);
	}
	if (vdev_path == NULL) {
		(void) snprintf(vdev_desc, sizeof (vdev_desc), "%s",
		    vdev_value);
	} else {
		s = escape_string(vdev_path);
		(void) snprintf(vdev_desc, sizeof (vdev_desc), "path=%s,%s",
		    s, vdev_value);
		free(s);
	}
	return (vdev_desc);
}

/*
 * vdev summary stats are a combination of the data shown by
 * `zpool status` and `zpool list -v`
 */
static int
print_summary_stats(nvlist_t *nvroot, const char *pool_name,
    const char *parent_name)
{
	uint_t c;
	vdev_stat_t *vs;
	char *vdev_desc = NULL;
	vdev_desc = get_vdev_desc(nvroot, parent_name);
	if (nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) != 0) {
		return (1);
	}
	printf("%s%s,name=%s,state=%s,%s\n ", POOL_MEASUREMENT, tags,
	    pool_name, zpool_state_to_name((vdev_state_t)vs->vs_state,
	    (vdev_aux_t)vs->vs_aux), vdev_desc);
	print_kv("alloc", vs->vs_alloc);
	print_kv(",free", vs->vs_space - vs->vs_alloc);
	print_kv(",size", vs->vs_space);
	print_kv(",read_bytes", vs->vs_bytes[ZIO_TYPE_READ]);
	print_kv(",read_errors", vs->vs_read_errors);
	print_kv(",read_ops", vs->vs_ops[ZIO_TYPE_READ]);
	print_kv(",write_bytes", vs->vs_bytes[ZIO_TYPE_WRITE]);
	print_kv(",write_errors", vs->vs_write_errors);
	print_kv(",write_ops", vs->vs_ops[ZIO_TYPE_WRITE]);
	print_kv(",checksum_errors", vs->vs_checksum_errors);
	print_kv(",fragmentation", vs->vs_fragmentation);
/*	printf(" %llu\n\n\n", (u_longlong_t)timestamp);*/
	printf(" \n\n\n");
	return (0);
}

/*
 * top-level vdev stats are at the pool level
 */
static int
print_top_level_vdev_stats(nvlist_t *nvroot, const char *pool_name)
{
	nvlist_t *nv_ex;
	uint64_t value;
        /* need to get for each vdev in the tree wit hthe following cb:
	 * for_each_vdev_cb((void *) zhp, nvroot, func, data)
	 */
	/* short_names become part of the metric name */
	struct queue_lookup {
	    char *name;
	    char *short_name;
	};
	struct queue_lookup queue_type[] = {
	    {ZPOOL_CONFIG_VDEV_SYNC_R_ACTIVE_QUEUE, "sync_r_active_queue"},
	    {ZPOOL_CONFIG_VDEV_SYNC_W_ACTIVE_QUEUE, "sync_w_active_queue"},
	    {ZPOOL_CONFIG_VDEV_ASYNC_R_ACTIVE_QUEUE, "async_r_active_queue"},
	    {ZPOOL_CONFIG_VDEV_ASYNC_W_ACTIVE_QUEUE, "async_w_active_queue"},
	    {ZPOOL_CONFIG_VDEV_SCRUB_ACTIVE_QUEUE, "async_scrub_active_queue"},
	    {ZPOOL_CONFIG_VDEV_SYNC_R_PEND_QUEUE, "sync_r_pend_queue"},
	    {ZPOOL_CONFIG_VDEV_SYNC_W_PEND_QUEUE, "sync_w_pend_queue"},
	    {ZPOOL_CONFIG_VDEV_ASYNC_R_PEND_QUEUE, "async_r_pend_queue"},
	    {ZPOOL_CONFIG_VDEV_ASYNC_W_PEND_QUEUE, "async_w_pend_queue"},
	    {ZPOOL_CONFIG_VDEV_SCRUB_PEND_QUEUE, "async_scrub_pend_queue"},
	    {NULL, NULL}
	};

	if (nvlist_lookup_nvlist(nvroot,
	    ZPOOL_CONFIG_VDEV_STATS_EX, &nv_ex) != 0) {
		return (6);
	}

	printf("%s%s,name=%s,vdev=root ", VDEV_MEASUREMENT, tags, pool_name);
	for (int i = 0; queue_type[i].name; i++) {
		if (nvlist_lookup_uint64(nv_ex,
					 queue_type[i].name, &value) != 0) {
			fprintf(stderr, "error: can't get %s\n",
			    queue_type[i].name);
			return (3);
		}
		if (i > 0)
			printf(",");
		else
			printf("\n");
		print_kv(queue_type[i].short_name, value);
	}

/*	printf(" %llu\n\n\n", (u_longlong_t)timestamp);*/
	printf("\n\n\n");
	return (0);
}

/*
 * recursive stats printer
 */
static int
print_recursive_stats(stat_printer_f func, nvlist_t *nvroot,
    const char *pool_name, const char *parent_name, int descend)
{
	uint_t c, children;
	nvlist_t **child;
	char vdev_name[256];
	int err;

	err = func(nvroot, pool_name, parent_name);
	if (err)
		return (err);

	if (descend && nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		(void) strlcpy(vdev_name, get_vdev_name(nvroot, parent_name),
		    sizeof (vdev_name));

		for (c = 0; c < children; c++) {
			print_recursive_stats(func, child[c], pool_name,
					      vdev_name, descend);
		}
	}
	return (0);
}

/*
 * call-back to print the stats from the pool config
 *
 * Note: if the pool is broken, this can hang indefinitely and perhaps in an
 * unkillable state.
 */

int
get_stats(zpool_handle_t *zhp, void *data)
{
	uint_t c;
	int err;
	boolean_t missing;
	nvlist_t *config, *nvroot;
	vdev_stat_t *vs;
	struct timespec tv;
	char *pool_name;

	/* if not this pool return quickly */
	if (data &&
	    /*strncmp(data, zhp->zpool_name, ZFS_MAX_DATASET_NAME_LEN) != 0) {*/
	    strncmp(data, zpool_get_name(zhp), ZFS_MAX_DATASET_NAME_LEN) != 0) {
		zpool_close(zhp);
		return (0);
	}

	if (zpool_refresh_stats(zhp, &missing) != 0) {
		zpool_close(zhp);
		return (1);
	}

	config = zpool_get_config(zhp, NULL);
	if (clock_gettime(CLOCK_REALTIME, &tv) != 0)
		timestamp = (uint64_t)time(NULL) * 1000000000;
	else
		timestamp =
		    ((uint64_t)tv.tv_sec * 1000000000) + (uint64_t)tv.tv_nsec;

	if (nvlist_lookup_nvlist(
	    config, ZPOOL_CONFIG_VDEV_TREE, &nvroot) != 0) {
	zpool_close(zhp);
		return (2);
	}
	if (nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) != 0) {
	zpool_close(zhp);
		return (3);
	}

	/*pool_name = escape_string(zhp->zpool_name);*/
	pool_name = (char *)zpool_get_name(zhp);
	err = print_recursive_stats(print_summary_stats, nvroot,
	    pool_name, NULL, 1);
	/* if any of these return an error, skip the rest */
	if (err == 0)
	err = print_top_level_vdev_stats(nvroot, pool_name);

	if (err == 0)
		err = print_scan_status(nvroot, pool_name);

	free(pool_name);
	zpool_close(zhp);
	return (err);
}
