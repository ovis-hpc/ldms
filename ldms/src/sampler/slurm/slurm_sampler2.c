/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2022 Open Grid Computing, Inc. All rights reserved.
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
#include <ctype.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include "coll/rbt.h"
#include "ovis_json/ovis_json.h"
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"

#define SAMP "slurm2"

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof(a)/sizeof(*a))
#endif /* ARRAY_LEN */

#define JOB_USER_STR_LEN 32
#define JOB_NAME_STR_LEN 256
#define JOB_TAG_STR_LEN 256

static ldmsd_msg_log_f msglog;
static struct rbt job_tree;
static pthread_mutex_t job_tree_lock = PTHREAD_MUTEX_INITIALIZER;
struct job_data;

#define WAIT_TIME 5 /* 5 seconds */
static TAILQ_HEAD(job_list, job_data) complete_job_list;

enum job_metric_order {
	JM_FIRST = 0,
	JOB_ID = 0,
	APP_ID,
	USER,
	JOB_NAME,
	JOB_TAG,
	JOB_STATE,
	JOB_SIZE,
	JOB_UID,
	JOB_GID,
	JOB_START,
	JOB_END,
	NODE_COUNT,
	TASK_COUNT,
	JM_LAST,
};

struct ldms_metric_template_s job_rec_metrics[] = {
	{ "job_id"       , 0,   LDMS_V_U64         , ""          , 1 },
	{ "app_id"       , 0,   LDMS_V_U64         , ""          , 1 },
	{ "user"         , 0,   LDMS_V_CHAR_ARRAY  , ""          , JOB_USER_STR_LEN },
	{ "job_name"     , 0,   LDMS_V_CHAR_ARRAY  , ""          , JOB_NAME_STR_LEN },
	{ "job_tag"      , 0,   LDMS_V_CHAR_ARRAY  , ""          , JOB_TAG_STR_LEN },
	{ "job_state"    , 0,   LDMS_V_U8          , ""          , 1 },
	{ "job_size"     , 0,   LDMS_V_U32         , ""          , 1 },
	{ "job_uid"      , 0,   LDMS_V_U32         , ""          , 1 },
	{ "job_gid"      , 0,   LDMS_V_U32         , ""          , 1 },
	{ "job_start"    , 0,   LDMS_V_U32         , "timestamp" , 1 },
	{ "job_end"      , 0,   LDMS_V_U32         , "timestamp" , 1 },
	{ "node_count"   , 0,   LDMS_V_U32         , ""          , 1 },
	{ "task_count"   , 0,   LDMS_V_U32         , ""          , 1 },
	{ 0 }
};
#define JOB_REC_LEN (ARRAY_LEN(job_rec_metrics) - 1)
static int job_metric_ids[JOB_REC_LEN];

enum task_metric_order {
	TM_FIRST = 0,
	TASK_JOB_ID = 0,
	TASK_PID,
	TASK_RANK,
	TASK_EXIT_STATUS,
	TM_LAST,
};

struct ldms_metric_template_s task_rec_metrics[] = {
	{ "job_id"             , 0,   LDMS_V_U64         , ""     , 1 },
	{ "task_pid"           , 0,   LDMS_V_U32         , ""     , 1 },
	{ "task_rank"          , 0,   LDMS_V_U32         , ""     , 1 },
	{ "task_exit_status"   , 0,   LDMS_V_U32         , ""     , 1 },
	{ 0 }
};
#define TASK_REC_LEN (ARRAY_LEN(task_rec_metrics) - 1)
static int task_metric_ids[TASK_REC_LEN];

#define DEFAULT_STREAM_NAME "slurm"
#define SCHEMA_NAME "mt-slurm2"
#define JOB_COUNT 8
#define TASK_PER_JOB_COUNT 8
#define JOB_LIST_MNAME "job_list"
#define TASK_LIST_MNAME "task_list"

typedef struct task_data {
	union ldms_value v[TASK_REC_LEN];
	ldms_mval_t rec_inst;
	TAILQ_ENTRY(task_data) ent;
} *task_data_t;

typedef struct job_data {
	enum slurm_job_state {
		JOB_FREE = 0,
		JOB_STARTING = 1,
		JOB_RUNNING = 2,
		JOB_STOPPING = 3,
		JOB_COMPLETE = 4
	} state;
	char user[JOB_USER_STR_LEN];
	char job_name[JOB_NAME_STR_LEN];
	char job_tag[JOB_TAG_STR_LEN];
	union ldms_value v[JOB_REC_LEN];
	ldms_mval_t rec_inst;
	/* List of tasks that has not been assigned the task_pid. */
	TAILQ_HEAD(task_list, task_data) task_list;
	struct rbn rbn;
	TAILQ_ENTRY(job_data) ent; /* stopped_job_list entry */
} *job_data_t;

