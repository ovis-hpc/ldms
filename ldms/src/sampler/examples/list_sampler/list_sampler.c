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
 * \file list_sampler.c
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
#include "sampler_base.h"

static ovis_log_t mylog;

static ldms_set_t set = NULL;
#define SAMP "list_sampler"
static int metric_offset;
static base_data_t base;

/* metric index of "list of lists" */
int list_o_lists;

struct list_metric {
	const char *name;
	enum ldms_value_type type;
	int item_count;
	int array_count;
	int mid;
	int list_len;
};

struct list_schema {
	struct list_metric metrics[LDMS_V_LAST];
};

#define ITEM_COUNT	3
#define ARRAY_COUNT	4
static int item_count = ITEM_COUNT;
static int array_count = ARRAY_COUNT;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)
struct list_schema list_schema = {
	{
		{ stringify(LDMS_V_CHAR), LDMS_V_CHAR, ITEM_COUNT },
		{ stringify(LDMS_V_U8), LDMS_V_U8, ITEM_COUNT },
		{ stringify(LDMS_V_S8), LDMS_V_S8, ITEM_COUNT },
		{ stringify(LDMS_V_U16), LDMS_V_U16, ITEM_COUNT },
		{ stringify(LDMS_V_S16), LDMS_V_S16, ITEM_COUNT },
		{ stringify(LDMS_V_U32), LDMS_V_U32, ITEM_COUNT },
		{ stringify(LDMS_V_S32), LDMS_V_S32, ITEM_COUNT },
		{ stringify(LDMS_V_U64), LDMS_V_U64, ITEM_COUNT },
		{ stringify(LDMS_V_S64), LDMS_V_S64, ITEM_COUNT },
		{ stringify(LDMS_V_F32), LDMS_V_F32, ITEM_COUNT },
		{ stringify(LDMS_V_D64), LDMS_V_D64, ITEM_COUNT },
		{ stringify(LDMS_V_CHAR_ARRAY), LDMS_V_CHAR_ARRAY, ITEM_COUNT, ARRAY_COUNT },
		{ stringify(LDMS_V_U8_ARRAY), LDMS_V_U8_ARRAY, ITEM_COUNT, ARRAY_COUNT },
		{ stringify(LDMS_V_S8_ARRAY), LDMS_V_S8_ARRAY, ITEM_COUNT, ARRAY_COUNT },
		{ stringify(LDMS_V_U16_ARRAY), LDMS_V_U16_ARRAY, ITEM_COUNT, ARRAY_COUNT },
		{ stringify(LDMS_V_S16_ARRAY), LDMS_V_S16_ARRAY, ITEM_COUNT, ARRAY_COUNT },
		{ stringify(LDMS_V_U32_ARRAY), LDMS_V_U32_ARRAY, ITEM_COUNT, ARRAY_COUNT },
		{ stringify(LDMS_V_S32_ARRAY), LDMS_V_S32_ARRAY, ITEM_COUNT, ARRAY_COUNT },
		{ stringify(LDMS_V_U64_ARRAY), LDMS_V_U64_ARRAY, ITEM_COUNT, ARRAY_COUNT },
		{ stringify(LDMS_V_S64_ARRAY), LDMS_V_S64_ARRAY, ITEM_COUNT, ARRAY_COUNT },
		{ stringify(LDMS_V_F32_ARRAY), LDMS_V_F32_ARRAY, ITEM_COUNT, ARRAY_COUNT },
		{ stringify(LDMS_V_D64_ARRAY), LDMS_V_D64_ARRAY, ITEM_COUNT, ARRAY_COUNT },
		{ NULL, -1 }
	}
};

#define LBUFSZ 256
static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	int i;
	int rc;
	struct list_metric *m;
	/* list-of-list has ITEM_COUNT lists */
	size_t total_sz = ldms_list_heap_size_get(LDMS_V_LIST, ITEM_COUNT, 1);

	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/* Location of first metric from proc/meminfo file */
	metric_offset = ldms_schema_metric_count_get(schema);

	/* Fill the schema */
	for (i = 0, m = &list_schema.metrics[i]; list_schema.metrics[i].name != NULL;
	     i++, m = &list_schema.metrics[i]) {
		size_t heap_sz = ldms_list_heap_size_get(m->type, m->item_count, m->array_count);
		m->mid = ldms_schema_metric_list_add(schema, m->name, NULL, heap_sz);
		total_sz += heap_sz;
		assert(m->mid >= 0);
	}
	/* Add a list-of-lists metric */
	list_o_lists = ldms_schema_metric_list_add(schema, "list_o_lists", NULL, total_sz);

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

 err:
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
	return  "config name=" SAMP " " BASE_CONFIG_USAGE;
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, NULL);
	if (rc != 0){
		return rc;
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
		mval->v_char = 'a' + item;
		break;
	case LDMS_V_U8:
		mval->v_u8 = (uint8_t)item;
		break;
	case LDMS_V_S8:
		mval->v_u8 = -item;
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
			mval->a_f[i] = (float)(item + i);
		break;
	case LDMS_V_D64_ARRAY:
		for (i = 0; i < array_count; i++)
			mval->a_d[i] = (float)(1000000 + item + i);
		break;
	default:
		assert(0 == "Invalid metric type in list");
	}
}

static int sample(ldmsd_plug_handle_t handle)
{
	size_t len;
	ldms_mval_t mval, lval, ll;
	struct list_metric *m;
	enum ldms_value_type type;
	int i, item;
	size_t count;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	lval = ldms_metric_get(set, list_o_lists);
	if (ldms_list_len(set, lval) == 0) {
		/* The list-o-lists is a list of lists */
		for (item = 0; item < item_count; item++) {
			ll = ldms_list_append_item(set, lval, LDMS_V_LIST, 1);
			for (i = 0, m = &list_schema.metrics[i]; list_schema.metrics[i].name != NULL;
			     i++, m = &list_schema.metrics[i]) {
				if (ldms_type_is_array(m->type))
					ldms_list_append_item(set, ll, m->type, array_count);
				else
					ldms_list_append_item(set, ll, m->type, 1);
			}

		}
	}
	for (i = 0, m = &list_schema.metrics[i]; list_schema.metrics[i].name != NULL;
	     i++, m = &list_schema.metrics[i]) {
		lval = ldms_metric_get(set, m->mid);
		for (len = ldms_list_len(set, lval); len < item_count;
		     len = ldms_list_len(set, lval)) {
			count = 1;
			if (ldms_type_is_array(m->type))
				mval = ldms_list_append_item(set, lval, m->type, array_count);
			else
				mval = ldms_list_append_item(set, lval, m->type, 1);
			if (mval == NULL) {
				assert(0 == "oops");
			}
		}
		lval = ldms_metric_get(set, m->mid);
		for (item = 0, mval = ldms_list_first(set, lval, &type, &count); mval;
		     mval = ldms_list_next(set, mval, &type, &count), item++) {
			value_setter(mval, type, item);
		}
	}
	item = 0;
	ll = ldms_metric_get(set, list_o_lists);
	for (lval = ldms_list_first(set, ll, &type, &count); lval;
	     lval = ldms_list_next(set, lval, &type, &count)) {
		for (i = 0, mval = ldms_list_first(set, lval, &type, &count); mval;
		     mval = ldms_list_next(set, mval, &type, &count), i++) {
			value_setter(mval, list_schema.metrics[i].type, item);
		}
		item ++;
	}
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
