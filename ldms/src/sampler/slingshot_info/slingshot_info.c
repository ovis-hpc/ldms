/* -*- c-basic-offset: 8 -*- */
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

#include <stddef.h> /* libcxi.h neglects to include this */
#include <libcxi/libcxi.h>

#define SAMP "slingshot_info"
#define MAX_LINE_LEN 256

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof(a) / sizeof(*a))
#endif

#ifndef IFNAMSIZ
/* from "linux/if.h" */
#define IFNAMSIZ 16
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define DEFAULT_ARRAY_LEN 32

static ldmsd_msg_log_f log_fn;
static base_data_t sampler_base;
static ldms_record_t rec_def; /* a pointer */

static struct {
        int nic_record;
        int nic_list;
} index_store;

struct ldms_metric_template_s rec_metrics[] = {
	{ "name"             , 0,    LDMS_V_CHAR_ARRAY , ""        , CXIL_DEVNAME_MAX+1 } ,
	{ "interface"        , 0,    LDMS_V_CHAR_ARRAY , ""        , IFNAMSIZ } ,
	{ "fru_description"  , 0,    LDMS_V_CHAR_ARRAY , ""        , CXIL_FRUDESC_MAX+1 } ,
	{ "part_number"      , 0,    LDMS_V_CHAR_ARRAY , ""        , DEFAULT_ARRAY_LEN } ,
	{ "serial_number"    , 0,    LDMS_V_CHAR_ARRAY , ""        , DEFAULT_ARRAY_LEN } ,
	{ "firmware_version" , 0,    LDMS_V_CHAR_ARRAY , ""        , DEFAULT_ARRAY_LEN } ,
	{ "mac"              , 0,    LDMS_V_CHAR_ARRAY , ""        , DEFAULT_ARRAY_LEN } ,
	{ "nid"              , 0,    LDMS_V_U32        , ""        , 1  } ,
	{ "pid_granule"      , 0,    LDMS_V_U32        , ""        , 1  } ,
	{ "pcie_speed"       , 0,    LDMS_V_CHAR_ARRAY , ""        , DEFAULT_ARRAY_LEN } ,
	{ "pcie_slot"        , 0,    LDMS_V_CHAR_ARRAY , ""        , DEFAULT_ARRAY_LEN } ,
	{ "link_layer_retry" , 0,    LDMS_V_CHAR_ARRAY , ""        , DEFAULT_ARRAY_LEN } ,
	{ "link_loopback"    , 0,    LDMS_V_CHAR_ARRAY , ""        , DEFAULT_ARRAY_LEN } ,
	{ "link_media"       , 0,    LDMS_V_CHAR_ARRAY , ""        , DEFAULT_ARRAY_LEN } ,
	{ "link_mtu"         , 0,    LDMS_V_U32        , ""        , 1  } ,
	{ "link_speed"       , 0,    LDMS_V_CHAR_ARRAY , ""        , DEFAULT_ARRAY_LEN } ,
	{ "link_state"       , 0,    LDMS_V_CHAR_ARRAY , ""        , DEFAULT_ARRAY_LEN } ,
	{0},
};
#define REC_METRICS_LEN (ARRAY_LEN(rec_metrics) - 1)
int rec_metric_ids[REC_METRICS_LEN];
size_t rec_heap_sz;

static int initialize_ldms_structs()
{
        int rc;

        log_fn(LDMSD_LDEBUG, SAMP" initialize()\n");

        /* Create the per-nic record definition */
        rec_def = ldms_record_from_template("slingshot", rec_metrics, rec_metric_ids);
        rec_heap_sz = ldms_record_heap_size_get(rec_def);

        /* Create the schema */
        base_schema_new(sampler_base);
        if (sampler_base->schema == NULL)
                goto err2;
        rc = ldms_schema_record_add(sampler_base->schema, rec_def);
        if (rc < 0)
                goto err3;
        index_store.nic_record = rc;
        rc = ldms_schema_metric_list_add(sampler_base->schema, "nics", NULL, 1024);
        if (rc < 0) {
                goto err3;
        }
        index_store.nic_list = rc;

        /* Create the metric set */
        base_set_new(sampler_base);
        if (sampler_base->set == NULL)
                goto err3;

        return 0;
err3:
        base_del(sampler_base);
err2:
        ldms_record_delete(rec_def);
err1:
        log_fn(LDMSD_LERROR, SAMP" initialization failed\n");
        return -1;
}

static int config(struct ldmsd_plugin *self,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        int rc = 0;
        char *value;

        log_fn(LDMSD_LDEBUG, SAMP" config() called\n");

        sampler_base = base_config(avl, SAMP, "slingshot_metrics", log_fn);

        rc = initialize_ldms_structs();

        return rc;
}

