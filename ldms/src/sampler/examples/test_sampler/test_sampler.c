/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2018,2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2018,2021 Open Grid Computing, Inc. All rights reserved.
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
#include "ovis_json/ovis_json.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

#define SAMP "test_sampler"

#define DEFAULT_NUM_METRICS 10
#define DEFAULT_NUM_SETS 1
#define DEFAULT_METRIC_NAME_NAME "metric_"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif /* ARRAY_SIZE */

#define ARRAY_COUNT	4
#define LIST_MAX_LENGTH 5
#define METRIC_ATTR_DELIM ':'

struct test_sampler_set {
	char *name;
	char *producer;
	uint64_t jobid;
	uint64_t compid;
	struct test_sampler_schema *ts_schema;
	ldms_set_t set;
	int push;
	int skip_push;
	LIST_ENTRY(test_sampler_set) entry;
};
LIST_HEAD(test_sampler_set_list, test_sampler_set);

struct test_sampler_list_info {
	int max_len; /* The maximum list length */
	int min_len; /* The minimum list length */
	enum ldms_value_type type; /* The value type of the list entries */
	size_t cnt; /* Array length */
	union ldms_value init_value; /* List content initial value */
	char *rec_type_name; /* Record type's name if the entries are records. */
	int rec_type_minfo_idx;
};

struct test_sampler_metric_info;
struct test_sampler_record_type {
	struct test_sampler_metric_info *contents;
};

struct test_sampler_metric_info {
	int mtype; /* 2 is meta and 1 is data */
	union ldms_value init_value;
	union {
		struct test_sampler_list_info list;
		struct test_sampler_record_type rec_def;
	} info;
};
TAILQ_HEAD(test_sampler_metric_list, test_sampler_metric_info);

#define TEST_SAMPLER_F_USED_BASE 1
struct test_sampler_schema {
	char *name;
	ldms_schema_t schema;
	enum {
		TEST_SAMPLER_SCHEMA_CLASSIC = 0,
		TEST_SAMPLER_SCHEMA_LISTS,
	} type;
	struct test_sampler_set_list set_list;
	LIST_ENTRY(test_sampler_schema) entry;
	struct test_sampler_metric_info *metric_info;
};
LIST_HEAD(test_sampler_schema_list, test_sampler_schema);

struct test_sampler_stream_client {
	const char *xprt;
	const char *port;
	const char *host;
	ldms_t ldms;
	ldmsd_auth_t auth_dom;
	LIST_ENTRY(test_sampler_stream_client) entry;
};
LIST_HEAD(test_sampler_stream_client_list, test_sampler_stream_client);
struct test_sampler_stream {
	const char *name;
	const char *path;
	FILE *file;
	ldmsd_stream_type_t type;
	struct test_sampler_stream_client_list client_list;
	LIST_ENTRY(test_sampler_stream) entry;
};
LIST_HEAD(test_sampler_stream_list, test_sampler_stream);

static ldmsd_msg_log_f msglog;
static int num_sets;
static struct test_sampler_schema_list schema_list;
static struct test_sampler_set_list set_list;
static struct test_sampler_stream_list stream_list;

static struct test_sampler_schema *test_sampler_schema_find(
		struct test_sampler_schema_list *list, char *name)
{
	struct test_sampler_schema *ts_schema;
	LIST_FOREACH(ts_schema, list, entry) {
		if (0== strcmp(ts_schema->name, name))
			return ts_schema;
	}
	return NULL;
}

static struct test_sampler_stream *
__stream_find(struct test_sampler_stream_list *list, const char *name)
{
	struct test_sampler_stream *ts_stream;
	LIST_FOREACH(ts_stream, list, entry) {
		if (0 == strcmp(ts_stream->name, name))
			return ts_stream;
	}
	return NULL;
}

static struct test_sampler_stream *
__stream_new(const char *name, ldmsd_stream_type_t type)
{
	struct test_sampler_stream *ts_stream;

	ts_stream = calloc(1, sizeof(*ts_stream));
	if (!ts_stream)
		return NULL;
	ts_stream->name = strdup(name);
	if (!ts_stream->name) {
		free(ts_stream);
		return NULL;
	}
	ts_stream->type = type;
	LIST_INIT(&ts_stream->client_list);
	return ts_stream;
}

static void __stream_client_free(struct test_sampler_stream_client *c)
{
	free((char*)c->host);
	free((char*)c->port);
	free((char*)c->xprt);
	ldmsd_cfgobj_put(&c->auth_dom->obj);
	free(c);
}

static void __stream_free(struct test_sampler_stream *ts_stream)
{
	struct test_sampler_stream_client *c;
	free((char*)ts_stream->path);
	free((char*)ts_stream->name);
	while ((c = LIST_FIRST(&ts_stream->client_list))) {
		LIST_REMOVE(c, entry);
		__stream_client_free(c);
	}
	free(ts_stream);
}

static struct test_sampler_stream_client *
__stream_client_find(struct test_sampler_stream *s, const char *host,
			const char *port, const char *xprt, const char *auth)
{
	struct test_sampler_stream_client *c;
	LIST_FOREACH(c, &s->client_list, entry) {
		if (0 != strcmp(c->host, host))
			continue;
		if (0 != strcmp(c->port, port))
			continue;
		if (0 != strcmp(c->xprt, xprt))
			continue;
		if (0 != strcmp(c->auth_dom->obj.name, auth))
			continue;
		return c;
	}
	return NULL;
}

static void __metric_set(ldms_mval_t mval, enum ldms_value_type type, size_t len, uint64_t v)
{
	int i;
	if (LDMS_V_RECORD_INST == type) {
		for (i = 0; i < len; i++) {
			size_t cnt;
			ldms_mval_t ent = ldms_record_metric_get(mval, i);
			enum ldms_value_type typ = ldms_record_metric_type_get(mval, i, &cnt);
			__metric_set(ent, typ, cnt, v);
		}
	} else if (ldms_type_is_array(type)) {
		for (i = 0; i < len; i++) {
			switch (type) {
			case LDMS_V_CHAR_ARRAY:
				if (i == len - 1)
					mval->a_char[i] = '\0';
				else
					mval->a_char[i] = 'a' + (v%26);
				break;
			case LDMS_V_U8_ARRAY:
				mval->a_u8[i] = v;
				break;
			case LDMS_V_S8_ARRAY:
				mval->a_s8[i] = v;
				break;
			case LDMS_V_U16_ARRAY:
				mval->a_u16[i] = v;
				break;
			case LDMS_V_S16_ARRAY:
				mval->a_s16[i] = v;
				break;
			case LDMS_V_U32_ARRAY:
				mval->a_u32[i] = v;
				break;
			case LDMS_V_S32_ARRAY:
				mval->a_s32[i] = v;
				break;
			case LDMS_V_S64_ARRAY:
				mval->a_s64[i] = v;
				break;
			case LDMS_V_U64_ARRAY:
				mval->a_u64[i] = v;
				break;
			case LDMS_V_F32_ARRAY:
				mval->a_f[i] = v;
				break;
			case LDMS_V_D64_ARRAY:
				mval->a_d[i] = v;
				break;
			default:
				return;
			}
		}
	} else {
		switch (type) {
		case LDMS_V_CHAR:
			mval->v_char = 'a' + (v%26);
			break;
		case LDMS_V_U8:
			mval->v_u8 = v;
			break;
		case LDMS_V_S8:
			mval->v_s8 = v;
			break;
		case LDMS_V_U16:
			mval->v_u16 = v;
			break;
		case LDMS_V_S16:
			mval->v_s16 = v;
			break;
		case LDMS_V_U32:
			mval->v_u32 = v;
			break;
		case LDMS_V_S32:
			mval->v_s32 = v;
			break;
		case LDMS_V_S64:
			mval->v_s64 = v;
			break;
		case LDMS_V_U64:
			mval->v_u64 = v;
			break;
		case LDMS_V_F32:
			mval->v_f = v;
			break;
		case LDMS_V_D64:
			mval->v_d = v;
			break;
		default:
			return;
		}
	}
}

