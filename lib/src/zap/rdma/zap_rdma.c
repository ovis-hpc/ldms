/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2019 Open Grid Computing, Inc. All rights reserved.
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
#include <sys/errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <infiniband/verbs.h>
#include "coll/rbt.h"
#include "zap_rdma.h"

#define LOG__(ep, fmt, ...) do { \
	if (ep->z && ep->z->log_fn) \
		ep->z->log_fn(fmt, ##__VA_ARGS__); \
} while(0)

#define LOG_(rep, fmt, ...) do { \
	if (rep && rep->ep.z && rep->ep.z->log_fn) \
		rep->ep.z->log_fn(fmt, ##__VA_ARGS__); \
} while (0)

#ifdef DEBUG
#define DLOG__(ep, fmt, ...) LOG__(ep, "DEBUG: " fmt, ##__VA_ARGS__)
#define DLOG_(rep, fmt, ...) LOG_(rep, "DEBUG: " fmt, ##__VA_ARGS__)
#else
#define DLOG__(ep, fmt, ...)
#define DLOG_(rep, fmt, ...)
#endif

#ifdef EP_DEBUG
#define __zap_get_ep( _EP ) do { \
	(_EP)->z->log_fn("EP_DEBUG: %s() GET %p, ref_count: %d\n", __func__, _EP, \
						( _EP )->ref_count+1); \
	zap_get_ep(_EP); \
} while (0)
#define __zap_put_ep( _EP ) do { \
	(_EP)->z->log_fn("EP_DEBUG: %s() PUT %p, ref_count: %d\n", __func__, _EP, \
						( _EP )->ref_count-1); \
	zap_put_ep(_EP); \
} while (0)
#define __rdma_deliver_disconnected( _REP ) do { \
	(_REP)->ep.z->log_fn("EP_DEBUG: %s() deliver_disonnected %p, ref_count: %d\n", \
			__func__, _REP, (_REP)->ep.ref_count); \
	_rdma_deliver_disconnected(_REP); \
} while (0)
#else
#define __zap_get_ep(_EP) zap_get_ep(_EP)
#define __zap_put_ep(_EP) zap_put_ep(_EP)
#define __rdma_deliver_disconnected(_REP) _rdma_deliver_disconnected(_REP)
#endif

#ifdef CTXT_DEBUG
#define __flush_io_q( _REP ) do { \
	(_REP)->ep.z->log_fn("TMP_DEBUG: %s() flush_io_q %p, state %s\n", \
			__func__, _REP, zap_ep_state_str(_REP->ep.state)); \
	flush_io_q(_REP); \
} while (0)
#define __rdma_context_alloc( _REP, _CTXT, _OP, _RBUF ) ({ \
	(_REP)->ep.z->log_fn("TMP_DEBUG: %s(), context_alloc\n", __func__); \
	_rdma_context_alloc(_REP, _CTXT, _OP, _RBUF); \
})
#define __rdma_context_free( _CTXT ) ({ \
	(_CTXT)->ep->z->log_fn("TMP_DEBUG: %s(), context_free\n", __func__); \
	_rdma_context_free(_CTXT); \
})
#else /* CTXT_DEBUG */
#define __flush_io_q(_REP) flush_io_q(_REP)
#define __rdma_context_alloc( _REP, _CTXT, _OP, _RBUF ) _rdma_context_alloc(_REP, _CTXT, _OP, _RBUF)
#define __rdma_context_free( _CTXT ) _rdma_context_free(_CTXT)
#endif /* CTXT_DEBUG */

static int context_cmp(void *a, const void *b)
{
	if (a < b)
		return -1;
	if (a > b)
		return 1;
	return 0;
}

struct context_tree_entry {
	struct ibv_context *context;
	struct ibv_pd *pd;
	struct rbn rbn;
};
struct rbt context_tree = { 0, context_cmp };
pthread_mutex_t context_tree_lock = PTHREAD_MUTEX_INITIALIZER;
static struct ibv_pd *__rdma_get_pd(struct ibv_context *context)
{
	struct ibv_pd *pd = NULL;
	struct context_tree_entry *ce;
	struct rbn *rbn;

	pthread_mutex_lock(&context_tree_lock);
	rbn = rbt_find(&context_tree, context);
	if (rbn) {
		ce = container_of(rbn, struct context_tree_entry, rbn);
		pd = ce->pd;
		goto out;
	}
	pd = ibv_alloc_pd(context);
	if (!pd)
		goto out;
	ce = calloc(1, sizeof(*ce));
	if (!ce) {
		ibv_dealloc_pd(pd);
		pd = NULL;
		goto out;
	}
	ce->pd = pd;
	ce->context = context;
	rbn_init(&ce->rbn, context);
	rbt_ins(&context_tree, &ce->rbn);
 out:
	pthread_mutex_unlock(&context_tree_lock);
	return pd;
}

LIST_HEAD(ep_list, z_rdma_ep) ep_list;

static int z_rdma_fill_rq(struct z_rdma_ep *ep);
static void *cm_thread_proc(void *arg);
static void *cq_thread_proc(void *arg);
static int cq_event_handler(struct ibv_cq *cq, int count);
static void _rdma_context_free(struct z_rdma_context *ctxt);
static void __rdma_buffer_free(struct z_rdma_buffer *rbuf);
static int send_credit_update(struct z_rdma_ep *ep);
static void _rdma_deliver_disconnected(struct z_rdma_ep *rep);

static struct z_rdma_context *__rdma_get_context(struct ibv_wc *wc)
{
	return (struct z_rdma_context *)(unsigned long)wc->wr_id;
}

#define RDMA_SET_CONTEXT(__wr, __ctxt) \
	(__wr)->wr_id = (uint64_t)(unsigned long)(__ctxt);

static int cq_fd;
static int cm_fd;
#define CQ_EVENT_LEN 16

static int __enable_cq_events(struct z_rdma_ep *rep)
{
	/* handle CQ events */
	struct epoll_event cq_event;
	cq_event.data.ptr = rep;
	cq_event.events = EPOLLIN | EPOLLOUT;

	/* Release when deleting the cq_channel fd from the epoll */
	__zap_get_ep(&rep->ep);
	if (epoll_ctl(cq_fd, EPOLL_CTL_ADD, rep->cq_channel->fd, &cq_event)) {
		LOG_(rep, "RMDA: epoll_ctl CTL_ADD failed\n");
		__zap_put_ep(&rep->ep); /* Taken before adding cq_channel fd to epoll*/
		return errno;
	}

	return 0;
}

static void __rdma_teardown_conn(struct z_rdma_ep *ep)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;

	if (rep->cm_id && rep->ep.state == ZAP_EP_CONNECTED)
		assert(0);

	errno = 0;

	/*
	 * The last argument that is ignored needs to be non-NULL to
	 * support Linux version < 2.6.9
	 */
	struct epoll_event ignore;
	if (rep->cm_channel) {
		if (epoll_ctl(cm_fd, EPOLL_CTL_DEL, rep->cm_channel->fd, &ignore))
			LOG_(rep, "RDMA: epoll_ctl delete cm_channel "
					"error %d\n", errno);
	}

	/* Destroy the QP */
	if (rep->qp) {
		rdma_destroy_qp(rep->cm_id);
		rep->qp = NULL;
	}

	/* Destroy the RQ CQ */
	if (rep->rq_cq) {
		if (ibv_destroy_cq(rep->rq_cq))
			LOG_(rep, "RDMA: Error %d : ibv_destroy_cq failed\n",
			     errno);
		else
			rep->rq_cq = NULL;
	}
	/* Destroy the SQ CQ */
	if (rep->sq_cq) {
		if (ibv_destroy_cq(rep->sq_cq))
			LOG_(rep, "RDMA: Error %d : ibv_destroy_cq failed\n",
			     errno);
		else
			rep->sq_cq = NULL;
	}
	/* Destroy the CM id */
	if (rep->cm_id) {
		if (rdma_destroy_id(rep->cm_id))
			LOG_(rep, "RDMA: Error %d : rdma_destroy_id failed\n",
			     errno);
		else
			rep->cm_id = NULL;
	}
	if (rep->cm_channel) {
		rdma_destroy_event_channel(rep->cm_channel);
		rep->cm_channel = NULL;
	}

	if (rep->cq_channel) {
		if (ibv_destroy_comp_channel(rep->cq_channel))
			LOG_(rep, "RDMA: Error %d : "
			     "ibv_destroy_comp_channel failed\n", errno);
		else
			rep->cq_channel = NULL;
	}

	rep->rem_rq_credits = RQ_DEPTH;
	rep->sq_credits = SQ_DEPTH;
	rep->lcl_rq_credits = 0;
}

static void z_rdma_destroy(zap_ep_t zep)
{
	assert(zep->ref_count == 0);
	struct z_rdma_ep *rep = (void*)zep;
	pthread_mutex_lock(&rep->ep.lock);
	__rdma_teardown_conn(rep);
	pthread_mutex_unlock(&rep->ep.lock);
	if (rep->parent_ep)
		__zap_put_ep(&rep->parent_ep->ep);
	DLOG_(rep, "rep: %p freed\n", rep);
	free(rep);
}

