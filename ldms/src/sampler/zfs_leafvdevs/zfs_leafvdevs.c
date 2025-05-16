/* -*- c-basic-offset: 8 -*- */
/* Copyright 2022 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

#define _GNU_SOURCE
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

#define SAMP "zfs_leafvdevs"

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof(a) / sizeof(*a))
#endif

/* Function prototypes */

static int get_leaf_stats(zpool_handle_t *, void *);
static int vdevs_count(zpool_handle_t *, void *);
typedef int (*stat_printer_f) (nvlist_t *, const char *, zpool_handle_t *);

static base_data_t sampler_base;
static libzfs_handle_t *g_zfs;

static struct {
	int vdev_list_idx;
	int vdev_rec_idx;
} index_store;

static ldms_mval_t list_handle;

/* metric templates for a virtual device */
static struct ldms_metric_template_s zfs_leafvdevs[] = {
	{"zpoolname", 0, LDMS_V_CHAR_ARRAY, "", ZFS_MAX_DATASET_NAME_LEN},
	{"state", 0, LDMS_V_CHAR_ARRAY, "", ZFS_MAX_DATASET_NAME_LEN},
	{"vdevname", 0, LDMS_V_CHAR_ARRAY, "", ZFS_MAX_DATASET_NAME_LEN},
	{"vdevpath", 0, LDMS_V_CHAR_ARRAY, "", ZFS_MAX_DATASET_NAME_LEN},
	{"vdevguid", 0, LDMS_V_CHAR_ARRAY, "", ZFS_MAX_DATASET_NAME_LEN},
	{"alloc", 0, LDMS_V_U64, "", 1},
	{"free", 0, LDMS_V_U64, "", 1},
	{"size", 0, LDMS_V_U64, "", 1},
	{"read_bytes", 0, LDMS_V_U64, "", 1},
	{"read_errors", 0, LDMS_V_U64, "", 1},
	{"read_ops", 0, LDMS_V_U64, "", 1},
	{"write_bytes", 0, LDMS_V_U64, "", 1},
	{"write_errors", 0, LDMS_V_U64, "", 1},
	{"write_ops", 0, LDMS_V_U64, "", 1},
	{"checksum_errors", 0, LDMS_V_U64, "", 1},
	{"fragmentation", 0, LDMS_V_U64, "", 1},
	{"init_errors", 0, LDMS_V_U64, "", 1},
	{"trim_errors", 0, LDMS_V_U64, "", 1},
	{"slow_ios", 0, LDMS_V_U64, "", 1},
	{0},
};

#define VDEV_METRICS_LEN (ARRAY_LEN(zfs_leafvdevs) - 1)
static int vdev_metric_ids[VDEV_METRICS_LEN];
static size_t zpool_vdev_heap_sz;
static uint_t leaf_vdev_count;

static ovis_log_t mylog;


/*****************************************************************************
 * Initialize the structure as schema and add them to the base schema.
 * Also calculate the size of memory needed per schema and add it to the ldms
 * schema list.
 ****************************************************************************/

static int initialize_ldms_structs()
{
	ldms_record_t zpool_vdev_def;	/* a pointer */
	int rc;

	ovis_log(mylog, OVIS_LDEBUG, SAMP " initialize()\n");

	/* Create the schema */
	base_schema_new(sampler_base);
	if (sampler_base->schema == NULL)
		goto err1;

	/* create the vdev record */
	zpool_vdev_def = ldms_record_from_template("zfs_leafvdevs_stats",
						   zfs_leafvdevs,
						   vdev_metric_ids);
	if (zpool_vdev_def == NULL)
		goto err2;

	zpool_vdev_heap_sz = ldms_record_heap_size_get(zpool_vdev_def);
	rc = ldms_schema_record_add(sampler_base->schema, zpool_vdev_def);
	if (rc < 0)
		goto err3;

	index_store.vdev_rec_idx = rc;
	leaf_vdev_count = 0;
	rc = zpool_iter(g_zfs, vdevs_count, NULL);
	/* add error for iter in case here */
	ovis_log(mylog, OVIS_LDEBUG, SAMP " leaf_vdev_count : %d\n", leaf_vdev_count);
	rc = ldms_schema_metric_list_add(sampler_base->schema,
					 "zpool_vdev_list",
					 NULL,
					 leaf_vdev_count * zpool_vdev_heap_sz);
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

	sampler_base = base_config(avl, ldmsd_plug_cfg_name_get(handle), "zfs_leafvdevs", mylog);
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
	new_heap_size += zpool_vdev_heap_sz;

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

	rc = zpool_iter(g_zfs, get_leaf_stats, NULL);
	if (rc != 0) {
		ovis_log(mylog, OVIS_LERROR,
		       SAMP " sample():zfs_pool print_stat() failed: %d\n", rc);
	}

	base_sample_end(sampler_base);

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

/*********** let's count the vdev here *************/
static int get_leaf_vdevs_count(nvlist_t * nvroot, const char *pool_name)
{
	uint_t children;
	nvlist_t **child;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
				       &child, &children) == 0) {
		for (int c = 0; c < children; c++) {
			get_leaf_vdevs_count(child[c], pool_name);
		}
	} else {
		leaf_vdev_count++;
	}
	return (0);
}

