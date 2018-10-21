/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file mpi_profiler.c
 * \brief Routines to manage the profiler
 */
#include <pthread.h>
#include "mpi.h"
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "../shm_util/ldms_shm_index.h"

#include "mpi_profiler.h"
#include "mpi_profiler_configuration.h"

#ifndef MPI3CONST
#if MPI_VERSION >= 3
#define MPI3CONST const
#else
#define MPI3CONST
#endif
#endif

static const int DEFAULT_NUMBER_OF_INDEX_ENTRIES = 4;
static const int ARRAY_MAX_DEFAULT = 1024;
static const int METRIC_MAX_DEFAULT = 1024;
static const int DEFAULT_SHM_TIMEOUT = 10;
static const char* SHM_SET_PREFIX_DEFAULT = "MPI_Metrics";
static int should_update_shm = 0;

static void (*shm_inc)(ldms_shm_set_t, int);

static void (*shm_add)(ldms_shm_set_t, int, int);

static unsigned int update_interval;
static pthread_t ldms_shm_updater_thread;
static ldms_shm_data_t *event_counters_thread_buffer = NULL;
static ldms_shm_data_t *event_counters_last_update = NULL;
static ldms_shm_data_t *event_counters_diff_from_last_update = NULL;

int log_level_info()
{
	return (profile_log_level >= LDMS_SHM_LOG_LINFO);
}

static void clean_shared_resources()
{
	if(log_level_info())
		printf("INFO: cleaning shared_resources\n\r");
	int remaining_users = ldms_shm_set_deregister_writer(profiler->shm_set);
	if(remaining_users == 0) { /* This is the last process, no readers or writers */
		/* Nobody uses this set. It's time for cleanup */
		ldms_shm_set_clean_entry_shared_resources(profiler->shm_set);
	}
}

static void clean_local_resources()
{
	if(NULL == profiler)
		return;
	if(log_level_info())
		printf("INFO: cleaning local_resources\n\r");
	if(NULL != profiler->conf) {
		free(profiler->conf);
		profiler->conf = NULL;
	}
	if(NULL != profiler->events) {
		int i;
		for(i = 0; i < profiler->num_base_events_with_types; i++) {
			if(NULL == profiler->events[i])
				continue;
			profiler->events[i]->base = NULL;
			if(NULL != profiler->events[i]->spec) {
				int j;
				for(j = 0; j < profiler->events[i]->spec_size;
						j++) {
					if(NULL == profiler->events[i]->spec[j])
						continue;
					if(NULL
							!= profiler->events[i]->spec[j]->desc)
						free(
								profiler->events[i]->spec[j]->desc);
					if(NULL
							!= profiler->events[i]->spec[j]->inc_mpi_types)
						free(
								profiler->events[i]->spec[j]->inc_mpi_types);
					if(NULL
							!= profiler->events[i]->spec[j]->exc_mpi_types)
						free(
								profiler->events[i]->spec[j]->exc_mpi_types);
					if(NULL
							!= profiler->events[i]->spec[j]->stat) {

						int k;
						for(k = 0;
								k
										< profiler->num_events_per_func;
								k++) {
							if(NULL
									!= profiler->events[i]->spec[j]->stat[k].desc)
								free(
										profiler->events[i]->spec[j]->stat[k].desc);
						}
						free(
								profiler->events[i]->spec[j]->stat);
					}
					free(profiler->events[i]->spec[j]);
				}
				free(profiler->events[i]->spec);
			}
			free(profiler->events[i]);
		}
		free(profiler->events);
	}

	if(NULL != profiler->shm_set) {
		ldms_shm_clear_set(profiler->shm_set);
		free(profiler->shm_set);
		profiler->shm_set = NULL;
	}
	if(NULL != profiler->shm_index) {
		ldms_shm_clear_index(profiler->shm_index);
		free(profiler->shm_index);
		profiler->shm_index = NULL;
	}
	if(NULL != profiler->ldms_shm_set_fslocation) {
		free(profiler->ldms_shm_set_fslocation);
		profiler->ldms_shm_set_fslocation = NULL;
	}
	if(NULL != profiler->ldms_shm_set_label) {
		free(profiler->ldms_shm_set_label);
		profiler->ldms_shm_set_label = NULL;
	}
}

