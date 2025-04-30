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
#ifndef __TIMER_BASE_H
#define __TIMER_BASE_H

#include "ldms.h"
#include "ldmsd.h"
#include "tsampler.h"
#include <sys/queue.h>
#include <pthread.h>
#include "sampler_base.h"

struct tsampler_timer_entry {
	struct tsampler_timer timer;
	TAILQ_ENTRY(tsampler_timer_entry) entry;
};

struct timer_base {
	enum {
		ST_INIT,
		ST_CONFIGURED,
		ST_RUNNING,
	} state;
	base_data_t cfg;	/* sampler base class data */
	ldms_set_t set;
	ldms_schema_t schema;
	uint64_t compid;
	pthread_mutex_t mutex;
	ovis_log_t mylog;
	TAILQ_HEAD(, tsampler_timer_entry) timer_list;
	char buff[1024]; /* string buffer for internal timer_base use */
	char iname[1024]; /* iname for internal use */
	char pname[1024]; /* producer name for internal use */
};

void timer_base_init(struct timer_base *tb);

int timer_base_config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl,
		      struct attr_value_list *avl, ovis_log_t mylog);

int timer_base_create_set(struct timer_base *tb, const char *config_name);

void timer_base_term(ldmsd_plug_handle_t handle);

int timer_base_sample(ldmsd_plug_handle_t handle);

void timer_base_cleanup(struct timer_base *tb);

int timer_base_add_hfmetric(struct timer_base *tb,
                            const char *name,
                            enum ldms_value_type type,
                            int n,
                            const struct timeval *interval,
                            tsampler_sample_cb cb,
                            void *cb_context,
                            void *ctxt);
#endif
