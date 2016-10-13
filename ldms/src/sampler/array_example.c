/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2011-2016 Sandia Corporation. All rights reserved.
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
 * \file array_example.c
 * \brief Example of an ldmsd sampler plugin that uses array metric.
 */
#include <errno.h>
#include <stdlib.h>
#include "ldms.h"
#include "ldmsd.h"
#include <stdlib.h>

#define array_metric_name_base "array_"
#define array_num_ele_default 10

static ldms_schema_t schema;
static ldms_set_t set;
static ldmsd_msg_log_f msglog;
static uint64_t compid = 0;
static uint64_t jobid = 0;

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

static int create_metric_set(const char *instance_name,
		int num_metrics, int num_ele, enum ldms_value_type type)
{
	union ldms_value v;
	int rc;

	schema = ldms_schema_new("array_example");
	if (!schema)
		return ENOMEM;

	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
        if (rc < 0) {
		rc = ENOMEM;
                goto err;
        }

        rc = ldms_schema_metric_add(schema, "job_id", LDMS_V_U64);
        if (rc < 0) {
                rc = ENOMEM;
                goto err;
        }

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

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
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
	int num_metrics;
	int num_ele;
	enum ldms_value_type type;
	const char *producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "array_example: missing producer\n");
		return ENOENT;
	}

	value = av_value(avl, "component_id");
        if (value)
                compid = (uint64_t)(atoi(value));
        else
                compid = 0;

	if (set) {
		msglog(LDMSD_LERROR, "array_example: Set already created.\n");
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

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, "array_example: missing instance.\n");
		return ENOENT;
	}

	int rc = create_metric_set(value, num_metrics, num_ele, type);
	if (rc) {
		msglog(LDMSD_LERROR, "array_example: failed to create a metric set.\n");
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
	static uint8_t off = 0;
	struct array_construct *ent = metric_list;
	union ldms_value v;
	mid = 2; //2 specialized metrics
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
	return  "config name=array_example producer=<prod_name> instance=<inst_name> [component_id=<compid>]\n"
		"       [num_metrics=<num_metrics>] [num_ele=<num_ele>] [type=<type>]\n"
		"    <prod_name>  The producer name\n"
		"    <inst_name>  The instance name\n"
		"    <compid>     Optional unique number identifier. Defaults to zero.\n"
		"    <num_metrics>    The number of metrics in the schema\n"
		"    <num_ele>    The number of elements in each array. All arrays have the same number of elements.\n"
		"    <type>       The type of metric arrays, e.g., U64_ARRAY, U8_ARRAY, etc\n";
}

static struct ldmsd_sampler array_example_plugin = {
	.base = {
		.name = "array_example",
		.term = term,
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
	return &array_example_plugin.base;
}
