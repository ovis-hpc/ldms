/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2017 Open Grid Computing, Inc. All rights reserved.
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
 * \file cray_aries_r_sampler.c
 * \brief unified custom data provider for a combination of metrics
 */

#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <ctype.h>
#include <wordexp.h>
#include "config.h"

#include "cray_sampler_base.h"
#include "aries_metrics_gpcdr.h"

#ifdef HAVE_LUSTRE
#include "lustre_metrics.h"
#endif

#include "ldmsd.h"
#include "ldmsd_sampler.h"
#include "cray_aries_r_sampler.h"

/* ============== Sampler Plugin APIs ================= */

static
int cray_aries_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	cray_aries_inst_t inst = (void*)pi;
	/* ldmsd_sampler_type_t samp = (void*)pi->base; */
	int rc, i;
	/*
	 * Will create each metric in the set, even if the source does not exist
	 */
	rc = 0;
	for (i = 0; i < NS_NUM; i++) {
		switch (i) {
		case NS_LINKSMETRICS:
			if (inst->off_hsn)
				break;
			rc = add_metrics_aries_linksmetrics(inst, schema);
			if (rc)
				return rc;
			rc = aries_linksmetrics_setup(inst);
			if (rc == ENOMEM)
				return rc;
			if (rc != 0) {/*  Warn but OK to continue */
				INST_LOG(inst, LDMSD_LERROR,
					 "linksmetrics invalid\n");
			}
			break;
		case NS_NICMETRICS:
			if (inst->off_hsn)
				break;
			rc = add_metrics_aries_nicmetrics(inst, schema);
			if (rc)
				return rc;
			rc = aries_nicmetrics_setup(inst);
			if (rc == ENOMEM)
				return rc;
			if (rc != 0) /*  Warn but OK to continue */
				INST_LOG(inst, LDMSD_LERROR,
					 "nicmetrics invalid\n");
			break;
		default:
			rc = add_metrics_generic(&inst->base, schema, i);
			if (rc) {
				INST_LOG(inst, LDMSD_LERROR,
					 "%s:  NS %s return error code %d in "
					 "add_metrics_generic\n",
					 __FILE__, ns_names[i], rc);
				return rc;
			}
		}
	}
	return 0;
}

