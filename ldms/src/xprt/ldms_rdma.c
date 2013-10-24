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

#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_rdma_xprt.h"

#define LOG__(x, ...) { if (x && x->log) x->log(__VA_ARGS__); }
#define LOG_(x, ...) { if (x && x->xprt && x->xprt->log) x->xprt->log(__VA_ARGS__); }

LIST_HEAD(rdma_list, ldms_rdma_xprt) rdma_list;

static int rdma_fill_rq(struct ldms_rdma_xprt *x);
static void *cm_thread_proc(void *arg);
static void *cq_thread_proc(void *arg);
static int cq_event_handler(struct ibv_cq *cq, int count);
static void rdma_context_free(struct rdma_context *ctxt);
static void rdma_buffer_free(struct rdma_buffer *rbuf);
static int send_credit_update(struct ldms_rdma_xprt *r);

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
	char lcl_buf[32];
	char rem_buf[32];
	struct sockaddr_in *lcl = (struct sockaddr_in *)&r->xprt->local_ss;
	struct sockaddr_in *rem = (struct sockaddr_in *)&r->xprt->remote_ss;

	(void)inet_ntop(AF_INET, &lcl->sin_addr, lcl_buf, sizeof(lcl_buf));
	(void)inet_ntop(AF_INET, &rem->sin_addr, rem_buf, sizeof(rem_buf));

	if (r->cm_id && r->conn_status == CONN_CONNECTED)
		assert(0);

	r->conn_status = CONN_IDLE;
	r->xprt->connected = 0;

	/* Destroy the QP */
	if (!r->server && r->qp)
		rdma_destroy_qp(r->cm_id);

	while (!LIST_EMPTY(&r->xprt->rbd_list)) {
		struct ldms_rbuf_desc *desc = LIST_FIRST(&r->xprt->rbd_list);
		ldms_free_rbd(desc);
	}
	/* Destroy the RQ CQ */
	if (r->rq_cq) {
		if (ibv_destroy_cq(r->rq_cq))
			LOG_(r, "RDMA: Error %d : ibv_destroy_cq failed\n",
			     errno);
	}
	/* Destroy the SQ CQ */
	if (r->sq_cq) {
		if (ibv_destroy_cq(r->sq_cq))
			LOG_(r, "RDMA: Error %d : ibv_destroy_cq failed\n",
			     errno);
	}
	/* Destroy the PD */
	if (r->pd) {
		if (ibv_dealloc_pd(r->pd))
			LOG_(r, "RDMA: Error %d : ibv_dealloc_pd failed\n",
			     errno);
	}
	/* Destroy the CM id */
	if (r->cm_id) {
		if (rdma_destroy_id(r->cm_id))
			LOG_(r, "RDMA: Error %d : rdma_destroy_id failed\n",
			     errno);
	}
	if (r->cm_channel)
		rdma_destroy_event_channel(r->cm_channel);

	if (r->cq_channel) {
		if (ibv_destroy_comp_channel(r->cq_channel))
			LOG_(r, "RDMA: Error %d : "
			     "ibv_destroy_comp__channel failed\n", errno);
	}
	r->cm_id = NULL;
	r->pd = NULL;
	r->rq_cq = NULL;
	r->sq_cq = NULL;
	r->qp = NULL;
	r->cq_channel = NULL;
	r->cm_channel = NULL;
	r->rem_rq_credits = RQ_DEPTH;
	r->sq_credits = SQ_DEPTH;
	r->lcl_rq_credits = 0;
	while (!TAILQ_EMPTY(&r->io_q)) {
		struct rdma_context *ctxt = TAILQ_FIRST(&r->io_q);
		TAILQ_REMOVE(&r->io_q, ctxt, pending_link);
		if (ctxt->rb)
			rdma_buffer_free(ctxt->rb);
		rdma_context_free(ctxt);
	}
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
	rbuf->data = (char *)(rbuf+1);
	rbuf->data_len = len;
	rbuf->mr = ibv_reg_mr(x->pd, rbuf->data, len, f);
	if (!rbuf->mr) {
		free(rbuf);
		LOG_(x, "RDMA: recv_buf reg_mr failed: error %d\n", errno);
		return NULL;
	}
	return rbuf;
}

