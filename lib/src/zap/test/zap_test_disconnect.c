/**
 * Copyright (c) 2013-2015,2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2015,2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file zap_test_disconnect.c
 * \brief Zap test for simultaneous disconnect.
 */

/**
 * \page zap_test_disconnect zap_test_disconnect
 *
 * \section summary SUMMARY
 *
 * Zap test for simultaneous disconnect.
 *
 * \section synopsis SYNOPSIS
 *
 * \code{.sh}
 * # Server #
 * zap_test_disconnect -x XPRT -p PORT -s [-w TIME]
 *
 *
 * # Client #
 * zap_test_disconnect -x XPRT -p PORT -h HOST [-w TIME]
 *
 * \endcode
 *
 * \section desc DESCRIPTION
 *
 * This test program, like other zap_test*, can run in two modes: server and
 * client. The test scheme is the following:
 *
 *   - server program starts, listening to a port
 *   - client program starts, connecting to the server
 *   - both client and server exchange a message
 *   - after a given time in seconds has passed (with '-w' option), the server
 *     and the client both call zap_close() -- trying to create a simultaneous
 *     close event.
 *   - after receiving a disconnect event from zap, both client and server wait
 *     for additional 1 second then exit.
 */

#include <unistd.h>
#include <inttypes.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/wait.h>
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
#include <semaphore.h>

#include "zap.h"
#include "zap_priv.h"

#define LOG(FMT, ...) printf(FMT, ##__VA_ARGS__)

const char *short_opt = "x:h:p:sw:?";
const struct option long_opt[] = {
	{"xprt",    1,  0,  'x'},
	{"host",    1,  0,  'h'},
	{"port",    1,  0,  'p'},
	{"server",  0,  0,  's'},
	{"wait",    1,  0,  'w'},
	{"help",    0,  0,  '?'},
	{0,         0,  0,  0}
};

const char *xprt = "sock";
uint16_t port = 55555;
const char *host = "localhost";
int server_mode = 0;
int wtime = 1;

void show_usage()
{
	printf(
"USAGE: zap_test_disconnect [-x XPRT] [-h HOST] [-p PORT] [-s] [-w TIME]\n"
"\n"
"OPTIONS:\n"
"	-x XPRT		Transport (default: %s)\n"
"	-h HOST		Host to connect to (client mode, default: %s)\n"
"	-p PORT		Port to listen/connect (default: %d)\n"
"	-w TIME		Wait time before close (default: %d)\n"
"	-s		Server mode flag\n",
xprt, host, port, wtime
	      );
}

void arg_handling(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case 'x':
		xprt = optarg;
		break;
	case 'h':
		host = optarg;
		break;
	case 'p':
		port = atoi(optarg);
		break;
	case 's':
		server_mode = 1;
		break;
	case 'w':
		wtime = atoi(optarg);
		break;
	case -1:
		/* no more arg */
		goto out;
	case '?':
	default:
		show_usage();
		exit(-1);
	}
	goto loop;
out:
	return;
}

char *ev_str[] = {
	[ZAP_EVENT_CONNECT_REQUEST]  =  "CONNECT_REQUEST",
	[ZAP_EVENT_CONNECT_ERROR]    =  "CONNECT_ERROR",
	[ZAP_EVENT_CONNECTED]        =  "CONNECTED",
	[ZAP_EVENT_REJECTED]         =  "REJECTED",
	[ZAP_EVENT_DISCONNECTED]     =  "DISCONNECTED",
	[ZAP_EVENT_RECV_COMPLETE]    =  "RECV_COMPLETE",
	[ZAP_EVENT_READ_COMPLETE]    =  "READ_COMPLETE",
	[ZAP_EVENT_WRITE_COMPLETE]   =  "WRITE_COMPLETE",
	[ZAP_EVENT_RENDEZVOUS]       =  "RENDEZVOUS",
};

char *state_str[] = {
	[ZAP_EP_INIT]        =  "ZAP_EP_INIT",
	[ZAP_EP_LISTENING]   =  "ZAP_EP_LISTENING",
	[ZAP_EP_CONNECTING]  =  "ZAP_EP_CONNECTING",
	[ZAP_EP_CONNECTED]   =  "ZAP_EP_CONNECTED",
	[ZAP_EP_PEER_CLOSE]  =  "ZAP_EP_PEER_CLOSE",
	[ZAP_EP_CLOSE]       =  "ZAP_EP_CLOSE",
	[ZAP_EP_ERROR]       =  "ZAP_EP_ERROR",
};