/* metric->vtype MUST be initialized before this function is called. */
static int __metric_init_value_set(union ldms_value *init_value,
				   enum ldms_value_type type,
				   const char *init_value_str)
{
	switch (type) {
	case LDMS_V_CHAR:
	case LDMS_V_CHAR_ARRAY:
		init_value->v_char = init_value_str[0];
		break;
	case LDMS_V_U8:
	case LDMS_V_U8_ARRAY:
		sscanf(init_value_str, "%" SCNu8, &(init_value->v_u8));
		break;
	case LDMS_V_U16:
	case LDMS_V_U16_ARRAY:
		sscanf(init_value_str, "%" SCNu16, &(init_value->v_u16));
		break;
	case LDMS_V_U32:
	case LDMS_V_U32_ARRAY:
		sscanf(init_value_str, "%" SCNu32, &(init_value->v_u32));
		break;
	case LDMS_V_U64:
	case LDMS_V_U64_ARRAY:
		sscanf(init_value_str, "%" SCNu64, &(init_value->v_u64));
		break;
	case LDMS_V_S8:
	case LDMS_V_S8_ARRAY:
		sscanf(init_value_str, "%" SCNi8, &(init_value->v_s8));
		break;
	case LDMS_V_S16:
	case LDMS_V_S16_ARRAY:
		sscanf(init_value_str, "%" SCNi16, &(init_value->v_s16));
		break;
	case LDMS_V_S32:
	case LDMS_V_S32_ARRAY:
		sscanf(init_value_str, "%" SCNi32, &(init_value->v_s32));
		break;
	case LDMS_V_S64:
	case LDMS_V_S64_ARRAY:
		sscanf(init_value_str, "%" SCNi64, &(init_value->v_s64));
		break;
	case LDMS_V_F32:
	case LDMS_V_F32_ARRAY:
		init_value->v_f = strtof(init_value_str, NULL);
		break;
	case LDMS_V_D64:
	case LDMS_V_D64_ARRAY:
		init_value->v_d = strtod(init_value_str, NULL);
		break;
	case LDMS_V_LIST:
	case LDMS_V_RECORD_ARRAY:
	case LDMS_V_RECORD_TYPE:
	case LDMS_V_RECORD_INST:
		/* do nothing */
		break;
	default:
		msglog(LDMSD_LERROR, "test_sampler: Unrecognized/not supported "
					"type '%d'\n", type);
		return EINVAL;
	}
	return 0;
}

/*
 * `strtok()` and `strtok_r()` do not return EMPTY token (""). If the delimiter
 * is ":" and the string to be tokenized is "a:b::d", `strtok()` and
 * `strtok_r()` would yield three tokens: "a", "b", and "d".
 *
 * `__strtok()` allows EMPTY tokens. "a:b::d" would yield four tokens: "a", "b",
 * "", and "d".
 */
static char *__strtok(char *s, char delim, char **ptr)
{
	char *d, *_p;

	if (s)
		_p = s;
	else
		_p = *ptr;
	if (!_p || _p[0] == 0)
		return NULL; /* no more tokens */
	d = strchr(_p, delim);
	if (d) {
		*d = '\0';
		*ptr = d+1;
	} else {
		*ptr = _p + strlen(_p);
	}
	return _p;
}

static void test_sampler_set_free(struct test_sampler_set *s)
{
	LIST_REMOVE(s, entry); /* Remove from the set_list of ts_schema */
	if (s->set) {
		ldmsd_set_deregister(s->name, SAMP);
		ldms_set_unpublish(s->set);
		ldms_set_delete(s->set);
	}
	free(s->name);
	free(s);
}

static void test_sampler_set_reset(struct test_sampler_set *s)
{
	if (s->set) {
		ldmsd_set_deregister(s->name, SAMP);
		ldms_set_unpublish(s->set);
		ldms_set_delete(s->set);
	}
	s->set = ldms_set_new(s->name, s->ts_schema->schema);
	if (!s->set) {
		ldmsd_log(LDMSD_LCRITICAL, SAMP ": Failed to create set %s\n", s->name);
		return;
	}
	ldms_set_publish(s->set);
	ldmsd_set_register(s->set, SAMP);
}

static struct test_sampler_set *test_sampler_set_create(ldms_set_t set,
					char *instance_name, int push,
				struct test_sampler_schema *ts_schema)
{
	struct test_sampler_set *ts_set = malloc(sizeof(*ts_set));
	if (!ts_set) {
		msglog(LDMSD_LERROR, SAMP ": Out of memory\n");
		return NULL;
	}
	ts_set->set = set;
	ts_set->name = strdup(instance_name);
	ts_set->ts_schema = ts_schema;
	ts_set->push = push;
	ts_set->skip_push = 1;
	ldms_set_publish(set);
	ldmsd_set_register(set, SAMP);
	LIST_INSERT_HEAD(&ts_schema->set_list, ts_set, entry);
	return ts_set;
}

static void test_sampler_schema_free(struct test_sampler_schema *ts_schema)
{
	struct test_sampler_set *s;
	if (!ts_schema)
		return;
	LIST_REMOVE(ts_schema, entry);
	if (ts_schema->schema)
		ldms_schema_delete(ts_schema->schema);
	while ((s = LIST_FIRST(&ts_schema->set_list))) {
		LIST_REMOVE(s, entry);
		test_sampler_set_free(s);
	}
	free(ts_schema->metric_info);
	free(ts_schema->name);
	free(ts_schema);
}

static struct test_sampler_schema *
test_sampler_schema_new(const char *name)
{
	struct test_sampler_schema *ts_schema;
	ts_schema = calloc(1, sizeof(*ts_schema));
	if (!ts_schema)
		return NULL;
	ts_schema->name = strdup(name);
	if (!ts_schema->name)
		goto err;
	LIST_INIT(&ts_schema->set_list);
	LIST_INSERT_HEAD(&schema_list, ts_schema, entry);
	return ts_schema;
err:
	test_sampler_schema_free(ts_schema);
	return NULL;
}

int get_num_metrics(char *s)
{
	int num_metrics = 0;
	while (s) {
		if (*s == ',') {
			num_metrics++;
		} else if (*s == '[') {
			s = strchr(s, ']');
		} else if (*s == '{') {
			s = strchr(s, '}');
		}
		s++;
	}
	return num_metrics;
}

/*
 * a primitive metric := NAME:MTYPE:VTYPE:INIT:LENGTH:UNIT
 * a record metric := NAME:MTYPE:RECORD:{NAME:VTYPE:INIT:LENGTH:UNI;,....}:UNIT
 * a list-of-primitive metric := NAME:MTYPE:VTYPE:[VTYPE:INIT:LENGTH]:LENGTH:UNIT
 * a list-of-record metric := NAME:MTYPE:VTYPE:[RECORD:<RECORD_TYPE_NAME>]:LENGTH:UNIT
 */

static int __parse_record_def(char *ptr, struct test_sampler_metric_info *_minfo,
				struct ldms_metric_template_s *_temp, int idx)
{
	struct test_sampler_metric_info *minfo = &_minfo[idx];
	struct ldms_metric_template_s *temp = &_temp[idx];
	char *vtype, *init_value, *count_str, *unit, *name;
	char delim = ':';
	int rc;
	int count = -1;

	char *end;
	int num_entries = 1;
	struct test_sampler_record_type *rec_def = &minfo->info.rec_def;
	enum ldms_value_type type;
	temp->type = LDMS_V_RECORD_TYPE;

	assert(*ptr == '{');
	ptr++; /* Skip '{' */