static inline void job_id_set(job_data_t job, uint64_t job_id)
{
	job->v[JOB_ID].v_u64 = job_id;
}

static inline uint64_t job_id_get(job_data_t job)
{
	return job->v[JOB_ID].v_u64;
}

static inline uint32_t task_pid_get(task_data_t task)
{
	return task->v[TASK_PID].v_u32;
}

static inline void task_pid_set(task_data_t task, uint64_t task_pid)
{
	task->v[TASK_PID].v_u64 = task_pid;
}

static int tree_node_cmp(void *a, const void *b)
{
	uint64_t a_ = *(uint64_t *)a;
	uint64_t b_ = *(uint64_t *)b;
	if (a_ < b_)
		return -1;
	if (a_ > b_)
		return 1;
	return 0;
}

static inline void task_data_free(task_data_t task)
{
	free(task);
}

static task_data_t task_data_alloc(job_data_t job)
{
	task_data_t task = calloc(1, sizeof(*task));
	if (!task)
		return NULL;
	task->v[TASK_JOB_ID].v_u64 = job_id_get(job);
	TAILQ_INSERT_TAIL(&job->task_list, task, ent);
	return task;
}

static void job_data_free(job_data_t job)
{
	task_data_t task;
	while ((task = TAILQ_FIRST(&job->task_list))) {
		TAILQ_REMOVE(&job->task_list, task, ent);
		task_data_free(task);
	}
	free(job);
}

static job_data_t job_data_alloc(uint64_t job_id)
{
	job_data_t job;

	job = calloc(1, sizeof(*job));
	if (!job)
		return NULL;
	job_id_set(job, job_id);
	rbn_init(&job->rbn, &job->v[JOB_ID].v_u64);
	job->v[JOB_STATE].v_u8 = JOB_STARTING;
	rbt_ins(&job_tree, &job->rbn);
	TAILQ_INIT(&job->task_list);
	return job;
}

static job_data_t job_data_find(uint64_t job_id)
{
	job_data_t job = NULL;
	struct rbn *rbn;
	rbn = rbt_find(&job_tree, &job_id);
	if (rbn)
		job = container_of(rbn, struct job_data, rbn);
	return job;
}

static task_data_t task_data_find(job_data_t job, uint64_t task_pid)
{
	task_data_t task;
	TAILQ_FOREACH(task, &job->task_list, ent) {
		if (task_pid_get(task) == task_pid)
			break;
	}
	return task;
}

static char *stream;
static char *producer_name;
static char *instance_name;
static uint64_t comp_id;
static uid_t uid;
static gid_t gid;
static uint32_t perm;
static int uid_is_set;
static int gid_is_set;
static int perm_is_set;
static int job_list_len;
static int task_list_len; /* Aggregated number of tasks per job */
static int wait_time;

static ldms_set_t set;
static ldms_schema_t schema;
static ldms_record_t job_rec_def;
static ldms_record_t task_rec_def;
static int comp_id_idx;
static int job_rec_def_idx;
static int task_rec_def_idx;
static int job_list_idx;
static int task_list_idx;
static size_t job_rec_size;
static size_t task_rec_size;

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=slurm_sampler producer=<producer_name> instance=<instance_name>\n"
		"         [stream=<stream_name>] [component_id=<component_id>] [perm=<permissions>]\n"
		"         [uid=<user_name>] [gid=<group_name>]\n"
		"         [job_count=<job_count>] [task_count=<task_count>]"
		"         [delete_time=<delete_time>]\n"
		"     producer      A unique name for the host providing the data\n"
		"     instance      A unique name for the metric set\n"
		"     stream        A stream name to subscribe the slurm_sampler2 to. Defaults to '" DEFAULT_STREAM_NAME "'\n"
		"     component_id  A unique number for the component being monitored. Defaults to zero.\n"
		"     uid           The user-id of the set's owner (defaults to geteuid())\n"
		"     gid           The group id of the set's owner (defaults to getegid())\n"
		"     perm          The set's access permissions (defaults to 0777)\n"
		"     job_count     The estimated maximum number of jobs. The actual number can be larger.\n"
		"                   Default to 8\n"
		"     task_count    The estimated maximum number of tasks/job. The actual number can be larger.\n"
		"                   Default to 8\n"
		"     delete_time   The data of completed jobs will persist in the set at least 'delete_time' seconds\n"
		"                   before it gets deleted from the set.\n"
		"                   0 means to delete any completed jobs when\n"
		"                   the plugin receives the data of a new job. Default to 5 seconds.\n"
		"                   -1 means to not delete any jobs. This is for debugging.\n";
}

