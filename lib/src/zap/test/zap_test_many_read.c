/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2021 Open Grid Computing, Inc. All rights reserved.
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
 * \file zap_test_many_read.c
 *
 * Test multiple zap reads.
 *
 * To test, run two processes of the test program as follows:
 * ```
 * # run server with optionally NUM_SETS number of sets (memory regions) for
 * # reading. The default NUM_SETS is 1024.
 * $ zap_test_many_read -h HOST -p PORT -x XPRT [-n NUM_SETS] -s
 *
 * # run client. The NUM_SETS must be the same as that of the server.
 * $ zap_test_many_read -h HOST -p PORT -x XPRT [-n NUM_SETS]
 * ```
 *
 * Here's the scenario:
 * - The server allocate memory regions for the sets and listen on the specified
 *   port.
 * - The server periodically update sets.
 * - The client send a message, asking the server to zap_share the sets.
 * - The server zap_share its sets.
 * - The client receives RENDEZVOUS events.
 * - The client periodically zap_read sets from the server.
 *
 */
#include <unistd.h>
#include <sys/syscall.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/un.h>
#include <ctype.h>
#include <netdb.h>
#include <dlfcn.h>
#include <assert.h>
#include <libgen.h>
#include <time.h>
#include "zap.h"

#define NUM_METRICS 128

#ifdef NDEBUG
#define ASSERT(COND) do { \
	if (COND) \
		break; \
	printf("assert(" #COND ") failed.\n"); \
	exit(-1); \
} while (0)
#else
#define ASSERT(COND) assert(COND)
#endif

void test_log(const char *fmt, ...)
{
	static pthread_mutex_t _mtx = PTHREAD_MUTEX_INITIALIZER;
	va_list ap;
	char buff[4096];
	struct timespec _t;
	pid_t _tid = syscall(SYS_gettid);

	pthread_mutex_lock(&_mtx);
	va_start(ap, fmt);
	vsnprintf(buff, sizeof(buff), fmt, ap);
	clock_gettime(CLOCK_REALTIME, &_t);
	printf("[%ld.%09ld] (%d) %.*s", _t.tv_sec, _t.tv_nsec, _tid, (int)sizeof(buff), buff);
	pthread_mutex_unlock(&_mtx);
}

#define LOG(FMT, ...) do { \
	test_log( "%s:%d " FMT, __func__, __LINE__,  ## __VA_ARGS__); \
} while (0)

#pragma pack(push, 4)
struct set {
	char   name[64];
	struct timespec ts;
	int    consistent;
	int    metrics[NUM_METRICS];
};

enum msg_type {
	DIR_REQ,
	DIR_REP,
};

/* base of all messages */
struct msg {
	enum msg_type type;
};

struct msg_dir_req {
	struct msg msg;
	int    num_sets;
};

struct msg_dir_rep {
	struct msg  msg;
	int         idx; /* set index */
	void        *addr;
	int         len;
};

typedef union msg_u {
	struct msg         msg;
	struct msg_dir_req dir_req;
	struct msg_dir_rep dir_rep;
} *msg_t;

struct remote_set_desc {
	zap_map_t map;
	void      *addr;
	int       len;
};

#pragma pack(pop)

zap_t zap;
struct set *sets;
zap_map_t  *lmaps;
struct remote_set_desc *rsets;

int num_sets = 1024;
struct zap_mem_info meminfo;
int completions = 0;
pthread_cond_t cond;

pthread_mutex_t mutex;

const char *xprt = NULL;
const char *host = NULL;