static int __rdma_setup_conn(struct z_rdma_ep *rep)
{
	struct ibv_qp_init_attr qp_attr;
	int ret = -ENOMEM;

	/* Allocate PD */
	rep->pd = __rdma_get_pd(rep->cm_id->verbs);
	if (!rep->pd) {
		LOG_(rep, "RDMA: ibv_alloc_pd failed\n");
		goto err_0;
	}

	rep->cq_channel = ibv_create_comp_channel(rep->cm_id->verbs);
	if (!rep->cq_channel) {
		LOG_(rep, "RDMA: ibv_create_comp_channel failed\n");
		goto err_0;
	}

	/* Create a new Send Queue CQ. */
	rep->sq_cq = ibv_create_cq(rep->cm_id->verbs,
				   SQ_DEPTH + 2,
				   rep, rep->cq_channel, 0);
	if (!rep->sq_cq) {
		LOG_(rep, "RDMA: ibv_create_cq failed\n");
		goto err_0;
	}

	/* Create a new Receive Queue CQ. */
	rep->rq_cq = ibv_create_cq(rep->cm_id->verbs,
				   RQ_DEPTH + 2,
				   rep, rep->cq_channel, 0);
	if (!rep->rq_cq) {
		LOG_(rep, "RDMA: ibv_create_cq failed\n");
		goto err_0;
	}

	ret = ibv_req_notify_cq(rep->rq_cq, 0);
	if (ret) {
		LOG_(rep, "RMDA: ibv_create_cq failed\n");
		goto err_0;
	}

	ret = ibv_req_notify_cq(rep->sq_cq, 0);
	if (ret) {
		LOG_(rep, "RMDA: ibv_create_cq failed\n");
		goto err_0;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.cap.max_send_wr = SQ_DEPTH + 2; /* +2 for the credit update */
	qp_attr.cap.max_recv_wr = RQ_DEPTH + 2; /* +2 for the credit update */
	qp_attr.cap.max_recv_sge = SQ_SGE;
	qp_attr.cap.max_send_sge = RQ_SGE;
	qp_attr.qp_type = IBV_QPT_RC;
	qp_attr.send_cq = rep->sq_cq;
	qp_attr.recv_cq = rep->rq_cq;

	ret = rdma_create_qp(rep->cm_id, rep->pd, &qp_attr);
	rep->qp = rep->cm_id->qp;
	if (ret) {
		LOG_(rep, "RDMA: rdma_create_qp failed\n");
		goto err_0;
	}

	ret = z_rdma_fill_rq(rep);
	if (ret)
		goto err_0;

	return 0;

err_0:
	rep->ep.state = ZAP_EP_ERROR;
	return ret;
}

static struct z_rdma_buffer *
__rdma_buffer_alloc(struct z_rdma_ep *rep, size_t len,
		  enum ibv_access_flags f)
{
	struct z_rdma_buffer *rbuf;

	rbuf = calloc(1, sizeof *rbuf + len);
	if (!rbuf)
		return NULL;
	rbuf->data = (char *)(rbuf+1);
	rbuf->data_len = len;
	rbuf->mr = ibv_reg_mr(rep->pd, rbuf->data, len, f);
	if (!rbuf->mr) {
		free(rbuf);
		LOG_(rep, "RDMA: reg_mr failed: error %d\n", errno);
		return NULL;
	}
	return rbuf;
}

static void __rdma_buffer_free(struct z_rdma_buffer *rbuf)
{
	ibv_dereg_mr(rbuf->mr);
	free(rbuf);
}

static inline
int __ep_state_check(struct z_rdma_ep *rep)
{
	zap_err_t rc = 0;
	if (rep->ep.state != ZAP_EP_CONNECTED)
		rc = ZAP_ERR_NOT_CONNECTED;
	return rc;
}

/* Must be called with the endpoint lock held */
static struct z_rdma_context *
_rdma_context_alloc(struct z_rdma_ep *rep,
		   void *usr_context, enum ibv_wc_opcode op,
		   struct z_rdma_buffer *rbuf)
{
	struct z_rdma_context *ctxt = calloc(1, sizeof *ctxt);
	if (!ctxt)
		return NULL;

	ctxt->usr_context = usr_context;
	ctxt->op = op;
	ctxt->rb = rbuf;
	ctxt->ep = &rep->ep;
	__zap_get_ep(&rep->ep);
	LIST_INSERT_HEAD(&rep->active_ctxt_list, ctxt, active_ctxt_link);
	return ctxt;
}

/* Must be called with the endpoint lock held */
static void _rdma_context_free(struct z_rdma_context *ctxt)
{
	LIST_REMOVE(ctxt, active_ctxt_link);
	__zap_put_ep(ctxt->ep);
	free(ctxt);
}

/*
 * Enqueue a context to the pending I/O queue.
 *
 * Must be called with the credit_lock held.
 */
static int queue_io(struct z_rdma_ep *rep, struct z_rdma_context *ctxt)
{
	TAILQ_INSERT_TAIL(&rep->io_q, ctxt, pending_link);
	return 0;
}

/* Must be called with the credit lock held */
static void flush_io_q(struct z_rdma_ep *rep)
{
	struct z_rdma_context *ctxt;
	struct zap_event ev = {
		.status = ZAP_ERR_FLUSH,
	};

	while (!TAILQ_EMPTY(&rep->io_q)) {
		ctxt = TAILQ_FIRST(&rep->io_q);
		TAILQ_REMOVE(&rep->io_q, ctxt, pending_link);
		switch (ctxt->op) {
		case IBV_WC_SEND:
			/*
			 * Zap version 1.3.0.0 doesn't have
			 * the SEND_COMPLETE event
			 */
			goto free;
			break;
		case IBV_WC_RDMA_WRITE:
			ev.type = ZAP_EVENT_WRITE_COMPLETE;
			ev.context = ctxt->usr_context;
			break;
		case IBV_WC_RDMA_READ:
			ev.type = ZAP_EVENT_READ_COMPLETE;
			ev.context = ctxt->usr_context;
			break;
		case IBV_WC_RECV:
		default:
			assert(0 == "Invalid type");
			break;
		}
		rep->ep.cb(&rep->ep, &ev);

	free:
		if (ctxt->rb)
			__rdma_buffer_free(ctxt->rb);
		__rdma_context_free(ctxt);
	}

}

/*
 * Called by submit_pending and z_rdma_post_send.
 *
 * Must be called with the credit_lock held.
 */
static zap_err_t
post_send(struct z_rdma_ep *rep,
	  struct z_rdma_context *ctxt, struct ibv_send_wr **bad_wr,
	  int is_rdma)
{
	int rc;
	struct z_rdma_message_hdr *msg;
	/*
	 * RDMA_READ/WRITE do not have a message header.
	 */
	if (!is_rdma) {
		msg = (struct z_rdma_message_hdr *)(unsigned long)
			ctxt->wr.sg_list[0].addr;
		msg->credits = htons(rep->lcl_rq_credits);
		rep->lcl_rq_credits = 0;
	}
	zap_err_t zrc = ZAP_ERR_OK;
	rc = ibv_post_send(rep->qp, &ctxt->wr, bad_wr);
	if (rc) {
		DLOG_(rep, "%s: error %d posting send is_rdma %d sq_credits %d "
		     "lcl_rq_credits %d rem_rq_credits %d.\n", __func__,
		     rc, is_rdma, rep->sq_credits, rep->lcl_rq_credits,
		     rep->rem_rq_credits);
		zrc = ZAP_ERR_TRANSPORT;
	}
	return zrc;
}

char * op_str[] = {
	[IBV_WC_SEND]        =  "IBV_WC_SEND",
	[IBV_WC_RDMA_WRITE]  =  "IBV_WC_RDMA_WRITE",
	[IBV_WC_RDMA_READ]   =  "IBV_WC_RDMA_READ",
	[IBV_WC_COMP_SWAP]   =  "IBV_WC_COMP_SWAP",
	[IBV_WC_FETCH_ADD]   =  "IBV_WC_FETCH_ADD",
	[IBV_WC_BIND_MW]     =  "IBV_WC_BIND_MW"
};

/*
 * This version is called from within get_credits, after checking for
 * previously queued I/O, and from submit_pending. It requires that the
 * credit_lock is held by the caller.
 */
static int _get_credits(struct z_rdma_ep *rep, int is_rdma)
{
	/* Get an SQ credit first */
	if (rep->sq_credits)
		rep->sq_credits--;
	else
		return EWOULDBLOCK;

	if (is_rdma)
		/* RDMA ops don't need an RQ credit */
		return 0;

	/* Get an RQ credit */
	if (rep->rem_rq_credits)
		rep->rem_rq_credits--;
	else {
		/* Return the SQ credit taken above */
		rep->sq_credits++;
		return EWOULDBLOCK;
	}
	return 0;
}

/*
 * Determines if there is space in the SQ for all WR and if there is a
 * slot available in the remote peer's RQ for SENDs.
 */
static int get_credits(struct z_rdma_ep *rep, int is_rdma)
{
	int rc = 0;

	/* Don't submit I/O ahead of previously posted I/O. */
	if (!TAILQ_EMPTY(&rep->io_q)) {
		rc = EWOULDBLOCK;
		goto out;
	}
	rc = _get_credits(rep, is_rdma);
 out:
	return rc;
}

static void put_sq(struct z_rdma_ep *rep)
{
	pthread_mutex_lock(&rep->credit_lock);
	if (rep->sq_credits < SQ_DEPTH)
		rep->sq_credits++;
	pthread_mutex_unlock(&rep->credit_lock);
}

/*
 * Walk the list of pending I/O and submit if there are sufficient
 * credits available.
 */
static void submit_pending(struct z_rdma_ep *rep)
{
	struct ibv_send_wr *badwr;
	struct z_rdma_context *ctxt;
	int is_rdma;

	pthread_mutex_lock(&rep->credit_lock);
	while (!TAILQ_EMPTY(&rep->io_q)) {
		ctxt = TAILQ_FIRST(&rep->io_q);
		is_rdma = (ctxt->op != IBV_WC_SEND);
		if (_get_credits(rep, is_rdma))
			goto out;

		TAILQ_REMOVE(&rep->io_q, ctxt, pending_link);

		if (post_send(rep, ctxt, &badwr, is_rdma)) {
			LOG_(rep, "Error posting queued I/O.\n");
			__rdma_context_free(ctxt);
		}

	}
 out:
	pthread_mutex_unlock(&rep->credit_lock);
}

static zap_err_t __rdma_post_send(struct z_rdma_ep *rep, struct z_rdma_buffer *rbuf)
{
	int rc;
	struct ibv_send_wr *bad_wr;

	pthread_mutex_lock(&rep->ep.lock);
	struct z_rdma_context *ctxt =
		__rdma_context_alloc(rep, NULL, IBV_WC_SEND, rbuf);
	if (!ctxt) {
		errno = ENOMEM;
		pthread_mutex_unlock(&rep->ep.lock);
		return ZAP_ERR_RESOURCE;
	}
	pthread_mutex_unlock(&rep->ep.lock);

	ctxt->sge.addr = (uint64_t) (unsigned long) rbuf->data;
	ctxt->sge.length = rbuf->data_len;
	ctxt->sge.lkey = rbuf->mr->lkey;

	ctxt->wr.opcode = IBV_WR_SEND;
	ctxt->wr.next = NULL;
	ctxt->wr.send_flags = IBV_SEND_SIGNALED;
	ctxt->wr.sg_list = &ctxt->sge;
	ctxt->wr.num_sge = 1;

	RDMA_SET_CONTEXT(&ctxt->wr, ctxt);

	pthread_mutex_lock(&rep->credit_lock);
	if (!get_credits(rep, 0))
		rc = post_send(rep, ctxt, &bad_wr, 0);
	else
		rc = queue_io(rep, ctxt);
	pthread_mutex_unlock(&rep->credit_lock);
	if (rc)
		goto err_0;

	return rc;
 err_0:
	pthread_mutex_lock(&rep->ep.lock);
	__rdma_context_free(ctxt);
	pthread_mutex_unlock(&rep->ep.lock);
	return ZAP_ERR_NOT_CONNECTED;
}

static zap_err_t z_rdma_close(zap_ep_t ep)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	pthread_mutex_lock(&rep->ep.lock);
	if (!rep->cm_id)
		goto out;
	switch (rep->ep.state) {
	case ZAP_EP_LISTENING:
	case ZAP_EP_CONNECTED:
		rdma_disconnect(rep->cm_id);
		rep->ep.state = ZAP_EP_CLOSE;
		break;
	case ZAP_EP_ERROR:
		rdma_disconnect(rep->cm_id);
		rep->ep.state = ZAP_EP_ERROR;
		break;
	case ZAP_EP_PEER_CLOSE:
		/* rdma_disconnect is called already. */
	default:
		/* suppressing compilation warning about unhandling cases */
		break;
	}
 out:
	pthread_mutex_unlock(&rep->ep.lock);
	return ZAP_ERR_OK;
}