static int create_metric_set()
{
	int rc;

	/* Create the schema */
	schema = ldms_schema_new(SCHEMA_NAME);
	if (!schema) {
		msglog(LDMSD_LCRITICAL, SAMP "[%d]: Memory allocation error.\n",
									__LINE__);
		return ENOMEM;
	}

	job_rec_def = ldms_record_from_template("job_data", job_rec_metrics,
							    job_metric_ids);
	if (!job_rec_def) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": Failed to create the job "
				"record definition. Error %d.\n", rc);
		goto err;
	}

	task_rec_def = ldms_record_from_template("task_data", task_rec_metrics, task_metric_ids);
	if (!task_rec_def) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": Failed to create the task "
				     "record definition. Error %d\n", rc);
		goto err;
	}

	comp_id_idx = ldms_schema_metric_add(schema, "component_id", LDMS_V_U64);
	if (comp_id_idx < 0) {
		rc = -comp_id_idx;
		msglog(LDMSD_LERROR, SAMP ": Failed to add the component_id "
						  "metric. Error %d.\n", rc);
		goto err;
	}

	job_rec_def_idx = ldms_schema_record_add(schema, job_rec_def);
	if (job_rec_def_idx < 0) {
		rc = -job_rec_def_idx;
		msglog(LDMSD_LERROR, SAMP ": Failed to add the job record "
					     "definition. Error %d\n", rc);
		goto err;
	}

	task_rec_def_idx = ldms_schema_record_add(schema, task_rec_def);
	if (task_rec_def_idx < 0) {
		rc = -task_rec_def_idx;
		msglog(LDMSD_LERROR, SAMP ": Failed to add the task record "
					      "definition. Error %d\n", rc);
		goto err;
	}

	job_rec_size = ldms_record_heap_size_get(job_rec_def);
	task_rec_size = ldms_record_heap_size_get(task_rec_def);

	job_list_idx = ldms_schema_metric_list_add(schema, JOB_LIST_MNAME,
						   NULL, job_list_len * job_rec_size);
	if (job_list_idx < 0) {
		rc = -job_list_idx;
		msglog(LDMSD_LERROR, SAMP ": Failed to add the job record "
					     "definition. Error %d\n", rc);
		goto err;
	}

	task_list_idx = ldms_schema_metric_list_add(schema, TASK_LIST_MNAME,
						    "", task_list_len * task_rec_size);
	if (task_list_idx < 0) {
		rc = -task_list_idx;
		msglog(LDMSD_LERROR, SAMP ": Failed to add the task_list "
						"metric. Error %d.\n", rc);
		goto err;
	}

	/* Create the set */
	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": Failed to create the set. "
						      "Error %d\n", rc);
		goto err;
	}

	if (uid_is_set)
		ldms_set_uid_set(set, uid);
	if (gid_is_set)
		ldms_set_gid_set(set, gid);
	if (perm_is_set)
		ldms_set_perm_set(set, perm);
	ldms_metric_set_u64(set, comp_id_idx, comp_id);
	ldms_set_producer_name_set(set, producer_name);
	ldms_set_publish(set);
	ldmsd_set_register(set, SAMP);
	return 0;
err:
	ldms_schema_delete(schema);
	ldms_record_delete(job_rec_def);
	ldms_record_delete(task_rec_def);
	return rc;
}