void on_server_recv(zap_ep_t ep, zap_event_t ev)
{
	zap_err_t err;
	int i;
	struct msg_dir_rep rep = { .msg.type = DIR_REP };
	msg_t msg = (void*)ev->data;
	/* expecting only DIR_REQ */
	ASSERT( ev->data_len == sizeof(msg->dir_req) );
	ASSERT( msg->msg.type == DIR_REQ );
	ASSERT( msg->dir_req.num_sets == num_sets );
	for (i = 0; i < num_sets; i++) {
		rep.idx = i;
		rep.addr = &sets[i];
		rep.len  = sizeof(sets[i]);
		err = zap_share(ep, lmaps[i], (void*)&rep, sizeof(rep));
		ASSERT( err == ZAP_ERR_OK );
	}
}

void server_cb(zap_ep_t ep, zap_event_t ev)
{
	zap_err_t err;

	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		err = zap_accept(ep, server_cb, NULL, 0);
		ASSERT( err == ZAP_ERR_OK ); /* zap_accept */
		break;
	case ZAP_EVENT_CONNECTED:
		LOG("client connected (%p)\n", ep);
		break;
	case ZAP_EVENT_DISCONNECTED:
		LOG("client disconnected (%p)\n", ep);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		on_server_recv(ep, ev);
		break;
	case ZAP_EVENT_SEND_COMPLETE:
	case ZAP_EVENT_SEND_MAPPED_COMPLETE:
		if (ev->status) {
			LOG("%s completed with status %s\n",
				zap_event_str(ev->type),
				zap_err_str(ev->status));
			ASSERT(0); /* send completed with a status */
		}
		/* no-op */
		break;

	case ZAP_EVENT_RENDEZVOUS:
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_REJECTED:
	case ZAP_EVENT_READ_COMPLETE:
	case ZAP_EVENT_WRITE_COMPLETE:
		printf("Unexpected Zap event %s\n", zap_event_str(ev->type));
		ASSERT(0); /* unexpected event */
		break;
	default:
		LOG("Unhandled Zap event %s\n", zap_event_str(ev->type));
		ASSERT(0); /* unknown event */
	}
}

zap_mem_info_t test_meminfo(void)
{
	return &meminfo;
}

void do_server(zap_t zap, struct sockaddr_in *sin)
{
	zap_err_t err;
	zap_ep_t ep;
	int i, j;
	struct timespec ts;

	for (i = 0; i < num_sets; i++) {
		sets[i].consistent = 0;
		clock_gettime(CLOCK_REALTIME, &ts);
		sets[i].ts = ts;
		for (j = 0; j < NUM_METRICS; j++) {
			sets[i].metrics[j] = (int)ts.tv_sec + i + j;
		}
		sets[i].consistent = 1;
	}

	ep = zap_new(zap, server_cb);
	if (!ep) {
		LOG("Could not create the zap endpoint.\n");
		ASSERT(0); /* zap_new */
		return;
	}

	err = zap_listen(ep, (struct sockaddr *)sin, sizeof(*sin));
	if (err) {
		LOG("zap_listen failed.\n");
		ASSERT(0); /* zap_listen */
		zap_free(ep);
		return;
	}

 loop:
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec = 0;
	ts.tv_nsec = 1000000000 - ts.tv_nsec;
	ts.tv_sec  = ts.tv_nsec / 1000000000;
	ts.tv_nsec = ts.tv_nsec % 1000000000;
	nanosleep(&ts, NULL);
	for (i = 0; i < num_sets; i++) {
		sets[i].consistent = 0;
		clock_gettime(CLOCK_REALTIME, &ts);
		sets[i].ts = ts;
		for (j = 0; j < NUM_METRICS; j++) {
			sets[i].metrics[j] = (int)ts.tv_sec + i + j;
		}
		sets[i].consistent = 1;
	}
	goto loop;
}

void on_client_connected(zap_ep_t ep, zap_event_t ev)
{
	struct msg_dir_req req = { .msg.type = DIR_REQ, .num_sets = num_sets };
	zap_err_t err;
	err = zap_send(ep, (void*)&req, sizeof(req));
	ASSERT( err == ZAP_ERR_OK );
}

