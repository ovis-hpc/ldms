/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
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
 * \file job.c
 * \brief shared job data provider
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <time.h>
#include <pthread.h>
#include <strings.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <coll/htbl.h>
#include <json/json_util.h>
#include <assert.h>
#include <sched.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_stream.h"
#include "slurm_sampler.h"

static ldmsd_msg_log_f msglog;
static ldms_set_t job_set = NULL;
static ldmsd_msg_log_f msglog;
static ldms_schema_t job_schema;
static char *instance_name;
static char *schema_name;
static char *producer_name;
static char *stream;
static gid_t gid;
static uid_t uid;
static uint32_t perm;
static uint64_t comp_id;

#define PID_LIST_LEN 64
#define JOB_LIST_LEN 8
static int job_list_len = JOB_LIST_LEN;		/* The size of the job list in job_set */
static int job_slot;				/* The slot to be used by the next call to get_job */
static int task_list_len = -1;			/* The size of the task list (i.e. max pids per job) */

typedef struct job_data {
	uint64_t job_id;
	enum slurm_job_state job_state;

	int job_slot;		/* this job's slot in the metric set */
	int local_task_count;	/* local tasks in this job */
	int task_init_count;	/* task_init events processed */

	struct rbn job_ent;
	TAILQ_ENTRY(job_data) slot_ent;
} *job_data_t;

pthread_mutex_t job_lock = PTHREAD_MUTEX_INITIALIZER;
struct rbt job_tree;		/* indexed by job_id */
TAILQ_HEAD(slot_list, job_data) free_slot_list; /* list of available slots */

struct action_s;
typedef struct action_s *action_t;
typedef int (*act_process_fn_t)(ldms_set_t, action_t action, json_entity_t e);
struct action_s {
	act_process_fn_t act_fn;
	ldms_schema_t schema;
	char *name;
	int midx;
	enum ldms_value_type mtype;

	struct hent ent;
};

/*
 * Find the job_data record with the specified job_id
 */
static job_data_t get_job_data(uint64_t job_id)
{
	job_data_t jd = NULL;
	struct rbn *rbn;
	rbn = rbt_find(&job_tree, &job_id);
	if (rbn)
		jd = container_of(rbn, struct job_data, job_ent);
	return jd;
}

/*
 * Allocate a job_data slot for the specified job_id.
 *
 * The slot table is consulted for the next available slot.
 */
static job_data_t alloc_job_data(uint64_t job_id, int local_task_count)
{
	job_data_t jd;

	jd = TAILQ_FIRST(&free_slot_list);
	if (jd) {
		TAILQ_REMOVE(&free_slot_list, jd, slot_ent);
		jd->job_id = job_id;
		jd->job_state = JOB_STARTING;
		jd->local_task_count = local_task_count;
		jd->task_init_count = 0;
		rbn_init(&jd->job_ent, &jd->job_id);
		rbt_ins(&job_tree, &jd->job_ent);
	}
	return jd;
}

static void release_job_data(job_data_t jd)
{
	jd->job_state = JOB_FREE;
	rbt_del(&job_tree, &jd->job_ent);
	TAILQ_INSERT_TAIL(&free_slot_list, jd, slot_ent);
}

static int cur_idx;
static int comp_id_idx;
static int job_id_idx;
static int app_id_idx;
static int job_state_idx;
static int job_tstamp_idx;
static int job_start_idx;
static int job_end_idx;
static int job_uid_idx;
static int job_gid_idx;
static int job_size_idx;
static int node_count_idx;
static int task_count_idx;
static int task_pid_idx;
static int task_rank_idx;
static int task_exit_status_idx;