static zap_err_t z_rdma_connect(zap_ep_t ep,
				struct sockaddr *sin, socklen_t sa_len,
				char *data, size_t data_len)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct epoll_event cm_event;
	zap_err_t zerr;
	int rc;

	ZAP_VERSION_SET(rep->conn_data.v);

	if (data_len > ZAP_RDMA_CONN_DATA_MAX) {
		zerr = ZAP_ERR_PARAMETER;
		goto err_0;
	}

	if (data_len) {
		rep->conn_data.data_len = data_len;
		memcpy(rep->conn_data.data, data, data_len);
	}

	zerr = zap_ep_change_state(&rep->ep, ZAP_EP_INIT, ZAP_EP_CONNECTING);
	if (zerr)
		goto err_0;

	/* Create the event CM event channel */
	zerr = ZAP_ERR_RESOURCE;
	rep->cm_channel = rdma_create_event_channel();
	if (!rep->cm_channel)
		goto err_1;

	/* Create the connecting CM ID */
	rc = rdma_create_id(rep->cm_channel, &rep->cm_id, rep, RDMA_PS_TCP);
	if (rc)
		goto err_1;

	cm_event.events = EPOLLIN | EPOLLOUT;
	cm_event.data.ptr = rep;
	__zap_get_ep(&rep->ep); /* Release when disconnected */
	rc = epoll_ctl(cm_fd, EPOLL_CTL_ADD, rep->cm_channel->fd, &cm_event);
	if (rc)
		goto err_2;

	/* Bind the provided address to the CM Id */
	rc = rdma_resolve_addr(rep->cm_id, NULL, sin, 2000);
	if (rc) {
		zerr = ZAP_ERR_ADDRESS;
		goto err_3;
	}

	return ZAP_ERR_OK;

 err_3:
	/*
	 * These are all synchronous clean-ups. IB does not support
	 * re-connecting on the same cm_id, so blow everything away
	 * and start over next time.
	 */
	(void)epoll_ctl(cm_fd, EPOLL_CTL_DEL, rep->cm_channel->fd, NULL);
 err_2:
	__zap_put_ep(&rep->ep);
 err_1:
	zap_ep_change_state(&rep->ep, ZAP_EP_CONNECTING, ZAP_EP_INIT);
 err_0:
	return zerr;
}

static int __rdma_post_recv(struct z_rdma_ep *rep, struct z_rdma_buffer *rb)
{
	struct ibv_recv_wr wr;
	struct ibv_sge sge;
	struct ibv_recv_wr *bad_wr;
	struct z_rdma_context *ctxt;
	int rc;

	pthread_mutex_lock(&rep->ep.lock);
	ctxt = __rdma_context_alloc(rep, NULL, IBV_WC_RECV, rb);
	if (!ctxt) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}

	sge.addr = (uint64_t) (unsigned long) rb->data;
	sge.length = rb->data_len;
	sge.lkey = rb->mr->lkey;
	wr.sg_list = &sge;
	wr.next = NULL;
	wr.num_sge = 1;

	RDMA_SET_CONTEXT(&wr, ctxt);
	rc = ibv_post_recv(rep->qp, &wr, &bad_wr);
	if (rc) {
		__rdma_context_free(ctxt);
		rc = zap_errno2zerr(rc);
	}
out:
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;
}

zap_err_t z_map_err(int wc_status)
{
	switch (wc_status) {
	case IBV_WC_SUCCESS:
		return ZAP_ERR_OK;

	case IBV_WC_WR_FLUSH_ERR:
		return ZAP_ERR_FLUSH;

	case IBV_WC_LOC_LEN_ERR:
		return ZAP_ERR_LOCAL_LEN;

	case IBV_WC_LOC_EEC_OP_ERR:
	case IBV_WC_LOC_QP_OP_ERR:
	case IBV_WC_FATAL_ERR:
	case IBV_WC_GENERAL_ERR:
		return ZAP_ERR_LOCAL_OPERATION;

	case IBV_WC_LOC_RDD_VIOL_ERR:
	case IBV_WC_LOC_ACCESS_ERR:
	case IBV_WC_LOC_PROT_ERR:
		return ZAP_ERR_LOCAL_PERMISSION;

	case IBV_WC_MW_BIND_ERR:
		return ZAP_ERR_MAPPING;

	case IBV_WC_REM_ACCESS_ERR:
		return ZAP_ERR_REMOTE_PERMISSION;

	case IBV_WC_REM_INV_REQ_ERR:
	case IBV_WC_REM_OP_ERR:
	case IBV_WC_BAD_RESP_ERR:
	case IBV_WC_REM_ABORT_ERR:
	case IBV_WC_INV_EECN_ERR:
	case IBV_WC_INV_EEC_STATE_ERR:
	case IBV_WC_REM_INV_RD_REQ_ERR:
		return ZAP_ERR_REMOTE_OPERATION;

	case IBV_WC_RETRY_EXC_ERR:
	case IBV_WC_RNR_RETRY_EXC_ERR:
		return ZAP_ERR_RETRY_EXCEEDED;

	case IBV_WC_RESP_TIMEOUT_ERR:
		return ZAP_ERR_TIMEOUT;
	default:
		return ZAP_ERR_TRANSPORT;
	};
}

static void process_send_wc(struct z_rdma_ep *rep, struct ibv_wc *wc,
			    struct z_rdma_buffer *rb)
{
	if (!wc->status && rb)
		__rdma_buffer_free(rb);
}

static void process_read_wc(struct z_rdma_ep *rep, struct ibv_wc *wc,
			    void *usr_context)
{
	struct zap_event zev;
	zev.type = ZAP_EVENT_READ_COMPLETE;
	zev.context = usr_context;
	zev.status = z_map_err(wc->status);
	rep->ep.cb(&rep->ep, &zev);
}

static void process_write_wc(struct z_rdma_ep *rep, struct ibv_wc *wc,
			     void *usr_context)
{
	struct zap_event zev;
	zev.type = ZAP_EVENT_WRITE_COMPLETE;
	zev.context = usr_context;
	zev.status = z_map_err(wc->status);
	rep->ep.cb(&rep->ep, &zev);
}

