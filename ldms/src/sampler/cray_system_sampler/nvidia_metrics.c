/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014-2016 Sandia Corporation. All rights reserved.
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
#include <dlfcn.h>
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
#include "ldmsd.h"

#include "nvml.h"
#include "nvidia_metrics.h"

/* Defined in cray_sampler_base.c */
extern ovis_log_t __cray_sampler_base;

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
static nvmlReturn_t (*nvmlDeviceGetMemoryErrorCounterPtr)(nvmlDevice_t,
							  nvmlMemoryErrorType_t,
							  nvmlEccCounterType_t,
							  nvmlMemoryLocation_t,
							  unsigned long long *);
//static nvmlReturn_t (*nvmlDeviceGetDetailedEccErrorsPtr) (nvmlDevice_t, nvmlEccBitType_t, nvmlEccCounterType_t, nvmlEccErrorCounts_t *);
static nvmlReturn_t (*nvmlDeviceGetTotalEccErrorsPtr) (nvmlDevice_t, nvmlEccBitType_t, nvmlEccCounterType_t, unsigned long long *);
static nvmlReturn_t (*nvmlDeviceGetUtilizationRatesPtr) (nvmlDevice_t, nvmlUtilization_t *);

#define NVIDIA_MAX_DEVICES 4
#define NVIDIA_MAX_METRIC_NAME_SIZE (NVML_DEVICE_NAME_BUFFER_SIZE+40)
static unsigned int nvidia_device_count = 0;
static char nvidia_device_names[NVIDIA_MAX_DEVICES][NVML_DEVICE_NAME_BUFFER_SIZE];
static nvmlPciInfo_t nvidia_pci[NVIDIA_MAX_DEVICES];
static nvmlDevice_t nvidia_device[NVIDIA_MAX_DEVICES];
static char* gpudevicestr = NULL;

static char* NVIDIA_METRICS[] = {"gpu_power_usage",
				 "gpu_power_limit",
				 "gpu_pstate",
				 "gpu_temp",
				 "gpu_memory_used",
				 "gpu_agg_dbl_ecc_l1_cache",
				 "gpu_agg_dbl_ecc_l2_cache",
				 "gpu_agg_dbl_ecc_device_memory",
				 "gpu_agg_dbl_ecc_register_file",
				 "gpu_agg_dbl_ecc_texture_memory",
				 "gpu_agg_dbl_ecc_total_errors",
				 "gpu_util_rate"};

#define NUM_NVIDIA_METRICS (sizeof(NVIDIA_METRICS)/sizeof(NVIDIA_METRICS[0]))
static int* metric_table_nvidia = NULL;
static int nvidia_valid = 0;

int config_nvidia(struct attr_value_list* kwl,
		  struct attr_value_list* avl){
	char* value = NULL;

	value = av_value(avl, "gpu_devices");
	if (value){
		ovis_log(__cray_sampler_base, OVIS_LDEBUG, "cray_system_sampler configuring for gpudevices <%s>\n",
		       value);
		gpudevicestr = strdup(value);
	} else {
		ovis_log(__cray_sampler_base, OVIS_LDEBUG, "cray_system_sampler configuring no gpudevices\n");
		gpudevicestr = NULL;
	}

	return 0;
};


static char *replace_underscore(char *s)
{
	char *s1;

	s1 = s;
	while ( *s1 ) {
		if ( *s1 == '_'){
			*s1 = ' ';
		}
		++s1;
	}
	return s;
};


