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
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include <coll/rbt.h>
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_sock_xprt.h"

#define LOG__(r, level, fmt, ...) do { \
	if (r && r->log) \
		r->log(level, fmt, ##__VA_ARGS__); \
} while(0)

#define LOG_(r, level, fmt, ...) do { \
	if (r && r->xprt && r->xprt->log) \
		r->xprt->log(level, fmt, ##__VA_ARGS__); \
} while(0)

#if USE_TF
#if (defined(__linux) && USE_TID)
#define TF(x) if(x && x->log) x->log(LDMS_LINFO,"Thd%lu:%s:%lu:%s\n", (unsigned long)pthread_self, __FUNCTION__, __LINE__,__FILE__)
#else
#define TF(x) if(x && x->log) x->log(LDMS_LINFO,"%s:%d\n", __FUNCTION__, __LINE__)
#endif /* linux tid */
#else
#define TF(x)
#endif /* 1 or 0 disable tf */

static struct event_base *io_event_loop;
static pthread_t io_thread;

pthread_mutex_t sock_list_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(sock_list, ldms_sock_xprt) sock_list;
pthread_mutex_t key_tree_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t last_key;
static int key_comparator(void *a, void *b)
{
	uint32_t x = (uint32_t)(unsigned long)a;
	uint32_t y = (uint32_t)(unsigned long)b;

	return x - y;
}
static struct rbt key_tree = {
	.root = NULL,
	.comparator = key_comparator
};

static struct sock_key *find_key_(uint32_t key)
{
	struct rbn *z;
	struct sock_key *k = NULL;
	z = rbt_find(&key_tree, (void *)(unsigned long)key);
	if (z)
		k = container_of(z, struct sock_key, rb_node);
	return k;
}

static struct sock_key *find_key(uint32_t key)
{
	struct sock_key *k;
	pthread_mutex_lock(&key_tree_lock);
	k = find_key_(key);
	pthread_mutex_unlock(&key_tree_lock);
	return k;
}


static struct sock_key *alloc_key(struct ldms_xprt *x, void *buf)
{
	struct rbn *z;
	struct sock_key *k = calloc(1, sizeof *k);
	if (!k)
		goto out;
	pthread_mutex_lock(&key_tree_lock);
 next_key:
	k->key = ++last_key;
	z = rbt_find(&key_tree, (void *)(unsigned long)last_key);
	if (z) {
		x->log(LDMS_LDEBUG, "%s: key collision at %d.\n", __func__, last_key);
		goto next_key;
	}
	k->buf = buf;
	k->rb_node.key = (void *)(unsigned long)k->key;
	rbt_ins(&key_tree, &k->rb_node);
	pthread_mutex_unlock(&key_tree_lock);
 out:
	return k;
}

static void delete_key(struct ldms_xprt *x, uint32_t key)
{
	struct sock_key *k;
	pthread_mutex_lock(&key_tree_lock);
	/* Make sure the key is in the tree */
	k = find_key_(key);
	if (!k) {
		x->log(LDMS_LDEBUG, "%s: The specified key %d, is not in the tree.\n", __func__, key);
		goto out;
	}
	rbt_del(&key_tree, &k->rb_node);
	free(k);
 out:
	pthread_mutex_unlock(&key_tree_lock);
}

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
	TF(x);
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
	free(r->xprt);
	free(r);
}

static int set_nonblock(struct ldms_xprt *x, int fd)
{
	TF(x);
	int flags;

	flags = fcntl(fd, F_GETFL);
	if(flags == -1) {
		x->log(LDMS_LDEBUG,"Error getting flags on fd %d", fd);
		return -1;
	}
	flags |= O_NONBLOCK;
	if(fcntl(fd, F_SETFL, flags)) {
		x->log(LDMS_LDEBUG,"Error setting non-blocking I/O on fd %d", fd);
		return -1;
	}
	return 0;
}

