/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014-2016 Sandia Corporation. All rights reserved.
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
 * \file generic_sampler.c
 * \brief reads data from /tmp/metrics.
 *
 * This sampler reads data from /tmp/metrics. The sampler expects the file to be
 * in the following format:
 *   METRIC_NAME: METRIC_VALUE
 *
 * Metrics are defined in 'config' call with a parameter 'metrics' in the
 * following format.
 *   metrics=NAME1:TYPE1,NAME2:TYPE2,...
 *
 * TYPE can be one of the following
 * INT (or I for short)
 * UINT (or U for short)
 * DOUBLE (or D for short)
 *
 * The value from /tmp/metrics file will be interpret as defined in 'config'
 * command.
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <coll/str_map.h>
#include "ldms.h"
#include "ldmsd.h"

/* TYPES */
typedef struct gs_metric {
	char *name; /**< metric name */
	enum ldms_value_type type; /**< type */
	int mh; /**< ldms metric index */
	TAILQ_ENTRY(gs_metric) entry; /**< List entry */
} *gs_metric_t;

/* GLOBAL VARS */

TAILQ_HEAD(, gs_metric) gs_list = TAILQ_HEAD_INITIALIZER(gs_list); /**< List of metrics */
str_map_t gs_map = NULL; /**< gs metric map */
const char *path = "/tmp/metrics"; /**< metric file path */
static ldms_set_t set;
static ldms_schema_t schema;
static char buff[65536];
static uint64_t comp_id;

int gs_fd = -1; /**< File descriptor to the metric file */

/* Log */
static ovis_log_t mylog;


/* FUNCTIONS */

static gs_metric_t gs_metric_alloc(const char *name, enum ldms_value_type type)
{
	gs_metric_t gs_metric = calloc(1, sizeof(*gs_metric));
	if (!gs_metric)
		return NULL;
	gs_metric->name = strdup(name);
	if (!gs_metric->name) {
		free(gs_metric);
		return NULL;
	}
	gs_metric->type = type;
	return gs_metric;
}

#pragma GCC diagnostic ignored "-Wunused-function"
static void gs_metric_free(gs_metric_t gs_metric)
{
	if (gs_metric->name)
		free(gs_metric->name);
	free(gs_metric);
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return
"config name=generic_sampler component_id=<comp_id>  path=<path>\n"
"                            producer=<prod_name> instance=<inst_name>\n"
"                            mx=M1:T1,M2:T2,...\n"
"    <comp_id>     The component id value.\n"
"    <path>        The path to the metric file (default: /tmp/metrics).\n"
"    <mx>          The comma-separated list of metric name (M#) and type (T#).\n"
"                    The type can be one of the I,U and D (for int, uint and\n"
"                    double respectively)\n"
"    <inst_name>   The instance name.\n"
"    <prod_name>   The producer name.\n";
}

static int create_metric_set(const char *inst_name, const char *prod_name)
{
	int rc = 0;
	gs_metric_t gs_metric;

	schema = ldms_schema_new("generic_sampler");
	if (!schema)
		return ENOMEM;

	TAILQ_FOREACH(gs_metric, &gs_list, entry) {
		gs_metric->mh = ldms_schema_metric_add(schema, gs_metric->name,
						       gs_metric->type);
	}

	set = ldms_set_new(inst_name, schema);
	if (set)
		ldms_set_producer_name_set(set, prod_name);
	else
		rc = errno;
	return rc;
}

