/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2020 Open Grid Computing, Inc. All rights reserved.
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

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

#define DEFAULT_NUM_METRICS 10
#define DEFAULT_NUM_SETS 1
#define DEFAULT_METRIC_NAME_NAME "metric_"

#define TEST_SAMPLER_DEFAULT_NUM_METRICS 3
#define TEST_SAMPLER_DEFAULT_ARRAY_SZ 3
#define TEST_SAMPLER_DEFAULT_UNIT ""
#define TEST_SAMPLER_DEFAULT_SCHEMA_TYPE TEST_SAMPLER_TYPE_DEFAULT

#define TEST_SAMPLER_DEFAULT_SUFFIX_W_TIME 1 /* true */
#define TEST_SAMPLER_DEFAULT_PUSH_AT 0
#define TEST_SAMPLER_DEFAULT_DELETE_AT 0 /* false */
#define TEST_SAMPLER_DEFAULT_CREATE_AT 0 /* create right after delete */

struct test_sampler_metric {
	char *name;
	int mtype; /* 2 is meta and 1 is data */
	enum ldms_value_type vtype;
	int array_sz; /* Number of elements in an array metric */
	int idx;
	char *unit;
	TAILQ_ENTRY(test_sampler_metric) entry;
	union ldms_value init_value;
	union ldms_value latest_value;
};

struct test_sampler_set {
	char *name;
	ldms_set_t set;
	int push_at;
	int delete_at;
	int from_delete_count;
	int create_at;
	int is_suffix;
	int skip_push;
	unsigned int sample_count;
	LIST_ENTRY(test_sampler_set) entry;
};
LIST_HEAD(test_sampler_set_list, test_sampler_set);

typedef struct test_sampler_inst_s *test_sampler_inst_t;
struct test_sampler_inst_s {
	struct ldmsd_plugin_inst_s base;

	/* Point to the ldmsd_sampler_type_s->samp_sample */
	int (*samp_sample)(ldmsd_plugin_inst_t pi);

	char base_set_name[256];
	int num_sets;
	enum {
		TEST_SAMPLER_TYPE_SCALAR = 1,
		TEST_SAMPLER_TYPE_ARRAY,
		TEST_SAMPLER_TYPE_ALL,
		TEST_SAMPLER_TYPE_DEFAULT,
		TEST_SAMPLER_TYPE_MANUAL,
	} type;
	int num_metrics; /* Use only by default */
	json_entity_t metrics; /* Use only be manual */
	int array_sz; /* Use only by array & all */
	TAILQ_HEAD(test_sampler_metric_list, test_sampler_metric) metric_list;
	struct test_sampler_set_list set_list;
	char *unit; /* default unit */
};

/* metric->vtype MUST be initialized before this function is called. */
static int __metric_init_value_set(test_sampler_inst_t inst,
				   struct test_sampler_metric *metric,
				   const char *init_value_str)
{
	switch (metric->vtype) {
	case LDMS_V_CHAR:
		metric->init_value.v_char = init_value_str[0];
		break;
	case LDMS_V_CHAR_ARRAY:
		memcpy(metric->init_value.a_char, init_value_str, strlen(init_value_str));
		metric->init_value.a_char[strlen(init_value_str)] = '\0';
		break;
	case LDMS_V_U8:
	case LDMS_V_U8_ARRAY:
		sscanf(init_value_str, "%" SCNu8, &(metric->init_value.v_u8));
		break;
	case LDMS_V_U16:
	case LDMS_V_U16_ARRAY:
		sscanf(init_value_str, "%" SCNu16, &(metric->init_value.v_u16));
		break;
	case LDMS_V_U32:
	case LDMS_V_U32_ARRAY:
		sscanf(init_value_str, "%" SCNu32, &(metric->init_value.v_u32));
		break;
	case LDMS_V_U64:
	case LDMS_V_U64_ARRAY:
		sscanf(init_value_str, "%" SCNu64, &(metric->init_value.v_u64));
		break;
	case LDMS_V_S8:
	case LDMS_V_S8_ARRAY:
		sscanf(init_value_str, "%" SCNi8, &(metric->init_value.v_s8));
		break;
	case LDMS_V_S16:
	case LDMS_V_S16_ARRAY:
		sscanf(init_value_str, "%" SCNi16, &(metric->init_value.v_s16));
		break;
	case LDMS_V_S32:
	case LDMS_V_S32_ARRAY:
		sscanf(init_value_str, "%" SCNi32, &(metric->init_value.v_s32));
		break;
	case LDMS_V_S64:
	case LDMS_V_S64_ARRAY:
		sscanf(init_value_str, "%" SCNi64, &(metric->init_value.v_s64));
		break;
	case LDMS_V_F32:
	case LDMS_V_F32_ARRAY:
		metric->init_value.v_f = strtof(init_value_str, NULL);
		break;
	case LDMS_V_D64:
	case LDMS_V_D64_ARRAY:
		metric->init_value.v_d = strtod(init_value_str, NULL);
		break;
	default:
		INST_LOG(inst, LDMSD_LERROR,
			 "Unrecognized/not supported type '%s'\n",
			 ldms_metric_type_to_str(metric->vtype));
		return EINVAL;
	}
	return 0;
}

