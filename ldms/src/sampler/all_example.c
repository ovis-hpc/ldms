/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2017 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
#include <stdlib.h>

#define array_metric_name_base "array_"
#define array_num_ele_default 3

static ldms_schema_t schema;
static ldms_set_t set;
static ldmsd_msg_log_f msglog;
static uint64_t compid = 0;

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
static int create_metric_set(const char *instance_name)
{
	union ldms_value v;
	int rc;

	schema = ldms_schema_new("all_example");
	if (!schema)
		return ENOMEM;

	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		rc = ENOMEM;
		msglog(LDMSD_LERROR, "all_example: adding cid failed\n");
	        goto err;
	}

	rc = ldms_schema_metric_add(schema, "job_id", LDMS_V_U64);
	if (rc < 0) {
	        rc = ENOMEM;
		msglog(LDMSD_LERROR, "all_example: adding jid failed\n");
	        goto err;
	}

	struct metric_construct *ent;
	if (num_metrics >= 0)
		return EINVAL; // does not support multiple configuration/instances.
	metric_list = metric_construct_entries;
	ent = &metric_list[0];
	while (ent->name) {
		msglog(LDMSD_LDEBUG, "all_example: adding %s\n", ent->name);
		if (ent->type >= LDMS_V_CHAR_ARRAY)
			rc = ldms_schema_metric_array_add(schema, ent->name, ent->type, ent->n+1);
		else
			rc = ldms_schema_metric_add(schema, ent->name, ent->type);
		if (rc < 0) {
			msglog(LDMSD_LERROR, "all_example: adding %s failed\n", 
				ent->name);
			rc = ENOMEM;
			goto err;
		}
		ent++;
		msglog(LDMSD_LDEBUG, "all_example: ok\n");
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		msglog(LDMSD_LERROR, "all_example: set new failed.\n", 
			ent->name);
		goto err;
	}

	//add specialized metrics
	v.v_u64 = compid;
	ldms_metric_set(set, 0, &v);
	v.v_u64 = 0;
	ldms_metric_set(set, 1, &v);

	return 0;
 err:
	ldms_schema_delete(schema);
	return rc;
}
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	const char *producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "all_example: missing producer\n");
		return ENOENT;
	}

	value = av_value(avl, "component_id");
	if (value)
	        compid = (uint64_t)(atoi(value));
	else
	        compid = 0;

	if (set) {
		msglog(LDMSD_LERROR, "all_example: Set already created.\n");
		return EINVAL;
	}

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, "all_example: missing instance.\n");
		return ENOENT;
	}

	int rc = create_metric_set(value);
	if (rc) {
		msglog(LDMSD_LERROR, "all_example: failed to create a metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);
	return 0;
}

static int sample(struct ldmsd_sampler *self)
{
	if (!set)
		return EINTR;
	ldms_transaction_begin(set);
	int i, mid;
	static uint8_t off = 76; // ascii L
	struct metric_construct *ent = metric_list;
	union ldms_value v;
	mid = 2; //2 specialized metrics
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
	ldms_transaction_end(set);
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static void term(struct ldmsd_plugin *self)
{
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=all_example producer=<prod_name> instance=<inst_name> [component_id=<compid>]\n"
		"    <prod_name>  The producer name\n"
		"    <inst_name>  The instance name\n"
		"    <compid>     Optional unique number identifier. Defaults to zero.\n";
}

static struct ldmsd_sampler all_example_plugin = {
	.base = {
		.name = "all_example",
		.term = term,
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	set = NULL;
	return &all_example_plugin.base;
}
