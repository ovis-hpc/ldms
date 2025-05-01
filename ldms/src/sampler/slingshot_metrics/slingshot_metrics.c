/* -*- c-basic-offset: 8 -*- */
/* Copyright 2022 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "config.h"
#include "sampler_base.h"

#include <stddef.h> /* libcxi.h neglects to include this */
#include <libcxi/libcxi.h>
#include <cassini_cntr_desc.h> /* needed at least starting with shs-2.1.0 */
#define _GNU_SOURCE

#define SAMP "slingshot_metrics"
#define MAX_LINE_LEN 256
#define MAX_DEVICES 64

static ovis_log_t mylog;
static base_data_t sampler_base;
static size_t rec_heap_sz;

static struct {
        int name; /* name of the slingshot interface */
        int nic_record;
        int nic_list;
} index_store;

static struct {
        int num_counters;
        int ldms_index[C1_CNTR_COUNT];
        enum c_cntr_type slingshot_index[C1_CNTR_COUNT];
        uint64_t value[C1_CNTR_COUNT];
} counters;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

/* we will look up the counter names in the c1_cntr_descs array */
static const char * const default_counter_names[] = {
        "ixe_mem_cor_err_cntr",
        "ixe_mem_ucor_err_cntr",
        "ixe_port_dfa_mismatch",
        "ixe_hdr_checksum_errors",
        "ixe_ipv4_checksum_errors",
        "ixe_hrp_req_errors",
        "ixe_ip_options_errors",
        "ixe_get_len_errors",
        "ixe_roce_icrc_error",
        "ixe_parser_par_errors",
        "ixe_pbuf_rd_errors",
        "ixe_hdr_ecc_errors",
        "ixe_rx_udp_pkt",
        "ixe_rx_tcp_pkt",
        "ixe_rx_ipv4_pkt",
        "ixe_rx_ipv6_pkt",
        "ixe_rx_roce_pkt",
        "ixe_rx_ptl_gen_pkt",
        "ixe_rx_ptl_sml_pkt",
        "ixe_rx_ptl_unrestricted_pkt",
        "ixe_rx_ptl_smallmsg_pkt",
        "ixe_rx_ptl_continuation_pkt",
        "ixe_rx_ptl_restricted_pkt",
        "ixe_rx_ptl_connmgmt_pkt",
        "ixe_rx_ptl_response_pkt",
        "ixe_rx_unrecognized_pkt",
        "ixe_rx_ptl_sml_amo_pkt",
        "ixe_rx_ptl_msgs",
        "ixe_rx_ptl_multi_msgs",
        "ixe_rx_ptl_mr_msgs",
        "ixe_rx_pkt_drop_pct",
        "ixe_rx_pkt_drop_rmu_norsp",
        "ixe_rx_pkt_drop_rmu_wrsp",
        "ixe_rx_pkt_drop_ixe_parser",
        "ixe_rx_pkt_ipv4_options",
        "ixe_rx_pkt_ipv6_options",
        "ixe_rx_eth_seg",
        "ixe_rx_roce_seg",
        "ixe_rx_roce_spseg",
};

static struct cxil_device_list *cache_cxil_device_list;
static time_t cache_cxil_device_list_refresh_interval;
static struct cxil_dev *cache_cxil_dev[MAX_DEVICES];

static int cache_cxil_dev_get(uint32_t dev_id, struct cxil_dev **dev)
{
        if (dev_id >= MAX_DEVICES) {
                return -1;
        }

        if (cache_cxil_dev[dev_id] == NULL) {
                int rc;

                rc = cxil_open_device(dev_id, &cache_cxil_dev[dev_id]);
                if (rc != 0) {
                        *dev = NULL;
                        return rc;
                }
        }

        *dev = cache_cxil_dev[dev_id];

        return 0;
}

static void cache_cxil_dev_close(struct cxil_dev *dev)
{
        int i;

        for (i = 0; i < MAX_DEVICES; i++) {
                if (cache_cxil_dev[i] == NULL) {
                        continue;
                } else if (cache_cxil_dev[i] == dev) {
                        cxil_close_device(cache_cxil_dev[i]);
                        cache_cxil_dev[i] = NULL;
                        break;
                }
        }
}

static void cache_cxil_dev_close_all()
{
        int i;

        for (i = 0; i < MAX_DEVICES; i++) {
                if (cache_cxil_dev[i] == NULL) {
                        continue;
                }
                cxil_close_device(cache_cxil_dev[i]);
                cache_cxil_dev[i] = NULL;
        }
}