	temp->rec_def = ldms_record_create(temp->name);
	if (!temp->rec_def)
		return ENOMEM;

	end = strchr(ptr, '}');
	if (ptr == end) {
		msglog(LDMSD_LERROR, "test_sampler: add_schema: "
				"found an empty record definition '{}'\n");
		return EINVAL;
	}
	/* Get the number of entries */
	name = strchr(ptr, ';');
	while (name && name < end) {
		name++;
		num_entries++;
		name = strchr(name, ';');
	}

	rec_def->contents = malloc(num_entries *
					sizeof(struct test_sampler_metric_info));
	if (!rec_def->contents)
		return ENOMEM;

	int i = 0;
	while (ptr < end) {
		/* Populate record entries */
		name = __strtok(NULL, delim, &ptr);
		if (!name || ('\0' == name[0]))
			return EINVAL;
		vtype = __strtok(NULL, delim, &ptr);
		if (!vtype || ('\0' == vtype[0]))
			return EINVAL;
		type = ldms_metric_str_to_type(vtype);
		if (LDMS_V_NONE == type) {
			ldmsd_log(LDMSD_LERROR, "test_sampler: "
				"Invalid record entry type '%s'\n", vtype);
			return EINVAL;
		}

		init_value = __strtok(NULL, delim, &ptr);
		if (!init_value || ('\0' == init_value[0]))
			return EINVAL;
		rc = __metric_init_value_set(&rec_def->contents[i].init_value,
					type, init_value);
		if (rc)
			return rc;

		count_str = __strtok(NULL, delim, &ptr);
		if (count_str && ('\0' != count_str[0]))
			count = atoi(count_str);
		if (count < 0)
			return EINVAL;
		if (strchr(ptr, ';') && (strchr(ptr, ';') < strchr(ptr, '}')))
			unit = __strtok(NULL, ';', &ptr);
		else
			unit = __strtok(NULL, '}', &ptr);
		if (!unit || ('\0' == unit[0]))
			unit = "unit";
		rc = ldms_record_metric_add(temp->rec_def, name, unit,
							type, count);
		if (rc < 0)
			return -rc;
		i++;
	}

	assert(*ptr == ':');
	ptr++;
	unit = __strtok(NULL, delim, &ptr);
	if (!unit || ('\0' == unit[0]))
		unit = "unit";
	temp->unit = strdup(unit);
	if (!temp->unit)
		return ENOMEM;
	return 0;
}

static int __parse_list_str(char *ptr, struct test_sampler_metric_info *_minfo,
				struct ldms_metric_template_s *_temp, int idx)
{
	struct test_sampler_metric_info *minfo = &_minfo[idx];
	struct ldms_metric_template_s *temp = &_temp[idx];
	struct test_sampler_list_info *list = &minfo->info.list;
	char *vtype, *init_value, *count_str, *unit;
	char delim = ':';
	int rc;

	/* The name and metric type have been parsed. */
	assert(*ptr == '[');
	ptr++; /* Skip '[' */
	list->min_len = 1;

	vtype = __strtok(NULL, delim, &ptr);
	if (!vtype || ('\0' == vtype[0]))
		return EINVAL;
	list->type = ldms_metric_str_to_type(vtype);

	init_value = __strtok(NULL, delim, &ptr);
	if (!init_value || ('\0' == init_value[0]))
		return EINVAL;
	if (LDMS_V_RECORD_INST == list->type) {
		list->rec_type_name = strdup(init_value);
		if (!list->rec_type_name)
			return ENOMEM;
	} else {
		rc = __metric_init_value_set(&list->init_value,
					     list->type, init_value);
		if (rc)
			return rc;
	}
	count_str = __strtok(NULL, ']', &ptr);
	if (ldms_type_is_array(list->type)) {
		/* Array */
		if (count_str && ('\0' != count_str[0]))
			list->cnt = atoi(count_str);
		else
			list->cnt = ARRAY_COUNT;
	} else {
		list->cnt = 1;
	}
	assert(*ptr == ':');
	ptr++;

	count_str = __strtok(NULL, delim, &ptr);
	if (count_str && ('\0' != count_str[0]))
		list->max_len = atoi(count_str);
	else
		list->max_len = LIST_MAX_LENGTH;
	if (LDMS_V_RECORD_INST == list->type) {
		int i;
		for (i = 0; i < idx; i++) {
			if (_temp[i].rec_def && (0 == strcmp(_temp[i].name, list->rec_type_name))) {
				temp->len = list->max_len * ldms_record_heap_size_get(_temp[i].rec_def);
			}
		}
	} else {
		temp->len = ldms_list_heap_size_get(list->type, list->max_len, list->cnt);
	}

	unit = __strtok(NULL, delim, &ptr);
	if (!unit || ('\0' == unit[0]))
		unit = "unit";
	temp->unit = strdup(unit);
	if (!temp->unit)
		return ENOMEM;
	if (LDMS_V_RECORD_INST == list->type) {
		int i;
		for (i = 0; i < idx; i++) {
			if (0 == strcmp(_temp[i].name, list->rec_type_name)) {
				temp->len = list->max_len * ldms_record_heap_size_get(_temp[i].rec_def);
				list->rec_type_minfo_idx = i;
				break;
			}
		}
		if (i == idx) {
			/* Cannot find the rec_def */
			msglog(LDMSD_LERROR, "test_sampler: add_schema: "
					"rec_def '%s' not found.\n",
					list->rec_type_name);
			return EINVAL;
		}
	}
	return 0;
}


static int __parse_metric_str(char *s, char delim, struct test_sampler_metric_info *_minfo,
				struct ldms_metric_template_s *_temp, int idx)
{
	char *name, *mtype, *vtype, *init_value, *count_str, *unit, *ptr;
	int rc = 0;

	struct test_sampler_metric_info *minfo = &_minfo[idx];
	struct ldms_metric_template_s *temp = &_temp[idx];

	memset(minfo, 0, sizeof(*minfo));
	memset(temp, 0, sizeof(*temp));

	/*
	 * <name>:<mtype>:<vtype>:...
	 */
	name = __strtok(s, delim, &ptr);
	if (!name || ('\0' == name[0]))
		return EINVAL;
	temp->name = strdup(name);
	if (!temp->name)
		return ENOMEM;

	mtype = __strtok(NULL, delim, &ptr);
	if (!mtype || ('\0' == mtype[0]))
		return EINVAL;
	if ((0 == strcasecmp(mtype, "data")) || (0 == strcasecmp(mtype, "d"))) {
		temp->flags = minfo->mtype = LDMS_MDESC_F_DATA;
	} else if ((0 == strcasecmp(mtype, "meta")) ||
			(0 == strcasecmp(mtype, "m"))) {
		temp->flags = minfo->mtype = LDMS_MDESC_F_META;
	} else {
		return EINVAL;
	}

	vtype = __strtok(NULL, delim, &ptr);
	if (!vtype || ('\0' == vtype[0]))
		return EINVAL;
	temp->type = ldms_metric_str_to_type(vtype);
	if (LDMS_V_LIST == temp->type) {
		/* list */
		rc = __parse_list_str(ptr, _minfo, _temp, idx);
		if (rc)
			return rc;
	} else if (LDMS_V_RECORD_INST == temp->type) {
		/* record type */
		rc = __parse_record_def(ptr, _minfo, _temp, idx);
		if (rc)
			return rc;
	} else {
		/* Primitive */
		/*
		 * Primitive
		 * <name>:<mtype>:<dtype>:<init_value>:<array_len>:<unit>
		 */
		init_value = __strtok(NULL, delim, &ptr);
		if (!init_value || ('\0' == init_value[0]))
			return EINVAL;

		count_str = __strtok(NULL, delim, &ptr);
		if (count_str && ('\0' != count_str[0]))
			temp->len = atoi(count_str);
		else
			temp->len = 1;

		unit = __strtok(NULL, delim, &ptr);
		if (!unit || ('\0' == unit[0]))
			unit = "";
		temp->unit = strdup(unit);
		if (!temp->unit)
			return ENOMEM;

		rc = __metric_init_value_set(&minfo->init_value, temp->type, init_value);
	}

