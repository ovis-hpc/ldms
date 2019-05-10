/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2017,2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2017,2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file aries_mmr.c
 * \brief aries network metric provider (reads gpcd mmr)
 *
 * parses 1 config file. All names go in as is.
 */

#define AR_MAX_LEN 256
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <pthread.h>
#include <linux/limits.h>

#include "aries_mmr.h"

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		 ldmsd_log((lvl), "%s: " fmt, \
		 INST(inst)->inst_name, ##__VA_ARGS__)

static struct listmatch_t LMT[END_LMT] = {
	{"AR_NL_PRF_REQ_", 14},
	{"AR_NL_PRF_RSP_", 14},
	{"AR_RTR_", 7},
	{"AR_NL_PRF_PTILE_", 16},
	{"AR_NIC_", 7}
};

int filterKeepAll(const char* name)
{
	return 1;
}

int filterKeepNic(const char* name)
{
	if (strncmp(LMT[NIC_LMT].header, name, LMT[NIC_LMT].len) == 0)
		return 1;
	else
		return 0;
}

int filterKeepRouter(const char* name)
{
	if (strncmp(LMT[NIC_LMT].header, name, LMT[NIC_LMT].len) == 0)
		return 0;
	else
		return 1;
}

static int parseConfig(aries_mmr_inst_t inst, const char* fname)
{
	FILE *mf;
	char *s;
	char name[AR_MAX_LEN];
	char lbuf[AR_MAX_LEN];
	int countA = 0;
	int rc;

	inst->rawlist = NULL;
	inst->numraw = 0;

	mf = fopen(fname, "r");
	if (!mf){
		INST_LOG(inst, LDMSD_LERROR, "Cannot open file <%s>\n", fname);
		return EINVAL;
	}

	fseek(mf, 0, SEEK_SET);
	//parse once to get the number of metrics
	while ((s = fgets(lbuf, sizeof(lbuf), mf))) {
		rc = sscanf(lbuf," %s", name);
		if ((rc != 1) || (strlen(name) == 0) || (name[0] == '#')){
			INST_LOG(inst, LDMSD_LDEBUG,
				 "aries_mmr: skipping input <%s>\n", lbuf);
			continue;
		}
		rc = inst->filter(name);
		if (!rc) {
			INST_LOG(inst, LDMSD_LDEBUG,
				 "filtering input <%s>\n", lbuf);
			continue;
		}
		countA++;
	}

	if (!countA) {
		rc = ENOENT;
		goto out;
	}

	//parse again to populate the metrics
	fseek(mf, 0, SEEK_SET);
	inst->rawlist = calloc(countA, sizeof(*inst->rawlist));
	if (!inst->rawlist) {
		rc = ENOMEM;
		goto out;
	}
	while ((s = fgets(lbuf, sizeof(lbuf), mf))) {
		rc = sscanf(lbuf," %s", name);
		if ((rc != 1) || (strlen(name) == 0) || (name[0] == '#')){
			continue;
		}
		rc = inst->filter(name);
		if (!rc)
			continue;
		INST_LOG(inst, LDMSD_LDEBUG,
			 "config read <%s>\n", lbuf);
		inst->rawlist[inst->numraw] = strdup(name);
		if (!inst->rawlist[inst->numraw]) {
			rc = ENOMEM;
			goto out;
		}
		inst->numraw++;
		if (inst->numraw == countA)
			break;
	}

	rc = 0;
out:
	fclose(mf);
	return rc;
}

/**
 * Build linked list of tile performance counters we wish to get values for.
 * No aggregation.
 */
int addMetricToContext(aries_mmr_inst_t inst, gpcd_context_t* lctx,
		       const char* met)
{
	gpcd_mmr_desc_t *desc;
	int status;
	int i;

	if (lctx == NULL){
		INST_LOG(inst, LDMSD_LERROR, "aries_mmr: NULL context\n");
		return -1;
	}

	desc = (gpcd_mmr_desc_t *)
		gpcd_lookup_mmr_byname((char*)met);
	if (!desc) {
		INST_LOG(inst, LDMSD_LINFO,
			 "aries_mmr: Could not lookup <%s>\n", met);
		return -1;
	}

	status = gpcd_context_add_mmr(lctx, desc);
	if (status != 0) {
		INST_LOG(inst, LDMSD_LERROR,
			 "aries_mmr: Could not add mmr for <%s>\n", met);
		return -1;
	}

	return 0;
}

int find_type(const char *name)
{
	if (strncmp(LMT[NIC_LMT].header, name, LMT[NIC_LMT].len) == 0)
		return NIC_T;
	if ((strncmp(LMT[REQ_LMT].header, name, LMT[REQ_LMT].len) == 0) ||
	    (strncmp(LMT[RSP_LMT].header, name, LMT[RSP_LMT].len) == 0))
		return REQ_T;
	if (strncmp(LMT[PTILE_LMT].header, name, LMT[PTILE_LMT].len) == 0)
		return PTILE_T;
	if (strncmp(LMT[RTR_LMT].header, name, LMT[RTR_LMT].len) == 0)
		return RC_T;
	return OTHER_T;
}

int addMetric(aries_mmr_inst_t inst, ldms_schema_t schema, const char* name)
{
	int i;
	int mid;
	int rc;
	struct met *e;
	int type = find_type(name);

	rc = addMetricToContext(inst, inst->mvals[type].ctx, name);
	if (rc)
		return rc;
	mid = ldms_schema_metric_add(schema, name, LDMS_V_U64, "");
	if (mid < 0)
		return ENOMEM; /* caller use this to decide whether to stop */
	e = calloc(1, sizeof(*e));
	if (!e)
		return ENOMEM;
	e->metric_index = mid;
	LIST_INSERT_HEAD(&inst->mvals[type].mlist, e, entry);
	inst->mvals[type].num_metrics++;

	return 0;
}
/* ============== Sampler Plugin APIs ================= */

static
int aries_mmr_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	aries_mmr_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int i, rc;

	rc = ldms_schema_meta_array_add(schema, "aries_rtr_id",
					LDMS_V_CHAR_ARRAY, "",
					strlen(inst->rtrid)+1);
	if (rc < 0)
		return -rc; /* rc == -errno */
	for (i = 0; i < inst->numraw; i++){
		rc = addMetric(inst, schema, inst->rawlist[i]);
		if (rc == ENOMEM)
			return rc;
		else if (rc)
			INST_LOG(inst, LDMSD_LINFO,
				 "cannot add metric <%s>. Skipping\n",
				 inst->rawlist[i]);
		free(inst->rawlist[i]);
		inst->rawlist[i] = NULL;
	}
	free(inst->rawlist);
	inst->rawlist = NULL;
	inst->numraw = 0;
	return 0;
}

