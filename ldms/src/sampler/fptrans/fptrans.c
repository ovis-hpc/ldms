/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016-2018 National Technology & Engineering Solutions
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
 * \file fptrans.c
 * \brief store fp value for testing transmission correctness.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h> // needed for strtoull processing of comp_id
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"
#include <math.h>

static ldms_set_t set = NULL;
#define SAMP "fptrans"
static base_data_t base;
static int metric_offset;

static ovis_log_t mylog;

static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	int rc;

	schema = base_schema_new(base);
	if (!schema){
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/* Location of first metric */
	metric_offset = ldms_schema_metric_count_get(schema);

	/* add float metric */
	rc = ldms_schema_metric_add(schema, "float_pi", LDMS_V_F32);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}
	/* add double metric */
	rc = ldms_schema_metric_add(schema, "double_pi", LDMS_V_D64);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}
#define ASZ 2
	rc = ldms_schema_metric_array_add(schema, "float_array_pi", LDMS_V_F32_ARRAY, ASZ);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}
	rc = ldms_schema_metric_array_add(schema, "double_array_pi", LDMS_V_D64_ARRAY, ASZ);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	//add metric values s
	int metric_no = metric_offset;
	union ldms_value v;
	v.v_f = M_PI;
	ldms_metric_set(set, metric_no++, &v);
	v.v_d = M_PI;
	ldms_metric_set(set, metric_no++, &v);
	int i;
	v.v_f = M_PI;
	for (i = 0; i < ASZ; i++)
		ldms_metric_array_set_val(set, metric_no, i, &v);
	v.v_f = 1;
	ldms_metric_array_set_val(set, metric_no, 0, &v);
	metric_no++;
	v.v_d = M_PI;
	for (i = 0; i < ASZ; i++)
		ldms_metric_array_set_val(set, metric_no, i, &v);
	v.v_d = 0;
	ldms_metric_array_set_val(set, metric_no, 0, &v);

	return 0;

 err:
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
	char* misplaced[]={"policy"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(mylog, OVIS_LERROR, "config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}
	for (i = 0; i < (sizeof(misplaced)/sizeof(misplaced[0])); i++){
		value = av_value(avl, misplaced[i]);
		if (value){
			ovis_log(mylog, OVIS_LERROR, "config argument %s is misplaced.\n",
			       misplaced[i]);
			return EINVAL;
		}
	}

	return 0;
}


static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP BASE_CONFIG_USAGE;
}


static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{

	void * arg = NULL;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	base= base_config(avl, self->inst_name, SAMP, mylog);
	if (!base)
		goto err;

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		return rc;
	}

	return 0;

err:
	base_del(base);
	return rc;
}

static int sample(struct ldmsd_sampler *self)
{
	union ldms_value v;

	if (!set) {
		ovis_log(mylog, OVIS_LERROR, "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	v.v_f = M_PI;
	ldms_metric_set(set, metric_offset, &v);
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
	if (mylog)
		ovis_log_destroy(mylog);
}

static struct ldmsd_sampler fptrans_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.sample = sample,
};

struct ldmsd_plugin *get_plugin()
{
	int rc;
	mylog = ovis_log_register("sampler."SAMP, "The log subsystem of the " SAMP " plugin");
	if (!mylog) {
		rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the subsystem "
				"of '" SAMP "' plugin. Error %d\n", rc);
	}
	set = NULL;
	return &fptrans_plugin.base;
}
