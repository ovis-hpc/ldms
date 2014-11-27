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
#include "lustre_metrics.h"
#include "cray_sampler_base.h"
#include "gemini_metrics_gpcd.h"


/* General vars */
static ldms_set_t set;
static ldmsd_msg_log_f msglog;
static uint64_t comp_id;


static ldms_set_t get_set()
{
	return set;
}

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc;
	uint64_t metric_value;
	char *s;
	char lbuf[256];
	char metric_name[128];
	int i;

	/*
	 * Determine the metric set size.
	 * Will create each metric in the set, even if the source does not exist
	 */


	tot_data_sz = 0;
	tot_meta_sz = 0;

	for (i = 0; i < NS_NUM; i++){
		switch (i){
		case NS_ENERGY:
			meta_sz = 0;
			data_sz = 0;
			rc = 0;
			break;
		case NS_LINKSMETRICS:
			rc = get_metric_size_gem_link_perf(&meta_sz, &data_sz, msglog);
			break;
		case NS_NICMETRICS:
			rc = get_metric_size_nic_perf(&meta_sz, &data_sz, msglog);
			break;
		default:
			//returns zero vals if not in generic
			rc = get_metric_size_generic(&meta_sz, &data_sz, i, msglog);
		}
		if (rc)
			return rc;
		tot_meta_sz += meta_sz;
		tot_data_sz += data_sz;
	}


	/* Create the metric set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	/*
	 * Define all the metrics.
	 */
	rc = ENOMEM;

	for (i = 0; i < NS_NUM; i++) {
		switch(i){
		case NS_ENERGY:
			rc = 0;
			break;
		case NS_LINKSMETRICS:
			rc = add_metrics_gem_link_perf(set, comp_id, msglog);
			if (rc)
				goto err;
			rc = gem_link_perf_setup(msglog);
			if (rc == ENOMEM)
				goto err;
			if (rc != 0) /*  Warn but OK to continue */
				msglog(LDMS_LDEBUG,"cray_gemini_d_sampler: gem_link_perf invalid\n");
			break;
		case NS_NICMETRICS:
			rc = add_metrics_nic_perf(set, comp_id, msglog);
			if (rc)
				goto err;
			rc = nic_perf_setup(msglog);
			if (rc == ENOMEM)
				goto err;
			if (rc != 0) /*  Warn but OK to continue */
				msglog(LDMS_LDEBUG,"cray_gemini_d_sampler: nic_perf invalid\n");
			break;
		default:
			rc = add_metrics_generic(set, comp_id, i, msglog);
			if (rc)
				goto err;
		}
	}

 err:
	ldms_destroy_set(set);
	return rc;
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc = 0;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtol(value, NULL, 0);

	value = av_value(avl, "llite");
	if (value) {
		rc = handle_llite(value);
		if (rc)
			goto out;
	} else {
		rc = EINVAL;
		goto out;
	}

	value = av_value(avl,"hsn_metrics_type");
	if (value) {
		hsn_metrics_type = atoi(value);
		if ((hsn_metrics_type < HSN_METRICS_COUNTER) ||
		    (hsn_metrics_type > HSN_METRICS_BOTH)){
			rc = EINVAL;
			goto out;
		}
	} else {
		hsn_metrics_type = HSN_METRICS_COUNTER;
	}

	/* always need rtrfile for gpcd metrics */
	value = av_value(avl, "rtrfile");
	if (value)
		rtrfile = strdup(value);
	else
		rtrfile = NULL;

	value = av_value(avl, "set");
	if (value)
		rc = create_metric_set(value);

out:
	return rc;
}

#if 0
static uint64_t dt = 999999999;
#endif

static int sample(void)
{
	int rc;
	int retrc;
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
		msglog(LDMS_LDEBUG,"cray_gemini_d_sampler: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);

	retrc = 0;
	for (i = 0; i < NS_NUM; i++){
		switch(i){
		case NS_ENERGY:
			rc = 0;
			break;
		case NS_LINKSMETRICS:
			rc = sample_metrics_gem_link_perf(msglog);
			break;
		case NS_NICMETRICS:
			rc = sample_metrics_nic_perf(msglog);
			break;
		default:
			rc = sample_metrics_generic(i, msglog);
		}
		/* Continue if error, but eventually report an error code */
		if (rc)
			retrc = rc;
	}

 out:
	ldms_end_transaction(set);

#if 0
        clock_gettime(CLOCK_REALTIME, &time2);
        uint64_t beg_nsec = (time1.tv_sec)*1000000000+time1.tv_nsec;
        uint64_t end_nsec = (time2.tv_sec)*1000000000+time2.tv_nsec;
        dt = end_nsec - beg_nsec;
#endif
	return retrc;
}

static void term(void)
{
	if (set)
		ldms_destroy_set(set);
	set = NULL;
}

static const char *usage(void)
{
	return  "config name=cray_gemini_d_sampler component_id=<comp_id>"
		" set=<setname> rtrfile=<parsedrtr.txt> llite=<ostlist>\n"
		"    comp_id             The component id value.\n"
		"    setname             The set name.\n",
		"    parsedrtr           The parsed interconnect file.\n",
		"    ostlist             Lustre OSTs\n",
		"    hsn_metrics_type 0/1/2- COUNTER,DERIVED,BOTH.\n";
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

	lustre_idx_map = str_map_create(1021);
	if (!lustre_idx_map)
		goto err;

	if (str_map_id_init(lustre_idx_map, LUSTRE_METRICS,
				LUSTRE_METRICS_LEN, 1))
		goto err;

	init_complete = 1;

out:
	return &cray_gemini_d_sampler_plugin.base;

err:
	if (lustre_idx_map) {
		str_map_free(lustre_idx_map);
		lustre_idx_map = NULL;
	}
	return NULL;
}
