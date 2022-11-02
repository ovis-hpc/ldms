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


#define _GNU_SOURCE

#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include "ovis_log/ovis_log.h"
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

#include "gmg_ldms_util.h"
#include "gather_gpu_metrics_from_one_api.h"

static ovis_log_t __gpu_metrics_log;

static uint32_t g_numberOfDevicesInSchema = 0;

static ldms_schema_t schema = NULL;
static ldms_set_t set = NULL;
static int metric_offset = 0;
static base_data_t base = NULL;

#define LBUFSZ 256

void free_set() {
    if (set) {
        ldms_set_delete(set);
        set = NULL;
    }
}

void free_schema() {
    if (schema) {
        ldms_schema_delete(schema);
        schema = NULL;
    }
}

void free_base() {
    if (base) {
        base_del(base);
        base = NULL;
    }
}

ze_driver_handle_t getGpuDriver() {
    ze_result_t res = initializeOneApi();   // only slow the first time it is called for each process
    if (res != ZE_RESULT_SUCCESS) {
        ovis_log(__gpu_metrics_log, OVIS_LERROR, "!!!initializeOneApi() => 0x%x\n", res);
        return NULL;
    }

    ze_driver_handle_t hDriver = getDriver();
    if (hDriver == NULL) {
        ovis_log(__gpu_metrics_log, OVIS_LERROR, "!!!getDriver() => NULL\n");
        return NULL;
    }

    return hDriver;
}

/**
 * This function customizes the base plugin instance with the metric set schema of this plugin.  Note that we
 * are not popluting any data structure with any actual data in this function.
 * @param base instance of base plugin class.
 * @return error code.
 */
static int create_metric_set_schema_and_set(base_data_t base) {
    int rc = 0; // return code

    const uint32_t c_minExpectedDevices = 6;

    ze_driver_handle_t hDriver = getGpuDriver();
    if (hDriver == NULL) {
        ovis_log(__gpu_metrics_log, OVIS_LERROR, "!!!getGpuDriver() => NULL\n");
        goto err;
    }

    uint32_t numDevices = 0;
    ze_device_handle_t *phDevices = enumerateGpuDevices(hDriver, &numDevices);
    if (phDevices == NULL) {
        ovis_log(__gpu_metrics_log, OVIS_LERROR, "!!!enumerateGpuDevices(&numDevices=%p) => NULL, %d\n", &numDevices, numDevices);
        goto err;
    }
    freeZeDeviceHandle(phDevices);
    phDevices = NULL;

    g_numberOfDevicesInSchema = MAX(numDevices,
                                    c_minExpectedDevices);     // currently, the HW has space for up to 6 GPUs

    schema = base_schema_new(base);
    if (!schema) {
        ovis_log(__gpu_metrics_log, OVIS_LERROR,
               "!!!%s: The schema '%s' could not be created, errno=%d.\n",
               __FILE__, base->schema_name, errno);
        rc = errno;
        goto err;
    }

    // Location of first metric to be inserting.  On my test system, a new schema already contains
    // 3 metrics.  Metric offset is simply the upper bound of where our gpu metrics data reside.
    // This offset is used by populateMetricSet() since the metrics set needs to be populated with
    // the same order as the entries in the metric schema.
    metric_offset = ldms_schema_metric_count_get(schema);

    rc = populateMetricSchema(schema, g_numberOfDevicesInSchema);
    if (rc < 0) {
        errno = ENOMEM;
        goto err;
    }

    set = base_set_new(base);
    if (!set) {
        rc = errno;
        goto err;
    }

    return 0;

    err:
    free_set();
    free_schema();
    return rc;
}

static void printValList(const char *szListName, struct attr_value_list *av_list) {
    size_t listSize = MIN(av_list->count, av_list->size);
    for (size_t i = 0; i < listSize; i++) {
        ovis_log(__gpu_metrics_log, OVIS_LDEBUG, "%s[%d] = %s:%s\n",
               szListName, i, av_name(av_list, i), av_value_at_idx(av_list, i));
    }
}

/**
 * Check for invalid flags, with particular emphasis on warning the user about.
 */
static int config_check(struct attr_value_list *keyword_list, struct attr_value_list *attribute_value_list, void *arg) {
    char *value;
    int i;

    char *deprecated[] = {"set"};

    for (i = 0; i < (sizeof(deprecated) / sizeof(deprecated[0])); i++) {
        value = av_value(attribute_value_list, deprecated[i]);
        if (value) {
            ovis_log(__gpu_metrics_log, OVIS_LERROR, SAMP ": !!!config argument %s has been deprecated.\n",
                   deprecated[i]);
            return EINVAL;
        }
    }

    return 0;
}

/**
 * Provides usage information.  Note that BASE_CONFIG_USAGE is defined in sampler_base.h.
 * @param self this plugin instance.
 * @return usage string.
 */
static const char *usage(struct ldmsd_plugin *self) {
    return "config name=" SAMP " "
    BASE_CONFIG_USAGE;
}

/**
 * Plugin instance constructor.  Base is an instance of the sampler base "class".
 */
