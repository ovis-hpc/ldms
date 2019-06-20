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
 * \file mpi_profiler_configuration.h
 * \brief Routines to configure the profiler and events
 */
#ifndef MPI_PROFILER_CONFIGURATION_H
#define MPI_PROFILER_CONFIGURATION_H

#include "mpi.h"
#include "mpi_profiler_func_list.h"

/**
 * enumeration for the mpi_profiler log level
 */
typedef enum ldms_shm_profile_log_level {
	LDMS_SHM_PROFILING_DISABLED = 0, /* profiling is disabled */
	LDMS_SHM_LOG_LNONE = 1, /* profiling is enabled with minimum log information  */
	LDMS_SHM_LOG_LINFO = 2 /* profiling is enabled with more log information  */
} ldms_shm_profile_log_level_t;

/**
 * enumeration for the MPI event update type
 */
typedef enum {
	LDMS_SHM_MPI_EVENT_UPDATE_LOCAL = 0, /* Updates to counters are buffered locally and are transfered to the shared memory using an updater thread periodically */
	LDMS_SHM_MPI_EVENT_UPDATE_SHARED = 1 /* Updates to counters get reflected immediately in the shared memory */
} ldms_shm_mpi_event_update_type_t;

/**
 * enumeration for the method of counter update
 */
typedef enum {
	LDMS_SHM_MPI_STAT_GLOBAL = 0, /* The stats (counters) are collected globally for all ranks in the current node. One counter per event for all ranks */
	LDMS_SHM_MPI_STAT_LOCAL = 1 /* The stats (counters) are collected locally for each ranks in the current node. One counter per event for each rank */
} ldms_shm_MPI_stat_scope_t;

/**
 * enumeration for the MPI function configuration
 */
typedef enum {
	LDMS_SHM_MPI_FUNC_INCLUDE = 0, /* Include MPI functions  */
	LDMS_SHM_MPI_FUNC_EXCLUDE = 1 /* Exclude MPI functions  */
} ldms_shm_mpi_func_inc_exc_t;

/**
 * structure to record the information related to the MPI profiler configuration
 */
typedef struct ldms_shm_mpi_profiler_configuration {
	ldms_shm_profile_log_level_t profile_log_level; /* MPI profiler log level */
	ldms_shm_MPI_stat_scope_t scope; /* scope of the counter update */
	ldms_shm_mpi_event_update_type_t event_update_type; /* type of the counter update */
}*ldms_shm_mpi_profiler_configuration_t;

/**
 * \brief determines if the MPI function specified by the id is disabled for the profiling
 *
 * \param func_id The id of the MPI function
 * \return 1 if the MPI function is not configured for profiling and 0 otherwise
 */
int is_disabled(ldms_shm_MPI_func_id_t func_id);
/**
 * \brief initializes the events according the configurations
 *
 * \return the array of event names that are successfully configured
 */
char **init_events();

#endif /* MPI_PROFILER_CONFIGURATION_H */