/* MT (Mutli-Tenant) Schema
 *                        +-+-+...+-+
 * cur_idx                | | |   | |
 *                        +-+-+...+-+
 * comp_id_idx            | | |   | |
 *                        +-+-+...+-+
 * app_id_idx             | | |   | |
 *                        +-+-+...+-+
 * job_id_idx             | | |   | |
 *                        +-+-+...+-+
 * job_tstamp_idx         | | |   | |
 *                        +-+-+...+-+
 * job_state_idx          | | |   | |
 *                        +-+-+...+-+
 * job_size_idx           | | |   | |
 *                        +-+-+...+-+
 * job_uid_idx            | | |   | |
 *                        +-+-+...+-+
 * job_gid_idx            | | |   | |
 *                        +-+-+...+-+
 * job_start_idx          | | |   | |
 *                        +-+-+...+-+
 * job_end_idx            | | |   | |
 *                        +-+-+...+-+
 * node_count_idx         | | |   | |
 *                        +-+-+...+-+
 * task_count_idx         | | |   | |
 *                        +-+-+...+-+
 *
 *                        +-+-+-+-+-+...+-+
 * task_pid_idx           | | | | | |   | |
 *                        +-+-+-+-+-+...+-+
 * task_rank_idx          | | | | | |   | |
 *                        +-+-+-+-+-+...+-+
 * task_exit_status_idx   | | | | | |   | |
 *                        +-+-+-+-+-+...+-+
 */