static void handle_recv(struct z_rdma_ep *rep,
			struct z_rdma_message_hdr *msg, size_t len)
{
	struct zap_event zev;

	memset(&zev, 0, sizeof zev);
	zev.type = ZAP_EVENT_RECV_COMPLETE;
	zev.status = ZAP_ERR_OK;
	zev.data = (unsigned char *)(msg + 1);
	zev.data_len = len - sizeof(*msg);
	rep->ep.cb(&rep->ep, &zev);
}

static void handle_rendezvous(struct z_rdma_ep *rep,
			      struct z_rdma_message_hdr *msg, size_t len)
{
	struct zap_event zev;
	struct z_rdma_share_msg *sh = (struct z_rdma_share_msg *)msg;
	struct z_rdma_map *map;

	map = calloc(1, sizeof(*map));
	if (!map)
		return;

	map->map.ref_count = 1;
	map->map.type = ZAP_MAP_REMOTE;
	map->map.ep = &rep->ep;
	map->map.addr = (void *)(unsigned long)sh->va;
	map->map.len = ntohl(sh->len);
	map->map.acc = ntohl(sh->acc);
	map->rkey = sh->rkey;

	__zap_get_ep(&rep->ep); /* will be put in zap_unmap() */
	pthread_mutex_lock(&rep->ep.lock);
	LIST_INSERT_HEAD(&rep->ep.map_list, &map->map, link);
	pthread_mutex_unlock(&rep->ep.lock);

	memset(&zev, 0, sizeof zev);
	zev.type = ZAP_EVENT_RENDEZVOUS;
	zev.status = ZAP_ERR_OK;
	zev.map = &map->map;
	zev.data_len = len - sizeof(*sh);
	if (zev.data_len)
		zev.data = (void*)sh->msg;
	rep->ep.cb(&rep->ep, &zev);
}

static void process_recv_wc(struct z_rdma_ep *rep, struct ibv_wc *wc,
			    struct z_rdma_buffer *rb)
{
	struct z_rdma_message_hdr *msg = (struct z_rdma_message_hdr *)rb->data;
	uint16_t msg_type;
	int ret;

	pthread_mutex_lock(&rep->credit_lock);
	rep->rem_rq_credits += ntohs(msg->credits);
	pthread_mutex_unlock(&rep->credit_lock);

	/*
	 * If this was a credit update, there is no data to deliver
	 * to the application and it did not consume a request slot.
	 */
	msg_type = ntohs(msg->msg_type);
	switch (msg_type) {
	case Z_RDMA_MSG_SEND:
		assert(wc->byte_len >= sizeof(*msg) + 1);
		if (wc->byte_len < sizeof(*msg) + 1)
			goto err_wrong_dsz;
		handle_recv(rep, msg, wc->byte_len);
		break;

	case Z_RDMA_MSG_RENDEZVOUS:
		assert(wc->byte_len >= sizeof(struct z_rdma_share_msg));
		if (wc->byte_len < sizeof(struct z_rdma_share_msg))
			goto err_wrong_dsz;
		handle_rendezvous(rep, msg, wc->byte_len);
		break;

	case Z_RDMA_MSG_CREDIT_UPDATE:
		assert(wc->byte_len == sizeof(struct z_rdma_message_hdr));
		if (wc->byte_len != sizeof(struct z_rdma_message_hdr))
			goto err_wrong_dsz;
		break;
	default:
		LOG_(rep, "%s(): Unknown message type '%d'\n",
				__func__, msg_type);
		assert(0);
	}

	ret = __rdma_post_recv(rep, rb);
	if (ret) {
		LOG_(rep, "ep %p: Post recv buffer fail. %s\n",
				rep, zap_err_str(ret));
		__rdma_buffer_free(rb);
		goto out;
	}

	/* Credit updates are not counted */
	if (msg_type == Z_RDMA_MSG_CREDIT_UPDATE)
		goto out;

	pthread_mutex_lock(&rep->credit_lock);
	if (rep->lcl_rq_credits < RQ_DEPTH)
		rep->lcl_rq_credits ++;
	if (rep->lcl_rq_credits > (RQ_DEPTH >> 1)) {
		if (send_credit_update(rep))
			LOG_(rep, "RDMA: credit update could not be sent.\n");
	}
	pthread_mutex_unlock(&rep->credit_lock);

 out:
	return;
 err_wrong_dsz:
	LOG_(rep, "%s(): msg type '%d': Invalid data size.\n",
				__func__, msg_type);
	z_rdma_close(&rep->ep);
	return;
}

static int z_rdma_fill_rq(struct z_rdma_ep *rep)
{
	int i;

	for (i = 0; i < RQ_DEPTH+2; i++) {
		struct z_rdma_buffer *rbuf =
			__rdma_buffer_alloc(rep, RQ_BUF_SZ,
					  IBV_ACCESS_LOCAL_WRITE);
		if (rbuf) {
			int rc = __rdma_post_recv(rep, rbuf);
			if (rc) {
				__rdma_buffer_free(rbuf);
				return rc;
			}

		} else
			return ENOMEM;
	}
	return 0;
}

static zap_err_t z_rdma_reject(zap_ep_t ep, char *data, size_t data_len)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	int rc;

	struct z_rdma_reject_msg *msg;
	size_t len = sizeof(*msg) + data_len;
	msg = calloc(1, len);
	if (!msg) {
		return ZAP_ERR_RESOURCE;
	}

	msg->hdr.msg_type = htons(Z_RDMA_MSG_REJECT);
	msg->len = data_len;
	memcpy(msg->msg, data, data_len);
	rep->conn_req_decision = Z_RDMA_PASSIVE_REJECT;
	rc = rdma_reject(rep->cm_id, (void *)msg, len);
	if (rc) {
		free(msg);
		return zap_errno2zerr(errno);
	}
	free(msg);
	zap_free(ep);
	return ZAP_ERR_OK;
}

static zap_err_t z_rdma_accept(zap_ep_t ep, zap_cb_fn_t cb,
				char *data, size_t data_len)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct rdma_conn_param conn_param;
	struct z_rdma_accept_msg *msg;

	int ret;

	if (data_len > ZAP_RDMA_ACCEPT_DATA_MAX - sizeof(*msg)) {
		return ZAP_ERR_PARAMETER;
	}

	size_t len = sizeof(*msg) + data_len;
	msg = calloc(1, len);
	if (!msg) {
		return ZAP_ERR_RESOURCE;
	}
	msg->hdr.msg_type = htons(Z_RDMA_MSG_ACCEPT);
	msg->len = data_len;
	memcpy(msg->data, data, data_len);

	/* Replace the callback with the one provided by the caller */
	rep->ep.cb = cb;

	__zap_get_ep(&rep->ep); /* Release when disconnected */
	ret = __rdma_setup_conn(rep);
	if (ret) {
		goto err_0;
	}

	/* Accept the connection */
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 4;
	conn_param.initiator_depth = 4;
	conn_param.private_data_len = len;
	conn_param.private_data = msg;
	ret = rdma_accept(rep->cm_id, &conn_param);
	if (ret) {
		ret = zap_errno2zerr(errno);
		goto err_0;
	}

	rep->conn_req_decision = Z_RDMA_PASSIVE_ACCEPT;
	free(msg);
	return ZAP_ERR_OK;
err_0:
	__zap_put_ep(&rep->ep);
	free(msg);
	return ret;
}

char *err_msg[] = {
	"IBV_WC_SUCCESS",
	"IBV_WC_LOC_LEN_ERR",
	"IBV_WC_LOC_QP_OP_ERR",
	"IBV_WC_LOC_EEC_OP_ERR",
	"IBV_WC_LOC_PROT_ERR",
	"IBV_WC_WR_FLUSH_ERR",
	"IBV_WC_MW_BIND_ERR",
	"IBV_WC_BAD_RESP_ERR",
	"IBV_WC_LOC_ACCESS_ERR",
	"IBV_WC_REM_INV_REQ_ERR",
	"IBV_WC_REM_ACCESS_ERR",
	"IBV_WC_REM_OP_ERR",
	"IBV_WC_RETRY_EXC_ERR",
	"IBV_WC_RNR_RETRY_EXC_ERR",
	"IBV_WC_LOC_RDD_VIOL_ERR",
	"IBV_WC_REM_INV_RD_REQ_ERR",
	"IBV_WC_REM_ABORT_ERR",
	"IBV_WC_INV_EECN_ERR",
	"IBV_WC_INV_EEC_STATE_ERR",
	"IBV_WC_FATAL_ERR",
	"IBV_WC_RESP_TIMEOUT_ERR",
	"IBV_WC_GENERAL_ERR"
};

