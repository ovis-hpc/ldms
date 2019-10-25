/**
 * Copyright (c) 2015-2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2016,2018 Open Grid Computing, Inc. All rights reserved.
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
#include "sampler_base.h"

static ldms_set_t set;
static ldmsd_msg_log_f msglog;
static uint64_t compid = UINT64_MAX;
static double period = 20; // seconds
static double amplitude = 10; // integer height of waves
static double origin = 1440449892; // 8-24-2015-ish
static int metric_offset;
base_data_t base;

#define SAMP "synthetic"


static const char *metric_name[4] =
{
	"sine",
	"square",
	"saw",
	NULL
};

static int create_metric_set(base_data_t base)
{
	int rc;
	ldms_schema_t schema;
	union ldms_value v;

	schema = base_schema_new(base);
	if (!schema) {
		msglog(LDMSD_LERROR, "The scheam '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/* Location of first metric */
	metric_offset = ldms_schema_metric_count_get(schema);

	int k;
	for (k = 0; metric_name[k] != NULL; k++) {
		rc = ldms_schema_metric_add(schema, metric_name[k], LDMS_V_U64);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}


	v.v_u64 = 0;
	for (k = 0; metric_name[k] != NULL; k++) {
		ldms_metric_set(set, k + metric_offset, &v);
	}
	return 0;

 err:
	return rc;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP " origin=<f> height=<f> period=<f>] " BASE_CONFIG_USAGE
		"    origin  The zero time for periodic functions (float).\n"
		"    height  The amplitude for periodic functions (float).\n"
		"    period  The function period (float).\n"
	;
}

/**
 * \brief Configuration
 *
 * config name=synthetic [origin=<f> height=<f> period=<f>]
 *     origin      The zero time for periodic functions
 *     height      The amplitude of functions
 *     period      The function period
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc;

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}


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

	/* component id is set in the base, but is also used in the sampler */
	value = av_value(avl, "component_id");
	if (value)
		compid = strtoull(value, NULL, 0);

	base = base_config(avl, SAMP, SAMP, msglog);
	if (!base)
		goto err;

	rc = create_metric_set(base);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}
	return 0;

err:
	base_del(base);
	return rc;
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

	base_sample_begin(base);

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

	base_sample_end(base);
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	if (base)
		base_del(base);
	base = NULL;
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
