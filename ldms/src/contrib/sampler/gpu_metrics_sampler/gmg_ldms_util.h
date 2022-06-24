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


#ifndef _GMG_LDMS_UTIL_H_
#define _GMG_LDMS_UTIL_H_

#include "gmg_log.h"
#include "gather_gpu_metrics_from_one_api.h"    // brings in OneAPI headers


typedef void (*funcPtr_t)(void);

typedef const char *(*stringGetMetricFuncPtr_t)(ze_device_handle_t);
typedef const uint8_t *(*const8PtrGetMetricFuncPtr_t)(ze_device_handle_t);
typedef double (*doubleGetMetricFuncPtr_t)(ze_device_handle_t);
typedef uint64_t (*u64GetMetricFuncPtr_t)(ze_device_handle_t);
typedef int32_t (*s32GetMetricFuncPtr_t)(ze_device_handle_t);

typedef struct metric {
    const char *name;
    enum ldms_value_type type;
    funcPtr_t pf;
    uint32_t count;
} metric_t;


extern const metric_t metricsDefinitions[];
extern const size_t c_numMetrics;


/**
 * These functions are only used by the LDMS plugin to upload
 * the GPU metrics to LDMS.
 */

/**
 * Populates the GPU metric schema.  This is analogous to an SQL CREATE TABLE.
 * @param schema This is a schema pointer.
 * @param numDevices Number of GPU devices.
 * @return >= The index of the last metric index added.
 *         <0 Insufficient resources or duplicate name.
 */
int populateMetricSchema(
        ldms_schema_t schema,
        uint32_t numDevices
);

/**
 * Populates the GPU metrics set.  This is analogous to a row in an SQL table.
 * @param phDevices device handle.
 * @param numDevices Number of GPU devices.
 * @param s Metrics set.
 * @param firstGpuMetricId Index to first GPU metric.
 */
void populateMetricSet(
        ze_device_handle_t *phDevices,
        uint32_t numDevices,
        ldms_set_t s,
        int firstGpuMetricId
);

#endif // _GMG_LDMS_UTIL_H_