void ts_metric_free(struct test_sampler_metric *metric)
{
	if (metric->name)
		free(metric->name);
	if (metric->unit)
		free(metric->unit);
	free(metric);
}

static struct test_sampler_metric *
ts_metric_new(test_sampler_inst_t inst, const char *name,
			  const char *mtype, enum ldms_value_type vtype,
			  const char *init_value, int array_sz,
			  const char *unit)
{
	int count = array_sz;
	struct test_sampler_metric *metric;

	if (vtype == LDMS_V_CHAR_ARRAY) {
		if (init_value && (count < strlen(init_value) + 1))
			count = strlen(init_value) + 1;
		metric = malloc(sizeof(*metric) + count);
	} else {
		/* No need to allocate memory for the other array types */
		metric = malloc(sizeof(*metric));
	}

	if (!metric)
		return NULL;
	metric->name = strdup(name);
	if (!metric->name)
		goto err;

	metric->unit = strdup(unit);
	if (!metric->unit)
		goto err;

	if ((0 == strcasecmp(mtype, "data")) || (0 == strcasecmp(mtype, "d"))) {
		metric->mtype = LDMS_MDESC_F_DATA;
	} else if ((0 == strcasecmp(mtype, "meta")) ||
			(0 == strcasecmp(mtype, "m"))) {
		metric->mtype = LDMS_MDESC_F_META;
	} else {
		goto err;
	}

	metric->vtype = vtype;
	if (metric->vtype == LDMS_V_NONE) {
		goto err;
	}

	if (__metric_init_value_set(inst, metric, init_value)) {
		goto err;
	}

	metric->array_sz = count;
	TAILQ_INSERT_TAIL(&inst->metric_list, metric, entry);
	return metric;
err:
	errno = ENOMEM;
	ts_metric_free(metric);
	return NULL;
}

void metric_list_empty(test_sampler_inst_t inst)
{
	struct test_sampler_metric *metric;

	metric = TAILQ_FIRST(&inst->metric_list);
	while (metric) {
		TAILQ_REMOVE(&inst->metric_list, metric, entry);
		ts_metric_free(metric);
		metric = TAILQ_FIRST(&inst->metric_list);
	}
}

/* create a new set for test_sampler_set */
static void __set_create(test_sampler_inst_t inst,
				struct test_sampler_set *ts_set)
{
	char buff[512];
	const char *name;
	struct timeval tv;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(inst);
	if (ts_set->is_suffix) {
		gettimeofday(&tv, NULL);
		snprintf(buff, sizeof(buff), "%s:%ld.%06ld",
			 ts_set->name, tv.tv_sec, tv.tv_usec);
		name = buff;
	} else {
		name = ts_set->name;
	}
	ts_set->set = samp->create_set(&inst->base, name,
				samp->schema, ts_set);
	if (!ts_set->set) {
		INST_LOG(inst, LDMSD_LERROR, "Failed to create the set "
				"'%s' with error %d.\n", ts_set->name, errno);
	}
}

static void __set_delete(test_sampler_inst_t inst,
				struct test_sampler_set *ts_set)
{
	int rc;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(inst);
	rc = samp->delete_set(&inst->base, ldms_set_name_get(ts_set->set));
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR, "Failed to delete the set '%s' "
				"with error %d.\n", ts_set->name, rc);
	} else {
		ts_set->set = NULL;
	}
}

static
void ts_set_free(test_sampler_inst_t inst, struct test_sampler_set *ts_set)
{
	if (ts_set->set)
		__set_delete(inst, ts_set);
	if (ts_set->name)
		free(ts_set->name);
	free(ts_set);
}

void set_list_empty(test_sampler_inst_t inst)
{
	struct test_sampler_set *ts_set;
	ts_set = LIST_FIRST(&inst->set_list);
	while (ts_set) {
		LIST_REMOVE(ts_set, entry);
		ts_set_free(inst, ts_set);
	}
}

static struct test_sampler_set *
ts_set_new(test_sampler_inst_t inst, const char *instance_name,
			  int push_at, int delete_at,
			  int create_from_delete_at,
			  int is_suffix)
{
	struct test_sampler_set *ts_set = calloc(1, sizeof(*ts_set));
	if (!ts_set)
		goto oom;
	ts_set->name = strdup(instance_name);
	if (!ts_set->name)
		goto oom;
	ts_set->push_at = push_at;
	ts_set->delete_at = delete_at;
	ts_set->create_at = create_from_delete_at;
	ts_set->is_suffix = is_suffix;
	ts_set->skip_push = 1;
	ts_set->sample_count = 1;
	LIST_INSERT_HEAD(&inst->set_list, ts_set, entry);
	return ts_set;

oom:
	errno = ENOMEM;
	if (ts_set)
		free(ts_set);
	return NULL;
}

