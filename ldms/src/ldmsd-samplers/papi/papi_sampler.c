/* -*- c-basic-offset: 8 -*-
 *
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
 * \file papi_sampler.c
 *
 * PAPI Sampler for Slurm-based application.
 */

#include "papi_sampler.h"

/* ============== Helper Functions ================= */

/*
 * Find the job_data record with the specified job_id
 */
static job_data_t get_job_data(papi_sampler_inst_t inst, uint64_t job_id)
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
static job_data_t alloc_job_data(papi_sampler_inst_t inst, uint64_t job_id)
{
	job_data_t jd;

	jd = calloc(1, sizeof *jd);
	if (jd) {
		jd->inst = inst;
		jd->job_id = job_id;
		jd->job_state = JOB_PAPI_IDLE;
		jd->job_state_time = time(NULL);
		jd->task_init_count = 0;

		TAILQ_INIT(&jd->event_list);
		LIST_INIT(&jd->task_list);

		rbn_init(&jd->job_ent, &jd->job_id);
		rbt_ins(&inst->job_tree, &jd->job_ent);
	}
	return jd;
}

static void release_job_data(job_data_t jd)
{
	papi_sampler_inst_t inst = jd->inst;
	job_task_t t;
	jd->job_state = JOB_PAPI_COMPLETE;
	jd->job_state_time = time(NULL);
	if (jd->papi_init) {
		LIST_FOREACH(t, &jd->task_list, entry) {
			PAPI_cleanup_eventset(t->event_set);
			PAPI_destroy_eventset(&t->event_set);
		}
		jd->papi_init = 0;
	}
	rbt_del(&inst->job_tree, &jd->job_ent);
	LIST_INSERT_HEAD(&inst->job_expiry_list, jd, expiry_entry);
}

static void free_job_data(job_data_t jd)
{
	papi_event_t ev;
	job_task_t t;

	if (jd->set) {
		ldms_set_unpublish(jd->set);
		LDMSD_SAMPLER(jd->inst)->delete_set(LDMSD_INST(jd->inst),
					 ldms_set_instance_name_get(jd->set));
	}

	while (!TAILQ_EMPTY(&jd->event_list)) {
		ev = TAILQ_FIRST(&jd->event_list);
		free(ev->event_name);
		TAILQ_REMOVE(&jd->event_list, ev, entry);
		free(ev);
	}

	while (!LIST_EMPTY(&jd->task_list)) {
		t = LIST_FIRST(&jd->task_list);
		LIST_REMOVE(t, entry);
		free(t);
	}

	free(jd->schema_name);
	free(jd->instance_name);
	free(jd);
}

static void *cleanup_proc(void *arg)
{
	papi_sampler_inst_t inst = arg;
	time_t now;
	job_data_t job;
	LIST_HEAD(,job_data) delete_list;
	LIST_INIT(&delete_list);
	while (1) {
		sleep(inst->job_expiry);
		now = time(NULL);
		pthread_mutex_lock(&inst->job_lock);
		LIST_FOREACH(job, &inst->job_expiry_list, expiry_entry) {
			if ((now - job->job_end) > inst->job_expiry)
				LIST_INSERT_HEAD(&delete_list, job, delete_entry);
		}
		while (!LIST_EMPTY(&delete_list)) {
			job = LIST_FIRST(&delete_list);
			INST_LOG(inst, LDMSD_LINFO,
				"[%d]: deleting instance '%s', "
				"set %p, set_id %ld.\n",
				__LINE__, job->instance_name, job->set,
				ldms_set_id(job->set));
			LIST_REMOVE(job, expiry_entry);
			LIST_REMOVE(job, delete_entry);
			free_job_data(job);
		}
		pthread_mutex_unlock(&inst->job_lock);
	}
	return NULL;
}

