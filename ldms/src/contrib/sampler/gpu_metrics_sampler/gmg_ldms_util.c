/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 Intel Corporation
 * Copyright (c) 2011-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2011-2018 Open Grid Computing, Inc. All rights reserved.
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


#include "gmg_log.h"
#include "gmg_ldms_util.h"
#include <level_zero/zes_api.h>


const metric_t metricsDefinitions[] = {
        {.name = "device_name", .type = LDMS_V_CHAR_ARRAY, .pf = (funcPtr_t) getGpuDeviceName, .count = ZE_MAX_DEVICE_NAME},
        {.name = "device_uuid", .type = LDMS_V_U8_ARRAY, .pf = (funcPtr_t) getGpuUuid, .count = ZE_MAX_DEVICE_UUID_SIZE},
        {.name = "serial_number", .type = LDMS_V_CHAR_ARRAY, .pf = (funcPtr_t) getGpuSerialNumber, .count = ZES_STRING_PROPERTY_SIZE},
        {.name = "gpu_util (%)", .type = LDMS_V_D64, .pf = (funcPtr_t) getGpuUtilization},
        {.name = "mem_util (%)", .type = LDMS_V_D64, .pf = (funcPtr_t) getMemoryUtilization},
        {.name = "mem_vram_used", .type = LDMS_V_U64, .pf = (funcPtr_t) getMemVRAMUsed},
        {.name = "sys_clock_freq (MHz)", .type = LDMS_V_S32, .pf = (funcPtr_t) getSysClockFreq},
        {.name = "mem_read_bandwidth (kilobaud)", .type = LDMS_V_D64, .pf = (funcPtr_t) getMemoryReadBandwidth},
        {.name = "mem_write_bandwidth (kilobaud)", .type = LDMS_V_D64, .pf = (funcPtr_t) getMemoryWriteBandwidth},
        {.name = "perf_level", .type = LDMS_V_D64, .pf = (funcPtr_t) getPerfLevel},
        {.name = "power_usage (mW)", .type = LDMS_V_S32, .pf = (funcPtr_t) getPowerUsage},
//        {.name = "power_cap (mW)", .type = LDMS_V_S32, .pf = (funcPtr_t) getPowerCap},    // no longer supported
        {.name = "gpu_temp (Celsius)", .type = LDMS_V_D64, .pf = (funcPtr_t) getGpuTemp},
//        {.name = "pci_max_bandwidth (baud)", .type = LDMS_V_U64, .pf = (funcPtr_t) getPciMaxSpeed}    // currently OneAPI does not support this
};

const size_t c_numMetrics = sizeof(metricsDefinitions) / sizeof(metricsDefinitions[0]);

/**
* Functions only used by gmg_test.
*/

void constructMetricName(const char *szBaseMetricName, uint8_t deviceId, char *szMetricName) {
    snprintf(szMetricName, MAX_METRIC_NAME_LENGTH, "gpu%02x.", deviceId);
    strncpy(szMetricName + 6, szBaseMetricName, MAX_METRIC_NAME_LENGTH - 6);
    GMGLOG(LDMSD_LDEBUG, "metricName = %s\n", szMetricName);
}

/**
 * Populates the GPU metric schema.  This is analogous to an SQL CREATE TABLE.
 * @param schema This is a schema pointer.
 * @param numDevices Number of GPU devices.
 * @return >= The index of the last metric index added.
 *         <0 Insufficient resources or duplicate name.
 *
 */
int populateMetricSchema(ldms_schema_t schema, uint32_t numDevices) {
    int rc = 0;

    for (uint32_t deviceId = 0; deviceId < MIN(numDevices, MAX_NUMBER_DEVICE_INDEX); deviceId++) {
        for (size_t i = 0; i < c_numMetrics; i++) {
            char szMetricName[MAX_METRIC_NAME_LENGTH + 1] = {};
            constructMetricName(metricsDefinitions[i].name, deviceId, szMetricName);
            GMGLOG(LDMSD_LDEBUG, "metricsDefinitions[i=%d].name = %s\n", i, metricsDefinitions[i].name);
            GMGLOG(LDMSD_LDEBUG, "szMetricName = %s\n", szMetricName);
            if (ldms_type_is_array(metricsDefinitions[i].type)) {
                rc = ldms_schema_metric_array_add(schema, szMetricName,
                                                  metricsDefinitions[i].type, metricsDefinitions[i].count);
            } else {
                rc = ldms_schema_metric_add(schema, szMetricName, metricsDefinitions[i].type);
            }
            if (rc < 0) {
                GMGLOG(LDMSD_LERROR, "!!!Insufficient resources or duplicate name: rc = %d\n", rc);
                break;
            }
        }
    }

    return rc;
}