static json_entity_t attr_find(test_sampler_inst_t inst,
				json_entity_t json,
				enum json_value_e json_type,
				char *attr_name, int is_required,
				char *ebuf, size_t ebufsz)
{
	json_entity_t v;
	errno = 0;
	v = json_value_find(json, attr_name);
	if (!v) {
		if (is_required) {
			snprintf(ebuf, ebufsz, "%s: The '%s' is missing.\n",
					inst->base.inst_name, attr_name);
			errno = ENOENT;
		}
		return NULL;
	}
	if (v->type != json_type) {
		snprintf(ebuf, ebufsz, "%s: The given '%s' value is not a %s.\n",
			inst->base.inst_name, attr_name, json_type_str(json_type));
		errno = EINVAL;
		return NULL;
	}
	return v;
}

enum ldms_value_type __type_to_scalar(enum ldms_value_type type)
{
	switch (type) {
	case LDMS_V_U8:
	case LDMS_V_U8_ARRAY:
		return LDMS_V_U8;
	case LDMS_V_S8:
	case LDMS_V_S8_ARRAY:
		return LDMS_V_S8;
	case LDMS_V_U16:
	case LDMS_V_U16_ARRAY:
		return LDMS_V_U16;
	case LDMS_V_S16:
	case LDMS_V_S16_ARRAY:
		return LDMS_V_S16;
	case LDMS_V_U32:
	case LDMS_V_U32_ARRAY:
		return LDMS_V_U32;
	case LDMS_V_S32:
	case LDMS_V_S32_ARRAY:
		return LDMS_V_S32;
	case LDMS_V_U64:
	case LDMS_V_U64_ARRAY:
		return LDMS_V_U64;
	case LDMS_V_S64:
	case LDMS_V_S64_ARRAY:
		return LDMS_V_S64;
	case LDMS_V_F32:
	case LDMS_V_F32_ARRAY:
		return LDMS_V_F32;
	case LDMS_V_D64:
	case LDMS_V_D64_ARRAY:
		return LDMS_V_D64;
	default:
		return LDMS_V_NONE;
	}
}

static inline
void __val_inc(ldms_mval_t val, enum ldms_value_type type)
{
	/* NOTE0 only support non-array */
	/* NOTE1 data in val is in HOST format */
	switch (type) {
	case LDMS_V_U8:
	case LDMS_V_S8:
		val->v_u8++;
		break;
	case LDMS_V_U16:
	case LDMS_V_S16:
		val->v_u16++;
		break;
	case LDMS_V_U32:
	case LDMS_V_S32:
		val->v_u32++;
		break;
	case LDMS_V_U64:
	case LDMS_V_S64:
		val->v_u64++;
		break;
	case LDMS_V_F32:
		val->v_f++;
		break;
	case LDMS_V_D64:
		val->v_d++;
		break;
	default:
		break;
	}
}

