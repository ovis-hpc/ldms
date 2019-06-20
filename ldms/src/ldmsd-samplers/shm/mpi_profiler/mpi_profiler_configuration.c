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
 * \file mpi_profiler_configuration.c
 * \brief Routines to configure the profiler and events
 */

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/queue.h>

#include "../shm_util/ldms_shm_event_set.h"
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

#define LDMS_SHM_MPI_CONFIG_EVENT_DELIMITER ","
#define LDMS_SHM_MPI_CONFIG_EVENT_TYPE_DELIMITER ":"
#define LDMS_SHM_MPI_CONFIG_EVENT_TYPES_DELIMITER "#"
#define LDMS_SHM_MPI_CONFIG_FUNC_ARG_DELIMITER "@"
#define LDMS_SHM_MPI_CONFIG_FUNC_SIZE_TYPE_DELIMITER ";"
#define LDMS_SHM_MPI_CONFIG_FUNC_ARG_SIZE_DELIMITER "<"
#define LDMS_SHM_MPI_CONFIG_FUNC_ARG_TYPE_DELIMITER "-"

static const char* EVENT_TYPE_BYTES = "bytes";
static const char* EVENT_TYPE_CALLS = "calls";

typedef struct mpi_profiler_config {
	char *mpi_func_name;
	char** mpi_event_types;
	int num_of_event_types;
	char** mpi_sizes;
	int num_of_sizes;
	char** mpi_types;
	int num_of_mpi_types;
}* mpi_profiler_config_t;

struct event_config_param {
	char *param;
	LIST_ENTRY(event_config_param)
	entry;
};

static inline void enable_function(ldms_shm_MPI_func_id_t func_id)
{
	profiler->ldms_shm_mpi_base_events[func_id].enabled = 1;
}

int is_disabled(ldms_shm_MPI_func_id_t func_id)
{
	return (0 == profiler->ldms_shm_mpi_base_events[func_id].enabled);
}

static char** prase_conf(const char *conf, const char *delim, int *count_tokens)
{
	LIST_HEAD(conf_token_list, event_config_param)
	conf_token_list;
	LIST_INIT(&conf_token_list);
	count_tokens[0] = 0;
	char* status;
	char *conf_copy = strdup(conf);
	char *next_token = strtok_r(conf_copy, delim, &status);
	while(next_token) {
		struct event_config_param *ecp = calloc(1, sizeof *ecp);
		if(NULL == ecp) {
			printf(
					"ERROR: memory allocation error in profiler configuration\n\t");
			free(conf_copy);
			return NULL;
		}
		ecp->param = strdup(next_token);
		LIST_INSERT_HEAD(&conf_token_list, ecp, entry);
		count_tokens[0]++;
		next_token = strtok_r(NULL, delim, &status);
	}
	free(conf_copy);
	if(count_tokens[0] > 0) {
		char **tokens = calloc(count_tokens[0], sizeof(char*));
		int index = count_tokens[0] - 1;
		struct event_config_param *ecp;
		LIST_FOREACH(ecp, &conf_token_list, entry)
		{
			tokens[index] = strdup(ecp->param);
			index--;
		}
		LIST_FOREACH(ecp, &conf_token_list, entry)
		{
			free(ecp->param);
			free(ecp);
		}
		return tokens;
	}
	return NULL;
}