static int cmp_job_id(void *a, const void *b)
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
create_metric_set(job_data_t job)
{
	papi_sampler_inst_t inst = job->inst;
	ldmsd_sampler_type_t samp = LDMSD_SAMPLER(inst);
	ldms_schema_t schema;
	int i, rc = -1;
	job_task_t t;
	char *tmp;

	tmp = samp->schema_name;
	samp->schema_name = job->schema_name;
	schema = LDMSD_SAMPLER(inst)->create_schema(&inst->base);
	samp->schema_name = tmp;
	if (schema == NULL) {
		job->job_state = JOB_PAPI_ERROR;
		job->job_state_time = time(NULL);
		return ENOMEM;
	}

	/* `create_schema()` already add component_id, job_id and app_id */
	/* component_id */
	job->comp_id_mid = SAMPLER_COMP_IDX;
	/* job_id */
	job->job_id_mid = SAMPLER_JOB_IDX;
	/* app_id */
	job->app_id_mid = SAMPLER_APP_IDX;
	/* job_state */
	job->job_state_mid =
		ldms_schema_metric_add(schema, "job_state", LDMS_V_U8, "");
	if (job->job_state_mid < 0)
		goto err;
	/* job_start */
	job->job_start_mid =
		ldms_schema_meta_add(schema, "job_start", LDMS_V_U32, "");
	if (job->job_start_mid < 0)
		goto err;
	/* job_end */
	job->job_end_mid =
		ldms_schema_metric_add(schema, "job_end", LDMS_V_U32, "");
	if (job->job_end_mid < 0)
		goto err;
	/* task_count */
	job->task_count_mid =
		ldms_schema_meta_add(schema, "task_count", LDMS_V_U32, "");
	if (job->task_count_mid < 0)
		goto err;

	/* task_pid */
	job->task_pids_mid =
		ldms_schema_meta_array_add(schema, "task_pids",
					   LDMS_V_U32_ARRAY, "",
					   job->task_count);
	if (job->task_pids_mid < 0)
		goto err;

	/* task_pid */
	job->task_ranks_mid =
		ldms_schema_meta_array_add(schema, "task_ranks",
					   LDMS_V_U32_ARRAY, "",
					   job->task_count);
	if (job->task_ranks_mid < 0)
		goto err;

	/* events */
	papi_event_t ev;
	TAILQ_FOREACH(ev, &job->event_list, entry) {
		ev->mid = ldms_schema_metric_array_add(schema,
						       ev->event_name,
						       LDMS_V_S64_ARRAY, "",
						       job->task_count);
		if (ev->mid < 0)
			goto err;
	}

	job->instance_name = malloc(256);
	snprintf(job->instance_name, 256, "%s/%s/%lu",
		 samp->producer_name, job->schema_name,
		 job->job_id);
	job->set = samp->create_set(&inst->base, job->instance_name, schema, job);
	if (!job->set) {
		rc = errno;
		INST_LOG(inst, LDMSD_LERROR,
		         "[%d]: Error %d creating the metric set '%s'.\n",
			 __LINE__, rc, job->instance_name);
		goto err;
	}
	ldms_metric_set_u64(job->set, job->job_id_mid, job->job_id);
	ldms_metric_set_u64(job->set, job->comp_id_mid, samp->component_id);
	ldms_metric_set_u64(job->set, job->app_id_mid, job->app_id);
	ldms_metric_set_u32(job->set, job->task_count_mid, job->task_count);
	ldms_metric_set_u32(job->set, job->job_start_mid, job->job_start);
	ldms_metric_set_u32(job->set, job->job_end_mid, job->job_end);
	ldms_metric_set_u8(job->set, job->job_state_mid, job->job_state);
	i = 0;
	LIST_FOREACH(t, &job->task_list, entry) {
		ldms_metric_array_set_u32(job->set, job->task_pids_mid, i, t->pid);
		ldms_metric_array_set_u32(job->set, job->task_ranks_mid, i++, t->rank);
	}
	ldms_set_publish(job->set);
	ldms_schema_delete(schema);
	return 0;
 err:
	job->job_state = JOB_PAPI_ERROR;
	job->job_state_time = time(NULL);
	if (schema)
		ldms_schema_delete(schema);
	return rc;
}