static inline
void __metric_inc(ldms_set_t set, int mid, int j)
{
	union ldms_value v;
	switch (ldms_metric_type_get(set, mid)) {
	case LDMS_V_U8_ARRAY:
		v.v_u8 = ldms_metric_array_get_u8(set, mid, j);
		v.v_u8++;
		ldms_metric_array_set_u8(set, mid, j, v.v_u8);
		break;
	case LDMS_V_S8_ARRAY:
		v.v_s8 = ldms_metric_array_get_s8(set, mid, j);
		v.v_s8++;
		ldms_metric_array_set_s8(set, mid, j, v.v_s8);
		break;
	case LDMS_V_U16_ARRAY:
		v.v_u16 = ldms_metric_array_get_u16(set, mid, j);
		v.v_u16++;
		ldms_metric_array_set_u16(set, mid, j, v.v_u16);
		break;
	case LDMS_V_S16_ARRAY:
		v.v_s16 = ldms_metric_array_get_s16(set, mid, j);
		v.v_s16++;
		ldms_metric_array_set_s16(set, mid, j, v.v_s16);
		break;
	case LDMS_V_U32_ARRAY:
		v.v_u32 = ldms_metric_array_get_u32(set, mid, j);
		v.v_u32++;
		ldms_metric_array_set_u32(set, mid, j, v.v_u32);
		break;
	case LDMS_V_S32_ARRAY:
		v.v_s32 = ldms_metric_array_get_s32(set, mid, j);
		v.v_s32++;
		ldms_metric_array_set_s32(set, mid, j, v.v_s32);
		break;
	case LDMS_V_S64_ARRAY:
		v.v_s64 = ldms_metric_array_get_s64(set, mid, j);
		v.v_s64++;
		ldms_metric_array_set_s64(set, mid, j, v.v_s64);
		break;
	case LDMS_V_U64_ARRAY:
		v.v_u64 = ldms_metric_array_get_u64(set, mid, j);
		v.v_u64++;
		ldms_metric_array_set_u64(set, mid, j, v.v_u64);
		break;
	case LDMS_V_F32_ARRAY:
		v.v_f = ldms_metric_array_get_float(set, mid, j);
		v.v_f++;
		ldms_metric_array_set_float(set, mid, j, v.v_f);
		break;
	case LDMS_V_D64_ARRAY:
		v.v_d = ldms_metric_array_get_double(set, mid, j);
		v.v_d++;
		ldms_metric_array_set_double(set, mid, j, v.v_d);
		break;
	case LDMS_V_U8:
		v.v_u8 = ldms_metric_get_u8(set, mid);
		v.v_u8++;
		ldms_metric_set_u8(set, mid, v.v_u8);
		break;
	case LDMS_V_S8:
		v.v_s8 = ldms_metric_get_s8(set, mid);
		v.v_s8++;
		ldms_metric_set_s8(set, mid, v.v_s8);
		break;
	case LDMS_V_U16:
		v.v_u16 = ldms_metric_get_u16(set, mid);
		v.v_u16++;
		ldms_metric_set_u16(set, mid, v.v_u16);
		break;
	case LDMS_V_S16:
		v.v_s16 = ldms_metric_get_s16(set, mid);
		v.v_s16++;
		ldms_metric_set_s16(set, mid, v.v_s16);
		break;
	case LDMS_V_U32:
		v.v_u32 = ldms_metric_get_u32(set, mid);
		v.v_u32++;
		ldms_metric_set_u32(set, mid, v.v_u32);
		break;
	case LDMS_V_S32:
		v.v_s32 = ldms_metric_get_s32(set, mid);
		v.v_s32++;
		ldms_metric_set_s32(set, mid, v.v_s32);
		break;
	case LDMS_V_S64:
		v.v_s64 = ldms_metric_get_s64(set, mid);
		v.v_s64++;
		ldms_metric_set_s64(set, mid, v.v_s64);
		break;
	case LDMS_V_U64:
		v.v_u64 = ldms_metric_get_u64(set, mid);
		v.v_u64++;
		ldms_metric_set_u64(set, mid, v.v_u64);
		break;
	case LDMS_V_F32:
		v.v_f = ldms_metric_get_float(set, mid);
		v.v_f++;
		ldms_metric_set_float(set, mid, v.v_f);
		break;
	case LDMS_V_D64:
		v.v_d = ldms_metric_get_double(set, mid);
		v.v_d++;
		ldms_metric_set_double(set, mid, v.v_d);
		break;
	default:
		return;
	}
}

static
int update_schema_manual(test_sampler_inst_t inst, ldms_schema_t schema)
{
	struct test_sampler_metric *m;

	TAILQ_FOREACH(m, &inst->metric_list, entry) {
		if (m->mtype == LDMS_MDESC_F_DATA) {
			if (ldms_type_is_array(m->vtype)) {
				m->idx = ldms_schema_metric_array_add(schema,
							m->name, m->vtype,
							m->unit, m->array_sz);
			} else {
				m->idx = ldms_schema_metric_add(schema,
						m->name, m->vtype, m->unit);
			}
		} else {
			if (ldms_type_is_array(m->mtype)) {
				m->idx = ldms_schema_meta_array_add(schema,
							m->name, m->vtype,
							m->unit, m->array_sz);
			} else {
				m->idx = ldms_schema_meta_add(schema,
						m->name, m->vtype, m->unit);
			}
		}
		if (m->idx < 0)
			return m->idx;
	}
	return 0;
}

static int update_schema_default(test_sampler_inst_t inst, ldms_schema_t schema)
{
	char *name;
	int i, idx, rc;

	for (i = 0; i < inst->num_metrics; i++) {
		rc = asprintf(&name, "metric_%d", i);
		if (rc < 0) {
			INST_LOG(inst, LDMSD_LCRITICAL, "Out of memory\n");
			return ENOMEM;
		}
		idx = ldms_schema_metric_add(schema, name, LDMS_V_U64, inst->unit);
		free(name);
		if (idx < 0)
			return -idx;
	}
	return 0;
}

static int update_schema_array(test_sampler_inst_t inst, ldms_schema_t schema)
{
	int t, idx;
	const char *name;

	for (t = LDMS_V_CHAR_ARRAY; t < LDMS_V_LAST; t++) {
		name = ldms_metric_type_to_str(t);
		idx = ldms_schema_metric_array_add(schema, name, t,
						inst->unit, inst->array_sz);
		if (idx < 0)
			return -idx;
	}
	return 0;
}

static int update_schema_scalar(test_sampler_inst_t inst, ldms_schema_t schema)
{
	int t, idx;
	const char *name;

	for (t = LDMS_V_FIRST; t < LDMS_V_LAST; t++) {
		if (ldms_type_is_array(t))
			break;
		name = ldms_metric_type_to_str(t);
		idx = ldms_schema_metric_add(schema, name, t, inst->unit);
		if (idx < 0)
			return -idx;
	}
	return 0;
}

