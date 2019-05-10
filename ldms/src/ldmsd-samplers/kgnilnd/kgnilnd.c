/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010,2014-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010,2014-2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file kgnilnd.c
 * \brief /proc/kgnilnd data provider
 *
 * Unlike other samplers, we reopen the kgnilnd file each time we sample. If the
 * open fails, we still return success instead of an error so that that sampler
 * will not be stopped.
 */
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

static const char *procfile = "/proc/kgnilnd/stats";

typedef struct kgnilnd_inst_s *kgnilnd_inst_t;
struct kgnilnd_inst_s {
	struct ldmsd_plugin_inst_s base;

	FILE *mf; /* metric file */
	int nmetrics;
};

static char *replace_space(char *s)
{
	char *s1;

	s1 = s;
	while ( *s1 ) {
		if ( isspace( *s1 ) ) {
			*s1 = '_';
		}
		++ s1;
	}
	return s;
}

/* ============== Sampler Plugin APIs ================= */

static
int kgnilnd_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	kgnilnd_inst_t inst = (void*)pi;
	int rc;
	char *s;
	char lbuf[256];
	uint64_t v;

	rc = fseek(inst->mf, 0, SEEK_SET);
	if (rc)
		return errno;

	inst->nmetrics = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), inst->mf);
		if (!s)
			break;
		char* end = strchr(s, ':');
		if (end && *end) {
			*end = '\0';
			replace_space(s);
			if (sscanf(end + 1, " %" PRIu64 "\n", &v) == 1) {
				rc = ldms_schema_metric_add(schema, s, LDMS_V_U64, NULL);
				if (rc < 0) {
					return -rc; /* rc == -errno */
				}
				inst->nmetrics++;
			}
		} else {
			return EINVAL; /* expecting NAME: VALUE format */
		}
	} while (s);
	return 0;
}

static
void __reset_metrics(kgnilnd_inst_t inst, ldms_set_t set)
{
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int i;
	for (i = 0; i < inst->nmetrics; i++) {
		ldms_metric_set_u64(set, i+samp->first_idx, 0);
	}
}

static
int kgnilnd_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	kgnilnd_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;

	char lbuf[256];
	char *s;
	uint64_t v;
	int idx;

	if (!inst->mf){
		inst->mf = fopen(procfile, "r");
		if (!inst->mf) {
			ldmsd_log(LDMSD_LERROR,
				  "Could not open the kgnilnd file '%s'...\n",
				  procfile);
			__reset_metrics(inst, set);
			return errno;
		}
	}

	if (fseek(inst->mf, 0, SEEK_SET) != 0){
		/* perhaps the file handle has become invalid.
		 * close it so it will reopen it on the next round.
		 */
		ldmsd_log(LDMSD_LERROR, "Could not seek in the %s file "
			  "'%s'...closing filehandle\n",
			  pi->inst_name, procfile);
		fclose(inst->mf);
		inst->mf = NULL;
		__reset_metrics(inst, set);
		return errno;
	}

	idx = samp->first_idx;
	do {
		s = fgets(lbuf, sizeof(lbuf), inst->mf);
		if (!s)
			break;

		char* end = strchr(s, ':');
		if (end && *end) {
			*end = '\0';
			replace_space(s);
			if (sscanf(end + 1, " %" PRIu64 "\n", &v) == 1) {
				ldms_metric_set_u64(set, idx, v);
				idx++;
			} else {
				goto bad_format;
			}
		} else {
			/* bad foramt */
			goto bad_format;
		}
	} while (s);

	return 0;

bad_format:
	/* bad foramt */
	ldmsd_log(LDMSD_LERROR, "%s: bad line format: %s\n",
		  pi->inst_name, s);
	return EINVAL;
}


/* ============== Common Plugin APIs ================= */

static
const char *kgnilnd_desc(ldmsd_plugin_inst_t pi)
{
	return "kgnilnd - /proc/kgnilnd data sampler";
}

static
const char *kgnilnd_help(ldmsd_plugin_inst_t pi)
{
	return "kgnilnd takes no extra arguments.";
}

static
void kgnilnd_del(ldmsd_plugin_inst_t pi)
{
	/* The undo of kgnilnd_init */
	kgnilnd_inst_t inst = (void*)pi;
	if (inst->mf) {
		fclose(inst->mf);
		inst->mf = NULL;
	}
}

static
int kgnilnd_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	kgnilnd_inst_t inst = (void*)pi;
	ldms_set_t set;
	int rc;

	if (inst->mf) {
		snprintf(ebuf, ebufsz, "%s: already configured.\n",
			 pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	inst->mf = fopen(procfile, "r");
	if (!inst->mf) {
		snprintf(ebuf, ebufsz, "%s: Could not open the file "
				"'%s'...aborting\n", pi->inst_name, procfile);
		return errno;
	}
	setbuf(inst->mf, NULL); /* disable buffering */

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
int kgnilnd_init(ldmsd_plugin_inst_t pi)
{
	kgnilnd_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = kgnilnd_update_schema;
	samp->update_set = kgnilnd_update_set;

	inst->mf = NULL;
	inst->nmetrics = 0;
	return 0;
}

static
struct kgnilnd_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "kgnilnd",

                /* Common Plugin APIs */
		.desc   = kgnilnd_desc,
		.help   = kgnilnd_help,
		.init   = kgnilnd_init,
		.del    = kgnilnd_del,
		.config = kgnilnd_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	kgnilnd_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
