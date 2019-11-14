/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017, Sandia Corporation.
 * Copyright (c) 2016, Lawrence Livermore National Security, LLC.
 * Copyright (c) 2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
 *
 * Produced at the Lawrence Livermore National Laboratory. Written by
 * Kathleen Shoga <shoga1@llnl.gov> (Lawrence Livermore National Lab)
 *
 * Ported to LDMSD Plugin Instance API by Open Grid Computing.
 *
 * LLNL-CODE-685879 All rights reserved.
 * This file is part of EDAC Plugin, Version 1.0
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (as published
 * by the Free Software Foundation) version 2, dated June 1991.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the terms
 * and conditions of the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Please see LLNL_LICENSE_EDAC for the full details.
 *
 * This work was performed under the auspices of the U.S. Department of
 * Energy by Lawrence Livermore National Laboratory under
 * Contract DE-AC52-07NA27344.
 *
 * This plugin uses the template of the other LDMS plugins, but it
 * gives access to different data. The following is the original header.
 *
 *
 * Copyright (c) 2011 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2011 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
 * \file edac.c
 * \Grab edac data; based off of clock.c.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <dirent.h>
// extra stuff not needed.
#include <stdlib.h> // needed for strtoull processing of comp_id
//#include <string.h> // needed for memcpy in ldms.h unused feature

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, \
		INST(inst)->inst_name, ## __VA_ARGS__ )

#define MAXSZ 70

typedef struct edac_inst_s *edac_inst_t;
struct edac_inst_s {
	struct ldmsd_plugin_inst_s base;

	int edac_valid; /* sample disabled until configured correctly */
	int max_mc;
	int max_csrow;
	int totalCommands;

	char command[MAXSZ][MAXSZ+1];
	char edac_name[MAXSZ][MAXSZ];
};

static char *replace_slash(char *s)
{
	char *s1;

	s1 = s;
	while ( *s1 ) {
		if ( *s1 == '/' ) {
			*s1 = '_';
		}
		++ s1;
	}
	return s;
}

/* ============== Sampler Plugin APIs ================= */

static
int edac_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	edac_inst_t inst = (void*)pi;
	FILE * myFile;
	int i, rc;
	int counter=0;
	int mc_counter=0;
	int csrow_counter=0;

	for (mc_counter=0; mc_counter < inst->max_mc; mc_counter++)
	{
		sprintf(inst->command[counter], "/sys/devices/system/edac/mc/mc%d/ce_count",mc_counter);
		counter++;
		sprintf(inst->command[counter], "/sys/devices/system/edac/mc/mc%d/ce_noinfo_count",mc_counter);
		counter++;
		sprintf(inst->command[counter], "/sys/devices/system/edac/mc/mc%d/ue_count",mc_counter);
		counter++;
		sprintf(inst->command[counter], "/sys/devices/system/edac/mc/mc%d/ue_noinfo_count",mc_counter);
		counter++;
		for (csrow_counter=0; csrow_counter < inst->max_csrow; csrow_counter++)
		{
			sprintf(inst->command[counter],"/sys/devices/system/edac/mc/mc%d/csrow%d/ce_count",mc_counter,csrow_counter);
			counter++;
			sprintf(inst->command[counter],"/sys/devices/system/edac/mc/mc%d/csrow%d/ue_count",mc_counter,csrow_counter);
			counter++;
			sprintf(inst->command[counter],"/sys/devices/system/edac/mc/mc%d/csrow%d/ch0_ce_count",mc_counter,csrow_counter);
			counter++;
		}
		sprintf(inst->command[counter], "/sys/devices/system/edac/mc/mc%d/seconds_since_reset",mc_counter);
		counter++;
	}

	// Running through commands to check if they are all available. If not, then skip rest of setup and send error
	for ( i=0; i<inst->totalCommands; i+=1 )
	{
		myFile = fopen(inst->command[i], "r");
		if (myFile == NULL)
		{
			INST_LOG(inst, LDMSD_LERROR,
				 "failed to open file during config.\n");
			inst->edac_valid=0;
			return EINVAL;
		}
		fclose(myFile);
	}

	// Setting the names
	for (i=0; i<inst->totalCommands; i++)
	{
		strcpy(inst->edac_name[i], inst->command[i]+28);
		replace_slash(inst->edac_name[i]);
	}

	for(i = 0; i < inst->totalCommands; i++ )
	{
		rc = ldms_schema_metric_add(schema, inst->edac_name[i], LDMS_V_U64, "");
		if (rc < 0) {
			INST_LOG(inst, LDMSD_LERROR, "metric_add failed %s\n",
				 inst->edac_name[i]);
			return -rc; /* rc == -errno */
		}
	}
	return 0;
}

