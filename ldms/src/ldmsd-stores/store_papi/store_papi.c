/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file store_papi.c
 *
 * \brief PAPI-store
 *
 * This store handle the PAPI event metric set. It parses each set as
 * it arrives and produces output of the form:
 *
 * timestamp, component_id, job_id, app_id, rank, papi-event-value
 *
 * Each PAPI event in a metric set instance results in an object in
 * the Sos store. Although the schema for the objectg store are
 * actually all the same, there are different schema for each event
 * name in order to distinguish one event from the
 * other. Specifically, all PAPI_TOT_CYC events will be stored in a
 * schema named PAPI_TOT_CYC. Similarly, all PAPI_TOT_INS will be
 * stored in a schema named PAPI_TOT_INS.
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <sys/syscall.h>
#include <assert.h>
#include <sos/sos.h>

#include "ldmsd.h"
#include "ldmsd_store.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

typedef struct store_papi_inst_s *store_papi_inst_t;
struct store_papi_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
	char *path;
	sos_t sos;
	pthread_mutex_t lock;
	struct rbt schema_tree;	/* Tree of PAPI schema in this store */
};

typedef struct sos_schema_ref_s {
	sos_schema_t schema;
	struct rbn rbn;
} *sos_schema_ref_t;

/* Analyze data across time for a given job/rank */
static const char *job_rank_time_attrs[] = { "job_id", "rank", "timestamp" };
/* Analyze data across rank for a given job/time */
static const char *job_time_rank_attrs[] = { "job_id", "timestamp", "rank" };
/* Find all jobs in a given time period */
static const char *time_job_attrs[] = { "timestamp", "job_id" };
/* Component-wide analysis by time */
static const char *comp_time_attrs[] = { "component_id", "timestamp" };

/*
 * All PAPI-event schema use the template below. The schema.name and
 * attrs[5].name field are overriden for each event-name
 */
struct sos_schema_template papi_event_schema = {
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
			.name = "", /* Placeholder for the PAPI event name */
			.type = SOS_TYPE_UINT64,
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
	EVENT_ATTR
};

static int schema_cmp(void *a, const void *b)
{
	char *x = a;
	char *y = (char *)b;

	return strcmp(x, y);
}

static size_t sos_schema_template_sz(struct sos_schema_template *s)
{
	size_t sz = sizeof(*s);
	struct sos_schema_template_attr *a;
	for (a = s->attrs; a->name; a++) {
		sz += sizeof(*a);
	}
	sz += sizeof(*a); /* the termination record */
	return sz;
}

static sos_schema_t get_schema(store_papi_inst_t inst, const char *name)
{
	/* caller must have inst->lock held */
	int rc;
	struct rbn *rbn;
	sos_schema_ref_t ref;
	sos_schema_t schema = NULL;

	rbn = rbt_find(&inst->schema_tree, name);
	if (rbn) {
		ref = container_of(rbn, struct sos_schema_ref_s, rbn);
		schema = ref->schema;
		goto out;
	}
	ref = malloc(sizeof *ref);
	if (!ref) {
		INST_LOG(inst, LDMSD_LERROR,
			 "%s[%d]: Memory allocation error.\n",
			 __func__, __LINE__);
		goto out;
	}
	/* Look it up */
	schema = sos_schema_by_name(inst->sos, name);
	if (!schema) {
		/* Create a new schema for 'name' */
		size_t sz = sos_schema_template_sz(&papi_event_schema);
		char buff[sz];
		struct sos_schema_template *tmp = (void*)buff;
		memcpy(buff, &papi_event_schema, sz);
		tmp->name = name;
		tmp->attrs[EVENT_ATTR].name = name;
		schema = sos_schema_from_template(tmp);
		if (!schema) {
			INST_LOG(inst, LDMSD_LERROR,
				 "%s[%d]: Error %d allocating '%s' schema.\n",
				 __func__, __LINE__, errno, name);
			free(ref);
			goto out;
		}
		rc = sos_schema_add(inst->sos, schema);
		if (rc) {
			INST_LOG(inst, LDMSD_LERROR,
				 "%s[%d]: Error %d adding '%s' schema to "
				 "container.\n", __func__, __LINE__, rc, name);
			free(ref);
			goto out;
		}
	}
	ref->schema = schema;
	rbn_init(&ref->rbn, (char *)sos_schema_name(schema));
	rbt_ins(&inst->schema_tree, &ref->rbn);
 out:
	return schema;
}

