/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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

/*
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_sock_xprt.h"

#define LOG__(r, fmt, ...) do { \
	if (r && r->log) \
		r->log(fmt, ##__VA_ARGS__); \
} while(0)

#define LOG_(r, fmt, ...) do { \
	if (r && r->xprt && r->xprt->log) \
		r->xprt->log(fmt, ##__VA_ARGS__); \
} while(0)

pthread_mutex_t sock_lock;

static struct event_base *io_event_loop;
static pthread_t io_thread;

LIST_HEAD(sock_list, ldms_sock_xprt) sock_list;

static void *io_thread_proc(void *arg);

static void sock_event(struct bufferevent *buf_event, short error, void *arg);
static void sock_read(struct bufferevent *buf_event, void *arg);
static void sock_write(struct bufferevent *buf_event, void *arg);

static void timeout_cb(int fd , short events, void *arg);
static struct ldms_sock_xprt * setup_connection(struct ldms_sock_xprt *x,
						int sockfd,
						struct sockaddr*remote_addr,
						socklen_t sa_len);
static void sock_xprt_error_handling(struct ldms_sock_xprt *s);

static int _setup_connection(struct ldms_sock_xprt *r,
			      struct sockaddr *remote_addr, socklen_t sa_len);

static int __set_socket_options(struct ldms_sock_xprt *s);

static void release_buf_event(struct ldms_sock_xprt *r);

static struct ldms_sock_xprt *sock_from_xprt(ldms_t d)
{
	return ((struct ldms_xprt *)d)->private;
}

void sock_xprt_cleanup(void)
{
	void *dontcare;

	if (io_event_loop)
		event_base_loopbreak(io_event_loop);
	if (io_thread) {
		pthread_cancel(io_thread);
		pthread_join(io_thread, &dontcare);
	}
	if (io_event_loop)
		event_base_free(io_event_loop);
}

static void sock_xprt_close(struct ldms_xprt *x)
{
	struct ldms_sock_xprt *s = sock_from_xprt(x);
	release_buf_event(s);
	close(s->sock);
	s->sock = 0;
}

static void sock_xprt_term(struct ldms_sock_xprt *r)
{
	LIST_REMOVE(r, client_link);
	if (r->listen_ev)
		free(r->listen_ev);
	free(r);
}

static int set_nonblock(struct ldms_xprt *x, int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if(flags == -1) {
		x->log("Error getting flags on fd %d", fd);
		return -1;
	}
	flags |= O_NONBLOCK;
	if(fcntl(fd, F_SETFL, flags)) {
		x->log("Error setting non-blocking I/O on fd %d", fd);
		return -1;
	}
	return 0;
}

static int __set_socket_options(struct ldms_sock_xprt *s)
{
	int rc;
	int sd = s->sock;
	int val;

	val = 1;
	if (setsockopt(sd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val))) {
		LOG_(s, "SOCK: Error in setsockopt TCP_KEEPCNT (val=%d\n):"
				" %m\n", val);
		goto err;
	}

	val = 1;
	if (setsockopt(sd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val))) {
		LOG_(s, "SOCK: Error in setsockopt TCP_KEEPCNT (val=%d\n):"
				" %m\n", val);
		goto err;
	}

	val = 1;
	if (setsockopt(sd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val))) {
		LOG_(s, "SOCK: Error in setsockopt TCP_KEEPIDLE (val=%d\n):"
				" %m\n", val);
		goto err;
	}

	val = 10;
	if (setsockopt(sd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val))) {
		LOG_(s, "SOCK: Error in setsockopt TCP_KEEPINTVL (val=%d\n):"
				" %m\n", val);
		goto err;
	}

	return 0;
err:
	return -1;
}

static int sock_xprt_connect(struct ldms_xprt *x,
			     struct sockaddr *sa, socklen_t sa_len)
{
	struct ldms_sock_xprt *r = sock_from_xprt(x);
	struct sockaddr_storage ss;

	r->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (r->sock < 0)
		return -1;
	int rc = __set_socket_options(r);
	if (rc)
		goto err;
	r->type = LDMS_SOCK_ACTIVE;
	rc = connect(r->sock, sa, sa_len);
	if (rc)
		goto err;
	sa_len = sizeof(ss);
	rc = getsockname(r->sock, (struct sockaddr *)&ss, &sa_len);
	if (rc)
		goto err;
	if (_setup_connection(r, (struct sockaddr *)&ss, sa_len) != 0)
		goto err;
	return 0;

err:
	return -1;
}
int process_sock_read_rsp(struct ldms_sock_xprt *x, struct sock_read_rsp *rsp)
{
	size_t len;
	/* Check the response status */
	if (rsp->status)
		return ntohl(rsp->status);

	/* Move the set data into the local buffer */
	len = ntohl(rsp->buf_info.size);
	memcpy((void *)(unsigned long)rsp->buf_info.lbuf, (char *)(rsp+1), len);

	if (x->xprt && x->xprt->read_complete_cb)
		x->xprt->read_complete_cb(x->xprt, (void *)(unsigned long)rsp->hdr.xid);
	return 0;
}

