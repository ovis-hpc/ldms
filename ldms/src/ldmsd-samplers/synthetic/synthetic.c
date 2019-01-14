/**
 * Copyright (c) 2015-2016,2018-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2016,2018-2019 Open Grid Computing, Inc. All rights
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
 * \file synthetic.c
 * \brief synthetic data provider yielding waves offset by component id.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
//#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

typedef struct synthetic_inst_s *synthetic_inst_t;
struct synthetic_inst_s {
	struct ldmsd_plugin_inst_s base;

	double period; // seconds
	double amplitude; // integer height of waves
	double origin; // 8-24-2015-ish
};

static const char *metric_name[4] =
{
	"sine",
	"square",
	"saw",
	NULL
};

/* ============== Sampler Plugin APIs ================= */

static
int synthetic_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	int rc, k;
	for (k = 0; metric_name[k] != NULL; k++) {
		rc = ldms_schema_metric_add(schema, metric_name[k], LDMS_V_U64,
					    NULL);
		if (rc < 0) {
			return -rc;
		}
	}
	return 0;
}

static
int synthetic_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	synthetic_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	union ldms_value v;
	int k;
	for (k = 0; metric_name[k] != NULL; k++) {
		double t0 = inst->origin, t;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		t  = tv.tv_sec + tv.tv_usec*1e-6;
		t = t - t0;
		double x = fmod(t,inst->period);
		double y;
		// create unit range values
		switch (k) {
		case 0: // sine
			y = sin((x/inst->period) * 4 * M_PI);
			break;
		case 1: // square
			y = (x < inst->period/2) ? 1 : -1;
			break;
		case 2: // saw
			y = x / inst->period;
			break;
		default:
			y = 1;
		}

		v.v_u64 = llround((inst->amplitude*y) +
				(samp->component_id + 1)*2*inst->amplitude + 1);
		ldms_metric_set(set, k + samp->first_idx, &v);
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *synthetic_desc(ldmsd_plugin_inst_t pi)
{
	return "synthetic - sampler yielding waves offset by component id";
}

static
const char *synthetic_help(ldmsd_plugin_inst_t pi)
{
	return
"synthetic config synopsis\n"
"    config name=INST [COMMON_OPTIONS] origin=<f> height=<f> period=<f>\n"
"\n"
"Option descriptions\n"
"    origin  The zero time for periodic functions (float).\n"
"    height  The amplitude for periodic functions (float).\n"
"    period  The function period (float).\n";
}

static
void synthetic_del(ldmsd_plugin_inst_t pi)
{
	/* do nothing */
}

static
int synthetic_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	synthetic_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;
	char *value;
	char *endp = NULL;

	if (!LIST_EMPTY(&samp->set_list)) {
		snprintf(ebuf, ebufsz, "%s: already configured.\n",
			 pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */
	value = av_value(avl, "origin");
	if (value) {
		double x = strtod(value, &endp);
		if (x != 0) {
			inst->origin = x;
		}
	}

	value = av_value(avl, "period");
	if (value) {
		double x = strtod(value, &endp);
		if (x != 0) {
			inst->period = x;
		}
	}

	value = av_value(avl, "height");
	if (value) {
		double x = strtod(value, &endp);
		if (x != 0) {
			inst->amplitude = x;
		}
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
int synthetic_init(ldmsd_plugin_inst_t pi)
{
	synthetic_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = synthetic_update_schema;
	samp->update_set = synthetic_update_set;

	inst->period = 20; // seconds
	inst->amplitude = 10; // integer height of waves
	inst->origin = 1440449892; // 8-24-2015-ish
	return 0;
}

static
struct synthetic_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "synthetic",

                /* Common Plugin APIs */
		.desc   = synthetic_desc,
		.help   = synthetic_help,
		.init   = synthetic_init,
		.del    = synthetic_del,
		.config = synthetic_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	synthetic_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
