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
#include "ovis_event_priv.h"
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/syscall.h>

#define __TIMER_VALID(tv) ((tv)->tv_sec >= 0)

#define ROUND(x, p) ( ((x)+((p)-1))/(p)*(p) )
#define USEC 1000000

#define OVIS_EVENT_HEAP_SIZE_DEFAULT 16384

static
void ovis_scheduler_destroy(ovis_scheduler_t m);

static void __ovis_event_next_wakeup(const struct timeval *now, ovis_event_t ev);

static inline
void ovis_scheduler_ref_get(ovis_scheduler_t m)
{
	pthread_mutex_lock(&m->mutex);
	m->refcount++;
	pthread_mutex_unlock(&m->mutex);
}

static inline
void ovis_scheduler_ref_put(ovis_scheduler_t m)
{
	if (!m)
		return;
	assert(m->refcount > 0);
	pthread_mutex_lock(&m->mutex);
	m->refcount--;
	if (m->refcount == 0) {
		pthread_mutex_unlock(&m->mutex);
		ovis_scheduler_destroy(m);
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
int ovis_event_lt(ovis_event_t e0, ovis_event_t e1)
{
	return timercmp(&e0->priv.tv, &e1->priv.tv, <);
}

static inline
void ovis_event_heap_float(struct ovis_event_heap *h, int idx)
{
	int pidx;
	ovis_event_t ev = h->ev[idx];
	while (idx) {
		pidx = (idx - 1)/2;
		if (!ovis_event_lt(ev, h->ev[pidx])) {
			break;
		}
		h->ev[idx] = h->ev[pidx];
		h->ev[pidx]->priv.idx = idx;
		idx = pidx;
	}
	h->ev[idx] = ev;
	ev->priv.idx = idx;
}

static inline
void ovis_event_heap_sink(struct ovis_event_heap *h, int idx)
{
	int l, r, x;
	ovis_event_t ev = h->ev[idx];
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
		h->ev[x]->priv.idx = idx;
		idx = x;
		l = idx*2+1;
		r = l+1;
	}
	h->ev[idx] = ev;
	ev->priv.idx = idx;
}

static inline
int ovis_event_heap_insert(struct ovis_event_heap *h, ovis_event_t ev)
{
	if (h->heap_len == h->alloc_len) {
		return ENOMEM;
	}
	h->ev[h->heap_len++] = ev;
	ovis_event_heap_float(h, h->heap_len-1);
	return 0;
}

static inline
int ovis_event_heap_remove(struct ovis_event_heap *h, ovis_event_t ev)
{
	if (ev->priv.idx >= 0) {
		h->ev[ev->priv.idx] = h->ev[--h->heap_len];
		h->ev[ev->priv.idx]->priv.idx = ev->priv.idx;
		ovis_event_heap_sink(h, ev->priv.idx);
		ev->priv.idx = -1;
	}
	return 0;
}

static inline
void ovis_event_heap_update(struct ovis_event_heap *h, int idx)
{
	ovis_event_t ev = h->ev[idx];
	ovis_event_heap_float(h, idx);
	if (ev->priv.idx == idx) {
		ovis_event_heap_sink(h, idx);
	}
}

static inline
ovis_event_t ovis_event_heap_pop(struct ovis_event_heap *h)
{
	ovis_event_t ev = h->ev[0];
	ovis_event_heap_remove(h, ev);
	return ev;
}

static inline
ovis_event_t ovis_event_heap_top(struct ovis_event_heap *h)
{
	if (h->heap_len > 0)
		return h->ev[0];
	return NULL;
}

static
void __ovis_event_pipe_cb(ovis_event_t ev)
{
	int rc;
	ovis_scheduler_t m = ev->param.ctxt;
	ovis_event_t pev; /* event read from the pipe */
loop:
	/* just reap whatever we have in the channel */
	rc = read(m->pfd[0], &pev, sizeof(pev));
	if (rc < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;
		assert(0 == "__ovis_event_pipe_cb: unexpected logic" && errno);
	}
	goto loop;
}

static inline int __ovis_event_get_heap_size()
{
	char *sz_str = getenv("OVIS_EVENT_HEAP_SIZE");
	if (!sz_str)
		return OVIS_EVENT_HEAP_SIZE_DEFAULT;
	return strtoul(sz_str, NULL, 0);
}

ovis_scheduler_t ovis_scheduler_new()
{
	int rc;
	uint32_t heap_sz;
	ovis_scheduler_t m = calloc(1,sizeof(*m));
	if (!m)
		goto out;

	TAILQ_INIT(&m->oneshot_tq);

	rc = pthread_mutex_init(&m->mutex, NULL);
	if (rc) {
		free(m);
		goto err2;
	}
	m->efd = -1;
	m->pfd[0] = -1;
	m->pfd[1] = -1;
	m->heap = NULL;
	m->evcount = 0;
	m->refcount = 1;
	m->state = OVIS_EVENT_MANAGER_INIT;

	heap_sz = __ovis_event_get_heap_size();
	m->heap = ovis_event_heap_create(heap_sz);
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

	m->ovis_ev.param.ctxt = m;
	m->ovis_ev.param.cb_fn = __ovis_event_pipe_cb;
	m->ovis_ev.priv.tv.tv_sec = -1;
	m->ovis_ev.priv.tv.tv_usec = 0;
	m->ovis_ev.param.fd = m->pfd[0];
	m->ovis_ev.priv.idx = -1;
	m->ovis_ev.param.epoll_events = EPOLLIN;
	m->ovis_ev.param.type = OVIS_EVENT_EPOLL;

	m->ev[0].events = m->ovis_ev.param.epoll_events;
	m->ev[0].data.ptr = &m->ovis_ev;
	rc = epoll_ctl(m->efd, EPOLL_CTL_ADD, m->pfd[0], &m->ev[0]);
	if (rc != 0)
		goto err;

	goto out;

err:
	ovis_scheduler_free(m);
err2:
	m = NULL;
out:
	return m;
}

static
void ovis_scheduler_destroy(ovis_scheduler_t m)
{
	if (m->efd >= 0)
		close(m->efd);

	if (m->pfd[0] >= 0)
		close(m->pfd[0]);

	if (m->pfd[1] >= 0)
		close(m->pfd[1]);

	if (m->heap)
		ovis_event_heap_free(m->heap);

	pthread_mutex_destroy(&m->mutex);
	free(m);
}

void ovis_scheduler_free(ovis_scheduler_t m)
{
	ovis_scheduler_ref_put(m);
}

/**
 * Process events in the heap and returns time-to-next-event.
 *
 * \retval timeout the timeout (milliseconds) to the next event.
 */
static
int ovis_event_heap_process(ovis_scheduler_t m)
{
	struct timeval tv, dtv;
	uint64_t us;
	ovis_event_t ev;
	int timeout = -1;

loop:
	pthread_mutex_lock(&m->mutex);

	/* process the immediate events first */
	ev = TAILQ_FIRST(&m->oneshot_tq);
	if (ev) {
		TAILQ_REMOVE(&m->oneshot_tq, ev, priv.entry);
		m->evcount--;
		ev->priv.idx = -1;
		goto process_event;
	}

	ev = ovis_event_heap_top(m->heap);
	if (!ev) {
		timeout = -1;
		goto out;
	}

	gettimeofday(&tv, NULL);
	if (!timercmp(&ev->priv.tv, &tv, >)) {
		/* current time is greater than event time */
		goto process_event;
	}
	timersub(&ev->priv.tv, &tv, &dtv);
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
	switch (ev->param.type) {
	case OVIS_EVENT_TIMEOUT:
	case OVIS_EVENT_EPOLL_TIMEOUT:
		/* timeout event application callback */
		gettimeofday(&tv, NULL);
		timeradd(&tv, &ev->param.timeout, &ev->priv.tv);
		ovis_event_heap_update(m->heap, ev->priv.idx);
		ev->cb.type = OVIS_EVENT_TIMEOUT;
		break;
	case OVIS_EVENT_PERIODIC:
		/* periodic event application callback */
		gettimeofday(&tv, NULL);
		us = tv.tv_sec * USEC + tv.tv_usec;
		us = ROUND(us, ev->param.periodic.period_us);
		us += ev->param.periodic.phase_us;
		ev->priv.tv.tv_sec = us / USEC;
		ev->priv.tv.tv_usec = us % USEC;
		ovis_event_heap_update(m->heap, ev->priv.idx);
		ev->cb.type = OVIS_EVENT_PERIODIC;
		break;
	case OVIS_EVENT_ONESHOT:
		/* no op */
		ev->cb.type = OVIS_EVENT_ONESHOT;
		break;
	default:
		assert(0 == "Unexpected event type");
	}
	pthread_mutex_unlock(&m->mutex);
	ev->param.cb_fn(ev);
	goto loop;
out:
	if (m->state == OVIS_EVENT_MANAGER_RUNNING)
		m->state = OVIS_EVENT_MANAGER_WAITING;
	pthread_mutex_unlock(&m->mutex);
	return timeout;
}

static
int __ovis_event_timer_update(ovis_scheduler_t m, ovis_event_t ev)
{
	struct timeval tv;
	pthread_mutex_lock(&m->mutex);
	gettimeofday(&tv, NULL);
	timeradd(&tv, &ev->param.timeout, &ev->priv.tv);
	ovis_event_heap_update(m->heap, ev->priv.idx);
	pthread_mutex_unlock(&m->mutex);
	return 0;
}

void *ovis_event_get_ctxt(ovis_event_t e)
{
	return e->param.ctxt;
}

int ovis_event_get_fd(ovis_event_t e)
{
	return e->param.fd;
}

ovis_event_t ovis_event_epoll_new(ovis_event_cb_fn cb_fn, void *ctxt,
				  int fd, uint32_t epoll_events)
{
	ovis_event_t ev = calloc(1, sizeof(*ev));
	if (!ev)
		goto out;
	ev->param.type = OVIS_EVENT_EPOLL;
	ev->param.fd = fd;
	ev->param.epoll_events = epoll_events;
	ev->param.cb_fn = cb_fn;
	ev->param.ctxt = ctxt;
	ev->priv.idx = -1;
out:
	return ev;
}

ovis_event_t ovis_event_timeout_new(ovis_event_cb_fn cb_fn, void *ctxt,
				    const struct timeval *timeout)
{
	ovis_event_t ev = calloc(1, sizeof(*ev));
	if (!ev)
		goto out;
	ev->param.type = OVIS_EVENT_TIMEOUT;
	ev->param.timeout = *timeout;
	ev->param.cb_fn = cb_fn;
	ev->param.ctxt = ctxt;
	ev->priv.idx = -1;
out:
	return ev;
}

ovis_event_t ovis_event_epoll_timeout_new(ovis_event_cb_fn cb_fn, void *ctxt,
					  int fd, uint32_t epoll_events,
					  const struct timeval *timeout)
{
	ovis_event_t ev = calloc(1, sizeof(*ev));
	if (!ev)
		goto out;
	ev->param.type = OVIS_EVENT_EPOLL_TIMEOUT;
	ev->param.fd = fd;
	ev->param.epoll_events = epoll_events;
	ev->param.timeout = *timeout;
	ev->param.cb_fn = cb_fn;
	ev->param.ctxt = ctxt;
	ev->priv.idx = -1;
out:
	return ev;
}

ovis_event_t ovis_event_periodic_new(ovis_event_cb_fn cb_fn, void *ctxt,
				     const struct ovis_periodic_s *p)
{
	ovis_event_t ev = calloc(1, sizeof(*ev));
	if (!ev)
		goto out;
	ev->param.type = OVIS_EVENT_PERIODIC;
	ev->param.periodic = *p;
	ev->param.fd = -1;
	ev->param.cb_fn = cb_fn;
	ev->param.ctxt = ctxt;
	ev->priv.idx = -1;
out:
	return ev;
}

static void __ovis_event_next_wakeup(const struct timeval *now, ovis_event_t ev)
{
	uint64_t us;
	switch (ev->param.type) {
	case OVIS_EVENT_TIMEOUT:
	case OVIS_EVENT_EPOLL_TIMEOUT:
		timeradd(now, &ev->param.timeout, &ev->priv.tv);
		break;
	case OVIS_EVENT_PERIODIC:
		us = now->tv_sec * USEC + now->tv_usec;
		us = ROUND(us, ev->param.periodic.period_us);
		us += ev->param.periodic.phase_us;
		ev->priv.tv.tv_sec = us / USEC;
		ev->priv.tv.tv_usec = us % USEC;
		break;
	default:
		assert(0 == "Bad event type");
	}
}

int ovis_scheduler_event_add(ovis_scheduler_t m, ovis_event_t ev)
{
	int rc = 0;
	ssize_t wb;

	if (ev->param.type & OVIS_EVENT_EPOLL) {
		struct epoll_event e;
		memset(&e, 0, sizeof(e));
		e.events = ev->param.epoll_events;
		e.data.ptr = ev;
		rc = epoll_ctl(m->efd, EPOLL_CTL_ADD, ev->param.fd, &e);
		if (rc)
			goto out;
		pthread_mutex_lock(&m->mutex);
		m->evcount++;
		pthread_mutex_unlock(&m->mutex);
	}

	if (ev->param.type & (OVIS_EVENT_TIMEOUT|OVIS_EVENT_PERIODIC)) {
		/* timer or periodic event */
		if (ev->priv.idx != -1) {
			rc = EEXIST;
			goto out;
		}
		pthread_mutex_lock(&m->mutex);
		struct timeval tv;
		/* calculate wake up time */
		gettimeofday(&tv, NULL);
		__ovis_event_next_wakeup(&tv, ev);
		rc = ovis_event_heap_insert(m->heap, ev);
		if (rc) {
			pthread_mutex_unlock(&m->mutex);
			goto out;
		}
		m->evcount++;
		/* notify only if the new event affect the next timeout */
		if (m->state == OVIS_EVENT_MANAGER_WAITING
				&& ev->priv.idx == 0) {
			wb = write(m->pfd[1], &ev, sizeof(ev));
			if (wb == -1) {
				rc = errno;
				pthread_mutex_unlock(&m->mutex);
				goto out;
			}
		}
		pthread_mutex_unlock(&m->mutex);
	}

	if (ev->param.type == OVIS_EVENT_ONESHOT) {
		if (ev->priv.idx != -1) {
			rc = EEXIST;
			goto out;
		}
		pthread_mutex_lock(&m->mutex);
		ev->priv.idx = 0;
		m->evcount++;
		TAILQ_INSERT_TAIL(&m->oneshot_tq, ev, priv.entry);
		if (m->state == OVIS_EVENT_MANAGER_WAITING) {
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

int ovis_scheduler_event_del(ovis_scheduler_t m, ovis_event_t ev)
{
	int rc = 0;
	ssize_t wb;

	if (ev->param.type & OVIS_EVENT_EPOLL) {
		/* remove from epoll */
		struct epoll_event e;
		e.events = ev->param.epoll_events;
		e.data.ptr = ev;
		rc = epoll_ctl(m->efd, EPOLL_CTL_DEL, ev->param.fd, &e);
		if (rc) {
#ifdef DEBUG
			printf("ovis_event: %s: epoll_ctl failed. ev->param.fd = %d\n",
				__func__, ev->param.fd);
#endif /* DEBUG */
			goto out;
		}
		pthread_mutex_lock(&m->mutex);
		m->evcount--;
		pthread_mutex_unlock(&m->mutex);
	}

	pthread_mutex_lock(&m->mutex);
	if (ev->priv.idx >= 0) {
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
int ovis_event_term_check(ovis_scheduler_t m)
{
	int rc;
	pthread_mutex_lock(&m->mutex);
	switch (m->state) {
	case OVIS_EVENT_MANAGER_INIT:
		assert(0 == "unexepected OVIS_EVENT_MANAGER_INIT in ovis_event_term_check");
		break;
	case OVIS_EVENT_MANAGER_RUNNING:
	case OVIS_EVENT_MANAGER_WAITING:
		/* OK */
		rc = 0;
		break;
	case OVIS_EVENT_MANAGER_TERM:
		rc = EINTR;
		break;
	default:
		rc = EINVAL;
	}
	pthread_mutex_unlock(&m->mutex);
	return rc;
}

static uint64_t __timespec_diff_us(struct timespec *start, struct timespec *end)
{
	if (start->tv_sec == 0) {
		/*
		 * This is the first sample, so ignore it for the difference.
		 */
		return 0;
	}

	uint64_t secs_ns, nsecs;
	secs_ns = (end->tv_sec - start->tv_sec) * 1000000000;
	nsecs = end->tv_nsec - start->tv_nsec;
	return (secs_ns + nsecs) / 1000;
}

static void __thrstat_reset(ovis_event_thrstat_t stats)
{
	memset(&stats->wait_end, 0, sizeof(stats->wait_end));
	memset(&stats->wait_start, 0, sizeof(stats->wait_start));
	stats->wait_tot = stats->proc_tot = 0;

	clock_gettime(CLOCK_REALTIME, &stats->start);
	stats->waiting = 0;
}

static void __thrstat_wait_start(ovis_event_thrstat_t stats)
{
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	stats->wait_start = now;
	stats->proc_tot += __timespec_diff_us(&stats->wait_end, &now);
	stats->waiting = 1;
}

static void __thrstat_wait_end(ovis_event_thrstat_t stats)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	stats->wait_end = now;
	stats->wait_tot += __timespec_diff_us(&stats->wait_start, &now);
	stats->waiting = 0;
}

static void __thrstat_init(ovis_scheduler_t m)
{
	m->stats.tid = syscall(SYS_gettid);
	m->stats.thread_id = (uint64_t)pthread_self();
	__thrstat_reset(&m->stats);
}

int ovis_scheduler_loop(ovis_scheduler_t m, int return_on_empty)
{
	ovis_event_t ev;
	int timeout;
	int i;
	int rc = 0;
	int cnt;

	__thrstat_init(m);
	ovis_scheduler_ref_get(m);
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

	__thrstat_wait_start(&m->stats);
	cnt = epoll_wait(m->efd, m->ev, MAX_EPOLL_EVENTS, timeout);
	__thrstat_wait_end(&m->stats);
	if (cnt < 0) {
		if (errno == EINTR)
			goto loop;
		return errno;
	}
	if (cnt == 0)
		goto loop;

	pthread_mutex_lock(&m->mutex);
	if (m->state == OVIS_EVENT_MANAGER_WAITING)
		m->state = OVIS_EVENT_MANAGER_RUNNING;
	pthread_mutex_unlock(&m->mutex);

	for (i = 0; i < cnt; i++) {
		rc = ovis_event_term_check(m);
		if (rc)
			goto out;

		ev = m->ev[i].data.ptr;
		ev->cb.type = OVIS_EVENT_EPOLL;
		ev->cb.epoll_events = m->ev[i].events;
		if (ev->param.type & OVIS_EVENT_TIMEOUT && ev->priv.idx != -1) {
			/* i/o event has an active timeout */
			rc = __ovis_event_timer_update(m, ev);
		}
		if (ev->param.cb_fn) {
			ev->param.cb_fn(ev);
		}
	}

	rc = ovis_event_term_check(m);
	if (rc)
		goto out;

	goto loop;
out:
	ovis_scheduler_ref_put(m);
	return rc;
}

int ovis_scheduler_term(ovis_scheduler_t m)
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
	default:
		rc = EINVAL;
	}
	pthread_mutex_unlock(&m->mutex);
	return rc;
}

int ovis_scheduler_epoll_event_mod(ovis_scheduler_t s, ovis_event_t ev,
				   int epoll_events)
{
	struct epoll_event epev;
	memset(&epev, 0, sizeof(epev));
	epev.events = epoll_events;
	epev.data.ptr = ev;
	int rc;
	rc = epoll_ctl(s->efd, EPOLL_CTL_MOD, ev->param.fd, &epev);
	if (rc) {
		rc = errno;
	} else {
		ev->param.epoll_events = epoll_events;
	}
	return rc;
}

void ovis_scheduler_thrstat_free(struct ovis_scheduler_thrstat *res)
{
	if (!res)
		return;
	free(res->name);
	free(res);
}

struct ovis_scheduler_thrstat *
ovis_scheduler_thrstat_get(ovis_scheduler_t sch, struct timespec *now)
{
	struct ovis_scheduler_thrstat *res;

	res = malloc(sizeof(*res));
	if (!res) {
		errno = ENOMEM;
		return NULL;
	}

	if (sch->stats.name) {
		res->name = strdup(sch->stats.name);
		if (!res->name) {
			goto err;
		}
	} else {
		res->name = NULL;
	}

	res->tid = sch->stats.tid;
	res->thread_id = sch->stats.thread_id;
	res->idle = sch->stats.wait_tot;
	res->active = sch->stats.proc_tot;
	if (sch->stats.waiting) {
		res->idle += __timespec_diff_us(&sch->stats.wait_start, now);
	} else {
		res->active += __timespec_diff_us(&sch->stats.wait_end, now);
	}
	res->dur = res->idle +
				res->active;
	res->idle_pc = res->idle * 100.0 / res->dur;
	res->active_pc = res->active  * 100.0 / res->dur;
	res->ev_cnt = sch->evcount;
	return res;
err:
	ovis_scheduler_thrstat_free(res);
	return NULL;
}

int ovis_scheduler_name_set(ovis_scheduler_t s, const char *name)
{
	if (s->stats.name) {
		free(s->stats.name);
	}
	s->stats.name = strdup(name);
	if (!s->stats.name)
		return errno;
	return 0;
}

const char *ovis_scheduler_name_get(ovis_scheduler_t s)
{
	return s->stats.name;
}