void verify_set(struct set *s)
{
	int i;
	int idx = ((void*)s - (void*)sets)/sizeof(sets[0]);
	if (!s->consistent)
		return ; /* skip verification if the set is not consistent */
	for (i = 0; i < NUM_METRICS; i++) {
		ASSERT(s->metrics[i] == s->ts.tv_sec + i + idx);
	}
}

void on_client_read_complete(zap_ep_t ep, zap_event_t ev)
{
	ASSERT( ev->status == ZAP_ERR_OK );
	verify_set(ev->context);
	pthread_mutex_lock(&mutex);
	completions++;
	ASSERT(completions <= num_sets);
	if (completions == num_sets)
		pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
}

void on_client_rendezvous(zap_ep_t ep, zap_event_t ev)
{
	struct msg_dir_rep *rep = (void*)ev->data;
	int i;
	ASSERT( ev->status == ZAP_ERR_OK );
	ASSERT( ev->data_len == sizeof(*rep) );
	ASSERT( rep->msg.type == DIR_REP );

	i = rep->idx;
	rsets[i].map  = ev->map;
	rsets[i].addr = rep->addr;
	rsets[i].len  = rep->len;

	pthread_mutex_lock(&mutex);
	completions++;
	ASSERT(completions <= num_sets);
	if (completions == num_sets)
		pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
}

void client_cb(zap_ep_t ep, zap_event_t ev)
{
	switch (ev->type) {
	case ZAP_EVENT_CONNECTED:
		on_client_connected(ep, ev);
		break;
	case ZAP_EVENT_SEND_MAPPED_COMPLETE:
	case ZAP_EVENT_SEND_COMPLETE:
		/* no-op */
		break;
	case ZAP_EVENT_DISCONNECTED:
		LOG("Disconnected\n");
		break;
	case ZAP_EVENT_READ_COMPLETE:
		on_client_read_complete(ep, ev);
		break;
	case ZAP_EVENT_RENDEZVOUS:
		on_client_rendezvous(ep, ev);
		break;

	case ZAP_EVENT_WRITE_COMPLETE:
	case ZAP_EVENT_RECV_COMPLETE:
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_REJECTED:
	case ZAP_EVENT_CONNECT_REQUEST:
		LOG("Unexpected Zap event %s\n", zap_event_str(ev->type));
		ASSERT(0);
		break;
	default:
		LOG("Unhandled Zap event %s\n", zap_event_str(ev->type));
		ASSERT(0);
	}
}

void do_client(zap_t zap, struct sockaddr_in *sin)
{
	zap_err_t err;
	zap_ep_t ep;
	struct timespec ts;
	int i, round;

	ep = zap_new(zap, client_cb);
	if (!ep) {
		LOG("Could not create the zap endpoing.\n");
		ASSERT(0); /* zap_new */
		return;
	}

	completions = 0;

	zap_set_ucontext(ep, sin);
	err = zap_connect(ep, (struct sockaddr *)sin, sizeof(*sin),
			  NULL, 0);
	if (err) {
		LOG("zap_connect failed.\n");
		ASSERT(0); /* zap_connect */
		return;
	}

	pthread_mutex_lock(&mutex);
	while (completions < num_sets) {
		pthread_cond_wait(&cond, &mutex);
	}
	LOG("Received %d rendezvous\n", completions);
	pthread_mutex_unlock(&mutex);
	round = 0;
 loop:
	/* Periodically update sets every second (+200ms offset) */
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec = 0;
	ts.tv_nsec = 1200000000 - ts.tv_nsec;
	ts.tv_sec  = ts.tv_nsec / 1000000000;
	ts.tv_nsec = ts.tv_nsec % 1000000000;
	nanosleep(&ts, NULL);
	pthread_mutex_lock(&mutex);
	completions = 0;
	for (i = 0; i < num_sets; i++) {
		err = zap_read(ep, rsets[i].map, rsets[i].addr,
				   lmaps[i], (void*)&sets[i], sizeof(sets[i]),
				   &sets[i]);
		ASSERT(err == ZAP_ERR_OK); /* zap_read */
	}
	while (completions < num_sets) {
		pthread_cond_wait(&cond, &mutex);
	}
	LOG("(round: %d) %d read completions\n", round, completions);
	pthread_mutex_unlock(&mutex);
	round++;
	goto loop;
}


