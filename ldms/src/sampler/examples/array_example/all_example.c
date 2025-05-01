/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016-2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file all_example.c
 * \brief Example of an ldmsd sampler plugin that uses all metric types,
 * useful for testing store formats.
 */
#include <errno.h>
#include <stdlib.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "sampler_base.h"


#define SAMP "all_example"
#define array_metric_name_base "array_"
#define array_num_ele_default 3

static ldms_set_t set;
static int metric_offset;
static base_data_t base;

static ovis_log_t mylog;

struct metric_construct {
	const char *name;
	enum ldms_value_type type;
	int n;
};

struct metric_construct metric_construct_entries[] = {
	{"char", LDMS_V_CHAR, 1},
	{"u8", LDMS_V_U8, 1},
	{"s8", LDMS_V_S8, 1},
	{"u16", LDMS_V_U16, 1},
	{"s16", LDMS_V_S16, 1},
	{"u32", LDMS_V_U32, 1},
	{"s32", LDMS_V_S32, 1},
	{"u64", LDMS_V_U64, 1},
	{"s64", LDMS_V_S64, 1},
	{"f32", LDMS_V_F32, 1},
	{"d32", LDMS_V_D64, 1},
	{"char_array", LDMS_V_CHAR_ARRAY, array_num_ele_default},
	{"u8_array", LDMS_V_U8_ARRAY, array_num_ele_default},
	{"s8_array", LDMS_V_S8_ARRAY, array_num_ele_default},
	{"u16_array", LDMS_V_U16_ARRAY, array_num_ele_default},
	{"s16_array", LDMS_V_S16_ARRAY, array_num_ele_default},
	{"u32_array", LDMS_V_U32_ARRAY, array_num_ele_default},
	{"s32_array", LDMS_V_S32_ARRAY, array_num_ele_default},
	{"u64_array", LDMS_V_U64_ARRAY, array_num_ele_default},
	{"s64_array", LDMS_V_S64_ARRAY, array_num_ele_default},
	{"float_array", LDMS_V_F32_ARRAY, array_num_ele_default},
	{"double_array", LDMS_V_D64_ARRAY, array_num_ele_default},
	{"char_end", LDMS_V_CHAR, 1},
	{NULL, LDMS_V_NONE, array_num_ele_default},
};

static struct metric_construct *metric_list;

static int num_metrics = -1;
static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	int rc;


	schema = base_schema_new(base);
	if (!schema){
		rc = errno;
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		goto err;
	}


	struct metric_construct *ent;
	if (num_metrics >= 0)
		return EINVAL; // does not support multiple configuration/instances.

	/* Location of first metric */
	metric_offset = ldms_schema_metric_count_get(schema);

	metric_list = metric_construct_entries;
	ent = &metric_list[0];
	while (ent->name) {
		ovis_log(mylog, OVIS_LDEBUG, "adding %s\n", ent->name);
		if (ent->type >= LDMS_V_CHAR_ARRAY)
			rc = ldms_schema_metric_array_add(schema, ent->name, ent->type, ent->n+1);
		else
			rc = ldms_schema_metric_add(schema, ent->name, ent->type);
		if (rc < 0) {
			ovis_log(mylog, OVIS_LERROR, "adding %s failed\n",
				ent->name);
			rc = ENOMEM;
			goto err;
		}
		ent++;
		ovis_log(mylog, OVIS_LDEBUG, "ok\n");
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		ovis_log(mylog, OVIS_LERROR, "set new failed.\n");
		goto err;
	}

	return 0;

 err:
	return rc;
}
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;
	if (set) {
		ovis_log(mylog, OVIS_LERROR, "all_example: Set already created.\n");
		return EINVAL;
	}

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base) {
		rc = errno;
		goto err;
	}

	rc = create_metric_set(base);
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
	static uint8_t off = 76; // ascii L
	struct metric_construct *ent = metric_list;
	union ldms_value v;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINTR;
	}

	base_sample_begin(base);
	mid = metric_offset;
	while (ent->name) {
		for (i = 0; i < ent->n; i++) {
			switch (ent->type) {
			case LDMS_V_CHAR:
			case LDMS_V_CHAR_ARRAY:
				v.v_s8 = off;
				break;
			case LDMS_V_S8:
			case LDMS_V_S8_ARRAY:
				v.v_s8 = off;
				break;
			case LDMS_V_U8:
			case LDMS_V_U8_ARRAY:
				v.v_u8 = off;
				break;
			case LDMS_V_S16:
			case LDMS_V_S16_ARRAY:
				v.v_s16 = off;
				break;
			case LDMS_V_U16:
			case LDMS_V_U16_ARRAY:
				v.v_u16 = off;
				break;
			case LDMS_V_S32:
			case LDMS_V_S32_ARRAY:
				v.v_s32 = off;
				break;
			case LDMS_V_U32:
			case LDMS_V_U32_ARRAY:
				v.v_u32 = off;
				break;
			case LDMS_V_S64:
			case LDMS_V_S64_ARRAY:
				v.v_s64 = off;
				break;
			case LDMS_V_U64:
			case LDMS_V_U64_ARRAY:
				v.v_u64 = off;
				break;
			case LDMS_V_F32:
			case LDMS_V_F32_ARRAY:
				v.v_f = off;
				break;
			case LDMS_V_D64:
			case LDMS_V_D64_ARRAY:
				v.v_d = off;
				break;
			default:
				v.v_u64 = 0;
			}
			if (ent->type < LDMS_V_CHAR_ARRAY) {
				ldms_metric_set(set, mid, &v);
			} else {
				ldms_metric_array_set_val(set, mid, i, &v);
				if (i == ent->n-1) {
					v.v_u64 = 0;
					ldms_metric_array_set_val(set, mid, i+1, &v);
				}
			}
		}
		mid++;
		ent++;
	}

	base_sample_end(base);
	return 0;
}

static void term(ldmsd_plug_handle_t handle)
{
	if (base)
		base_del(base);
	base = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return "config_name=" SAMP " " BASE_CONFIG_USAGE;
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
		.term = term,
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