uint64_t last_sock_read_req;
int process_sock_read_req(struct ldms_sock_xprt *x, struct sock_read_req *req)
{
	struct sock_read_rsp rsp;
	size_t len;
	int ret;

	len = ntohl(req->buf_info.size);

	/* Prepare and send read response header */
	last_sock_read_req = rsp.hdr.xid = req->hdr.xid;
	rsp.hdr.cmd = htonl(SOCK_READ_RSP_CMD);
	rsp.hdr.len = htonl(sizeof(rsp) + len);
	rsp.status = 0;
	memcpy(&rsp.buf_info, &req->buf_info, sizeof req->buf_info);

	ret = bufferevent_write(x->buf_event, &rsp, sizeof(rsp));
	if (ret < 0)
		goto err;

	/* Write the requested local buffer back to the socket */
	ret =  bufferevent_write(x->buf_event,
			 (void *)(unsigned long)req->buf_info.rbuf, len);
 err:
	return ret;
}

int process_sock_req(struct ldms_sock_xprt *x, struct ldms_request *req)
{
	switch (ntohl(req->hdr.cmd)) {
	case SOCK_READ_REQ_CMD:
		return process_sock_read_req(x, (struct sock_read_req *)req);
	case SOCK_READ_RSP_CMD:
		return process_sock_read_rsp(x, (struct sock_read_rsp *)req);
	default:
		x->xprt->log("Invalid request on socket transport %d\n",
			     ntohl(req->hdr.cmd));
	}
	return EINVAL;
}

static void sock_xprt_error_handling(struct ldms_sock_xprt *s)
{
	if (s->type == LDMS_SOCK_PASSIVE)
		ldms_xprt_close(s->xprt);
	else
		s->xprt->connected = 0;
}

static int process_xprt_io(struct ldms_sock_xprt *s, struct ldms_request *req)
{
	int cmd;

	cmd = ntohl(req->hdr.cmd);

	/* The sockets transport must handle solicited read */
	if (cmd & LDMS_CMD_XPRT_PRIVATE) {
		int ret = process_sock_req(s, req);
		if (ret) {
			s->xprt->log("Error %d processing transport request.\n",
				     ret);
			goto close_out;
		}
	} else
		s->xprt->recv_cb(s->xprt, req);
	return 0;
 close_out:
	sock_xprt_error_handling(s);
	return -1;
}

static void sock_write(struct bufferevent *buf_event, void *arg)
{
}

