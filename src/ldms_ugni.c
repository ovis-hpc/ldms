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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include <gni_pub.h>
#include <pmi.h>
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_ugni.h"

/* Gemini Transport Variables */
int first;
int job_size;
uint8_t ptag;
int cookie;
int modes = 0;
int device_id = 0;
int cq_entries = 128;
gni_cdm_handle_t cdm_handle;
gni_nic_handle_t nic_handle;
uint32_t local_address;

static struct event_base *io_event_loop;
static pthread_t io_thread;

pthread_mutex_t ugni_list_lock;
LIST_HEAD(ugni_list, ldms_ugni_xprt) ugni_list;
pthread_mutex_t desc_list_lock;
LIST_HEAD(desc_list, ugni_desc) desc_list;

static void *io_thread_proc(void *arg);

static void ugni_event(struct bufferevent *buf_event, short error, void *arg);
static void ugni_read(struct bufferevent *buf_event, void *arg);
static void ugni_write(struct bufferevent *buf_event, void *arg);

static void timeout_cb(int fd , short events, void *arg);
static struct ldms_ugni_xprt * setup_connection(struct ldms_ugni_xprt *x,
						int sockfd,
						struct sockaddr*remote_addr,
						socklen_t sa_len);
static void _setup_connection(struct ldms_ugni_xprt *r,
			      struct sockaddr *remote_addr, socklen_t sa_len);

static void release_buf_event(struct ldms_ugni_xprt *r);

static struct ldms_ugni_xprt *ugni_from_xprt(ldms_t d)
{
	return ((struct ldms_xprt *)d)->private;
}

void ugni_xprt_cleanup(void)
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

static void ugni_xprt_close(struct ldms_xprt *x)
{
	struct ldms_ugni_xprt *s = ugni_from_xprt(x);

	release_buf_event(s);
	close(s->sock);
	s->sock = 0;
}

static void ugni_xprt_term(struct ldms_ugni_xprt *r)
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

static int ugni_xprt_connect(struct ldms_xprt *x,
			     struct sockaddr *sa, socklen_t sa_len)
{
	struct ldms_ugni_xprt *r = ugni_from_xprt(x);
	struct sockaddr_storage ss;

	r->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (r->sock < 0)
		return -1;
	int rc = connect(r->sock, sa, sa_len);
	if (rc)
		goto err;
	sa_len = sizeof(ss);
	rc = getsockname(r->sock, (struct sockaddr *)&ss, &sa_len);
	if (rc)
		goto err;
	_setup_connection(r, (struct sockaddr *)&ss, sa_len);
	return 0;

err:
	close(r->sock);
	r->sock = 0;
	return -1;
}

int process_ugni_hello_req(struct ldms_ugni_xprt *x, struct ugni_hello_req *req)
{
	int rc;
	x->ugni_remote_address = ntohl(req->address);
	rc = GNI_EpBind(x->ugni_ep, x->ugni_remote_address, local_address);
	if (rc = GNI_RC_SUCCESS)
		x->xprt->connected = 1;
	return rc;
}

int process_ugni_msg(struct ldms_ugni_xprt *x, struct ldms_request *req)
{
	switch (ntohl(req->hdr.cmd)) {
	case UGNI_HELLO_REQ_CMD:
		return process_ugni_hello_req(x, (struct ugni_hello_req *)req);
	default:
		x->xprt->log("Invalid request on uGNI transport %d\n",
			     ntohl(req->hdr.cmd));
	}
	return EINVAL;
}

static int process_xprt_io(struct ldms_ugni_xprt *s, struct ldms_request *req)
{
	int cmd;

	cmd = ntohl(req->hdr.cmd);

	/* The sockets transport must handle solicited read */
	if (cmd & LDMS_CMD_XPRT_PRIVATE) {
		int ret = process_ugni_msg(s, req);
		if (ret) {
			s->xprt->log("Error %d processing transport request.\n",
				     ret);
			goto close_out;
		}
	} else
		s->xprt->recv_cb(s->xprt, req);
	return 0;
 close_out:
	ldms_xprt_close(s->xprt);
	ldms_release_xprt(s->xprt);
	return -1;
}

