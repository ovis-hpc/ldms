/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2018 Open Grid Computing, Inc. All rights reserved.
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

struct test_sampler_metric {
	char *name;
	int mtype; /* 2 is meta and 1 is data */
	enum ldms_value_type vtype;
	int count; /* Number of elements in an array metric */
	int idx;
	TAILQ_ENTRY(test_sampler_metric) entry;
	union ldms_value init_value;
	union ldms_value latest_value;
};
TAILQ_HEAD(test_sampler_metric_list, test_sampler_metric);

struct test_sampler_schema {
	char *name;
	ldms_schema_t schema;
	enum schema_type {
		TEST_SAMPLER_SCHEMA_TYPE_DEFAULT = 1,
		TEST_SAMPLER_SCHEMA_TYPE_MANUAL = 2,
		TEST_SAMPLER_SCHEMA_TYPE_AUTO = 3
	} type;
	struct test_sampler_metric_list list;
	LIST_ENTRY(test_sampler_schema) entry;
};
LIST_HEAD(test_sampler_schema_list, test_sampler_schema);

struct test_sampler_set {
	char *name;
	struct test_sampler_schema *ts_schema;
	ldms_set_t set;
	int push;
	int skip_push;
	LIST_ENTRY(test_sampler_set) entry;
};
LIST_HEAD(test_sampler_set_list, test_sampler_set);

typedef struct test_sampler_inst_s *test_sampler_inst_t;
struct test_sampler_inst_s {
	struct ldmsd_plugin_inst_s base;

	int (*samp_sample)(ldmsd_plugin_inst_t pi);

	char base_set_name[256];
	int num_sets;
	int num_metrics;
	struct test_sampler_schema_list schema_list;
	struct test_sampler_set_list set_list;
	int set_del_int;
	int ts_suffix; /* switch to append ts at the end of set name */
};

ldms_schema_t __schema_new(test_sampler_inst_t inst, const char *name)
{
	/* TODO Please change `create_schema()` API to receive name */
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(inst);
	const char *tmp;
	ldms_schema_t schema;

	tmp = samp->schema_name;
	samp->schema_name = (void*)name;
	schema = samp->create_schema(&inst->base);
	samp->schema_name = (void*)tmp;
	return schema;
}

static struct test_sampler_schema *__schema_find(
		struct test_sampler_schema_list *list, const char *name)
{
	struct test_sampler_schema *ts_schema;
	LIST_FOREACH(ts_schema, list, entry) {
		if (0== strcmp(ts_schema->name, name))
			return ts_schema;
	}
	return NULL;
}

static struct test_sampler_set *__set_find(
		struct test_sampler_set_list *list, const char *name)
{
	struct test_sampler_set *ts_set;
	LIST_FOREACH(ts_set, list, entry) {
		if (0 == strcmp(ts_set->name, name))
			return ts_set;
	}
	return NULL;
}

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

static struct test_sampler_metric *
__test_sampler_metric_new(test_sampler_inst_t inst, const char *name,
			  const char *mtype, enum ldms_value_type vtype,
			  const char *init_value, const char *count_str)
{
	int count = 0;
	struct test_sampler_metric *metric;
	if (!count_str) {
		count = 0;
	} else {
		count = atoi(count_str);
	}

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
	if (!metric->name) {
		free(metric);
		return NULL;
	}

	if ((0 == strcasecmp(mtype, "data")) || (0 == strcasecmp(mtype, "d"))) {
		metric->mtype = LDMS_MDESC_F_DATA;
	} else if ((0 == strcasecmp(mtype, "meta")) ||
			(0 == strcasecmp(mtype, "m"))) {
		metric->mtype = LDMS_MDESC_F_META;
	} else {
		free(metric->name);
		free(metric);
		return NULL;
	}

	metric->vtype = vtype;
	if (metric->vtype == LDMS_V_NONE) {
		free(metric->name);
		free(metric);
		return NULL;
	}

	if (__metric_init_value_set(inst, metric, init_value)) {
		free(metric->name);
		free(metric);
		return NULL;
	}

	metric->count = count;

	return metric;
}

static struct test_sampler_metric *
__schema_metric_new(test_sampler_inst_t inst, char *s)
{
	char *name, *mtype, *vtype, *init_value, *count_str, *ptr;
	name = strtok_r(s, ":", &ptr);
	if (!name)
		return NULL;
	mtype = strtok_r(NULL, ":", &ptr);
	if (!mtype)
		return NULL;
	vtype = strtok_r(NULL, ":", &ptr);
	if (!vtype)
		return NULL;
	init_value = strtok_r(NULL, ":", &ptr);
	if (!init_value)
		return NULL;
	count_str = strtok_r(NULL, ":", &ptr);
	if (!count_str)
		count_str = "0";

	struct test_sampler_metric *metric;
	metric = __test_sampler_metric_new(inst, name, mtype,
				ldms_metric_str_to_type(vtype),
				init_value, count_str);

	return metric;
}

void __schema_metric_destroy(struct test_sampler_metric *metric)
{
	free(metric->name);
	free(metric);
}

static int test_sampler_set_new(test_sampler_inst_t inst,
				struct test_sampler_set *ts_set);
static int test_sampler_set_del(test_sampler_inst_t inst,
				struct test_sampler_set *ts_set);

