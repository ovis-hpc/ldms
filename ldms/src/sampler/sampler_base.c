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
	free(base->job_set_name);
	free(base);
}

static void init_job_data(base_data_t base)
{
	if (base->job_set)
		return;

	base->job_set = ldms_set_by_name(base->job_set_name);
	if (!base->job_set) {
		if (!base_missing_warned_set(base)) {
			base_missing_warned_on(base, BASE_WARN_SET);
			base->log(base->job_log_lvl,
			    "%s: The job data set named, %s, does not exist. Valid job "
			    "data will not be associated with the metric values.\n",
			    base->pi_name, base->job_set_name);
		}
		goto err;
	} else {
		if (base_missing_warned_set(base)) {
			base_missing_warned_off(base, BASE_WARN_SET);
			base->log(base->job_log_lvl,
			    "%s: The missing job data set named, %s, has appeared. Valid job "
			    "data is now associated with the metric values.\n",
			    base->pi_name, base->job_set_name);
		}
		base->job_id_idx = ldms_metric_by_name(base->job_set, "job_id");
		if (base->job_id_idx < 0) {
			if (!base_missing_warned_jobid(base)) {
				base_missing_warned_on(base, BASE_WARN_JOBID);
				base->log(base->job_log_lvl,
				    "%s: The specified job_set '%s' is missing "
				    "the 'job_id' attribute and cannot be used.\n",
				    base->pi_name, base->job_set_name);
			}
			goto err;
		} else {
			if (base_missing_warned_jobid(base)) {
				base_missing_warned_off(base, BASE_WARN_JOBID);
				base->log(base->job_log_lvl,
				    "%s: The specified job_set '%s' now has "
				    "the 'job_id' attribute and will be used.\n",
				    base->pi_name, base->job_set_name);
			}
		}
		/* app_id is optional */
		base->app_id_idx = ldms_metric_by_name(base->job_set, "app_id");

		/* If job_slot_list is present, we know it's the mt-slurm sampler */
		base->job_slot_list_idx = ldms_metric_by_name(base->job_set, "job_slot_list");
		base->job_slot_list_tail_idx = ldms_metric_by_name(base->job_set, "job_slot_list_tail");

		base->job_start_idx = ldms_metric_by_name(base->job_set, "job_start");
		if (base->job_start_idx < 0) {
			if (!base_missing_warned_start(base)) {
				base_missing_warned_on(base, BASE_WARN_START);
				base->log(base->job_log_lvl,
				    "%s: The specified job_set '%s' is missing "
				    "the 'job_start' attribute and cannot be used.\n",
				    base->pi_name, base->job_set_name);
			}
			goto err;
		} else {
			if (base_missing_warned_start(base)) {
				base_missing_warned_off(base, BASE_WARN_START);
				base->log(base->job_log_lvl,
				    "%s: The specified job_set '%s' now has "
				    "the 'job_start' attribute and will be used.\n",
				    base->pi_name, base->job_set_name);
			}
		}
		base->job_end_idx = ldms_metric_by_name(base->job_set, "job_end");
		if (base->job_end_idx < 0) {
			if (!base_missing_warned_end(base)) {
				base_missing_warned_on(base, BASE_WARN_END);
				base->log(base->job_log_lvl,
				    "%s: The specified job_set '%s' is missing "
				    "the 'job_end' attribute and cannot be used.\n",
				    base->pi_name, base->job_set_name);
			}
			goto err;
		} else {
			if (base_missing_warned_end(base)) {
				base_missing_warned_off(base, BASE_WARN_END);
				base->log(base->job_log_lvl,
				    "%s: The specified job_set '%s' now has "
				    "the 'job_end' attribute and will be used.\n",
				    base->pi_name, base->job_set_name);
			}
		}
	}
	return;
 err:
	base->job_set = NULL;
	base->job_id_idx = -1;
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

	base->job_log_lvl = LDMSD_LINFO;

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
		base->job_set_name = strdup("job_info");
	else
		base->job_set_name = strdup(job_set_name);

	int rc = base_auth_parse(avl, &base->auth, log);
	if (rc)
		goto einval;

	value = av_value(avl, "set_array_card");
	base->set_array_card = (value)?(strtol(value, NULL, 0)):(1);
	base->log = log;
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

int __set_init(base_data_t base)
{
	int rc;
	ldms_set_producer_name_set(base->set, base->producer_name);
	ldms_metric_set_u64(base->set, BASE_COMPONENT_ID, base->component_id);
	ldms_metric_set_u64(base->set, BASE_JOB_ID, 0);
	ldms_metric_set_u64(base->set, BASE_APP_ID, 0);
	base_auth_set(&base->auth, base->set);

	rc = ldms_set_publish(base->set);
	if (rc) {
		base->log(LDMSD_LERROR,"base_set_new: ldms_set_publish failed for %s\n",
				base->instance_name);
		return rc;
	}
	ldmsd_set_register(base->set, base->pi_name);
	return 0;
}

ldms_set_t base_set_new(base_data_t base)
{
	int rc;
	base->missing_warned = 0;
	errno = 0;
	base->set = ldms_set_new(base->instance_name, base->schema);
	if (!base->set) {
		const char *serr = STRERROR(errno);
		base->log(LDMSD_LERROR,"base_set_new: ldms_set_new failed %d(%s) for %s\n",
				errno, serr, base->instance_name);
		return NULL;
	}
	rc = __set_init(base);
	if (rc) {
		ldms_set_delete(base->set);
		base->set = NULL;
	}
	return base->set;
}

ldms_set_t base_set_new_heap(base_data_t base, size_t heap_sz)
{
	int rc;
	base->missing_warned = 0;
	errno = 0;
	base->set = ldms_set_new_with_heap(base->instance_name, base->schema, heap_sz);
	if (!base->set) {
		const char *serr = STRERROR(errno);
		base->log(LDMSD_LERROR,"base_set_new: ldms_set_new failed %d(%s) for %s\n",
				errno, serr, base->instance_name);
		return NULL;
	}
	rc = __set_init(base);
	if (rc) {
		ldms_set_delete(base->set);
		base->set = NULL;
	}
	return base->set;
}

void base_set_delete(base_data_t base)
{
	if (!base->set)
		return;
	ldmsd_set_deregister(base->instance_name, base->pi_name);
	ldms_set_unpublish(base->set);
	ldms_set_delete(base->set);
	base->set = NULL;
}

void base_sample_begin(base_data_t base)
{
	uint64_t job_id = 0;
	uint64_t app_id = 0;
	uint32_t start, end;
	struct ldms_timestamp ts;

	if (!base->set)
		return;

	/* Check if job data is available */
	if (!base->job_set)
		init_job_data(base);

	ldms_transaction_begin(base->set);
	if (base->job_id_idx < 0)
		return;

	ts = ldms_transaction_timestamp_get(base->set);

	if (base->job_slot_list_idx < 0) {
		start = ldms_metric_get_u64(base->job_set, base->job_start_idx);
		end = ldms_metric_get_u64(base->job_set, base->job_end_idx);
		if ((ts.sec >= start) && ((end == 0) || (ts.sec <= end))) {
			job_id = ldms_metric_get_u64(base->job_set, base->job_id_idx);
			app_id = ldms_metric_get_u64(base->job_set, base->app_id_idx);
		}
	} else {
		int slot_idx = ldms_metric_get_s32(base->job_set, base->job_slot_list_tail_idx);
		int slot = ldms_metric_array_get_s32(base->job_set, base->job_slot_list_idx, slot_idx);
		if (slot < 0) {
			job_id = app_id = 0;
			goto out;
		}
		start = ldms_metric_array_get_u32(base->job_set, base->job_start_idx, slot);
		end = ldms_metric_array_get_u32(base->job_set, base->job_end_idx, slot);
		if ((ts.sec >= start) && ((end == 0) || (ts.sec <= end))) {
			job_id = ldms_metric_array_get_u64(base->job_set, base->job_id_idx, slot);
			app_id = ldms_metric_array_get_u64(base->job_set, base->app_id_idx, slot);
		}
	}
 out:
	ldms_metric_set_u64(base->set, BASE_JOB_ID, job_id);
	ldms_metric_set_u64(base->set, BASE_APP_ID, app_id);
}

void base_sample_end(base_data_t base)
{
	if (!base->set)
		return;
	ldms_transaction_end(base->set);
}

int base_auth_parse(struct attr_value_list *avl, struct base_auth *auth,
		    ldmsd_msg_log_f log)
{
	char *value;
	/* uid, gid, permission */
        auth->uid_is_set = false;
        auth->gid_is_set = false;
        auth->perm_is_set = false;
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
			auth->uid = pwd->pw_uid;
		} else {
			auth->uid = strtol(value, NULL, 0);
		}
		auth->uid_is_set = true;
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
			auth->gid = grp->gr_gid;
		} else {
			auth->gid = strtol(value, NULL, 0);
		}
		auth->gid_is_set = true;
	}
	value = av_value(avl, "perm");
	if (value) {
		long lval, pval;
		errno = 0;
		lval = strtol(value, NULL, 8);
		pval = lval & 0777;
		if (errno || pval != lval || pval == 0) {
			log(LDMSD_LERROR,
			    "The permission bits must specified "
			    "as a non-zero octal number <= 777; got %s.\n", value);
			goto einval;
		}
		auth->perm = pval;
		auth->perm_is_set = true;
	}
	return 0;
einval:
	return 1;
}

void base_auth_set(const struct base_auth *auth, ldms_set_t set)
{
        if (auth->uid_is_set)
                ldms_set_uid_set(set, auth->uid);
        if (auth->gid_is_set)
                ldms_set_gid_set(set, auth->gid);
        if (auth->perm_is_set)
                ldms_set_perm_set(set, auth->perm);
}