static void
sample_job(job_data_t job)
{
	long long values[64];
	papi_event_t ev;
	job_task_t t;
	int ev_idx, task_idx, rc;

	ldms_transaction_begin(job->set);
	ldms_metric_set_u8(job->set, job->job_state_mid, job->job_state);
	task_idx = 0;
	LIST_FOREACH(t, &job->task_list, entry) {
		rc = PAPI_read(t->event_set, values);
		if (rc != PAPI_OK) {
			INST_LOG(job->inst, LDMSD_LERROR,
				 "papi_sampler [%d]: PAPI error '%s' "
				 "reading EventSet for pid %d.\n", __LINE__,
				 PAPI_strerror(rc), t->pid);
		}
		rc = PAPI_reset(t->event_set);
		ev_idx = 0;
		TAILQ_FOREACH(ev, &job->event_list, entry) {
			ldms_metric_array_set_s64(job->set, ev->mid,
						  task_idx, values[ev_idx++]);
		}
		task_idx++;
	}
	ldms_transaction_end(job->set);
}

static int stream_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			  ldmsd_stream_type_t stream_type,
			  const char *msg, size_t msg_len,
			  json_entity_t entity);

static int
handle_job_init(papi_sampler_inst_t inst, uint64_t job_id, json_entity_t e)
{
	int rc;
	int local_tasks;
	uint64_t job_start;
	json_entity_t attr, data, dict;
	job_data_t job = NULL;

	attr = json_attr_find(e, "timestamp");
	if (!attr) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'timestamp' attribute in 'init' event.\n");
		return EINVAL;
	}
	job_start = json_value_int(json_attr_value(attr));

	data = json_attr_find(e, "data");
	if (!data) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'data' attribute in 'init' event.\n");
		return EINVAL;
	}

	dict = json_attr_value(data);
	attr = json_attr_find(dict, "local_tasks");
	if (attr) {
		local_tasks = json_value_int(json_attr_value(attr));
	} else {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'local_tasks' attribute in 'init' event.\n");
		return EINVAL;
	}

	/* Parse the the subscriber_data attribute*/
	attr = json_attr_find(dict, "subscriber_data");
	if (!attr) {
		INST_LOG(inst, LDMSD_LERROR,
			 "[%d]: subscriber_data missing from init message, "
			 "job %ld ignored.\n", __LINE__, job_id);
		return EINVAL;
	}

	/*
	 * The subscriber data is a JSON string. The data destined for
	 * this sampler is in the "papi_sampler" attribute. It has the
	 * following format:
	 *
	 * { . . .
	 *   "papi_sampler" :
	 *       { "file" : <config-file-path>,
	 *         "config" : <config>
	 *       },
	 *   . . .
	 * }
	 *
	 * The sampler ignores the job if:
	 * - The subscriber data is missing the "papi_sampler" attribute
	 * - The papi_sampler dictionary has neither the "file" nor the "config" attributes.
	 * - The papi configuration is invalid
	 *
	 * Otherwise,
	 * - If the "file" attribute is present, it is the path to a
         *   file containing the sampler configuration.
	 * - If the "file" attribute is missing, the "config"
         *   attribute contains the papi sampler configuration.
	 * - In either case, the format of the configuration is JSON
	 */
	json_entity_t subs_data = json_attr_value(attr);
	if (json_entity_type(subs_data) != JSON_DICT_VALUE) {
		INST_LOG(inst, LDMSD_LINFO,
			 "[%d]: subscriber_data is not a dictionary, "
			 "job %ld ignored.\n", __LINE__, job_id);
		rc = EINVAL;
		goto out;
	}
	attr = json_attr_find(subs_data, "papi_sampler");
	if (!attr)  {
		INST_LOG(inst, LDMSD_LINFO,
			 "[%d]: subscriber_data missing papi_sampler "
			 "attribute, job is ignored.\n", __LINE__);
		rc = EINVAL;
		goto out;
	}
	dict = json_attr_value(attr);

	json_entity_t file_attr, config_attr;
	file_attr = json_attr_find(dict, "file");
	config_attr = json_attr_find(dict, "config");
	if (!file_attr && !config_attr) {
		INST_LOG(inst, LDMSD_LERROR,
			 "[%d]: papi_config object must contain either the "
			 "'file' or 'config' attribute.\n", __LINE__);
		rc = EINVAL;
		goto out;
	}
	job = alloc_job_data(inst, job_id);
	if (!job) {
		INST_LOG(inst, LDMSD_LERROR,
			 "[%d]: Memory allocation failure, job ignored.\n",
			 __LINE__);
		rc = ENOMEM;
		goto out;
	}
	if (file_attr) {
		json_entity_t file_name = json_attr_value(file_attr);
		if (json_entity_type(file_name) != JSON_STRING_VALUE) {
			INST_LOG(inst, LDMSD_LERROR,
				 "[%d]: papi_config 'file' attribute must be a "
				 "string, job %ld ignored.", __LINE__, job_id);
			rc = EINVAL;
			goto out;
		}
		rc = papi_process_config_file(job, json_value_str(file_name)->str);
	} else {
		json_entity_t config_string = json_attr_value(config_attr);
		if (json_entity_type(config_string) != JSON_STRING_VALUE) {
			INST_LOG(inst, LDMSD_LERROR,
				 "[%d]: papi_config 'config' attribute must be "
				 "a string, job %ld ignored.",
				 __LINE__, job_id);
			rc = EINVAL;
			goto out;
		}
		rc = papi_process_config_data(job,
				json_value_str(config_string)->str,
				json_value_str(config_string)->str_len);
	}
	job->job_state = JOB_PAPI_INIT;
	job->job_state_time = time(NULL);
	job->job_start = job_start;
	job->task_count = local_tasks;
	job->job_end = 0;
 out:
	if (rc && job)
		release_job_data(job);
	return rc;
}