static struct test_sampler_set *
__create_test_sampler_set(test_sampler_inst_t inst, const char *instance_name,
			  int push, struct test_sampler_schema *ts_schema)
{
	int rc;
	struct test_sampler_set *ts_set = calloc(1, sizeof(*ts_set));
	if (!ts_set) {
		INST_LOG(inst, LDMSD_LERROR, "Out of memory\n");
		return NULL;
	}
	ts_set->name = strdup(instance_name);
	if (!ts_set->name)
		goto err0;
	ts_set->ts_schema = ts_schema;
	ts_set->push = push;
	ts_set->skip_push = 1;
	rc = test_sampler_set_new(inst, ts_set);
	if (rc)
		goto err1;
	LIST_INSERT_HEAD(&inst->set_list, ts_set, entry);
	return ts_set;

err1:
	free(ts_set->name);
err0:
	free(ts_set);
	return NULL;
}

static void __delete_test_sampler_set(test_sampler_inst_t inst,
				      struct test_sampler_set *ts_set)
{
	if (ts_set->set)
		test_sampler_set_del(inst, ts_set);
	if (ts_set->name)
		free(ts_set->name);
	LIST_REMOVE(ts_set, entry);
	free(ts_set);
}

/* create a new set for test_sampler_set */
static int test_sampler_set_new(test_sampler_inst_t inst,
				struct test_sampler_set *ts_set)
{
	char buff[512];
	const char *name;
	struct timeval tv;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(inst);
	if (ts_set->set)
		return EEXIST;
	if (inst->ts_suffix) {
		gettimeofday(&tv, NULL);
		snprintf(buff, sizeof(buff), "%s:%ld.%06ld",
			 ts_set->name, tv.tv_sec, tv.tv_usec);
		name = buff;
	} else {
		name = ts_set->name;
	}
	ts_set->set = samp->create_set(&inst->base, name,
				       ts_set->ts_schema->schema, ts_set);
	if (!ts_set->set)
		return errno;
	return 0;
}

static int test_sampler_set_del(test_sampler_inst_t inst,
				struct test_sampler_set *ts_set)
{
	if (!ts_set->set)
		return ENOENT;
	LDMSD_SAMPLER(inst)->delete_set(&inst->base, ldms_set_name_get(ts_set->set));
	ts_set->set = NULL;
	return 0;
}

static int create_metric_set(test_sampler_inst_t inst,
			     const char *schema_name, int push)
{
	int rc, i, j;
	union ldms_value v;
	char metric_name[128];
	char instance_name[512];
	ldms_schema_t schema = NULL;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(inst);

	struct test_sampler_schema *ts_schema;
	struct test_sampler_metric *metric;
	ts_schema = __schema_find(&inst->schema_list, (char *)schema_name);
	if (!ts_schema) {
		ts_schema = malloc(sizeof(*ts_schema));
		ts_schema->name = strdup(schema_name);
		ts_schema->type = TEST_SAMPLER_SCHEMA_TYPE_DEFAULT;
		schema = __schema_new(inst, schema_name);
		if (!schema)
			return ENOMEM;
		ts_schema->schema = schema;
		TAILQ_INIT(&ts_schema->list);
		for (i = 0; i < inst->num_metrics; i++) {
			snprintf(metric_name, 127, "metric_%d", i);
			metric = __test_sampler_metric_new(inst,
					metric_name, "d", LDMS_V_U64,
					"0", NULL);
			metric->idx = ldms_schema_metric_add(schema,
						metric_name, LDMS_V_U64, "");
			if (metric->idx < 0) {
				rc = ENOMEM;
				goto free_schema;
			}
			TAILQ_INSERT_TAIL(&ts_schema->list, metric, entry);
		}
		LIST_INSERT_HEAD(&inst->schema_list, ts_schema, entry);
	}

	struct test_sampler_set *ts_set, *next_ts_set;

	for (i = 0; i < inst->num_sets; i++) {
		snprintf(instance_name, 511, "%s_%d", inst->base_set_name, i);
		ts_set = __create_test_sampler_set(inst, instance_name,
						   push, ts_schema);
		if (!ts_set) {
			rc = errno;
			goto free_sets;
		}
		for (j = 0; j < inst->num_metrics; j++) {
			v.v_u64 = j;
			ldms_metric_set(ts_set->set, samp->first_idx + j, &v);
		}
	}

	return 0;

free_sets:
	ts_set = LIST_FIRST(&inst->set_list);
	if (!ts_set)
		goto free_schema;
	while (ts_set) {
		next_ts_set = LIST_NEXT(ts_set, entry);
		const char *sname = ldms_set_schema_name_get(ts_set->set);
		if (0 == strcmp(schema_name, sname)) {
			__delete_test_sampler_set(inst, ts_set);
		}
		ts_set = next_ts_set;
	}
free_schema:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	return rc;
}

static const char *__attr_find(test_sampler_inst_t inst, json_entity_t json,
		char *ebuf, size_t ebufsz, int is_required, char *attr_name)
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
	if (v->type != JSON_STRING_VALUE) {
		snprintf(ebuf, ebufsz, "%s: The given '%s' value is "
				"not a string.\n", inst->base.inst_name, attr_name);
		errno = EINVAL;
		return NULL;
	}
	return json_value_str(v)->str;
}

