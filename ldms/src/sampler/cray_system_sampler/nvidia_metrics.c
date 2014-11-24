/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014 Sandia Corporation. All rights reserved.
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
 * \file nvidia_metrics.c
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

#include "nvidia_metrics.h"

int get_metric_size_nvidia(size_t *m_sz, size_t *d_sz,
			   ldmsd_msg_log_f msglog){
	//FIXME: for now get the info for all the devices.
	//later make it specified devices.

	int i;
	char name[NVIDIA_MAX_METRIC_NAME_SIZE];

	for (i = 0; i < nvidia_device_count; i++){
		for (j = 0; j < NVIDIA_METRICS_LEN; j++){
			snprintf(name, NVIDIA_MAX_METRIC_NAME_SIZE,
				 "%s.%s", nvidia_device_names[i],
				 NVIDIA_METRICS[j]);
			rc = ldms_get_metric_size(name, LDMS_V_U64, &m, &d);
			if (rc)
				return rc;
			msize += m;
			dsize += d;
		}
	}
	*m_sz = msize;
	*d_sz = dsize;
	return 0;
}


int add_metrics_nvidia(ldms_set_t set, int comp_id,
		       ldmsd_msg_log_f msglog){

	//FIXME: for now get the info for all the devices.
	//later make it specified devices

	int i, j;
	char name[NVIDIA_MAX_METRIC_NAME_SIZE];

	for (i = 0; i < nvidia_device_count; i++){
		for (j = 0; j < NVIDIA_METRICS_LEN; j++){
			snprintf(name, NVIDIA_MAX_METRIC_NAME_SIZE,
				 "%s.%s", nvidia_device_names[i],
				 NVIDIA_METRICS[j]);
			ldms_metric_t m = ldms_add_metric(set, name,
							  LDMS_V_U64);
			
			if (!m)
				return ENOMEM;
			lss->metrics[i+1] = m;
			ldms_set_user_data(m, comp_id);
			count++;
		}
	}
	return 0;
}

#ifdef HAVE_NVIDIA

static nvmlReturn_t getPState(nvmlDevice_t dev, unsigned int* retx){
	nvmlPstates_t state = NVML_PSTATE_15;
	unsigned int ret = 0;
	nvmlReturn_t rc;

	rc = nvmlDeviceGetPerformanceState( dev, &state );

	if (rc == NVML_SUCCESS){
		switch (state) {
		case NVML_PSTATE_15:
			ret = 15;
			break;
		case NVML_PSTATE_14:
			ret = 14;
			break;
		case NVML_PSTATE_13:
			ret = 13;
			break;
		case NVML_PSTATE_12:
			ret = 12;
			break;
		case NVML_PSTATE_11:
			ret = 11;
			break;
		case NVML_PSTATE_10:
			ret = 10;
			break;
		case NVML_PSTATE_9:
			ret = 9;
			break;
		case NVML_PSTATE_8:
			ret = 8;
			break;
		case NVML_PSTATE_7:
			ret = 7;
			break;
		case NVML_PSTATE_6:
			ret = 6;
			break;
		case NVML_PSTATE_5:
			ret = 5;
			break;
		case NVML_PSTATE_4:
			ret = 4;
			break;
		case NVML_PSTATE_3:
			ret = 3;
			break;
		case NVML_PSTATE_2:
			ret = 2;
			break;
		case NVML_PSTATE_1:
			ret = 1;
			break;
		case NVML_PSTATE_0:
			ret = 0;
		case NVML_PSTATE_UNKNOWN:
		default:
			ret = 0;

		}
	}

	*retx = ret;

	return rc;
}
#endif


