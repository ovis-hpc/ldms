/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2021 Open Grid Computing, Inc. All rights reserved.
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
 * \file record_sampler.c
 * \brief An example on how to use LDMS list.
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
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "../../sampler_base.h"

static ovis_log_t mylog;
static ldms_set_t set = NULL;
#define SAMP "record_sampler"
static base_data_t base;


int device_list_mid; /* metric index of device list */
int round_mid; /* metric index of the number of update round */

struct rec_metric {
	const char *name;
	enum ldms_value_type type;
	int array_count;
	int mid;
};

#define ITEM_COUNT	3
#define ARRAY_COUNT	8
static int item_count = ITEM_COUNT;
static int array_count = ARRAY_COUNT;
ldms_record_t rec_def;
int rec_def_idx;
int rec_array_idx;

static int with_name = 0;

#define stringify(_x) #_x
struct rec_metric rec_metrics[] = {
	{ stringify(LDMS_V_CHAR), LDMS_V_CHAR, 0 },
	{ stringify(LDMS_V_U8), LDMS_V_U8, 0 },
	{ stringify(LDMS_V_S8), LDMS_V_S8, 0 },
	{ stringify(LDMS_V_U16), LDMS_V_U16, 0 },
	{ stringify(LDMS_V_S16), LDMS_V_S16, 0 },
	{ stringify(LDMS_V_U32), LDMS_V_U32, 0 },
	{ stringify(LDMS_V_S32), LDMS_V_S32, 0 },
	{ stringify(LDMS_V_U64), LDMS_V_U64, 0 },
	{ stringify(LDMS_V_S64), LDMS_V_S64, 0 },
	{ stringify(LDMS_V_F32), LDMS_V_F32, 0 },
	{ stringify(LDMS_V_D64), LDMS_V_D64, 0 },
	{ stringify(LDMS_V_CHAR_ARRAY), LDMS_V_CHAR_ARRAY, ARRAY_COUNT },
	{ stringify(LDMS_V_U8_ARRAY), LDMS_V_U8_ARRAY, ARRAY_COUNT },
	{ stringify(LDMS_V_S8_ARRAY), LDMS_V_S8_ARRAY, ARRAY_COUNT },
	{ stringify(LDMS_V_U16_ARRAY), LDMS_V_U16_ARRAY, ARRAY_COUNT },
	{ stringify(LDMS_V_S16_ARRAY), LDMS_V_S16_ARRAY, ARRAY_COUNT },
	{ stringify(LDMS_V_U32_ARRAY), LDMS_V_U32_ARRAY, ARRAY_COUNT },
	{ stringify(LDMS_V_S32_ARRAY), LDMS_V_S32_ARRAY, ARRAY_COUNT },
	{ stringify(LDMS_V_U64_ARRAY), LDMS_V_U64_ARRAY, ARRAY_COUNT },
	{ stringify(LDMS_V_S64_ARRAY), LDMS_V_S64_ARRAY, ARRAY_COUNT },
	{ stringify(LDMS_V_F32_ARRAY), LDMS_V_F32_ARRAY, ARRAY_COUNT },
	{ stringify(LDMS_V_D64_ARRAY), LDMS_V_D64_ARRAY, ARRAY_COUNT },
	{ NULL, -1 }
};

/* the special name metric in the record */
struct rec_metric rec_metrics_name = { "name", LDMS_V_CHAR_ARRAY, 16, -1 };

