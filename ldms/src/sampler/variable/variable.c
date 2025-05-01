/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2018 National Technology & Engineering Solutions
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
 * \file variable.c
 * \brief 'variable set' test data provider
 *
 * Recreates the data set at a different size/name/schema every
 * sameticks samples (default 4).
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

static ovis_log_t mylog;

static ldms_set_t set = NULL;
static char *producer_name;
static ldms_schema_t schema;
#define SAMP "variable"
static char *default_schema_name = SAMP;
static uint64_t compid;

static int metric_offset = 1; /* compid is metric 0 */

static char *schema_name;
static char *instance_name;

static int sameticks = 4;
static int curtick = 0;
static int maxmets = 9; /* must be single digit */
static int minmets = 1;
static int curmets = 1;
static const char *namebase = "metnum";
static uint64_t mval = 0;

static int create_metric_set(const char *in, char* sn)
{
	int rc, i;
	union ldms_value v;
#define NMSZ 128
	char metric_name[NMSZ];

	ovis_log(mylog, OVIS_LDEBUG, "creating set at %" PRIu64 "\n", mval);
	if (sn) {
		int len = strlen(sn)+12;
		schema_name = malloc(len);
		if (!schema_name) {
			ovis_log(mylog, OVIS_LERROR, "missing schema_name\n");
			rc = ENOMEM;
			goto err;
		}
		snprintf(schema_name, len, "%s%d", sn, curmets);
	} else {
		size_t d = strlen(schema_name) - 1;
		schema_name[d] = (char)(48+ curmets);
		ovis_log(mylog, OVIS_LDEBUG, "redefined schema name %s %d\n", schema_name, curmets);
	}
	schema = ldms_schema_new(schema_name);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR, "schema_new fail\n");
		rc = ENOMEM;
		goto err;
	}
	if (in) {
		int len = strlen(in)+12;
		instance_name = malloc(len);
		if (!instance_name) {
			ovis_log(mylog, OVIS_LERROR, "missing instance_name\n");
			rc = ENOMEM;
			goto err;
		}
		snprintf(instance_name, len, "%s%d", in, curmets);
	} else {
		size_t d = strlen(instance_name) - 1;
		instance_name[d] = (char)(curmets + 48); /*ascii */
		ovis_log(mylog, OVIS_LDEBUG, "redefine instance name %s %d\n", instance_name, curmets);
	}

	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, "can't add component_id\n");
		rc = ENOMEM;
		goto err;
	}

	i = 0;
	while (i < curmets) {
		snprintf(metric_name, NMSZ, "%s%d", namebase, i);
		ovis_log(mylog, OVIS_LDEBUG, "add metric %s\n", metric_name);
		rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
		if (rc < 0) {
			ovis_log(mylog, OVIS_LERROR, "failed metric add %s\n",
				metric_name);
			rc = ENOMEM;
			goto err;
		}
		i++;
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		ovis_log(mylog, OVIS_LERROR, "can't create set\n");
		goto err;
	}

	/* add specialized metrics */
	v.v_u64 = compid;
	ldms_metric_set(set, 0, &v);
	/* add regular metrics */
	i = 0;
	while (i < curmets) {
		v.v_u64 = mval;
		mval++;
		ldms_metric_set(set, i + metric_offset, &v);
		i++;
	}
	ovis_log(mylog, OVIS_LDEBUG, "created set at %" PRIu64 "\n", mval);
	rc = ldms_set_publish(set);
	if (rc) {
		ldms_set_delete(set);
		set = NULL;
		goto err;
	}
	ldmsd_set_register(set, SAMP);
	return 0;

 err:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (schema_name) {
		free(schema_name);
		schema_name = NULL;
	}
	if (instance_name) {
		free(instance_name);
		instance_name = NULL;
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

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " producer=<prod_name> instance=<inst_name> [component_id=<compid> schema=<sname> with_jobid=<jid>]\n"
		"    <prod_name>  The producer name\n"
		"    <inst_name>  The instance name\n"
		"    <compid>     Optional unique number identifier. Defaults to zero.\n"
		"    <sname>      Optional schema name. Defaults to '" SAMP "'\n";
}

/**
 * \brief Configuration
 *
 * config name=variable producer=<name> instance=<instance_name> [component_id=<compid> schema=<sname>] [with_jobid=<jid>]
 *     producer_name    The producer id value.
 *     instance_name    The set name.
 *     component_id     The component id. Defaults to zero
 *     sname            Optional schema name. Defaults to variable
 *     jid              lookup jobid or report 0.
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *sname;
	void * arg = NULL;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		ovis_log(mylog, OVIS_LERROR, "missing producer.\n");
		return ENOENT;
	}
	producer_name = strdup(producer_name);
	if (!producer_name) {
		ovis_log(mylog, OVIS_LERROR, "OOM.\n");
		return ENOMEM;
	}

	value = av_value(avl, "component_id");
	if (value)
		compid = (uint64_t)(atoi(value));
	else
		compid = 0;

	value = av_value(avl, "instance");
	if (!value) {
		ovis_log(mylog, OVIS_LERROR, "missing instance.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0) {
		ovis_log(mylog, OVIS_LERROR, "schema name invalid.\n");
		return EINVAL;
	}

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	rc = create_metric_set(value, sname);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);
	return 0;
}

static int sample(ldmsd_plug_handle_t handle)
{
	int rc;
	union ldms_value v;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}
	curtick++;
	if (sameticks == curtick) {
		ovis_log(mylog, OVIS_LDEBUG, "set needs redefinition\n");
		curtick = 0;
		curmets++;
		if (curmets > maxmets) {
			curmets = minmets;
		}
		if (set) {
			ldmsd_set_deregister(ldms_set_instance_name_get(set), SAMP);
			ldms_set_delete(set);
			set = NULL;
		}
		if (schema)
			ldms_schema_delete(schema);
		schema = NULL;
		rc = create_metric_set(NULL, NULL);
		if (rc) {
			ovis_log(mylog, OVIS_LERROR, "failed to recreate a metric set.\n");
			return rc;
		}
		ldms_set_producer_name_set(set, producer_name);
	}

	ldms_transaction_begin(set);

	int i;
	for (i = 0; i < curmets; i++) {
		v.v_u64 = mval;
		mval++;
		/* ovis_log(mylog, OVIS_LDEBUG, "setting %d of %d\n",
			i + metric_offset, curmets);
		*/
		ldms_metric_set(set, i + metric_offset, &v);
	}

	ldms_transaction_end(set);
	return 0;
}

static void term(ldmsd_plug_handle_t handle)
{
	if (set) {
		ldmsd_set_deregister(ldms_set_instance_name_get(set), SAMP);
		ldms_set_delete(set);
		set = NULL;
	}
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	free(schema_name);
	free(producer_name);
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
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
