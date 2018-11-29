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
 * \file faux_job.c
 */

/*
 * See `_help` variable below for plugin usage.
 */

#include <sys/time.h>

#include "ldmsd.h"
#include "ldmsd_sampler.h"

typedef struct faux_job_inst_s *faux_job_inst_t;
struct faux_job_inst_s {
	struct ldmsd_plugin_inst_s base;
	int job_id;
	int job_id_idx;
	int app_id;
	int app_id_idx;
	int job_start_idx;
	int job_end_idx;
};

/* ============== Sampler Plugin APIs ================= */

static
int faux_job_update_schema(ldmsd_plugin_inst_t i, ldms_schema_t schema)
{
	faux_job_inst_t inst = (void*)i;
	int idx;

	/* `job_id` and `app_id` has already been added */

	idx = ldms_schema_metric_add(schema, "job_start", LDMS_V_U64, "sec");
	if (idx < 0)
		return -idx;
	inst->job_start_idx = idx;

	idx = ldms_schema_metric_add(schema, "job_end", LDMS_V_U64, "sec");
	if (idx < 0)
		return -idx;
	inst->job_end_idx = idx;

	return 0;
}

static
int faux_job_update_set(ldmsd_plugin_inst_t i, ldms_set_t set, void *ctxt)
{
	faux_job_inst_t inst = (void*)i;
	struct timeval tv;

	gettimeofday(&tv, NULL);

	/* job_id */
	ldms_metric_set_u64(set, inst->job_id_idx, inst->job_id++);

	/* app_id */
	ldms_metric_set_u64(set, inst->app_id_idx, inst->app_id++);

	/* job_start */
	ldms_metric_set_u64(set, inst->job_start_idx, tv.tv_sec - 60);

	/* job_end */
	ldms_metric_set_u64(set, inst->job_end_idx, tv.tv_sec + 60);

	/*
	 * NOTE: job_start -- job_end spanning 60s before and after _now_
	 *       to make the job current.
	 */
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *faux_job_desc(ldmsd_plugin_inst_t i)
{
	return "faux_job - faux_job sampler plugin";
}

const char *_help = "\
faux_job sampler synopsis:\n\
    config inst=NAME [COMMON_OPTIONS] [job_id=NUM] [app_id=NUM]\n\
\n\
Descriptions:\n\
    faux_job sampler creats a set that contains jobinfo metrics  (job_id, \n\
    app_id, job_start, job_end). The job_id and app_id are incremented by one\n\
    for every sampling interval. The initial value of job_id and app_id can \n\
    be set by `job_id` and `app_id` parameter respectively.\n\
    \n\
    The default `job_id` is 1, and the default `app_id` is 10.\n\
\n\
Example:\n\
    ```\n\
    load name=jobinfo plugin=faux_job \n\
    config inst=jobinfo interval=3600000000 offset=0 job_id=10 app_id=20 \n\
    # change job_id, app_id every 1 hour \n\
    start  name=jobinfo \n\
    ```\n\
\n\
";

static
const char *faux_job_help(ldmsd_plugin_inst_t i)
{
	return _help;
}

static
void faux_job_del(ldmsd_plugin_inst_t i)
{
	/* The undo of faux_job_init */
}

static
int faux_job_config(ldmsd_plugin_inst_t i, struct attr_value_list *avl,
		    struct attr_value_list *kwl, char *ebuf, int ebufsz)
{
	faux_job_inst_t inst = (void*)i;
	ldmsd_sampler_type_t samp = (void*)i->base;
	ldms_set_t set;
	int rc;
	char *val;

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
	inst->job_id_idx = ldms_metric_by_name(set, "job_id");
	inst->app_id_idx = ldms_metric_by_name(set, "app_id");

	val = av_value(avl, "job_id");
	inst->job_id = val?atoi(val):1;

	val = av_value(avl, "app_id");
	inst->app_id = val?atoi(val):10;

	ldms_transaction_begin(set);
	faux_job_update_set(i, set, NULL);
	ldms_transaction_end(set);
	return 0;
}

static
int faux_job_init(ldmsd_plugin_inst_t i)
{
	ldmsd_sampler_type_t samp = (void*)i->base;
	/* override update_schema() and update_set() */
	samp->update_schema = faux_job_update_schema;
	samp->update_set = faux_job_update_set;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct faux_job_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "faux_job",

                /* Common Plugin APIs */
		.desc   = faux_job_desc,
		.help   = faux_job_help,
		.init   = faux_job_init,
		.del    = faux_job_del,
		.config = faux_job_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	faux_job_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
