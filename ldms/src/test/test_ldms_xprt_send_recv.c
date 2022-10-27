/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017-2018 Open Grid Computing, Inc. All rights reserved.
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
/*
 * test_ldms_xprt_send_recv.c
 *
 *  Created on: Jan 19, 2017
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <semaphore.h>
#include <errno.h>
#include "ldms.h"
#define FMT "x:p:h:sfn"

char *xprt = "sock";
char *host = "localhost";
int port = 10001;
char *port_s = "10001";
int is_server;
int is_null;
sem_t exit_sem;

enum msg_type {
	MSG_REQ = 1,
	MSG_RESP,
	MSG_NULL,
};

struct user_msg {
	enum msg_type type;
	char *msg;
	size_t len;
};

struct recv_arg {
	ldms_t sender_x;
};

#define msg_req_str "Request"
#define msg_resp_str "Response"


static char *__type2str(enum msg_type type)
{
	switch (type) {
	case MSG_REQ:
		return "REQUEST";
	case MSG_RESP:
		return "RESPONSE";
	case MSG_NULL:
		return "NULL";
	default:
		return "UNKNOWN";
		break;
	}
}

static void __check_msg_correctness(struct user_msg *msg)
{
	char *correct_str;
	switch (msg->type) {
	case MSG_REQ:
		correct_str = msg_req_str;
		break;
	case MSG_RESP:
		correct_str = msg_resp_str;
		break;
	case MSG_NULL:
		printf("Received NULL. Unexpected!!!\n");
		printf("FAIL\n");
		exit(-1);
	default:
		printf("Unrecognized msg type.\n");
		exit(-1);
	}

	if (0 != strncmp(msg->msg, correct_str, msg->len)) {
		printf("Received wrong message. Expecting: '%s'."
				" Received: '%s'\n",
				correct_str, msg->msg);
		printf("FAIL\n");
		exit(-1);
	}
}

static int msg_send(ldms_t x, enum msg_type msg_type)
{
	int rc;
	struct user_msg msg;
	printf("Sending '%s'\n", __type2str(msg_type));
	switch (msg_type) {
	case MSG_REQ:
		msg.type = MSG_REQ;
		msg.msg = msg_req_str;
		msg.len = strlen(msg.msg);
		break;
	case MSG_RESP:
		msg.type = MSG_RESP;
		msg.msg = msg_resp_str;
		msg.len = strlen(msg.msg);
		break;
	case MSG_NULL:
		goto send_null;
	default:
		printf("Unrecognized msg type.\n");
		exit(-1);
		break;
	}
	rc = ldms_xprt_send(x, (char *)&msg, sizeof(msg));
	if (rc) {
		printf("%s: ldms_xprt_send error: %d\n", msg.msg, rc);
		printf("FAIL\n");
		exit(-1);
	}
	return rc;
send_null:
	rc = ldms_xprt_send(x, NULL, 0);
	if (rc == EINVAL) {
		ldms_xprt_close(x);
	} else {
		printf("ldms fails to detect that the message is NULL.\n");
		printf("FAIL\n");
		exit(-1);
	}
	return rc;
}

static void server_recv_cb(ldms_t x, char *msg_buf, size_t msg_len,
					struct recv_arg *cb_arg)
{
	struct user_msg *recv_msg = (struct user_msg *)msg_buf;
	printf("Received '%s'\n", __type2str(recv_msg->type));
	__check_msg_correctness(recv_msg);
	switch (recv_msg->type) {
	case MSG_REQ:
		msg_send(x, MSG_RESP);
		sleep(1);
		msg_send(x, MSG_REQ);
		break;
	case MSG_RESP:
		printf("Closing the connection\n");
		ldms_xprt_close(x);
		sem_post(&exit_sem);
		break;
	default:
		printf("Unrecognized received message type '%d'\n", recv_msg->type);
		printf("FAIL\n");
		exit(-1);
	}
}

static void server_listen_connect_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	struct recv_arg *recv_arg = (struct recv_arg *)cb_arg;
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		printf("There is a new connection.\n");
		recv_arg->sender_x = x;
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		printf("The connection is disconnected.\n");
		free(recv_arg);
		ldms_xprt_put(x);
		break;
	case LDMS_XPRT_EVENT_RECV:
		server_recv_cb(x, e->data, e->data_len, recv_arg);
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		printf("Received SEND_COMPLETE\n");
		break;
	default:
		printf("Unexpected ldms conn event %d.\n", e->type);
		exit(-1);
	}
}

static void do_server(struct sockaddr_in *sin)
{
	int rc;
	ldms_t ldms;

	ldms = ldms_xprt_new(xprt);
	if (!ldms) {
		printf("ldms_xprt_new error\n");
		exit(-1);
	}

	struct recv_arg *recv_arg = malloc(sizeof(*recv_arg));
	if (!recv_arg) {
		printf("Out of memory\n");
		exit(-1);
	}
	rc = ldms_xprt_listen(ldms, (void *)sin, sizeof(*sin),
			server_listen_connect_cb, recv_arg);
	if (rc) {
		printf("ldms_xprt_listen: %d\n", rc);
		exit(-1);
	}

	printf("Listening on port %d\n", port);
}

static void client_recv_cb(ldms_t x, char *msg_buf, size_t msg_len, void *cb_arg)
{
	struct user_msg *recv_msg = (struct user_msg *)msg_buf;
	printf("Received '%s'\n", __type2str(recv_msg->type));
	__check_msg_correctness(recv_msg);
	switch (recv_msg->type) {
	case MSG_REQ:
		msg_send(x, MSG_RESP);
		break;
	case MSG_RESP:
		break;
	default:
		printf("Unrecognized received message type '%d'\n", recv_msg->type);
		printf("FAIL\n");
		exit(-1);
	}
}

static void client_connect_cb(ldms_t x, ldms_xprt_event_t e, void *arg)
{
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		printf("Connected\n");
		if (is_null) {
			msg_send(x, MSG_NULL);
		} else {
			msg_send(x, MSG_REQ);
		}
		break;
	case LDMS_XPRT_EVENT_ERROR:
		printf("con_error\n");
		ldms_xprt_put(x);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		printf("disconnected\n");
		ldms_xprt_put(x);
		sem_post(&exit_sem);
		break;
	case LDMS_XPRT_EVENT_RECV:
		client_recv_cb(x, e->data, e->data_len, arg);
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		printf("Received SEND_COMPLETE\n");
		break;
	default:
		printf("Unhandled ldms event '%d'\n", e->type);
		exit(-1);
	}
}

static void do_client(struct sockaddr_in *_sin)
{
	int rc;
	struct addrinfo *ai;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};

	if (!host) {
		printf("Please give the hostname\n");
		exit(-1);
	}
	rc = getaddrinfo(host, port_s, &hints, &ai);
	if (rc) {
		printf("%s/%s: getaddrinfo failed: %s\n",
				host, port_s, gai_strerror(rc));
		exit(-1);
	}

	ldms_t x;
	x = ldms_xprt_new(xprt);
	if (!x) {
		printf("ldms_xprt_new error\n");
		exit(-1);
	}
	printf("connecting to %s:%d\n", host, port);
	rc = ldms_xprt_connect(x, ai->ai_addr, ai->ai_addrlen,
					client_connect_cb, NULL);
	if (rc)
		printf("ldms_xprt_connect error: %d\n", rc);
	freeaddrinfo(ai);
}

static void usage()
{
	printf("	-n 		Send NULL. The client should assert.\n");
	printf("	-h host		Host name to connect to\n");
	printf("	-p port		listener port or port to connect\n");
	printf("	-s		Server mode\n");
	printf("	-x xprt		sock, rdma, or ugni\n");
}

static void process_arg(int argc, char **argv)
{
	char op;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'x':
			xprt = strdup(optarg);
			break;
		case 'p':
			port_s = strdup(optarg);
			port = atoi(optarg);
			break;
		case 'h':
			host = strdup(optarg);
			break;
		case 's':
			is_server = 1;
			break;
		case 'n':
			is_null = 1;
			break;
		case '?':
			usage();
			exit(0);
		default:
			printf("Unrecognized argument '%c'\n", op);
			exit(1);
			break;
		}
	}
}

int main(int argc, char **argv) {
	process_arg(argc, argv);
	struct sockaddr_in sin = {0};
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;

	ldms_init(512 * 1024);
	sem_init(&exit_sem, 0, 0);

	if (is_server)
		do_server(&sin);
	else
		do_client(&sin);

	sem_wait(&exit_sem);
	printf("DONE\n");
	return 0;
}