static int __set_socket_options(struct ldms_sock_xprt *s)
{

	int sd = s->sock;
	int val;

	val = 1;
	if (setsockopt(sd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val))) {
		LOG_(s, LDMS_LDEBUG, "SOCK: Error in setsockopt TCP_KEEPALIVE (val=%d\n):" /* fixed typo in log message*/
				" %m\n", val);
		goto err;
	}

	val = 1;
	if (setsockopt(sd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val))) {
		LOG_(s, LDMS_LDEBUG, "SOCK: Error in setsockopt TCP_KEEPCNT (val=%d\n):"
				" %m\n", val);
		goto err;
	}

	val = 1;
	if (setsockopt(sd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val))) {
		LOG_(s, LDMS_LDEBUG, "SOCK: Error in setsockopt TCP_KEEPIDLE (val=%d\n):"
				" %m\n", val);
		goto err;
	}

	val = 10;
	if (setsockopt(sd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val))) {
		LOG_(s, LDMS_LDEBUG, "SOCK: Error in setsockopt TCP_KEEPINTVL (val=%d\n):"
				" %m\n", val);
		goto err;
	}

	return 0;
err:
	return -1;
}

static int sock_xprt_check_proceed(struct ldms_xprt *x)
{
	return 0;
}

static int sock_xprt_connect(struct ldms_xprt *x,
			     struct sockaddr *sa, socklen_t sa_len)
{
	TF(x);
	struct ldms_sock_xprt *r = sock_from_xprt(x);
	struct sockaddr_storage ss;
	int epfd;
	int epcount;
	struct epoll_event event;

	r->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (r->sock < 0)
		return -1;

	int rc = __set_socket_options(r);
	if (rc)
		goto err;
	r->type = LDMS_SOCK_ACTIVE;
	fcntl(r->sock, F_SETFL, O_NONBLOCK);
	epfd = epoll_create(1);
	if (epfd < 0)
		goto err;
	rc = connect(r->sock, sa, sa_len);
	if (errno != EINPROGRESS) {
		close(epfd);
		goto err;
	}
	memset(&event,0,sizeof(event));  // reset random stack bits to zero
	event.events = EPOLLIN | EPOLLOUT | EPOLLHUP;
	event.data.fd = r->sock;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, r->sock, &event)) {
		close(epfd);
		goto err;
	}
	epcount = epoll_wait(epfd, &event, 1, 5000 /* 5s */);
	close(epfd);
	if (!epcount || (event.events & (EPOLLERR | EPOLLHUP)))
		goto err;

	fcntl(r->sock, F_SETFL, ~O_NONBLOCK);
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
	TF(x->xprt);
	size_t len;

	struct sock_key *k;

	/* Check the response status */
	if (rsp->status)
		return ntohl(rsp->status);

	/* Look up our local key */
	k = find_key(rsp->buf_info.lkey);
	if (!k)
		return EINVAL;

	/* Move the set data into the local buffer */
	len = ntohl(rsp->buf_info.size);
	memcpy(k->buf, (char *)(rsp+1), len);

	if (x->xprt && x->xprt->read_complete_cb)
		x->xprt->read_complete_cb(x->xprt,
					  (void *)(unsigned long)rsp->hdr.xid, 0);
	return 0;
}

uint64_t last_sock_read_req;
int process_sock_read_req(struct ldms_sock_xprt *x, struct sock_read_req *req)
{
	TF(x->xprt);
	struct sock_read_rsp rsp;
	size_t len;
	int ret;

	uint32_t key;
	struct sock_key *k;
	int status = 0;

	key = req->buf_info.rkey;
	k = find_key(key);
	if (!k)
		status = EINVAL;

	/* Prepare and send read response header */
	if (!status) {
		len = htonl(req->buf_info.size);
	} else
		len = 0;
	last_sock_read_req = rsp.hdr.xid = req->hdr.xid;
	rsp.hdr.cmd = htonl(SOCK_READ_RSP_CMD);
	rsp.hdr.len = htonl(sizeof(rsp) + len);

	rsp.status = htonl(status);

	memcpy(&rsp.buf_info, &req->buf_info, sizeof req->buf_info);

	ret = bufferevent_write(x->buf_event, &rsp, sizeof(rsp));
	if (ret < 0 || status)
		goto err;

	/* Write the requested local buffer back to the socket */
	ret =  bufferevent_write(x->buf_event, k->buf, len);
 err:
	return ret;
}

/* There is a known, expected, and by design ok data race
 * between process_sock_req and
 * ldms_begin/end_transaction calls as part of the overlap
 * of servicing data pull requests and sampler execution
 * in independent threads.
 */
