/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_sock_xprt.h"

LIST_HEAD(sock_list, struct ldms_sock_xprt) sock_list;

static int io_event_fd;
static int cm_event_fd;
static pthread_t io_thread;
static pthread_t cm_thread;
#define IO_POLL_HINT 512
#define CM_POLL_HINT 16

static void *io_thread_proc(void *arg);
static void *cm_thread_proc(void *arg);

static void *cm_thread_proc(void *arg);
static void *server_proc(void *arg);


static struct ldms_sock_xprt *sock_from_xprt(ldms_t d)
{
	return ((struct ldms_xprt *)d)->private;
}

void sock_xprt_cleanup(void)
{
	void *dontcare;

	if (io_thread) {
		pthread_cancel(io_thread);
		pthread_join(io_thread, &dontcare);
	}
	if (cm_thread) {
		pthread_cancel(cm_thread);
		pthread_join(cm_thread, &dontcare);
	}
}

static void sock_xprt_close(struct ldms_xprt *x)
{
	struct ldms_sock_xprt *s = sock_from_xprt(x);
	epoll_ctl(io_event_fd, EPOLL_CTL_DEL, s->sock, NULL);
	close(s->sock);
}

static void sock_xprt_term(struct ldms_sock_xprt *r)
{
	LIST_REMOVE(r, client_link);
	free(r);
}

static int sock_xprt_connect(struct ldms_xprt *x, struct sockaddr *sa, socklen_t sa_len)
{
	struct ldms_sock_xprt *r = sock_from_xprt(x);
	int rc;

	r->sock = socket(PF_INET, SOCK_STREAM, 0);
	if (r->sock < 0)
		return errno;

	rc = connect(r->sock, sa, sa_len);
	if (rc) {
		close(r->sock);
		r->sock = 0;
		return errno;
	}

	r->conn_status = CONN_CONNECTED;
	x->connected = 1;

	/* Create the receive processing thread */
	rc = pthread_create(&r->cq_thread, NULL, server_proc, r);
	if (rc) {
		close(r->sock);
		r->conn_status = CONN_CLOSED;
		rc = ENOMEM;
	}
	return rc;
}

int process_sock_read_rsp(struct ldms_sock_xprt *x, struct ldms_request_hdr *hdr)
{
	struct sock_read_rsp rsp;
	size_t len;
	int ret;

	/* Read remainder of request */
	ret = recv(x->sock, &(((char *)&rsp)[sizeof *hdr]), sizeof(rsp) - sizeof(*hdr), MSG_WAITALL);
	if (ret < sizeof(rsp) - sizeof(*hdr)) { 
		printf("Short read %d receiving remainder of SOCK_READ_RSP_CMD.\n", ret);
		return EBADF;
	}

	/* Check the response status */
	if (rsp.status)
		return rsp.status;

	/* Read the set data into the local buffer */
	len = ntohl(rsp.buf_info.size);
	ret = recv(x->sock, (void *)(unsigned long)rsp.buf_info.lbuf, len, MSG_WAITALL);
	if (ret < len) {
		printf("Error %d receiving SOCK_READ_RSP_CMD data.\n", ret);
		return EBADF;
	}

	if (x->xprt && x->xprt->read_complete_cb)
		x->xprt->read_complete_cb(x->xprt, (void *)(unsigned long)hdr->xid);
	return 0;
}

int process_sock_read_req(struct ldms_sock_xprt *x, struct ldms_request_hdr *rq_hdr)
{
	struct sock_buf_remote_data buf_info;
	struct sock_read_rsp rsp;
	size_t len;
	int ret;

	/* Read remainder of request */
	ret = recv(x->sock, &buf_info, sizeof(buf_info), MSG_WAITALL);
	if (ret < sizeof(buf_info))
		return ENOTCONN;

	len = ntohl(buf_info.size);

	/* Prepare and send read response header */
	rsp.hdr.xid = rq_hdr->xid;
	rsp.hdr.cmd = htonl(SOCK_READ_RSP_CMD);
	rsp.hdr.len = htonl(sizeof(rsp) + len);
	rsp.status = 0;
	rsp.buf_info = buf_info;

	ret = send(x->sock, &rsp, sizeof(rsp), 0);
	if (ret < sizeof(rsp))
		return ENOTCONN;

	/* Write the requested local buffer back to the socket */
	ret = send(x->sock, (void *)(unsigned long)buf_info.rbuf, len, 0);
	if (ret < len)
		return ENOTCONN;

	return 0;
}

