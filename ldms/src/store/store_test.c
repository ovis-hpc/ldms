/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2026 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2026 Open Grid Computing, Inc. All rights reserved.
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


/*
 * store_test.c
 *
 * A storage plugin that emulates storage latency without persisting any
 * data. Each store()/commit() call sleeps for a configured duration and
 * returns; no data is written to memory, a file, or any backend.
 *
 * Intended for performance/throughput testing of the ldmsd storage
 * pipeline (worker pool, queueing, backpressure) where a deterministic,
 * controllable store time is required and real backend I/O variance would
 * confound results. For correctness/data-persistence testing, use a real
 * store plugin (e.g. store_csv, store_sos) instead.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ovis_util/util.h"
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"

#define LOG_(ts, level, ...) do { \
	ovis_log(ts->log, level, ## __VA_ARGS__); \
} while (0);

typedef struct store_test_s {
	pthread_mutex_t cfg_lock;
	ovis_log_t log;
	long latency_us; /* store()/commit() delay, in microseconds. Default 0. */
} *store_test_t;

/*
 * Opaque handle returned by open_store(). store_test does not need any
 * per-container/schema state, but a non-NULL handle is required so that
 * ldmsd_store_open() (and therefore strgp_start) succeeds normally.
 */
struct store_test_handle {
	char *container;
	char *schema;
};

static void __sleep_latency(store_test_t ts)
{
	struct timespec req, rem;

	if (ts->latency_us <= 0)
		return;

	req.tv_sec = ts->latency_us / 1000000;
	req.tv_nsec = (ts->latency_us % 1000000) * 1000;
	while (nanosleep(&req, &rem) == -1 && errno == EINTR)
		req = rem;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "    config name=<INST> plugin=store_test latency=<TIME>\n"
		"        latency - Required. Time that store() and commit()\n"
		"                  sleep before returning. No data is\n"
		"                  persisted. Accepts a number with an\n"
		"                  optional unit suffix: us (default unit if\n"
		"                  omitted), ms, s, m, h, d (e.g. 500us,\n"
		"                  10ms, 1s, 2m). A value of 0 or a negative number\n"
		"                  means no delay.\n";
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl,
		   struct attr_value_list *avl)
{
	store_test_t ts = ldmsd_plug_ctxt_get(handle);
	char *value;
	long latency_us;
	int rc;

	value = av_value(avl, "latency");
	if (!value) {
		LOG_(ts, OVIS_LERROR,
		     "store_test: the 'latency' attribute is required.\n");
		return EINVAL;
	}

	rc = ovis_time_str2us(value, &latency_us);
	if (rc || latency_us < 0) {
		LOG_(ts, OVIS_LERROR,
		     "store_test: invalid latency value '%s'; "
		     "expected a non-negative number with an "
		     "optional unit (us, ms, s, m, h, d).\n", value);
		return EINVAL;
	}

	pthread_mutex_lock(&ts->cfg_lock);
	ts->latency_us = latency_us;
	pthread_mutex_unlock(&ts->cfg_lock);

	LOG_(ts, OVIS_LINFO, "store_test: latency set to %ld us.\n", latency_us);

	return 0;
}

static ldmsd_store_handle_t
open_store(ldmsd_plug_handle_t handle, const char *container, const char *schema,
	   struct ldmsd_strgp_metric_list *metric_list)
{
	store_test_t ts = ldmsd_plug_ctxt_get(handle);
	struct store_test_handle *sh;

	sh = calloc(1, sizeof(*sh));
	if (!sh) {
		LOG_(ts, OVIS_LCRITICAL,
		     "memory allocation failed in open_store.\n");
		return NULL;
	}

	if (container) {
		sh->container = strdup(container);
		if (!sh->container) {
			LOG_(ts, OVIS_LCRITICAL,
			     "memory allocation failed in open_store.\n");
			free(sh);
			return NULL;
		}
	}
	if (schema) {
		sh->schema = strdup(schema);
		if (!sh->schema) {
			LOG_(ts, OVIS_LCRITICAL,
			     "memory allocation failed in open_store.\n");
			free(sh->container);
			free(sh);
			return NULL;
		}
	}
	return sh;
}

static void close_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	struct store_test_handle *sh = _sh;

	if (!sh)
		return;
	free(sh->container);
	free(sh->schema);
	free(sh);
}

static int flush_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t sh)
{
	/* No buffered data to flush. */
	return 0;
}

static int store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t sh,
		  ldms_set_t set, int *metric_arry, size_t metric_count)
{
	store_test_t ts = ldmsd_plug_ctxt_get(handle);

	LOG_(ts, OVIS_LDEBUG, "store() sleeping %ld us.\n",
	     ts->latency_us);
	__sleep_latency(ts);
	return 0;
}

static int commit_rows(ldmsd_plug_handle_t handle, ldmsd_strgp_t strgp,
			ldms_set_t set, ldmsd_row_list_t row_list, int row_count)
{
	store_test_t ts = ldmsd_plug_ctxt_get(handle);

	LOG_(ts, OVIS_LDEBUG,
	     "commit() sleeping %ld us for %d row(s).\n",
	     ts->latency_us, row_count);
	__sleep_latency(ts);
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	store_test_t ts = calloc(1, sizeof(*ts));
	if (!ts) {
		ovis_log(ldmsd_plug_log_get(handle), OVIS_LCRITICAL,
			 "failed to allocate plugin context.\n");
		return ENOMEM;
	}
	pthread_mutex_init(&ts->cfg_lock, NULL);
	ts->log = ldmsd_plug_log_get(handle);
	ts->latency_us = 0;
	ldmsd_plug_ctxt_set(handle, ts);
	return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	store_test_t ts = ldmsd_plug_ctxt_get(handle);
	if (!ts)
		return;
	pthread_mutex_destroy(&ts->cfg_lock);
	free(ts);
}

struct ldmsd_store ldmsd_plugin_interface = {
	.base.type        = LDMSD_PLUGIN_STORE,
	.base.flags       = LDMSD_PLUGIN_MULTI_INSTANCE,
	.base.config      = config,
	.base.usage       = usage,
	.base.constructor = constructor,
	.base.destructor  = destructor,

	.open   = open_store,
	.store  = store,
	.flush  = flush_store,
	.close  = close_store,
	.commit = commit_rows,
};