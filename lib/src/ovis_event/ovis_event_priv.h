/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016-2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016-2017 Open Grid Computing, Inc. All rights reserved.
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
#ifndef __OVIS_EVENT_PRIV_H
#define __OVIS_EVENT_PRIV_H
#include "ovis-ldms-config.h"
#include "ovis_event.h"
#include "../coll/heap.h"
#include <pthread.h>
#include <stddef.h>

#define MAX_EPOLL_EVENTS 128

struct ovis_event_heap {
	uint32_t alloc_len;
	uint32_t heap_len;
	ovis_event_t ev[OVIS_FLEX];
};

typedef struct ovis_event_thrstat {
	char *name;
	pid_t tid;
	uint64_t thread_id;
	struct timespec start;
	struct timespec wait_start;
	struct timespec wait_end;
	int waiting;
	uint64_t wait_tot;
	uint64_t proc_tot;
} *ovis_event_thrstat_t;

struct ovis_scheduler_s {
	int evcount;
	int refcount;
	int efd; /* epoll fd */
	int pfd[2]; /* pipe for event notification */
	struct ovis_event_s ovis_ev;
	struct epoll_event ev[MAX_EPOLL_EVENTS];
	pthread_mutex_t mutex;
	struct ovis_event_heap *heap;
	enum {
		OVIS_EVENT_MANAGER_INIT,
		OVIS_EVENT_MANAGER_RUNNING,
		OVIS_EVENT_MANAGER_WAITING,
		OVIS_EVENT_MANAGER_TERM,
	} state;
	struct ovis_event_thrstat stats;
	TAILQ_HEAD(, ovis_event_s) oneshot_tq;
};

#endif
