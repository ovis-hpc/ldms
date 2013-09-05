/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include "gni_pub.h"
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_ugni.h"
#include "mmalloc/mmalloc.h"

/* Convenience macro for logging errors */
#define LOG_(x, ...) { if (x && x->xprt && x->xprt->log) x->xprt->log(__VA_ARGS__); }

/* Gemini Transport Variables */
#define UGNI_PTAG   0x84
#define UGNI_COOKIE 0xa9380000

static struct ldms_ugni_xprt ugni_gxp;
int first;
uint8_t ptag;
int cookie;
int modes = 0;
int device_id = 0;
int cq_entries = 128;
static int reg_count;

LIST_HEAD(mh_list, ugni_mh) mh_list;
pthread_mutex_t ugni_mh_lock;

static struct event_base *io_event_loop;
static pthread_t io_thread;
static pthread_t cq_thread;

pthread_mutex_t ugni_lock;
pthread_mutex_t ugni_list_lock;
LIST_HEAD(ugni_list, ldms_ugni_xprt) ugni_list;
pthread_mutex_t desc_list_lock;
LIST_HEAD(desc_list, ugni_desc) desc_list;

static void *io_thread_proc(void *arg);
static void *cq_thread_proc(void *arg);

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

#define UGNI_MAX_OUTSTANDING_BTE 8192
static gni_return_t ugni_job_setup(uint8_t ptag, uint32_t cookie)
{
	gni_job_limits_t limits;
	gni_return_t grc;

	/* Do not apply any resource limits */
	limits.mdd_limit = GNI_JOB_INVALID_LIMIT;
	limits.fma_limit = GNI_JOB_INVALID_LIMIT;
	limits.cq_limit = GNI_JOB_INVALID_LIMIT;

	/* This limits the fan-out of the aggregator */
	limits.bte_limit = UGNI_MAX_OUTSTANDING_BTE;

	/* Do not use an NTT */
	limits.ntt_size = 0;

	/* GNI_ConfigureJob():
	 * -device_id should always be 0 for XE
	 * -job_id should always be 0 (meaning "no job container created")
	 */
	pthread_mutex_lock(&ugni_lock);
	grc = GNI_ConfigureJob(0, 0, ptag, cookie, &limits);
	pthread_mutex_unlock(&ugni_lock);
	return grc;
}

/*
 * We have to keep a dense array of CQ. So as CQ come and go, this array needs
 * to get rebuilt
 */
#define CQ_BUMP_COUNT 1024
static gni_cq_handle_t *cq_table = NULL; /* table of all CQ being monitored...on per host */
static int cq_table_count = 0;		 /* size of table */
static int cq_use_count = 0;		 /* elements consumed in the table */

/*
 * add_cq_table is an array of CQ that need to be added to the CQ
 * table. Entries are added to this table when the connection is made.
 */
static int add_cq_used = 0;
static gni_cq_handle_t add_cq_table[1024];
/*
 * rem_cq_table is an array of CQ that need to be removed from the CQ table.
 * Entries are added to this table when a connection goes away.
 */

static int rem_cq_used = 0;
static gni_cq_handle_t rem_cq_table[1024];

/*
 * The cq_table_lock serializes access to the add_cq_table and rem_cq_table.
 */
static pthread_mutex_t cq_table_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cq_empty_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cq_empty_cv = PTHREAD_COND_INITIALIZER;

/*
 * Allocate an I/O request context. These descriptors contain the context for
 * RDMA submitted to the transport.
 */
static struct ugni_desc *alloc_desc(struct ldms_ugni_xprt *gxp)
{
	struct ugni_desc *desc = NULL;
	pthread_mutex_lock(&desc_list_lock);
	if (!LIST_EMPTY(&desc_list)) {
		desc = LIST_FIRST(&desc_list);
		LIST_REMOVE(desc, link);
	}
	pthread_mutex_unlock(&desc_list_lock);
	desc = desc ? desc : calloc(1, sizeof *desc);
	if (desc)
		desc->gxp = gxp;
	return desc;
}

/*
 * Free an I/O request context.
 */