static mpi_profiler_config_t* config_function_parameters(const char *app_config)
{
	int successful_event_count = 0, num_base_events = 0;
	char** mpi_events = prase_conf(app_config,
			LDMS_SHM_MPI_CONFIG_EVENT_DELIMITER, &num_base_events);
	profiler->num_base_events = num_base_events;

	mpi_profiler_config_t* all_configs = calloc(profiler->num_base_events,
			sizeof(mpi_profiler_config_t*));

	int e;
	for(e = 0; e < profiler->num_base_events; e++) {
		all_configs[e] = calloc(1, sizeof(*all_configs[e]));
		int event_types_provided;
		int size_param_provided;

		char *mpi_func_name = NULL;
		char** mpi_event_types = NULL;
		int num_of_event_types = 0;
		char** mpi_sizes = NULL;
		int num_of_sizes = 0;
		char** mpi_types = NULL;
		int num_of_mpi_types = 0;

		char *rest_of_configuration;
		char *event_type_conf = strdup(mpi_events[e]);
		int num_of_func_name_arg_param = 0;

		char** func_name_arg_param = prase_conf(event_type_conf,
				LDMS_SHM_MPI_CONFIG_EVENT_TYPE_DELIMITER,
				&num_of_func_name_arg_param);
		if(num_of_func_name_arg_param == 2) {
			event_types_provided = 1;
			mpi_func_name = func_name_arg_param[0];
			rest_of_configuration = func_name_arg_param[1];
		} else if(num_of_func_name_arg_param == 1) {
			/* FIXME
			 * Type of the event should be set to default, rest of the configuration, should be checked
			 */
			rest_of_configuration = event_type_conf;
			event_types_provided = 0;
		} else {
			/* FIXME
			 * ERROR
			 */
			printf(
					"Configuration Error when parsing based on this delimiter: \"%s\" (%d) \n\r",
					LDMS_SHM_MPI_CONFIG_EVENT_TYPE_DELIMITER,
					num_of_func_name_arg_param);
			continue;
		}
		char *func_arg_conf = strdup(rest_of_configuration);
		int num_of_func_arg_param = 0;
		char** mpi_event_type_params = prase_conf(func_arg_conf,
				LDMS_SHM_MPI_CONFIG_FUNC_ARG_DELIMITER,
				&num_of_func_arg_param);
		if(num_of_func_arg_param == 2) {
			size_param_provided = 1;
			if(event_types_provided) {
				mpi_event_types =
						prase_conf(
								mpi_event_type_params[0],
								LDMS_SHM_MPI_CONFIG_EVENT_TYPES_DELIMITER,
								&num_of_event_types);
			} else {
				mpi_func_name = mpi_event_type_params[0];
			}
			rest_of_configuration = mpi_event_type_params[1];
		} else if(num_of_func_arg_param == 1) {
			/* FIXME
			 * Size of the event should be set to default, rest of the configuration, should be checked
			 */
			rest_of_configuration = func_arg_conf;
			size_param_provided = 0;
		} else {
			/* FIXME
			 * ERROR
			 */
			printf(
					"Configuration Error when parsing based on this delimiter: \"%s\" \n\r",
					LDMS_SHM_MPI_CONFIG_FUNC_ARG_DELIMITER);
			continue;
		}

		char *func_size_type_conf = strdup(rest_of_configuration);
		int num_of_size_types = 0;
		char** mpi_size_type = prase_conf(func_size_type_conf,
				LDMS_SHM_MPI_CONFIG_FUNC_SIZE_TYPE_DELIMITER,
				&num_of_size_types);
		if(num_of_size_types == 2) {
			if(size_param_provided) {
				mpi_sizes =
						prase_conf(mpi_size_type[0],
								LDMS_SHM_MPI_CONFIG_FUNC_ARG_SIZE_DELIMITER,
								&num_of_sizes);
			} else if(!size_param_provided
					&& event_types_provided) {
				mpi_event_types =
						prase_conf(mpi_size_type[0],
								LDMS_SHM_MPI_CONFIG_EVENT_TYPES_DELIMITER,
								&num_of_event_types);
			} else {
				mpi_func_name = mpi_size_type[0];
			}
			mpi_types =
					prase_conf(mpi_size_type[1],
							LDMS_SHM_MPI_CONFIG_FUNC_ARG_TYPE_DELIMITER,
							&num_of_mpi_types);
		} else if(num_of_size_types == 1) {
			/* FIXME
			 * Type of the messages should be set to default
			 */
			if(size_param_provided) {
				mpi_sizes =
						prase_conf(func_size_type_conf,
								LDMS_SHM_MPI_CONFIG_FUNC_ARG_SIZE_DELIMITER,
								&num_of_sizes);
			} else if(!size_param_provided
					&& event_types_provided) {
				mpi_event_types =
						prase_conf(func_size_type_conf,
								LDMS_SHM_MPI_CONFIG_EVENT_TYPES_DELIMITER,
								&num_of_event_types);
			} else {
				mpi_func_name = func_size_type_conf;
			}
		} else {
			/* FIXME
			 * ERROR
			 */
			printf(
					"Configuration Error when parsing based on this delimiter: \"%s\" \n\r",
					LDMS_SHM_MPI_CONFIG_FUNC_SIZE_TYPE_DELIMITER);
			continue;
		}

		if(num_of_sizes != 0
				&& (num_of_sizes != 2 && num_of_sizes != 3)) {
			/* FIXME
			 * ERROR
			 */
			printf(
					"Configuration Error because of a problem with size filter specification: %d  \n\r",
					num_of_sizes);
			continue;
		}

		all_configs[e]->mpi_event_types = mpi_event_types;
		all_configs[e]->mpi_func_name = mpi_func_name;
		all_configs[e]->mpi_sizes = mpi_sizes;
		all_configs[e]->mpi_types = mpi_types;
		all_configs[e]->num_of_event_types = num_of_event_types;
		all_configs[e]->num_of_mpi_types = num_of_mpi_types;
		all_configs[e]->num_of_sizes = num_of_sizes;
		successful_event_count++;
		if(all_configs[e]->num_of_event_types) {
			profiler->num_base_events_with_types +=
					all_configs[e]->num_of_event_types;
		} else {
			profiler->num_base_events_with_types++;
		}
	}
	if(profiler->num_base_events != successful_event_count) {
		printf(
				"WARN: %d events were provided, but %d were not configured properly\n\r",
				num_base_events,
				(num_base_events - successful_event_count));
		profiler->num_base_events = successful_event_count;
	}
	return all_configs;
}

