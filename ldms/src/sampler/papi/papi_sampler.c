/* -*- c-basic-offset: 8 -*-
 *
 * Copyright (c) 2019,2023 Open Grid Computing, Inc. All rights reserved.
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
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <assert.h>
#include <papi.h>
#include <ovis_json/ovis_json.h>
#include <ovis_log/ovis_log.h>
#include "ldms.h"
#include "ldmsd.h"
#include "../sampler_base.h"
#include "papi_sampler.h"
#include "papi_hook.h"

#define SAMP "papi_sampler"

static ovis_log_t mylog;
static char *papi_stream_name;
base_data_t papi_base = NULL;
ldms_stream_client_t stream_client = NULL;

pthread_mutex_t job_lock = PTHREAD_MUTEX_INITIALIZER;
struct rbt job_tree;		/* indexed by job_key */

#define DEFAULT_JOB_EXPIRY	60
static int papi_job_expiry = DEFAULT_JOB_EXPIRY;
LIST_HEAD(,job_data) job_expiry_list; /* list of jobs awaiting cleanup */

/*
 * Find the job_data record with the specified job_id and step_id
 */
static job_data_t get_job_data(uint64_t job_id, uint64_t step_id)
{
	job_data_t jd = NULL;
	struct job_key key = { job_id, step_id };
	struct rbn *rbn;
	rbn = rbt_find(&job_tree, &key);
	if (rbn)
		jd = container_of(rbn, struct job_data, job_ent);
	return jd;
}

/*
 * Allocate a job_data slot for the specified job_id.
 *
 * The slot table is consulted for the next available slot.
 */
static job_data_t alloc_job_data(uint64_t job_id, uint64_t step_id)
{
	job_data_t jd;

	jd = calloc(1, sizeof *jd);
	if (jd) {
		jd->base = papi_base;
		jd->key.job_id = job_id;
		jd->key.step_id = step_id;
		jd->job_state = JOB_PAPI_IDLE;
		jd->job_state_time = time(NULL);
		jd->task_init_count = 0;

		TAILQ_INIT(&jd->event_list);
		LIST_INIT(&jd->task_list);

		rbn_init(&jd->job_ent, &jd->key);
		rbt_ins(&job_tree, &jd->job_ent);
	}
	return jd;
}

static void release_job_data(job_data_t jd)
{
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
	rbt_del(&job_tree, &jd->job_ent);
	if (jd->job_end == 0) {
		jd->job_end = jd->job_state_time;
	}
	LIST_INSERT_HEAD(&job_expiry_list, jd, expiry_entry);
}