static void free_desc(struct ugni_desc *desc)
{
	pthread_mutex_lock(&desc_list_lock);
	LIST_INSERT_HEAD(&desc_list, desc, link);
	pthread_mutex_unlock(&desc_list_lock);
}

static gni_return_t ugni_dom_init(uint8_t ptag, uint32_t cookie, uint32_t inst_id,
				  struct ldms_ugni_xprt *gxp)
{
	gni_return_t grc;

	if (!ptag)
		return GNI_RC_INVALID_PARAM;

	pthread_mutex_lock(&ugni_lock);
	grc = GNI_CdmCreate(inst_id, ptag, cookie, GNI_CDM_MODE_FMA_SHARED,
			    &gxp->dom.cdm);
	if (grc)
		goto err;

	grc = GNI_CdmAttach(gxp->dom.cdm, 0, &gxp->dom.info.pe_addr, &gxp->dom.nic);
	if (grc != GNI_RC_SUCCESS)
		goto err;

	gxp->dom.info.ptag = ptag;
	gxp->dom.info.cookie = cookie;
	gxp->dom.info.inst_id = inst_id;

	pthread_mutex_unlock(&ugni_lock);
	return GNI_RC_SUCCESS;
 err:
	pthread_mutex_unlock(&ugni_lock);
	return grc;
}

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
	if (cq_thread) {
		pthread_cancel(cq_thread);
		pthread_join(cq_thread, &dontcare);
	}
	if (io_event_loop)
		event_base_free(io_event_loop);
}

static void ugni_xprt_close(struct ldms_xprt *x)
{
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	gni_return_t grc;
	release_buf_event(gxp);
	close(gxp->sock);
	gxp->sock = 0;
	if (gxp->ugni_ep) {
		pthread_mutex_lock(&ugni_lock);
		grc = GNI_EpDestroy(gxp->ugni_ep);
		pthread_mutex_unlock(&ugni_lock);
		if (grc != GNI_RC_SUCCESS)
			gxp->xprt->log("Error %d destroying Ep %p.\n",
				       grc, gxp->ugni_ep);
		gxp->ugni_ep = NULL;
	}
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
	if (flags == -1) {
		x->log("Error getting flags on fd %d", fd);
		return -1;
	}
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags)) {
		x->log("Error setting non-blocking I/O on fd %d", fd);
		return -1;
	}
	return 0;
}

#define UGNI_CQ_DEPTH 2048
static int ugni_xprt_connect(struct ldms_xprt *x,
			     struct sockaddr *sa, socklen_t sa_len)
{
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	struct sockaddr_storage ss;
	int cq_depth;
	char *cq_depth_s;
	int rc;
	gni_return_t grc;

	gxp->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (gxp->sock < 0)
		return -1;

	if (!ugni_gxp.dom.src_cq) {
		cq_depth_s = getenv("LDMS_UGNI_CQ_DEPTH");
		if (!cq_depth_s) {
			cq_depth = UGNI_CQ_DEPTH;
		} else {
			cq_depth = atoi(cq_depth_s);
			if (cq_depth == 0)
				cq_depth = UGNI_CQ_DEPTH;
		}

		pthread_mutex_lock(&ugni_lock);
		grc = GNI_CqCreate(gxp->dom.nic, cq_depth, 0,
				   GNI_CQ_BLOCKING, NULL, NULL, &ugni_gxp.dom.src_cq);
		pthread_mutex_unlock(&ugni_lock);
		if (grc != GNI_RC_SUCCESS) {
			gxp->xprt->log("The CQ could not be created.\n");
			goto err;
		}
		gxp->dom.src_cq = ugni_gxp.dom.src_cq;
		cq_use_count = 1;
		pthread_mutex_lock(&cq_empty_lock);
		pthread_cond_signal(&cq_empty_cv);
		pthread_mutex_unlock(&cq_empty_lock);
	}

	pthread_mutex_lock(&ugni_lock);
	grc = GNI_EpCreate(gxp->dom.nic, gxp->dom.src_cq, &gxp->ugni_ep);
	pthread_mutex_unlock(&ugni_lock);
	if (grc != GNI_RC_SUCCESS)
		goto err;