static void resize_metric_set(int expected_remaining_nics)
{
        size_t previous_heap_size;
        size_t new_heap_size;

        previous_heap_size = ldms_set_heap_size_get(sampler_base->set);
        base_set_delete(sampler_base);

        new_heap_size = previous_heap_size;
        new_heap_size += ldms_record_heap_size_get(rec_def) * expected_remaining_nics;
        new_heap_size += ldms_list_heap_size_get(LDMS_V_RECORD_ARRAY, expected_remaining_nics, 1);

        base_set_new_heap(sampler_base, new_heap_size);
        if (sampler_base->set == NULL) {
                ldmsd_log(LDMSD_LERROR,
                          SAMP" : Failed to resize metric set heap: %d\n", errno);
        }
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


static void set_pcie_speed(ldms_mval_t record_instance, int index, const char *device_name)
{
        char path[PATH_MAX];
        char buf1[PATH_MAX];
        char buf2[PATH_MAX];
        char *speed = buf1;
        char *width = buf2;
        FILE *fp;
        char *rc;

        snprintf(path, PATH_MAX, "/sys/class/cxi/%s/device/current_link_speed",
                 device_name);
        fp = fopen(path, "r");
        if (fp == NULL) {
                log_fn(LDMSD_LWARNING, SAMP" unable to open \"%s\"\n", path);
                return;
        }
        rc = fgets(speed, PATH_MAX, fp);
        fclose(fp);
        if (rc == NULL) {
                return;
        }
        strip_whitespace(&speed);

        snprintf(path, PATH_MAX, "/sys/class/cxi/%s/device/current_link_width",
                 device_name);
        fp = fopen(path, "r");
        if (fp == NULL) {
                log_fn(LDMSD_LWARNING, SAMP" unable to open \"%s\"\n", path);
                return;
        }
        rc = fgets(width, PATH_MAX, fp);
        fclose(fp);
        if (rc == NULL) {
                return;
        }
        strip_whitespace(&width);

        snprintf(path, PATH_MAX, "%s x%s", speed, width);
        ldms_record_array_set_str(record_instance, index, path);
}

static void set_pcie_slot(ldms_mval_t record_instance, int index,
                          struct cxil_devinfo *dev_info)
{
        char buf[DEFAULT_ARRAY_LEN];

        snprintf(buf, sizeof(buf), "%.4x:%.2x:%.2x:%x",
                 dev_info->pci_domain,
                 dev_info->pci_bus,
                 dev_info->pci_device,
                 dev_info->pci_function);

        ldms_record_array_set_str(record_instance, index, buf);
}


static void set_metric_from_sys(ldms_mval_t record_instance, int index,
                                const char *device_name, const char *subpath)
{
        char path[PATH_MAX];
        char buf[PATH_MAX];
        char *value = buf;
        FILE *fp;
        char *rc;

        snprintf(path, PATH_MAX, "/sys/class/cxi/%s/%s", device_name, subpath);
        fp = fopen(path, "r");
        if (fp == NULL) {
                log_fn(LDMSD_LWARNING, SAMP" unable to open \"%s\"\n", path);
                return;
        }
        rc = fgets(value, PATH_MAX, fp);
        fclose(fp);
        if (rc == NULL) {
                return;
        }
        strip_whitespace(&value);
        ldms_record_array_set_str(record_instance, index, value);
}

static void get_interface_name(const char *device_name, char *interface, int len)
{
        glob_t globbuf;
        char pattern[PATH_MAX];
        char *ptr;
        char *tmp;
        int rc;

        snprintf(pattern, PATH_MAX,
                 "/sys/class/net/*/device/cxi/%s", device_name);
        glob(pattern, GLOB_NOSORT, NULL, &globbuf);
        if (rc != 0) {
                return;
        }
        ptr = globbuf.gl_pathv[0];
        ptr += strlen("/sys/class/net/");
        for (tmp = ptr; *tmp != '/' && *tmp != '\0'; tmp++) {
        }
        *tmp = '\0';
        strncpy(interface, ptr, len);
        globfree(&globbuf);
}

static void get_mac_address(const char *interface, char *mac_address, int len)
{
        char path[PATH_MAX];
        char buf[PATH_MAX];
        char *value = buf;
        FILE *fp;
        char *rc;

        snprintf(path, PATH_MAX, "/sys/class/net/%s/address", interface);
        fp = fopen(path, "r");
        if (fp == NULL) {
                log_fn(LDMSD_LWARNING, SAMP" unable to open \"%s\"\n", path);
                return;
        }
        rc = fgets(value, PATH_MAX, fp);
        fclose(fp);
        if (rc == NULL) {
                return;
        }
        strip_whitespace(&value);
        strncpy(mac_address, value, len);
}

static int sample(struct ldmsd_sampler *self)
{
        struct cxil_device_list *device_list;
        ldms_mval_t list_handle;
        int i;
        int j;
        int rc = 0;

        base_sample_begin(sampler_base);
        rc = cxil_get_device_list(&device_list);
        if (rc != 0) {
                log_fn(LDMSD_LERROR, SAMP" sample(): cxil_get_device_list() failed: %d\n", rc);
                base_sample_end(sampler_base);
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
                        log_fn(LDMSD_LDEBUG, SAMP": ldms_record_alloc() failed, resizing metric set\n");
                        resize_metric_set(device_list->count - i);
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

        return rc;
}

static void term(struct ldmsd_plugin *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" term() called\n");
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