static void
handle_papi_error(job_data_t job)
{
	long long values[64];
	job_task_t t;
	LIST_FOREACH(t, &job->task_list, entry) {
		if (t->papi_start) {
			PAPI_stop(t->event_set, values);
			t->papi_start = 0;
		}
		if (t->papi_init) {
			PAPI_cleanup_eventset(t->event_set);
			PAPI_destroy_eventset(&t->event_set);
			t->papi_init = 0;
		}
	}
	job->job_state = JOB_PAPI_ERROR;
	job->job_state_time = time(NULL);
	/* job_lock is held, call chain: stream_recv_cb
	 *                               -> handle_task_init
	 *                               -> handle_papi_error */
	ldms_transaction_begin(job->set);
	ldms_metric_set_u8(job->set, job->job_state_mid, job->job_state);
	ldms_transaction_end(job->set);
}

static void
handle_task_init(job_data_t job, json_entity_t e)
{
	papi_sampler_inst_t inst = job->inst;
	json_entity_t attr;
	json_entity_t data, dict;
	job_task_t t;
	int rc, task_pid, task_rank;
	papi_event_t ev;

	if (job->job_state != JOB_PAPI_INIT)
		return;
	data = json_attr_find(e, "data");
	if (!data) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'data' attribute in 'task_init' event.\n");
		return;
	}
	dict = json_attr_value(data);

	attr = json_attr_find(dict, "task_pid");
	if (!attr) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'task_pid' attribute in 'task_init' event.\n");
		return;
	}
	task_pid = json_value_int(json_attr_value(attr));

	attr = json_attr_find(dict, "task_global_id");
	if (!attr) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Missing 'task_global_id' attribute in "
			 "'task_init' event.\n");
		return;
	}
	task_rank = json_value_int(json_attr_value(attr));

	t = malloc(sizeof *t);
	if (!t) {
		INST_LOG(inst, LDMSD_LERROR,
			 "[%d]: Memory allocation failure.\n", __LINE__);
		return;
	}
	t->papi_init = 0;
	t->papi_start = 0;
	t->pid = task_pid;
	t->rank = task_rank;
	t->event_set = PAPI_NULL;
	LIST_INSERT_HEAD(&job->task_list, t, entry);

	job->task_init_count += 1;
	if (job->task_init_count < job->task_count)
		return;

	if (create_metric_set(job))
		return;

	/* pause syspapi */
	ldmsd_stream_deliver("syspapi_stream", LDMSD_STREAM_STRING,
			     "pause", 6, NULL);

	LIST_FOREACH(t, &job->task_list, entry) {
		rc = PAPI_create_eventset(&t->event_set);
		if (rc != PAPI_OK) {
			INST_LOG(inst, LDMSD_LERROR,
				 "[%d]: PAPI error '%s' creating EventSet.\n",
				 __LINE__, PAPI_strerror(rc));
			job->job_state = JOB_PAPI_ERROR;
			job->job_state_time = time(NULL);
			goto err;
		}
		rc = PAPI_assign_eventset_component(t->event_set, 0);
		if (rc != PAPI_OK) {
			INST_LOG(inst, LDMSD_LERROR,
				 "[%d]: PAPI error '%s' assign EventSet "
				 "to CPU.\n", __LINE__, PAPI_strerror(rc));
			job->job_state = JOB_PAPI_ERROR;
			job->job_state_time = time(NULL);
			goto err;
		}
		rc = PAPI_set_multiplex(t->event_set);
		if (rc != PAPI_OK) {
			INST_LOG(inst, LDMSD_LERROR,
				 "[%d]: PAPI error '%s' setting multiplex.\n",
				 __LINE__, PAPI_strerror(rc));
			job->job_state = JOB_PAPI_ERROR;
			job->job_state_time = time(NULL);
			goto err;
		}
		t->papi_init = 1;
		TAILQ_FOREACH(ev, &job->event_list, entry) {
			rc = PAPI_add_event(t->event_set, ev->event_code);
			if (rc != PAPI_OK) {
				INST_LOG(inst, LDMSD_LERROR,
					 "[%d]: PAPI error '%s' adding "
					 "event '%s'.\n", __LINE__,
					 PAPI_strerror(rc), ev->event_name);
				job->job_state = JOB_PAPI_ERROR;
				job->job_state_time = time(NULL);
				goto err;
			}
		}
		rc = PAPI_attach(t->event_set, t->pid);
		if (rc != PAPI_OK) {
			INST_LOG(inst, LDMSD_LERROR,
				 "[%d]: PAPI error '%s' attaching EventSet to "
				 "pid %d.\n", __LINE__,
				 PAPI_strerror(rc), t->pid);
			job->job_state = JOB_PAPI_ERROR;
			job->job_state_time = time(NULL);
			goto err;
		}
	}
	LIST_FOREACH(t, &job->task_list, entry) {
		rc = PAPI_start(t->event_set);
		if (rc != PAPI_OK) {
			INST_LOG(inst, LDMSD_LERROR,
				 "[%d]: PAPI error '%s' starting EventSet for "
				 "pid %d.\n", __LINE__,
				 PAPI_strerror(rc), t->pid);
			job->job_state = JOB_PAPI_ERROR;
			job->job_state_time = time(NULL);
			goto err;
		}
		t->papi_start = 1;
	}
	job->job_state = JOB_PAPI_RUNNING;
	job->job_state_time = time(NULL);
	/* job_lock is held, call chain: stream_recv_cb
	 *                               -> handle_task_init */
	ldms_transaction_begin(job->set);
	ldms_metric_set_u8(job->set, job->job_state_mid, job->job_state);
	ldms_transaction_end(job->set);
	return;
 err:
	handle_papi_error(job);
}