static ldms_shm_mpi_event_spec_t create_event_spec(
		ldms_shm_mpi_event_spec_type_t spec_type,
		ldms_shm_mpi_event_type_t event_type, char *desc,
		MPI_Datatype *inc_mpi_types, MPI_Datatype *exc_mpi_types,
		ldms_shm_mpi_msg_bin_t msg_bin)
{
	ldms_shm_mpi_event_spec_t spec = calloc(1, sizeof(*spec));
	if(NULL == spec)
		return NULL;
	spec->spec_type = spec_type;
	spec->event_type = event_type;
	spec->desc = desc;
	spec->inc_mpi_types = inc_mpi_types;
	spec->exc_mpi_types = exc_mpi_types;
	spec->msg_bin.min = msg_bin.min;
	spec->msg_bin.max = msg_bin.max;
	return spec;
}

static MPI_Datatype convert_to_mpi_type(char *mpi_type_str)
{
	/* FIXME */
	return MPI_INT;
}

static ldms_shm_mpi_event_spec_t* create_specs(
		mpi_profiler_config_t event_config, int *spec_size)
{
	ldms_shm_mpi_event_spec_t *specs = NULL;

	ldms_shm_mpi_event_spec_type_t spec_type =
			LDMS_SHM_MPI_EVENT_SPEC_DEFAULT;
	ldms_shm_mpi_event_type_t event_type = LDMS_SHM_MPI_EVENT_FUNC_CALLS;
	char *desc;
	char *size_desc = NULL;
	MPI_Datatype *inc_mpi_types = NULL;
	MPI_Datatype *exc_mpi_types = NULL;
	ldms_shm_mpi_msg_bin_t msg_bin;
	msg_bin.min = -1, msg_bin.max = -1;
	*spec_size = 1;

	if(event_config->num_of_sizes) {

		if(event_config->num_of_sizes == 3) {
			msg_bin.min = atoi(event_config->mpi_sizes[0]);
			msg_bin.max = atoi(event_config->mpi_sizes[2]);
			size_desc =
					malloc(
							strlen(
									event_config->mpi_sizes[0])
									+ strlen(
											event_config->mpi_sizes[1])
									+ strlen(
											event_config->mpi_sizes[2])
									+ 5);
			if(NULL == size_desc) {
				printf(
						"ERROR: memory allocation error in profiler configuration\n\r");
				return NULL;
			}
			sprintf(size_desc, "(%s<%s<%s)",
					event_config->mpi_sizes[0],
					event_config->mpi_sizes[1],
					event_config->mpi_sizes[2]);
		} else if(event_config->num_of_sizes == 2) {
			char *endptr;
			errno = 0;
			long val = strtol(event_config->mpi_sizes[0], &endptr,
					10);
			if(!errno) {
				msg_bin.min = val;
			} else {
				val = strtol(event_config->mpi_sizes[1],
						&endptr, 10);
				if(!errno) {
					msg_bin.max = val;
				} else {
					/* FIXME
					 * Error!
					 */
				}
			}
			size_desc =
					malloc(
							strlen(
									event_config->mpi_sizes[0])
									+ strlen(
											event_config->mpi_sizes[1])
									+ 4);
			if(NULL == size_desc) {
				printf(
						"ERROR: memory allocation error in profiler configuration\n\r");
				return NULL;
			}
			sprintf(size_desc, "(%s<%s)",
					event_config->mpi_sizes[0],
					event_config->mpi_sizes[1]);
		} else {
			/* FIXME
			 * Error!
			 */
		}

	}
	if(event_config->num_of_mpi_types) {
		inc_mpi_types = calloc(event_config->num_of_mpi_types,
				sizeof(MPI_Datatype));
		if(NULL == inc_mpi_types) {
			printf(
					"ERROR: memory allocation error in profiler configuration\n\r");
			if(NULL != size_desc)
				free(size_desc);
			return NULL;
		}
		int mt;
		for(mt = 0; mt < event_config->num_of_mpi_types; mt++) {
			inc_mpi_types[mt] = convert_to_mpi_type(
					event_config->mpi_event_types[mt]);
		}
	}
	if(event_config->num_of_event_types) {
		if(event_config->num_of_event_types == 2) {
			*spec_size = 2;
			specs = calloc(*spec_size,
					sizeof(ldms_shm_mpi_event_spec_t*));

			if(size_desc == NULL) {
				desc =
						malloc(
								strlen(
										event_config->mpi_event_types[0])
										+ 1);
				sprintf(desc, "%s",
						event_config->mpi_event_types[0]);
			} else {
				desc =
						malloc(
								strlen(
										event_config->mpi_event_types[0])
										+ strlen(
												size_desc)
										+ 2);
				sprintf(desc, "%s.%s",
						event_config->mpi_event_types[0],
						size_desc);
			}

			specs[0] = create_event_spec(spec_type, event_type,
					desc, inc_mpi_types, exc_mpi_types,
					msg_bin);
			event_type = LDMS_SHM_MPI_EVENT_ARG_BYTES;
			if(size_desc == NULL) {
				desc =
						malloc(
								strlen(
										event_config->mpi_event_types[1])
										+ 1);
				sprintf(desc, "%s",
						event_config->mpi_event_types[1]);
			} else {
				desc =
						malloc(
								strlen(
										event_config->mpi_event_types[1])
										+ strlen(
												size_desc)
										+ 2);
				sprintf(desc, "%s.%s",
						event_config->mpi_event_types[1],
						size_desc);
			}
			specs[1] = create_event_spec(spec_type, event_type,
					desc, inc_mpi_types, exc_mpi_types,
					msg_bin);
		} else {
			if(strcmp(event_config->mpi_event_types[0],
					EVENT_TYPE_CALLS) == 0)
				event_type = LDMS_SHM_MPI_EVENT_FUNC_CALLS;
			else
				event_type = LDMS_SHM_MPI_EVENT_ARG_BYTES;/*FIXME*/
		}
	}
	if(specs == NULL) {
		specs = calloc(*spec_size, sizeof(ldms_shm_mpi_event_spec_t*));
		const char *event_type_str;
		if(event_type == LDMS_SHM_MPI_EVENT_FUNC_CALLS)
			event_type_str = EVENT_TYPE_CALLS;
		else
			event_type_str = EVENT_TYPE_BYTES;
		if(size_desc == NULL) {
			desc = malloc(strlen(event_type_str) + 1);
			sprintf(desc, "%s", event_type_str);
		} else {
			desc = malloc(
					strlen(event_type_str)
							+ strlen(size_desc)
							+ 2);
			sprintf(desc, "%s.%s", event_type_str, size_desc);
		}
		specs[0] = create_event_spec(spec_type, event_type, desc,
				inc_mpi_types, exc_mpi_types, msg_bin);
	}
	return specs;
}

