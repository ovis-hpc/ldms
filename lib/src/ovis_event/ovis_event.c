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
#include <stdio.h>
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
struct ovis_event_heap *ovis_event_heap_create(uint32_t alloc_len)
{
	struct ovis_event_heap *h = malloc(sizeof(*h) + alloc_len*sizeof(h->ev[0]));
	if (!h)
		return h;
	h->alloc_len = alloc_len;
	h->heap_len = 0;
	return h;
}

static inline
void ovis_event_heap_free(struct ovis_event_heap *h)
{
	free(h);
}

static inline
int ovis_event_lt(struct ovis_event *e0, struct ovis_event *e1)
{
	return timercmp(&e0->tv, &e1->tv, <);
}

static inline
void ovis_event_heap_float(struct ovis_event_heap *h, int idx)
{
	int pidx;
	struct ovis_event *ev = h->ev[idx];
	while (idx) {
		pidx = (idx - 1)/2;
		if (!ovis_event_lt(ev, h->ev[pidx])) {
			break;
		}
		h->ev[idx] = h->ev[pidx];
		h->ev[pidx]->idx = idx;
		idx = pidx;
	}
	h->ev[idx] = ev;
	ev->idx = idx;
}

static inline
void ovis_event_heap_sink(struct ovis_event_heap *h, int idx)
{
	int l, r, x;
	struct ovis_event *ev = h->ev[idx];
	l = idx*2+1;
	r = l+1;
	while (l < h->heap_len) {
		if (r >= h->heap_len) {
			x = l;
			goto cmp;
		}
		if (ovis_event_lt(h->ev[l], h->ev[r])) {
			x = l;
		} else {
			x = r;
		}
	cmp:
		if (!ovis_event_lt(h->ev[x], ev)) {
			break;
		}
		h->ev[idx] = h->ev[x];
		h->ev[x]->idx = idx;
		idx = x;
		l = idx*2+1;
		r = l+1;
	}
	h->ev[idx] = ev;
	ev->idx = idx;
}

static inline
int ovis_event_heap_insert(struct ovis_event_heap *h, struct ovis_event *ev)
{
	if (h->heap_len == h->alloc_len) {
		return ENOMEM;
	}
	h->ev[h->heap_len++] = ev;
	ovis_event_heap_float(h, h->heap_len-1);
	return 0;
}

static inline
int ovis_event_heap_remove(struct ovis_event_heap *h, struct ovis_event *ev)
{
	if (ev->idx >= 0) {
		h->ev[ev->idx] = h->ev[--h->heap_len];
		h->ev[ev->idx]->idx = ev->idx;
		ovis_event_heap_sink(h, ev->idx);
		ev->idx = -1;
	}
	return 0;
}

static inline
void ovis_event_heap_update(struct ovis_event_heap *h, int idx)
{
	struct ovis_event *ev = h->ev[idx];
	ovis_event_heap_float(h, idx);
	if (ev->idx == idx) {
		ovis_event_heap_sink(h, idx);
	}
}

static inline
struct ovis_event *ovis_event_heap_pop(struct ovis_event_heap *h)
{
	struct ovis_event *ev = h->ev[0];
	ovis_event_heap_remove(h, ev);
	return ev;
}

static inline
struct ovis_event *ovis_event_heap_top(struct ovis_event_heap *h)
{
	if (h->heap_len > 0)
		return h->ev[0];
	return NULL;
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
	m->heap = NULL;
	m->evcount = 0;
	m->refcount = 1;
	m->state = OVIS_EVENT_MANAGER_INIT;

	m->heap = ovis_event_heap_create(4096);
	if (!m->heap)
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
	m->ovis_ev.idx = -1;
	m->ovis_ev.epoll_events = EPOLLIN;

	m->ev[0].events = m->ovis_ev.epoll_events;
	m->ev[0].data.ptr = &m->ovis_ev;
	rc = epoll_ctl(m->efd, EPOLL_CTL_ADD, m->pfd[0], &m->ev[0]);
	if (rc != 0)
		goto err;

	goto out;

err:
	ovis_event_manager_free(m);
	m = NULL;
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

	if (m->heap)
		ovis_event_heap_free(m->heap);

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
	int timeout = -1;

loop:
	pthread_mutex_lock(&m->mutex);
	ev = ovis_event_heap_top(m->heap);
	if (!ev) {
		timeout = -1;
		goto out;
	}