static int config_add_schema(test_sampler_inst_t inst, json_entity_t json,
						char *ebuf, size_t ebufsz)
{
	int rc = 0;
	struct test_sampler_schema *ts_schema;
	ldms_schema_t schema;
	const char *schema_name = __attr_find(inst, json, ebuf, ebufsz,
							1, "schema");
	if (!schema_name) {
		rc = errno;
		return rc;
	}

	ts_schema = __schema_find(&inst->schema_list, schema_name);
	if (ts_schema) {
		snprintf(ebuf, ebufsz, "Schema '%s' already exists.", schema_name);
		return EEXIST;
	}

	const char *metrics, *value, *set_array_card_str;
	const char *init_value = NULL;
	int num_metrics, set_array_card;
	metrics = __attr_find(inst, json, ebuf, ebufsz, 0, "metrics");
	if (!metrics && (errno == EINVAL))
		return EINVAL;
	value = __attr_find(inst, json, ebuf, ebufsz, 0, "num_metrics");
	if (!value && (errno == EINVAL))
		return EINVAL;
	if (!metrics && !value) {
		snprintf(ebuf, ebufsz, "Either metrics or num_metrics "
						"must be given.");
		return EINVAL;
	}

	set_array_card_str = __attr_find(inst, json, ebuf, ebufsz, 0, "set_array_card");
	if (!set_array_card_str) {
		if (errno == EINVAL)
			return EINVAL;
		else
			set_array_card = 1; /* Default */
	} else {
		set_array_card = strtol(set_array_card_str, NULL, 0);
	}

	ts_schema = malloc(sizeof(*ts_schema));
	if (!ts_schema) {
		INST_LOG(inst, LDMSD_LERROR, "Out of memory\n");
		return ENOMEM;
	}
	TAILQ_INIT(&ts_schema->list);
	LIST_INSERT_HEAD(&inst->schema_list, ts_schema, entry);
	struct test_sampler_metric *metric;
	if (metrics) {
		char *_metrics = strdup(metrics);
		if (!_metrics) {
			INST_LOG(inst, LDMSD_LERROR, "Out of memory\n");
			return ENOMEM;
		}
		ts_schema->type = TEST_SAMPLER_SCHEMA_TYPE_MANUAL;
		char *s, *ptr;
		s = strtok_r(_metrics, ",", &ptr);
		while (s) {
			metric = __schema_metric_new(inst, s);
			if (!metric) {
				rc = EINVAL;
				goto cleanup;
			}
			s = strtok_r(NULL, ",", &ptr);
			TAILQ_INSERT_TAIL(&ts_schema->list, metric, entry);
		}
	} else {
		ts_schema->type = TEST_SAMPLER_SCHEMA_TYPE_AUTO;
		enum ldms_value_type type;
		num_metrics = atoi(value);

		value = __attr_find(inst, json, ebuf, ebufsz, 0, "type");
		if (value) {
			type = ldms_metric_str_to_type(value);
			if (type == LDMS_V_NONE) {
				rc = EINVAL;
				goto cleanup;
			}
		} else {
			if (errno == EINVAL) {
				rc = EINVAL;
				goto cleanup;
			}
			type = LDMS_V_U64;
		}

		init_value = __attr_find(inst, json, ebuf, ebufsz, 0, "init_value");
		if (!init_value) {
			if (errno == EINVAL) {
				rc = EINVAL;
				goto cleanup;
			}
			init_value = "0";
		}

		int i;
		char name[128];
		for (i = 0; i < num_metrics; i++) {
			snprintf(name, 128, "%s%d", DEFAULT_METRIC_NAME_NAME, i);
			metric = __test_sampler_metric_new(inst, name, "data",
					type, init_value, "0");
			if (!metric) {
				INST_LOG(inst, LDMSD_LERROR,
					 "Failed to create metric.\n");
				goto cleanup;
			}
			TAILQ_INSERT_TAIL(&ts_schema->list, metric, entry);
		}
	}

	schema = __schema_new(inst, schema_name);
	if (!schema) {
		INST_LOG(inst, LDMSD_LERROR, "Failed to create a schema\n");
		rc = ENOMEM;
		goto cleanup;
	}
	ldms_schema_array_card_set(schema, set_array_card);
	TAILQ_FOREACH(metric, &ts_schema->list, entry) {
		if (metric->mtype == LDMS_MDESC_F_DATA) {
			if (ldms_type_is_array(metric->vtype)) {
				metric->idx = ldms_schema_metric_array_add(
						schema, metric->name,
						metric->vtype, "",
						metric->count);
			} else {
				metric->idx = ldms_schema_metric_add(
						schema, metric->name,
						metric->vtype, "");
			}
		} else {
			if (ldms_type_is_array(metric->vtype)) {
				metric->idx = ldms_schema_meta_array_add(
						schema, metric->name,
						metric->vtype, "",
						metric->count);
			} else {
				metric->idx = ldms_schema_meta_add(
						schema, metric->name,
						metric->vtype, "");
			}
		}
	}

	ts_schema->schema = schema;
	ts_schema->name = strdup(schema_name);
	return 0;

cleanup:
	metric = TAILQ_FIRST(&ts_schema->list);
	while (metric) {
		TAILQ_REMOVE(&ts_schema->list, metric, entry);
		__schema_metric_destroy(metric);
		metric = TAILQ_FIRST(&ts_schema->list);
	}
	LIST_REMOVE(ts_schema, entry);
	free(ts_schema);
	return rc;
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

static int config_add_set(test_sampler_inst_t inst, json_entity_t json,
						char *ebuf, size_t ebufsz)
{
	int rc = 0;
	struct test_sampler_schema *ts_schema;

	const char *schema_name = __attr_find(inst, json, ebuf, ebufsz, 1, "schema");
	if (!schema_name) {
		rc = errno;
		return rc;
	}

	const char *set_name = __attr_find(inst, json, ebuf, ebufsz, 1, "instance");
	if (!set_name) {
		rc = errno;
		return rc;
	}
	ts_schema = __schema_find(&inst->schema_list, schema_name);
	if (!ts_schema) {
		snprintf(ebuf, ebufsz, "Schema '%s' does not exist.", schema_name);
		return EINVAL;
	}

	const char *producer = __attr_find(inst, json, ebuf, ebufsz, 0, "producer");
	if (!producer && (errno == EINVAL)) {
		return EINVAL;
	}
	const char *compid = __attr_find(inst, json, ebuf, ebufsz, 0, "component_id");
	if (!compid && (errno == EINVAL))
		return EINVAL;
	const char *jobid = __attr_find(inst, json, ebuf, ebufsz, 0, "jobid");
	if (!jobid && (errno == EINVAL))
		return EINVAL;
	const char *push_s = __attr_find(inst, json, ebuf, ebufsz, 0, "push");
	if (!push_s && (errno == EINVAL))
		return EINVAL;
	int push = 0;
	if (push_s)
		push = atoi(push_s);

	struct test_sampler_set *ts_set;
	ts_set = __set_find(&inst->set_list, set_name);
	if (ts_set) {
		snprintf(ebuf, ebufsz, "Set '%s' already exists\n", set_name);
		return EINVAL;
	}

	ts_set = __create_test_sampler_set(inst, set_name, push, ts_schema);
	if (!ts_set) {
		rc = errno;
		goto err0;
	}

	union ldms_value v;
	int mid = 0;
	char *endptr;
	if (compid) {
		v.v_u64 = strtoull(compid, &endptr, 0);
		if (*endptr != '\0') {
			snprintf(ebuf, ebufsz, "invalid component_id %s\n", compid);
			rc = EINVAL;
			goto err1;
		}
	} else {
		v.v_u64 = 0;
	}
	mid = ldms_metric_by_name(ts_set->set, "component_id");
	if (mid >= 0)
		ldms_metric_set(ts_set->set, mid, &v);
	if (jobid) {
		v.v_u64 = strtoull(jobid, &endptr, 0);
		if (*endptr != '\0') {
			snprintf(ebuf, ebufsz, "invalid jobid %s\n", jobid);
			rc = EINVAL;
			goto err1;
		}
	} else {
		v.v_u64 = 0;
	}
	mid = ldms_metric_by_name(ts_set->set, "jobid");
	if (mid >= 0)
		ldms_metric_set(ts_set->set, mid, &v);

	int i;
	struct test_sampler_metric *metric;
	TAILQ_FOREACH(metric, &ts_schema->list, entry) {
		if (metric->vtype == LDMS_V_CHAR_ARRAY) {
			ldms_metric_array_set(ts_set->set, metric->idx,
					&(metric->init_value), 0, metric->count);
		} else if (ldms_type_is_array(metric->vtype)) {
			v = metric->init_value;
			for (i = 0; i < metric->count; i++) {
				ldms_metric_array_set_val(ts_set->set,
						metric->idx,
						i, &v);
				__val_inc(&v, __type_to_scalar(metric->vtype));
			}
		} else {
			ldms_metric_set(ts_set->set, metric->idx,
						&(metric->init_value));
		}
		metric->latest_value = metric->init_value;
	}

	if (producer) {
		ldms_set_producer_name_set(ts_set->set, producer);
	}

	return 0;
err1:
	__delete_test_sampler_set(inst, ts_set);
err0:
	return rc;
}

static int config_add_default(test_sampler_inst_t inst, json_entity_t json,
						char *ebuf, size_t ebufsz)
{
	const char *sname;
	const char *s;
	const char *push_s;
	int rc, push;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(inst);
	sname = __attr_find(inst, json, ebuf, ebufsz, 0, "schema");
	if (!sname && (errno = EINVAL))
		return EINVAL;
	if (!sname)
		sname = samp->schema_name;
	if (strlen(sname) == 0){
		snprintf(ebuf, ebufsz, "schema name '%s' is invalid.", sname);
		return EINVAL;
	}

	s = __attr_find(inst, json, ebuf, ebufsz, 0, "base");
	if (!s && (errno == EINVAL))
		return EINVAL;
	if (s) {
		snprintf(inst->base_set_name, sizeof(inst->base_set_name),
			 "%s", s);
	}

	s = __attr_find(inst, json, ebuf, ebufsz, 0, "num_sets");
	if (!s && (errno == EINVAL))
		return EINVAL;
	if (!s)
		inst->num_sets = DEFAULT_NUM_SETS;
	else
		inst->num_sets = atoi(s);

	s = __attr_find(inst, json, ebuf, ebufsz, 0, "num_metrics");
	if (!s && (errno == EINVAL))
		return EINVAL;
	if (!s)
		inst->num_metrics = DEFAULT_NUM_METRICS;
	else
		inst->num_metrics = atoi(s);

	push_s = __attr_find(inst, json, ebuf, ebufsz, 0, "push");
	if (!push_s && (errno == EINVAL))
		return EINVAL;
	if (push_s)
		push = atoi(push_s);
	else
		push = 0;

	rc = create_metric_set(inst, sname, push);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR, "failed to create metric sets.\n");
		return rc;
	}
	return 0;

}