static
int aries_mmr_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	aries_mmr_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int i, rc;
	gpcd_mmr_list_t *listp = NULL;
	struct met *np;
	union ldms_value v;

	for (i = 0; i < END_T; i++) {
		if (inst->mvals[i].num_metrics == 0)
			continue;
		rc = gpcd_context_read_mmr_vals(inst->mvals[i].ctx);
		if (rc) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Cannot read raw mmr vals\n");
			return rc;
		}
		listp = inst->mvals[i].ctx->list;
		if (!listp){
			INST_LOG(inst, LDMSD_LERROR,
				 "Context list is null\n");
			return ENOENT;
		}
		LIST_FOREACH(np, &inst->mvals[i].mlist, entry) {
			if (!listp) {
				INST_LOG(inst, LDMSD_LERROR,
					 "Incompatible context list and "
					 "metric list.\n");
				return EINVAL;
			}
			v.v_u64 = listp->value;
			ldms_metric_set(set, np->metric_index, &v);
			listp = listp->next;
		}
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *aries_mmr_desc(ldmsd_plugin_inst_t pi)
{
	return "aries_mmr - aries_mmr sampler plugin";
}

static
char *_help = "\
aries_mmr configuration synopsis:\n\
    config name=INST [COMMON_OPTIONS] file=<PATH> aries_rtr_id=<STR>\n\
\n\
Option descriptions:\n\
    file          A path to metric config file.\n\
    aries_rtr_id  A string identifying router ID.\n\
";

static
const char *aries_mmr_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

int aries_mmr_config(ldmsd_plugin_inst_t pi, json_entity_t json,
					char *ebuf, int ebufsz)
{
	aries_mmr_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;
	const char *value;

	if (inst->configured) {
		snprintf(ebuf, ebufsz, "%s: already configured.\n",
			 pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	value = json_attr_find_str(json, "aries_rtr_id");
	inst->rtrid = strdup(value?value:"");
	if (!inst->rtrid) {
		snprintf(ebuf, ebufsz, "%s: out of memory.\n",
			 pi->inst_name);
		return ENOMEM;
	}

	value = json_attr_find_str(json, "file");
	if (value) {
		rc = parseConfig(inst, value);
		if (rc){
			snprintf(ebuf, ebufsz,
				 "%s: error parsing <%s>\n",
				 pi->inst_name, value);
			return rc;
		}
	} else {
		snprintf(ebuf, ebufsz, "%s: must specify input file\n",
			 pi->inst_name);
		return EINVAL;
	}

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;

	/* set rtrid once */
	ldms_metric_array_set_str(set, samp->first_idx, inst->rtrid);

	inst->configured = 1;
	return 0;
}

void aries_mmr_del(ldmsd_plugin_inst_t pi)
{
	aries_mmr_inst_t inst = (void*)pi;
	int i;
	struct met *m;

	/* cleanup gpcd context and list of metric index */
	for (i = 0; i < END_T; i++){
		while ((m = LIST_FIRST(&inst->mvals[i].mlist))) {
			LIST_REMOVE(m, entry);
			free(m);
		}
		if (inst->mvals[i].ctx)
			gpcd_remove_context(inst->mvals[i].ctx);
	}

	/* also the `rawlist` */
	if (inst->rawlist) {
		for (i = 0; i < inst->numraw; i++) {
			if (inst->rawlist[i])
				free(inst->rawlist[i]);
		}
		free(inst->rawlist);
	}
}

int aries_mmr_init(ldmsd_plugin_inst_t pi)
{
	aries_mmr_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int i;
	/* override update_schema() and update_set() */
	samp->update_schema = aries_mmr_update_schema;
	samp->update_set = aries_mmr_update_set;

	for (i = 0; i < END_T; i++){
		inst->mvals[i].num_metrics = 0;
		LIST_INIT(&inst->mvals[i].mlist);
		inst->mvals[i].ctx = gpcd_create_context();
		if (!inst->mvals[i].ctx){
			INST_LOG(inst, LDMSD_LERROR,
				 "Could not create context\n");
			return EINVAL;
		}
	}
	return 0;
}

static
struct aries_mmr_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "aries_mmr",

                /* Common Plugin APIs */
		.desc   = aries_mmr_desc,
		.help   = aries_mmr_help,
		.init   = aries_mmr_init,
		.del    = aries_mmr_del,
		.config = aries_mmr_config,
	},
	/* plugin-specific data initialization (for new()) here */
	.filter = filterKeepAll,
};

ldmsd_plugin_inst_t new()
{
	aries_mmr_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