pid_t master_pid, worker_pid[2];
zap_t zap = NULL;
zap_ep_t listen_ep = NULL;
zap_ep_t ep = NULL;

sem_t _sem;
sem_t *sem = &_sem;

static zap_mem_info_t mem_fn(void)
{
	return NULL;
}

static void init_zap(zap_ep_t *epp, zap_cb_fn_t cb)
{
	zap_err_t zerr;
	zap_ep_t ep;
	zap = zap_get(xprt, mem_fn);
	if (!zap) {
		zerr = errno;
		LOG("zap_get err %d: %s\n", zerr, zap_err_str(zerr));
		exit(-1);
	}
	ep = zap_new(zap, cb);
	if (!epp) {
		zerr = errno;
		LOG("zap_new err %d: %s\n", zerr, zap_err_str(zerr));
		exit(-1);
	}
	*epp = ep;

	int rc = sem_init(sem, 0, 0);
	if (rc) {
		LOG("sem_init error: %m\n");
		exit(-1);
	}
}

static void mutual_cb(zap_ep_t _ep, zap_event_t ev)
{
	zap_err_t zerr;
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		LOG("Accepting connection ...\n");
		ep = _ep;
		zerr = zap_accept(_ep, mutual_cb, (void *)ev->data, ev->data_len);
		if (zerr) {
			LOG("zap_accept error %d: %s\n", zerr,
							zap_err_str(zerr));
			exit(-1);
		}
		break;
	case ZAP_EVENT_CONNECTED:
		LOG("Connected\n");
		sem_post(sem);
		break;
	case ZAP_EVENT_DISCONNECTED:
		LOG("ZAP_EVENT_DISCONNECTED\n");
		if (_ep->state == ZAP_EP_PEER_CLOSE)
			LOG("Passively Disconnected %p\n", _ep);
		if (_ep->state == ZAP_EP_CLOSE)
			LOG("Actively Disconnected %p\n", _ep);
		sem_post(sem);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		LOG("recv: %s\n", (char*)ev->data);
		break;
	default:
		LOG("Unhandled zap event: %s\n", ev_str[ev->type]);
	}
}

static void do_mutual()
{
	/* wait for connected event */
	sem_wait(sem);
	LOG("sending: abc\n");
	zap_err_t zerr = zap_send(ep, "abc", 4);

	if (zerr) {
		LOG("zap_send err %d: %s\n", zerr, zap_err_str(zerr));
	}
	sleep(wtime);
	zap_close(ep);

	/* wait for close completion */
	sem_wait(sem);
	zap_free(ep);
}

static void do_server()
{
	zap_err_t zerr;

	init_zap(&listen_ep, mutual_cb);
	struct sockaddr_in sin = {0};
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	zerr = zap_listen(listen_ep, (void*)&sin, sizeof(sin));
	if (zerr) {
		LOG("zap_listen error %d: %s\n", zerr, zap_err_str(zerr));
		exit(-1);
	}

	LOG("listening ...\n");

	do_mutual();

	LOG("closing listening ep ...\n");
	zap_free(listen_ep);
}

static void do_client()
{
	struct addrinfo *ai;
	char _port[8];
	zap_err_t zerr;

	init_zap(&ep, mutual_cb);
	sprintf(_port, "%d", port);
	int rc = getaddrinfo(host, _port, NULL, &ai);
	if (rc) {
		LOG("getaddrinfo error %d: %s\n", rc, gai_strerror(rc));
		exit(-1);
	}

	LOG("connecting ...\n");
	zerr = zap_connect(ep, ai->ai_addr, ai->ai_addrlen, NULL, 0);
	if (zerr) {
		LOG("zap_connect error %d: %s\n", zerr, zap_err_str(zerr));
		exit(-1);
	}

	freeaddrinfo(ai);

	do_mutual();
}

int main(int argc, char **argv)
{
	arg_handling(argc, argv);

	if (server_mode)
		do_server();
	else
		do_client();
	sleep(1);
	LOG("exit.\n");
	return 0;
}
