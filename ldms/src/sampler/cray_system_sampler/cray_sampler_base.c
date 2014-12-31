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
 * \file general_metrics.c
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <ctype.h>
#include <wordexp.h>
#include "rca_metrics.h"
#include "general_metrics.h"
#include "cray_sampler_base.h"

#ifdef HAVE_LUSTRE
#include "lustre_metrics.h"
#endif

#ifdef HAVE_CRAY_NVIDIA
#include "nvidia_metrics.h"
#endif

static int offns[NS_NUM] = { 0 };

int set_offns_generic(cray_system_sampler_sources_t i){
	offns[i] = 1;
}

int get_offns_generic(cray_system_sampler_sources_t i){
	return offns[i];
}

int config_generic(struct attr_value_list* kwl,
			  struct attr_value_list* avl,
			  ldmsd_msg_log_f msglog){
	char *value = NULL;
	int flag;
	int rc = 0;

	/*
	  options to turn off the generic ones here

	  NOTE: unless we ask for these values in
	  the main sampler, any ones overridden there
	  (e.g., hsn related ones) wont be affected
	  by this

	  NOTE: there is no innate checking to make sure you
	  havent turned off on that adds metrics in this but
	  then tries to populate them with an overridden
	  sample in the sampler. If you think you might write
	  such a thing, use the get_offns function to check

	  NOTE: eventually just want to add in ptrs to functions for
	  the different stages of the ones the user wants and then
	  just go thru the list
	*/


/*

  Currently don't allow these to be turned off
	value = av_value(avl, "off_nettopo");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_NETTOPO] = 1;
		}
	}

	value = av_value(avl, "off_linksmetrics");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_LINKSMETRICS] = 1;
		}
	}

	value = av_value(avl, "off_nicmetrics");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_NICMETRICS] = 1;
		}
	}
*/

	value = av_value(avl, "off_energy");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_ENERGY] = 1;
		}
	}

	value = av_value(avl, "off_vmstat");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_VMSTAT] = 1;
		}
	}

	value = av_value(avl, "off_loadavg");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_LOADAVG] = 1;
		}
	}

	value = av_value(avl, "off_current_freemem");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_CURRENT_FREEMEM] = 1;
		}
	}

	value = av_value(avl, "off_kgnilnd");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_KGNILND] = 1;
		}
	}

	//note: you can also turn off lustre but not specifying
	//any llites. If you do specify llites, this has precedence
#ifdef HAVE_LUSTRE
	value = av_value(avl, "off_lustre");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_LUSTRE] = 1;
		}
	}
#endif

	value = av_value(avl, "off_procnetdev");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_PROCNETDEV] = 1;
		}
	}

#ifdef HAVE_CRAY_NVIDIA
	value = av_value(avl, "off_nvidia");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_NVIDIA] = 1;
		}
	}

	if (!offns[NS_NVIDIA]){
		rc = config_nvidia(kwl, avl, msglog);
	}
#endif

	return rc;
};

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

