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
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/errno.h>
#include <assert.h>
#include "omq.h"

// static pthread_mutex_t mq_lock;	/* global MQ lock */
// static LIST_HEAD(,omq_s) mq_head = LIST_HEAD_INITIALIZER(mq_head);
static omq_msg_t __msg_alloc(omq_t q, size_t size, void *context);
static void __msg_free(omq_msg_t msg);

static void timespec_diff(struct timespec *start, struct timespec *stop,
			  struct timespec *result)
{
	if ((stop->tv_nsec - start->tv_nsec) < 0) {
		result->tv_sec = stop->tv_sec - start->tv_sec - 1;
		result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
	} else {
		result->tv_sec = stop->tv_sec - start->tv_sec;
		result->tv_nsec = stop->tv_nsec - start->tv_nsec;
	}
}

static void *mq_proc(void *ctxt)
{
	int rc = ETIMEDOUT;
	omq_t q = ctxt;
	omq_msg_t msg;
	struct timespec ts;
	(void)clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 60;
	pthread_mutex_lock(&q->lock);
	while (!rc || rc == ETIMEDOUT) {
		rc = pthread_cond_timedwait(&q->cv, &q->lock, &ts);
		if (rc == ETIMEDOUT)
			continue;
		assert(!TAILQ_EMPTY(&q->q));
		for (msg = omq_msg_consume(q); msg; msg = omq_msg_consume(q)) {
			if (0 == (q->completed_cnt % 2048)) {
				struct timespec diff_ts;
				timespec_diff(&msg->enqueue_ts, &msg->dequeue_ts,
					      &diff_ts);
				printf("q_depth : %ld, "
				       "tv_sec : %ld, tv_nsec : %ld\n",
				       q->q_depth,
				       diff_ts.tv_sec, diff_ts.tv_nsec);
			}
			q->process_fn(q, msg);
			q->completed_cnt += 1;
		}
	}
	return NULL;
}

static omq_msg_t __msg_alloc(omq_t q, size_t size, void *context)
{
	omq_msg_t msg = malloc(size + sizeof(*msg));
	if (msg) {
		msg->data_size = size;
		msg->context = context;
		msg->q = q;
	}
	return msg;
}

static void __msg_free(omq_msg_t msg)
{
	free(msg);
}

omq_t omq_new(char *name,
	      omq_alloc_msg_fn alloc_fn, omq_free_msg_fn free_fn,
	      omq_process_fn process_fn)
{
	omq_t q = calloc(1, sizeof(*q));
	if (!q)
		return NULL;
	strncpy(q->name, name, sizeof(q->name));
	if (alloc_fn)
		q->alloc_fn = alloc_fn;
	else
		q->alloc_fn = __msg_alloc;
	if (free_fn)
		q->free_fn = free_fn;
	else
		q->free_fn = __msg_free;
	q->process_fn = process_fn;
	pthread_mutex_init(&q->lock, NULL);
	pthread_cond_init(&q->cv, NULL);
	TAILQ_INIT(&q->q);

	(void)pthread_create(&q->thread, NULL, mq_proc, q);
	pthread_setname_np(q->thread, name);
	return q;
}

omq_msg_t omq_msg_alloc(omq_t q, size_t size, void *context)
{
	return q->alloc_fn(q, size, context);
}

void omq_msg_free(omq_msg_t msg)
{
	msg->q->free_fn(msg);
}

int omq_msg_publish(omq_msg_t msg)
{
	(void)clock_gettime(CLOCK_REALTIME, &msg->enqueue_ts);
	pthread_mutex_lock(&msg->q->lock);
	TAILQ_INSERT_TAIL(&msg->q->q, msg, entry);
	pthread_mutex_unlock(&msg->q->lock);
	pthread_cond_signal(&msg->q->cv);
	__sync_fetch_and_add(&msg->q->q_depth, 1);
	return 0;
}

void omq_lock(omq_t q)
{
	pthread_mutex_lock(&q->lock);
}

void omq_unlock(omq_t q)
{
	pthread_mutex_unlock(&q->lock);
}

void omq_signal(omq_t q)
{
	pthread_cond_signal(&q->cv);
}

/**
 * \brief omq_msg_consume
 *
 * Returns the tail of the omq_t or NULL if the queue is empty. This must be
 * called holding the omq_t lock
 *
 * \param q Handle for the omq_t
 */
omq_msg_t omq_msg_consume(omq_t q)
{
	omq_msg_t msg;
	msg = TAILQ_FIRST(&q->q);
	if (msg) {
		TAILQ_REMOVE(&q->q, msg, entry);
		(void)clock_gettime(CLOCK_REALTIME, &msg->dequeue_ts);
		__sync_fetch_and_sub(&msg->q->q_depth, 1);
	}
	return msg;
}
