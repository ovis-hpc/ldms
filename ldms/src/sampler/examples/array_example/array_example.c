/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011-2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2011-2016,2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file array_example.c
 * \brief Example of an ldmsd sampler plugin that uses array metric.
 */
#include <errno.h>
#include <stdlib.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include <stdlib.h>
#include "sampler_base.h"
#define SAMP "array_example"

#define array_metric_name_base "array_"
#define array_num_ele_default 10

static int metric_offset;
static ldms_set_t set;
static base_data_t base;

static ovis_log_t mylog;

struct array_construct {
	const char *name;
	enum ldms_value_type type;
	int n;
};

struct array_construct array_contruct_entries[] = {
	{"u8_array", LDMS_V_U8_ARRAY, 10},
	{"s8_array", LDMS_V_S8_ARRAY, 10},
	{"u16_array", LDMS_V_U16_ARRAY, 10},
	{"s16_array", LDMS_V_S16_ARRAY, 10},
	{"u32_array", LDMS_V_U32_ARRAY, 10},
	{"s32_array", LDMS_V_S32_ARRAY, 10},
	{"u64_array", LDMS_V_U64_ARRAY, 10},
	{"s64_array", LDMS_V_S64_ARRAY, 10},
	{"float_array", LDMS_V_F32_ARRAY, 10},
	{"double_array", LDMS_V_D64_ARRAY, 10},
	{NULL, LDMS_V_NONE, 10},
};

static struct array_construct *metric_list;

static int create_metric_set(int num_metrics, int num_ele, enum ldms_value_type type,
			     base_data_t base)
{
	ldms_schema_t schema;
	int rc;

	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/* Location of first metric */
	metric_offset = ldms_schema_metric_count_get(schema);

	struct array_construct *ent;
	if (num_metrics < 0) {
		metric_list = array_contruct_entries;
		ent = &metric_list[0];
			while (ent->name) {
				rc = ldms_schema_metric_array_add(schema, ent->name, ent->type, ent->n);
				if (rc < 0) {
					rc = ENOMEM;
					goto err;
				}
				ent++;
			}
	} else {
		metric_list = calloc(num_metrics + 1, sizeof(*ent));
		if (!metric_list) {
			rc = ENOMEM;
			goto err;
		}
		if (num_ele < 0)
			num_ele = array_num_ele_default;
		if (type == LDMS_V_NONE)
			type = LDMS_V_U64;

		char name[128];
		int i;
		for (i = 0; i < num_metrics; i++) {
			ent = &metric_list[i];
			snprintf(name, 128, "%s%d", array_metric_name_base, i);
			ent->name = strdup(name);
			ent->n = num_ele;
			ent->type = type;
			rc = ldms_schema_metric_array_add(schema, ent->name,
					ent->type, ent->n);
			if (rc < 0) {
				rc = ENOMEM;
				goto err;
			}
		}
		metric_list[num_metrics].name = NULL;
		metric_list[num_metrics].n = num_ele;
		metric_list[num_metrics].type = LDMS_V_NONE;
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

 err:
	return rc;
}


static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int num_metrics;
	int num_ele;
	enum ldms_value_type type;
	int rc;


	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	value = av_value(avl, "num_metrics");
	if (value)
		num_metrics = atoi(value);
	else
		num_metrics = -1;
	value = av_value(avl, "num_ele");
	if (value)
		num_ele = atoi(value);
	else
		num_ele = -1;
	value = av_value(avl, "type");
	if (value)
		type = ldms_metric_str_to_type(value);
	else
		type = LDMS_V_NONE;

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base){
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(num_metrics, num_ele, type, base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}
	return 0;

err:
	base_del(base);
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{

	int i, mid;
	static uint8_t off = 0;
	struct array_construct *ent = metric_list;
	union ldms_value v;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	mid = metric_offset;
	while (ent->name) {
		for (i = 0; i < ent->n; i++) {
			switch (ent->type) {
			case LDMS_V_S8_ARRAY:
				v.v_s8 = -i + off;
				break;
			case LDMS_V_U8_ARRAY:
				v.v_u8 = i + off;
				break;
			case LDMS_V_S16_ARRAY:
				v.v_s16 = -i + off;
				break;
			case LDMS_V_U16_ARRAY:
				v.v_u16 = i + off;
				break;
			case LDMS_V_S32_ARRAY:
				v.v_s32 = -i + off;
				break;
			case LDMS_V_U32_ARRAY:
				v.v_u32 = i + off;
				break;
			case LDMS_V_S64_ARRAY:
				v.v_s64 = -i + off;
				break;
			case LDMS_V_U64_ARRAY:
				v.v_u64 = i + off;
				break;
			case LDMS_V_F32_ARRAY:
				v.v_f = i/10.0 + off;
				break;
			case LDMS_V_D64_ARRAY:
				v.v_d = i/10.0 + off;
				break;
			default:
				v.v_u64 = 0;
			}
			ldms_metric_array_set_val(set, mid, i, &v);
		}
		mid++;
		ent++;
	}
	off++;
	base_sample_end(base);
	return 0;
}

static void term(ldmsd_plug_handle_t handle)
{
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static const char *usage(ldmsd_plug_handle_t handle)
{

	return "config name=array_example [num_metrics=<num_metrics>] [num_ele=<num_ele>] [type=<type>]\n" \
		BASE_CONFIG_USAGE \
		"    <num_metrics>    The number of metrics in the schema\n"
		"    <num_ele>    The number of elements in each array. All arrays have the same number of elements.\n"
		"    <type>       The type of metric arrays, e.g., U64_ARRAY, U8_ARRAY, etc\n";
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
	set = NULL;

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
