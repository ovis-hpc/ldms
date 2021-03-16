/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <dirent.h>
#include <coll/rbt.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ldms.h"
#include "ldmsd.h"
#include <dcgm_agent.h>
#include "config.h"
#include "jobid_helper.h"

#define _GNU_SOURCE

#define SAMP "dcgm"

static unsigned short default_fields[] = {
        DCGM_FI_DEV_GPU_TEMP,
        DCGM_FI_DEV_POWER_USAGE,
#if 0
        DCGM_FI_PROF_GR_ENGINE_ACTIVE,
        DCGM_FI_PROF_SM_ACTIVE,
        DCGM_FI_PROF_SM_OCCUPANCY,
        DCGM_FI_PROF_PIPE_TENSOR_ACTIVE,
        DCGM_FI_PROF_DRAM_ACTIVE,
        DCGM_FI_PROF_PIPE_FP64_ACTIVE,
        DCGM_FI_PROF_PIPE_FP32_ACTIVE,
        DCGM_FI_PROF_PIPE_FP16_ACTIVE,
        DCGM_FI_PROF_PCIE_TX_BYTES,
        DCGM_FI_PROF_PCIE_RX_BYTES,
#endif
#if 0
        DCGM_FI_PROF_NVLINK_TX_BYTES,
        DCGM_FI_PROF_NVLINK_RX_BYTES,
#endif
};

static struct {
        char *schema_name;
        unsigned short *fields;
        int fields_len;
        long interval;
} conf;

static ldmsd_msg_log_f log_fn;
static int dcgm_initialized = 0;
static char producer_name[LDMS_PRODUCER_NAME_MAX];
static short standalone = 1;
static char *host_ip = "127.0.0.1";
static dcgmGpuGrp_t gpu_group_id;
static dcgmFieldGrp_t field_group_id;
static int gpu_id_metric_index;
static unsigned int gpu_ids[DCGM_MAX_NUM_DEVICES];
static int gpu_ids_count;
static ldms_schema_t gpu_schema;
/* note that ldms_set_t is a pointer */
/* NOTE: we are assuming here that GPU ids will start at zero and
   not exceed the DCGM_MAX_NUM_DEVICES count in value */
static ldms_set_t gpu_sets[DCGM_MAX_NUM_DEVICES];

/* We won't use many of the entries in this array, but DCGM_FI_MAX_FIELDS is
is only around 1000.  We trade off memory usage to allow quick translation of
DCGM field ids to ldms index numbers. */
static struct {
        unsigned short ldms_type;
        int ldms_index;
} translation_table[DCGM_FI_MAX_FIELDS];

static dcgmHandle_t dcgm_handle;

/* returns ldms type */
static unsigned short dcgm_to_ldms_type(unsigned short dcgm_field_type)
{
        switch (dcgm_field_type) {
        case DCGM_FT_BINARY:
                /* we do not handle dcgm binary blobs */
                return LDMS_V_NONE;
        case DCGM_FT_DOUBLE:
                return LDMS_V_D64;
        case DCGM_FT_INT64:
                return LDMS_V_S64;
        case DCGM_FT_STRING:
                return LDMS_V_CHAR_ARRAY;
        case DCGM_FT_TIMESTAMP:
                return LDMS_V_S64;
        default:
                return LDMS_V_NONE;
        }
}

static int sample_cb(unsigned int gpu_id, dcgmFieldValue_v1 *values,
                                   int num_values, void *user_data)
{
        ldms_set_t set = gpu_sets[gpu_id];
        int i;

        for (i = 0; i < num_values; i++) {
                dcgmFieldValue_v1 *value = &values[i];
                int ldms_index = translation_table[value->fieldId].ldms_index;
                int ldms_type = translation_table[value->fieldId].ldms_type;

                if (dcgm_to_ldms_type(value->fieldType) != ldms_type) {
                        log_fn(LDMSD_LERROR, SAMP" data type mismatch, "
                               "field=%d, expected ldms=%d, received dcgm=%d\n",
                               value->fieldId, ldms_type, value->fieldType);
                        continue;
                }

                switch(value->fieldType) {
                case DCGM_FT_BINARY:
                        /* we do not handle binary blobs */
                        break;
                case DCGM_FT_DOUBLE:
                        ldms_metric_set_double(set, ldms_index, value->value.dbl);
                        break;
                case DCGM_FT_INT64:
                        ldms_metric_set_s64(set, ldms_index, value->value.i64);
                        break;
                case DCGM_FT_STRING:
                        ldms_metric_array_set_str(set, ldms_index, value->value.str);
                        break;
                case DCGM_FT_TIMESTAMP:
                        /* should this use value->ts instead?? */
                        ldms_metric_set_s64(set, ldms_index, value->value.i64);
                        break;
                default:
                        log_fn(LDMSD_LERROR, SAMP" unexpected data type, field=%d, received=%d\n",
                               value->fieldType, value->fieldType);
                        break;
                }
        }

        return 0;
}

