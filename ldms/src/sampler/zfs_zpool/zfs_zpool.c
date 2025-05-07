/* -*- c-basic-offset: 8 -*- */
/* Copyright 2022 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

#define _GNU_SOURCE
/* Next we include the headers to bring in the zfslib in action */
#include <stdlib.h>
#include <ctype.h>
#include <glob.h>
#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <libzfs.h>
#include <libzutil.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "config.h"
#include "sampler_base.h"
#include "libzfs.h"
#include <stddef.h>

#define SAMP "zfs_zpool"

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof(a) / sizeof(*a))
#endif

/* Function prototypes */
static int get_zpool_stats(zpool_handle_t * zhp, void *data);
static int get_zpool_count(zpool_handle_t * zhp, void *data);
static int get_pool_scan_status(nvlist_t * nvroot, const char *pool_name,
				ldms_mval_t * record_instance);

#define POOL_MEASUREMENT         "zpool_stats"
#define SCAN_MEASUREMENT         "zpool_scan_stats"
#define VDEV_MEASUREMENT         "zpool_vdev_stats"
#define POOL_LATENCY_MEASUREMENT "zpool_latency"
#define POOL_QUEUE_MEASUREMENT   "zpool_vdev_queue"
#define MIN_LAT_INDEX   10	/* minimum latency index 10 = 1024ns */
#define POOL_IO_SIZE_MEASUREMENT        "zpool_io_size"
#define MIN_SIZE_INDEX  9	/* minimum size index 9 = 512 bytes */

typedef int (*stat_printer_f) (nvlist_t *, const char *, const char *);

int complained_about_sync = 0;

static base_data_t sampler_base;
static libzfs_handle_t *g_zfs;

static struct {
	int vdev_list_idx;
	int vdev_rec_idx;
} index_store;

static ldms_mval_t list_handle;

typedef enum op_func_type {
	FUNC_NOFUNCREQ = 0,
	FUNC_SCRUB = 1,
	FUNC_RESILVER = 2,
	FUNC_REBUILD = 3,
	FUNC_SCAN = 4
} op_func_type_;

static const char *const operation_types[] = {
	[FUNC_NOFUNCREQ] = "none",
	[FUNC_SCRUB] = "scrub",
	[FUNC_RESILVER] = "resilver",
	[FUNC_REBUILD] = "rebuild",
	[FUNC_SCAN] = "scan"
};

/* metric templates for a zpool and scan status if any */
static struct ldms_metric_template_s zfs_zpool[] = {
	{"pool", 0, LDMS_V_CHAR_ARRAY, "", ZFS_MAX_DATASET_NAME_LEN},
	{"state", 0, LDMS_V_CHAR_ARRAY, "", ZFS_MAX_DATASET_NAME_LEN},
	{"total", 0, LDMS_V_U64, "", 1},
	{"allocated", 0, LDMS_V_U64, "", 1},
	{"free", 0, LDMS_V_U64, "", 1},
	{"used", 0, LDMS_V_U64, "", 1},
	{"scan_func", 0, LDMS_V_CHAR_ARRAY, "", ZFS_MAX_DATASET_NAME_LEN},
	{"scan_status", 0, LDMS_V_CHAR_ARRAY, "", ZFS_MAX_DATASET_NAME_LEN},
	{"scan_repaired", 0, LDMS_V_U64, "", 1},
	{"scan_completed_in", 0, LDMS_V_U64, "", 1},
	{"scan_errors", 0, LDMS_V_U32, "", 1},
	{"scan_completed_on", 0, LDMS_V_U64, "", 1},
	{0}
};

#define ZPOOL_METRICS_LEN (ARRAY_LEN(zfs_zpool) - 1)
static int zpool_metric_ids[ZPOOL_METRICS_LEN];
static size_t zpool_heap_sz;

static ovis_log_t mylog;

static int zpool_list_len = 0;	/* Aggregated number of vdev per zpool */

/*****************************************************************************
 * Initialize the structure as schema and add them to the base schema.
 * Also calculate the size of memory needed per schema and add it to the ldms
 * schema list.
 ****************************************************************************/