static void rdma_buffer_free(struct rdma_buffer *rbuf)
{
	int rc = ibv_dereg_mr(rbuf->mr);
	free(rbuf);
}

static struct rdma_context *
rdma_context_alloc(struct ldms_rdma_xprt *r,
		   void *usr_context, enum ibv_wc_opcode op,
		   struct rdma_buffer *rbuf)
{
	struct rdma_context *ctxt = calloc(1, sizeof *ctxt);
	if (!ctxt)
		return NULL;

	ctxt->usr_context = usr_context;
	ctxt->op = op;
	ctxt->rb = rbuf;
	ctxt->x = r;

	/* pair with rdma_context_free */
	ldms_xprt_get(r->xprt);
	return ctxt;
}

/*
 * Enqueue a context to the pending I/O queue.
 *
 * Must be called with the credit_lock held.
 */
static int queue_io(struct ldms_rdma_xprt *x, struct rdma_context *ctxt)
{
	TAILQ_INSERT_TAIL(&x->io_q, ctxt, pending_link);
	return 0;
}

/*
 * Called by submit_pending and post_send.
 *
 * Must be called with the credit_lock held.
 */
static int post_send(struct ldms_rdma_xprt *x,
		     struct rdma_context *ctxt, struct ibv_send_wr **bad_wr,
		     int is_rdma)
{
	int rc;
	struct rdma_request_hdr *msg;
	if (x->conn_status != CONN_CONNECTED) {
		LOG_(x, "%s: not connected conn_status %d\n",
		     __func__, x->conn_status);
		errno = ENOTCONN;
		return ENOTCONN;
	}

	/*
	 * Only RDMA_SEND ops generate completions at the peer
	 */
	if (!is_rdma) {
		msg = (struct rdma_request_hdr *)(unsigned long)
			ctxt->wr.sg_list[0].addr;
		msg->credits = htonl(x->lcl_rq_credits);
		x->lcl_rq_credits = 0;
	}

	rc = ibv_post_send(x->qp, &ctxt->wr, bad_wr);
	if (rc) {
		LOG_(x, "%s: error %d posting send is_rdma %d sq_credits %d "
		     "lcl_rq_credits %d rem_rq_credits %d.\n", __func__,
		     rc, is_rdma, x->sq_credits, x->lcl_rq_credits, x->rem_rq_credits);
		ldms_release_xprt(x->xprt);
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

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
static char *xlate_cmd(int cmd)
{
	static char cmd_s[16];
	if (cmd >= 0 && cmd <= ARRAY_SIZE(cmd_str))
		return cmd_str[cmd];

	sprintf(cmd_s, "%x", cmd);
	return cmd_s;
}

static char *xlate_ctxt(struct rdma_context *ctxt)
{
	static char s[80];
	struct ldms_rdma_xprt *x = ctxt->x;
	if (ctxt->op != IBV_WC_SEND)
		sprintf(s, "%s", (ctxt->op < 0 ? "RECV" : op_str[ctxt->op]));
	else {
		struct rdma_request_hdr *rh = (void *)(unsigned long)
			ctxt->wr.sg_list[0].addr;
		struct ldms_request_hdr *hdr = (void *)(rh+1);
		int cmd = ntohl(hdr->cmd);
		sprintf(s, "SEND '%s'",
		     xlate_cmd(cmd),
		     (void *)(unsigned long)hdr->xid);
	}
	return s;
}

/*
 * This version is called from within get_credits, after checking for
 * previously queued I/O, and from submit_pending. It requires that the
 * credit_lock is held by the caller.
 */
static int _get_credits(struct ldms_rdma_xprt *x, int is_rdma)
{
	/* Get an SQ credit first */
	if (x->sq_credits)
		x->sq_credits--;
	else
		return EWOULDBLOCK;

	if (is_rdma)
		/* RDMA ops don't need an RQ credit */
		return 0;

	/* Get an RQ credit */
	if (x->rem_rq_credits)
		x->rem_rq_credits--;
	else {
		/* Return the SQ credit taken above */
		x->sq_credits++;
		return EWOULDBLOCK;
	}
	return 0;
}

/*
 * Determines if there is space in the SQ for all WR and if there is a
 * slot available in the remote peer's RQ for SENDs.
 */
static int get_credits(struct ldms_rdma_xprt *x, int is_rdma)
{
	int rc = 0;

	/* Don't submit I/O ahead of previously posted I/O. */
	if (!TAILQ_EMPTY(&x->io_q)) {
		rc = EWOULDBLOCK;
		goto out;
	}
	rc = _get_credits(x, is_rdma);
 out:
	return rc;
}

static void put_sq(struct ldms_rdma_xprt *x)
{
	pthread_mutex_lock(&x->credit_lock);
	if (x->sq_credits < SQ_DEPTH)
		x->sq_credits++;
	pthread_mutex_unlock(&x->credit_lock);
}

/*
 * Walk the list of pending I/O and submit if there are sufficient
 * credits available.
 */
static void submit_pending(struct ldms_rdma_xprt *x)
{
	struct ibv_send_wr *badwr;
	struct rdma_context *ctxt;
	int is_rdma;

	if (x->conn_status != CONN_CONNECTED)
		return;

	pthread_mutex_lock(&x->credit_lock);
	while (!TAILQ_EMPTY(&x->io_q)) {
		ctxt = TAILQ_FIRST(&x->io_q);
		is_rdma = (ctxt->op != IBV_WC_SEND);
		if (_get_credits(x, is_rdma))
			goto out;

		TAILQ_REMOVE(&x->io_q, ctxt, pending_link);

		if (post_send(x, ctxt, &badwr, is_rdma))
			LOG_(x, "Error posting queued I/O.\n");
	}
 out:
	pthread_mutex_unlock(&x->credit_lock);
}

static void rdma_context_free(struct rdma_context *ctxt)
{
	/* pair with rdma_context_alloc */
	ldms_release_xprt(ctxt->x->xprt);
	free(ctxt);
}

static int rdma_post_send(struct ldms_rdma_xprt *x, struct rdma_buffer *rbuf)
{
	int rc;
	struct ibv_send_wr *bad_wr;
	struct rdma_context *ctxt = rdma_context_alloc(x, NULL, IBV_WC_SEND, rbuf);
	if (!ctxt) {
		errno = ENOMEM;
		return ENOMEM;
	}
	if (x->conn_status != CONN_CONNECTED)
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
	pthread_mutex_lock(&x->credit_lock);
	if (!get_credits(x, 0))
		rc = post_send(x, ctxt, &bad_wr, 0);
	else
		rc = queue_io(x, ctxt);
	pthread_mutex_unlock(&x->credit_lock);
	return rc;
 err_0:
	free(ctxt);
	return ENOTCONN;
}

static void rdma_xprt_close(struct ldms_xprt *x)
{
	struct ldms_rdma_xprt *r = rdma_from_xprt(x);
	if (r->cm_id && (r->conn_status == CONN_CONNECTED ||
				r->conn_status == CONN_ERROR_CONNECTED))
		rdma_disconnect(r->cm_id);
	/* NOTE: rdma_disconnect should not be called if the connection is not
	 * established. The status after the connection is established can only
	 * be CONN_CONNECTED or CONN_ERROR_CONNECTED.
	 */
}

static int rdma_xprt_connect(struct ldms_xprt *x,
			     struct sockaddr *sin, socklen_t sa_len)
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
		rc = ENOMEM;
		goto err_1;
	}

	cm_event.events = EPOLLIN | EPOLLOUT;
	cm_event.data.ptr = r;

	/* pair with release in RDMA_CM_EVENT_TIMEWAIT_EXIT */
	ldms_xprt_get(r->xprt);
	rc = epoll_ctl(cm_fd, EPOLL_CTL_ADD, r->cm_channel->fd, &cm_event);
	if (rc)
		goto err_2;

	sem_init(&r->sem, 0, 0);

	r->conn_status = CONN_CONNECTING;

	/* Bind the provided address to the CM Id */
	rc = rdma_resolve_addr(r->cm_id, NULL,
			       (struct sockaddr *)&x->remote_ss,
			       2000);
	if (rc) {
		if (errno)
			rc = errno;
		goto err_3;
	}

	do {
		rc = sem_wait(&r->sem);
	} while (rc && errno == EINTR);

	if (r->conn_status == CONN_ERROR) {
		rc = EHOSTDOWN;
		goto err_3;
	}

	cq_event.data.ptr = r;
	cq_event.events = EPOLLIN | EPOLLOUT;

	/* pair with release in RDMA_CM_EVENT_TIMEWAIT_EXIT */
	ldms_xprt_get(r->xprt);
	rc = epoll_ctl(cq_fd, EPOLL_CTL_ADD, r->cq_channel->fd, &cq_event);
	if (rc) {
		rc = ENOMEM;
		goto err_4;
	}

	rc = rdma_fill_rq(r);
	if (rc) {
		rc = ENOMEM;
		goto err_4;
	}
	return 0;

 err_4:
	/* We became connected. The CM will receive a disconnect event
	 * and we'll clean up there. In the case where we failed to
	 * add the CQ fd to the CQ channel, we won't get CQ events,
	 * but there have been no WR posted yet to clean up.
	 */
	rdma_disconnect(r->cm_id);
	return rc;

 err_3:
	/* These are all syncrhonous clean-ups. IB does not support
	 * reconnecting on the same cm_id, so blow everything away
	 * and start over next time
	 */
	(void)epoll_ctl(cm_fd, EPOLL_CTL_DEL, r->cm_channel->fd, NULL);
 err_2:
	/* pair with get before the first epoll_ctl in this function */
	ldms_release_xprt(r->xprt);
 err_1:
 err_0:
	return rc;
}

