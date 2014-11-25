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

static const char* (*nvmlErrorStringPtr) (nvmlReturn_t);
static nvmlReturn_t (*nvmlInitPtr) (void);
static nvmlReturn_t (*nvmlShutdownPtr) (void);
static nvmlReturn_t (*nvmlDeviceGetCountPtr) (unsigned int *);
static nvmlReturn_t (*nvmlDeviceGetHandleByIndexPtr) (unsigned int, nvmlDevice_t *);
static nvmlReturn_t (*nvmlDeviceGetHandleByIndexPtr) (unsigned int, nvmlDevice_t *);
static nvmlReturn_t (*nvmlDeviceGetNamePtr) (nvmlDevice_t, char *, unsigned int);
static nvmlReturn_t (*nvmlDeviceGetPciInfoPtr) (nvmlDevice_t, nvmlPciInfo_t *);
static nvmlReturn_t (*nvmlDeviceGetPowerUsagePtr) (nvmlDevice_t, unsigned int *);
static nvmlReturn_t (*nvmlDeviceGetPowerManagementLimitPtr) (nvmlDevice_t, unsigned int*);
static nvmlReturn_t (*nvmlDeviceGetPerformanceStatePtr) (nvmlDevice_t, unsigned int*);
static nvmlReturn_t (*nvmlDeviceGetTemperaturePtr) (nvmlDevice_t, nvmlTemperatureSensors_t, unsigned int *);
static nvmlReturn_t (*nvmlDeviceGetMemoryInfoPtr) (nvmlDevice_t, nvmlMemory_t *);
static nvmlReturn_t (*nvmlDeviceGetDetailedEccErrorsPtr) (nvmlDevice_t, nvmlEccBitType_t, nvmlEccCounterType_t, nvmlEccErrorCounts_t *);
static nvmlReturn_t (*nvmlDeviceGetTotalEccErrorsPtr) (nvmlDevice_t, nvmlEccBitType_t, nvmlEccCounterType_t, unsigned long long *);
static nvmlReturn_t (*nvmlDeviceGetUtilizationRatesPtr) (nvmlDevice_t, nvmlUtilization_t *);

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


