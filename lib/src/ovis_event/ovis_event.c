/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
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
#include "ovis_event_priv.h"
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <assert.h>

#define __TIMER_VALID(tv) ((tv)->tv_sec >= 0)

static
void ovis_event_manager_destroy(ovis_event_manager_t m);

static inline
void ovis_event_manager_ref_get(ovis_event_manager_t m)
{
	pthread_mutex_lock(&m->mutex);
	m->refcount++;
	pthread_mutex_unlock(&m->mutex);
}

static inline
void ovis_event_manager_ref_put(ovis_event_manager_t m)
{
	pthread_mutex_lock(&m->mutex);
	assert(m->refcount > 0);
	m->refcount--;
	if (m->refcount == 0) {
		pthread_mutex_unlock(&m->mutex);
		ovis_event_manager_destroy(m);
		return;
	}
	pthread_mutex_unlock(&m->mutex);
}

static inline
ovis_event_t hnode_to_event(ovis_heap_node_t n)
{
	return ((void*)n) - offsetof(struct ovis_event, hnode);
}

int ovis_event_heap_cmp(ovis_heap_node_t a, ovis_heap_node_t b)
{
	ovis_event_t _a = hnode_to_event(a);
	ovis_event_t _b = hnode_to_event(b);
	if (_a->tv.tv_sec < _b->tv.tv_sec) {
		return -1;
	}
	if (_a->tv.tv_sec > _b->tv.tv_sec) {
		return 1;
	}
	if (_a->tv.tv_usec < _b->tv.tv_usec) {
		return -1;
	}
	if (_a->tv.tv_usec > _b->tv.tv_usec) {
		return 1;
	}
	return 0;
}

static
void __ovis_event_pipe_cb(uint32_t events, const struct timeval *tv, ovis_event_t ev)
{
	int rc;
	ovis_event_manager_t m = ev->ctxt;
	ovis_event_t pev; /* event read from the pipe */
loop:
	/* just reap whatever we have in the channel */
	rc = read(m->pfd[0], &pev, sizeof(pev));
	if (rc < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;
		assert(0 && errno);
	}
	goto loop;
}

ovis_event_manager_t ovis_event_manager_create()
{
	int rc;
	ovis_event_manager_t m = malloc(sizeof(*m));
	if (!m)
		goto out;

	pthread_mutex_init(&m->mutex, NULL);
	m->efd = -1;
	m->pfd[0] = -1;
	m->pfd[1] = -1;
	m->event_heap = NULL;
	m->evcount = 0;
	m->refcount = 1;
	m->state = OVIS_EVENT_MANAGER_INIT;

	m->event_heap = ovis_heap_create(4096, ovis_event_heap_cmp);
	if (!m->event_heap)
		goto err;

	m->efd = epoll_create(4096); /* size is ignored since Linux 2.6.8 */
	if (m->efd == -1)
		goto err;

	rc = pipe(m->pfd);
	if (rc != 0)
		goto err;

	rc = fcntl(m->pfd[0], F_SETFL, O_NONBLOCK);
	if (rc != 0)
		goto err;

	rc = fcntl(m->pfd[1], F_SETFL, O_NONBLOCK);
	if (rc != 0)
		goto err;

	m->ovis_ev.ctxt = m;
	m->ovis_ev.cb = __ovis_event_pipe_cb;
	m->ovis_ev.tv.tv_sec = -1;
	m->ovis_ev.tv.tv_usec = 0;
	m->ovis_ev.timer.tv_sec = -1;
	m->ovis_ev.timer.tv_sec = 0;
	m->ovis_ev.fd = m->pfd[0];
	m->ovis_ev.hnode.idx = -1;
	m->ovis_ev.epoll_events = EPOLLIN;

	m->ev[0].events = m->ovis_ev.epoll_events;
	m->ev[0].data.ptr = &m->ovis_ev;
	rc = epoll_ctl(m->efd, EPOLL_CTL_ADD, m->pfd[0], &m->ev[0]);
	if (rc != 0)
		goto err;

	goto out;

err:
	ovis_event_manager_free(m);

out:
	return m;
}

