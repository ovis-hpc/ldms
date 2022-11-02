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
#ifndef __PAPI_SAMPLER__
#define __PAPI_SAMPLER__

#include <ovis_log/ovis_log.h>

enum job_state {
	JOB_PAPI_IDLE,
	JOB_PAPI_INIT,
	JOB_PAPI_RUNNING,
	JOB_PAPI_STOPPING,
	JOB_PAPI_ERROR,
	JOB_PAPI_COMPLETE
};

typedef struct papi_event {
	int event_code;
	char *event_name;
	int mid;
	PAPI_event_info_t event_info;
	TAILQ_ENTRY(papi_event) entry;
} *papi_event_t;

typedef struct job_task {
	pid_t pid;
	int rank;
	int event_set;
	int papi_init;
	int papi_start;
	LIST_ENTRY(job_task) entry;
} *job_task_t;

typedef struct job_data {
	base_data_t base;
	char *schema_name;
	char *instance_name;
	uint64_t component_id;
	ldms_set_t set;

	int comp_id_mid;
	int job_id_mid;
	int app_id_mid;
	int job_state_mid;
	int job_start_mid;
	int job_end_mid;
	int task_count_mid;
	int task_pids_mid;
	int task_ranks_mid;

	struct job_key {
		uint64_t job_id;
		uint64_t step_id;
	} key;
	enum job_state job_state;
	uint64_t job_state_time; /* Last time the state was changed */
	uint64_t job_start;
	uint64_t job_end;

	/* Local tasks in this job */
	int task_count;

	/* Task_init events processed */
	int task_init_count;

	/* Tasks in this job */
	LIST_HEAD(,job_task) task_list;

	/* Events sampled in each task */
	int papi_init;
	TAILQ_HEAD(,papi_event) event_list;

	struct rbn job_ent;
	LIST_ENTRY(job_data) expiry_entry;
	LIST_ENTRY(job_data) delete_entry;
} *job_data_t;

extern base_data_t papi_base;
int papi_process_config_data(job_data_t job, char *buf, size_t buflen, ovis_log_t logger);
int papi_process_config_file(job_data_t job, const char *path, ovis_log_t logger);

#endif
