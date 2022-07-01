/**
 * Copyright (c) 2017-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017-2018 Open Grid Computing, Inc. All rights reserved.
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
#ifndef SAMPLER_BASE_H
#define SAMPLER_BASE_H

#include <stdbool.h>
#include "ldmsd.h"

struct base_auth {
	uid_t uid;
	bool uid_is_set;
	gid_t gid;
	bool gid_is_set;
	int perm;
	bool perm_is_set;
};

typedef struct base_data_s {
	char *pi_name;
	char *instance_name;
	char *producer_name;
	char *schema_name;
	char *job_set_name;
	ldms_schema_t schema;
	ldms_set_t job_set;
	ldms_set_t set;
	struct base_auth auth;
	int set_array_card;
	uint64_t component_id;
	int job_id_idx;
	int job_slot_list_idx;
	int job_slot_list_tail_idx;
	int app_id_idx;
	int job_start_idx;
	int job_end_idx;
	ldmsd_msg_log_f log;
	int job_log_lvl;
	unsigned missing_warned; /* 0 bit if warning not issued since set last seen */
} *base_data_t;

#define BASE_WARN_SET 0x1
#define BASE_WARN_JOBID 0x2
#define BASE_WARN_START 0x4
#define BASE_WARN_END 0x8
#define base_missing_warned_set(b) (BASE_WARN_SET & b->missing_warned)
#define base_missing_warned_jobid(b) (BASE_WARN_JOBID & b->missing_warned)
#define base_missing_warned_start(b) (BASE_WARN_START & b->missing_warned)
#define base_missing_warned_end(b) (BASE_WARN_END & b->missing_warned)
#define base_missing_warned_off(b,bit) (b->missing_warned &= (~bit))
#define base_missing_warned_on(b,bit) (b->missing_warned |= bit)


#define BASE_COMPONENT_ID	0
#define BASE_JOB_ID		1
#define BASE_APP_ID		2
#define BASE_CONFIG_SYNOPSIS \
	"producer=<name> instance=<name> [component_id=<int>] [schema=<name>]\n" \
	"       [job_set=<name>] [job_id=<name>] [app_id=<name>] [job_start=<name>] [job_end=<name>]\n" \
	"       [uid=<user-id>] [gid=<group-id>] [perm=<mode_t permission bits>]\n"
#define BASE_CONFIG_DESC \
	"    producer     A unique name for the host providing the data\n" \
	"    instance     A unique name for the metric set\n" \
	"    component_id A unique number for the component being monitored, Defaults to zero.\n" \
	"    schema       The name of the metric set schema, Defaults to the sampler name\n" \
	"    job_set      The instance name of the set containing the job data, default is 'job_info'\n" \
	"    job_id       The name of the metric containing the Job Id, default is 'job_id'\n" \
	"    app_id       The name of the metric containing the Application Id, default is 'app_id'\n" \
	"    job_start    The name of the metric containing the Job start time, default is 'job_start'\n" \
	"    job_end      The name of the metric containing the Job end time, default is 'job_end'\n" \
	"    uid          The user-id of the set's owner (defaults to geteuid())\n" \
	"    gid          The group id of the set's owner (defaults to getegid())\n" \
	"    perm         The set's access permissions (defaults to 0777)\n"

#define BASE_CONFIG_USAGE BASE_CONFIG_SYNOPSIS BASE_CONFIG_DESC

/**
 * \brief Create a sample schema with the standard metrics
 *
 * This function creates a schema that contains the stanard
 * metrics component_id, and job_id
 *
 * \returns The schema or NULL if there is an error
 */
ldms_schema_t base_schema_new(base_data_t base);

/**
 * \brief Configures job data acquisition for the sampler
 *
 * \param avl The attribute value keyword list provided to the config call
 * \param name The sampler name
 * \param def_schema The default schema name
 * \param log_fn The error logging function.
 * \return job_data_t if job data acquisition is configured on the system
 * \return NULL if job data is not configured or mis-configured
 */
base_data_t base_config(struct attr_value_list *avl,
			const char *name, const char *def_schema,
			ldmsd_msg_log_f log);

/**
 * \brief Create and initialize the base entries of the metric set
 * \param base The base data structure
 */
ldms_set_t base_set_new(base_data_t base);

/**
 * \brief Create and initialize the base entries of the metric set with a specific heap size.
 *
 * The function is similar to \c base_set_new(), except it takes a heap size.
 *
 * \param base The base data structure
 * \param heap_sz the heap size
 *
 * \return An LDMS set
 */
ldms_set_t base_set_new_heap(base_data_t base, size_t heap_sz);

/**
 * \brief Deregister and delete the LDMS set
 * \param base The base data structure
 */
void base_set_delete(base_data_t base);

/**
 * \brief Start the base metric sampling
 *
 * This function calls ldms_transaction_begin() and then stores the
 * current job id in the metric set. If job data is not
 * configured on the system, no job data is stored in the set.
 *
 * \param jd The job data structure returned by sampler_job_config
 * \param set The metric set containing the job_id_metric
 * \param job_id_metric The job id metric in \c set
 */
void base_sample_begin(base_data_t base);

/**
 * \brief Finish the base metric sampling
 *
 * Calls ldmsd_transaction_end()
 */
void base_sample_end(base_data_t base);

/**
 * \brief Release resources associated with base_data
 *
 * This function deletes the memory allocated for the configuration string data,
 * the schema, and the containing structure. The set is not destroyed
 */
void base_del(base_data_t base);

/**
 * \brief parse uid, gid, and perm values from attributes, if they exist
 * \param avl attribute source to use.
 * \param auth output sturct base_auth into which the uid/gid/perm values are set
 * \return 0 on success, 1 on invalid user/group lookup.
 */
int base_auth_parse(struct attr_value_list *avl, struct base_auth *auth,
		    ldmsd_msg_log_f log);

/**
 * \brief Set the authentication values in the ldms_set_t from base_data_t
 *
 * Take any configured authentication options from the base_data_t base
 * structure and apply them to the ldms_set_t metric set. The autentication
 * options in the base_data_t structure are set by base_auth_config(). If the
 * base_data_t structure was created by base_config(), base_config() will have
 * already called base_auth_config() on the structure. It is not necessary
 * to employ this function directly if your plugin calls base_set_new() to
 * create its metric sets. base_set_new() will call base_auth_set().
 *
 * \param [in] base The base_data_t struct that holds the authentication options
 * \param [out] set The metric set that shall have its authentication values set
 */
void base_auth_set(const struct base_auth *auth, ldms_set_t set);

#endif
