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
#include "ldms.h"
#include "ldmsd.h"
#include "slurm_sampler.h"

#define STORE "store_slurm"

static ovis_log_t mylog;

#define LOG_(level, ...) do { \
	ovis_log(mylog, level, ## __VA_ARGS__); \
} while(0);

enum store_slurm_verbosity {
	/**
	 * \brief SUMMARY
	 * timestamp-start, component_id, job_id, state, rank, start_time, end_time
	 */
	SUMMARY,

	/**
	 * \brief BY_RANK
	 * timestamp-start, component_id-0,   job_id, status, rank-0,   start_time, end_time
	 * timestamp-start, component_id-..., job_id, status, rank-..., start_time, end_time
	 * timestamp-start, component_id-M,   job_id, status, rank-N,   start_time, end_time
	 *
	 * timestamp-end, component_id-0,   job_id, status, rank-0,   start_time, end_time
	 * timestamp-end, component_id-..., job_id, status, rank-..., start_time, end_time
	 * timestamp-end, component_id-M,   job_id, status, rank-N,   start_time, end_time
	 */
	BY_RANK,

	/**
	 * \brief BY_TIME
	 * timestamp-start, component_id-0,   job_id, status, rank-0,   start_time, end_time
	 * timestamp-start, component_id-..., job_id, status, rank-..., start_time, end_time
	 * timestamp-start, component_id-M,   job_id, status, rank-N,   start_time, end_time
	 *
	 * timestamp-1, component_id-0,   job_id, status, rank-0,   start_time, end_time
	 * timestamp-1, component_id-..., job_id, status, rank-..., start_time, end_time
	 * timestamp-1, component_id-M,   job_id, status, rank-N,   start_time, end_time
	 *
	 * timestamp-..., component_id-0,   job_id, status, rank-0,   start_time, end_time
	 * timestamp-..., component_id-..., job_id, status, rank-..., start_time, end_time
	 * timestamp-..., component_id-M,   job_id, status, rank-N,   start_time, end_time
	 *
	 * timestamp-end, component_id-0,   job_id, status, rank-0,   start_time, end_time
	 * timestamp-end, component_id-..., job_id, status, rank-..., start_time, end_time
	 * timestamp-end, component_id-M,   job_id, status, rank-N,   start_time, end_time
	 */
	BY_TIME
};
static int verbosity = SUMMARY;

typedef struct sos_handle_s {
	int ref_count;
	char path[PATH_MAX];
	sos_t sos;
	LIST_ENTRY(sos_handle_s) entry;
} *sos_handle_t;

static LIST_HEAD(sos_handle_list, sos_handle_s) sos_handle_list;

/*
 * NOTE:
 *   <sos::path> = <root_path>/<container>
 */
struct sos_instance {
	struct ldmsd_store *store;
	char *container;
	char *schema_name;
	char *path; /**< <root_path>/<container> */
	sos_handle_t sos_handle; /**< sos handle */
	sos_schema_t sos_schema;
	pthread_mutex_t lock; /**< lock at metric store level */

	sos_attr_t job_rank_time_attr;
	sos_attr_t job_time_rank_attr;

	LIST_ENTRY(sos_instance) entry;
};
static pthread_mutex_t cfg_lock;
LIST_HEAD(sos_inst_list, sos_instance) inst_list;

static char root_path[PATH_MAX]; /**< store root path */

static const char *comp_time_attrs[] = { "component_id", "timestamp" };
static const char *time_job_attrs[] = { "timestamp", "job_id" };
static const char *job_rank_time_attrs[] = { "job_id", "task_rank", "timestamp" };
static const char *job_time_rank_attrs[] = { "job_id", "timestamp", "task_rank" };
static const char *tag_start_job_attrs[] = { "job_tag", "job_start", "job_id" };

