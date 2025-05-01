/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016-2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file cray_power_sampler.c
 */
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "timer_base.h"
#include "sampler_base.h"

static ovis_log_t mylog;

typedef enum {
	CPS_ENERGY,
	/* CPS_FRESHNESS, */
	/* CPS_GENERATION, */
	CPS_POWER,
	/* CPS_POWER_CAP, */
	/* CPS_STARTUP, */
	/* CPS_VERSION, */
	CPS_LAST,
} cps_metric_type;

static const char *cps_names[] = {
	[CPS_ENERGY]      =  "energy",
	/* [CPS_FRESHNESS]   =  "freshness", */
	/* [CPS_GENERATION]  =  "generation", */
	[CPS_POWER]       =  "power",
	/* [CPS_POWER_CAP]   =  "power_cap", */
	/* [CPS_STARTUP]     =  "startup", */
	/* [CPS_VERSION]     =  "version", */
};

static const char *cps_files[] = {
	[CPS_ENERGY]      =  "/sys/cray/pm_counters/energy",
	/* [CPS_FRESHNESS]   =  "/sys/cray/pm_counters/freshness", */
	/* [CPS_GENERATION]  =  "/sys/cray/pm_counters/generation", */
	[CPS_POWER]       =  "/sys/cray/pm_counters/power",
	/* [CPS_POWER_CAP]   =  "/sys/cray/pm_counters/power_cap", */
	/* [CPS_STARTUP]     =  "/sys/cray/pm_counters/startup", */
	/* [CPS_VERSION]     =  "/sys/cray/pm_counters/version", */
};

struct cray_power_sampler {
	struct timer_base base;
	int fd[CPS_LAST];
	struct timeval hfinterval;
	int hfcount;
};

static const char *cray_power_sampler_usage(ldmsd_plug_handle_t handle)
{
	return  "config name=cray_power_sampler producer=<prod_name>"
		" instance=<inst_name> [hfinterval=<hfinterval>] "
		" [hfcount=<hfcount>] [component_id=<compid>] [schema=<sname>] "
		" [with_jobid=(0|1)]\n"
		"    <prod_name>  The producer name.\n"
		"    <inst_name>  The instance name.\n"
		"    <hfinterval> (Optional) the high-frequency interval (micro second, default: 100000).\n"
		"    <hfcount>    (Optional) the number of elements in the ring buffer (default: 600).\n"
		"    <compid>     (Optional) unique number identifier. Defaults to zero.\n"
		"    <sname>      (Optional) schema name. Defaults to 'sampler_timer'.\n"
		"    with_jobid   (Optional) enable(1) or disable(0) job info lookup (default: 1).\n"
		;
}

static
void cray_power_sampler_cleanup(struct cray_power_sampler *cps)
{
	int fd, i;
	/* terminate timers */
	timer_base_cleanup(&cps->base);
	for (i = 0; i < CPS_LAST; i++) {
		fd = cps->fd[i];
		if (fd >= 0) {
			close(fd);
		}
	}
}

static
void cray_power_sampler_timer_cb(tsampler_timer_t t)
{
	int rc;
	int cps_idx;
	off_t off;
	struct cray_power_sampler *cps;
	uint64_t v;
	char buff[64];

	/* t->sampler, t->set and t->mid (metric-id) are set when the time is
	 * setup in create_metric_set().
	 *
	 * t->idx is the current index in the array provided by timer. This will
	 * keep modulo-increasing call-after-call (per timer).
	 * */

	cps = t->cb_context;

	cps_idx = (uint64_t)t->ctxt; /* abuse ctxt */

	off = lseek(cps->fd[cps_idx], 0, SEEK_SET);
	if (off < 0) {
		/* error */
		ovis_log(mylog, OVIS_LDEBUG, "lseek() error, "
				"errno: %d, cps_idx: %d\n.", errno, cps_idx);
		return;
	}

	rc = read(cps->fd[cps_idx], buff, sizeof(buff));
	if (rc < 0) {
		/* error */
		ovis_log(mylog, OVIS_LDEBUG, "read() error, "
				"errno: %d, cps_idx: %d\n.", errno, cps_idx); return;
	}

	v = strtoul(buff, NULL, 0);
	ldms_metric_array_set_u64(t->set, t->mid, t->idx, v);
}
static
int cray_power_sampler_config(ldmsd_plug_handle_t handle,
				struct attr_value_list *kwl,
				struct attr_value_list *avl)
{
	struct cray_power_sampler *cps = ldmsd_plug_ctxt_get(handle);
	char *v;
	uint64_t x;
	int rc;
	int i;

	rc = timer_base_config(handle, kwl, avl, mylog);
	if (rc) {
		goto out;
	}

	/* set default values */
	cps->hfinterval.tv_sec = 0;
	cps->hfinterval.tv_usec = 100000;
	cps->hfcount = 600;

	v = av_value(avl, "hfinterval");
	if (v) {
		x = strtoull(v, NULL, 0);
		if (!x) {
			rc = EINVAL;
			goto cleanup;
		}
		cps->hfinterval.tv_sec = x / 1000000;
		cps->hfinterval.tv_usec = x % 1000000;
	}

	v = av_value(avl, "hfcount");
	if (v) {
		cps->hfcount = strtol(v, NULL, 0);
		if (!cps->hfcount) {
			rc = EINVAL;
			goto cleanup;
		}
	}

	for (i = 0; i < CPS_LAST; i++) {
		cps->fd[i] = open(cps_files[i], O_RDONLY);
		if (cps->fd[i] < 0)
			goto cleanup;
		rc = timer_base_add_hfmetric(&cps->base, cps_names[i],
                                             LDMS_V_U64_ARRAY, cps->hfcount,
                                             &cps->hfinterval,
                                             cray_power_sampler_timer_cb, cps,
                                             (void*)(uint64_t)i);
		if (rc)
			goto cleanup;
	}

	rc = timer_base_create_set(&cps->base, ldmsd_plug_cfg_name_get(handle));
	if (rc) {
		goto cleanup;
	}
	goto out;

cleanup:
	timer_base_cleanup(&cps->base);
out:
	return rc;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	struct cray_power_sampler *cps;
	int rc;

	mylog = ovis_log_register("sampler.cray_power_sampler", "The log subsystem of the cray_power_sampler plugin");
	if (!mylog) {
		rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the subsystem "
				"of 'cray_power_sampler' plugin. Error %d\n", rc);
                return errno;
	}

        cps = calloc(1, sizeof(*cps));
	if (!cps)
		return ENOMEM;

	timer_base_init(&cps->base);

        ldmsd_plug_ctxt_set(handle, cps);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
        struct cray_power_sampler *cps = ldmsd_plug_ctxt_get(handle);

	cray_power_sampler_cleanup(cps);
        free(cps);
}

struct ldmsd_sampler ldmsd_plugin_interface  = {
        .base.type = LDMSD_PLUGIN_SAMPLER,
        .base.term = timer_base_term,
	.base.usage = cray_power_sampler_usage,
        .base.constructor = constructor,
        .base.destructor = destructor,
	.base.config = cray_power_sampler_config,
        .sample = timer_base_sample,
};
