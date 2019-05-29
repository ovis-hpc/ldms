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

const char* ns_names[] = {
       CSS_NS(CSS_STRWRAP)
};

#if 0
int set_offns_generic(cray_system_sampler_sources_t i)
{
	offns[i] = 1;
}

int get_offns_generic(cray_system_sampler_sources_t i)
{
	return offns[i];
}
#endif

int config_generic(cray_sampler_inst_t inst, json_entity_t json)
{
	const char *value = NULL;
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

	value = json_attr_find_str(json, "off_nettopo");
	if (value) {
		flag = atoi(value);
		if (flag == 1){
			inst->offns[NS_NETTOPO] = 1;
		}
	}

	value = json_attr_find_str(json, "off_energy");
	if (value) {
		flag = atoi(value);
		if (flag == 1){
			inst->offns[NS_ENERGY] = 1;
		}
	}

	value = json_attr_find_str(json, "off_vmstat");
	if (value) {
		flag = atoi(value);
		if (flag == 1){
			inst->offns[NS_VMSTAT] = 1;
		}
	}

	value = json_attr_find_str(json, "off_loadavg");
	if (value) {
		flag = atoi(value);
		if (flag == 1){
			inst->offns[NS_LOADAVG] = 1;
		}
	}

	value = json_attr_find_str(json, "off_current_freemem");
	if (value) {
		flag = atoi(value);
		if (flag == 1){
			inst->offns[NS_CURRENT_FREEMEM] = 1;
		}
	}

	value = json_attr_find_str(json, "off_kgnilnd");
	if (value) {
		flag = atoi(value);
		if (flag == 1){
			inst->offns[NS_KGNILND] = 1;
		}
	}

	//note: you can also turn off lustre but not specifying
	//any llites. If you do specify llites, this has precedence
#ifdef HAVE_LUSTRE
	value = json_attr_find_str(json, "llite");
	if (!value) {
		value = json_attr_find_str(json, "llites"); /* also try alt name */
	}
	if (value)
		construct_str_list(&inst->llites, value);

	value = json_attr_find_str(json, "off_lustre");
	if (value) {
		flag = atoi(value);
		if (flag == 1){
			inst->offns[NS_LUSTRE] = 1;
		}
	}
#endif

	value = json_attr_find_str(json, "off_procnetdev");
	if (value) {
		flag = atoi(value);
		if (flag == 1){
			inst->offns[NS_PROCNETDEV] = 1;
		}
	}

#ifdef HAVE_CRAY_NVIDIA
	value = json_attr_find_str(json, "off_nvidia");
	if (value) {
		flag = atoi(value);
		if (flag == 1){
			inst->offns[NS_NVIDIA] = 1;
		}
	}

	if (!inst->offns[NS_NVIDIA]){
		rc = config_nvidia(inst, json);
	}
#endif

	return rc;
};


static int add_metrics_simple(cray_sampler_inst_t inst, ldms_schema_t schema, char** metric_names,
			      int num_metrics, int** metric_table,
			      char (*fname)[], FILE** g_f)
{
	int i, rc;

	if (num_metrics == 0){
		return 0;
	}

	*metric_table = calloc(num_metrics, sizeof(int));
	if (! (*metric_table)){
		INST_LOG(inst, LDMSD_LERROR, "cannot calloc metric_table\n");
		return ENOMEM;
	}

	if (fname != NULL){
		*g_f = fopen(*fname, "r");
		if (!(*g_f)) {
			/* this is not necessarily an error */
			INST_LOG(inst, LDMSD_LERROR,
				 "Could not open the source file '%s'\n",
				 *fname);
		}
	} else {
		if (g_f && *g_f)
			*g_f = NULL;
	}


	for (i = 0; i < num_metrics; i++){
		rc =  ldms_schema_metric_add(schema, metric_names[i],
					     LDMS_V_U64, "");
		if (rc < 0){
			INST_LOG(inst, LDMSD_LERROR,
				 "cannot add metric %s\n", metric_names[i]);
			rc = ENOMEM;
			return rc;
		}
		(*metric_table)[i] = rc; //this is the num used for the assignment
	}

	return 0;
}