	return rc;
}

static int config_add_schema(struct attr_value_list *avl)
{
	int rc = 0;
	int i;
	struct test_sampler_schema *ts_schema;
	struct ldms_metric_template_s *temp;
	struct test_sampler_metric_info *minfo;
	int *mid;

	char *schema_name = av_value(avl, "schema");
	if (!schema_name) {
		msglog(LDMSD_LERROR, "test_sampler: Need schema_name\n");
		return EINVAL;
	}

	char mattr_delim = METRIC_ATTR_DELIM;
	char *tmp = av_value(avl, "delim");
	if (tmp)
		mattr_delim = tmp[0];

	ts_schema = test_sampler_schema_find(&schema_list, schema_name);
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

	if (metrics) {
		char *s = metrics;
		num_metrics = 1;

		while (*s) {
			if (*s == '{') {
				s = strchr(s, '}');
			} else if (*s == '[') {
				s = strchr(s, ']');
			} else if (*s == ',') {
				num_metrics++;
			}
			s++;
		}
	} else {
		num_metrics = atoi(value);
	}

	temp = calloc(num_metrics + 1, sizeof(struct ldms_metric_template_s));
	if (!temp) {
		rc = ENOMEM;
		goto cleanup;
	}
	minfo = calloc(num_metrics + 1, sizeof(struct test_sampler_metric_info));
	if (!minfo) {
		rc = ENOMEM;
		goto cleanup;
	}

	if (metrics) {
		int i;
		char *m;
		char *s = strdup(metrics);

		if (!s) {
			rc = ENOMEM;
			goto cleanup;
		}

		m = s;
		i = 0;
		while (*s) {
			if (*s == '{') {
				s = strchr(s, '}');
			} else if (*s == '[') {
				s = strchr(s, ']');
			} else if (*s == ',') {
				*s = '\0';
				rc = __parse_metric_str(m, mattr_delim, minfo, temp, i);
				if (rc)
					goto cleanup;
				i++;
				m = s + 1;
			}
			s++;
			if (*s == '\0') {
				/* Last metric */
				rc = __parse_metric_str(m, mattr_delim, minfo, temp, i);
				if (rc)
					goto cleanup;
			}
		}
	} else {
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
		if (!init_value)
			init_value = "0";

		char name[128];
		for (i = 0; i < num_metrics; i++) {
			snprintf(name, 128, "%s%d", DEFAULT_METRIC_NAME_NAME, i);
			temp[i].name = strdup(name);
			if (!temp[i].name)
				return ENOMEM;
			rc = __metric_init_value_set(&(minfo[i].init_value), type, init_value);
			if (rc)
				return rc;

			temp[i].type = type;
			temp[i].unit = NULL;
			temp[i].len = 1;

			minfo[i].mtype = 1; /* data */
		}
	}

	ts_schema = test_sampler_schema_new(schema_name);
	if (!ts_schema) {
		msglog(LDMSD_LERROR, "test_sampler: Out of memory\n");
		return ENOMEM;
	}

	mid = malloc(num_metrics * sizeof(int));
	if (!mid) {
		rc = ENOMEM;
		goto cleanup;
	}

	ts_schema->schema = ldms_schema_from_template(schema_name, temp, mid);
	if (!ts_schema->schema) {
		msglog(LDMSD_LERROR, "test_sampler: Failed to create a schema\n");
		rc = ENOMEM;
		goto cleanup;
	}
	ts_schema->metric_info = minfo;
	ldms_schema_array_card_set(ts_schema->schema, set_array_card);
	return 0;

cleanup:
	test_sampler_schema_free(ts_schema);
	return rc;
}

static int __init_set(struct test_sampler_set *ts_set)
{
	int rc = 0;
	union ldms_value v;
	int mid = 0;

	int i, j, card, len;
	struct test_sampler_metric_info *minfo;
	enum ldms_value_type type;

	minfo = ts_set->ts_schema->metric_info;
	card = ldms_set_card_get(ts_set->set);
	for (i = 0; i < card; i++) {
		type = ldms_metric_type_get(ts_set->set, i);
		if (type == LDMS_V_CHAR_ARRAY) {
			ldms_metric_array_set(ts_set->set, i,
					&(minfo[i].init_value), 0,
					ldms_metric_array_get_len(ts_set->set, i));
		} else if (ldms_type_is_array(type)) {
			len = ldms_metric_array_get_len(ts_set->set, i);
			for (j = 0; j < len; j++) {
				ldms_metric_array_set_val(ts_set->set, i, j,
							 &(minfo[i].init_value));
			}
		} else if (LDMS_V_LIST == type) {
			struct test_sampler_list_info *list;
			ldms_mval_t lh, lent;
			size_t cnt;

			list = &(minfo[i].info.list);
			len = list->max_len;
			lh = ldms_metric_get(ts_set->set, i);

			for (j = 0; j < len; j++) {
				if (LDMS_V_RECORD_INST == list->type) {
					int rdef_mid = ldms_metric_by_name(ts_set->set, list->rec_type_name);
					lent = ldms_record_alloc(ts_set->set, rdef_mid);
					cnt = ldms_record_card(lent);
					__metric_set(lent, list->type, cnt,
						minfo[rdef_mid].info.rec_def.contents[0].init_value.v_u64);
					rc = ldms_list_append_record(ts_set->set, lh, lent);
					if (rc)
						return rc;
				} else {
					lent = ldms_list_append_item(ts_set->set, lh, list->type, list->cnt);
					cnt = 1;
					__metric_set(lent, type, cnt, list->init_value.v_u64);
				}

			}
			continue;
		} else if (LDMS_V_RECORD_TYPE == type) {
			/* skip */
			continue;
		} else {
			ldms_metric_set(ts_set->set, i, &(minfo[i].init_value));
		}
	}

	if (ts_set->compid) {
		mid = ldms_metric_by_name(ts_set->set, "component_id");
		if (mid >= 0) {
			v.v_u64 = ts_set->compid;
			if (ldms_metric_is_array(ts_set->set, mid)) {
				uint32_t count = ldms_metric_array_get_len(ts_set->set, mid);
				for (j = 0; j < count; j++)
					ldms_metric_array_set_u64(ts_set->set, mid, j, ts_set->compid);
			} else {
				ldms_metric_set(ts_set->set, mid, &v);
			}
		} else {
			msglog(LDMSD_LERROR, "test_sampler: "
				"component_id=%lu is given at the action=add_set line "
				"but the set does not contain the metric 'component_id'\n",
				ts_set->compid);
		}
	}

	if (ts_set->jobid) {
		mid = ldms_metric_by_name(ts_set->set, LDMSD_JOBID);
		if (mid >= 0) {
			v.v_u64 = ts_set->jobid;
			if (ldms_metric_is_array(ts_set->set, mid)) {
				uint32_t count = ldms_metric_array_get_len(ts_set->set, mid);
				ldms_metric_array_set(ts_set->set, mid, &v, 0, count);
			} else {
				ldms_metric_set(ts_set->set, mid, &v);
			}
		} else {
			msglog(LDMSD_LERROR, "test_sampler: "
				"job_id=%lu is given at the action=add_set line "
				"but the set does not contain the metric 'component_id'\n",
				ts_set->jobid);
		}
	}

	ldms_set_producer_name_set(ts_set->set, ts_set->producer);
	return rc;
}