struct test_sampler_schema *__test_sampler_schema_new(test_sampler_inst_t inst,
				const char *name, enum schema_type type,
				ldms_schema_t schema)
{
	struct test_sampler_schema *ts_schema;
	ts_schema = calloc(1, sizeof(*ts_schema));
	if (!ts_schema) {
		INST_LOG(inst, LDMSD_LERROR, "Out of memory\n");
		return NULL;
	}
	ts_schema->name = strdup(name);
	if (!ts_schema->name) {
		free(ts_schema);
		INST_LOG(inst, LDMSD_LERROR, "Out of memory\n");
		return NULL;
	}
	ts_schema->type = type;
	ts_schema->schema = schema;
	TAILQ_INIT(&ts_schema->list);
	LIST_INSERT_HEAD(&inst->schema_list, ts_schema, entry);
	return ts_schema;
}

static void __test_sampler_schema_delete(test_sampler_inst_t inst,
					 struct test_sampler_schema *ts_schema)
{
	struct test_sampler_metric *metric;
	LIST_REMOVE(ts_schema, entry);
	while ((metric = TAILQ_FIRST(&ts_schema->list))) {
		TAILQ_REMOVE(&ts_schema->list, metric, entry);
		__schema_metric_destroy(metric);
	}
	if (ts_schema->schema)
		ldms_schema_delete(ts_schema->schema);
	if (ts_schema->name)
		free(ts_schema->name);
	free(ts_schema);
}