void setD64(ldms_set_t s, int metricId, ze_device_handle_t hDevice, doubleGetMetricFuncPtr_t pf) {
    if (pf == NULL) {
        GMGLOG(LDMSD_LERROR, "pf == NULL\n");
        return;
    }

    double val = pf(hDevice);
    GMGLOG(LDMSD_LINFO, "doublePf(hDevice=%p) => %lf\n", hDevice, val);

    ldms_metric_set_double(s, metricId, val);
}

void setU64(ldms_set_t s, int metricId, ze_device_handle_t hDevice, u64GetMetricFuncPtr_t pf) {
    if (pf == NULL) {
        GMGLOG(LDMSD_LERROR, "pf == NULL\n");
        return;
    }

    uint64_t val = pf(hDevice);
    GMGLOG(LDMSD_LINFO, "u64GetMetricFuncPtr_t(hDevice=%p) => %ld\n", hDevice, val);

    ldms_metric_set_u64(s, metricId, val);
}

void setS32(ldms_set_t s, int metricId, ze_device_handle_t hDevice, s32GetMetricFuncPtr_t pf) {
    if (pf == NULL) {
        GMGLOG(LDMSD_LERROR, "pf == NULL\n");
        return;
    }

    int32_t val = pf(hDevice);
    GMGLOG(LDMSD_LINFO, "s32GetMetricFuncPtr_t(hDevice=%p) => %d\n", hDevice, val);

    ldms_metric_set_s32(s, metricId, val);
}

void setString(ldms_set_t s,
               int metricId,
               ze_device_handle_t hDevice,
               stringGetMetricFuncPtr_t pf) {
    const char *str = pf(hDevice);
    ldms_metric_array_set_str(s, metricId, str);    // ldms_metric_array_set_str does not require a length argument
}

void setU8Array(ldms_set_t s, int metricId, ze_device_handle_t hDevice,
                const8PtrGetMetricFuncPtr_t pf, uint32_t count) {
    const uint8_t *u8Array = pf(hDevice);
    for (uint32_t j = 0; j < count; j++) {
        ldms_metric_array_set_u8(s, metricId, j, u8Array[j]);
    }
}

/**
 * Populates the GPU metrics set.  This is analogous to a row in an SQL table.
 * @param phDevices device handle.
 * @param numDevices Number of GPU devices.
 * @param s Metrics set.
 * @param firstGpuMetricId Index to first GPU metric.
 */
void populateMetricSet(ze_device_handle_t *phDevices, uint32_t numDevices, ldms_set_t s, int firstGpuMetricId) {
    int metricId = firstGpuMetricId;

    for (uint32_t deviceId = 0; deviceId < numDevices; deviceId++) {
        for (size_t i = 0; i < c_numMetrics; i++) {
            switch (metricsDefinitions[i].type) {
                case LDMS_V_CHAR_ARRAY:
                    setString(s, metricId++, phDevices[deviceId],
                              (stringGetMetricFuncPtr_t) (metricsDefinitions[i].pf));
                    break;
                case LDMS_V_U8_ARRAY:
                    setU8Array(s, metricId++, phDevices[deviceId],
                               (const8PtrGetMetricFuncPtr_t) (metricsDefinitions[i].pf), metricsDefinitions[i].count);
                    break;
                case LDMS_V_D64:
                    setD64(s, metricId++, phDevices[deviceId], (doubleGetMetricFuncPtr_t) (metricsDefinitions[i].pf));
                    break;
                case LDMS_V_S32:
                    setS32(s, metricId++, phDevices[deviceId], (s32GetMetricFuncPtr_t) (metricsDefinitions[i].pf));
                    break;
                case LDMS_V_U64:
                    setU64(s, metricId++, phDevices[deviceId], (u64GetMetricFuncPtr_t) (metricsDefinitions[i].pf));
                    break;
                default:
                    GMGLOG(LDMSD_LERROR, "!!!Unexpected metric type: %d\n", metricsDefinitions[i].type);
                    break;
            }
        }
    }
}