static void
handle_task_exit(job_data_t job, json_entity_t e)
{
	papi_sampler_inst_t inst = job->inst;
	long long values[64];
	json_entity_t attr;
	json_entity_t data = json_attr_find(e, "data");
	json_entity_t dict = json_attr_value(data);
	int rc;
	int task_pid;
	job_task_t t;

	/* Tell sampler to stop sampling */
	job->job_state = JOB_PAPI_STOPPING;
	job->job_state_time = time(NULL);
	/* job_lock is held, call chain: stream_recv_cb
	 *                               -> handle_task_exit */
	ldms_transaction_begin(job->set);
	ldms_metric_set_u8(job->set, job->job_state_mid, job->job_state);
	ldms_transaction_end(job->set);

	attr = json_attr_find(dict, "task_pid");
	task_pid = json_value_int(json_attr_value(attr));

	LIST_FOREACH(t, &job->task_list, entry) {
		if (t->pid != task_pid)
			continue;
		if (!t->papi_init)
			return;
		rc = PAPI_stop(t->event_set, values);
		if (rc != PAPI_OK) {
			INST_LOG(inst, LDMSD_LERROR,
				 "[%d]: PAPI error '%s' stopping EventSet "
				 "for pid %d.\n", __LINE__,
				 PAPI_strerror(rc), t->pid);
		}
		t->papi_start = 0;
		rc = PAPI_detach(t->event_set);
		if (rc != PAPI_OK) {
			INST_LOG(inst, LDMSD_LERROR,
				 "[%d]: Error '%s' de-attaching from "
				 "process pid= %d. rc= %d\n", __LINE__,
				 PAPI_strerror(rc), t->pid, rc);
		}
		PAPI_cleanup_eventset(t->event_set);
		PAPI_destroy_eventset(&t->event_set);
		break;
	}
}

