/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
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
	enum ldms_value_type type;
	union ldms_value init_value;
	int idx;
	TAILQ_ENTRY(test_sampler_metric) entry;
};
TAILQ_HEAD(test_sampler_metric_list, test_sampler_metric);

struct test_sampler_schema {
	char *name;
	ldms_schema_t schema;
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
static int test_sampler_compid_idx;
static int test_sampler_jobid_idx;

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

static struct test_sampler_metric *__schema_metric_new(char *s)
{
	char *name, *type, *value, *ptr;
	name = strtok_r(s, ":", &ptr);
	if (!name)
		return NULL;
	type = strtok_r(NULL, ":", &ptr);
	if (!type)
		return NULL;
	value = strtok_r(NULL, ":", &ptr);
	if (!value)
		return NULL;

	struct test_sampler_metric *metric = malloc(sizeof(*metric));
	if (!metric)
		return NULL;
	metric->name = strdup(name);
	if (!metric->name) {
		free(metric);
		return NULL;
	}

	metric->type = ldms_metric_str_to_type(type);
	if (metric->type == LDMS_V_NONE) {
		free(metric->name);
		free(metric);
		return NULL;
	}

	switch (metric->type) {
	case LDMS_V_U8:
		sscanf(value, "%" SCNu8, &(metric->init_value.v_u8));
		break;
	case LDMS_V_U16:
		sscanf(value, "%" SCNu16, &(metric->init_value.v_u16));
		break;
	case LDMS_V_U32:
		sscanf(value, "%" SCNu32, &(metric->init_value.v_u32));
		break;
	case LDMS_V_U64:
		sscanf(value, "%" SCNu64, &(metric->init_value.v_u64));
		break;
	case LDMS_V_S8:
		sscanf(value, "%" SCNi8, &(metric->init_value.v_s8));
		break;
	case LDMS_V_S16:
		sscanf(value, "%" SCNi16, &(metric->init_value.v_s16));
		break;
	case LDMS_V_S32:
		sscanf(value, "%" SCNi32, &(metric->init_value.v_s32));
		break;
	case LDMS_V_S64:
		sscanf(value, "%" SCNi64, &(metric->init_value.v_s64));
		break;
	case LDMS_V_F32:
		metric->init_value.v_f = strtof(value, NULL);
		break;
	case LDMS_V_D64:
		metric->init_value.v_d = strtod(value, NULL);
		break;
	default:
		msglog(LDMSD_LERROR, "test_sampler: Unrecognized "
				"type '%s'\n", type);
		free(metric->name);
		free(metric);
		return NULL;
		break;
	}

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
	int is_mvalue_init = 0;
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

