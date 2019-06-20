/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2012-2016,2018-2019 Open Grid Computing, Inc. All rights
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
 * \file filesingle.c
 * \brief reads from named input files that contain only a single metric
 * and may be missing.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

#define LINEMAX 1023
#define NAMEMAX 127
#define TYPEMAX 31
#define VALMAX 31

struct single {
	char file[LINEMAX+1];
	char name[NAMEMAX+1];
	char nametime[NAMEMAX+6];
	union ldms_value missing_val; /* val if open/read fails */
	enum ldms_value_type t;
	TAILQ_ENTRY(single) entry;
	union ldms_value val; /* most recent read */
	int64_t collect_time; /* how long open/read/close took */
};

typedef struct filesingle_inst_s *filesingle_inst_t;
struct filesingle_inst_s {
	struct ldmsd_plugin_inst_s base;

	TAILQ_HEAD(single_list, single) metric_list;
	int collect_times;
};

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

static void clear_metric_list(filesingle_inst_t inst)
{
	while (!TAILQ_EMPTY(&inst->metric_list)) {
		struct single *s = TAILQ_FIRST(&inst->metric_list);
		TAILQ_REMOVE(&inst->metric_list, s, entry);
		free(s);
	}
}

void errusage(filesingle_inst_t inst, const char *l, int lno)
{
	INST_LOG(inst, LDMSD_LERROR, "Parsing error in line %d: %s\n", lno, l);
	INST_LOG(inst, LDMSD_LERROR, "Expecting: <name> <path> <type> <default>\n");
	INST_LOG(inst, LDMSD_LERROR, "where type is one of U[8,16,32,64] or S[8,16,32,64] or F32 or F64 or CHAR.\n");
}

/* parse lines of: name source type default */
int parse_single_conf(filesingle_inst_t inst, const char *conf)
{
	if (!conf)
		return EINVAL;
	int rc = 0;
	FILE *in = fopen(conf, "r");
	if (!in) {
		rc = errno;
		INST_LOG(inst, LDMSD_LERROR, "Cannot open %s\n", conf);
		return errno;
	}
	char *line = NULL;
	int lno = 0;
	char name[NAMEMAX+1];
	char file[LINEMAX+1];
	char defstr[VALMAX+1];
	char typestr[TYPEMAX+1];
	char linebuf[LINEMAX+1];

	while (1) {
		struct single *metric = NULL;
		lno++;
		line = fgets(linebuf, LINEMAX, in);
		if (!line) {
			break;
		}
		while (line < linebuf + LINEMAX && *line != '\0' && isspace(*line))
			line++;
		if (line < linebuf + LINEMAX && line[0] == '#')
			continue; /* skip comments */
		int nitems = sscanf(line, "%" stringify(NAMEMAX) "s "
			       "%" stringify(LINEMAX) "s "
			       "%" stringify(TYPEMAX) "s "
			       "%" stringify(VALMAX) "s",
				name, file, typestr, defstr);
		if (nitems < 4) {
			errusage(inst, line, lno);
			rc = EINVAL;
			goto out;
		}
		char *u = typestr;
		while (*u != '\0') {
			*u = toupper(*u);
			u++;
		}
		enum ldms_value_type vt = ldms_metric_str_to_type(typestr);
		if (vt == LDMS_V_NONE) {
			rc = EINVAL;
			errusage(inst, line, lno);
			goto out;
		}
		union ldms_value val;
		val.v_u64 = 0;
		rc = ldms_mval_parse_scalar(&val, vt, defstr);
		if (rc) {
			INST_LOG(inst, LDMSD_LERROR,
				 "%s%d: default %s invalid\n",
				 conf, lno, defstr);
			goto out;
		}
		metric = malloc(sizeof(*metric));
		if (!metric) {
			rc = ENOMEM;
			INST_LOG(inst, LDMSD_LERROR,
				 "out of memory parsing %s\n", conf);
			goto out;
		}
		strcpy(metric->file, file);
		strcpy(metric->name, name);
		sprintf(metric->nametime, "%s%s", name, ".time");
		metric->missing_val = val;
		metric->val = val;
		metric->collect_time = -1;
		metric->t = vt;
		TAILQ_INSERT_TAIL(&inst->metric_list, metric, entry);
	}
	goto done;

out:
	clear_metric_list(inst);
done:
	fclose(in);
	return rc;
}

