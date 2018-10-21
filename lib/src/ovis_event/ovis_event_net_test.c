/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017 Open Grid Computing, Inc. All rights reserved.
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
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <getopt.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "ovis_event.h"

const char *short_opts = "APh:p:?";

struct option long_opts[] = {
	{"active",   0,  0,  'A'},
	{"passive",  0,  0,  'P'},
	{"host",     1,  0,  'h'},
	{"port",     1,  0,  'p'},
	{0,          0,  0,  0}
};

const char *host = "localhost";
const char *port = "12345";
int is_active = 0; /* is active-side */

ovis_scheduler_t os;

const char msg[] = "Hello world!";

__attribute__((format(printf, 1, 2)))
void logger(const char *fmt, ...)
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	va_list ap;
	char ts[128];
	struct timeval tv;
	struct tm tm;
	pthread_mutex_lock(&mutex);
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	strftime(ts, sizeof(ts), "%F %T", &tm);
	printf("%s.%06ld ", ts, tv.tv_usec);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	pthread_mutex_unlock(&mutex);
}

int __epoll_event[] = {
	EPOLLIN,
	EPOLLPRI,
	EPOLLOUT,
	EPOLLRDNORM,
	EPOLLRDBAND,
	EPOLLWRNORM,
	EPOLLWRBAND,
	EPOLLMSG,
	EPOLLERR,
	EPOLLHUP,
	EPOLLRDHUP,
	EPOLLWAKEUP,
	EPOLLONESHOT,
	EPOLLET,
};

const char *__epoll_event_str[] = {
	"EPOLLIN",
	"EPOLLPRI",
	"EPOLLOUT",
	"EPOLLRDNORM",
	"EPOLLRDBAND",
	"EPOLLWRNORM",
	"EPOLLWRBAND",
	"EPOLLMSG",
	"EPOLLERR",
	"EPOLLHUP",
	"EPOLLRDHUP",
	"EPOLLWAKEUP",
	"EPOLLONESHOT",
	"EPOLLET",
};

int __epollstr(int epoll_events, char *buff, size_t bufsz)
{
	size_t len, alen, slen;
	int i, n;
	const char *limit = "";
	n = sizeof(__epoll_event) / sizeof(__epoll_event[0]);
	slen = len = 0;
	alen = bufsz;
	for (i = 0; i < n; i++) {
		if ((epoll_events & __epoll_event[i]) == 0)
			continue;
		len = snprintf(buff + slen, alen, "%s%s",
				limit, __epoll_event_str[i]);
		limit = "|";
		if (len >= alen)
			return ENOMEM;
		slen += len;
		alen -= len;
	}
	return 0;
}

void usage()
{
	printf("\
USAGE: ovis_event_net_test (-A|-P) [-h HOST] [-p PORT]\n\
\n\
OPTIONS:\n\
	-A,--active	Run the active-side routine\n\
	-P,--passive	Run the passive-side routine (this is the default).\n\
	-h,--host HOST	Specify host address (default: localhost).\n\
	-p,--port PORT	Specify the port (default: 12345).\n\
\n\
DESCRIPTION:\n\
	This test program needs two instances to complete the test. One \n\
	being PASSIVE side, and the other being ACTIVE side.\n\
\n\
EXAMPLE:\n\
	$ ovis_event_net_test -P -h localhost -p 12345 # This will block.\n\
	# \n\
	# On another terminal \n\
	$ ovis_event_net_test -A -h localhost -p 12345\n\
	");
}

void handle_args(int argc, char **argv)
{
	int c;
	c = getopt_long(argc, argv, short_opts, long_opts, NULL);
	switch (c) {
	case -1:
		goto out;
	case 'P':
		is_active = 0;
		break;
	case 'A':
		is_active = 1;
		break;
	case 'h':
		host = optarg;
		break;
	case 'p':
		port = optarg;
		break;
	case '?':
	default:
		usage();
		exit(-1);
	}
out:
	/* do nothing */;
}

void passive_cb(ovis_event_t ev)
{
	char buff[4096];
	int rc, len;

	assert(ev->cb.type == OVIS_EVENT_EPOLL);
	assert(ev->cb.epoll_events & EPOLLIN);

	rc = __epollstr(ev->cb.epoll_events, buff, sizeof(buff));
	logger("PASSIVE epoll_events: %#x, %s\n", ev->cb.epoll_events, buff);

	len = read(ev->param.fd, buff, sizeof(buff));
	if (len > 0) {
		/* read event, echo back */
		logger("PASSIVE read '%s'\n", buff);
		assert(strcmp(buff, msg) == 0);
		logger("PASSIVE echoing back '%s'\n", buff);
		write(ev->param.fd, buff, len);
	} else if (len == 0) {
		/* peer hang up */
		logger("PASSIVE peer hung up.\n");
		ovis_scheduler_event_del(os, ev);
		close(ev->param.fd);
		ovis_event_free(ev);
	} else {
		assert(0 == "read error");
	}
}