int  add_metrics_nvidia(ldms_schema_t schema){

	char name[NVIDIA_MAX_METRIC_NAME_SIZE];
	size_t m, d;
	char* pch;
	char* saveptr = NULL;
	int count;
	int i, j;
	int rc;


	nvidia_device_count = 0;
	if (NUM_NVIDIA_METRICS == 0){
		ovis_log(__cray_sampler_base, OVIS_LDEBUG, "Adding no nvidia gpu metrics\n");
		return 0;
	}

	if (gpudevicestr == NULL){
		//its ok to have no devices
		ovis_log(__cray_sampler_base, OVIS_LDEBUG, "No nvidia devices specified\n");
		return 0;
	} else {
		ovis_log(__cray_sampler_base, OVIS_LDEBUG, "Device string is <%s>\n", gpudevicestr);
	}

	//determine the devices...independent of whether they exist or not
	count = 0;
	pch = strtok_r(gpudevicestr, ",\n", &saveptr); //NOTE: free?
	while (pch != NULL){
		if (count == NVIDIA_MAX_DEVICES){
			ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML: Too many devices %d\n", count);
			return EINVAL;
		}
		if (strlen(pch) == 0){
			ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML: Empty device name %d\n", count);
			return EINVAL;
		}
		snprintf(nvidia_device_names[count], NVML_DEVICE_NAME_BUFFER_SIZE,
			 "%s", pch);

		nvidia_device[count] = NULL; //Note: this works
		count++;
		pch = strtok_r(NULL, ",\n", &saveptr);
	}

	nvidia_device_count = count;

	if (nvidia_device_count == 0){
		ovis_log(__cray_sampler_base, OVIS_LDEBUG,"Adding no nvidia gpu metrics\n");
		return 0;
	}

//	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "nvidia metric table allocating space for %d metrics\n",
//	       (nvidia_device_count*NUM_NVIDIA_METRICS));
	metric_table_nvidia = calloc((nvidia_device_count*NUM_NVIDIA_METRICS), sizeof(int));
	if (!metric_table_nvidia){
		ovis_log(__cray_sampler_base, OVIS_LDEBUG,"cray_system_sampler: cannot calloc metric_table_nvidia\n");
		return ENOMEM;
	}

	count = 0;
	for (i = 0; i < nvidia_device_count; i++){
		for (j = 0; j < NUM_NVIDIA_METRICS; j++){
			snprintf(name, NVIDIA_MAX_METRIC_NAME_SIZE,
				 "%s.%s", nvidia_device_names[i],
				 NVIDIA_METRICS[j]);
			rc = ldms_schema_metric_add(schema, name, LDMS_V_U64);
			if (rc < 0){
				ovis_log(__cray_sampler_base, OVIS_LERROR, "Failed to add metric <%s>\n", name);
				return ENOMEM;
			}
			metric_table_nvidia[count] = rc; //this is the num used for the assignment
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
		*retx = 0;
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


int sample_metrics_nvidia(ldms_set_t set){
	int i, j;
	int metric_count = 0;

	if (nvidia_valid == 0){
		return 0;
	}

	//NOTE: how to handle errs? keep going?
	for (i = 0; i < nvidia_device_count; i++){
		unsigned int ret;
		unsigned long long retl;
		nvmlReturn_t rc;
		nvmlMemory_t meminfo;
		nvmlUtilization_t util;
		union ldms_value v1, v2;

		if (nvidia_device[i] == NULL) {
			ovis_log(__cray_sampler_base, OVIS_LDEBUG, "Device is null for <%s>\n",
			       nvidia_device_names[i]);
			continue;
		}

		//NOTE: well known order
		if (nvmlDeviceGetPowerUsagePtr != NULL){
			rc = (*nvmlDeviceGetPowerUsagePtr)(nvidia_device[i], &ret);
			if (rc != NVML_SUCCESS){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG,
				       "ERR: issue getting power usage for device %d\n",
				       i);
				v1.v_u64 = 0;
			} else {
				v1.v_u64 = (unsigned long long) ret;
			}
		} else {
			v1.v_u64 = 0;
		}
		ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);

		if (nvmlDeviceGetPowerManagementLimitPtr != NULL){
			rc = (*nvmlDeviceGetPowerManagementLimitPtr)(nvidia_device[i], &ret);
			if (rc != NVML_SUCCESS){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG,
				       "ERR: issue getting power limit for device %d\n",
				       i);
				v1.v_u64 = 0;
			} else {
				v1.v_u64 = (unsigned long long) ret;
			}
		} else {
			v1.v_u64 = 0;
		}
		ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);

		rc = getPState(nvidia_device[i], &ret);
		if (rc != NVML_SUCCESS){
			ovis_log(__cray_sampler_base, OVIS_LDEBUG,
			       "ERR: issue getting pstate for device %d\n",
			       i);
			v1.v_u64 = 0;
		} else {
			v1.v_u64 = (unsigned long long) ret;
		}
		ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);

		if (nvmlDeviceGetTemperaturePtr != NULL){
			rc = (*nvmlDeviceGetTemperaturePtr)(nvidia_device[i], NVML_TEMPERATURE_GPU, &ret);
			if (rc != NVML_SUCCESS){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG,
				       "ERR: issue getting temperature for device %d\n",
				       i);
				v1.v_u64 = 0;
			} else {
				v1.v_u64 = (unsigned long long) ret;
			}
		} else {
			v1.v_u64 = 0;
		}
		ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);

		if (nvmlDeviceGetMemoryInfoPtr != NULL){
			rc = (*nvmlDeviceGetMemoryInfoPtr)(nvidia_device[i], &meminfo);
			if (rc !=  NVML_SUCCESS){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG,
				       "ERR: issue getting memory used for device %d\n",
				       i);
				v1.v_u64 = 0;
			} else {
				v1.v_u64 = (unsigned long long) (meminfo.used);
			}
		} else {
			v1.v_u64 = 0;
		}
		ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);


		if (nvmlDeviceGetMemoryErrorCounterPtr != NULL){
			rc = nvmlDeviceGetMemoryErrorCounterPtr(nvidia_device[i],
								NVML_MEMORY_ERROR_TYPE_UNCORRECTED,
								NVML_AGGREGATE_ECC,
								NVML_MEMORY_LOCATION_L1_CACHE,
								&retl);
			if (rc !=  NVML_SUCCESS){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG,
				       "ERR: issue getting aggregate double detailed ECC l1 cache Errors for device %d\n",
				       i);
				v1.v_u64 = 0;
				ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			} else {
				v1.v_u64 = retl;
				ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			}

			rc = nvmlDeviceGetMemoryErrorCounterPtr(nvidia_device[i],
								NVML_MEMORY_ERROR_TYPE_UNCORRECTED,
								NVML_AGGREGATE_ECC,
								NVML_MEMORY_LOCATION_L2_CACHE,
								&retl);
			if (rc !=  NVML_SUCCESS){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG,
				       "ERR: issue getting aggregate double detailed ECC l2 cache Errors for device %d\n",
				       i);
				v1.v_u64 = 0;
				ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			} else {
				v1.v_u64 = retl;
				ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			}

			rc = nvmlDeviceGetMemoryErrorCounterPtr(nvidia_device[i],
								NVML_MEMORY_ERROR_TYPE_UNCORRECTED,
								NVML_AGGREGATE_ECC,
								NVML_MEMORY_LOCATION_DEVICE_MEMORY,
								&retl);
			if (rc !=  NVML_SUCCESS){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG,
				       "ERR: issue getting aggregate double detailed ECC device memory Errors for device %d\n", i);
				v1.v_u64 = 0;
				ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			} else {
				v1.v_u64 = retl;
				ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			}

			rc = nvmlDeviceGetMemoryErrorCounterPtr(nvidia_device[i],
								NVML_MEMORY_ERROR_TYPE_UNCORRECTED,
								NVML_AGGREGATE_ECC,
								NVML_MEMORY_LOCATION_REGISTER_FILE,
								&retl);
			if (rc !=  NVML_SUCCESS){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG,
				       "ERR: issue getting aggregate double detailed ECC register file Errors for device %d\n", i);
				v1.v_u64 = 0;
				ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			} else {
				v1.v_u64 = retl;
				ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			}


			rc = nvmlDeviceGetMemoryErrorCounterPtr(nvidia_device[i],
								NVML_MEMORY_ERROR_TYPE_UNCORRECTED,
								NVML_AGGREGATE_ECC,
								NVML_MEMORY_LOCATION_TEXTURE_MEMORY,
								&retl);
			if (rc !=  NVML_SUCCESS){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG,
				       "ERR: issue getting aggregate double detailed ECC texture memory Errors for device %d\n", i);
				v1.v_u64 = 0;
				ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			} else {
				v1.v_u64 = retl;
				ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			}

		} else {
			v1.v_u64 = 0;
			ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
			ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
		}

		if (nvmlDeviceGetTotalEccErrorsPtr != NULL){
			unsigned long long teep;
			rc = (*nvmlDeviceGetTotalEccErrorsPtr)(nvidia_device[i],
							       NVML_MEMORY_ERROR_TYPE_UNCORRECTED,
							       NVML_AGGREGATE_ECC,  &teep);
			if (rc !=  NVML_SUCCESS){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG,
				       "ERR: issue getting aggregate double ECC total Errors for device %d\n",
				       i);
				v2.v_u64 = 0;
			} else {
				v2.v_u64 = teep;
			}
		} else {
			v2.v_u64 = 0;
		}
		ldms_metric_set(set, metric_table_nvidia[metric_count++], &v2);

		//TODO: is there a way to get the raw counters and not use their diff?
		if (nvmlDeviceGetUtilizationRatesPtr != NULL){
			rc = (*nvmlDeviceGetUtilizationRatesPtr)(nvidia_device[i], &util);
			if (rc !=  NVML_SUCCESS){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG,
				       "ERR: issue getting GPU Utilization Rate for device %d\n",
				       i);
				v1.v_u64 = 0;
			} else {
				v1.v_u64 = (unsigned long long)(util.gpu);
			}
		} else {
			v1.v_u64 = 0;
		}
		ldms_metric_set(set, metric_table_nvidia[metric_count++], &v1);
	}

	return 0;

}


