/* -*- c-basic-offser: 8 -*- */
/* Copyright 2022 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <glob.h>
#include "ldms.h"
#include "ldmsd.h"
#include "config.h"
#include "sampler_base.h"

#include <stddef.h>

/* Next we include the headers to bring in the zfslib in action */
#include "zpool_zfs.h"

#define SAMP "zpools_stats"
#define MAX_LINE_LEN 256

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof(a) / sizeof(*a))
#endif

#ifndef IFNAMSIZ
/* from "linux/if.h" */
#define IFNAMSIZ 16
#endif

#ifndef PATH_MAX
#define PATH_MAX 2048
#endif

#define DEFAULT_ARRAY_LEN 32

#define ZPOOL_LIST_SNAME "zpool_list"
#define VDEV_LIST_SNAME  "vdev_list"

static ldmsd_msg_log_f  log_fn;
static base_data_t      sampler_base;
static libzfs_handle_t *g_zfs;

static struct {
        int zpool_list_idx;
        int vdev_list_idx;
} index_store;


struct ldms_metric_template_s zpool_metrics[] = {
    { "zpoolname", 0, LDMS_V_CHAR_ARRAY, "", MAX_LINE_LEN } ,
	{ "state"    , 0, LDMS_V_CHAR_ARRAY, "", IFNAMSIZ     } ,
	{0},
};


/* metric templates for a virtual device */
static struct ldms_metric_template_s vdev_metrics[] = {
    {"zpoolname",                0, LDMS_V_CHAR_ARRAY, "", MAX_LINE_LEN },
    {"vdevname",                 0, LDMS_V_CHAR_ARRAY, "", MAX_LINE_LEN },
    {"path",                     0, LDMS_V_CHAR_ARRAY, "",     PATH_MAX },
    {"alloc",                    0, LDMS_V_U64,        "",            1 },
    {"free",                     0, LDMS_V_U64,        "",            1 },
    {"size",                     0, LDMS_V_U64,        "",            1 },
    {"read_bytes",               0, LDMS_V_U64,        "",            1 },
    {"read_errors",              0, LDMS_V_U64,        "",            1 },
    {"read_ops",                 0, LDMS_V_U64,        "",            1 },
    {"write_bytes",              0, LDMS_V_U64,        "",            1 },
    {"write_errors",             0, LDMS_V_U64,        "",            1 },
    {"write_ops",                0, LDMS_V_U64,        "",            1 },
    {"checksum_errors",          0, LDMS_V_U64,        "",            1 },
    {"fragmentation",            0, LDMS_V_U64,        "",            1 },
    {"sync_r_active_queue",      0, LDMS_V_U64,        "",            1 },
    {"sync_w_active_queue",      0, LDMS_V_U64,        "",            1 },
    {"async_r_active_queue",     0, LDMS_V_U64,        "",            1 },
    {"async_w_active_queue",     0, LDMS_V_U64,        "",            1 },
    {"async_scrub_active_queue", 0, LDMS_V_U64,        "",            1 },
    {"sync_r_pend_queue",        0, LDMS_V_U64,        "",            1 },
    {"sync_w_pend_queue",        0, LDMS_V_U64,        "",            1 },
    {"async_r_pend_queue",       0, LDMS_V_U64,        "",            1 },
    {"async_w_pend_queue",       0, LDMS_V_U64,        "",            1 },
    {"async_scrub_pend_queue",   0, LDMS_V_U64,        "",            1 },
    {0},
};

/* need to find a better way more intuitive than that
 * to manage heap. Like auto resize as a base function */

#define VDEV_METRICS_LEN (ARRAY_LEN(vdev_metrics) - 1)
static int    vdev_metric_ids[VDEV_METRICS_LEN];
static size_t vdev_heap_sz;

#define ZPOOL_METRICS_LEN (ARRAY_LEN(zpool_metrics) - 1)
static int    zpool_metric_ids[ZPOOL_METRICS_LEN];
static size_t zpool_heap_sz;


static int zpool_list_len = 2;
static int vdev_list_len  = 2; /* Aggregated number of vdev per zpool */



/*****************************************************************************
 * Initialize the structure as schema and add them to the base schema.
 * Also calculate the size of memory needed per schema and add it to the ldms
 * schema list.
 ****************************************************************************/