static int dcgm_init()
{
        dcgmReturn_t rc;

        rc = dcgmInit();
        if (rc != DCGM_ST_OK) {
                log_fn(LDMSD_LERROR, SAMP" dcgmInit() failed: %s(%d)\n",
                       errorString(rc), rc);
                return -1;
        }

        if (standalone) {
                rc = dcgmConnect(host_ip, &dcgm_handle);
                if (rc != DCGM_ST_OK) {
                        log_fn(LDMSD_LERROR, SAMP" dcgmConnect() failed: %s(%d)\n",
                               errorString(rc), rc);
                        return -1;
                }
        } else {
                rc = dcgmStartEmbedded(DCGM_OPERATION_MODE_AUTO, &dcgm_handle);
                if (rc != DCGM_ST_OK) {
                        log_fn(LDMSD_LERROR, SAMP" dcgmStartEmbedded() failed: %s(%d)\n",
                               errorString(rc), rc);
                        return -1;
                }
        }

        rc = dcgmGetAllSupportedDevices(dcgm_handle, gpu_ids, &gpu_ids_count);
        if (rc != DCGM_ST_OK) {
                log_fn(LDMSD_LERROR, SAMP" dcgmGetAllSupportedDevices() failed: %s(%d)\n",
                       errorString(rc), rc);
                return -1;
        }
        if (gpu_ids_count == 0) {
                log_fn(LDMSD_LERROR, SAMP" no supported gpus found\n");
                return -1;
        }

        /* Group tpye DCGM_GROUP_DEFAULT means all GPUs on the system */
        rc = dcgmGroupCreate(dcgm_handle, DCGM_GROUP_DEFAULT,
                             (char *)"ldmsd_group", &gpu_group_id);
        if (rc != DCGM_ST_OK){
                log_fn(LDMSD_LERROR, SAMP" dcgmGroupCreate failed: %s(%d)\n",
                       errorString(rc), rc);
                return -1;
        }

        rc = dcgmFieldGroupCreate(dcgm_handle, conf.fields_len, conf.fields,
                                   (char *)"ldmsd_fields", &field_group_id);
        if(rc != DCGM_ST_OK){
                log_fn(LDMSD_LERROR, SAMP" dcgmFieldGroupCreate failed: %s(%d)\n",
                       errorString(rc), rc);
                return -1;
        }

        rc = dcgmWatchFields(dcgm_handle, gpu_group_id, field_group_id,
                             conf.interval, (double)(conf.interval*3)/1000000, 50);
        if (rc != DCGM_ST_OK){
                log_fn(LDMSD_LERROR, SAMP" dcgmWatchFields failed: %s(%d)\n",
                       errorString(rc), rc);
                return -1;
        }

        dcgmUpdateAllFields(dcgm_handle, 1);

        dcgm_initialized = 1;
        return 0;
}

static void dcgm_fini()
{
        if (!dcgm_initialized)
                return;
        dcgmUnwatchFields(dcgm_handle, gpu_group_id, field_group_id);
        dcgmFieldGroupDestroy(dcgm_handle, field_group_id);
        dcgmGroupDestroy(dcgm_handle, gpu_group_id);
        if(standalone) {
                dcgmDisconnect(dcgm_handle);
        } else {
                dcgmStopEmbedded(dcgm_handle);
        }
        dcgmShutdown();
        dcgm_initialized = 0;
        /* clear translation table */
}

