/*
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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_rdma_xprt.h"

#define LOG__(x, ...) { if (x && x->log) x->log(__VA_ARGS__); }
#define LOG_(x, ...) { if (x && x->xprt && x->xprt->log) x->xprt->log(__VA_ARGS__); }

LIST_HEAD(rdma_list, ldms_rdma_xprt) rdma_list;;

struct rdma_context {
	void *usr_context;	/* user context if any */
	enum ibv_wc_opcode op;	/* work-request op (can't be trusted
				   in wc on error */
	struct rdma_buffer *rb;	/* RDMA buffer if any */
};

static int rdma_fill_rq(struct ldms_rdma_xprt *x);
static void *cm_thread_proc(void *arg);
static void *cq_thread_proc(void *arg);
static int cq_event_handler(struct ibv_cq *cq);

static struct ldms_rdma_xprt *rdma_from_xprt(ldms_t d)
{
	return ((struct ldms_xprt *)d)->private;
}

static struct rdma_context *rdma_get_context(struct ibv_wc *wc)
{
	return (struct rdma_context *)(unsigned long)wc->wr_id;
}

#define RDMA_SET_CONTEXT(__wr, __ctxt) (__wr)->wr_id = (uint64_t)(unsigned long)(__ctxt);


static int cq_fd;
static int cm_fd;
#define CQ_EVENT_LEN 16


static void rdma_teardown_conn(struct ldms_rdma_xprt *r)
{
	int ret = 0;

	if (r->cm_id && r->conn_status == CONN_CONNECTED) {
		ret = rdma_disconnect(r->cm_id);
		if (ret)
                        LOG_(r, "RDMA: Error %d : rdma_disconnect failed\n", errno);
	}

	/* Destroy the QP */
	if (!r->server && r->qp) {
		rdma_destroy_qp(r->cm_id);
	}

	/* Destroy the CQ */
	if (r->cq) {
		ret = ibv_destroy_cq(r->cq);
		if (ret) {
			LOG_(r, "RDMA: Error %d : ibv_destroy_cq failed\n", errno);
		}
	}

	/* Destroy the PD */
	if (r->pd){
		ret = ibv_dealloc_pd(r->pd);
		if (ret) {
			LOG_(r, "RDMA: Error %d : ibv_dealloc_pd failed\n", errno);
		}
	}

	/* Destroy the CM id */
	if (r->cm_id) {
		ret = rdma_destroy_id(r->cm_id);
		if (ret)
			LOG_(r, "RDMA: Error %d : rdma_destroy_id failed\n", errno);
	}

	if (r->cm_channel) {
		rdma_destroy_event_channel(r->cm_channel);
	}

	if (r->cq_channel) {
		ret = ibv_destroy_comp_channel(r->cq_channel);
		if (ret)
			LOG_(r, "RDMA: Error %d : ibv_destroy_comp__channel failed\n", errno);
	}

	r->cm_id = NULL;
	r->pd = NULL;
	r->cq = NULL;
	r->qp = NULL;
	r->cq_channel = NULL;
	r->cm_channel = NULL;
}

static void rdma_xprt_destroy(struct ldms_xprt *x)
{
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);

	/* Remove myself from the client list */
	LIST_REMOVE(r, client_link);
	rdma_teardown_conn(r);
	free(r);
}

static struct rdma_buffer *rdma_buffer_alloc(struct ldms_rdma_xprt *x,
					     size_t len,
					     enum ibv_access_flags f)
{
	struct rdma_buffer *rbuf;

	rbuf = calloc(1, sizeof *rbuf + len);
	if (!rbuf)
		return NULL;
	rbuf->next = NULL;
	rbuf->data = rbuf+1;
	rbuf->data_len = len;
	rbuf->mr = ibv_reg_mr(x->pd, rbuf->data, len, f);
	if (!rbuf->mr) {
		free(rbuf);
		LOG_(x, "RDMA: recv_buf reg_mr failed\n");
		return NULL;
	}
	return rbuf;
}

static void rdma_buffer_free(struct rdma_buffer *rbuf)
{
	ibv_dereg_mr(rbuf->mr);
	free(rbuf);
}