int sample_metrics_nvidia(ldmsd_msg_log_f msglog){
	int i, j;
	int found_metrics = 0;
	int metric_count = 0;

	if (nvidia_valid == 0){
		return 0;
	}

#ifndef HAVE_NVIDIA
	//FIXME: do they need to be zero'ed?
	return EINVAL;
#else
	//FIXME: how to handle errs? keep going?
	for (i = 0; i < nvidia_device_count; i++){
		unsigned int ret;
		nvmlReturn_t rc;
		nvmlMemory_t meminfo;
		nvmlEccErrorCounts_t eccCounts;
		nvmlUtilization_t util;
		union ldms_value v1, v2;

		//FIXME: well known order
		/* gpu_power_usage
		   gpu_power_limit
		   gpu_pstate
		   gpu_temp
		   gpu_memory_used
		   gpu_agg_dbl_ecc_register_file
		   gpu_agg_dbl_ecc_l1_cache
		   gpu_agg_dbl_ecc_l2_cache
		   gpu_agg_dbl_ecc_total_errors
		   gpu_util_rate
		*/

		rc = nvmlDeviceGetPowerUsage(device[i], &ret);
		if (rc != NVML_SUCCESS){
			msglog(LDMS_LDEBUG,
			       "ERR: issue getting power usage for device %d\n",
			       i);
			v1.v_u64 = 0;
		} else {
			v1.v_u64 = (unsigned long long) ret;
			found_metrics++;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v1);

		rc = nvmlDeviceGetPowerManagementLimit(device[i], &ret);
		if (rc != NVML_SUCCESS){
			msglog(LDMS_LDEBUG,
			       "ERR: issue getting power limit for device %d\n",
			       i);
			v1.v_u64 = 0;
		} else {
			v1.v_u64 = (unsigned long long) ret;
			found_metrics++;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v1);

		rc = getPState(device[i], &ret);
		if (rc != NVML_SUCCESS){
			msglog(LDMS_LDEBUG,
			       "ERR: issue getting pstate for device %d\n",
			       i);
			v1.v_u64 = 0;
		} else {
			v1.v_u64 = (unsigned long long) ret;
			found_metrics++;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v1);

		rc = nvmlDeviceGetTemperature(device[i], NVML_TEMPERATURE_GPU, &ret);
		if (rc != NVML_SUCCESS){
			msglog(LDMS_LDEBUG,
			       "ERR: issue getting temperature for device %d\n",
			       i);
			v1.v_u64 = 0;
		} else {
			v1.v_u64 = (unsigned long long) ret;
			found_metrics++;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v1);

		rc = nvmlDeviceGetMemoryInfo(device[i], &meminfo);
		if (rc !=  NVML_SUCCESS){
			msglog(LDMS_LDEBUG,
			       "ERR: issue getting memory used for device %d\n",
			       i);
			v1.v_u64 = 0;
		} else {
			v1.v_u64 = (unsigned long long) (meminfo.used);
			found_metrics++;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v1);

		//FIXME: WHAT ABOUT Texture Memory?

		rc = nvmlDeviceGetDetailedEccErrors(device[i],
						    NVML_MEMORY_ERROR_TYPE_UNCORRECTED,
						    NVML_AGGREGATE_ECC, &eccCounts);

		if (rc !=  NVML_SUCCESS){
			msglog(LDMS_LDEBUG,
			       "ERR: issue getting aggregate double detailed ECC Errors for device %d\n",
			       i);
			v1.v_u64 = 0;
			ldms_set_metric(metric_table_vmstat[metric_count++], &v1);
			ldms_set_metric(metric_table_vmstat[metric_count++], &v1);
			ldms_set_metric(metric_table_vmstat[metric_count++], &v1);
			ldms_set_metric(metric_table_vmstat[metric_count++], &v1);
		} else {
			v1.v_u64 = (unsigned long long) (eccCounts.deviceMemory);
			ldms_set_metric(metric_table_vmstat[metric_count++], &v1);
			found_metrics++;
			v1.v_u64 = (unsigned long long) (eccCounts.registerFile);
			ldms_set_metric(metric_table_vmstat[metric_count++], &v1);
			found_metrics++;
			v1.v_u64 = (unsigned long long) (eccCounts.l1Cache);
			ldms_set_metric(metric_table_vmstat[metric_count++], &v1);
			found_metrics++;
			v1.v_u64 = (unsigned long long) (eccCounts.l2Cache);
			ldms_set_metric(metric_table_vmstat[metric_count++], &v1);
			found_metrics++;
		}

		rc = nvmlDeviceGetTotalEccErrors(device[i], 
						 NVML_MEMORY_ERROR_TYPE_UNCORRECTED,
						 NVML_AGGREGATE_ECC,  &v2.v_u64);
		if (rc !=  NVML_SUCCESS){
			msglog(LDMS_LDEBUG,
			       "ERR: issue getting aggregate double ECC total Errors for device %d\n",
			       i);
			v2.v_u64 = 0;
		} else {
			found_metrics++;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v2);

		//FIXME: is there a way to get the raw counters and not use their diff?

		rc = nvmlDeviceGetUtilizationRates(device[i], &util);
		if (rc !=  NVML_SUCCESS){
			msglog(LDMS_LDEBUG,
			       "ERR: issue getting GPU Utilization Rate for device %d\n",
			       i);
			v1.v_u64 = 0;
		} else {
			v1.v_u64 = (unsigned long long)(util.gpu);
			found_metrics++;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v1);
	}

	if (found_metrics != NUM_NVIDIA_METRICS*nvidia_device_count) 
		return EINVAL;

	return 0;

#endif

}