static void
handle_job_exit(job_data_t job, json_entity_t e)
{
	json_entity_t attr = json_attr_find(e, "timestamp");
	uint64_t timestamp = json_value_int(json_attr_value(attr));

	job->job_state = JOB_PAPI_COMPLETE;
	/* job_lock is held, call chain: stream_recv_cb
	 *                               -> handle_job_exit */
	ldms_transaction_begin(job->set);
	ldms_metric_set_u8(job->set, job->job_state_mid, job->job_state);
	ldms_transaction_end(job->set);
	job->job_state_time = time(NULL);
	job->job_end = timestamp;
	release_job_data(job);
}

static int
stream_recv_cb(ldmsd_stream_client_t c, void *ctxt,
	       ldmsd_stream_type_t stream_type,
	       const char *msg, size_t msg_len,
	       json_entity_t entity)
{
	papi_sampler_inst_t inst = ctxt;
	int rc = 0;
	json_entity_t event, data, dict, attr;

	if (stream_type != LDMSD_STREAM_JSON) {
		INST_LOG(inst, LDMSD_LDEBUG,
			 "Unexpected stream type data...ignoring: %s\n", msg);
		return EINVAL;
	}

	event = json_attr_find(entity, "event");
	if (!event) {
		INST_LOG(inst, LDMSD_LERROR, "'event' attribute missing\n");
		goto out_0;
	}

	json_str_t event_name = json_value_str(json_attr_value(event));
	data = json_attr_find(entity, "data");
	if (!data) {
		INST_LOG(inst, LDMSD_LERROR, "'%s' event is missing "
			 "the 'data' attribute\n", event_name->str);
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
	if (0 == strncmp(event_name->str, "init", 4)) {
		job = get_job_data(inst, job_id); /* protect against duplicate entries */
		if (job) {
			INST_LOG(inst, LDMSD_LINFO,
				 "[%d]: ignoring duplicate init event received "
				 "for job %ld.\n", __LINE__, job_id);
			goto out_0;
		}
		rc = handle_job_init(inst, job_id, entity);
	} else if (0 == strncmp(event_name->str, "task_init_priv", 14)) {
		job = get_job_data(inst, job_id);
		if (!job) {
			INST_LOG(inst, LDMSD_LINFO,
				 "'%s' event was received for job %ld with no "
				 "job_data\n", event_name->str, job_id);
			goto out_1;
		}
		handle_task_init(job, entity);
	} else if (0 == strncmp(event_name->str, "task_exit", 9)) {
		job = get_job_data(inst, job_id);
		if (!job) {
			INST_LOG(inst, LDMSD_LINFO,
				 "'%s' event was received for job %ld with no "
				 "job_data\n", event_name->str, job_id);
			goto out_1;
		}
		handle_task_exit(job, entity);
	} else if (0 == strncmp(event_name->str, "exit", 4)) {
		job = get_job_data(inst, job_id);
		if (!job) {
			INST_LOG(inst, LDMSD_LINFO,
				 "'%s' event was received for job %ld with no "
				 "job_data\n", event_name->str, job_id);
			goto out_1;
		}
		handle_job_exit(job, entity);
	} else {
		INST_LOG(inst, LDMSD_LDEBUG,
			 "ignoring event '%s'\n", event_name->str);
	}
 out_1:
	pthread_mutex_unlock(&inst->job_lock);
 out_0:
	return rc;
}

/* ============== Sampler Plugin APIs ================= */

static
int papi_sampler_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	papi_sampler_inst_t inst = (void*)pi;
	job_data_t job;
	struct rbn *rbn;
	uint64_t now = time(NULL);
	LIST_HEAD(,job_data) delete_list;
	LIST_INIT(&delete_list);

	pthread_mutex_lock(&inst->job_lock);
	RBT_FOREACH(rbn, &inst->job_tree) {
		job = container_of(rbn, struct job_data, job_ent);
		switch (job->job_state) {
		case JOB_PAPI_IDLE:
		case JOB_PAPI_COMPLETE:
			assert(0 == "IDLE and COMPLETE Jobs must not be in job tree");
		case JOB_PAPI_RUNNING:
			assert(job->set);
			sample_job(job);
			break;
		case JOB_PAPI_STOPPING:
		case JOB_PAPI_ERROR:
			assert(job->set);
			if (job->job_state != ldms_metric_get_u8(job->set, job->job_state_mid)) {
				/* Update job state in the set */
				ldms_transaction_begin(job->set);
				ldms_metric_set_u8(job->set, job->job_state_mid, job->job_state);
				ldms_transaction_end(job->set);
			}
		case JOB_PAPI_INIT:
			/*
			 * Don't let sets linger in !RUNNING state for
			 * longer than the cleanup time. This is
			 * typically a sign that a slurm event was
			 * dropped due to a crashed process.
			 */
			if (now - job->job_state_time > (2 * inst->job_expiry))
				LIST_INSERT_HEAD(&delete_list, job, delete_entry);
			break;
		}
	}
	while (!LIST_EMPTY(&delete_list)) {
		job = LIST_FIRST(&delete_list);
		INST_LOG(inst, LDMSD_LINFO,
			 "[%d]: forcing cleanup of instance '%s', "
			 "set %p, set_id %ld.\n",
			 __LINE__, job->instance_name, job->set,
			 ldms_set_id(job->set));
		LIST_REMOVE(job, delete_entry);
		release_job_data(job);
	}
	if (rbt_empty(&inst->job_tree)) {
		/* resume syspapi when no job is running */
		ldmsd_stream_deliver("syspapi_stream", LDMSD_STREAM_STRING,
				     "resume", 7, NULL);
	}
	pthread_mutex_unlock(&inst->job_lock);
	return 0;
}

