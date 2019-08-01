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
 * \file slurm_sampler.c
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
#include "ldmsd_sampler.h"
#include "ldmsd_stream.h"
#include "slurm_sampler.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

#define PID_LIST_LEN 64
#define JOB_LIST_LEN 8

typedef struct slurm_sampler_inst_s *slurm_sampler_inst_t;
struct slurm_sampler_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
	ldms_set_t job_set;
	char *stream;
	int job_list_len;  /* The size of the job list in job_set */
	int job_slot;      /* The slot to be used by the next call to get_job */
	int task_list_len; /* The size of the task list (i.e. max pids per job) */

	pthread_mutex_t job_lock;
	struct rbt job_tree; /* indexed by job_id */
	TAILQ_HEAD(slot_list, job_data) free_slot_list; /* list of available slots */

	int cur_idx;
	int comp_id_idx;
	int job_id_idx;
	int app_id_idx;
	int job_state_idx;
	int job_start_idx;
	int job_end_idx;
	int job_uid_idx;
	int job_gid_idx;
	int job_size_idx;
	int node_count_idx;
	int task_count_idx;
	int task_pid_idx;
	int task_rank_idx;
	int task_exit_status_idx;
};

typedef struct job_data {
	uint64_t job_id;
	enum slurm_job_state job_state;

	int job_slot;		/* this job's slot in the metric set */
	int local_task_count;	/* local tasks in this job */
	int task_init_count;	/* task_init events processed */

	struct rbn job_ent;
	TAILQ_ENTRY(job_data) slot_ent;
} *job_data_t;

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

/* ====== Local functions ====== */

/*
 * Find the job_data record with the specified job_id
 */
static job_data_t get_job_data(slurm_sampler_inst_t inst, uint64_t job_id)
{
	job_data_t jd = NULL;
	struct rbn *rbn;
	rbn = rbt_find(&inst->job_tree, &job_id);
	if (rbn)
		jd = container_of(rbn, struct job_data, job_ent);
	return jd;
}

/*
 * Allocate a job_data slot for the specified job_id.
 *
 * The slot table is consulted for the next available slot.
 */
static job_data_t alloc_job_data(slurm_sampler_inst_t inst,
				 uint64_t job_id, int local_task_count)
{
	job_data_t jd;

	jd = TAILQ_FIRST(&inst->free_slot_list);
	if (jd) {
		TAILQ_REMOVE(&inst->free_slot_list, jd, slot_ent);
		jd->job_id = job_id;
		jd->job_state = JOB_STARTING;
		jd->local_task_count = local_task_count;
		jd->task_init_count = 0;
		rbn_init(&jd->job_ent, &jd->job_id);
		rbt_ins(&inst->job_tree, &jd->job_ent);
	}
	return jd;
}

static void release_job_data(slurm_sampler_inst_t inst, job_data_t jd)
{
	jd->job_state = JOB_FREE;
	rbt_del(&inst->job_tree, &jd->job_ent);
	TAILQ_INSERT_TAIL(&inst->free_slot_list, jd, slot_ent);
}

static void handle_job_init(slurm_sampler_inst_t inst,
			    job_data_t job, json_entity_t e)
{
	int int_v;
	uint64_t timestamp;
	json_entity_t attr, data, dict;

	attr = json_attr_find(e, "timestamp");
	if (!attr) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'timestamp' attribute in 'init' event.\n");
		return;
	}
	timestamp = json_value_int(json_attr_value(attr));

	data = json_attr_find(e, "data");
	if (!data) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'data' attribute in 'init' event.\n");
		return;
	}
	dict = json_attr_value(data);

	ldms_metric_set_u32(inst->job_set, inst->cur_idx, job->job_slot);
	ldms_metric_array_set_u64(inst->job_set, inst->job_id_idx, job->job_slot, job->job_id);
	ldms_metric_array_set_u8(inst->job_set, inst->job_state_idx, job->job_slot, JOB_STARTING);
	ldms_metric_array_set_u32(inst->job_set, inst->job_start_idx, job->job_slot, timestamp);
	ldms_metric_array_set_u32(inst->job_set, inst->job_end_idx, job->job_slot, 0);

	attr = json_attr_find(dict, "nnodes");
	if (attr) {
		int_v = json_value_int(json_attr_value(attr));
		ldms_metric_array_set_u32(inst->job_set, inst->node_count_idx, job->job_slot, int_v);
	}

	attr = json_attr_find(dict, "local_tasks");
	if (attr) {
		int_v = json_value_int(json_attr_value(attr));
		ldms_metric_array_set_u32(inst->job_set, inst->task_count_idx, job->job_slot, int_v);
	}

	attr = json_attr_find(dict, "uid");
	if (attr) {
		int_v = json_value_int(json_attr_value(attr));
		ldms_metric_array_set_u32(inst->job_set, inst->job_uid_idx, job->job_slot, int_v);
	}

	attr = json_attr_find(dict, "gid");
	if (attr) {
		int_v = json_value_int(json_attr_value(attr));
		ldms_metric_array_set_u32(inst->job_set, inst->job_gid_idx, job->job_slot, int_v);
	}

	attr = json_attr_find(dict, "total_tasks");
	if (!attr) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'total_tasks' attribute in 'init' event.\n");
		return;
	}
	int_v = json_value_int(json_attr_value(attr));
	ldms_metric_array_set_u32(inst->job_set, inst->job_size_idx, job->job_slot, int_v);

	int i;
	for (i = 0; i < inst->task_list_len; i++) {
		ldms_metric_array_set_u32(inst->job_set, inst->task_pid_idx + job->job_slot, i, 0);
		ldms_metric_array_set_u32(inst->job_set, inst->task_rank_idx + job->job_slot, i, 0);
		ldms_metric_array_set_u32(inst->job_set, inst->task_exit_status_idx + job->job_slot, i, 0);
	}
}