struct sos_schema_template slurm_schema_template = {
	.name = "job",
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
		},
		{
			.name = "job_name",
			.type = SOS_TYPE_CHAR_ARRAY,
		},
		{
			.name = "job_tag",
			.type = SOS_TYPE_CHAR_ARRAY,
		},
		{
			.name = "job_user",
			.type = SOS_TYPE_CHAR_ARRAY,
		},
		{
			.name = "job_size",
			.type = SOS_TYPE_UINT32,
		},
		{
			.name = "job_status",
			.type = SOS_TYPE_UINT16,
		},
		{
			.name = "uid",
			.type = SOS_TYPE_UINT32,
		},
		{
			.name = "gid",
			.type = SOS_TYPE_UINT32,
		},
		{
			.name = "job_start",
			.type = SOS_TYPE_UINT32,
		},
		{
			.name = "job_end",
			.type = SOS_TYPE_UINT32,
		},
		{
			.name = "node_count",
			.type = SOS_TYPE_UINT32,
		},
		{
			.name = "task_count",
			.type = SOS_TYPE_UINT32,
		},
		{
			.name = "task_pid",
			.type = SOS_TYPE_UINT32,
		},
		{
			.name = "task_rank",
			.type = SOS_TYPE_UINT32,
		},
		{
			.name = "task_exit_status",
			.type = SOS_TYPE_UINT32,
		},
		{
			.name = "time_job",
			.type = SOS_TYPE_JOIN,
			.indexed = 1,
			.join_list = time_job_attrs,
			.size = 2
		},
		{
			.name = "comp_time",
			.type = SOS_TYPE_JOIN,
			.indexed = 1,
			.join_list = comp_time_attrs,
			.size = 2
		},
		{
			.name = "job_rank_time",
			.type = SOS_TYPE_JOIN,
			.indexed = 1,
			.join_list = job_rank_time_attrs,
			.size = 3
		},
		{
			.name = "job_time_rank",
			.type = SOS_TYPE_JOIN,
			.indexed = 1,
			.join_list = job_time_rank_attrs,
			.size = 3
		},
		{
			.name = "tag_start_job",
			.type = SOS_TYPE_JOIN,
			.indexed = 1,
			.join_list = tag_start_job_attrs,
			.size = 3
		},
		{ NULL }
	}
};

enum schema_attr_ids {
	TIMESTAMP_ATTR,
	COMPONENT_ID_ATTR,
	JOB_ID_ATTR,
	APP_ID_ATTR,
	JOB_NAME_ATTR,
	JOB_TAG_ATTR,
	JOB_USER_ATTR,
	JOB_SIZE_ATTR,
	JOB_STATUS_ATTR,
	UID_ATTR,
	GID_ATTR,
	JOB_START_ATTR,
	JOB_END_ATTR,
	NODE_COUNT_ATTR,
	TASK_COUNT_ATTR,
	TASK_PID_ATTR,
	TASK_RANK_ATTR,
	TASK_EXIT_STATUS_ATTR,
	TIME_JOB_ATTR,
	COMP_TIME_ATTR,
	JOB_RANK_TIME_ATTR
};

sos_handle_t create_handle(const char *path, sos_t sos)
{
	sos_handle_t h = calloc(1, sizeof(*h));
	if (!h)
		return NULL;
	int len = strlen(path);
	if (len >= sizeof(h->path)) {
		errno = ENAMETOOLONG;
		free(h);
		return NULL;
	}
	memcpy(h->path, path, len+1);
	h->ref_count = 1;
	h->sos = sos;

	pthread_mutex_lock(&cfg_lock);
	LIST_INSERT_HEAD(&sos_handle_list, h, entry);
	pthread_mutex_unlock(&cfg_lock);
	return h;
}

sos_handle_t create_container(const char *path)
{
	int rc = 0;
	sos_t sos;
	time_t t;
	char part_name[16];	/* Unix timestamp as string */
	sos_part_t part;

	rc = sos_container_new(path, 0660);
	if (rc) {
		LOG_(OVIS_LERROR, "Error %d creating the container at '%s'\n",
		       rc, path);
		goto err_0;
	}
	sos = sos_container_open(path, SOS_PERM_RW);
	if (!sos) {
		LOG_(OVIS_LERROR, "Error %d opening the container at '%s'\n",
		       errno, path);
		goto err_0;
	}
	/*
	 * Create the first partition. All other partitions and
	 * rollover are handled with the SOS partition commands
	 */
	t = time(NULL);
	sprintf(part_name, "%d", (unsigned int)t);
	rc = sos_part_create(sos, part_name, path);
	if (rc) {
		LOG_(OVIS_LERROR, "Error %d creating the partition '%s' in '%s'\n",
		       rc, part_name, path);
		goto err_1;
	}
	part = sos_part_find(sos, part_name);
	if (!part) {
		LOG_(OVIS_LERROR, "Newly created partition was not found\n");
		goto err_1;
	}
	rc = sos_part_state_set(part, SOS_PART_STATE_PRIMARY);
	if (rc) {
		LOG_(OVIS_LERROR, "New partition could not be made primary\n");
		goto err_2;
	}
	sos_part_put(part);
	return create_handle(path, sos);
 err_2:
	sos_part_put(part);
 err_1:
	sos_container_close(sos, SOS_COMMIT_ASYNC);
 err_0:
	if (rc)
		errno = rc;
	return NULL;
}

