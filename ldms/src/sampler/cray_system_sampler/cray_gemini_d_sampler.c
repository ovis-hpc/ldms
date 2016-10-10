/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
 * \file cray_gemini_d_sampler.c
 * \brief unified custom data provider for a combination of metrics
 *
 * NOTE: cray_gemini_d sampler is deprecated in v3.
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
#include "gemini_metrics_gpcd.h"

#ifdef HAVE_LUSTRE
#include "lustre_metrics.h"
#endif


/* General vars */
static ldms_set_t set;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;
static int off_hsn = 0;

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int create_metric_set(const char *instance_name)
{
	int rc;
	uint64_t metric_value;
	char *s;
	char lbuf[256];
	char metric_name[128];
	int i;


	schema = ldms_schema_new("cray_gemini_d");
	if (!schema)
		return ENOMEM;

	/*
	 * Will create each metric in the set, even if the source does not exist
	 */

	rc = 0;
	for (i = 0; i < NS_NUM; i++) {
		switch(i){
		case NS_LINKSMETRICS:
			if (!off_hsn){
				rc = add_metrics_gem_link_perf(schema, msglog);
				if (rc)
					goto err;
				rc = gem_link_perf_setup(msglog);
				if (rc == ENOMEM)
					goto err;
				if (rc != 0) /*  Warn but OK to continue */
					msglog(LDMSD_LERROR,"cray_gemini_d_sampler: gem_link_perf invalid\n");
			}
			break;
		case NS_NICMETRICS:
			if (!off_hsn){
				rc = add_metrics_nic_perf(set, comp_id, msglog);
				if (rc)
					goto err;
				rc = nic_perf_setup(msglog);
				if (rc == ENOMEM)
					goto err;
				if (rc != 0) /*  Warn but OK to continue */
					msglog(LDMSD_LERROR,"cray_gemini_d_sampler: nic_perf invalid\n");
			}
			break;
		default:
			rc = add_metrics_generic(schema, i, msglog);
			if (rc)
				goto err;
		}
	}


	set = ldms_set_new(instance_name, schema);
	if (!set){
		rc = errno;
		goto err;
	}

	return 0;

 err:
	ldms_schema_delete(schema);
	return rc;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value = NULL;
	char *instancename = NULL;
	char *rvalue = NULL;
	int mvalue = -1;
	int rc = 0;

	off_hsn = 0;
	producer_name = av_value(avl, "producer");
	if (!producer_name){
		msglog(LDMSD_LERROR, "cray_gemini_d_sampler: missing producer\n");
		return ENOENT;
	}

	instancename = av_value(avl, "instance");
	if (!instancename){
		msglog(LDMSD_LERROR, "cray_gemini_d_sampler: missing instance\n");
		return ENOENT;
	}

#ifdef HAVE_LUSTRE
	if (get_offns_generic(NS_LUSTRE)){
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
		off_hsn = (atoi(value) == 1? 1: 0);

	if (!offhsn){
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

//no energy for gemini
	set_offns_generic(NS_ENERGY);
	rc = config_generic(kwl, avl, msglog);
	if (rc != 0){
		goto out;
	}


	rc = create_metric_set(instancename);
	if (rc){
		msglog(LDMSD_LERROR, "cray_gemini_d_sampler: failed to create a metric set.\n");
		return rc;
	}

	ldms_set_producer_name_set(set, producer_name);
	return 0;

out:
	return rc;
}

#if 0
static uint64_t dt = 999999999;
#endif

static int sample(struct ldmsd_sampler *self)
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
		msglog(LDMSD_LDEBUG,"cray_gemini_d_sampler: plugin not initialized\n");
		return EINVAL;
	}
	ldms_transaction_begin(set);


	for (i = 0; i < NS_NUM; i++){
		rc = 0;
		switch(i){
		case NS_LINKSMETRICS:
			if (!off_hsn){
				rc = sample_metrics_gem_link_perf(set, msglog);
			} else {
				rc = 0;
			}
			break;
		case NS_NICMETRICS:
			if (!off_hsn){
				rc = sample_metrics_nic_perf(set, msglog);
			} else {
				rc = 0;
			}
			break;
		default:
			rc = sample_metrics_generic(set, i, msglog);
		}
		/* Continue if error, but report an error code */
		if (rc) {
			msglog(LDMSD_LDEBUG, "cray_aries_r_sampler: NS %d return error code %d\n",
			       i, rc);
		}
	}

 out:
	ldms_transaction_end(set);

#if 0
	clock_gettime(CLOCK_REALTIME, &time2);
	uint64_t beg_nsec = (time1.tv_sec)*1000000000+time1.tv_nsec;
	uint64_t end_nsec = (time2.tv_sec)*1000000000+time2.tv_nsec;
	dt = end_nsec - beg_nsec;
#endif

	//always return 0 so it will continue even if there was an error in a subset of metrics
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
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=cray_gemini_d_sampler producer_name=<comp_id>"
		" instance_name=<instance_name>"
		" set=<setname> rtrfile=<parsedrtr.txt> llite=<ostlist>"
		" gpu_devices=<gpulist> off_<namespace>=1\n"
		"    producer_name       The producer id value.\n"
		"    instance_name       The set name.\n",
		"    parsedrtr           The parsed interconnect file.\n",
		"    ostlist             Lustre OSTs\n",
		"    gpu_devices         GPU devices names\n",
		"    hsn_metrics_type 0/1/2- COUNTER,DERIVED,BOTH.\n",
		"    off_<namespace>     Collection for variable classes\n",
		"                        can be turned off: hsn (both links and nics)\n",
		"                        vmstat, loadavg, current_freemem, kgnilnd\n",
		"                        lustre, procnetdev, nvidia\n";
}


static struct ldmsd_sampler cray_gemini_d_sampler_plugin = {
	.base = {
		.name = "cray_gemini_d_sampler",
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
	static int init_complete = 0;
	if (init_complete)
		goto out;

	init_complete = 1;

out:
	return &cray_gemini_d_sampler_plugin.base;

err:

	return NULL;
}