static int clean_updater_thread()
{
	int rc = 0;
	if(ldms_shm_updater_thread) {
		if(log_level_info())
			printf("INFO: cleaning updater_thread\n\r");
		should_update_shm = 0;

		rc = pthread_cancel(ldms_shm_updater_thread);
		if(rc) {
			printf("ERROR: in canceling the updater thread\n\r");
			return rc;
		}
		rc = pthread_join(ldms_shm_updater_thread, NULL);
		if(rc) {
			printf("ERROR: in cleaning the updater thread\n\r");
			return rc;
		}

		/* last update */
		if(LDMS_SHM_MPI_STAT_LOCAL == profiler->conf->scope) {
			ldms_shm_counter_group_assign(profiler->shm_set,
					profiler->event_index_map,
					profiler->local_mpi_event_counters,
					profiler->num_base_events_with_types);
			if(log_level_info())
				printf(
						"INFO: ldms_shm_updater_thread (%d) last call updated %d events in shm_set\n\r",
						profiler->rankid,
						profiler->num_base_events_with_types);
		} else {
			int i;
			for(i = 0; i < profiler->num_base_events_with_types;
					i++) {
				event_counters_diff_from_last_update[i].val =
						profiler->local_mpi_event_counters[i].val
								- event_counters_last_update[i].val;
			}
			ldms_shm_counter_group_add_atomic(profiler->shm_set,
					profiler->event_index_map,
					event_counters_diff_from_last_update,
					profiler->num_base_events_with_types);
		}
		free(event_counters_thread_buffer);
		free(event_counters_last_update);
		free(event_counters_diff_from_last_update);
		free(profiler->local_mpi_event_counters);
		free(profiler->event_index_map);
		profiler->local_mpi_event_counters = NULL;
		profiler->event_index_map = NULL;
	}
	return rc;
}

static void clean_events_desc(char **events_desc)
{
	if(NULL == events_desc)
		return;
	int i;
	for(i = 0; i < profiler->num_base_events_with_types; i++) {
		if(NULL != events_desc[i])
			free(events_desc[i]);
	}
	free(events_desc);
}

static void clean_local_after_init(char **events_desc,
		int *num_elements_per_event)
{
	clean_events_desc(events_desc);
	if(NULL != num_elements_per_event)
		free(num_elements_per_event);
}

static void clean_profiler()
{
	if(log_level_info())
		printf("INFO: %d: cleaning profiler\n\r", profiler->rankid);
	clean_shared_resources();
	clean_updater_thread();
	clean_local_resources();
}

static void ldms_shm_mpi_Exit_handler(void)
{
	int flag;
	MPI_Finalized(&flag);
	if(!flag && profile_log_level) {
		clean_profiler();
	}
}

static inline void print_event(ldms_shm_MPI_func_id_t func_id, int stat_index)
{
	int s = 0;
	do {
		printf("INFO: taskid=%d, event=%s, counter=%" PRIu64 "\n\r",
				profiler->rankid,
				profiler->events[func_id]->spec[s]->stat[stat_index].desc,
				profiler->events[func_id]->spec[s]->stat[stat_index].counter);
		s++;
	} while(s < profiler->events[func_id]->spec_size);

}

static inline void disable_function(ldms_shm_MPI_func_id_t func_id)
{
	profiler->ldms_shm_mpi_base_events[func_id].enabled = 0;
}

static void init_base_events()
{
	ldms_shm_mpi_init_func_names(profiler->ldms_shm_mpi_base_events);
	int func_id;
	for(func_id = 0; func_id < LDMS_SHM_MPI_NUM_FUNCTIONS; func_id++) {
		profiler->ldms_shm_mpi_base_events[func_id].func_id = func_id;
		disable_function(func_id);
	}
}