static void free_job_data(job_data_t jd)
{
	papi_event_t ev;
	job_task_t t;

	if (jd->set) {
		ldmsd_set_deregister(jd->instance_name, SAMP);
		ldms_set_unpublish(jd->set);
		ldms_set_delete(jd->set);
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

	if (jd->schema_name)
		free(jd->schema_name);
	if (jd->instance_name)
		free(jd->instance_name);
	free(jd);
}

static void *cleanup_proc(void *arg)
{
	time_t now;
	job_data_t job;
	LIST_HEAD(,job_data) delete_list;
	LIST_INIT(&delete_list);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	int sleep_time = papi_job_expiry / 10;
	if (!sleep_time)
		sleep_time = 1;
	while (1) {
		sleep(sleep_time);
		now = time(NULL);
		pthread_mutex_lock(&job_lock);
		LIST_FOREACH(job, &job_expiry_list, expiry_entry) {
			assert(job->job_end);
			if ((now - job->job_end) > papi_job_expiry) {
				fprintf(stderr,
					"now %ld job_end %ld dur %ld\n",
					now, job->job_end, now - job->job_end);
				LIST_INSERT_HEAD(&delete_list, job, delete_entry);
			}
		}
		while (!LIST_EMPTY(&delete_list)) {
			job = LIST_FIRST(&delete_list);
			if (job->instance_name) {
				ovis_log(mylog, OVIS_LINFO,
				       "papi_sampler [%d]: deleting instance '%s', "
				       "set_id %ld.\n",
				       __LINE__, job->instance_name,
				       (job->set ? ldms_set_id(job->set) : -1));
			}
			LIST_REMOVE(job, expiry_entry);
			LIST_REMOVE(job, delete_entry);
			free_job_data(job);
		}
		pthread_mutex_unlock(&job_lock);
	}
	return NULL;
}

static int create_metric_set(job_data_t job)
{
	ldms_schema_t schema;
	int i;
	job_task_t t;

	schema = ldms_schema_new(job->schema_name);
	if (schema == NULL) {
		job->job_state = JOB_PAPI_ERROR;
		job->job_state_time = time(NULL);
		return ENOMEM;
	}

	/* component_id */
	job->comp_id_mid = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (job->comp_id_mid < 0)
		goto err;
	/* job_id */
	job->job_id_mid = ldms_schema_meta_add(schema, "job_id", LDMS_V_U64);
	if (job->job_id_mid < 0)
		goto err;
	/* app_id */
	job->app_id_mid = ldms_schema_meta_add(schema, "app_id", LDMS_V_U64);
	if (job->app_id_mid < 0)
		goto err;
	/* job_state */
	job->job_state_mid =
		ldms_schema_metric_add(schema, "job_state", LDMS_V_U8);
	if (job->job_state_mid < 0)
		goto err;
	/* job_start */
	job->job_start_mid =
		ldms_schema_meta_add(schema, "job_start", LDMS_V_U32);
	if (job->job_start_mid < 0)
		goto err;
	/* job_end */
	job->job_end_mid =
		ldms_schema_metric_add(schema, "job_end", LDMS_V_U32);
	if (job->job_end_mid < 0)
		goto err;
	/* task_count */
	job->task_count_mid =
		ldms_schema_meta_add(schema, "task_count", LDMS_V_U32);
	if (job->task_count_mid < 0)
		goto err;

	/* task_pid */
	job->task_pids_mid =
		ldms_schema_meta_array_add(schema, "task_pids",
					   LDMS_V_U32_ARRAY,
					   job->task_count);
	if (job->task_pids_mid < 0)
		goto err;

	/* task_pid */
	job->task_ranks_mid =
		ldms_schema_meta_array_add(schema, "task_ranks",
					   LDMS_V_U32_ARRAY,
					   job->task_count);
	if (job->task_ranks_mid < 0)
		goto err;

	/* events */
	papi_event_t ev;
	TAILQ_FOREACH(ev, &job->event_list, entry) {
		ev->mid = ldms_schema_metric_array_add(schema,
						       ev->event_name,
						       LDMS_V_S64_ARRAY,
						       job->task_count);
		if (ev->mid < 0)
			goto err;
	}

	job->instance_name = malloc(256);
	snprintf(job->instance_name, 256, "%s/%s/%lu.%lu",
		 job->base->producer_name, job->schema_name,
		 job->key.job_id, job->key.step_id);
	job->set = ldms_set_new(job->instance_name, schema);
	if (!job->set)
		goto err;

	base_auth_set(&job->base->auth, job->set);
	ldms_set_producer_name_set(job->set, job->base->producer_name);
	ldms_metric_set_u64(job->set, job->job_id_mid, job->key.job_id);
	ldms_metric_set_u64(job->set, job->comp_id_mid, job->base->component_id);
	ldms_metric_set_u64(job->set, job->app_id_mid, job->key.step_id);
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
	ldmsd_set_register(job->set, SAMP);
	ldms_schema_delete(schema);
	return 0;
 err:
	ovis_log(mylog, OVIS_LERROR,
	       "papi_sampler [%d]: Error %d creating the metric set '%s'.\n",
	       __LINE__, errno, job->instance_name);
	if (schema)
		ldms_schema_delete(schema);
	return errno;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=papi_sampler producer=<producer_name> instance=<instance_name>\n"
		"         [stream=<stream_name>] [component_id=<component_id>] [perm=<permissions>]\n"
                "         [uid=<user_name>] [gid=<group_name>]\n"
                "         [job_expiry=<seconds>]\n"
		"     producer      A unique name for the host providing the data\n"
		"     instance      A unique name for the metric set\n"
		"     stream        A stream name to subscribe to. Defaults to 'slurm'\n"
		"     component_id  A unique number for the component being monitored. Defaults to zero.\n"
		"     job_expiry    Number of seconds to retain sets for completed jobs. Defaults to 60s\n";
}

static void sample_job(job_data_t job)
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
			ovis_log(mylog, OVIS_LERROR, "papi_sampler [%d]: PAPI error '%s' "
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

static int sample(struct ldmsd_sampler *self)
{
	job_data_t job;
	struct rbn *rbn;
	uint64_t now = time(NULL);
	LIST_HEAD(,job_data) delete_list;
	LIST_INIT(&delete_list);

	pthread_mutex_lock(&job_lock);
	RBT_FOREACH(rbn, &job_tree) {
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
			if (now - job->job_state_time > (2 * papi_job_expiry))
				LIST_INSERT_HEAD(&delete_list, job, delete_entry);
			break;
		}
	}
	while (!LIST_EMPTY(&delete_list)) {
		job = LIST_FIRST(&delete_list);
		ovis_log(mylog, OVIS_LINFO,
		       "papi_sampler [%d]: forcing cleanup of instance '%s', "
		       "set %p, set_id %ld.\n",
		       __LINE__, job->instance_name, job->set,
		       ldms_set_id(job->set));
		LIST_REMOVE(job, delete_entry);
		release_job_data(job);
	}
	if (rbt_empty(&job_tree)) {
		/* resume syspapi when no job is running */
		exec_task_empty_hook();
	}
	pthread_mutex_unlock(&job_lock);
	return 0;
}

static int handle_step_init(job_data_t job, uint64_t job_id, uint64_t app_id, json_entity_t e)
{
	int rc;
	int local_tasks;
	uint64_t job_start;
	json_entity_t attr, data, dict;

	attr = json_attr_find(e, "timestamp");
	if (!attr) {
		ovis_log(mylog, OVIS_LERROR, "Missing 'timestamp' attribute "
				"in 'step_init' event.\n");
		return EINVAL;
	}
	job_start = json_value_int(json_attr_value(attr));

	data = json_attr_find(e, "data");
	if (!data) {
		ovis_log(mylog, OVIS_LERROR, "Missing 'data' attribute "
		       "in 'init' event.\n");
		return EINVAL;
	}

	dict = json_attr_value(data);
	attr = json_attr_find(dict, "local_tasks");
	if (attr) {
		local_tasks = json_value_int(json_attr_value(attr));
	} else {
		ovis_log(mylog, OVIS_LERROR, "Missing 'local_tasks' attribute "
		       "in 'init' event.\n");
		return EINVAL;
	}

	/* Parse the the subscriber_data attribute*/
	attr = json_attr_find(dict, "subscriber_data");
	if (!attr) {
		ovis_log(mylog, OVIS_LERROR,
		       "papi_sampler[%d]: subscriber_data missing from init message, job %ld ignored.\n",
		       __LINE__, job_id);
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
		ovis_log(mylog, OVIS_LINFO,
		       "papi_sampler[%d]: subscriber_data is not a dictionary, job %ld ignored.\n",
		       __LINE__, job_id);
		rc = EINVAL;
		goto out;
	}
	attr = json_attr_find(subs_data, "papi_sampler");
	if (!attr)  {
		ovis_log(mylog, OVIS_LINFO,
		       "papi_sampler[%d]: subscriber_data missing papi_sampler attribute, job is ignored.\n",
		       __LINE__);
		rc = EINVAL;
		goto out;
	}
	dict = json_attr_value(attr);

	json_entity_t file_attr, config_attr;
	file_attr = json_attr_find(dict, "file");
	config_attr = json_attr_find(dict, "config");
	if (!file_attr && !config_attr) {
		ovis_log(mylog, OVIS_LERROR,
		       "papi_sampler[%d]: papi_config object must contain "
		       "either the 'file' or 'config' attribute.\n",
		       __LINE__);
		rc = EINVAL;
		goto out;
	}
	if (job) {
		ovis_log(mylog, OVIS_LERROR,
				"papi_sampler[%d]: Duplicate step initialization for %lu.%lu",
				__LINE__, job_id, app_id);
		rc = EINVAL;
		goto out;
	}
	job = alloc_job_data(job_id, app_id);
	if (!job) {
		ovis_log(mylog, OVIS_LERROR,
				"papi_sampler[%d]: Memory allocation failure, job ignored.\n",
				__LINE__);
		rc = ENOMEM;
		goto out;
	}
	if (file_attr) {
		json_entity_t file_name = json_attr_value(file_attr);
		if (json_entity_type(file_name) != JSON_STRING_VALUE) {
			ovis_log(mylog, OVIS_LERROR,
			       "papi_sampler[%ld]: papi_config 'file' attribute "
			       "must be a string, job %d ignored.",
			       job_id, __LINE__);
			rc = EINVAL;
			goto out;
		}
		rc = papi_process_config_file(job, json_value_str(file_name)->str, mylog);
		if (rc) {
			switch (rc) {
			case ENOENT:
				ovis_log(mylog, OVIS_LERROR,
				       "The configuration "
				       "file, %s, does not exist.\n",
				       json_value_str(file_name)->str);
				break;
			case EPERM:
				ovis_log(mylog, OVIS_LERROR,
				       "Permission denied "
				       "processing the %s configuration file\n",
				       json_value_str(file_name)->str);
				break;
			default:
				ovis_log(mylog, OVIS_LERROR,
				       "Error %d processing "
				       "the %s configuration file\n",
				       rc, json_value_str(file_name)->str);

				break;
			}
			goto out;
		}
	} else {
		json_entity_t config_string = json_attr_value(config_attr);
		if (json_entity_type(config_string) != JSON_STRING_VALUE) {
			ovis_log(mylog, OVIS_LERROR,
			       "papi_config 'config' "
			       "attribute must be a string, job %ld ignored.",
			       job_id);
			rc = EINVAL;
			goto out;
		}
		rc = papi_process_config_data(job,
					      json_value_str(config_string)->str,
					      json_value_str(config_string)->str_len,
					      mylog);
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

static void handle_papi_error(job_data_t job)
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

static void handle_task_init(job_data_t job, json_entity_t e)
{
	json_entity_t attr;
	json_entity_t data, dict;
	job_task_t t;
	int rc, task_pid, task_rank;
	papi_event_t ev;

	if (job->job_state != JOB_PAPI_INIT) {
		/* Error during startup */
		job->job_state = JOB_PAPI_ERROR;
		release_job_data(job);
		return;
	}
	data = json_attr_find(e, "data");
	if (!data) {
		ovis_log(mylog, OVIS_LERROR, "Missing 'data' attribute "
		       "in 'task_init' event.\n");
		return;
	}
	dict = json_attr_value(data);

	attr = json_attr_find(dict, "task_pid");
	if (!attr) {
		ovis_log(mylog, OVIS_LERROR, "Missing 'task_pid' attribute "
		       "in 'task_init' event.\n");
		return;
	}
	task_pid = json_value_int(json_attr_value(attr));

	attr = json_attr_find(dict, "task_global_id");
	if (!attr) {
		ovis_log(mylog, OVIS_LERROR, "Missing 'task_global_id' attribute "
		       "in 'task_init' event.\n");
		return;
	}
	task_rank = json_value_int(json_attr_value(attr));

	t = malloc(sizeof *t);
	if (!t) {
		ovis_log(mylog, OVIS_LERROR,
		       "papi_sampler[%d]: Memory allocation failure.\n",
		       __LINE__);
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

	if (create_metric_set(job)) {
		release_job_data(job);
		return;
	}

	/* pause syspapi */
	exec_task_init_hook();

	LIST_FOREACH(t, &job->task_list, entry) {
		rc = PAPI_create_eventset(&t->event_set);
		if (rc != PAPI_OK) {
			ovis_log(mylog, OVIS_LERROR, "papi_sampler [%d]: PAPI error '%s' "
			       "creating EventSet.\n", __LINE__, PAPI_strerror(rc));
			job->job_state = JOB_PAPI_ERROR;
			job->job_state_time = time(NULL);
			goto err;
		}
		rc = PAPI_assign_eventset_component(t->event_set, 0);
		if (rc != PAPI_OK) {
			ovis_log(mylog, OVIS_LERROR, "papi_sampler [%d]: PAPI error '%s' "
			       "assign EventSet to CPU.\n", __LINE__, PAPI_strerror(rc));
			job->job_state = JOB_PAPI_ERROR;
			job->job_state_time = time(NULL);
			goto err;
		}
		rc = PAPI_set_multiplex(t->event_set);
		if (rc != PAPI_OK) {
			ovis_log(mylog, OVIS_LERROR, "papi_sampler [%d]: PAPI error '%s' "
			       "setting multiplex.\n", __LINE__, PAPI_strerror(rc));
			job->job_state = JOB_PAPI_ERROR;
			job->job_state_time = time(NULL);
			goto err;
		}
		t->papi_init = 1;
		TAILQ_FOREACH(ev, &job->event_list, entry) {
			rc = PAPI_add_event(t->event_set, ev->event_code);
			if (rc != PAPI_OK) {
				ovis_log(mylog, OVIS_LERROR, "papi_sampler [%d]: PAPI error '%s' "
				       "adding event '%s'.\n", __LINE__, PAPI_strerror(rc),
				       ev->event_name);
				job->job_state = JOB_PAPI_ERROR;
				job->job_state_time = time(NULL);
				goto err;
			}
		}
		rc = PAPI_attach(t->event_set, t->pid);
		if (rc != PAPI_OK) {
			ovis_log(mylog, OVIS_LERROR, "papi_sampler [%d]: PAPI error '%s' "
			       "attaching EventSet to pid %d.\n", __LINE__,
			       PAPI_strerror(rc), t->pid);
			job->job_state = JOB_PAPI_ERROR;
			job->job_state_time = time(NULL);
			goto err;
		}
	}
	LIST_FOREACH(t, &job->task_list, entry) {
		rc = PAPI_start(t->event_set);
		if (rc != PAPI_OK) {
			ovis_log(mylog, OVIS_LERROR, "papi_sampler [%d]: PAPI error '%s' "
			       "starting EventSet for pid %d.\n", __LINE__,
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

static void handle_task_exit(job_data_t job, json_entity_t e)
{
	long long values[64];
	json_entity_t attr;
	json_entity_t data = json_attr_find(e, "data");
	json_entity_t dict = json_attr_value(data);
	int rc;
	int task_pid;
	job_task_t t;

	if (job->job_state != JOB_PAPI_RUNNING && job->job_state != JOB_PAPI_STOPPING) {
		/* Error during startup */
		job->job_state = JOB_PAPI_ERROR;
		release_job_data(job);
		return;
	}

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
			ovis_log(mylog, OVIS_LERROR, "papi_sampler [%d]: PAPI error '%s' "
			       "stopping EventSet for pid %d.\n", __LINE__,
			       PAPI_strerror(rc), t->pid);
		}
		t->papi_start = 0;
		rc = PAPI_detach(t->event_set);
		if (rc != PAPI_OK) {
			ovis_log(mylog, OVIS_LERROR, "papi_sampler[%d]: Error '%s' de-attaching from "
			       "process pid= %d. rc= %d\n", __LINE__,
			       PAPI_strerror(rc), t->pid, rc);
		}
		PAPI_cleanup_eventset(t->event_set);
		PAPI_destroy_eventset(&t->event_set);
		break;
	}
}

static void handle_job_exit(job_data_t job, json_entity_t e)
{
	json_entity_t attr = json_attr_find(e, "timestamp");
	uint64_t timestamp = json_value_int(json_attr_value(attr));

	job->job_state = JOB_PAPI_COMPLETE;
	/* job_lock is held, call chain: stream_recv_cb
	 *                               -> handle_job_exit */
	if (job->set) {
		/* set is not guaranteed to exist, e.g. init may failed or job
		 * exited (canceled) even before the task_init_priv event */
		ldms_transaction_begin(job->set);
		ldms_metric_set_u8(job->set, job->job_state_mid, job->job_state);
		ldms_transaction_end(job->set);
	}
	job->job_state_time = time(NULL);
	job->job_end = timestamp;
	release_job_data(job);
}

static int stream_recv_cb(ldms_stream_type_t stream_type,
			  const char *msg, size_t msg_len,
			  json_entity_t entity)
{
	int rc = 0;
	json_entity_t event, data, dict, attr;

	if (stream_type != LDMS_STREAM_JSON) {
		ovis_log(mylog, OVIS_LDEBUG, "papi_sampler: Unexpected stream type data...ignoring\n");
		ovis_log(mylog, OVIS_LDEBUG, "papi_sampler:" "%s\n", msg);
		return EINVAL;
	}

	event = json_attr_find(entity, "event");
	if (!event) {
		ovis_log(mylog, OVIS_LERROR, "'event' attribute missing\n");
		goto out_0;
	}

	json_str_t event_name = json_value_str(json_attr_value(event));
	data = json_attr_find(entity, "data");
	if (!data) {
		ovis_log(mylog, OVIS_LERROR, "'%s' event is missing "
		       "the 'data' attribute\n", event_name->str);
		goto out_0;
	}
	dict = json_attr_value(data);
	attr = json_attr_find(dict, "job_id");
	if (!attr) {
		ovis_log(mylog, OVIS_LERROR, "The event is missing the "
		       "'job_id' attribute.\n");
		goto out_0;
	}

	uint64_t job_id = json_value_int(json_attr_value(attr));
	uint64_t step_id = 0;

	attr = json_attr_find(dict, "step_id");
	if (attr)
		step_id = json_value_int(json_attr_value(attr));

	job_data_t job;

	pthread_mutex_lock(&job_lock);
	job = get_job_data(job_id, step_id);
	if (0 == strncmp(event_name->str, "step_init",9)) {
		rc = handle_step_init(job, job_id, step_id, entity);
	} else if (0 == strncmp(event_name->str, "task_init_priv", 14)) {
		if (job)
			handle_task_init(job, entity);
	} else if (0 == strncmp(event_name->str, "task_exit", 9)) {
		if (job)
			handle_task_exit(job, entity);
	} else if (0 == strncmp(event_name->str, "exit", 4)) {
		if (job)
			handle_job_exit(job, entity);
	} else {
		ovis_log(mylog, OVIS_LDEBUG,
		       "ignoring event '%s'\n", event_name->str);
	}
 	pthread_mutex_unlock(&job_lock);
 out_0:
	return rc;
}

static int stream_cb(ldms_stream_event_t ev, void *arg)
{
	if (ev->type != LDMS_STREAM_EVENT_RECV)
		return 0;
	return stream_recv_cb(ev->recv.type, ev->recv.data, ev->recv.data_len,
			      ev->recv.json);
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;

	if (papi_base) {
		ovis_log(mylog, OVIS_LERROR, "papi_sampler config: already configured.\n");
		return EEXIST;
	}

	value = av_value(avl, "job_expiry");
	if (value)
		papi_job_expiry = strtol(value, NULL, 0);

	papi_base = base_config(avl, self->inst_name, "papi-events", mylog);
	if (!papi_base)
		return errno;
	value = av_value(avl, "stream");
	if (!value) {
		papi_stream_name = "slurm";
	} else {
		papi_stream_name = strdup(value);
		if (!papi_stream_name) {
			ovis_log(mylog, OVIS_LERROR, "papi_sampler[%d]: Memory allocation failure.\n", __LINE__);
			base_del(papi_base);
			papi_base = NULL;
			return EINVAL;
		}
	}
	stream_client =  ldms_stream_subscribe(papi_stream_name, 0, stream_cb, self, "papi_sampler");
	if (!stream_client) {
		ovis_log(mylog, OVIS_LERROR, "papi_sampler[%d]: Error %d attempting "
		       "subscribe to the '%s' stream.\n",
		       __LINE__, errno, papi_stream_name);
	}
	return errno;
}

static void term(struct ldmsd_plugin *self)
{
	if (papi_base) {
		base_del(papi_base);
		papi_base = NULL;
	}
	if (stream_client) {
		ldms_stream_close(stream_client);
		stream_client = NULL;
	}
	if (mylog)
		ovis_log_destroy(mylog);
}

static struct ldmsd_sampler papi_sampler = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.sample = sample
};

struct ldmsd_plugin *get_plugin()
{
	int rc;
	mylog = ovis_log_register("sampler."SAMP, "Message for the " SAMP " plugin");
	if (!mylog) {
		rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the log subsystem "
					"of '" SAMP "' plugin. Error %d\n", rc);
	}
	return &papi_sampler.base;
}

static int cmp_job_key(void *a, const void *b)
{
	struct job_key *a_ = (struct job_key *)a;
	struct job_key *b_ = (struct job_key *)b;
	if (a_->job_id < b_->job_id)
		return -1;
	if (a_->job_id > b_->job_id)
		return 1;
	if (a_->step_id < b_->step_id)
		return -1;
	if (a_->step_id > b_->step_id)
		return 1;
	return 0;
}

static void __attribute__ ((constructor)) papi_sampler_init(void)
{
	pthread_t cleanup_thread;
	int rc = PAPI_library_init(PAPI_VER_CURRENT);
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, "Error %d attempting to "
			     "initialize the PAPI library.\n", rc);
	}
	PAPI_thread_init(pthread_self);
	rbt_init(&job_tree, cmp_job_key);
	LIST_INIT(&job_expiry_list);

	(void)pthread_create(&cleanup_thread, NULL, cleanup_proc, NULL);
}

static void __attribute__ ((destructor)) papi_sampler_term(void)
{
	PAPI_shutdown();
}
