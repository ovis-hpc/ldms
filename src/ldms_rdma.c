/*
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
#include <sys/types.h>
#include <netinet/in.h>
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_rdma_xprt.h"

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


static void rdma_xprt_term(struct ldms_rdma_xprt *r)
{
	/* Remove myself from the client list */
	LIST_REMOVE(r, client_link);

	if (r->cm_id && r->conn_status == CONN_CONNECTED)
		rdma_disconnect(r->cm_id);

	/* Shut down the CQ thread */
	if (r->cq_thread) {
		pthread_cancel(r->cq_thread);
		pthread_join(r->cq_thread, NULL);
	}

	/* Shut down the CM thread */
	if (r->cm_thread && r->server) {
		pthread_cancel(r->cm_thread);
		pthread_join(r->cm_thread, NULL);
	}

	/* Destroy the QP */
	if (!r->server && r->qp)
		rdma_destroy_qp(r->cm_id);

	/* Destroy the CQ */
	if (r->cq)
		ibv_destroy_cq(r->cq);

	/* Destroy the CQ completion channel */
	if (r->cq_channel)
		ibv_destroy_comp_channel(r->cq_channel);

	/* Destroy the PD */
	if (r->pd)
		ibv_dealloc_pd(r->pd);

	/* Destroy the CM id */
	if (r->cm_id)
		rdma_destroy_id(r->cm_id);

	/* Destroy the event CM event channel if we're the listener */
	if (r->cm_channel && r->server)
		rdma_destroy_event_channel(r->cm_channel);
}

static struct rdma_buffer *rdma_buffer_alloc(struct ldms_rdma_xprt *x,
					     size_t len,
					     enum ibv_access_flags f)
{
	struct rdma_buffer *rbuf;

	rbuf = malloc(sizeof *rbuf + len);
	if (!rbuf)
		return NULL;
	rbuf->next = NULL;
	rbuf->data = rbuf+1;
	rbuf->data_len = len;
	rbuf->mr = ibv_reg_mr(x->pd, rbuf->data, len, f);
	if (!rbuf->mr) {
		free(rbuf);
		fprintf(stderr, "recv_buf reg_mr failed\n");
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
	struct rdma_context *ctxt = malloc(sizeof *ctxt);
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

static int rdma_xprt_connect(struct ldms_xprt *x, struct sockaddr *sin, socklen_t sa_len)
{
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);
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

	/* Create the CM event thread that will wait for events on the CM channel */
	rc = pthread_create(&r->cm_thread, NULL, cm_thread_proc, r);
	if (rc) {
		rc = ENOMEM;
		goto err_0;
	}

	/* Bind the provided address to the CM Id */
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
		goto err_0;
	}

	/* Create the CQ event thread that will wait for events on the CQ channel */
	rc = pthread_create(&r->cq_thread, NULL, cq_thread_proc, r);
	if (rc) {
		rc = ENOMEM;
		goto err_0;
	}
	errno = rc;
	return rc;

 err_0:
	rdma_xprt_term(r);
	errno = rc;
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
		fprintf(stderr, "ibv_post_recv failed: %d\n", ret);
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

static void cleanup_proc(void *arg)
{
	struct ldms_rdma_xprt *r = arg;

	ldms_release_xprt(r->xprt);
}

static void server_cq_handler(struct ldms_rdma_xprt *x)
{
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	int ret;

	while (1) {
		/* Get the next event ... this will block */
		ret = ibv_get_cq_event(x->cq_channel, &ev_cq, &ev_ctx);
		if (ret)
			break;

		/* Re-arm the CQ */
		ret = ibv_req_notify_cq(ev_cq, 0);
		if (ret)
			break;

		/* Process the CQ */
		ret = cq_event_handler(ev_cq);

		/* Ack the event */
		ibv_ack_cq_events(ev_cq, 1);
	}
}

static void *server_proc(void *arg)
{
	struct ldms_rdma_xprt *x = arg;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_cleanup_push(cleanup_proc, x);
	server_cq_handler(x);
	pthread_cleanup_pop(1);
	return NULL;
}

static void rdma_accept_request(struct ldms_rdma_xprt *server,
				struct rdma_cm_id *cma_id)
{
	struct ldms_rdma_xprt *r;
	struct rdma_conn_param conn_param;
	struct ibv_qp_init_attr qp_attr;
	ldms_t _x;
	int ret;

	/* Create a transport instance for this new connection */
	_x = ldms_create_xprt("rdma", server->xprt->log);
	if (!_x) {
		fprintf(stderr, "Could not create a new transport.\n");
		return;
	}