static char *get_default_inc_functions()
{
	return "MPI_Send";
}

static int create_stats(ldms_shm_MPI_stat_scope_t scope,
		ldms_shm_MPI_func_id_t func_id, int global_event_index,
		int base_event_with_type_start_index, char **events_desc)
{
	int s, i;
	int num_delimiter_chars = 1; /* '.' */
	for(s = 0; s < profiler->events[func_id]->spec_size; s++) {
		int base_event_index = base_event_with_type_start_index + s;
		profiler->events[func_id]->spec[s]->stat = calloc(
				profiler->num_events_per_func,
				sizeof(ldms_shm_mpi_stat_t));
		if(NULL == profiler->events[func_id]->spec[s]->stat) {
			printf(
					"memory allocaiton error in profiler configuration\n\r");
			return -1;
		}
		int event_len =
				strlen(profiler->events[func_id]->base->desc)
						+ strlen(
								profiler->events[func_id]->spec[s]->desc)
						+ num_delimiter_chars + 1;
		events_desc[base_event_index] = malloc(
				event_len * sizeof(char));
		snprintf(events_desc[base_event_index], event_len, "%s.%s",
				profiler->events[func_id]->base->desc,
				profiler->events[func_id]->spec[s]->desc);

		if(LDMS_SHM_MPI_EVENT_UPDATE_LOCAL
				== profiler->conf->event_update_type) {
			profiler->local_mpi_event_counters[s].val = 0;
		}

		for(i = 0; i < profiler->num_events_per_func; i++) {

			profiler->events[func_id]->spec[s]->stat[i].counter = 0;
			profiler->events[func_id]->spec[s]->stat[i].event_id =
					global_event_index;
			profiler->events[func_id]->spec[s]->stat[i].desc =
					strdup(events_desc[base_event_index]);

			if(LDMS_SHM_MPI_EVENT_UPDATE_LOCAL
					== profiler->conf->event_update_type) {
				if(LDMS_SHM_MPI_STAT_LOCAL
						== profiler->conf->scope) {
					if(profiler->rankid == i)
						profiler->event_index_map[s] =
								global_event_index;
				} else {
					profiler->event_index_map[s] =
							global_event_index;
				}
			}

			global_event_index++;
		}
	}
	return global_event_index;
}

