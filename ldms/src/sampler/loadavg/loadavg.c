/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
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
 * \file loadavg.c
 * \brief /proc/loadavg data provider
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
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"


#define PROC_FILE "/proc/loadavg"
static char *procfile = PROC_FILE;

static ldms_set_t set;

#define SAMP "loadavg"
static char *default_schema_name = SAMP;
static FILE *mf;
static ldmsd_msg_log_f msglog;
static int metric_offset;
static base_data_t base;

union la_value {
	double d;
	uint64_t u;
};
struct use_met {
	const char *name;
	int collect;
	enum ldms_value_type vtype;
	union la_value v;
};

#define MET1 "load1min"
#define MET2 "load5min"
#define MET3 "load15min"
#define MET4 "runnable"
#define MET5 "scheduling_entities"
#define MET6 "newest_pid"
struct use_met metname[] = {
	{ MET1, 1, LDMS_V_D64, {0} },
	{ MET2, 1, LDMS_V_D64, {0} },
	{ MET3, 1, LDMS_V_D64, {0} },
	{ MET4, 1, LDMS_V_U64, {0} },
	{ MET5, 1, LDMS_V_U64, {0} },
	{ MET6, 1, LDMS_V_U64, {0} }
};

#define METLEN 6

#ifdef DUMP_LOADAVG
static void dump_metrics() {
	int i;
	for (i = 0; i < METLEN; i++) {
		msglog(LDMSD_LDEBUG, SAMP ": %s %d %d\n",
			metname[i].name, metname[i].collect, metname[i].vtype);
	}
}
#endif

static int parse_metrics(const char *s)
{
	size_t i;
	int rc = 0;
	if (!s)
		return 0;
	char *x = strdup(s);
	if (!x) {
		msglog(LDMSD_LERROR, SAMP ": out of memory\n");
		return ENOMEM;
	}
	for (i = 0; i < METLEN; i++)
		metname[i].collect = 0;
	char *saveptr = NULL;
	char *m;
	for (m = strtok_r(x, ",", &saveptr); m; m = strtok_r(NULL, ",", &saveptr)) {
		for (i = 0; i < METLEN; i++) {
			if (0 == strcmp(metname[i].name, m)) {
				metname[i].collect = 1;
				break;
			}
		}
		if (i == METLEN) {
			msglog(LDMSD_LERROR, SAMP ": unknown metric %s in %s\n", m, s);
			rc = EINVAL;
			goto out;
		}
	}
out:
	free(x);
	return rc;
}

#define MAX_AUTO_NAME 128
static char der_schema[MAX_AUTO_NAME];
static const char *make_schema_name() {
	int i;
	int k = 0;
	for (i = 0; i < METLEN; i++)
		if (metname[i].collect)
			k += 1;
	if (k == METLEN) {
		return default_schema_name;
	}
	snprintf(der_schema, sizeof(der_schema), "loadavg%d%d%d%d%d%d",
		metname[0].collect,
		metname[1].collect,
		metname[2].collect,
		metname[3].collect,
		metname[4].collect,
		metname[5].collect
	);
	return der_schema;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	int rc;
	int i;

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, SAMP ": Could not open " PROC_FILE
				" : exiting sampler\n");
		return ENOENT;
	}
	fclose(mf);
	mf = NULL;

	schema = base_schema_new(base);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	/* Location of first metric from proc file */
	metric_offset = ldms_schema_metric_count_get(schema);


	/* Make sure these are added in the order they will appear in the file */
	for (i = 0; i < METLEN; i++) {
		if (metname[i].collect) {
			/*
			msglog(LDMSD_LDEBUG, SAMP ": adding metric %s %d\n",
				metname[i].name, (int)metname[i].vtype);
			*/
			/* rc = ldms_schema_metric_add(schema, metname[i].name, metname[i].vtype); */
			/* all coerced to u64 for now... */
			rc = ldms_schema_metric_add(schema, metname[i].name, LDMS_V_U64);
			if (rc < 0) {
				rc = ENOMEM;
				goto err;
			}
		} else {
			/*
			msglog(LDMSD_LDEBUG, SAMP ": skipping metric %s\n",
				metname[i].name);
			*/
		}
	}
	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