void listen_cb(ovis_event_t ev)
{
	ovis_event_t new_ev;
	int rc, sd;
	struct sockaddr addr;
	char ebuff[4096];
	socklen_t addrlen;

	assert(ev->cb.type == OVIS_EVENT_EPOLL);
	assert(ev->cb.epoll_events & EPOLLIN);

	rc = __epollstr(ev->cb.epoll_events, ebuff, sizeof(ebuff));
	logger("listen_cb epoll_events: %#x, %s\n", ev->cb.epoll_events, ebuff);

	logger("accepting new connection.\n");
	sd = accept(ev->param.fd, &addr, &addrlen);
	assert(sd >= 0);

	new_ev = ovis_event_epoll_new(passive_cb, NULL, sd, EPOLLIN);
	assert(new_ev);

	rc = ovis_scheduler_event_add(os, new_ev);
	assert(rc == 0);
}

void handle_SIGINT(int sig)
{
	ovis_scheduler_term(os);
}

void passive_proc(const struct sockaddr *addr, int addrlen)
{
	int rc;
	ovis_event_t listen_ev;
	int sd;
	struct sigaction sact = {
		.sa_handler = handle_SIGINT,
	};

	logger("Press Ctrl-C to stop the process\n");

	logger("entering passive_proc().\n");

	rc = sigaction(SIGINT, &sact, NULL);
	assert(rc == 0);

	sd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
	assert(sd >= 0);
	rc = bind(sd, addr, addrlen);
	assert(rc == 0);
	listen_ev = ovis_event_epoll_new(listen_cb, NULL, sd, EPOLLIN);
	assert(listen_ev);
	rc = listen(sd, 16);
	assert(rc == 0);
	rc = ovis_scheduler_event_add(os, listen_ev);
	assert(rc == 0);

	logger("exiting passive_proc(), done setting up.\n");
}

void active_cleanup(ovis_event_t ev)
{
	int rc;
	logger("ACTIVE cleaning up\n");
	rc = ovis_scheduler_event_del(os, ev);
	assert(rc == 0);
	close(ev->param.fd);
	ovis_event_free(ev);
	/* also terminate the scheduler */
	rc = ovis_scheduler_term(os);
	assert(rc == 0);
}

void active_cb(ovis_event_t ev)
{
	char ebuff[4096];
	char buff[128] = "";
	int len, rc, sock_rc;
	socklen_t socklen;

	assert(ev->cb.type == OVIS_EVENT_EPOLL);

	rc = __epollstr(ev->cb.epoll_events, ebuff, sizeof(ebuff));
	assert(rc == 0);

	logger("ACTIVE epoll_events: %#x, %s\n", ev->cb.epoll_events, ebuff);

	if (ev->cb.epoll_events & EPOLLERR) {
		socklen = sizeof(sock_rc);
		rc = getsockopt(ev->param.fd, SOL_SOCKET, SO_ERROR,
				&sock_rc, &socklen);
		assert(rc == 0);
		logger("ACTIVE EPOLLERR, rc: %d\n", sock_rc);
		active_cleanup(ev);
		return;
	}

	if (ev->cb.epoll_events & EPOLLOUT) {
		/* we're now connected ... now remove the EPOLLOUT */
		rc = ovis_scheduler_event_del(os, ev);
		assert(rc == 0);
		ev->param.epoll_events = EPOLLIN;
		rc = ovis_scheduler_event_add(os, ev);
		assert(rc == 0);
		logger("ACTIVE EPOLLOUT event\n");
		logger("ACTIVE writing '%s'\n", msg);
		write(ev->param.fd, msg, sizeof(msg));
	}

	if (ev->cb.epoll_events & EPOLLIN) {
		logger("ACTIVE EPOLLIN event\n");
		len = read(ev->param.fd, buff, sizeof(buff));
		logger("ACTIVE received: '%s'\n", buff);
		assert(strcmp(msg, buff) == 0);
		active_cleanup(ev);
	}
}

void active_proc(const struct sockaddr *addr, int addrlen)
{
	int rc;
	int sd;
	ovis_event_t ev;

	logger("entering active_proc()\n");

	sd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
	assert(sd >= 0);

	ev = ovis_event_epoll_new(active_cb, NULL, sd, EPOLLIN|EPOLLOUT);
	assert(ev);

	rc = connect(sd, addr, addrlen);
	assert(rc == 0 || errno == EINPROGRESS);

	rc = ovis_scheduler_event_add(os, ev);
	assert(rc == 0);

	logger("exiting active_proc(), done setting up\n");
}

void *event_proc(void *arg)
{
	int rc;
	sigset_t ss;

	/* Mask all signals */
	sigfillset(&ss);
	sigprocmask(SIG_SETMASK, &ss, NULL);

	logger("ovis_scheduler_loop START\n");
	rc = ovis_scheduler_loop(os, 0);
	logger("ovis_scheduler_loop END, rc: %d\n", rc);
	return NULL;
}

int main(int argc, char **argv)
{
	int rc;
	struct addrinfo hint = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *ai;
	pthread_t thr;

	handle_args(argc, argv);

	os = ovis_scheduler_new();
	assert(os);
	pthread_create(&thr, NULL, event_proc, NULL);

	rc = getaddrinfo(host, port, &hint, &ai);
	assert(rc == 0);
	if (is_active) {
		active_proc(ai->ai_addr, ai->ai_addrlen);
	} else {
		passive_proc(ai->ai_addr, ai->ai_addrlen);
	}
	freeaddrinfo(ai);

	pthread_join(thr, NULL);

	ovis_scheduler_free(os);
	return 0;
}