static int cq_event_handler(struct ibv_cq *cq, int count)
{
	struct ibv_wc wc;
	int rc, poll_count = 0;

	while (count-- && ((rc = ibv_poll_cq(cq, 1, &wc)) == 1)) {
		struct z_rdma_context *ctxt = __rdma_get_context(&wc);
		zap_ep_t ep = ctxt->ep;
		poll_count++;

		if (wc.status) {
			DLOG__(ep, "RDMA: cq_event_handler: endpoint %p, "
				"WR op '%s', wc.status '%s'\n", ep,
				(ctxt->op & IBV_WC_RECV ? "IBV_WC_RECV" : op_str[ctxt->op]),
				err_msg[wc.status]);
			/*
			 * The cm_channel will deliver rejected/disconnected.
			 * The endpoint state will be changed then.
			 * After this point all send/write/read should return a
			 * synchronous error.
			 *
			 */
			if (wc.status != IBV_WC_WR_FLUSH_ERR) {
				LOG__(ep, "RDMA: cq_event_handler: endpoint %p, "
					"WR op '%s', wc.status '%s'\n", ep,
					(ctxt->op & IBV_WC_RECV ? "IBV_WC_RECV" : op_str[ctxt->op]),
					err_msg[wc.status]);
				LOG__(ep, "    addr %p len %d lkey %p.\n",
				      ctxt->wr.sg_list[0].addr,
				      ctxt->wr.sg_list[0].length,
				      ctxt->wr.sg_list[0].lkey);
				pthread_mutex_lock(&ep->lock);
				ep->state = ZAP_EP_ERROR;
				pthread_mutex_unlock(&ep->lock);
				z_rdma_close(ep);
			}
		}

		struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
		switch (ctxt->op) {
		case IBV_WC_SEND:
			process_send_wc(rep, &wc, ctxt->rb);
			put_sq(rep);
			break;

		case IBV_WC_RDMA_WRITE:
			process_write_wc(rep, &wc, ctxt->usr_context);
			put_sq(rep);
			break;

		case IBV_WC_RDMA_READ:
			process_read_wc(rep, &wc, ctxt->usr_context);
			put_sq(rep);
			break;

		case IBV_WC_RECV:
			if (!wc.status)
				process_recv_wc(rep, &wc, ctxt->rb);
			break;

		default:
			LOG_(rep,"RDMA: Invalid completion\n");
		}

		if (wc.status && ctxt->rb)
			__rdma_buffer_free(ctxt->rb);

		pthread_mutex_lock(&rep->ep.lock);
		__rdma_context_free(ctxt);
		pthread_mutex_unlock(&rep->ep.lock);
	}
	return poll_count;
}

