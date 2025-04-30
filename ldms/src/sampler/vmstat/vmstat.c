/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2017 Open Grid Computing, Inc. All rights reserved.
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
 * \file vmstat.c
 * \brief /proc/vmstat data provider
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
#include "ldmsd_plug_api.h"
#include "sampler_base.h"

#define PROC_FILE "/proc/vmstat"

static char *procfile = PROC_FILE;

static ldms_set_t set;
#define SAMP "vmstat"
static FILE *mf = 0;
static int metric_offset = 1;
static base_data_t base;

static ovis_log_t mylog;

#define LBUFSZ 256
static int create_metric_set(base_data_t base)
{
	int rc;
	uint64_t metric_value;
	char *s;
	char lbuf[LBUFSZ];
	char metric_name[LBUFSZ];
	ldms_schema_t schema;

	mf = fopen(procfile, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open the " SAMP " file "
				"'%s'...exiting\n", procfile);
		return ENOENT;
	}

	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = ENOMEM;
		goto err;
	}

	/* Location of first metric from proc/vmstat file */
	metric_offset = ldms_schema_metric_count_get(schema);

	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &metric_value);
		if (rc < 2)
			goto err;
		rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
		if (rc < 0)
			goto err;
	} while (s);

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}
	return 0;

 err:
	if (mf)
		fclose(mf);
	mf = NULL;
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

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " " BASE_CONFIG_USAGE;
}


static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}
	rc = config_check(kwl, avl, NULL);
	if (rc)
		return rc;

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base)
		return EINVAL;

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}
	return 0;

 err:
	base_del(base);
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
	int rc;
	int metric_no;
	char *s;
	char lbuf[LBUFSZ];
	char metric_name[LBUFSZ];
	union ldms_value v;

	if (!set) {
		ovis_log(ldmsd_plug_log_get(handle), OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	metric_no = metric_offset;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &v.v_u64);
		if (rc != 2) {
			rc = EINVAL;
			goto out;
		}
		ldms_metric_set(set, metric_no, &v);
		metric_no++;
	} while (s);
	rc = 0;
 out:
	base_sample_end(base);
	return rc;
}

static void term(ldmsd_plug_handle_t handle)
{
	if (mf)
		fclose(mf);
	mf = NULL;
	if (base)
		base_del(base);
	base = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
	set = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
