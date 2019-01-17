/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017-2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file aries_linkstatus
 * \brief link metrics status from gpcdr
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
#include "ldms.h"

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define LINKSTATUS_FILE "/sys/devices/virtual/gni/gpcdr0/metricsets/links/metrics"
#define NUMROW_TILE 5
#define NUMCOL_TILE 8

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, ##__VA_ARGS__)

typedef struct aries_linkstatus_inst_s *aries_linkstatus_inst_t;
struct aries_linkstatus_inst_s {
	struct ldmsd_plugin_inst_s base;

	int configured;
	char *lsfile;
	char *lrfile;
};

/* ============== Sampler Plugin APIs ================= */

static
int aries_linkstatus_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	char lbuf[256];
	int i, rc;

	for (i = 0; i < NUMROW_TILE; i++) {
		snprintf(lbuf,255, "sendlinkstatus_r%d", i);
		rc = ldms_schema_metric_array_add(schema, lbuf, LDMS_V_U8_ARRAY,
				"lanes", NUMCOL_TILE);
		if (rc < 0) {
			return -rc; /* rc == -errno */
		}
	}
	/* Well known aries tiles dimension */
	for (i = 0; i < NUMROW_TILE; i++) {
		snprintf(lbuf,255, "recvlinkstatus_r%d", i);
		rc = ldms_schema_metric_array_add(schema, lbuf, LDMS_V_U8_ARRAY,
				"lanes", NUMCOL_TILE);
		if (rc < 0) {
			return -rc; /* rc == -errno */
		}
	}
	return 0;
}

static
int aries_linkstatus_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	aries_linkstatus_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;

	/* doing this infrequently, so open and close each time */
	int row, col, rc, metric_no;
	union ldms_value v;
	char *s, lbuf[256];
	FILE *mf = fopen(inst->lsfile, "r");

	if (!mf) {
		INST_LOG(inst, LDMSD_LERROR, "Could not open file '%s'\n",
			 inst->lsfile);
		return errno;
	}

	/* NOTE: not ensuring that we have any of these.
	   This is a double pass if they are the same file */
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		//send before receive
		rc = sscanf(lbuf, "sendlinkstatus:0%1d%1d %"PRIu8,
			    &row, &col, &v.v_u8);
		if (rc == 3) {
			if ((row >= NUMROW_TILE) || (col >= NUMCOL_TILE)){
				INST_LOG(inst, LDMSD_LDEBUG,
					 "bad row col '%s'\n", lbuf);
			} else {
				metric_no = samp->first_idx + row;
				ldms_metric_array_set_val(set, metric_no,
							  col, &v);
			}
		}
	} while (s);
	fclose(mf);

	mf = fopen(inst->lrfile, "r");
	if (!mf) {
		INST_LOG(inst, LDMSD_LERROR, "Could not open file '%s'\n",
			 inst->lrfile);
		return errno;
	}

	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "recvlinkstatus:0%1d%1d %"PRIu8,
			    &row, &col, &v.v_u8);
		if (rc == 3) {
			if ((row >= NUMROW_TILE) || (col >= NUMCOL_TILE)){
				INST_LOG(inst, LDMSD_LDEBUG,
					 "bad row col '%s'\n", lbuf);
			} else {
				//well known aries layout
				metric_no = samp->first_idx + NUMROW_TILE + row;
				ldms_metric_array_set_val(set, metric_no,
							  col, &v);
			}
		}
	} while (s);
	fclose(mf);
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *aries_linkstatus_desc(ldmsd_plugin_inst_t pi)
{
	return "aries_linkstatus - aries_linkstatus sampler plugin";
}

static
char *_help = "\
aries_linkstatus takes no extra options.\n\
";

static
const char *aries_linkstatus_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void aries_linkstatus_del(ldmsd_plugin_inst_t pi)
{
	aries_linkstatus_inst_t inst = (void*)pi;

	/* The undo of aries_linkstatus_init and instance cleanup */
}

static
int aries_linkstatus_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	aries_linkstatus_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;
	const char *fname;
	FILE *mf;

	if (inst->configured) {
		snprintf(ebuf, ebufsz, "%s: already configured\n",
			 pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	fname = av_value(avl, "file_send");
	inst->lsfile = strdup(fname?fname:LINKSTATUS_FILE);
	if (!inst->lsfile) {
		snprintf(ebuf, ebufsz, "%s: not enough memory.\n",
			 pi->inst_name);
		return ENOMEM;
	}
	if (strlen(inst->lsfile) == 0){
		snprintf(ebuf, ebufsz, "%s: file name invalid.\n",
			 pi->inst_name);
		return EINVAL;
	}
	mf = fopen(inst->lsfile, "r");
	if (!mf) {
		snprintf(ebuf, ebufsz, "%s: cannot open `file_send`: %s.\n",
			 pi->inst_name, inst->lsfile);
		return EINVAL;
	}
	fclose(mf);

	fname = av_value(avl, "file_recv");
	inst->lrfile = strdup(fname?fname:LINKSTATUS_FILE);
	if (!inst->lrfile) {
		snprintf(ebuf, ebufsz, "%s: not enough memory.\n",
			 pi->inst_name);
		return ENOMEM;
	}
	if (strlen(inst->lrfile) == 0){
		snprintf(ebuf, ebufsz, "%s: file name invalid.\n",
			 pi->inst_name);
		return EINVAL;
	}
	mf = fopen(inst->lrfile, "r");
	if (!mf) {
		snprintf(ebuf, ebufsz, "%s: cannot open `file_recv`: %s.\n",
			 pi->inst_name, inst->lrfile);
		return EINVAL;
	}
	fclose(mf);

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;
	inst->configured = 1;
	return 0;
}

static
int aries_linkstatus_init(ldmsd_plugin_inst_t pi)
{
	aries_linkstatus_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = aries_linkstatus_update_schema;
	samp->update_set = aries_linkstatus_update_set;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct aries_linkstatus_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "aries_linkstatus",

                /* Common Plugin APIs */
		.desc   = aries_linkstatus_desc,
		.help   = aries_linkstatus_help,
		.init   = aries_linkstatus_init,
		.del    = aries_linkstatus_del,
		.config = aries_linkstatus_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	aries_linkstatus_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