static
int edac_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	edac_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int rc;
	int metric_no;
	union ldms_value v;
	FILE * myFile = NULL;
	char * s;
	char lineBuffer[256];
	int i;

	if (!inst->edac_valid)
		return 0;

	metric_no = samp->first_idx;
	for ( i=0; i<inst->totalCommands; i+=1 )
	{
		myFile = fopen(inst->command[i], "r");
		if (myFile == NULL)
		{
			inst->edac_valid = 0;
			INST_LOG(inst, LDMSD_LERROR,
				 "failed to open file: %s\n", inst->command[i]);
			return EINVAL;
		}
		s = fgets(lineBuffer, sizeof(lineBuffer), myFile);
		if (!s) {
			inst->edac_valid = 0;
			return EINVAL;
		}
		rc = sscanf(lineBuffer, "%" PRIu64, &v.v_u64);
		if (rc != 1) {
			INST_LOG(inst, LDMSD_LERROR, "read as uint64_t failed.\n");
			inst->edac_valid=0;
			return EINVAL;
		}
		ldms_metric_set(set, metric_no, &v);
		metric_no++;
		fclose(myFile);
		myFile = NULL;
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *edac_desc(ldmsd_plugin_inst_t pi)
{
	return "edac - edac sampler plugin";
}

static
char *_help =
"config name=INST [COMMON_OPTIONS] max_mc=<max_mc> max_csrow=<max_csrow>\n"
"\n"
"    <max_mc>      The max number of mc.\n"
"    <max_csrow>   The max number of csrows per mc.\n";

static
const char *edac_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void edac_del(ldmsd_plugin_inst_t pi)
{
	/* do nothing */
}

static
int edac_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	edac_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	json_entity_t value;
	const char *mvalue;
	int rc;
	long tmp;

	if (inst->edac_valid) {
		snprintf(ebuf, ebufsz, "%s: already configured.\n",
			 pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */
	value = json_value_find(json, "max_mc");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			snprintf(ebuf, ebufsz, "%s: The given 'max_mc' value "
					"is not a string.\n", inst->base.inst_name);
			return EINVAL;
		}
		mvalue = json_value_str(value)->str;
		tmp = strtol(mvalue, NULL,10);
		if (tmp > 0)
			inst->max_mc = (int)tmp;
		else
		{
			snprintf(ebuf, ebufsz,
				 "%s: Plugin input %s for max_mc not valid.\n",
				 pi->inst_name, mvalue);
			return EINVAL;
		}
	}

	value = json_value_find(json, "max_csrow");
	if (value)
	{
		if (value->type != JSON_STRING_VALUE) {
			snprintf(ebuf, ebufsz, "%s: The given 'max_mc' value "
					"is not a string.\n", inst->base.inst_name);
			return EINVAL;
		}
		mvalue = json_value_str(value)->str;
		tmp=strtol(mvalue, NULL,10);
		if (tmp > 0)
			inst->max_csrow = (int)tmp;
		else
		{
			snprintf(ebuf, ebufsz,
				 "%s: Plugin input %s for max_csrow not "
				 "valid.\n", pi->inst_name, mvalue);
			return EINVAL;
		}
	}
	inst->totalCommands = (inst->max_mc*5) + (inst->max_mc*inst->max_csrow*3);
	if (inst->totalCommands > MAXSZ)
	{
		snprintf(ebuf, ebufsz, "%s: Plugin total registers larger than "
			 "allowed. Check max_mc*5+max_mc*max_csrow*3 < %d "
			 "(max_mc: %d, max_csrow: %d)\n",
			 pi->inst_name, MAXSZ, inst->max_mc, inst->max_csrow);
		return EINVAL;
	}

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;
	inst->edac_valid = 1;
	return 0;
}

static
int edac_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = edac_update_schema;
	samp->update_set = edac_update_set;

	return 0;
}

static
struct edac_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "edac",

                /* Common Plugin APIs */
		.desc   = edac_desc,
		.help   = edac_help,
		.init   = edac_init,
		.del    = edac_del,
		.config = edac_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	edac_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
