/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2017,2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2017,2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file vmstat.c
 * \brief /proc/vmstat data provider
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

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

static char *procfile = "/proc/vmstat";

typedef struct vmstat_inst_s *vmstat_inst_t;
struct vmstat_inst_s {
	struct ldmsd_plugin_inst_s base;
	FILE *mf;
};

/* ============== Sampler Plugin APIs ================= */

static
int vmstat_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	vmstat_inst_t inst = (void*)pi;
	int rc;
	char *s;
	char lbuf[256];
	char metric_name[128];
	uint64_t mv;

	fseek(inst->mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), inst->mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &mv);
		if (rc < 2) {
			return EINVAL;
		}
		rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64, "");
		if (rc < 0)
			return -rc; /* rc == -errno */
	} while (s);
	return 0;
}

static
int vmstat_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	vmstat_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int rc;
	char *s;
	char lbuf[256];
	char metric_name[128];
	int idx;
	uint64_t mv;

	idx = samp->first_idx;
	fseek(inst->mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), inst->mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &mv);
		if (rc < 2)
			return EINVAL;
		ldms_metric_set_u64(set, idx++, mv);
	} while (s);
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *vmstat_desc(ldmsd_plugin_inst_t pi)
{
	return "vmstat - vmstat sampler plugin";
}

static
char *_help = "\
vmstat takes no extra options.\n\
";

static
const char *vmstat_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void vmstat_del(ldmsd_plugin_inst_t pi)
{
	vmstat_inst_t inst = (void*)pi;

	if (inst->mf)
		fclose(inst->mf);
}

static
int vmstat_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	vmstat_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;

	if (inst->mf) {
		snprintf(ebuf, ebufsz, "%s: already configured.\n",
			 pi->inst_name);
		return EALREADY;
	}

	inst->mf = fopen(procfile, "r");
	if (!inst->mf) {
		snprintf(ebuf, ebufsz, "%s: cannot open file %s, errno: %d\n",
			 pi->inst_name, procfile, errno);
		return errno;
	}

	rc = samp->base.config(pi, avl, kwl, ebuf, ebufsz);
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
int vmstat_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = vmstat_update_schema;
	samp->update_set = vmstat_update_set;

	return 0;
}

static
struct vmstat_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "vmstat",

                /* Common Plugin APIs */
		.desc   = vmstat_desc,
		.help   = vmstat_help,
		.init   = vmstat_init,
		.del    = vmstat_del,
		.config = vmstat_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	vmstat_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