static void close_container(sos_handle_t h)
{
	assert(h->ref_count == 0);
	sos_container_close(h->sos, SOS_COMMIT_ASYNC);
	free(h);
}

static void put_container_no_lock(sos_handle_t h)
{
	h->ref_count--;
	if (h->ref_count == 0) {
		/* remove from list, destroy the handle */
		LIST_REMOVE(h, entry);
		close_container(h);
	}
}

static void put_container(sos_handle_t h)
{
	pthread_mutex_lock(&cfg_lock);
	put_container_no_lock(h);
	pthread_mutex_unlock(&cfg_lock);
}

static sos_handle_t find_container(const char *path)
{
	sos_handle_t h;
	LIST_FOREACH(h, &sos_handle_list, entry){
		if (0 != strncmp(path, h->path, sizeof(h->path)))
			continue;

		/* found */
		/* take reference */
		pthread_mutex_lock(&cfg_lock);
		h->ref_count++;
		pthread_mutex_unlock(&cfg_lock);
		return h;
	}
	return NULL;
}

/**
 * \brief Configuration
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	struct sos_instance *si;
	int rc, len;
	char *value;
	value = av_value(avl, "verbosity");
	if (!value) {
		verbosity = SUMMARY;
	} else {
		verbosity = atoi(value);
	}
	value = av_value(avl, "path");
	if (!value) {
		LOG_(OVIS_LERROR,
		       "%s[%d]: The 'path' configuraiton option is required.\n",
		       __func__, __LINE__);
		return EINVAL;
	}
	len = strlen(value);
	if (len >= PATH_MAX) {
		LOG_(OVIS_LERROR,
		       "%s[%d]: The 'path' is too long.\n",
		       __func__, __LINE__);
		return ENAMETOOLONG;
	}
	pthread_mutex_lock(&cfg_lock);
	memcpy(root_path, value, len+1);

	/*
	 * Run through all open containers and close them. They will
	 * get re-opened when store() is next called
	 */
	rc = ENOMEM;
	LIST_FOREACH(si, &inst_list, entry) {
		pthread_mutex_lock(&si->lock);
		if (si->sos_handle) {
			put_container_no_lock(si->sos_handle);
			si->sos_handle = NULL;
		}
		size_t pathlen =
			strlen(root_path) + strlen(si->container) + 4;
		if (si->path)
			free(si->path);
		si->path = malloc(pathlen);
		if (!si->path) {
			LOG_(OVIS_LERROR, "%s[%d]: Memory allocation error.\n",
			       __func__, __LINE__);
			goto err_0;
		}
		sprintf(si->path, "%s/%s", root_path, si->container);
		pthread_mutex_unlock(&si->lock);
	}
	pthread_mutex_unlock(&cfg_lock);
	return 0;

 err_0:
	pthread_mutex_unlock(&cfg_lock);
	return rc;
}

static void term(ldmsd_plug_handle_t handle)
{
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "    config name=" STORE " path=<path>\n"
		"       path The path to primary storage\n";
}

static ldmsd_store_handle_t
open_store(ldmsd_plug_handle_t s, const char *container, const char *schema,
	   struct ldmsd_strgp_metric_list *metric_list)
{
	struct sos_instance *si = NULL;