/* ============== Sampler Plugin APIs ================= */

static
int filesingle_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	filesingle_inst_t inst = (void*)pi;
	int rc;
	struct single *s;

	TAILQ_FOREACH(s, &inst->metric_list, entry) {
		rc = ldms_schema_metric_add(schema, s->name, s->t, "");
		if (rc < 0)
			return -rc; /* rc == -errno */
	}

	if (inst->collect_times) {
		TAILQ_FOREACH(s, &inst->metric_list, entry) {
			rc = ldms_schema_metric_add(schema, s->nametime,
						    LDMS_V_S64, "");
			if (rc < 0)
				return -rc; /* rc == -errno */
		}
	}

	return 0;
}

static
int filesingle_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	filesingle_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int i;

	int rc;
	char *l;
	char lbuf[VALMAX+1];
	union ldms_value v;
	FILE *mf;
	struct timeval tv[2];
	struct timeval *tv_now = &tv[0];
	struct timeval *tv_prev = &tv[1];
	struct timeval tv_diff;

	i = samp->first_idx;
	struct single *s = NULL;
	TAILQ_FOREACH(s, &inst->metric_list, entry) {

		if (inst->collect_times) {
			gettimeofday(tv_prev, 0);
		}
		mf = fopen(s->file, "r");
		if (!mf)
			goto skip;

		l = fgets(lbuf, sizeof(lbuf), mf);
		fclose(mf);
		if (!l)
			goto skip;

		if (inst->collect_times) {
			gettimeofday(tv_now, 0);
			timersub(tv_now, tv_prev, &tv_diff);
			s->collect_time = tv_diff.tv_sec * 1000000 +
				tv_diff.tv_usec;
		}

		rc = ldms_mval_parse_scalar(&(s->val), s->t, l);
		if (rc != 0)
			goto skip;
		ldms_metric_set(set, i, &(s->val));
		i++;
		continue;
skip:
		ldms_metric_set(set, i, &(s->missing_val));
		s->collect_time = -1;
		i++;
	}

	if (inst->collect_times) {
		TAILQ_FOREACH(s, &inst->metric_list, entry) {
			if (s->collect_time != -1) {
				v.v_s64 = s->collect_time;
				ldms_metric_set(set, i, &v);
			}
			i++;
		}
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *filesingle_desc(ldmsd_plugin_inst_t pi)
{
	return "filesingle - filesingle sampler plugin";
}

static
char *_help = "\
filesingle configuration synopsis:\n\
    config name=<INST> [COMMON_OPTIONS] \n\
           conf=<metric definitions file> [timing]\n\
\n\
    where each line of file is:\n\
    <name> <file> <type> <default>\n\
    where type is one of U[8,16,32,64] or S[8,16,32,64]\n\
    or F32 or F64 or CHAR.\n";

static
const char *filesingle_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int filesingle_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	filesingle_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	ldms_set_t set;
	int rc;
	json_entity_t attr;

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	attr = json_attr_find(json, "timing");
	if (attr) {
		inst->collect_times = 1;
	}
	INST_LOG(inst, LDMSD_LINFO, "timing = %d\n", inst->collect_times);

	const char *conf = json_attr_find_str(json, "conf");
	rc = parse_single_conf(inst, conf);
	if (rc)
		return rc;

	if (TAILQ_EMPTY(&inst->metric_list)) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Empty set not allowed. (%s)\n", conf);
		return EINVAL;
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
void filesingle_del(ldmsd_plugin_inst_t pi)
{
	filesingle_inst_t inst = (void*)pi;
	clear_metric_list(inst);
}

static
int filesingle_init(ldmsd_plugin_inst_t pi)
{
	filesingle_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	/* override update_schema() and update_set() */
	samp->update_schema = filesingle_update_schema;
	samp->update_set = filesingle_update_set;

	TAILQ_INIT(&inst->metric_list);
	return 0;
}

static
struct filesingle_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "filesingle",

                /* Common Plugin APIs */
		.desc   = filesingle_desc,
		.help   = filesingle_help,
		.init   = filesingle_init,
		.del    = filesingle_del,
		.config = filesingle_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	filesingle_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