static void *cq_thread_proc(void *arg)
{
	int cq_fd = (int)(unsigned long)arg;
	struct epoll_event cq_events[16];
	struct z_rdma_ep *rep;
	int i;
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	int ret;
	int rc;
	sigset_t sigset;

	sigfillset(&sigset);
	rc = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	if (rc) {
		fprintf(stderr, "zap_rdma: ERROR: pthread_sigmask errno: %d\n",
				errno);
		assert(rc == 0);
	}

	while (1) {
		int fd_count = epoll_wait(cq_fd, cq_events, 16, -1);
		if (fd_count < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		for (i = 0; i < fd_count; i++) {
			rep = cq_events[i].data.ptr;
			__zap_get_ep(&rep->ep); /* Release after process the cq_ev */
			/* Get the next event ... this will block */
			ret = ibv_get_cq_event(rep->cq_channel, &ev_cq, &ev_ctx);
			if (ret) {
				LOG_(rep, "cq_channel is %d at %d.\n",
				     rep->cq_channel->fd, __LINE__);
				LOG_(rep, "RDMA: Error %d at %s:%d\n",
					     errno, __func__, __LINE__);
				goto skip;
			}

			/* Ack the event */
			ibv_ack_cq_events(ev_cq, 1);

			/*
			 * Process the CQs. Speculatively reap SQ WQE
			 * in case they are required to respond to a
			 * request sitting in the RQ. Reap RQ WQE one
			 * at a time before revisiting the SQ again to
			 * reclaim space used by a previous response.
			 */
			do {
				if (rep->sq_cq) {
					ret = cq_event_handler(rep->sq_cq, SQ_DEPTH+2);
				}
				if (ev_cq != rep->sq_cq)
					ret += cq_event_handler(ev_cq, 1);
			} while (ret);

			/* Re-arm the CQ */
			ret = ibv_req_notify_cq(ev_cq, 0);
			if (ret) {
				LOG_(rep, "RDMA: Error %d at %s:%d\n",
				     errno, __func__, __LINE__);
			}

		skip:
			pthread_mutex_lock(&rep->ep.lock);
			if (LIST_EMPTY(&rep->active_ctxt_list)) {
				/*
				 * All outstanding work requests
				 * have been completed or flushed.
				 */

				/*
				 * The last argument that is ignored needs to be
				 * non-NULL to support Linux version < 2.6.9
				 */
				struct epoll_event ignore;
				if (epoll_ctl(cq_fd, EPOLL_CTL_DEL,
						rep->cq_channel->fd, &ignore)) {
					LOG_(rep, "RDMA: epoll_ctl: "
						"delete cq_channel error %d\n",
						errno);
				} else {
					/*
					 * Taken when adding the cq_channel
					 * fd to the epoll
					 */
					DLOG_(rep, "RDMA: %p, put reference back "
						"after removing the cq_channel "
						"fd\n", rep);
					__zap_put_ep(&rep->ep);
				}

#ifdef DEBUG
				if (rep->deferred_disconnected == -1) {
					DLOG_(rep, "rdma: %s(): %p, "
						"deferred_disconnected "
						"already called\n");
				}

#endif
				if (rep->deferred_disconnected == 1) {
					/*
					 * Zap has delivered all the work
					 * completion events.
					 *
					 * Now deliver the Zap disconnected event.
					 */
					pthread_mutex_unlock(&rep->ep.lock);
					__rdma_deliver_disconnected(rep);
					rep->deferred_disconnected = -1;
				} else {
					pthread_mutex_unlock(&rep->ep.lock);
				}
			} else {
				pthread_mutex_unlock(&rep->ep.lock);
				submit_pending(rep);
			}
			__zap_put_ep(&rep->ep); /* Taken when getting the ev_cq */
		}
	}
	return NULL;
}

static zap_ep_t z_rdma_new(zap_t z, zap_cb_fn_t cb)
{
	struct z_rdma_ep *rep;

	rep = calloc(1, sizeof *rep);
	if (!rep) {
		errno = ZAP_ERR_RESOURCE;
		return NULL;
	}

	rep->rem_rq_credits = RQ_DEPTH;
	rep->lcl_rq_credits = 0;
	rep->sq_credits = SQ_DEPTH;

	TAILQ_INIT(&rep->io_q);
	LIST_INIT(&rep->active_ctxt_list);
	pthread_mutex_init(&rep->credit_lock, NULL);
	return (zap_ep_t)&rep->ep;
}

char *cma_event_str[] = {
	"RDMA_CM_EVENT_ADDR_RESOLVED",
	"RDMA_CM_EVENT_ADDR_ERROR",
	"RDMA_CM_EVENT_ROUTE_RESOLVED",
	"RDMA_CM_EVENT_ROUTE_ERROR",
	"RDMA_CM_EVENT_CONNECT_REQUEST",
	"RDMA_CM_EVENT_CONNECT_RESPONSE",
	"RDMA_CM_EVENT_CONNECT_ERROR",
	"RDMA_CM_EVENT_UNREACHABLE",
	"RDMA_CM_EVENT_REJECTED",
	"RDMA_CM_EVENT_ESTABLISHED",
	"RDMA_CM_EVENT_DISCONNECTED",
	"RDMA_CM_EVENT_DEVICE_REMOVAL",
	"RDMA_CM_EVENT_MULTICAST_JOIN",
	"RDMA_CM_EVENT_MULTICAST_ERROR",
	"RDMA_CM_EVENT_ADDR_CHANGE",
	"RDMA_CM_EVENT_TIMEWAIT_EXIT"
};

static void
handle_addr_resolved(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id)
{
	struct zap_event zev;
	int ret;
	assert(rep->ep.state == ZAP_EP_CONNECTING);
	ret = rdma_resolve_route(cma_id, 2000);
	if (ret) {
		(void)epoll_ctl(cm_fd, EPOLL_CTL_DEL,
				rep->cm_channel->fd, NULL);
		zev.type = ZAP_EVENT_CONNECT_ERROR;
		zev.status = ZAP_ERR_ADDRESS;
		zap_ep_change_state(&rep->ep, ZAP_EP_CONNECTING, ZAP_EP_ERROR);
		rep->ep.cb(&rep->ep, &zev);
		__zap_put_ep(&rep->ep); /* taken in z_rdma_connect */
	}
}

static void
handle_connect_request(struct z_rdma_ep *rep, struct rdma_cm_event *event)
{
	zap_ep_t new_ep;
	struct z_rdma_ep *new_rep;
	struct zap_event zev = {0};
	assert(rep->ep.state == ZAP_EP_LISTENING);
	zev.type = ZAP_EVENT_CONNECT_REQUEST;
	zev.status = ZAP_ERR_OK;
	struct z_rdma_conn_data *conn_data = (void*)event->param.conn.private_data;
	struct rdma_cm_id *cma_id = event->id;

	/* Check version */
	if (!event->param.conn.private_data_len ||
		!zap_version_check(&conn_data->v)) {
		LOG_(rep, "RDMA: zap version unsupported %hhu.%hhu.\n",
				conn_data->v.major, conn_data->v.minor);
		rdma_reject(cma_id, NULL, 0);
		return;
	}

	if (conn_data->data_len) {
		zev.data_len = conn_data->data_len;
		zev.data = (void*)conn_data->data;
	}

	new_ep = zap_new(rep->ep.z, rep->ep.app_cb);
	if (!new_ep) {
		LOG_(rep, "RDMA: could not create a new"
		     "endpoint for a connection request.\n");
		rdma_reject(cma_id, NULL, 0);
		return;
	}
	void *ctxt = zap_get_ucontext(&rep->ep);
	zap_set_ucontext(new_ep, ctxt);
	new_rep = (struct z_rdma_ep *)new_ep;
	new_rep->cm_id = cma_id;
	new_rep->parent_ep = rep;
	__zap_get_ep(&rep->ep); /* Release when the new endpoint is destroyed */
	cma_id->context = new_rep;
	zap_ep_change_state(new_ep, ZAP_EP_INIT, ZAP_EP_ACCEPTING);
	new_rep->ep.cb(new_ep, &zev);
	/* Don't use the new endpoint after calling the app's callback function
	 * because the endpoint might be destroyed already in case there is
	 * an error in the z_rdma_accept function or the application rejects
	 * the request.
	 */
}

static void
handle_route_resolved(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id)
{
	struct rdma_conn_param conn_param;
	struct zap_event zev;
	int ret;
	assert(rep->ep.state == ZAP_EP_CONNECTING);
	ret = __rdma_setup_conn(rep);
	if (ret)
		goto err;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 4;
	conn_param.initiator_depth = 4;
	conn_param.retry_count = 7;

	/* always send version data */
	conn_param.private_data_len = rep->conn_data.data_len
					+ sizeof(rep->conn_data);
	conn_param.private_data = &rep->conn_data;

	ret = rdma_connect(rep->cm_id, &conn_param);
	if (ret)
		goto err;
	return;
err:
	(void)epoll_ctl(cm_fd, EPOLL_CTL_DEL,
			rep->cm_channel->fd, NULL);
	zev.type = ZAP_EVENT_CONNECT_ERROR;
	zev.status = ZAP_ERR_ROUTE;
	rep->ep.state = ZAP_EP_ERROR;
	rep->ep.cb(&rep->ep, &zev);
	__zap_put_ep(&rep->ep); /* Release the ref taken in z_rdma_connect() */
}

static void
handle_conn_error(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id, int reason)
{
	struct zap_event zev = {0};
	zev.status = reason;
	if (rep->cq_channel)
		__enable_cq_events(rep);
	rep->ep.state = ZAP_EP_ERROR;
	switch (rep->ep.state) {
	case ZAP_EP_ACCEPTING:
		/* Passive side. */
		switch (rep->conn_req_decision) {
		case Z_RDMA_PASSIVE_ACCEPT:
			/*
			 * App accepted the conn req already.
			 * Deliver the error.
			 */
			zev.type = ZAP_EVENT_DISCONNECTED;
			rep->ep.cb(&rep->ep, &zev);
			__zap_put_ep(&rep->ep);
			break;
		case Z_RDMA_PASSIVE_NONE:
			/*
			 * App does not make any decision yet.
			 * No need to deliver any events to the app.
			 */
		case Z_RDMA_PASSIVE_REJECT:
			/*
			 * App rejected the conn req already.
			 * No need to deliver any events to the app.
			 */
			return;
		default:
			LOG_(rep, "Unrecognized connection request "
					"decision '%d'\n",
					rep->conn_req_decision);
			assert(0);
			break;
		}
		break;
	case ZAP_EP_CONNECTING:
	case ZAP_EP_ERROR:
		zev.type = ZAP_EVENT_CONNECT_ERROR;
		rep->ep.cb(&rep->ep, &zev);
		__zap_put_ep(&rep->ep);
		break;
	default:
		assert(0 == "wrong rep->ep.state");
		break;
	}
}

static void
handle_rejected(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id,
					struct rdma_cm_event *event)
{
	struct z_rdma_reject_msg *rej_msg = NULL;
	struct zap_event zev = {0};
	zev.status = ZAP_ERR_CONNECT;

	/* State before being rejected is CONNECTING, but
	 * cq thread (we're on cm thread) can race and change endpoint state to
	 * ERROR from the posted recv. */
	assert(rep->ep.state == ZAP_EP_CONNECTING ||
			rep->ep.state == ZAP_EP_ERROR ||
			rep->ep.state == ZAP_EP_ACCEPTING);

	if (rep->ep.state == ZAP_EP_ACCEPTING) {
		/* passive side. No need to look into the rejected message */
		handle_conn_error(rep, cma_id, ZAP_ERR_CONNECT);
		return;
	}

	/* Active side */
	if (event->param.conn.private_data_len) {
		rej_msg = (struct z_rdma_reject_msg *)
				event->param.conn.private_data;
	}

	if (!rej_msg || ntohs(rej_msg->hdr.msg_type) != Z_RDMA_MSG_REJECT) {
#ifdef ZAP_DEBUG
		rep->rejected_conn_error_count++;
#endif /* ZAP_DEBUG */
		/* The server doesn't exist. */
		handle_conn_error(rep, cma_id, ZAP_ERR_CONNECT);
		return;
	}
#ifdef ZAP_DEBUG
	rep->rejected_count++;
#endif /* ZAP_DEBUG */
	zev.data_len = rej_msg->len;
	zev.data = (unsigned char *)rej_msg->msg;
	zev.type = ZAP_EVENT_REJECTED;

	__enable_cq_events(rep);
	rep->ep.state = ZAP_EP_ERROR;
	rep->ep.cb(&rep->ep, &zev);
	__zap_put_ep(&rep->ep); /* Release the reference taken when the endpoint got created. */
}

static void
handle_established(struct z_rdma_ep *rep, struct rdma_cm_event *event)
{
	struct z_rdma_accept_msg *msg = NULL;
	struct zap_event zev = {
		.type = ZAP_EVENT_CONNECTED,
		.status = ZAP_ERR_OK,
	};
	switch (rep->ep.state) {
	case ZAP_EP_CONNECTING:
	case ZAP_EP_ACCEPTING:
		zap_ep_change_state(&rep->ep, rep->ep.state, ZAP_EP_CONNECTED);
		break;
	default:
		DLOG_(rep, "Error: handle_established: ep %p, "
				"unexpected state '%s'\n",
				rep, zap_ep_state_str(rep->ep.state));
		assert(0 == "wrong rep->ep.state");
	}

	/*
	 * iwarp and ib behave differently. The private data len for IB will
	 * always be non-zero.
	 */
	if (event->param.conn.private_data_len)
		msg = (struct z_rdma_accept_msg *)event->param.conn.private_data;

	if (msg && (ntohs(msg->hdr.msg_type) == Z_RDMA_MSG_ACCEPT) && (msg->len > 0)) {
		zev.data_len = msg->len;
		zev.data = (void*)msg->data;
	}

	rep->ep.cb(&rep->ep, &zev);
	__enable_cq_events(rep);
}

static void _rdma_deliver_disconnected(struct z_rdma_ep *rep)
{
	struct zap_event zev = {
		.type = ZAP_EVENT_DISCONNECTED,
		.status = ZAP_ERR_OK,
	};
	rep->ep.cb(&rep->ep, &zev);
	__zap_put_ep(&rep->ep); /* Release the last reference */
}

static void
handle_disconnected(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id)
{
	DLOG_(rep, "RDMA: handle_disconnected %p, state '%s'\n",
			rep, zap_ep_state_str(rep->ep.state));
	pthread_mutex_lock(&rep->ep.lock);
	switch (rep->ep.state) {
	case ZAP_EP_CONNECTED:
		/* Peer close while connected */
		rdma_disconnect(rep->cm_id);
		rep->ep.state = ZAP_EP_PEER_CLOSE;
		break;
	case ZAP_EP_CLOSE:
		/*
		 * Peer has received the disconnected event
		 * and called rdma_disconnect(). This is expected.
		 */
		break;
	case ZAP_EP_ERROR:
		/* We closed and the peer has too. */
		break;
	case ZAP_EP_PEER_CLOSE:
		LOG_(rep, "RDMA: multiple disconnects on the same endpoint.\n");
		break;
	case ZAP_EP_LISTENING:
		break;
	default:
		LOG_(rep, "RDMA: unexpected disconnect in state %d.\n",
		     rep->ep.state);
		assert(0);
		break;
	}
	pthread_mutex_unlock(&rep->ep.lock);

	pthread_mutex_lock(&rep->credit_lock);
	__flush_io_q(rep);
	pthread_mutex_unlock(&rep->credit_lock);

	pthread_mutex_lock(&rep->ep.lock);
	if (!LIST_EMPTY(&rep->active_ctxt_list)) {
		rep->deferred_disconnected = 1;
		pthread_mutex_unlock(&rep->ep.lock);
	} else {
		pthread_mutex_unlock(&rep->ep.lock);
		__rdma_deliver_disconnected(rep);
	}
}

static void cma_event_handler(struct z_rdma_ep *rep,
			      struct rdma_cm_id *cma_id,
			      struct rdma_cm_event *event)
{
	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		handle_addr_resolved(rep, cma_id);
		break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		handle_route_resolved(rep, cma_id);
		break;

	case RDMA_CM_EVENT_CONNECT_REQUEST:
		handle_connect_request(rep, event);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		handle_established(rep, event);
		break;

	case RDMA_CM_EVENT_REJECTED:
		handle_rejected(rep, cma_id, event);
		break;

	case RDMA_CM_EVENT_DISCONNECTED:
		handle_disconnected(rep, cma_id);
		break;

	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		break;

	case RDMA_CM_EVENT_CONNECT_ERROR:
		handle_conn_error(rep, cma_id, ZAP_ERR_CONNECT);
		break;

	case RDMA_CM_EVENT_ADDR_ERROR:
		handle_conn_error(rep, cma_id, ZAP_ERR_ADDRESS);
		break;

	case RDMA_CM_EVENT_ROUTE_ERROR:
		handle_conn_error(rep, cma_id, ZAP_ERR_ROUTE);
		break;

	case RDMA_CM_EVENT_UNREACHABLE:
		handle_conn_error(rep, cma_id, ZAP_ERR_HOST_UNREACHABLE);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
	default:
		LOG_(rep, "RDMA: Unhandled event %s, ignoring\n",
		     rdma_event_str(event->event));
		break;
	}
}