static int create_metric_set(void)
{
	int rc;
	int i;

	if (!instance_name) {
		msglog(LDMSD_LERROR, "slurm_sampler: The sampler has not been configured.\n");
		rc = EINVAL;
		goto err;
	}

	job_schema = ldms_schema_new(schema_name);
	if (job_schema == NULL) {
		rc = ENOMEM;
		goto err;
	}

	/* component_id */
	comp_id_idx = ldms_schema_metric_array_add(job_schema, "component_id",
						   LDMS_V_U64_ARRAY, job_list_len);
	if (comp_id_idx < 0)
		goto err;
	/* job_id */
	job_id_idx = ldms_schema_metric_array_add(job_schema, "job_id",
						  LDMS_V_U64_ARRAY, job_list_len);
	if (job_id_idx < 0)
		goto err;
	/* app_id */
	app_id_idx = ldms_schema_metric_array_add(job_schema, "app_id",
						  LDMS_V_U64_ARRAY, job_list_len);
	if (app_id_idx < 0)
		goto err;
	/* cur_idx */
	cur_idx = ldms_schema_metric_add(job_schema, "current_slot", LDMS_V_U32);
	if (cur_idx < 0)
		goto err;
	/* job_state */
	job_state_idx =
		ldms_schema_metric_array_add(job_schema, "job_state",
					     LDMS_V_U8_ARRAY,
					     job_list_len);
	if (job_state_idx < 0)
		goto err;
	/* job_stamp */
	job_tstamp_idx = ldms_schema_metric_array_add(job_schema, "job_tstamp",
						  LDMS_V_U32_ARRAY, job_list_len);
	if (job_tstamp_idx < 0)
		goto err;
	/* job_size */
	job_size_idx =
		ldms_schema_metric_array_add(job_schema, "job_size",
					     LDMS_V_U32_ARRAY, job_list_len);
	if (job_size_idx < 0)
		goto err;
	/* job_uid */
	job_uid_idx =
		ldms_schema_metric_array_add(job_schema, "job_uid",
					     LDMS_V_U32_ARRAY, job_list_len);
	if (job_uid_idx < 0)
		goto err;
	/* job_gid */
	job_gid_idx =
		ldms_schema_metric_array_add(job_schema, "job_gid",
					     LDMS_V_U32_ARRAY, job_list_len);
	if (job_gid_idx < 0)
		goto err;
	/* job_start */
	job_start_idx =
		ldms_schema_metric_array_add(job_schema, "job_start",
					     LDMS_V_U32_ARRAY, job_list_len);
	if (job_start_idx < 0)
		goto err;
	/* job_end */
	job_end_idx =
		ldms_schema_metric_array_add(job_schema, "job_end",
					     LDMS_V_U32_ARRAY, job_list_len);
	if (job_end_idx < 0)
		goto err;
	/* node_count */
	node_count_idx =
		ldms_schema_metric_array_add(job_schema, "node_count",
					     LDMS_V_U32_ARRAY, job_list_len);
	if (node_count_idx < 0)
		goto err;
	/* task_count */
	task_count_idx =
		ldms_schema_metric_array_add(job_schema, "task_count",
					     LDMS_V_U32_ARRAY, job_list_len);
	if (task_count_idx < 0)
		goto err;

	/* task_pid */
	task_pid_idx = task_count_idx + 1;
	for (i = 0; i < job_list_len; i++) {
		char metric_name[80];
		sprintf(metric_name, "task_pid_%d", i);
		rc = ldms_schema_metric_array_add(job_schema, metric_name,
						  LDMS_V_U32_ARRAY,
						  task_list_len);
		if (rc < 0)
			goto err;
	}

	/* task_rank */
	task_rank_idx = rc + 1;
	for (i = 0; i < job_list_len; i++) {
		char metric_name[80];
		sprintf(metric_name, "task_rank_%d", i);
		rc = ldms_schema_metric_array_add(job_schema, metric_name,
						  LDMS_V_U32_ARRAY,
						  task_list_len);
		if (rc < 0)
			goto err;
	}

	/* task_exit_status */
	task_exit_status_idx = rc + 1;
	for (i = 0; i < job_list_len; i++) {
		char metric_name[80];
		sprintf(metric_name, "task_exit_status_%d", i);
		rc = ldms_schema_metric_array_add(job_schema, metric_name,
						  LDMS_V_U32_ARRAY,
						  task_list_len);
		if (rc < 0)
			goto err;
	}

	job_set = ldms_set_new_with_auth(instance_name, job_schema,
					 uid, gid, perm);
	if (!job_set) {
		rc = errno;
		goto err;
	}
	ldms_set_producer_name_set(job_set, producer_name);
	for (i = 0; i < job_list_len; i++)
		ldms_metric_array_set_u64(job_set, comp_id_idx, i, comp_id);
	ldms_set_publish(job_set);
	return 0;
 err:
	if (job_schema)
		ldms_schema_delete(job_schema);
	job_schema = NULL;
	return rc;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=kokkos_store path=<path> port=<port_no> log=<path>\n"
		"     path      The path to the root of the SOS container store.\n"
		"     port      The port number to listen on for incoming connections (defaults to 18080).\n"
		"     log       The log file for sample updates (defaults to /var/log/kokkos.log).\n";
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static int sample(struct ldmsd_sampler *self)
{
	return 0;
}

static int slurm_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity);
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc;

	if (job_set) {
		msglog(LDMSD_LERROR, "slurm_sampler: Set already created.\n");
		return EINVAL;
	}

	value = av_value(avl, "stream");
	if (value)
		stream = strdup(value);
	else
		stream = strdup("slurm");
	ldmsd_stream_subscribe(stream, slurm_recv_cb, self);

	value = av_value(avl, "producer");
	if (!value) {
		msglog(LDMSD_LERROR, "slurm_sampler: missing producer.\n");
		return ENOENT;
	}
	producer_name = strdup(value);
	if (!producer_name) {
		msglog(LDMSD_LERROR, "slurm_sampler[%d]: memory allocation error.\n", __LINE__);
		return ENOMEM;
	}

	value = av_value(avl, "component_id");
	if (value)
		comp_id = (uint64_t)(atoi(value));
	else
		comp_id = 0;

	/* uid, gid, permission */
	value = av_value(avl, "uid");
	if (value) {
		if (isalpha(value[0])) {
			/* Try to lookup the user name */
			struct passwd *pwd = getpwnam(value);
			if (!pwd) {
				msglog(LDMSD_LERROR,
				       "slurm_sampler: The specified user '%s' does not exist\n",
				       value);
				return EINVAL;
			}
			uid = pwd->pw_uid;
		} else {
			uid = strtol(value, NULL, 0);
		}
	} else {
		uid = geteuid();
	}
	value = av_value(avl, "gid");
	if (value) {
		if (isalpha(value[0])) {
			/* Try to lookup the group name */
			struct group *grp = getgrnam(value);
			if (!grp) {
				msglog(LDMSD_LERROR,
				       "slurm_sampler: The specified group '%s' does not exist\n",
				       value);
				return EINVAL;
			}
			gid = grp->gr_gid;
		} else {
			gid = strtol(value, NULL, 0);
		}
	} else {
		gid = getegid();
	}
	value = av_value(avl, "perm");
	if (value && value[0] != '0') {
		msglog(LDMSD_LINFO,
		    "slurm_sampler: Warning, the permission bits '%s' are not specified "
		    "as an Octal number.\n",
		    value);
	}
	perm = (value)?(strtol(value, NULL, 0)):(0777);

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, "slurm_sampler: missing instance.\n");
		return ENOENT;
	}
	instance_name = strdup(value);
	if (!instance_name) {
		msglog(LDMSD_LERROR, "slurm_sampler[%d]: memory allocation error.\n", __LINE__);
		return ENOMEM;
	}

	value = av_value(avl, "job_count");
	if (value)
		job_list_len = atoi(value);
	int i;
	for (i = 0; i < job_list_len; i++) {
		job_data_t job = malloc(sizeof *job);
		if (!job) {
			msglog(LDMSD_LERROR, "slurm_sapler[%d]: memory "
			       "allocation failure.\n", __LINE__);
			goto err;
		}
		job->job_slot = i;
		job->job_state = JOB_FREE;
		TAILQ_INSERT_TAIL(&free_slot_list, job, slot_ent);
	}

	value = av_value(avl, "task_count");
	if (value)
		task_list_len = atoi(value);
	if (task_list_len < 0) {
		cpu_set_t cpu_set;
		rc = sched_getaffinity(getpid(), sizeof(cpu_set), &cpu_set);
		if (rc == 0) {
			task_list_len = CPU_COUNT(&cpu_set);
		} else {
			task_list_len = PID_LIST_LEN;
		}
	}

	schema_name = "mt-slurm";

	rc = create_metric_set();
	if (rc) {
		msglog(LDMSD_LERROR, "slurm-sampler: error %d creating "
		       "the slurm job data metric set\n", rc);
	}

	return rc;
 err:
	return rc;
}