#define min_t(t, x, y) (t)((t)x < (t)y?(t)x:(t)y)
static void sock_read(struct bufferevent *buf_event, void *arg)
{

	struct ldms_sock_xprt *r = (struct ldms_sock_xprt *)arg;
	struct evbuffer *evb;
	struct ldms_request_hdr hdr;
	struct ldms_request *req;
	size_t len;
	size_t reqlen;
	size_t buflen;
	do {
		evb = bufferevent_get_input(buf_event);
		buflen = evbuffer_get_length(evb);
		if (buflen < sizeof(hdr))
			break;
		evbuffer_copyout(evb, &hdr, sizeof(hdr));
		reqlen = ntohl(hdr.len);
		if (buflen < reqlen)
			break;
		req = malloc(reqlen);
		if (!req) {
			r->xprt->log("%s Memory allocation failure reqlen %zu\n",
				     __FUNCTION__, reqlen);
			sock_xprt_error_handling(r);
			break;
		}
		len = evbuffer_remove(evb, req, reqlen);
		assert(len == reqlen);
		process_xprt_io(r, req);
		free(req);
	} while (1);
}

static void *io_thread_proc(void *arg)
{
	event_base_dispatch(io_event_loop);
	return NULL;
}

static void release_buf_event(struct ldms_sock_xprt *r)
{
	pthread_mutex_lock(&sock_lock);
	if (r->listen_ev) {
		evconnlistener_free(r->listen_ev);
		r->listen_ev = NULL;
	}
	if (r->buf_event) {
		bufferevent_free(r->buf_event);
		r->buf_event = NULL;
	}
	pthread_mutex_unlock(&sock_lock);
}

static void sock_event(struct bufferevent *buf_event, short events, void *arg)
{
	struct ldms_sock_xprt *r = arg;

	if (events & ~BEV_EVENT_CONNECTED) {
		/* Peer disconnect or other error */
		if (events & (BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT))
			r->xprt->log("Socket errors %#x\n", events);
		sock_xprt_error_handling(r);
	} else
		r->xprt->log("Peer connect complete %#x\n", events);
}

static int _setup_connection(struct ldms_sock_xprt *r,
			      struct sockaddr *remote_addr, socklen_t sa_len)
{
	int rc = 0;
	r->conn_status = CONN_CONNECTED;
	memcpy((char *)&r->xprt->remote_ss, (char *)remote_addr, sa_len);
	r->xprt->ss_len = sa_len;
	r->xprt->connected = 1;

	if (set_nonblock(r->xprt, r->sock))
		r->xprt->log("Warning: error setting non-blocking I/O on an "
			     "incoming connection.\n");

	/* Initialize send and recv I/O events */
	r->buf_event = bufferevent_socket_new(io_event_loop, r->sock, BEV_OPT_THREADSAFE);
	if(!r->buf_event) {
		r->xprt->log("Error initializing buffered I/O event for "
			     "fd %d.\n", r->sock);
		rc = -1;
		goto out;
	}
	bufferevent_setcb(r->buf_event, sock_read, sock_write, sock_event, r);
	if (bufferevent_enable(r->buf_event, EV_READ | EV_WRITE))
		r->xprt->log("Error enabling buffered I/O event for fd %d.\n",
			     r->sock);
out:
	return rc;
}

static struct ldms_sock_xprt *
setup_connection(struct ldms_sock_xprt *p, int sockfd,
		 struct sockaddr *remote_addr, socklen_t sa_len)
{
	struct ldms_sock_xprt *r;
	ldms_t _x;

	/* Create a transport instance for this new connection */
	_x = ldms_create_xprt("sock", p->xprt->log);
	if (!_x) {
		p->xprt->log("Could not create a new transport.\n");
		close(sockfd);
		return NULL;
	}

	r = sock_from_xprt(_x);
	r->type = LDMS_SOCK_PASSIVE;
	r->sock = sockfd;
	r->xprt->local_ss = p->xprt->local_ss;
	if (_setup_connection(r, remote_addr, sa_len) != 0) {
		sock_xprt_error_handling(r);
		return NULL;
	}
	return r;
}

static void sock_connect(struct evconnlistener *listener,
			 evutil_socket_t sockfd,
			 struct sockaddr *address, int socklen, void *arg)
{
	struct ldms_sock_xprt *r = arg;
	struct ldms_sock_xprt *new_r = NULL;

	new_r = setup_connection(r, sockfd, (struct sockaddr *)address, socklen);
	if (!new_r)
		return;

