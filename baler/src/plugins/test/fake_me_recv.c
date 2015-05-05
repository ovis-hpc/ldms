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
#include <stdio.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>

#include "zap/zap.h"

enum me_input_type {
	ME_INPUT_DATA,
	ME_NO_DATA
};

#pragma pack(4)
struct me_msg {
	enum me_input_type tag;
	uint32_t comp_id;
	uint32_t metric_type_id;
	struct timeval timestamp;
	double value;
};
#pragma pack()

const char *short_opt = "x:p:?";
struct option long_opt[] = {
	{"xprt", 1, 0, 'x'},
	{"port", 1, 0, 'p'},
	{"help", 0, 0, '?'},
	{0, 0, 0, 0}
};

pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;

void usage()
{
	printf(
"Usage: fake_me_recv [-x XPRT] [-p PORT]\n"
"\n"
"	XPRT	can be sock or rdma (default: sock)\n"
"	PORT	listening port number (default: 55555)\n"
	);
}

char *xprt = "sock";
uint16_t port = 55555;

zap_t zap;
zap_ep_t ep;

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
		port = atoi(optarg);
		break;
	case -1:
		/* No more args */
		return;
	default:
		usage();
		exit(-1);
	}
	goto loop;
}

void zerr_assert(zap_err_t zerr, const char *prefix)
{
	if (zerr) {
		printf("%s: %s\n", prefix, zap_err_str(zerr));
		exit(-1);
	}
}

void zap_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

zap_mem_info_t zap_meminfo(void)
{
	return NULL;
}

void print_me_msg(struct me_msg *msg)
{
	printf("----------\n");
	printf("metric_type_id: %u\n", msg->metric_type_id);
	printf("comp_id: %u\n", msg->comp_id);
	printf("tag: %u\n", msg->tag);
	printf("ts: %ld:%ld\n", msg->timestamp.tv_sec, msg->timestamp.tv_usec);
	printf("value: %lf\n", msg->value);
	printf("----------\n");
}

void handle_recv(zap_ep_t zep, zap_event_t ev)
{
	struct me_msg *msg;
	if (ev->data_len != sizeof(*msg)) {
		printf("Expecting len: %zu, but got %zu\n", sizeof(*msg),
				ev->data_len);
		return;
	}
	msg = (void*)ev->data;
	msg->metric_type_id = ntohl(msg->metric_type_id);
	msg->comp_id = ntohl(msg->comp_id);
	msg->tag = ntohl(msg->tag);
	msg->timestamp.tv_sec = ntohl(msg->timestamp.tv_sec);
	msg->timestamp.tv_usec = ntohl(msg->timestamp.tv_usec);
	print_me_msg(msg);
}

void callback(zap_ep_t zep, zap_event_t ev)
{
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		printf("Connection request.\n");
		zap_accept(zep, callback, NULL, 0);
		break;
	case ZAP_EVENT_DISCONNECTED:
		printf("Disconnected.\n");
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		printf("Connect error ...\n");
		break;
	case ZAP_EVENT_CONNECTED:
		printf("Connected.\n");
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		handle_recv(zep, ev);
		break;
	}
}

int main(int argc, char **argv)
{
	zap_err_t zerr;
	handle_args(argc, argv);
	zerr = zap_get(xprt, &zap, zap_log, zap_meminfo);
	zerr_assert(zerr, "zap_get");
	zerr = zap_new(zap, &ep, callback);
	zerr_assert(zerr, "zap_new");
	struct sockaddr_in sin = {0};
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	zerr = zap_listen(ep, (void*)&sin, sizeof(sin));
	zerr_assert(zerr, "zap_listen");
	pthread_mutex_lock(&done_mutex);
	pthread_cond_wait(&done_cond, &done_mutex);
	return 0;
}