static char** config_exclude_functions(char* exclude_str)
{
	return NULL;
	/*
	 enable_all_functions();
	 char* exc_functions = strdup(
	 exclude_str);
	 char* func_name;
	 char* status;
	 int func_counter = 0;
	 func_name = strtok_r(exc_functions, ",",
	 &status);
	 while(func_name) {
	 func_counter++;
	 ldms_shm_MPI_func_id_t func_id =
	 find_func_id(
	 func_name);
	 if(func_id
	 < LDMS_SHM_MPI_Send_ID) {
	 //FIXME
	 printf(
	 "Error! invalid function name: %s\n\r",
	 func_name);
	 } else {
	 disable_function(
	 func_id);
	 }
	 func_name = strtok_r(NULL, ",",
	 &status);
	 }
	 num_base_events =
	 LDMS_SHM_MPI_NUM_FUNCTIONS
	 - func_counter;
	 num_base_events_with_types = num_base_events;

	 int total_num_events = calc_total_number_of_events(scope);

	 char **events_desc = calloc(total_num_events, sizeof(char*));

	 int func_id = 0;
	 int global_event_index = 0;
	 for(func_id = 0; func_id < LDMS_SHM_MPI_NUM_FUNCTIONS; func_id++) {
	 if(is_enabled(func_id)) {
	 events[func_id] = calloc(1, sizeof(struct ldms_shm_mpi_event));
	 events[func_id]->base = &ldms_shm_mpi_base_events[func_id];
	 events[func_id]->scope = scope;
	 events[func_id]->spec = create_default_specs();
	 global_event_index = create_stats(scope, func_id, global_event_index, events_desc);
	 }
	 }
	 return events_desc;
	 */
}