void post_Pcontrol_call(const int flag)
{
	if(flag == 0) {
		printf("profiling has been disabled!\n\r");
		profile_log_level = LDMS_SHM_PROFILING_DISABLED;
	}
}

void *ldms_shm_periodic_update()
{
	if(log_level_info())
		printf("INFO: ldms_shm_updater_thread (%d): starting!\n\r",
				profiler->rankid);
	while(should_update_shm) {

		if(profiler->conf->scope == LDMS_SHM_MPI_STAT_LOCAL) {
			ldms_shm_counter_group_assign(profiler->shm_set,
					profiler->event_index_map,
					profiler->local_mpi_event_counters,
					profiler->num_base_events_with_types);
			if(log_level_info())
				printf(
						"INFO ldms_shm_updater_thread (%d): updated %d events in shm_set\n\r",
						profiler->rankid,
						profiler->num_base_events_with_types);
		} else {
			int i;
			if(!event_counters_thread_buffer) {
				event_counters_thread_buffer =
						calloc(
								profiler->num_base_events_with_types,
								sizeof(ldms_shm_data_t));
				event_counters_last_update =
						calloc(
								profiler->num_base_events_with_types,
								sizeof(ldms_shm_data_t));
				event_counters_diff_from_last_update =
						calloc(
								profiler->num_base_events_with_types,
								sizeof(ldms_shm_data_t));
			}

			for(i = 0; i < profiler->num_base_events_with_types;
					i++) {
				event_counters_thread_buffer[i].val =
						profiler->local_mpi_event_counters[i].val;
				event_counters_diff_from_last_update[i].val =
						event_counters_thread_buffer[i].val
								- event_counters_last_update[i].val;
			}

			ldms_shm_counter_group_add_atomic(profiler->shm_set,
					profiler->event_index_map,
					event_counters_diff_from_last_update,
					profiler->num_base_events_with_types);

			for(i = 0; i < profiler->num_base_events_with_types;
					i++) {
				event_counters_last_update[i].val =
						event_counters_thread_buffer[i].val;
			}
		}

		usleep(update_interval);
	}
	if(log_level_info())
		printf("INFO: ldms_shm_updater_thread (%d): exiting!\n\r",
				profiler->rankid);
	pthread_exit(NULL);
}

static int config_updater_thread()
{
	int rc;
	should_update_shm = 1;
	update_interval = LDMS_SHM_MPI_SHM_UPDATE_INTERVAL_DEFAULT;
	char* update_interval_str = getenv(
			LDMS_SHM_MPI_SHM_UPDATE_INTERVAL_ENV_VAR_NAME);
	if(!update_interval_str) {
		if(log_level_info())
			printf(
					"INFO: The environment variable: \"%s\" is empty. Setting to default %d ...\n\r",
					LDMS_SHM_MPI_SHM_UPDATE_INTERVAL_ENV_VAR_NAME,
					LDMS_SHM_MPI_SHM_UPDATE_INTERVAL_DEFAULT);
	} else {
		update_interval = atoi(update_interval_str);
	}
	rc = pthread_create(&ldms_shm_updater_thread, NULL,
			ldms_shm_periodic_update, NULL);
	return rc;
}