static int initialize_ldms_structs()
{
	/*ldms_record_t zpool_def;  a pointer */
	ldms_record_t zpool_vdev_def;	/* a pointer */
	int rc;

	ovis_log(mylog, OVIS_LDEBUG, SAMP " initialize()\n");

	/* Create the schema */
	base_schema_new(sampler_base);
	if (sampler_base->schema == NULL)
		goto err1;

	/* create the vdev record */
	zpool_vdev_def = ldms_record_from_template("zfs_zpool_stats",
						   zfs_zpool, zpool_metric_ids);
	if (zpool_vdev_def == NULL)
		goto err2;

	zpool_heap_sz = ldms_record_heap_size_get(zpool_vdev_def);
	rc = ldms_schema_record_add(sampler_base->schema, zpool_vdev_def);
	if (rc < 0)
		goto err3;

	index_store.vdev_rec_idx = rc;
	rc = zpool_iter(g_zfs, get_zpool_count, NULL);
	/* add error for iter in case here */
	rc = ldms_schema_metric_list_add(sampler_base->schema,
					 "zpool_list",
					 NULL, zpool_list_len * zpool_heap_sz);
	if (rc < 0)
		goto err2;

	index_store.vdev_list_idx = rc;

	/* Create the metric set */
	base_set_new(sampler_base);
	if (sampler_base->set == NULL)
		goto err2;

	return 0;

 err3:
	/* We only manually delete record template when it
	 * hasn't been added to the schema yet */
	ldms_record_delete(zpool_vdev_def);
 err2:
	base_schema_delete(sampler_base);
 err1:
	ovis_log(mylog, OVIS_LERROR, SAMP " initialization failed\n");
	return -1;
}

/*****************************************************************************
 * WHAT:
 * 1) Initialize the sampler base schema.
 * 2) Initialize all structure and memory.
 * 3) initialize the zfslib to sample the zpools stats.
 * CALLER:
 * ldms daemon. In error the plugin is aborted.
 ****************************************************************************/

static int config(ldmsd_plug_handle_t handle,
		  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc = 0;

	ovis_log(mylog, OVIS_LDEBUG, SAMP " config() called\n");

	sampler_base = base_config(avl, ldmsd_plug_cfg_name_get(handle), "zfs_zpool", mylog);
	if ((g_zfs = libzfs_init()) == NULL) {
		rc = errno;
		ovis_log(mylog, OVIS_LERROR,
			  SAMP " : Failed to initialize libzfs: %d\n", errno);
		ovis_log(mylog, OVIS_LERROR,
			  SAMP
			  " : Is the zfs module loaded or zrepl running?\n");
	} else {
		rc = initialize_ldms_structs();
	}

	if (rc < 0) {
		base_del(sampler_base);
		sampler_base = NULL;
	}

	return rc;
}

/*****************************************************************************
 * WHAT:
 * reallocate heap size plus 1 zpool struct and one vdev struct
 * CALLER:
 * self, (plugin)
 ****************************************************************************/
static int resize_metric_set()
{
	size_t previous_heap_size;
	size_t new_heap_size;
	int rc = 0;

	previous_heap_size = ldms_set_heap_size_get(sampler_base->set);
	base_set_delete(sampler_base);

	new_heap_size = previous_heap_size;
	new_heap_size += zpool_heap_sz;

	if (base_set_new_heap(sampler_base, new_heap_size) == NULL) {
		rc = errno;
		ovis_log(mylog, OVIS_LERROR,
			  SAMP " : Failed to resize metric set heap: %d\n",
			  errno);
	} else {
		ovis_log(mylog, OVIS_LDEBUG, "ldms resize of list successful\n");
	}
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
	int rc = 0;

	base_sample_begin(sampler_base);

	list_handle =
	    ldms_metric_get(sampler_base->set, index_store.vdev_list_idx);
	ldms_list_purge(sampler_base->set, list_handle);

	rc = zpool_iter(g_zfs, get_zpool_stats, NULL);
	if (rc != 0) {
		ovis_log(mylog, OVIS_LERROR,
		       SAMP " sample():zfs_pool print_stat() failed: %d\n", rc);
		base_sample_end(sampler_base);
		goto err1;
	}
	/* this is where the rubber meets the pavement */

	base_sample_end(sampler_base);

 err1:
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	ovis_log(mylog, OVIS_LDEBUG, SAMP " usage() called\n");
	return "config name=" SAMP " " BASE_CONFIG_SYNOPSIS BASE_CONFIG_DESC;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	ovis_log(mylog, OVIS_LDEBUG, SAMP " term() called\n");
	base_set_delete(sampler_base);
	base_del(sampler_base);
	sampler_base = NULL;
}

