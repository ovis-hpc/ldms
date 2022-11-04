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
#include "config.h"
#include "sampler_base.h"

#include <stddef.h> /* libcxi.h neglects to include this */
#include <libcxi/libcxi.h>
#define _GNU_SOURCE

#define SAMP "slingshot_metrics"
#define MAX_LINE_LEN 256
#define MAX_DEVICES 64

static ldmsd_msg_log_f log_fn;
static base_data_t sampler_base;
static ldms_record_t nic_record; /* a pointer */

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
                        log_fn(LDMSD_LDEBUG, SAMP" refresh_all: removing dev_id %d\n", i);
                        cxil_close_device(cache_cxil_dev[i]);
                        cache_cxil_dev[i] = NULL;
                } else if (!missing && cache_cxil_dev[i] == NULL) {
                        log_fn(LDMSD_LDEBUG, SAMP" refresh_all: opening dev_id %d\n", i);
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

                log_fn(LDMSD_LDEBUG, SAMP" updating device list cache\n");
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

static void initialize_ldms_record_metrics() {
        int i;
        int index;

        for (i = 0; i < counters.num_counters; i++) {
                int rc;
                char const * const_name;

                const_name = c1_cntr_descs[counters.slingshot_index[i]].name;
                /* log_fn(LDMSD_LDEBUG, SAMP" counter name = %s\n", const_name); */
                index = ldms_record_metric_add(nic_record, const_name, NULL,
                                               LDMS_V_U64, 0);
                counters.ldms_index[i] = index;
        }
}

static int initialize_ldms_structs()
{
        int rc;

        log_fn(LDMSD_LDEBUG, SAMP" initialize()\n");

        /* Create the per-nic record definition */
        nic_record = ldms_record_create("slingshot_nic");
        if (nic_record == NULL)
                goto err1;
        rc = ldms_record_metric_add(nic_record, "name", NULL,
                                    LDMS_V_CHAR_ARRAY, CXIL_DEVNAME_MAX+1);
        if (rc < 0)
                goto err2;
        index_store.name = rc;
        initialize_ldms_record_metrics();

        /* Create the schema */
        base_schema_new(sampler_base);
        if (sampler_base->schema == NULL)
                goto err2;
        rc = ldms_schema_record_add(sampler_base->schema, nic_record);
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
        ldms_record_delete(nic_record);
err1:
        log_fn(LDMSD_LERROR, SAMP" initialization failed\n");
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
                log_fn(LDMSD_LERROR, SAMP" unrecognized counter group \"%s\"\n",
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
                log_fn(LDMSD_LERROR, SAMP" counter not found \"%s\"\n",
                       counter_name);
                return -1;
        }

        /* check to for duplicate */
        for (int i; i < counters.num_counters; i++) {
                if (counters.slingshot_index[i] == slingshot_index) {
                        /* just a warning, not fatal */
                        log_fn(LDMSD_LWARNING, SAMP" skipping duplicate counter \"%s\"\n",
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
                log_fn(LDMSD_LERROR, SAMP" parse_counters_string() strdup failed: %d", errno);
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
                log_fn(LDMSD_LERROR, SAMP" parse_counters_file() failed fopen of \"%s\": %d\n",
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

static int config(struct ldmsd_plugin *self,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        int rc = 0;
        char *value;

        log_fn(LDMSD_LDEBUG, SAMP" config() called\n");

        sampler_base = base_config(avl, SAMP, "slingshot_metrics", log_fn);

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
                        log_fn(LDMSD_LERROR, SAMP" refresh_interval must be a decimal number\n");
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
        return rc;
}

static void resize_metric_set(int expected_remaining_nics)
{
        size_t previous_heap_size;
        size_t new_heap_size;

        previous_heap_size = ldms_set_heap_size_get(sampler_base->set);
        base_set_delete(sampler_base);

        new_heap_size = previous_heap_size;
        new_heap_size += ldms_record_heap_size_get(nic_record) * expected_remaining_nics;
        new_heap_size += ldms_list_heap_size_get(LDMS_V_RECORD_ARRAY, expected_remaining_nics, 1);

        base_set_new_heap(sampler_base, new_heap_size);
        if (sampler_base->set == NULL) {
                ldmsd_log(LDMSD_LERROR,
                          SAMP" : Failed to resize metric set heap: %d\n", errno);
        }
}

static int sample(struct ldmsd_sampler *self)
{
        struct cxil_device_list *device_list;
        ldms_mval_t list_handle;
        int i;
        int j;
        int rc = 0;

        base_sample_begin(sampler_base);
        rc = cache_cxil_device_list_get(&device_list);
        if (rc != 0) {
                log_fn(LDMSD_LERROR, SAMP" sample(): cache_cxil_device_list_get() failed: %d\n", rc);
                base_sample_end(sampler_base);
                return -1;
        }
        log_fn(LDMSD_LDEBUG, SAMP" sample(): # slingshot nics: %u\n", device_list->count);

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
                        log_fn(LDMSD_LDEBUG, SAMP": ldms_record_alloc() failed, resizing metric set\n");
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

        return rc;
}

static void term(struct ldmsd_plugin *self)
{
        cache_cxil_device_list_free();
        cache_cxil_dev_close_all();
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