	si = calloc(1, sizeof(*si));
	if (!si)
		goto out;
	si->container = strdup(container);
	if (!si->container)
		goto err1;
	si->schema_name = strdup(schema);
	if (!si->schema_name)
		goto err2;
	size_t pathlen =
		strlen(root_path) + strlen(si->container) + 4;
	si->path = malloc(pathlen);
	if (!si->path)
		goto err3;
	sprintf(si->path, "%s/%s", root_path, container);
	pthread_mutex_init(&si->lock, NULL);
	pthread_mutex_lock(&cfg_lock);
	LIST_INSERT_HEAD(&inst_list, si, entry);
	pthread_mutex_unlock(&cfg_lock);
	return si;
 err3:
	free(si->schema_name);
 err2:
	free(si->container);
 err1:
	free(si);
 out:
	return NULL;
}

static int
_open_store(struct sos_instance *si, ldms_set_t set)
{
	int rc;
	sos_schema_t schema;

	/* Check if the container is already open */
	si->sos_handle = find_container(si->path);
	if (si->sos_handle) {
		/* See if the required schema is already present */
		schema = sos_schema_by_name(si->sos_handle->sos, si->schema_name);
		if (!schema)
			goto add_schema;
		si->sos_schema = schema;
		return 0;
	}
	/* See if it exists, but has not been opened yet. */
	sos_t sos = sos_container_open(si->path, SOS_PERM_RW);
	if (sos) {
		/* Create a new handle and add it for this SOS */
		si->sos_handle = create_handle(si->path, sos);
		if (!si->sos_handle) {
			sos_container_close(sos, SOS_COMMIT_ASYNC);
			return ENOMEM;
		}

		/* See if the schema exists */
		schema = sos_schema_by_name(sos, si->schema_name);
		if (!schema)
			goto add_schema;
		goto out;
	}

	si->sos_handle = create_container(si->path);
	if (!si->sos_handle) {
		return errno;
	}

 add_schema:
	slurm_schema_template.name = si->schema_name;
	schema = sos_schema_from_template(&slurm_schema_template);
	if (!schema)
		goto err_0;
	rc = sos_schema_add(si->sos_handle->sos, schema);
	if (rc) {
		sos_schema_free(schema);
		if (rc == EEXIST) {
			/* Added by our failover peer? */
			schema = sos_schema_by_name(si->sos_handle->sos,
						    si->schema_name);
			if (schema)
				goto out;
		}
		LOG_(OVIS_LERROR, "Error %d adding the schema to the container\n", rc);
		goto err_1;
	}
 out:
	si->sos_schema = schema;
	si->job_rank_time_attr = sos_schema_attr_by_name(schema, "job_rank_time");
	si->job_time_rank_attr = sos_schema_attr_by_name(schema, "job_time_rank");
	return 0;
 err_1:
	sos_schema_free(schema);
 err_0:
	put_container(si->sos_handle);
	si->sos_handle = NULL;
	return EINVAL;
}