static sos_t create_container(store_papi_inst_t inst)
{
	ldmsd_store_type_t store = LDMSD_STORE(inst);
	int rc = 0;
	sos_t sos;
	time_t t;
	char part_name[16];	/* Unix timestamp as string */
	sos_part_t part;

	rc = sos_container_new(inst->path, store->perm);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Error %d creating the container at '%s'\n",
			 rc, inst->path);
		goto err_0;
	}
	sos = sos_container_open(inst->path, SOS_PERM_RW);
	if (!sos) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Error %d opening the container at '%s'\n",
			 errno, inst->path);
		goto err_0;
	}
	/*
	 * Create the first partition. All other partitions and
	 * rollover are handled with the SOS partition commands
	 */
	t = time(NULL);
	sprintf(part_name, "%d", (unsigned int)t);
	rc = sos_part_create(sos, part_name, inst->path);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Error %d creating the partition '%s' in '%s'\n",
			 rc, part_name, inst->path);
		goto err_1;
	}
	part = sos_part_find(sos, part_name);
	if (!part) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Newly created partition was not found\n");
		goto err_1;
	}
	rc = sos_part_state_set(part, SOS_PART_STATE_PRIMARY);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR,
			 "New partition could not be made primary\n");
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

/* ============== Store Plugin APIs ================= */

static int
store_papi_open(ldmsd_plugin_inst_t pi, ldmsd_strgp_t strgp)
{
	/* Perform `open` operation */
	store_papi_inst_t inst = (void*)pi;

	/* See if it exists, but has not been opened yet. */
	inst->sos = sos_container_open(inst->path, SOS_PERM_RW);
	if (inst->sos) {
		return 0;
	}

	/* Create the container */
	inst->sos = create_container(inst);
	if (!inst->sos)
		return errno;
	return 0;
}

static int
store_papi_close(ldmsd_plugin_inst_t pi)
{
	/* Perform `close` operation */
	struct rbn *rbn;
	sos_schema_ref_t ref;
	store_papi_inst_t inst = (void*)pi;
	pthread_mutex_lock(&inst->lock);
	if (inst->sos) {
		sos_container_close(inst->sos, SOS_COMMIT_ASYNC);
		inst->sos = NULL;
	}
	while ((rbn = rbt_min(&inst->schema_tree))) {
		rbt_del(&inst->schema_tree, rbn);
		ref = container_of(rbn, struct sos_schema_ref_s, rbn);
		sos_schema_free(ref->schema);
		free(ref);
	}
	pthread_mutex_unlock(&inst->lock);
	return 0;
}

static int
store_papi_flush(ldmsd_plugin_inst_t pi)
{
	store_papi_inst_t inst = (void*)pi;
	pthread_mutex_lock(&inst->lock);
	if (inst->sos)
		sos_container_commit(inst->sos, SOS_COMMIT_ASYNC);
	pthread_mutex_unlock(&inst->lock);
	return 0;
}

static uint64_t get_by_name(store_papi_inst_t inst, ldms_set_t set,
			    const char *name)
{
	int mid = ldms_metric_by_name(set, name);
	if (mid < 0) {
		INST_LOG(inst, LDMSD_LERROR,
			 "%s[%d]: set is missing the '%s' attribute\n",
			 __func__, __LINE__, name);
		return -1;
	}
	return ldms_metric_get_u64(set, mid);
}