static int config_add_set(struct attr_value_list *avl)
{
	int rc = 0;
	struct test_sampler_schema *ts_schema;
	ldms_set_t set;
	struct test_sampler_set *ts_set;

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
	ts_schema = test_sampler_schema_find(&schema_list, schema_name);
	if (!ts_schema) {
		msglog(LDMSD_LERROR, "test_sampler: Schema '%s' does not "
				"exist.\n", schema_name);
		return EINVAL;
	}

	char *compid = av_value(avl, "component_id");
	char *jobid = av_value(avl, LDMSD_JOBID);

	char *producer = av_value(avl, "producer");
	if (!producer) {
		msglog(LDMSD_LERROR, "test_sampler: Need producer name\n");
		return EINVAL;
	}

	char *push_s = av_value(avl, "push");
	int push = 0;
	if (push_s)
		push = atoi(push_s);

	set = ldms_set_new(set_name, ts_schema->schema);
	if (!set) {
		msglog(LDMSD_LERROR, "test_sampler: Cannot create "
				"set '%s'\n", set_name);
		return ENOMEM;
	}
	ts_set = test_sampler_set_create(set, set_name, push, ts_schema);
	if (!ts_set) {
		rc = ENOMEM;
		goto err0;
	}
	ts_set->push = push;

	ts_set->producer = strdup(producer);
	if (!ts_set->producer) {
		msglog(LDMSD_LCRITICAL, "test_sampler: Out of memory\n");
		goto err0;
	}

	union ldms_value v;
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
	ts_set->compid = v.v_u64;

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
	ts_set->jobid = v.v_u64;

	rc = __init_set(ts_set);
	if (rc)
		goto err0;

	return 0;
err0:
	ldms_set_delete(set);
err1:
	free(ts_set);
	return rc;
}

#define DEFAULT_SCHEMA_NAME "my_schema"
#define DEFAULT_BASE_SET_NAME "my_set"
static int config_add_default(struct attr_value_list *avl)
{
	char *sname, *s, *base_set_name;
	int rc, num_metrics;
	int i, *mid;
	struct ldms_metric_template_s *temp = NULL;
	struct ldms_metric_template_s *m;
	struct test_sampler_metric_info *minfo = NULL;
	struct test_sampler_metric_info *info;
	struct test_sampler_schema *ts_schema;
	sname = s = base_set_name = NULL;

	sname = av_value(avl, "schema");
	if (!sname)
		sname = strdup(DEFAULT_SCHEMA_NAME);
	if (strlen(sname) == 0){
		msglog(LDMSD_LERROR, "test_sampler: schema name invalid.\n");
		return EINVAL;
	}

	base_set_name = av_value(avl, "base");
	if (!base_set_name)
		base_set_name = strdup(DEFAULT_BASE_SET_NAME);

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

	temp = calloc(num_metrics + 1, sizeof(struct ldms_metric_template_s));
	if (!temp) {
		rc = ENOMEM;
		goto err;
	}
	minfo = calloc(num_metrics + 1, sizeof(struct test_sampler_metric_info));
	if (!minfo) {
		rc = ENOMEM;
		goto err;
	}
	mid = calloc(num_metrics + 1, sizeof(int));
	if (!mid) {
		rc = ENOMEM;
		goto err;
	}
	for (i = 0, m = temp, info = minfo; i < num_metrics; i++, m++, info++) {
		rc = asprintf((char **)&m->name, "metric_%d", i);
		if (rc < 0) {
			rc = errno;
			goto err;
		}
		rc = asprintf((char**)&m->unit, "unit");
		if (rc < 0) {
			rc = errno;
			goto err;
		}
		m->len = 1;
		m->type = LDMS_V_U64;
		info->init_value.v_u64 = 1;
		m->flags = info->mtype = LDMS_MDESC_F_DATA;
	}

	ts_schema = test_sampler_schema_new(sname);
	if (!ts_schema)
		goto err;
	ts_schema->schema = ldms_schema_from_template(sname, temp, mid);
	if (!ts_schema->schema)
		goto err;
	ts_schema->metric_info = minfo;
	return 0;
err:
	free(sname);
	free(base_set_name);
	free(s);
	free(temp);
	return rc;
}

/*
 * The metric order is the same as in sampler_base.c:base_schema_new().
 */
struct ldms_metric_template_s base_schema_temp[] = {
		{ "component_id",	LDMS_MDESC_F_META, LDMS_V_U64, "", 1 },
		{ "job_id",		LDMS_MDESC_F_META, LDMS_V_U64, "", 1 },
		{ "app_id",		LDMS_MDESC_F_META, LDMS_V_U64, "", 1 },
		{ NULL, -1 }
};

#define UNIT		"unit"
struct ldms_metric_template_s default_rec_contents[] = {
		{ stringify(LDMS_V_CHAR), 0, LDMS_V_CHAR, UNIT, 1 },
		{ stringify(LDMS_V_U8), 0, LDMS_V_U8, UNIT, 1 },
		{ stringify(LDMS_V_S8), 0, LDMS_V_S8, UNIT, 1 },
		{ stringify(LDMS_V_U16), 0, LDMS_V_U16, UNIT, 1 },
		{ stringify(LDMS_V_S16), 0, LDMS_V_S16, UNIT, 1 },
		{ stringify(LDMS_V_U32), 0, LDMS_V_U32, UNIT, 1 },
		{ stringify(LDMS_V_S32), 0, LDMS_V_S32, UNIT, 1 },
		{ stringify(LDMS_V_U64), 0, LDMS_V_U64, UNIT, 1 },
		{ stringify(LDMS_V_S64), 0, LDMS_V_S64, UNIT, 1 },
		{ stringify(LDMS_V_F32), 0, LDMS_V_F32, UNIT, 1 },
		{ stringify(LDMS_V_D64), 0, LDMS_V_D64, UNIT, 1 },
		{ stringify(LDMS_V_CHAR_ARRAY), 0, LDMS_V_CHAR_ARRAY, UNIT, ARRAY_COUNT },
		{ stringify(LDMS_V_U8_ARRAY), 0, LDMS_V_U8_ARRAY, UNIT, ARRAY_COUNT },
		{ stringify(LDMS_V_S8_ARRAY), 0, LDMS_V_S8_ARRAY, UNIT, ARRAY_COUNT },
		{ stringify(LDMS_V_U16_ARRAY), 0, LDMS_V_U16_ARRAY, UNIT, ARRAY_COUNT },
		{ stringify(LDMS_V_S16_ARRAY), 0, LDMS_V_S16_ARRAY, UNIT, ARRAY_COUNT },
		{ stringify(LDMS_V_U32_ARRAY), 0, LDMS_V_U32_ARRAY, UNIT, ARRAY_COUNT },
		{ stringify(LDMS_V_S32_ARRAY), 0, LDMS_V_S32_ARRAY, UNIT, ARRAY_COUNT },
		{ stringify(LDMS_V_U64_ARRAY), 0, LDMS_V_U64_ARRAY, UNIT, ARRAY_COUNT },
		{ stringify(LDMS_V_S64_ARRAY), 0, LDMS_V_S64_ARRAY, UNIT, ARRAY_COUNT },
		{ stringify(LDMS_V_F32_ARRAY), 0, LDMS_V_F32_ARRAY, UNIT, ARRAY_COUNT },
		{ stringify(LDMS_V_D64_ARRAY), 0, LDMS_V_D64_ARRAY, UNIT, ARRAY_COUNT },
		{ NULL, -1 }
};

/*
 * set metrics
 * { base_schema_temp ...,
 *   record_type,
 *   round,
 *   list_1_len,
 *   list_1,
 *   list_2_len,
 *   list_2,
 *   .
 *   .
 *   .
 * }
 */