static int
make_obj(ldms_set_t set, int slot, int task, sos_obj_t obj)
{
	struct sos_value_s v_;
	sos_value_t v;
	uint64_t u64;
	uint32_t u32;
	struct ldms_timestamp timestamp = ldms_transaction_timestamp_get(set);

	/* Timestamp */
	v = sos_value_by_id(&v_, obj, TIMESTAMP_ATTR);
	v->data->prim.timestamp_.tv.tv_usec = timestamp.usec;
	v->data->prim.timestamp_.tv.tv_sec = timestamp.sec;
	sos_value_put(v);

	/* Component ID */
	u64 = ldms_metric_array_get_u64(set, COMPONENT_ID_MID(set), slot);
	v = sos_value_by_id(&v_, obj, COMPONENT_ID_ATTR);
	v->data->prim.uint64_ = u64;
	sos_value_put(v);

	/* Job ID */
	u64 = ldms_metric_array_get_u64(set, JOB_ID_MID(set), slot);
	v = sos_value_by_id(&v_, obj, JOB_ID_ATTR);
	v->data->prim.uint64_ = u64;
	sos_value_put(v);

	/* App ID */
	u64 = ldms_metric_array_get_u64(set, APP_ID_MID(set), slot);
	v = sos_value_by_id(&v_, obj, APP_ID_ATTR);
	v->data->prim.uint64_ = u64;
	sos_value_put(v);

	/* job_name */
	sos_attr_t attr;
	attr = sos_schema_attr_by_id(sos_obj_schema(obj), JOB_NAME_ATTR);
	sos_obj_attr_from_str(obj, attr,
			      ldms_metric_array_get_str(set,
							JOB_NAME_MID(set) + slot),
			      NULL);

	/* job_tag */
	attr = sos_schema_attr_by_id(sos_obj_schema(obj), JOB_TAG_ATTR);
	sos_obj_attr_from_str(obj, attr,
			      ldms_metric_array_get_str(set,
							JOB_TAG_MID(set) + slot),
			      NULL);

	/* job_user */
	attr = sos_schema_attr_by_id(sos_obj_schema(obj), JOB_USER_ATTR);
	sos_obj_attr_from_str(obj, attr,
			      ldms_metric_array_get_str(set,
							JOB_USER_MID(set) + slot),
			      NULL);

	/* UID */
	u32 = ldms_metric_array_get_u32(set, JOB_UID_MID(set), slot);
	v = sos_value_by_id(&v_, obj, UID_ATTR);
	v->data->prim.uint32_ = u32;
	sos_value_put(v);

	/* GID */
	u32 = ldms_metric_array_get_u32(set, JOB_GID_MID(set), slot);
	v = sos_value_by_id(&v_, obj, GID_ATTR);
	v->data->prim.uint32_ = u32;
	sos_value_put(v);

	/* job_start */
	v = sos_value_by_id(&v_, obj, JOB_START_ATTR);
	v->data->prim.uint32_ = ldms_metric_array_get_u32(set, JOB_START_MID(set), slot);
	sos_value_put(v);

	/* job_size */
	u32 = ldms_metric_array_get_u32(set, JOB_SIZE_MID(set), slot);
	v = sos_value_by_id(&v_, obj, JOB_SIZE_ATTR);
	v->data->prim.uint32_ = u32;
	sos_value_put(v);

	/* node_count */
	v = sos_value_by_id(&v_, obj, NODE_COUNT_ATTR);
	v->data->prim.uint32_ = ldms_metric_array_get_u32(set, NODE_COUNT_MID(set), slot);
	sos_value_put(v);

	/* task_count */
	v = sos_value_by_id(&v_, obj, TASK_COUNT_ATTR);
	v->data->prim.uint32_ = ldms_metric_array_get_u32(set, TASK_COUNT_MID(set), slot);
	sos_value_put(v);

	/* task_pid */
	v = sos_value_by_id(&v_, obj, TASK_PID_ATTR);
	v->data->prim.uint32_ = ldms_metric_array_get_u32(set, TASK_PID_MID(set) + slot, task);
	sos_value_put(v);

	/* task_rank */
	v = sos_value_by_id(&v_, obj, TASK_RANK_ATTR);
	v->data->prim.uint32_ = ldms_metric_array_get_u32(set, TASK_RANK_MID(set) + slot, task);
	sos_value_put(v);

	/* task_exit_status */
	v = sos_value_by_id(&v_, obj, TASK_EXIT_STATUS_ATTR);
	v->data->prim.uint32_ = ldms_metric_array_get_u32(set, TASK_EXIT_STATUS_MID(set) + slot, task);

	return 0;
}


static int
store_summary(struct sos_instance *si, ldms_set_t set, int slot)
{
	struct sos_value_s v_;
	sos_value_t v;
	int rc;
	sos_obj_t obj;
	SOS_KEY(key);
	sos_key_t k;
	uint64_t job_id;

	job_id = ldms_metric_array_get_u64(set, JOB_ID_MID(set), slot);
	k = sos_key_for_attr(key, si->job_rank_time_attr,
			     job_id, 0, 0);
	if (!k)
		return errno;

	obj = sos_index_find_sup(sos_attr_index(si->job_rank_time_attr), key);
	if (obj) {
		int match;
		/* Check check the job_id and rank matches */
		v = sos_value_by_id(&v_, obj, JOB_ID_ATTR);
		match = (v->data->prim.uint64_ == job_id);
		sos_value_put(v);
		if (!match) {
			sos_obj_put(obj);
			obj = NULL;
		}
	}
	if (!obj) {
		obj = sos_obj_new(si->sos_schema);
		if (!obj) {
			rc = errno;
			LOG_(OVIS_LERROR, "%s[%d]: Error %d allocating '%s' object.\n",
			       __func__, __LINE__, rc, sos_schema_name(si->sos_schema));
			return rc;
		}
		make_obj(set, slot, 0, obj);
		sos_obj_index(obj);
	}

	/* job_status */
	v = sos_value_by_id(&v_, obj, JOB_STATUS_ATTR);
	if (ldms_metric_array_get_u8(set, JOB_STATE_MID(set), slot) <= JOB_RUNNING)
		/* running */
		v->data->prim.uint16_ = 1;
	else
		/* complete */
		v->data->prim.uint16_ = 2;
	sos_value_put(v);

	/* job_end */
	v = sos_value_by_id(&v_, obj, JOB_END_ATTR);
	v->data->prim.uint32_ = ldms_metric_array_get_u32(set, JOB_END_MID(set), slot);
	sos_value_put(v);

	sos_obj_put(obj);

	return 0;
}