struct ldmsd_sampler ldmsd_plugin_interface = {
        .base = {
                .type = LDMSD_PLUGIN_SAMPLER,
                .config = config,
                .usage = usage,
		.constructor = constructor,
		.destructor = destructor,
        },
        .sample = sample,
};

/*
 * top-level vdev stats are at the pool level moving to its own plugin
 */

static int get_detailed_pool_stats(nvlist_t * nvroot, const char *pool_name)
{
	nvlist_t *nv_ex;
	uint64_t cap;
	ldms_mval_t record_instance;
	uint_t c;
	vdev_stat_t *vs;
	int rc = 0;

	if (nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
				       (uint64_t **) & vs, &c) != 0) {
		return (1);
	}

	if (nvlist_lookup_nvlist(nvroot,
				 ZPOOL_CONFIG_VDEV_STATS_EX, &nv_ex) != 0) {
		rc = 6;
	}

	if (rc == 0) {
		record_instance = ldms_record_alloc(sampler_base->set,
						    index_store.vdev_rec_idx);

		if (record_instance == NULL) {
			ovis_log(mylog, OVIS_LDEBUG,
			       SAMP
			       ": ldms_record_alloc() failed, resizing metric set\n");
			resize_metric_set();
			record_instance = ldms_record_alloc(sampler_base->set,
							    index_store.vdev_rec_idx);
			if (record_instance == NULL)
				rc = 2;
		}
	}

	if (rc == 0) {
		rc = ldms_list_append_record(sampler_base->set, list_handle,
					     record_instance);

		/* zpoolname    0 */
		ldms_record_array_set_str(record_instance, zpool_metric_ids[0],
					  pool_name);
		/* zpool state  1 */
		ldms_record_array_set_str(record_instance, zpool_metric_ids[1],
					  zpool_state_to_name((vdev_state_t)
							      vs->vs_state,
							      (vdev_aux_t)
							      vs->vs_aux));
		/* total     2 */
		ldms_record_set_u64(record_instance, zpool_metric_ids[2],
				    vs->vs_space);
		/* allocated 3 */
		ldms_record_set_u64(record_instance, zpool_metric_ids[3],
				    vs->vs_alloc);
		/* free     4 */
		ldms_record_set_u64(record_instance, zpool_metric_ids[4],
				    vs->vs_space - vs->vs_alloc);
		/* used     5 */
		cap = (vs->vs_space == 0) ? 0 :
		    (vs->vs_alloc * 100 / vs->vs_space);
		ldms_record_set_u64(record_instance, zpool_metric_ids[5], cap);

		/* Here we call the get_pool_scan function to fill the rest of the
		 * record */
		rc = get_pool_scan_status(nvroot, pool_name, &record_instance);

	}
	return (0);
}

static int get_zpool_count(zpool_handle_t * zhp, void *data)
{
	int rc = 0;

	if (zhp != NULL) {
		zpool_list_len++;
		zpool_close(zhp);
	} else {
		rc = 1;
	}
	return (rc);
}