	char *metrics, *value;
	char *init_value = NULL;
	int num_metrics;
	metrics = av_value(avl, "metrics");
	value = av_value(avl, "num_metrics");
	if (!metrics && !value) {
		msglog(LDMSD_LERROR, "test_sampler: Either metrics or "
				"num_metrics must be given\n");
		return EINVAL;
	}

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
		is_mvalue_init = 1;
	} else {
		is_need_int = 1;
		enum ldms_value_type type;
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

		init_value = av_value(avl, "init_value");

		int i;
		char name[128];
		for (i = 0; i < num_metrics; i++) {
			metric = malloc(sizeof(*metric));
			snprintf(name, 128, "%s%d", DEFAULT_METRIC_NAME_NAME, i);
			metric->name = strdup(name);
			metric->type = type;
			TAILQ_INSERT_TAIL(&ts_schema->list, metric, entry);
		}
	}

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		msglog(LDMSD_LERROR, "test_sampler: Failed to create a schema\n");
		rc = ENOMEM;
		goto cleanup;
	}

	test_sampler_compid_idx = ldms_schema_meta_add(schema,
			"component_id", LDMS_V_U64);
	test_sampler_jobid_idx = ldms_schema_metric_add(schema,
			"jobid", LDMS_V_U64);
	TAILQ_FOREACH(metric, &ts_schema->list, entry) {
		metric->idx = ldms_schema_metric_add(schema, metric->name, metric->type);
		if (is_mvalue_init)
			continue;
		switch (metric->type) {
		case LDMS_V_D64:
			if (init_value)
				metric->init_value.v_d = strtod(init_value, NULL);
			else
				metric->init_value.v_d = metric->idx;
			break;
		case LDMS_V_F32:
			if (init_value)
				metric->init_value.v_f = strtof(init_value, NULL);
			else
				metric->init_value.v_f = metric->idx;
			break;
		case LDMS_V_S64:
			if (init_value)
				sscanf(init_value, "%" SCNi64, &(metric->init_value.v_s64));
			else
				metric->init_value.v_s64 = metric->idx;
			break;
		case LDMS_V_S32:
			if (init_value)
				sscanf(init_value, "%" SCNi32, &(metric->init_value.v_s32));
			else
				metric->init_value.v_s32 = metric->idx;
			break;
		case LDMS_V_S16:
			if (init_value)
				sscanf(init_value, "%" SCNi16, &(metric->init_value.v_s16));
			else
				metric->init_value.v_s16 = metric->idx;
			break;
		case LDMS_V_S8:
			if (init_value)
				sscanf(init_value, "%" SCNi8, &(metric->init_value.v_s8));
			else
				metric->init_value.v_s8 = metric->idx;
			break;
		case LDMS_V_U64:
			if (init_value)
				sscanf(init_value, "%" SCNu64, &(metric->init_value.v_u64));
			else
				metric->init_value.v_u64 = metric->idx;
			break;
		case LDMS_V_U32:
			if (init_value)
				sscanf(init_value, "%" SCNu32, &(metric->init_value.v_u32));
			else
				metric->init_value.v_u32 = metric->idx;
			break;
		case LDMS_V_U16:
			if (init_value)
				sscanf(init_value, "%" SCNu16, &(metric->init_value.v_u16));
			else
				metric->init_value.v_u16 = metric->idx;
			break;
		case LDMS_V_U8:
			if (init_value)
				sscanf(init_value, "%" SCNu8, &(metric->init_value.v_u8));
			else
				metric->init_value.v_u8 = metric->idx;
			break;
		default:
			msglog(LDMSD_LERROR, "test_sampler: "
					"Unrecognized type\n");
			rc = EINVAL;
			goto cleanup;
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

	char *push_s = av_value(avl, "push");
	int push = 0;
	if (push_s)
		push = atoi(push_s);

	struct test_sampler_set *ts_set;
	ts_set = __set_find(&set_list, set_name);
	if (!set) {
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
		ldms_set_delete(set);
		return ENOMEM;
	}

	union ldms_value v;
	v.v_u64 = 0;
	ldms_metric_set(ts_set->set, test_sampler_compid_idx, &v);
	ldms_metric_set(ts_set->set, test_sampler_jobid_idx, &v);
	int i = test_sampler_jobid_idx + 1;
	struct test_sampler_metric *metric;
	TAILQ_FOREACH(metric, &ts_schema->list, entry) {
		ldms_metric_set(ts_set->set, i, &(metric->init_value));
		i++;
	}
	return 0;
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
	if (producer_name) {
		struct test_sampler_set *ts_set;
		LIST_FOREACH(ts_set, &set_list, entry) {
			ldms_set_producer_name_set(ts_set->set, producer_name);
		}
	}

	compid = av_value(avl, "component_id");
	if (compid) {
		struct test_sampler_set *ts_set;
		union ldms_value v;
		sscanf(compid, "%" SCNu64, &v.v_u64);
		LIST_FOREACH(ts_set, &set_list, entry) {
			ldms_metric_set(ts_set->set,
					test_sampler_compid_idx, &v);
		}
	}

	push = av_value(avl, "push");

	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	assert(0 == "not implemented");
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
			v.v_u64 = ldms_metric_get_u64(set, metric->idx);
			v.v_u64++;
			ldms_metric_set(set, metric->idx, &v);
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
			if (set_array[i])
				ldms_set_delete(set_array[i]);
			set_array[i] = NULL;
		}
		free(set_array);
	}
	struct test_sampler_set *ts;
	ts = LIST_FIRST(&set_list);
	while (ts) {
		LIST_REMOVE(ts, entry);
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
		"       [metrics=<metric name>:<metric type>:<init value>,<metric name>:<metric type>:<init value>,...]\n"
		"       [num_metrics=<num_metrics>] [type=<metric type>]\n"
		"	[init_value=<init_value>]\n"
		"\n"
		"    Either giving metrics or num_metrics. The default init value\n"
		"    is the metric index.\n"
		"    The valid metric types are, for example, D64, F32, S64, U64, S64_ARRAY\n"
		""
		"\n"
		"config name=test_sampler action=add_set instance=<set_name>\n"
		"       schema=<schema_name> [push=<push>]\n"
		"\n"
		"    <set name>      The set name\n"
		"    <schema name>   The schema name\n"
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
		"config name=test_sampler [producer=<prod_name>] [component_id=<comp_id>]\n"
		"    <prod_name>  The producer name\n"
		"    <comp_id>    The component ID\n";

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