int process_sock_req(struct ldms_sock_xprt *x, struct ldms_request *req)
{
	TF(x->xprt);
	switch (ntohl(req->hdr.cmd)) {
	case SOCK_READ_REQ_CMD:
		return process_sock_read_req(x, (struct sock_read_req *)req);
	case SOCK_READ_RSP_CMD:
		return process_sock_read_rsp(x, (struct sock_read_rsp *)req);
	default:
		x->xprt->log(LDMS_LDEBUG,"Invalid request on socket transport %d\n",
			     ntohl(req->hdr.cmd));
	}
	return EINVAL;
}

static void sock_xprt_error_handling(struct ldms_sock_xprt *s)
{
	TF(s->xprt);
	if (s->type == LDMS_SOCK_PASSIVE)
		ldms_xprt_close(s->xprt);
	else
		s->xprt->connected = 0;
}

static int process_xprt_io(struct ldms_sock_xprt *s, struct ldms_request *req)
{
	TF(s->xprt);
	int cmd;

	cmd = ntohl(req->hdr.cmd);

	/* The sockets transport must handle solicited read */
	if (cmd & LDMS_CMD_XPRT_PRIVATE) {
		int ret = process_sock_req(s, req);
		if (ret) {
			s->xprt->log(LDMS_LDEBUG,"Error %d processing transport request.\n",
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
		TF(r->xprt);
		req = malloc(reqlen);
		if (!req) {
			r->xprt->log(LDMS_LDEBUG,"%s Memory allocation failure reqlen %zu\n",
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
	TF(r->xprt);
	pthread_mutex_lock(&sock_list_lock);
	if (r->listen_ev) {
		evconnlistener_free(r->listen_ev);
		r->listen_ev = NULL;
	}
	if (r->buf_event) {
		bufferevent_free(r->buf_event);
		r->buf_event = NULL;
	}
	pthread_mutex_unlock(&sock_list_lock);
}

static void sock_event(struct bufferevent *buf_event, short events, void *arg)
{
	struct ldms_sock_xprt *r = arg;
	TF(r->xprt);

	if (events & ~BEV_EVENT_CONNECTED) {
		/* Peer disconnect or other error */
		if (events & (BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT))
			r->xprt->log(LDMS_LDEBUG,"Socket errors %#x\n", events);
		sock_xprt_error_handling(r);
	} else
		r->xprt->log(LDMS_LDEBUG,"Peer connect complete %#x\n", events);
}

static int _setup_connection(struct ldms_sock_xprt *r,
			      struct sockaddr *remote_addr, socklen_t sa_len)
{
	TF(r->xprt);
	int rc = 0;
	r->conn_status = CONN_CONNECTED;
	memcpy((char *)&r->xprt->remote_ss, (char *)remote_addr, sa_len);
	r->xprt->ss_len = sa_len;
	r->xprt->connected = 1;

	if (set_nonblock(r->xprt, r->sock))
		r->xprt->log(LDMS_LDEBUG,"Warning: error setting non-blocking I/O on an "
			     "incoming connection.\n");

	/* Initialize send and recv I/O events */
	r->buf_event = bufferevent_socket_new(io_event_loop, r->sock, BEV_OPT_THREADSAFE);
	if(!r->buf_event) {
		r->xprt->log(LDMS_LDEBUG,"Error initializing buffered I/O event for "
			     "fd %d.\n", r->sock);
		rc = -1;
		goto out;
	}
	bufferevent_setcb(r->buf_event, sock_read, sock_write, sock_event, r);
	if (bufferevent_enable(r->buf_event, EV_READ | EV_WRITE))
		r->xprt->log(LDMS_LDEBUG,"Error enabling buffered I/O event for fd %d.\n",
			     r->sock);
out:
	return rc;
}

static struct ldms_sock_xprt *
setup_connection(struct ldms_sock_xprt *p, int sockfd,
		 struct sockaddr *remote_addr, socklen_t sa_len)
{
	TF(p->xprt);
	struct ldms_sock_xprt *r;
	ldms_t _x;

	/* Create a transport instance for this new connection */
	_x = ldms_create_xprt("sock", p->xprt->log);
	if (!_x) {
		p->xprt->log(LDMS_LDEBUG,"Could not create a new transport.\n");
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

/* listening server accept equivalent */
static void sock_connect(struct evconnlistener *listener,
			 evutil_socket_t sockfd,
			 struct sockaddr *address, int socklen, void *arg)
{
	struct ldms_sock_xprt *r = arg;
	struct ldms_sock_xprt *new_r = NULL;
	TF(r->xprt);

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
	TF(x);
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
		x->log(LDMS_LDEBUG,"Warning: Could not set listening socket to non-blocking\n");

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
	TF(x);
	char lcl_buf[32];
	char rem_buf[32];
	struct sockaddr_in *lcl = (struct sockaddr_in *)&x->local_ss;
	struct sockaddr_in *rem = (struct sockaddr_in *)&x->remote_ss;

	(void)inet_ntop(AF_INET, &lcl->sin_addr, lcl_buf, sizeof(lcl_buf));
	(void)inet_ntop(AF_INET, &rem->sin_addr, rem_buf, sizeof(rem_buf));

	struct ldms_sock_xprt *r = sock_from_xprt(x);
	x->log(LDMS_LDEBUG,"sock_xprt_destroy: destroying %x\n", r);
	sock_xprt_term(r);
}

static int sock_xprt_send(struct ldms_xprt *x, void *buf, size_t len)
{
	TF(x);
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
	struct ldms_rbuf_desc *desc;
	struct sock_key *mk, *dk;

	desc = calloc(1, sizeof *desc);
	if (!desc)
		return NULL;

	if (xprt_data && sizeof(*xd) > xprt_data_len) {
		x->log(LDMS_LERROR,"%s:%d: sock_rbuf_alloc called with xprt_data size %zu."
			" Expected at least %zu.  "
			"Remote protocol implementation mismatch?\n",
			__FILE__,__LINE__, xprt_data_len, sizeof(*xd));
		return NULL;
	}
	if (xprt_data) {
		assert(xprt_data_len >= sizeof(struct sock_buf_xprt_data));
		xd = calloc(xprt_data_len,1);
	} else {
		xd = calloc(sizeof(struct sock_buf_xprt_data),1);
	}
	if (!xd)
		goto err_0;

	mk = alloc_key(x, set->meta);
	if (!mk)
		goto err_1;

	dk = alloc_key(x, set->data);
	if (!dk)
		goto err_2;

	if (xprt_data) {
		/*
		 * We did a lookup, this is the xprt_data provided by
		 * the peer. We need a local keys for our side of the
		 * data. The remote key was provided by the peer.
		 */
		desc->xprt_data_len = xprt_data_len;
		desc->xprt_data = xd;
		memcpy(xd, xprt_data, xprt_data_len);
		xd->meta.lkey = mk->key;
		xd->data.lkey = dk->key;
	} else {
		/*
		 * We're responding to a lookup. This is the rkey, the
		 * peer will use to read our data.
		 */
		xd->meta.size = htonl(set->meta->meta_size);
		xd->meta.rkey = mk->key;
		xd->meta.lkey = 0;
		xd->data.size = htonl(set->meta->data_size);
		xd->data.rkey = dk->key;
		xd->data.lkey = 0;
		desc->xprt_data = xd;
		desc->xprt_data_len = sizeof(*xd);
	}
	return desc;
 err_2:
	delete_key(x, mk->key);
 err_1:
	free(xd);
 err_0:
	free(desc);
	return NULL;
}

void sock_rbuf_free(struct ldms_xprt *x, struct ldms_rbuf_desc *desc)
{
	struct sock_buf_xprt_data *xd = desc->xprt_data;
	if (xd) {
		if (xd->meta.lkey == 0) {
			delete_key(x, xd->meta.rkey);
			delete_key(x, xd->data.rkey);
		} else {
			delete_key(x, xd->meta.lkey);
			delete_key(x, xd->data.lkey);
		}
		free(xd);
	}
	free(desc);
}

static int sock_read_meta_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
{
	TF(x);
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
	TF(x);
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
	pthread_mutex_init(&sock_list_lock, 0);
	pthread_mutex_init(&key_tree_lock, 0);
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

struct ldms_xprt *xprt_get(recv_cb_t recv_cb,
			   read_complete_cb_t read_complete_cb,
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
	x->log(LDMS_LDEBUG,"xprt_get: created %x\n", r);
	TF(x);
	x->connect = sock_xprt_connect;
	x->check_proceed = sock_xprt_check_proceed;
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