static int config_profiler()
{
	int rc = 0;
	profiler->conf = calloc(1,
			sizeof(struct ldms_shm_mpi_profiler_configuration));
	if(NULL == profiler->conf) {
		printf(
				"ERROR: failed to allocate memory for the profiler configuration\n\r");
		rc = ENOMEM;
		return rc;
	}
	profiler->conf->profile_log_level =
			LDMS_SHM_MPI_PROFILER_LOG_LEVEL_DEFAULT;
	profiler->conf->scope = LDMS_SHM_MPI_STAT_SCOPE_DEFAULT;

	char* scope_str = getenv(LDMS_SHM_MPI_STAT_SCOPE_ENV_VAR_NAME);
	if(!scope_str) {
		if(log_level_info())
			printf(
					"INFO: The environment variable: \"%s\" is empty. Setting to default %d ...\n\r",
					LDMS_SHM_MPI_STAT_SCOPE_ENV_VAR_NAME,
					LDMS_SHM_MPI_STAT_SCOPE_DEFAULT);
	} else {
		profiler->conf->scope = atoi(scope_str);
	}

	if(LDMS_SHM_MPI_STAT_LOCAL == profiler->conf->scope) {
		shm_inc = &ldms_shm_non_atomic_counter_inc;
		shm_add = &ldms_shm_non_atomic_counter_add;
		profiler->stat_index = profiler->rankid;
	} else {
		shm_inc = &ldms_shm_atomic_counter_inc;
		shm_add = &ldms_shm_atomic_counter_add;
	}

	profiler->conf->event_update_type = LDMS_SHM_MPI_EVENT_UPDATE_SHARED;
	char* event_update_str = getenv(LDMS_SHM_MPI_EVENT_UPDATE_ENV_VAR_NAME);
	if(!event_update_str) {
		if(log_level_info())
			printf(
					"INFO: The environment variable: \"%s\" is empty. Setting to default %d ...\n\r",
					LDMS_SHM_MPI_EVENT_UPDATE_ENV_VAR_NAME,
					LDMS_SHM_MPI_EVENT_UPDATE_SHARED);
	} else {
		profiler->conf->event_update_type = atoi(event_update_str);
	}

	char* mpi_profiler_log_level_str = getenv(
			LDMS_SHM_MPI_PROFILER_LOG_LEVEL_ENV_VAR_NAME);
	if(!mpi_profiler_log_level_str) {
		if(log_level_info())
			printf(
					"INFO: The environment variable: \"%s\" is empty. Setting to default %d ...\n\r",
					LDMS_SHM_MPI_PROFILER_LOG_LEVEL_ENV_VAR_NAME,
					LDMS_SHM_MPI_PROFILER_LOG_LEVEL_DEFAULT);
	} else {
		profiler->conf->profile_log_level = atoi(
				mpi_profiler_log_level_str);
	}
	profile_log_level = profiler->conf->profile_log_level;

	if(LDMS_SHM_MPI_EVENT_UPDATE_LOCAL
			== profiler->conf->event_update_type) {
		int rc = config_updater_thread();
		if(rc)
			return rc;
	}
	return rc;
}

static void build_fs_location_name()
{
	char *revised_app_name = strrchr(ldms_shm_mpi_app_name, '/');
	if(revised_app_name != NULL)
		ldms_shm_mpi_app_name = revised_app_name + 1;
	int num_tasks_len = log10(profiler->total_ranks) + 1;
	int num_delimiter_chars = 3;
	int ldms_shm_set_fslocation_len = strlen(ldms_shm_index_name)
			+ strlen(LDMS_SHM_SET_FSLOCATION_PREFIX)
			+ strlen(ldms_shm_mpi_app_name) + num_tasks_len
			+ num_delimiter_chars + 1;
	profiler->ldms_shm_set_fslocation = calloc(ldms_shm_set_fslocation_len,
			sizeof(char));
	snprintf(profiler->ldms_shm_set_fslocation, ldms_shm_set_fslocation_len,
			"%s_%s_%s_%d", LDMS_SHM_SET_FSLOCATION_PREFIX,
			ldms_shm_index_name + 1, ldms_shm_mpi_app_name,
			profiler->total_ranks);
}

static void build_set_label()
{
	int num_tasks_len = log10(profiler->total_ranks) + 1;
	int num_delimiter_chars = 2; /* _ _ */
	int ldms_shm_set_label_len = strlen(SHM_SET_PREFIX_DEFAULT)
			+ strlen(ldms_shm_mpi_app_name) + num_tasks_len
			+ num_delimiter_chars + 1;
	profiler->ldms_shm_set_label = calloc(ldms_shm_set_label_len,
			sizeof(char));
	snprintf(profiler->ldms_shm_set_label, ldms_shm_set_label_len,
			"%s_%s_%d", SHM_SET_PREFIX_DEFAULT,
			ldms_shm_mpi_app_name, profiler->total_ranks);
}

