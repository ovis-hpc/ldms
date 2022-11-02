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

ovis_log_t __cray_sampler_log;

const char* ns_names[] = {
       CSS_NS(CSS_STRWRAP)
};

void set_cray_sampler_log(ovis_log_t pi_log)
{
	__cray_sampler_log = pi_log;
}

int set_offns_generic(cray_system_sampler_sources_t i){
	offns[i] = 1;
}

int get_offns_generic(cray_system_sampler_sources_t i){
	return offns[i];
}

int config_generic(struct attr_value_list* kwl,
			  struct attr_value_list* avl){
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


	value = av_value(avl, "off_nettopo");
	if (value){
		flag = atoi(value);
		if (flag == 1){
			offns[NS_NETTOPO] = 1;
		}
	}

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
		rc = config_nvidia(kwl, avl);
	}
#endif

	return rc;
};


static int add_metrics_simple(ldms_schema_t schema, char** metric_names,
			      int num_metrics, int** metric_table,
			      char (*fname)[], FILE** g_f)
{
	int i, rc;

	if (num_metrics == 0){
		return 0;
	}

	*metric_table = calloc(num_metrics, sizeof(int));
	if (! (*metric_table)){
		ovis_log(__cray_sampler_log, OVIS_LERROR,"cannot calloc metric_table\n");
		return ENOMEM;
	}

	if (fname != NULL){
		*g_f = fopen(*fname, "r");
		if (!(*g_f)) {
			/* this is not necessarily an error */
			ovis_log(__cray_sampler_log, OVIS_LERROR,"WARNING: Could not open the source file '%s'\n",
			       *fname);
		}
	} else {
		if (g_f && *g_f)
			*g_f = NULL;
	}


	for (i = 0; i < num_metrics; i++){
		rc =  ldms_schema_metric_add(schema, metric_names[i],
				      LDMS_V_U64);
		if (rc < 0){
			ovis_log(__cray_sampler_log, OVIS_LERROR,"cannot add metric %s\n",
			       metric_names[i]);
			rc = ENOMEM;
			return rc;
		}
		(*metric_table)[i] = rc; //this is the num used for the assignment
	}

	return 0;
}