static int config(struct ldmsd_plugin *self,
                  struct attr_value_list *keyword_list,
                  struct attr_value_list *attribute_value_list) {
#ifdef ENABLE_AUTO_SIMULATION
    // Note that HPCM will restore any manual changes to sampler.conf, so it is infeasible to change to
    // SIMULATION mode using manual conf changes.  We can do this using a file existence check.
    autoSetSimulationMode();
#endif

    if (getSimulationMode() == true) {
        // Log this ERROR so that it appears in /opt/clmgr/log/ldms_sampler.log
        ovis_log(__gpu_metrics_log, OVIS_LERROR, "Simulation mode is ON\n");    // no really an error so don't prefix with '!!!'
    }

    printValList("keyword_list", keyword_list);
    printValList("attribute_value_list", attribute_value_list);

    int rc;

    if (set) {
        ovis_log(__gpu_metrics_log, OVIS_LERROR, SAMP ": !!!Set already created.\n");
        return EINVAL;
    }

    rc = config_check(keyword_list, attribute_value_list, NULL);
    if (rc != 0) {
        return rc;
    }

    // Create an instance from the base "class".  This is effectively calling
    // the base class constructor.
    base = base_config(attribute_value_list, SAMP, SAMP, __gpu_metrics_log);
    if (!base) {
        rc = errno;
        goto err;
    }

    // Create the metric set schema in the base instance.  This plugin instance
    // is considered well-defined after the metric set schema is defined.
    rc = create_metric_set_schema_and_set(base);
    if (rc) {
        ovis_log(__gpu_metrics_log, OVIS_LERROR, SAMP ": !!!failed to create a metric set.\n");
        goto err;
    }

    return 0;
    err:
    free_set();
    free_base();
    free_schema();
    return rc;
}

/**
 * LDMS call this function to obtain the current set of metrics.
 * @param self this plugin instance.
 * @return current set of metrics.
 */
static ldms_set_t get_set(struct ldmsd_sampler *self) {
    return set;
}

/**
 * LDMS calls this function to sample GPU metrics and store them in the metrics set.
 * @param self
 * @return 0 if successful; otherwise returns EINVAL.
 */
static int sample(struct ldmsd_sampler *self) {
    if (!set) {
        ovis_log(__gpu_metrics_log, OVIS_LDEBUG, SAMP ": plugin not initialized\n");
        return EINVAL;
    }

    ze_driver_handle_t hDriver = getGpuDriver();
    if (hDriver == NULL) {
        ovis_log(__gpu_metrics_log, OVIS_LERROR, "!!!getGpuDriver() => NULL\n");
        return EINVAL;
    }

    uint32_t numDevices = 0;
    ze_device_handle_t *phDevices = enumerateGpuDevices(hDriver, &numDevices);
    if (phDevices == NULL) {
        ovis_log(__gpu_metrics_log, OVIS_LERROR, "!!!enumerateGpuDevices(&numDevices=%p) => NULL, %d\n", &numDevices, numDevices);
        return EINVAL;
    }
    uint32_t numDevicesToSample = MIN(g_numberOfDevicesInSchema, numDevices);   // cannot sample more than schema size

    base_sample_begin(base);
    populateMetricSet(phDevices, numDevicesToSample, set, metric_offset);
    base_sample_end(base);
    size_t mallocCount = getMallocCount();
    if (mallocCount != 1) {
        // Only allocated memory is the device handler array.
        ovis_log(__gpu_metrics_log, OVIS_LERROR, SAMP ": !!!mallocCount=%ld != 1\n", mallocCount);
    }

    freeZeDeviceHandle(phDevices);
    phDevices = NULL;
    return 0;
}

/**
 * Release any opened resource.  Note that we have to call OneAPI C++ destructor to
 * close any opened handles.
 * @param self this plugin instance.
 */
static void term(struct ldmsd_plugin *self) {
    // No longer need to free device handle array here.

    size_t mallocCount = getMallocCount();
    if (mallocCount) {
        // This following log message is never printed;  maybe term was never called.
        ovis_log(__gpu_metrics_log, OVIS_LERROR, SAMP ": !!!mallocCount=%ld != 0\n", mallocCount);
    }

    free_base();
    free_set();
    free_schema();
    if (__gpu_metrics_log)
	    ovis_log_destroy(__gpu_metrics_log);
}

/**
 * This structure defines the plugin instance.  .base probably refers to the
 * base "class".  So, the assignments in base are overrides.
 */
static struct ldmsd_sampler gpu_metrics_plugin = {
        .base = {
                .name = SAMP,
                .type = LDMSD_PLUGIN_SAMPLER,
                .term = term,       // destructor
                .config = config,   // constructor
                .usage = usage,
        },
        .get_set = get_set,
        .sample = sample,
};

/**
 * LDMS calls this to obtain a plugin instance.  Plugin is initialized here.
 * @param pf logging function provided by LDMS.
 * @return plugin instance.
 */
struct ldmsd_plugin *get_plugin() {
    __gpu_metrics_log = ovis_log_register("sampler."SAMP, "Messages for the " SAMP " plugin");
    if (!__gpu_metrics_log) {
	    ovis_log(NULL, OVIS_LWARN, "Failed to create the " SAMP " plugin's "
			    "log subsystem. Error %d.\n", errno);
    }
    setGmgLoggingFunction(__gpu_metrics_log);
    set = NULL;
    return &gpu_metrics_plugin.base;
}