static int slurm_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity);
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc;

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	/* stream name */
	value = av_value(avl, "stream");
	if (value)
		stream = strdup(value);
	else
		stream = strdup(DEFAULT_STREAM_NAME);
	if (!stream) {
		msglog(LDMSD_LCRITICAL, SAMP "[%d]: memory allocation error.\n", __LINE__);
		return ENOMEM;
	}
	ldmsd_stream_subscribe(stream, slurm_recv_cb, self);

	/* producer */
	value = av_value(avl, "producer");
	if (!value) {
		msglog(LDMSD_LERROR, SAMP ": 'producer' is required.\n");
		rc = EINVAL;
		goto err;
	}
	producer_name = strdup(value);
	if (!producer_name) {
		msglog(LDMSD_LCRITICAL, SAMP "[%d]: memory allocation error.\n", __LINE__);
		rc = ENOMEM;
		goto err;
	}

	/* component_id */
	value = av_value(avl, "component_id");
	if (value)
		comp_id = (uint64_t)(atoi(value));

	/* uid */
	value = av_value(avl, "uid");
	if (value) {
		if (isalpha(value[0])) {
			/* Try to lookup the user name */
			struct passwd *pwd = getpwnam(value);
			if (!pwd) {
				msglog(LDMSD_LERROR,
				       SAMP ": The specified user '%s' does not exist\n",
				       value);
				rc = EINVAL;
				goto err;
			}
			uid = pwd->pw_uid;
		} else {
			uid = strtol(value, NULL, 0);
		}
		uid_is_set = 1;
	}

	/* gid */
	value = av_value(avl, "gid");
	if (value) {
		if (isalpha(value[0])) {
			/* Try to lookup the group name */
			struct group *grp = getgrnam(value);
			if (!grp) {
				msglog(LDMSD_LERROR,
				       SAMP ": The specified group '%s' does not exist\n",
				       value);
				rc = EINVAL;
				goto err;
			}
			gid = grp->gr_gid;
		} else {
			gid = strtol(value, NULL, 0);
		}
		gid_is_set = 1;
	}

	/* permission */
	value = av_value(avl, "perm");
	if (value) {
		if (value[0] != '0') {
			msglog(LDMSD_LINFO,
			       SAMP ": Warning, the permission bits '%s' are not specified "
			       "as an Octal number.\n",
			       value);
		}
		perm = strtol(value, NULL, 0);
		perm_is_set = 1;
	}

	/* job list's length */
	value = av_value(avl, "job_count");
	if (value)
		job_list_len = atoi(value);
	else
		job_list_len = JOB_COUNT;

	/* Task list's length */
	value = av_value(avl, "task_count");
	if (value)
		task_list_len = atoi(value) * job_list_len;
	else
		task_list_len = TASK_PER_JOB_COUNT * job_list_len;

	value = av_value(avl, "delete_time");
	if (value)
		wait_time = atoi(value);
	else
		wait_time = WAIT_TIME;

	/* instance name */
	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, SAMP ": 'instance' is required.\n");
		rc = EINVAL;
		goto err;
	}
	instance_name = strdup(value);
	if (!instance_name) {
		msglog(LDMSD_LCRITICAL, SAMP "[%d]: memory allocation error.\n", __LINE__);
		rc = ENOMEM;
		goto err;
	}

	rc = create_metric_set();
	if (rc) {
		msglog(LDMSD_LERROR, "slurm-sampler: error %d creating "
		       "the slurm job data metric set\n", rc);
		goto err;
	}

	return 0;
err:
	free(stream);
	free(producer_name);
	free(instance_name);
	/*
	 * Set to NULL to prevent double free in the case that
	 * the plugin is reconfigured and has an error again.
	 */
	stream = producer_name = instance_name = NULL;
	return rc;
}

static void job_metric_set(job_data_t job, int metric_order)
{
	ldms_mval_t v;

	if (!job->rec_inst)
		return;

	if ((metric_order < 0) || (metric_order > JOB_REC_LEN)) {
		assert(0 == "metric_order > JOB_REC_LEN");
		return;
	}

	switch (metric_order) {
	case USER:
		v = ldms_record_metric_get(job->rec_inst, job_metric_ids[metric_order]);
		snprintf(v->a_char, JOB_USER_STR_LEN, "%s", job->user);
		break;
	case JOB_NAME:
		v = ldms_record_metric_get(job->rec_inst, job_metric_ids[metric_order]);
		snprintf(v->a_char, JOB_NAME_STR_LEN, "%s", job->job_name);
		break;
	case JOB_TAG:
		v = ldms_record_metric_get(job->rec_inst, job_metric_ids[metric_order]);
		snprintf(v->a_char, JOB_TAG_STR_LEN, "%s", job->job_tag);
		break;
	default:
		ldms_record_metric_set(job->rec_inst, job_metric_ids[metric_order],
						      &job->v[metric_order]);
		break;
	}
}

static inline void task_metric_set(task_data_t task, int metric_order)
{
	ldms_record_metric_set(task->rec_inst, task_metric_ids[metric_order],
						     &task->v[metric_order]);
}

static void remove_completed_jobs(ldms_mval_t job_list, ldms_mval_t task_list, uint64_t now)
{
	job_data_t job, nxt;
	task_data_t task;

	if (-1 == wait_time) {
		/* Do not remove any jobs */
		return;
	}

	job = TAILQ_FIRST(&complete_job_list);
	while (job) {
		nxt = TAILQ_NEXT(job, ent);
		if (0 == wait_time) {
			if (job->v[JOB_STATE].v_u32 != JOB_COMPLETE)
				goto next;
		} else {
			if (now - job->v[JOB_END].v_u64 <= wait_time) {
				goto next;
			}
		}

		msglog(LDMSD_LDEBUG, SAMP ": Remove job %d\n", job_id_get(job));
		rbt_del(&job_tree, &job->rbn);
		TAILQ_REMOVE(&complete_job_list, job, ent);
		if (job->rec_inst)
			ldms_list_remove_item(set, job_list, job->rec_inst);
		while ((task = TAILQ_FIRST(&job->task_list))) {
			TAILQ_REMOVE(&job->task_list, task, ent);
			if (task->rec_inst)
				ldms_list_remove_item(set, task_list, task->rec_inst);
			task_data_free(task);
		}
		job_data_free(job);
	next:
		job = nxt;
	}
}