/* ============== Common Plugin APIs ================= */

static
const char *papi_sampler_desc(ldmsd_plugin_inst_t pi)
{
	return "papi_sampler - PAPI sampler plugin";
}

static
char *_help = "\
papi_sampler config synopsis:\
    config name=INST [COMMON_OPTIONS]\n\
           [stream=<stream_name>] [job_expiry=<seconds>]\n\
\n\
Option descriptions:\n\
    stream        A stream name to subscribe to. Defaults to 'slurm'.\n\
    job_expiry    Number of seconds to retain sets for completed jobs.\n\
                  Defaults to 60s\n\
";

static
const char *papi_sampler_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int papi_sampler_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	papi_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int rc;
	json_entity_t jval;
	const char *value;

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	jval = json_value_find(json, "job_expiry");
	if (jval) {
		if (jval->type != JSON_STRING_VALUE) {
			INST_LOG(inst, LDMSD_LERROR,
				 "`job_expiry` attribute is not string.");
			return EINVAL;
		}
		value = jval->value.str_->str;
		inst->job_expiry = strtol(value, NULL, 0);
	}

	jval = json_value_find(json, "stream");
	if (!jval) {
		value = "slurm";
	} else {
		if (jval->type != JSON_STRING_VALUE) {
			INST_LOG(inst, LDMSD_LERROR,
				 "`stream` attribute is not string.");
			return EINVAL;
		}
		value = jval->value.str_->str;
	}

	inst->stream_name = strdup(value);
	if (!inst->stream_name) {
		INST_LOG(inst, LDMSD_LERROR,
			 "[%d]: Memory allocation failure.\n", __LINE__);
		return EINVAL;
	}
	inst->stream_client = ldmsd_stream_subscribe(inst->stream_name,
						     stream_recv_cb, inst);
	/* schema & set are created on-the-fly when job started */
	return 0;
}

