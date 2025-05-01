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
#include "ldmsd_plug_api.h"
#include "sampler_base.h"

#define PROC_FILE "/proc/meminfo"
#define SAMP "meminfo"

typedef struct meminfo_s {
	ovis_log_t log;
	ldms_set_t set;
	FILE *mf;
	int metric_offset;
	base_data_t base;
} *meminfo_t;

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
		ovis_log(mi->log, OVIS_LERROR,
			 "Could not open the " SAMP " file "
			 "'%s'...exiting sampler\n", PROC_FILE);
		return ENOENT;
	}

	schema = base_schema_new(mi->base);
	if (!schema) {
		ovis_log(mi->log, OVIS_LERROR,
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

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " " BASE_CONFIG_USAGE;
}

static int config(ldmsd_plug_handle_t handle,
		  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	meminfo_t mi = ldmsd_plug_ctxt_get(handle);
	int rc;

	if (mi->set) {
		ovis_log(ldmsd_plug_log_get(handle), OVIS_LERROR, "Set already created.\n");
		return EBUSY;
	}

	mi->base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mi->log);
	if (!mi->base) {
		rc = errno;
		goto err;
	}

	rc = create_metric_set(mi);
	if (rc) {
		ovis_log(mi->log, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}
	mi->log = ldmsd_plug_log_get(handle);
	return 0;
 err:
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
	meminfo_t mi = ldmsd_plug_ctxt_get(handle);
	int rc;
	int metric_no;
	char *s;
	char lbuf[256];
	char metric_name[LBUFSZ];
	union ldms_value v;

	if (!mi->set) {
		ovis_log(mi->log, OVIS_LDEBUG, "plugin not initialized\n");
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

static int constructor(ldmsd_plug_handle_t handle)
{
	meminfo_t mi = calloc(1, sizeof(*mi));
	if (mi) {
		ldmsd_plug_ctxt_set(handle, mi);
		return 0;
	}
	return ENOMEM;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	meminfo_t mi = ldmsd_plug_ctxt_get(handle);
	if (mi->mf)
		fclose(mi->mf);
	if (mi->base)
		base_del(mi->base);
	if (mi->set)
		ldms_set_delete(mi->set);
	free(mi);
};

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base.type = LDMSD_PLUGIN_SAMPLER,
	.base.flags = LDMSD_PLUGIN_MULTI_INSTANCE,
	.base.config = config,
	.base.usage = usage,
	.base.constructor = constructor,
	.base.destructor = destructor,

	.sample = sample,
};