int add_metrics_generic(ldms_schema_t schema,
			       cray_system_sampler_sources_t source_id)
{
	int i;
	int rc = 0;

	if (offns[source_id]){
		//skip it
		return 0;
	}

	switch (source_id){
	case NS_NETTOPO:
		rc = add_metrics_simple(schema,
					nettopo_meshcoord_metricname,
					NETTOPODIM,
					&nettopo_metric_table,
					NULL, NULL);
		if (rc != 0)
			return rc;
		rc = nettopo_setup();
		if (rc != 0){
			/* continue on, but with invalid values */
			ovis_log(__cray_sampler_log, OVIS_LERROR, "netopo_setup failed. All nettopo values for this nid will be invalid\n");
		}
		return 0;
		break;
	case NS_VMSTAT:
		sample_metrics_vmstat_ptr = NULL; //V3 CHECK
		rc = add_metrics_simple(schema, VMSTAT_METRICS,
					NUM_VMSTAT_METRICS,
					&metric_table_vmstat,
					&VMSTAT_FILE, &v_f);
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
		rc = add_metrics_simple(schema, LOADAVG_METRICS,
					  NUM_LOADAVG_METRICS,
					  &metric_table_loadavg,
					  &LOADAVG_FILE, &l_f);
		if (rc != 0)
			return rc;
		if (l_f != NULL){
			fclose(l_f);
			l_f = NULL;
		}

		return rc;

		break;
	case NS_ENERGY:
		for (i = 0; i < NUM_ENERGY_METRICS; i++){
			ene_f[i] = NULL;
		}
		/* note this has an array of files that we will have to open and close each time */
		rc = add_metrics_simple(schema, ENERGY_METRICS,
					NUM_ENERGY_METRICS,
					&metric_table_energy,
					NULL, NULL);
		if (rc != 0)
			return rc;
		break;
	case NS_CURRENT_FREEMEM:
		cf_m = 0;
		sample_metrics_cf_ptr = NULL; //V3 CHECK
		rc = add_metrics_simple(schema, CURRENT_FREEMEM_METRICS,
					NUM_CURRENT_FREEMEM_METRICS,
					&metric_table_current_freemem,
					&CURRENT_FREEMEM_FILE, &cf_f);
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
		rc = add_metrics_simple(schema, PROCNETDEV_METRICS,
					NUM_PROCNETDEV_METRICS,
					&metric_table_procnetdev,
					&PROCNETDEV_FILE, &pnd_f);
		if (rc != 0)
			return rc;
		rc = procnetdev_setup();
		if (rc != 0) /* Warn but OK to continue */
			ovis_log(__cray_sampler_log, OVIS_LERROR,"procnetdev invalid\n");
		break;
	case NS_KGNILND:
		return add_metrics_simple(schema, KGNILND_METRICS,
					  NUM_KGNILND_METRICS,
					  &metric_table_kgnilnd,
					  &KGNILND_FILE, &k_f);
		break;
	case NS_LUSTRE:
#ifdef HAVE_LUSTRE
		lustre_sampler_set_pilog();
		return add_metrics_lustre(schema);
#else
		//default unused
		return 0;
#endif
		break;
	case NS_NVIDIA:
#ifdef HAVE_CRAY_NVIDIA
		rc = add_metrics_nvidia(schema);
		if (rc != 0) {
			ovis_log(__cray_sampler_log, OVIS_LERROR, "Error adding metrics nvidia\n");
			return rc;
		}
		// if this fails because cannot load the library will have nvidia_valid = 0
		rc = nvidia_setup();
		if (rc != 0) /* Warn but ok to continue...nvidia_valid may be 0 */
			ovis_log(__cray_sampler_log, OVIS_LDEBUG, "cray_nvidia invalid\n");
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

int sample_metrics_generic(ldms_set_t set, cray_system_sampler_sources_t source_id)
{
	int rc = 0;

	if (offns[source_id]){
		//skip it
		return 0;
	}

	if (set == NULL){
		//this shouldnt happen
		return 0;
	}

	switch (source_id){
	case NS_NETTOPO:
		rc = sample_metrics_nettopo(set);
		break;
	case NS_VMSTAT:
		if (sample_metrics_vmstat_ptr != NULL)
			rc = sample_metrics_vmstat_ptr(set);
		else
			rc = 0;
		break;
	case NS_CURRENT_FREEMEM:
		if (sample_metrics_cf_ptr != NULL)
			rc = sample_metrics_cf_ptr(set);
		else
			rc = 0;
		break;
	case NS_ENERGY:
		rc = sample_metrics_energy(set);
		//ok if any of these fail
		break;
	case NS_LOADAVG:
		rc = sample_metrics_loadavg(set);
		break;
	case NS_KGNILND:
		rc = sample_metrics_kgnilnd(set);
		break;
	case NS_PROCNETDEV:
		rc = sample_metrics_procnetdev(set);
		break;
	case NS_LUSTRE:
#ifdef HAVE_LUSTRE
		rc = sample_metrics_lustre(set);
#else
		//do nothing
		rc = 0;
#endif
		break;
	case NS_NVIDIA:
#ifdef HAVE_CRAY_NVIDIA
		rc = sample_metrics_nvidia(set);
#else
		//do nothing
		rc = 0;
#endif
		break;
	default:
		//will handle it elsewhere
		break;
	}

	if (rc != 0) {
		ovis_log(__cray_sampler_log, OVIS_LDEBUG,
		       "%s:  NS %s return error code %d in sample_metrics_generic\n",
		       __FILE__, ns_names[source_id], rc);
	}


	return rc;
}