static ldms_mval_t __job_rec_alloc(ldms_set_t set)
{
	ldms_mval_t rec = ldms_record_alloc(set, job_rec_def_idx);
	if (!rec)
		return NULL;
	memset(((ldms_record_inst_t)rec)->rec_data, 0,
			ldms_record_value_size_get(job_rec_def));
	return rec;
}

static ldms_mval_t __task_rec_alloc(ldms_set_t set)
{
	ldms_mval_t rec = ldms_record_alloc(set, task_rec_def_idx);
	if (!rec)
		return NULL;
	memset(((ldms_record_inst_t)rec)->rec_data, 0,
			ldms_record_value_size_get(task_rec_def));
	return rec;
}

static int __expand_heap()
{
	msglog(LDMSD_LDEBUG, SAMP ": %s\n", __func__);
	size_t heap_sz = ldms_set_heap_size_get(set);

	ldmsd_set_deregister(instance_name, SAMP);
	ldms_set_unpublish(set);
	ldms_set_delete(set);

	heap_sz += job_rec_size * job_list_len +
			task_rec_size * task_list_len;
	set = ldms_set_new_with_heap(instance_name, schema, heap_sz);
	if (!set) {
		int rc = errno;
		msglog(LDMSD_LERROR, SAMP ": Failed to create set %s with a "
				     "larger heap size %lu. Error %d\n",
				     instance_name, heap_sz, rc);
		return rc;
	}

	ldms_metric_set_u64(set, comp_id_idx, comp_id);
	ldms_set_producer_name_set(set, producer_name);

	/* Set the metric values */
	ldms_mval_t job_list, task_list, rec;
	job_data_t job;
	task_data_t task;
	struct rbn *rbn;
	int i;

	ldms_transaction_begin(set);
	job_list = ldms_metric_get(set, job_list_idx);
	task_list = ldms_metric_get(set, task_list_idx);

	RBT_FOREACH(rbn, &job_tree) {
		job = container_of(rbn, struct job_data, rbn);

		/* Add a job record */
		job->rec_inst = rec = __job_rec_alloc(set);
		for (i = 0; i < JOB_REC_LEN; i++)
			job_metric_set(job, i);
		ldms_list_append_record(set, job_list, rec);

		TAILQ_FOREACH(task, &job->task_list, ent) {
			task->rec_inst = rec = __task_rec_alloc(set);
			for (i = 0; i < TASK_REC_LEN; i++)
				task_metric_set(task, i);
			ldms_list_append_record(set, task_list, rec);
		}
	}
	ldms_transaction_end(set);
	ldmsd_set_register(set, SAMP);
	ldms_set_publish(set);

	return 0;
}