static void cache_cxil_dev_refresh_all()
{
        int i;
        int j;
        bool missing;

        for (i = 0; i < MAX_DEVICES; i++) {
                missing = true;
                for (j = 0; j < cache_cxil_device_list->count; j++) {
                        if (i == cache_cxil_device_list->info[j].dev_id) {
                                missing = false;
                                break;
                        }
                }
                if (missing && cache_cxil_dev[i] != NULL) {
                        ovis_log(mylog, OVIS_LDEBUG, "refresh_all: removing dev_id %d\n", i);
                        cxil_close_device(cache_cxil_dev[i]);
                        cache_cxil_dev[i] = NULL;
                } else if (!missing && cache_cxil_dev[i] == NULL) {
                        ovis_log(mylog, OVIS_LDEBUG, "refresh_all: opening dev_id %d\n", i);
                        cxil_open_device(i, &cache_cxil_dev[i]);
                }
        }
}

static int cache_cxil_device_list_get(struct cxil_device_list **dev_list)
{
        static time_t last_refresh = 0;
        time_t current_time;
        int rc = 0;

        current_time = time(NULL);
        if (current_time >= last_refresh + cache_cxil_device_list_refresh_interval) {
                struct cxil_device_list *tmp_list;

                ovis_log(mylog, OVIS_LDEBUG, "updating device list cache\n");
                tmp_list = cache_cxil_device_list;
                cache_cxil_device_list = NULL;
                cxil_free_device_list(tmp_list);

                rc = cxil_get_device_list(&tmp_list);
                if (rc == 0) {
                        cache_cxil_device_list = tmp_list;
                        last_refresh = current_time;
                }

                cache_cxil_dev_refresh_all();
        }

        *dev_list = cache_cxil_device_list;
        return rc;
}

static void cache_cxil_device_list_free()
{
        if (cache_cxil_device_list != NULL) {
                struct cxil_device_list *tmp_list;

                tmp_list = cache_cxil_device_list;
                cache_cxil_device_list = NULL;
                cxil_free_device_list(tmp_list);
        }
}

static int initialize_ldms_record_metrics(ldms_record_t rec_def) {
        int index;
        int i;

        for (i = 0; i < counters.num_counters; i++) {
                int rc;
                char const * const_name;

                const_name = c1_cntr_descs[counters.slingshot_index[i]].name;
		/* ovis_log(mylog, OVIS_LDEBUG, "counter name = %s\n", const_name); */
                index = ldms_record_metric_add(rec_def, const_name, NULL,
                                               LDMS_V_U64, 0);
                if (index < 0)
                        return index;
                counters.ldms_index[i] = index;
        }

        return 0;
}

static int initialize_ldms_structs()
{
        ldms_record_t rec_def; /* a pointer */
        int rc;

        ovis_log(mylog, OVIS_LDEBUG, "initialize()\n");

        /* Create the schema */
        base_schema_new(sampler_base);
        if (sampler_base->schema == NULL)
                goto err1;
        rec_def = ldms_record_create("slingshot_nic");
        if (rec_def == NULL)
                goto err2;
        rc = ldms_record_metric_add(rec_def, "name", NULL,
                                    LDMS_V_CHAR_ARRAY, CXIL_DEVNAME_MAX+1);
        if (rc < 0)
                goto err3;
        index_store.name = rc;
        rc = initialize_ldms_record_metrics(rec_def);
        if (rc < 0)
                goto err3;
        rec_heap_sz = ldms_record_heap_size_get(rec_def);
        rc = ldms_schema_record_add(sampler_base->schema, rec_def);
        if (rc < 0)
                goto err3;
        index_store.nic_record = rc;
        rc = ldms_schema_metric_list_add(sampler_base->schema, "nics", NULL, 1024);
        if (rc < 0)
                goto err2;
        index_store.nic_list = rc;

        /* Create the metric set */
        base_set_new(sampler_base);
        if (sampler_base->set == NULL)
                goto err2;

        return 0;
err3:
        /* We only manually delete rec_def when it hasn't been added to
           the schema yet */
        ldms_record_delete(rec_def);
err2:
        base_schema_delete(sampler_base);
err1:
        ovis_log(mylog, OVIS_LERROR, "initialization failed\n");
        return -1;
}

static int find_slingshot_index(const char counter_name[]) {
        int i;

        for (i = 0; i < C1_CNTR_SIZE; i++) {
                if (c1_cntr_descs[i].name == NULL) {
                        continue;
                }
                if (!strcmp(counter_name, c1_cntr_descs[i].name)) {
                        return i;
                }
        }

        return -1;
}

static int use_counter(const char * const counter_name);

