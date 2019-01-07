/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014-2016,2019 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014-2016,2019 Sandia Corporation. All rights reserved.
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
#include "ldmsd_sampler.h"

/* TYPES */
typedef struct gs_metric {
	char *name; /**< metric name */
	enum ldms_value_type type; /**< type */
	int mh; /**< ldms metric index */
	char unit[LDMS_UNIT_MAX]; /**< unit */
	TAILQ_ENTRY(gs_metric) entry; /**< List entry */
} *gs_metric_t;

typedef struct generic_sampler_inst_s *generic_sampler_inst_t;
struct generic_sampler_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
	TAILQ_HEAD(, gs_metric) gs_list; /**< List of metrics */
	str_map_t gs_map; /**< gs metric map */
	int gs_fd; /**< File descriptor to the metric file */
	char *path; /**< metric file path */
	char buff[65536];
};

/* FUNCTIONS */

static gs_metric_t gs_metric_alloc(const char *name, enum ldms_value_type type,
				   const char *unit)
{
	gs_metric_t gs_metric = calloc(1, sizeof(*gs_metric));
	if (!gs_metric)
		return NULL;
	gs_metric->name = strdup(name);
	if (!gs_metric->name) {
		free(gs_metric);
		return NULL;
	}
	if (unit) {
		snprintf(gs_metric->unit, sizeof(gs_metric->unit), "%s", unit);
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

/* ============== Sampler Plugin APIs ================= */

static
int generic_sampler_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	generic_sampler_inst_t inst = (void*)pi;
	int rc;
	gs_metric_t ent;
	TAILQ_FOREACH(ent, &inst->gs_list, entry) {
		rc = ldms_schema_metric_add(schema, ent->name, ent->type,
					    ent->unit);
		if (rc < 0)
			return -rc; /* rc == -errno */
		ent->mh = rc;
	}
	return 0;
}

static
int generic_sampler_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	generic_sampler_inst_t inst = (void*)pi;
	gs_metric_t gs_metric;
	ssize_t sz;
	off_t offset;
	union ldms_value value;
	char *tok, *v;
	int rc = 0;
	if (inst->gs_fd == -1) {
		inst->gs_fd = open(inst->path, O_RDONLY);
		if (inst->gs_fd < 0) {
			ldmsd_log(LDMSD_LERROR,
				  "generic_sampler: Cannot open file: %s\n",
				  inst->path);
			rc = errno;
			goto out;
		}
	}
	offset = lseek(inst->gs_fd, 0, SEEK_SET);
	if (offset < 0) {
		rc = errno;
		ldmsd_log(LDMSD_LERROR,
			  "generic_sampler: lseek fail, errno: %d\n", errno);
		close(inst->gs_fd);
		inst->gs_fd = -1;
		goto out;
	}
	sz = read(inst->gs_fd, inst->buff, sizeof(inst->buff) - 1);
	if (sz < 0) {
		rc = errno;
		ldmsd_log(LDMSD_LERROR,
			  "generic_sampler: cannot read %s, errno: %d\n",
			  inst->path, errno);
		close(inst->gs_fd);
		inst->gs_fd = -1;
		goto out;
	}
	inst->buff[sz] = 0;
	tok = inst->buff;
	while (tok) {
		v = strchr(tok, ':');
		if (!v) {
			rc = EINVAL;
			goto out;
		}
		*v = 0;
		v++;
		gs_metric = (void*)str_map_get(inst->gs_map, tok);
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
	return rc;
}


/* ============== Common Plugin APIs ================= */

static
const char *generic_sampler_desc(ldmsd_plugin_inst_t pi)
{
	return "generic_sampler - sampler plugin for 'NAME: VALUE' data format";
}

static
const char *generic_sampler_help(ldmsd_plugin_inst_t pi)
{
	return
"generic_sampler - specific synopsis\n"
"\n"
"    config [COMMON_OPTIONS] [path=<path>] mx=M1:T1[:U1],...\n"
"\n"
"Option descriptions:\n"
"    <path>        The path to the metric file (default: /tmp/metrics).\n"
"    <mx>          The comma-separated list of metric name (M#), type (T#),\n"
"                    and optionally unit (U#). The type can be one of the\n"
"                    I,U and D (for int, uint and double respectively).\n"
"                    The unit is a string (max len 7).\n"
"                    (Example: mx=read:U:bytes,write:U:bytes).\n"
"                    NOTE: The metric name can also be in range. For example,\n"
"                          `val{0..10}:d` generates val0, val1, ... val10\n"
"                          metrics of type double with no units.\n"
"                          Leading-zero formatting is also supported, e.g.\n"
"                          `val{001..10}:d` makes val001, val002, ... val010\n"
"                          metrics of type double.\n"
"\n"
"\n"
"NOTE: The data file is expected to be a text file with the following format\n"
"      for each line:\n"
"\n"
"      METRIC_NAME: METRIC_VALUE\n"
"\n";
}

static
void generic_sampler_del(ldmsd_plugin_inst_t pi)
{
	/* The undo of generic_sampler_init */
	generic_sampler_inst_t inst = (void*)pi;
	gs_metric_t ent;
	if (inst->gs_fd >= 0)
		close(inst->gs_fd);
	if (inst->gs_map)
		str_map_free(inst->gs_map);
	if (inst->path)
		free(inst->path);
	while ((ent = TAILQ_FIRST(&inst->gs_list))) {
		TAILQ_REMOVE(&inst->gs_list, ent, entry);
		gs_metric_free(ent);
	}
}

int permute_metrics(generic_sampler_inst_t inst,
		     char *name, enum ldms_value_type type, const char *unit)
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
		gs_metric = gs_metric_alloc(name, type, unit);
		if (!gs_metric)
			return ENOMEM;
		rc = str_map_insert(inst->gs_map, gs_metric->name,
				    (uint64_t)gs_metric);
		if (rc) {
			gs_metric_free(gs_metric);
			return rc;
		}
		TAILQ_INSERT_TAIL(&inst->gs_list, gs_metric, entry);
		return 0;
	}
	*v1 = 0;
	range = strdup(v0+1);
	if (!range) {
		ldmsd_log(LDMSD_LERROR, "%s: %s: range strdup: ENOMEM\n",
			  inst->base.inst_name, __func__);
		return ENOMEM;
	}
	suffix = strdup(v1+1);
	if (!suffix) {
		ldmsd_log(LDMSD_LDEBUG, "%s: %s: suffix strdup: ENOMEM\n",
			  inst->base.inst_name, __func__);
		return ENOMEM;
	}

	tok = range;
	while (*tok) {
		rc = sscanf(tok, "%[0]%d%n", lz_str, &a, &c);
		switch (rc) {
		case 0:
			lz = 0;
			rc = sscanf(tok, "%d%n", &a, &c);
			if (rc != 1) {
				ldmsd_log(LDMSD_LDEBUG, "%s: %s: "
					  "Expecting a number but got: %s\n",
					  inst->base.inst_name, __func__, tok);
				return EINVAL;
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
				ldmsd_log(LDMSD_LDEBUG, "%s: %s: "
					  "Expecting a number"
					  " after '..', but got: %s\n",
					inst->base.inst_name, __func__, tok);
				return EINVAL;
			}
			tok += c;
			/* Range */
			/* reuse 'c' */
			for (c = a; c <= b; c++) {
				sprintf(v0, "%0*d%s", lz, c, suffix);
				rc = permute_metrics(inst, name, type, unit);
				if (rc)
					return rc;
			}
		} else {
			/* Single number */
			sprintf(v0, "%0*d%s", lz, a, suffix);
			rc = permute_metrics(inst, name, type, unit);
			if (rc)
				return rc;
		}
		if (*tok == ',')
			tok++;
	}
	free(range);
	free(suffix);
	return 0;
}