	rc = connect(gxp->sock, sa, sa_len);
	if (rc)
		goto err1;
	sa_len = sizeof(ss);
	rc = getsockname(gxp->sock, (struct sockaddr *)&ss, &sa_len);
	if (rc)
		goto err1;
	_setup_connection(gxp, (struct sockaddr *)&ss, sa_len);

	/*
	 * When we receive the peer's hello request, we will bind the endpoint
	 * to his PE and move to connected.
	 */
	return 0;

err1:
	pthread_mutex_lock(&ugni_lock);
	grc = GNI_EpDestroy(gxp->ugni_ep);
	pthread_mutex_unlock(&ugni_lock);
	if (grc != GNI_RC_SUCCESS)
		gxp->xprt->log("Error %d destroying Ep %p.\n",
				grc, gxp->ugni_ep);
	gxp->ugni_ep = NULL;
err:
	close(gxp->sock);
	gxp->sock = 0;
	return -1;
}

/*
 * Received by the active side (aggregator/ldms_ls node).
 */
int process_ugni_hello_req(struct ldms_ugni_xprt *x, struct ugni_hello_req *req)
{
	int rc;
	x->rem_pe_addr = ntohl(req->pe_addr);
	x->rem_inst_id = ntohl(req->inst_id);
	pthread_mutex_lock(&ugni_lock);
	rc = GNI_EpBind(x->ugni_ep, x->rem_pe_addr, x->rem_inst_id);
	pthread_mutex_unlock(&ugni_lock);
	if (rc == GNI_RC_SUCCESS)
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
	int oldtype;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
	event_base_dispatch(io_event_loop);
	return NULL;
}

static ldms_log_fn_t ugni_log;
static gni_return_t process_cq(gni_cq_handle_t cq, gni_cq_entry_t cqe)
{
	gni_return_t grc;
	gni_post_descriptor_t *post;
	do {
		if (GNI_CQ_GET_TYPE(cqe) != GNI_CQ_EVENT_TYPE_POST) {
			ugni_log("Unexepcted cqe type %d cqe %08x on CQ %p\n",
				 GNI_CQ_GET_TYPE(cqe), cqe, cq);
			continue;
		}
		post = NULL;
		pthread_mutex_lock(&ugni_lock);
		grc = GNI_GetCompleted(cq, cqe, &post);
		pthread_mutex_unlock(&ugni_lock);
		if (grc != GNI_RC_SUCCESS) {
			ugni_log("Error %d getting post descriptor from CQ %p\n",
			       grc, cq);
			grc = GNI_RC_SUCCESS;
			continue;
		}
		struct ugni_desc *desc = (struct ugni_desc *)post;
		if (!desc) {
			ugni_log("Post descriptor is Null!\n");
			continue;
		}
		switch (desc->post.type) {
		case GNI_POST_RDMA_GET:
			if (desc->gxp->xprt && desc->gxp->xprt->read_complete_cb)
				desc->gxp->xprt->
					read_complete_cb(desc->gxp->xprt,
							 desc->context);
			break;
		default:
			if (desc->gxp)
				desc->gxp->xprt->log("Unknown completion "
						     "type %d on transport "
						     "%p.\n",
						     desc->post.type, desc->gxp);
		}
		free_desc(desc);
		pthread_mutex_lock(&ugni_lock);
		grc = GNI_CqGetEvent(cq, &cqe);
		pthread_mutex_unlock(&ugni_lock);
	} while (grc == GNI_RC_SUCCESS);

	return GNI_RC_SUCCESS;
}

#define WAIT_20SECS 20000
static void *cq_thread_proc(void *arg)
{
	gni_return_t grc;
	gni_cq_entry_t event_data;
	gni_cq_entry_t cqe;
	uint32_t which;
	int oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
	while (1) {
		uint64_t timeout = WAIT_20SECS;
		pthread_mutex_lock(&cq_empty_lock);
		while (!cq_use_count)
			pthread_cond_wait(&cq_empty_cv, &cq_empty_lock);
		pthread_mutex_unlock(&cq_empty_lock);

		grc = GNI_CqWaitEvent(ugni_gxp.dom.src_cq, timeout, &cqe);
		if (grc) {
			if (grc != GNI_RC_TIMEOUT) {
				ugni_log("%s: Error %d  monitoring the CQ.\n", __func__, grc);
				exit(1);
			} else {
				continue;
			}
		} else {
			if (grc = process_cq(ugni_gxp.dom.src_cq, cqe))
				ugni_log("Error %d processing CQ %p.\n", grc, ugni_gxp.dom.src_cq);
		}
	}
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
			LOG_(r, "Socket errors %x\n", events);
		r->xprt->connected = 0;
		ldms_xprt_close(r->xprt);
		ldms_release_xprt(r->xprt);
	} else
		LOG_(r, "Peer connect complete %x\n", events);
}

