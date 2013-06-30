/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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

#include "zap.h"
#include "zap_rdma.h"

#define LOG__(ep, ...) { if (ep->z && ep->z->log_fn) ep->z->log_fn(__VA_ARGS__); }
#define LOG_(rep, ...) \
{ \
	if (rep && rep->ep.z && rep->ep.z->log_fn) \
		rep->ep.z->log_fn(__VA_ARGS__); \
}

LIST_HEAD(ep_list, z_rdma_ep) ep_list;

static int rdma_fill_rq(struct z_rdma_ep *ep);
static void *cm_thread_proc(void *arg);
static void *cq_thread_proc(void *arg);
static int cq_event_handler(struct ibv_cq *cq, int count);
static void rdma_context_free(struct rdma_context *ctxt);
static void rdma_buffer_free(struct rdma_buffer *rbuf);
static int send_credit_update(struct z_rdma_ep *ep);

static struct rdma_context *rdma_get_context(struct ibv_wc *wc)
{
	return (struct rdma_context *)(unsigned long)wc->wr_id;
}

#define RDMA_SET_CONTEXT(__wr, __ctxt) \
	(__wr)->wr_id = (uint64_t)(unsigned long)(__ctxt);

static int cq_fd;
static int cm_fd;
#define CQ_EVENT_LEN 16

static void rdma_teardown_conn(struct z_rdma_ep *ep)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;

	if (rep->cm_id && rep->ep.state == ZAP_EP_CONNECTED)
		assert(0);

	rep->ep.state = ZAP_EP_INIT;

	/* Destroy the QP */
	if (rep->qp)
		rdma_destroy_qp(rep->cm_id);

	/* Destroy the RQ CQ */
	if (rep->rq_cq) {
		if (ibv_destroy_cq(rep->rq_cq))
			LOG_(rep, "RDMA: Error %d : ibv_destroy_cq failed\n",
			     errno);
	}
	/* Destroy the SQ CQ */
	if (rep->sq_cq) {
		if (ibv_destroy_cq(rep->sq_cq))
			LOG_(rep, "RDMA: Error %d : ibv_destroy_cq failed\n",
			     errno);
	}
	/* Destroy the PD */
	if (rep->pd) {
		if (ibv_dealloc_pd(rep->pd))
			LOG_(rep, "RDMA: Error %d : ibv_dealloc_pd failed\n",
			     errno);
	}
	/* Destroy the CM id */
	if (rep->cm_id) {
		if (rdma_destroy_id(rep->cm_id))
			LOG_(rep, "RDMA: Error %d : rdma_destroy_id failed\n",
			     errno);
	}
	if (rep->cm_channel)
		rdma_destroy_event_channel(rep->cm_channel);

	if (rep->cq_channel) {
		if (ibv_destroy_comp_channel(rep->cq_channel))
			LOG_(rep, "RDMA: Error %d : "
			     "ibv_destroy_comp__channel failed\n", errno);
	}
	rep->cm_id = NULL;
	rep->pd = NULL;
	rep->rq_cq = NULL;
	rep->sq_cq = NULL;
	rep->qp = NULL;
	rep->cq_channel = NULL;
	rep->cm_channel = NULL;
	rep->rem_rq_credits = RQ_DEPTH;
	rep->sq_credits = SQ_DEPTH;
	rep->lcl_rq_credits = 0;
	while (!TAILQ_EMPTY(&rep->io_q)) {
		struct rdma_context *ctxt = TAILQ_FIRST(&rep->io_q);
		TAILQ_REMOVE(&rep->io_q, ctxt, pending_link);
		if (ctxt->rb)
			rdma_buffer_free(ctxt->rb);
		rdma_context_free(ctxt);
	}
}

static struct rdma_buffer *
rdma_buffer_alloc(struct z_rdma_ep *rep, size_t len,
		  enum ibv_access_flags f)
{
	struct rdma_buffer *rbuf;

	rbuf = calloc(1, sizeof *rbuf + len);
	if (!rbuf)
		return NULL;
	rbuf->data = (char *)(rbuf+1);
	rbuf->data_len = len;
	rbuf->mr = ibv_reg_mr(rep->pd, rbuf->data, len, f);
	if (!rbuf->mr) {
		free(rbuf);
		LOG_(rep, "RDMA: recv_buf reg_mr failed: error %d\n", errno);
		return NULL;
	}
	return rbuf;
}

static void rdma_buffer_free(struct rdma_buffer *rbuf)
{
	(void)ibv_dereg_mr(rbuf->mr);
	free(rbuf);
}

static struct rdma_context *
rdma_context_alloc(struct z_rdma_ep *rep,
		   void *usr_context, enum ibv_wc_opcode op,
		   struct rdma_buffer *rbuf)
{
	struct rdma_context *ctxt = calloc(1, sizeof *ctxt);
	if (!ctxt)
		return NULL;

