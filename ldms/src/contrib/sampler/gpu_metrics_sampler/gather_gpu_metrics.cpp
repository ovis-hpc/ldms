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


#include "gather_gpu_metrics_from_one_api.h"
#include <level_zero/zes_api.h>

#include <iostream>
#include <iomanip>
#include <chrono>

using namespace std::chrono;
using namespace std;


void printGpuMetrics(ze_device_handle_t device, uint32_t devNumber) {
    cout << " " << devNumber << "    -1 Format: Value          Descriptive text" << endl;

    uint32_t metricNumber = 0;
    cout << fixed << setprecision(2);
    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getGpuUtilization(device) << "           gpu_util" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getMemoryUtilization(device) << "           mem_util" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getMemVRAMUsed(device) << "       mem_vram_used" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getSysClockFreq(device) << "           sys_clock_freq" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getMemoryReadBandwidth(device) << "          mem_read_bandwidth" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getMemoryWriteBandwidth(device) << "          mem_write_bandwidth" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getPerfLevel(device) << "           perf_level" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getPowerUsage(device) << "             power_usage" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getGpuTemp(device) << "          gpu_temp" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasFatalAcceleratorResetsError(device) << "       ue_accelerator_eng_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasFatalCachesError(device) << "       ue_cache_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasFatalPrgmError(device) << "       ue_programming_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasFatalDriverError(device) << "       ue_driver_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasFatalComputeError(device) << "       ue_compute_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasFatalNonComputeError(device) << "       ue_non_compute_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasFatalDisplayError(device) << "       ue_display_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasCorrectableAcceleratorResetsError(device) << "       ce_accelerator_eng_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasCorrectableCachesError(device) << "       ce_cache_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasCorrectablePrgmError(device) << "       ce_programming_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasCorrectableDriverError(device) << "       ce_driver_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasCorrectableComputeError(device) << "       ce_compute_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasCorrectableNonComputeError(device) << "       ce_non_compute_err" << endl;

    cout << " " << devNumber << "     " << metricNumber++ << "         "
         << getRasCorrectableDisplayError(device) << "       ce_display_err" << endl;
}

int sampleGpuMetrics(int argc, char *argv[]) {
    auto start = high_resolution_clock::now();

    ze_result_t res = initializeOneApi();
    if (res != ZE_RESULT_SUCCESS) {
        cerr << "!!!initializeOneApi => 0x" << hex << res << endl;
        return 1;
    }

    cout << "-1    -1         GPU Driver discovery ..." << endl;
    ze_driver_handle_t hDriver = getDriver();
    if (hDriver == NULL) {
        cerr << "!!!getDriver() => NULL" << endl;
        return 2;
    }

    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(stop - start);
    cout << "-1    -1         GPU driver discovered in " << duration.count() / 1000 <<
         " ms" << endl;

    cout << "-1    -1         Enumerating devices managed by driver[0] ..." << endl;
    uint32_t numDevices = 0;
    ze_device_handle_t *phDevices = enumerateGpuDevices(hDriver, &numDevices);
    if (phDevices == NULL) {
        cerr << "!!!enumerateGpuDevices() => NULL" << endl;
        return 3;
    }
    cout << "-1    -1         " << numDevices << " GPU device(s) discovered" << endl;

    uint32_t devNumber = 0;
    for (uint32_t i = 0; i < numDevices; i++) {
        cout << " " << devNumber << "    -1         Device name = " << getGpuDeviceName(phDevices[devNumber]) << endl;
        cout << " " << devNumber << "    -1         Device uuid = " << convertUuidToString(getGpuUuid(phDevices[devNumber])) << endl;
        cout << " " << devNumber << "    -1         Serial number = " << getGpuSerialNumber(phDevices[devNumber])<< endl;
        printGpuMetrics(phDevices[i], devNumber++);
    }

    freeZeDeviceHandle(phDevices);
    phDevices = NULL;

    return 0;
}

int main(int argc, char *argv[]) {
#ifdef ENABLE_AUTO_SIMULATION
    autoSetSimulationMode();
#endif
    auto start = high_resolution_clock::now();
    int res = sampleGpuMetrics(argc, argv);
    if (res != 0) {
        return res;
    }
    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(stop - start);
    cout << "-1    -1         " << "GPU metrics gatherer took " << duration.count() / 1000 << " ms" << endl;
    return 0;
}