#define LIST_NAME "list_"
#define LIST_LEN_NAME "list_len_"
#define RECORD_TYPE "test_record"
static int config_add_lists(struct attr_value_list *avl)
{
	struct test_sampler_list_info *linfo;
	char *schema_name, *s, *a, *ptr;
	int rc, i, j, card, list_id;
	int num_lists = 1;
	int rec_content_ids[LDMS_V_LAST];
	struct test_sampler_schema *ts_schema = NULL;
	struct ldms_metric_template_s *temp = NULL;
	struct test_sampler_metric_info *minfo, *rcontent;
	ldms_record_t rec_def;
	int *mid = NULL;
	int round_idx = -1;
	int rec_type_idx;
	time_t t;
	rc = 0;
	srand((unsigned)time(&t));

	schema_name = av_value(avl, "schema");
	if (!schema_name) {
		msglog(LDMSD_LERROR, "test_sampler: schema is required.\n");
		rc = EINVAL;
		goto err;
	}

	if (strlen(schema_name) == 0) {
		msglog(LDMSD_LERROR, "test_sampler: schema name invalid.\n");
		rc = EINVAL;
		goto err;
	}

	s = av_value(avl, "value_types");
	if (!s) {
		num_lists = 1;
	} else {
		char *b;
		num_lists = 0;
		a = strdup(s);
		if (!a) {
			msglog(LDMSD_LCRITICAL, "test_sampler: Out of memory\n");
			rc = ENOMEM;
			goto err;
		}
		b = strtok_r(a, ",", &ptr);
		while (b) {
			num_lists++;
			b = strtok_r(NULL, ",", &ptr);
		}
		free(a);
	}

	linfo = calloc(num_lists, sizeof(struct test_sampler_list_info));
	if (!linfo) {
		msglog(LDMSD_LCRITICAL, "test_sampler: Out of memory\n");
		rc = ENOMEM;
		goto err;
	}
	if (s) {
		i = 0;
		a = strtok_r(s, ",", &ptr);
		while (a) {
			linfo[i].type = ldms_metric_str_to_type(a);
			if (LDMS_V_NONE == linfo[i].type) {
				msglog(LDMSD_LERROR, "test_sampler: unrecognized value type '%s'\n", a);
				rc = EINVAL;
				goto err;
			}
			if (ldms_type_is_array(linfo[i].type))
				linfo[i].cnt = ARRAY_COUNT;
			if (LDMS_V_RECORD_INST == linfo[i].type) {
				linfo[i].rec_type_name = strdup(RECORD_TYPE);
				if (!linfo[i].rec_type_name) {
					rc = ENOMEM;
					goto err;
				}
			}
			i++;
			a = strtok_r(NULL, ",", &ptr);
		}
	} else {
		linfo[0].type = LDMS_V_RECORD_INST;
	}

	/* Maximum number of list entries */
	s = av_value(avl, "max_len");
	if (s) {
		i = 0;
		a = strtok_r(s, ",", &ptr);
		while (a) {
			linfo[i++].max_len = atoi(a);
			a = strtok_r(NULL, ",", &ptr);
		}
		for (;i < num_lists; i++)
			linfo[i].max_len = linfo[i-1].max_len;
	} else {
		for (i = 0; i < num_lists; i++)
			linfo[i].max_len = LIST_MAX_LENGTH;
	}

	s = av_value(avl, "min_len");
	if (s) {
		i = 0;
		a = strtok_r(s, ",", &ptr);
		while (a) {
			linfo[i++].min_len = atoi(a);
			a = strtok_r(NULL, ",", &ptr);
		}
		for (; i < num_lists; i++)
			linfo[i].min_len = linfo[i-1].min_len;
	} else {
		for (i = 0; i < num_lists; i++)
			linfo[i].min_len = 1;
	}

	card = ARRAY_SIZE(base_schema_temp) - 1;
	card += 1; /* record type */
	card += 1; /* round */
	card += 2*num_lists; /* list length and list */

	/* Create a record definition */
	rec_def = ldms_record_from_template(RECORD_TYPE, default_rec_contents, rec_content_ids);
	if (!rec_def) {
		msglog(LDMSD_LERROR, "test_sampler: schema %s: "
				"Failed to create the record type.\n",
				schema_name);
		rc = errno;
		goto err;
	}

	/* +1 for the terminating element */
	temp = calloc(card + 1, sizeof(struct ldms_metric_template_s));
	if (!temp) {
		msglog(LDMSD_LERROR, "test_sampler: Out of memory\n");
		rc = ENOMEM;
		goto err;
	}
	mid = malloc((card + 1) * sizeof(int));
	if (!mid) {
		msglog(LDMSD_LCRITICAL, "test_sampler: Out of memory\n");
		rc = ENOMEM;
		goto err;
	}
	/* copy base template */
	for (i = 0; base_schema_temp[i].name; i++) {
		memcpy(&temp[i], &base_schema_temp[i],
				sizeof(struct ldms_metric_template_s));
	}
	/* record type */
	temp[i].name = RECORD_TYPE;
	temp[i].rec_def = rec_def;
	temp[i].type = LDMS_V_RECORD_TYPE;
	temp[i].unit = "";
	rec_type_idx = i;
	i++;

	/* round */
	round_idx = i;
	temp[i].name = "round";
	temp[i].type = LDMS_V_U64;
	temp[i].unit = "round";
	i++;

	for (j = 0; j < num_lists; j++) {
		char n[16];
		snprintf(n, 16, "list_%d_len", j + 1);
		temp[i].name = strdup(n);
		temp[i].type = LDMS_V_U64;
		temp[i].unit = "";
		i++;

		snprintf(n, 16, "list_%d", j + 1);
		temp[i].name = strdup(n);
		temp[i].type = LDMS_V_LIST;
		if (LDMS_V_RECORD_INST == linfo[j].type) {
			temp[i].rec_def = rec_def;
			temp[i].len = linfo[j].max_len * ldms_record_heap_size_get(rec_def);
		} else if (ldms_type_is_array(linfo[j].type)) {
			temp[i].len = ldms_list_heap_size_get(linfo[j].type, linfo[j].max_len, linfo[j].cnt);
		} else {
			temp[i].len = ldms_list_heap_size_get(linfo[j].type, linfo[j].max_len, 1);
		}
		temp[i].unit = "";
		i++;
	}

	ts_schema = test_sampler_schema_find(&schema_list, schema_name);
	if (ts_schema) {
		msglog(LDMSD_LERROR, "test_sampler: "
				"Schema '%s' already exists.\n",
				schema_name);
		rc = EEXIST;
		goto err;
	}

	ts_schema = test_sampler_schema_new(schema_name);
	if (!ts_schema) {
		msglog(LDMSD_LERROR, "test_sampler: Our of memory\n");
		rc = ENOMEM;
		goto err;
	}
	ts_schema->type = TEST_SAMPLER_SCHEMA_LISTS;

	ts_schema->schema = ldms_schema_from_template(schema_name, temp, mid);
	if (!ts_schema->schema) {
		msglog(LDMSD_LERROR, "test_sampler: failed to create "
					"a schema %s\n", schema_name);
		rc = EINTR;
		goto err;
	}

	minfo = calloc(card + 1, sizeof(struct test_sampler_metric_info));
	if (!minfo) {
		msglog(LDMSD_LCRITICAL, "test_sampler: Out of memory\n");
		rc = ENOMEM;
		goto err;
	}
	list_id = 0;
	for (i = 0; i < card; i++) {
		minfo[i].mtype = LDMS_MDESC_F_DATA;
		if (round_idx == i)
			minfo[i].init_value.v_u64 = 0;
		else
			minfo[i].init_value.v_u64 = 1;
		if (LDMS_V_LIST == temp[i].type) {
			if (LDMS_V_RECORD_INST == linfo[list_id].type)
				linfo[list_id].rec_type_minfo_idx = rec_type_idx;
			minfo[i].info.list = linfo[list_id++];
		} else if (LDMS_V_RECORD_TYPE == temp[i].type) {
			minfo[i].info.rec_def.contents = calloc(ARRAY_SIZE(default_rec_contents),
								sizeof(struct test_sampler_metric_info));
			if (!minfo[i].info.rec_def.contents) {
				msglog(LDMSD_LCRITICAL, "test_sampler: Out of memory\n");
				rc = ENOMEM;
				goto err;
			}
			rcontent = minfo[i].info.rec_def.contents;
			for (j = 0; default_rec_contents[j].name; j++) {
				char *s;
				if ((LDMS_V_CHAR == default_rec_contents[j].type) ||
					(LDMS_V_CHAR_ARRAY == default_rec_contents[j].type)) {
					s = "a";
				} else {
					s = "1";
				}
				__metric_init_value_set(&rcontent[j].init_value,
							default_rec_contents[j].type,
							s);
			}
		}
	}
	ts_schema->metric_info = minfo;
out:
	if (temp) {
		if (round_idx >= 0) {
			for (i = round_idx + 1; i < card; i++) {
				free((char*)temp[i].name);
			}
		}
		free(temp);
	}
	free(mid);
	return rc;
err:
	if (ts_schema)
		test_sampler_schema_free(ts_schema);
	goto out;
}

