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
 * \file tsampler.h
 */

/**
 * \page tsampler
 *
 * \section name NAME
 *
 * tsampler - a utility to setup extra timers for ldmsd sampler plugins.
 *
 * \section synopsis SYNOPSIS
 * \code
 * #include "tsampler.h"
 * ...
 * void my_cb(tsampler_timer_t t)
 * {
 * 	ldms_metric_array_set_u64(t->set, t->mid, t->idx, <SOME_VALUE>);
 * }
 * ...
 * struct tsampler_timer t;
 * t.cb = my_cb;
 * t.set = set;
 * t.mid = metric_id;
 * t.interval.tv_sec = 0;
 * t.interval.tv_usec = 100000; // 10 Hz
 * t.ctxt = <SOME_CONTEXT>;
 * tsampler_timer_add(&t);
 * // my_cb will be called every 0.1 sec
 * ...
 * tsampler_timer_remove(&t);
 * // remove and stop the timer
 * \endcode
 *
 * \section description DESCRIPTION
 *
 * This utility aims to ease the use of ldms metric array to collect
 * data in a higher-frequency.
 *
 */
#ifndef __TSAMPLER_H
#define __TSAMPLER_H

#include <stdint.h>
#include <sys/queue.h>
#include <sys/time.h>
#include "ldms.h"
#include "ldmsd.h"

typedef struct tsampler tsampler_t;
typedef struct tsampler_timer *tsampler_timer_t;

typedef void (*tsampler_sample_cb)(tsampler_timer_t timer);

struct tsampler_timer {
	/* these are the setup parameters */
	tsampler_sample_cb cb;
	struct ldmsd_sampler *sampler; /* sampler */
	ldms_set_t set; /* set */
	int mid; /* metric id */
	int tid; /* metric id for timestamp associated with actual metric sample */
	struct timeval interval; /* callback interval */
	void *ctxt; /* additional context */

	/* idx will be set to current index in the array before callback */
	int idx;
	/* n is the number of elements in the array */
	int n;

	struct timeval time; /* time when the timer wakes up */

	/* information for tsampler internal usage, please ignore these fields */
	struct {
		ovis_event_t ev;
	} __internal;
};

/**
 * \brief add (activate) timer.
 *
 * The caller must have following parameters in \c t setup:
 *   - \c t->cb callback function
 *   - \c t->set the ldms_set contained
 *   - \c t->mid metric id
 *   - \c t->interval timer interval
 *   - \c t->ctxt (optional) user context
 *
 * \param t the pointer to ::tsampler_timer structure.
 *
 * \retval 0 if the timer \c t is added and activated successfully.
 * \retval EINVAL if at least one of the parameter is invalid.
 *
 * \note The timer \c t must not be freed until it is removed (by calling
 * tsampler_remove_timer()).
 */
int tsampler_timer_add(tsampler_timer_t t);

/**
 * \brief remove and deactivate the timer \c t.
 * \note This does not free the memory occupied by \c t. The application is
 * responsible for freeing it.
 */
void tsampler_timer_remove(tsampler_timer_t t);

#endif
