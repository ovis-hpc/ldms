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
/**
 * \file ovis_event.h
 */

/**
 * \page ovis_event ovis_event library
 *
 * \section name NAME
 *
 * ovis_event library - an epoll wrapper with timer and periodic events
 *
 * \section synopsis SYNOPSIS
 *
 * \code
 * #include <ovis_event/ovis_event.h>
 *
 * typedef void (*ovis_event_cb)(ovis_event_t ev);
 *
 * ovis_scheduler_t ovis_scheduler_new();
 * ovis_event_t ovis_event_epoll_new(ovis_event_cb_fn cb, void *ctxt,
 *                                   int fd, uint32_t epoll_events);
 * ovis_event_t ovis_event_timeout_new(ovis_event_cb_fn cb, void *ctxt,
 *                                     const struct timeval *tv);
 * ovis_event_t ovis_event_epoll_timeout_new(ovis_event_cb_fn cb, void *ctxt,
 *                                           int fd, uint32_t epoll_events,
 *                                           const struct timeval *tv);
 * ovis_event_t ovis_event_periodic_new(ovis_event_cb_fn cb, void *ctxt,
 *                                      const struct ovis_periodic_s *p);
 * int ovis_scheduler_event_add(ovis_scheduler_t m, ovis_event_t ev);
 * int ovis_scheduler_event_del(ovis_scheduler_t m, ovis_event_t ev);
 * int ovis_scheduler_loop(struct ovis_scheduler *m, int return_on_empty);
 * \endcode
 *
 *
 * \section description DESCRIPTION
 *
 * ovis_event library is an epoll wrapper with timer event support. Each event
 * is delivered to the application via callback interface (see \c ovis_event_cb
 * definition above). There are three kinds of events to deliver to the
 * application: epoll event, timeout event, and periodic event.
 *
 * ::ovis_event_epoll_new() creates an epoll event. An epoll event is delivered
 * when the ovis scheduler receive a notification from epoll subsystem.  Please
 * see \c epoll_ctl(2) for more information about epoll events.
 *
 * ::ovis_event_epoll_timeout_new() creates an epoll event with timeout.
 * An application can create an epoll event with timeout. In other words,
 * the application will receive a notification when the time run out or the
 * file descriptor is ready, whichever came first. The timeout got reset every
 * callback.
 *
 * ::ovis_event_timeout_new() creates a pure timeout event. A timeout event is
 * delivered to the application when the specified timeout value has passed.
 * If the application wants an event that arrive periodically, a timeout event
 * is NOT recommended as the wake up time contains slight slack and the
 * application will see a shifting wake up time. For periodic use case, we
 * recommend periodic event.
 *
 * ::ovis_event_periodic_new() creates a periodic event. This is different from
 * a timeout event in that the periodic event wake up time will (try to) be
 * aligned with the wall clock. The application suppled a period (in
 * micro-seconds) and a phase (a time shift in microseconds), and the ovis
 * scheduler will try to wake up periodically at \c period*n+phase. The periodic
 * event might have a slight wake up time slack, but it does not have
 * continuously time shifting like the timeout event.
 *
 *
 * \section example EXAMPLE
 *
 * The following code snippet are examples of how to use ovis_event. Please also
 * see \ref ovis_event_test.c for another usage example
 *
 * \code
 * #include <ovis_event/ovis_event.h>
 *
 * // example of a callback function
 * void callback(ovis_event_t e)
 * {
 *     void *ctxt = e->param.ctxt;
 *     int fd = ovis_event_get_fd(e);
 *
 *     switch (ev->cb.type) {
 *     case OVIS_EVENT_EPOLL:
 *         if (ev->cb.epoll_events & EPOLLIN)
 *             printf("fd: %d, input ready\n", e->param.fd);
 *         if (ev->cb.epoll_events & EPOLLOUT)
 *             printf("fd: %d, output ready\n", e->param.fd);
 *         if (ev->cb.epoll_events & EPOLLHUP)
 *             printf("fd: %d, hang-up\n", e->param.fd);
 *         break;
 *     case OVIS_EVENT_TIMEOUT:
 *         printf("timeout event");
 *         break;
 *     case OVIS_EVENT_PERIODIC:
 *         printf("periodic event");
 *         break;
 *     default:
 *         assert(0 == "Bad event");
 *         break;
 *     }
 * }
 * ...
 *
 * // code for create a scheduler and execute the scheduler loop (on a thread)
 * ovis_scheduler_t s;
 * s = ovis_scheduler_new();
 * ovis_scheduler_loop(s, 0); // this will block
 * ...
 *
 * // code for create and add event (on another thread)
 * int fd; // some file descriptor
 * struct timeval timeout = {20, 0};
 * ovis_event_t e;
 * void *my_ctxt; // some context of the event
 * // creating epoll-with-timeout event
 * e = ovis_event_epoll_timeout_new(callback, my_ctxt, fd, EPOLLIN, &timeout);
 * rc = ovis_scheduler_event_add(s, e);
 * // callback() function will be called when a timeout (20s) occur, or when fd
 * // is ready for read. The timeout is reset every time the callback is called.
 * // Use ovis_event_epoll_new() to create an epoll event without a timeout.
 * ...
 *
 *
 * // In some case, you want periodic event instead of a timeout event due to
 * // the fact that the wake-up time of the timeout event will keep drifting
 * // forward a little for every wake-up. Periodic events on the other hand will
 * // try to schedule the wake up time that is aligned to the wall clock.
 * // The following is an example of waking up every 1 second with 500 ms offset
 * // (e.g. 1.5  2.5  3.5  4.5 ....).
 * struct ovis_periodic_s p = {1000000, 500000};
 * ovis_event_t e;
 * e = ovis_event_periodic_new(callback, my_ctxt, &p);
 * ovis_scheduler_event_add(s, e);
 *
 * \endcode
 */