static int init_profiler()
{
	profiler = calloc(1, sizeof(struct ldms_shm_mpi_profiler));
	if(NULL == profiler) {
		printf("ERROR: Failed to allocated memory for the profiler\n");
		return ENOMEM;
	}

	MPI_Comm_rank(MPI_COMM_WORLD, &profiler->rankid);
	MPI_Comm_size(MPI_COMM_WORLD, &profiler->total_ranks);

	profiler->num_events_per_func = 0;
	profiler->num_base_events_with_types = 0;
	profiler->stat_index = 0;
	profiler->local_mpi_event_counters = NULL;
	profiler->event_index_map = NULL;
	profiler->conf = NULL;
	profiler->events = NULL;
	profiler->shm_set = NULL;
	profiler->shm_index = NULL;
	profiler->ldms_shm_set_label = NULL;
	profiler->ldms_shm_set_fslocation = NULL;
	return 0;
}

static void clean_on_init_error(char **events_desc, int *num_elements_per_event)
{
	clean_local_after_init(events_desc, num_elements_per_event);
	clean_profiler();
}

void post_init_call()
{

	int i;
	int rc = init_profiler();
	if(rc) {
		post_Pcontrol_call(0);
		return;
	}
	init_base_events();
	rc = config_profiler();
	if(rc) {
		clean_on_init_error(NULL, NULL);
		post_Pcontrol_call(0);
		return;
	}

	char **events_desc = init_events();
	if(NULL == events_desc) {
		post_Pcontrol_call(0);
		return;
	}
	int *num_elements_per_event = calloc(profiler->num_events_per_func,
			sizeof(int));
	if(NULL == num_elements_per_event) {
		printf("ERROR: Failed to allocate memory!\n");
		clean_on_init_error(events_desc, NULL);
		post_Pcontrol_call(0);
		return;
	}
	for(i = 0; i < profiler->num_base_events_with_types; i++) {
		num_elements_per_event[i] = profiler->num_events_per_func;
	}
	profiler->shm_index = ldms_shm_index_open(ldms_shm_index_name,
			METRIC_MAX_DEFAULT, ARRAY_MAX_DEFAULT,
			DEFAULT_NUMBER_OF_INDEX_ENTRIES, DEFAULT_SHM_TIMEOUT);
	if(profiler->shm_index) {
		if(log_level_info())
			printf("INFO: index (%s) was successfully opened\n\r",
					ldms_shm_index_name);
	} else {
		printf("ERROR: failed to open the index (%s)\n\r",
				ldms_shm_index_name);
		clean_on_init_error(events_desc, num_elements_per_event);
		post_Pcontrol_call(0);
		return;
	}

	build_fs_location_name();
	build_set_label();

	profiler->shm_set = ldms_shm_index_register_set(
			profiler->ldms_shm_set_label,
			profiler->ldms_shm_set_fslocation,
			profiler->num_base_events_with_types,
			num_elements_per_event, events_desc);

	if(profiler->shm_set == NULL) {
		clean_on_init_error(events_desc, num_elements_per_event);
		post_Pcontrol_call(0);
		return;
	} else {
		if(log_level_info() && 0 == profiler->rankid)
			printf(
					"INFO: shm_set with %d events and label \"%s\" has been created successfully at the index entry \"%s\".\n\r",
					profiler->total_num_events,
					profiler->ldms_shm_set_label,
					profiler->ldms_shm_set_fslocation);
	}

	clean_local_after_init(events_desc, num_elements_per_event);
	atexit(ldms_shm_mpi_Exit_handler);
	if(log_level_info())
		printf("INFO: %d: end MPI_Init\n\r", profiler->rankid);
}

int post_finalize_call()
{
	clean_profiler();
	if(log_level_info())
		printf("INFO: %d: end MPI_Finalize\n\r", profiler->rankid);
	return 0;
}

