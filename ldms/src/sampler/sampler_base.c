/* -*- c-basic-offset: 8 -*-
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
/**
 * \file sampler_base.c
 * \brief Routines that are generally useful to sampler writers.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

void base_del(base_data_t base)
{
	if (!base)
		return;
	if (base->instance_name && base->pi_name)
		ldmsd_set_deregister(base->instance_name, base->pi_name);
	if (base->pi_name)
		free(base->pi_name);
	if (base->producer_name)
		free(base->producer_name);
	if (base->instance_name)
		free(base->instance_name);
	if (base->schema_name)
		free(base->schema_name);
	if (base->schema)
		ldms_schema_delete(base->schema);
	if (base->job_set)
		ldms_set_put(base->job_set);
	free(base);
}

base_data_t base_config(struct attr_value_list *avl,
			const char *name, const char *def_schema,
			ldmsd_msg_log_f log)
{
	char *job_set_name;
	char *value;
	errno = 0;

	base_data_t base = calloc(1, sizeof(*base));
	if (!base) {
		log(LDMSD_LERROR, "Memory allocation failure in %s\n", name);
		errno = ENOMEM;
		return NULL;
	}
	base->log = log;

	base->pi_name = strdup(name);
	if (!base->pi_name) {
		log(LDMSD_LERROR, "Memory allocation failure in %s\n", name);
		free(base);
		errno = ENOMEM;
		return NULL;
	}

	value = av_value(avl, "producer");
	if (!value) {
		log(LDMSD_LERROR, "%s: config string is missing the producer name.\n", name);
		goto einval;
	}
	base->producer_name = strdup(value);

	value = av_value(avl, "component_id");
	if (value)
		base->component_id = (uint64_t)(atoi(value));

	value = av_value(avl, "instance");
	if (!value) {
		log(LDMSD_LERROR,
		    "%s: configuration is missing the instance name.\n",
		    name);
		goto einval;
	}
	base->instance_name = strdup(value);

	value = av_value(avl, "schema");
	if (!value || value[0] == '\0')
		base->schema_name = strdup(def_schema);
	else
		base->schema_name = strdup(value);

	base->job_id_idx = BASE_JOB_ID;
	job_set_name = av_value(avl, "job_set");
	if (!job_set_name)
		job_set_name = "job_info";
	base->job_set = ldms_set_by_name(job_set_name);
	if (!base->job_set) {
		log(LDMSD_LINFO,
		    "%s: The job data set named, %s, does not exist. Job "
		    "data will not be associated with the metric values.\n",
		    name, job_set_name);
		base->job_id_idx = -1;
	} else {
		value = av_value(avl, "job_id");
		if (!value)
			value = "job_id";
		base->job_id_idx = ldms_metric_by_name(base->job_set, value);
		if (base->job_id_idx < 0) {
			log(LDMSD_LINFO,
			    "%s: The specified job_set '%s' is missing "
			    "the 'job_id' attribute and cannot be used.\n",
			    name, job_set_name);
			goto einval;
		}
		value = av_value(avl, "app_id");
		if (!value)
			value = "app_id";
		base->app_id_idx = ldms_metric_by_name(base->job_set, value);
		if (base->app_id_idx < 0) {
			log(LDMSD_LINFO,
			    "%s: The specified job_set '%s' is missing "
			    "the 'app_id' attribute and cannot be used.\n",
			    name, job_set_name);
			goto einval;
		}
		value = av_value(avl, "job_start");
		if (!value)
			value = "job_start";
		base->job_start_idx = ldms_metric_by_name(base->job_set, value);
		if (base->job_start_idx < 0) {
			log(LDMSD_LINFO,
			    "%s: The specified job_set '%s' is missing "
			    "the 'job_start' attribute and cannot be used.\n",
			    name, job_set_name);
			goto einval;
		}
		value = av_value(avl, "job_end");
		if (!value)
			value = "job_end";
		base->job_end_idx = ldms_metric_by_name(base->job_set, value);
		if (base->job_end_idx < 0) {
			log(LDMSD_LERROR,
			    "%s: The specified job_set '%s' is missing "
			    "the 'job_end' attribute and cannot be used.\n",
			    name, job_set_name);
			goto einval;
		}
	}
	/* uid, gid, permission */
	value = av_value(avl, "uid");
	if (value) {
		if (isalpha(value[0])) {
			/* Try to lookup the user name */
			struct passwd *pwd = getpwnam(value);
			if (!pwd) {
				log(LDMSD_LERROR,
				    "%s: The specified user '%s' does not exist\n",
				    value);
				goto einval;
			}
			base->uid = pwd->pw_uid;
		} else {
			base->uid = strtol(value, NULL, 0);
		}
	} else {
		base->uid = geteuid();
	}
	value = av_value(avl, "gid");
	if (value) {
		if (isalpha(value[0])) {
			/* Try to lookup the group name */
			struct group *grp = getgrnam(value);
			if (!grp) {
				log(LDMSD_LERROR,
				    "%s: The specified group '%s' does not exist\n",
				    value);
				goto einval;
			}
			base->gid = grp->gr_gid;
		} else {
			base->gid = strtol(value, NULL, 0);
		}
	} else {
		base->gid = getegid();
	}
	value = av_value(avl, "perm");
	if (value && value[0] != '0') {
		log(LDMSD_LINFO,
		    "%s: Warning, the permission bits '%s' are not specified "
		    "as an Octal number.\n",
		    value);
	}
	base->perm = (value)?(strtol(value, NULL, 0)):(0777);
	value = av_value(avl, "set_array_card");
	base->set_array_card = (value)?(strtol(value, NULL, 0)):(1);
	return base;