static int config_add_stream(struct attr_value_list *avl)
{
	int rc = 0;
	char *stream_name, *type, *path, *xprt, *host, *port, *auth;
	struct test_sampler_stream *ts_stream = NULL;
	enum ldmsd_stream_type_e stype;
	struct test_sampler_stream_client *c = NULL;
	int free_stream = 0;

	stream_name = av_value(avl, "stream");
	if (!stream_name) {
		msglog(LDMSD_LERROR, "test_sampler: 'stream' is required.\n");
		return EINVAL;
	}

	type = av_value(avl, "type"); /* stream type */
	if (!type) {
		msglog(LDMSD_LERROR, "test_sampler: 'type' is required.\n");
		return EINVAL;
	}
	if (0 == strcasecmp(type, "json")) {
		stype = LDMSD_STREAM_JSON;
	} else if (0 == strcasecmp(type, "string")) {
		stype = LDMSD_STREAM_STRING;
	} else {
		msglog(LDMSD_LERROR, "test_sampler: The 'type' value ('%s') is "
							   "invalid.\n", type);
		return EINVAL;
	}

	path = av_value(avl, "path");
	if (!path) {
		msglog(LDMSD_LERROR, "test_sampler: 'path' is required.\n");
		return EINVAL;
	}

	host = av_value(avl, "host");
	if (!host)
		host = "localhost";
	port = av_value(avl, "port");
	if (!port) {
		msglog(LDMSD_LERROR, "test_sampler: 'port' is required.\n");
		return EINVAL;
	}
	xprt = av_value(avl, "xprt");
	if (!xprt)
		xprt = "sock";
	auth = av_value(avl, "auth");
	if (!auth)
		auth = DEFAULT_AUTH;

	ts_stream = __stream_find(&stream_list, stream_name);
	if (ts_stream) {
		if (0 != strcmp(path, ts_stream->path)) {
			msglog(LDMSD_LERROR, "test_sampler: stream '%s' "
					"already exists.\n", stream_name);
			return EINVAL;
		}
	} else {
		free_stream = 1;
		ts_stream = __stream_new(stream_name, stype);
		if (!ts_stream)
			goto enomem;
		ts_stream->path = strdup(path);
		if (!ts_stream->path) {
			goto enomem;
		}
		ts_stream->file = fopen(ts_stream->path, "r");
		if (!ts_stream->file) {
			msglog(LDMSD_LERROR, "test_sampler: Cannot open file '%s'\n",
								ts_stream->path);
			rc = EINVAL;
			goto err;
		}
		LIST_INSERT_HEAD(&stream_list, ts_stream, entry);
	}

	c = __stream_client_find(ts_stream, host, port, xprt, auth);
	if (c) {
		msglog(LDMSD_LERROR, "test_sampler: stream '%s' the client "
				"%s:%s:%s already exists.\n", ts_stream->name,
				c->xprt, c->port, c->host, c->auth_dom->obj.name);
	} else {
		c = malloc(sizeof(*c));
		if (!c)
			goto enomem;
		c->host = strdup(host);
		if (!c->host)
			goto enomem;
		c->xprt = strdup(xprt);
		if (!c->xprt)
			goto enomem;
		c->port = strdup(port);
		if (!c->port)
			goto enomem;
		c->auth_dom = ldmsd_auth_find(auth);
		if (!c->auth_dom) {
			msglog(LDMSD_LERROR, "test_sampler: Cannot find auth '%s'\n", auth);
			rc = EINVAL;
			goto err;
		}
		LIST_INSERT_HEAD(&ts_stream->client_list, c, entry);
	}
	return 0;

enomem:
	msglog(LDMSD_LERROR, "test_sampler: Out of memory\n");
	rc = ENOMEM;
err:
	if (c)
		__stream_client_free(c);
	if (free_stream)
		__stream_free(ts_stream);
	return rc;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *action;
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
		} else if (0 == strcmp(action, "add_lists")) {
			rc = config_add_lists(avl);
		} else if (0 == strcmp(action, "add_stream")) {
			rc = config_add_stream(avl);
		} else {
			msglog(LDMSD_LERROR, "test_sampler: Unrecognized "
				"action '%s'.\n", action);
			rc = EINVAL;
		}
		return rc;
	}
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	assert(0 == "not implemented");
}

static int __gen_list_len(int min_len, int max_len)
{
	int llen = rand();
	return (llen % (max_len + 1 - min_len)) + min_len;
}

static int __sample_classic(struct test_sampler_set *ts_set)
{
	struct test_sampler_metric_info *minfo;
	int rc;
	int i, j, card, len;
	enum ldms_value_type type;
	ldms_mval_t mval;
	uint64_t v;

	minfo = ts_set->ts_schema->metric_info;
	card = ldms_set_card_get(ts_set->set);

	ldms_transaction_begin(ts_set->set);
	for (i = 0; i < card; i++) {
		if (LDMS_MDESC_F_META & ldms_metric_flags_get(ts_set->set, i))
			continue;
		type = ldms_metric_type_get(ts_set->set, i);
		mval = ldms_metric_get(ts_set->set, i);
		if (LDMS_V_RECORD_TYPE == type) {
			continue;
		}
		if (LDMS_V_LIST == type) {
			struct test_sampler_list_info *list;
			ldms_mval_t lent;
			size_t cnt;

			list = &minfo[i].info.list;
			len = __gen_list_len(list->min_len, list->max_len);
			lent = ldms_list_first(ts_set->set, mval, &type, &cnt);

			if (LDMS_V_RECORD_INST == list->type) {
				type = ldms_record_metric_type_get(lent, 0, &cnt);
				lent = ldms_record_metric_get(lent, 0);
			}
			v = lent->v_u64 + 1;
			ldms_list_purge(ts_set->set, mval);
			for (j = 0; j < len; j++) {
				if (LDMS_V_RECORD_INST == list->type) {
					int rdef_mid = ldms_metric_by_name(ts_set->set, list->rec_type_name);

					lent = ldms_record_alloc(ts_set->set, rdef_mid);
					if (!lent)
						goto delete_set;
					cnt = ldms_record_card(lent);
					rc = ldms_list_append_record(ts_set->set, mval, lent);
					if (rc)
						return rc;
				} else {
					cnt = list->cnt;
					lent = ldms_list_append_item(ts_set->set, mval, list->type, list->cnt);
					if (!lent)
						goto delete_set;
				}
				__metric_set(lent, list->type, cnt, v);
			}
		} else {
			v = mval->v_u64 + 1;
			if (ldms_metric_is_array(ts_set->set, i))
				len = ldms_metric_array_get_len(ts_set->set, i);
			else
				len = 1;
			__metric_set(mval, type, len, v);
		}
	}
	ldms_transaction_end(ts_set->set);
	return 0;
delete_set:
	ldms_transaction_end(ts_set->set);
	test_sampler_set_reset(ts_set);
	rc = __init_set(ts_set);
	return rc;
}