static
int __process_metrics(generic_sampler_inst_t inst, char *mx)
{
	int rc;
	char *tok, *ptr, *t, *u;
	enum ldms_value_type type;
	tok = strtok_r(mx, ",", &ptr);
	while (tok) {
		t = strchr(tok, ':');
		if (!t)
			return EINVAL;
		*t = 0;
		t++;

		switch (tolower(*t)) {
		case 'i':
			type = LDMS_V_S64;
			break;
		case 'u':
			type = LDMS_V_U64;
			break;
		case 'd':
			type = LDMS_V_D64;
			break;
		default:
			ldmsd_log(LDMSD_LERROR, "%s config: unknown type %c\n",
				  inst->base.inst_name, *t);
			return EINVAL;
		}
		t++;

		if (*t == ':') {
			u = t+1;
			if (strlen(u) >= LDMS_UNIT_MAX)
				return EINVAL;
		} else if (!*t) {
			/* no unit */
			u = NULL;
		} else {
			/* syntax error */
			return EINVAL;
		}
		rc = permute_metrics(inst, tok, type, u);
		if (rc)
			return rc;
		tok = strtok_r(NULL, ",", &ptr);
	}
	return 0;
}

static
int generic_sampler_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	generic_sampler_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;
	char *value;

	rc = samp->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */
	value = av_value(avl, "path");
	if (value) {
		inst->path = strdup(value);
		if (!inst->path) {
			return ENOMEM;
		}
	}

	value = av_value(avl, "mx");
	if (!value) {
		ldmsd_log(LDMSD_LERROR, "%s: No 'mx' is given.\n",
			  pi->inst_name);
		return EINVAL;
	}
	rc = __process_metrics(inst, value);
	if (rc)
		return rc;

	/* create schema */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;

	/* create set */
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;
	return 0;
}

static
int generic_sampler_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	generic_sampler_inst_t inst = (void*)pi;
	/* override update_schema() and update_set() */
	samp->update_schema = generic_sampler_update_schema;
	samp->update_set = generic_sampler_update_set;

	/* NOTE More initialization code here if needed */
	inst->path = NULL;
	inst->gs_fd = -1;
	TAILQ_INIT(&inst->gs_list);
	inst->gs_map = str_map_create(65537);
	if (!inst->gs_map) {
		return ENOMEM;
	}

	return 0;
}

static
struct generic_sampler_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "generic_sampler",

                /* Common Plugin APIs */
		.desc   = generic_sampler_desc,
		.help   = generic_sampler_help,
		.init   = generic_sampler_init,
		.del    = generic_sampler_del,
		.config = generic_sampler_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	generic_sampler_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