static struct rdma_context *rdma_context_alloc(void *usr_context, enum ibv_wc_opcode op, struct rdma_buffer *rbuf)
{
	struct rdma_context *ctxt = calloc(1, sizeof *ctxt);
	if (!ctxt)
		return NULL;
	ctxt->usr_context = usr_context;
	ctxt->op = op;
	ctxt->rb = rbuf;

	return ctxt;
}

static void rdma_context_free(struct rdma_context *ctxt)
{
	free(ctxt);
}

static int rdma_post_send(struct ldms_rdma_xprt *x, struct rdma_buffer *rbuf)
{
	struct ibv_send_wr *bad_wr;
	struct ibv_send_wr wr;
	struct ibv_sge sge;
	struct rdma_context *ctxt = rdma_context_alloc(NULL, IBV_WR_SEND, rbuf);
	if (!ctxt) {
		errno = ENOMEM;
		return ENOMEM;
	}

	sge.addr = (uint64_t) (unsigned long) rbuf->data;
	sge.length = rbuf->data_len;
	sge.lkey = rbuf->mr->lkey;

	wr.opcode = IBV_WR_SEND;
	wr.next = NULL;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.sg_list = &sge;
	wr.num_sge = 1;

	RDMA_SET_CONTEXT(&wr, ctxt);
	return ibv_post_send(x->qp, &wr, &bad_wr);
}

static void rdma_xprt_close(struct ldms_xprt *x)
{
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);
	if (r->cm_id && r->conn_status == CONN_CONNECTED)
		rdma_disconnect(r->cm_id);
}

static int rdma_xprt_connect(struct ldms_xprt *x, struct sockaddr *sin, socklen_t sa_len)
{
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);
	struct epoll_event cm_event;
	struct epoll_event cq_event;
	int rc;

	/* Create the event CM event channel */
	r->cm_channel = rdma_create_event_channel();
	if (!r->cm_channel) {
		rc = ENOMEM;
		goto err_0;
	}

	/* Create the connecting CM ID */
	rc = rdma_create_id(r->cm_channel, &r->cm_id, r, RDMA_PS_TCP);
	if (rc) {
		rc = EHOSTUNREACH;
		goto err_0;
	}

	cm_event.events = EPOLLIN | EPOLLOUT;
	cm_event.data.ptr = r;
	ldms_xprt_get(r->xprt);
	rc = epoll_ctl(cm_fd, EPOLL_CTL_ADD, r->cm_channel->fd, &cm_event);
	if (rc)
		goto err_0;

	/* Bind the provided address to the CM Id */
	sem_init(&r->sem, 0, 0);
	r->conn_status = CONN_CONNECTING;
	rc = rdma_resolve_addr(r->cm_id, NULL, (struct sockaddr *) &x->remote_ss, 2000);
	if (rc) {
		rc = EHOSTUNREACH;
		goto err_0;
	}

	do {
		rc = sem_wait(&r->sem);
	} while (rc && errno == EINTR);

	if (r->conn_status < 0) {
		rc = EHOSTDOWN;
		goto err_1;
	}

	cq_event.data.ptr = r;
	cq_event.events = EPOLLIN | EPOLLOUT;
	ldms_xprt_get(r->xprt);
	rc = epoll_ctl(cq_fd, EPOLL_CTL_ADD, r->cq_channel->fd, &cq_event);
	if (rc) {
		LOG_(r, "RDMA: Error adding CQ fd to event queue.\n");
		rdma_disconnect(r->cm_id);
		goto err_2;
	}

	errno = rc;
	return rc;

 err_2:
	ldms_release_xprt(r->xprt);
 err_1:
	epoll_ctl(cm_fd, EPOLL_CTL_DEL, r->cm_channel->fd, NULL);
	ldms_release_xprt(r->xprt);
 err_0:
	rdma_teardown_conn(r);
	return rc;
}

static int rdma_post_recv(struct ldms_rdma_xprt *r, struct rdma_buffer *rb)
{
	struct ibv_recv_wr wr;
	struct ibv_sge sge;
	struct ibv_recv_wr *bad_wr;
	struct rdma_context *ctxt = rdma_context_alloc(NULL, 0, rb);
	if (!ctxt)
		return -ENOMEM;
	sge.addr = (uint64_t) (unsigned long) rb->data;
	sge.length = rb->data_len;
	sge.lkey = rb->mr->lkey;
	wr.sg_list = &sge;
	wr.next = NULL;
	wr.num_sge = 1;

	RDMA_SET_CONTEXT(&wr, ctxt);
	return ibv_post_recv(r->qp, &wr, &bad_wr);
}

