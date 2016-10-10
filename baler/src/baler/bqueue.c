/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
 *
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
#include "bqueue.h"

struct bqueue *bqueue_new()
{
	struct bqueue *q = malloc(sizeof(*q));
	if (!q)
		return NULL;
	TAILQ_INIT(&q->head);
	pthread_mutex_init(&q->mutex, NULL);
	pthread_cond_init(&q->cond, NULL);
	q->state = BQUEUE_STATE_ACTIVE;
	return q;
}

void bqueue_nq(struct bqueue *q, struct bqueue_entry *ent)
{
	pthread_mutex_lock(&q->mutex);
	TAILQ_INSERT_TAIL(&q->head, ent, link);
	pthread_cond_broadcast(&q->cond);
	pthread_mutex_unlock(&q->mutex);
}

struct bqueue_entry *bqueue_dq(struct bqueue *q)
{
	struct bqueue_entry *ent = NULL;
	pthread_mutex_lock(&q->mutex);

	while (q->state == BQUEUE_STATE_ACTIVE &&
			(ent = TAILQ_FIRST(&q->head)) == NULL) {
		pthread_cond_wait(&q->cond, &q->mutex);
	}

	if (ent)
		TAILQ_REMOVE(&q->head, ent, link);

	pthread_mutex_unlock(&q->mutex);
	return ent;
}

void bqueue_free(struct bqueue *q)
{
	pthread_cond_destroy(&q->cond);
	pthread_mutex_destroy(&q->mutex);
	free(q);
}

void bqueue_term(struct bqueue *q)
{
	pthread_mutex_lock(&q->mutex);
	q->state = BQUEUE_STATE_TERM;
	pthread_cond_broadcast(&q->cond);
	pthread_mutex_unlock(&q->mutex);
}
