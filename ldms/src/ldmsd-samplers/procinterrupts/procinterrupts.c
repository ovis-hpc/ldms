/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2016,2018-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with NTESS,
 * the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2016,2018-2019 Open Grid Computing, Inc. All rights
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
 * \file procinterrupts.c
 * \brief /proc/interrupts data provider
 */
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

static char *procfile = "/proc/interrupts";

typedef struct procinterrupts_inst_s *procinterrupts_inst_t;
struct procinterrupts_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
	FILE *mf;
	int nprocs;
	char lbuf[4096]; /* NB: Some machines have many cores */
};

static int getNProcs(char buf[]){
	int nproc = 0;
	char* pch;
	pch = strtok(buf, " ");
	while (pch != NULL){
		if (pch[0] == '\n'){
			break;
		}
		if (pch[0] != ' '){
			nproc++;
		}
		pch = strtok(NULL," ");
	}

	return nproc;
}

/* ============== Sampler Plugin APIs ================= */

static
int procinterrupts_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	procinterrupts_inst_t inst = (void*)pi;
	char *s;
	int i;
	char metric_name[128];

	fseek(inst->mf, 0, SEEK_SET);
	s = fgets(inst->lbuf, sizeof(inst->lbuf), inst->mf);
	inst->nprocs = getNProcs(inst->lbuf);
	if (inst->nprocs <= 0) {
		ldmsd_log(LDMSD_LINFO, "Bad number of CPU.\n");
		return EINVAL;
	}
	while ((s = fgets(inst->lbuf, sizeof(inst->lbuf), inst->mf))) {
		int currcol = 0;
		char *beg_name, *ptr;
		char *pch = strtok_r (inst->lbuf," ", &ptr);
		while (pch != NULL && currcol <= inst->nprocs){
			if (pch[0] == '\n')
				break;
			if (currcol == 0){
				/* Strip the colon from metric name if present */
				i = strlen(pch);
				if (i && pch[i-1] == ':')
					pch[i-1] = '\0';
				beg_name = pch;
			} else {
				snprintf(metric_name, 128, "irq.%s#%d",
						beg_name, (currcol-1));
				i = ldms_schema_metric_add(schema, metric_name,
							    LDMS_V_U64, NULL);
				if (i < 0) {
					return -i; /* i == -errno */
				}
			}
			currcol++;
			pch = strtok_r(NULL," ", &ptr);
		}
	}
	return 0;
}

static
int procinterrupts_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set,
			       void *ctxt)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	procinterrupts_inst_t inst = (void*)pi;
	char *s;
	uint64_t val;
	int idx;

	fseek(inst->mf, 0, SEEK_SET);
	s = fgets(inst->lbuf, sizeof(inst->lbuf), inst->mf);
	inst->nprocs = getNProcs(inst->lbuf);
	if (inst->nprocs <= 0) {
		ldmsd_log(LDMSD_LINFO, "Bad number of CPU.\n");
		return EINVAL;
	}
	idx = samp->first_idx;
	while ((s = fgets(inst->lbuf, sizeof(inst->lbuf), inst->mf))) {
		int currcol = 0;
		char *ptr;
		char *pch = strtok_r (s, " ", &ptr);
		while (pch != NULL && currcol <= inst->nprocs){
			if (pch[0] == '\n')
				break;
			if (currcol == 0){
				/* do nohting */
			} else {
				val = atol(pch);
				ldms_metric_set_u64(set, idx, val);
				idx++;
			}
			currcol++;
			pch = strtok_r(NULL," ", &ptr);
		}
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *procinterrupts_desc(ldmsd_plugin_inst_t pi)
{
	return "procinterrupts - /proc/interrupts data sampler";
}

static
const char *procinterrupts_help(ldmsd_plugin_inst_t pi)
{
	return "procinterrupts takes no extra attributes.";
}

static
void procinterrupts_del(ldmsd_plugin_inst_t pi)
{
	procinterrupts_inst_t inst = (void*)pi;
	if (inst->mf)
		fclose(inst->mf);
}

static
int procinterrupts_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	procinterrupts_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;

	if (inst->mf) {
		snprintf(ebuf, ebufsz, "%s: already configured.\n",
			 pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	inst->mf = fopen(procfile, "r");
	if (!inst->mf) {
		snprintf(ebuf, ebufsz, "%s: cannot open file: %s, errno: %d.\n",
			 pi->inst_name, procfile, errno);
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
int procinterrupts_init(ldmsd_plugin_inst_t pi)
{
	procinterrupts_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = procinterrupts_update_schema;
	samp->update_set = procinterrupts_update_set;

	inst->mf = NULL;

	return 0;
}

static
struct procinterrupts_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "procinterrupts",

                /* Common Plugin APIs */
		.desc   = procinterrupts_desc,
		.help   = procinterrupts_help,
		.init   = procinterrupts_init,
		.del    = procinterrupts_del,
		.config = procinterrupts_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	procinterrupts_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