static int rdma_post_recv(struct ldms_rdma_xprt *r, struct rdma_buffer *rb)
{
	struct ibv_recv_wr wr;
	struct ibv_sge sge;
	struct ibv_recv_wr *bad_wr;
	struct rdma_context *ctxt;
	int rc;

	if (r->conn_status > CONN_CONNECTED) {
		errno = ENOTCONN;
		return ENOTCONN;
	}

	ctxt = rdma_context_alloc(r, NULL, -1, rb);
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

static void process_send_wc(struct ldms_rdma_xprt *r, struct ibv_wc *wc,
			    struct rdma_buffer *rb)
{
	if (rb)
		rdma_buffer_free(rb);
}

static void process_read_wc(struct ldms_rdma_xprt *r, struct ibv_wc *wc,
			    void *usr_context)
{
	if (r->xprt && r->xprt->read_complete_cb)
		r->xprt->read_complete_cb(r->xprt, usr_context);
}

static void process_recv_wc(struct ldms_rdma_xprt *r, struct ibv_wc *wc,
			    struct rdma_buffer *rb)
{
	struct rdma_request_hdr *rh = (struct rdma_request_hdr *)rb->data;
	struct ldms_request *req = (struct ldms_request *)(rh + 1);
	int ret;
	int cmd = ntohl(req->hdr.cmd);

	pthread_mutex_lock(&r->credit_lock);
	r->rem_rq_credits += ntohl(rh->credits);
	pthread_mutex_unlock(&r->credit_lock);

	if (cmd & LDMS_CMD_XPRT_PRIVATE) {
		if (rdma_post_recv(r, rb)) {
			LOG_(r, "RDMA: ibv_post_recv failed: %d\n", ret);
			rdma_buffer_free(rb);
		}
		return;
	}

	if (r->xprt && r->xprt->recv_cb)
		r->xprt->recv_cb(r->xprt, req);

	ret = rdma_post_recv(r, rb);
	if (ret) {
		LOG_(r, "RDMA: ibv_post_recv failed: %d\n", ret);
		rdma_buffer_free(rb);
	}

	pthread_mutex_lock(&r->credit_lock);
	if (r->lcl_rq_credits < RQ_DEPTH)
		r->lcl_rq_credits ++;
	if (r->lcl_rq_credits > (RQ_DEPTH >> 1)) {
		if (send_credit_update(r))
			LOG_(r, "RDMA: credit update could not be sent.\n");
	}
	pthread_mutex_unlock(&r->credit_lock);
}

static int rdma_fill_rq(struct ldms_rdma_xprt *x)
{
	int i;

	for (i = 0; i < RQ_DEPTH+2; i++) {
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

	/* Create a new RQ CQ. */
	r->rq_cq = ibv_create_cq(r->cm_id->verbs,
				 RQ_DEPTH + 2,
				 r, r->cq_channel, 0);
	if (!r->rq_cq) {
		LOG_(server, "RDMA: ibv_create_cq failed\n");
		goto out_0;
	}

	/* Create a new SQ CQ. */
	r->sq_cq = ibv_create_cq(r->cm_id->verbs,
				 SQ_DEPTH + 2,
				 r, r->cq_channel, 0);
	if (!r->sq_cq) {
		LOG_(server, "RDMA: ibv_create_cq failed\n");
		goto out_0;
	}

	ret = ibv_req_notify_cq(r->sq_cq, 0);
	if (ret) {
		LOG_(server, "RDMA: ibv_create_cq failed\n");
		goto out_0;
	}

	ret = ibv_req_notify_cq(r->rq_cq, 0);
	if (ret) {
		LOG_(server, "RDMA: ibv_create_cq failed\n");
		goto out_0;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.cap.max_send_wr = SQ_DEPTH+2; /* +2 for the credit update */
	qp_attr.cap.max_recv_wr = RQ_DEPTH+2; /* +2 for the credit update */
	qp_attr.cap.max_recv_sge = SQ_SGE;
	qp_attr.cap.max_send_sge = RQ_SGE;
	qp_attr.qp_type = IBV_QPT_RC;
	qp_attr.send_cq = r->sq_cq;
	qp_attr.recv_cq = r->rq_cq;

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

	/* For cq channel */
	/* pair with release in RDMA_CM_EVENT_TIMEWAIT_EXIT */
	ldms_xprt_get(r->xprt);
	ret = epoll_ctl(cq_fd, EPOLL_CTL_ADD, r->cq_channel->fd, &cq_event);
	if (ret) {
		LOG_(server, "RDMA: Could not add passive side CQ fd (%d)"
		     "to event queue.\n",
		     r->cq_channel->fd);
		goto out_1;
	}

	/* Accept the connection */
	struct rdma_conn_param conn_param = {0};
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 4;
	conn_param.retry_count = 7;
	ret = rdma_accept(r->cm_id, &conn_param);
	if (ret)
		goto out_1;

	memcpy(&r->xprt->local_ss, rdma_get_local_addr(r->cm_id), sizeof(struct sockaddr_in));
	memcpy(&r->xprt->remote_ss, rdma_get_peer_addr(r->cm_id), sizeof(struct sockaddr_in));
	return;

 out_1:
	/* Match with ldms_xprt_get before epoll_ctl */
	ldms_release_xprt(r->xprt);
 out_0:
	/* to destroy the endpoint if things gone wrong in accepting */
	ldms_release_xprt(r->xprt);
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
		struct ldms_rdma_xprt *x = ctxt->x;
		ret++;
		if (wc.status) {
			x->conn_status = CONN_ERROR_CONNECTED;
			x->xprt->connected = 0;
			if (wc.status != IBV_WC_WR_FLUSH_ERR) {
				struct rdma_request_hdr *rh = (void *)(unsigned long)
					ctxt->wr.sg_list[0].addr;
				struct ldms_request_hdr *hdr = (void *)(rh+1);
				int cmd = ntohl(hdr->cmd);
				LOG_(x, "RDMA: WR op '%s' failed with status '%s'\n",
				     (ctxt->op < 0 ? "RECV" : op_str[ctxt->op]),
				     err_msg[wc.status]);
				LOG_(x, "    addr %p len %d lkey %p.\n",
				     ctxt->wr.sg_list[0].addr,
				     ctxt->wr.sg_list[0].length,
				     ctxt->wr.sg_list[0].lkey);
				LOG_(x, "RDMA: cmd '%s' %p.\n",
				     xlate_cmd(cmd),
				     (void *)(unsigned long)hdr->xid);
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
			process_send_wc(x, &wc, ctxt->rb);
			put_sq(x);
			break;

		case IBV_WC_RDMA_WRITE:
			put_sq(x);
			break;

		case IBV_WC_RDMA_READ:
			process_read_wc(x, &wc, ctxt->usr_context);
			put_sq(x);
			break;

		case IBV_WC_RECV:
			process_recv_wc(x, &wc, ctxt->rb);
			break;

		default:
			LOG_(x,"RDMA: Invalid completion\n");
		}
		rdma_context_free(ctxt);
	}
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

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	while (1) {
		int fd_count = epoll_wait(cq_fd, cq_events, 16, -1);
		if (fd_count < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		for (i = 0; i < fd_count; i++) {
			r = cq_events[i].data.ptr;
			/* pair with release after submit_pending */
			ldms_xprt_get(r->xprt);

			/* Get the next event ... this will block */
			ret = ibv_get_cq_event(r->cq_channel, &ev_cq, &ev_ctx);
			if (ret) {
				LOG_(r, "cq_channel is %d at %d.\n",
				     r->cq_channel->fd, __LINE__);
				LOG_(r, "RDMA: Error %d at %s:%d\n",
					     errno, __func__, __LINE__);
				goto skip;
			}

			/* Re-arm the CQ */
			ret = ibv_req_notify_cq(ev_cq, 0);
			if (ret) {
				LOG_(r, "RDMA: Error %d at %s:%d\n",
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
				ret = cq_event_handler(r->sq_cq, SQ_DEPTH+2);
				if (ev_cq != r->sq_cq)
					ret += cq_event_handler(ev_cq, 1);
			} while (ret);

			/* Ack the event */
			ibv_ack_cq_events(ev_cq, 1);
			if (ret) {
				LOG_(r, "RDMA: Error %d at %s:%d\n",
				     errno, __func__, __LINE__);
			}
		skip:
			submit_pending(r);
			/* pair with get before ibv_get_cq_event */
			ldms_release_xprt(r->xprt);
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

	x->sq_cq = ibv_create_cq(x->cm_id->verbs,
				 SQ_DEPTH + 2,
				 x, x->cq_channel, 0);
	if (!x->sq_cq) {
		LOG_(x, "RDMA: ibv_create_cq failed\n");
		goto err_0;
	}

	x->rq_cq = ibv_create_cq(x->cm_id->verbs,
				 RQ_DEPTH + 2,
				 x, x->cq_channel, 0);
	if (!x->rq_cq) {
		LOG_(x, "RDMA: ibv_create_cq failed\n");
		goto err_0;
	}

	ret = ibv_req_notify_cq(x->rq_cq, 0);
	if (ret) {
		LOG_(x, "RMDA: ibv_create_cq failed\n");
		goto err_0;
	}

	ret = ibv_req_notify_cq(x->sq_cq, 0);
	if (ret) {
		LOG_(x, "RMDA: ibv_create_cq failed\n");
		goto err_0;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.cap.max_send_wr = SQ_DEPTH+2;
	qp_attr.cap.max_recv_wr = RQ_DEPTH+2;
	qp_attr.cap.max_recv_sge = SQ_SGE;
	qp_attr.cap.max_send_sge = RQ_SGE;
	qp_attr.qp_type = IBV_QPT_RC;
	qp_attr.send_cq = x->sq_cq;
	qp_attr.recv_cq = x->rq_cq;
	ret = rdma_create_qp(x->cm_id, x->pd, &qp_attr);
	x->qp = x->cm_id->qp;
	if (ret) {
		LOG_(x, "RDMA: rdma_create_qp failed\n");
		goto err_0;
	}

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 4;
	conn_param.retry_count = 7;
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
	memcpy(&x->xprt->local_ss, rdma_get_local_addr(x->cm_id), sizeof(struct sockaddr_in));
	memcpy(&x->xprt->remote_ss, rdma_get_peer_addr(x->cm_id), sizeof(struct sockaddr_in));
	return 0;
 err_0:
	x->conn_status = CONN_ERROR;
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

static int cma_event_handler(struct ldms_rdma_xprt *r,
			     struct rdma_cm_id *cma_id,
			     enum rdma_cm_event_type event)
{
	struct sockaddr_in *sin;
	int ret = 0;
	struct ldms_rdma_xprt *x = cma_id->context;
	char buf[32];
	char *s;

	switch (event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		ret = rdma_resolve_route(cma_id, 2000);
		if (ret) {
			sin = (struct sockaddr_in *)&x->xprt->remote_ss;
			LOG_(x, "RDMA: resolve route failed for %s:%hu.\n",
			     inet_ntop(AF_INET, &sin->sin_addr,
				       buf, sizeof(buf)),
			     ntohs(sin->sin_port));
			x->conn_status = CONN_ERROR;
			x->xprt->connected = 0;
			(void)epoll_ctl(cm_fd, EPOLL_CTL_DEL,
					x->cm_channel->fd, NULL);
			sem_post(&x->sem);
		}
		break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		ret = rdma_setup_conn(x);
		if (ret) {
			sin = (struct sockaddr_in *)&x->xprt->remote_ss;
			LOG_(x, "RDMA: setup connection failed for %s:%hu.\n",
			     inet_ntop(AF_INET, &sin->sin_addr,
				       buf, sizeof(buf)),
			     ntohs(sin->sin_port));
			x->conn_status = CONN_ERROR;
			x->xprt->connected = 0;
			sem_post(&x->sem);
		}
		break;

	case RDMA_CM_EVENT_CONNECT_REQUEST:
		rdma_accept_request(x, cma_id);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		x->conn_status = CONN_CONNECTED;
		x->xprt->connected = 1;
		sem_post(&x->sem);
		break;

	case RDMA_CM_EVENT_REJECTED:
		/* pair with get in rdma_setup_conn */
		ldms_release_xprt(x->xprt);
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
		x->conn_status = CONN_ERROR;
		x->xprt->connected = 0;
		ret = -1;
		sem_post(&x->sem);
		break;

	case RDMA_CM_EVENT_DISCONNECTED:
		x->conn_status = CONN_CLOSED;
		x->xprt->connected = 0;
		rdma_disconnect(x->cm_id);
		/* For passive side, this release pairs with transport creation.
		 * For active side, this release pairs with get in
		 * rdma_setup_conn.
		 */
		ldms_release_xprt(x->xprt);
		sem_post(&x->sem);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		if (x->conn_status == CONN_CONNECTED)
			x->conn_status = CONN_ERROR_CONNECTED;
		else
			x->conn_status = CONN_ERROR;
		x->xprt->connected = 0;
		sem_post(&x->sem);
		break;

	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		if (x->cm_channel) {
			ret = epoll_ctl(cm_fd, EPOLL_CTL_DEL,
					x->cm_channel->fd, NULL);
			if (ret) {
				LOG_(x, "RDMA: Error %d removing "
					     "transport from the "
					     "CM event queue.\n", ret);
			}
			/* pair with EPOLL_CTL_ADD for cm_channel */
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
			/* pair with EPOLL_CTL_ADD for cq_channel */
			ldms_release_xprt(x->xprt);
		}
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

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	while (1) {
		ret = epoll_wait(cm_fd, cm_events, 16, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		for (i = 0; i < ret; i++) {
			r = cm_events[i].data.ptr;
			if (!r || !r->cm_channel)
				continue;
			if (rdma_get_cm_event(r->cm_channel, &event))
				continue;

			/*
			 * Ack the event first otherwise, you can end up
			 * dead-locking if the cm_id is being
			 * destroyed due to disconnect.
			 */
			enum rdma_cm_event_type ev = event->event;
			struct rdma_cm_id *cm_id = event->id;
			rdma_ack_cm_event(event);
			(void)cma_event_handler(r, cm_id, ev);
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
	/* pair with get in this function */
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

/*
 * Best effort credit update. The receipt of this message kicks the i/o
 * on the deferred credit queue in the peer. Called with the credit_lock held.
 */
static int send_credit_update(struct ldms_rdma_xprt *r)
{
	struct rdma_credit_update_req *req;
	struct rdma_buffer *rbuf;
	struct rdma_context *ctxt;
	struct ibv_send_wr *bad_wr;

	if (r->conn_status != CONN_CONNECTED) {
		errno = ENOTCONN;
		return ENOTCONN;
	}

	rbuf = rdma_buffer_alloc(r, RQ_BUF_SZ, IBV_ACCESS_LOCAL_WRITE);
	if (!rbuf) {
		errno = ENOMEM;
		return ENOMEM;
	}

	ctxt = rdma_context_alloc(r, NULL, IBV_WC_SEND, rbuf);
	if (!ctxt) {
		rdma_buffer_free(rbuf);
		errno = ENOMEM;
		return ENOMEM;
	}

	req = (struct rdma_credit_update_req *)rbuf->data;
	req->hdr.xid = 0;
	req->hdr.cmd = htonl(RDMA_CREDIT_UPDATE_CMD);
	req->hdr.len = htonl(sizeof(req));
	req->rdma_hdr.credits = htonl(r->lcl_rq_credits);
	r->lcl_rq_credits = 0;
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

	if (ibv_post_send(r->qp, &ctxt->wr, &bad_wr)) {
		LOG_(r, "%s: sq_credits %d lcl_rq_credits %d rem_rq_credits %d.\n",
		     __func__, r->sq_credits, r->lcl_rq_credits, r->rem_rq_credits);
		rdma_context_free(ctxt);
		rdma_buffer_free(rbuf);
		if (errno)
			return errno;
		return ENOSPC;
	}
	return 0;
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
		rc = ENOSPC;
		goto out;
	}

	rbuf = rdma_buffer_alloc(r, RQ_BUF_SZ, IBV_ACCESS_LOCAL_WRITE);
	if (!rbuf) {
		rc = ENOMEM;
		goto out;
	}

	/* copy the data, leaving room for the rdma header */
	memcpy(rbuf->data+sizeof(struct rdma_request_hdr), req, len);
	rbuf->data_len = len + sizeof(struct rdma_request_hdr);

	return rdma_post_send(r, rbuf);
 out:
	errno = rc;
	return rc;
}

/** Allocate a remote buffer */
struct ldms_rbuf_desc *rdma_rbuf_alloc(struct ldms_xprt *x,
				       struct ldms_set *set,
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
	int rc;
	struct ibv_send_wr *bad_wr;
	struct rdma_context *ctxt =
		rdma_context_alloc(r, context, IBV_WC_RDMA_READ, NULL);

	ctxt->sge.addr = laddr;
	ctxt->sge.length = len;
	ctxt->sge.lkey = mr->lkey;

	memset(&ctxt->wr, 0, sizeof(ctxt->wr));
	ctxt->wr.sg_list = &ctxt->sge;
	ctxt->wr.num_sge = 1;
	ctxt->wr.opcode = IBV_WR_RDMA_READ;
	ctxt->wr.send_flags = IBV_SEND_SIGNALED;
	ctxt->wr.wr.rdma.remote_addr = raddr;
	ctxt->wr.wr.rdma.rkey = rkey;

	RDMA_SET_CONTEXT(&ctxt->wr, ctxt);
	pthread_mutex_lock(&r->credit_lock);
	if (!get_credits(r, 1)) {
		rc = post_send(r, ctxt, &bad_wr, 1);
		if (rc) {
			LOG_(r, "RDMA: post_send failed: code %d\n", errno);
			if (errno)
				rc = errno;
		}
	} else
		rc = queue_io(r, ctxt);
	pthread_mutex_unlock(&r->credit_lock);
	return rc;
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

void rdma_xprt_cleanup(void)
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
	atexit(rdma_xprt_cleanup);
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

struct ldms_xprt *xprt_get(int (*recv_cb)(struct ldms_xprt *, void *),
			   int (*read_complete_cb)(struct ldms_xprt *, void *),
			   ldms_log_fn_t log_fn)
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
	x->log = log_fn;
	r = calloc(1, sizeof(struct ldms_rdma_xprt));
	if (!r) {
		errno = ENOMEM;
		goto err_1;
	}

	x->max_msg = RQ_BUF_SZ - sizeof(struct rdma_request_hdr);
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

	r->rem_rq_credits = RQ_DEPTH;
	r->lcl_rq_credits = 0;
	r->sq_credits = SQ_DEPTH;

	TAILQ_INIT(&r->io_q);
	pthread_mutex_init(&r->credit_lock, NULL);
	r->xprt = x;

	sem_init(&r->sem, 0, 0);

	LIST_INSERT_HEAD(&rdma_list, r, client_link);
	return x;

 err_1:
	free(x);
 err_0:
	return NULL;
}
