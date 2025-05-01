/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016,2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file hfclock.c
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

static ovis_log_t mylog;

struct hfclock {
	struct timer_base base;
	struct timeval hfinterval;
	int hfcount;
};

static const char *hfclock_usage(ldmsd_plug_handle_t handle)
{
	return  "config name=hfclock producer=<prod_name>"
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
void hfclock_cleanup(struct hfclock *hf)
{
	timer_base_cleanup(&hf->base);
}

static
void hfclock_timer_cb(tsampler_timer_t t)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ldms_metric_array_set_double(t->set, t->mid, t->idx, tv.tv_sec + tv.tv_usec/1e6);
}

static
int hfclock_config(ldmsd_plug_handle_t handle,
				struct attr_value_list *kwl,
				struct attr_value_list *avl)
{
	struct hfclock *hf = (struct hfclock *)ldmsd_plug_ctxt_get(handle);
	int rc;
	char *v;
	uint64_t x;

	rc = timer_base_config(hf, kwl, avl, mylog);
	if (rc)
		goto out;

	/* set default values */
	hf->hfinterval.tv_sec = 0;
	hf->hfinterval.tv_usec = 100000;
	hf->hfcount = 600;

	v = av_value(avl, "hfinterval");
	if (v) {
		x = strtoull(v, NULL, 0);
		if (!x) {
			rc = EINVAL;
			goto cleanup;
		}
		hf->hfinterval.tv_sec = x / 1000000;
		hf->hfinterval.tv_usec = x % 1000000;
	}

	v = av_value(avl, "hfcount");
	if (v) {
		hf->hfcount = strtol(v, NULL, 0);
		if (!hf->hfcount) {
			rc = EINVAL;
			goto cleanup;
		}
	}

	rc = timer_base_add_hfmetric(&hf->base, "clock",
                                     LDMS_V_D64_ARRAY, hf->hfcount,
                                     &hf->hfinterval,
                                     hfclock_timer_cb, NULL,
                                     NULL);
	if (rc)
		goto cleanup;

	rc = timer_base_create_set(&hf->base, ldmsd_plug_cfg_name_get(handle));
	if (rc)
		goto cleanup;

	goto out;

cleanup:
	hfclock_cleanup(hf);

out:
	return rc;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	struct hfclock *hf;
	int rc;

	mylog = ovis_log_register("sampler.hfclock", "The log subsystem of the hfclock plugin");
	if (!mylog) {
		rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the subsystem "
				"of 'hfclock' plugin. Error %d\n", rc);
	}
	hf = calloc(1, sizeof(*hf));
        if (!hf)
                return ENOMEM;

        timer_base_init(&hf->base);

        ldmsd_plug_ctxt_set(handle, hf);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	struct hfclock *hf = ldmsd_plug_ctxt_get(handle);

	hfclock_cleanup(hf);
        free(hf);
}

struct ldmsd_sampler ldmsd_plugin_interface = {
        .base.type = LDMSD_PLUGIN_SAMPLER,
	.base.term = NULL,
	.base.config = hfclock_config,
	.base.usage = hfclock_usage,
        .base.constructor = constructor,
        .base.destructor = destructor,
	.sample = timer_base_sample,
};