err:
	schema = NULL;
	return rc;
}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			msglog(LDMSD_LERROR, SAMP ": config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP BASE_CONFIG_USAGE " [metrics=<mlist>]\n"
		"    <sname>      The schema name, if generated default is not good enough.\n"
		"    <mlist>      comma separated list of metrics to include. If not given, all are included.\n"
		"                 complete list is " MET1 "," MET2 "," MET3 "," MET4 "," MET5 "," MET6 "\n"
		"\n";
}

/**
 * \brief Configuration
 *
 * config name=loadavg producer_name=<name> instance_name=<instance_name> [component_id=<compid> schema=<sname>] [metrics=list]
 *     producer_name    The producer id value.
 *     instance_name    The set name.
 *     component_id     The component id. Defaults to zero
 *     sname            Optional schema name. Defaults to meminfo
 *     list		csv list of wanted metrics. defaults to all.
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, NULL);
	if (rc != 0){
		return rc;
	}

	char *metrics = av_value(avl, "metrics");
	if (parse_metrics(metrics)) {
		return EINVAL;
	}
#ifdef DUMP_LOADAVG
	dump_metrics();
#endif
	const char *def_schema_name = make_schema_name();

	base = base_config(avl, SAMP, def_schema_name, msglog);
	if (!base) {
		rc = errno;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create the metric set.\n");
		goto err;
	}
	msglog(LDMSD_LDEBUG, SAMP ": plugin configured.\n");
	return 0;
 err:
	base_del(base);
	return rc;
}

static int logdisappear = 1;

static int sample(struct ldmsd_sampler *self)
{
	int rc, i;
	char *s;
	char lbuf[256];

	if (!set) {
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}
	if (!mf) {
		/* it was there in config, so disappear may be temporary*/
		mf = fopen(procfile, "r");
		if (!mf)
			return 0;
	}

	rc = fseek(mf, 0, SEEK_SET);
	if (rc) {
		if (logdisappear) {
			msglog(LDMSD_LERROR, SAMP ": %s disappeared.\n",
				procfile);
			logdisappear = 0;
		}
		fclose(mf);
		mf = NULL;
		return 0;
	} else {
		logdisappear = 1;
	}

	base_sample_begin(base);

	/*
	 * Format of the file is documented in man proc section loadavg
	 */
	s = fgets(lbuf, sizeof(lbuf), mf);
	if (!s) {
		msglog(LDMSD_LERROR, SAMP ": fgets failed.\n");
		rc = ENOENT;
		goto out;
	}

#define LINE_FMT "%lg %lg %lg %" SCNu64 "/%" SCNu64 " %" SCNu64 "\n"
	for (i = 0; i < METLEN; i++) {
		metname[0].v.u = 0;
	}
	rc = sscanf(lbuf, LINE_FMT, &metname[0].v.d, &metname[1].v.d, &metname[2].v.d,
		&metname[3].v.u, &metname[4].v.u, &metname[5].v.u);
	/*
	msglog(LDMSD_LDEBUG, SAMP ": %g %g %g %" PRIu64 "/%" PRIu64 " %" PRIu64 "\n", metname[0].v.d, metname[1].v.d, metname[2].v.d, metname[3].v.u, metname[4].v.u, metname[5].v.u);
	*/
	if (rc < METLEN) {
		rc = EINVAL;
		msglog(LDMSD_LERROR, SAMP ": fail " PROC_FILE "\n");
		goto out;
	}
	int j = 0;
	rc = 0;
	for (i = 0; i < METLEN; i++) {
		if (metname[i].collect) {
			switch (metname[i].vtype) {
			case LDMS_V_D64:
				ldms_metric_set_u64(set, (j + metric_offset),
					(uint64_t)(100*metname[i].v.d));
				break;
			case LDMS_V_U64:
				ldms_metric_set_u64(set, (j + metric_offset),
					metname[i].v.u);
				break;
			default:
				msglog(LDMSD_LCRITICAL, SAMP ": sample() memory corruption detected.\n");
				rc = EINVAL;
				goto out;
			}
			j++;
		}
	}
out:
	if (mf) {
		fclose(mf);
		mf = NULL;
	}
	base_sample_end(base);

	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	if (mf)
		fclose(mf);
	mf = NULL;
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}


static struct ldmsd_sampler loadavg_plugin = {
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
	return &loadavg_plugin.base;
}