static int update_schema_all(test_sampler_inst_t inst, ldms_schema_t schema)
{
	int rc = update_schema_scalar(inst, schema);
	if (rc)
		return rc;
	return update_schema_array(inst, schema);
}

static
int process_instances(test_sampler_inst_t inst, json_entity_t instances,
						char *ebuf, size_t ebufsz)
{
	json_entity_t ent, v, d;
	int push_at, delete_at, create_at, is_suffix;
	struct test_sampler_set *ts_set;
	char *set_name;

	for (ent = json_attr_first(instances); ent; ent = json_attr_next(ent)) {
		set_name = json_attr_name(ent)->str;
		d = json_attr_value(ent);

		push_at = TEST_SAMPLER_DEFAULT_PUSH_AT;
		delete_at = TEST_SAMPLER_DEFAULT_DELETE_AT;
		create_at = TEST_SAMPLER_DEFAULT_CREATE_AT;
		is_suffix = TEST_SAMPLER_DEFAULT_SUFFIX_W_TIME;

		if (d->type != JSON_DICT_VALUE) {
			/* No special options are given */
			goto create_ts_set;
		}

		/* push */
		v = attr_find(inst, d, JSON_INT_VALUE,
					"push_at", 0, ebuf, ebufsz);
		if (!v && (errno == EINVAL))
			return EINVAL;
		if (v)
			push_at = json_value_int(v);

		/* delete */
		v = attr_find(inst, d, JSON_INT_VALUE,
					"delete_at", 0, ebuf, ebufsz);
		if (!v && (errno == EINVAL))
			return EINVAL;
		if (v)
			delete_at = json_value_int(v);

		/* create */
		v = attr_find(inst, d, JSON_INT_VALUE,
					"create_from_delete_at", 0, ebuf, ebufsz);
		if (!v && (errno == EINVAL))
			return EINVAL;
		if (v)
			create_at = json_value_int(v);

		/* suffix */
		v = attr_find(inst, d, JSON_BOOL_VALUE,
					"suffix_with_time", 0, ebuf, ebufsz);
		if (!v && (errno == EINVAL))
			return EINVAL;
		if (v)
			is_suffix = json_value_bool(v);

	create_ts_set:
		/* Create test_sampler_set */
		ts_set = ts_set_new(inst, set_name, push_at, delete_at, create_at, is_suffix);
		if (!ts_set)
			return ENOMEM;
	}
	return 0;
}

static
int process_metrics(test_sampler_inst_t inst, json_entity_t metrics,
					char *ebuf, size_t ebufsz)
{
	int rc;
	json_entity_t ent, v, d;
	char *name, *mtype, *init_value, *unit;
	enum ldms_value_type vtype;
	int array_sz;
	struct test_sampler_metric *metric;

	for (ent = json_attr_first(metrics); ent; ent = json_attr_next(ent)) {
		name = json_attr_name(ent)->str;
		d = json_attr_value(ent);

		/* metric_type */
		v = attr_find(inst, d, JSON_STRING_VALUE, "metric_type",
							0, ebuf, ebufsz);
		if (v) {
			mtype = json_value_str(v)->str;
		} else {
			if (errno == EINVAL) {
				rc = EINVAL;
				goto err;
			} else {
				mtype = "data";
			}
		}

		/* value_type */
		v = attr_find(inst, d, JSON_STRING_VALUE, "value_type",
							0, ebuf, ebufsz);
		if (v) {
			vtype = ldms_metric_str_to_type(json_value_str(v)->str);
			if (LDMS_V_NONE == vtype) {
				snprintf(ebuf, ebufsz, "%s: metric %s has "
						"invalid value type (%s).",
						inst->base.inst_name, name,
						json_value_str(v)->str);
				rc = EINVAL;
				goto err;
			}
		} else {
			if (errno == EINVAL) {
				rc = EINVAL;
				goto err;
			}else{
				vtype = LDMS_V_U64;
			}
		}

		/* init_value */
		v = attr_find(inst, d, JSON_STRING_VALUE, "init_value",
							0, ebuf, ebufsz);
		if (v) {
			init_value = json_value_str(v)->str;
		} else {
			if (EINVAL == errno) {
				rc = EINVAL;
				goto err;
			} else {
				init_value = "0";
			}
		}

		/* array_sz */
		v = attr_find(inst, d, JSON_INT_VALUE, "array_sz",
							0, ebuf, ebufsz);
		if (v) {
			if (!ldms_type_is_array(vtype)) {
				snprintf(ebuf, ebufsz, "%s: metric %s not an "
						"array. array_sz is ignored.",
						inst->base.inst_name, name);
				array_sz = 0;
			} else {
				array_sz = json_value_int(v);
			}
		} else {
			if (EINVAL == errno) {
				rc = EINVAL;
				goto err;
			} else {
				array_sz = 0;
			}
		}

		/* unit */
		v = attr_find(inst, d, JSON_STRING_VALUE,
				"unit", 0, ebuf, ebufsz);
		if (!v) {
			if (EINVAL == errno) {
				snprintf(ebuf, ebufsz, "%s: unit of "
						"metric (%s) is not a string.",
						LDMSD_INST(inst)->inst_name, name);
				rc = EINVAL;
				goto err;
			} else {
				unit = inst->unit;
			}
		} else {
			unit = json_value_str(v)->str;
		}

		metric = ts_metric_new(inst, name, mtype, vtype,
						init_value, array_sz, unit);
		if (!metric)
			goto oom;
	}

	return 0;
oom:
	INST_LOG(inst, LDMSD_LCRITICAL, "Out of memory\n");
	rc = ENOMEM;
err:
	metric_list_empty(inst);
	return rc;
}