static int __add_all_scalar(test_sampler_inst_t inst,
			    struct test_sampler_schema *ts_schema)
{
	const char *mname;
	ldms_schema_t schema = ts_schema->schema;
	struct test_sampler_metric *metric;
	enum ldms_value_type type;
	for (type = LDMS_V_FIRST; type < LDMS_V_LAST; type++) {
		if (ldms_type_is_array(type))
			break;
		mname = ldms_metric_type_to_str(type);
		metric = __test_sampler_metric_new(inst, mname, "data",
						type, "0", NULL);
		metric->idx = ldms_schema_metric_add(schema, mname, type, "");
		if (metric->idx < 0)
			return metric->idx;
		TAILQ_INSERT_TAIL(&ts_schema->list, metric, entry);
	}
	return 0;
}

static int __add_all_array(test_sampler_inst_t inst,
			   struct test_sampler_schema *ts_schema,
			   const char *array_sz_str)
{
	const char *mname;
	ldms_schema_t schema = ts_schema->schema;
	struct test_sampler_metric *metric;
	enum ldms_value_type type;
	for (type = LDMS_V_FIRST; type < LDMS_V_LAST; type++) {
		if (!ldms_type_is_array(type))
			continue;
		mname = ldms_metric_type_to_str(type);
		metric = __test_sampler_metric_new(inst, mname, "data",
						type, "0", array_sz_str);
		metric->idx = ldms_schema_metric_array_add(schema, mname, type,
							"", metric->count);
		if (metric->idx < 0)
			return metric->idx;
		TAILQ_INSERT_TAIL(&ts_schema->list, metric, entry);
	}
	return 0;
}

static int config_add_scalar(test_sampler_inst_t inst, json_entity_t json,
						char *ebuf, size_t ebufsz)
{
	const char *schema_name, *set_array_card_str;
	int set_array_card;
	struct test_sampler_schema *ts_schema;
	ldms_schema_t schema;
	int rc;

	schema_name = __attr_find(inst, json, ebuf, ebufsz, 0, "schema");
	if (!schema_name && (errno == EINVAL))
		return EINVAL;
	if (!schema_name)
		schema_name = "test_sampler_scalar";
	if (strlen(schema_name) == 0){
		snprintf(ebuf, ebufsz, "schema name '%s' is invalid.", schema_name);
		return EINVAL;
	}
	set_array_card_str = __attr_find(inst, json, ebuf, ebufsz, 0, "set_array_card");
	if (!set_array_card_str && (errno == EINVAL))
		return EINVAL;
	if (!set_array_card_str)
		set_array_card = 1;
	else
		set_array_card = strtol(set_array_card_str, NULL, 0);

	ts_schema = __schema_find(&inst->schema_list, schema_name);
	if (ts_schema) {
		snprintf(ebuf, ebufsz, "Schema '%s' already exists.\n",
			 schema_name);
		return EEXIST;
	}

	schema = __schema_new(inst, schema_name);
	if (!schema) {
		INST_LOG(inst, LDMSD_LERROR,
			 "test_sampler: Failed to create schema '%s'\n",
			 schema_name);
		return ENOMEM;
	}

	ts_schema = __test_sampler_schema_new(inst, schema_name,
				TEST_SAMPLER_SCHEMA_TYPE_AUTO, schema);
	if (!ts_schema)
		return ENOMEM;

	ldms_schema_array_card_set(schema, set_array_card);
	rc = __add_all_scalar(inst, ts_schema);
	if (rc < 0) {
		__test_sampler_schema_delete(inst, ts_schema);
		rc = -rc;
	}
	return rc;
}