static void process_send_wc(struct ldms_rdma_xprt *r, struct ibv_wc *wc, struct rdma_buffer *rb)
{
	if (rb)
		rdma_buffer_free(rb);
}

static void process_read_wc(struct ldms_rdma_xprt *r, struct ibv_wc *wc, void *usr_context)
{
	if (r->xprt && r->xprt->read_complete_cb)
		r->xprt->read_complete_cb(r->xprt, usr_context);
}

static void process_recv_wc(struct ldms_rdma_xprt *r, struct ibv_wc *wc, struct rdma_buffer *rb)
{
	struct ldms_request *req = rb->data;
	int ret;

	if (r->xprt && r->xprt->recv_cb)
		r->xprt->recv_cb(r->xprt, req);

	ret = rdma_post_recv(r, rb);
	if (ret) {
		LOG_(r, "RDMA: ibv_post_recv failed: %d\n", ret);
		rdma_buffer_free(rb);
	}
}

static int rdma_fill_rq(struct ldms_rdma_xprt *x)
{
	int i;

	for (i = 0; i < RQ_DEPTH; i++) {
		struct rdma_buffer *rbuf = rdma_buffer_alloc(x, RQ_BUF_SZ,
							     IBV_ACCESS_LOCAL_WRITE);
		if (rbuf) {
			int rc = rdma_post_recv(x, rbuf);
			if (rc)
				return rc;
		} else
			return -ENOMEM;
	}
	return 0;
}

static void rdma_accept_request(struct ldms_rdma_xprt *server,
				struct rdma_cm_id *cma_id)
{
	struct epoll_event cq_event;
	struct ldms_rdma_xprt *r;
	struct rdma_conn_param conn_param;
	struct ibv_qp_init_attr qp_attr;
	ldms_t _x;
	int ret;

	/*
	 * Create a transport instance for this new connection.
	 * This reference will get dropped by cm_thread_proc when it
	 * gets the DISCONNECTED event.
	 */
	_x = ldms_create_xprt("rdma", server->xprt->log);
	if (!_x) {
		LOG_(server, "RDMA: Could not create a new transport.\n");
		return;
	}

	/* Replace listener context with new connection context */
	r = rdma_from_xprt(_x);
	r->cm_id = cma_id;
	r->cm_id->context = r;
	r->conn_status = CONN_CONNECTING;

	/* Allocate PD */
	r->pd = ibv_alloc_pd(r->cm_id->verbs);
	if (!r->pd) {
		LOG_(server, "RDMA: ibv_alloc_pd failed\n");
		goto out_0;
	}

	r->cq_channel = ibv_create_comp_channel(r->cm_id->verbs);
	if (!r->cq_channel) {
		LOG_(server, "RDMA: ibv_create_comp_channel failed\n");
		goto out_0;
	}

	/* Create a new CQ. Our parent's CQ channel will get notifications */
	r->cq = ibv_create_cq(r->cm_id->verbs, SQ_DEPTH + RQ_DEPTH + 1,
			      r, r->cq_channel, 0);
	if (!r->cq) {
		LOG_(server, "RDMA: ibv_create_cq failed\n");
		goto out_0;
	}