static void handle_job_init(job_data_t job, json_entity_t e)
{
	int rc = 0;
	uint64_t timestamp;
	json_entity_t av, dict;

	msglog(LDMSD_LDEBUG, SAMP ": job %d: Received 'init' event\n",
						   job_id_get(job));

	av = json_value_find(e, "timestamp");
	if (!av) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Missing 'timestamp' attribute "
				     "in 'init' event.\n", job_id_get(job));
		return;
	}
	timestamp = json_value_int(av);

	dict = json_value_find(e, "data");
	if (!dict) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Missing 'data' attribute "
				     "in 'init' event.\n", job_id_get(job));
		return;
	}

	job->v[JOB_START].v_u32 = timestamp;

	/* node count */
	av = json_value_find(dict, "nnodes");
	if (av)
		job->v[NODE_COUNT].v_u32 = json_value_int(av);

	/* uid */
	av = json_value_find(dict, "uid");
	if (av)
		job->v[JOB_UID].v_u32 = json_value_int(av);

	/* gid */
	av = json_value_find(dict, "gid");
	if (av)
		job->v[JOB_GID].v_u32 = json_value_int(av);

	/* job_size */
	av = json_value_find(dict, "total_tasks");
	if (!av) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Missing 'total_tasks' attribute "
				     "in 'init' event.\n", job_id_get(job));
	} else {
		job->v[JOB_SIZE].v_u32 = json_value_int(av);
	}

	/* task count */
	av = json_value_find(dict, "local_tasks");
	if (!av) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Missing 'local_tasks' attribute "
				     "in 'init' event.\n", job_id_get(job));
		return;
	} else {
		job->v[TASK_COUNT].v_u64 = json_value_int(av);
	}

	ldms_mval_t job_rec, job_list, task_list;
	ldms_transaction_begin(set);
	job_list = ldms_metric_get(set, job_list_idx);
	task_list = ldms_metric_get(set, task_list_idx);

	/*
	 * Delete the job records and the corresponding task records
	 * from the job_list and task_list after the jobs
	 * have stopped for at least CLEANUP_WAIT_TIME.
	 */
	remove_completed_jobs(job_list, task_list, timestamp);

	job_rec = __job_rec_alloc(set);
	if (!job_rec) {
		rc = errno;
		if (ENOMEM == rc) {
			goto expand_heap;
		} else {
			msglog(LDMSD_LERROR, SAMP ": job %d: Failed to create a new "
					     "job record. Error %d\n",
					     job_id_get(job), rc);
			goto out;
		}
	}
	job->rec_inst = job_rec;
	ldms_list_append_record(set, job_list, job_rec);
	/* Initialize the record entry */
	job_metric_set(job, JOB_ID);
	job_metric_set(job, JOB_START);
	job_metric_set(job, JOB_END);
	job_metric_set(job, JOB_STATE);
	job_metric_set(job, TASK_COUNT);
	job_metric_set(job, JOB_SIZE);
	job_metric_set(job, JOB_UID);
	job_metric_set(job, JOB_GID);

out:
	ldms_transaction_end(set);
	return;

expand_heap:
	ldms_transaction_end(set);
	(void)__expand_heap();
	return;
}

static void handle_step_init(job_data_t job, json_entity_t e)
{
	json_entity_t av, dict;

	msglog(LDMSD_LDEBUG, SAMP ": job %d: Received 'step_init' event\n", job_id_get(job));

	dict = json_value_find(e, "data");
	if (!dict) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Missing 'data' attribute in "
				     "'step_init' event.\n", job_id_get(job));
		return;
	}

	/* user */
	av = json_value_find(dict, "job_user");
	if (av) {
		if (json_entity_type(av) == JSON_STRING_VALUE) {
			snprintf(job->user, JOB_USER_STR_LEN, "%s",
					json_value_str(av)->str);
		}
	}

	/* job_name */
	av = json_value_find(dict, "job_name");
	if (av) {
		if (json_entity_type(av) == JSON_STRING_VALUE) {
			snprintf(job->job_name, JOB_NAME_STR_LEN, "%s",
					json_value_str(av)->str);
		}
	}

	/* If subscriber data is present, look for an instance tag */
	json_entity_t subs_dict;
	subs_dict = json_value_find(dict, "subscriber_data");
	if (subs_dict) {
		if (json_entity_type(subs_dict) == JSON_DICT_VALUE) {
			av = json_value_find(subs_dict, "job_tag");
			if (av) {
				if (json_entity_type(av) == JSON_STRING_VALUE) {
					snprintf(job->job_tag, JOB_TAG_STR_LEN,
						"%s", json_value_str(av)->str);
				}
			}
		}
	}

	/* node count */
	av = json_value_find(dict, "nnodes");
	if (av)
		job->v[NODE_COUNT].v_u32 = json_value_int(av);

	/* step_id */
	av = json_value_find(dict, "step_id");
	if (av)
		job->v[APP_ID].v_u64 = json_value_int(av);

	/* uid */
	av = json_value_find(dict, "uid");
	if (av)
		job->v[JOB_UID].v_u32 = json_value_int(av);

	/* gid */
	av = json_value_find(dict, "gid");
	if (av)
		job->v[JOB_GID].v_u32 = json_value_int(av);

	/* task count */
	av = json_value_find(dict, "total_tasks");
	if (!av) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Missing 'total_tasks' attribute "
				     "in 'step_init' event.\n", job_id_get(job));
	} else {
		job->v[JOB_SIZE].v_u32 = json_value_int(av);
	}

	if (!job->rec_inst) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Ignore'step_init' data "
				     "because no job record in "
				     "the job list metric.\n",
				     job_id_get(job));
		return;
	}

	/* task count */
	av = json_value_find(dict, "local_tasks");
	if (!av) {
		msglog(LDMSD_LERROR, SAMP ": job %d: task %d: Missing 'local_tasks' "
						"attribute in 'step_init' event.\n",
								  job_id_get(job));
	} else {
		job->v[TASK_COUNT].v_u64 = json_value_int(av);
	}


	ldms_transaction_begin(set);
	job_metric_set(job, USER);
	job_metric_set(job, JOB_NAME);
	job_metric_set(job, JOB_TAG);
	job_metric_set(job, NODE_COUNT);
	job_metric_set(job, JOB_SIZE);
	job_metric_set(job, APP_ID);
	job_metric_set(job, JOB_UID);
	job_metric_set(job, JOB_GID);
	job_metric_set(job, TASK_COUNT);
	ldms_transaction_end(set);
}