int process_sock_req(struct ldms_sock_xprt *x, struct ldms_request_hdr *req)
{
	switch (ntohl(req->cmd)) {
	case SOCK_READ_REQ_CMD:
		return process_sock_read_req(x, req);
	case SOCK_READ_RSP_CMD:
		return process_sock_read_rsp(x, req);
	default:
		printf("Invalid request on socket transport %d\n", req->cmd);
	}
	return EINVAL;
}

static int process_xprt_io(struct ldms_sock_xprt *s)
{
	struct ldms_request hdr;
	char *req_buf = (char *)&hdr;
	int ret;
	int reqlen;
	int cmd;

	/* Read the request header */
	ret = recv(s->sock, req_buf, sizeof(struct ldms_request_hdr), MSG_WAITALL);
	if (ret <= 0)
		goto close_out;

	cmd = ntohl(hdr.hdr.cmd);

	/* The sockets transport must handle solicited read */
	if (cmd & LDMS_CMD_XPRT_PRIVATE) {
		int ret = process_sock_req(s, (struct ldms_request_hdr *)req_buf);
		if (ret) {
			printf("Error %d processing transport request.\n", ret);
			goto close_out;
		}
	} else if (s->xprt && s->xprt->recv_cb) {
		/* Read the remainder of the request */
		reqlen = ntohl(hdr.hdr.len);
		if (ret < reqlen) {
			if (reqlen > sizeof(hdr)) {
				req_buf = malloc(reqlen);
				memcpy(req_buf, &hdr, ret);
			}
			ret = recv(s->sock, &req_buf[ret],
				   reqlen - ret, MSG_WAITALL);
			if (ret <= 0)
				goto close_out;
		}
		s->xprt->recv_cb(s->xprt, req_buf);
	}
	if (req_buf != (char *)&hdr)
		free(req_buf);
	return 0;

 close_out:
	if (req_buf != (char *)&hdr)
		free(req_buf);
	if (s->xprt) {
		ldms_xprt_close(s->xprt);
		ldms_release_xprt(s->xprt);
	}
	return -1;
}

static void *io_thread_proc(void *arg)
{
	struct epoll_event events[16];
	int nfds, fd;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	do {
		nfds = epoll_wait(io_event_fd, events, 16, -1);
		if (nfds < 0) {
			if (errno != EINTR)
				break;
			else
				continue;
		}
		for (fd = 0; fd < nfds; fd++)
			process_xprt_io(events[fd].data.ptr);
	} while (1);
	close(io_event_fd);
	return NULL;
}

static void *server_proc(void *arg)
{
	struct ldms_sock_xprt *s = arg;
	int ret;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	do {
		ret = process_xprt_io(s);
	} while (ret == 0);
	printf("Exiting %s.\n", __func__);
	return NULL;
}

static int sock_accept_request(struct ldms_sock_xprt *server,
			       int new_sock,
			       struct sockaddr_storage *ss,
			       socklen_t ss_len)
{
	struct epoll_event event;
	struct ldms_sock_xprt *r;
	struct ldms_xprt *x;
	ldms_t _x;
	int ret;

	/* Create a transport instance for this new connection */
	_x = ldms_create_xprt("sock");
	if (!_x) {
		fprintf(stdout, "Could not create a new transport.\n");
		close(new_sock);
		return EINVAL;
	}

	r = sock_from_xprt(_x);
	r->sock = new_sock;
	r->conn_status = CONN_CONNECTED;
	x = (struct ldms_xprt *)_x;
	x->ss = *ss;
	x->ss_len = ss_len;
	x->connected = 1;

	event.events = EPOLLIN;
	event.data.ptr = r;
	ret = epoll_ctl(io_event_fd, EPOLL_CTL_ADD, r->sock, &event);
	if (ret) {
		ldms_xprt_close(r->xprt);
		ldms_release_xprt(r->xprt);
	}

	return ret;
}

