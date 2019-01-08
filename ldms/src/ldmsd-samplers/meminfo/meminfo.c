/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2011-2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file meminfo.c
 * \brief /proc/meminfo data provider
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

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

static char *procfile = "/proc/meminfo";

typedef struct meminfo_inst_s *meminfo_inst_t;
struct meminfo_inst_s {
	struct ldmsd_plugin_inst_s base;

	FILE *mf;
};

/* ============== Sampler Plugin APIs ================= */

static
int meminfo_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	meminfo_inst_t inst = (void*)pi;
	char *s;
	char lbuf[256];
	char name[128];
	char unit[16];
	uint64_t value;
	int rc;

	rc = fseek(inst->mf, 0, SEEK_SET);
	if (rc)
		return errno;
	while ((s = fgets(lbuf, sizeof(lbuf), inst->mf))) {
		unit[0] = 0;
		rc = sscanf(lbuf, "%[^:]: %lu %s" , name, &value, unit);
		if (rc < 2) {
			ldmsd_log(LDMSD_LWARNING, "%s: bad format %s\n",
				  pi->inst_name, s);
			/* unit may not be present */
			return EINVAL;
		}

		rc = ldms_schema_metric_add(schema, name, LDMS_V_U64, unit);
		if (rc < 0)
			return -rc; /* rc == -errno */
	}

	return 0;
}

static
int meminfo_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	meminfo_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	char *s;
	char lbuf[256];
	char name[128];
	uint64_t value;
	int i, rc;

	fseek(inst->mf, 0, SEEK_SET);
	i = 0;
	while ((s = fgets(lbuf, sizeof(lbuf), inst->mf))) {
		rc = sscanf(lbuf, "%[^:]: %lu" , name, &value);
		if (rc < 2) {
			/* unit may not be present */
			ldmsd_log(LDMSD_LWARNING, "%s: bad format %s\n",
				  pi->inst_name, s);
			return EINVAL;
		}
		ldms_metric_set_u64(set, samp->first_idx + i, value);
		i++;
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *meminfo_desc(ldmsd_plugin_inst_t pi)
{
	return "meminfo - /proc/meminfo data sampler";
}

static
const char *meminfo_help(ldmsd_plugin_inst_t pi)
{
	return "meminfo does not take extra options.";
}

static
void meminfo_del(ldmsd_plugin_inst_t pi)
{
	/* The undo of meminfo_init */
	meminfo_inst_t inst = (void*)pi;
	if (inst->mf) {
		fclose(inst->mf);
		inst->mf = NULL;
	}
}

static
int meminfo_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	meminfo_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;

	if (inst->mf) {
		snprintf(ebuf, ebufsz, "%s: plugin already configured.\n",
			 pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	inst->mf = fopen(procfile, "r");
	if (!inst->mf) {
		snprintf(ebuf, ebufsz, "%s: cannot open file %s\n",
			 pi->inst_name, procfile);
		return errno;
	}

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
int meminfo_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	meminfo_inst_t inst = (void*)pi;
	/* override update_schema() and update_set() */
	samp->update_schema = meminfo_update_schema;
	samp->update_set = meminfo_update_set;

	inst->mf = NULL;
	return 0;
}

static
struct meminfo_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "meminfo",

                /* Common Plugin APIs */
		.desc   = meminfo_desc,
		.help   = meminfo_help,
		.init   = meminfo_init,
		.del    = meminfo_del,
		.config = meminfo_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	meminfo_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