static nvmlReturn_t getPState(nvmlDevice_t dev, unsigned int* retx){
	unsigned int ret = 0;
	nvmlPstates_t state;
	nvmlReturn_t rc;
	
	if (nvmlDeviceGetPerformanceStatePtr == NULL){
		*ret = 0;
		return NVML_PSTATE_UNKNOWN;
	}

	rc = (*nvmlDeviceGetPerformanceStatePtr)(dev, &state);
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


int sample_metrics_nvidia(ldmsd_msg_log_f msglog){
	int i, j;
	int found_metrics = 0;
	int metric_count = 0;

	if (nvidia_valid == 0){
		return 0;
	}


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

		if (nvmlDeviceGetPowerUsagePtr != NULL){
			rc = (*nvmlDeviceGetPowerUsagePtr)(device[i], &ret);
			if (rc != NVML_SUCCESS){
				msglog(LDMS_LDEBUG,
				       "ERR: issue getting power usage for device %d\n",
				       i);
				v1.v_u64 = 0;
			} else {
				v1.v_u64 = (unsigned long long) ret;
				found_metrics++;
			}
		} else {
			v1.v_u64 = 0;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v1);

		if (nvmlDeviceGetPowerManagementLimitPtr != NULL){
			rc = (*nvmlDeviceGetPowerManagementLimitPtr)(device[i], &ret);
			if (rc != NVML_SUCCESS){
				msglog(LDMS_LDEBUG,
				       "ERR: issue getting power limit for device %d\n",
				       i);
				v1.v_u64 = 0;
			} else {
				v1.v_u64 = (unsigned long long) ret;
				found_metrics++;
			}
		} else {
			v1.v_u64 = 0;
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

		if (nvmlDeviceGetTemperaturePtr != NULL){
			rc = (*nvmlDeviceGetTemperaturePtr)(device[i], NVML_TEMPERATURE_GPU, &ret);
			if (rc != NVML_SUCCESS){
				msglog(LDMS_LDEBUG,
				       "ERR: issue getting temperature for device %d\n",
				       i);
				v1.v_u64 = 0;
			} else {
				v1.v_u64 = (unsigned long long) ret;
				found_metrics++;
			}
		} else {
			v1.v_u64 = 0;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v1);

		if (nvmlDeviceGetMemoryInfoPtr != NULL){
			rc = (*nvmlDeviceGetMemoryInfoPtr)(device[i], &meminfo);
			if (rc !=  NVML_SUCCESS){
				msglog(LDMS_LDEBUG,
				       "ERR: issue getting memory used for device %d\n",
				       i);
				v1.v_u64 = 0;
			} else {
				v1.v_u64 = (unsigned long long) (meminfo.used);
				found_metrics++;
			}
		} else {
			v1.v_u64 = 0;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v1);

		if (nvmlDeviceGetDetailedEccErrorsPtr != NULL){
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


		if (nvmlDeviceGetTotalEccErrorsPtr != NULL){
			rc = (*nvmlDeviceGetTotalEccErrorsPtr)(device[i], 
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
		} else {
			v2.v_u64 = 0;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v2);

		//FIXME: is there a way to get the raw counters and not use their diff?
		if (nvmlDeviceGetUtilizationRatesPtr != NULL){
			rc = (*nvmlDeviceGetUtilizationRatesPtr)(device[i], &util);
			if (rc !=  NVML_SUCCESS){
				msglog(LDMS_LDEBUG,
				       "ERR: issue getting GPU Utilization Rate for device %d\n",
				       i);
				v1.v_u64 = 0;
			} else {
				v1.v_u64 = (unsigned long long)(util.gpu);
				found_metrics++;
			}
		} else {
			v1.v_u64 = 0;
		}
		ldms_set_metric(metric_table_vmstat[metric_count++], &v1);
	}

	if (found_metrics != NUM_NVIDIA_METRICS*nvidia_device_count) 
		return EINVAL;

	return 0;

}


static int loadFcnts(){
	void *dl1;

	dl1 = dlopen("libnvidia-ml.so", RTLD_NOW | RTLD_GLOBAL );
	//dl1 = dlopen("libnvidia-ml.so", RTLD_NOW | RTLD_DEEPBIND);
	if (!dl1){
		msglog(LDMS_LDEBUG, "NVML runtime library libnvidia-ml.so not found\n");
		return -1;
	}

	nvmlErrorStringPtr = dlsym(dl1, "nvmlErrorString");
	if ((dlerror() != NULL) || (!nvmlErrorStringPtr)){
		msglog(LDMS_LDEBUG, "NVML ErrorString not found\n");
		return -1;
	}

	nvmlInitPtr = dlsym(dl1, "nvmlInit");
	if ((dlerror() != NULL) || (!nvmlInitPtr)){
		msglog(LDMS_LDEBUG, "NVML init not found\n");
		return -1;
	}

	nvmlShutdownPtr = dlsym(dl1, "nvmlShutdown");
	if ((dlerror() != NULL) || (!nvmlShutdownPtr)){
		msglog(LDMS_LDEBUG, "NVML shutdown not found\n");
		return -1;
	}

	nvmlDeviceGetCountPtr = dlsym(dl1, "nvmlDeviceGetCount");
	if ((dlerror() != NULL) || (!nvmlDeviceGetCountPtr)){
		msglog(LDMS_LDEBUG, "NVML DeviceGetCountPtr not found\n");
		return -1;
	}

	nvmlDeviceGetHandleByIndexPtr = dlsym(dl1, "nvmlDeviceGetHandleByIndex");
	if ((dlerror() != NULL) || (!nvmlDeviceGetHandleByIndexPtr)){
		msglog(LMDS_LDEBUG, "NVML DeviceGetHandleByIndexPtr not found\n");
		return -1;
	}

	nvmlDeviceGetNamePtr = dlsym(dl1, "nvmlDeviceGetName");
	if ((dlerror() != NULL) || (!nvmlDeviceGetNamePtr)){
		msglog(LDMS_LDEBUG, "NVML DeviceGetNamePtr not found\n");
		return -1;
	}

	nvmlDeviceGetPciInfoPtr = dlsym(dl1, "nvmlDeviceGetPciInfo");
	if ((dlerror() != NULL) || (!nvmlDeviceGetPciInfo)){
		msglog(LDMS_LDEBUG, "NVML DeviceGetPciInfo not found\n");
		return -1;
	}

	// these ok to be null
	nvmlDeviceGetPowerUsagePtr = dlsym(dl1, "nvmlDeviceGetPowerUsage");
	if ((dlerror() != NULL) || (!nvmlDeviceGetPowerUsage)){
		msglog(LDMS_LDEBUG, "NVML DeviceGetPowerUsage not found\n");
	}

	nvmlDeviceGetPowerManagementLimitPtr = dlsym(dl1, "nvmlDeviceGetPowerManagementLimit");
	if ((dlerror() != NULL) || (!nvmlDeviceGetPowerManagementLimit)){
		msglog(LDMS_LDEBUG, "NVML DeviceGetPowerManagementLimit not found\n");
	}

	nvmlDeviceGetPerformanceStatePtr = dlsym(dl1, "nvmlDeviceGetPerformanceState");
	if ((dlerror() != NULL) || (!nvmlDeviceGetPerformanceStatePtr)){
		msglog(LDMS_LDEBUG, "NVML DeviceGetPerformanceState not found\n");
	}

	nvmlDeviceGetTemperaturePtr = dlsym(dl1, "nvmlDeviceGetTemperature");
	if ((dlerror() != NULL) || (!nvmlDeviceGetTemperaturePtr)){
		msglog(LDMS_LDEBUG, "NVML DeviceGetTemperature not found\n");
	}

	nvmlDeviceGetMemoryInfoPtr = dlsym(dl1, "nvmlDeviceGetMemoryInfo");
	if ((dlerror() != NULL) || (!nvmlDeviceGetMemoryInfoPtr)){
		msglog(LDMS_LDEBUG, "NVML DeviceGetMemoryInfo not found\n");
	}

	nvmlDeviceGetDetailedEccErrorsPtr = dlsym(dl1, "nvmlDeviceGetDetailedEccErrors");
	if ((dlerror() != NULL) || (!nvmlDeviceGetDetailedEccErrors)){
		msglog(LDMS_LDEBUG, "NVML DeviceGetDetailedEccErrors not found\n");
	}

	nvmlDeviceGetTotalEccErrorsPtr = dlsym(dl1, "nvmlDeviceGetTotalEccErrors");
	if ((dlerror() != NULL) || (!nvmlDeviceGetDetailedEccErrors)){
		msglog(LDMS_LDEBUG, "NVML DeviceGetDetailedEccErrors not found\n");
	}

	nvmlDeviceGetUtilizationRatesPtr = dlsym(dl1, "nvmlDeviceGetUtilizationRates");
	if ((dlerror() != NULL) || (!nvmlDeviceGetUtilizationRates)){
		msglog(LDMS_LDEBUG, "NVML DeviceGetUtilizationRates not found\n");
	}

	return 0;

}

int nvidia_shutdown(ldmsd_msg_log_f msglog){
	nvmlReturn_t result;

	result =(*nvmlShutdownPtr)();
        if (NVML_SUCCESS != result) {
		msglog(LDMS_LDEBUG, "Failed to shutdown NVML: %s\n", (*nvmlErrorStringPtr)(result));
		return -1;
	}

	return 0;
}


int nvidia_setup(ldmsd_msg_log_f msglog){
	nvmlReturn_t result;
	nvmlDevice_t device;
	int i;
	int rc;

	nvidia_device_count = 0;
	nvidia_valid = 0;

	rc = loadFctns();
	if (rc != 0){
		msglog(LDMS_LDEBUG, "NVML loadFctns failed\n");
		return EINVAL;
	}

	//FIXME: is there anywhere I can do shutdown? what happens if it isnt?
	result = (*nvmlInitPtr)();
	if (result != NVML_SUCCESS){
		msglog(LDMS_LDEBUG, "NVML: Failed to initialize NVML: %s\n",
		       (*nvmlErrorStringPtr)(result));
		return EINVAL;
	}

	result = (*nvmlDeviceGetCountPtr)(&nvidia_device_count);
	if (result != NVML_SUCCESS){
		msglog(LDMS_LDEBUG, "NVML: Failed to query device count: %s\n",
		       (*nvmlErrorStringPtr)(result));
		return EINVAL;
	}

	for (i = 0; i < nvidia_device_count; i++){
		if (i >= NVIDIA_MAX_DEVICES){
			msglog(LDMS_LDEBUG, "NVML: Too many devices %d\n", i);
			return EINVAL;
		}
		result = (*nvmlDeviceGetHandleByIndexPtr)(i, &device);
		if (result != NVML_SUCCESS){
			msglog(LDMS_LDEBUG, "NVML: Failed to get handle of device %d: %s\n",
			       i, (*nvmlErrorStringPtr)(result));
			return EINVAL;
		}

		result = (*nvmlDeviceGetNamePtr)(device, nvidia_device_names[i],
					   NVML_DEVICE_NAME_BUFFER_SIZE);
		if (result != NVML_SUCCESS){
			msglog(LDMS_LDEBUG, "NVML: Failed to get name of device %d: %s\n",
			       i, (*nvmlErrorStringPtr)(result));
			return EINVAL;
		}

		//FIXME: will we need this?
		result = (*nvmlDeviceGetPciInfoPtr)(device, &nvidia_pci[i]);
		if (result != NVML_SUCCESS){
			msglog(LDMS_LDEBUG, "NVML: Failed to get pci info for device %d: %s\n",
			       i, (*nvmlErrorStringPtr)(result));
			return EINVAL;
		}
	}

	nvidia_valid = 1;
	return 0;
}