static void
handle_task_init(slurm_sampler_inst_t inst, job_data_t job, json_entity_t e)
{
	json_entity_t attr;
	json_entity_t data, dict;
	int task_id;
	int int_v;

	data = json_attr_find(e, "data");
	if (!data) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'data' attribute in 'task_init' event.\n");
		return;
	}
	dict = json_attr_value(data);

	attr = json_attr_find(dict, "task_id");
	if (!attr) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'task_id' attribute in 'task_init' event.\n");
		return;
	}
	task_id = json_value_int(json_attr_value(attr));

	attr = json_attr_find(dict, "task_pid");
	if (!attr) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'task_pid' attribute in "
			 "'task_init' event.\n");
		return;
	}
	int_v = json_value_int(json_attr_value(attr));
	ldms_metric_array_set_u32(inst->job_set,
			inst->task_pid_idx + job->job_slot, task_id, int_v);

	attr = json_attr_find(dict, "task_global_id");
	if (!attr) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'task_global_id' attribute in "
			 "'task_init' event.\n");
		return;
	}
	int_v = json_value_int(json_attr_value(attr));
	ldms_metric_array_set_u32(inst->job_set,
			inst->task_rank_idx + job->job_slot, task_id, int_v);

	job->task_init_count += 1;
	if (job->task_init_count == job->local_task_count)
		ldms_metric_array_set_u8(inst->job_set,
				inst->job_state_idx, job->job_slot,
				JOB_RUNNING);
}

static void
handle_task_exit(slurm_sampler_inst_t inst, job_data_t job, json_entity_t e)
{
	json_entity_t attr;
	json_entity_t data = json_attr_find(e, "data");
	json_entity_t dict = json_attr_value(data);
	int task_id;
	int int_v;

	ldms_metric_array_set_u8(inst->job_set, inst->job_state_idx,
				 job->job_slot, JOB_STOPPING);

	attr = json_attr_find(dict, "task_id");
	task_id = json_value_int(json_attr_value(attr));

	attr = json_attr_find(dict, "task_exit_status");
	int_v = json_value_int(json_attr_value(attr));
	ldms_metric_array_set_u32(inst->job_set,
			inst->task_exit_status_idx + job->job_slot,
			task_id, int_v);

	job->task_init_count -= 1;
}

static void handle_job_exit(slurm_sampler_inst_t inst,
			    job_data_t job, json_entity_t e)
{
	json_entity_t attr = json_attr_find(e, "timestamp");
	uint64_t timestamp = json_value_int(json_attr_value(attr));

	ldms_metric_array_set_u32(inst->job_set, inst->job_end_idx,
				  job->job_slot, timestamp);
	ldms_metric_array_set_u8(inst->job_set, inst->job_state_idx,
				 job->job_slot, JOB_COMPLETE);
}