static int use_counter_group(const char * const counter_group)
{
        int i;
        bool all_counters = false;
        enum c_cntr_group group;
        int rc;

        if (!strcmp(counter_group, "all")) {
                all_counters = true;
        } else if (!strcmp(counter_group, "ext")) {
                group = C_CNTR_GROUP_EXT;
        } else if (!strcmp(counter_group, "pi_ipd")) {
                group = C_CNTR_GROUP_PI_IPD;
        } else if (!strcmp(counter_group, "mb")) {
                group = C_CNTR_GROUP_MB;
        } else if (!strcmp(counter_group, "cq")) {
                group = C_CNTR_GROUP_CQ;
        } else if (!strcmp(counter_group, "lpe")) {
                group = C_CNTR_GROUP_LPE;
        } else if (!strcmp(counter_group, "hni")) {
                group = C_CNTR_GROUP_HNI;
        } else if (!strcmp(counter_group, "ext2")) {
                group = C_CNTR_GROUP_EXT2;
        } else {
                ovis_log(mylog, OVIS_LERROR, "unrecognized counter group \"%s\"\n",
                       counter_group);
                return -1;
        }

        for (int i = 0; i < C1_CNTR_SIZE; i++) {
                if (c1_cntr_descs[i].name == NULL) {
                        /* there are holes in the slingshot counters array
                           due to grouping, and leaving space for future new
                           counters */
                        continue;
                } else if (all_counters || c1_cntr_descs[i].group == group) {
                        rc = use_counter(c1_cntr_descs[i].name);
                        if (rc != 0) {
                                return rc;
                        }
                }
        }
        return 0;
}

static int use_counter(const char * const counter_name)
{
        int counter_num;
        int slingshot_index;
        int i;
        const char * const group_prefix = "group:";

        if (!strncmp(counter_name, group_prefix, strlen(group_prefix))) {
                return use_counter_group(counter_name+strlen(group_prefix));
        }

        slingshot_index = find_slingshot_index(counter_name);
        if (slingshot_index == -1) {
                ovis_log(mylog, OVIS_LERROR, "counter not found \"%s\"\n",
                       counter_name);
                return -1;
        }

        /* check to for duplicate */
        for (int i; i < counters.num_counters; i++) {
                if (counters.slingshot_index[i] == slingshot_index) {
                        /* just a warning, not fatal */
                        ovis_log(mylog, OVIS_LWARNING, "skipping duplicate counter \"%s\"\n",
                               counter_name);
                        return 0;
                }
        }

        /* record the slingshot counter index for this counter name */
        counter_num = counters.num_counters;
        counters.slingshot_index[counter_num] = slingshot_index;
        counters.num_counters = counter_num + 1;

        return 0;
}