static int initialize_ldms_structs()
{
        ldms_record_t zpool_def; /* a pointer */
        ldms_record_t vdev_def;  /* a pointer */
        int rc;

        log_fn(LDMSD_LDEBUG, SAMP" initialize()\n");

        /* Create the schema */
        base_schema_new(sampler_base);
        if (sampler_base->schema == NULL)
            goto err1;

        /* create the zpool record */
        zpool_def = ldms_record_from_template("zpools_stats", zpool_metrics, zpool_metric_ids);
        if (zpool_def == NULL)
            goto err2;

        /* create the vdev record */
        vdev_def  = ldms_record_from_template("vdevs_stats", vdev_metrics, vdev_metric_ids);
        if (vdev_def == NULL)
            goto err2;

        zpool_heap_sz = ldms_record_heap_size_get(zpool_def);
        rc = ldms_schema_record_add(sampler_base->schema, zpool_def);
        if (rc < 0)
            goto err3;
        index_store.zpool_list_idx = rc;

        vdev_heap_sz = ldms_record_heap_size_get(vdev_def);
        rc = ldms_schema_record_add(sampler_base->schema, vdev_def);
        if (rc < 0)
            goto err4;
        index_store.vdev_list_idx = rc;

        /* Adding zpool schema to the list */
        rc = ldms_schema_metric_list_add(sampler_base->schema,
                                         ZPOOL_LIST_SNAME,
                                         NULL,
                                         zpool_list_len * zpool_heap_sz);
        if (rc < 0)
            goto err2;

        /* Adding vdev schema to the list */
        rc = ldms_schema_metric_list_add(sampler_base->schema,
                                         VDEV_LIST_SNAME,
                                         NULL,
                                         vdev_list_sz * vdev_heap_sz);
        if (rc < 0)
            goto err2;

        /* Create the metric set */
        base_set_new(sampler_base);
        if (sampler_base->set == NULL)
                goto err2;

        return 0;

err4:
        /* We only manually delete record template when it
         * hasn't been added to the schema yet */
        ldms_record_delete(vdev_def);
err3:
        /* We only manually delete record template when it
         * hasn't been added to the schema yet */
        ldms_record_delete(zpool_def);
err2:
        base_schema_delete(sampler_base);
err1:
        log_fn(LDMSD_LERROR, SAMP" initialization failed\n");
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


static int config(struct ldmsd_plugin *self,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        int rc = 0;
        char *value;

        log_fn(LDMSD_LDEBUG, SAMP" config() called\n");

        sampler_base = base_config(avl, SAMP, "zpools_metrics", log_fn);
        if ((g_zfs = libzfs_init()) == NULL) {
            rc = errno;
            ldmsd_log(LDMSD_LERROR,
                      SAMP" : Failed to initialize libzfs: %d\n", errno);
            ldmsd_log(LDMSD_LERROR,
                      SAMP" : Is the zfs module loaded or zrepl running?\n");
        } else {
            rc = initialize_ldms_structs();
        }

        /*  should be called from sample
            ret = zpool_iter(g_zfs, print_stats, argv[optind]);
            return (ret);
         */

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
        int    rc = 0;

        previous_heap_size = ldms_set_heap_size_get(sampler_base->set);
        base_set_delete(sampler_base);

        new_heap_size = previous_heap_size;
        new_heap_size += zpool_heap_sz  + vdev_heap_sz;

        if (base_set_new_heap(sampler_base, new_heap_size) == NULL) {
            rc = errno;
           ldmsd_log(LDMSD_LERROR,
                     SAMP" : Failed to resize metric set heap: %d\n", errno);
        }
        return rc;
}

/* strip leading and trailing whitespace */
static void strip_whitespace(char **start)
{
        /* strip leading whitespace */
        while (isspace(**start)) {
                (*start)++;
        }

        /* strip trailing whitespace */
        char * last;
        last = *start + strlen(*start) - 1;
        while (last > *start) {
                if (isspace(last[0])) {
                        last--;
                } else {
                        break;
                }
        }
        last[1] = '\0';
}


static int sample(struct ldmsd_sampler *self)
{
        struct cxil_device_list *device_list;
        ldms_mval_t list_handle;
        int i;
        int j;
        int rc = 0;

        base_sample_begin(sampler_base);
        rc = zpool_iter(g_zfs, get_stats, NULL);
        if (rc != 0) {
            log_fn(LDMSD_LERROR, SAMP" sample():zfs_pool print_stat() failed: %d\n", rc);
            base_sample_end(sampler_base);
            goto err1;
        }
        log_fn(LDMSD_LDEBUG, SAMP" sample(): # slingshot nics: %u\n", device_list->count);

        list_handle = ldms_metric_get(sampler_base->set, index_store.nic_list);
        ldms_list_purge(sampler_base->set, list_handle);
        for (i = 0; i < device_list->count; i++) {
                struct cxil_devinfo *dev_info = &device_list->info[i];
                ldms_mval_t record_instance;
                char interface[IFNAMSIZ];
                char mac_address[DEFAULT_ARRAY_LEN];

                interface[0] = '\0';
                record_instance = ldms_record_alloc(sampler_base->set,
                                                    index_store.nic_record);
                if (record_instance == NULL) {
                        log_fn(LDMSD_LDEBUG,
                               SAMP": ldms_record_alloc() failed, resizing metric set\n");
                        if (resize_metric_set() != 0) {
                            log_fn(LDMSD_LERROR,
                                   SAMP" memory reallocation failed: %d\n", rc);
                            base_sample_end(sampler_base);
                        }
                        break;
                }
                rc = ldms_list_append_record(sampler_base->set, list_handle,
                                             record_instance);

                /* name 0 */
                ldms_record_array_set_str(record_instance, rec_metric_ids[0],
                                          dev_info->device_name);
                /* interface 1 */
                get_interface_name(dev_info->device_name, interface, sizeof(interface));
                ldms_record_array_set_str(record_instance, rec_metric_ids[1],
                                          interface);
                /* fru_description 2 */
                ldms_record_array_set_str(record_instance, rec_metric_ids[2],
                                          dev_info->fru_description);
                /* part_number 3 */
                set_metric_from_sys(record_instance, rec_metric_ids[3],
                                    dev_info->device_name, "device/fru/part_number");
                /* serial_number 4 */
                set_metric_from_sys(record_instance, rec_metric_ids[4],
                                    dev_info->device_name, "device/fru/serial_number");
                /* firmware_version 5 */
                set_metric_from_sys(record_instance, rec_metric_ids[5],
                                    dev_info->device_name, "device/uc/qspi_blob_version");
                /* mac 6 */
                get_mac_address(interface, mac_address, sizeof(mac_address));
                ldms_record_array_set_str(record_instance, rec_metric_ids[6],
                                          mac_address);
                /* nid 7 */
                ldms_record_set_u32(record_instance, rec_metric_ids[7], dev_info->nid);
                /* pid_granule 8 */
                ldms_record_set_u32(record_instance, rec_metric_ids[8], dev_info->pid_granule);
                /* pcie_speed 9 */
                set_pcie_speed(record_instance, rec_metric_ids[9], dev_info->device_name);
                /* pcie_slot 10 */
                set_pcie_slot(record_instance, rec_metric_ids[10], dev_info);
                /* link_layer_retry 11 */
                set_metric_from_sys(record_instance, rec_metric_ids[11],
                                    dev_info->device_name, "device/port/link_layer_retry");
                /* link_loopback 12 */
                set_metric_from_sys(record_instance, rec_metric_ids[12],
                                    dev_info->device_name, "device/port/loopback");
                /* link_media 13 */
                set_metric_from_sys(record_instance, rec_metric_ids[13],
                                    dev_info->device_name, "device/port/media");
                /* link_mtu 14 */
                ldms_record_set_u32(record_instance, rec_metric_ids[14], dev_info->link_mtu);
                /* link_speed 15 */
                set_metric_from_sys(record_instance, rec_metric_ids[15],
                                    dev_info->device_name, "device/port/speed");
                /* link_state 16 */
                set_metric_from_sys(record_instance, rec_metric_ids[16],
                                    dev_info->device_name, "device/port/link");
        }
        base_sample_end(sampler_base);

        cxil_free_device_list(device_list);
err1:
        return rc;
}

static void term(struct ldmsd_plugin *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" term() called\n");
        base_set_delete(sampler_base);
        base_del(sampler_base);
        sampler_base = NULL;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static const char *usage(struct ldmsd_plugin *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" usage() called\n");
	return  "config name=" SAMP " " BASE_CONFIG_SYNOPSIS
                BASE_CONFIG_DESC
                ;
}

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
        static struct ldmsd_sampler plugin = {
                .base = {
                        .name = SAMP,
                        .type = LDMSD_PLUGIN_SAMPLER,
                        .term = term,
                        .config = config,
                        .usage = usage,
                },
                .get_set = get_set,
                .sample = sample,
        };

        log_fn = pf;
        log_fn(LDMSD_LDEBUG, SAMP" get_plugin() called ("PACKAGE_STRING")\n");

        return &plugin.base;
}
