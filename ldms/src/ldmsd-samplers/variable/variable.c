/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
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
 * \file variable.c
 * \brief 'variable set' test data provider
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

static const char *namebase = "metnum";

typedef struct variable_inst_s *variable_inst_t;
struct variable_inst_s {
	struct ldmsd_plugin_inst_s base;

	int sameticks;
	int curtick;
	int maxmets;
	int minmets;
	int curmets;
	uint64_t mval;

	int (*sampler_sample)(ldmsd_plugin_inst_t);
};

/* ============== Sampler Plugin APIs ================= */

static
int variable_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	variable_inst_t inst = (void*)pi;
	int rc, i;
	#define NMSZ 128
	char metric_name[NMSZ];

	i = 0;
	while (i < inst->curmets) {
		snprintf(metric_name, NMSZ, "%s%d", namebase, i);
		INST_LOG(inst, LDMSD_LDEBUG, "add metric %s\n", metric_name);
		rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64, "");
		if (rc < 0) {
			INST_LOG(inst, LDMSD_LERROR, "failed metric add %s\n",
				metric_name);
			rc = ENOMEM;
			return -rc;
		}
		i++;
	}
	return 0;
}

static
int variable_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	variable_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int i;
	for (i = 0; i < inst->curmets; i++) {
		ldms_metric_set_u64(set, i + samp->first_idx, inst->mval++);
	}
	return 0;
}

static int variable_sample(ldmsd_plugin_inst_t pi)
{
	variable_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	struct ldmsd_set_entry_s *sent;
	ldms_set_t set;
	ldms_schema_t schema;

	inst->curtick++;
	if (inst->sameticks == inst->curtick) {
		INST_LOG(inst, LDMSD_LDEBUG, "set needs update\n");
		inst->curtick = 0;
		inst->curmets++;
		if (inst->curmets > inst->maxmets) {
			inst->curmets = inst->minmets;
		}
		if ((sent = LIST_FIRST(&samp->set_list))) {
			LIST_REMOVE(sent, entry);
			ldms_set_delete(sent->set);
			free(sent);
		}
		schema = samp->create_schema(&inst->base);
		if (!schema) {
			INST_LOG(inst, LDMSD_LERROR,
				 "schema creation failed.\n");
			return errno;
		}
		set = samp->create_set(&inst->base, samp->set_inst_name,
								schema, NULL);
		ldms_schema_delete(schema);
		if (!set) {
			INST_LOG(inst, LDMSD_LERROR,
				 "failed to recreate a metric set.\n");
			return errno;
		}
	}
	inst->sampler_sample(pi);
	return 0;
}

/* ============== Common Plugin APIs ================= */

static
const char *variable_desc(ldmsd_plugin_inst_t pi)
{
	return "variable - variable sampler plugin";
}

static
char *_help = "\
variable takes no extra options.\n\
";

static
const char *variable_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int variable_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	variable_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	ldms_schema_t schema;
	ldms_set_t set;
	int rc;

	rc = samp->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	/* create schema + set */
	schema = samp->create_schema(pi);
	if (!schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, schema, NULL);
	ldms_schema_delete(schema);
	if (!set)
		return errno;
	return 0;
}

static
void variable_del(ldmsd_plugin_inst_t pi)
{
	/* do nothing */
}

static
int variable_init(ldmsd_plugin_inst_t pi)
{
	variable_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	/* override update_schema() and update_set() */
	samp->update_schema = variable_update_schema;
	samp->update_set = variable_update_set;

	/* save sampler->sample before overriding it */
	inst->sampler_sample = samp->sample;
	/* overriding sample() so that we can create / destroy set */
	samp->sample = variable_sample;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct variable_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "variable",

                /* Common Plugin APIs */
		.desc   = variable_desc,
		.help   = variable_help,
		.init   = variable_init,
		.del    = variable_del,
		.config = variable_config,
	},
	.sameticks = 4,
	.maxmets   = 9,
	.minmets   = 1,
	.curmets   = 1,
};

ldmsd_plugin_inst_t new()
{
	variable_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