static ldms_set_t gpu_metric_set_create(int gpu_id)
{
        ldms_set_t set;
        char instance_name[256];

        log_fn(LDMSD_LDEBUG, SAMP" gpu_metric_set_create() (gpu %d)\n", gpu_id);

        snprintf(instance_name, sizeof(instance_name), "%s/gpu_%d",
                 producer_name, gpu_id);
        set = ldms_set_new(instance_name, gpu_schema);
        ldms_set_producer_name_set(set, producer_name);
        ldms_metric_set_s32(set, gpu_id_metric_index, gpu_id);
        ldms_set_publish(set);

        return set;
}

static void gpu_metric_set_destroy(ldms_set_t set)
{
        ldms_set_unpublish(set);
        ldms_set_delete(set);
}


static int gpu_schema_create()
{
        ldms_schema_t sch;
        int rc;
        int i;

        log_fn(LDMSD_LDEBUG, SAMP" gpu_schema_create()\n");
        sch = ldms_schema_new(conf.schema_name);
        if (sch == NULL)
                goto err1;
        jobid_helper_schema_add(sch);
        rc = ldms_schema_meta_add(sch, "gpu_id", LDMS_V_S32);
        if (rc < 0)
                goto err2;
        gpu_id_metric_index = rc;

        /* add gpu stats entries */
        for (i = 0; i < conf.fields_len; i++) {
                dcgm_field_meta_p field_meta;
                unsigned short ldms_type;

                field_meta = DcgmFieldGetById(conf.fields[i]);
                ldms_type = dcgm_to_ldms_type(field_meta->fieldType);
                rc = ldms_schema_metric_add(sch, field_meta->tag, ldms_type);
                if (rc < 0)
                        goto err2;
                translation_table[conf.fields[i]].ldms_index = rc;
                translation_table[conf.fields[i]].ldms_type = ldms_type;
        }
        gpu_schema = sch;

        return 0;
err2:
        ldms_schema_delete(sch);
err1:
        log_fn(LDMSD_LERROR, SAMP" schema creation failed\n");
        return -1;
}

static void gpu_schema_destroy()
{
        ldms_schema_delete(gpu_schema);
        gpu_schema = NULL;
}

static int gpu_sample()
{
        int i;
        int rc = 0;

        for (i = 0; i < gpu_ids_count; i++) {
                ldms_transaction_begin(gpu_sets[gpu_ids[i]]);
                jobid_helper_metric_update(gpu_sets[gpu_ids[i]]);
        }
        rc = dcgmGetLatestValues(dcgm_handle, gpu_group_id, field_group_id,
                                 &sample_cb, NULL);
        if (rc != DCGM_ST_OK){
                /* TODO */
                rc = -1;
        }
        for (i = 0; i < gpu_ids_count; i++) {
                ldms_transaction_end(gpu_sets[gpu_ids[i]]);
        }

        return rc;
}

static int parse_fields_value(const char *fields_str, unsigned short **fields_out,
                              int *fields_len_out)
{
        char *tmp_fields = NULL;
        char *tmp;
        char *saveptr;
        char *token;
        int count;
        unsigned short *fields = NULL;

        tmp_fields = strdup(fields_str);
        if (tmp_fields == NULL) {
                log_fn(LDMSD_LERROR, SAMP" parse_fields_value() strdup failed: %d", errno);
                return -1;
        }

        for (count = 0, tmp = tmp_fields; ; count++, tmp = NULL) {
                unsigned short *new_fields;

                token = strtok_r(tmp, ",", &saveptr);
                if (token == NULL)
                        break;
                new_fields = realloc(fields, sizeof(unsigned short)*(count+1));
                if (new_fields == NULL) {
                        log_fn(LDMSD_LERROR, SAMP" parse_fields_value() realloc failed: %d", errno);
                        goto err1;
                }
                fields = new_fields;
                errno = 0;
                fields[count] = strtol(token, NULL, 10);
                if (errno != 0) {
                        log_fn(LDMSD_LERROR, SAMP" parse_fields_value() conversion error: %d\n", errno);
                        goto err1;
                }
                if (fields[count] >= DCGM_FI_MAX_FIELDS) {
                        log_fn(LDMSD_LERROR, SAMP" parse_fields_value() field values must be less than %d\n",
                               DCGM_FI_MAX_FIELDS);
                        goto err1;
                }
        }

        free(tmp_fields);
        *fields_out = fields;
        *fields_len_out = count;
        return 0;

err1:
        free(tmp_fields);
        free(fields);
        return -1;
}