static int slurm_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity)
{
	slurm_sampler_inst_t inst = ctxt;
	int rc = 0;
	json_entity_t event, data, dict, attr;

	if (stream_type != LDMSD_STREAM_JSON) {
		INST_LOG(inst, LDMSD_LDEBUG, "Unexpected stream type data...ignoring\n");
		INST_LOG(inst, LDMSD_LDEBUG, "%s\n", msg);
		return EINVAL;
	}

	event = json_attr_find(entity, "event");
	if (!event) {
		INST_LOG(inst, LDMSD_LERROR, "'event' attribute missing\n");
		goto out_0;
	}

	attr = json_attr_find(entity, "timestamp");
	if (!attr) {
		INST_LOG(inst, LDMSD_LERROR, "'timestamp' attribute missing\n");
		goto out_0;
	}

	json_str_t event_name = json_value_str(json_attr_value(event));
	data = json_attr_find(entity, "data");
	if (!data) {
		INST_LOG(inst, LDMSD_LERROR,
			 "'%s' event is missing the 'data' attribute\n",
			 event_name->str);
		goto out_0;
	}
	dict = json_attr_value(data);
	attr = json_attr_find(dict, "job_id");
	if (!attr) {
		INST_LOG(inst, LDMSD_LERROR,
			 "The event is missing the 'job_id' attribute.\n");
		goto out_0;
	}

	uint64_t job_id = json_value_int(json_attr_value(attr));
	job_data_t job;

	pthread_mutex_lock(&inst->job_lock);
	ldms_transaction_begin(inst->job_set);
	if (0 == strncmp(event_name->str, "init", 4)) {
		job = get_job_data(inst, job_id); /* protect against duplicate entries */
		if (!job) {
			uint64_t local_task_count;
			attr = json_attr_find(dict, "local_tasks");
			if (!attr) {
				INST_LOG(inst, LDMSD_LERROR,
					 "'%s' event is missing the "
					 "'local_tasks'.\n", event_name->str);
				goto out_1;
			}
			/* Allocate the job_data used to track the job */
			local_task_count = json_value_int(json_attr_value(attr));
			job = alloc_job_data(inst, job_id, local_task_count);
			if (!job) {
				INST_LOG(inst, LDMSD_LERROR,
					 "[%d] Memory allocation failure.\n",
					 __LINE__);
				goto out_1;
			}
			handle_job_init(inst, job, entity);
		}
	} else if (0 == strncmp(event_name->str, "task_init_priv", 14)) {
		job = get_job_data(inst, job_id);
		if (!job) {
			INST_LOG(inst, LDMSD_LERROR,
				 "'%s' event was received for job %ld with no "
				 "job_data\n", event_name->str, job_id);
			goto out_1;
		}
		handle_task_init(inst, job, entity);
	} else if (0 == strncmp(event_name->str, "task_exit", 9)) {
		job = get_job_data(inst, job_id);
		if (!job) {
			INST_LOG(inst, LDMSD_LERROR,
				 "'%s' event was received for job %ld with no "
				 "job_data\n", event_name->str, job_id);
			goto out_1;
		}
		handle_task_exit(inst, job, entity);
	} else if (0 == strncmp(event_name->str, "exit", 4)) {
		job = get_job_data(inst, job_id);
		if (!job) {
			INST_LOG(inst, LDMSD_LERROR,
				 "'%s' event was received for job %ld with no "
				 "job_data\n", event_name->str, job_id);
			goto out_1;
		}
		handle_job_exit(inst, job, entity);
		release_job_data(inst, job);
	} else {
		INST_LOG(inst, LDMSD_LDEBUG,
		       "slurm_sampler: ignoring event '%s'\n", event_name->str);
	}
 out_1:
	ldms_transaction_end(inst->job_set);
	pthread_mutex_unlock(&inst->job_lock);
 out_0:
	return rc;
}

/* ============== Sampler Plugin APIs ================= */


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

/*
 * This overrides `samp_create_schema()` (the default implementation of
 * `ldmsd_sampler_type_s.create_schema()`) as our `component_id`, `job_id` and
 * `app_id` are different from the default implementation.
 */
