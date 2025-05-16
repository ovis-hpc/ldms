/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020-2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2021, Advanced Micro Devices, Inc. All rights reserved.
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
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include "rdcinfo.h"
#include <pthread.h>


/** rdc sampler
 * This sampler produces sets per-gpu of gpu metrics or a single wide set.
 * The schemas are named to be automatically unique from a base name
 * the user provides with the schema= option.
 */

/* global variables, since this sampler makes no sense in multi-instance mode. */
static rdcinfo_inst_t singleton;
static char *usage_str = NULL;

/* ============== Common Plugin APIs ================= */

static int config(ldmsd_plug_handle_t handle,
		  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	(void)kwl;
	rdcinfo_inst_t inst = singleton;
	if (!inst)
		return ENOMEM;
	int rc = 0;
	pthread_mutex_lock(&inst->lock);

	if (inst->base) {
		/* last config call seen wins */
		rdcinfo_reset(inst);
		INST_LOG(inst, OVIS_LDEBUG, SAMP ": reconfiguring.\n");
	}

	rc = rdcinfo_config(inst, avl);
	if (rc)
		goto err;

	pthread_mutex_unlock(&inst->lock);
	return 0;
 err:
	rdcinfo_reset(inst);
	pthread_mutex_unlock(&inst->lock);
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
	rdcinfo_inst_t inst = singleton;
	if (!inst)
		return EINVAL;
	pthread_mutex_lock(&inst->lock);
	int rc = rdcinfo_sample(inst);
	if (rc) {
		/* must stop the noisy loop inside the rdc library */
		rdcinfo_reset(inst);
		INST_LOG(inst, OVIS_LWARNING, SAMP ": deconfigured.\n");
	}
	pthread_mutex_unlock(&inst->lock);
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle) {
	(void)self;
	if (!usage_str)
		usage_str = rdcinfo_usage();
	return usage_str;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	singleton = rdcinfo_new(ldmsd_plug_log_get(handle));
	if (!singleton) {
		ovis_log(ldmsd_plug_log_get(handle),
                         OVIS_LERROR, SAMP ": unable to allocate singleton\n");
		errno = ENOMEM;
		return -1;
	}

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	if (usage_str) {
		free(usage_str);
		usage_str = NULL;
	}
	rdcinfo_inst_t inst = singleton;
	if (!inst)
		return;
	pthread_mutex_lock(&inst->lock);
	INST_LOG(inst, OVIS_LDEBUG, SAMP " term() called\n");
	singleton = NULL;
	rdcinfo_reset(inst);
	pthread_mutex_unlock(&inst->lock);
	rdcinfo_delete(inst);
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
