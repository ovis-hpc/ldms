/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#include <stdlib.h>
#include <stdbool.h>
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
#include "ldmsd_plug_api.h"
#include <dcgm_agent.h>
#include "config.h"
#include "jobid_helper.h"
#include "sampler_base.h"
#include "dstring.h"

#define _GNU_SOURCE

#define SAMP "dcgm_sampler"

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

static ovis_log_t mylog;

static bool dcgm_initialized = false;
static bool sampler_configured = false;
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
static base_data_t base;
static char *field_help;

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
                        ovis_log(mylog, OVIS_LERROR, "data type mismatch, "
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
                        ovis_log(mylog, OVIS_LERROR, "unexpected data type, field=%d, received=%d\n",
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
                ovis_log(mylog, OVIS_LERROR, "dcgmInit() failed: %s(%d)\n",
                       errorString(rc), rc);
                return -1;
        }

        if (standalone) {
                rc = dcgmConnect(host_ip, &dcgm_handle);
                if (rc != DCGM_ST_OK) {
                        ovis_log(mylog, OVIS_LERROR, "dcgmConnect() failed: %s(%d) (is DCGM's nv-hostengine daemon running?)\n",
                               errorString(rc), rc);
                        return -1;
                }
        } else {
                rc = dcgmStartEmbedded(DCGM_OPERATION_MODE_AUTO, &dcgm_handle);
                if (rc != DCGM_ST_OK) {
                        ovis_log(mylog, OVIS_LERROR, "dcgmStartEmbedded() failed: %s(%d)\n",
                               errorString(rc), rc);
                        return -1;
                }
        }

        rc = dcgmGetAllSupportedDevices(dcgm_handle, gpu_ids, &gpu_ids_count);
        if (rc != DCGM_ST_OK) {
                ovis_log(mylog, OVIS_LERROR, "dcgmGetAllSupportedDevices() failed: %s(%d)\n",
                       errorString(rc), rc);
                return -1;
        }
        if (gpu_ids_count == 0) {
                ovis_log(mylog, OVIS_LERROR, "no supported gpus found\n");
                return -1;
        }

        /* Group tpye DCGM_GROUP_DEFAULT means all GPUs on the system */
        rc = dcgmGroupCreate(dcgm_handle, DCGM_GROUP_DEFAULT,
                             (char *)"ldmsd_group", &gpu_group_id);
        if (rc != DCGM_ST_OK){
                ovis_log(mylog, OVIS_LERROR, "dcgmGroupCreate failed: %s(%d)\n",
                       errorString(rc), rc);
                return -1;
        }

        rc = dcgmFieldGroupCreate(dcgm_handle, conf.fields_len, conf.fields,
                                   (char *)"ldmsd_fields", &field_group_id);
        if(rc != DCGM_ST_OK){
                ovis_log(mylog, OVIS_LERROR, "dcgmFieldGroupCreate failed: %s(%d)\n",
                       errorString(rc), rc);
                return -1;
        }

        rc = dcgmWatchFields(dcgm_handle, gpu_group_id, field_group_id,
                             conf.interval, (double)(conf.interval*3)/1000000, 50);
        if (rc != DCGM_ST_OK){
                ovis_log(mylog, OVIS_LERROR, "dcgmWatchFields failed: %s(%d)\n",
                       errorString(rc), rc);
                return -1;
        }

        dcgmUpdateAllFields(dcgm_handle, 1);

        dcgm_initialized = true;
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
        dcgm_initialized = false;
}