static void handle_job_init(job_data_t job, json_entity_t e)
{
	int task;
	int int_v;
	uint64_t timestamp;
	json_entity_t attr, data, dict;

	attr = json_attr_find(e, "timestamp");
	if (!attr) {
		msglog(LDMSD_LERROR, "slurm_sampler: Missing 'timestamp' attribute "
		       "in 'init' event.\n");
		return;
	}
	timestamp = json_value_int(json_attr_value(attr));

	data = json_attr_find(e, "data");
	if (!data) {
		msglog(LDMSD_LERROR, "slurm_sampler: Missing 'data' attribute "
		       "in 'init' event.\n");
		return;
	}
	dict = json_attr_value(data);

	ldms_metric_set_u32(job_set, cur_idx, job->job_slot);
	ldms_metric_array_set_u64(job_set, job_id_idx, job->job_slot, job->job_id);
	ldms_metric_array_set_u8(job_set, job_state_idx, job->job_slot, JOB_STARTING);
	ldms_metric_array_set_u32(job_set, job_start_idx, job->job_slot, timestamp);
	ldms_metric_array_set_u32(job_set, job_end_idx, job->job_slot, 0);

	attr = json_attr_find(dict, "nnodes");
	if (attr) {
		int_v = json_value_int(json_attr_value(attr));
		ldms_metric_array_set_u32(job_set, node_count_idx, job->job_slot, int_v);
	}

	attr = json_attr_find(dict, "local_tasks");
	if (attr) {
		int_v = json_value_int(json_attr_value(attr));
		ldms_metric_array_set_u32(job_set, task_count_idx, job->job_slot, int_v);
	}

	attr = json_attr_find(dict, "uid");
	if (attr) {
		int_v = json_value_int(json_attr_value(attr));
		ldms_metric_array_set_u32(job_set, job_uid_idx, job->job_slot, int_v);
	}

	attr = json_attr_find(dict, "gid");
	if (attr) {
		int_v = json_value_int(json_attr_value(attr));
		ldms_metric_array_set_u32(job_set, job_gid_idx, job->job_slot, int_v);
	}

	attr = json_attr_find(dict, "total_tasks");
	if (!attr) {
		msglog(LDMSD_LERROR, "slurm_sampler: Missing 'total_tasks' attribute "
		       "in 'init' event.\n");
		return;
	}
	int_v = json_value_int(json_attr_value(attr));
	ldms_metric_array_set_u32(job_set, job_size_idx, job->job_slot, int_v);

	int i;
	for (i = 0; i < task_list_len; i++) {
		ldms_metric_array_set_u32(job_set, task_pid_idx + job->job_slot, i, 0);
		ldms_metric_array_set_u32(job_set, task_rank_idx + job->job_slot, i, 0);
		ldms_metric_array_set_u32(job_set, task_exit_status_idx + job->job_slot, i, 0);
	}
}