#ifndef __OVIS_EVENT_H
#define __OVIS_EVENT_H

#include <sys/epoll.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/queue.h>

typedef enum ovis_event_type_e {
	OVIS_EVENT_EPOLL          =  0x1,
	OVIS_EVENT_TIMEOUT        =  0x2,
	OVIS_EVENT_EPOLL_TIMEOUT  =  0x3,  /*  EPOLL|TIMEOUT  */
	OVIS_EVENT_PERIODIC       =  0x4,
	OVIS_EVENT_ONESHOT        =  0x8,
} ovis_event_type_t;

typedef struct ovis_event_s *ovis_event_t;
typedef struct ovis_scheduler_s *ovis_scheduler_t;

typedef struct ovis_periodic_s {
	uint64_t period_us; /* period in microseconds */
	uint64_t phase_us; /* phase in microseconds */
} *ovis_periodic_t;

typedef union ovis_event_time_param_u {
	struct timeval timeout;
	struct ovis_periodic_s periodic;
} *ovis_event_time_param_t;

/**
 * callback interface for event notification.
 */
typedef void (*ovis_event_cb_fn)(ovis_event_t ev);

/**
 * OVIS event structure.
 *
 * Please use one of the following functions to create an ovis_event.
 * - ::ovis_event_epoll_new()
 * - ::ovis_event_epoll_timeout_new()
 * - ::ovis_event_timeout_new()
 * - ::ovis_event_periodic_new()
 *
 * Otherwise, please use ::OVIS_EVENT_INITIALIZER or ::OVIS_EVENT_INIT to
 * initialize the event structure and set the \c .param accordingly afterward.
 */
struct ovis_event_s {
	/* application-supplied parameters */
	struct {
		ovis_event_type_t type;
		uint32_t epoll_events;
		int fd;
		ovis_event_cb_fn cb_fn;
		union {
			struct timeval timeout;
			struct ovis_periodic_s periodic;
		};
		void *ctxt;
	} param;

	/* callback information from ovis_scheduler to application */
	struct {
		ovis_event_type_t type; /* what causes the callback */
		uint32_t epoll_events; /* for event = OVIS_EVENT_EPOLL */
	} cb;

	/* private data for ovis_scheduler */
	struct {
		struct timeval tv;
		int idx;
		TAILQ_ENTRY(ovis_event_s) entry;
	} priv; /* private data for ovis_scheduler */
};

#define OVIS_EVENT_INITIALIZER {.param = {.fd=-1}, .priv = {.idx=-1},}

#define OVIS_EVENT_INIT(ev) do {\
	(ev)->param.fd = -1; \
	(ev)->priv.idx = -1; \
} while(0)

struct ovis_scheduler_thrstat {
	char *name;
	pid_t tid;
	uint64_t thread_id;
	uint64_t idle; /* Idle time in micro-seconds */
	uint64_t active; /* Active time in micro-seconds */
	uint64_t dur; /* Total time in micro-seconds */
	double idle_pc; /* Percentage of the idle time */
	double active_pc; /* Percentage of the active time */
	uint64_t ev_cnt; /* Number of events */
};

