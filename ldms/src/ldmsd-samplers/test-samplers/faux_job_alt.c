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
 * \file faux_job_alt.c
 *
 * Faux job sampler -- alternative metric names.
 *
 * This is an alternative faux job sampler plugin that provide faux job_id and
 * app_id that changes every sample.
 *
 * - alt_job_id = now() % 1000
 * - alt_app_id = job_id + 10
 *
 */

#include <sys/time.h>

#include "ldmsd.h"
#include "ldmsd_sampler.h"

typedef struct faux_job_alt_inst_s *faux_job_alt_inst_t;
struct faux_job_alt_inst_s {
	struct ldmsd_plugin_inst_s base;
	int alt_job_id;
	int alt_job_id_idx;
	int alt_app_id_idx;
	int alt_job_start_idx;
	int alt_job_end_idx;
};

/* ============== Sampler Plugin APIs ================= */
static
ldms_schema_t faux_job_alt_create_schema(ldmsd_plugin_inst_t i)
{
	faux_job_alt_inst_t inst = (void*)i;
	int idx;
	ldmsd_sampler_type_t samp = (void*)i->base;
	ldms_schema_t schema = ldms_schema_new(samp->schema_name);
	if (!schema)
		goto out;

	idx = ldms_schema_metric_add(schema, "alt_job_id", LDMS_V_U64, "");
	if (idx < 0)
		goto idx_err;
	inst->alt_job_id_idx = idx;

	idx = ldms_schema_metric_add(schema, "alt_app_id", LDMS_V_U64, "");
	if (idx < 0)
		goto idx_err;
	inst->alt_app_id_idx = idx;

	idx = ldms_schema_metric_add(schema, "alt_job_start", LDMS_V_U64,
				     "sec");
	if (idx < 0)
		goto idx_err;
	inst->alt_job_start_idx = idx;

	idx = ldms_schema_metric_add(schema, "alt_job_end", LDMS_V_U64, "sec");
	if (idx < 0)
		goto idx_err;
	inst->alt_job_end_idx = idx;

	goto out;

 idx_err:
	errno = -idx;
	ldms_schema_delete(schema);
 out:
	return schema;
}

static
int faux_job_alt_update_set(ldmsd_plugin_inst_t i, ldms_set_t set, void *ctxt)
{
	faux_job_alt_inst_t inst = (void*)i;
	struct timeval tv;

	inst->alt_job_id += 1;

	gettimeofday(&tv, NULL);

	/* alt_job_id */
	ldms_metric_set_u64(set, inst->alt_job_id_idx, inst->alt_job_id);

	/* alt_app_id */
	ldms_metric_set_u64(set, inst->alt_app_id_idx, 10 + inst->alt_job_id);

	/* alt_job_start */
	ldms_metric_set_u64(set, inst->alt_job_start_idx, tv.tv_sec - 60);

	/* alt_job_end */
	ldms_metric_set_u64(set, inst->alt_job_end_idx, tv.tv_sec + 60);

	/*
	 * NOTE: job_start -- job_end spanning 60s before and after _now_
	 *       to make the job current.
	 */
	return 0;
}

/* ============== Common Plugin APIs ================= */

static
const char *faux_job_alt_desc(ldmsd_plugin_inst_t i)
{
	return "faux_job_alt - faux_job_alt alternative sampler plugin";
}

static
const char *faux_job_alt_help(ldmsd_plugin_inst_t i)
{
	return "faux_job_alt sampler takes no extra options.";
}

static
void faux_job_alt_del(ldmsd_plugin_inst_t i)
{
	/* The undo of faux_job_alt_init */
}

static
int faux_job_alt_config(ldmsd_plugin_inst_t i, struct attr_value_list *avl,
			struct attr_value_list *kwl, char *ebuf, int ebufsz)
{
	faux_job_alt_inst_t inst = (void*)i;
	ldmsd_sampler_type_t samp = (void*)i->base;
	ldms_set_t set;
	int rc;

	rc = samp->base.config(&inst->base, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	/* create schema + set */
	samp->schema = samp->create_schema(i);
	if (!samp->schema) {
		snprintf(ebuf, ebufsz, "Cannot create schema, rc: %d\n", errno);
		return errno;
	}
	set = samp->create_set(i, samp->set_inst_name, samp->schema, NULL);
	if (!set) {
		snprintf(ebuf, ebufsz, "Cannot create set, rc: %d\n", errno);
		return errno;
	}
	return 0;
}

static
int faux_job_alt_init(ldmsd_plugin_inst_t i)
{
	ldmsd_sampler_type_t samp = (void*)i->base;

	/* override create schema so that it doesn't have `job_id`
	 * and `app_id` metrics. */
	samp->create_schema = faux_job_alt_create_schema;

	/* override update_set() */
	samp->update_set = faux_job_alt_update_set;

	return 0;
}

static
struct faux_job_alt_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "faux_job_alt",

                /* Common Plugin APIs */
		.desc   = faux_job_alt_desc,
		.help   = faux_job_alt_help,
		.init   = faux_job_alt_init,
		.del    = faux_job_alt_del,
		.config = faux_job_alt_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	faux_job_alt_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