static int vdevs_count(zpool_handle_t * zhp, void *data)
{
	int rc = 0;
	nvlist_t *config, *nvroot;
	char *pool_name;

	if ((config = zpool_get_config(zhp, NULL)) != NULL) {
		if ((rc =
		     nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
					  &nvroot)) != 0) {
			zpool_close(zhp);
			return (rc);
		}

		pool_name = (char *)zpool_get_name(zhp);
		rc = get_leaf_vdevs_count(nvroot, pool_name);
	} else {
		ovis_log(mylog, OVIS_LERROR,
		       SAMP " zpool get config failed in vdevs_count\n");
	}
	zpool_close(zhp);
	return (rc);
}

/********************* Done counting vdevs *****************************/

/*
 * vdev summary stats are a combination of the data shown by
 *  zpool status` and `zpool list -v
 *  zpoolname
 *  state
 *  vdevname
 *  alloc
 *  free
 *  size
 *  read_bytes
 *  read_errors
 *  read_ops
 *  write_bytes
 *  write_errors
 *  write_ops
 *  checksum_errors
 *  fragmentation
 *  init_errors
 */
static int get_leafvdev_stats(nvlist_t * nvroot, const char *pool_name,
			      zpool_handle_t * zhp)
{
	uint_t c;
	vdev_stat_t *vs;
	char *vdev_name = NULL;
	char *vdev_path = NULL;
	char *vdev_guid = NULL;
	ldms_mval_t record_instance;
	int rc = 0;		/*return code */

	vdev_name = zpool_vdev_name(g_zfs, zhp, nvroot, 0);
	vdev_path =
	    zpool_vdev_name(g_zfs, zhp, nvroot,
			    VDEV_NAME_PATH | VDEV_NAME_FOLLOW_LINKS);
	vdev_guid = zpool_vdev_name(g_zfs, zhp, nvroot, VDEV_NAME_GUID);

	if (nvlist_lookup_uint64_array(nvroot,
				       ZPOOL_CONFIG_VDEV_STATS,
				       (uint64_t **) & vs, &c) != 0) {
		rc = 1;
	}

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

	if (rc == 0) {
		rc = ldms_list_append_record(sampler_base->set, list_handle,
					     record_instance);

		/* zpoolname    0 */
		ldms_record_array_set_str(record_instance, vdev_metric_ids[0],
					  pool_name);
		/* zpool state  1 */
		ldms_record_array_set_str(record_instance, vdev_metric_ids[1],
					  zpool_state_to_name((vdev_state_t)
							      vs->vs_state,
							      (vdev_aux_t)
							      vs->vs_aux));
		/* vdevname     2 */
		ldms_record_array_set_str(record_instance, vdev_metric_ids[2],
					  vdev_name);
		/* vdevpath     3 */
		ldms_record_array_set_str(record_instance, vdev_metric_ids[3],
					  vdev_path);
		/* vdevguid     4 */
		ldms_record_array_set_str(record_instance, vdev_metric_ids[4],
					  vdev_guid);
		/* alloc        5 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[5],
				    vs->vs_alloc);
		/* free         6 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[6],
				    vs->vs_space - vs->vs_alloc);
		/* size         7 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[7],
				    vs->vs_space);
		/* read_bytes   8 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[8],
				    vs->vs_bytes[ZIO_TYPE_READ]);
		/* iread_errors 9 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[9],
				    vs->vs_read_errors);
		/* read_ops     10 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[10],
				    vs->vs_ops[ZIO_TYPE_READ]);
		/* write_bytes  11 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[11],
				    vs->vs_bytes[ZIO_TYPE_WRITE]);
		/* write_errors 12 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[12],
				    vs->vs_write_errors);
		/* write_ops    13 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[13],
				    vs->vs_ops[ZIO_TYPE_WRITE]);
		/* checksum errors 14 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[14],
				    vs->vs_checksum_errors);
		/* fragmentation 15 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[15],
				    vs->vs_fragmentation);
		/* initialization errors 16 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[16],
				    vs->vs_initialize_errors);
		/* trim errors 17 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[17],
				    vs->vs_trim_errors);
		/* slow ios 18 */
		ldms_record_set_u64(record_instance, vdev_metric_ids[18],
				    vs->vs_slow_ios);

	}

	free(vdev_name);
	free(vdev_path);
	free(vdev_guid);

	return (rc);
}

/*
 * recursive stats printer
 */
static int get_recursive_stats(stat_printer_f func, nvlist_t * nvroot,
			       const char *pool_name, zpool_handle_t * zhp)
{
	uint_t c, children;
	nvlist_t **child;
	int err;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
				       &child, &children) == 0) {
		for (c = 0; c < children; c++) {
			get_recursive_stats(func, child[c], pool_name, zhp);
		}
	} else {
		err = func(nvroot, pool_name, zhp);
		if (err)
			return (err);
	}
	return (0);
}

/*
 * call-back to print the stats from the pool config
 *
 * Note: if the pool is broken, this can hang indefinitely and perhaps in an
 * unkillable state.
 */

static int get_leaf_stats(zpool_handle_t * zhp, void *data)
{
	int err = 0;
	nvlist_t *config, *nvroot;
	char *pool_name;

	if ((config = zpool_get_config(zhp, NULL)) != NULL) {
		if ((err =
		     nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
					  &nvroot)) != 0)
			zpool_close(zhp);
		if (err == 0) {
			pool_name = (char *)zpool_get_name(zhp);
			err = get_recursive_stats(get_leafvdev_stats, nvroot,
						  pool_name, zhp);
		}
	} else {
		ovis_log(mylog, OVIS_LERROR,
		       SAMP " zpool get config failed in get_leaf_stats\n");
		err = 1;
	}
	zpool_close(zhp);
	return (err);
}
