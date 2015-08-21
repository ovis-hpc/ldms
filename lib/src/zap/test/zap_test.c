/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2015 Sandia Corporation. All rights reserved.
 *
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
 * \file zap_test.c
 * \author Tom Tucker
 * \author Narate Taerat
 *
 * \brief Zap basic functionality test.
 *
 * This test program runs in two mode: server and client. The server (with '-s'
 * option in command-line argument) should start before the client. The
 * client-server interactions will test the following basic functionality of
 * zap:
 * 	- listen
 * 	- connect
 * 	- reject
 * 	- accept
 * 	- send
 * 	- receive
 * 	- share memory map
 * 	- read (rdma-like operation)
 *	- write (rdma-like operation)
 *
 * The scheme is as follows:
 * 	- server start
 * 	- client start
 * 	- client request a connection
 * 	- server reject
 * 	- client try again
 * 	- server accept
 * 	- client send "Hello there!" to server
 * 	- server echo back
 * 	- client share write memory map
 * 	- server get rendezvous event and write to the shared memory map
 * 		- server also share read memory map
 * 	- client get the rendezvous event (read-only memory map, shared from the
 * 		server)
 * 	- client read the memory map
 * 	- on client read complete, it print the read data and written data (from
 * 	  the shared write memory map).
 * 		- client unmap the write map and send "read/write dare" message
 * 		  to server
 * 	- server receive "read/write dare" message
 *		- server perform write operation (expecting to fail)
 *		- server torn down the connection
 *	- server and client get DISCONNECTED event
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

/* Data src/sink for RDMA_WRITE */
char write_buf[1024];

zap_map_t write_map = NULL; /* exporting write map */
zap_map_t read_map = NULL; /* exporting read map */

/* Data src/sink for RDMA_READ */
char read_buf[1024];

zap_map_t remote_map = NULL; /* remote memory mapping */

void do_send(zap_ep_t ep, char *message)
{
	zap_err_t err;
	printf("Sending: %s\n", message);
	err = zap_send(ep, message, strlen(message) + 1);
	if (err)
		printf("Error %d sending message.\n", err);
}

void handle_recv(zap_ep_t ep, zap_event_t ev)
{
	int len = strlen((char*)ev->data) + 1;
	printf("%s: len %zu '%s'.\n", __func__,
	       ev->data_len, (char *)ev->data);
	if (len != ev->data_len) {
		printf("%s: wrong length!!! expecting %d but got %zd\n",
				__func__, len, ev->data_len);
	}
	assert(len == ev->data_len);

	if (strncmp(dare, ev->data, strlen(dare)+1) != 0) {
		/* regular message received, just echo back and return */
		do_send(ep, ev->data);
		return;
	}

	/* write dare received */
	zap_err_t err;
	zap_map_t src_write_map;

	strcpy(write_buf, "Thanks for sharing!");

	/* Map the data to send */
	err = zap_map(ep, &src_write_map, write_buf, sizeof write_buf,
			ZAP_ACCESS_NONE);
	if (err) {
		printf("%s:%d returns %d.\n", __func__, __LINE__, err);
		return;
	}

	/* Perform an RDMA_WRITE to the map we just received */
	err = zap_write(ep, src_write_map, zap_map_addr(src_write_map),
			remote_map, zap_map_addr(remote_map),
			zap_map_len(src_write_map), src_write_map);
	if (err) {
		printf("Error %d for RDMA_WRITE to share.\n", err);
		return;
	}
}

void handle_rendezvous(zap_ep_t ep, zap_event_t ev)
{
	zap_err_t err;
	zap_map_t src_write_map;
	zap_map_t dst_write_map;

	if (ev->status) {
		printf("error %d received in rendezvous event.\n", ev->status);
		return;
	}
	printf("rendezvous msg_len: %zu\n", ev->data_len);
	printf("rendezvous message: %s\n", (char*)ev->data);
	strcpy(write_buf, "thanks for sharing!");

	/* map the data to send */
	err = zap_map(ep, &src_write_map, write_buf, sizeof write_buf,
			ZAP_ACCESS_NONE);
	if (err) {
		printf("%s:%d returns %d.\n", __func__, __LINE__, err);
		return;
	}

	/* perform an rdma_write to the map we just received */
	remote_map = dst_write_map = ev->map;
	err = zap_write(ep, src_write_map, zap_map_addr(src_write_map),
			dst_write_map, zap_map_addr(dst_write_map),
			zap_map_len(src_write_map), src_write_map);
	if (err) {
		printf("error %d for rdma_write to share.\n", err);
		return;
	}
	printf("Write map is %p.\n", src_write_map);

	/* Create a map for our peer to read from */
	strcpy(read_buf, "This is the data source for the RDMA_READ.");
	err = zap_map(ep, &read_map, read_buf, sizeof read_buf, ZAP_ACCESS_READ);
	if (err) {
		printf("Error %d for map of RDMA_READ memory.\n", err);
		return;
	}
	err = zap_share(ep, read_map, "from server", 12);
	if (err) {
		printf("Error %d for share of RDMA_READ map.\n", err);
		return;
	}
}