/**
 * Create an OVIS event scheduler.
 *
 * An OVIS event scheduler handles event registration and event dispatch. The
 * scheduler itself is a structure holding information. It does not create a new
 * thread. The application must call ::ovis_scheduler_loop() to run scheduler
 * routine. The function will block, and the caller thread is used for
 * ovis event scheduling.
 *
 * \retval m a handle to \c ovis_scheduler.
 * \retval NULL on failure. In this case, \c errno is also set to describe the
 *              error.
 */
ovis_scheduler_t ovis_scheduler_new();

/**
 * Destroy the unused event manager.
 *
 * \param m the ovis event manager handle. Ignores NULL input.
 */
void ovis_scheduler_free(ovis_scheduler_t m);

/**
 * Create a new epoll event.
 */
ovis_event_t ovis_event_epoll_new(ovis_event_cb_fn cb, void *ctxt,
				  int fd, uint32_t epoll_events);

/**
 * Create a new timeout event.
 */
ovis_event_t ovis_event_timeout_new(ovis_event_cb_fn cb, void *ctxt,
				    const struct timeval *tv);

/**
 * Create a new epoll with timeout event.
 */
ovis_event_t ovis_event_epoll_timeout_new(ovis_event_cb_fn cb, void *ctxt,
					  int fd, uint32_t epoll_events,
					  const struct timeval *tv);

/**
 * Create a periodic event.
 */
ovis_event_t ovis_event_periodic_new(ovis_event_cb_fn cb, void *ctxt,
				     const struct ovis_periodic_s *p);

/**
 * Add an event into the scheduler.
 *
 * \param s the ovis scheduler handle.
 * \param ev the ovis event to be added.
 *
 * \retval 0 if OK.
 * \retval errno if error.
 *
 * \note The scheduler owns the event after this call. The application should
 * not modify the event before removing it from the scheduler by calling \c
 * ovis_event_del().
 */
int ovis_scheduler_event_add(ovis_scheduler_t s, ovis_event_t ev);

/**
 * Remove an event \c ev from the scheduler.
 *
 * \param s the scheduler handle.
 * \param ev the event handle.
 *
 * \retval 0 if OK.
 * \retval errno if error.
 */
int ovis_scheduler_event_del(ovis_scheduler_t m, ovis_event_t ev);

/**
 * Modify epoll events of an ovis event \p ev in the scheduler \p s.
 *
 * \retval 0 if success.
 * \retval errno if error.
 */
int ovis_scheduler_epoll_event_mod(ovis_scheduler_t s, ovis_event_t ev,
				   int epoll_events);

/**
 * Free memory allocated from ::ovis_event_create().
 *
 * \param ev the event handle to be freed. Ignores NULL input.
 */
void ovis_event_free(ovis_event_t ev);

/**
 * Event loop function for the scheduler \c m to manage and deliver events.
 * \param s the scheduler handle.
 * \param return_on_empty return if event queue is empty.
 *
 * \retval ENOENT if event loop terminated on empty queue.
 * \retval EINTR if the event loop terminated by ::ovis_event_term().
 */
int ovis_scheduler_loop(ovis_scheduler_t m, int return_on_empty);

/**
 * Singal the scheduler to terminate the event loop.
 */
int ovis_scheduler_term(ovis_scheduler_t s);

/**
 * Set the name of the scheduler.
 *
 * This name will also be in the result of the thrstat.
 *
 * \retval 0 If success.
 * \retval errno If error.
 */
int ovis_scheduler_name_set(ovis_scheduler_t s, const char *name);

/**
 * Get the name of the scheduler.
 *
 * \retval NULL If the name has never set.
 * \retval name The name of the scheduler.
 */
const char *ovis_scheduler_name_get(ovis_scheduler_t s);

/**
 * \brief Return the thread statistics of a scheduler
 *
 * The caller must free the returned statistics.
 *
 * \param  sch   a handle of an ovis_schedule structure
 * \param  now   The current time
 *
 * \return A pointer to an ovis_scheduler_thrstat structure
 * \see ovis_scheduler_thrstat_free()
 */
struct ovis_scheduler_thrstat *
ovis_scheduler_thrstat_get(ovis_scheduler_t sch, struct timespec *now);

/**
 * \brief Free an ovis_scheduler_thrstat returned by ovis_scheduler_thrstat_get
 */
void ovis_scheduler_thrstat_free(struct ovis_scheduler_thrstat *res);

#endif