static void handle_task_init(job_data_t job, json_entity_t e)
{
	json_entity_t av, dict;
	uint64_t task_pid;
	task_data_t task;

	dict = json_value_find(e, "data");
	if (!dict) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Missing 'data' attribute in "
				    "'task_init_priv' event.\n", job_id_get(job));
		return;
	}

	av = json_value_find(dict, "task_pid");
	if (!av) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Missing 'task_pid' attribute "
				     "in 'task_init_priv' event.\n");
		return;
	}
	task_pid = json_value_int(av);

	msglog(LDMSD_LDEBUG, SAMP ": job %d: task %d: "
			    "Received 'task_init_priv' event\n",
			    job_id_get(job), task_pid);

	task = task_data_alloc(job);
	if (!task) {
		msglog(LDMSD_LCRITICAL, SAMP "[%d]: Memory allocation error.\n",
								     __LINE__);
		return;
	}
	task_pid_set(task, task_pid);

	/* task rank */
	av = json_value_find(dict, "task_global_id");
	if (!av) {
		msglog(LDMSD_LERROR, SAMP ": job %d: task %d: Missing "
				     "'task_global_id' attribute in "
				     "'task_init_priv' event.\n",
				     job_id_get(job), task_pid_get(task));
		return;
	}
	task->v[TASK_RANK].v_u64 = json_value_int(av);
	job->v[JOB_STATE].v_u32 = JOB_RUNNING;

	int i;
	ldms_mval_t task_rec, task_list;
	ldms_transaction_begin(set);
	task_list = ldms_metric_get(set, task_list_idx);
	job_metric_set(job, JOB_STATE);
	task_rec = __task_rec_alloc(set);
	if (!task_rec)
		goto expand_heap;
	ldms_list_append_record(set, task_list, task_rec);
	task->rec_inst = task_rec;
	for (i = 0; i < TASK_REC_LEN; i++)
		task_metric_set(task, i);
	ldms_transaction_end(set);
	return;
expand_heap:
	ldms_transaction_end(set);
	(void)__expand_heap();
	return;
}

static void handle_task_exit(job_data_t job, json_entity_t e)
{
	json_entity_t dict, av;
	uint64_t task_pid;
	task_data_t task;

	dict = json_value_find(e, "data");
	if (!dict) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Missing 'data' attribute in "
				    "'task_exit' event.\n", job_id_get(job));
		return;
	}

	av = json_value_find(dict, "task_pid");
	if (!av) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Missing 'task_pid' attribute "
				     "in 'task_exit' event.\n", job_id_get(job));
		return;
	}
	task_pid = json_value_int(av);
	msglog(LDMSD_LDEBUG, SAMP ": job %d: task %d: Received 'task_exit' event\n",
					     job_id_get(job), task_pid);

	task = task_data_find(job, task_pid);
	if (!task) {
		msglog(LDMSD_LERROR, SAMP ": job %d: task %d: Cannot find "
				     "the task_data in 'task_exit' event.\n", job_id_get(job), task_pid);
		return;
	}

	av = json_value_find(dict, "task_exit_status");
	if (!av) {
		msglog(LDMSD_LERROR, SAMP ": job %d: task %d: Missing "
				     "'task_exit_status' attribute "
				     "in 'task_exit' event.\n", job_id_get(job),
				     task_pid_get(task));
		return;
	}
	task->v[TASK_EXIT_STATUS].v_u64 = json_value_int(av);
	job->v[JOB_STATE].v_u64 = JOB_STOPPING;

	ldms_transaction_begin(set);
	job_metric_set(job, JOB_STATE);
	task_metric_set(task, TASK_EXIT_STATUS);
	ldms_transaction_end(set);
}

static void handle_job_exit(job_data_t job, json_entity_t e)
{
	json_entity_t av;

	msglog(LDMSD_LDEBUG, SAMP ": job %d: Received 'job_exit' event.\n",
							 job_id_get(job));

	av = json_value_find(e, "timestamp");
	if (!av) {
		msglog(LDMSD_LERROR, SAMP ": job %d: Missing 'timestamp' "
				"attribute in 'job_exit' event.\n", job_id_get(job));
		return;
	}
	job->v[JOB_END].v_u64 = json_value_int(av);
	job->v[JOB_STATE].v_u64 = JOB_COMPLETE;

	ldms_transaction_begin(set);
	job_metric_set(job, JOB_STATE);
	job_metric_set(job, JOB_END);
	ldms_transaction_end(set);
}

