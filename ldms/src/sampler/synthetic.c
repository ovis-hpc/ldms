/*
 * Copyright (c) 2015-16 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015-16 Sandia Corporation. All rights reserved.
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
#include "ldms_jobid.h"

static ldms_set_t set;
static ldms_schema_t schema;
static ldmsd_msg_log_f msglog;
static uint64_t compid = UINT64_MAX;
static double period = 20; // seconds
static double amplitude = 10; // integer height of waves
static double origin = 1440449892; // 8-24-2015-ish
static int metric_offset = 1;

#define SAMP "synthetic"
static char *default_schema_name = SAMP;
LJI_GLOBALS;

static const char *metric_name[4] =
{
	"sine",
	"square",
	"saw",
	NULL
};

static int create_metric_set(char *instance_name, char *schema_name)
{
	int rc;
	union ldms_value v;
	schema = ldms_schema_new(schema_name);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}

	metric_offset++;
	rc = LJI_ADD_JOBID(schema);
	if (rc < 0) {
		goto err;
	}

	int k;
	for (k = 0; metric_name[k] != NULL; k++) {
		rc = ldms_schema_metric_add(schema, metric_name[k], LDMS_V_U64);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	v.v_u64 = compid;
	ldms_metric_set(set,0,&v);

	LJI_SAMPLE(set,1);

	v.v_u64 = 0;
	for (k = 0; metric_name[k] != NULL; k++) {
		ldms_metric_set(set, k + metric_offset, &v);
	}
	return 0;

 err:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	return rc;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP " producer=<prod_name> instance=<inst_name> [component_id=<compid> schema=<sname> with_jobid=<jid> origin=<f> height=<f> period=<f>]\n"
		"    <prod_name>  The producer name\n"
		"    <inst_name>  The instance name\n"
		"    <compid>     Optional unique number identifier. Defaults to zero.\n"
		LJI_DESC
		"    <sname>      Optional schema name. Defaults to '" SAMP "'\n"
		"    origin  The zero time for periodic functions (float).\n"
		"    height  The amplitude for periodic functions (float).\n"
		"    period  The function period (float).\n"
	;
}

/**
 * \brief Configuration
 *
 * config name=synthetic producer_name=<name> instance_name=<instance_name> [component_id=<compid> schema=<sname>] [with_jobid=<bool> origin=<f> height=<f> period=<f>]
 *     producer_name    The producer id value.
 *     instance_name    The set name.
 *     component_id     The component id. Defaults to zero
 *     sname            Optional schema name. Defaults to meminfo
 *     bool             lookup jobid in set or not.
 *     origin      The zero time for periodic functions
 *     height      The amplitude of functions
 *     period      The function period
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *producer_name;
	char *sname;
	int rc;

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, SAMP ": missing producer.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0) {
		msglog(LDMSD_LERROR, SAMP ": schema name invalid.\n");
		return EINVAL;
	}

	value = av_value(avl, "component_id");
	if (value)
		compid = strtoull(value, NULL, 0);

	char *endp = NULL;
	value = av_value(avl, "origin");
	if (value) {
		double x = strtod(value, &endp);
		if (x != 0) {
			origin = x;
		}
	}

	value = av_value(avl, "period");
	if (value) {
		double x = strtod(value, &endp);
		if (x != 0) {
			period = x;
		}
	}

	value = av_value(avl, "height");
	if (value) {
		double x = strtod(value, &endp);
		if (x != 0) {
			amplitude = x;
		}
	}

	LJI_CONFIG(value,avl);

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, SAMP ": missing instance.\n");
		return ENOENT;
	}
	rc = create_metric_set(value, sname);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int sample(struct ldmsd_sampler *self)
{
	union ldms_value v;

	if (!set) {
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}
	ldms_transaction_begin(set);

	LJI_SAMPLE(set,1);

	int k;
	for (k = 0; metric_name[k] != NULL; k++) {
		double t0 = origin, t;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		t  = tv.tv_sec + tv.tv_usec*1e-6;
		t = t - t0;
		double x = fmod(t,period);
		double y;
		// create unit range values
		switch (k) {
		case 0: // sine
			y = sin((x/period) * 4 * M_PI);
			break;
		case 1: // square
			y = (x < period/2) ? 1 : -1;
			break;
		case 2: // saw
			y = x / period;
			break;
		default:
			y = 1;
		}

		v.v_u64 = llround( (amplitude * y ) + (compid + 1) * 2 * amplitude + 1);
		ldms_metric_set(set, k + metric_offset, &v);
	}

	ldms_transaction_end(set);
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler synthetic_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &synthetic_plugin.base;
}

