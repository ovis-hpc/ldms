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
 * \file cray_system_sampler.c
 * \brief unified custom data provider for a combination of metrics
 */

#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <ctype.h>
#include <wordexp.h>
#include "config.h"
#include "rca_metrics.h"
#include "general_metrics.h"

#ifdef HAVE_GPCDR
#include "gemini_metrics_gpcdr.h"

typedef enum {
	NS_NETTOPO,
	NS_LINKSMETRICS,
	NS_NICMETRICS,
	NS_LUSTRE,
	NS_VMSTAT,
	NS_LOADAVG,
	NS_CURRENT_FREEMEM,
	NS_KGNILND,
	NS_PROCNETDEV,
	NS_NUM
} cray_system_sampler_sources_t;
#else
#include "gemini_metrics_gpcd.h"

typedef enum {
	NS_NETTOPO,
	NS_GEM_LINK_PERF,
	NS_NIC_PERF,
	NS_LUSTRE,
	NS_VMSTAT,
	NS_LOADAVG,
	NS_CURRENT_FREEMEM,
	NS_KGNILND,
	NS_PROCNETDEV,
	NS_NUM
} cray_system_sampler_sources_t;
#endif


/* General vars */
ldms_set_t set;
ldmsd_msg_log_f msglog;
uint64_t comp_id;


char *replace_space(char *s)
{
	char *s1;

	s1 = s;
	while ( *s1 ) {
		if ( isspace( *s1 ) ) {
			*s1 = '_';
		}
		++s1;
	}
	return s;
}

static ldms_set_t get_set()
{
	return set;
}


static int get_metric_size_simple(char** metric_names, int num_metrics,
				  size_t *m_sz, size_t *d_sz,
				  ldmsd_msg_log_f msglog)
{

	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int i, rc;

	tot_data_sz = 0;
	tot_meta_sz = 0;



	for (i = 0; i < num_metrics; i++){
		rc = ldms_get_metric_size(metric_names[i], LDMS_V_U64,
							&meta_sz, &data_sz);
		if (rc)
			return rc;
		tot_meta_sz+= meta_sz;
		tot_data_sz+= data_sz;
	}

	*m_sz = tot_meta_sz;
	*d_sz = tot_data_sz;

	return 0;

}

