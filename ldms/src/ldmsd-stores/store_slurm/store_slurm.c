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
 * \file store_slurm.c
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
#include "../../ldmsd-samplers/slurm/slurm_sampler.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

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
typedef struct store_slurm_inst_s *store_slurm_inst_t;
struct store_slurm_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
	int verbosity;
	char *path;
	sos_t sos;
	sos_schema_t sos_schema;
	pthread_mutex_t lock; /**< lock at metric store level */
	sos_attr_t job_rank_time_attr;
	sos_attr_t job_time_rank_attr;
};

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

sos_t create_container(store_slurm_inst_t inst)
{
	ldmsd_store_type_t store = LDMSD_STORE(inst);
	int rc = 0;
	sos_t sos;
	time_t t;
	const char *path = inst->path;
	char part_name[16];	/* Unix timestamp as string */
	sos_part_t part;

	rc = sos_container_new(path, store->perm);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Error %d creating the container at '%s'\n", rc, path);
		goto err_0;
	}
	sos = sos_container_open(path, SOS_PERM_RW);
	if (!sos) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Error %d opening the container at '%s'\n", errno, path);
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
		INST_LOG(inst, LDMSD_LERROR,
			 "Error %d creating the partition '%s' in '%s'\n",
			 rc, part_name, path);
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

static void
__store_slurm_close(store_slurm_inst_t inst)
{
	/* caller must hold inst->lock */
	if (inst->sos_schema) {
		sos_schema_free(inst->sos_schema);
		inst->sos_schema = NULL;
		inst->job_rank_time_attr = NULL;
		inst->job_time_rank_attr = NULL;
	}
	if (inst->sos) {
		sos_container_close(inst->sos, SOS_COMMIT_ASYNC);
		inst->sos = NULL;
	}
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

static int
store_slurm_open(ldmsd_plugin_inst_t pi, ldmsd_strgp_t strgp)
{
	/* Perform `open` operation */
	int rc;
	store_slurm_inst_t inst = (void*)pi;
	size_t sz = sos_schema_template_sz(&slurm_schema_template);
	char buff[sz];
	struct sos_schema_template *tmp = (void*)buff;

	pthread_mutex_lock(&inst->lock);
	inst->sos = sos_container_open(inst->path, SOS_PERM_RW);
	if (inst->sos)
		goto get_schema;
	/* else, create the container */
	inst->sos = create_container(inst);
	if (!inst->sos) {
		rc = errno;
		goto err;
	}

 get_schema:
	inst->sos_schema = sos_schema_by_name(inst->sos, strgp->schema);
	if (inst->sos_schema)
		goto out;
	/* else, create a schema */
	memcpy(tmp, &slurm_schema_template, sz);
	tmp->name = strgp->schema;
	inst->sos_schema = sos_schema_from_template(tmp);
	if (!inst->sos_schema) {
		rc = errno;
		INST_LOG(inst, LDMSD_LERROR,
			 "Error %d creating a new SOS schema.\n", rc);
		goto err;
	}
	rc = sos_schema_add(inst->sos, inst->sos_schema);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Error %d adding the schema to the container.\n", rc);
		goto err;
	}
 out:
	inst->job_rank_time_attr = sos_schema_attr_by_name(inst->sos_schema,
							   "job_rank_time");
	if (!inst->job_rank_time_attr) {
		rc = ENOENT;
		INST_LOG(inst, LDMSD_LERROR,
			 "`job_rank_time` attribute not found in "
			 "'%s' sos schema\n", strgp->schema);
		goto err;
	}
	inst->job_time_rank_attr = sos_schema_attr_by_name(inst->sos_schema,
							   "job_time_rank");
	if (!inst->job_rank_time_attr) {
		rc = ENOENT;
		INST_LOG(inst, LDMSD_LERROR,
			 "`job_time_rank` attribute not found in "
			 "'%s' sos schema\n", strgp->schema);
		goto err;
	}
	pthread_mutex_unlock(&inst->lock);
	return 0;

 err:
	__store_slurm_close(inst);
	pthread_mutex_unlock(&inst->lock);
	return rc;
}

static int
store_slurm_close(ldmsd_plugin_inst_t pi)
{
	/* Perform `close` operation */
	store_slurm_inst_t inst = (void*)pi;
	pthread_mutex_lock(&inst->lock);
	__store_slurm_close(inst);
	pthread_mutex_unlock(&inst->lock);
	return 0;
}