	ctxt->usr_context = usr_context;
	ctxt->op = op;
	ctxt->rb = rbuf;
	ctxt->ep = &rep->ep;

	zap_get_ep(&rep->ep);
	return ctxt;
}

/*
 * Enqueue a context to the pending I/O queue.
 *
 * Must be called with the credit_lock held.
 */
static int queue_io(struct z_rdma_ep *rep, struct rdma_context *ctxt)
{
	TAILQ_INSERT_TAIL(&rep->io_q, ctxt, pending_link);
	return 0;
}

/*
 * Called by submit_pending and post_send.
 *
 * Must be called with the credit_lock held.
 */
static zap_err_t
post_send(struct z_rdma_ep *rep,
	  struct rdma_context *ctxt, struct ibv_send_wr **bad_wr,
	  int is_rdma)
{
	int rc;
	struct rdma_message_hdr *msg;
	if (rep->ep.state != ZAP_EP_CONNECTED) {
		LOG_(rep, "%s: not connected state %d\n",
		     __func__, rep->ep.state);
		return ZAP_ERR_NOT_CONNECTED;
	}
	/*
	 * RDMA_READ/WRITE do not have a message header.
	 */
	if (!is_rdma) {
		msg = (struct rdma_message_hdr *)(unsigned long)
			ctxt->wr.sg_list[0].addr;
		msg->credits = htons(rep->lcl_rq_credits);
		rep->lcl_rq_credits = 0;
	}

	rc = ibv_post_send(rep->qp, &ctxt->wr, bad_wr);
	if (rc) {
		LOG_(rep, "%s: error %d posting send is_rdma %d sq_credits %d "
		     "lcl_rq_credits %d rem_rq_credits %d.\n", __func__,
		     rc, is_rdma, rep->sq_credits, rep->lcl_rq_credits,
		     rep->rem_rq_credits);
		zap_put_ep(&rep->ep);
	}
	return rc;
}

char *cmd_str[] = {
	"LDMS_CMD_DIR",
	"LDMS_CMD_DIR_CANCEL",
	"LDMS_CMD_LOOKUP",
	"LDMS_CMD_UPDATE",
	"LDMS_CMD_REQ_NOTIFY",
	"LDMS_CMD_CANCEL_NOTIFY",
	"LDMS_CMD_REPLY",
	"LDMS_CMD_DIR_REPLY",
	"LDMS_CMD_LOOKUP_REPLY",
	"LDMS_CMD_REQ_NOTIFY_REPLY",
};

char * op_str[] = {
	"IBV_WC_SEND",
	"IBV_WC_RDMA_WRITE",
	"IBV_WC_RDMA_READ",
	"IBV_WC_COMP_SWAP",
	"IBV_WC_FETCH_ADD",
	"IBV_WC_BIND_MW"
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
	struct rdma_context *ctxt;
	int is_rdma;

	if (rep->ep.state != ZAP_EP_CONNECTED)
		return;

	pthread_mutex_lock(&rep->credit_lock);
	while (!TAILQ_EMPTY(&rep->io_q)) {
		ctxt = TAILQ_FIRST(&rep->io_q);
		is_rdma = (ctxt->op != IBV_WC_SEND);
		if (_get_credits(rep, is_rdma))
			goto out;

		TAILQ_REMOVE(&rep->io_q, ctxt, pending_link);

		if (post_send(rep, ctxt, &badwr, is_rdma))
			LOG_(rep, "Error posting queued I/O.\n");
	}
 out:
	pthread_mutex_unlock(&rep->credit_lock);
}

static void rdma_context_free(struct rdma_context *ctxt)
{
	zap_put_ep(ctxt->ep);
	free(ctxt);
}

static int rdma_post_send(struct z_rdma_ep *rep, struct rdma_buffer *rbuf)
{
	int rc;
	struct ibv_send_wr *bad_wr;
	struct rdma_context *ctxt =
		rdma_context_alloc(rep, NULL, IBV_WC_SEND, rbuf);
	if (!ctxt) {
		errno = ENOMEM;
		return ENOMEM;
	}
	if (rep->ep.state != ZAP_EP_CONNECTED)
		goto err_0;

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
	return rc;
 err_0:
	free(ctxt);
	return ENOTCONN;
}

static zap_err_t z_rdma_close(zap_ep_t ep)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	if (rep->cm_id && rep->ep.state == ZAP_EP_CONNECTED)
		rdma_disconnect(rep->cm_id);
	return ZAP_ERR_OK;
}

