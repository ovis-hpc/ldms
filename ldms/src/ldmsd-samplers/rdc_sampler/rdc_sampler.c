/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2021, Advanced Micro Devices, Inc. All rights reserved.
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
 * \file rdc_sampler.c
 *
 * This sampler uses \c rdc interface to collect AMD GPU metrics.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <rdc/rdc.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

/* TYPES */

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

typedef struct rdcinfo_inst_s *rdcinfo_inst_t;
struct rdcinfo_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
	rdc_handle_t rdc_handle;
	rdc_gpu_group_t group_id;
	rdc_field_grp_t field_group_id;
	rdc_group_info_t group_info;
	rdc_field_group_info_t field_info;
};

/* FUNCTIONS */

static char* field_id_metrics(rdc_field_t field_id)
{
	switch(field_id) {
	case RDC_FI_GPU_CLOCK:
	case RDC_FI_MEM_CLOCK:
		return "Hz";
	case RDC_FI_MEMORY_TEMP:
	case RDC_FI_GPU_TEMP:
		return "millideg";
	case RDC_FI_POWER_USAGE:
		return "microwatt";
	case RDC_FI_PCIE_TX:
	case RDC_FI_PCIE_RX:
		return "Bps";
	case RDC_FI_GPU_UTIL:
		return "%";
	case RDC_FI_GPU_MEMORY_USAGE:
	case RDC_FI_GPU_MEMORY_TOTAL:
		return "bytes";
	case RDC_FI_ECC_CORRECT_TOTAL:
	case RDC_FI_ECC_UNCORRECT_TOTAL:
		return "";
	default:
		return "";
	}

	return "";
}

/* ============== Sampler Plugin APIs ================= */

static
int rdcinfo_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	rdcinfo_inst_t inst = (void*)pi;
	rdc_status_t result;
	char name[512];
	int rc;

	/* For each GPU and each metric */
	for (uint32_t gindex = 0; gindex < inst->group_info.count; gindex++) {
		for (uint32_t findex = 0; findex < inst->field_info.count; findex++) {
			snprintf(name, sizeof(name), "gpu%d:%s", 
				inst->group_info.entity_ids[gindex], 
				field_id_string(inst->field_info.field_ids[findex]));
			rc = ldms_schema_metric_add(schema, name, LDMS_V_U64, 
				field_id_metrics(inst->field_info.field_ids[findex]));
			if (rc < 0)
				return -rc; /* rc == -errno */
		}
	}

	return 0;
}

static
int rdcinfo_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	rdcinfo_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int i = 0;
	rdc_status_t result;

	for (uint32_t gindex = 0; gindex < inst->group_info.count; gindex++) {
		for (uint32_t findex = 0; findex < inst->field_info.count; findex++, i++) {
			rdc_field_value value;
			result = rdc_field_get_latest_value(inst->rdc_handle,
					inst->group_info.entity_ids[gindex], inst->field_info.field_ids[findex], &value);
			if (result != RDC_ST_OK) {
				ldmsd_log(LDMSD_LWARNING, "%s:Failed to get (gpu %d: field: %d): %s\n",
					pi->inst_name, inst->group_info.entity_ids[gindex],
					inst->field_info.field_ids[findex], rdc_status_string(result));
				continue;
			}
			ldms_metric_set_u64(set, samp->first_idx + i, value.value.l_int);
		}
	}

	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *rdcinfo_desc(ldmsd_plugin_inst_t pi)
{
	return "rdc_sampler - sampler plugin for RDC data";
}

static
char *_help = "\
rdc_sampler config synopsis: \
    config name=INST [COMMON_OPTIONS] [metrics=METRICS] \n\
\n\
Option descriptions:\n\
    metrics   The comma-separated list of metrics to monitor.\n\
              The default is "" (empty), which is equivalent to monitor ALL\n\
              metrics.\n\
\n\
The rdc_sampler collects AMD GPU data according to the given 'metrics' option.\n\
For example metrics=RDC_FI_GPU_CLOCK,RDC_FI_GPU_TEMP,RDC_FI_POWER_USAGE,RDC_FI_GPU_MEMORY_USAGE\n\
\n\
";