einval:
	errno = EINVAL;
	base_del(base);
	return NULL;
}

ldms_schema_t base_schema_new(base_data_t base)
{
	int rc;
	base->schema = ldms_schema_new(base->schema_name);
	if (!base->schema) {
		errno = ENOMEM;
		goto err_0;
	}

	rc = ldms_schema_meta_add(base->schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		errno = rc;
		goto err_1;
	}

	rc = ldms_schema_metric_add(base->schema, "job_id", LDMS_V_U64);
	if (rc < 0) {
		errno = ENOMEM;
		goto err_1;
	}

	rc = ldms_schema_metric_add(base->schema, "app_id", LDMS_V_U64);
	if (rc < 0) {
		errno = ENOMEM;
		goto err_1;
	}
	rc = ldms_schema_array_card_set(base->schema, base->set_array_card);
	if (rc < 0) {
		errno = rc;
		goto err_1;
	}
	return base->schema;
 err_1:
	ldms_schema_delete(base->schema);
	base->schema = NULL;
 err_0:
	return NULL;
}

ldms_set_t base_set_new(base_data_t base)
{
	int rc;
	base->set = ldms_set_new(base->instance_name, base->schema);
	if (!base->set) {
		size_t ssz = ldms_schema_set_size(base->instance_name,
			base->schema);
		if (ssz) {
			base->log(LDMSD_LERROR, "ldms_set_new failed for %s. Needs %zu additional memory reservation.\n",
			base->instance_name, ssz);
		} else {
			base->log(LDMSD_LERROR, "ldms_set_new failed for %s. Schema problem.\n");
		}
		return NULL;
	}
	ldms_set_producer_name_set(base->set, base->producer_name);
	ldms_metric_set_u64(base->set, BASE_COMPONENT_ID, base->component_id);
	ldms_metric_set_u64(base->set, BASE_JOB_ID, 0);
	ldms_metric_set_u64(base->set, BASE_APP_ID, 0);
	ldms_set_config_auth(base->set, base->uid, base->gid, base->perm);
	rc = ldms_set_publish(base->set);
	if (rc) {
		ldms_set_delete(base->set);
		base->set = NULL;
		errno = rc;
		return NULL;
	}
	ldmsd_set_register(base->set, base->pi_name);
	return base->set;
}

void base_sample_begin(base_data_t base)
{
	uint64_t job_id = 0;
	uint64_t app_id = 0;
	uint32_t start, end;
	struct ldms_timestamp ts;
	if (!base->set)
		return;

	ldms_transaction_begin(base->set);
	if (!base->job_set)
		return;

	start = ldms_metric_get_u64(base->job_set, base->job_start_idx);
	end = ldms_metric_get_u64(base->job_set, base->job_end_idx);
	ts = ldms_transaction_timestamp_get(base->set);

	if ((ts.sec >= start) && ((end == 0) || (ts.sec <= end))) {
		job_id = ldms_metric_get_u64(base->job_set, base->job_id_idx);
		app_id = ldms_metric_get_u64(base->job_set, base->app_id_idx);
	}
	ldms_metric_set_u64(base->set, BASE_JOB_ID, job_id);
	ldms_metric_set_u64(base->set, BASE_APP_ID, app_id);
}

void base_sample_end(base_data_t base)
{
	if (!base->set)
		return;
	ldms_transaction_end(base->set);
}