	int rc = __set_socket_options(new_r);
	if (rc) {
		sock_xprt_error_handling(new_r);
		return;
	}

}

static int sock_xprt_listen(struct ldms_xprt *x, struct sockaddr *sa, socklen_t sa_len)
{
	int rc;
	struct ldms_sock_xprt *r = sock_from_xprt(x);
	int optval = 1;

	r->sock = socket(PF_INET, SOCK_STREAM, 0);
	if (r->sock < 0) {
		rc = errno;
		goto err_0;
	}

	setsockopt(r->sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

	if (set_nonblock(x, r->sock))
		x->log("Warning: Could not set listening socket to non-blocking\n");

	rc = ENOMEM;
	r->listen_ev = evconnlistener_new_bind(io_event_loop, sock_connect, r,
					       LEV_OPT_THREADSAFE | LEV_OPT_REUSEABLE,
					       1024, sa, sa_len);
	if (!r->listen_ev)
		goto err_0;

	r->sock = evconnlistener_get_fd(r->listen_ev);
	return 0;
 err_0:
	/* close and destroy if listen failed */
	ldms_xprt_close(r->xprt);
	return rc;
}

static void sock_xprt_destroy(struct ldms_xprt *x)
{
	char lcl_buf[32];
	char rem_buf[32];
	struct sockaddr_in *lcl = (struct sockaddr_in *)&x->local_ss;
	struct sockaddr_in *rem = (struct sockaddr_in *)&x->remote_ss;

	(void)inet_ntop(AF_INET, &lcl->sin_addr, lcl_buf, sizeof(lcl_buf));
	(void)inet_ntop(AF_INET, &rem->sin_addr, rem_buf, sizeof(rem_buf));

	struct ldms_sock_xprt *r = sock_from_xprt(x);
	sock_xprt_term(r);
}

static int sock_xprt_send(struct ldms_xprt *x, void *buf, size_t len)
{
	struct ldms_sock_xprt *r = sock_from_xprt(x);
	int rc;

	if (r->conn_status != CONN_CONNECTED)
		return -ENOTCONN;

	rc = bufferevent_write(r->buf_event, buf, len);
	return rc;
}

/** Allocate a remote buffer. If we are the producer, the xprt_data
 *  will be NULL. In this case, we fill in the local side
 * information.
 */
struct ldms_rbuf_desc *sock_rbuf_alloc(struct ldms_xprt *x,
				       struct ldms_set *set,
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

	rc = bufferevent_write(r->buf_event, &read_req, sizeof(read_req));
	return rc;
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

	rc = bufferevent_write(r->buf_event, &read_req, sizeof(read_req));
	return rc;
}

static struct timeval to;
static struct event *keepalive;
static void timeout_cb(int s, short events, void *arg)
{
	to.tv_sec = 10;
	to.tv_usec = 0;
	evtimer_add(keepalive, &to);
}

static int once = 0;
static int init_once()
{
	int rc = ENOMEM;

	evthread_use_pthreads();
	pthread_mutex_init(&sock_lock, 0);
	io_event_loop = event_base_new();
	if (!io_event_loop)
		return errno;

	keepalive = evtimer_new(io_event_loop, timeout_cb, NULL);
	if (!keepalive)
		goto err_1;

	to.tv_sec = 1;
	to.tv_usec = 0;
	evtimer_add(keepalive, &to);

	rc = pthread_create(&io_thread, NULL, io_thread_proc, 0);
	if (rc)
		goto err_1;

	atexit(sock_xprt_cleanup);
	return 0;

 err_1:
	event_base_free(io_event_loop);
	return rc;
}

struct ldms_xprt *xprt_get(int (*recv_cb)(struct ldms_xprt *, void *),
			   int (*read_complete_cb)(struct ldms_xprt *, void *),
			   ldms_log_fn_t log_fn)
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

	x->max_msg = (1024 * 1024);
	x->log = log_fn;
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

	return x;
 err_1:
	free(x);
 err_0:
	return NULL;
}