void permute_metrics(char *name, enum ldms_value_type type)
{
	char *v0, *v1, *range, *suffix, *tok;
	int rc;
	int a, b, c;
	char lz_str[32];
	int lz = 0; /* leading zero */
	gs_metric_t gs_metric;

	v0 = strchr(name, '{');
	v1 = strchr(name, '}');
	if (!v0) {
		/* This is the end of permutation */
		gs_metric = gs_metric_alloc(name, type);
		if (!gs_metric)
			return;
		rc = str_map_insert(gs_map, gs_metric->name,
						(uint64_t)gs_metric);
		if (rc)
			return;
		TAILQ_INSERT_TAIL(&gs_list, gs_metric, entry);
		return;
	}
	*v1 = 0;
	range = strdup(v0+1);
	if (!range) {
		ovis_log(mylog, OVIS_LERROR, "generic_sampler: %s: range strdup: "
				"ENOMEM\n", __func__);
		return;
	}
	suffix = strdup(v1+1);
	if (!suffix) {
		ovis_log(mylog, OVIS_LDEBUG, "generic_sampler: %s: suffix strdup: "
				"ENOMEM\n", __func__);
		return;
	}

	tok = range;
	while (*tok) {
		rc = sscanf(tok, "%[0]%d%n", lz_str, &a, &c);
		switch (rc) {
		case 0:
			lz = 0;
			rc = sscanf(tok, "%d%n", &a, &c);
			if (rc != 1) {
				ovis_log(mylog, OVIS_LDEBUG, "generic_sampler: %s: "
					"Expecting a number but got: %s\n",
					__func__, tok);
				return;
			}
			break;
		case 2:
			lz = strlen(lz_str) + 1;
			break;
		}
		tok += c;
		if (*tok == '.') {
			rc = sscanf(tok, "%*[.]%d%n", &b, &c);
			if (rc != 1) {
				ovis_log(mylog, OVIS_LDEBUG, "generic_sampler: %s: "
					"Expecting a number"
					" after '..', but got: %s\n", __func__,
					tok);
				return;
			}
			tok += c;
			/* Range */
			/* reuse 'c' */
			for (c = a; c <= b; c++) {
				sprintf(v0, "%0*d%s", lz, c, suffix);
				permute_metrics(name, type);
			}
		} else {
			/* Single number */
			sprintf(v0, "%0*d%s", lz, a, suffix);
			permute_metrics(name, type);
		}
		if (*tok == ',')
			tok++;
	}
	free(range);
	free(suffix);
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value, *ptr, *tok, *t;
	enum ldms_value_type type;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtoull(value, NULL, 0);

	value = av_value(avl, "path");
	if (value) {
		path = strdup(value);
		if (!path) {
			return ENOMEM;
		}
	}

	value = av_value(avl, "mx");
	if (!value) {
		ovis_log(mylog, OVIS_LERROR, "generic_sampler: No 'mx' is given.\n");
		return EINVAL;
	}

	tok = strtok_r(value, ",", &ptr);
	while (tok) {
		t = strchr(tok, ':');
		if (!t)
			return EINVAL;
		*t = 0;
		t++;
		switch (*t) {
		case 'I':
		case 'i':
			type = LDMS_V_S64;
			break;
		case 'U':
		case 'u':
			type = LDMS_V_U64;
			break;
		case 'D':
		case 'd':
			type = LDMS_V_D64;
			break;
		default:
			ovis_log(mylog, OVIS_LERROR, "generic_sampler config: "
					"unknown type %c\n", *t);
			return EINVAL;
		}
		permute_metrics(tok, type);

		/* REMOVE ME */

		tok = strtok_r(NULL, ",", &ptr);
	}
	value = av_value(avl, "instance");
	if (!value) {
		ovis_log(mylog, OVIS_LERROR, "generic_sampler config: 'set' is "
				"not specified\n");
		return EINVAL;
	}
	char *prod_name = av_value(avl, "producer");
	return create_metric_set(value, prod_name);
}

static int sample(ldmsd_plug_handle_t handle)
{
	gs_metric_t gs_metric;
	int rc = 0;
	ssize_t sz;
	off_t offset;
	union ldms_value value;
	char *tok, *v;
	ldms_transaction_begin(set);
	if (gs_fd == -1) {
		gs_fd = open(path, O_RDONLY);
		if (gs_fd < 0) {
			ovis_log(mylog, OVIS_LERROR, "generic_sampler: Cannot open file:"
					" %s\n", path);
			rc = ENOENT;
			goto out;
		}
	}
	offset = lseek(gs_fd, 0, SEEK_SET);
	if (offset < 0) {
		ovis_log(mylog, OVIS_LERROR, "generic_sampler: lseek fail, errno: %d\n",
				errno);
		close(gs_fd);
		gs_fd = -1;
		rc = EIO;
		goto out;
	}
	sz = read(gs_fd, buff, sizeof(buff) - 1);
	if (sz < 0) {
		ovis_log(mylog, OVIS_LERROR, "generic_sampler: cannot read %s, errno: %d\n", path,
				errno);
		close(gs_fd);
		gs_fd = -1;
		rc = EIO;
		goto out;
	}
	buff[sz] = 0;
	tok = buff;
	while (tok) {
		v = strchr(tok, ':');
		if (!v) {
			rc = EINVAL;
			goto out;
		}
		*v = 0;
		v++;
		gs_metric = (void*)str_map_get(gs_map, tok);
		if (!gs_metric)
			goto skip;
		value.v_u64 = 0;
		switch (gs_metric->type) {
		case LDMS_V_S64:
			value.v_s64 = strtoll(v, NULL, 0);
			break;
		case LDMS_V_U64:
			value.v_u64 = strtoull(v, NULL, 0);
			break;
		case LDMS_V_D64:
			value.v_d = strtod(v, NULL);
			break;
		default:
			assert(0);
		}
		ldms_metric_set(set, gs_metric->mh, &value);
skip:
		tok = strchr(v, '\n');
		while (tok && *tok && isspace(*tok)) {
			tok++;
		}
	}
out:
	ldms_transaction_end(set);
	return rc;
}

int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
	if (!gs_map)
		gs_map = str_map_create(65537);
	if (!gs_map)
		return -1;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