static
const char *rdcinfo_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void rdcinfo_del(ldmsd_plugin_inst_t pi)
{
	rdcinfo_inst_t inst = (void*)pi;

	INST_LOG(inst, LDMSD_LINFO, "shutdown rdc\n");
	if (inst->rdc_handle == NULL)
		return;

	/* The undo of rdcinfo_init */
	rdc_status_t result;
	result = rdc_field_unwatch(inst->rdc_handle, inst->group_id, inst->field_group_id);
	result = rdc_group_field_destroy(inst->rdc_handle, inst->field_group_id);
	result = rdc_group_gpu_destroy(inst->rdc_handle, inst->group_id);
	rdc_stop_embedded(inst->rdc_handle);
	rdc_shutdown();
}

static
int rdcinfo_config_find_int_value(ldmsd_plugin_inst_t pi,
		json_entity_t json, char *attribute)
{
	rdcinfo_inst_t inst = (void*)pi;
	json_entity_t jval = json_value_find(json, attribute);

	if (!jval) {
		INST_LOG(inst, LDMSD_LDEBUG, "%s: %s attribute required\n",
				 pi->inst_name, attribute);
		return -EINVAL;
	}
	return strtol(jval->value.str_->str, NULL, 10);
}

static
int rdcinfo_config(ldmsd_plugin_inst_t pi, json_entity_t json,
			char *ebuf, int ebufsz)
{
	rdcinfo_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;

	/* default to all metrics */
	rdc_field_t field_ids[] = {
		RDC_FI_GPU_CLOCK,
		RDC_FI_MEM_CLOCK,
		RDC_FI_MEMORY_TEMP,
		RDC_FI_GPU_TEMP,
		RDC_FI_POWER_USAGE,
		RDC_FI_PCIE_TX,
		RDC_FI_PCIE_RX,
		RDC_FI_GPU_UTIL,
		RDC_FI_GPU_MEMORY_USAGE,
		RDC_FI_GPU_MEMORY_TOTAL,
		RDC_FI_ECC_CORRECT_TOTAL,
		RDC_FI_ECC_UNCORRECT_TOTAL,
		RDC_EVNT_XGMI_0_THRPUT,
		RDC_EVNT_XGMI_1_THRPUT
	};
	uint32_t num_fields_max = sizeof(field_ids)/sizeof(field_ids[0]);
	uint32_t num_fields = 0;
	json_entity_t jval;
	rdc_status_t result;
	uint32_t update_freq;
	uint32_t max_keep_age;
	uint32_t max_keep_samples;

	if (inst->rdc_handle) {
		snprintf(ebuf, ebufsz, "%s: plugin already configured.\n",
			pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */
	jval = json_value_find(json, "metrics");
	if (!jval) {
		snprintf(ebuf, ebufsz, "%s: `metrics` attribute required\n",
				 pi->inst_name);
		return EINVAL;
	} else {
		char *tkn, *ptr;

		INST_LOG(inst, LDMSD_LDEBUG, "metrics=%s.\n", jval->value.str_->str);
		tkn = strtok_r(jval->value.str_->str, ",", &ptr);
		while (tkn) {
			rdc_field_t cur_field_id = get_field_id_from_name(tkn);
			INST_LOG(inst, LDMSD_LDEBUG, "field %d:%s.\n", cur_field_id, tkn);
			if (cur_field_id != RDC_FI_INVALID) {
				if (num_fields >= num_fields_max) {
					snprintf(ebuf, ebufsz, "exceed the max fields, check configure file...\n");
					return -1;
				}
				field_ids[num_fields++]= cur_field_id;
			}
			tkn = strtok_r(NULL, ",", &ptr);
		}
	}

	rc = rdcinfo_config_find_int_value(pi, json, "update_freq");
	if ((rc == -EINVAL) || (rc == LONG_MAX) || (rc == LONG_MIN))
		update_freq = 1000000;
	else
		update_freq = rc;
	INST_LOG(inst, LDMSD_LDEBUG, "update_freq=%d.\n", update_freq);

	rc = rdcinfo_config_find_int_value(pi, json, "max_keep_age");
	if ((rc == -EINVAL) || (rc == LONG_MAX) || (rc == LONG_MIN))
		max_keep_age = 60;
	else
		max_keep_age = rc;
	INST_LOG(inst, LDMSD_LDEBUG, "max_keep_age=%d.\n", max_keep_age);

	rc = rdcinfo_config_find_int_value(pi, json, "max_keep_samples");
	if ((rc == -EINVAL) || (rc == LONG_MAX) || (rc == LONG_MIN))
		max_keep_samples = 10;
	else
		max_keep_samples = rc;
	INST_LOG(inst, LDMSD_LDEBUG, "max_keep_samples=%d.\n", max_keep_samples);

	/* rdc specific initialization */
	result = rdc_init(0);
	result = rdc_start_embedded(RDC_OPERATION_MODE_AUTO, &(inst->rdc_handle));
	if (result != RDC_ST_OK) {
		snprintf(ebuf, ebufsz, "Failed to start rdc in embedded mode: %s.\n",
			rdc_status_string(result));
		return result;
	}

	/* Create the group for all GPUs */
	result = rdc_group_gpu_create(inst->rdc_handle, RDC_GROUP_DEFAULT,
			"rdc_ldms_group", &(inst->group_id));
	if (result != RDC_ST_OK) {
		snprintf(ebuf, ebufsz, "Failed to create the group: %s.\n",
			rdc_status_string(result));
		return result;
	}

	/* Create the field group */
	result = rdc_group_field_create(inst->rdc_handle, num_fields ,
			&field_ids[0], "rdc_ldms_field_group", &(inst->field_group_id));
	if (result != RDC_ST_OK) {
		snprintf(ebuf, ebufsz, "Failed to create the field group: %s.\n",
			rdc_status_string(result));
		return result;
	}

	/* Get the group info and field info */
	result = rdc_group_gpu_get_info(inst->rdc_handle, inst->group_id, &(inst->group_info));
	if (result != RDC_ST_OK) {
		snprintf(ebuf, ebufsz, "Failed to get gpu group info: %s.\n",
			rdc_status_string(result));
		return result;
	}
	result = rdc_group_field_get_info(inst->rdc_handle, inst->field_group_id, &(inst->field_info));
	if (result != RDC_ST_OK) {
		snprintf(ebuf, ebufsz, "Failed to get field group info: %s.\n",
			rdc_status_string(result));
		return result;
	}

	result = rdc_field_watch(inst->rdc_handle,
			inst->group_id,
			inst->field_group_id,
			update_freq,
			max_keep_age,
			max_keep_samples);
	if (result != RDC_ST_OK) {
		snprintf(ebuf, ebufsz, "Failed to watch the field group: %s.\n",
			rdc_status_string(result));
		return result;
	}

	/* RDC need to fetch the metrics into cache, which will take at least one update. */
	usleep(update_freq);

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;
	return 0;
}

static
int rdcinfo_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	rdcinfo_inst_t inst = (void*)pi;
	/* override update_schema() and update_set() */
	samp->update_schema = rdcinfo_update_schema;
	samp->update_set = rdcinfo_update_set;

	/* NOTE More initialization code here if needed */
	inst->rdc_handle = NULL;
	return 0;
}

static
struct rdcinfo_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "rdc_sampler",

		/* Common Plugin APIs */
		.desc   = rdcinfo_desc,
		.help   = rdcinfo_help,
		.init   = rdcinfo_init,
		.del    = rdcinfo_del,
		.config = rdcinfo_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	rdcinfo_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