static
void ovis_event_manager_destroy(ovis_event_manager_t m)
{
	if (m->efd >= 0)
		close(m->efd);

	if (m->pfd[0] >= 0)
		close(m->pfd[0]);

	if (m->pfd[1] >= 0)
		close(m->pfd[1]);

	if (m->event_heap)
		ovis_heap_free(m->event_heap);

	free(m);
}

void ovis_event_manager_free(ovis_event_manager_t m)
{
	ovis_event_manager_ref_put(m);
}

/**
 * Process events in the heap and returns time-to-next-event.
 *
 * \retval timeout the timeout (milliseconds) to the next event.
 */
static
int ovis_event_heap_process(struct ovis_event_manager *m)
{
	struct timeval tv, dtv;
	ovis_event_t ev;
	ovis_heap_node_t hnode;
	int timeout = -1;

loop:
	pthread_mutex_lock(&m->mutex);
	hnode = ovis_heap_top(m->event_heap);
	if (!hnode) {
		timeout = -1;
		goto out;
	}
	ev = hnode_to_event(hnode);

	gettimeofday(&tv, NULL);
	if (!timercmp(&ev->tv, &tv, >)) {
		/* current time is greater than event time */
		goto process_event;
	}
	timersub(&ev->tv, &tv, &dtv);
	assert(dtv.tv_sec >= 0);
	assert(dtv.tv_usec >= 0);
	timeout = dtv.tv_sec*1000 + dtv.tv_usec/1000;
	goto out;

process_event:
	if (ev->flags & OVIS_EVENT_PERSISTENT) {
		/* re-calculate timer for persistent events*/
		timeradd(&tv, &ev->timer, &ev->tv);
		ovis_heap_update(m->event_heap, hnode);
	} else {
		hnode = ovis_heap_pop(m->event_heap);
		m->evcount--;
	}
	pthread_mutex_unlock(&m->mutex);
	/* timeout event application callback */
	ev->cb(0, &tv, ev);
	goto loop;
out:
	pthread_mutex_unlock(&m->mutex);
	return timeout;
}

int ovis_event_timer_update(ovis_event_manager_t m, ovis_event_t ev)
{
	struct timeval tv;
	pthread_mutex_lock(&m->mutex);
	gettimeofday(&tv, NULL);
	timeradd(&tv, &ev->timer, &ev->tv);
	ovis_heap_update(m->event_heap, &ev->hnode);
	pthread_mutex_unlock(&m->mutex);
	return 0;
}

void *ovis_event_get_ctxt(ovis_event_t e)
{
	return e->ctxt;
}

int ovis_event_get_fd(ovis_event_t e)
{
	return e->fd;
}

ovis_event_t ovis_event_create(int fd, uint32_t epoll_events,
				const struct timeval *timer, int is_persistent,
				ovis_event_cb cb, void *ctxt)
{
	/* check validity before allocation */
	if (fd < 0 && ! timer) {
		errno = EINVAL;
		return NULL;
	}

	ovis_event_t ev = calloc(1, sizeof(*ev));
	if (!ev)
		goto out;
	ev->fd = fd;
	ev->epoll_events = epoll_events;
	ev->hnode.idx = -1;
	if (timer)
		ev->timer = *timer;
	else
		ev->timer.tv_sec = -1;
	if (is_persistent)
		ev->flags |= OVIS_EVENT_PERSISTENT;
	ev->cb = cb;
	ev->ctxt = ctxt;
out:
	return ev;
}

int ovis_event_add(ovis_event_manager_t m, ovis_event_t ev)
{
	int rc = 0;

	if (ev->fd >= 0) {
		struct epoll_event e;
		e.events = ev->epoll_events;
		e.data.ptr = ev;
		rc = epoll_ctl(m->efd, EPOLL_CTL_ADD, ev->fd, &e);
		if (rc)
			goto out;
		pthread_mutex_lock(&m->mutex);
		m->evcount++;
		pthread_mutex_unlock(&m->mutex);
	}

	if (__TIMER_VALID(&ev->timer)) {
		/* timer event */
		if (ev->hnode.idx != -1) {
			rc = EEXIST;
			goto out;
		}
		pthread_mutex_lock(&m->mutex);
		struct timeval tv;
		gettimeofday(&tv, NULL);
		timeradd(&tv, &ev->timer, &ev->tv);
		rc = ovis_heap_insert(m->event_heap, &ev->hnode);
		if (rc) {
			pthread_mutex_unlock(&m->mutex);
			goto out;
		}
		m->evcount++;
		pthread_mutex_unlock(&m->mutex);
		/* notify the event-loop about the newly added event */
		write(m->pfd[1], &ev, sizeof(ev));
	}

out:
	return rc;
}

