/*
 * Copyright (c) 2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2017 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
#include <ldmsd.h>
typedef struct base_data_s {
	char *instance_name;
	char *producer_name;
	char *schema_name;
	ldms_schema_t schema;
	ldms_set_t job_set;
	ldms_set_t set;
	uint64_t component_id;
	int job_id_idx;
	int job_start_idx;
	int job_end_idx;
} *base_data_t;

#define BASE_COMPONENT_ID	0
#define BASE_JOB_ID		1
#define BASE_CONFIG_USAGE \
	"producer=<name> instance=<name> [component_id=<int>] [schema=<name>]\n" \
	"    producer     A unique name for the host providing the data\n" \
	"    instance     A unique name for the metric set\n" \
	"    component_id A unique number for the component being monitored, Defaults to zero.\n" \
	"    schema       The name of the metric set schema, Defaults to the sampler name\n"

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

#endif
