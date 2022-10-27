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
 * \file zap_test_reconnect.c
 * \brief Zap test for reconnecting scenario
 */

/**
 * \page zap_test_reconnect zap_test_reconnect
 *
 * \section summary SUMMARY
 *
 * Zap test for the case that the client keeps reconnecting to the server.
 *
 * \section synopsis SYNOPSIS
 *
 * \code{.sh}
 * # Server #
 * zap_test_reconnect -x XPRT -p PORT -s
 *
 * # Client #
 * zap_test_reconnect -x XPRT -p PORT -h HOST [-f,--forever]
 *
 * \endcode
 *
 * \section desc DESCRIPTION
 *
 * This test program, like other zap_test*, can run in two modes: server and
 * client. The test scheme is the following:
 *
 * Server:
 *   After the server starts, it will listen and wait for messages from a client.
 * Upon receiving a message, it will send the client back the messages. The server
 * runs indefinitely. The server should be started/stopped repeatedly in order
 * to test the robustness of the reconnect scenario.
 *
 * Client:
 *   The client will try to connect/re-connect to the server. In the case that
 * the server does not exist, the client will free the endpoint and create a new
 * one to reconnect to the server. While the connection is connected, the client
 * will send ten consecutive messages to the server and then sleep for 2 seconds.
 * If the --forever/-f option is not given, the client will close the connection
 * and exit. On the other hand, it will send the ten consecutive messages every
 * 2 second interval as long as the connection is connected. When the connection
 * is disconnected, it will try to reconnect again.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <netinet/ip.h>
#include <sys/time.h>
#include <netdb.h>
#include <errno.h>
#include "zap/zap.h"

char *short_opt = "h:x:p:sf?";
struct option long_opt[] = {
	{"host",     1,  0,  'h'},
	{"xprt",     1,  0,  'x'},
	{"port",     1,  0,  'p'},
	{"server",   0,  0,  's'},
	{"forever",  0,  0,  'f'},
	{"help",     0,  0,  '?'},
	{0,0,        0,  0}
};

void usage()
{
	printf(
"Usage: test [-x <XPRT>] [-p <PORT>] [-h <HOST>] [--forever] [-s]\n"
"\n"
"OPTIONS:\n"
"	-x XPRT		Transport. The default is sock.\n"
"	-p PORT		Port to listen/connect. The default is 55555\n"
"	-h HOST		(client mode only) Host to connect to. The default is localhost.\n"
"	-s		Indicates server mode.\n"
"	-f,--forever	(client mode only) the client will keep sending messages to\n"
"			the server forever.\n"
	);
}

char *host = "localhost";
char *xprt = "rdma";
uint16_t port = 55555;
int server_mode = 0;
int forever = 0;

void handle_args(int argc, char **argv)
{
	char c;
loop:
	c = getopt_long(argc, argv, short_opt, long_opt, NULL);
	switch (c) {
	case 'x':
		xprt = optarg;
		break;
	case 'p':
		port = strtoul(optarg, NULL, 10);
		break;
	case 'h':
		host = optarg;
		break;
	case 's':
		server_mode = 1;
		break;
	case 'f':
		forever = 1;
		break;
	case -1:
		goto out;
	case '?':
	default:
		usage();
		exit(0);
	}
	goto loop;
out:
	return;
}

zap_t zap;
pthread_cond_t reconnect_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t ep_mutex = PTHREAD_MUTEX_INITIALIZER;

int exiting = 0;
pthread_mutex_t exiting_mutex = PTHREAD_MUTEX_INITIALIZER;

int done = 0;

zap_mem_info_t zap_mem_info(void)
{
	return NULL;
}

enum {
	DISCONNECTED,
	CONNECTING,
	CONNECTED
} flag;
pthread_mutex_t flag_lock = PTHREAD_MUTEX_INITIALIZER;
void server_cb(zap_ep_t zep, zap_event_t ev)
{
	struct sockaddr_storage lsin = {0};
	struct sockaddr_storage rsin = {0};
	socklen_t slen;
	char *data;

	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		zap_accept(zep, server_cb, (void*)ev->data, ev->data_len);
		break;
	case ZAP_EVENT_CONNECTED:
		printf("connected\n");
		break;
	case ZAP_EVENT_DISCONNECTED:
		slen = sizeof(lsin);
		zap_get_name(zep, (void*)&lsin, (void*)&rsin, &slen);
		printf("%X disconnected\n", ((struct sockaddr_in *)&rsin)->sin_addr.s_addr);
		zap_free(zep);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		data = (char *)ev->data;
		printf("recv: %s\n", data);
		printf("echoing: %s\n", data);
		zap_send(zep, ev->data, ev->data_len);
		break;
	case ZAP_EVENT_REJECTED:
	case ZAP_EVENT_CONNECT_ERROR:
		printf("Unexpected Zap event %s\n", zap_event_str(ev->type));
		assert(0);
	default:
		printf("Unhandled Zap event %s\n", zap_event_str(ev->type));
		exit(-1);
	}
}

void client_cb(zap_ep_t zep, zap_event_t ev)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	pthread_mutex_lock(&flag_lock);
	switch(ev->type) {
	case ZAP_EVENT_CONNECTED:
		flag = CONNECTED;
		printf("%s\n", zap_event_str(ev->type));
		break;
	case ZAP_EVENT_REJECTED:
		/* RDMA usually returns REJECTED event for unsuccessful
		 * connection request */
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_DISCONNECTED:
		printf("%s\n", zap_event_str(ev->type));
		pthread_mutex_lock(&exiting_mutex);
		if (!exiting) {
			flag = DISCONNECTED;
		}
		pthread_mutex_unlock(&exiting_mutex);
		zap_free(zep);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		printf("recv: %s\n", (char*)ev->data);
		break;
	default:
		printf("Unhandled Zap event %s\n", zap_event_str(ev->type));
		pthread_mutex_unlock(&flag_lock);
		exit(-1);
	}
	pthread_mutex_unlock(&flag_lock);
}