static int get_metric_size_generic(size_t *m_sz, size_t *d_sz,
				   cray_system_sampler_sources_t source_id)
{

	int i, rc;

	switch (source_id){
	case NS_NETTOPO:
		return get_metric_size_simple(nettopo_meshcoord_metricname,
					      NETTOPODIM,
					      m_sz, d_sz, msglog);
		break;
	case NS_VMSTAT:
		return get_metric_size_simple(VMSTAT_METRICS,
					      NUM_VMSTAT_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_LOADAVG:
		return get_metric_size_simple(LOADAVG_METRICS,
					      NUM_LOADAVG_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_CURRENT_FREEMEM:
		return get_metric_size_simple(CURRENT_FREEMEM_METRICS,
					      NUM_CURRENT_FREEMEM_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_PROCNETDEV:
		return get_metric_size_simple(PROCNETDEV_METRICS,
					      NUM_PROCNETDEV_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_KGNILND:
		return get_metric_size_simple(KGNILND_METRICS,
					      NUM_KGNILND_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_LUSTRE:
		return get_metric_size_lustre(m_sz, d_sz, msglog);
		break;
#ifdef HAVE_GPCDR
	case NS_LINKSMETRICS:
		return get_metric_size_linksmetrics(m_sz, d_sz, msglog);
		break;
	case NS_NICMETRICS:
		return get_metric_size_nicmetrics(m_sz, d_sz, msglog);
		break;
#else
	case NS_NIC_PERF:
		return get_metric_size_nic_perf(m_sz, d_sz, msglog);
		break;
	case NS_GEM_LINK_PERF:
		return get_metric_size_gem_link_perf(m_sz, d_sz, msglog);
		break;
#endif
	default:
		break;
	}

	return 0;
}


static int add_metrics_simple(ldms_set_t set, char** metric_names,
			      int num_metrics, ldms_metric_t** metric_table,
			      char (*fname)[], FILE** g_f,
			      int comp_id, ldmsd_msg_log_f msglog)
{
	int i, rc;

	if (num_metrics == 0){
		return 0;
	}

	*metric_table = calloc(num_metrics, sizeof(ldms_metric_t));
	if (! (*metric_table)){
		msglog("cray_system_sampler: cannot calloc metric_table\n");
		return ENOMEM;
	}

	if (fname != NULL){
		*g_f = fopen(*fname, "r");
		if (!(*g_f)) {
			/* this is not an error */
			msglog("WARNING: Could not open the source file '%s'\n",
			       *fname);
		}
	} else {
		if (g_f && *g_f)
			*g_f = NULL;
	}


	for (i = 0; i < num_metrics; i++){
		(*metric_table)[i] = ldms_add_metric(set, metric_names[i],
						     LDMS_V_U64);

		if (!(*metric_table)[i]){
			msglog("cray_system_sampler: cannot add metric %d\n",
			       i);
			rc = ENOMEM;
			return rc;
		}
		ldms_set_user_data((*metric_table)[i], comp_id);
	}

	return 0;
}


static int add_metrics_generic(int comp_id,
			       cray_system_sampler_sources_t source_id)
{
	int i, rc;

	switch (source_id){
	case NS_NETTOPO:
		rc = add_metrics_simple(set,
					nettopo_meshcoord_metricname,
					NETTOPODIM,
					&nettopo_metric_table,
					NULL, NULL,
					comp_id, msglog);
		if (rc != 0)
			return rc;
		nettopo_setup(msglog);
		return 0;
	case NS_VMSTAT:
		return add_metrics_simple(set, VMSTAT_METRICS,
					  NUM_VMSTAT_METRICS,
					  &metric_table_vmstat,
					  &VMSTAT_FILE, &v_f,
					  comp_id, msglog);
		break;
	case NS_LOADAVG:
		return add_metrics_simple(set, LOADAVG_METRICS,
					  NUM_LOADAVG_METRICS,
					  &metric_table_loadavg,
					  &LOADAVG_FILE, &l_f,
					  comp_id, msglog);
		break;
	case NS_CURRENT_FREEMEM:
		return add_metrics_simple(set, CURRENT_FREEMEM_METRICS,
					  NUM_CURRENT_FREEMEM_METRICS,
					  &metric_table_current_freemem,
					  &CURRENT_FREEMEM_FILE, &cf_f,
					  comp_id, msglog);
		break;
	case NS_PROCNETDEV:
		rc = add_metrics_simple(set, PROCNETDEV_METRICS,
					  NUM_PROCNETDEV_METRICS,
					  &metric_table_procnetdev,
					  &PROCNETDEV_FILE, &pnd_f,
					  comp_id, msglog);
		if (rc != 0)
			return rc;
		rc = procnetdev_setup(msglog);
		if (rc != 0) /* Warn but OK to continue */
			msglog("cray_system_sampler: procnetdev invalid\n");
		break;
	case NS_KGNILND:
		return add_metrics_simple(set, KGNILND_METRICS,
					  NUM_KGNILND_METRICS,
					  &metric_table_kgnilnd,
					  &KGNILND_FILE, &k_f,
					  comp_id, msglog);
		break;
	case NS_LUSTRE:
		return add_metrics_lustre(set, comp_id, msglog);
		break;
#ifdef HAVE_GPCDR
	case NS_LINKSMETRICS:
		rc = add_metrics_linksmetrics(set, comp_id, msglog);
		if (rc != 0)
			return rc;
		rc = linksmetrics_setup(msglog);
		if (rc == ENOMEM)
			return rc;
		if (rc != 0) /*  Warn but OK to continue */
			msglog("cray_system_sampler: linksmetrics invalid\n");
		return 0;
		break;
	case NS_NICMETRICS:
		rc = add_metrics_nicmetrics(set, comp_id, msglog);
		if (rc != 0)
			return rc;
		rc = nicmetrics_setup(msglog);
		if (rc == ENOMEM)
			return rc;
		if (rc != 0) /*  Warn but OK to continue */
			msglog("cray_system_sampler: nicmetrics invalid\n");
		return 0;
		break;
#else
	case NS_NIC_PERF:
		rc = add_metrics_nic_perf(set, comp_id, msglog);
		if (rc != 0)
			return rc;
		rc = nic_perf_setup(msglog);
		if (rc == ENOMEM)
			return rc;
		if (rc != 0) /*  Warn but OK to continue */
			msglog("cray_system_sampler: nic_perf invalid\n");
		return 0;
		break;
	case NS_GEM_LINK_PERF:
		rc = add_metrics_gem_link_perf(set, comp_id, msglog);
		if (rc != 0)
			return rc;
		rc = gem_link_perf_setup(msglog);
		if (rc == ENOMEM)
			return rc;
		if (rc != 0) /*  Warn but OK to continue */
			msglog("cray_system_sampler: gem_link_perf invalid\n");
		return 0;
		break;
#endif
	default:
		break;
	}

	return 0;
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
		rc =  get_metric_size_generic(&meta_sz, &data_sz, i);
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
		rc = add_metrics_generic(comp_id, i);
		if (rc)
			goto err;
	}

	return 0;

 err:
	ldms_set_release(set);
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

	value = av_value(avl,"gemini_metrics_type");
	if (value) {
		gemini_metrics_type = atoi(value);
		if ((gemini_metrics_type < GEMINI_METRICS_COUNTER) ||
		    (gemini_metrics_type > GEMINI_METRICS_BOTH)){
			rc = EINVAL;
			goto out;
		}
	} else {
		gemini_metrics_type = GEMINI_METRICS_COUNTER;
	}


#ifdef HAVE_GPCDR
	/* for GPCDR only need rtrfile for derived metrics */
	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){

		value = av_value(avl, "rtrfile");
		if (value)
			rtrfile = strdup(value);
		else
			rtrfile = NULL;
	}
#else
	/* always need rtrfile for gpcd metrics */
	value = av_value(avl, "rtrfile");
	if (value)
		rtrfile = strdup(value);
	else
		rtrfile = NULL;
#endif

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
		msglog("cray_system_sampler: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);

	retrc = 0;
	for (i = 0; i < NS_NUM; i++){
		switch (i){
		case NS_NETTOPO:
			rc = sample_metrics_nettopo(msglog);
			break;
		case NS_VMSTAT:
			rc = sample_metrics_vmstat(msglog);
			break;
		case NS_CURRENT_FREEMEM:
			rc = sample_metrics_current_freemem(msglog);
			break;
		case NS_LOADAVG:
			rc = sample_metrics_loadavg(msglog);
			break;
		case NS_KGNILND:
			rc = sample_metrics_kgnilnd(msglog);
			break;
		case NS_PROCNETDEV:
			rc = sample_metrics_procnetdev(msglog);
			break;
		case NS_LUSTRE:
			rc = sample_metrics_lustre(msglog);
			break;
#ifdef HAVE_GPCDR
		case NS_LINKSMETRICS:
			rc = sample_metrics_linksmetrics(msglog);
			break;
		case NS_NICMETRICS:
			rc = sample_metrics_nicmetrics(msglog);
			break;
#else
		case NS_NIC_PERF:
			rc = sample_metrics_nic_perf(msglog);
			break;
		case NS_GEM_LINK_PERF:
			rc = sample_metrics_gem_link_perf(msglog);
			break;
#endif
		default:
			//do nothing
			break;
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
	return  "config name=cray_system_sampler component_id=<comp_id>"
		" set=<setname> rtrfile=<parsedrtr.txt> llite=<ostlist>\n"
		"    comp_id             The component id value.\n"
		"    setname             The set name.\n",
		"    parsedrtr           The parsed interconnect file.\n",
		"    ostlist             Lustre OSTs\n",
		"    gemini_metrics_type 0/1/2- COUNTER,DERIVED,BOTH.\n";
}


static struct ldmsd_sampler cray_system_sampler_plugin = {
	.base = {
		.name = "cray_system_sampler",
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
	return &cray_system_sampler_plugin.base;

err:
	if (lustre_idx_map) {
		str_map_free(lustre_idx_map);
		lustre_idx_map = NULL;
	}
	return NULL;
}