static void *cm_thread_proc(void *arg)
{
	struct epoll_event events[16];
	int nfds, fd;
	struct ldms_sock_xprt *r;
	int ret;

	do {
		pthread_testcancel();
		nfds = epoll_wait(cm_event_fd, events, 16, -1);
		if (nfds < 0) {
			if (errno != EINTR)
				break;
			else
				continue;
		}

		for (fd = 0; fd < nfds; fd++) {
			struct sockaddr_storage ss;
			socklen_t ss_len = sizeof(ss);
			r = events[fd].data.ptr;
			r->xprt->ss_len = sizeof(r->xprt->ss);
			ret = accept(r->sock, (struct sockaddr *)&ss, &ss_len);
			if (ret < 0) {
				printf("Error accepting new connection.\n");
			} else {
				ret = sock_accept_request(r, ret, &ss, ss_len);
				if (ret) {
					printf("Transport error processing accept request.\n");
				}
			}
		}
	} while (1);
	printf("Exiting %s.\n", __func__);
	return NULL;
}

static int sock_xprt_listen(struct ldms_xprt *x, struct sockaddr *sa, socklen_t sa_len)
{
	int rc;
	struct ldms_sock_xprt *r = sock_from_xprt(x);
	int optval = 1;

	/* This is a server */
	r->server = 1;

	r->sock = socket(PF_INET, SOCK_STREAM, 0);
	if (r->sock < 0) {
		rc = errno;
		goto err_0;
	}

	setsockopt(r->sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

	/* Bind to the provided address */
	rc = bind(r->sock, sa, sa_len);
	if (rc)
		goto err_0;

	rc = listen(r->sock, 10);
	if (rc)
		goto err_0;

	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.ptr = r;
	rc = epoll_ctl(cm_event_fd, EPOLL_CTL_ADD, r->sock, &event);
	if (rc)
		goto err_0;
	return rc;

 err_0:
	ldms_xprt_close(r->xprt);
	ldms_release_xprt(r->xprt);
	return rc;
}

static void sock_xprt_destroy(struct ldms_xprt *x)
{
	struct ldms_sock_xprt *r = sock_from_xprt(x);
	sock_xprt_term(r);
}

static int sock_xprt_send(struct ldms_xprt *x, void *buf, size_t len)
{
	struct ldms_sock_xprt *r = sock_from_xprt(x);
	int rc;

	if (r->conn_status != CONN_CONNECTED)
		return -ENOTCONN;

	rc = send(r->sock, buf, len, 0);
	if (rc == len)
		return 0;
	return -1;
}

/** Allocate a remote buffer. If we are the producer, the xprt_data
 *  will be NULL. In this case, we fill in the local side
 * information.
 */
struct ldms_rbuf_desc *sock_rbuf_alloc(struct ldms_xprt *x,
				       struct ldms_set *set,
				       enum ldms_rbuf_type type,
				       void *xprt_data,
				       size_t xprt_data_len)
{
	struct sock_buf_xprt_data *xd;
	struct ldms_rbuf_desc *desc = calloc(1, sizeof(struct ldms_rbuf_desc));
	if (!desc)
		return NULL;
	xd = calloc(1, sizeof *xd);
	if (!xd)
		goto err_0;

	if (xprt_data) {
		/* The peer has provided us with the remote buffer
		 * information. We need to fill in our local buffer
		 * information so we can fulfill a read response
		 * without looking anything up.
		 */
		desc->xprt_data_len = xprt_data_len;
		desc->xprt_data = xd;
		// ASSERT(xprt_data_len == sizeof(*xd));
		memcpy(xd, xprt_data, sizeof(*xd));
		xd->meta.lbuf = (uint64_t)(unsigned long)set->meta;
		xd->data.lbuf = (uint64_t)(unsigned long)set->data;
	} else {
		xd->meta.rbuf = (uint64_t)(unsigned long)set->meta;
		xd->data.rbuf = (uint64_t)(unsigned long)set->data;
		xd->meta.size = htonl(set->meta->meta_size);
		xd->data.size = htonl(set->meta->data_size);
		desc->xprt_data = xd;
		desc->xprt_data_len = sizeof(*xd);
	}
	return desc;

 err_0:
	free(desc);
	return NULL;
}

void sock_rbuf_free(struct ldms_xprt *x, struct ldms_rbuf_desc *desc)
{
	struct sock_buf_remote_data *rbuf = desc->xprt_data;
	if (rbuf)
		free(rbuf);
	free(desc);
}

static int sock_read_meta_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
{
	struct ldms_sock_xprt *r = sock_from_xprt(x);
	struct ldms_set_desc *sd = s;
	struct sock_buf_xprt_data* xd = sd->rbd->xprt_data;
	struct sock_read_req read_req;
	int rc;

	read_req.hdr.xid = (uint64_t)(unsigned long)context;
	read_req.hdr.cmd = htonl(SOCK_READ_REQ_CMD);
	read_req.hdr.len = htonl(sizeof(read_req));

	memcpy(&read_req.buf_info, &xd->meta, sizeof read_req.buf_info);
	if (len)
		read_req.buf_info.size = htonl(len);

	rc = send(r->sock, &read_req, sizeof(read_req), 0);
	if (rc == sizeof(read_req))
		return 0;
	return -1;
}

static int sock_read_data_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
{
	struct ldms_sock_xprt *r = sock_from_xprt(x);
	struct ldms_set_desc *sd = s;
	struct sock_buf_xprt_data* xd = sd->rbd->xprt_data;
	struct sock_read_req read_req;
	int rc;

	read_req.hdr.xid = (uint64_t)(unsigned long)context;
	read_req.hdr.cmd = htonl(SOCK_READ_REQ_CMD);
	read_req.hdr.len = htonl(sizeof(read_req));

	memcpy(&read_req.buf_info, &xd->data, sizeof read_req.buf_info);
	if (len)
		read_req.buf_info.size = htonl(len);

	rc = send(r->sock, &read_req, sizeof(read_req), 0);
	if (rc == sizeof(read_req))
		return 0;
	return -1;
}

static int once = 0;
static int init_once()
{
	int rc;
	io_event_fd = epoll_create(IO_POLL_HINT);
	if (io_event_fd < 0)
		return -errno;

	cm_event_fd = epoll_create(CM_POLL_HINT);
	rc = -errno;
	if (cm_event_fd < 0)
		goto err_0;

	rc = pthread_create(&io_thread, NULL, io_thread_proc, 0);
	if (rc)
		goto err_1;
	rc = pthread_create(&cm_thread, NULL, cm_thread_proc, 0);
	if (rc)
		goto err_2;

	atexit(sock_xprt_cleanup);
	return 0;
 err_2:
	pthread_cancel(io_thread);
 err_1:
	close(cm_event_fd);
 err_0:
	close(io_event_fd);
	return rc;
}

struct ldms_xprt *xprt_get(int (*recv_cb)(struct ldms_xprt *, void *),
			   int (*read_complete_cb)(struct ldms_xprt *, void *))
{
	struct ldms_xprt *x;
	struct ldms_sock_xprt *r;
	x = calloc(1, sizeof (*x));
	if (!x) {
		errno = ENOMEM;
		goto err_0;
	}
	if (!once) {
		int rc = init_once();
		if (rc) {
			errno = rc;
			goto err_1;
		}
		once = 1;
	}

	r = calloc(1, sizeof(struct ldms_sock_xprt));
	LIST_INSERT_HEAD(&sock_list, r, client_link);

	x->connect = sock_xprt_connect;
	x->listen = sock_xprt_listen;
	x->destroy = sock_xprt_destroy;
	x->close = sock_xprt_close;
	x->send = sock_xprt_send;
	x->read_meta_start = sock_read_meta_start;
	x->read_data_start = sock_read_data_start;
	x->read_complete_cb = read_complete_cb;
	x->recv_cb = recv_cb;
	x->alloc = sock_rbuf_alloc;
	x->free = sock_rbuf_free;
	x->private = r;
	r->xprt = x;

	sem_init(&r->sem, 0, 0);
	return x;
 err_1:
	free(x);
 err_0:
	return NULL;
}