int ovis_event_del(ovis_event_manager_t m, ovis_event_t ev)
{
	int rc = 0;
	if (ev->fd >= 0) {
		/* remove from epoll */
		struct epoll_event e;
		e.events = ev->epoll_events;
		e.data.ptr = ev;
		rc = epoll_ctl(m->efd, EPOLL_CTL_DEL, ev->fd, &e);
		if (rc)
			goto out;
		pthread_mutex_lock(&m->mutex);
		m->evcount--;
		pthread_mutex_unlock(&m->mutex);
	}

	pthread_mutex_lock(&m->mutex);
	if (ev->hnode.idx >= 0) {
		ovis_heap_remove(m->event_heap, &ev->hnode);
		m->evcount--;
		/* notify, to recalculate next timeout */
		write(m->pfd[1], &ev, sizeof(ev));
	}
	pthread_mutex_unlock(&m->mutex);
out:
	return rc;
}

void ovis_event_free(ovis_event_t ev)
{
	free(ev);
}

static
int ovis_event_term_check(ovis_event_manager_t m)
{
	int rc;
	pthread_mutex_lock(&m->mutex);
	switch (m->state) {
	case OVIS_EVENT_MANAGER_INIT:
		assert(0);
		break;
	case OVIS_EVENT_MANAGER_RUNNING:
		/* OK */
		rc = 0;
		break;
	case OVIS_EVENT_MANAGER_TERM:
		rc = EINTR;
		break;
	}
	pthread_mutex_unlock(&m->mutex);
	return rc;
}

int ovis_event_loop(ovis_event_manager_t m, int return_on_empty)
{
	struct timeval tv, dtv, etv;
	ovis_event_t ev;
	int timeout;
	int i;
	int fd;
	int rc = 0;

	ovis_event_manager_ref_get(m);
	pthread_mutex_lock(&m->mutex);
	switch (m->state) {
	case OVIS_EVENT_MANAGER_INIT:
	case OVIS_EVENT_MANAGER_TERM:
		m->state = OVIS_EVENT_MANAGER_RUNNING;
		rc = 0;
		break;
	case OVIS_EVENT_MANAGER_RUNNING:
		rc = EINVAL;
		break;
	}
	pthread_mutex_unlock(&m->mutex);
	if (rc)
		goto out;

loop:
	timeout = ovis_event_heap_process(m);
	pthread_mutex_lock(&m->mutex);
	if (!m->evcount && return_on_empty) {
		pthread_mutex_unlock(&m->mutex);
		return ENOENT;
	}
	pthread_mutex_unlock(&m->mutex);

	rc = epoll_wait(m->efd, m->ev, MAX_OVIS_EVENTS, timeout);

	for (i = 0; i < rc; i++) {
		rc = ovis_event_term_check(m);
		if (rc)
			goto out;
		ev = m->ev[i].data.ptr;
		if (ev->cb) {
			ev->cb(m->ev[i].events, NULL, ev);
		}
		if (ev->hnode.idx != -1) {
			/* i/o event has an active timer */
			rc = ovis_event_timer_update(m, ev);
		}
	}

	rc = ovis_event_term_check(m);
	if (rc)
		goto out;

	goto loop;
out:
	ovis_event_manager_ref_put(m);
	return rc;
}

int ovis_event_term(ovis_event_manager_t m)
{
	int rc;
	ovis_event_t none = NULL;
	pthread_mutex_lock(&m->mutex);
	switch (m->state) {
	case OVIS_EVENT_MANAGER_RUNNING:
		m->state = OVIS_EVENT_MANAGER_TERM;
		rc = 0;
		write(m->pfd[1], &none, sizeof(none));
		break;
	case OVIS_EVENT_MANAGER_INIT:
	case OVIS_EVENT_MANAGER_TERM:
		rc = EINVAL;
		break;
	}
	pthread_mutex_unlock(&m->mutex);
	return rc;
}