static int
store_ranks(struct sos_instance *si, ldms_set_t set, int slot)
{
	struct sos_value_s v_;
	sos_value_t v;
	int rc;
	sos_obj_t obj;
	SOS_KEY(key);
	sos_key_t k;
	int task;
	uint64_t job_id;
	uint32_t rank;

	job_id = ldms_metric_array_get_u64(set, JOB_ID_MID(set), slot);
	for (task = 0; task < ldms_metric_array_get_u32(set, TASK_COUNT_MID(set), slot); task++) {
		rank = ldms_metric_array_get_u32(set, TASK_RANK_MID(set) + slot, task);
		k = sos_key_for_attr(key, si->job_rank_time_attr,
				     job_id, rank, 0);
		if (!k)
			return errno;

		obj = sos_index_find_sup(sos_attr_index(si->job_rank_time_attr), key);
		if (obj) {
			int match;
			/* Check check the job_id and rank matches */
			v = sos_value_by_id(&v_, obj, JOB_ID_ATTR);
			match = (v->data->prim.uint64_ == job_id);
			sos_value_put(v);
			if (match) {
				/* check rank */
				v = sos_value_by_id(&v_, obj, TASK_RANK_ATTR);
				match = (v->data->prim.uint32_ == rank);
				sos_value_put(v);
				if (!match) {
					sos_obj_put(obj);
					obj = NULL;
				}
			} else {
				sos_obj_put(obj);
				obj = NULL;
			}
		}
		if (!obj) {
			obj = sos_obj_new(si->sos_schema);
			if (!obj) {
				rc = errno;
				LOG_(OVIS_LERROR, "%s[%d]: Error %d allocating '%s' object.\n",
				       __func__, __LINE__, rc, sos_schema_name(si->sos_schema));
				return rc;
			}
			make_obj(set, slot, task, obj);
			sos_obj_index(obj);
		}

		/* job_status */
		v = sos_value_by_id(&v_, obj, JOB_STATUS_ATTR);
		if (ldms_metric_array_get_u8(set, JOB_STATE_MID(set), slot) <= JOB_RUNNING)
			/* running */
			v->data->prim.uint16_ = 1;
		else
			/* complete */
			v->data->prim.uint16_ = 2;
		sos_value_put(v);

		/* job_end */
		v = sos_value_by_id(&v_, obj, JOB_END_ATTR);
		v->data->prim.uint32_ = ldms_metric_array_get_u32(set, JOB_END_MID(set), slot);
		sos_value_put(v);

		sos_obj_put(obj);
	}

	return 0;
}

static int
store_times(struct sos_instance *si, ldms_set_t set, int slot)
{
	struct sos_value_s v_;
	sos_value_t v;
	int rc;
	sos_obj_t obj;
	int task;

	for (task = 0; task < ldms_metric_array_get_u32(set, TASK_COUNT_MID(set), slot); task++) {
		obj = sos_obj_new(si->sos_schema);
		if (!obj) {
			rc = errno;
			LOG_(OVIS_LERROR, "%s[%d]: Error %d allocating '%s' object.\n",
			       __func__, __LINE__, rc, sos_schema_name(si->sos_schema));
			return rc;
		}
		make_obj(set, slot, task, obj);
		sos_obj_index(obj);

		/* job_status */
		v = sos_value_by_id(&v_, obj, JOB_STATUS_ATTR);
		if (ldms_metric_array_get_u8(set, JOB_STATE_MID(set), slot) <= JOB_RUNNING)
			/* running */
			v->data->prim.uint16_ = 1;
		else
			/* complete */
			v->data->prim.uint16_ = 2;
		sos_value_put(v);

		/* job_end */
		v = sos_value_by_id(&v_, obj, JOB_END_ATTR);
		v->data->prim.uint32_ = ldms_metric_array_get_u32(set, JOB_END_MID(set), slot);
		sos_value_put(v);

		sos_obj_put(obj);
	}

	return 0;
}