static ldms_schema_t
slurm_sampler_create_schema(ldmsd_plugin_inst_t pi)
{
	slurm_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	ldms_schema_t schema = ldms_schema_new(samp->schema_name);
	int rc;
	int i;

	if (!schema)
		return NULL;
	/* component_id */
	inst->comp_id_idx = ldms_schema_metric_array_add(schema,
					"component_id", LDMS_V_U64_ARRAY, "",
					inst->job_list_len);
	if (inst->comp_id_idx < 0)
		goto err;
	/* job_id */
	inst->job_id_idx = ldms_schema_metric_array_add(schema, "job_id",
						  LDMS_V_U64_ARRAY, "",
						  inst->job_list_len);
	if (inst->job_id_idx < 0)
		goto err;
	/* app_id */
	inst->app_id_idx = ldms_schema_metric_array_add(schema, "app_id",
						  LDMS_V_U64_ARRAY, "",
						  inst->job_list_len);
	if (inst->app_id_idx < 0)
		goto err;
	/* cur_idx */
	inst->cur_idx = ldms_schema_metric_add(schema, "current_slot",
					       LDMS_V_U32, "");
	if (inst->cur_idx < 0)
		goto err;
	/* job_state */
	inst->job_state_idx =
		ldms_schema_metric_array_add(schema, "job_state",
					     LDMS_V_U8_ARRAY, "",
					     inst->job_list_len);
	if (inst->job_state_idx < 0)
		goto err;
	/* job_size */
	inst->job_size_idx =
		ldms_schema_metric_array_add(schema, "job_size",
					     LDMS_V_U32_ARRAY, "",
					     inst->job_list_len);
	if (inst->job_size_idx < 0)
		goto err;
	/* job_uid */
	inst->job_uid_idx =
		ldms_schema_metric_array_add(schema, "job_uid",
					     LDMS_V_U32_ARRAY, "",
					     inst->job_list_len);
	if (inst->job_uid_idx < 0)
		goto err;
	/* job_gid */
	inst->job_gid_idx =
		ldms_schema_metric_array_add(schema, "job_gid",
					     LDMS_V_U32_ARRAY, "",
					     inst->job_list_len);
	if (inst->job_gid_idx < 0)
		goto err;
	/* job_start */
	inst->job_start_idx =
		ldms_schema_metric_array_add(schema, "job_start",
					     LDMS_V_U32_ARRAY, "",
					     inst->job_list_len);
	if (inst->job_start_idx < 0)
		goto err;
	/* job_end */
	inst->job_end_idx =
		ldms_schema_metric_array_add(schema, "job_end",
					     LDMS_V_U32_ARRAY, "",
					     inst->job_list_len);
	if (inst->job_end_idx < 0)
		goto err;
	/* node_count */
	inst->node_count_idx =
		ldms_schema_metric_array_add(schema, "node_count",
					     LDMS_V_U32_ARRAY, "",
					     inst->job_list_len);
	if (inst->node_count_idx < 0)
		goto err;
	/* task_count */
	inst->task_count_idx =
		ldms_schema_metric_array_add(schema, "task_count",
					     LDMS_V_U32_ARRAY, "",
					     inst->job_list_len);
	if (inst->task_count_idx < 0)
		goto err;

	/* task_pid */
	inst->task_pid_idx = inst->task_count_idx + 1;
	for (i = 0; i < inst->job_list_len; i++) {
		char metric_name[80];
		sprintf(metric_name, "task_pid_%d", i);
		rc = ldms_schema_metric_array_add(schema, metric_name,
						  LDMS_V_U32_ARRAY,
						  "", inst->task_list_len);
		if (rc < 0)
			goto err;
	}

	/* task_rank */
	inst->task_rank_idx = rc + 1;
	for (i = 0; i < inst->job_list_len; i++) {
		char metric_name[80];
		sprintf(metric_name, "task_rank_%d", i);
		rc = ldms_schema_metric_array_add(schema, metric_name,
						  LDMS_V_U32_ARRAY,
						  "", inst->task_list_len);
		if (rc < 0)
			goto err;
	}

	/* task_exit_status */
	inst->task_exit_status_idx = rc + 1;
	for (i = 0; i < inst->job_list_len; i++) {
		char metric_name[80];
		sprintf(metric_name, "task_exit_status_%d", i);
		rc = ldms_schema_metric_array_add(schema, metric_name,
						  LDMS_V_U32_ARRAY,
						  "", inst->task_list_len);
		if (rc < 0)
			goto err;
	}

	return schema;
 err:
	if (schema)
		ldms_schema_delete(schema);
	return NULL;
}

static int
slurm_sampler_sample(ldmsd_plugin_inst_t inst)
{
	/* DO NOTHING */
	return 0;
}


/* ============== Common Plugin APIs ================= */

static const char *
slurm_sampler_desc(ldmsd_plugin_inst_t pi)
{
	return "slurm_sampler - SLURM job sampler (use with slurm_notifier)";
}

static char *_help = "\
slurm_sampler synopsis:\n\
    config name=INST [COMMON_OPTIONS] [stream=STR] [job_count=INT] \n\
                                      [task_count=INT]\n\
\n\
Option descriptions:\n\
    stream      The name of the LDMSD stream that send slurm job data\n\
                to this plugin. Default: slurm.\n\
    job_count   The maximum number of concurrent jobs. Default: 8.\n\
    task_count  The maximum number of PIDs in a job. If this is -1,\n\
                the plugin will set this to the number of CPU cores.\n\
                Default: -1.\n\
\n\
";

