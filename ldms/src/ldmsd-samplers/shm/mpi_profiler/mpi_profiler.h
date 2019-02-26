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
 * \file mpi_profiler.h
 * \brief Routines to manage the profiler
 */
#ifndef MPI_PROFILER_H
#define MPI_PROFILER_H

#include <stdint.h>
#include "mpi.h"
#include "../shm_util/ldms_shm_event_set.h"
#include "mpi_profiler_func_list.h"
#include "mpi_profiler_configuration.h"

#define LDMS_SHM_INDEX_ENV_VAR_NAME "LDMS_SHM_INDEX"
#define LDMS_SHM_MPI_STAT_SCOPE_ENV_VAR_NAME "LDMS_SHM_MPI_STAT_SCOPE"
#define LDMS_SHM_MPI_FUNC_INCLUDE_ENV_VAR_NAME "LDMS_SHM_MPI_FUNC_INCLUDE"
#define LDMS_SHM_MPI_FUNC_EXCLUDE_ENV_VAR_NAME "LDMS_SHM_MPI_FUNC_EXCLUDE"
#define LDMS_SHM_MPI_EVENT_UPDATE_ENV_VAR_NAME "LDMS_SHM_MPI_EVENT_UPDATE"
#define LDMS_SHM_MPI_SHM_UPDATE_INTERVAL_ENV_VAR_NAME "LDMS_SHM_MPI_SHM_UPDATE_INTERVAL"
#define LDMS_SHM_MPI_PROFILER_LOG_LEVEL_ENV_VAR_NAME "LDMS_SHM_MPI_PROFILER_LOG_LEVEL"

#define LDMS_SHM_MPI_PROFILER_LOG_LEVEL_DEFAULT 1
#define LDMS_SHM_MPI_SHM_UPDATE_INTERVAL_DEFAULT 80000
#define LDMS_SHM_MPI_FUNC_BASE_DEFAULT "MPI_Send"
#define LDMS_SHM_MPI_STAT_SCOPE_DEFAULT 1
#define LDMS_SHM_MPI_FUNC_INC_EXC_DEFAULT 0

/**
 * enumeration for the mpi_event type
 */
typedef enum {
	LDMS_SHM_MPI_EVENT_FUNC_CALLS = 0, /* Count number of calls to the MPI function */
	LDMS_SHM_MPI_EVENT_ARG_BYTES = 1 /* Count number of bytes handled by the MPI function */
} ldms_shm_mpi_event_type_t;

/**
 * structure to store information related to base MPI events
 */
typedef struct ldms_shm_mpi_event_base {
	ldms_shm_MPI_func_id_t func_id; /* MPI function id */
	uint8_t enabled; /* Flag to determine whether profiling is enabled for this base MPI event */
	char desc[MAX_LDMS_SHM_MPI_FUNC_NAME]; /* description for this MPI event */
} ldms_shm_mpi_event_base_t;

/**
 * structure to store information related to MPI event statistics
 */
typedef struct ldms_shm_mpi_stat {
	int event_id; /* the unique event identifier */
	char *desc; /* the description for the event */
	uint64_t counter; /* The local counter value for this event (used for log level info) */
} ldms_shm_mpi_stat_t;

/**
 * structure to store information related to message size bin for MPI event configuration
 */
typedef struct ldms_shm_mpi_msg_bin {
	int min;
	int max;
} ldms_shm_mpi_msg_bin_t;

/**
 * enumeration for event specification type
 */
typedef enum {
	LDMS_SHM_MPI_EVENT_SPEC_DEFAULT = 0,
	LDMS_SHM_MPI_EVENT_SPEC_USER_PROVIDED = 1
} ldms_shm_mpi_event_spec_type_t;

/**
 * structure to hold information related to MPI event specification
 */
typedef struct ldms_shm_mpi_event_spec {
	ldms_shm_mpi_event_spec_type_t spec_type; /* type of event specification */
	ldms_shm_mpi_event_type_t event_type; /* type of MPI event */
	char *desc; /* description for the event specification */
	MPI_Datatype *inc_mpi_types; /* Included MPI_Datatypes for profiling */
	MPI_Datatype *exc_mpi_types; /* Excluded MPI_Datatypes for profiling */
	ldms_shm_mpi_msg_bin_t msg_bin; /* Message size bin for event filtering  */
	ldms_shm_mpi_stat_t *stat; /* statistics for the event */
}*ldms_shm_mpi_event_spec_t;

/**
 * structure to hold information related to MPI event
 */
typedef struct ldms_shm_mpi_event {
	ldms_shm_mpi_event_base_t *base; /* Base MPI Event */
	int spec_size; /* number of event specifications for this event  */
	ldms_shm_mpi_event_spec_t *spec; /* List of specifications for this event  */
	ldms_shm_MPI_stat_scope_t scope; /* The scope of counters for this event */
}*ldms_shm_mpi_event_t;

/**
 *structure to hold information related to the MPI profiler
 */
typedef struct ldms_shm_mpi_profiler {
	ldms_shm_mpi_profiler_configuration_t conf; /* Configuration for the profiler */
	ldms_shm_mpi_event_base_t ldms_shm_mpi_base_events[LDMS_SHM_MPI_NUM_FUNCTIONS]; /* base MPI events */
	ldms_shm_data_t* local_mpi_event_counters; /* local counters used by updater thread if enabled */
	int *event_index_map; /* the mapping between events and counter indices in the shared memory */
	ldms_shm_mpi_event_t *events; /* events to collect */
	char *ldms_shm_set_label; /* Name of metric set */
	char* ldms_shm_set_fslocation; /* Name of the shared memory location for the metric set */
	ldms_shm_set_t shm_set; /* shared memory set */
	ldms_shm_index_t shm_index; /* shared memory index */
	int stat_index; /* The index of the statistics that are being updated by the current MPI process */
	int total_ranks; /* total number of ranks */
	int rankid; /* The rank id of the current MPI process */
	int num_base_events; /* Number of base MPI events that are configured for profiling */
	int num_base_events_with_types; /* Number of base MPI events including different event types that are configured for profiling */
	int num_events_per_func; /* Number of events per MPI functions (base_event with type) */
	int total_num_events; /* Total number of events */
}*ldms_shm_mpi_profiler_t;

ldms_shm_mpi_profiler_t profiler;
char * ldms_shm_mpi_app_name;
ldms_shm_profile_log_level_t profile_log_level;
char* ldms_shm_index_name;

/**
 * \brief get the log level info
 *
 * \return the log level info
 */
int log_level_info();

/**
 * \brief control the profiling
 *
 * \param flag to control the profiling (flag = 0 disables the profiling
 */
void post_Pcontrol_call(const int flag);

/**
 * \brief function to be called after the call to MPI_init
 */
void post_init_call();

/**
 * \brief function to be called after the call to MPI_finalize
 */
int post_finalize_call();
/**
 * \brief function to be called after a call to MPI functions with count and datatype in arguments
 *
 * \param func_id the id of the MPI function that is called
 * \param count the count parameter of the MPI function
 * \param datatype the data type parameter of the MPI function
 */
void post_msg_based_function_call(ldms_shm_MPI_func_id_t func_id, int count,
		MPI_Datatype datatype);
/**
 * \brief function to be called after a call to other MPI functions
 *
 * \param func_id the id of the MPI function that is called
 */
void post_function_call(ldms_shm_MPI_func_id_t func_id);

#endif /* MPI_PROFILER_H */
