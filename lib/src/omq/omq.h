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
#ifndef _OMQ_H_
#define _OMQ_H_

#include <errno.h>
#include <pthread.h>
#include <sys/queue.h>
#include <inttypes.h>
/*
 * Implement a OGC Message Queue
 *
 * A highly efficient multi-threaded producer <-> consumer queue for submitting
 * work to be completed by a pool of threads. The submitter allocates and
 * publishes the message. The consumer processes and frees the resources for
 * the message.
 *
 * There is one worker thread for each queue.
 */
#define OMQ_NAME	31

typedef struct omq_msg_s {
	TAILQ_ENTRY(omq_msg_s)  entry;
	struct omq_s *q;
	void *context;
	size_t data_size;
	struct timespec enqueue_ts;
	struct timespec dequeue_ts;
	unsigned char data[];
} *omq_msg_t;

typedef struct omq_s {
	char name[OMQ_NAME+1];
	TAILQ_HEAD(,omq_msg_s) q;
	pthread_t thread;
	pthread_cond_t cv;
	pthread_mutex_t lock;

	uint64_t q_depth;
	size_t completed_cnt;	/* Totoal messages processed */

	/* Message constructor */
	omq_msg_t (*alloc_fn)(struct omq_s *, size_t, void *);
	/* Message destructor */
	void (*free_fn)(omq_msg_t);
	/* Message processor */
	int (*process_fn)(struct omq_s *, omq_msg_t);

} *omq_t;

typedef omq_msg_t (*omq_alloc_msg_fn)(omq_t, size_t, void *);
typedef void (*omq_free_msg_fn)(omq_msg_t msg);
typedef int (*omq_process_fn)(omq_t q, omq_msg_t msg);

omq_t omq_new(char *, omq_alloc_msg_fn, omq_free_msg_fn, omq_process_fn);
omq_msg_t omq_msg_alloc(omq_t, size_t, void *);
void omq_msg_free(omq_msg_t);
int omq_msg_publish(omq_msg_t);
omq_msg_t omq_msg_consume(omq_t q);
void omq_lock(omq_t q);
void omq_unlock(omq_t q);
void omq_signal(omq_t q);

#endif /* _OMQ_H_ */