static void handle_cm_event(struct z_rdma_ep *rep)
{
	int rc;
	struct rdma_cm_event *event;

	rc = rdma_get_cm_event(rep->cm_channel, &event);
	if (rc)
		return;

	struct rdma_cm_id *cm_id = event->id;
	/*
	 * Many connection oriented events are delivered on the
	 * cm_channel of the listener cm_id. Use the endpoint from the
	 * cm_id context, not the one from the cm_channel.
	 */
	rep = (struct z_rdma_ep *)cm_id->context;
	__zap_get_ep(&rep->ep);
	cma_event_handler(rep, cm_id, event);
	rdma_ack_cm_event(event);
	__zap_put_ep(&rep->ep);
}

/*
 * Connection Manager Thread - event thread that processes CM events.
 */
static void *cm_thread_proc(void *arg)
{
	int cm_fd = (int)(unsigned long)arg;
	struct epoll_event cm_events[16];
	struct z_rdma_ep *rep;
	int ret, i;
	int rc;
	sigset_t sigset;

	sigfillset(&sigset);
	rc = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	if (rc) {
		fprintf(stderr, "zap_rdma: ERROR: pthread_sigmask errno: %d\n",
				errno);
		assert(rc == 0);
	}

	while (1) {
		ret = epoll_wait(cm_fd, cm_events, 16, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		for (i = 0; i < ret; i++) {
			rep = cm_events[i].data.ptr;
			if (!rep || !rep->cm_channel) {
				/* This shouldn't happen */
				assert(0);
			}
			handle_cm_event(rep);
		}
	}
	return NULL;
}

static zap_err_t
z_rdma_listen(zap_ep_t ep, struct sockaddr *sin, socklen_t sa_len)
{
	zap_err_t zerr;
	int rc;
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct epoll_event cm_event;

	zerr = zap_ep_change_state(&rep->ep, ZAP_EP_INIT, ZAP_EP_LISTENING);
	if (zerr) {
		LOG__(ep,"%s: %s failed. zerr %d\n", __FUNCTION__, "zap_ep_change_state", (int)zerr);
		goto out;
	}

	/* Create the event CM event channel */
	zerr = ZAP_ERR_RESOURCE;
	rep->cm_channel = rdma_create_event_channel();
	if (!rep->cm_channel) {
		LOG__(ep,"%s: %s failed. zerr %d\n", __FUNCTION__, "rdma_create_event_channel", (int)zerr);
		goto err_0;
	}

	/* Create the listening CM ID */
	rc = rdma_create_id(rep->cm_channel, &rep->cm_id, rep, RDMA_PS_TCP);
	if (rc) {
		LOG__(ep,"%s: %s failed. zerr %d, rc %d\n", __FUNCTION__, "rdma_create_id", (int)zerr, rc);
		goto err_1;
	}

	/* Bind the provided address to the CM Id */
	zerr = ZAP_ERR_BUSY;
	rc = rdma_bind_addr(rep->cm_id, sin);
	if (rc) {
		LOG__(ep,"%s: %s failed. zerr %d rc %d\n", __FUNCTION__, "rdma_bind_addr", (int)zerr, rc);
		goto err_2;
	}

	cm_event.events = EPOLLIN | EPOLLOUT;
	cm_event.data.ptr = rep;
	zerr = ZAP_ERR_RESOURCE;
	rc = epoll_ctl(cm_fd, EPOLL_CTL_ADD, rep->cm_channel->fd, &cm_event);
	if (rc) {
		LOG__(ep,"%s: %s failed. zerr %d rc %d\n", __FUNCTION__, "epoll_ctl", (int)zerr, rc);
		goto err_3;
	}

	/*
	 * Asynchronous listen. Connection requests handled in
	 * cm_thread_proc
	 */
	rc = rdma_listen(rep->cm_id, 3);
	if (rc) {
		LOG__(ep,"%s: %s failed. zerr %d rc %d\n", __FUNCTION__, "rdma_listen", (int)zerr, rc);
		goto err_3;
	}

	return ZAP_ERR_OK;

 err_3:
	rc = epoll_ctl(cm_fd, EPOLL_CTL_DEL,
			rep->cm_channel->fd, NULL);
 err_2:
	rdma_destroy_id(rep->cm_id);
	rep->cm_id = NULL;
 err_1:
	rdma_destroy_event_channel(rep->cm_channel);
	rep->cm_channel = NULL;
 err_0:
	zap_ep_change_state(&rep->ep, ZAP_EP_LISTENING, ZAP_EP_ERROR);
 out:
	return zerr;
}

/*
 * Best effort credit update. The receipt of this message kicks the i/o
 * on the deferred credit queue in the peer. Called with the credit_lock held.
 */
static int send_credit_update(struct z_rdma_ep *rep)
{
	struct z_rdma_message_hdr *req;
	struct z_rdma_buffer *rbuf;
	struct z_rdma_context *ctxt;
	struct ibv_send_wr *bad_wr;
	int rc;

	rbuf = __rdma_buffer_alloc(rep, RQ_BUF_SZ, IBV_ACCESS_LOCAL_WRITE);
	if (!rbuf)
		return ENOMEM;

	req = (struct z_rdma_message_hdr *)rbuf->data;
	req->credits = htons(rep->lcl_rq_credits);
	req->msg_type = htons(Z_RDMA_MSG_CREDIT_UPDATE);
	rep->lcl_rq_credits = 0;
	rbuf->data_len = sizeof(req);
	pthread_mutex_unlock(&rep->credit_lock);

	pthread_mutex_lock(&rep->ep.lock);
	ctxt = __rdma_context_alloc(rep, NULL, IBV_WC_SEND, rbuf);
	if (!ctxt) {
		__rdma_buffer_free(rbuf);
		pthread_mutex_unlock(&rep->ep.lock);
		rc = ENOMEM;
		goto out;
	}

	ctxt->sge.addr = (uint64_t)(unsigned long)rbuf->data;
	ctxt->sge.length = sizeof(*req);
	ctxt->sge.lkey = rbuf->mr->lkey;

	memset(&ctxt->wr, 0, sizeof(ctxt->wr));
	ctxt->wr.sg_list = &ctxt->sge;
	ctxt->wr.num_sge = 1;
	ctxt->wr.opcode = IBV_WR_SEND;
	ctxt->wr.send_flags = IBV_SEND_SIGNALED;

	RDMA_SET_CONTEXT(&ctxt->wr, ctxt);

	if (ibv_post_send(rep->qp, &ctxt->wr, &bad_wr)) {
		LOG_(rep,
		     "%s: sq_credits %d lcl_rq_credits %d "
		     "rem_rq_credits %d.\n",
		     __func__, rep->sq_credits, rep->lcl_rq_credits,
		     rep->rem_rq_credits);
		__rdma_context_free(ctxt);
		__rdma_buffer_free(rbuf);
		rc = ENOSPC;
		pthread_mutex_unlock(&rep->ep.lock);
		goto out;
	}
	pthread_mutex_unlock(&rep->ep.lock);
	rc = 0;
out:
	pthread_mutex_lock(&rep->credit_lock);
	return rc;
}

static zap_err_t z_get_name(zap_ep_t ep, struct sockaddr *local_sa,
			    struct sockaddr *remote_sa, socklen_t *sa_len)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;

	memcpy(remote_sa, rdma_get_local_addr(rep->cm_id), sizeof(struct sockaddr_in));
	memcpy(local_sa, rdma_get_peer_addr(rep->cm_id), sizeof(struct sockaddr_in));
	*sa_len = sizeof(struct sockaddr_in);
	return ZAP_ERR_OK;
}

static zap_err_t z_rdma_send(zap_ep_t ep, char *buf, size_t len)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct z_rdma_message_hdr *hdr;
	struct z_rdma_buffer *rbuf;
	zap_err_t rc;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc)
		goto out;

	if (len > ep->z->max_msg) {
		rc = ZAP_ERR_NO_SPACE;
		goto out;
	}

	rbuf = __rdma_buffer_alloc(rep, RQ_BUF_SZ, IBV_ACCESS_LOCAL_WRITE);
	if (!rbuf) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}
	pthread_mutex_unlock(&rep->ep.lock);

	hdr = (struct z_rdma_message_hdr *)rbuf->data;
	hdr->msg_type = htons(Z_RDMA_MSG_SEND);

	/* copy the data, leaving room for the rdma header */
	memcpy(rbuf->data+sizeof(struct z_rdma_message_hdr), buf, len);
	rbuf->data_len = len + sizeof(struct z_rdma_message_hdr);

	rc = __rdma_post_send(rep, rbuf);
	if (rc)
		__rdma_buffer_free(rbuf);

	return rc;
 out:
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;
}