int resolve(const char *hostname, struct sockaddr_in *sin)
{
	struct hostent *h;

	h = gethostbyname(hostname);
	if (!h) {
		printf("Error resolving hostname '%s'\n", hostname);
		return -1;
	}

	if (h->h_addrtype != AF_INET) {
		printf("Hostname '%s' resolved to an unsupported"
				" address family\n", hostname);
		return -1;
	}

	memset(sin, 0, sizeof *sin);
	sin->sin_addr.s_addr = *(unsigned int *)(h->h_addr_list[0]);
	sin->sin_family = h->h_addrtype;
	return 0;
}

#define FMT_ARGS "x:p:h:sn:"
void usage(int argc, char *argv[]) {
	printf("usage: %s -x name -p port_no -h host [-s] [-n NUM_SETS].\n"
	       "    -x name	The transport to use.\n"
	       "    -p port_no	The port number.\n"
	       "    -h host	The host name or IP address. Must be specified\n"
	       "		if this is the client.\n"
	       "    -s		This is a server.\n"
	       "    -n NUM_SETS	The number of sets (default: 1024).\n"
	       ,
	       argv[0]);
	exit(1);
}

int main(int argc, char *argv[])
{
	int rc;
	int is_server = 0;
	unsigned short port_no = 0;
	int i, ptmp = -1;
	struct sockaddr_in sin = {};
	zap_err_t err;

	setbuf(stdout, NULL);

	while (-1 != (rc = getopt(argc, argv, FMT_ARGS))) {
		switch (rc) {
		case 's':
			is_server = 1;
			break;
		case 'h':
			host = optarg;
			break;
		case 'x':
			xprt = optarg;
			break;
		case 'p':
			ptmp = atoi(optarg);
			if (ptmp > 0 && ptmp < USHRT_MAX) {
				port_no = ptmp;
			}
			break;
		case 'n':
			num_sets = atoi(optarg);
			break;
		default:
			usage(argc, argv);
			break;
		}
	}
	if (!xprt)
		usage(argc, argv);

	if (port_no == 0)
		usage(argc, argv);

	if (!is_server && !host)
		usage(argc, argv);

	memset(&sin, 0, sizeof sin);
	if (host) {
		if (resolve(host, &sin))
			usage(argc, argv);
	} else
		sin.sin_family = AF_INET;
	sin.sin_port = htons(port_no);

	lmaps = calloc(num_sets, sizeof(lmaps[0]));
	ASSERT(lmaps != NULL);
	rsets = calloc(num_sets, sizeof(rsets[0]));
	ASSERT(rsets != NULL);
	sets = calloc(num_sets, sizeof(sets[0]));
	ASSERT(sets != NULL);

	/* create local memory maps */
	for (i = 0; i < num_sets; i++) {
		err = zap_map(&lmaps[i], &sets[i], sizeof(sets[i]),
			      ZAP_ACCESS_READ|ZAP_ACCESS_WRITE);
		ASSERT( err == ZAP_ERR_OK ); /* zap_map */
	}

	meminfo.start = sets;
	meminfo.len = num_sets * sizeof(sets[0]);

	zap = zap_get(xprt, test_meminfo);
	if (!zap) {
		printf("%s: could not load the '%s' xprt.\n",
		       __func__, xprt);
		exit(1);
	}
	if (is_server)
		do_server(zap, &sin);
	else
		do_client(zap, &sin);

	/* This sleep is to ensure that we see the resources get cleaned
	 * properly */
	sleep(1);
	return 0;
}