	/* Replace listener context with new connection context */
	r = rdma_from_xprt(_x);
	r->cm_id = cma_id;
	r->cm_id->context = r;
	r->xprt->connected = 1;

	/* Allocate PD */
	r->pd = ibv_alloc_pd(r->cm_id->verbs);
	if (!r->pd) {
		fprintf(stderr, "ibv_alloc_pd failed\n");
		goto out_0;
	}

	r->cq_channel = ibv_create_comp_channel(r->cm_id->verbs);
	if (!r->cq_channel) {
		fprintf(stderr, "ibv_create_comp_channel failed\n");
		goto out_0;
	}

	/* Create a new CQ. Our parent's CQ channel will get notifications */
	r->cq = ibv_create_cq(r->cm_id->verbs, SQ_DEPTH + RQ_DEPTH,
			      r, r->cq_channel, 0);
	if (!r->cq) {
		fprintf(stderr, "ibv_create_cq failed\n");
		goto out_0;
	}

	ret = ibv_req_notify_cq(r->cq, 0);
	if (ret) {
		fprintf(stderr, "ibv_create_cq failed\n");
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
		fprintf(stderr, "rdma_create_qp failed\n");
		goto out_0;
	}

	/* Post receive buffers to the RQ */
	if (rdma_fill_rq(r))
		goto out_0;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 4;
	conn_param.initiator_depth = 1;

	/* Accept the connection */
	ret = rdma_accept(r->cm_id, &conn_param);
	if (ret)
		goto out_0;

	/* Create the CQ event thread that will wait for events on the
	 * CQ channel */
	ret = pthread_create(&r->cq_thread, NULL, server_proc, r);
	if (ret) {
		/* NB: Racing with CM callback thread here. */
		rdma_disconnect(r->cm_id);

		/* Clean up synchronously */
		server_proc(r);
	}
	return;

 out_0:
	rdma_xprt_term(r);
	ldms_release_xprt(_x);
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
			if (wc.status != IBV_WC_WR_FLUSH_ERR)
				fprintf(stderr,
					"cq completion failed status %d\n",
					wc.status);
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
			fprintf(stderr, "unknown!!!!! completion\n");
			ret = -1;
			goto err;
		}
		rdma_context_free(ctxt);
	}
	if (ret) {
		fprintf(stderr, "poll error %d\n", ret);
		goto err;
	}

	return 0;

err:
	return ret;
}

static void *cq_thread_proc(void *arg)
{
	struct ldms_rdma_xprt *x = arg;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_cleanup_push(cleanup_proc, x);
	while (1) {
		struct ibv_cq *ev_cq;
		void *ev_ctx;
		int ret;

		/* Get the next event ... this will block */
		ret = ibv_get_cq_event(x->cq_channel, &ev_cq, &ev_ctx);
		if (ret)
			break;

		/* Re-arm the CQ */
		ret = ibv_req_notify_cq(ev_cq, 0);
		if (ret)
			break;

		/* Process the CQ */
		ret = cq_event_handler(ev_cq);

		/* Ack the event */
		ibv_ack_cq_events(ev_cq, 1);
		if (ret)
			break;
	}
	pthread_cleanup_pop(1);
	return NULL;
}

int rdma_setup_conn(struct ldms_rdma_xprt *x)
{
	struct rdma_conn_param conn_param;
	struct ibv_qp_init_attr qp_attr;
	int ret = -ENOMEM;

	x->pd = ibv_alloc_pd(x->cm_id->verbs);
	if (!x->pd) {
		fprintf(stderr, "ibv_alloc_pd failed\n");
		goto err_0;
	}

	x->cq_channel = ibv_create_comp_channel(x->cm_id->verbs);
	if (!x->cq_channel) {
		fprintf(stderr, "ibv_create_comp_channel failed\n");
		goto err_0;
	}

	x->cq = ibv_create_cq(x->cm_id->verbs, SQ_DEPTH + RQ_DEPTH,
			      x, x->cq_channel, 0);
	if (!x->cq) {
		fprintf(stderr, "ibv_create_cq failed\n");
		goto err_0;
	}

	ret = ibv_req_notify_cq(x->cq, 0);
	if (ret) {
		fprintf(stderr, "ibv_create_cq failed\n");
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
		fprintf(stderr, "rdma_create_qp failed\n");
		goto err_0;
	}

	ret = rdma_fill_rq(x);
	if (ret)
		goto err_0;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 4;
	conn_param.retry_count = 10;

	return rdma_connect(x->cm_id, &conn_param);

 err_0:
	x->conn_status = CONN_ERROR;
	return ret;
}

