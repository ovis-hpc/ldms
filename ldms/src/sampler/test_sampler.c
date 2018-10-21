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
#include "ldms.h"
#include "ldmsd.h"

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

static ldms_set_t *set_array;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;
static char *default_base_set_name = "set";
static char *base_set_name ;
static int num_sets;
static int num_metrics;
static char *default_schema_name = "test_sampler";
static struct test_sampler_schema_list schema_list;
static struct test_sampler_set_list set_list;

static struct test_sampler_schema *__schema_find(
		struct test_sampler_schema_list *list, char *name)
{
	struct test_sampler_schema *ts_schema;
	LIST_FOREACH(ts_schema, list, entry) {
		if (0== strcmp(ts_schema->name, name))
			return ts_schema;
	}
	return NULL;
}

static struct test_sampler_set *__set_find(
		struct test_sampler_set_list *list, char *name)
{
	struct test_sampler_set *ts_set;
	LIST_FOREACH(ts_set, list, entry) {
		if (0 == strcmp(ts_set->name, name))
			return ts_set;
	}
	return NULL;
}

/* metric->vtype MUST be initialized before this function is called. */
static int __metric_init_value_set(struct test_sampler_metric *metric,
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
		msglog(LDMSD_LERROR, "test_sampler: Unrecognized/not supported"
					"type '%s'\n", metric->vtype);
		return EINVAL;
	}
	return 0;
}

static struct test_sampler_metric *
__test_sampler_metric_new(const char *name, const char *mtype,
		enum ldms_value_type vtype, const char *init_value,
		const char *count_str)
{
	int count = 0;
	struct test_sampler_metric *metric;

	count = atoi(count_str);
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

	if (__metric_init_value_set(metric, init_value)) {
		free(metric->name);
		free(metric);
		return NULL;
	}

	metric->count = count;

	return metric;
}

static struct test_sampler_metric *__schema_metric_new(char *s)
{
	char *name, *mtype, *vtype, *init_value, *count_str, *ptr;
	int count = 0;
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
	metric = __test_sampler_metric_new(name, mtype,
				ldms_metric_str_to_type(vtype),
				init_value, count_str);

	return metric;
}

void __schema_metric_destroy(struct test_sampler_metric *metric)
{
	free(metric->name);
	free(metric);
}

static struct test_sampler_set *__create_test_sampler_set(ldms_set_t set,
					char *instance_name, int push,
				struct test_sampler_schema *ts_schema)
{
	struct test_sampler_set *ts_set = malloc(sizeof(*ts_set));
	if (!ts_set) {
		msglog(LDMSD_LERROR, "test_sampler: Out of memory\n");
		return NULL;
	}
	ts_set->set = set;
	ts_set->name = strdup(instance_name);
	ts_set->ts_schema = ts_schema;
	ts_set->push = push;
	ts_set->skip_push = 1;
	LIST_INSERT_HEAD(&set_list, ts_set, entry);
	ldms_set_publish(set);
	ldmsd_set_register(set, "test_sampler");
	return ts_set;
}

