/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file cray_gemini_r_sampler.c
 * \brief unified custom data provider for a combination of metrics
 */

#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <ctype.h>
#include <wordexp.h>
#include "config.h"
#include "cray_sampler_base.h"
#include "gemini_metrics_gpcdr.h"
#include "ldmsd_plug_api.h"

#ifdef HAVE_LUSTRE
#include "lustre_metrics.h"
#endif


#include "../sampler_base.h"

ovis_log_t cray_gemini_log;

/* General vars */
static ldms_set_t set = NULL;
static char *default_schema_name = "cray_gemini_r";
static int off_hsn = 0;

static base_data_t base;

static int create_metric_set(base_data_t base)
{

	int rc, i;
	ldms_schema_t schema;

	schema = base_schema_new(base);
        if (!schema) {
                ovis_log(cray_gemini_log, OVIS_LERROR,
                       "%s: The schema '%s' could not be created, errno=%d.\n",
                       __FILE__, base->schema_name, errno);
                goto err;
        }

	/*
	 * Will create each metric in the set, even if the source does not exist
	 */

	rc = 0;
	for (i = 0; i < NS_NUM; i++) {
		switch(i){
		case NS_LINKSMETRICS:
			if (!off_hsn){
				rc = add_metrics_linksmetrics(schema);
				if (rc)
					goto err;
				rc = linksmetrics_setup();
				if (rc == ENOMEM)
					goto err;
				if (rc != 0) /*  Warn but OK to continue */
					ovis_log(cray_gemini_log, OVIS_LERROR,"linksmetrics invalid\n");
			}
			break;
		case NS_NICMETRICS:
			if (!off_hsn){
				rc = add_metrics_nicmetrics(schema);
				if (rc)
					goto err;
				rc = nicmetrics_setup();
				if (rc == ENOMEM)
					return rc;
				if (rc != 0) /*  Warn but OK to continue */
					ovis_log(cray_gemini_log, OVIS_LERROR,"nicmetrics invalid\n");
			}
			break;
		default:
			rc = add_metrics_generic(schema, i);
			if (rc) {
				ovis_log(cray_gemini_log, OVIS_LERROR,
				       "%s:  NS %s return error code %d in add_metrics_generic\n",
				       __FILE__, ns_names[i], rc);
				goto err;
			}
		}
	}

	set = base_set_new(base);
        if (!set) {
                ovis_log(cray_gemini_log, OVIS_LERROR, "%s: set null in create_metric_set\n",
                       __FILE__);
                rc = errno;
                goto err;
        }
	return 0;
 err:
	return rc;
}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl){

	char *value;
	int i;

	char* deprecated[]={"set"};
	int numdep = 1;


	for (i = 0; i < numdep; i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(cray_gemini_log, OVIS_LERROR, "%s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}


static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value = NULL;
	char *rvalue = NULL;
	int mvalue = -1;
	int rc = 0;


	rc = config_check(kwl, avl);
	if (rc != 0){
		return rc;
	}

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), default_schema_name,
                           cray_gemini_log);
        if (!base) {
                rc = errno;
                goto out;
        }


	off_hsn = 0;
	set_offns_generic(NS_ENERGY);
	rc = config_generic(kwl, avl);
	if (rc != 0){
		goto out;
	}

#ifdef HAVE_LUSTRE
	if (!get_offns_generic(NS_LUSTRE)){
		value = av_value(avl, "llite");
		if (value) {
			rc = handle_llite(value);
			if (rc)
				goto out;
		} else {
			/* if no llites, the treat as if off....
			   this is consistent with the man page.
			   why was this otherwise? */
			set_offns_generic(NS_LUSTRE);
		}
	}
#endif

	value = av_value(avl, "off_hsn");
	if (value)
		off_hsn = (atoi(value) == 1? 1:0);

	if (!off_hsn){
		value = av_value(avl,"hsn_metrics_type");
		if (value) {
			mvalue = atoi(value);
		}

		value = av_value(avl, "rtrfile");
		if (value)
			rvalue = value;

		rc = hsn_metrics_config(mvalue, rvalue);
		if (rc != 0)
			goto out;
	}


	rc = create_metric_set(base);
	if (rc){
		ovis_log(cray_gemini_log, OVIS_LERROR, "failed to create a metric set.\n");
		return rc;
	}
	return 0;

out:
	base_del(base);
	return rc;
}

#if 0
static uint64_t dt = 999999999;
#endif

static int sample(ldmsd_plug_handle_t handle)
{
	int rc;
	char *s;
	char lbuf[256];
	char metric_name[128];
	union ldms_value v;
	int i;


#if 0
	struct timespec time1, time2;
	clock_gettime(CLOCK_REALTIME, &time1);
#endif

	if (!set) {
		ovis_log(cray_gemini_log, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}
	base_sample_begin(base);

	for (i = 0; i < NS_NUM; i++){
		rc = 0;
		switch(i){
		case NS_LINKSMETRICS:
			if (!off_hsn){
				rc = sample_metrics_linksmetrics(set);
			} else {
				rc = 0;
			}
			break;
		case NS_NICMETRICS:
			if (!off_hsn){
				rc = sample_metrics_nicmetrics(set);
			} else {
				rc = 0;
			}
			break;
		default:
			rc = sample_metrics_generic(set, i);
		}
		/* Continue if error, but report an error code */
		if (rc) {
			ovis_log(cray_gemini_log, OVIS_LDEBUG,
			       "NS %s return error code %d\n",
			       ns_names[i], rc);
		}
	}

 out:
	base_sample_end(base);

#if 0
	clock_gettime(CLOCK_REALTIME, &time2);
	uint64_t beg_nsec = (time1.tv_sec)*1000000000+time1.tv_nsec;
	uint64_t end_nsec = (time2.tv_sec)*1000000000+time2.tv_nsec;
	dt = end_nsec - beg_nsec;
#endif

	//always return 0 so it will continue even if there was an error in a subset of metrics
	return 0;

}

static void term(ldmsd_plug_handle_t handle)
{
	if (base) {
                base_del(base);
                base = NULL;
        }
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=cray_gemini_r_sampler producer=<pname> component_id=<compid>"
		" instance=<iname> [schema=<sname>]"
		" rtrfile=<parsedrtr.txt> llite=<ostlist>"
		" gpu_devices=<gpulist> off_<namespace>=1\n"
		"    producer            The producer name value.\n"
		"    instance            The set name.\n",
		"    component_id        A unique number identifier\n"
		"    schema              Optional schema name. Defaults to 'cray_gemini_r'\n"
		"    parsedrtr           The parsed interconnect file.\n",
		"    ostlist             Lustre OSTs\n",
		"    gpu_devices         GPU devices names\n",
		"    hsn_metrics_type 0/1/2- COUNTER,DERIVED,BOTH.\n",
		"    off_<namespace>     Collection for variable classes\n",
		"                        can be turned off: hsn (both links and nics)\n",
		"                        vmstat, loadavg, current_freemem, kgnilnd\n",
		"                        lustre, procnetdev, nvidia\n";
}

static int constructor(ldmsd_plug_handle_t handle)
{
	static int init_complete = 0;
	if (init_complete)
		return -1;

	cray_gemini_log = ldmsd_plug_log_get(handle);
	set_cray_sampler_log(cray_gemini_log);
	init_complete = 1;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.name = "cray_gemini_r_sampler",
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