static
void papi_sampler_del(ldmsd_plugin_inst_t pi)
{
	papi_sampler_inst_t inst = (void*)pi;
	struct rbn *rbn;
	job_data_t job;
	/* The undo of papi_sampler_init and instance cleanup */

	/* close the stream subscription, no events delivered after close  */
	ldmsd_stream_close(inst->stream_client);

	pthread_mutex_lock(&inst->job_lock);

	/* terminate cleanup thread */
	pthread_cancel(inst->cleanup_thread);
	pthread_join(inst->cleanup_thread, NULL);

	/* clean up job tree */
	while ((rbn = rbt_min(&inst->job_tree))) {
		job = container_of(rbn, struct job_data, job_ent);
		release_job_data(job);
	}

	pthread_mutex_unlock(&inst->job_lock);

	/* clean up expiry list */
	while ((job = LIST_FIRST(&inst->job_expiry_list))) {
		LIST_REMOVE(job, expiry_entry);
		free_job_data(job);
	}
}

static int once = 0;

static
int papi_sampler_init(ldmsd_plugin_inst_t pi)
{
	papi_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	/* override update_schema() and update_set() */
	samp->update_schema = NULL;
	samp->update_set = papi_sampler_update_set;

	if (0 == __sync_fetch_and_or(&once, 1)) {
		/* library init only once */
		PAPI_library_init(PAPI_VER_CURRENT);
		PAPI_thread_init(pthread_self);
	}

	rbt_init(&inst->job_tree, cmp_job_id);
	LIST_INIT(&inst->job_expiry_list);

	(void)pthread_create(&inst->cleanup_thread, NULL, cleanup_proc, inst);
	return 0;
}

static
struct papi_sampler_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = LDMSD_SAMPLER_TYPENAME,
		.plugin_name = "papi_sampler",

                /* Common Plugin APIs */
		.desc   = papi_sampler_desc,
		.help   = papi_sampler_help,
		.init   = papi_sampler_init,
		.del    = papi_sampler_del,
		.config = papi_sampler_config,

	},
	/* plugin-specific data initialization (for new()) here */
	.job_expiry = DEFAULT_JOB_EXPIRY,
};

ldmsd_plugin_inst_t new()
{
	papi_sampler_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