static void _setup_connection(struct ldms_ugni_xprt *gxp,
			      struct sockaddr *remote_addr, socklen_t sa_len)
{
	int rc;

	gxp->conn_status = CONN_CONNECTED;
	memcpy((char *)&gxp->xprt->remote_ss, (char *)remote_addr, sa_len);
	gxp->xprt->ss_len = sa_len;

	if (set_nonblock(gxp->xprt, gxp->sock))
		LOG_(gxp,"Warning: error setting non-blocking I/O on an "
			     "incoming connection.\n");

	/* Initialize send and recv I/O events */
	gxp->buf_event = bufferevent_socket_new(io_event_loop, gxp->sock, BEV_OPT_THREADSAFE);
	if(!gxp->buf_event) {
		LOG_(gxp, "Error initializing buffered I/O event for "
		     "fd %d.\n", gxp->sock);
		goto err_0;
	}

	bufferevent_setcb(gxp->buf_event, ugni_read, ugni_write, ugni_event, gxp);
	if (bufferevent_enable(gxp->buf_event, EV_READ | EV_WRITE))
		LOG_(gxp, "Error enabling buffered I/O event for fd %d.\n",
		     gxp->sock);

	return;

 err_0:
	ldms_xprt_close(gxp->xprt);
	ldms_release_xprt(gxp->xprt);
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

static void ugni_connect_req(struct evconnlistener *listener,
			     evutil_socket_t sockfd,
			     struct sockaddr *address, int socklen, void *arg)
{
	struct ldms_ugni_xprt *gxp = arg;
	struct ldms_ugni_xprt *new_gxp = NULL;
	static int conns;

	new_gxp = setup_connection(gxp, sockfd,
				   (struct sockaddr *)&address, socklen);
	if (new_gxp)
		conns ++;

	struct timeval connect_tv;
	gettimeofday(&connect_tv, NULL);