static int config_add_array(test_sampler_inst_t inst, json_entity_t json,
						char *ebuf, size_t ebufsz)
{
	const char *schema_name, *set_array_card_str, *array_sz;
	int set_array_card;
	struct test_sampler_schema *ts_schema;
	ldms_schema_t schema;
	int rc;

	schema_name = __attr_find(inst, json, ebuf, ebufsz, 0, "schema");
	if (!schema_name && (errno == EINVAL))
		return EINVAL;
	if (!schema_name)
		schema_name = "test_sampler_array";
	if (strlen(schema_name) == 0){
		snprintf(ebuf, ebufsz, "schema name '%s' is invalid.", schema_name);
		return EINVAL;
	}
	set_array_card_str = __attr_find(inst, json, ebuf, ebufsz, 0, "set_array_card");
	if (!set_array_card_str && (errno == EINVAL))
		return EINVAL;
	if (!set_array_card_str)
		set_array_card = 1;
	else
		set_array_card = strtol(set_array_card_str, NULL, 0);
	array_sz = __attr_find(inst, json, ebuf, ebufsz, 1, "metric_array_sz");
	if (!array_sz) {
		rc = errno;
		return rc;
	}

	ts_schema = __schema_find(&inst->schema_list, schema_name);
	if (ts_schema) {
		snprintf(ebuf, ebufsz, "Schema '%s' already exists.", schema_name);
		return EEXIST;
	}

	schema = __schema_new(inst, schema_name);
	if (!schema) {
		INST_LOG(inst, LDMSD_LERROR, "Failed to create "
				"schema '%s'\n", schema_name);
		return ENOMEM;
	}

	ts_schema = __test_sampler_schema_new(inst, schema_name,
				TEST_SAMPLER_SCHEMA_TYPE_AUTO, schema);
	if (!ts_schema)
		return ENOMEM;

	ldms_schema_array_card_set(schema, set_array_card);
	rc = __add_all_array(inst, ts_schema, array_sz);
	if (rc < 0) {
		__test_sampler_schema_delete(inst, ts_schema);
		rc = -rc;
	}
	return rc;
}

static int config_add_all(test_sampler_inst_t inst, json_entity_t json,
					char *ebuf, size_t ebufsz)
{
	const char *schema_name, *set_array_card_str, *array_sz;
	int set_array_card;
	struct test_sampler_schema *ts_schema;
	ldms_schema_t schema;
	int rc;

	schema_name = __attr_find(inst, json, ebuf, ebufsz, 0, "schema");
	if (!schema_name && (errno == EINVAL))
		return EINVAL;
	if (!schema_name)
		schema_name = "test_sampler_all";
	if (strlen(schema_name) == 0){
		snprintf(ebuf, ebufsz, "schema name '%s' is invalid.", schema_name);
		return EINVAL;
	}
	set_array_card_str = __attr_find(inst, json, ebuf, ebufsz, 0, "set_array_card");
	if (!set_array_card_str && (errno == EINVAL))
		return EINVAL;
	if (!set_array_card_str)
		set_array_card = 1;
	else
		set_array_card = strtol(set_array_card_str, NULL, 0);
	array_sz = __attr_find(inst, json, ebuf, ebufsz, 1, "metric_array_sz");
	if (!array_sz) {
		rc = errno;
		return rc;
	}

	ts_schema = __schema_find(&inst->schema_list, schema_name);
	if (ts_schema) {
		snprintf(ebuf, ebufsz, "Schema '%s' already exists.",
			 schema_name);
		return EEXIST;
	}

	schema = __schema_new(inst, schema_name);
	if (!schema) {
		INST_LOG(inst, LDMSD_LERROR, "Failed to create schema '%s'\n",
			 schema_name);
		return ENOMEM;
	}

	ts_schema = __test_sampler_schema_new(inst, schema_name,
				TEST_SAMPLER_SCHEMA_TYPE_AUTO, schema);
	if (!ts_schema)
		return ENOMEM;

	ldms_schema_array_card_set(schema, set_array_card);
	rc = __add_all_scalar(inst, ts_schema);
	if (rc < 0) {
		__test_sampler_schema_delete(inst, ts_schema);
		rc = -rc;
		return rc;
	}
	rc = __add_all_array(inst, ts_schema, array_sz);
	if (rc < 0) {
		__test_sampler_schema_delete(inst, ts_schema);
		rc = -rc;
	}
	return rc;
}