void do_write_complete(zap_ep_t ep, zap_event_t ev)
{
	zap_err_t err;
	printf("Write complete with status: %s\n", zap_err_str(ev->status));
	zap_map_t write_src_map = (void *)(unsigned long)ev->context;
	printf("Unmapping write map %p.\n", write_src_map);
	err = zap_unmap(ep, write_src_map);
	if (err)
		printf("%s:%d returns %d.\n", __func__, __LINE__, err);
	if (ev->status != ZAP_ERR_OK) {
		/* Error occur, torn down the connection */
		zap_close(ep);
	}
}

#define CONN_DATA "Hello, world!"
#define ACCEPT_DATA "Accepted!"
void server_cb(zap_ep_t ep, zap_event_t ev)
{
	static int reject = 1;
	zap_err_t err;

	printf("---- %s: BEGIN: ep %p event %s -----\n", __func__, ep, ev_str[ev->type]);
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		if (reject) {
			printf("  ... REJECTING\n");
			err = zap_reject(ep);
		} else {
			if (!ev->data) {
				printf("Error: No connect data is received.\n");
				exit(1);
			} else if (0 != strcmp(ev->data, CONN_DATA)) {
				printf("Error: received wrong connect data. Expected: %s. Received: %s\n",
					CONN_DATA, ev->data);
				exit(1);
			} else {
				printf("  ... ACCEPTING data: '%s' data_len: %jd\n",
				       ev->data, ev->data_len);
				err = zap_accept(ep, server_cb, ACCEPT_DATA, strlen(ACCEPT_DATA));
			}
		}
		/* alternating between reject and accept */
		reject = !reject;
		break;
	case ZAP_EVENT_CONNECTED:
		break;
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_REJECTED:
		printf("Unexpected Zap event %s\n", zap_event_str(ev->type));
		assert(0);
		break;
	case ZAP_EVENT_DISCONNECTED:
		if (remote_map) {
			zap_unmap(ep, remote_map);
			remote_map = NULL;
		}
		if (read_map) {
			zap_unmap(ep, read_map);
			read_map = NULL;
		}

		zap_free(ep);
		done = 1;
		pthread_cond_broadcast(&done_cv);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		handle_recv(ep, ev);
		break;
	case ZAP_EVENT_READ_COMPLETE:
		assert(0);
		break;
	case ZAP_EVENT_WRITE_COMPLETE:
		do_write_complete(ep, ev);
		break;
	case ZAP_EVENT_RENDEZVOUS:
		handle_rendezvous(ep, ev);
		break;
	default:
		printf("Unhandled Zap event %s\n", zap_event_str(ev->type));
		exit(-1);
	}
	printf("---- %s: END: ep %p event %s -----\n", __func__, ep, ev_str[ev->type]);
}

void do_rendezvous(zap_ep_t ep)
{
	zap_err_t err;

	err = zap_map(ep, &write_map, write_buf, sizeof(write_buf),
		      ZAP_ACCESS_WRITE);
	if (err) {
		printf("Error %d mapping the write buffer.\n", err);
		zap_close(ep);
	}

	err = zap_share(ep, write_map, "from client", 12);
	if (err) {
		printf("Error %d sharing the write map.\n", err);
		zap_close(ep);
	}
}

void do_read_and_verify_write(zap_ep_t ep, zap_event_t ev)
{
	zap_err_t err;
	zap_map_t src_read_map;
	zap_map_t dst_read_map;

	if (ev->status) {
		printf("Error %d received in rendezvous event.\n", ev->status);
		return;
	}

	if (ev->data_len) {
		printf("rendezvous msg_len: %zu\n", ev->data_len);
		printf("rendezvous message: %s\n", (char*)ev->data);
	}

	/* Read source comes from peer. */
	if (ev->map) {
		remote_map = src_read_map = ev->map;
	} else {
		src_read_map = remote_map;
	}

	/* Create some memory to receive the read data */
	err = zap_map(ep, &dst_read_map, read_buf, zap_map_len(src_read_map),
		      ZAP_ACCESS_WRITE | ZAP_ACCESS_READ);
	if (err) {
		printf("Error %d mapping RDMA_READ data sink memory.\n", err);
		return;
	}

	/* Perform an RDMA_READ from the map we just received */
	err = zap_read(ep, src_read_map, zap_map_addr(src_read_map),
		       dst_read_map, zap_map_addr(dst_read_map),
		       zap_map_len(src_read_map),
		       dst_read_map);
	if (err) {
		printf("Error %d received from zap_read.\n", err);
		return;
	}
	printf("Read Map is %p.\n", dst_read_map);

	/* Let's see what the partner wrote in our write_buf */
	printf("WRITE BUFFER CONTAINS: '%s'.\n", write_buf);
}

