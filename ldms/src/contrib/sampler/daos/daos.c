/* -*- c-basic-offset: 8 -*-
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * Subject to the terms and conditions of this license, each copyright holder
 * and contributor hereby grants to those receiving rights under this license
 * a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable
 * (except for failure to satisfy the conditions of this license) patent
 * license to make, have made, use, offer to sell, sell, import, and otherwise
 * transfer this software, where such license applies only to those patent
 * claims, already acquired or hereafter acquired, licensable by such copyright
 * holder or contributor that are necessarily infringed by:
 *
 * (a) their Contribution(s) (the licensed copyrights of copyright holders and
 *     non-copyrightable additions of contributors, in source or binary form)
 *     alone; or
 *
 * (b) combination of their Contribution(s) with the work of authorship to
 *     which such Contribution(s) was added by such copyright holder or
 *     contributor, if, at the time the Contribution is added, such addition
 *     causes such combination to be necessarily infringed. The patent license
 *     shall not apply to any other combinations which include the
 *     Contribution.
 *
 * Except as expressly stated above, no rights or licenses from any copyright
 * holder or contributor is granted under this license, whether expressly, by
 * implication, estoppel or otherwise.
 *
 * DISCLAIMER
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "ldms.h"
#include "ldmsd.h"
#include "config.h"
#include "sampler_base.h"

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"
#include "daos_types.h"
#include "daos.h"

#include "rank_target.h"
#include "pool_target.h"

ldmsd_msg_log_f log_fn;
static ldmsd_msg_log_f msglog;
static int engine_count = 2;
static int target_count = 8;
char system_name[DAOS_SYS_NAME_MAX+1];
char producer_name[LDMS_PRODUCER_NAME_MAX];

#define DEFAULT_SYS_NAME "daos_server"

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char	*ival;

	log_fn(LDMSD_LDEBUG, SAMP": config() called\n");
	ival = av_value(avl, "producer");
	if (ival) {
		if (strlen(ival) < sizeof(producer_name)) {
			strncpy(producer_name, ival, sizeof(producer_name));
		} else {
			log_fn(LDMSD_LERROR, SAMP": config: producer name too long.\n");
			return -EINVAL;
		}
	}
	log_fn(LDMSD_LDEBUG, SAMP": producer: %s\n", producer_name);

	ival = av_value(avl, "system");
	if (ival) {
		if (strlen(ival) < sizeof(system_name)) {
			strncpy(system_name, ival, sizeof(system_name));
		} else {
			log_fn(LDMSD_LERROR, SAMP": config: system name too long.\n");
			return -EINVAL;
		}
	}
	log_fn(LDMSD_LDEBUG, SAMP": system: %s\n", system_name);

	ival = av_value(avl, "engine_count");
	if (ival) {
		int cfg_engine_count = atoi(ival);

		if (cfg_engine_count > 0)
			engine_count = cfg_engine_count;
	}
	log_fn(LDMSD_LDEBUG, SAMP": engine_count: %d\n", engine_count);

	ival = av_value(avl, "target_count");
	if (ival) {
		int cfg_tgt_count = atoi(ival);

		if (cfg_tgt_count > 0)
			target_count = cfg_tgt_count;
	}
	log_fn(LDMSD_LDEBUG, SAMP": target_count: %d\n", target_count);

out:
	return 0;
}

int get_daos_rank(struct d_tm_context *ctx, uint32_t *rank)
{
	uint64_t		 val;
	struct d_tm_node_t	*node;
	int			 rc;

	node = d_tm_find_metric(ctx, "/rank");
	if (node == NULL)
		return -EINVAL;

	rc = d_tm_get_gauge(ctx, &val, NULL, node);
	if (rc < 0) {
		log_fn(LDMSD_LERROR, SAMP": get_daos_rank: d_tm_get_gauge failed: %d\n", rc);
		return rc;
	}
	*rank = val;

	return 0;
}

static int sample(struct ldmsd_sampler *self)
{
	struct d_tm_context	*ctx = NULL;
	uint32_t		 rank = -1;
	int			 i;
	int			 rc = 0;

	log_fn(LDMSD_LDEBUG, SAMP": sample() called\n");
	if (rank_target_schema_is_initialized() < 0) {
		if (rank_target_schema_init() < 0) {
			log_fn(LDMSD_LERROR, SAMP": rank_target_schema_init failed.\n");
			return -ENOMEM;
		}
	}
	if (pool_target_schema_is_initialized() < 0) {
		if (pool_target_schema_init() < 0) {
			log_fn(LDMSD_LERROR, SAMP": pool_target_schema_init failed.\n");
			return -ENOMEM;
		}
	}

	rank_targets_refresh(system_name, engine_count, target_count);
	pool_targets_refresh(system_name, engine_count, target_count);

	for (i = 0; i < engine_count; i++) {
		ctx = d_tm_open(i);
		if (!ctx) {
			log_fn(LDMSD_LDEBUG, SAMP": Failed to open tm shm %d\n", i);
			continue;
		}

		rc = get_daos_rank(ctx, &rank);
		if (rc != 0) {
			log_fn(LDMSD_LERROR, SAMP": Failed to get rank from tm shm %d: %d\n", i, rc);
			continue;
		}

		rank_targets_sample(ctx, rank);
		pool_targets_sample(ctx, rank);

		d_tm_close(&ctx);
	}

	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	log_fn(LDMSD_LDEBUG, SAMP" term() called\n");
	rank_targets_destroy();
	rank_target_schema_fini();
	pool_targets_destroy();
	pools_destroy();
	pool_target_schema_fini();
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static const char *usage(struct ldmsd_plugin *self)
{
	log_fn(LDMSD_LDEBUG, SAMP" usage() called\n");
	return  "config name=" SAMP " " BASE_CONFIG_USAGE;
}

static struct ldmsd_sampler daos_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	log_fn = pf;
	log_fn(LDMSD_LDEBUG, SAMP": get_plugin() called ("PACKAGE_STRING")\n");
	gethostname(producer_name, sizeof(producer_name));
	strncpy(system_name, DEFAULT_SYS_NAME, sizeof(system_name));

	return &daos_plugin.base;
}