static zap_err_t z_rdma_connect(zap_ep_t ep,
				struct sockaddr *sin, socklen_t sa_len)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct epoll_event cm_event;
	zap_err_t zerr = ZAP_ERR_RESOURCE;
	int rc;

	/* Create the event CM event channel */
	rep->cm_channel = rdma_create_event_channel();
	if (!rep->cm_channel)
		goto err_0;

	/* Create the connecting CM ID */
	rc = rdma_create_id(rep->cm_channel, &rep->cm_id, rep, RDMA_PS_TCP);
	if (rc)
		goto err_1;

	cm_event.events = EPOLLIN | EPOLLOUT;
	cm_event.data.ptr = rep;
	zap_get_ep(&rep->ep);
	rc = epoll_ctl(cm_fd, EPOLL_CTL_ADD, rep->cm_channel->fd, &cm_event);
	if (rc)
		goto err_2;

	rep->ep.state = ZAP_EP_CONNECTING;

	/* Bind the provided address to the CM Id */
	rc = rdma_resolve_addr(rep->cm_id, NULL, sin, 2000);
	if (rc) {
		zerr = ZAP_ERR_ADDRESS;
		goto err_3;
	}

	return 0;

 err_3:
	/*
	 * These are all syncrhonous clean-ups. IB does not support
	 * re-connecting on the same cm_id, so blow everything away
	 * and start over next time.
	 */
	(void)epoll_ctl(cm_fd, EPOLL_CTL_DEL, rep->cm_channel->fd, NULL);
 err_2:
	zap_put_ep(&rep->ep);
 err_1:
	rdma_teardown_conn(rep);
 err_0:
	return zerr;
}

static int rdma_post_recv(struct z_rdma_ep *rep, struct rdma_buffer *rb)
{
	struct ibv_recv_wr wr;
	struct ibv_sge sge;
	struct ibv_recv_wr *bad_wr;
	struct rdma_context *ctxt;

	if (rep->ep.state > ZAP_EP_CONNECTED)
		return ZAP_ERR_NOT_CONNECTED;

	ctxt = rdma_context_alloc(rep, NULL, -1, rb);
	if (!ctxt)
		return ZAP_ERR_RESOURCE;

	sge.addr = (uint64_t) (unsigned long) rb->data;
	sge.length = rb->data_len;
	sge.lkey = rb->mr->lkey;
	wr.sg_list = &sge;
	wr.next = NULL;
	wr.num_sge = 1;

	RDMA_SET_CONTEXT(&wr, ctxt);
	return ibv_post_recv(rep->qp, &wr, &bad_wr);
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
			    struct rdma_buffer *rb)
{
	if (rb)
		rdma_buffer_free(rb);
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
			struct rdma_message_hdr *msg, size_t len)
{
	struct zap_event zev;

	memset(&zev, 0, sizeof zev);
	zev.type = ZAP_EVENT_RECV_COMPLETE;
	zev.status = ZAP_ERR_OK;
	zev.data = msg + 1;
	zev.data_len = len;
	rep->ep.cb(&rep->ep, &zev);
}

static void handle_rendezvous(struct z_rdma_ep *rep,
			      struct rdma_message_hdr *msg, size_t len)
{
	struct zap_event zev;
	struct rdma_share_msg *sh = (struct rdma_share_msg *)msg;
	struct z_rdma_map *map;

	map = calloc(1, sizeof(*map));
	if (!map)
		return;

	map->map.type = ZAP_MAP_REMOTE;
	map->map.ep = &rep->ep;
	map->map.addr = (void *)(unsigned long)sh->va;
	map->map.len = ntohl(sh->len);
	map->map.acc = ntohl(sh->acc);
	map->rkey = sh->rkey;

	pthread_mutex_lock(&rep->ep.lock);
	LIST_INSERT_HEAD(&rep->ep.map_list, &map->map, link);
	pthread_mutex_unlock(&rep->ep.lock);

	memset(&zev, 0, sizeof zev);
	zev.type = ZAP_EVENT_RENDEZVOUS;
	zev.status = ZAP_ERR_OK;
	zev.map = &map->map;
	rep->ep.cb(&rep->ep, &zev);
}

static void process_recv_wc(struct z_rdma_ep *rep, struct ibv_wc *wc,
			    struct rdma_buffer *rb)
{
	struct rdma_message_hdr *msg = (struct rdma_message_hdr *)rb->data;
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
	case RDMA_SEND:
		handle_recv(rep, msg, wc->byte_len);
		break;

	case RDMA_RENDEZVOUS:
		handle_rendezvous(rep, msg, wc->byte_len);
		break;

	case RDMA_CREDIT_UPDATE:
		break;
	}

	ret = rdma_post_recv(rep, rb);
	if (ret) {
		rdma_buffer_free(rb);
	}

	/* Credit updates are not counted */
	if (msg_type == RDMA_CREDIT_UPDATE)
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
}