/* ============== Sampler Plugin APIs ================= */

static
int test_sampler_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	int rc = 0;
	test_sampler_inst_t inst = (void *)pi;

	switch (inst->type) {
	case TEST_SAMPLER_TYPE_SCALAR:
		rc = update_schema_scalar(inst, schema);
		break;
	case TEST_SAMPLER_TYPE_ARRAY:
		rc = update_schema_array(inst, schema);
		break;
	case TEST_SAMPLER_TYPE_ALL:
		rc = update_schema_all(inst, schema);
		break;
	case TEST_SAMPLER_TYPE_DEFAULT:
		rc = update_schema_default(inst, schema);
		break;
	case TEST_SAMPLER_TYPE_MANUAL:
		rc = update_schema_manual(inst, schema);
		break;
	default:
		INST_LOG(inst, LDMSD_LCRITICAL, "Unknown schema type. This is "
				"impossible because the plugin checked the type "
				"at the configuration time.");
		assert(0 == "Impossible");
		break;
	}
	return rc;
}

static
int test_sampler_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	int j, idx, card, len;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(pi);

	card = ldms_set_card_get(set);
	for (idx = samp->first_idx; idx < card; idx++) {
		if (ldms_metric_flags_get(set, idx) & LDMS_MDESC_F_META) {
			/* Don't change meta metric value */
			continue;
		}

		if (ldms_metric_is_array(set, idx)) {
			len = ldms_metric_array_get_len(set, idx);
			for (j = 0; j < len; j++) {
				__metric_inc(set, idx, j);
			}
		} else {
			__metric_inc(set, idx, 0);
		}
	}

	return 0;
}

int test_sampler_sample(ldmsd_plugin_inst_t pi)
{
	test_sampler_inst_t inst = (void*)pi;
	int j;
	struct test_sampler_set *ts_set;

	/* intercept normal sample routine to optionally delete sets */
	LIST_FOREACH(ts_set, &inst->set_list, entry) {
		if (ts_set->delete_at > 0) {
			ts_set->from_delete_count++;
			if (NULL == ts_set->set)
				goto create_set;
			j = ts_set->sample_count % ts_set->delete_at;
			if (j == 0) {
				/* delete the set */
				__set_delete(inst, ts_set);
				ts_set->from_delete_count = 0;
			} else {
				/* nothing to do */
			}
		create_set:
			if (ts_set->from_delete_count == ts_set->create_at) {
				__set_create(inst, ts_set);
			}

		}
	}
	/* then, resume normal sample routine */
	inst->samp_sample(&inst->base);

	/*
	 * All sets have been updated, increment the sample counts
	 * and do push routine.
	 */
	LIST_FOREACH(ts_set, &inst->set_list, entry) {
		ts_set->sample_count++;
		if (NULL == ts_set->set)
			continue;
		/* push */
		if (!ts_set->push_at)
			continue;
		if (ts_set->push_at == ts_set->skip_push) {
			ldms_xprt_push(ts_set->set);
			ts_set->skip_push = 1;
		} else {
			ts_set->skip_push++;
		}
	}

	return 0;
}

/* ============== Common Plugin APIs ================= */

static
const char *test_sampler_desc(ldmsd_plugin_inst_t pi)
{
	return "test_sampler - test_sampler sampler plugin";
}