int get_metric_size_generic(size_t *m_sz, size_t *d_sz,
			    cray_system_sampler_sources_t source_id,
			    ldmsd_msg_log_f msglog)
{

	int i;
	int rc = 0;

	*m_sz = 0;
	*d_sz = 0;

	if (offns[source_id]){
		//skip it
		return 0;
	}

	switch (source_id){
	case NS_NETTOPO:
		return get_metric_size_simple(nettopo_meshcoord_metricname,
					      NETTOPODIM,
					      m_sz, d_sz, msglog);
		break;
	case NS_VMSTAT:
		sample_metrics_vmstat_ptr = NULL;
		return get_metric_size_simple(VMSTAT_METRICS,
					      NUM_VMSTAT_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_LOADAVG:
		return get_metric_size_simple(LOADAVG_METRICS,
					      NUM_LOADAVG_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_ENERGY:
		return get_metric_size_simple(ENERGY_METRICS,
					      NUM_ENERGY_METRICS,
					      m_sz, d_sz, msglog);
	case NS_CURRENT_FREEMEM:
		sample_metrics_cf_ptr = NULL;
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
#ifdef HAVE_LUSTRE
		return get_metric_size_lustre(m_sz, d_sz, msglog);
#else
		//unused
		return 0;
#endif
		break;
	case NS_NVIDIA:
#ifdef HAVE_CRAY_NVIDIA
		return get_metric_size_nvidia(m_sz, d_sz, msglog);
#else
		//unused
		return 0;
#endif
		break;
	default:
		//will handle it elsewhere
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
		msglog(LDMS_LERROR,"cray_system_sampler: cannot calloc metric_table\n");
		return ENOMEM;
	}

	if (fname != NULL){
		*g_f = fopen(*fname, "r");
		if (!(*g_f)) {
			/* this is not necessarily an error */
			msglog(LDMS_LERROR,"WARNING: Could not open the source file '%s'\n",
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
			msglog(LDMS_LERROR,"cray_system_sampler: cannot add metric %d\n",
			       i);
			rc = ENOMEM;
			return rc;
		}
		ldms_set_user_data((*metric_table)[i], comp_id);
	}

	return 0;
}


int add_metrics_generic(ldms_set_t set, int comp_id,
			       cray_system_sampler_sources_t source_id,
			       ldmsd_msg_log_f msglog)
{
	int i;
	int rc = 0;

	if (offns[source_id]){
		//skip it
		return 0;
	}

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
		rc = add_metrics_simple(set, VMSTAT_METRICS,
					NUM_VMSTAT_METRICS,
					&metric_table_vmstat,
					&VMSTAT_FILE, &v_f,
					comp_id, msglog);
		if (rc != 0) {
			sample_metrics_vmstat_ptr == NULL;
			return rc;
		}
		if (v_f != NULL){
			fclose(v_f);
			v_f = NULL;
		}
		if (sample_metrics_vmstat_ptr == NULL) {
			//could be set from current_freemem
			sample_metrics_vmstat_ptr == sample_metrics_vmstat;
		}
		return rc;

		break;
	case NS_LOADAVG:
		rc = add_metrics_simple(set, LOADAVG_METRICS,
					  NUM_LOADAVG_METRICS,
					  &metric_table_loadavg,
					  &LOADAVG_FILE, &l_f,
					  comp_id, msglog);
		if (rc != 0)
			return rc;
		if (l_f != NULL){
			fclose(l_f);
			l_f = NULL;
		}

		return rc;

		break;
	case NS_ENERGY:
		rc = add_metrics_simple(set, ENERGY_METRICS,
					NUM_ENERGY_METRICS,
					&metric_table_energy,
					&ENERGY_FILE, &ene_f,
					comp_id, msglog);
		if (rc != 0)
			return rc;
		if (ene_f)
			fclose(ene_f);
		ene_f = NULL;
		return 0;
		break;
	case NS_CURRENT_FREEMEM:
		cf_m = 0;
		rc = add_metrics_simple(set, CURRENT_FREEMEM_METRICS,
					NUM_CURRENT_FREEMEM_METRICS,
					&metric_table_current_freemem,
					&CURRENT_FREEMEM_FILE, &cf_f,
					comp_id, msglog);
		if (rc != 0)
			return rc; //This will NOT happen if the file DNE
		if (cf_f != NULL) {
			fclose(cf_f);
			cf_f = NULL;
			sample_metrics_cf_ptr = &sample_metrics_current_freemem;
		} else {
			/* if there is no current_freemem, get it out of vmstat */
			sample_metrics_cf_ptr = NULL;
			sample_metrics_vmstat_ptr = sample_metrics_vmcf;
		}
		return rc;

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
			msglog(LDMS_LERROR,"cray_system_sampler: procnetdev invalid\n");
		break;
	case NS_KGNILND:
		return add_metrics_simple(set, KGNILND_METRICS,
					  NUM_KGNILND_METRICS,
					  &metric_table_kgnilnd,
					  &KGNILND_FILE, &k_f,
					  comp_id, msglog);
		break;
	case NS_LUSTRE:
#ifdef HAVE_LUSTRE
		return add_metrics_lustre(set, comp_id, msglog);
#else
		//default unused
		return 0;
#endif
		break;
	case NS_NVIDIA:
#ifdef HAVE_CRAY_NVIDIA
		rc = add_metrics_nvidia(set, comp_id, msglog);
		if (rc != 0) {
			msglog(LDMS_LERROR, "Error adding metrics nvidia\n");
			return rc;
		}
		// if this fails because cannot load the library will have nvidia_valid = 0
		rc = nvidia_setup(msglog);
		if (rc != 0) /* Warn but ok to continue...nvidia_valid may be 0 */
			msglog(LDMS_LDEBUG, "cray_system_sampler: cray_nvidia invalid\n");
		return 0;
#else
		//default unused
		return 0;
#endif
		break;
	default:
		//will handle it elsewhere
		break;
	}

	return 0;
}

int sample_metrics_generic(cray_system_sampler_sources_t source_id,
			   ldmsd_msg_log_f msglog)
{
	int rc = 0;

	if (offns[source_id]){
		//skip it
		return 0;
	}

	switch (source_id){
	case NS_NETTOPO:
		rc = sample_metrics_nettopo(msglog);
		break;
	case NS_VMSTAT:
		if (sample_metrics_vmstat_ptr != NULL)
			rc = sample_metrics_vmstat_ptr(msglog);
		else
			rc = 0;
		break;
	case NS_CURRENT_FREEMEM:
		if (sample_metrics_cf_ptr != NULL)
			rc = sample_metrics_cf_ptr(msglog);
		else
			rc = 0;
		break;
	case NS_ENERGY:
		rc = sample_metrics_energy(msglog);
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
#ifdef HAVE_LUSTRE
		rc = sample_metrics_lustre(msglog);
#else
		//do nothing
		rc = 0;
#endif
		break;
	case NS_NVIDIA:
#ifdef HAVE_CRAY_NVIDIA
		rc = sample_metrics_nvidia(msglog);
#else
		//do nothing
		rc = 0;
#endif
		break;
	default:
		//will handle it elsewhere
		break;
	}

	return rc;
}