static ldms_set_t gpu_metric_set_create(int gpu_id)
{
        ldms_set_t set;
        char instance_name[256];

        ovis_log(mylog, OVIS_LDEBUG, "gpu_metric_set_create() (gpu %d)\n", gpu_id);
	if (base) {
		char *tmp = base->instance_name;
		size_t len = strlen(tmp);
		base->instance_name = malloc( len + 20);
		if (!base->instance_name) {
			base->instance_name = tmp;
			ovis_log(mylog, OVIS_LERROR, "out of memory\n");
			return NULL;
		}
		/* append gpu_# to user-defined instance name */
		snprintf(base->instance_name, len + 20, "%s/gpu_%d",  tmp, gpu_id);
		/* override single set assumed in sampler_base api */
		set = base_set_new(base);
		if (!set) {
			ovis_log(mylog, OVIS_LERROR, "failed to make %s set for %s\n",
				base->instance_name, SAMP);
			base->instance_name = tmp;
			return set;
		}
		base_auth_set(&base->auth, set);
		base->set = NULL;
		free(base->instance_name);
		base->instance_name = tmp;
	} else {
		snprintf(instance_name, sizeof(instance_name), "%s/gpu_%d",
			 producer_name, gpu_id);
		set = ldms_set_new(instance_name, gpu_schema);
		ldms_set_producer_name_set(set, producer_name);
		ldms_set_publish(set);
		ldmsd_set_register(set, SAMP);
	}
	ldms_metric_set_s32(set, gpu_id_metric_index, gpu_id);

        return set;
}

static void gpu_metric_set_destroy(ldms_set_t set)
{
        ldmsd_set_deregister(ldms_set_instance_name_get(set), SAMP);
        ldms_set_unpublish(set);
        ldms_set_delete(set);
}


static int gpu_schema_create()
{
        ldms_schema_t sch;
        int rc;
        int i;

        ovis_log(mylog, OVIS_LDEBUG, "gpu_schema_create()\n");
	if (!base) {
		sch = ldms_schema_new(conf.schema_name);
		if (sch == NULL)
			goto err1;
		rc = jobid_helper_schema_add(sch);
		if (rc < 0)
			goto err2;
	} else {
		sch = base_schema_new(base);
		if (sch == NULL)
			goto err1;
	}
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
                if (ldms_type == LDMS_V_NONE) {
                        ovis_log(mylog, OVIS_LERROR,
                                 "DCGM field %d has a DCGM type %d, which is not supported by this sampler\n",
                                 conf.fields[i], field_meta->fieldType);
                        goto err2;
                }
                if (ldms_type == LDMS_V_CHAR_ARRAY) {
                        rc = ldms_schema_metric_array_add(sch, field_meta->tag,
                                                          ldms_type, field_meta->valueFormat->width+1);
                } else {
                        rc = ldms_schema_metric_add(sch, field_meta->tag, ldms_type);
                }
                if (rc < 0) {
                        ovis_log(mylog, OVIS_LERROR,
                                 "failed adding ldms metric to set for DCGM field %d, DCGM type %d\n",
                                 conf.fields[i], field_meta->fieldType);
                        goto err2;
                }
                translation_table[conf.fields[i]].ldms_index = rc;
                translation_table[conf.fields[i]].ldms_type = ldms_type;
        }
        gpu_schema = sch;

        return 0;
err2:
	if (base)
		base_schema_delete(base);
	else
		ldms_schema_delete(sch);
err1:
        ovis_log(mylog, OVIS_LERROR, "schema creation failed.\n");
        return -1;
}

static void gpu_schema_destroy()
{
	if (base)
		base_schema_delete(base);
	else
		ldms_schema_delete(gpu_schema);
        gpu_schema = NULL;
}