static int rdma_fill_rq(struct z_rdma_ep *rep)
{
	int i;

	for (i = 0; i < RQ_DEPTH+2; i++) {
		struct rdma_buffer *rbuf =
			rdma_buffer_alloc(rep, RQ_BUF_SZ,
					  IBV_ACCESS_LOCAL_WRITE);
		if (rbuf) {
			int rc = rdma_post_recv(rep, rbuf);
			if (rc)
				return rc;
		} else
			return ENOMEM;
	}
	return 0;
}

static zap_err_t z_rdma_reject(zap_ep_t ep)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	rdma_reject(rep->cm_id, NULL, 0);
	return ZAP_ERR_OK;
}

static zap_err_t z_rdma_accept(zap_ep_t ep, zap_cb_fn_t cb)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct epoll_event cq_event;
	struct rdma_conn_param conn_param;
	struct ibv_qp_init_attr qp_attr;
	int ret;

	/* Replace the callback with the one provided by the caller */
	rep->ep.cb = cb;

	/* Allocate PD */
	rep->pd = ibv_alloc_pd(rep->cm_id->verbs);
	if (!rep->pd) {
		LOG_(rep, "RDMA: ibv_alloc_pd failed\n");
		goto out_0;
	}

	rep->cq_channel = ibv_create_comp_channel(rep->cm_id->verbs);
	if (!rep->cq_channel) {
		LOG_(rep, "RDMA: ibv_create_comp_channel failed\n");
		goto out_0;
	}

	/* Create a new RQ CQ. */
	rep->rq_cq = ibv_create_cq(rep->cm_id->verbs,
				 RQ_DEPTH + 2,
				 rep, rep->cq_channel, 0);
	if (!rep->rq_cq) {
		LOG_(rep, "RDMA: ibv_create_cq failed\n");
		goto out_0;
	}

	/* Create a new SQ CQ. */
	rep->sq_cq = ibv_create_cq(rep->cm_id->verbs,
				   SQ_DEPTH + 2,
				   rep, rep->cq_channel, 0);
	if (!rep->sq_cq) {
		LOG_(rep, "RDMA: ibv_create_cq failed\n");
		goto out_0;
	}

	ret = ibv_req_notify_cq(rep->sq_cq, 0);
	if (ret) {
		LOG_(rep, "RDMA: ibv_create_cq failed\n");
		goto out_0;
	}

	ret = ibv_req_notify_cq(rep->rq_cq, 0);
	if (ret) {
		LOG_(rep, "RDMA: ibv_create_cq failed\n");
		goto out_0;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.cap.max_send_wr = SQ_DEPTH+2; /* +2 for the credit update */
	qp_attr.cap.max_recv_wr = RQ_DEPTH+2; /* +2 for the credit update */
	qp_attr.cap.max_recv_sge = SQ_SGE;
	qp_attr.cap.max_send_sge = RQ_SGE;
	qp_attr.qp_type = IBV_QPT_RC;
	qp_attr.send_cq = rep->sq_cq;
	qp_attr.recv_cq = rep->rq_cq;

	ret = rdma_create_qp(rep->cm_id, rep->pd, &qp_attr);
	rep->qp = rep->cm_id->qp;
	if (ret) {
		LOG_(rep, "RDMA: rdma_create_qp failed\n");
		goto out_0;
	}

	/* Post receive buffers to the RQ */
	if (rdma_fill_rq(rep))
		goto out_0;

	cq_event.events = EPOLLIN | EPOLLOUT;
	cq_event.data.ptr = rep;
	zap_get_ep(&rep->ep);
	ret = epoll_ctl(cq_fd, EPOLL_CTL_ADD, rep->cq_channel->fd, &cq_event);
	if (ret) {
		LOG_(rep, "RDMA: Could not add passive side CQ fd (%d)"
		     "to event queue.\n",
		     rep->cq_channel->fd);
		goto out_1;
	}

	/* Accept the connection */
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 4;
	conn_param.initiator_depth = 4;
	ret = rdma_accept(rep->cm_id, &conn_param);
	if (ret)
		goto out_1;

	return ZAP_ERR_OK;

 out_1:
       zap_put_ep(&rep->ep);
 out_0:
       zap_put_ep(&rep->ep);
       return ZAP_ERR_RESOURCE;
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
	int ret = 0;

	while (count-- && ((ret = ibv_poll_cq(cq, 1, &wc)) == 1)) {
		struct rdma_context *ctxt = rdma_get_context(&wc);
		zap_ep_t ep = ctxt->ep;
		ret++;
		if (wc.status) {
			if (wc.status != IBV_WC_WR_FLUSH_ERR) {
				ep->state = ZAP_EP_ERROR;

				LOG__(ep, "RDMA: WR op '%s' failed with status '%s'\n",
				      (ctxt->op < 0 ? "RECV" : op_str[ctxt->op]),
				      err_msg[wc.status]);
				LOG__(ep, "    addr %p len %d lkey %p.\n",
				      ctxt->wr.sg_list[0].addr,
				      ctxt->wr.sg_list[0].length,
				      ctxt->wr.sg_list[0].lkey);
			}
			if (ctxt) {
				if (ctxt->rb)
					rdma_buffer_free(ctxt->rb);
				rdma_context_free(ctxt);
			}
			continue;
		}
		struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
		switch (wc.opcode) {
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
			process_recv_wc(rep, &wc, ctxt->rb);
			break;

		default:
			LOG_(rep,"RDMA: Invalid completion\n");
		}
		rdma_context_free(ctxt);
	}
	return ret;
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

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	while (1) {
		int fd_count = epoll_wait(cq_fd, cq_events, 16, -1);
		if (fd_count < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		for (i = 0; i < fd_count; i++) {
			rep = cq_events[i].data.ptr;

			/* Get the next event ... this will block */
			ret = ibv_get_cq_event(rep->cq_channel, &ev_cq, &ev_ctx);
			if (ret) {
				LOG_(rep, "cq_channel is %d at %d.\n",
				     rep->cq_channel->fd, __LINE__);
				LOG_(rep, "RDMA: Error %d at %s:%d\n",
					     errno, __func__, __LINE__);
				goto skip;
			}

			/* Re-arm the CQ */
			ret = ibv_req_notify_cq(ev_cq, 0);
			if (ret) {
				LOG_(rep, "RDMA: Error %d at %s:%d\n",
				     errno, __func__, __LINE__);
				goto skip;
			}

			/*
			 * Process the CQs. Speculatively reap SQ WQE
			 * in case they are required to respond to a
			 * request sitting in the RQ. Reap RQ WQE one
			 * at a time before revisiting the SQ again to
			 * reclaim space used by a previous response.
			 */
			do {
				ret = cq_event_handler(rep->sq_cq, SQ_DEPTH+2);
				if (ev_cq != rep->sq_cq)
					ret += cq_event_handler(ev_cq, 1);
			} while (ret);

			/* Ack the event */
			ibv_ack_cq_events(ev_cq, 1);
			if (ret) {
				LOG_(rep, "RDMA: Error %d at %s:%d\n",
				     errno, __func__, __LINE__);
			}
		skip:
			submit_pending(rep);
		}
	}
	return NULL;
}

static zap_err_t z_rdma_new(zap_t z, zap_ep_t *pep, zap_cb_fn_t cb)
{
	struct z_rdma_ep *rep;

	rep = calloc(1, sizeof *rep);
	if (!rep)
		return ZAP_ERR_RESOURCE;

	rep->rem_rq_credits = RQ_DEPTH;
	rep->lcl_rq_credits = 0;
	rep->sq_credits = SQ_DEPTH;

	TAILQ_INIT(&rep->io_q);
	pthread_mutex_init(&rep->credit_lock, NULL);
	*pep = &rep->ep;
	return ZAP_ERR_OK;
}

static int rdma_setup_conn(struct z_rdma_ep *rep)
{
	struct rdma_conn_param conn_param;
	struct ibv_qp_init_attr qp_attr;
	int ret = -ENOMEM;

	rep->pd = ibv_alloc_pd(rep->cm_id->verbs);
	if (!rep->pd) {
		LOG_(rep, "RDMA: ibv_alloc_pd failed\n");
		goto err_0;
	}

	rep->cq_channel = ibv_create_comp_channel(rep->cm_id->verbs);
	if (!rep->cq_channel) {
		LOG_(rep, "RDMA: ibv_create_comp_channel failed\n");
		goto err_0;
	}

	rep->sq_cq = ibv_create_cq(rep->cm_id->verbs,
				   SQ_DEPTH + 2,
				   rep, rep->cq_channel, 0);
	if (!rep->sq_cq) {
		LOG_(rep, "RDMA: ibv_create_cq failed\n");
		goto err_0;
	}

	rep->rq_cq = ibv_create_cq(rep->cm_id->verbs,
				   RQ_DEPTH + 2,
				   rep, rep->cq_channel, 0);
	if (!rep->rq_cq) {
		LOG_(rep, "RDMA: ibv_create_cq failed\n");
		goto err_0;
	}

	struct epoll_event cq_event;
	cq_event.data.ptr = rep;
	cq_event.events = EPOLLIN | EPOLLOUT;
	zap_get_ep(&rep->ep);
	ret = epoll_ctl(cq_fd, EPOLL_CTL_ADD, rep->cq_channel->fd, &cq_event);
	if (ret) {
		LOG_(rep, "RMDA: epoll_ctl CTL_ADD failed\n");
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
	qp_attr.cap.max_send_wr = SQ_DEPTH+2;
	qp_attr.cap.max_recv_wr = RQ_DEPTH+2;
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

	ret = rdma_fill_rq(rep);
	if (ret)
		goto err_0;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 4;
	conn_param.initiator_depth = 4;
	conn_param.retry_count = 7;

	/*
	 * Take a transport reference, it will get dropped by cm_thread_proc
	 * when it gets the DISCONNECTED event.
	 */
	zap_get_ep(&rep->ep);
	ret = rdma_connect(rep->cm_id, &conn_param);
	if (ret) {
		zap_put_ep(&rep->ep);
		goto err_0;
	}
	return 0;
 err_0:
	rep->ep.state = ZAP_EP_ERROR;
	return ret;
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
		rep->ep.state = ZAP_EP_ERROR;
		rep->ep.cb(&rep->ep, &zev);
	}
}

static void
handle_connect_request(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id)
{
	zap_ep_t new_ep;
	struct z_rdma_ep *new_rep;
	zap_err_t zerr;
	struct zap_event zev;
	assert(rep->ep.state == ZAP_EP_LISTENING);
	zev.type = ZAP_EVENT_CONNECT_REQUEST;
	zev.status = ZAP_ERR_OK;
	zerr = zap_new(rep->ep.z, &new_ep, rep->ep.cb);
	if (zerr) {
		LOG_(rep, "RDMA: could not create a new"
		     "endpoint for a connection request.\n");
		return;
	}
	new_rep = (struct z_rdma_ep *)new_ep;
	new_rep->cm_id = cma_id;
	cma_id->context = new_rep;
	new_rep->ep.state = ZAP_EP_CONNECTING;
	new_rep->ep.cb(new_ep, &zev);
}

static void
handle_route_resolved(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id)
{
	struct zap_event zev;
	int ret;
	assert(rep->ep.state == ZAP_EP_CONNECTING);
	ret = rdma_setup_conn(rep);
	if (ret) {
		(void)epoll_ctl(cm_fd, EPOLL_CTL_DEL,
				rep->cm_channel->fd, NULL);
		zev.type = ZAP_EVENT_CONNECT_ERROR;
		zev.status = ZAP_ERR_ROUTE;
		rep->ep.state = ZAP_EP_ERROR;
		rep->ep.cb(&rep->ep, &zev);
	}
}

static void
handle_rejected(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id)
{
	struct zap_event zev;
	zev.type = ZAP_EVENT_REJECTED;
	zev.status = ZAP_ERR_CONNECT;
	rep->ep.state = ZAP_EP_ERROR;
	rep->ep.cb(&rep->ep, &zev);
}

static void
handle_established(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id)
{
	struct zap_event zev;
	assert(rep->ep.state == ZAP_EP_CONNECTING);
	zev.type = ZAP_EVENT_CONNECTED;
	zev.status = ZAP_ERR_OK;
	rep->ep.state = ZAP_EP_CONNECTED;
	rep->ep.cb(&rep->ep, &zev);
}

static void
handle_error(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id, int reason)
{
	struct zap_event zev;
	assert(rep->ep.state == ZAP_EP_CONNECTING);
	zev.type = ZAP_EVENT_CONNECT_ERROR;
	zev.status = reason;
	rep->ep.state = ZAP_EP_ERROR;
	rep->ep.cb(&rep->ep, &zev);
}

static void
handle_disconnected(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id)
{
	assert((rep->ep.state == ZAP_EP_CONNECTED) || /* peer disconnected */
	       (rep->ep.state == ZAP_EP_DISCONNECTING)); /* we disconnected */

	rep->ep.state = ZAP_EP_DISCONNECTED;
	rdma_disconnect(rep->cm_id);
}

static void
handle_timewait(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id)
{
	struct zap_event zev;
	assert(rep->ep.state == ZAP_EP_DISCONNECTED ||
	       rep->ep.state == ZAP_EP_ERROR);
	zev.type = ZAP_EVENT_DISCONNECTED;
	zev.status = ZAP_ERR_OK;
	rep->ep.cb(&rep->ep, &zev);
	rep->ep.state = ZAP_EP_INIT;
	zap_put_ep(&rep->ep);
}

static void cma_event_handler(struct z_rdma_ep *rep,
			      struct rdma_cm_id *cma_id,
			      enum rdma_cm_event_type event)
{
	switch (event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		handle_addr_resolved(rep, cma_id);
		break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		handle_route_resolved(rep, cma_id);
		break;

	case RDMA_CM_EVENT_CONNECT_REQUEST:
		handle_connect_request(rep, cma_id);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		handle_established(rep, cma_id);
		break;

	case RDMA_CM_EVENT_REJECTED:
		handle_rejected(rep, cma_id);
		break;

	case RDMA_CM_EVENT_DISCONNECTED:
		handle_disconnected(rep, cma_id);
		break;

	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		handle_timewait(rep, cma_id);
		break;

	case RDMA_CM_EVENT_CONNECT_ERROR:
		handle_error(rep, cma_id, ZAP_ERR_CONNECT);
		break;

	case RDMA_CM_EVENT_ADDR_ERROR:
		handle_error(rep, cma_id, ZAP_ERR_ADDRESS);
		break;

	case RDMA_CM_EVENT_ROUTE_ERROR:
		handle_error(rep, cma_id, ZAP_ERR_ROUTE);
		break;

	case RDMA_CM_EVENT_UNREACHABLE:
		handle_error(rep, cma_id, ZAP_ERR_HOST_UNREACHABLE);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
	default:
		LOG_(rep, "RDMA: Unhandled event %s, ignoring\n",
		     rdma_event_str(event));
		break;
	}
}

/*
 * Connection Manager Thread - event thread that processes CM events.
 */
static void *cm_thread_proc(void *arg)
{
	int cm_fd = (int)(unsigned long)arg;
	struct epoll_event cm_events[16];
	struct rdma_cm_event *event;
	struct z_rdma_ep *rep;
	int ret, i;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	while (1) {
		ret = epoll_wait(cm_fd, cm_events, 16, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		for (i = 0; i < ret; i++) {
			rep = cm_events[i].data.ptr;
			if (!rep || !rep->cm_channel)
				continue;
			if (rdma_get_cm_event(rep->cm_channel, &event))
				continue;

			/*
			 * Ack the event first otherwise, you can end up
			 * dead-locking if the cm_id is being
			 * destroyed due to disconnect.
			 */
			enum rdma_cm_event_type ev = event->event;
			struct rdma_cm_id *cm_id = event->id;
			rdma_ack_cm_event(event);
			/*
			 * Many connection oriented events are
			 * delivered on the cm_channel of the listener
			 * cm_id. Use the endpoint from the cm_id context,
			 * not the one from the cm_channel.
			 */
			rep = (struct z_rdma_ep *)cm_id->context;
			cma_event_handler(rep, cm_id, ev);
		}
	}
	return NULL;
}

static zap_err_t
z_rdma_listen(zap_ep_t ep, struct sockaddr *sin, socklen_t sa_len)
{
	zap_err_t zerr = ZAP_ERR_RESOURCE;
	int rc;
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct epoll_event cm_event;

	/* Create the event CM event channel */
	rep->cm_channel = rdma_create_event_channel();
	if (!rep->cm_channel)
		goto err_0;

	/* Create the listening CM ID */
	rc = rdma_create_id(rep->cm_channel, &rep->cm_id, rep, RDMA_PS_TCP);
	if (rc)
		goto err_1;

	/* Bind the provided address to the CM Id */
	zerr = ZAP_ERR_BUSY;
	rc = rdma_bind_addr(rep->cm_id, sin);
	if (rc)
		goto err_2;

	cm_event.events = EPOLLIN | EPOLLOUT;
	cm_event.data.ptr = rep;
	zap_get_ep(&rep->ep);
	zerr = ZAP_ERR_RESOURCE;
	rc = epoll_ctl(cm_fd, EPOLL_CTL_ADD, rep->cm_channel->fd, &cm_event);
	if (rc)
		goto err_3;

	/*
	 * Asynchronous listen. Connection requests handled in
	 * cm_thread_proc
	 */
	rc = rdma_listen(rep->cm_id, 3);
	if (rc)
		goto err_3;

	rep->ep.state = ZAP_EP_LISTENING;
	return ZAP_ERR_OK;

 err_3:
	rc = epoll_ctl(cm_fd, EPOLL_CTL_DEL,
			rep->cm_channel->fd, NULL);
	zap_put_ep(&rep->ep);
 err_2:
	rdma_destroy_id(rep->cm_id);
	rep->cm_id = NULL;
 err_1:
	rdma_destroy_event_channel(rep->cm_channel);
	rep->cm_channel = NULL;
 err_0:
	return zerr;
}

/*
 * Best effort credit update. The receipt of this message kicks the i/o
 * on the deferred credit queue in the peer. Called with the credit_lock held.
 */
static int send_credit_update(struct z_rdma_ep *rep)
{
	struct rdma_message_hdr *req;
	struct rdma_buffer *rbuf;
	struct rdma_context *ctxt;
	struct ibv_send_wr *bad_wr;

	if (rep->ep.state != ZAP_EP_CONNECTED)
		return ENOTCONN;

	rbuf = rdma_buffer_alloc(rep, RQ_BUF_SZ, IBV_ACCESS_LOCAL_WRITE);
	if (!rbuf)
		return ENOMEM;

	ctxt = rdma_context_alloc(rep, NULL, IBV_WC_SEND, rbuf);
	if (!ctxt) {
		rdma_buffer_free(rbuf);
		return ENOMEM;
	}

	req = (struct rdma_message_hdr *)rbuf->data;
	req->credits = htons(rep->lcl_rq_credits);
	req->msg_type = htons(RDMA_CREDIT_UPDATE);
	rep->lcl_rq_credits = 0;
	rbuf->data_len = sizeof(req);

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
		rdma_context_free(ctxt);
		rdma_buffer_free(rbuf);
		return ENOSPC;
	}
	return 0;
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

static zap_err_t z_rdma_send(zap_ep_t ep, void *buf, size_t len)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct rdma_message_hdr *hdr;
	struct rdma_buffer *rbuf;
	int rc;

	if (len > RQ_BUF_SZ) {
		rc = ZAP_ERR_NO_SPACE;
		goto out;
	}

	rbuf = rdma_buffer_alloc(rep, RQ_BUF_SZ, IBV_ACCESS_LOCAL_WRITE);
	if (!rbuf) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}

	hdr = (struct rdma_message_hdr *)rbuf->data;
	hdr->msg_type = htons(RDMA_SEND);

	/* copy the data, leaving room for the rdma header */
	memcpy(rbuf->data+sizeof(struct rdma_message_hdr), buf, len);
	rbuf->data_len = len + sizeof(struct rdma_message_hdr);

	return rdma_post_send(rep, rbuf);
 out:
	return rc;
}

