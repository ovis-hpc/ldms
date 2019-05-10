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
 * \file clock.c
 */

#include <sys/time.h>

#include "ldmsd.h"
#include "ldmsd_sampler.h"

typedef struct clock_inst_s *clock_inst_t;
struct clock_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
};

/* ============== Sampler Plugin APIs ================= */

static
int clock_update_schema(ldmsd_plugin_inst_t i, ldms_schema_t schema)
{
	int idx;

	idx = ldms_schema_metric_add(schema, "tv_sec", LDMS_V_U64, "sec");
	if (idx < 0)
		return -idx;
	idx = ldms_schema_metric_add(schema, "tv_usec", LDMS_V_U64, "usec");
	if (idx < 0)
		return -idx;
	return 0;
}

static
int clock_update_set(ldmsd_plugin_inst_t i, ldms_set_t set, void *ctxt)
{
	ldmsd_sampler_type_t samp = (void*)i->base;
	struct timeval tv;

	gettimeofday(&tv, 0);

	ldms_metric_set_u64(set, samp->first_idx, tv.tv_sec);

	ldms_metric_set_u64(set, samp->first_idx + 1, tv.tv_usec);

	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *clock_desc(ldmsd_plugin_inst_t i)
{
	return "clock - clock sampler plugin";
}

static
const char *clock_help(ldmsd_plugin_inst_t i)
{
	return "clock sampler plugin takes no extra options";
}

static
void clock_del(ldmsd_plugin_inst_t i)
{
	/* The undo of clock_init */
}

static
int clock_config(ldmsd_plugin_inst_t i, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	ldmsd_sampler_type_t samp = (void*)i->base;
	ldms_set_t set;
	int rc;

	rc = samp->base.config(i, json, ebuf, ebufsz);
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
int clock_init(ldmsd_plugin_inst_t i)
{
	ldmsd_sampler_type_t samp = (void*)i->base;
	/* override update_schema() and update_set() */
	samp->update_schema = clock_update_schema;
	samp->update_set = clock_update_set;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct clock_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "clock",

                /* Common Plugin APIs */
		.desc   = clock_desc,
		.help   = clock_help,
		.init   = clock_init,
		.del    = clock_del,
		.config = clock_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	clock_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