static inline
void __metric_inc(ldms_set_t set, struct test_sampler_metric *metric, int i)
{
	union ldms_value v;
	switch (metric->vtype) {
	case LDMS_V_U8_ARRAY:
		v.v_u8 = ldms_metric_array_get_u8(set, metric->idx, i);
		v.v_u8++;
		ldms_metric_array_set_u8(set, metric->idx, i, v.v_u8);
		break;
	case LDMS_V_S8_ARRAY:
		v.v_s8 = ldms_metric_array_get_s8(set, metric->idx, i);
		v.v_s8++;
		ldms_metric_array_set_s8(set, metric->idx, i, v.v_s8);
		break;
	case LDMS_V_U16_ARRAY:
		v.v_u16 = ldms_metric_array_get_u16(set, metric->idx, i);
		v.v_u16++;
		ldms_metric_array_set_u16(set, metric->idx, i, v.v_u16);
		break;
	case LDMS_V_S16_ARRAY:
		v.v_s16 = ldms_metric_array_get_s16(set, metric->idx, i);
		v.v_s16++;
		ldms_metric_array_set_s16(set, metric->idx, i, v.v_s16);
		break;
	case LDMS_V_U32_ARRAY:
		v.v_u32 = ldms_metric_array_get_u32(set, metric->idx, i);
		v.v_u32++;
		ldms_metric_array_set_u32(set, metric->idx, i, v.v_u32);
		break;
	case LDMS_V_S32_ARRAY:
		v.v_s32 = ldms_metric_array_get_s32(set, metric->idx, i);
		v.v_s32++;
		ldms_metric_array_set_s32(set, metric->idx, i, v.v_s32);
		break;
	case LDMS_V_S64_ARRAY:
		v.v_s64 = ldms_metric_array_get_s64(set, metric->idx, i);
		v.v_s64++;
		ldms_metric_array_set_s64(set, metric->idx, i, v.v_s64);
		break;
	case LDMS_V_U64_ARRAY:
		v.v_u64 = ldms_metric_array_get_u64(set, metric->idx, i);
		v.v_u64++;
		ldms_metric_array_set_u64(set, metric->idx, i, v.v_u64);
		break;
	case LDMS_V_F32_ARRAY:
		v.v_f = ldms_metric_array_get_float(set, metric->idx, i);
		v.v_f++;
		ldms_metric_array_set_float(set, metric->idx, i, v.v_f);
		break;
	case LDMS_V_D64_ARRAY:
		v.v_d = ldms_metric_array_get_double(set, metric->idx, i);
		v.v_d++;
		ldms_metric_array_set_double(set, metric->idx, i, v.v_d);
		break;
	case LDMS_V_U8:
		v.v_u8 = ldms_metric_get_u8(set, metric->idx);
		v.v_u8++;
		ldms_metric_set_u8(set, metric->idx, v.v_u8);
		break;
	case LDMS_V_S8:
		v.v_s8 = ldms_metric_get_s8(set, metric->idx);
		v.v_s8++;
		ldms_metric_set_s8(set, metric->idx, v.v_s8);
		break;
	case LDMS_V_U16:
		v.v_u16 = ldms_metric_get_u16(set, metric->idx);
		v.v_u16++;
		ldms_metric_set_u16(set, metric->idx, v.v_u16);
		break;
	case LDMS_V_S16:
		v.v_s16 = ldms_metric_get_s16(set, metric->idx);
		v.v_s16++;
		ldms_metric_set_s16(set, metric->idx, v.v_s16);
		break;
	case LDMS_V_U32:
		v.v_u32 = ldms_metric_get_u32(set, metric->idx);
		v.v_u32++;
		ldms_metric_set_u32(set, metric->idx, v.v_u32);
		break;
	case LDMS_V_S32:
		v.v_s32 = ldms_metric_get_s32(set, metric->idx);
		v.v_s32++;
		ldms_metric_set_s32(set, metric->idx, v.v_s32);
		break;
	case LDMS_V_S64:
		v.v_s64 = ldms_metric_get_s64(set, metric->idx);
		v.v_s64++;
		ldms_metric_set_s64(set, metric->idx, v.v_s64);
		break;
	case LDMS_V_U64:
		v.v_u64 = ldms_metric_get_u64(set, metric->idx);
		v.v_u64++;
		ldms_metric_set_u64(set, metric->idx, v.v_u64);
		break;
	case LDMS_V_F32:
		v.v_f = ldms_metric_get_float(set, metric->idx);
		v.v_f++;
		ldms_metric_set_float(set, metric->idx, v.v_f);
		break;
	case LDMS_V_D64:
		v.v_d = ldms_metric_get_double(set, metric->idx);
		v.v_d++;
		ldms_metric_set_double(set, metric->idx, v.v_d);
		break;
	default:
		return;
	}
}

static void __metric_increment(test_sampler_inst_t inst,
			       struct test_sampler_metric *metric,
			       ldms_set_t set)
{
	int i = 0;

	if (ldms_type_is_array(metric->vtype)) {
		for (i = 0; i < metric->count; i++) {
			__metric_inc(set, metric, i);
		}
	} else {
		__metric_inc(set, metric, 0);
	}
}

static void __test_schema_free(struct test_sampler_schema *tschema)
{
	struct test_sampler_metric *tm;
	tm = TAILQ_FIRST(&tschema->list);
	while (tm) {
		TAILQ_REMOVE(&tschema->list, tm, entry);
		__schema_metric_destroy(tm);
		tm = TAILQ_FIRST(&tschema->list);
	}
	if (tschema->name) {
		free(tschema->name);
		tschema->name = NULL;
	}

	if (tschema->schema) {
		ldms_schema_delete(tschema->schema);
		tschema->schema = NULL;
	}

	free(tschema);
}


/* ============== Sampler Plugin APIs ================= */

static
int test_sampler_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	/* do nothing */
	return 0;
}

static
int test_sampler_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	test_sampler_inst_t inst = (void*)pi;
	struct test_sampler_set *ts_set = ctxt;
	struct test_sampler_metric *metric;

	TAILQ_FOREACH(metric, &ts_set->ts_schema->list, entry) {
		if (metric->mtype == LDMS_MDESC_F_META)
			continue;
		if (0 == strcmp(metric->name, "job_id"))
			continue;
		__metric_increment(inst, metric, set);
	}

	return 0;
}