static inline int filter_event(ldms_shm_mpi_event_spec_t spec, int count,
		MPI_Datatype datatype)
{
	int filter = 0;
	if(spec->msg_bin.min == -1 && spec->msg_bin.max == -1) {
		filter = 1;
	} else {
		/* FIXME cache
		 *
		 */
		int size = 0;
		MPI_Type_size(datatype, &size);
		size = size * count;
		if(spec->msg_bin.min != -1 && spec->msg_bin.max != -1) {
			if(size > spec->msg_bin.min && size < spec->msg_bin.max)
				filter = 1;
		} else if(spec->msg_bin.min != -1) {
			if(size > spec->msg_bin.min)
				filter = 1;
		} else {
			if(size < spec->msg_bin.max)
				filter = 1;
		}
	}
	return filter;
}

void post_msg_based_function_call(ldms_shm_MPI_func_id_t func_id, int count,
		MPI_Datatype datatype)
{
	if(is_disabled(func_id))
		return;
	int s = 0, size = 0;
	do {
		if(filter_event(profiler->events[func_id]->spec[s], count,
				datatype) == 1) {
			switch(profiler->events[func_id]->spec[s]->event_type) {

			case LDMS_SHM_MPI_EVENT_FUNC_CALLS:
				switch(profiler->conf->event_update_type) {

				case LDMS_SHM_MPI_EVENT_UPDATE_LOCAL:
					profiler->local_mpi_event_counters[s].val++;
					break;
				case LDMS_SHM_MPI_EVENT_UPDATE_SHARED:
					shm_inc(profiler->shm_set,
							profiler->events[func_id]->spec[s]->stat[profiler->stat_index].event_id);
					break;
				default:
					printf(
							"ERROR: invalid update type\n\r");
					break;
				}
				if(profile_log_level >= LDMS_SHM_LOG_LINFO)
					profiler->events[func_id]->spec[s]->stat[profiler->stat_index].counter++;
				break;
			case LDMS_SHM_MPI_EVENT_ARG_BYTES:
				/* FIXME
				 * cache this (MPI_Type sizes)
				 */
				MPI_Type_size(datatype, &size);
				switch(profiler->conf->event_update_type) {

				case LDMS_SHM_MPI_EVENT_UPDATE_LOCAL:
					profiler->local_mpi_event_counters[s].val +=
							(size * count);
					break;
				case LDMS_SHM_MPI_EVENT_UPDATE_SHARED:
					shm_add(profiler->shm_set,
							profiler->events[func_id]->spec[s]->stat[profiler->stat_index].event_id,
							size * count);
					break;
				default:
					printf(
							"ERROR: invalid update type\n\r");
					break;
				}
				if(profile_log_level >= LDMS_SHM_LOG_LINFO)
					profiler->events[func_id]->spec[s]->stat[profiler->stat_index].counter +=
							(size * count);
				break;
			default:
				printf("ERROR: undefined event type (%d)\n\r",
						profiler->events[func_id]->spec[s]->event_type);
				break;
			}
		}
		s++;
	} while(s < profiler->events[func_id]->spec_size);

	if(profile_log_level >= LDMS_SHM_LOG_LINFO)
		print_event(func_id, profiler->stat_index);
}

void post_function_call(ldms_shm_MPI_func_id_t func_id)
{
	if(is_disabled(func_id))
		return;
	int s = 0;
	do {
		switch(profiler->conf->event_update_type) {

		case LDMS_SHM_MPI_EVENT_UPDATE_LOCAL:
			profiler->local_mpi_event_counters[s].val++;
			break;
		case LDMS_SHM_MPI_EVENT_UPDATE_SHARED:
			shm_inc(profiler->shm_set,
					profiler->events[func_id]->spec[s]->stat[profiler->stat_index].event_id);
			break;
		default:
			printf("ERROR: invalid update type\n\r");
			break;
		}
		if(profile_log_level >= LDMS_SHM_LOG_LINFO)
			profiler->events[func_id]->spec[s]->stat[profiler->stat_index].counter++;
		s++;
	} while(s < profiler->events[func_id]->spec_size);
	if(profile_log_level >= LDMS_SHM_LOG_LINFO) {
		print_event(func_id, profiler->stat_index);
	}
}
