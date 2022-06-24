/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 Intel Corporation
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


#ifndef _GATHER_GPU_METRICS_FROM_ONE_API_H
#define _GATHER_GPU_METRICS_FROM_ONE_API_H

#if defined(__cplusplus)
#pragma once
#endif

#include "gmg_common_util.h"
#include <level_zero/zes_api.h>


#define SAMP "gpu_metrics"
#define MAX_METRIC_NAME_LENGTH 256
#define MAX_NUMBER_DEVICE_INDEX 255

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef ENABLE_AUTO_SIMULATION
#define SIMULATION_CONTROL_FILE "/opt/ucs/RUN_GPU_METRICS_GATHERERS_IN_SIMULATION_MODE"
void autoSetSimulationMode();
#endif

size_t getMallocCount();

bool getSimulationMode();

void setSimulationMode(
        bool isInSimulationMode
);


ze_result_t initializeOneApi();

ze_driver_handle_t getDriver();

ze_device_handle_t *enumerateGpuDevices(
        ze_driver_handle_t hDriver,
        uint32_t *pCount
);

void freeZeDeviceHandle(
        ze_device_handle_t *phDevice
);

const char *getGpuDeviceName(
        ze_device_handle_t hDevice
);

const uint8_t *getGpuUuid(
        ze_device_handle_t hDevice
);

/**
 * When invoked in SAMPLING mode, this function returns the actual gpu utilization.  When invoked in SIMULATION mode,
 * this function returns fluctuating simulated gpu utilization.
 * @param hDevice handle to gpu device.
 * @return gpu utilization in percentage.
 */
double getGpuUtilization(
        ze_device_handle_t hDevice
);

double getMemoryUtilization(
        ze_device_handle_t hDevice
);

uint64_t getMemVRAMUsed(
        ze_device_handle_t hDevice
);

int32_t getSysClockFreq(
        ze_device_handle_t hDevice
);

double getMemoryReadBandwidth(
        ze_device_handle_t hDevice
);

double getMemoryWriteBandwidth(
        ze_device_handle_t hDevice
);

double getPerfLevel(
        ze_device_handle_t hDevice
);

int32_t getPowerUsage(
        ze_device_handle_t hDevice
);

int32_t getPowerCap(
        ze_device_handle_t hDevice
);

double getGpuTemp(
        ze_device_handle_t hDevice
);

int64_t getPciMaxSpeed(
        zes_device_handle_t hDevice
);

const char *getGpuSerialNumber(
        zes_device_handle_t hDevice
);

#if defined(__cplusplus)
} // extern "C"
#endif // __cplusplus

#endif // _GATHER_GPU_METRICS_FROM_ONE_API_H
