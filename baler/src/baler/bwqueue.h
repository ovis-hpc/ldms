/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2016 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
 * \file bwqueue.h
 * \author Narate Taerat (narate@ogc.us)
 *
 * \defgroup bwqueue Baler Work Queue
 * \{
 * Work Queue for Baler. This work queue will be used to handle work
 * in input queue and output queue in Baler Daemon (balerd.c).
 */
#ifndef __BWQUEUE_H
#define __BWQUEUE_H

#include "bcommon.h"
#include "btypes.h"
#include "butils.h"
#include <sys/queue.h>
#include <pthread.h>
#include <semaphore.h>

/**
 * Baler Input Queue entry data.
 */
struct binq_data {
	enum {
		BINQ_DATA_MSG = 0,
		BINQ_DATA_METRIC,
	} type;
	struct bstr *hostname; /**< Hostname. */
	struct timeval tv; /**< Time value. */
	uint32_t tok_count; /**< Token count, for convenient ptn allocation.*/
	struct bstr_list_head tokens; /**< Pointer to message tokens. */
};

/**
 * Baler Output Queue entry data.
 *
 * A single output queue entry will only be processed by the output plugin
 * (`op`) specified in the boutq_data.
 */
struct boutq_data {
	struct boutplugin *op; /** Output plugin. */
	uint32_t comp_id; /**< Component ID (extracted from hostname). */
	struct timeval tv; /**< Time value. */
	struct bmsg *msg; /**< Parsed message, which also includes pattern in it. */
};

/**
 * Baler Work Queue head.
 */
TAILQ_HEAD(bwq_head, bwq_entry);

/**
 * Baler Work Queue entry.
 */
struct bwq_entry {
	union {
		struct binq_data in;
		struct boutq_data out;
	} data;
	void *ctxt;
	TAILQ_ENTRY(bwq_entry) link; /**< Link to next/prev entry. */
};

/**
 * Free function for binq entry.
 * \param ent The input queue entry.
 */
static
void binq_entry_free(struct bwq_entry *ent)
{
	bstr_list_free_entries(&ent->data.in.tokens);
	free(ent->data.in.hostname);
	free(ent);
}

/**
 * Free function for boutq entry.
 * \param ent The output queue entry.
 */
static
void boutq_entry_free(struct bwq_entry *ent)
{
	bmsg_free(ent->data.out.msg);
	free(ent);
}

/**
 * Baler Work Queue. A thread-safe queue data structure.
 */
struct bwq {
	struct bwq_head head; /**< Queue head */
	pthread_mutex_t qmutex; /**< Queue mutex */
	sem_t nq_sem; /**< Semaphore for enqueueing */
	sem_t dq_sem; /**< Semaphore for dequeueing */
};

/**
 * Thread-safe enqueue function for ::bwq structure.
 * \param q The queue.
 * \param ent A queue entry to be inserted into \a q.
 */
static inline
void bwq_nq(struct bwq *q, struct bwq_entry *ent)
{
	sem_wait(&q->nq_sem);
	pthread_mutex_lock(&q->qmutex);
	TAILQ_INSERT_TAIL(&q->head, ent, link);
	sem_post(&q->dq_sem);
	pthread_mutex_unlock(&q->qmutex);
}

/**
 * Dequeue the given \a q.
 * \note The first entry is removed from the queue (as the name suggests).
 * \return NULL if \a q is empty.
 * \return A pointer to ::bwq_entry if \a q is not empty.
 */
static inline
struct bwq_entry* bwq_dq(struct bwq *q)
{
	struct bwq_entry *ent;
	sem_wait(&q->dq_sem);
	pthread_mutex_lock(&q->qmutex);
	ent = TAILQ_FIRST(&q->head);
	TAILQ_REMOVE(&q->head, ent, link);
	sem_post(&q->nq_sem);
	pthread_mutex_unlock(&q->qmutex);
	return ent;
}

/**
 * Initialization of ::bwq.
 * \param q The ::bwq to be initialized.
 * \param qsize The queue size.
 */
void bwq_init(struct bwq *q, size_t qsize);

/**
 * Convenient allocation function WITHOUT structure initialization.
 * \return On success, a pointer to ::bwq.
 * \return NULL on failure.
 */
struct bwq* bwq_alloc();

/**
 * Convenient allocation function WITH structure initialization.
 *
 * \param qsize The size of the work queue.
 *
 * \return On success, a pointer to ::bwq.
 * \return NULL on failure.
 */
struct bwq* bwq_alloci(size_t qsize);

#endif /* __BWQUEUE_H */
/**\}*/