	if ((connect_tv.tv_sec - report_tv.tv_sec) >= 10) {
		double rate;
		rate = (double)conns / (double)(connect_tv.tv_sec - report_tv.tv_sec);
		gxp->xprt->log("Connection rate is %.2f connectios/second\n", rate);
		report_tv = connect_tv;
		conns = 0;
	}
	/*
	 * Send peer (aggregator/ldms_ls) our PE so he can bind to us.
	 */
	struct ugni_hello_req req;
	req.hdr.cmd = htonl(UGNI_HELLO_REQ_CMD);
	req.hdr.len = htonl(sizeof(req));
	req.hdr.xid = (uint64_t)(unsigned long)0; /* no response to hello */
	req.pe_addr = htonl(new_gxp->dom.info.pe_addr);
	req.inst_id = htonl(new_gxp->dom.info.inst_id);
	(void)bufferevent_write(new_gxp->buf_event, &req, sizeof(req));
}

static int ugni_xprt_listen(struct ldms_xprt *x, struct sockaddr *sa, socklen_t sa_len)
{
	int rc;
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	int optval = 1;

	gettimeofday(&listen_tv, NULL);
	report_tv = listen_tv;

	gxp->sock = socket(PF_INET, SOCK_STREAM, 0);
	if (gxp->sock < 0) {
		rc = errno;
		goto err_0;
	}

	setsockopt(gxp->sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

	if (set_nonblock(x, gxp->sock))
		x->log("Warning: Could not set listening socket to non-blocking\n");

	rc = ENOMEM;
	gxp->listen_ev = evconnlistener_new_bind(io_event_loop, ugni_connect_req, gxp,
						 LEV_OPT_THREADSAFE | LEV_OPT_REUSEABLE,
						 1024, sa, sa_len);
	if (!gxp->listen_ev)
		goto err_0;

	gxp->sock = evconnlistener_get_fd(gxp->listen_ev);
	return 0;
 err_0:
	ldms_xprt_close(gxp->xprt);
	ldms_release_xprt(gxp->xprt);
	return rc;
}

static void ugni_xprt_destroy(struct ldms_xprt *x)
{
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	ugni_xprt_term(gxp);
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

gni_return_t ugni_get_mh(struct ldms_ugni_xprt *gxp,
			 void *addr, size_t size, gni_mem_handle_t *mh)
{
	gni_return_t grc = GNI_RC_SUCCESS;
	struct ugni_mh *umh;
	int need_mh = 0;
	unsigned long start;
	unsigned long end;

	pthread_mutex_lock(&ugni_mh_lock);
 	umh = LIST_FIRST(&mh_list);
	if (!umh) {
		struct mm_info mmi;
		mm_get_info(&mmi);
		start = (unsigned long)mmi.start;
		end = start + mmi.size;
		need_mh = 1;
	}
	if (!need_mh)
		goto out;

	umh = malloc(sizeof *umh);
	umh->start = start;
	umh->end = end;
	umh->ref_count = 0;

	grc = GNI_MemRegister(gxp->dom.nic, umh->start, end - start,
			      NULL,
			      GNI_MEM_READWRITE | GNI_MEM_RELAXED_PI_ORDERING,
			      -1, &umh->mh);
	if (grc != GNI_RC_SUCCESS) {
		free(umh);
		goto out;
	}
	LIST_INSERT_HEAD(&mh_list, umh, link);
	reg_count++;
out:
	*mh = umh->mh;
	umh->ref_count++;
	pthread_mutex_unlock(&ugni_mh_lock);
	return grc;
}

/*
 * Allocate a remote buffer. If we are the producer, the xprt_data
 * will be NULL. In this case, we fill in the local side
 * information.
 */
struct ldms_rbuf_desc *ugni_rbuf_alloc(struct ldms_xprt *x,
				       struct ldms_set *set,
				       void *xprt_data,
				       size_t xprt_data_len)
{
	gni_return_t grc;
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	struct ugni_buf_local_data *lcl_data;
	struct ldms_rbuf_desc *desc = calloc(1, sizeof(struct ldms_rbuf_desc));
	if (!desc)
		return NULL;

	lcl_data = calloc(1, sizeof *lcl_data);
	if (!lcl_data)
		goto err_0;

	lcl_data->meta = set->meta;
	lcl_data->meta_size = set->meta->meta_size;
	lcl_data->data = set->data;
	lcl_data->data_size = set->meta->data_size;
	grc = ugni_get_mh(gxp, set->meta, set->meta->meta_size, &lcl_data->meta_mh);
	if (grc)
		goto err_1;
	grc = ugni_get_mh(gxp, set->data, set->meta->data_size, &lcl_data->data_mh);
	if (grc)
		goto err_1;

	desc->lcl_data = lcl_data;

	if (xprt_data) {
		desc->xprt_data_len = xprt_data_len;
		desc->xprt_data = calloc(1, xprt_data_len);
		if (!desc->xprt_data)
			goto err_1;
		memcpy(desc->xprt_data, xprt_data, xprt_data_len);
	} else {
		struct ugni_buf_remote_data *rem_data = calloc(1, sizeof *rem_data);
		if (!rem_data)
			goto err_1;
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
 err_1:
	free(lcl_data);
 err_0:
	free(desc);
	gxp->xprt->log("RBUF allocation failed. Registration count is %d\n", reg_count);
	return NULL;
}

void ugni_rbuf_free(struct ldms_xprt *x, struct ldms_rbuf_desc *desc)
{
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	struct ugni_buf_remote_data *rbuf = desc->xprt_data;
	struct ugni_buf_local_data *lbuf = desc->lcl_data;

	if (rbuf)
		free(rbuf);

	if (lbuf)
		free(lbuf);

	free(desc);
}

static int ugni_read_start(struct ldms_ugni_xprt *gxp,
			   uint64_t laddr, gni_mem_handle_t local_mh,
			   uint64_t raddr, gni_mem_handle_t remote_mh,
			   uint32_t len, void *context)
{
	gni_return_t grc;
	struct ugni_desc *desc = alloc_desc(gxp);
	if (!desc)
		return ENOMEM;

	desc->post.type = GNI_POST_RDMA_GET;
	desc->post.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->post.dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	desc->post.local_addr = laddr;
	desc->post.local_mem_hndl = local_mh;
	desc->post.remote_addr = raddr;
	desc->post.remote_mem_hndl = remote_mh;
	desc->post.length = (len + 3) & ~3;
	desc->post.post_id = (uint64_t)(unsigned long)desc;
	desc->context = context;
	pthread_mutex_lock(&ugni_lock);
	grc = GNI_PostRdma(gxp->ugni_ep, &desc->post);
	pthread_mutex_unlock(&ugni_lock);
	if (grc != GNI_RC_SUCCESS)
		return -1;
	return 0;
}

static int ugni_read_meta_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
{
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	struct ldms_set_desc *sd = s;
	struct ugni_buf_remote_data* rbuf = sd->rbd->xprt_data;
	struct ugni_buf_local_data* lbuf = sd->rbd->lcl_data;

	int rc = ugni_read_start(gxp,
				 (uint64_t)(unsigned long)lbuf->meta,
				 lbuf->meta_mh,
				 rbuf->meta_buf,
				 rbuf->meta_mh,
				 (len?len:ntohl(rbuf->meta_size)),
				 context);
}

static int ugni_read_data_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
{
	struct ldms_ugni_xprt *gxp = ugni_from_xprt(x);
	struct ldms_set_desc *sd = s;
	struct ugni_buf_remote_data* rbuf = sd->rbd->xprt_data;
	struct ugni_buf_local_data* lbuf = sd->rbd->lcl_data;

	int rc = ugni_read_start(gxp,
				 (uint64_t)(unsigned long)lbuf->data,
				 lbuf->data_mh,
				 rbuf->data_buf,
				 rbuf->data_mh,
				 (len?len:ntohl(rbuf->data_size)),
				 context);
	return 0;
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
	pthread_mutex_init(&ugni_mh_lock, 0);
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

	rc = pthread_create(&cq_thread, NULL, cq_thread_proc, 0);
	if (rc)
		goto err_2;

	atexit(ugni_xprt_cleanup);
	rc = ugni_job_setup(UGNI_PTAG, UGNI_COOKIE);
	if (rc != GNI_RC_SUCCESS)
		goto err_3;

	rc = ugni_dom_init(UGNI_PTAG, UGNI_COOKIE, getpid(), &ugni_gxp);
	if (rc != GNI_RC_SUCCESS)
		goto err_3;

	return 0;
 err_3:
	pthread_cancel(cq_thread);
 err_2:
	pthread_cancel(io_thread);
 err_1:
	event_base_free(io_event_loop);
	return rc;
}

struct ldms_xprt *xprt_get(int (*recv_cb)(struct ldms_xprt *, void *),
			   int (*read_complete_cb)(struct ldms_xprt *, void *),
			   ldms_log_fn_t log_fn)
{
	struct ldms_xprt *x;
	struct ldms_ugni_xprt *gxp;

	if (!ugni_log)
		ugni_log = log_fn;
	if (!once) {
		int rc = init_once();
		if (rc) {
			errno = rc;
			goto err_0;
		}
		once = 1;
	}

	x = calloc(1, sizeof (*x));
	if (!x) {
		errno = ENOMEM;
		goto err_0;
	}

	gxp = calloc(1, sizeof(struct ldms_ugni_xprt));
	*gxp = ugni_gxp;
	LIST_INSERT_HEAD(&ugni_list, gxp, client_link);

	x->max_msg = (1024*1024);
	x->log = log_fn;
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
	x->private = gxp;
	gxp->xprt = x;
	return x;

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
	gni_return_t grc;
	struct ugni_mh *mh;
	while (!LIST_EMPTY(&mh_list)) {
		mh = LIST_FIRST(&mh_list);
		LIST_REMOVE(mh, link);
		(void)GNI_MemDeregister(ugni_gxp.dom.nic, &mh->mh);
		free(mh);
	}
}
