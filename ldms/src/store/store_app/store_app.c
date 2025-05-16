/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
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
 * \file store_app.c
 */

#include <assert.h>
#include <sos/sos.h>

#include "ldmsd.h"

int perm = 0660;

static ovis_log_t mylog;

#define LOG(lvl, fmt, ...) \
		ovis_log(mylog, (lvl), "store_app: " fmt, ##__VA_ARGS__)

typedef struct store_app_cont_s *store_app_cont_t;
struct store_app_cont_s {
	sos_t sos;
	pthread_mutex_t lock;
	struct rbt schema_tree;	/* Tree of schema in this store */
	char path[PATH_MAX];
};

typedef struct sos_schema_ref_s {
	sos_schema_t schema;
	struct rbn rbn;
} *sos_schema_ref_t;

static char root_path[PATH_MAX]; /**< store root path */

/* ============== Store Plugin APIs ================= */

static sos_t create_container(store_app_cont_t cont)
{
	int rc = 0;
	sos_t sos;
	time_t t;
	char part_name[16];	/* Unix timestamp as string */
	sos_part_t part;

	rc = sos_container_new(cont->path, perm);
	if (rc) {
		LOG(OVIS_LERROR, "Error %d creating the container at '%s'\n",
				  rc, cont->path);
		goto err_0;
	}
	sos = sos_container_open(cont->path, SOS_PERM_RW);
	if (!sos) {
		LOG(OVIS_LERROR, "Error %d opening the container at '%s'\n",
				  errno, cont->path);
		goto err_0;
	}
	/*
	 * Create the first partition. All other partitions and
	 * rollover are handled with the SOS partition commands
	 */
	t = time(NULL);
	sprintf(part_name, "%d", (unsigned int)t);
	rc = sos_part_create(sos, part_name, cont->path);
	if (rc) {
		LOG(OVIS_LERROR,
		    "Error %d creating the partition '%s' in '%s'\n",
		    rc, part_name, cont->path);
		goto err_1;
	}
	part = sos_part_find(sos, part_name);
	if (!part) {
		LOG(OVIS_LERROR, "Newly created partition was not found\n");
		goto err_1;
	}
	rc = sos_part_state_set(part, SOS_PART_STATE_PRIMARY);
	if (rc) {
		LOG(OVIS_LERROR, "New partition could not be made primary\n");
		goto err_2;
	}
	sos_part_put(part);
	return sos;
 err_2:
	sos_part_put(part);
 err_1:
	sos_container_close(sos, SOS_COMMIT_ASYNC);
 err_0:
	if (rc)
		errno = rc;
	return NULL;
}

static void store_app_close(ldmsd_plug_handle_t handle, ldmsd_store_handle_t sh);

static ldmsd_store_handle_t
store_app_open(ldmsd_plug_handle_t handle, const char *container, const char *schema,
	       struct ldmsd_strgp_metric_list *metric_list)
{
	/* Perform `open` operation */
	int len;
	store_app_cont_t cont = calloc(1, sizeof(*cont));
	if (!cont)
		return NULL;
	rbt_init(&cont->schema_tree, (void*)strcmp);
	pthread_mutex_init(&cont->lock, NULL);
	len = snprintf(cont->path, sizeof(cont->path),
			"%s/%s", root_path, container);
	if (len >= sizeof(cont->path)) {
		errno = ENAMETOOLONG;
		goto err;
	}

	cont->sos = sos_container_open(cont->path, SOS_PERM_RW);
	if (cont->sos) /* open success */
		return cont;
	if (errno != ENOENT) /* path existed but open failed */
		goto err;
	/* container does not exist */
	cont->sos = create_container(cont);
	if (!cont->sos)
		goto err;
	return 0;
 err:
	store_app_close(handle, cont);
	return NULL;
}

static void store_app_close(ldmsd_plug_handle_t handle, ldmsd_store_handle_t sh)
{
	/* Perform `close` operation */
	store_app_cont_t cont = sh;
	struct rbn *rbn;
	sos_schema_ref_t ref;
	if (cont->sos)
		sos_container_close(cont->sos, SOS_COMMIT_ASYNC);
	while ((rbn = rbt_min(&cont->schema_tree))) {
		rbt_del(&cont->schema_tree, rbn);
		ref = container_of(rbn, struct sos_schema_ref_s, rbn);
		sos_schema_free(ref->schema);
		free(ref);
	}
	pthread_mutex_destroy(&cont->lock);
	free(cont);
}

static int store_app_flush(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	/* Perform `flush` operation */
	store_app_cont_t cont = _sh;
	pthread_mutex_lock(&cont->lock);
	if (cont->sos)
		sos_container_commit(cont->sos, SOS_COMMIT_ASYNC);
	pthread_mutex_unlock(&cont->lock);
	return 0;
}

/* Analyze data across time for a given job/rank */
static const char *job_rank_time_attrs[] = { "job_id", "rank", "timestamp" };
/* Analyze data across rank for a given job/time */
static const char *job_time_rank_attrs[] = { "job_id", "timestamp", "rank" };
/* Find all jobs in a given time period */
static const char *time_job_attrs[] = { "timestamp", "job_id" };
/* Component-wide analysis by time */
static const char *comp_time_attrs[] = { "component_id", "timestamp" };

/*
 * All sos schema use the template below. The schema.name and attrs[5].name
 * field are overriden for each metric from app_sampler.
 */
struct sos_schema_template app_schema_template = {
	.name = NULL,
	.attrs = {
		{
			.name = "timestamp",
			.type = SOS_TYPE_TIMESTAMP,
			.indexed = 0,
		},
		{
			.name = "component_id",
			.type = SOS_TYPE_UINT64,
			.indexed = 0,
		},
		{
			.name = "job_id",
			.type = SOS_TYPE_UINT64,
			.indexed = 0,
		},
		{
			.name = "app_id",
			.type = SOS_TYPE_UINT64,
			.indexed = 0,
		},
		{
			.name = "rank",
			.type = SOS_TYPE_UINT64,
		},
		{
			.name = "", /* Placeholder for the metric */
		},
		{
			.name = "comp_time",
			.type = SOS_TYPE_JOIN,
			.size = 2,
			.join_list = comp_time_attrs,
			.indexed = 1,
		},
		{	/* time_job */
			.name = "time_job",
			.type = SOS_TYPE_JOIN,
			.size = 2,
			.join_list = time_job_attrs,
			.indexed = 1,
		},
		{	/* job_rank_time */
			.name = "job_rank_time",
			.type = SOS_TYPE_JOIN,
			.size = 2,
			.join_list = job_rank_time_attrs,
			.indexed = 1,
		},
		{	/* job_time_rank */
			.name = "job_time_rank",
			.type = SOS_TYPE_JOIN,
			.size = 3,
			.join_list = job_time_rank_attrs,
			.indexed = 1,
		},
		{ NULL }
	}
};

enum sos_attr_ids {
	TIMESTAMP_ATTR,
	COMPONENT_ATTR,
	JOB_ATTR,
	APP_ATTR,
	RANK_ATTR,
	METRIC_ATTR
};

static
sos_type_t sos_type_map[] = {
	[LDMS_V_NONE] = SOS_TYPE_UINT32,
	[LDMS_V_U8] = SOS_TYPE_UINT32,
	[LDMS_V_S8] = SOS_TYPE_INT32,
	[LDMS_V_U16] = SOS_TYPE_UINT16,
	[LDMS_V_S16] = SOS_TYPE_UINT16,
	[LDMS_V_U32] = SOS_TYPE_UINT32,
	[LDMS_V_S32] = SOS_TYPE_INT32,
	[LDMS_V_U64] = SOS_TYPE_UINT64,
	[LDMS_V_S64] = SOS_TYPE_INT64,
	[LDMS_V_F32] = SOS_TYPE_FLOAT,
	[LDMS_V_D64] = SOS_TYPE_DOUBLE,
	[LDMS_V_CHAR_ARRAY] = SOS_TYPE_CHAR_ARRAY,
	[LDMS_V_U8_ARRAY] = SOS_TYPE_BYTE_ARRAY,
	[LDMS_V_S8_ARRAY] = SOS_TYPE_BYTE_ARRAY,
	[LDMS_V_U16_ARRAY] = SOS_TYPE_UINT16_ARRAY,
	[LDMS_V_S16_ARRAY] = SOS_TYPE_INT16_ARRAY,
	[LDMS_V_U32_ARRAY] = SOS_TYPE_UINT32_ARRAY,
	[LDMS_V_S32_ARRAY] = SOS_TYPE_INT32_ARRAY,
	[LDMS_V_U64_ARRAY] = SOS_TYPE_UINT64_ARRAY,
	[LDMS_V_S64_ARRAY] = SOS_TYPE_INT64_ARRAY,
	[LDMS_V_F32_ARRAY] = SOS_TYPE_FLOAT_ARRAY,
	[LDMS_V_D64_ARRAY] = SOS_TYPE_DOUBLE_ARRAY,
};

static sos_schema_t
__get_sos_schema(store_app_cont_t cont, const char *name,
		 enum ldms_value_type mtype)
{
	/* get sos schema for the metric `name` */
	struct rbn *rbn;
	sos_schema_ref_t ref = NULL;
	sos_schema_t schema = NULL;
	int rc;

	rbn = rbt_find(&cont->schema_tree, name);
	if (rbn) {
		ref = container_of(rbn, struct sos_schema_ref_s, rbn);
		schema = ref->schema;
		goto out;
	}
	ref = calloc(1, sizeof(*ref));
	if (!ref)
		goto out;
	schema = sos_schema_by_name(cont->sos, name);
	if (!schema) {
		/* Create a new schema for 'name' */
		struct sos_schema_template *tmp = &app_schema_template;
		tmp->name = name;
		bzero(&tmp->attrs[METRIC_ATTR], sizeof(tmp->attrs[0]));
		tmp->attrs[METRIC_ATTR].name = name;
		tmp->attrs[METRIC_ATTR].type = sos_type_map[mtype];
		schema = sos_schema_from_template(tmp);
		if (!schema) {
			LOG(OVIS_LERROR,
			    "%s[%d]: Error %d allocating '%s' schema.\n",
			    __func__, __LINE__, errno, name);
			free(ref);
			goto out;
		}
		rc = sos_schema_add(cont->sos, schema);
		if (rc) {
			LOG(OVIS_LERROR,
			    "%s[%d]: Error %d adding '%s' schema to "
			    "container.\n", __func__, __LINE__, rc, name);
			free(ref);
			goto out;
		}
	}
	ref->schema = schema;
	rbn_init(&ref->rbn, (char *)sos_schema_name(schema));
	rbt_ins(&cont->schema_tree, &ref->rbn);
 out:
	return schema;
}

struct metric_desc_s {
	const char *name;
	enum ldms_value_type type;
	ldms_mval_t val;
	ldms_set_t set;
	int idx;
	int count;
};

static int
__sos_val_set(sos_value_t sos_val, struct metric_desc_s *m)
{
	/* LDMS data is stored in little endian */
	int i;
	switch (m->type) {
	case LDMS_V_CHAR:
		sos_val->data->prim.byte_ = m->val->v_char;
		break;
	case LDMS_V_U8:
		sos_val->data->prim.byte_ = m->val->v_u8;
		break;
	case LDMS_V_S8:
		sos_val->data->prim.byte_ = m->val->v_s8;
		break;
	case LDMS_V_U16:
		sos_val->data->prim.uint16_ = le16toh(m->val->v_u16);
		break;
	case LDMS_V_S16:
		sos_val->data->prim.int16_ = le16toh(m->val->v_s16);
		break;
	case LDMS_V_U32:
		sos_val->data->prim.uint32_ = le32toh(m->val->v_u32);
		break;
	case LDMS_V_S32:
		sos_val->data->prim.int32_ = le32toh(m->val->v_s32);
		break;
	case LDMS_V_U64:
		sos_val->data->prim.uint64_ = le64toh(m->val->v_u64);
		break;
	case LDMS_V_S64:
		sos_val->data->prim.int64_ = le64toh(m->val->v_s64);
		break;
	case LDMS_V_F32:
		sos_val->data->prim.uint32_ = le32toh(m->val->v_u32);
		break;
	case LDMS_V_D64:
		sos_val->data->prim.uint64_ = le64toh(m->val->v_u64);
		break;
	case LDMS_V_CHAR_ARRAY:
		sos_val->data->array.count = m->count;
		memcpy(sos_val->data->array.data.char_, m->val->a_char, m->count);
		break;
	case LDMS_V_U8_ARRAY:
		sos_val->data->array.count = m->count;
		memcpy(sos_val->data->array.data.byte_, m->val->a_u8, m->count);
		break;
	case LDMS_V_S8_ARRAY:
		sos_val->data->array.count = m->count;
		memcpy(sos_val->data->array.data.char_, m->val->a_s8, m->count);
		break;
	case LDMS_V_U16_ARRAY:
	case LDMS_V_S16_ARRAY:
		sos_val->data->array.count = m->count;
		for (i = 0; i < m->count; i++) {
			sos_val->data->array.data.uint16_[i] = le16toh(m->val->a_u16[i]);
		}
		break;
	case LDMS_V_U32_ARRAY:
	case LDMS_V_S32_ARRAY:
	case LDMS_V_F32_ARRAY:
		sos_val->data->array.count = m->count;
		for (i = 0; i < m->count; i++) {
			sos_val->data->array.data.uint32_[i] = le32toh(m->val->a_u32[i]);
		}
		break;
	case LDMS_V_U64_ARRAY:
	case LDMS_V_S64_ARRAY:
	case LDMS_V_D64_ARRAY:
		sos_val->data->array.count = m->count;
		for (i = 0; i < m->count; i++) {
			sos_val->data->array.data.uint64_[i] = le64toh(m->val->a_u64[i]);
		}
		break;
	default:
		assert(0 == "BAD TYPE");
	}
	return 0;
}

static int
__store_mval(store_app_cont_t cont, struct metric_desc_s *m,
	     struct ldms_timestamp *timestamp, uint64_t component_id,
	     uint64_t job_id, uint64_t app_id, uint64_t rank)
{
	sos_schema_t sos_schema;
	sos_obj_t sos_obj;
	sos_attr_t sos_attr;
	sos_value_t v;
	struct sos_value_s v_;

	sos_schema = __get_sos_schema(cont, m->name, m->type);
	if (!sos_schema)
		return errno;
	sos_obj = sos_obj_new(sos_schema);
	if (!sos_obj) {
		LOG(OVIS_LERROR,
		    "%s[%d]: Error %d allocating Sos object for '%s'\n",
		    __func__, __LINE__, errno, m->name);
		return errno;
	}

	/* Timestamp */
	v = sos_value_by_id(&v_, sos_obj, TIMESTAMP_ATTR);
	v->data->prim.timestamp_.tv.tv_usec = timestamp->usec;
	v->data->prim.timestamp_.tv.tv_sec = timestamp->sec;
	sos_value_put(v);

	/* Component ID */
	v = sos_value_by_id(&v_, sos_obj, COMPONENT_ATTR);
	v->data->prim.uint64_ = component_id;
	sos_value_put(v);

	/* Job ID */
	v = sos_value_by_id(&v_, sos_obj, JOB_ATTR);
	v->data->prim.uint64_ = job_id;
	sos_value_put(v);

	/* App ID */
	v = sos_value_by_id(&v_, sos_obj, APP_ATTR);
	v->data->prim.uint64_ = app_id;
	sos_value_put(v);

	/* Rank */
	v = sos_value_by_id(&v_, sos_obj, RANK_ATTR);
	v->data->prim.uint64_ = rank;
	sos_value_put(v);

	/* Metric value */
	if (ldms_type_is_array(m->type)) {
		sos_attr = sos_schema_attr_by_id(sos_schema, METRIC_ATTR);
		v = sos_array_new(&v_, sos_attr, sos_obj, m->count);
	} else {
		v = sos_value_by_id(&v_, sos_obj, METRIC_ATTR);
	}
	__sos_val_set(v, m);
	sos_value_put(v);

	/* Index the object */
	sos_obj_index(sos_obj);
	sos_obj_put(sos_obj);

	return 0;
}

static int
store_app_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh, ldms_set_t set,
	        int *metric_arry, size_t metric_count)
{
	/* `store` data from `set` into the store */
	store_app_cont_t cont = (void*)_sh;
	/* store each metric in its own schema */
	/* assuming metrics in the `set` to be:
	 *   COMP_ID, JOB_ID, APP_ID, TASK_RANK, METRIC0, ... */
	uint64_t comp_id, job_id, app_id, task_rank;
	int card = ldms_set_card_get(set);
	int i, rc;
	struct ldms_timestamp timestamp;
	struct metric_desc_s m;

	pthread_mutex_lock(&cont->lock);
	timestamp = ldms_transaction_timestamp_get(set);
	comp_id = ldms_metric_get_u64(set, 0);
	job_id = ldms_metric_get_u64(set, 1);
	app_id = ldms_metric_get_u64(set, 2);
	task_rank = ldms_metric_get_u64(set, 3);

	m.set = set;
	for (i = 4; i < card; i++) {
		m.type = ldms_metric_type_get(set, i);
		m.val = ldms_metric_get(set, i);
		m.name = ldms_metric_name_get(set, i);
		m.idx = i;
		m.count = ldms_type_is_array(m.type) ?
				ldms_metric_array_get_len(set, i) : 1;
		rc = __store_mval(cont, &m, &timestamp, comp_id,
				  job_id, app_id, task_rank);
		if (rc) {
			/* give a warning and continue on */
			LOG(OVIS_LWARNING,
			    "storing value failed, rc: %d\n", rc);
		}
	}
	pthread_mutex_unlock(&cont->lock);
	return 0;
}

