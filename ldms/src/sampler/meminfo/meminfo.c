/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2011-2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file meminfo.c
 * \brief /proc/meminfo data provider
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
#include <pthread.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

#define PROC_FILE "/proc/meminfo"
#define SAMP "meminfo"

typedef struct meminfo_s {
	ldms_set_t set;
	FILE *mf;
	int metric_offset;
	base_data_t base;
} *meminfo_t;
static ovis_log_t mylog;

#define LBUFSZ 256
static int create_metric_set(meminfo_t mi)
{
	ldms_schema_t schema;
	int rc, i;
	uint64_t metric_value;
	char *s;
	char lbuf[LBUFSZ];
	char metric_name[LBUFSZ];

	mi->mf = fopen(PROC_FILE, "r");
	if (!mi->mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open the " SAMP " file "
				"'%s'...exiting sampler\n", PROC_FILE);
		return ENOENT;
	}

	schema = base_schema_new(mi->base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, mi->base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/* Location of first metric from proc/meminfo file */
	mi->metric_offset = ldms_schema_metric_count_get(schema);

	/*
	 * Process the file to define all the metrics.
	 */
	fseek(mi->mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mi->mf);
		if (!s)
			break;

		rc = sscanf(lbuf, "%s %" PRIu64,
			    metric_name, &metric_value);
		if (rc < 2)
			break;

		/* Strip the colon from metric name if present */
		i = strlen(metric_name);
		if (i && metric_name[i-1] == ':')
			metric_name[i-1] = '\0';

		rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
	} while (s);

	mi->set = base_set_new(mi->base);
	if (!mi->set) {
		rc = errno;
		goto err;
	}

	return 0;

 err:
	if (schema)
		base_schema_delete(mi->base);
	if (mi->mf) {
		fclose(mi->mf);
		mi->mf = NULL;
	}
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
			ovis_log(mylog, OVIS_LERROR, "config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP " " BASE_CONFIG_USAGE;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	meminfo_t mi = self->context;
	int rc;

	if (mi->set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EBUSY;
	}

	rc = config_check(kwl, avl, NULL);
	if (rc != 0){
		return rc;
	}

	mi->base = base_config(avl, self->inst_name, SAMP, mylog);
	if (!mi->base) {
		rc = errno;
		goto err;
	}

	rc = create_metric_set(mi);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}
	return 0;
 err:
	return rc;
}

static int sample(struct ldmsd_sampler *self)
{
	meminfo_t mi = self->base.context;
	int rc;
	int metric_no;
	char *s;
	char lbuf[256];
	char metric_name[LBUFSZ];
	union ldms_value v;

	if (!mi->set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(mi->base);
	metric_no = mi->metric_offset;
	fseek(mi->mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mi->mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %"PRIu64, metric_name, &v.v_u64);
		if (rc != 2 && rc != 3) {
			rc = EINVAL;
			goto out;
		}

		ldms_metric_set(mi->set, metric_no, &v);
		metric_no++;
	} while (s);
 out:
	base_sample_end(mi->base);
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	meminfo_t mi = self->context;
	if (mi->mf)
		fclose(mi->mf);
	if (mi->base)
		base_del(mi->base);
	if (mi->set)
		ldms_set_delete(mi->set);
	if (mylog)
		ovis_log_destroy(mylog);
}

static struct ldmsd_sampler meminfo_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.context_size = sizeof(struct meminfo_s),
	},
	.sample = sample,
};

struct ldmsd_plugin *get_plugin()
{
	int rc;
	if (!mylog) {
		mylog = ovis_log_register("sampler."SAMP, "The log subsystem of the " SAMP " plugin");
		if (!mylog) {
			rc = errno;
			ovis_log(NULL, OVIS_LWARN, "Failed to create the subsystem "
					"of '" SAMP "' plugin. Error %d\n", rc);
		}
	}
	return &meminfo_plugin.base;
}