static int parse_counters_string(const char * const counters_string)
{
        char *working_string;
        char *saveptr;
        char *tmp;
        char *token;
        int rc = 0;

        working_string = strdup(counters_string);
        if (working_string == NULL) {
                ovis_log(mylog, OVIS_LERROR, "parse_counters_string() strdup failed: %d", errno);
                rc = -1;
                goto err0;
        }

        for (tmp = working_string; (token = strtok_r(tmp, ",", &saveptr)) != NULL; tmp = NULL) {
                rc = use_counter(token);
                if (rc != 0) {
                        goto err1;
                }
        }
err1:
        free(working_string);
err0:
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

static int parse_counters_file(const char * const counters_file)
{
        FILE *fp;
        char buf[MAX_LINE_LEN];
        int rc = 0;

        fp = fopen(counters_file, "r");
        if (fp == NULL) {
                ovis_log(mylog, OVIS_LERROR, "parse_counters_file() failed fopen of \"%s\": %d\n",
                       counters_file, errno);
                return -1;
        }

        while (fgets(buf, MAX_LINE_LEN, fp) != NULL) {
                char *tmp = buf;
                strip_whitespace(&tmp);
                if (tmp[0] == '#') {
                        continue;
                }
                rc = use_counter(tmp);
                if (rc != 0) {
                        break;
                }
        }
        fclose(fp);

        return rc;
}

static int use_default_counters()
{
        int i;
        int rc = 0;

        for (i = 0; i < ARRAY_SIZE(default_counter_names); i++) {
                rc = use_counter(default_counter_names[i]);
                if (rc != 0) {
                        break;
                }
        }

        return rc;
}

static int config(ldmsd_plug_handle_t handle,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        int rc = 0;
        char *value;

        ovis_log(mylog, OVIS_LDEBUG, "config() called\n");

        sampler_base = base_config(avl, ldmsd_plug_cfg_name_get(handle), "slingshot_metrics", mylog);

        value = av_value(avl, "counters");
        if (value != NULL) {
                rc = parse_counters_string(value);
                if (rc != 0) {
                        goto err;
                }
        }

        value = av_value(avl, "counters_file");
        if (value != NULL) {
                rc = parse_counters_file(value);
                if (rc != 0) {
                        goto err;
                }
        }

        if (counters.num_counters == 0) {
                rc = use_default_counters();
                if (rc != 0) {
                        goto err;
                }
        }

        value = av_value(avl, "refresh_interval_sec");
        if (value != NULL) {
                char *end;
                long val;

                strip_whitespace(&value);
                val = strtol(value, &end, 10);
                if (*end != '\0') {
                        ovis_log(mylog, OVIS_LERROR, "refresh_interval must be a decimal number\n");
                        rc = EINVAL;
                        return rc;
                }
                cache_cxil_device_list_refresh_interval = (time_t)val;
        } else {
                cache_cxil_device_list_refresh_interval = (time_t)600;
        }

        rc = initialize_ldms_structs();
        if (rc != 0) {
                goto err;
        }

        return 0;
err:
        base_del(sampler_base);
        sampler_base = NULL;
        return rc;
}

static void resize_metric_set(int expected_remaining_nics)
{
        size_t previous_heap_size;
        size_t new_heap_size;

        previous_heap_size = ldms_set_heap_size_get(sampler_base->set);
        base_set_delete(sampler_base);

        new_heap_size = previous_heap_size;
        new_heap_size += rec_heap_sz * expected_remaining_nics;
        new_heap_size += ldms_list_heap_size_get(LDMS_V_RECORD_ARRAY, expected_remaining_nics, 1);

        base_set_new_heap(sampler_base, new_heap_size);
        if (sampler_base->set == NULL) {
                ovis_log(mylog, OVIS_LERROR,
                          "Failed to resize metric set heap: %d\n", errno);
        }
}

static int sample(ldmsd_plug_handle_t handle)
{
        struct cxil_device_list *device_list;
        ldms_mval_t list_handle;
        int i;
        int j;
        int rc = 0;

        base_sample_begin(sampler_base);
        rc = cache_cxil_device_list_get(&device_list);
        if (rc != 0) {
                ovis_log(mylog, OVIS_LERROR, "sample(): cache_cxil_device_list_get() failed: %d\n", rc);
                base_sample_end(sampler_base);
                return -1;
        }
        ovis_log(mylog, OVIS_LDEBUG, "sample(): # slingshot nics: %u\n", device_list->count);

        list_handle = ldms_metric_get(sampler_base->set, index_store.nic_list);
        ldms_list_purge(sampler_base->set, list_handle);
        for (i = 0; i < device_list->count; i++) {
                struct cxil_devinfo *dev_info = &device_list->info[i];
                struct cxil_dev *dev;
                ldms_mval_t record_instance;

                rc = cache_cxil_dev_get(dev_info->dev_id, &dev);
                if (rc != 0) {
                        continue;
                }

                record_instance = ldms_record_alloc(sampler_base->set,
                                                    index_store.nic_record);
                if (record_instance == NULL) {
                        ovis_log(mylog, OVIS_LDEBUG, SAMP": ldms_record_alloc() failed, resizing metric set\n");
                        resize_metric_set(device_list->count - i);
                        break;
                }
                ldms_list_append_record(sampler_base->set, list_handle,
                                        record_instance);

                ldms_record_array_set_str(record_instance, index_store.name,
                                          dev_info->device_name);

                for (j = 0; j < counters.num_counters; j++) {
                        /* cxil_read_n_cntrs was no faster here in testing */
                        rc = cxil_read_cntr(dev,
                                            counters.slingshot_index[j],
                                            &counters.value[j],
                                            NULL);
                        if (rc != 0) {
                                ovis_log(mylog, OVIS_LWARNING, "sample(): cxil_read_cntr() failed for device %s, rc = %d\n",
                                         dev_info->device_name, rc);
                                cache_cxil_dev_close(dev);
                                /* FIXME - we should really free the record here,
                                   and avoid adding it to the list, but there is
                                   currently no ldms_record_free() function */
                                break;
                        }
                        ldms_record_set_u64(record_instance,
                                            counters.ldms_index[j],
                                            counters.value[j]);
                }
        }
        base_sample_end(sampler_base);

        return 0;
}

static void term(ldmsd_plug_handle_t handle)
{
        ovis_log(mylog, OVIS_LDEBUG, "term() called\n");
        base_set_delete(sampler_base);
        base_del(sampler_base);
        sampler_base = NULL;
        cache_cxil_device_list_free();
        cache_cxil_dev_close_all();
        ovis_log(mylog, OVIS_LDEBUG, "term() called\n");
}

static const char *usage(ldmsd_plug_handle_t handle)
{
        ovis_log(mylog, OVIS_LDEBUG, " usage() called\n");
	return  "config name=" SAMP " " BASE_CONFIG_SYNOPSIS
                BASE_CONFIG_DESC
                ;
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
                .term = term,
                .config = config,
                .usage = usage,
		.constructor = constructor,
		.destructor = destructor,
        },
        .sample = sample,
};