static
char *_help = "\
\n\
* add_schema\n\
    config name=<INST> action=add_schema schema=<schema_name>\n\
           [set_array_card=number of elements in the set ring buffer]\n\
	   [metrics=<name>:<data|meta>:<type>:<init value>,...]\n\
	   [num_metrics=<num_metrics>] [type=<metric type>]\n\
	   [init_value=<init_value>]\n\
\n\
    Either giving metrics or num_metrics. The default init value\n\
    is the metric index.\n\
\n\
    If 'metrics' is given, the meta and data metrics in the schema will be \n\
    exactly as the given metric list. Otherwise, 'component_id' and 'job_id' \n\
    metrics will be automatically added to the schema. The valid metric types\n\
    are either 'meta' or 'data'. The valid value types are, for example, D64,\n\
    F32, S64, U64, S64_ARRAY\n\
\n\
\n\
* add_scalar \n\
    config name=test_sampler action=add_scalar [schema=<schema name>]\n\
           [set_array_card=set_array_card]\n\
\n\
    Create a schema with the metrics of each scalar type\n\
\n\
    <set_array_card>     number of elements in the set ring buffer\n\
\n\
\n\
* add_array \n\
    config name=test_sampler action=add_array [schema=<schema name>]\n\
           [set_array_card=<set_array_card>]\n\
	   metric_array_sz=<metric_array_sz>\n\
\n\
    Create a schema with the metrics of each array type\n\
\n\
    <set_array_card>     number of elements in the set ring buffer\n\
    <metric_array_sz>    number of elements in each array metric\n\
\n\
\n\
* add_all \n\
    config name=test_sampler action=add_all [schema=<schema name>]\n\
           [set_array_card=<set_array_card>]\n\
	   metric_array_sz=<metric_array_sz>\n\
\n\
    Create a schema with the metrics of each scalar and array type\n\
\n\
    <set_array_card>>    number of elements in the set ring buffer\n\
    <metric_array_sz>    number of elements in each array metric\n\
\n\
\n\
* add_set \n\
    config name=test_sampler action=add_set instance=<set_name>\n\
           schema=<schema_name>\n\
	   [producer=<producer>] [component_id=<compid>] [jobid=<jobid>]\n\
	   [push=<push>]\n\
\n\
    <set name>      The set name\n\
    <schema name>   The schema name\n\
    <producer>      The producer name\n\
    <compid>        The component ID\n\
    <jobid>         The job ID\n\
    <push>          A positive number. \n\
                    The default is 0 meaning no pushing update.\n.\
                    1 means the sampler will push every update,\n\
                    2 means the sampler will push every other update,\n\
                    3 means the sampler will push every third updates,\n\
                    and so on.\n\
\n\
\n\
* `default` action\n\
    config name=test_sampler action=default [base=<base>] [schema=<sname>] \n\
           [num_sets=<nsets>] [num_metrics=<nmetrics>] [push=<push>]\n\
\n\
    <base>       The base of set names\n\
    <sname>      Optional schema name. Defaults to 'test_sampler'\n\
    <nsets>      Number of sets\n\
    <nmetrics>   Number of metrics\n\
    <push>       A positive number. The default is 0 meaning no pushing update.\n\
                 1 means the sampler will push every update,\n\
                 2 means the sampler will push every other update,\n\
                 3 means the sampler will push every third updates,\n\
                 and so on.\n\
\n\
\n\
* no action: just configuring some basic values\n\
    config name=test_sampler [producer=<prod_name>] [component_id=<comp_id>]\n\
           [jobid=<jobid>] [set_delete_interval=<interval>] [ts_suffix=<0|1>]\n\
\n\
    <prod_name>  The producer name\n\
    <comp_id>    The component ID\n\
    <jobid>      The job ID\n\
    <interval>   The number of samples before deleting the sets\n\
    ts_suffix    If this option is 1, the sets will be created\n\
                 with `:timestamp` appending at the end of the\n\
                 set name.\n\
";