static void ugni_write(struct bufferevent *buf_event, void *arg)
{
}

#define min_t(t, x, y) (t)((t)x < (t)y?(t)x:(t)y)
static void ugni_read(struct bufferevent *buf_event, void *arg)
{

	struct ldms_ugni_xprt *r = (struct ldms_ugni_xprt *)arg;
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
			ldms_xprt_close(r->xprt);
			ldms_release_xprt(r->xprt);
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

static void release_buf_event(struct ldms_ugni_xprt *r)
{
	pthread_mutex_lock(&ugni_list_lock);
	if (r->listen_ev) {
		evconnlistener_free(r->listen_ev);
		r->listen_ev = NULL;
	}
	if (r->buf_event) {
		bufferevent_free(r->buf_event);
		r->buf_event = NULL;
	}
	pthread_mutex_unlock(&ugni_list_lock);
}

static void ugni_event(struct bufferevent *buf_event, short events, void *arg)
{
	struct ldms_ugni_xprt *r = arg;

	if (events & ~BEV_EVENT_CONNECTED) {
		/* Peer disconnect or other error */
		if (events & (BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT))
			r->xprt->log("Socket errors %x\n", events);
		r->xprt->connected = 0;
		ldms_xprt_close(r->xprt);
		ldms_release_xprt(r->xprt);
	} else
		r->xprt->log("Peer connect complete %x\n", events);
}

struct ugni_desc *alloc_desc()
{
	struct ugni_desc *desc = NULL;
	pthread_mutex_lock(&ugni_list_lock);
	if (!LIST_EMPTY(&desc_list)) {
		desc = LIST_FIRST(&desc_list);
		LIST_REMOVE(desc, link);
	}
	pthread_mutex_lock(&ugni_list_lock);
	return (desc ? desc : calloc(1, sizeof *desc));
}

void free_desc(struct ugni_desc *desc)
{
	pthread_mutex_lock(&ugni_list_lock);
	LIST_INSERT_HEAD(&desc_list, desc, link);
	pthread_mutex_unlock(&ugni_list_lock);
}

void cq_complete(gni_cq_entry_t *_cqe, void *_r)
{
	struct ldms_ugni_xprt *r = _r;
	gni_cq_entry_t cqe;
	struct ugni_desc *desc;
	gni_post_descriptor_t *post;

	int rc = GNI_CqGetEvent(r->ugni_cq, &cqe);
	r->xprt->log("GNI_CqGetEvent returns %d\n", rc);
	rc = GNI_GetCompleted(r->ugni_cq, cqe, &post);
	desc = (struct ugni_desc *)post;
	if (r->xprt && r->xprt->read_complete_cb)
		r->xprt->read_complete_cb(r->xprt, desc->context);
	free_desc(desc);
}

static void _setup_connection(struct ldms_ugni_xprt *r,
			      struct sockaddr *remote_addr, socklen_t sa_len)
{
	struct ugni_hello_req req;
	int rc;

	r->conn_status = CONN_CONNECTED;
	memcpy((char *)&r->xprt->remote_ss, (char *)remote_addr, sa_len);
	r->xprt->ss_len = sa_len;

	if (set_nonblock(r->xprt, r->sock))
		r->xprt->log("Warning: error setting non-blocking I/O on an "
			     "incoming connection.\n");

	/* Initialize send and recv I/O events */
	r->buf_event = bufferevent_socket_new(io_event_loop, r->sock, BEV_OPT_THREADSAFE);
	if(!r->buf_event) {
		r->xprt->log("Error initializing buffered I/O event for "
			     "fd %d.\n", r->sock);
		goto err_0;
	}
	bufferevent_setcb(r->buf_event, ugni_read, ugni_write, ugni_event, r);
	if (bufferevent_enable(r->buf_event, EV_READ | EV_WRITE))
		r->xprt->log("Error enabling buffered I/O event for fd %d.\n",
			     r->sock);
	rc = GNI_CqCreate(nic_handle, 16, 0, GNI_CQ_NOBLOCK,
			  cq_complete, r, &r->ugni_cq);
	if (rc != GNI_RC_SUCCESS)
		goto err_0;
	rc = GNI_EpCreate(nic_handle, r->ugni_cq, &r->ugni_ep);
	if (rc != GNI_RC_SUCCESS)
		goto err_0;

	/* When we receive the peer's hello, we move the endpoint to connected */
	req.hdr.cmd = htonl(UGNI_HELLO_REQ_CMD);
	req.hdr.len = htonl(sizeof(req));
	req.hdr.xid = (uint64_t)(unsigned long)0; /* no response to hello */
	req.address = local_address;
	(void)bufferevent_write(r->buf_event, &req, sizeof(req));
	return;

 err_0:
	ldms_xprt_close(r->xprt);
	ldms_release_xprt(r->xprt);
}

static struct ldms_ugni_xprt *
setup_connection(struct ldms_ugni_xprt *p, int sockfd,
		 struct sockaddr *remote_addr, socklen_t sa_len)
{
	struct ldms_ugni_xprt *r;
	ldms_t _x;

	/* Create a transport instance for this new connection */
	_x = ldms_create_xprt("ugni", p->xprt->log);
	if (!_x) {
		p->xprt->log("Could not create a new transport.\n");
		close(sockfd);
		return NULL;
	}

	r = ugni_from_xprt(_x);
	r->sock = sockfd;
	r->xprt->local_ss = p->xprt->local_ss;
	_setup_connection(r, remote_addr, sa_len);
	return r;
}

struct timeval listen_tv;
struct timeval report_tv;
// #define CRAY_BUG
#ifdef CRAY_BUG
struct timeval last_tv;
#endif

static void ugni_connect(struct evconnlistener *listener,
			 evutil_socket_t sockfd,
			 struct sockaddr *address, int socklen, void *arg)
{
	struct ldms_ugni_xprt *r = arg;
	struct ldms_ugni_xprt *new_r = NULL;
	static int conns;
	
	new_r = setup_connection(r, sockfd, (struct sockaddr *)&address, socklen);
	if (new_r)
		conns ++;

	struct timeval connect_tv;
	gettimeofday(&connect_tv, NULL);

	if ((connect_tv.tv_sec - report_tv.tv_sec) >= 10) {
		double rate;
		rate = (double)conns / (double)(connect_tv.tv_sec - report_tv.tv_sec);
		r->xprt->log("Connection rate is %.2f connectios/second\n", rate);
		report_tv = connect_tv;
		conns = 0;
	}
}

static int ugni_xprt_listen(struct ldms_xprt *x, struct sockaddr *sa, socklen_t sa_len)
{
	int rc;
	struct ldms_ugni_xprt *r = ugni_from_xprt(x);
	int optval = 1;

	gettimeofday(&listen_tv, NULL);
	report_tv = listen_tv;

	r->sock = socket(PF_INET, SOCK_STREAM, 0);
	if (r->sock < 0) {
		rc = errno;
		goto err_0;
	}

	setsockopt(r->sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

	if (set_nonblock(x, r->sock))
		x->log("Warning: Could not set listening socket to non-blocking\n");

	rc = ENOMEM;
	r->listen_ev = evconnlistener_new_bind(io_event_loop, ugni_connect, r,
					       LEV_OPT_THREADSAFE | LEV_OPT_REUSEABLE,
					       1024, sa, sa_len);
	if (!r->listen_ev)
		goto err_0;

	r->sock = evconnlistener_get_fd(r->listen_ev);
	return 0;
 err_0:
	ldms_xprt_close(r->xprt);
	ldms_release_xprt(r->xprt);
	return rc;
}

static void ugni_xprt_destroy(struct ldms_xprt *x)
{
	struct ldms_ugni_xprt *r = ugni_from_xprt(x);
	ugni_xprt_term(r);
}

static int ugni_xprt_send(struct ldms_xprt *x, void *buf, size_t len)
{
	struct ldms_ugni_xprt *r = ugni_from_xprt(x);
	int rc;

	if (r->conn_status != CONN_CONNECTED)
		return -ENOTCONN;

	rc = bufferevent_write(r->buf_event, buf, len);
	return rc;
}

/*
 * Allocate a remote buffer. If we are the producer, the xprt_data
 * will be NULL. In this case, we fill in the local side
 * information.
 */
struct ldms_rbuf_desc *ugni_rbuf_alloc(struct ldms_xprt *x,
				       struct ldms_set *set,
				       enum ldms_rbuf_type type,
				       void *xprt_data,
				       size_t xprt_data_len)
{
	int rc;
	struct ldms_ugni_xprt *r = ugni_from_xprt(x);
	struct ugni_buf_local_data *lcl_data;
	struct ldms_rbuf_desc *desc = calloc(1, sizeof(struct ldms_rbuf_desc));
	if (!desc)
		return NULL;

	lcl_data = calloc(1, sizeof *lcl_data);
	if (!lcl_data)
		goto err_0;

	desc->type  = type;

	lcl_data->meta = set->meta;
	lcl_data->meta_size = set->meta->meta_size;
	rc = GNI_MemRegister(nic_handle,
			     (uint64_t)(unsigned long)lcl_data->meta,
			     lcl_data->meta_size,
			     r->ugni_cq, GNI_MEM_READWRITE,
			     -1, &lcl_data->meta_mh);
	if (rc != GNI_RC_SUCCESS)
		goto err_1;

	lcl_data->data = set->data;
	lcl_data->data_size = set->meta->data_size;
	rc = GNI_MemRegister(nic_handle,
			     (uint64_t)(unsigned long)lcl_data->data,
			     lcl_data->data_size,
			     r->ugni_cq, GNI_MEM_READWRITE,
			     -1, &lcl_data->data_mh);
	if (rc != GNI_RC_SUCCESS)
		goto err_2;

	desc->lcl_data = lcl_data;

	if (xprt_data) {
		desc->xprt_data_len = xprt_data_len;
		desc->xprt_data = calloc(1, xprt_data_len);
		if (!desc->xprt_data)
			goto err_3;
		memcpy(desc->xprt_data, xprt_data, xprt_data_len);
	} else {
		struct ugni_buf_remote_data *rem_data = calloc(1, sizeof *rem_data);
		if (!rem_data)
			goto err_3;
		rem_data->meta_buf = (uint64_t)(unsigned long)lcl_data->meta;
		rem_data->meta_size = htonl(lcl_data->meta_size);
		rem_data->meta_mh = lcl_data->meta_mh;
		rem_data->data_buf = (uint64_t)(unsigned long)lcl_data->data;
		rem_data->data_size = htonl(lcl_data->data_size);
		rem_data->data_mh = lcl_data->data_mh;
		desc->xprt_data = rem_data;
		desc->xprt_data_len = sizeof(*rem_data);
	}
	return desc;
 err_3:
	(void)GNI_MemDeRegister(nic_handle, &lcl_data->data_mh);
 err_2:
	(void)GNI_MemDeRegister(nic_handle, &lcl_data->meta_mh);
 err_1:
	free(lcl_data);
 err_0:
	free(desc);
	return NULL;
}

void ugni_rbuf_free(struct ldms_xprt *x, struct ldms_rbuf_desc *desc)
{
	struct ugni_buf_remote_data *rbuf = desc->xprt_data;
	if (rbuf)
		free(rbuf);
	free(desc);
}

static int ugni_read_start(struct ldms_ugni_xprt *r,
			   uint64_t laddr, gni_mem_handle_t local_mh,
			   uint64_t raddr, gni_mem_handle_t remote_mh,
			   uint32_t len, void *context)
{
	int rc;
	struct ugni_desc *desc = alloc_desc();
	if (!desc)
		return ENOMEM;

	memset(&desc, 0, sizeof(*desc));
	desc->fma.type = GNI_POST_FMA_GET;
	desc->fma.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->fma.dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	desc->fma.local_addr = laddr;
	desc->fma.local_mem_hndl = local_mh;
	desc->fma.remote_addr = raddr;
	desc->fma.remote_mem_hndl = remote_mh;
	desc->fma.length = len;
	desc->context = context;
	rc = GNI_PostFma(r->ugni_ep, &desc->fma);
	if (rc != GNI_RC_SUCCESS)
		return -1;
	return 0; 
}

static int ugni_read_meta_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
{
	struct ldms_ugni_xprt *r = ugni_from_xprt(x);
	struct ldms_set_desc *sd = s;
	struct ugni_buf_remote_data* rbuf = sd->rbd->xprt_data;
	struct ugni_buf_local_data* lbuf = sd->rbd->lcl_data;

	return ugni_read_start(r,
			       (uint64_t)(unsigned long)lbuf->meta,
			       lbuf->meta_mh,
			       rbuf->meta_buf,
			       rbuf->meta_mh,
			       (len?len:ntohl(rbuf->meta_size)),
			       context);
}

static int ugni_read_data_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
{
	struct ldms_ugni_xprt *r = ugni_from_xprt(x);
	struct ldms_set_desc *sd = s;
	struct ugni_buf_remote_data* rbuf = sd->rbd->xprt_data;
	struct ugni_buf_local_data* lbuf = sd->rbd->lcl_data;

	return ugni_read_start(r,
			       (uint64_t)(unsigned long)lbuf->data,
			       lbuf->data_mh,
			       rbuf->meta_buf,
			       rbuf->meta_mh,
			       (len?len:ntohl(rbuf->meta_size)),
			       context);
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
	pthread_mutex_init(&ugni_list_lock, 0);
	pthread_mutex_init(&desc_list_lock, 0);
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

	atexit(ugni_xprt_cleanup);

	rc = PMI_Init(&first);
	if (rc != PMI_SUCCESS)
		goto err_2;
	
	rc = PMI_Get_size(&job_size);
	if (rc != PMI_SUCCESS)
		goto err_3;

	int rank_id;
	rc = PMI_Get_rank(&rank_id);
	if (rc != PMI_SUCCESS)
		goto err_3;

	ptag = get_ptag();
	cookie = get_cookie();

	rc = GNI_CdmCreate(rank_id, ptag, cookie, modes, &cdm_handle);
	if (rc != GNI_RC_SUCCESS)
		goto err_3;

	rc = GNI_CdmAttach(cdm_handle, device_id, &local_address, &nic_handle);
	if (rc != GNI_RC_SUCCESS)
		goto err_4;

	return 0;
 err_4:
	(void)GNI_CdmDestroy(cdm_handle);
 err_3:
	PMI_Finalize();
 err_2:
	pthread_cancel(io_thread);
 err_1:
	event_base_free(io_event_loop);
	return rc;
}

struct ldms_xprt *xprt_get(int (*recv_cb)(struct ldms_xprt *, void *),
			   int (*read_complete_cb)(struct ldms_xprt *, void *))
{
	struct ldms_xprt *x;
	struct ldms_ugni_xprt *r;
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

	r = calloc(1, sizeof(struct ldms_ugni_xprt));
	LIST_INSERT_HEAD(&ugni_list, r, client_link);

	x->max_msg = (256 * 1024);
	x->connect = ugni_xprt_connect;
	x->listen = ugni_xprt_listen;
	x->destroy = ugni_xprt_destroy;
	x->close = ugni_xprt_close;
	x->send = ugni_xprt_send;
	x->read_meta_start = ugni_read_meta_start;
	x->read_data_start = ugni_read_data_start;
	x->read_complete_cb = read_complete_cb;
	x->recv_cb = recv_cb;
	x->alloc = ugni_rbuf_alloc;
	x->free = ugni_rbuf_free;
	x->private = r;
	r->xprt = x;

	return x;
 err_1:
	free(x);
 err_0:
	return NULL;
}

static void __attribute__ ((constructor)) ugni_init();
static void ugni_init()
{
}

static void __attribute__ ((destructor)) ugni_fini(void);
static void ugni_fini()
{
	if (once) {
		(void)GNI_CdmDestroy(cdm_handle);
		PMI_Finalize();
	}
}