static int gpu_sample()
{
        int i;
        int rc = DCGM_ST_OK;

	ldms_set_t set_old = base->set; /* this should be null */

        for (i = 0; i < gpu_ids_count; i++) {
		if (base) {
			base->set = gpu_sets[gpu_ids[i]];
			base_sample_begin(base);
			base->set = set_old;
		} else {
			ldms_transaction_begin(gpu_sets[gpu_ids[i]]);
			jobid_helper_metric_update(gpu_sets[gpu_ids[i]]);
		}
        }
        rc = dcgmGetLatestValues(dcgm_handle, gpu_group_id, field_group_id,
                                 &sample_cb, NULL);
        if (rc != DCGM_ST_OK){
                ovis_log(mylog, OVIS_LERROR, SAMP" failed dcgmGetLatestValues(): %d\n", rc);
                rc = -1;
        }
        for (i = 0; i < gpu_ids_count; i++) {
		if (base) {
			base->set = gpu_sets[gpu_ids[i]];
			base_sample_end(base);
			base->set = set_old;
		} else {
			ldms_transaction_end(gpu_sets[gpu_ids[i]]);
		}
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
                ovis_log(mylog, OVIS_LERROR, "parse_fields_value() strdup failed: %d", errno);
                return -1;
        }

        for (count = 0, tmp = tmp_fields; ; count++, tmp = NULL) {
                unsigned short *new_fields;

                token = strtok_r(tmp, ",", &saveptr);
                if (token == NULL)
                        break;
                new_fields = realloc(fields, sizeof(unsigned short)*(count+1));
                if (new_fields == NULL) {
                        ovis_log(mylog, OVIS_LERROR, "parse_fields_value() realloc failed: %d", errno);
                        goto err1;
                }
                fields = new_fields;
                errno = 0;
                fields[count] = strtol(token, NULL, 10);
                if (errno != 0) {
                        ovis_log(mylog, OVIS_LERROR, "parse_fields_value() conversion error: %d\n", errno);
                        goto err1;
                }
                if (fields[count] >= DCGM_FI_MAX_FIELDS) {
                        ovis_log(mylog, OVIS_LERROR, "parse_fields_value() field values must be less than %d\n",
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

const char *typeString(int ft)
{
	switch (ft) {
	case DCGM_FT_DOUBLE:
		return "double";
	case DCGM_FT_INT64:
		return "int64_t";
	case DCGM_FT_STRING:
		return "string";
	case DCGM_FT_TIMESTAMP:
		return "timestamp";
	default:
		return "unsupported_data_type";
	}
}

#define NUSAGE 20480
static void init_field_help(char *preamble)
{
	if (!dcgm_initialized) {
		dcgmReturn_t rc = dcgmInit();
		if (rc != DCGM_ST_OK) {
			return;
		}
	}

	dstring_t ds;
	dstr_init2(&ds, NUSAGE);

	int i;
	dstrcat(&ds, preamble, DSTRING_ALL);
	dstrcat(&ds, "field_id\ttag/metric\t\ttype\t(units)\n", DSTRING_ALL);
        for (i = 0; i < DCGM_FI_MAX_FIELDS; i++) {
                dcgm_field_meta_p field_meta;
                field_meta = DcgmFieldGetById(i);
		if (field_meta) {
			dstrcat_int(&ds, (int64_t)field_meta->fieldId);
			dstrcat(&ds, "\t", 1);
			dstrcat(&ds, field_meta->tag, DSTRING_ALL);
			dstrcat(&ds, "\t", 1);
			dstrcat(&ds, typeString(field_meta->fieldType), DSTRING_ALL);
			dstrcat(&ds, "\t(", 2);
			dstrcat(&ds, (field_meta->valueFormat ?
                                        field_meta->valueFormat->unit :
                                        "no_format"), DSTRING_ALL);
			dstrcat(&ds, ")\n", 2);
		}
        }
	field_help = dstr_extract(&ds);
	dstr_free(&ds);

	if (!dcgm_initialized) {
		dcgmShutdown();
	}
}

/**************************************************************************
 * Externally accessed functions
 **************************************************************************/

static int config(ldmsd_plug_handle_t handle,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        char *value;
        int rc = -1;
        int i;

        ovis_log(mylog, OVIS_LDEBUG, "config() called\n");
	if (sampler_configured) {
		ovis_log(mylog, OVIS_LERROR, "config() called twice. Stop it first.\n");
		return EINVAL;
	}
	int use_base = 0;
        value = av_value(avl, "use_base");
        if (value != NULL) {
		use_base = 1;
		ovis_log(mylog, OVIS_LDEBUG, "Using sampler_base\n");
	} else {
		ovis_log(mylog, OVIS_LDEBUG, "Ignoring sampler_base\n");
	}

        value = av_value(avl, "interval");
        if (value == NULL) {
                ovis_log(mylog, OVIS_LERROR, "config() \"interval\" option missing\n");
                goto err0;
        }
        errno = 0;
        conf.interval = strtol(value, NULL, 10);
        if (errno != 0) {
                ovis_log(mylog, OVIS_LERROR, "config() \"interval\" value conversion error: %d\n", errno);
                goto err0;
        }

	if (! use_base) {
		int jc = jobid_helper_config(avl);
		if (jc) {
			ovis_log(mylog, OVIS_LERROR, "set name for job_set="
				" is too long.\n");
			rc = jc;
			goto err0;
		}
		value = av_value(avl, "schema");
		if (value != NULL) {
		        conf.schema_name = strdup(value);
		} else {
		        conf.schema_name = strdup("dcgm");
		}
		if (conf.schema_name == NULL) {
		        ovis_log(mylog, OVIS_LERROR, "config() strdup schema failed: %d", errno);
		        goto err0;
		}
	} else {
		base = base_config(avl, ldmsd_plug_cfg_name_get(handle), "dcgm", mylog);
		conf.schema_name = strdup(base->schema_name);
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
                        ovis_log(mylog, OVIS_LERROR, "config() malloc of conf.fields failed");
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
                        ovis_log(mylog, OVIS_LERROR,
				"gpu id %d is greater than DCGM_MAX_NUM_DEVICES (%d), will require code fix\n",
				i , DCGM_MAX_NUM_DEVICES);
                        goto err4;
                }
                gpu_sets[gpu_ids[i]] = gpu_metric_set_create(gpu_ids[i]);
        }
        sampler_configured = true;

        return 0;

err4:
        for (i = i-1; i >= 0; i--) {
                gpu_metric_set_destroy(gpu_sets[gpu_ids[i]]);
        }
	gpu_schema_destroy();
	if (base) {
		free(base->instance_name);
                base->instance_name = NULL;
                base_del(base);
                base = NULL;
	}
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

static int sample(ldmsd_plug_handle_t handle)
{
	ovis_log(mylog, OVIS_LDEBUG, SAMP" sample() called\n");
        if (!sampler_configured) {
                ovis_log(mylog, OVIS_LERROR, SAMP" sampler has not been configured\n");
                return -1;
        }
        if (!dcgm_initialized) {
                dcgm_init();
        }
        if (dcgm_initialized) {
                int rc;
                rc = gpu_sample();
                if (rc != DCGM_ST_OK) {
                        /* assume catastrophic error */
                        dcgm_fini();
                }
        }

        return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
        ovis_log(mylog, OVIS_LDEBUG, "usage() called\n");
	char *preamble = "config name=" SAMP
	" interval=<interval(us)> [fields=<fields>]\n"
	" [schema=<schema_name>] [job_set=<metric set name>]\n"
	" [use_base=<*>\n"
	"   [uid=<int>] [gid=<int>] [perm=<octal>] [instance=<name>]\n"
	"    [producer=<name>] [job_id=<metric name in job_set set>]\n"
	" ]\n"
	" name=<plugin_name>\n"
	" interval=<interval(us)> DCGM query interval (microsecond)\n"
	"         must match dcgm_sampler interval for plugin start\n"
	" fields=<fields>  list of DCGM field_ids\n"
	" schema=<schema_name> default " SAMP "\n"
	" job_set=<job metric set name>\n"
	" If use_base=<*> is given, the additional parameters are applied\n"
	" (see ldms_sampler_base).\n"
	"    producer     A unique name for the host providing the timing data\n"
	"                 (default $HOSTNAME)\n"
	"    instance     A unique name for the timing metric set\n"
	"                 (default $producer/" SAMP "/gpu_X)\n"
	"    component_id A unique number for the component being monitoring.\n"
	"                 (default 0)\n"
	"    schema       The base name of the port metrics schema.\n"
	"                 (default " SAMP ".\n"
	"    uid          The user-id of the set's owner\n"
	"    gid          The group id of the set's owner\n"
	"    perm         The set's access permissions\n"
	" The field numbers are tabulated:\n"
	" (Not all can be ldms metrics, as indicated by 'unsupported_data_type')\n";
	if (!field_help)
		init_field_help(preamble);
	return field_help ? field_help : preamble;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
	gethostname(producer_name, sizeof(producer_name));

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	int i;

	ovis_log(mylog, OVIS_LDEBUG, "term() called\n");
        gpu_schema_destroy();
	if (base) {
		free(base->instance_name);
		base->instance_name = NULL;
		base_del(base);
		base = NULL;
	}
	free(conf.schema_name);
	conf.schema_name = NULL;
        free(conf.fields);
        conf.fields = NULL;
        conf.fields_len = 0;
        conf.interval = 0;
        for (i = 0; i < gpu_ids_count; i++) {
                gpu_metric_set_destroy(gpu_sets[gpu_ids[i]]);
        }
        dcgm_fini();
	free(field_help);
	field_help = NULL;
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