#define LBUFSZ 256
static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	int i;
	int rc;
	struct rec_metric *m;
	/* list-of-list has ITEM_COUNT lists */
	size_t total_sz = ldms_list_heap_size_get(LDMS_V_LIST, item_count, 1);

	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err_0;
	}

	rec_def = ldms_record_create("device_record");
	if (!rec_def) {
		ovis_log(mylog, OVIS_LERROR, "ldms_record_create() error: %d\n", errno);
		rc = errno;
		goto err_1;
	}

	round_mid = ldms_schema_metric_add(schema, "round", LDMS_V_U32);

	/* Fill the record definition */
	for (i = 0, m = &rec_metrics[i]; rec_metrics[i].name != NULL;
	     i++, m = &rec_metrics[i]) {
		m->mid = ldms_record_metric_add(rec_def, m->name, "unit", m->type, m->array_count);
		assert(m->mid >= 0);
	}
	if (with_name) {
		m = &rec_metrics_name;
		m->mid = ldms_record_metric_add(rec_def, m->name, "", m->type, m->array_count);
	}
	total_sz += item_count * ldms_record_heap_size_get(rec_def);
	/* Add record definition into the schema */
	rec_def_idx = ldms_schema_record_add(schema, rec_def);
	assert(rec_def_idx >= 0);
	/* Add a list (of records) */
	device_list_mid = ldms_schema_metric_list_add(schema, "device_list", NULL, total_sz);
	/* Add an array of records */
	rec_array_idx = ldms_schema_record_array_add(schema, "device_array", rec_def, ITEM_COUNT);
	assert(rec_array_idx >= 0);

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err_1;
	}

	return 0;
 err_1:
	ldms_schema_delete(schema);
 err_0:
	return rc;
}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(mylog, OVIS_LERROR, "config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " " BASE_CONFIG_SYNOPSIS
		"       [with_name=0|1]\n"
		BASE_CONFIG_DESC
		"    with_name    1 to generate dev_name in the device, or\n"
		"                 0 to not generate dev_name (default: 0)\n"
		;
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;
	char *_with_name = NULL;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, NULL);
	if (rc != 0){
		return rc;
	}

	_with_name = av_value(avl, "with_name");

	if (_with_name) {
		with_name = atoi(_with_name);
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

static
void value_setter(ldms_mval_t mval, enum ldms_value_type typ, int item)
{
	int i;
	switch (typ) {
	case LDMS_V_CHAR:
		mval->v_char = 'a' + (item%2);
		break;
	case LDMS_V_U8:
		mval->v_u8 = (uint8_t)item;
		break;
	case LDMS_V_S8:
		mval->v_s8 = -(item % 128);
		break;
	case LDMS_V_U16:
		mval->v_u16 = item + 1000;
		break;
	case LDMS_V_S16:
		mval->v_s16 = -(item + 1000);
		break;
	case LDMS_V_U32:
		mval->v_u32 = item + 100000;
		break;
	case LDMS_V_S32:
		mval->v_s32 = -(item + 100000);
		break;
	case LDMS_V_U64:
		mval->v_u64 = item + 200000;
		break;
	case LDMS_V_S64:
		mval->v_s64 = -(item + 200000);
		break;
	case LDMS_V_F32:
		mval->v_f = (float)item;
		break;
	case LDMS_V_D64:
		mval->v_d = (double)item;
		break;
	case LDMS_V_CHAR_ARRAY:
		snprintf(mval->a_char, array_count, "a_%d", item);
		break;
	case LDMS_V_U8_ARRAY:
		for (i = 0; i < array_count; i++)
			mval->a_u8[i] = item + i;
		break;
	case LDMS_V_S8_ARRAY:
		for (i = 0; i < array_count; i++)
			mval->a_s8[i] = -(item + i);
		break;
	case LDMS_V_U16_ARRAY:
		for (i = 0; i < array_count; i++)
			mval->a_u16[i] = 1000 + (item + i);
		break;
	case LDMS_V_S16_ARRAY:
		for (i = 0; i < array_count; i++)
			mval->a_s16[i] = -(1000 + (item + i));
		break;
	case LDMS_V_U32_ARRAY:
		for (i = 0; i < array_count; i++)
			mval->a_u32[i] = 100000 + (item + i);
		break;
	case LDMS_V_S32_ARRAY:
		for (i = 0; i < array_count; i++)
			mval->a_s32[i] = -(100000 + (item + i));
		break;
	case LDMS_V_U64_ARRAY:
		for (i = 0; i < array_count; i++)
			mval->a_u64[i] = 500000 + (item + i);
		break;
	case LDMS_V_S64_ARRAY:
		for (i = 0; i < array_count; i++)
			mval->a_s64[i] = -(500000 + (item + i));

		break;
	case LDMS_V_F32_ARRAY:
		for (i = 0; i < array_count; i++)
			mval->a_f[i] = 0.5f+item + i;
		break;
	case LDMS_V_D64_ARRAY:
		for (i = 0; i < array_count; i++)
			mval->a_d[i] = 0.75 + item + i;
		break;
	default:
		assert(0 == "Invalid metric type in list");
	}
}

static int sample(ldmsd_plug_handle_t handle)
{
	ldms_mval_t lh, rec_inst, mval, rec_array;
	struct rec_metric *m;
	enum ldms_value_type typ;
	size_t count;
	int i, rc;
	uint32_t round;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	round = ldms_metric_get_u32(set, round_mid);
	lh = ldms_metric_get(set, device_list_mid);
	if (ldms_list_len(set, lh) == 0) {
		/* first time */
		assert( round == 0);
		for (i = 0; i < item_count; i++) {
			rec_inst = ldms_record_alloc(set, rec_def_idx);
			assert( rec_inst );
			rc = ldms_list_append_record(set, lh, rec_inst);
			assert( rc == 0);
		}
	}
	round += 1;
	i = 0;
	rec_inst = ldms_list_first(set, lh, &typ, &count);
	while (rec_inst) {
		for (m = rec_metrics; m->name; m++) {
			mval = ldms_record_metric_get(rec_inst, m->mid);
			value_setter(mval, m->type, round + i);
		}
		if (with_name) {
			m = &rec_metrics_name;
			mval = ldms_record_metric_get(rec_inst, m->mid);
			snprintf(mval->a_char, m->array_count, "list%d", i);
		}
		rec_inst = ldms_list_next(set, rec_inst, &typ, &count);
		i++;
	}
	assert( i == item_count );
	rec_array = ldms_metric_get(set, rec_array_idx);
	for (i = 0; i < ITEM_COUNT; i++) {
		rec_inst = ldms_record_array_get_inst(rec_array, i);
		for (m = rec_metrics; m->name; m++) {
			mval = ldms_record_metric_get(rec_inst, m->mid);
			value_setter(mval, m->type, round + i + ITEM_COUNT);
		}
		if (with_name) {
			m = &rec_metrics_name;
			mval = ldms_record_metric_get(rec_inst, m->mid);
			snprintf(mval->a_char, m->array_count, "arr%d", i);
		}
	}

	ldms_metric_set_u32(set, round_mid, round);
	base_sample_end(base);
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
	set = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
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
