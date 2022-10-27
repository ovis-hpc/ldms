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
/**
 * \file zap_test_big.c
 *
 * \brief Zap big read and big write tests.
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
#include "zap.h"

static int done = 0;
pthread_mutex_t done_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t done_cv = PTHREAD_COND_INITIALIZER;
zap_t zap;

char *dare = "read/write dare";

char *ev_str[] = {
	[ZAP_EVENT_CONNECT_REQUEST] = "CONNECT_REQUEST",
	[ZAP_EVENT_CONNECT_ERROR] = "CONNECT_ERROR",
	[ZAP_EVENT_CONNECTED] = "CONNECTED",
	[ZAP_EVENT_REJECTED] = "REJECTED",
	[ZAP_EVENT_DISCONNECTED] = "DISCONNECTED",
	[ZAP_EVENT_RECV_COMPLETE] = "RECV_COMPLETE",
	[ZAP_EVENT_READ_COMPLETE] = "READ_COMPLETE",
	[ZAP_EVENT_WRITE_COMPLETE] = "WRITE_COMPLETE",
	[ZAP_EVENT_RENDEZVOUS] = "RENDEZVOUS",
};

#define DEFAULT_WSIZE (1023*1024)

int WSIZE = DEFAULT_WSIZE;
char fill_ch = 0xcc; /* character to fill the memory for testing */
char *mem0;
char *mem1;
zap_map_t map0 = NULL; /* map for mem0 */
zap_map_t map1 = NULL; /* map for mem1 */
zap_map_t remote_map = NULL; /* remote memory mapping */

/* Data src/sink for RDMA_WRITE */
char write_buf[1024];

/* Data src/sink for RDMA_READ */
char read_buf[1024];

#define CONN_DATA "Hello, world!"
#define ACCEPT_DATA "Accepted!"
#define REJECT_DATA "Rejected ... try again"

void server_cb(zap_ep_t ep, zap_event_t ev)
{
	zap_err_t err;

	printf("---- %s: BEGIN: ep %p event %s -----\n", __func__, ep, ev_str[ev->type]);
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		if (!ev->data) {
			printf("Error: No connect data is received.\n");
			exit(1);
		} else if (0 != strcmp((void*)ev->data, CONN_DATA)) {
			printf("Error: received wrong connect data. "
				"Expected: %s. Received: %s\n",
				CONN_DATA, ev->data);
			exit(1);
		} else {
			printf("  ... ACCEPTING data: '%s' data_len: %jd\n",
			       ev->data, ev->data_len);
			err = zap_accept(ep, server_cb, ACCEPT_DATA,
					strlen(ACCEPT_DATA) + 1);
			if (err) {
				/* Zap will cleanup the endpoint */
				printf("Error: zap_accept fails %s\n",
						zap_err_str(err));
			}
		}
		break;
	case ZAP_EVENT_CONNECTED:
		err = zap_map(&map0, mem0, WSIZE,
			      ZAP_ACCESS_READ|ZAP_ACCESS_WRITE);
		if (err) {
			printf("Error: zap_map() error: %d\n", err);
			zap_close(ep);
			break;
		}
		err = zap_share(ep, map0, NULL, 0);
		if (err) {
			printf("Error: zap_share() error: %d\n", err);
			zap_unmap(map0);
			zap_close(ep);
			break;
		}
		break;
	case ZAP_EVENT_DISCONNECTED:
		if (map0) {
			zap_unmap(map0);
			map0 = NULL;
		}
		if (map1) {
			zap_unmap(map1);
			map1 = NULL;
		}
		zap_free(ep);
		done = 1;
		pthread_cond_broadcast(&done_cv);
		break;
	default:
		printf("Unexpected Zap event %s\n", zap_event_str(ev->type));
		assert(0);
		break;
	}
	printf("---- %s: END: ep %p event %s -----\n", __func__, ep, ev_str[ev->type]);
}