int test_sampler_sample(ldmsd_plugin_inst_t pi)
{
	test_sampler_inst_t inst = (void*)pi;
	int rc;
	static int sample_count = 0;

	int j;
	struct test_sampler_set *ts_set;

	sample_count += 1;

	/* intercept normal sample routine to optionally delete sets */
	LIST_FOREACH(ts_set, &inst->set_list, entry) {
		if (inst->set_del_int > 0) {
			j = (sample_count) % inst->set_del_int;
			if (j == 0) {
				if (!ts_set->set)
					continue; /* no need to delete */
				/* delete the set */
				test_sampler_set_del(inst, ts_set);
				continue;
			}
			/* else, let through */
		}
		if (!ts_set->set) {
			/* try re-creating the set */
			rc = test_sampler_set_new(inst, ts_set);
			if (rc)
				continue;
		}

	}
	/* then, resume normal sample routine */
	inst->samp_sample(&inst->base);

	/* all sets updated, do push routine */
	LIST_FOREACH(ts_set, &inst->set_list, entry) {
		if (!ts_set->push)
			continue;
		if (ts_set->push == ts_set->skip_push) {
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

typedef int (*action_fn)(test_sampler_inst_t inst, json_entity_t json,
					char *ebuf, size_t ebufsz);

static
int test_sampler_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	test_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int rc;
	const char *action;
	const char *compid;
	const char *jobid;
	const char *set_del_int_str;
	const char *ts_suffix_str;
	const char *producer_name;
	action_fn act_fn;

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	action = __attr_find(inst, json, ebuf, ebufsz, 0, "action");
	if (!action && (errno == EINVAL))
		return EINVAL;
	if (action) {
		rc = 0;
		if (0 == strcmp(action, "add_schema")) {
			act_fn = config_add_schema;
		} else if (0 == strcmp(action, "add_set")) {
			act_fn = config_add_set;
		} else if (0 == strcmp(action, "default")) {
			act_fn = config_add_default;
		} else if (0 == strcmp(action, "add_scalar")) {
			act_fn = config_add_scalar;
		} else if (0 == strcmp(action, "add_array")) {
			act_fn = config_add_array;
		} else if (0 == strcmp(action, "add_all")) {
			act_fn = config_add_all;
		} else {
			act_fn = NULL;
			snprintf(ebuf, ebufsz,
					"Unrecognized action '%s'.\n", action);
			rc = EINVAL;
		}
		if (act_fn)
			rc = act_fn(inst, json, ebuf, ebufsz);
		return rc;
	}

	producer_name = __attr_find(inst, json, ebuf, ebufsz, 0, "producer");
	if (!producer_name && (errno == EINVAL))
		return EINVAL;
	compid = __attr_find(inst, json, ebuf, ebufsz, 0, "component_id");
	if (!compid && (errno == EINVAL))
		return EINVAL;
	jobid = __attr_find(inst, json, ebuf, ebufsz, 0, "jobid");
	if (!jobid && (errno == EINVAL))
		return EINVAL;
	set_del_int_str = __attr_find(inst, json, ebuf, ebufsz, 0, "set_delete_interval");
	if (!set_del_int_str && (errno == EINVAL))
		return EINVAL;
	ts_suffix_str = __attr_find(inst, json, ebuf, ebufsz, 0, "ts_suffix");
	if (!ts_suffix_str && (errno == EINVAL))
		return EINVAL;

	if (!compid)
		compid = "0";
	if (!jobid)
		jobid = "0";
	if (set_del_int_str)
		inst->set_del_int = atoi(set_del_int_str);
	if (ts_suffix_str)
		inst->ts_suffix = atoi(ts_suffix_str);

	struct test_sampler_set *ts_set;
	union ldms_value vcompid, vjobid;
	sscanf(compid, "%" SCNu64, &vcompid.v_u64);
	sscanf(jobid, "%" SCNu64, &vjobid.v_u64);
	int mid;

	LIST_FOREACH(ts_set, &inst->set_list, entry) {
		if (producer_name)
			ldms_set_producer_name_set(ts_set->set, producer_name);
		if (compid) {

			mid = ldms_metric_by_name(ts_set->set, "component_id");
			if (mid < 0) {
				INST_LOG(inst, LDMSD_LINFO,
					 "No component_id in set '%s'\n",
					 ts_set->name);
				continue;
			}
			ldms_metric_set(ts_set->set, mid, &vcompid);
		}
		if (jobid) {
			mid = ldms_metric_by_name(ts_set->set, "jobid");
			if (mid < 0) {
				INST_LOG(inst, LDMSD_LINFO,
					 "No job ID in set '%s'\n",
					 ts_set->name);
				continue;
			}
			ldms_metric_set(ts_set->set, mid, &vjobid);
		}
	}
	return 0;
}

static
void test_sampler_del(ldmsd_plugin_inst_t pi)
{
	test_sampler_inst_t inst = (void*)pi;
	struct test_sampler_set *ts;
	struct test_sampler_schema *tschema;

	while ((ts = LIST_FIRST(&inst->set_list))) {
		__delete_test_sampler_set(inst, ts);
	}

	while ((tschema = LIST_FIRST(&inst->schema_list))) {
		LIST_REMOVE(tschema, entry);
		__test_schema_free(tschema);
	}
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

	json_entity_t attr, envs, str;
	envs = json_entity_new(JSON_LIST_VALUE);
	if (!envs)
		goto enomem;
	attr = json_entity_new(JSON_ATTR_VALUE, "env", envs);
	if (!attr) {
		json_entity_free(envs);
		goto enomem;
	}
	str = json_entity_new(JSON_STRING_VALUE, "TEST_SAMPLER_ENV");
	if (!str) {
		json_entity_free(attr);
		goto enomem;
	}
	json_item_add(envs, str);
	json_attr_add(result, attr);
	return result;

enomem:
	ldmsd_plugin_qjson_err_set(result, ENOMEM, "Out of memory");
	return result;
}

static
int test_sampler_init(ldmsd_plugin_inst_t pi)
{
	test_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	/* override update_schema() and update_set() */
	samp->update_schema = test_sampler_update_schema;
	samp->update_set = test_sampler_update_set;
	inst->samp_sample = samp->sample;
	samp->sample = test_sampler_sample;

	/* NOTE More initialization code here if needed */
	samp->base.query = test_sampler_query;
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
	test_sampler_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
