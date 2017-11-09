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
/**
 * \file ovis_event.h
 * \author Narate Taerat (narate at ogc dot us)
 */

/**
 * \page ovis_event ovis_event library
 *
 * \section name NAME
 *
 * ovis_event library - an epoll wrapper with timer event
 *
 * \section synopsis SYNOPSIS
 *
 * \code
 * #include <ovis_event/ovis_event.h>
 *
 * typedef void (*ovis_event_cb)(uint32_t events,
 * 			const struct timeval *tv, struct ovis_event *e);
 *
 * ovis_event_manager_t ovis_event_manager_create();
 * ovis_event_t ovis_event_create(int fd, uint32_t epoll_events,
 *			const union ovis_event_time_param_u *t, int flags,
 *			ovis_event_cb cb, void *ctxt);
 * int ovis_event_add(ovis_event_manager_t m, ovis_event_t ev);
 * int ovis_event_del(ovis_event_manager_t m, ovis_event_t ev);
 * int ovis_event_loop(struct ovis_event_manager *m, int return_on_empty);
 *
 * int ovis_event_get_fd(ovis_event_t e);
 * void *ovis_event_get_ctxt(ovis_event_t e);
 * \endcode
 *
 *
 * \section description DESCRIPTION
 *
 * ovis_event library is an epoll wrapper with timer event support. Each event
 * is delivered to the application via callback interface (see \c ovis_event_cb
 * definition above). There are two kinds of events to deliver to the
 * application: timer event and epoll event. A timer event is delivered when the
 * count-down timer reaches 0. An epoll event is delivered when the file
 * descriptor is ready for read or write.
 *
 * Application can create an event that is both timer and epoll. In other words,
 * the application will receive a notification when the timer run out or the
 * file descriptor is ready, whichever came first.
 *
 * A timer event is delivered only once. The application may decide to add it
 * again to re-arm the event. To avoid repeatedly adding a timer event, the
 * application may create a <i>persistent</i> timer event instead.
 *
 * If a timer event is <i>persistent</i>, the timer of the event is
 * automatically reset and re-added into the event queue.
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
 * void callback(uint32_t events, const struct timeval *tv, struct ovis_event *e)
 * {
 *     void *ctxt = ovis_event_get_ctxt(e);
 *     int fd = ovis_event_get_fd(e);
 *
 *     if (events == 0) {
 *         // timer event
 *     }
 *
 *     if (events & EPOLLIN) {
 *         // Input ready event
 *     }
 *
 *     if (events & EPOLLOUT) {
 *         // Output ready event
 *     }
 *
 *     if (events & EPOLLHUP) {
 *         // Hang-up event ...
 *     }
 *     ...
 * }
 * ...
 *
 * // code for create event manager and execute event loop (on one thread)
 * ovis_event_manager_t m;
 * m = ovis_event_manager_create();
 * ovis_event_loop(m, 0); // this will block
 * ...
 *
 * // code for create and add event (on another thread)
 * int fd; // some file descriptor
 * struct timeval timer = {20, 0};
 * ovis_event_t e;
 * void *my_ctxt; // some context of the event
 * e = ovis_event_create(fd, EPOLLIN, (void*)&timer, OVIS_EVENT_PERSISTENT,
 *                       callback, my_ctxt);
 * ovis_event_add(m, e);
 * // callback() function will be called every 20 seconds, or when fd is ready
 * // for read. timer is reset every time the callback is called.
 *
 * \endcode
 */
#ifndef __OVIS_EVENT_H
#define __OVIS_EVENT_H

#include <sys/epoll.h>
#include <stdint.h>
#include <sys/time.h>

#define  OVIS_EVENT_PERSISTENT  0x001
#define  OVIS_EVENT_TIMER       0x010
#define  OVIS_EVENT_PERIODIC    0x100

typedef struct ovis_event *ovis_event_t;
typedef struct ovis_event_manager *ovis_event_manager_t;

typedef struct ovis_periodic_s {
	uint64_t period_us; /* period in microseconds */
	uint64_t phase_us; /* phase in microseconds */
} *ovis_periodic_t;

typedef union ovis_event_time_param_u {
	struct timeval timer;
	struct ovis_periodic_s periodic;
} *ovis_event_time_param_t;