static int
__sample_lists(struct test_sampler_set *ts_set)
{
	int i, j, h;
	int card, rec_type_mid, llen;
	struct test_sampler_metric_info *info = ts_set->ts_schema->metric_info;
	time_t t;
	ldms_mval_t lh, rec_inst, mval;
	enum ldms_value_type type;
	size_t cnt, len;
	uint64_t round;
	int round_mid, rc;
	struct test_sampler_list_info *list;

	card = ldms_set_card_get(ts_set->set);
	round_mid = ldms_metric_by_name(ts_set->set, "round");
	round = ldms_metric_get_u64(ts_set->set, round_mid);
	round++;
	srand((unsigned)time(&t));

	ldms_transaction_begin(ts_set->set);
	/* Set the round value */
	ldms_metric_set_u64(ts_set->set, round_mid, round);

	/*
	 * The remaining metrics are alternate between the list length and the list
	 */
	for (i = round_mid + 2, j = 0; i < card; i += 2, j++) {
		/* It must be a list. */
		list = &(info[i].info.list);
		lh = ldms_metric_get(ts_set->set, i);
		ldms_list_purge(ts_set->set, lh);

		/* randomly select the number of list length */
		llen = __gen_list_len(list->min_len, list->max_len);
		/* Set the list length */
		ldms_metric_set_u64(ts_set->set, i - 1, llen);

		/* Populate the list */
		uint64_t v;
		for (j = 0; j < llen; j++) {
			v = round + j;
			if (LDMS_V_RECORD_INST == list->type) {
				rec_type_mid = ldms_metric_by_name(ts_set->set, list->rec_type_name);
				assert(rec_type_mid >= 0);
				rec_inst = ldms_record_alloc(ts_set->set, rec_type_mid);
				assert(rec_inst);
				cnt = ldms_record_card(rec_inst);
				for (h = 0; h < cnt; h++) {
					mval = ldms_record_metric_get(rec_inst, h);
					type = ldms_record_metric_type_get(rec_inst, h, &len);
					if (ldms_type_is_array(type))
						assert(len == ARRAY_COUNT);
					__metric_set(mval, type, len, v);
				}
				rc = ldms_list_append_record(ts_set->set, lh, rec_inst);
				assert(rc == 0);
			} else {
				mval = ldms_list_append_item(ts_set->set, lh,
							list->type, list->cnt);
				assert(mval);
				__metric_set(mval, list->type, list->cnt, v);
			}
		}
	}
	ldms_transaction_end(ts_set->set);
	return 0;
}

static int sample(struct ldmsd_sampler *self)
{
	int rc;
	struct test_sampler_set *ts_set;
	struct test_sampler_schema *ts_schema;

	LIST_FOREACH(ts_schema, &schema_list, entry) {
		LIST_FOREACH(ts_set, &ts_schema->set_list, entry) {
			if (TEST_SAMPLER_SCHEMA_LISTS == ts_schema->type) {
				rc = __sample_lists(ts_set);
			} else if (TEST_SAMPLER_SCHEMA_CLASSIC == ts_schema->type) {
				rc = __sample_classic(ts_set);
			}
			if (rc)
				return rc;

			if (ts_set->push) {
				if (ts_set->push == ts_set->skip_push) {
					ldms_xprt_push(ts_set->set);
					ts_set->skip_push = 1;
				} else {
					ts_set->skip_push++;
				}
			}
		}
	}

	struct test_sampler_stream *ts_stream;
	struct test_sampler_stream_client *c;
	LIST_FOREACH(ts_stream, &stream_list, entry) {
		LIST_FOREACH(c, &ts_stream->client_list, entry) {
			rc = ldmsd_stream_publish_file(ts_stream->name,
					(LDMSD_STREAM_JSON==ts_stream->type)?"json":"string",
					c->xprt, c->host, c->port,
					c->auth_dom->plugin,
					c->auth_dom->attrs, ts_stream->file);
			if (rc) {
				msglog(LDMSD_LERROR, "test_sampler: Failed to "
					"publish stream '%s' to %s:%s:%s:%s\n",
					ts_stream->name, c->xprt, c->port,
					c->host, c->auth_dom->obj.name);
			}
		}
	}
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	struct test_sampler_schema *tschema;
	while ((tschema = LIST_FIRST(&schema_list))) {
		LIST_REMOVE(tschema, entry);
		test_sampler_schema_free(tschema);
	}

	struct test_sampler_stream *ts_stream;
	while ((ts_stream = LIST_FIRST(&stream_list))) {
		LIST_REMOVE(ts_stream, entry);
		__stream_free(ts_stream);
	}
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "Create and define schema:\n"
		"config name=test_sampler action=add_schema schema=<schema_name>\n"
		"       [set_array_card=number of elements in the set ring buffer]\n"
		"       [metrics=<comma-separated list of metrics' definition]\n"
		"       [num_metrics=<num_metrics>] [type=<metric type>]\n"
		"	[init_value=<init_value>]\n"
		"\n"
		"    Either giving metrics or num_metrics. The default init value\n"
		"    is the metric index.\n"
		"    \n"
		"    If 'metrics' is given, the meta and data metrics in the schema will be exactly as the given metric list.\n"
		"    Otherwise, 'component_id' and 'job_id' metrics will be automatically added to the schema.\n"
		"    The valid metric types are either 'meta' or 'data'.\n"
		"    The valid value types are, for example, D64, F32, S64, U64, S64_ARRAY, LIST, RECORD. The strings are not case-sensitive.\n"
		"       For primitive-type metrics, the format is <metric name>:<metric type>:<value type>:<init value>:<array size>:<unit>.\n"
		"       For record definitions, the format is <record def's name>:<metric type>:record:{<entry's name>:<value type>:<init value>:<array size>:unit;...}:<unit>.\n"
		"       For lists of primite-type entries, the format is <metric name>:<metric type>:list:[<entries' value type>:<init value>:<array size>]:<max list length>:<unit>.\n"
		"       For lists of records, the format is <metric name>:<metric type>:list:[record:<record def's name>:]:<max list length>:unit.\n"
		"       The record definitions must be given before referring to it.\n"
		"\n"
		"Create sets:\n"
		"config name=test_sampler action=add_set instance=<set_name>\n"
		"       schema=<schema_name> producer=<producer>\n"
		"       [component_id=<compid>] ["LDMSD_JOBID"=<jobid>]\n"
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
		"Create the default schema:\n"
		"config name=test_sampler action=default [base=<base>] [schema=<sname>]\n"
		"       [num_sets=<nsets>] [num_metrics=<nmetrics>]\n"
		"\n"
		"    <base>       The base of set names\n"
		"    <sname>      Optional schema name. Defaults to 'test_sampler'\n"
		"    <nsets>      Number of sets\n"
		"    <nmetrics>   Number of metrics\n"
		"\n"
		"Create a schema containing a single list:\n"
		"\n"
		"config name=test_sampler action=add_lists [schema=<schema>]\n"
		"\n"
		"    <schema>       The schema name\n"
		"\n"
		"Publish streams:\n"
		"config name=test_sampler action=add_stream stream=<stream_name>\n"
		"       path=<path> port=<port> [host=<host>] [xprt=<xprt>] [auth=<auth>]\n"
		"\n"
		"    <stream_name>    Stream name\n"
		"    <path>           Path to the file containing the stream data\n"
		"    <port>           Port of a stream client\n"
		"    <host>           Host of a stream client\n"
		"    <xprt>           Transport to connect to a stream client\n"
		"    <auth>           A authentication domain name\n";
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
	LIST_INIT(&stream_list);
	return &test_sampler_plugin.base;
}