/**************************************************************************
 * Externally accessed functions
 **************************************************************************/

static int config(struct ldmsd_plugin *self,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        char *value;
        int rc = -1;
        int i;

        log_fn(LDMSD_LDEBUG, SAMP" config() called\n");

        jobid_helper_config(avl);
        value = av_value(avl, "interval");
        if (value == NULL) {
                log_fn(LDMSD_LERROR, SAMP" config() \"interval\" option missing\n");
                goto err0;
        }
        errno = 0;
        conf.interval = strtol(value, NULL, 10);
        if (errno != 0) {
                log_fn(LDMSD_LERROR, SAMP" config() \"interval\" value conversion error: %d\n", errno);
                goto err0;
        }

        value = av_value(avl, "schema");
        if (value != NULL) {
                conf.schema_name = strdup(value);
        } else {
                conf.schema_name = strdup("dcgm");
        }
        if (conf.schema_name == NULL) {
                log_fn(LDMSD_LERROR, SAMP" config() strdup schema failed: %d", errno);
                goto err0;
        }

        value = av_value(avl, "fields");
        if (value != NULL) {
                rc = parse_fields_value(value, &conf.fields, &conf.fields_len);
                if (rc != 0) {
                        goto err1;
                }
        } else {
                /* use defaults */
                conf.fields = malloc(sizeof(default_fields));
                if (conf.fields == NULL) {
                        log_fn(LDMSD_LERROR, SAMP" config() malloc of conf.fields failed");
                        goto err1;
                }
                memcpy(conf.fields, default_fields, sizeof(default_fields));
                conf.fields_len = sizeof(default_fields)/sizeof(default_fields[0]);
        }

        rc = dcgm_init();
        if (rc != 0)
                goto err2;
        rc = gpu_schema_create();
        if (rc != 0)
                goto err3;
        for (i = 0; i < gpu_ids_count; i++) {
                if (gpu_ids[i] > DCGM_MAX_NUM_DEVICES) {
                        log_fn(LDMSD_LERROR, SAMP" gpu id %d is greater than DCGM_MAX_NUM_DEVICES (%d), will require code fix\n");
                        goto err4;
                }
                gpu_sets[gpu_ids[i]] = gpu_metric_set_create(gpu_ids[i]);
        }

        return 0;

err4:
        for (i = i-1; i >= 0; i--) {
                gpu_metric_set_destroy(gpu_sets[gpu_ids[i]]);
        }
        gpu_schema_destroy();
err3:
        dcgm_fini();
err2:
        free(conf.fields);
        conf.fields = NULL;
        conf.fields_len = 0;
        conf.interval = 0;
err1:
        free(conf.schema_name);
        conf.schema_name = NULL;
err0:
        return rc;
}

static int sample(struct ldmsd_sampler *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" sample() called\n");
        gpu_sample();
        return 0;
}

static void term(struct ldmsd_plugin *self)
{
        int i;

        free(conf.schema_name);
        conf.schema_name = NULL;
        free(conf.fields);
        conf.fields = NULL;
        conf.fields_len = 0;
        conf.interval = 0;
        for (i = 0; i < gpu_ids_count; i++) {
                gpu_metric_set_destroy(gpu_sets[gpu_ids[i]]);
        }
        gpu_schema_destroy();
        dcgm_fini();
        log_fn(LDMSD_LDEBUG, SAMP" term() called\n");
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static const char *usage(struct ldmsd_plugin *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" usage() called\n");
	return  "config name=" SAMP;
}

static struct ldmsd_sampler nvidia_dcgm_plugin = {
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

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
        log_fn = pf;
        log_fn(LDMSD_LDEBUG, SAMP" get_plugin() called ("PACKAGE_STRING")\n");
        gethostname(producer_name, sizeof(producer_name));

        return &nvidia_dcgm_plugin.base;
}