int sample_metrics_vmstat(ldmsd_msg_log_f msglog)
{
	char lbuf[256];
	char metric_name[128];
	int found_metrics;
	char* s;
	union ldms_value v;
	int j, rc;


	/* open and close each time */
	if (v_f)
		fclose(v_f);

	if (VMSTAT_FILE != NULL){
		v_f = fopen(VMSTAT_FILE, "r");
		if (!v_f)
			return 0;
	}

	found_metrics = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), v_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &v.v_u64);
		if (rc != 2) {
			msglog(LDMS_LDEBUG,"ERR: Issue reading the source file '%s'\n",
								VMSTAT_FILE);
			fclose(v_f);
			v_f = 0;
			rc = EINVAL;
			return rc;
		}
		for (j = 0; j < NUM_VMSTAT_METRICS; j++){
			if (!strcmp(metric_name, VMSTAT_METRICS[j])){
				ldms_set_metric(metric_table_vmstat[j], &v);
				found_metrics++;
				break;
			}
		}
	} while (s);

	fclose(v_f);
	v_f = 0;

	if (found_metrics != NUM_VMSTAT_METRICS){
		return EINVAL;
	}

	return 0;

}

int nvidia_setup(ldmsd_msg_log_f msglog){
	nvidia_device_count = 0;
	nvidia_valid = 0;

#ifndef HAVE_NVIDIA
	return EINVAL;
#else
	nvmlReturn_t result;
	nvmlDevice_t device;
	int i;
	
	//FIXME: is there anywhere I can do shutdown? what happens if it isnt?
	result = nvmlInit();
	if (result != NVML_SUCCESS){
		msglog(LDMS_LDEBUG, "NVML: Failed to initialize NVML: %s\n",
		       nvmlErrorString(result));
		return EINVAL;
	}

	result = nvmlDeviceGetCount(&nvidia_device_count);
	if (result != NVML_SUCCESS){
		msglog(LDMS_LDEBUG, "NVML: Failed to query device count: %s\n",
		       nvmlErrorString(result));
		return EINVAL;
	}

	for (i = 0; i < nvidia_device_count; i++){
		if (i >= NVIDIA_MAX_DEVICES){
			msglog(LDMS_LDEBUG, "NVML: Too many devices %d\n", i);
			return EINVAL;
		}
		result = nvmlDeviceGetHandleByIndex(i, &device);
		if (result != NVML_SUCCESS){
			msglog(LDMS_LDEBUG, "NVML: Failed to get handle of device %d: %s\n",
			       i, nvmlErrorString(result));
			return EINVAL;
		}

		result = nvmlDeviceGetName(device, nvidia_device_names[i],
					   NVML_DEVICE_NAME_BUFFER_SIZE);
		if (result != NVML_SUCCESS){
			msglog(LDMS_LDEBUG, "NVML: Failed to get name of device %d: %s\n",
			       i, nvmlErrorString(result));
			return EINVAL;
		}

		//FIXME: will we need this?
		result = nvmlDeviceGetPciInfo(device, &nvidia_pci[i]);
		if (result != NVML_SUCCESS){
			msglog(LDMS_LDEBUG, "NVML: Failed to get pci info for device %d: %s\n",
			       i, nvmlErrorString(result));
			return EINVAL;
		}
	}

	nvidia_valid = 1;
	return 0;
#endif
}