	ret = ibv_req_notify_cq(r->cq, 0);
	if (ret) {
		LOG_(server, "RDMA: ibv_create_cq failed\n");
		goto out_0;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.cap.max_send_wr = SQ_DEPTH;
	qp_attr.cap.max_recv_wr = RQ_DEPTH;
	qp_attr.cap.max_recv_sge = SQ_SGE;
	qp_attr.cap.max_send_sge = RQ_SGE;
	qp_attr.qp_type = IBV_QPT_RC;
	qp_attr.send_cq = r->cq;
	qp_attr.recv_cq = r->cq;

	ret = rdma_create_qp(r->cm_id, r->pd, &qp_attr);
	r->qp = r->cm_id->qp;
	if (ret) {
		LOG_(server, "RDMA: rdma_create_qp failed\n");
		goto out_0;
	}

	/* Post receive buffers to the RQ */
	if (rdma_fill_rq(r))
		goto out_0;

	cq_event.events = EPOLLIN | EPOLLOUT;
	cq_event.data.ptr = r;
	ldms_xprt_get(r->xprt);
	ret = epoll_ctl(cq_fd, EPOLL_CTL_ADD, r->cq_channel->fd, &cq_event);
	if (ret) {
		LOG_(server, "RDMA: Could not add passive side CQ fd "
				  "to event queue.\n");
		goto out_1;
	}

	/* Accept the connection */
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 4;
	conn_param.initiator_depth = 4;
	ret = rdma_accept(r->cm_id, &conn_param);
	if (ret)
		goto out_1;

	return;

 out_1:
	ldms_release_xprt(r->xprt);
 out_0:
	ldms_release_xprt(r->xprt);
}

static int cq_event_handler(struct ibv_cq *cq)
{
	struct ibv_wc wc;
	struct ldms_rdma_xprt *r = cq->cq_context;
	int ret;

	while ((ret = ibv_poll_cq(cq, 1, &wc)) == 1) {
		struct rdma_context *ctxt = rdma_get_context(&wc);

		ret = 0;
		if (wc.status) {
			if (wc.status != IBV_WC_WR_FLUSH_ERR) {
				r->xprt->log("RDMA: cq completion "
					     "failed status %d\n",
					     wc.status);
				ret = rdma_disconnect(r->cm_id);
				if(ret) {
					r->xprt->log("RDMA: rdma_disconnect failed status %d\n",
						errno);
				}
			}
			if (ctxt) {
				if (ctxt->rb)
					rdma_buffer_free(ctxt->rb);
				rdma_context_free(ctxt);
			}
			continue;
		}

		switch (wc.opcode) {
		case IBV_WC_SEND:
			process_send_wc(r, &wc, ctxt->rb);
			break;

		case IBV_WC_RDMA_WRITE:
			break;

		case IBV_WC_RDMA_READ:
			process_read_wc(r, &wc, ctxt->usr_context);
			break;

		case IBV_WC_RECV:
			process_recv_wc(r, &wc, ctxt->rb);
			break;

		default:
			r->xprt->log("RDMA: Invalid completion\n");
			ret = -1;
			goto err;
		}
		rdma_context_free(ctxt);
	}
	if (ret) {
		r->xprt->log("RDMA: poll error %d\n", ret);
		goto err;
	}

	return 0;

err:
	return ret;
}
static void *cq_thread_proc(void *arg)
{
	int cq_fd = (int)(unsigned long)arg;
	struct epoll_event cq_events[16];
	struct ldms_rdma_xprt *r;
	int i;
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	int ret;
	int fd_count;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	while (1) {
		int fd_count = epoll_wait(cq_fd, cq_events, 16, -1);
		if (fd_count < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		for (i = 0; i < fd_count; i++) {
			r = cq_events[i].data.ptr;

			/* Get the next event ... this will block */
			ret = ibv_get_cq_event(r->cq_channel, &ev_cq, &ev_ctx);
			if (ret) {
				r->xprt->log("RDMA: Error %d at %s:%d\n",
					     ret, __func__, __LINE__);
				continue;
			}

			/* Re-arm the CQ */
			ret = ibv_req_notify_cq(ev_cq, 0);
			if (ret) {
				r->xprt->log("RDMA: Error %d at %s:%d\n",
					     ret, __func__, __LINE__);
				continue;
			}

			/* Process the CQ */
			ret = cq_event_handler(ev_cq);

			/* Ack the event */
			ibv_ack_cq_events(ev_cq, 1);
			if (ret) {
				r->xprt->log("RDMA: Error %d at %s:%d\n",
					     ret, __func__, __LINE__);
				continue;
			}
		}
	}
	return NULL;
}

int rdma_setup_conn(struct ldms_rdma_xprt *x)
{
	struct rdma_conn_param conn_param;
	struct ibv_qp_init_attr qp_attr;
	int ret = -ENOMEM;

	x->pd = ibv_alloc_pd(x->cm_id->verbs);
	if (!x->pd) {
		LOG_(x, "RDMA: ibv_alloc_pd failed\n");
		goto err_0;
	}

	x->cq_channel = ibv_create_comp_channel(x->cm_id->verbs);
	if (!x->cq_channel) {
		LOG_(x, "RDMA: ibv_create_comp_channel failed\n");
		goto err_0;
	}

	x->cq = ibv_create_cq(x->cm_id->verbs, SQ_DEPTH + RQ_DEPTH + 1,
			      x, x->cq_channel, 0);
	if (!x->cq) {
		LOG_(x, "RDMA: ibv_create_cq failed\n");
		goto err_0;
	}

	ret = ibv_req_notify_cq(x->cq, 0);
	if (ret) {
		LOG_(x, "RMDA: ibv_create_cq failed\n");
		goto err_0;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.cap.max_send_wr = SQ_DEPTH;
	qp_attr.cap.max_recv_wr = RQ_DEPTH;
	qp_attr.cap.max_recv_sge = SQ_SGE;
	qp_attr.cap.max_send_sge = RQ_SGE;
	qp_attr.qp_type = IBV_QPT_RC;
	qp_attr.send_cq = x->cq;
	qp_attr.recv_cq = x->cq;
	ret = rdma_create_qp(x->cm_id, x->pd, &qp_attr);
	x->qp = x->cm_id->qp;
	if (ret) {
		LOG_(x, "RDMA: rdma_create_qp failed\n");
		goto err_0;
	}

	ret = rdma_fill_rq(x);
	if (ret)
		goto err_0;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 4;
	conn_param.retry_count = 10;
	/*
	 * Take a transport reference, it will get dropped by cm_thread_proc
	 * when it gets the DISCONNECTED event.
	 */
	ldms_xprt_get(x->xprt);
	ret = rdma_connect(x->cm_id, &conn_param);
	if (ret) {
		ldms_release_xprt(x->xprt);
		goto err_0;
	}
	return 0;
 err_0:
	x->conn_status = CONN_ERROR;
	return ret;
}

static int cma_event_handler(struct ldms_rdma_xprt *r,
			     struct rdma_cm_id *cma_id,
			     enum rdma_cm_event_type event)
{
	int ret = 0;
	struct ldms_rdma_xprt *x = cma_id->context;
	char buf[32];
	char *s;

	if (r != x)
		printf("Yikes!!\n");

	switch (event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		LOG_(x, "RDMA: Event=Addr Resolved.\n");
		ret = rdma_resolve_route(cma_id, 2000);
		if (ret) {
			LOG_(x, "RDMA: host %s has crashed.\n",
			     inet_ntop(AF_INET, &((struct sockaddr_in *)&x->xprt->remote_ss)->sin_addr,
			                buf, sizeof(buf)));
			x->conn_status = CONN_ERROR;
			x->xprt->connected = 0;
			sem_post(&x->sem);
		}
		break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		LOG_(x, "RDMA: Event=Route Resolved.\n");
		ret = rdma_setup_conn(x);
		if (ret) {
			LOG_(x, "RDMA: host %s has crashed.\n",
			     inet_ntop(AF_INET, &((struct sockaddr_in *)&x->xprt->remote_ss)->sin_addr,
				       buf, sizeof(buf)));
			x->conn_status = CONN_ERROR;
			x->xprt->connected = 0;
			rdma_teardown_conn(x);
			sem_post(&x->sem);
		}
		break;

	case RDMA_CM_EVENT_CONNECT_REQUEST:
		LOG_(x, "RDMA: Event=Connect Request.\n");
		rdma_accept_request(x, cma_id); //Monn: changed r to x
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		LOG_(x, "RDMA: Event=Established.\n");
		x->conn_status = CONN_CONNECTED;
		x->xprt->connected = 1; // Monn: fixed r->xprt to x->xprt
		sem_post(&x->sem);
		break;

	case RDMA_CM_EVENT_REJECTED:
	case RDMA_CM_EVENT_CONNECT_ERROR:
		/* Drop the rdma_connect reference and fall through to error case */
		ldms_release_xprt(x->xprt);

	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
		s = inet_ntop(AF_INET, &((struct sockaddr_in *)&x->xprt->remote_ss)->sin_addr, buf, sizeof(buf));
		LOG_(x, "RDMA: IP_Addr %s Event=ERROR %d\n", (s?s:"bad_addr"), event);
		x->conn_status = CONN_ERROR;
		x->xprt->connected = 0;
		ret = -1;
		sem_post(&x->sem);
		break;

	case RDMA_CM_EVENT_DISCONNECTED:
		LOG_(x, "RDMA: Event=Disconnected\n");
		x->conn_status = CONN_CLOSED;
		x->xprt->connected = 0;
		if (x->cm_channel) {
			ret = epoll_ctl(cm_fd, EPOLL_CTL_DEL,
					x->cm_channel->fd, NULL);
			if (ret) {
				LOG_(x, "RDMA: Error %d removing "
					     "transport from the "
					     "CM event queue.\n", ret);
			}
			ldms_release_xprt(x->xprt);
		}
		if (x->cq_channel) {
			ret = epoll_ctl(cq_fd, EPOLL_CTL_DEL,
					x->cq_channel->fd, NULL);
			if (ret) {
				LOG_(x, "RDMA: Error %d removing "
					     "CQ fd from "
					     "event queue.\n", ret);
			}
			ldms_release_xprt(x->xprt);
		}
		rdma_teardown_conn(x);
		ldms_release_xprt(x->xprt);
		sem_post(&x->sem);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		LOG_(x, "RDMA: Event=DeviceRemoval\n");
		x->conn_status = CONN_ERROR;
		x->xprt->connected = 0;
		sem_post(&x->sem);
		break;

	default:
		LOG_(x, "RDMA: Unhandled event %s, ignoring\n",
			     rdma_event_str(event));
		break;
	}
	return ret;
}

/**
 * Connection Manager Thread - event thread that processes CM events.
 */
static void *cm_thread_proc(void *arg)
{
	int cm_fd = (int)(unsigned long)arg;
	struct epoll_event cm_events[16];
	struct rdma_cm_event *event;
	struct ldms_rdma_xprt *r;
	int ret, i;

	while (1) {
		pthread_testcancel();
		ret = epoll_wait(cm_fd, cm_events, 16, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		for (i = 0; i < ret; i++) {
			r = cm_events[i].data.ptr;
			ret = rdma_get_cm_event(r->cm_channel, &event);
			if (ret)
				continue;

			/*
			 * Ack the event first otherwise, you can end up
			 * dead-locking if the cm_id is being
			 * destroyed due to disconnect.
			 */
			enum rdma_cm_event_type ev = event->event;
			struct rdma_cm_id *cm_id = event->id;
			rdma_ack_cm_event(event);
			ret = cma_event_handler(r, cm_id, ev);
			if (ret)
				LOG_(r,
				     "RDMA: Error %d returned by "
				     "event handler.\n", ret);
		}
	}
	return NULL;
}

static int rdma_xprt_listen(struct ldms_xprt *x, struct sockaddr *s, socklen_t sa_len)
{
	int rc;
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);
	struct epoll_event cm_event;

	/* This is a server */
	r->server = 1;

	/* Create the event CM event channel */
	r->cm_channel = rdma_create_event_channel();
	if (!r->cm_channel) {
		rc = ENOMEM;
		goto err_0;
	}

	/* Create the listening CM ID */
	rc = rdma_create_id(r->cm_channel, &r->cm_id, r, RDMA_PS_TCP);
	if (rc)
		goto err_1;

	/* Bind the provided address to the CM Id */
	rc = rdma_bind_addr(r->cm_id, (struct sockaddr *)&x->local_ss);
	if (rc)
		goto err_2;

	cm_event.events = EPOLLIN | EPOLLOUT;
	cm_event.data.ptr = r;
	ldms_xprt_get(r->xprt);
	rc = epoll_ctl(cm_fd, EPOLL_CTL_ADD, r->cm_channel->fd, &cm_event);
	if (rc)
		goto err_3;

	/*
	 * Asynchronous listen. Connection requests handled in
	 * cm_thread_proc
	 */
	rc = rdma_listen(r->cm_id, 3);
	if (rc)
		goto err_3;

	return 0;

 err_3:
	rc = epoll_ctl(cm_fd, EPOLL_CTL_DEL,
			r->cm_channel->fd, NULL);
	ldms_release_xprt(r->xprt);
 err_2:
	rdma_destroy_id(r->cm_id);
	r->cm_id = NULL;
 err_1:
	rdma_destroy_event_channel(r->cm_channel);
	r->cm_channel = NULL;
 err_0:
	errno = rc;
	return rc;
}

static int rdma_xprt_send(struct ldms_xprt *x, void *buf, size_t len)
{
	struct ldms_request *req = buf;
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);
	struct rdma_buffer *rbuf;
	int rc;

	if (r->conn_status != CONN_CONNECTED &&
	    r->conn_status != CONN_CONNECTING) {
		LOG__(x, "RDMA: Oh my goodness -- send before conn.\n");
		rc = ENOTCONN;
		goto out;
	}

	if (len > RQ_BUF_SZ) {
		rc = ENOMEM;
		goto out;
	}

	rbuf = rdma_buffer_alloc(r, RQ_BUF_SZ, IBV_ACCESS_LOCAL_WRITE);
	if (!rbuf) {
		rc = ENOMEM;
		goto out;
	}

	memcpy(rbuf->data, req, len);
	rbuf->data_len = len;

	return rdma_post_send(r, rbuf);
 out:
	errno = rc;
	return rc;
}

/** Allocate a remote buffer */
struct ldms_rbuf_desc *rdma_rbuf_alloc(struct ldms_xprt *x,
				       struct ldms_set *set,
				       enum ldms_rbuf_type type,
				       void *xprt_data,
				       size_t xprt_data_len)
{
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);
	struct rdma_buf_local_data *lbuf;
	struct ldms_rbuf_desc *desc = calloc(1, sizeof(struct ldms_rbuf_desc));
	if (!desc)
		return NULL;

	lbuf = calloc(1, sizeof *lbuf);
	if (!lbuf)
		goto err_0;

	desc->type = type;

	lbuf->meta = set->meta;
	lbuf->meta_size = set->meta->meta_size;
	lbuf->meta_mr = ibv_reg_mr(r->pd,
				   lbuf->meta, lbuf->meta_size,
				   IBV_ACCESS_LOCAL_WRITE |
				   IBV_ACCESS_REMOTE_WRITE |
				   IBV_ACCESS_REMOTE_READ);
	if (!lbuf->meta_mr)
		goto err_1;

	lbuf->data = set->data;
	lbuf->data_size = set->meta->data_size;
	lbuf->data_mr = ibv_reg_mr(r->pd,
				   lbuf->data, lbuf->data_size,
				   IBV_ACCESS_LOCAL_WRITE |
				   IBV_ACCESS_REMOTE_WRITE |
				   IBV_ACCESS_REMOTE_READ);
	if (!lbuf->data_mr)
		goto err_2;

	desc->lcl_data = lbuf;

	if (xprt_data) {
		desc->xprt_data_len = xprt_data_len;
		desc->xprt_data = calloc(1, xprt_data_len);
		if (!desc->xprt_data)
			goto err_3;
		memcpy(desc->xprt_data, xprt_data, xprt_data_len);
	} else {
		struct rdma_buf_remote_data *rbuf = calloc(1, sizeof *rbuf);
		if (!rbuf)
			goto err_3;
		rbuf->meta_buf = (uint64_t)(unsigned long)lbuf->meta;
		rbuf->meta_size = htonl(lbuf->meta_size);
		rbuf->meta_rkey = lbuf->meta_mr->rkey;
		rbuf->data_buf = (uint64_t)(unsigned long)lbuf->data;
		rbuf->data_size = htonl(lbuf->data_size);
		rbuf->data_rkey = lbuf->data_mr->rkey;
		desc->xprt_data = rbuf;
		desc->xprt_data_len = sizeof(*rbuf);
	}
	return desc;

 err_3:
	ibv_dereg_mr(lbuf->data_mr);
 err_2:
	ibv_dereg_mr(lbuf->meta_mr);
 err_1:
	free(lbuf);
 err_0:
	free(desc);
	return NULL;
}

void rdma_rbuf_free(struct ldms_xprt *x, struct ldms_rbuf_desc *desc)
{
	struct rdma_buf_local_data *lbuf = desc->lcl_data;
	struct rdma_buf_remote_data *rbuf = desc->xprt_data;

	if (lbuf) {
		if (lbuf->meta_mr)
			ibv_dereg_mr(lbuf->meta_mr);
		if (lbuf->data_mr)
			ibv_dereg_mr(lbuf->data_mr);
		free(lbuf);
	}
	if (rbuf)
		free(rbuf);
	free(desc);
}

static int rdma_read_start(struct ldms_rdma_xprt *r,
			   uint64_t laddr, struct ibv_mr *mr,
			   uint64_t raddr, uint32_t rkey,
			   uint32_t len, void *context)
{
	struct ibv_send_wr wr, *bad_wr;
	struct ibv_sge sge;
	struct rdma_context *ctxt =
		rdma_context_alloc(context, IBV_WR_RDMA_READ, NULL);

	sge.addr = laddr;
	sge.length = len;
	sge.lkey = mr->lkey;

	memset(&wr, 0, sizeof(wr));
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_RDMA_READ;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.wr.rdma.remote_addr = raddr;
	wr.wr.rdma.rkey = rkey;

	RDMA_SET_CONTEXT(&wr, ctxt);
	return ibv_post_send(r->qp, &wr, &bad_wr);
}

static int rdma_read_meta_start(struct ldms_xprt *x, ldms_set_t s,
				size_t len, void *context)
{
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);
	struct ldms_set_desc *sd = s;
	struct rdma_buf_remote_data* rbuf = sd->rbd->xprt_data;
	struct rdma_buf_local_data* lbuf = sd->rbd->lcl_data;