int add_metrics_generic(cray_sampler_inst_t inst, ldms_schema_t schema,
			cray_system_sampler_sources_t source_id)
{
	int rc = 0;
	FILE *v_f = NULL;

	if (inst->offns[source_id]){
		//skip it
		return 0;
	}

	switch (source_id){
	case NS_NETTOPO:
		rc = add_metrics_simple(inst, schema,
					nettopo_meshcoord_metricname,
					NETTOPODIM,
					&inst->nettopo_metric_table,
					NULL, NULL);
		if (rc != 0)
			return rc;
		rc = nettopo_setup(inst);
		if (rc != 0){
			/* continue on, but with invalid values */
			INST_LOG(inst, LDMSD_LERROR,
				 "netopo_setup failed. All nettopo values "
				 "for this nid will be invalid\n");
		}
		return 0;
		break;
	case NS_VMSTAT:
		inst->sample_metrics_vmstat_ptr = NULL; //V3 CHECK
		rc = add_metrics_simple(inst, schema, VMSTAT_METRICS,
					NUM_VMSTAT_METRICS,
					&inst->metric_table_vmstat,
					&VMSTAT_FILE, &v_f);
		if (rc != 0) {
			inst->sample_metrics_vmstat_ptr = NULL;
			return rc;
		}
		if (v_f) {
			fclose(v_f);
			v_f = NULL;
		}
		if (inst->sample_metrics_vmstat_ptr == NULL) {
			//could be set from current_freemem
			inst->sample_metrics_vmstat_ptr = sample_metrics_vmstat;
		}
		return rc;

		break;
	case NS_LOADAVG:
		rc = add_metrics_simple(inst, schema, LOADAVG_METRICS,
					  NUM_LOADAVG_METRICS,
					  &inst->metric_table_loadavg,
					  &LOADAVG_FILE, &v_f);
		if (rc != 0)
			return rc;
		if (v_f) {
			fclose(v_f);
			v_f = NULL;
		}
		return rc;
		break;
	case NS_ENERGY:
		/* note this has an array of files that we will have to open and close each time */
		rc = add_metrics_simple(inst, schema, ENERGY_METRICS,
					NUM_ENERGY_METRICS,
					&inst->metric_table_energy,
					NULL, NULL);
		if (rc != 0)
			return rc;
		break;
	case NS_CURRENT_FREEMEM:
		inst->cf_m = 0;
		inst->sample_metrics_cf_ptr = NULL; //V3 CHECK
		rc = add_metrics_simple(inst, schema, CURRENT_FREEMEM_METRICS,
					NUM_CURRENT_FREEMEM_METRICS,
					&inst->metric_table_current_freemem,
					&CURRENT_FREEMEM_FILE, &v_f);
		if (rc != 0)
			return rc; //This will NOT happen if the file DNE
		if (v_f) {
			fclose(v_f);
			v_f = NULL;
			inst->sample_metrics_cf_ptr = sample_metrics_current_freemem;
		} else {
			/* if there is no current_freemem, get it out of vmstat */
			inst->sample_metrics_cf_ptr = NULL;
			inst->sample_metrics_vmstat_ptr = sample_metrics_vmcf;
		}
		return rc;

		break;
	case NS_PROCNETDEV:
		rc = add_metrics_simple(inst, schema, PROCNETDEV_METRICS,
					NUM_PROCNETDEV_METRICS,
					&inst->metric_table_procnetdev,
					&PROCNETDEV_FILE, &inst->pnd_f);
		if (rc != 0)
			return rc;
		rc = procnetdev_setup(inst);
		if (rc != 0) /* Warn but OK to continue */
			INST_LOG(inst, LDMSD_LERROR,
				 "cray_system_sampler: procnetdev invalid\n");
		break;
	case NS_KGNILND:
		return add_metrics_simple(inst, schema, KGNILND_METRICS,
					  NUM_KGNILND_METRICS,
					  &inst->metric_table_kgnilnd,
					  &KGNILND_FILE, &v_f);
		break;
	case NS_LUSTRE:
#ifdef HAVE_LUSTRE
		return add_metrics_lustre(schema, &inst->llites,
					  &inst->lms_list);
#else
		//default unused
		return 0;
#endif
		break;
	case NS_NVIDIA:
#ifdef HAVE_CRAY_NVIDIA
		rc = add_metrics_nvidia(inst, schema);
		if (rc != 0) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Error adding metrics nvidia\n");
			return rc;
		}
		// if this fails because cannot load the library will have nvidia_valid = 0
		rc = nvidia_setup(inst);
		if (rc != 0) /* Warn but ok to continue...nvidia_valid may be 0 */
			INST_LOG(inst, LDMSD_LDEBUG,
				 "cray_system_sampler: cray_nvidia invalid\n");
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

int sample_metrics_generic(cray_sampler_inst_t inst, ldms_set_t set,
			   cray_system_sampler_sources_t source_id)
{
	int rc = 0;

	if (inst->offns[source_id]){
		//skip it
		return 0;
	}

	if (set == NULL){
		//this shouldnt happen
		return 0;
	}

	switch (source_id){
	case NS_NETTOPO:
		rc = sample_metrics_nettopo(inst, set);
		break;
	case NS_VMSTAT:
		rc = (inst->sample_metrics_vmstat_ptr)?
				inst->sample_metrics_vmstat_ptr(inst, set):0;
		break;
	case NS_CURRENT_FREEMEM:
		rc = (inst->sample_metrics_cf_ptr)?
				inst->sample_metrics_cf_ptr(inst, set):0;
		break;
	case NS_ENERGY:
		rc = sample_metrics_energy(inst, set);
		//ok if any of these fail
		break;
	case NS_LOADAVG:
		rc = sample_metrics_loadavg(inst, set);
		break;
	case NS_KGNILND:
		rc = sample_metrics_kgnilnd(inst, set);
		break;
	case NS_PROCNETDEV:
		rc = sample_metrics_procnetdev(inst, set);
		break;
	case NS_LUSTRE:
#ifdef HAVE_LUSTRE
		rc = sample_metrics_lustre(set, &inst->lms_list);
#else
		//do nothing
		rc = 0;
#endif
		break;
	case NS_NVIDIA:
#ifdef HAVE_CRAY_NVIDIA
		rc = sample_metrics_nvidia(inst, set);
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
		INST_LOG(inst, LDMSD_LDEBUG,
			 "%s:  NS %s return error code %d in "
			 "sample_metrics_generic\n",
			 __FILE__, ns_names[source_id], rc);
	}

	return rc;
}

void cray_sampler_base_del(cray_sampler_inst_t inst)
{
	int *mtable[] = {
		inst->metric_table_energy,
		inst->metric_table_current_freemem,
		inst->metric_table_vmstat,
		inst->metric_table_loadavg,
		inst->metric_table_procnetdev,
		inst->metric_table_kgnilnd,
		inst->nettopo_metric_table,
	};
	int i, N;

	/* clean up metric tables */
	N = sizeof(mtable) / sizeof(mtable[0]);
	for (i = 0; i < N; i++) {
		if (mtable[i])
			free(mtable[i]);
	}

	/* clean up open files */
	if (inst->pnd_f)
		fclose(inst->pnd_f);
	/* Lustre */
	lustre_metric_src_list_free(&inst->lms_list);
	free_str_list(&inst->llites);
}

json_entity_t query_generic(cray_sampler_inst_t inst, const char *query)
{
	json_entity_t attr, envs, str;
	json_entity_t result = ldmsd_sampler_query(&inst->base, query);
	if (!result)
		return NULL;
	if (0 != strcmp(query, "env")) {
		/* Override only the 'env' query */
		return result;
	}

	envs = json_entity_new(JSON_LIST_VALUE);
	if (!envs)
		goto enomem;
	str = json_entity_new(JSON_STRING_VALUE, "env");
	if (!str) {
		json_entity_free(envs);
		goto enomem;
	}
	attr = json_entity_new(JSON_ATTR_VALUE, str, envs);
	if (!attr) {
		json_entity_free(str);
		json_entity_free(envs);
		goto enomem;
	}

#ifdef HAVE_CRAY_NVIDIA

	str = json_entity_new(JSON_STRING_VALUE, "LDMSD_CRAY_NVIDIA_PLUGIN_LIBPATH");
	if (!str)
		goto err;
	json_item_add(envs, str);

#endif /* HAVE_CRAY_NVIDIA */
	json_attr_add(result, attr);
	return result;
err:
	json_entity_free(attr);
enomem:
	ldmsd_plugin_qjson_err_set(result, ENOMEM, "Out of memory");
	return result;
}
