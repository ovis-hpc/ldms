/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2012-2016,2018 Open Grid Computing, Inc. All rights reserved.
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
#include <sys/time.h>
#include <pthread.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_jobid.h"

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;
#define SAMP "filesingle"
static char *default_schema_name = SAMP;
static uint64_t compid;

/** computed Location of first metric from user file */
static int metric_offset; 
LJI_GLOBALS;
static int collect_times = 0;

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

TAILQ_HEAD(single_list, single) metric_list;


#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

static void clear_metric_list() {
	while (!TAILQ_EMPTY(&metric_list)) {
		struct single *s = TAILQ_FIRST(&metric_list);
		TAILQ_REMOVE(&metric_list, s, entry);
		free(s);
	}
}

void errusage(const char *l, int lno)
{
	msglog(LDMSD_LERROR, SAMP ": Parsing error in line %d: %s\n", lno, l);
	msglog(LDMSD_LERROR, SAMP ": Expecting: <name> <path> <type> <default>\n");
	msglog(LDMSD_LERROR, SAMP ": where type is one of U[8,16,32,64] or S[8,16,32,64] or F32 or F64 or CHAR.\n");
}

/* parse lines of: name source type default */
int parse_single_conf(const char *conf) {
	if (!conf)
		return EINVAL;
	int rc = 0;
	FILE *in = fopen(conf, "r");
	if (!in) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": Cannot open %s\n", conf);
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
			errusage(line, lno);
			rc = EINVAL;
			goto out; 
		}
		char *u = typestr;
		while (u != '\0') {
			*u = toupper(*u);
			u++;
		}
		enum ldms_value_type vt = ldms_metric_str_to_type(typestr);
		if (vt == LDMS_V_NONE) {
			rc = EINVAL;
			errusage(line, lno);
			goto out; 
		}
		union ldms_value val;
		val.v_u64 = 0;
		rc = ldms_mval_parse_scalar(&val, vt, defstr);
		if (rc) {
			msglog(LDMSD_LERROR, SAMP ": %s%d: default %s invalid\n",
				conf, lno, defstr);
			goto out;
		}
		metric = malloc(sizeof(*metric));
		if (!metric) {
			rc = ENOMEM;
			msglog(LDMSD_LERROR, SAMP ": out of memory parsing %s\n",
				conf);
			goto out;
		}
		strcpy(metric->file, file);
		strcpy(metric->name, name);
		sprintf(metric->nametime, "%s%s", name, ".time");
		metric->missing_val = val;
		metric->val = val;
		metric->collect_time = -1;
		metric->t = vt;
		TAILQ_INSERT_TAIL(&metric_list, metric, entry);
	}
	goto done;

out:
	clear_metric_list();
done:
	fclose(in);
	return rc;
}

static int create_metric_set(const char *instance_name, char* schema_name)
{
	ldms_schema_t schema;
	metric_offset = 0;
	int rc;
	rc = ENOMEM;

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		rc = ENOMEM;
		msglog(LDMSD_LERROR, SAMP
		       ": The schema '%s' could not be created.\n",
		       schema_name);
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
	metric_offset++;

	struct single *s;
	TAILQ_FOREACH(s, &metric_list, entry) {
		rc = ldms_schema_metric_add(schema, s->name, s->t);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
	}

	if (collect_times) {
		TAILQ_FOREACH(s, &metric_list, entry) {
			rc = ldms_schema_metric_add(schema, s->nametime,
							LDMS_V_S64);
			if (rc < 0) {
				rc = ENOMEM;
				goto err;
			}
		}
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	union ldms_value v;
	v.v_u64 = compid;
	ldms_metric_set(set, 0, &v);
	LJI_SAMPLE(set,1);

	return 0;

err:
	if (schema)
		ldms_schema_delete(schema);
	
	return rc;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *sname;
	int rc;

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, SAMP ": missing producer.\n");
		return ENOENT;
	}

	value = av_value(avl, "component_id");
	if (value)
		compid = (uint64_t)(atoi(value));
	else
		compid = 0;

	LJI_CONFIG(value, avl);

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, SAMP ": missing instance.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0) {
		msglog(LDMSD_LERROR, SAMP ": schema name invalid.\n");
		return EINVAL;
	}

	int tidx = av_idx_of(kwl, "timing");
	if (tidx != -1) {
		collect_times = 1;
	}
	msglog(LDMSD_LDEBUG, SAMP ": timing = %d\n", collect_times);

	const char *conf = av_value(avl, "conf");
	rc = parse_single_conf(conf);
	if (rc)
		return rc;

	if (TAILQ_EMPTY(&metric_list)) {
		msglog(LDMSD_LERROR, SAMP ": Empty set not allowed. (%s)\n", conf);
		return EINVAL;
	}

	rc = create_metric_set(value, sname);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create the metric set.\n");
		goto err;
	}
	ldms_set_producer_name_set(set, producer_name);

	return 0;

err:
	clear_metric_list();
	return rc;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int sample(struct ldmsd_sampler *self)
{
	int rc;
	char *l;
	char lbuf[VALMAX+1];
	union ldms_value v;
	int i;
	FILE *mf;
	struct timeval tv[2];
	struct timeval *tv_now = &tv[0];
	struct timeval *tv_prev = &tv[1];
	struct timeval tv_diff;

	if (!set) {
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	ldms_transaction_begin(set);
	LJI_SAMPLE(set, 1);
	i = metric_offset;
	struct single *s = NULL;
	TAILQ_FOREACH(s, &metric_list, entry) {

		if (collect_times) {
			gettimeofday(tv_prev, 0);
		}
		mf = fopen(s->file, "r");
		if (!mf)
			goto skip;

		l = fgets(lbuf, sizeof(lbuf), mf);
		fclose(mf);
		if (!l)
			goto skip;

		if (collect_times) {
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

	if (collect_times) {
		TAILQ_FOREACH(s, &metric_list, entry) {
			if (s->collect_time != -1) {
				v.v_s64 = s->collect_time;
				ldms_metric_set(set, i, &v);
			}
			i++;
		}
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
	clear_metric_list();
}

static const char *usage(struct ldmsd_plugin *self)
{
	return "config name=" SAMP " producer=<prod_name> instance=<inst_name> [component_id=<compid> schema=<sname> with_jobid=<jid>]"
	"  opt_file=<metric definitions file> [timing]\n"
	"  <prod_name>  The producer name\n"
	"  <inst_name>  The instance name\n"
	"  <compid>     Optional unique number identifier. Defaults to zero.\n"
	LJI_DESC
	"  <sname>      Optional schema name. Defaults to '" SAMP "'\n"

	"  Each line of the metric definitions file is:\n"
	"    <name> <file> <type> <default>\n"
	"  where type is one of U[8,16,32,64] or S[8,16,32,64]\n"
	"  or F32 or F64 or CHAR.\n";

}

static struct ldmsd_sampler filesingle_plugin = {
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
	set = NULL;
	TAILQ_INIT(&metric_list);
	return &filesingle_plugin.base;
}