static char **config_inc_functions(char* include_str)
{
	mpi_profiler_config_t* event_configs = config_function_parameters(
			include_str);

	profiler->num_events_per_func =
			(LDMS_SHM_MPI_STAT_LOCAL == profiler->conf->scope) ?
					profiler->total_ranks : 1;

	char **events_desc = malloc(
			profiler->num_base_events_with_types * sizeof(char*));

	if(LDMS_SHM_MPI_EVENT_UPDATE_LOCAL
			== profiler->conf->event_update_type) {
		profiler->local_mpi_event_counters = calloc(
				profiler->num_base_events_with_types,
				sizeof(ldms_shm_data_t));
		profiler->event_index_map = calloc(
				profiler->num_base_events_with_types,
				sizeof(ldms_shm_data_t));
	}

	int e, successful_event_counter = 0;
	int global_event_index = 0;
	int base_event_with_type_start_index = 0;
	for(e = 0; e < profiler->num_base_events; e++) {

		ldms_shm_MPI_func_id_t func_id = find_func_id(
				event_configs[e]->mpi_func_name);

		if(func_id < LDMS_SHM_MPI_Send_ID) {
			/* FIXME
			 *
			 */
			printf("Error! invalid function name: %s\n\r",
					event_configs[e]->mpi_func_name);
			continue;
		} else {
			enable_function(func_id);
			profiler->events[func_id] = calloc(1,
					sizeof(struct ldms_shm_mpi_event));
			profiler->events[func_id]->base =
					&profiler->ldms_shm_mpi_base_events[func_id];
			profiler->events[func_id]->scope =
					profiler->conf->scope;
			profiler->events[func_id]->spec = create_specs(
					event_configs[e],
					&profiler->events[func_id]->spec_size);
			global_event_index = create_stats(profiler->conf->scope,
					func_id, global_event_index,
					base_event_with_type_start_index,
					events_desc);
			if(global_event_index < 0)
				break;
			base_event_with_type_start_index +=
					profiler->events[func_id]->spec_size;
			successful_event_counter++;
		}
	}
	/* cleaning */
	if(NULL != event_configs) {
		for(e = 0; e < profiler->num_base_events; e++) {
			if(NULL != event_configs[e]->mpi_func_name) {
				free(event_configs[e]->mpi_func_name);
				event_configs[e]->mpi_func_name = NULL;
			}
			if(NULL != event_configs[e]->mpi_event_types) {
				int i;
				for(i = 0;
						i
								< event_configs[e]->num_of_event_types;
						i++) {
					if(NULL
							!= event_configs[e]->mpi_event_types[i]) {
						free(
								event_configs[e]->mpi_event_types[i]);
						event_configs[e]->mpi_event_types[i] =
								NULL;
					}
				}
				free(event_configs[e]->mpi_event_types);
				event_configs[e]->mpi_event_types = NULL;
			}
			if(NULL != event_configs[e]->mpi_sizes) {
				int i;
				for(i = 0; i < event_configs[e]->num_of_sizes;
						i++) {
					if(NULL
							!= event_configs[e]->mpi_sizes[i]) {
						free(
								event_configs[e]->mpi_sizes[i]);
						event_configs[e]->mpi_sizes[i] =
								NULL;
					}
				}
				free(event_configs[e]->mpi_sizes);
				event_configs[e]->mpi_sizes = NULL;
			}
			if(NULL != event_configs[e]->mpi_types) {
				int i;
				for(i = 0;
						i
								< event_configs[e]->num_of_mpi_types;
						i++) {
					if(NULL
							!= event_configs[e]->mpi_types[i]) {
						free(
								event_configs[e]->mpi_types[i]);
						event_configs[e]->mpi_types[i] =
								NULL;
					}
				}
				free(event_configs[e]->mpi_types);
				event_configs[e]->mpi_types = NULL;
			}
		}
	}
	if(global_event_index < 0)
		return NULL;
	profiler->num_base_events = successful_event_counter;
	if(log_level_info())
		printf(
				"configurations have been done:\n\tnum_base_events=%d\n\tnum_base_events_with_types=%d\n\t\n\tnum_events_per_func=%d\n\r",
				profiler->num_base_events,
				profiler->num_base_events_with_types,
				profiler->num_events_per_func);

	return events_desc;
}

static char **config_functions()
{
	char* include_str = getenv(LDMS_SHM_MPI_FUNC_INCLUDE_ENV_VAR_NAME);

	if(!include_str) {
		char* exclude_str = getenv(
		LDMS_SHM_MPI_FUNC_EXCLUDE_ENV_VAR_NAME);
		if(!exclude_str) {
			printf(
					"The environment variable: \"%s\" is empty. Setting to default %d ...\n\r",
					LDMS_SHM_MPI_FUNC_INCLUDE_ENV_VAR_NAME,
					LDMS_SHM_MPI_FUNC_INC_EXC_DEFAULT);
			profiler->num_base_events = 1;
			include_str = get_default_inc_functions();
			enable_function(LDMS_SHM_MPI_Send_ID);
		} else {
			/* FIXME fix the initializations. currently it works for include not exclude! */
			printf("exclude is not supported!\n\r");
			return config_exclude_functions(exclude_str);
		}
	}

	return config_inc_functions(include_str);
}

char **init_events()
{
	profiler->events = calloc(LDMS_SHM_MPI_NUM_FUNCTIONS,
			sizeof(ldms_shm_mpi_event_t));
	return config_functions();
}