	return rdma_read_start(r,
			       (uint64_t)(unsigned long)lbuf->meta,
			       lbuf->meta_mr,
			       rbuf->meta_buf,
			       rbuf->meta_rkey,
			       (len?len:ntohl(rbuf->meta_size)),
			       context);
}

static int rdma_read_data_start(struct ldms_xprt *x, ldms_set_t s,
				size_t len, void *context)
{
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);
	struct ldms_set_desc *sd = s;
	struct rdma_buf_remote_data* rbuf = sd->rbd->xprt_data;
	struct rdma_buf_local_data* lbuf = sd->rbd->lcl_data;

	return rdma_read_start(r,
			       (uint64_t)(unsigned long)lbuf->data,
			       lbuf->data_mr,
			       rbuf->data_buf,
			       rbuf->data_rkey,
			       (len?len:ntohl(rbuf->data_size)),
			       context);
}

static int init_complete = 0;
static pthread_t cm_thread, cq_thread;
static int init_once()
{
	int rc;

	cq_fd = epoll_create(512);
	if (!cq_fd)
		goto err_0;

	cm_fd = epoll_create(512);
	if (!cq_fd)
		goto err_1;

	/*
	 * Create the CQ event thread that will wait for events on the
	 * CQ channel
	 */
	rc = pthread_create(&cq_thread, NULL, cq_thread_proc,
			    (void *)(unsigned long)cq_fd);
	if (rc)
		goto err_3;

	/*
	 * Create the CM event thread that will wait for events on
	 * the CM channel
	 */
	rc = pthread_create(&cm_thread, NULL, cm_thread_proc,
			    (void *)(unsigned long)cm_fd);
	if (rc)
		goto err_2;

	init_complete = 1;
	return 0;

 err_3:
	pthread_cancel(cm_thread);
 err_2:
	close(cm_fd);
 err_1:
	close(cq_fd);
 err_0:
	return 1;
}