void client_cb(zap_ep_t ep, zap_event_t ev)
{
	zap_err_t err;
	printf("---- %s: BEGIN: ep %p event %s ----\n", __func__, ep, ev_str[ev->type]);
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_ERROR:
		zap_free(ep);
		done = 1;
		pthread_cond_broadcast(&done_cv);
		break;
	case ZAP_EVENT_CONNECTED:
		if (!ev->data) {
			printf("Error: No accepted data is received.\n");
			exit(1);
		}
		if (0 != strcmp((void*)ev->data, ACCEPT_DATA)) {
			printf("Error: received wrong accepted data. Expected: %s. Received: %s\n",
				ACCEPT_DATA, ev->data);
			exit(1);
		}
		printf("CONNECTED data: '%s' data_len: %jd\n", ev->data, ev->data_len);
		break;
	case ZAP_EVENT_DISCONNECTED:
		if (map0) {
			zap_unmap(map0);
			map0 = NULL;
		}
		if (map1) {
			zap_unmap(map1);
			map1 = NULL;
		}
		if (remote_map) {
			zap_unmap(remote_map);
			remote_map = NULL;
		}
		zap_free(ep);
		done = 1;
		pthread_cond_broadcast(&done_cv);
		break;
	case ZAP_EVENT_READ_COMPLETE:
		if (memcmp(mem0, mem1, WSIZE)) {
			printf("*** mem0 != mem1 ***\n");
			assert(0);
		} else {
			printf("*** mem0 == mem1 VERIFIED!!! ***\n");
		}
		zap_close(ep);
		break;
	case ZAP_EVENT_WRITE_COMPLETE:
		if (ev->status) {
			printf("ZAP_EVENT_WRITE_COMPLETE error, status: %d\n",
					ev->status);
			zap_close(ep);
			break;
		}
		err = zap_read(ep, remote_map, zap_map_addr(remote_map),
			       map1, mem1, WSIZE, NULL);
		if (err) {
			printf("zap_read() error: %d\n", err);
			zap_close(ep);
			break;
		}
		break;
	case ZAP_EVENT_RENDEZVOUS:
		assert(ev->map);
		remote_map = ev->map;
		memset(mem0, fill_ch, WSIZE);
		err = zap_map(&map0, mem0, WSIZE,
			      ZAP_ACCESS_READ|ZAP_ACCESS_WRITE);
		if (err) {
			printf("zap_map() error: %d\n", err);
			zap_close(ep);
			break;
		}
		err = zap_map(&map1, mem1, WSIZE,
			      ZAP_ACCESS_READ|ZAP_ACCESS_WRITE);
		if (err) {
			printf("zap_map() error: %d\n", err);
			zap_close(ep);
			break;
		}
		err = zap_write(ep, map0, mem0, remote_map,
				zap_map_addr(remote_map), WSIZE, NULL);
		if (err) {
			printf("zap_write() error: %d\n", err);
			zap_close(ep);
			break;
		}
		break;
	default:
		printf("Unexpected Zap event %s\n", zap_event_str(ev->type));
		assert(0);
		break;
	}
	printf("---- %s: END: ep %p event %s ----\n", __func__, ep, ev_str[ev->type]);
}

zap_mem_info_t test_meminfo(void)
{
	return NULL;
}

void do_server(zap_t zap, struct sockaddr_in *sin)
{
	zap_err_t err;
	zap_ep_t ep;
	int i;

	ep = zap_new(zap, server_cb);
	if (!ep) {
		printf("Could not create the zap endpoint.\n");
		return;
	}

	err = zap_listen(ep, (struct sockaddr *)sin, sizeof(*sin));
	if (err) {
		printf("zap_listen failed.\n");
		zap_free(ep);
		return;
	}

	pthread_mutex_lock(&done_lock);
	while (!done)
		pthread_cond_wait(&done_cv, &done_lock);
	pthread_mutex_unlock(&done_lock);
	for (i = 0; i < WSIZE; i++) {
		if (mem0[i] != fill_ch) {
			printf("Client-to-server write failed ...\n");
			assert(0);
			break;
		}
	}
	if (i == WSIZE) {
		printf("Client-to-server write is a success!\n");
	}
}

void do_client(zap_t zap, struct sockaddr_in *sin)
{
	zap_err_t err;
	zap_ep_t ep;

	ep = zap_new(zap, client_cb);
	if (!ep) {
		printf("Could not create the zap endpoing.\n");
		return;
	}
	zap_set_ucontext(ep, sin);
	err = zap_connect(ep, (struct sockaddr *)sin, sizeof(*sin),
			  CONN_DATA, sizeof(CONN_DATA) + 1);
	if (err) {
		printf("zap_connect failed.\n");
		return;
	}

	pthread_mutex_lock(&done_lock);
	while (!done)
		pthread_cond_wait(&done_cv, &done_lock);
	pthread_mutex_unlock(&done_lock);
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

#define FMT_ARGS "x:p:h:sm:"
void usage(int argc, char *argv[]) {
	printf("usage: %s -x name -p port_no [-h host] [-s].\n"
	       "    -x name	The transport to use.\n"
	       "    -p port_no	The port number.\n"
	       "    -h host	The host name or IP address. Must be specified\n"
	       "		if this is the client.\n"
	       "    -s		This is a server.\n"
	       "    -m bytes	Memory size (default: 65536).\n",
	       argv[0]);
	exit(1);
}

int main(int argc, char *argv[])
{
	int rc;
	int is_server = 0;
	char *transport = NULL;
	char *host = NULL;
	short port_no = -1;
	struct sockaddr_in sin;

	while (-1 != (rc = getopt(argc, argv, FMT_ARGS))) {
		switch (rc) {
		case 's':
			is_server = 1;
			break;
		case 'h':
			host = strdup(optarg);
			break;
		case 'x':
			transport = strdup(optarg);
			break;
		case 'p':
			port_no = atoi(optarg);
			break;
		case 'm':
			WSIZE = atoi(optarg);
			if (!WSIZE)
				WSIZE = DEFAULT_WSIZE;
			break;
		default:
			usage(argc, argv);
			break;
		}
	}

	mem0 = calloc(1, WSIZE);
	assert(mem0);
	mem1 = calloc(1, WSIZE);
	assert(mem1);

	if (!transport)
		usage(argc, argv);

	if (port_no == -1)
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

	zap = zap_get(transport, test_meminfo);
	if (!zap) {
		printf("%s: could not load the '%s' transport.\n",
		       __func__, transport);
		exit(1);
	}
	free(transport);
	if (is_server)
		do_server(zap, &sin);
	else
		do_client(zap, &sin);

	/* This sleep is to ensure that we see the resources get cleaned
	 * properly */
	sleep(1);
	free(mem0);
	free(mem1);
	return 0;
}