static
const char *test_sampler_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int test_sampler_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      	      char *ebuf, int ebufsz)
{
	test_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int rc;
	json_entity_t v;
	char *s;

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	v = attr_find(inst, json, JSON_DICT_VALUE,
				"instances", 1, ebuf, ebufsz);
	if (!v)
		return EINVAL;

	/* schema_type */
	v = attr_find(inst, json, JSON_STRING_VALUE, "schema_type", 0, ebuf, ebufsz);
	if (v) {
		s = json_value_str(v)->str;
		if (0 == strncmp(s, "scalar", 6))
			inst->type = TEST_SAMPLER_TYPE_SCALAR;
		else if (0 == strncmp(s, "array", 5))
			inst->type = TEST_SAMPLER_TYPE_ARRAY;
		else if (0 == strncmp(s, "all", 3))
			inst->type = TEST_SAMPLER_TYPE_ALL;
		else if (0 == strncmp(s, "default", 7))
			inst->type = TEST_SAMPLER_TYPE_DEFAULT;
		else if (0 == strncmp(s, "manual", 7))
			inst->type = TEST_SAMPLER_TYPE_MANUAL;
		else {
			snprintf(ebuf, ebufsz, "%s: Invalid auto_type '%s'",
					pi->inst_name, s);
			return EINVAL;
		}
	} else {
		inst->type = TEST_SAMPLER_DEFAULT_SCHEMA_TYPE;
	}

	/* num_metrics */
	v = attr_find(inst, json, JSON_INT_VALUE, "num_metrics", 0, ebuf, ebufsz);
	if (v) {
		if (inst->type != TEST_SAMPLER_TYPE_DEFAULT) {
			snprintf(ebuf, ebufsz, "%s: num_metrics is ignored.",
								pi->inst_name);
		} else {
			inst->num_metrics = json_value_int(v);
		}
	} else {
		if (EINVAL == errno) {
			rc = errno;
			goto err;
		}
		inst->num_metrics = TEST_SAMPLER_DEFAULT_NUM_METRICS;
	}

	/* array_size */
	v = attr_find(inst, json, JSON_INT_VALUE, "array_size", 0, ebuf, ebufsz);
	if (v) {
		inst->array_sz = json_value_int(v);
	} else {
		if (EINVAL == errno) {
			rc = errno;
			goto err;
		}
		inst->array_sz = TEST_SAMPLER_DEFAULT_ARRAY_SZ;
	}

	/* unit */
	v = attr_find(inst, json, JSON_STRING_VALUE, "unit", 0, ebuf, ebufsz);
	if (v) {
		inst->unit = strdup(json_value_str(v)->str);
	} else {
		if (EINVAL == errno) {
			rc = errno;
			goto err;
		}
		inst->unit = strdup(TEST_SAMPLER_DEFAULT_UNIT);
	}
	if (!inst->unit) {
		rc = ENOMEM;
		goto err;
	}

	/* metrics */
	v = attr_find(inst, json, JSON_DICT_VALUE, "metrics", 0, ebuf, ebufsz);
	if (v) {
		if (inst->type != TEST_SAMPLER_TYPE_MANUAL) {
			snprintf(ebuf, ebufsz, "%s: metrics is ignored.",
							pi->inst_name);
			rc = EINVAL;
			goto err;
		} else {
			inst->metrics = json_entity_copy(v);
			if (!inst->metrics) {
				INST_LOG(inst, LDMSD_LCRITICAL, "Out of memory\n");
				rc = ENOMEM;
				goto err;
			}
		}
		rc = process_metrics(inst, v, ebuf, ebufsz);
		if (rc)
			goto err;
	} else {
		if (EINVAL == errno) {
			rc = errno;
			goto err;
		}
		inst->metrics = NULL;
	}

	/* instances */
	v = attr_find(inst, json, JSON_DICT_VALUE, "instances", 1, ebuf, ebufsz);
	if (!v) {
		snprintf(ebuf, ebufsz, "%s: instances is missing.", pi->inst_name);
		rc = EINVAL;
		goto err;
	}
	rc = process_instances(inst, v, ebuf, ebufsz);
	if (rc)
		goto err;

	/* Create schema */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema) {
		rc = errno;
		goto err;
	}

	/* Create set */
	struct test_sampler_set *ts_set;
	LIST_FOREACH(ts_set, &inst->set_list, entry) {
		ts_set->set = samp->create_set(pi, ts_set->name, samp->schema, ts_set);
		if (!ts_set->set) {
			rc = errno;
			goto err;
		}
	}

	return 0;
err:
	if (inst->unit)
		free(inst->unit);
	metric_list_empty(inst);
	set_list_empty(inst);
	return rc;
}

static
void test_sampler_del(ldmsd_plugin_inst_t pi)
{
	test_sampler_inst_t inst = (void*)pi;

	if (inst->metrics)
		json_entity_free(inst->metrics);
	set_list_empty(inst);
	metric_list_empty(inst);
}


static
json_entity_t test_sampler_query(ldmsd_plugin_inst_t pi, const char *q)
{
	json_entity_t result = ldmsd_sampler_query(pi, q);
	if (!result)
		return NULL;

	/* Only override the 'env' query. */
	if (0 != strcmp(q, "env"))
		return result;

	result = ldmsd_plugin_inst_query_env_add(result, "TEST_SAMPLER_ENV");
	if (!result)
		goto enomem;
	return result;
enomem:
	errno = ENOMEM;
	return NULL;
}

static
int test_sampler_init(ldmsd_plugin_inst_t pi)
{
	test_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(inst);
	/* override update_schema() and update_set() */
	samp->update_schema = test_sampler_update_schema;
	samp->update_set = test_sampler_update_set;
	/*
	 * Preserve the pointer to
	 * the regular sample routine of
	 * ldmsd_sampler_type
	 */
	inst->samp_sample = samp->sample;
	samp->sample = test_sampler_sample;

	/* NOTE More initialization code here if needed */
	samp->base.query = test_sampler_query;

	TAILQ_INIT(&inst->metric_list);
	return 0;
}

static
struct test_sampler_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "test_sampler",

                /* Common Plugin APIs */
		.desc   = test_sampler_desc,
		.help   = test_sampler_help,
		.init   = test_sampler_init,
		.del    = test_sampler_del,
		.config = test_sampler_config,
	},
	/* plugin-specific data initialization (for new()) here */
	.base_set_name = "set",
};

ldmsd_plugin_inst_t new()
{
	test_sampler_inst_t inst = calloc(1, sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