static void handle_task_init(job_data_t job, json_entity_t e)
{
	json_entity_t attr;
	json_entity_t data, dict;
	int task_id;
	int int_v;

	data = json_attr_find(e, "data");
	if (!data) {
		msglog(LDMSD_LERROR, "slurm_sampler: Missing 'data' attribute "
		       "in 'task_init' event.\n");
		return;
	}
	dict = json_attr_value(data);

	attr = json_attr_find(dict, "task_id");
	if (!attr) {
		msglog(LDMSD_LERROR, "slurm_sampler: Missing 'task_id' attribute "
		       "in 'task_init' event.\n");
		return;
	}
	task_id = json_value_int(json_attr_value(attr));

	attr = json_attr_find(dict, "task_pid");
	if (!attr) {
		msglog(LDMSD_LERROR, "slurm_sampler: Missing 'task_pid' attribute "
		       "in 'task_init' event.\n");
		return;
	}
	int_v = json_value_int(json_attr_value(attr));
	ldms_metric_array_set_u32(job_set, task_pid_idx + job->job_slot, task_id, int_v);

	attr = json_attr_find(dict, "task_global_id");
	if (!attr) {
		msglog(LDMSD_LERROR, "slurm_sampler: Missing 'task_global_id' attribute "
		       "in 'task_init' event.\n");
		return;
	}
	int_v = json_value_int(json_attr_value(attr));
	ldms_metric_array_set_u32(job_set, task_rank_idx + job->job_slot, task_id, int_v);

	job->task_init_count += 1;
	if (job->task_init_count == job->local_task_count)
		ldms_metric_array_set_u8(job_set, job_state_idx, job->job_slot, JOB_RUNNING);
}

static void handle_task_exit(job_data_t job, json_entity_t e)
{
	json_entity_t attr;
	json_entity_t data = json_attr_find(e, "data");
	json_entity_t dict = json_attr_value(data);
	int task_id;
	int int_v;

	ldms_metric_array_set_u8(job_set, job_state_idx, job->job_slot, JOB_STOPPING);

	attr = json_attr_find(dict, "task_id");
	task_id = json_value_int(json_attr_value(attr));

	attr = json_attr_find(dict, "task_exit_status");
	int_v = json_value_int(json_attr_value(attr));
	ldms_metric_array_set_u32(job_set, task_exit_status_idx + job->job_slot, task_id, int_v);

	job->task_init_count -= 1;
}

static void handle_job_exit(job_data_t job, json_entity_t e)
{
	json_entity_t attr = json_attr_find(e, "timestamp");
	uint64_t timestamp = json_value_int(json_attr_value(attr));

	ldms_metric_array_set_u32(job_set, job_end_idx, job->job_slot, timestamp);
	ldms_metric_array_set_u8(job_set, job_state_idx, job->job_slot, JOB_COMPLETE);
}