static int create_metric_set(const char *schema_name, int push)
{
	int rc, i, j;
	ldms_set_t set;
	union ldms_value v;
	char *s;
	char metric_name[128];
	char instance_name[128];

	struct test_sampler_schema *ts_schema;
	struct test_sampler_metric *metric;
	ts_schema = __schema_find(&schema_list, (char *)schema_name);
	if (!ts_schema) {
		ts_schema = malloc(sizeof(*ts_schema));
		ts_schema->name = strdup(schema_name);
		ts_schema->type = TEST_SAMPLER_SCHEMA_TYPE_DEFAULT;
		schema = ldms_schema_new(schema_name);
		if (!schema)
			return ENOMEM;
		ts_schema->schema = schema;
		TAILQ_INIT(&ts_schema->list);
		for (i = 0; i < num_metrics; i++) {
			snprintf(metric_name, 127, "metric_%d", i);
			metric = malloc(sizeof(*metric));
			metric->name = strdup(metric_name);
			metric->idx = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
			if (metric->idx < 0) {
				rc = ENOMEM;
				goto free_schema;
			}
			TAILQ_INSERT_TAIL(&ts_schema->list, metric, entry);
		}
		LIST_INSERT_HEAD(&schema_list, ts_schema, entry);
	}

	struct test_sampler_set *ts_set, *next_ts_set;
	set_array = calloc(num_sets, sizeof(ldms_set_t));
	if (!set_array) {
		msglog(LDMSD_LERROR, "test_sampler: Out of memory\n");
		return ENOMEM;
	}

	for (i = 0; i < num_sets; i++) {
		snprintf(instance_name, 127, "%s_%d", base_set_name, i);
		set = ldms_set_new(instance_name, schema);
		if (!set) {
			rc = ENOMEM;
			goto free_sets;
		}

		for (j = 0; j < num_metrics; j++) {
			v.v_u64 = j;
			ldms_metric_set(set, j, &v);
		}
		set_array[i] = set;
		ts_set = __create_test_sampler_set(set, instance_name,
						push, ts_schema);
		if (!ts_set)
			goto free_sets;
	}

	return 0;

free_sets:
	ts_set = LIST_FIRST(&set_list);
	if (!ts_set)
		goto free_schema;
	next_ts_set = LIST_NEXT(ts_set, entry);
	while (ts_set) {
		if (0 == strcmp(schema_name, ldms_set_schema_name_get(ts_set->set))) {
			LIST_REMOVE(ts_set, entry);
			ldms_set_delete(ts_set->set);
			free(ts_set->name);
			free(ts_set);
		}
		ts_set = next_ts_set;
		if (!ts_set)
			break;
	}
free_schema:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	return rc;
}