static const char *
slurm_sampler_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static int
slurm_sampler_config(ldmsd_plugin_inst_t pi, json_entity_t json,
		     char *ebuf, int ebufsz)
{
	slurm_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	const char *value;
	int rc;

	if (inst->job_set) {
		snprintf(ebuf, ebufsz, "Set already created.\n");
		rc = EBUSY;
		goto err;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		goto err;

	/* force schema_name to be "mt-slurm" */
	if (samp->schema_name)
		free(samp->schema_name);
	samp->schema_name = strdup("mt-slurm");
	if (!samp->schema_name) {
		rc = ENOMEM;
		snprintf(ebuf, ebufsz, "Out of memory.\n");
		goto err;
	}

	/* Plugin-specific config here */
	value = json_attr_find_str(json, "stream");
	inst->stream = strdup(value?value:"slurm");
	if (!inst->stream) {
		rc = ENOMEM;
		snprintf(ebuf, ebufsz, "Out of memory.\n");
		goto err;
	}
	ldmsd_stream_subscribe(inst->stream, slurm_recv_cb, inst);

	value = json_attr_find_str(json, "job_count");
	if (value)
		inst->job_list_len = atoi(value);
	int i;
	for (i = 0; i < inst->job_list_len; i++) {
		job_data_t job = malloc(sizeof *job);
		if (!job) {
			rc = ENOMEM;
			snprintf(ebuf, ebufsz, "[%d]: memory allocation "
					       "failure.\n", __LINE__);
			goto err;
		}
		job->job_slot = i;
		job->job_state = JOB_FREE;
		TAILQ_INSERT_TAIL(&inst->free_slot_list, job, slot_ent);
	}

	value = json_attr_find_str(json, "task_count");
	if (value)
		inst->task_list_len = atoi(value);
	if (inst->task_list_len < 0) {
		cpu_set_t cpu_set;
		rc = sched_getaffinity(getpid(), sizeof(cpu_set), &cpu_set);
		if (rc == 0) {
			inst->task_list_len = CPU_COUNT(&cpu_set);
		} else {
			inst->task_list_len = PID_LIST_LEN;
		}
	}

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema) {
		rc = errno;
		snprintf(ebuf, ebufsz,
			 "failed creating schema, errno: %d\n", errno);
		goto err;
	}
	inst->job_set = samp->create_set(pi, samp->set_inst_name,
					 samp->schema, NULL);
	if (!inst->job_set) {
		snprintf(ebuf, ebufsz,
			 "failed creating set, errno: %d\n", errno);
		rc = errno;
		goto err;
	}

	rc = 0;

 err:
	if (rc)
		INST_LOG(inst, LDMSD_LERROR, "%s", ebuf);
	return rc;
}

static void
slurm_sampler_del(ldmsd_plugin_inst_t pi)
{
	slurm_sampler_inst_t inst = (void*)pi;

	/* The undo of slurm_sampler_init and instance cleanup */
	if (inst->stream)
		free(inst->stream);
}

static int
cmp_job_id(void *a, const void *b)
{
	uint64_t a_ = *(uint64_t *)a;
	uint64_t b_ = *(uint64_t *)b;
	if (a_ < b_)
		return -1;
	if (a_ > b_)
		return 1;
	return 0;
}

static int
slurm_sampler_init(ldmsd_plugin_inst_t pi)
{
	slurm_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;

	samp->create_schema = slurm_sampler_create_schema;
	samp->sample = slurm_sampler_sample;

	rbt_init(&inst->job_tree, cmp_job_id);
	TAILQ_INIT(&inst->free_slot_list);
	pthread_mutex_init(&inst->job_lock, NULL);

	return 0;
}

static struct slurm_sampler_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = LDMSD_SAMPLER_TYPENAME,
		.plugin_name = "slurm_sampler",

		/* Common Plugin APIs */
		.desc   = slurm_sampler_desc,
		.help   = slurm_sampler_help,
		.init   = slurm_sampler_init,
		.del    = slurm_sampler_del,
		.config = slurm_sampler_config,

	},

	.job_list_len = JOB_LIST_LEN,
	.task_list_len = -1, /* default: automatic */
};

ldmsd_plugin_inst_t
new()
{
	slurm_sampler_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