static int
store_slurm_flush(ldmsd_plugin_inst_t pi)
{
	store_slurm_inst_t inst = (void*)pi;
	pthread_mutex_lock(&inst->lock);
	if (inst->sos)
		sos_container_commit(inst->sos, SOS_COMMIT_ASYNC);
	pthread_mutex_unlock(&inst->lock);
	return 0;
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
store_summary(store_slurm_inst_t inst, ldms_set_t set, int slot)
{
	struct sos_value_s v_;
	sos_value_t v;
	int rc;
	sos_obj_t obj;
	SOS_KEY(key);
	sos_key_t k;
	uint64_t job_id;

	job_id = ldms_metric_array_get_u64(set, JOB_ID_MID(set), slot);
	k = sos_key_for_attr(key, inst->job_rank_time_attr,
			     job_id, 0, 0);
	if (!k)
		return errno;

	obj = sos_index_find_sup(sos_attr_index(inst->job_rank_time_attr), key);
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
		obj = sos_obj_new(inst->sos_schema);
		if (!obj) {
			rc = errno;
			INST_LOG(inst, LDMSD_LERROR,
				 "%s[%d]: Error %d allocating '%s' object.\n",
				 __func__, __LINE__, rc,
				 sos_schema_name(inst->sos_schema));
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
store_ranks(store_slurm_inst_t inst, ldms_set_t set, int slot)
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
		k = sos_key_for_attr(key, inst->job_rank_time_attr,
				     job_id, rank, 0);
		if (!k)
			return errno;

		obj = sos_index_find_sup(sos_attr_index(inst->job_rank_time_attr), key);
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
			obj = sos_obj_new(inst->sos_schema);
			if (!obj) {
				rc = errno;
				INST_LOG(inst, LDMSD_LERROR,
					 "%s[%d]: Error %d allocating '%s' "
					 "object.\n", __func__, __LINE__, rc,
					 sos_schema_name(inst->sos_schema));
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
store_times(store_slurm_inst_t inst, ldms_set_t set, int slot)
{
	struct sos_value_s v_;
	sos_value_t v;
	int rc;
	sos_obj_t obj;
	int task;

	for (task = 0; task < ldms_metric_array_get_u32(set, TASK_COUNT_MID(set), slot); task++) {
		obj = sos_obj_new(inst->sos_schema);
		if (!obj) {
			rc = errno;
			INST_LOG(inst, LDMSD_LERROR,
				 "%s[%d]: Error %d allocating '%s' object.\n",
				 __func__, __LINE__, rc,
				 sos_schema_name(inst->sos_schema));
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

static int
store_slurm_store(ldmsd_plugin_inst_t pi, ldms_set_t set, ldmsd_strgp_t strgp)
{
	/* `store` data from `set` into the store */
	store_slurm_inst_t inst = (void*)pi;
	int rc;

	pthread_mutex_lock(&inst->lock);
	if (!inst->sos) {
		rc = EINVAL;
		INST_LOG(inst, LDMSD_LERROR, "store '%s' not opened.\n",
			 inst->path);
		goto err;
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
			INST_LOG(inst, LDMSD_LINFO,
				 "Ignoring job %d in slot %d in state %d\n",
				 job_id, slot, state);
			goto skip;
		}
		/* Store the new job data */
		switch (inst->verbosity) {
		case SUMMARY:
			store_summary(inst, set, slot);
			break;
		case BY_RANK:
			store_ranks(inst, set, slot);
			break;
		case BY_TIME:
			store_times(inst, set, slot);
			break;
		}
	skip:
		list_idx++;
		if (list_idx >= slot_count)
			list_idx = 0;
	} while (list_idx != list_tail);
	pthread_mutex_unlock(&inst->lock);
	return 0;
err:
	pthread_mutex_unlock(&inst->lock);
	return rc;
}

/* ============== Common Plugin APIs ================= */

static const char *
store_slurm_desc(ldmsd_plugin_inst_t pi)
{
	return "store_slurm - store_slurm store plugin";
}

static char *_help = "\
store_slurm configuration synopsis\n\
    config name=INST perm=OCTAL_PERMISSION path=CONTAINER_PATH\n\
                     [verbosity=0|1|2]\n\
\n\
Option descriptions:\n\
    perm        Octal value for store permission (default: 660).\n\
    path        The path to the SOS container.\n\
    verbosity   0 (default) for summary data recording. Each job has exactly\n\
                  one record in the database containing summary information\n\
                  such as job start time and job end time.\n\
                1 for by-rank data recording. Each job has N records, where N\n\
                  is the number of ranks. Each record contains information\n\
                  of the job rank such as rank start time and rank end time.\n\
                2 for by-time data recording. The data recording in this case\n\
                  records all data whenever the data update arrives. So,\n\
                  a rank of a job may appear in multiple records.\n\
";

static const char *
store_slurm_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static int
store_slurm_config(ldmsd_plugin_inst_t pi, json_entity_t json,
		   char *ebuf, int ebufsz)
{
	store_slurm_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;
	int rc;
	json_entity_t jval;

	rc = store->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	jval = json_value_find(json, "path");
	if (!jval) {
		snprintf(ebuf, ebufsz, "missing `path` attribute.\n");
		return EINVAL;
	}
	if (jval->type != JSON_STRING_VALUE) {
		snprintf(ebuf, ebufsz, "`path` attribute is not a string.\n");
		return EINVAL;
	}
	inst->path = strdup(jval->value.str_->str);
	if (!inst->path) {
		snprintf(ebuf, ebufsz, "Out of memory.\n");
		return ENOMEM;
	}

	jval = json_value_find(json, "verbosity");
	if (!jval) {
		inst->verbosity = SUMMARY;
	} else {
		if (jval->type != JSON_STRING_VALUE) {
			snprintf(ebuf, ebufsz, "`verbosity` attribute is not a string.\n");
			return EINVAL;
		}
		inst->verbosity = atoi(jval->value.str_->str);
	}

	return 0;
}

static void
store_slurm_del(ldmsd_plugin_inst_t pi)
{
	store_slurm_inst_t inst = (void*)pi;

	__store_slurm_close(inst);
	if (inst->path)
		free(inst->path);
}

static int
store_slurm_init(ldmsd_plugin_inst_t pi)
{
	store_slurm_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;

	/* override store operations */
	store->open = store_slurm_open;
	store->close = store_slurm_close;
	store->flush = store_slurm_flush;
	store->store = store_slurm_store;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct store_slurm_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = LDMSD_STORE_TYPENAME,
		.plugin_name = "store_slurm",

                /* Common Plugin APIs */
		.desc   = store_slurm_desc,
		.help   = store_slurm_help,
		.init   = store_slurm_init,
		.del    = store_slurm_del,
		.config = store_slurm_config,

	},
	/* plugin-specific data initialization (for new()) here */
	.verbosity = SUMMARY,
};

ldmsd_plugin_inst_t new()
{
	store_slurm_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