int first_slot(ldms_set_t set, int *last_idx)
{
	return -1;
}

int next_slot()
{
	return -1;
}

static int
store(ldmsd_plug_handle_t handle,
      ldmsd_store_handle_t _sh,
      ldms_set_t set,
      int *metric_arry /* ignored */,
      size_t metric_count /* ignored */)
{
	int rc = 0;
	struct sos_instance *si = _sh;

	if (!si)
		return EINVAL;

	pthread_mutex_lock(&si->lock);
	if (!si->sos_handle) {
		rc = _open_store(si, set);
		if (rc) {
			pthread_mutex_unlock(&si->lock);
			LOG_(OVIS_LERROR, "Failed to create store "
			       "for %s.\n", si->container);
			errno = rc;
			goto err;
		}
	}

	int slot;
	int slot_count = ldms_metric_array_get_len(set, JOB_ID_MID(set));
	int list_tail = ldms_metric_get_u32(set, JOB_SLOT_LIST_TAIL_MID(set));
	int list_idx = list_tail;

	/*
	 * Scrub the the set from list_tail + 1 ... list_tail */
	do {
		int job_id;
		uint8_t state;
		slot = ldms_metric_array_get_s32(set, JOB_SLOT_LIST_MID(set), list_idx);
		if (slot < 0)
			goto skip;
		job_id = ldms_metric_array_get_u64(set, JOB_ID_MID(set), slot);
		state = ldms_metric_array_get_u8(set, JOB_STATE_MID(set), slot);
		if (state < JOB_RUNNING) {
			LOG_(OVIS_LINFO,
			       "Ignoring job %d in slot %d in state %d\n",
			       job_id, slot, state);
			goto skip;
		}
		/* Store the new job data */
		switch (verbosity) {
		case SUMMARY:
			store_summary(si, set, slot);
			break;
		case BY_RANK:
			store_ranks(si, set, slot);
			break;
		case BY_TIME:
			store_times(si, set, slot);
			break;
		}
	skip:
		list_idx++;
		if (list_idx >= slot_count)
			list_idx = 0;
	} while (list_idx != list_tail);
	pthread_mutex_unlock(&si->lock);
	return rc;
err:
	pthread_mutex_unlock(&si->lock);
	return errno;
}

static int flush_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	struct sos_instance *si = _sh;
	if (!_sh)
		return EINVAL;
	pthread_mutex_lock(&si->lock);
	if (si->sos_handle)
		sos_container_commit(si->sos_handle->sos, SOS_COMMIT_ASYNC);
	pthread_mutex_unlock(&si->lock);
	return 0;
}

static void close_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	struct sos_instance *si = _sh;

	if (!si)
		return;

	pthread_mutex_lock(&cfg_lock);
	LIST_REMOVE(si, entry);
	pthread_mutex_unlock(&cfg_lock);

	if (si->sos_handle)
		put_container(si->sos_handle);
	if (si->path)
		free(si->path);
	free(si->container);
	free(si);
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
		.term = term,
		.config = config,
		.usage = usage,
		.type = LDMSD_PLUGIN_STORE,
		.constructor = constructor,
		.destructor = destructor,
	},
	.open = open_store,
	.store = store,
	.flush = flush_store,
	.close = close_store,
};

static void __attribute__ ((constructor)) store_slurm_init();
static void store_slurm_init()
{
	pthread_mutex_init(&cfg_lock, NULL);
	LIST_INIT(&sos_handle_list);
}

static void __attribute__ ((destructor)) store_slurm_fini(void);
static void store_slurm_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	/* TODO: clean up container and metric trees */
}
