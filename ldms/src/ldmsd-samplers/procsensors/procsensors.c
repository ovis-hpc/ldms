/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2016,2018-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2012-2016,2018-2019 Open Grid Computing, Inc. All rights
 * reserved.
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
 * \file procsensors.c
 * \brief reads from proc the data that populates lm sensors (in*_input, fan*_input, temp*_input)
 *
 * NOTE: data files have to be opened and closed on each file in sys for the data vaules to change.
 * The actual functionality of the data gathering by the system takes time on systems.
 * Sample stores the data locally and then writes it out so that the collection will not occur during
 * partial set. There is therefore some slop in the actaul time for the data point.
 *
 * filename will be the variable name. mysql inserter will have to convert names and downselect which ones
 * to record.
 *
 * FIXME: decideif multipliers should go here....
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

static char* procsensorsfiledir = "/sys/devices/pci0000:00/0000:00:01.1/i2c-1/1-002f/";
const static int vartypes = 3;
const static char* varnames[] = {"in", "fan", "temp"};
const static int varbounds[] = {0, 9, 1, 9, 1, 6};

typedef struct procsensors_inst_s *procsensors_inst_t;
struct procsensors_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
};

/* ============== Sampler Plugin APIs ================= */

static
int procsensors_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	/* procsensors_inst_t inst = (void*)pi; */
	int i, j, rc;
	char metric_name[128];

	/* Add procsensors metrics to the schema */
	/*
	 * Process file to define all the metrics.
	 */

	for (i = 0; i < vartypes; i++){
		for (j = varbounds[2 * i]; j <= varbounds[2 * i + 1]; j++) {
			snprintf(metric_name, sizeof(metric_name),
				 "%s%d_input",varnames[i], j);
			rc = ldms_schema_metric_add(schema, metric_name,
						    LDMS_V_U64, "");
			if (rc < 0)
				return -rc; /* rc = -errno */
		}
	}

	return 0;
}

static
int procsensors_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	procsensors_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;

	int rc;
	int metric_no;
	char *s;
	char procfile[256];
	char lbuf[20];
	int i, j;
	FILE *mf;
	uint64_t val;

	metric_no = samp->first_idx;
	for (i = 0; i < vartypes; i++) {
		for (j = varbounds[2*i]; j <= varbounds[2*i+1]; j++) {
			snprintf(procfile, 255, "%s/%s%d_input",
					procsensorsfiledir, varnames[i],j);

			//FIXME: do we really want to open and close each one?
			mf = fopen(procfile, "r");
			if (!mf) {
				INST_LOG(inst, LDMSD_LERROR,
					 "Could not open the procsensors file "
					 "'%s'...exiting\n", procfile);
				return errno;
			}
			s = fgets(lbuf, sizeof(lbuf), mf);
			if (!s) {
				fclose(mf);
				return errno;
			}
			rc = sscanf(lbuf, "%"PRIu64 "\n", &val);
			if (rc != 1){
				fclose(mf);
				return errno;
			}
			ldms_metric_set_u64(set, metric_no, val);

			fclose(mf);
			metric_no++;
		}
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *procsensors_desc(ldmsd_plugin_inst_t pi)
{
	return "procsensors - procsensors sampler plugin";
}

static
char *_help = "\
procsensors takes no extra options.\n\
";

static
const char *procsensors_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int procsensors_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	procsensors_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	ldms_set_t set;
	int rc;

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
void procsensors_del(ldmsd_plugin_inst_t pi)
{
	/* do nothing */
}

static
int procsensors_init(ldmsd_plugin_inst_t pi)
{
	procsensors_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	/* override update_schema() and update_set() */
	samp->update_schema = procsensors_update_schema;
	samp->update_set = procsensors_update_set;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct procsensors_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "procsensors",

                /* Common Plugin APIs */
		.desc   = procsensors_desc,
		.help   = procsensors_help,
		.init   = procsensors_init,
		.del    = procsensors_del,
		.config = procsensors_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	procsensors_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