static int loadFctns(){
	char library_name[PATH_MAX];
	void *dl1;

	char *path = getenv("LDMSD_CRAY_NVIDIA_PLUGIN_LIBPATH");
	if (path) {
		ovis_log(__cray_sampler_base, OVIS_LDEBUG, "LDMSD_CRAY_NVIDIA_PLUGIN_LIBPATH <%s>\n", path);
		sprintf(library_name, "%s/libnvidia-ml.so", path);
	} else {
		ovis_log(__cray_sampler_base, OVIS_LERROR, "LDMSD_CRAY_NVIDIA_PLUGIN_LIBPATH undefined\n");
		path = getenv("LDMSD_PLUGIN_LIBPATH");
		if (!path) {
			ovis_log(__cray_sampler_base, OVIS_LERROR, "LDMSD_CRAY_NVIDIA_PLUGIN_LIBPATH undefined\n");
			sprintf(library_name, "libnvidia-ml.so");
		} else {
			sprintf(library_name, "%s/libnvidia-ml.so", path);
		}
	}

	dl1 = dlopen(library_name, RTLD_NOW | RTLD_GLOBAL );
	//dl1 = dlopen(library_name, RTLD_NOW | RTLD_DEEPBIND);
	if ((dlerror() != NULL) || (!dl1)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML runtime library libnvidia-ml.so not found\n");
		return -1;
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML runtime library libnvidia-ml.so found\n");

	nvmlErrorStringPtr = dlsym(dl1, "nvmlErrorString");
	if ((dlerror() != NULL) || (!nvmlErrorStringPtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML ErrorString not found\n");
		return -1;
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML ErrorString Found\n");

	nvmlInitPtr = dlsym(dl1, "nvmlInit");
	if ((dlerror() != NULL) || (!nvmlInitPtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML init not found\n");
		return -1;
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML init Found\n");

	nvmlShutdownPtr = dlsym(dl1, "nvmlShutdown");
	if ((dlerror() != NULL) || (!nvmlShutdownPtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML shutdown not found\n");
		return -1;
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML shutdown Found\n");

	nvmlDeviceGetCountPtr = dlsym(dl1, "nvmlDeviceGetCount");
	if ((dlerror() != NULL) || (!nvmlDeviceGetCountPtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetCountPtr not found\n");
		return -1;
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegetcount Found\n");

	nvmlDeviceGetHandleByIndexPtr = dlsym(dl1, "nvmlDeviceGetHandleByIndex");
	if ((dlerror() != NULL) || (!nvmlDeviceGetHandleByIndexPtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetHandleByIndexPtr not found\n");
		return -1;
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegethandlebyindex Found\n");

	nvmlDeviceGetNamePtr = dlsym(dl1, "nvmlDeviceGetName");
	if ((dlerror() != NULL) || (!nvmlDeviceGetNamePtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetNamePtr not found\n");
		return -1;
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegetname Found\n");

	nvmlDeviceGetPciInfoPtr = dlsym(dl1, "nvmlDeviceGetPciInfo");
	if ((dlerror() != NULL) || (!nvmlDeviceGetPciInfoPtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetPciInfo not found\n");
		return -1;
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegetpciinfo Found\n");

	// these ok to be null
	nvmlDeviceGetPowerUsagePtr = dlsym(dl1, "nvmlDeviceGetPowerUsage");
	if ((dlerror() != NULL) || (!nvmlDeviceGetPowerUsagePtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetPowerUsage not found\n");
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegetpowerusage Found\n");

	nvmlDeviceGetPowerManagementLimitPtr = dlsym(dl1, "nvmlDeviceGetPowerManagementLimit");
	if ((dlerror() != NULL) || (!nvmlDeviceGetPowerManagementLimitPtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetPowerManagementLimit not found\n");
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegetpowermanagementlimit Found\n");

	nvmlDeviceGetPerformanceStatePtr = dlsym(dl1, "nvmlDeviceGetPerformanceState");
	if ((dlerror() != NULL) || (!nvmlDeviceGetPerformanceStatePtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetPerformanceState not found\n");
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegetperformancestate Found\n");

	nvmlDeviceGetTemperaturePtr = dlsym(dl1, "nvmlDeviceGetTemperature");
	if ((dlerror() != NULL) || (!nvmlDeviceGetTemperaturePtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetTemperature not found\n");
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegettemperature Found\n");

	nvmlDeviceGetMemoryInfoPtr = dlsym(dl1, "nvmlDeviceGetMemoryInfo");
	if ((dlerror() != NULL) || (!nvmlDeviceGetMemoryInfoPtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetMemoryInfo not found\n");
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegetmemoryInfo Found\n");

	//NOTE: this will return a non-null value even though the function is deprecated.
	//it fails when it is tried to be called
//	nvmlDeviceGetDetailedEccErrorsPtr = dlsym(dl1, "nvmlDeviceGetDetailedEccErrors");
	nvmlDeviceGetMemoryErrorCounterPtr = dlsym(dl1, "nvmlDeviceGetMemoryErrorCounter");
	if ((dlerror() != NULL) || (!nvmlDeviceGetMemoryErrorCounterPtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetMemoryErrorCounter not found\n");
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegetMemoryErrorCounter Found\n");

	nvmlDeviceGetTotalEccErrorsPtr = dlsym(dl1, "nvmlDeviceGetTotalEccErrors");
	if ((dlerror() != NULL) || (!nvmlDeviceGetTotalEccErrorsPtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetTotalEccErrors not found\n");
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegetTotalEccErrors Found\n");

	nvmlDeviceGetUtilizationRatesPtr = dlsym(dl1, "nvmlDeviceGetUtilizationRates");
	if ((dlerror() != NULL) || (!nvmlDeviceGetUtilizationRatesPtr)){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML DeviceGetUtilizationRates not found\n");
	}
	ovis_log(__cray_sampler_base, OVIS_LDEBUG, "NVML devicegetUtilizationRates Found\n");

	return 0;

}

int nvidia_shutdown(){
	nvmlReturn_t result;

	result =(*nvmlShutdownPtr)();
	if (NVML_SUCCESS != result) {
		ovis_log(__cray_sampler_base, OVIS_LERROR, "Failed to shutdown NVML: %s\n", (*nvmlErrorStringPtr)(result));
		return -1;
	}


	return 0;
}


int nvidia_setup(){
	nvmlReturn_t result;
	nvmlDevice_t device;
	char name[NVML_DEVICE_NAME_BUFFER_SIZE];
	int count = 0;
	int i,j;
	int rc;

	nvidia_valid = 0;

	//if we've asked for no devices, then skip all this and consider it valid
	if (nvidia_device_count == 0){
		nvidia_valid = 1;
		return 0;
	}

	rc = loadFctns();
	if (rc != 0){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML loadFctns failed\n");
		return EINVAL;
	}

	//TODO: is there anywhere I can do shutdown? what happens if it isnt?
	result = (*nvmlInitPtr)();
	if (result != NVML_SUCCESS){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML: Failed to initialize NVML: %s\n",
		       (*nvmlErrorStringPtr)(result));
		return EINVAL;
	}

	result = (*nvmlDeviceGetCountPtr)(&count);
	if (result != NVML_SUCCESS){
		ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML: Failed to query device count: %s\n",
		       (*nvmlErrorStringPtr)(result));
		return EINVAL;
	}

	//determine which devices corresponds to the ones asked for. its ok to ask for
	//a device which does not exist.
	for (i = 0; i < count; i++){
		result = (*nvmlDeviceGetHandleByIndexPtr)(i, &device);
		if (result != NVML_SUCCESS){
			ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML: Failed to get handle of device %d: %s\n",
			       i, (*nvmlErrorStringPtr)(result));
			return EINVAL;
		}

		result = (*nvmlDeviceGetNamePtr)(device, name,
						 NVML_DEVICE_NAME_BUFFER_SIZE);
		if (result != NVML_SUCCESS){
			ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML: Failed to get name of device %d: %s\n",
			       i, (*nvmlErrorStringPtr)(result));
			return EINVAL;
		}

		//is this a device we are interested in?
		for (j = 0; j < nvidia_device_count; j++){
			//NOTE: temporarily we cannot pass in args with spaces. replace underscore with space before using
			char *tmpname = strdup(nvidia_device_names[j]);
			replace_underscore(tmpname);
			if (strcmp(name, tmpname) == 0){
				ovis_log(__cray_sampler_base, OVIS_LDEBUG, "Found matching device for <%s>\n",
				       tmpname);
				nvidia_device[j] = device;  //Note: copy works
				//NOTE: will we need this?
				result = (*nvmlDeviceGetPciInfoPtr)(nvidia_device[j], &nvidia_pci[j]);
				if (result != NVML_SUCCESS){
					ovis_log(__cray_sampler_base, OVIS_LERROR, "NVML: Failed to get pci info for device %s: %s\n",
					       nvidia_device_names[j], (*nvmlErrorStringPtr)(result));
					if (tmpname){
						free(tmpname);
					}
					tmpname = NULL;
					return EINVAL;
				}
				break;
			}
			if (tmpname){
				free(tmpname);
			}
			tmpname = NULL;
		}
	}

	nvidia_valid = 1;
	return 0;
}