static
int cray_aries_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	cray_aries_inst_t inst = (void*)pi;
	int i, rc;
	/* Populate set metrics */
	for (i = 0; i < NS_NUM; i++){
		rc = 0;
		switch(i){
		case NS_LINKSMETRICS:
			rc = (inst->off_hsn)?
				(0):
				(sample_metrics_aries_linksmetrics(inst, set));
			break;
		case NS_NICMETRICS:
			rc = (inst->off_hsn)?
				(0):
				(sample_metrics_aries_nicmetrics(inst, set));
			break;
		default:
			rc = sample_metrics_generic(&inst->base, set, i);
		}
		/* Continue if error, but report an error code */
		if (rc) {
			ldmsd_log(LDMSD_LDEBUG,
				  "cray_aries_r_sampler: NS %s return "
				  "error code %d\n", ns_names[i], rc);
			return rc;
		}
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *cray_aries_desc(ldmsd_plugin_inst_t pi)
{
	return "cray_aries - cray_aries sampler plugin";
}

static
char *_help ="\
cray_aries config synopsis:\n\
config name=<INST> [COMMON_OPTIONS]  [llite=<ostlist>] \n\
                   [gpu_devices=<gpulist>] [hsn_metrics_type=(1|2|3)] \n\
		   [off_<namespace>=1]\n\
\n\
Option descriptions:\n\
    llite               A comma-separated list of Lustre OSTs. If not \n\
                        specified, no OST metric is collected. \n\
    gpu_devices         GPU devices names. If not specified, no GPU metric \n\
                        is collected.\n\
    hsn_metrics_type    1 for COUNTER, 2 for DERIVED, and 3 for BOTH.\n\
                        (default: 1) \n\
    off_<namespace>     Collection for variable classes\n\
			can be turned off: hsn (both links and nics)\n\
			vmstat, loadavg, current_freemem, kgnilnd\n\
			lustre, procnetdev, nvidia\n";

static
const char *cray_aries_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void cray_aries_del(ldmsd_plugin_inst_t pi)
{
	cray_aries_inst_t inst = (void*)pi;
	int i, j;

	/* cleaning up linksmetrics (gpcdr) */
	for (i = 0; i < ENDLINKS; i++) {
		if (inst->lm_f[i])
			fclose(inst->lm_f[i]);
		if (inst->linksmetrics_base_metric_table[i])
			free(inst->linksmetrics_base_metric_table[i]);
		if (inst->linksmetrics_derived_metric_table[i])
			free(inst->linksmetrics_derived_metric_table[i]);
		if (inst->linksmetrics_base_diff[i])
			free(inst->linksmetrics_base_diff[i]);
		if (inst->linksmetrics_base_values[i]) {
			for (j = 0; j < 2; j++) {
				if (!inst->linksmetrics_base_values[i][j])
					continue;
				free(inst->linksmetrics_base_values[i][j]);
			}
			free(inst->linksmetrics_base_values[i]);
		}
	}

	/* cleaning up nicmetrics */
	if (inst->nm_f)
		fclose(inst->nm_f);
	if (inst->nicmetrics_base_metric_table)
		free(inst->nicmetrics_base_metric_table);
	if (inst->nicmetrics_derived_metric_table)
		free(inst->nicmetrics_derived_metric_table);
	if (inst->nicmetrics_base_values) {
		for (j = 0; j < 2; j++) {
			if (!inst->nicmetrics_base_values[j])
				continue;
			free(inst->nicmetrics_base_values[j]);
		}
		free(inst->nicmetrics_base_values);
	}

	/* cleanup the base */
	cray_sampler_base_del(&inst->base);
}

static
int cray_aries_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	cray_aries_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;
	char *value;

	rc = samp->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	rc = config_generic(&inst->base, kwl, avl);
	if (rc) {
		snprintf(ebuf, ebufsz, "configure error: %d\n", rc);
		return rc;
	}

	value = av_value(avl, "off_hsn");
	if (value)
		inst->off_hsn = (atoi(value) == 1);

	if (!inst->off_hsn) {
		value = av_value(avl,"hsn_metrics_type");
		if (value) {
			inst->hsn_metrics_type = atoi(value);
		}
		rc = hsn_metrics_config(ebuf, ebufsz);
		if (rc != 0)
			return rc;
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
int cray_aries_init(ldmsd_plugin_inst_t pi)
{
	/* cray_aries_inst_t inst = (void*)pi; */
	ldmsd_sampler_type_t samp = (void*)pi->base;

	/* override update_schema() and update_set() */
	samp->update_schema = cray_aries_update_schema;
	samp->update_set = cray_aries_update_set;

	/* Pointers, metric table pointers, FILEs are alrady initialized
	 * by new(). */

	return 0;
}

static
struct cray_aries_inst_s __inst = {
	.base = {
		/* struct cray_sampler_inst_s */
		.base = {
			/* struct ldmsd_plugin_inst_s */
			.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
			.type_name   = "sampler",
			.plugin_name = "cray_aries_r_sampler",

			/* Common Plugin APIs */
			.desc   = cray_aries_desc,
			.help   = cray_aries_help,
			.init   = cray_aries_init,
			.del    = cray_aries_del,
			.config = cray_aries_config,
		},
		/* all metric table pointers are NULL */
		/* LIST_HEADs are also { NULL } */
	},
	.hsn_metrics_type = HSN_METRICS_DEFAULT,
	/* All pointers are NULL */
	/* All FILEs are NULL */
};

ldmsd_plugin_inst_t new()
{
	cray_aries_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return (void*)inst;
}
