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
 * \file tsampler.c
 */

#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include "tsampler.h"
#include "ovis_event/ovis_event.h"

static pthread_t thr;
static ovis_scheduler_t evm;
static int ready;

static
void *thread_proc(void *arg)
{
	ovis_scheduler_loop(evm, 0);
	return NULL;
}

static
void tsampler_cb(ovis_event_t ev)
{
	tsampler_timer_t x = ev->param.ctxt;
	ldms_mval_t mv = ldms_metric_get(x->set, x->tid);
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (x->tid >= 0) {
		struct timeval *_tv = (void*)&mv->a_u64[x->idx * 2];
		*_tv = tv;
	}
	x->time = tv;
	x->cb(x);
	x->idx++;
	if (x->idx == x->n)
		x->idx = 0;
}

int tsampler_timer_add(tsampler_timer_t x)
{
	int rc = 0;
	union ovis_event_time_param_u tp;
	if (!ready)
		return EFAULT;
	if (x->interval.tv_sec < 0)
		return EINVAL;
	if (x->interval.tv_usec < 0 || 999999 < x->interval.tv_usec)
		return EINVAL;
	if (!x->cb)
		return EINVAL;
	if (x->mid < 0)
		return EINVAL;
	if (!x->set)
		return EINVAL;
	x->n = ldms_metric_array_get_len(x->set, x->mid);
	if (!x->n) {
		return EINVAL;
	}
	tp.periodic.period_us = x->interval.tv_sec * 1000000 +
				x->interval.tv_usec;
	tp.periodic.phase_us = 0;
	x->__internal.ev = ovis_event_periodic_new(tsampler_cb, x, &tp.periodic);
	if (!x->__internal.ev) {
		return errno;
	}
	x->idx = 0;
	rc = ovis_scheduler_event_add(evm, x->__internal.ev);
	if (rc) {
		ovis_event_free(x->__internal.ev);
	}
	return rc;
}

void tsampler_timer_remove(tsampler_timer_t x)
{
	if (x->__internal.ev) {
		ovis_scheduler_event_del(evm, x->__internal.ev);
		ovis_event_free(x->__internal.ev);
		x->__internal.ev = NULL;
	}
}

static __attribute__((constructor))
void __init()
{
	int rc;
	ready = 0;
	evm = ovis_scheduler_new();
	rc = pthread_create(&thr, NULL, thread_proc, NULL);
	if (rc)
		return;
	ready = 1;
}