static int slurm_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity)
{
	int rc;
	json_entity_t event, data, dict, attr;
	uint64_t tstamp;

	if (stream_type != LDMSD_STREAM_JSON) {
		msglog(LDMSD_LDEBUG, "slurm_sampler: Unexpected stream type data...ignoring\n");
		msglog(LDMSD_LDEBUG, "slurm_sampler:" "%s\n", msg);
		return EINVAL;
	}

	event = json_attr_find(entity, "event");
	if (!event) {
		msglog(LDMSD_LERROR, "slurm_sampler: 'event' attribute missing\n");
		goto out_0;
	}

	attr = json_attr_find(entity, "timestamp");
	if (!attr) {
		msglog(LDMSD_LERROR, "slurm_sampler: 'timestamp' attribute missing\n");
		goto out_0;
	}
	tstamp = json_value_int(json_attr_value(attr));

	json_str_t event_name = json_value_str(json_attr_value(event));
	data = json_attr_find(entity, "data");
	if (!data) {
		msglog(LDMSD_LERROR, "slurm_sampler: '%s' event is missing "
		       "the 'data' attribute\n", event_name->str);
		goto out_0;
	}
	dict = json_attr_value(data);
	attr = json_attr_find(dict, "job_id");
	if (!attr) {
		msglog(LDMSD_LERROR, "slurm_sampler: The event is missing the "
		       "'job_id' attribute.\n");
		goto out_0;
	}

	uint64_t job_id = json_value_int(json_attr_value(attr));
	job_data_t job;

	pthread_mutex_lock(&job_lock);
	ldms_transaction_begin(job_set);
	if (0 == strncmp(event_name->str, "init", 4)) {
		job = get_job_data(job_id); /* protect against duplicate entries */
		if (!job) {
			uint64_t local_task_count;
			attr = json_attr_find(dict, "local_tasks");
			if (!attr) {
				msglog(LDMSD_LERROR, "slurm_sampler: '%s' event "
				       "is missing the 'local_tasks'.\n", event_name->str);
				goto out_1;
			}
			/* Allocate the job_data used to track the job */
			local_task_count = json_value_int(json_attr_value(attr));
			job = alloc_job_data(job_id, local_task_count);
			if (!job) {
				msglog(LDMSD_LERROR,
				       "slurm_sampler[%d]: Memory allocation failure.\n",
				       __LINE__);
				goto out_1;
			}
			handle_job_init(job, entity);
		}
	} else if (0 == strncmp(event_name->str, "task_init_priv", 14)) {
		job = get_job_data(job_id);
		if (!job) {
			msglog(LDMSD_LERROR, "slurm_sampler: '%s' event "
			       "was received for job %d with no job_data\n",
			       event_name->str, job_id);
			goto out_1;
		}
		handle_task_init(job, entity);
	} else if (0 == strncmp(event_name->str, "task_exit", 9)) {
		job = get_job_data(job_id);
		if (!job) {
			msglog(LDMSD_LERROR, "slurm_sampler: '%s' event "
			       "was received for job %d with no job_data\n",
			       event_name->str, job_id);
			goto out_1;
		}
		handle_task_exit(job, entity);
	} else if (0 == strncmp(event_name->str, "exit", 4)) {
		job = get_job_data(job_id);
		if (!job) {
			msglog(LDMSD_LERROR, "slurm_sampler: '%s' event "
			       "was received for job %d with no job_data\n",
			       event_name->str, job_id);
			goto out_1;
		}
		handle_job_exit(job, entity);
		release_job_data(job);
	} else {
		msglog(LDMSD_LDEBUG,
		       "slurm_sampler: ignoring event '%s'\n", event_name->str);
	}
 out_1:
	ldms_transaction_end(job_set);
	pthread_mutex_unlock(&job_lock);
 out_0:
	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	if (job_schema)
		ldms_schema_delete(job_schema);
	job_schema = NULL;
	if (job_set)
		ldms_set_delete(job_set);
	job_set = NULL;
}

static struct ldmsd_sampler slurm_sampler = {
	.base = {
		.name = "slurm_sampler",
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &slurm_sampler.base;
}

static int cmp_job_id(void *a, const void *b)
{
	uint64_t a_ = *(uint64_t *)a;
	uint64_t b_ = *(uint64_t *)b;
	if (a_ < b_)
		return -1;	if (a_ > b_)
		return 1;
	return 0;
}

static void __attribute__ ((constructor)) slurm_sampler_init(void)
{
	rbt_init(&job_tree, cmp_job_id);
	TAILQ_INIT(&free_slot_list);
}

static void __attribute__ ((destructor)) slurm_sampler_term(void)
{
}