void do_server(struct sockaddr_in *sin)
{
	zap_ep_t ep;
	int zerr;
	ep = zap_new(zap, server_cb);
	if (!ep) {
		zerr = errno;
		printf("zap_new error: %d\n", zerr);
		exit(-1);
	}

	zerr = zap_listen(ep, (void*)sin, sizeof(*sin));
	if (zerr) {
		printf("zap_listen error: %d\n", zerr);
		exit(-1);
	}
	printf("Listening on port %hu\n", port);
	/* Run forever */
	while (1) {
		/* do nothing */
		sleep(60);
	}
}

void *send_msg(void *arg)
{
	struct sockaddr_in *sin = arg;
	struct sockaddr_storage lsin = {0};
	struct sockaddr_storage rsin = {0};
	socklen_t slen;
	struct timeval tv;
	zap_err_t zerr;
	int i, count;
	count = 10;
	char data[512];

	while (1) {
		zap_ep_t ep = NULL;
		pthread_mutex_lock(&flag_lock);
		if (flag != CONNECTED) {
			if (flag == CONNECTING) {
				pthread_mutex_unlock(&flag_lock);
				sleep(5);
				continue;
			}

			printf("connecting...\n");
			ep = zap_new(zap, client_cb);
			if (!ep) {
				zerr = errno;
				printf("zap_new error: %d\n", zerr);
				exit(-1);
			}
			printf("Connecting to %s:%hu\n", host, port);
			flag = CONNECTING;
			zerr = zap_connect(ep, (void*)sin, sizeof(*sin), NULL, 0);
			if (zerr) {
				printf("zap_connect error: %d\n", zerr);
				exit(-1);
			}
			pthread_mutex_unlock(&flag_lock);
			sleep(2);
			continue;
		}

		pthread_mutex_unlock(&flag_lock);
		gettimeofday(&tv, NULL);
		slen = sizeof(lsin);
		zap_get_name(ep, (void*)&lsin, (void*)&rsin, &slen);
		for (i = 0; i < count; i++) {
			printf("%d: Sending %d.%d to %X\n", i,
				(int)tv.tv_sec, (int)tv.tv_usec,
				((struct sockaddr_in *)&rsin)->sin_addr.s_addr);
			sprintf(data, "%d: %d.%d", i, (int)tv.tv_sec,
					(int)tv.tv_usec);
			pthread_mutex_lock(&flag_lock);
			if (flag == CONNECTED) {
				zerr = zap_send(ep, data, strlen(data) + 1);
				if (zerr)
					printf("Error %d in zap_send.\n", zerr);
			} else {
				break;
			}
			pthread_mutex_unlock(&flag_lock);
		}

		sleep(2);
		if (!forever) {
			pthread_mutex_lock(&exiting_mutex);
			exiting = 1;
			pthread_mutex_unlock(&exiting_mutex);
			if (flag == CONNECTED) {
				zerr = zap_close(ep);
				if (zerr)
					printf("Error %d in zap_close.\n", zerr);
			}
			break;
		}
	}
	return NULL;
}

void do_client(struct sockaddr_in *sin)
{
	struct addrinfo *ai;
	int rc;

	rc = getaddrinfo(host, NULL, NULL, &ai);
	if (rc) {
		printf("getaddrinfo error: %d\n", rc);
		exit(-1);
	}
	*sin = *(typeof(sin))ai[0].ai_addr;
	sin->sin_port = htons(port);
	freeaddrinfo(ai);
	pthread_t t;
	rc = pthread_create(&t, NULL, send_msg, sin);
	if (rc) {
		printf("pthread_create error %d\n", rc);
		exit(-1);
	}
	pthread_join(t, NULL);
}

int main(int argc, char **argv)
{
	zap_err_t zerr;
	handle_args(argc, argv);
	zap = zap_get(xprt, zap_mem_info);
	if (!zap) {
		zerr = errno;
		printf("zap_get error: %d\n", zerr);
		exit(-1);
	}

	struct sockaddr_in sin = {0};
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;

	if (server_mode)
		do_server(&sin);
	else
		do_client(&sin);
	sleep(1);
	return 0;
}