void do_read_complete(zap_ep_t ep, zap_event_t ev)
{
	if (ev->status != ZAP_ERR_OK)  {
		printf("Read error: %s\n", zap_err_str(ev->status));
		zap_close(ep);
		return ;
	}
	zap_err_t err;
	zap_map_t read_sink_map = (void *)(unsigned long)ev->context;
	printf("Unmapping read map %p.\n", read_sink_map);
	err = zap_unmap(ep, read_sink_map);
	if (err)
		printf("%s:%d returns %d.\n", __func__, __LINE__, err);
#if 0
	err = zap_unmap(ep, write_map);
	if (err)
		printf("%s:%d returns %d.\n", __func__, __LINE__, err);
#endif
	printf("READ BUFFER CONTAINS '%s'.\n", read_buf);
	zap_unmap(ep, write_map);

	do_send(ep, dare);
}


void client_cb(zap_ep_t ep, zap_event_t ev)
{
	struct sockaddr_in *sin;
	zap_err_t err;
	printf("---- %s: BEGIN: ep %p event %s ----\n", __func__, ep, ev_str[ev->type]);
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		printf("Unexpected Zap event %s\n", zap_event_str(ev->type));
		assert(0);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		done = 1;
		pthread_cond_broadcast(&done_cv);
		break;
	case ZAP_EVENT_CONNECTED:
		if (!ev->data) {
			printf("Error: No accepted data is received.\n");
			exit(1);
		}
		if (0 != strcmp(ev->data, ACCEPT_DATA)) {
			printf("Error: received wrong accepted data. Expected: %s. Received: %s\n",
				ACCEPT_DATA, ev->data);
			exit(1);
		}
		printf("CONNECTED data: '%s' data_len: %jd\n", ev->data, ev->data_len);
		do_send(ep, "Hello there!");
		break;
	case ZAP_EVENT_REJECTED:
		printf("REJECTED!\n");
		sin = zap_get_ucontext(ep);
		zap_free(ep);

		ep = zap_new(zap, client_cb);
		if (!ep) {
			printf("zap_new failed.\n");
			return;
		}
		zap_set_ucontext(ep, sin);
		err = zap_connect(ep, (struct sockaddr *)sin, sizeof(*sin),
				  CONN_DATA, sizeof(CONN_DATA));
		if (err) {
			printf("zap_connect failed.\n");
			return;
		}
		break;
	case ZAP_EVENT_DISCONNECTED:
		if (remote_map) {
			zap_unmap(ep, remote_map);
			remote_map = NULL;
		}

		zap_free(ep);
		done = 1;
		pthread_cond_broadcast(&done_cv);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		if (strncmp(dare, ev->data, strlen(dare)+1) == 0) {
			/* read dare, do read and verify write again */
			do_read_and_verify_write(ep, ev);
		} else {
			do_rendezvous(ep);
		}
		break;
	case ZAP_EVENT_READ_COMPLETE:
		do_read_complete(ep, ev);
		break;
	case ZAP_EVENT_WRITE_COMPLETE:
		assert(0);
		break;
	case ZAP_EVENT_RENDEZVOUS:
		do_read_and_verify_write(ep, ev);
		break;
	default:
		printf("Unhandled Zap event %s\n", zap_event_str(ev->type));
		exit(-1);
	}
	printf("---- %s: END: ep %p event %s ----\n", __func__, ep, ev_str[ev->type]);
}

void test_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	fflush(stdout);
}

zap_mem_info_t test_meminfo(void)
{
	return NULL;
}

void do_server(zap_t zap, struct sockaddr_in *sin)
{
	zap_err_t err;
	zap_ep_t ep;

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
			  CONN_DATA, sizeof(CONN_DATA));
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

#define FMT_ARGS "x:p:h:s"
void usage(int argc, char *argv[]) {
	printf("usage: %s -x name -p port_no [-h host] [-s].\n"
	       "    -x name	The transport to use.\n"
	       "    -p port_no	The port number.\n"
	       "    -h host	The host name or IP address. Must be specified\n"
	       "		if this is the client.\n"
	       "    -s		This is a server.\n",
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
	zap_err_t err;

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
		default:
			usage(argc, argv);
			break;
		}
	}
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

	zap = zap_get(transport, test_log, test_meminfo);
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
	return 0;
}