	gettimeofday(&tv, NULL);
	if (!timercmp(&ev->tv, &tv, >)) {
		/* current time is greater than event time */
		goto process_event;
	}
	timersub(&ev->tv, &tv, &dtv);
	assert(dtv.tv_sec >= 0);
	assert(dtv.tv_usec >= 0);
	/* tv_usec + 999 is for rounding-up transforming usec -> msec */
	timeout = dtv.tv_sec*1000 + (dtv.tv_usec + 999)/1000;
	/*
	printf("timeout: %d\n", timeout);
	printf("ev->tv: %ld.%06ld\n", ev->tv.tv_sec, ev->tv.tv_usec);
	printf("tv: %ld.%06ld\n", tv.tv_sec, tv.tv_usec);
	printf("dtv: %ld.%06ld\n", dtv.tv_sec, dtv.tv_usec);
	*/
	goto out;

process_event:
	if (ev->flags & OVIS_EVENT_PERSISTENT) {
		/* re-calculate timer for persistent events*/
		timeradd(&tv, &ev->timer, &ev->tv);
		ovis_event_heap_update(m->heap, ev->idx);
	} else {
		ev = ovis_event_heap_pop(m->heap);
		m->evcount--;
	}
	pthread_mutex_unlock(&m->mutex);
	/* timeout event application callback */
	ev->cb(0, &tv, ev);
	goto loop;
out:
	if (m->state == OVIS_EVENT_MANAGER_RUNNING)
		m->state = OVIS_EVENT_MANAGER_WAITING;
	pthread_mutex_unlock(&m->mutex);
	return timeout;
}

int ovis_event_timer_update(ovis_event_manager_t m, ovis_event_t ev)
{
	struct timeval tv;
	pthread_mutex_lock(&m->mutex);
	gettimeofday(&tv, NULL);
	timeradd(&tv, &ev->timer, &ev->tv);
	ovis_event_heap_update(m->heap, ev->idx);
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
	ev->idx = -1;
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
	ssize_t wb;

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
		if (ev->idx != -1) {
			rc = EEXIST;
			goto out;
		}
		pthread_mutex_lock(&m->mutex);
		struct timeval tv;
		gettimeofday(&tv, NULL);
		timeradd(&tv, &ev->timer, &ev->tv);
		rc = ovis_event_heap_insert(m->heap, ev);
		if (rc) {
			pthread_mutex_unlock(&m->mutex);
			goto out;
		}
		m->evcount++;
		/* notify only if the new event affect the next timeout */
		if (m->state == OVIS_EVENT_MANAGER_WAITING && ev->idx == 0) {
			wb = write(m->pfd[1], &ev, sizeof(ev));
			if (wb == -1) {
				rc = errno;
				pthread_mutex_unlock(&m->mutex);
				goto out;
			}
		}
		pthread_mutex_unlock(&m->mutex);
	}

out:
	return rc;
}

int ovis_event_del(ovis_event_manager_t m, ovis_event_t ev)
{
	int rc = 0;
	ssize_t wb;
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
	if (ev->idx >= 0) {
		ovis_event_heap_remove(m->heap, ev);
		m->evcount--;
		/* notify only last delete event */
		if (m->state == OVIS_EVENT_MANAGER_WAITING && m->evcount == 0) {
			wb = write(m->pfd[1], &ev, sizeof(ev));
			if (wb == -1) {
				rc = errno;
				pthread_mutex_unlock(&m->mutex);
				goto out;
			}
		}
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
	case OVIS_EVENT_MANAGER_WAITING:
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
	case OVIS_EVENT_MANAGER_WAITING:
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
	assert(rc >= 0 || errno == EINTR);
	pthread_mutex_lock(&m->mutex);
	if (m->state == OVIS_EVENT_MANAGER_WAITING)
		m->state = OVIS_EVENT_MANAGER_RUNNING;
	pthread_mutex_unlock(&m->mutex);

	for (i = 0; i < rc; i++) {
		rc = ovis_event_term_check(m);
		if (rc)
			goto out;
		ev = m->ev[i].data.ptr;
		if (ev->cb) {
			ev->cb(m->ev[i].events, NULL, ev);
		}
		if (ev->idx != -1) {
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
	ssize_t wb;
	ovis_event_t none = NULL;
	pthread_mutex_lock(&m->mutex);
	switch (m->state) {
	case OVIS_EVENT_MANAGER_RUNNING:
	case OVIS_EVENT_MANAGER_WAITING:
		m->state = OVIS_EVENT_MANAGER_TERM;
		rc = 0;
		wb = write(m->pfd[1], &none, sizeof(none));
		if (wb == -1)
			rc = errno;
		break;
	case OVIS_EVENT_MANAGER_INIT:
	case OVIS_EVENT_MANAGER_TERM:
		rc = EINVAL;
		break;
	}
	pthread_mutex_unlock(&m->mutex);
	return rc;
}
