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

int periodic = 1;

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
void reader_cb(uint32_t events, const struct timeval *tv, ovis_event_t ev)
{
	int rc;
	struct context *ctxt = ovis_event_get_ctxt(ev);
	int fd = ctxt->fd;
	ctxt->count++;
	if (events & EPOLLIN) {
		rc = read(fd, buff, sizeof(buff));
		assert(rc == sizeof(msg));
		test_log("(%d) receiving '%s'\n", ctxt->count, buff);
	} else {
		test_log("(%d) timeout event.\n", ctxt->count);
	}
}

void *terminate_loop(void *arg)
{
	ovis_event_manager_t m = arg;
	sleep(10);
	test_log("terminating ...\n");
	ovis_event_term(m);
	return NULL;
}

void reader_routine(int fd)
{
	pthread_t thread;
	union ovis_event_time_param_u tp;
	struct context ctxt = {fd, 0};
	int rc;
	int flags;
	ovis_event_manager_t m = ovis_event_manager_create();
	assert(m);
	pthread_create(&thread, NULL, terminate_loop, m);

	if (periodic) {
		tp.periodic.period_us = 1000000;
		tp.periodic.phase_us = 0;
		flags = OVIS_EVENT_PERIODIC;
	} else {
		tp.timer.tv_sec = 1;
		tp.timer.tv_usec = 0;
		flags = OVIS_EVENT_TIMER|OVIS_EVENT_PERSISTENT;
	}

	ovis_event_t ev = ovis_event_create(fd, EPOLLIN, &tp, flags,
					    reader_cb, &ctxt);
	assert(ev);
	rc = ovis_event_add(m, ev);
	assert(rc == 0);
	rc = ovis_event_loop(m, 0);
	test_log("terminated\n");
	pthread_join(thread, NULL);
}

void writer_cb(uint32_t events, const struct timeval *tv, ovis_event_t ev)
{
	struct context *ctxt = ovis_event_get_ctxt(ev);
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
	union ovis_event_time_param_u tp;
	struct context ctxt = {fd, 0};
	int rc = 0;
	int flags;
	ovis_event_manager_t m = ovis_event_manager_create();
	assert(m);
	pthread_create(&thread, NULL, terminate_loop, m);

	if (periodic) {
		tp.periodic.period_us = 3000000;
		tp.periodic.phase_us = 0;
		flags = OVIS_EVENT_PERIODIC;
	} else {
		tp.timer.tv_sec = 3;
		tp.timer.tv_usec = 0;
		flags = OVIS_EVENT_TIMER|OVIS_EVENT_PERSISTENT;
	}

	ovis_event_t ev = ovis_event_create(-1, 0, &tp, flags,
					    writer_cb, &ctxt);
	assert(ev);
	test_log("adding write event\n");
	rc = ovis_event_add(m, ev);
	assert(rc == 0);
	test_log("write event added\n");
	rc = ovis_event_loop(m, 0);
	test_log("terminated\n");
	pthread_join(thread, NULL);
}

int main(int argc, char **argv)
{
	pid_t pid;
	const char *use_timer;
	int rc;
	int pfd[2]; /* pfd[0] for read, pfd[1] for write */
	rc = pipe(pfd);
	assert(rc == 0);

	use_timer = getenv("USE_TIMER");
	if (use_timer)
		periodic = 0;
	printf("periodic: %d\n", periodic);

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