struct ldms_xprt *xprt_get(int (*recv_cb)(struct ldms_xprt *, void *),
			   int (*read_complete_cb)(struct ldms_xprt *, void *))
{
	struct ldms_xprt *x;
	struct ldms_rdma_xprt *r;

	if (!init_complete) {
		if (init_once()) {
			errno = ENOMEM;
			goto err_0;
		}
	}

	x = calloc(1, sizeof (*x));
	if (!x) {
		errno = ENOMEM;
		goto err_0;
	}
	r = calloc(1, sizeof(struct ldms_rdma_xprt));
	if (!r) {
		errno = ENOMEM;
		goto err_1;
	}

	x->connect = rdma_xprt_connect;
	x->listen = rdma_xprt_listen;
	x->close = rdma_xprt_close;
	x->destroy = rdma_xprt_destroy;
	x->send = rdma_xprt_send;
	x->read_meta_start = rdma_read_meta_start;
	x->read_data_start = rdma_read_data_start;
	x->read_complete_cb = read_complete_cb;
	x->recv_cb = recv_cb;
	x->alloc = rdma_rbuf_alloc;
	x->free = rdma_rbuf_free;
	x->private = r;
	pthread_mutex_init(&r->lock, NULL);
	r->xprt = x;

	sem_init(&r->sem, 0, 0);

	LIST_INSERT_HEAD(&rdma_list, r, client_link);
	return x;

 err_1:
	free(x);
 err_0:
	return NULL;
}