/**
 * callback interface for event notification.
 *
 * The registered callback function will be called to notify about the added
 * event. This callback function is invoked in two cases: epoll event and timer
 * event.
 *
 * In the case of epoll event, \c events value is set by \c epoll, and \c tv is
 * NULL.
 *
 * In the case of timer or periodic event, \c events is 0, and \c tv sets to
 * current time.
 *
 * \c e is the event assocated with the call for both cases.
 *
 * Application can use \c ovis_event_get_fd() and \c ovis_event_get_ctxt() to
 * obtain associated file descriptor and context accordingly.
 */
typedef void (*ovis_event_cb)(uint32_t events, const struct timeval *tv,
			      struct ovis_event *e);

/**
 * Create an ovis_event_manager.
 *
 * To run \c ovis_event_loop() and to add an event, a manager is need. This
 * function is for creating the event manager.
 *
 * \retval m a handle to \c ovis_event_manager.
 * \retval NULL on failure. In this case, \c errno is also set to describe the
 *              error.
 */
ovis_event_manager_t ovis_event_manager_create();

/**
 * Destroy the unused event manager.
 *
 * \param m the ovis event manager handle.
 */
void ovis_event_manager_free(ovis_event_manager_t m);

/**
 * Get event context (that was specified at the creation of the event).
 *
 * \param e the event handle.
 * \retval ctxt the event context.
 */
void *ovis_event_get_ctxt(ovis_event_t e);

/**
 * Get the file descriptor associated to the event \c e.
 *
 * \param e the event handle.
 * \retval fd the file descriptor.
 */
int ovis_event_get_fd(ovis_event_t e);

/**
 * Create an ovis event.
 *
 * \param fd the file descriptor associated to the event.
 * \param epoll_events epoll events to register for the file descriptor \c fd.
 *                     See <b>epoll_ctl(2)</b> for the details about epoll
 *                     events.
 * \param t the time parameter. If the flags has OVIS_EVENT_PERIODIC, the
 *          parameter will be accessed as periodic. Otherwise, the parameter
 *          is accessed as timeval which represents the timer.  This parameter
 *          can be \c NULL if the application wish to get only epoll event.
 * \param flags \c OVIS_EVENT_PERSISTENT for timer event to be persistent, i.e.
 *              the timer is reset and re-arm when an event is delivered.
 *              \c OVIS_EVENT_PERIODIC for creating periodic event. In this
 *              case, the parameter \c t is accessed as periodic.
 * \param cb the callback function for event notification.
 * \param ctxt the event context. application can obtabin this context later by
 *             calling \c ovis_event_get_ctxt().
 *
 * \retval e the event handle.
 * \retval NULL if error. \c errno is also set.
 */
ovis_event_t ovis_event_create(int fd, uint32_t epoll_events,
				const union ovis_event_time_param_u *t,
				int flags, ovis_event_cb cb, void *ctxt);

/**
 * Add an event into the event queue (managed by the manager \c m).
 *
 * \param m the ovis event manager handle.
 * \param ev the ovis event to be added.
 *
 * \retval 0 if OK.
 * \retval errno if error.
 *
 * \note When an event is in the queue, the manager owns the event \c ev. The
 * application should not modify \c ev before \c ev leaves the event queue. An
 * event \c ev leaves the event queue if:
 *   - it is a notifying non-persistent timer event in the callback function.
 *   - application directly call \c ovis_event_del() on the event \c ev.
 */
int ovis_event_add(ovis_event_manager_t m, ovis_event_t ev);

/**
 * Remove an event \c ev from the event queue.
 *
 * \param m the event manager handle.
 * \param ev the event handle.
 *
 * \retval 0 if OK.
 * \retval errno if error.
 */
int ovis_event_del(ovis_event_manager_t m, ovis_event_t ev);

/**
 * Free memory allocated from ::ovis_event_create().
 *
 * \param ev the event handle to be freed.
 */
void ovis_event_free(ovis_event_t ev);

/**
 * Event loop function for an event manager \c m to manage and deliver events.
 * \param m the event manager handle.
 * \param return_on_empty return if event queue is empty.
 *
 * \retval ENOENT if event loop terminated on empty queue.
 * \retval EINTR if the event loop terminated by ::ovis_event_term().
 */
int ovis_event_loop(ovis_event_manager_t m, int return_on_empty);

/**
 * Singal the manager to terminate the event loop.
 */
int ovis_event_term(ovis_event_manager_t m);

#endif