static int cma_event_handler(struct ldms_rdma_xprt *server,
			     struct rdma_cm_id *cma_id,
			     struct rdma_cm_event *event)
{
	int ret = 0;
	struct ldms_rdma_xprt *x = cma_id->context;

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		ret = rdma_resolve_route(cma_id, 2000);
		break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		ret = rdma_setup_conn(x);
		break;

	case RDMA_CM_EVENT_CONNECT_REQUEST:
		rdma_accept_request(server, cma_id);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		x->conn_status = CONN_CONNECTED;
		sem_post(&x->sem);
		break;

	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_REJECTED:
		x->conn_status = CONN_ERROR;
		ret = -1;
		sem_post(&x->sem);
		break;

	case RDMA_CM_EVENT_DISCONNECTED:
		x->conn_status = CONN_CLOSED;
		sem_post(&x->sem);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		x->conn_status = CONN_ERROR;
		sem_post(&x->sem);
		break;

	default:
		fprintf(stderr, "unhandled event: %s, ignoring\n",
			rdma_event_str(event->event));
		break;
	}
	if (ret) {
		x->conn_status = CONN_ERROR;
		sem_post(&x->sem);
	}
	return ret;
}

/**
 * Connection Manager Thread - event thread that processes CM events.
 */
static void *cm_thread_proc(void *arg)
{
	struct ldms_rdma_xprt *x = arg;
	struct rdma_cm_event *event;
	int ret;

	while (1) {
		pthread_testcancel();
		ret = rdma_get_cm_event(x->cm_channel, &event);
		if (ret)
			pthread_exit(&ret);

		ret = cma_event_handler(x, event->id, event);
		rdma_ack_cm_event(event);
		if (ret)
			pthread_exit(&ret);
	}
}

static int rdma_xprt_listen(struct ldms_xprt *x, struct sockaddr *s, socklen_t sa_len)
{
	int rc;
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);

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
		goto err_0;

	/* Bind the provided address to the CM Id */
	rc = rdma_bind_addr(r->cm_id, (struct sockaddr *)&x->local_ss);
	if (rc)
		goto err_0;

	/* Asynchronous listen. Connection requests handled in
	 * cm_thread_proc */
	rc = rdma_listen(r->cm_id, 3);
	if (rc)
		goto err_0;

	/* Create the CM event thread that will wait for events on the CM channel */
	rc = pthread_create(&r->cm_thread, NULL, cm_thread_proc, r);
	if (rc)
		goto err_0;

	return 0;

 err_0:
	rdma_xprt_term(r);
	errno = rc;
	return rc;
}

static void rdma_xprt_close(struct ldms_xprt *x)
{
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);
	rdma_xprt_term(r);

}

static int rdma_xprt_send(struct ldms_xprt *x, void *buf, size_t len)
{
	struct ldms_request *req = buf;
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);
	struct rdma_buffer *rbuf;
	int rc;

	if (r->conn_status != CONN_CONNECTED) {
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

	lbuf = malloc(sizeof *lbuf);
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
		desc->xprt_data = malloc(xprt_data_len);
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
	struct rdma_context *ctxt = rdma_context_alloc(context, IBV_WR_RDMA_READ, NULL);

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

static int rdma_read_meta_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
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

static int rdma_read_data_start(struct ldms_xprt *x, ldms_set_t s, size_t len, void *context)
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

struct ldms_xprt *xprt_get(int (*recv_cb)(struct ldms_xprt *, void *),
			   int (*read_complete_cb)(struct ldms_xprt *, void *))
{
	struct ldms_xprt *x;
	struct ldms_rdma_xprt *r;
	x = malloc(sizeof (*x));
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
	x->destroy = rdma_xprt_close;
	x->send = rdma_xprt_send;
	x->read_meta_start = rdma_read_meta_start;
	x->read_data_start = rdma_read_data_start;
	x->read_complete_cb = read_complete_cb;
	x->recv_cb = recv_cb;
	x->alloc = rdma_rbuf_alloc;
	x->free = rdma_rbuf_free;
	x->private = r;
	r->xprt = x;
	sem_init(&r->sem, 0, 0);

	LIST_INSERT_HEAD(&rdma_list, r, client_link);

	return x;

 err_1:
	free(x);
 err_0:
	return NULL;
}