static int get_pool_scan_status(nvlist_t * nvroot, const char *pool_name,
				ldms_mval_t * record_instance)
{
	uint_t c;
	int64_t elapsed;
	uint64_t pass_exam, paused_time, rate;
	pool_scan_stat_t *ps = NULL;
	char *state[DSS_NUM_STATES] = {
		"NONE",
		"scanning",
		"finished",
		"canceled"
	};
	/* operation_types func; */
	const char *func;

	(void)nvlist_lookup_uint64_array(nvroot,
					 ZPOOL_CONFIG_SCAN_STATS,
					 (uint64_t **) & ps, &c);

	/*
	 * ignore if there are no stats
	 */
	if (ps == NULL)
		return (0);

	/*
	 * return error if state is bogus
	 */
	if (ps->pss_state >= DSS_NUM_STATES || ps->pss_func >= POOL_SCAN_FUNCS) {
		if (complained_about_sync % 1000 == 0) {
			fprintf(stderr, "error: cannot decode scan stats: "
				"ZFS is out of sync with compiled zfs_zpool (ldms)");
			complained_about_sync++;
		}
		return (1);
	}

	switch (ps->pss_func) {

	case POOL_SCAN_NONE:
		func = operation_types[FUNC_NOFUNCREQ];
		break;
	case POOL_SCAN_SCRUB:
		func = operation_types[FUNC_SCRUB];
		break;
	case POOL_SCAN_RESILVER:
		func = operation_types[FUNC_RESILVER];
		break;
	default:
		func = operation_types[FUNC_SCAN];
	}

	paused_time = ps->pss_pass_scrub_spent_paused;

	/* calculations for this pass */
	if (ps->pss_state == DSS_SCANNING) {
		elapsed = (int64_t) time(NULL) - (int64_t) ps->pss_pass_start -
		    (int64_t) paused_time;
		elapsed = (elapsed > 0) ? elapsed : 1;
		pass_exam = ps->pss_pass_exam ? ps->pss_pass_exam : 1;
		rate = pass_exam / elapsed;
		rate = (rate > 0) ? rate : 1;
	} else {
		elapsed =
		    (int64_t) ps->pss_end_time - (int64_t) ps->pss_pass_start -
		    (int64_t) paused_time;
		elapsed = (elapsed > 0) ? elapsed : 1;
		pass_exam = ps->pss_pass_exam ? ps->pss_pass_exam : 1;
		rate = pass_exam / elapsed;
	}
	rate = rate ? rate : 1;

	/* scan_func         6 */
	ldms_record_array_set_str(*record_instance, zpool_metric_ids[6], func);
	/* scan_status       7 */
	ldms_record_array_set_str(*record_instance, zpool_metric_ids[7],
				  state[ps->pss_state]);
	/* scan_repaired     8 */
	ldms_record_set_u64(*record_instance, zpool_metric_ids[8],
			    ps->pss_processed);
	/* scan_completed_in 9 */
	ldms_record_set_u64(*record_instance, zpool_metric_ids[9],
			    ps->pss_end_time - ps->pss_start_time);
	/* scan_errors       10 */
	ldms_record_set_u64(*record_instance, zpool_metric_ids[10],
			    ps->pss_errors);
	/* scan_completed_on 11 */
	ldms_record_set_u64(*record_instance, zpool_metric_ids[11],
			    ps->pss_end_time);

	return (0);
}

/*
 * call-back to print the stats from the pool config
 *
 * Note: if the pool is broken, this can hang indefinitely and perhaps in an
 * unkillable state.
 */

static int get_zpool_stats(zpool_handle_t * zhp, void *data)
{
	uint_t c;
	int err = 0;
	nvlist_t *config, *nvroot;
	vdev_stat_t *vs;
	char *pool_name;

	if ((config = zpool_get_config(zhp, NULL)) != NULL) {
		if (nvlist_lookup_nvlist
		    (config, ZPOOL_CONFIG_VDEV_TREE, &nvroot) != 0) {
			err = 2;
			goto terminate;
		}
		if (nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
					       (uint64_t **) & vs, &c) != 0) {
			err = 3;
			goto terminate;
		}

		pool_name = (char *)zpool_get_name(zhp);

		err = get_detailed_pool_stats(nvroot, pool_name);
	}

 terminate:
	zpool_close(zhp);
	return (err);
}