static int
store_papi_store(ldmsd_plugin_inst_t pi, ldms_set_t set, ldmsd_strgp_t strgp)
{
	/* `store` data from `set` into the store */
	store_papi_inst_t inst = (void*)pi;

	sos_obj_t obj;
	sos_schema_t schema;
	struct sos_value_s v_;
	int local_ranks, event_mid, last_mid, i;

	struct ldms_timestamp timestamp = ldms_transaction_timestamp_get(set);
	uint64_t component_id = get_by_name(inst, set, "component_id");
	uint64_t job_id = get_by_name(inst, set, "job_id");
	uint64_t app_id = get_by_name(inst, set, "app_id");

	int rank_mid = ldms_metric_by_name(set, "task_ranks");
	if (rank_mid < 0) {
		INST_LOG(inst, LDMSD_LERROR,
			 "%s[%d]: set is missing the 'rank' attribute\n",
			 __func__, __LINE__);
		return -1;
	}
	pthread_mutex_lock(&inst->lock);
	if (!inst->sos) {
		errno = EINVAL;
		INST_LOG(inst, LDMSD_LERROR, "store not opened.\n");
		goto err;
	}
	local_ranks = ldms_metric_array_get_len(set, rank_mid);
	event_mid = rank_mid + 1;
	last_mid = ldms_set_card_get(set);
	for (event_mid = rank_mid + 1; event_mid < last_mid; event_mid ++) {
		for (i = 0; i < local_ranks; i++) {
			schema = get_schema(inst, ldms_metric_name_get(set,
								event_mid));
			if (!schema) /* get_schema() already log error */
				goto err;
			obj = sos_obj_new(schema);
			if (!obj) {
				INST_LOG(inst, LDMSD_LERROR,
					 "%s[%d]: Error %d allocating Sos "
					 "object for '%s'\n",
					 __func__, __LINE__, errno,
					 sos_schema_name(schema));
				goto err;
			}

			/* Timestamp */
			sos_value_t v = sos_value_by_id(&v_, obj, TIMESTAMP_ATTR);
			v->data->prim.timestamp_.tv.tv_usec = timestamp.usec;
			v->data->prim.timestamp_.tv.tv_sec = timestamp.sec;
			sos_value_put(v);

			/* Component ID */
			v = sos_value_by_id(&v_, obj, COMPONENT_ATTR);
			v->data->prim.uint64_ = component_id;
			sos_value_put(v);

			/* Job ID */
			v = sos_value_by_id(&v_, obj, JOB_ATTR);
			v->data->prim.uint64_ = job_id;
			sos_value_put(v);

			/* App ID */
			v = sos_value_by_id(&v_, obj, APP_ATTR);
			v->data->prim.uint64_ = app_id;
			sos_value_put(v);

			/* Rank */
			v = sos_value_by_id(&v_, obj, RANK_ATTR);
			v->data->prim.uint32_ = ldms_metric_array_get_u32(set, rank_mid, i);
			sos_value_put(v);

			/* Event count */
			v = sos_value_by_id(&v_, obj, EVENT_ATTR);
			v->data->prim.int64_ = ldms_metric_array_get_s64(set, event_mid, i);
			sos_value_put(v);

			sos_obj_index(obj);
			sos_obj_put(obj);
		}
	}
	pthread_mutex_unlock(&inst->lock);
	return 0;
err:
	pthread_mutex_unlock(&inst->lock);
	return errno;
}

/* ============== Common Plugin APIs ================= */

static const char *
store_papi_desc(ldmsd_plugin_inst_t pi)
{
	return "store_papi - store_papi store plugin";
}

static char *_help = "\
store_papi configuration synopsis\n\
    config name=INST perm=OCTAL_PERMISSION path=CONTAINER_PATH\n\
\n\
Option descriptions:\n\
    perm   Octal value for store permission (default: 660).\n\
    path   The path to the SOS container.\n\
";

static const char *
store_papi_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static int
store_papi_config(ldmsd_plugin_inst_t pi, json_entity_t json, char *ebuf, int ebufsz)
{
	store_papi_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;
	int rc;
	const char *val;

	rc = store->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	val = json_attr_find_str(json, "path");
	if (!val) {
		snprintf(ebuf, ebufsz, "missing `path` attribute.\n");
		return EINVAL;
	}

	inst->path = strdup(val);
	if (!inst->path) {
		snprintf(ebuf, ebufsz, "Out of memory.\n");
		return ENOMEM;
	}

	return 0;
}

static void
store_papi_del(ldmsd_plugin_inst_t pi)
{
	store_papi_inst_t inst = (void*)pi;
	/* make sure that it is closed */
	store_papi_close(pi);
	if (inst->path)
		free(inst->path);
}

static int
store_papi_init(ldmsd_plugin_inst_t pi)
{
	store_papi_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;

	/* override store operations */
	store->open = store_papi_open;
	store->close = store_papi_close;
	store->flush = store_papi_flush;
	store->store = store_papi_store;

	rbt_init(&inst->schema_tree, schema_cmp);

	return 0;
}

static
struct store_papi_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = LDMSD_STORE_TYPENAME,
		.plugin_name = "store_papi",

                /* Common Plugin APIs */
		.desc   = store_papi_desc,
		.help   = store_papi_help,
		.init   = store_papi_init,
		.del    = store_papi_del,
		.config = store_papi_config,

	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	store_papi_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