/* ============== Common Plugin APIs ================= */

static char *_help = "\
store_app configuration synopsis\n\
    config name=store_app perm=OCTAL_PERMISSION path=STORE_ROOT_PATH\n\
\n\
Option descriptions:\n\
    perm   Octal value for store permission (default: 660).\n\
    path   The path to the root of all SOS containers.\n\
";

static const char *
store_app_usage(ldmsd_plug_handle_t handle)
{
	return _help;
}

static int
store_app_config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl,
                 struct attr_value_list *avl)
{
	int len;
	char *val;

	val = av_value(avl, "path");
	if (!val) {
		LOG(OVIS_LERROR, "missing `path` attribute.\n");
		return EINVAL;
	}
	len = snprintf(root_path, sizeof(root_path), "%s", val);
	if (len >= sizeof(root_path)) {
		LOG(OVIS_LERROR, "`path` too long.\n");
		return ENAMETOOLONG;
	}
	val = av_value(avl, "perm");
	perm = val?strtoul(val, NULL, 8):0660;
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_store ldmsd_plugin_interface = {
	.base = {
		.config = store_app_config,
		.usage = store_app_usage,
		.type = LDMSD_PLUGIN_STORE,
		.constructor = constructor,
		.destructor = destructor,
	},
	.open = store_app_open,
	.store = store_app_store,
	.flush = store_app_flush,
	.close = store_app_close,
};
