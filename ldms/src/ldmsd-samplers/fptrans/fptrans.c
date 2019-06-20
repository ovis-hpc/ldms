/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file fptrans.c
 * \brief store fp value for testing transmission correctness.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h> // needed for strtoull processing of comp_id
#include <math.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

typedef struct fptrans_inst_s *fptrans_inst_t;
struct fptrans_inst_s {
	struct ldmsd_plugin_inst_s base;
};

/* ============== Sampler Plugin APIs ================= */

static
int fptrans_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	int rc;
	rc = ldms_schema_metric_add(schema, "float_pi", LDMS_V_F32, "");
	if (rc < 0)
		goto err;
	/* add double metric */
	rc = ldms_schema_metric_add(schema, "double_pi", LDMS_V_D64, "");
	if (rc < 0)
		goto err;
#define ASZ 2
	rc = ldms_schema_metric_array_add(schema, "float_array_pi",
					  LDMS_V_F32_ARRAY, "", ASZ);
	if (rc < 0)
		goto err;
	rc = ldms_schema_metric_array_add(schema, "double_array_pi",
					  LDMS_V_D64_ARRAY, "", ASZ);
	if (rc < 0)
		goto err;

	rc = 0;

 err:
	if (rc < 0)
		rc = -rc;
	return rc;
}

static
int fptrans_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	/* Populate set metrics */
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int i, idx = samp->first_idx;
	ldms_metric_set_float(set, idx++, M_PI);
	ldms_metric_set_double(set, idx++, M_PI);

	ldms_metric_array_set_float(set, idx, 0, 1);
	for (i = 1; i < ASZ; i++) {
		ldms_metric_array_set_float(set, idx, i, M_PI);
	}
	idx++;

	ldms_metric_array_set_double(set, idx, 0, 0);
	for (i = 1; i < ASZ; i++) {
		ldms_metric_array_set_double(set, idx, i, M_PI);
	}
	idx++;
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *fptrans_desc(ldmsd_plugin_inst_t pi)
{
	return "fptrans - store fp value for testing transmission correctness";
}

static
const char *fptrans_help(ldmsd_plugin_inst_t pi)
{
	return "fptrans only have common sampler options.";
}

static
void fptrans_del(ldmsd_plugin_inst_t pi)
{
	/* The undo of fptrans_init */
}

static
int fptrans_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;
	return 0;
}

static
int fptrans_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = fptrans_update_schema;
	samp->update_set = fptrans_update_set;

	return 0;
}

static
struct fptrans_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "fptrans",

                /* Common Plugin APIs */
		.desc   = fptrans_desc,
		.help   = fptrans_help,
		.init   = fptrans_init,
		.del    = fptrans_del,
		.config = fptrans_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	fptrans_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
