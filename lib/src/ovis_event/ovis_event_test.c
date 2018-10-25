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

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

#include "ovis_event.h"

const char msg[] = "Hello.";
const char *name = "HUHA";

static
int test_log(const char *fmt, ...)
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	static char buff[4096];
	int len;
	int alloc_len;
	struct timeval tv;
	struct tm tm;
	va_list ap;

	pthread_mutex_lock(&mutex);
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	alloc_len = 4096;
	len = snprintf(buff, alloc_len,
			"%d-%02d-%02d %02d:%02d:%02d.%06ld %s: ",
			tm.tm_year + 1900,
			tm.tm_mon + 1,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec,
			tv.tv_usec,
			name);
	alloc_len -= len;
	va_start(ap, fmt);
	len += vsnprintf(buff + len, alloc_len, fmt, ap);
	va_end(ap);
	if (-1 == write(0, buff, len)) { /* do nothing */ }
	pthread_mutex_unlock(&mutex);
	return len;
}

struct context {
	int fd;
	int count;
};

char buff[4096];
void reader_cb(ovis_event_t ev)
{
	int rc;
	struct context *ctxt = ev->param.ctxt;
	int fd = ctxt->fd;
	ctxt->count++;
	switch (ev->cb.type) {
	case OVIS_EVENT_EPOLL:
		assert(ev->cb.epoll_events & EPOLLIN);
		rc = read(fd, buff, sizeof(buff));
		assert(rc == sizeof(msg));
		test_log("(%d) receiving '%s'\n", ctxt->count, buff);
		break;
	case OVIS_EVENT_TIMEOUT:
		test_log("(%d) timeout event.\n", ctxt->count);
		break;
	default:
		assert(0 == "Bad event type");
	}
}

void *terminate_loop(void *arg)
{
	ovis_scheduler_t m = arg;
	sleep(10);
	test_log("terminating ...\n");
	ovis_scheduler_term(m);
	return NULL;
}

void reader_routine(int fd)
{
	pthread_t thread;
	struct timeval tv = {1, 0};
	struct context ctxt = {fd, 0};
	int rc;
	ovis_scheduler_t m = ovis_scheduler_new();
	assert(m);
	pthread_create(&thread, NULL, terminate_loop, m);

	ovis_event_t ev = ovis_event_epoll_timeout_new(reader_cb, &ctxt,
						       fd, EPOLLIN, &tv);
	assert(ev);
	rc = ovis_scheduler_event_add(m, ev);
	assert(rc == 0);
	rc = ovis_scheduler_loop(m, 0);
	test_log("terminated\n");
	pthread_join(thread, NULL);
}

void writer_cb(ovis_event_t ev)
{
	struct context *ctxt = ev->param.ctxt;
	int fd = ctxt->fd;
	int rc;
	ctxt->count++;
	test_log("(%d) writing '%s' ...\n", ctxt->count, msg);
	rc = write(fd, msg, sizeof(msg));
	assert(rc == sizeof(msg));
}

void writer_routine(int fd)
{
	pthread_t thread;
	struct timeval tv;
	struct context ctxt = {fd, 0};
	int rc = 0;
	ovis_scheduler_t m = ovis_scheduler_new();
	assert(m);
	pthread_create(&thread, NULL, terminate_loop, m);

	tv.tv_sec = 3;
	tv.tv_usec = 0;

	struct ovis_event_s _ev = OVIS_EVENT_INITIALIZER;
	_ev.param.type = OVIS_EVENT_TIMEOUT;
	_ev.param.cb_fn = writer_cb;
	_ev.param.ctxt = &ctxt;
	_ev.param.timeout.tv_sec = 3;
	_ev.param.timeout.tv_usec = 0;

	test_log("adding write event\n");
	rc = ovis_scheduler_event_add(m, &_ev);
	assert(rc == 0);
	test_log("write event added\n");
	rc = ovis_scheduler_loop(m, 0);
	test_log("terminated\n");
	pthread_join(thread, NULL);
}

int main(int argc, char **argv)
{
	pid_t pid;
	int rc;
	int pfd[2]; /* pfd[0] for read, pfd[1] for write */
	rc = pipe(pfd);
	assert(rc == 0);

	pid = fork();
	if (pid == 0) {
		close(pfd[0]);
		name = "writer";
		writer_routine(pfd[1]);
	} else {
		close(pfd[1]);
		name = "reader";
		reader_routine(pfd[0]);
	}
	return 0;
}
