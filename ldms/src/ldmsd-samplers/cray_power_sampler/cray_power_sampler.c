/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016-2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file cray_power_sampler.c
 *
 * This sampler uses \c set_array feature to sample the data at a higher
 * frequency than data aggregation over the network.
 */
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

typedef enum {
	CPS_FIRST,
	CPS_ENERGY = CPS_FIRST,
	/* CPS_FRESHNESS, */
	/* CPS_GENERATION, */
	CPS_POWER,
	/* CPS_POWER_CAP, */
	/* CPS_STARTUP, */
	/* CPS_VERSION, */
	CPS_LAST,
} cps_metric_type;

static const char *cps_names[] = {
	[CPS_ENERGY]      =  "energy",
	/* [CPS_FRESHNESS]   =  "freshness", */
	/* [CPS_GENERATION]  =  "generation", */
	[CPS_POWER]       =  "power",
	/* [CPS_POWER_CAP]   =  "power_cap", */
	/* [CPS_STARTUP]     =  "startup", */
	/* [CPS_VERSION]     =  "version", */
};

static const char *cps_files[] = {
	[CPS_ENERGY]      =  "/sys/cray/pm_counters/energy",
	/* [CPS_FRESHNESS]   =  "/sys/cray/pm_counters/freshness", */
	/* [CPS_GENERATION]  =  "/sys/cray/pm_counters/generation", */
	[CPS_POWER]       =  "/sys/cray/pm_counters/power",
	/* [CPS_POWER_CAP]   =  "/sys/cray/pm_counters/power_cap", */
	/* [CPS_STARTUP]     =  "/sys/cray/pm_counters/startup", */
	/* [CPS_VERSION]     =  "/sys/cray/pm_counters/version", */
};

static const char *cps_units[] = {
	[CPS_ENERGY]      =  "J",
	/* [CPS_FRESHNESS]   =  "", */
	/* [CPS_GENERATION]  =  "", */
	[CPS_POWER]       =  "W",
	/* [CPS_POWER_CAP]   =  "W", */
	/* [CPS_STARTUP]     =  "", */
	/* [CPS_VERSION]     =  "", */
};

typedef struct cray_power_inst_s *cray_power_inst_t;
struct cray_power_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
	int fd[CPS_LAST];
};

/* ============== Sampler Plugin APIs ================= */

static
int cray_power_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	int i, rc;
	for (i = CPS_FIRST; i < CPS_LAST; i++) {
		rc = ldms_schema_metric_add(schema, cps_names[i], LDMS_V_U64,
					    cps_units[i]);
		if (rc < 0)
			return -rc; /* rc == -errno */
	}
	return 0;
}

static
int cray_power_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	/* Populate set metrics */
	cray_power_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int i, rc;
	off_t off;
	uint64_t v;
	char buff[64];
	for (i = CPS_FIRST; i < CPS_LAST; i++) {
		off = lseek(inst->fd[i], 0, SEEK_SET);
		if (off < 0) {
			/* seek error */
			ldmsd_log(LDMSD_LDEBUG, "%s: lseek() error, "
				  "errno: %d, cps_idx: %d\n.",
				  pi->inst_name, errno, i);
			return errno;
		}
		rc = read(inst->fd[i], buff, sizeof(buff));
		if (rc < 0) {
			ldmsd_log(LDMSD_LDEBUG, "%s: read() error, "
				  "errno: %d, cps_idx: %d\n.",
				  pi->inst_name, errno, i);
			return errno;
		}
		v = strtoul(buff, NULL, 0);
		ldms_metric_set_u64(set, samp->first_idx + i, v);
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *cray_power_desc(ldmsd_plugin_inst_t i)
{
	return "cray_power_sampler - cray_power sampler plugin";
}

static char *help_str = "\
cray_power_sampler - specific synopsis \n\
\n\
    config [COMMON_OPTIONS] [set_array_card=<INT>] \n\
\n\
Option descriptions:\n\
\n\
    set_array_card (Optional) the number of elements in the ring buffer\n\
                   (default: 600 -- for 10 Hz sampling * 60 sec collection).\n\
";

static
const char *cray_power_help(ldmsd_plugin_inst_t i)
{
	return help_str;
}

static
void cray_power_del(ldmsd_plugin_inst_t pi)
{
	/* The undo of cray_power_init */
	cray_power_inst_t inst = (void*)pi;
	int i;
	for (i = CPS_FIRST; i < CPS_LAST; i++) {
		if (inst->fd[i] >= 0)
			close(inst->fd[i]);
	}
}

static
int cray_power_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;
	json_entity_t v;

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */

	/* handling set_array_card with default being 600 */
	v = json_value_find(json, "set_array_card");
	if (!v)
		samp->set_array_card = 600;
	/* otherwise, it has been handled by base.config() */

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
int cray_power_init(ldmsd_plugin_inst_t pi)
{
	cray_power_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int i;

	/* override update_schema() and update_set() */
	samp->update_schema = cray_power_update_schema;
	samp->update_set = cray_power_update_set;

	memset(inst->fd, -1, CPS_LAST * sizeof(int));

	for (i = CPS_FIRST; i < CPS_LAST; i++) {
		inst->fd[i] = open(cps_files[i], O_RDONLY);
		if (inst->fd[i] < 0)
			return errno;
	}

	return 0;
}

static
struct cray_power_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "cray_power_sampler",

                /* Common Plugin APIs */
		.desc   = cray_power_desc,
		.help   = cray_power_help,
		.init   = cray_power_init,
		.del    = cray_power_del,
		.config = cray_power_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	cray_power_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