static int slurm_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity)
{
	int rc = EINVAL;
	json_entity_t event, dict, av;

	if (stream_type != LDMSD_STREAM_JSON) {
		msglog(LDMSD_LDEBUG, SAMP ": Unexpected stream type data...ignoring\n");
		msglog(LDMSD_LDEBUG, SAMP ":" "%s\n", msg);
		return EINVAL;
	}

	event = json_value_find(entity, "event");
	if (!event) {
		msglog(LDMSD_LERROR, SAMP ": 'event' attribute missing\n");
		goto err_0;
	}

	json_str_t event_name = json_value_str(event);
	dict = json_value_find(entity, "data");
	if (!dict) {
		msglog(LDMSD_LERROR, SAMP ": '%s' event is missing "
		       "the 'data' attribute\n", event_name->str);
		goto err_0;
	}
	av = json_value_find(dict, "job_id");
	if (!av) {
		msglog(LDMSD_LERROR, SAMP ": '%s' event is missing the "
		       "'job_id' attribute.\n", event_name->str);
		goto err_0;
	}
	uint64_t job_id = json_value_int(av);

	job_data_t job;
	pthread_mutex_lock(&job_tree_lock);
	if (0 == strncmp(event_name->str, "init", 4)) {
		job = job_data_find(job_id);
		if (!job) {
			job = job_data_alloc(job_id);
			if (!job) {
				msglog(LDMSD_LCRITICAL,
					SAMP ": Memory allocation error when "
					"creating a job data object.\n");
				rc = ENOMEM;
				goto unlock_tree;
			}
		}
		handle_job_init(job, entity);
	} else if (0 == strncmp(event_name->str, "step_init", 9)) {
		job = job_data_find(job_id);
		if (!job) {
			msglog(LDMSD_LERROR, SAMP ": '%s' event was received "
					    "for job %d with no job_data.\n",
					    event_name->str, job_id);
			goto unlock_tree;
		}
		handle_step_init(job, entity);
	} else if (0 == strncmp(event_name->str, "task_init_priv", 14)) {
		job = job_data_find(job_id);
		if (!job) {
			msglog(LDMSD_LERROR, SAMP ": '%s' event was received "
					"for job %d with no job_data.\n",
					event_name->str, job_id);
			goto unlock_tree;
		}
		handle_task_init(job, entity);
	} else if (0 == strncmp(event_name->str, "task_exit", 9)) {
		job = job_data_find(job_id);
		if (!job) {
			msglog(LDMSD_LERROR, SAMP ": '%s' event was received "
					"for job %d with no job_data.\n",
					event_name->str, job_id);
			goto unlock_tree;
		}
		handle_task_exit(job, entity);
	} else if (0 == strncmp(event_name->str, "exit", 4)) {
		job = job_data_find(job_id);
		if (!job) {
			msglog(LDMSD_LERROR, SAMP ": '%s' event was received "
					"for job %d with no job_data.\n",
					event_name->str, job_id);
			goto unlock_tree;
		}
		handle_job_exit(job, entity);
		/*
		 * Add job to the stopped_job_list.
		 * It will be cleanup after it has stopped
		 * for at least CLEANUP_WAIT_TIME.
		 *
		 * The cleanup occurs in handle_job_init().
		 */
		TAILQ_INSERT_TAIL(&complete_job_list, job, ent);
	} else {
		msglog(LDMSD_LDEBUG, SAMP ": ignoring event '%s'\n",
						   event_name->str);
	}
	pthread_mutex_unlock(&job_tree_lock);
	return 0;
unlock_tree:
	pthread_mutex_unlock(&job_tree_lock);
err_0:
	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	if (set) {
		ldmsd_set_deregister(ldms_set_instance_name_get(set), SAMP);
		ldms_set_unpublish(set);
		ldms_set_delete(set);
	}
	set = NULL;
}

static int sample(struct ldmsd_sampler *self)
{
	/* no opt */
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	/* no opt */
	return NULL;
}

static struct ldmsd_sampler slurm2_plugin = {
	.base = {
		.name = SAMP,
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
	set = NULL;
	return &slurm2_plugin.base;
}

static void __attribute__ ((constructor)) slurm_sampler2_init(void)
{
	rbt_init(&job_tree, tree_node_cmp);
	TAILQ_INIT(&complete_job_list);
}

static void __attribute__ ((destructor)) slurm_sampler2_term(void)
{
}