static zap_err_t z_rdma_share(zap_ep_t ep, zap_map_t map,
				const char *msg, size_t msg_len)
{
	struct z_rdma_map *rmap = (struct z_rdma_map *)map;
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct z_rdma_share_msg *sm;
	struct z_rdma_buffer *rbuf;
	zap_err_t rc;
	size_t sz = sizeof(*sm) + msg_len;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc) {
		pthread_mutex_unlock(&rep->ep.lock);
		return rc;
	}

	if (sz > RQ_BUF_SZ) {
		/* msg too big! */
		pthread_mutex_unlock(&rep->ep.lock);
		return ZAP_ERR_NO_SPACE;
	}

	rbuf = __rdma_buffer_alloc(rep, RQ_BUF_SZ, IBV_ACCESS_LOCAL_WRITE);
	if (!rbuf) {
		pthread_mutex_unlock(&rep->ep.lock);
		return ZAP_ERR_RESOURCE;
	}
	pthread_mutex_unlock(&rep->ep.lock);

	sm = (struct z_rdma_share_msg *)rbuf->data;
	sm->hdr.msg_type = htons(Z_RDMA_MSG_RENDEZVOUS);
	sm->rkey = rmap->mr->rkey;
	sm->va = (unsigned long)rmap->map.addr;
	sm->len = htonl(rmap->map.len);
	sm->acc = htonl(rmap->map.acc);
	if (msg_len)
		memcpy(sm->msg, msg, msg_len);

	rbuf->data_len = sz;

	rc = __rdma_post_send(rep, rbuf);
	if (rc)
		__rdma_buffer_free(rbuf);
	return rc;
}

/* Map buffer */
static zap_err_t
z_rdma_map(zap_ep_t ep, zap_map_t *pm,
	   void *buf, size_t len, zap_access_t acc)
{
	zap_mem_info_t mem_info = ep->z->mem_info_fn();
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct z_rdma_map *map;
	int acc_flags;

	map = calloc(1, sizeof(*map));
	if (!map)
		goto err_0;

	acc_flags = (ZAP_ACCESS_WRITE & acc ? IBV_ACCESS_REMOTE_WRITE : 0);
	acc_flags |= (ZAP_ACCESS_READ & acc ? IBV_ACCESS_REMOTE_READ : 0);
	acc_flags |= IBV_ACCESS_LOCAL_WRITE;

	if (rep->cm_id->verbs->device->transport_type == IBV_TRANSPORT_IWARP) {
		/*
		 * iwarp requires the sink map of the read operation
		 * to be remotely writable. In the current zap API
		 * version 1.3.0.0, the zap transport library doesn't
		 * know the purpose of the map. Thus, we assume that
		 * all maps can be a sink map for a read operation.
		 */
		acc_flags |= IBV_ACCESS_REMOTE_WRITE;
	}

	if (mem_info)
		DLOG_(rep, "%s meminfo %p.\n", __func__, mem_info);
	map->mr = ibv_reg_mr(rep->pd, buf, len, acc_flags);
	if (!map->mr)
		goto err_1;

	*pm = &map->map;
	return ZAP_ERR_OK;
 err_1:
	free(map);
 err_0:
	return ZAP_ERR_RESOURCE;
}

static zap_err_t z_rdma_unmap(zap_ep_t ep, zap_map_t map)
{
	struct z_rdma_map *zm = (struct z_rdma_map *)map;
	if (zm->mr)
		ibv_dereg_mr(zm->mr);
	free(zm);
	return ZAP_ERR_OK;
}

static zap_err_t z_rdma_write(zap_ep_t ep,
			      zap_map_t src_map, char *src,
			      zap_map_t dst_map, char *dst,
			      size_t sz,
			      void *context)
{
	zap_err_t rc;
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct z_rdma_map *rmap = (struct z_rdma_map *)dst_map;
	struct z_rdma_map *lmap = (struct z_rdma_map *)src_map;
	struct ibv_send_wr *bad_wr;
	struct z_rdma_context *ctxt;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc)
		goto out;

	ctxt = __rdma_context_alloc(rep, context, IBV_WC_RDMA_WRITE, NULL);
	if (!ctxt) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}

	ctxt->sge.addr = (unsigned long)src;
	ctxt->sge.length = sz;
	ctxt->sge.lkey = lmap->mr->lkey;

	memset(&ctxt->wr, 0, sizeof(ctxt->wr));
	ctxt->wr.sg_list = &ctxt->sge;
	ctxt->wr.num_sge = 1;
	ctxt->wr.opcode = IBV_WR_RDMA_WRITE;
	ctxt->wr.send_flags = IBV_SEND_SIGNALED;
	ctxt->wr.wr.rdma.remote_addr = (unsigned long)dst;
	ctxt->wr.wr.rdma.rkey = rmap->rkey;

	RDMA_SET_CONTEXT(&ctxt->wr, ctxt);
	pthread_mutex_lock(&rep->credit_lock);
	if (!get_credits(rep, 1)) {
		rc = post_send(rep, ctxt, &bad_wr, 1);
		if (rc) {
			LOG_(rep, "RDMA: post_send failed: code %d\n", errno);
			if (errno)
				rc = errno;
		}
	} else
		rc = queue_io(rep, ctxt);
	pthread_mutex_unlock(&rep->credit_lock);
	if (rc)
		__rdma_context_free(ctxt);
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;
out:
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;
}

static zap_err_t z_rdma_read(zap_ep_t ep,
			     zap_map_t src_map, char *src,
			     zap_map_t dst_map, char *dst,
			     size_t sz,
			     void *context)
{
	zap_err_t rc;
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct z_rdma_map *lmap = (struct z_rdma_map *)dst_map;
	struct z_rdma_map *rmap = (struct z_rdma_map *)src_map;
	struct ibv_send_wr *bad_wr;
	struct z_rdma_context *ctxt;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc) {
		pthread_mutex_unlock(&rep->ep.lock);
		return rc;
	}

	ctxt = __rdma_context_alloc(rep, context, IBV_WC_RDMA_READ, NULL);
	if (!ctxt) {
		pthread_mutex_unlock(&rep->ep.lock);
		return ZAP_ERR_RESOURCE;
	}

	ctxt->sge.addr = (unsigned long)dst;
	ctxt->sge.length = sz;
	ctxt->sge.lkey = lmap->mr->lkey;
	memset(&ctxt->wr, 0, sizeof(ctxt->wr));
	ctxt->wr.sg_list = &ctxt->sge;
	ctxt->wr.num_sge = 1;
	ctxt->wr.opcode = IBV_WR_RDMA_READ;
	ctxt->wr.send_flags = IBV_SEND_SIGNALED;
	ctxt->wr.wr.rdma.remote_addr = (unsigned long)src;
	ctxt->wr.wr.rdma.rkey = rmap->rkey;

	RDMA_SET_CONTEXT(&ctxt->wr, ctxt);
	pthread_mutex_lock(&rep->credit_lock);
	if (!get_credits(rep, 1)) {
		rc = post_send(rep, ctxt, &bad_wr, 1);
		if (rc) {
			LOG_(rep, "RDMA: post_send failed: code %d\n", errno);
			if (errno)
				rc = errno;
		}
	} else
		rc = queue_io(rep, ctxt);
	pthread_mutex_unlock(&rep->credit_lock);

	if (rc)
		__rdma_context_free(ctxt);
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;
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
	if (!cm_fd)
		goto err_1;

	/*
	 * Create the CQ event thread that will wait for events on the
	 * CQ channel
	 */
	rc = pthread_create(&cq_thread, NULL, cq_thread_proc,
			    (void *)(unsigned long)cq_fd);
	if (rc)
		goto err_2;

	/*
	 * Create the CM event thread that will wait for events on
	 * the CM channel
	 */
	rc = pthread_create(&cm_thread, NULL, cm_thread_proc,
			    (void *)(unsigned long)cm_fd);
	if (rc)
		goto err_3;

	init_complete = 1;
	// atexit(z_rdma_cleanup);
	return 0;

 err_3:
	pthread_cancel(cq_thread);
 err_2:
	close(cm_fd);
 err_1:
	close(cq_fd);
 err_0:
	return 1;
}

zap_err_t zap_transport_get(zap_t *pz, zap_log_fn_t log_fn,
			    zap_mem_info_fn_t mem_info_fn)
{
	zap_t z;
	if (!init_complete) {
		if (init_once()) {
			errno = ENOMEM;
			goto err_0;
		}
	}

	z = calloc(1, sizeof (*z));
	if (!z) {
		errno = ENOMEM;
		goto err_0;
	}

	z->max_msg = RQ_BUF_SZ - sizeof(struct z_rdma_share_msg);
	z->new = z_rdma_new;
	z->destroy = z_rdma_destroy;
	z->connect = z_rdma_connect;
	z->accept = z_rdma_accept;
	z->reject = z_rdma_reject;
	z->listen = z_rdma_listen;
	z->close = z_rdma_close;
	z->send = z_rdma_send;
	z->read = z_rdma_read;
	z->write = z_rdma_write;
	z->map = z_rdma_map;
	z->unmap = z_rdma_unmap;
	z->share = z_rdma_share;
	z->get_name = z_get_name;

	*pz = z;
	return ZAP_ERR_OK;

 err_0:
	return ZAP_ERR_RESOURCE;
}