static int config_add_schema(struct attr_value_list *avl)
{
	int rc = 0;
	struct test_sampler_schema *ts_schema;
	ldms_schema_t schema;
	char *schema_name = av_value(avl, "schema");
	if (!schema_name) {
		msglog(LDMSD_LERROR, "test_sampler: Need schema_name\n");
		return EINVAL;
	}

	ts_schema = __schema_find(&schema_list, schema_name);
	if (ts_schema) {
		msglog(LDMSD_LERROR, "test_sampler: Schema '%s' already "
				"exists.\n", schema_name);
		return EEXIST;
	}

	char *metrics, *value, *set_array_card_str;
	char *init_value = NULL;
	int num_metrics, set_array_card;
	metrics = av_value(avl, "metrics");
	value = av_value(avl, "num_metrics");
	if (!metrics && !value) {
		msglog(LDMSD_LERROR, "test_sampler: Either metrics or "
				"num_metrics must be given\n");
		return EINVAL;
	}

	set_array_card_str = av_value(avl, "set_array_card");
	if (!set_array_card_str)
		set_array_card = 1; /* Default */
	else
		set_array_card = strtol(set_array_card_str, NULL, 0);
	ts_schema = malloc(sizeof(*ts_schema));
	if (!ts_schema) {
		msglog(LDMSD_LERROR, "test_sampler: Out of memory\n");
		return ENOMEM;
	}
	int is_need_int = 0;
	TAILQ_INIT(&ts_schema->list);
	LIST_INSERT_HEAD(&schema_list, ts_schema, entry);
	struct test_sampler_metric *metric;
	if (metrics) {
		ts_schema->type = TEST_SAMPLER_SCHEMA_TYPE_MANUAL;
		char *s, *ptr;
		s = strtok_r(metrics, ",", &ptr);
		while (s) {
			metric = __schema_metric_new(s);
			if (!metric) {
				rc = EINVAL;
				goto cleanup;
			}
			s = strtok_r(NULL, ",", &ptr);
			TAILQ_INSERT_TAIL(&ts_schema->list, metric, entry);
		}
	} else {
		ts_schema->type = TEST_SAMPLER_SCHEMA_TYPE_AUTO;
		is_need_int = 1;
		enum ldms_value_type type;
		int count = 1; /* Number of elements of an array metric */
		num_metrics = atoi(value);

		value = av_value(avl, "type");
		if (value) {
			type = ldms_metric_str_to_type(value);
			if (type == LDMS_V_NONE) {
				rc = EINVAL;
				goto cleanup;
			}
		} else {
			type = LDMS_V_U64;
		}

		value = av_value(avl, "count");
		if (value)
			count = atoi(value);

		init_value = av_value(avl, "init_value");
		if (!init_value)
			init_value = "0";

		int i;
		char name[128];
		for (i = 0; i < num_metrics; i++) {
			snprintf(name, 128, "%s%d", DEFAULT_METRIC_NAME_NAME, i);
			metric = __test_sampler_metric_new(name, "data",
					type, init_value, "0");
			if (!metric) {
				msglog(LDMSD_LERROR,
					"test_sampler: Failed to create metric.\n");
				goto cleanup;
			}
		}
	}

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		msglog(LDMSD_LERROR, "test_sampler: Failed to create a schema\n");
		rc = ENOMEM;
		goto cleanup;
	}
	ldms_schema_array_card_set(schema, set_array_card);
	if (!metrics) {
		ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
		ldms_schema_metric_add(schema, "job_id", LDMS_V_U64);
	}
	TAILQ_FOREACH(metric, &ts_schema->list, entry) {
		if (metric->mtype == LDMS_MDESC_F_DATA) {
			if (ldms_type_is_array(metric->vtype)) {
				metric->idx = ldms_schema_metric_array_add(schema,
						metric->name, metric->vtype,
						metric->count);
			} else {
				metric->idx = ldms_schema_metric_add(schema,
						metric->name, metric->vtype);
			}
		} else {
			if (ldms_type_is_array(metric->vtype)) {
				metric->idx = ldms_schema_meta_array_add(schema,
						metric->name, metric->vtype,
						metric->count);
			} else {
				metric->idx = ldms_schema_meta_add(schema,
						metric->name, metric->vtype);
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

static int config_add_set(struct attr_value_list *avl)
{
	int rc = 0;
	struct test_sampler_schema *ts_schema;
	ldms_set_t set;

	char *schema_name = av_value(avl, "schema");
	if (!schema_name) {
		msglog(LDMSD_LERROR, "test_sampler: Need schema name\n");
		return EINVAL;
	}

	char *set_name = av_value(avl, "instance");
	if (!set_name) {
		msglog(LDMSD_LERROR, "test_sampler: Need set name\n");
		return EINVAL;
	}
	ts_schema = __schema_find(&schema_list, schema_name);
	if (!ts_schema) {
		msglog(LDMSD_LERROR, "test_sampler: Schema '%s' does not "
				"exist.\n", schema_name);
		return EINVAL;
	}

	char *producer = av_value(avl, "producer");
	char *compid = av_value(avl, "component_id");
	char *jobid = av_value(avl, "jobid");

	char *push_s = av_value(avl, "push");
	int push = 0;
	if (push_s)
		push = atoi(push_s);

	struct test_sampler_set *ts_set;
	ts_set = __set_find(&set_list, set_name);
	if (ts_set) {
		msglog(LDMSD_LERROR, "test_sampler: Set '%s' already "
				"exists\n", set_name);
		return EINVAL;
	}

	set = ldms_set_new(set_name, ts_schema->schema);
	if (!set) {
		msglog(LDMSD_LERROR, "test_sampler: Cannot create "
				"set '%s'\n", set_name);
		return ENOMEM;
	}
	ts_set = __create_test_sampler_set(set, set_name, push, ts_schema);
	if (!ts_set) {
		rc = ENOMEM;
		goto err0;
	}

	union ldms_value v;
	int mid = 0;
	char *endptr;
	if (compid) {
		v.v_u64 = strtoull(compid, &endptr, 0);
		if (*endptr != '\0') {
			msglog(LDMSD_LERROR, "test_sampler: invalid "
					"component_id %s\n", compid);
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
			msglog(LDMSD_LERROR, "test_sampler: invalid "
					"jobid %s\n", jobid);
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
			for (i = 0; i < metric->count; i++) {
				ldms_metric_array_set_val(ts_set->set,
						metric->idx,
						i, &(metric->init_value));
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
err0:
	ldms_set_delete(set);
err1:
	free(ts_set);
	return rc;
}

static int config_add_default(struct attr_value_list *avl)
{
	char *sname;
	char *s;
	char *push_s;
	int rc, push;
	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0){
		msglog(LDMSD_LERROR, "test_sampler: schema name invalid.\n");
		return EINVAL;
	}

	base_set_name = av_value(avl, "base");
	if (!base_set_name)
		base_set_name = default_base_set_name;

	s = av_value(avl, "num_sets");
	if (!s)
		num_sets = DEFAULT_NUM_SETS;
	else
		num_sets = atoi(s);

	s = av_value(avl, "num_metrics");
	if (!s)
		num_metrics = DEFAULT_NUM_METRICS;
	else
		num_metrics = atoi(s);

	push_s = av_value(avl, "push");
	if (push_s)
		push = atoi(push_s);
	else
		push = 0;

	rc = create_metric_set(sname, push);
	if (rc) {
		msglog(LDMSD_LERROR, "test_sampler: failed to create metric sets.\n");
		return rc;
	}
	return 0;

}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *action;
	char *s;
	char *compid;
	char *jobid;
	char *push;
	void * arg = NULL;
	int rc;

	action = av_value(avl, "action");
	if (action) {
		rc = 0;
		if (0 == strcmp(action, "add_schema")) {
			rc = config_add_schema(avl);
		} else if (0 == strcmp(action, "add_set")) {
			rc = config_add_set(avl);
		} else if (0 == strcmp(action, "default")) {
			rc = config_add_default(avl);
		} else {
			msglog(LDMSD_LERROR, "test_sampler: Unrecognized "
				"action '%s'.\n", action);
			rc = EINVAL;
		}
		return rc;
	}

	producer_name = av_value(avl, "producer");
	compid = av_value(avl, "component_id");
	jobid = av_value(avl, "jobid");

	if (!compid)
		compid = "0";
	if (!jobid)
		jobid = "0";

	struct test_sampler_set *ts_set;
	union ldms_value vcompid, vjobid;
	sscanf(compid, "%" SCNu64, &vcompid.v_u64);
	sscanf(jobid, "%" SCNu64, &vjobid.v_u64);
	int mid;
	LIST_FOREACH(ts_set, &set_list, entry) {
		if (producer_name)
			ldms_set_producer_name_set(ts_set->set, producer_name);
		if (compid) {

			mid = ldms_metric_by_name(ts_set->set, "component_id");
			if (mid < 0) {
				msglog(LDMSD_LINFO, "No component_id in "
						"set '%s'\n", ts_set->name);
				continue;
			}
			ldms_metric_set(ts_set->set, mid, &vcompid);
		}
		if (jobid) {
			mid = ldms_metric_by_name(ts_set->set, "jobid");
			if (mid < 0) {
				msglog(LDMSD_LINFO, "No job ID in "
						"set '%s'\n", ts_set->name);
				continue;
			}
			ldms_metric_set(ts_set->set, mid, &vjobid);
		}
	}

	push = av_value(avl, "push");

	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	assert(0 == "not implemented");
}

static void __metric_increment(struct test_sampler_metric *metric, ldms_set_t set)
{
	union ldms_value v = metric->latest_value;
	int i = 0;

	if (ldms_type_is_array(metric->vtype)) {
		for (i = 0; i < metric->count; i++) {
			switch (metric->vtype) {
			case LDMS_V_U8_ARRAY:
			case LDMS_V_S8_ARRAY:
			case LDMS_V_U16_ARRAY:
			case LDMS_V_S16_ARRAY:
			case LDMS_V_U32_ARRAY:
			case LDMS_V_S32_ARRAY:
			case LDMS_V_S64_ARRAY:
			case LDMS_V_U64_ARRAY:
				metric->latest_value.v_u64++;
				break;
			case LDMS_V_F32_ARRAY:
				metric->latest_value.v_f++;
				break;
			case LDMS_V_D64_ARRAY:
				metric->latest_value.v_d++;
				break;
			default:
				return;
			}
			ldms_metric_array_set_val(set, metric->idx, i, &metric->latest_value);
		}
	} else {
		switch (metric->vtype) {
		case LDMS_V_U8:
		case LDMS_V_S8:
		case LDMS_V_U16:
		case LDMS_V_S16:
		case LDMS_V_U32:
		case LDMS_V_S32:
		case LDMS_V_S64:
		case LDMS_V_U64:
			metric->latest_value.v_u64++;
			break;
		case LDMS_V_F32:
			metric->latest_value.v_f++;
			break;
		case LDMS_V_D64:
			metric->latest_value.v_d++;
			break;
		default:
			return;
		}
		ldms_metric_set(set, metric->idx, &v);
	}
}

static int sample(struct ldmsd_sampler *self)
{
	int rc;
	union ldms_value v;
	ldms_set_t set;

	int j;
	struct test_sampler_set *ts_set;
	struct test_sampler_metric *metric;
	LIST_FOREACH(ts_set, &set_list, entry) {
		set = ts_set->set;
		ldms_transaction_begin(set);
		TAILQ_FOREACH(metric, &ts_set->ts_schema->list, entry) {
			if (metric->mtype == LDMS_MDESC_F_META)
				continue;
			if (0 == strcmp(metric->name, "job_id"))
				continue;

			__metric_increment(metric, set);
		}

		ldms_transaction_end(set);

		if (ts_set->push) {
			if (ts_set->push == ts_set->skip_push) {
				ldms_xprt_push(set);
				ts_set->skip_push = 1;
			} else {
				ts_set->skip_push++;
			}
		}
	}
	return 0;
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

static void term(struct ldmsd_plugin *self)
{
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	int i;
	if (set_array) {
		for (i = 0; i < num_sets; i++) {
			if (set_array[i]) {
				ldmsd_set_deregister(ldms_set_instance_name_get(set_array[i]),
							"test_sampler");
				ldms_set_delete(set_array[i]);
			}
			set_array[i] = NULL;
		}
		free(set_array);
	}
	struct test_sampler_set *ts;
	ts = LIST_FIRST(&set_list);
	while (ts) {
		LIST_REMOVE(ts, entry);
		ldmsd_set_deregister(ts->name, "test_sampler");
		ldms_set_delete(ts->set);
		free(ts->name);
		free(ts);
		ts = LIST_FIRST(&set_list);
	}

	struct test_sampler_schema *tschema;
	tschema = LIST_FIRST(&schema_list);
	while (tschema) {
		LIST_REMOVE(tschema, entry);
		__test_schema_free(tschema);
		tschema = LIST_FIRST(&schema_list);
	}
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=test_sampler action=add_schema schema=<schema_name>\n"
		"       [set_array_card=number of elements in the set ring buffer]\n"
		"       [metrics=<metric name>:<metric type>:<value type>:<init value>,<metric name>:<metric type>:<value type>:<init value>,...]\n"
		"       [num_metrics=<num_metrics>] [type=<metric type>]\n"
		"	[init_value=<init_value>]\n"
		"\n"
		"    Either giving metrics or num_metrics. The default init value\n"
		"    is the metric index.\n"
		"    \n"
		"    If 'metrics' is given, the meta and data metrics in the schema will be exactly as the given metric list.\n"
		"    Otherwise, 'component_id' and 'job_id' metrics will be automatically added to the schema.\n"
		"    The valid metric types are either 'meta' or 'data'.\n"
		"    The valid value types are, for example, D64, F32, S64, U64, S64_ARRAY\n"
		""
		"\n"
		"config name=test_sampler action=add_set instance=<set_name>\n"
		"       schema=<schema_name>\n"
		"       [producer=<producer>] [component_id=<compid>] [jobid=<jobid>]\n"
		"       [push=<push>]\n"
		"\n"
		"    <set name>      The set name\n"
		"    <schema name>   The schema name\n"
		"    <producer>      The producer name\n"
		"    <compid>        The component ID\n"
		"    <jobid>         The job ID\n"
		"    <push>          A positive number. The default is 0 meaning no pushing update.\n."
		"                    1 means the sampler will push every update,\n"
		"                    2 means the sampler will push every other update,\n"
		"                    3 means the sampler will push every third updates,\n"
		"                    and so on.\n"
		"\n"
		"config name=test_sampler action=default [base=<base>] [schema=<sname>]\n"
		"       [num_sets=<nsets>] [num_metrics=<nmetrics>] [push=<push>]\n"
		"\n"
		"    <base>       The base of set names\n"
		"    <sname>      Optional schema name. Defaults to 'test_sampler'\n"
		"    <nsets>      Number of sets\n"
		"    <nmetrics>   Number of metrics\n"
		"    <push>       A positive number. The default is 0 meaning no pushing update.\n"
		"                 1 means the sampler will push every update,\n"
		"                 2 means the sampler will push every other update,\n"
		"                 3 means the sampler will push every third updates,\n"
		"                 and so on.\n"
		"\n"
		"config name=test_sampler [producer=<prod_name>] [component_id=<comp_id>] [jobid=<jobid>]\n"
		"    <prod_name>  The producer name\n"
		"    <comp_id>    The component ID\n"
		"    <jobid>      The job ID\n";

}

static struct ldmsd_sampler test_sampler_plugin = {
	.base = {
		.name = "test_sampler",
		.type = LDMSD_PLUGIN_SAMPLER,
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
	LIST_INIT(&schema_list);
	LIST_INIT(&set_list);
	return &test_sampler_plugin.base;
}