static zap_err_t z_rdma_share(zap_ep_t ep, zap_map_t map, uint64_t ctxt)
{
	struct z_rdma_map *rmap = (struct z_rdma_map *)map;
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct rdma_share_msg *sm;
	struct rdma_buffer *rbuf;
	int rc;

	rbuf = rdma_buffer_alloc(rep, RQ_BUF_SZ, IBV_ACCESS_LOCAL_WRITE);
	if (!rbuf) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}

	sm = (struct rdma_share_msg *)rbuf->data;
	sm->hdr.msg_type = htons(RDMA_RENDEZVOUS);
	sm->rkey = rmap->mr->rkey;
	sm->va = (unsigned long)rmap->map.addr;
	sm->len = htonl(rmap->map.len);
	sm->acc = htonl(rmap->map.acc);
	sm->ctxt = ctxt;

	rbuf->data_len = sizeof(*sm);

	return rdma_post_send(rep, rbuf);
 out:
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

	acc_flags = (ZAP_ACCESS_WRITE ? IBV_ACCESS_REMOTE_WRITE : 0);
	acc_flags |= (ZAP_ACCESS_READ ? IBV_ACCESS_REMOTE_READ : 0);
	acc_flags |= IBV_ACCESS_LOCAL_WRITE;

	if (mem_info)
		LOG_(rep, "%s meminfo %p.\n", __func__, mem_info);
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
			      zap_map_t src_map, void *src,
			      zap_map_t dst_map, void *dst,
			      size_t sz,
			      void *context)
{
	int rc;
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct z_rdma_map *rmap = (struct z_rdma_map *)dst_map;
	struct z_rdma_map *lmap = (struct z_rdma_map *)src_map;
	struct ibv_send_wr *bad_wr;
	struct rdma_context *ctxt =
		rdma_context_alloc(rep, context, IBV_WC_RDMA_READ, NULL);

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
	return rc;
}

static zap_err_t z_rdma_read(zap_ep_t ep,
			     zap_map_t src_map, void *src,
			     zap_map_t dst_map, void *dst,
			     size_t sz,
			     void *context)
{
	zap_err_t rc;
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct z_rdma_map *lmap = (struct z_rdma_map *)dst_map;
	struct z_rdma_map *rmap = (struct z_rdma_map *)src_map;
	struct ibv_send_wr *bad_wr;
	struct rdma_context *ctxt =
		rdma_context_alloc(rep, context, IBV_WC_RDMA_READ, NULL);

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
	return rc;
}

static int init_complete = 0;
static pthread_t cm_thread, cq_thread;

static void z_rdma_cleanup(void)
{
	void *dontcare;

	if (cm_thread) {
		pthread_cancel(cm_thread);
		pthread_join(cm_thread, &dontcare);
	}
	if (cq_thread) {
		pthread_cancel(cq_thread);
		pthread_join(cq_thread, &dontcare);
	}
}

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
	atexit(z_rdma_cleanup);
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

	z->max_msg = RQ_BUF_SZ - sizeof(struct rdma_message_hdr);
	z->new = z_rdma_new;
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
