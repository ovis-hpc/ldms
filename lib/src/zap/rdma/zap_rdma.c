/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2020 Open Grid Computing, Inc. All rights reserved.
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
#define _GNU_SOURCE
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
#include <stdarg.h>
#include <signal.h>
#include <infiniband/verbs.h>
#include "coll/rbt.h"
#include <sys/syscall.h>
#include <netdb.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include "zap_rdma.h"

#define LOG(FMT, ...)							\
	do {								\
		pid_t _tid = (pid_t) syscall (SYS_gettid);		\
		struct timespec _ts;					\
		clock_gettime(CLOCK_REALTIME, &_ts);			\
		z_rdma_log_fn("ZAP_RDMA [%ld.%09ld](%d) " FMT,		\
				_ts.tv_sec, _ts.tv_nsec,		\
				_tid, ##__VA_ARGS__);			\
	} while (0)

/* log with line fn/number */
#define LOGL(FMT, ...) LOG( "%s:%d " FMT, __func__, __LINE__, ##__VA_ARGS__ )

#ifdef DEBUG
#define DLOG(FMT, ...) LOG(FMT, ##__VA_ARGS__)
#else
#define DLOG(FMT, ...) /* no-op */
#endif

#ifdef EP_DEBUG
#define __rdma_deliver_disconnected( _REP )				\
	do {								\
		LOG("EP_DEBUG: %s() deliver_disonnected %p, "		\
		    "ref_count: %d\n", __func__, _REP,			\
				      (_REP)->ep.ref_count);		\
		_rdma_deliver_disconnected(_REP);			\
	} while (0)
#else /* EP_DEBUG */
#define __rdma_deliver_disconnected(_REP) _rdma_deliver_disconnected(_REP)
#endif /* EP_DEBUG */

#ifdef CTXT_DEBUG
#define __flush_io_q( _REP )						\
	do {								\
		LOG("CTXT_DEBUG: %s:%d flush_io_q %p, state %s\n",	\
		    __func__, __LINE__, _REP,				\
		    __zap_ep_state_str(_REP->ep.state));		\
		flush_io_q(_REP);					\
	} while (0)
#define __rdma_context_alloc( _REP, _CTXT, _OP, _RBUF )			\
	({ \
		void *_ctxt = _rdma_context_alloc(_REP, _CTXT,		\
						  _OP, _RBUF);		\
		LOG("CTXT_DEBUG: %s:%d, context_alloc %p rep %p "	\
		    "app_context %p op %d (%s) rbuf %p\n",		\
		    __func__, __LINE__, _ctxt, _REP, _CTXT,		\
		    _OP, #_OP, _RBUF);					\
		(_ctxt);							\
	})
#define __rdma_context_free( _CTXT )					\
	do {								\
		LOG("CTXT_DEBUG: %s:%d, context_free %p rep %p "	\
		    "app_context %p op %d (%s) rbuf %p\n",		\
		    __func__, __LINE__, _CTXT, _CTXT->ep,		\
		    _CTXT->usr_context, _CTXT->op,			\
		    ibv_op_str(_CTXT->op), _CTXT->rb );			\
		_rdma_context_free(_CTXT);				\
	} while (0)
#else /* CTXT_DEBUG */
#define __flush_io_q(_REP) flush_io_q(_REP)
#define __rdma_context_alloc( _REP, _CTXT, _OP, _RBUF ) _rdma_context_alloc(_REP, _CTXT, _OP, _RBUF)
#define __rdma_context_free( _CTXT ) _rdma_context_free(_CTXT)
#endif /* CTXT_DEBUG */

static struct ibv_context **devices = NULL;
static int devices_len = 0;

#define CQ_EVENT_LEN 16

void __default_log_fn(const char *fmt, ...)
{
	va_list ap;
	pid_t tid;
	struct timespec ts;
	char buf[4096];

	tid = (pid_t) syscall (SYS_gettid);
	clock_gettime(CLOCK_REALTIME, &ts);
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	fprintf(stderr, "[%d] %ld.%09ld: | %s", tid, ts.tv_sec, ts.tv_nsec, buf);
}

static const char *ibv_op_str(int op)
{
	switch (op) {
	case IBV_WC_SEND: return "IBV_WC_SEND";
	case IBV_WC_RDMA_WRITE: return "IBV_WC_RDMA_WRITE";
	case IBV_WC_RDMA_READ: return "IBV_WC_RDMA_READ";
	case IBV_WC_COMP_SWAP: return "IBV_WC_COMP_SWAP";
	case IBV_WC_FETCH_ADD: return "IBV_WC_FETCH_ADD";
	case IBV_WC_BIND_MW: return "IBV_WC_BIND_MW";
	case IBV_WC_LOCAL_INV: return "IBV_WC_LOCAL_INV";
	case IBV_WC_RECV: return "IBV_WC_RECV";
	case IBV_WC_RECV_RDMA_WITH_IMM: return "IBV_WC_RECV_RDMA_WITH_IMM";
#ifdef HAVE_VERBS_IBVWCT_ENUMS
	case IBV_WC_TSO: return "IBV_WC_TSO";
	case IBV_WC_TM_ADD: return "IBV_WC_TM_ADD";
	case IBV_WC_TM_DEL: return "IBV_WC_TM_DEL";
	case IBV_WC_TM_SYNC: return "IBV_WC_TM_SYNC";
	case IBV_WC_TM_RECV: return "IBV_WC_TM_RECV";
	case IBV_WC_TM_NO_TAG: return "IBV_WC_TM_NO_TAG";
#endif
	}
	return "UNKNOWN_OP";
}

zap_log_fn_t z_rdma_log_fn = __default_log_fn;
static int ZAP_RDMA_MAX_PD = 16;

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
	int ce_id; /* context entry ID */
};
static int next_ce_id = 0;
struct rbt context_tree = { 0, context_cmp };
pthread_mutex_t context_tree_lock = PTHREAD_MUTEX_INITIALIZER;
static struct context_tree_entry *__rdma_get_ce(struct z_rdma_ep *rep)
{
	struct ibv_pd *pd = NULL;
	struct context_tree_entry *ce;
	struct rbn *rbn;
	struct ibv_context *context = rep->cm_id->verbs;

	pthread_mutex_lock(&context_tree_lock);
	rbn = rbt_find(&context_tree, context);
	if (rbn) {
		/* The ce already exist */
		ce = container_of(rbn, struct context_tree_entry, rbn);
		goto out;
	}
	ce = calloc(1, sizeof(*ce));
	if (!ce) {
		LOG("zap_rdma: Out of memory\n");
		goto out;
	}

	pd = ibv_alloc_pd(context);
	if (!pd) {
		LOG("zap_rdma: ibv_alloc_pd failed\n");
		free(ce);
		ce = NULL;
		goto out;
	}
	ce->pd = pd;
	ce->context = context;
	ce->ce_id = next_ce_id++;
	if (ZAP_RDMA_MAX_PD < ce->ce_id) {
		LOG("zap_rdma: the number of ibv_context (verbs) is "
				"more than the maximum number of PD. Please set "
				"the ZAP_RDMA_MAX_PD environment variable to "
				"the number of RDMA verbs on the machine.\n");
		ibv_dealloc_pd(pd);
		free(ce);
		ce = NULL;
		goto out;
	}
	assert(ce->ce_id < ZAP_RDMA_MAX_PD);
	rbn_init(&ce->rbn, context);
	rbt_ins(&context_tree, &ce->rbn);
 out:
	pthread_mutex_unlock(&context_tree_lock);
	return ce;
}

LIST_HEAD(ep_list, z_rdma_ep) ep_list;

static int z_rdma_fill_rq(struct z_rdma_ep *ep);
static int cq_event_handler(struct ibv_cq *cq, int count);
static void _rdma_context_free(struct z_rdma_context *ctxt);
static void __rdma_buffer_free(struct z_rdma_buffer *rbuf);
static int send_credit_update(struct z_rdma_ep *ep);
static void _rdma_deliver_disconnected(struct z_rdma_ep *rep);
static void process_write_wc(struct z_rdma_ep *rep, struct ibv_wc *wc, void *usr_context);
static void process_read_wc(struct z_rdma_ep *rep, struct ibv_wc *wc, void *usr_context);
static zap_err_t submit_wr(struct z_rdma_ep *rep, struct z_rdma_context *ctxt,
			   int is_rdma);
static int get_credits(struct z_rdma_ep *rep, int is_rdma);
static struct z_rdma_map *z_rdma_map_get(zap_map_t map);
static struct ibv_mr *z_rdma_mr_get(zap_map_t map, zap_ep_t ep);

static void flush_io_q(struct z_rdma_ep *rep);
static void z_rdma_handle_cq_event(struct z_rdma_epoll_ctxt *ctxt);

static struct z_rdma_buffer_pool * __rdma_buffer_pool_alloc(struct z_rdma_ep *rep);
static inline void __rdma_buffer_pool_free(struct z_rdma_buffer_pool *p);
static struct z_rdma_buffer * __rdma_buffer_alloc(struct z_rdma_ep *rep);
static inline void __rdma_buffer_free(struct z_rdma_buffer *rb);

/* Turn the given file status flags on */
__attribute__((unused))
static int fd_flags_on(int fd, int flags)
{
	int fl, rc;
	fl = fcntl(fd, F_GETFL);
	if (fl == -1) {
		LOG("fcntl(F_GETFL) failed, errno: %d\n", errno);
		return errno;
	}
	rc = fcntl(fd, F_SETFL, fl | flags);
	if (rc == -1) {
		LOG("fcntl(F_SETFL) failed, errno: %d\n", errno);
		return errno;
	}
	return 0;
}

/* Turn the given file status flags off */
__attribute__((unused))
static int fd_flags_off(int fd, int flags)
{
	int fl, rc;
	fl = fcntl(fd, F_GETFL);
	if (fl == -1) {
		LOG("fcntl(F_GETFL) failed, errno: %d\n", errno);
		return errno;
	}
	rc = fcntl(fd, F_SETFL, fl & ~flags);
	if (rc == -1) {
		LOG("fcntl(F_SETFL) failed, errno: %d\n", errno);
		return errno;
	}
	return 0;
}

/* manually flush outstanding active requests. This is rerequired on hfi1
 * hardware. */
__attribute__((unused))
static void __flush_active_context(struct z_rdma_ep *rep)
{
	/* NOTE: No need to lock the ep as we are in the clean up path and no
	 * other threads can access the context list. The application cannot
	 * post new requests as the operations are guarded by the endpoint
	 * state. */
	struct z_rdma_context *ctxt, *next_ctxt;
	struct ibv_wc wc = { .status = IBV_WC_WR_FLUSH_ERR };
	if (LIST_EMPTY(&rep->active_ctxt_list)) {
		DLOG("rep %p active context list empty\n", rep);
	} else {
		DLOG("rep %p flushing outstanding requests\n", rep);
	}
	ctxt = LIST_FIRST(&rep->active_ctxt_list);
	while (ctxt) {
		next_ctxt = LIST_NEXT(ctxt, active_ctxt_link);
		if (ctxt->is_pending)
			goto next; /* skip the pending ones */
		switch (ctxt->op) {
		case IBV_WC_RDMA_READ:
			process_read_wc(rep, &wc, ctxt->usr_context);
			break;
		case IBV_WC_RDMA_WRITE:
			process_write_wc(rep, &wc, ctxt->usr_context);
			break;
		case IBV_WC_SEND:
		case IBV_WC_RECV:
			if (ctxt->rb)
				__rdma_buffer_free(ctxt->rb);
			break;
		default:
			assert(0 == "Invalid type");
			break;
		}
		__rdma_context_free(ctxt);
	next:
		ctxt = next_ctxt;
	}
}

static struct z_rdma_context *__rdma_get_context(struct ibv_wc *wc)
{
	return (struct z_rdma_context *)(unsigned long)wc->wr_id;
}

#define RDMA_SET_CONTEXT(__wr, __ctxt) \
	(__wr)->wr_id = (uint64_t)(unsigned long)(__ctxt);

static int __enable_cq_events(struct z_rdma_ep *rep)
{
	/* handle CQ events */
	z_rdma_io_thread_t thr = Z_RDMA_EP_THR(rep);
	struct epoll_event cq_event;
	cq_event.data.ptr = &rep->cq_ctxt;
	cq_event.events = EPOLLIN;

	/* Release when deleting the cq_channel fd from the epoll */
	ref_get(&rep->ep.ref, "enable cq events");
	DLOG("adding cq_channel %p fd %d (rep %p)\n", rep->cq_channel, rep->cq_channel->fd, rep);
	if (epoll_ctl(thr->efd, EPOLL_CTL_ADD, rep->cq_channel->fd, &cq_event)) {
		LOG("RMDA: epoll_ctl CTL_ADD failed, "
				"cq_channel %p fd %d rep %p\n",
				rep->cq_channel, rep->cq_channel->fd, rep);
		ref_put(&rep->ep.ref, "enable cq events"); /* Taken before adding cq_channel fd to epoll*/
		return errno;
	}
	rep->cq_channel_enabled = 1;

	return 0;
}

static int __disable_cq_events(struct z_rdma_ep *rep)
{
	z_rdma_io_thread_t thr = Z_RDMA_EP_THR(rep);
	struct epoll_event ignore;
	if (!rep->cq_channel_enabled)
		return ENOENT;
	DLOG("removing cq_channel %p fd %d (rep %p)\n",
			rep->cq_channel, rep->cq_channel->fd, rep);
	if (epoll_ctl(thr->efd, EPOLL_CTL_DEL, rep->cq_channel->fd, &ignore)) {
		LOG("RMDA: epoll_ctl CTL_ADD failed, "
				"cq_channel %p fd %d rep %p\n",
				rep->cq_channel, rep->cq_channel->fd, rep);
		return errno;
	}
	rep->cq_channel_enabled = 0;
	ref_put(&rep->ep.ref, "enable cq events"); /* taken in __enable_cq_events() */
	return 0;
}

static int __ep_flush(struct z_rdma_ep *rep)
{
	/* Flushes the outstanding operations. This function is called when the
	 * endpoint is in a bad state (rejected, connection errors, or
	 * disconnected). */
	int ret;
	struct ibv_cq *ev_cq;
	void *ev_ctx;

	/* disable cq events */
	__disable_cq_events(rep);

	/* clear event from cq channel */
	if (rep->cq_channel) {
		ret = ibv_get_cq_event(rep->cq_channel, &ev_cq, &ev_ctx);
		if (0 == ret)
			ibv_ack_cq_events(ev_cq, 1);
	}
	/* process remaining cq events */
	do {
		ret = 0;
		if (rep->sq_cq) {
			ret += cq_event_handler(rep->sq_cq, SQ_DEPTH+2);
		}
		if (rep->rq_cq) {
			ret += cq_event_handler(rep->rq_cq, SQ_DEPTH+2);
		}
	} while (ret);

	/* flush remaining outstanding requests that HW failed to deliver
	 * completions (this happened to hfi1). */
	__flush_active_context(rep);

	/* flush pending requests */
	pthread_mutex_lock(&rep->credit_lock);
	__flush_io_q(rep);
	pthread_mutex_unlock(&rep->credit_lock);
	return 0;
}

static void __rdma_teardown_conn(struct z_rdma_ep *ep)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;

	DLOG("rep %p tearing down\n", rep);

	if (rep->cm_id && rep->ep.state == ZAP_EP_CONNECTED)
		assert(0);

	errno = 0;

	/* Destroy the QP */
	if (rep->qp) {
		DLOG("destroying qp %p cm_id %p (rep %p)\n", rep->qp, rep->cm_id, rep);
		rdma_destroy_qp(rep->cm_id);
		rep->qp = NULL;
	}

	/* Destroy the RQ CQ */
	if (rep->rq_cq) {
		DLOG("destroying rq_cq %p (rep %p)\n", rep->rq_cq, rep);
		if (ibv_destroy_cq(rep->rq_cq))
			LOG("RDMA: Error %d : ibv_destroy_cq failed\n",
			     errno);
		rep->rq_cq = NULL;
	}
	/* Destroy the SQ CQ */
	if (rep->sq_cq) {
		DLOG("destroying sq_cq %p (rep %p)\n", rep->sq_cq, rep);
		if (ibv_destroy_cq(rep->sq_cq))
			LOG("RDMA: Error %d : ibv_destroy_cq failed\n",
			     errno);
		rep->sq_cq = NULL;
	}
	/* Destroy the CM id */
	if (rep->cm_id) {
		DLOG("destroying cm_id %p (rep %p)\n", rep->cm_id, rep);
		if (rdma_destroy_id(rep->cm_id))
			LOG("RDMA: Error %d : rdma_destroy_id failed\n",
			     errno);
		rep->cm_id = NULL;
	}

	assert(0 == rep->cq_channel_enabled);

	if (rep->cq_channel) {
		DLOG("destroying cq_channel %p (rep %p)\n", rep->cq_channel, rep);
		if (ibv_destroy_comp_channel(rep->cq_channel))
			LOG("RDMA: Error %d : "
			    "ibv_destroy_comp_channel failed\n", errno);
		rep->cq_channel = NULL;
	}

	struct z_rdma_buffer_pool *p;
	while ((p = LIST_FIRST(&rep->vacant_pool))) {
		LIST_REMOVE(p, pool_link);
		__rdma_buffer_pool_free(p);
	}
	while ((p = LIST_FIRST(&rep->full_pool))) {
		LIST_REMOVE(p, pool_link);
		__rdma_buffer_pool_free(p);
	}

	rep->rem_rq_credits = RQ_DEPTH;
	rep->sq_credits = SQ_DEPTH;
	rep->lcl_rq_credits = 0;
	DLOG("rep %p DONE\n", rep);
}

static void z_rdma_destroy(zap_ep_t zep)
{
	struct z_rdma_ep *rep = (void*)zep;
	if (zep->thread)
		zap_io_thread_ep_release(zep);
	pthread_mutex_lock(&rep->ep.lock);
	__rdma_teardown_conn(rep);
	pthread_mutex_unlock(&rep->ep.lock);
	if (rep->parent_ep)
		ref_put(&rep->parent_ep->ep.ref, "new child ep");
	DLOG("rep: %p freed\n", rep);
	free(rep);
}

/* caller must HOLD rep->ep.lock */
static int __buf_pool_init(struct z_rdma_ep *rep)
{
	struct z_rdma_buffer_pool *p;

	LIST_INIT(&rep->vacant_pool);
	LIST_INIT(&rep->full_pool);
	p = __rdma_buffer_pool_alloc(rep);
	if (!p)
		return ENOMEM;
	LIST_INSERT_HEAD(&rep->vacant_pool, p, pool_link);
	return 0;
}

/* caller must hold rep->ep.lock */
static int __rdma_setup_conn(struct z_rdma_ep *rep)
{
	struct ibv_qp_init_attr qp_attr;
	int ret = -ENOMEM;
	struct context_tree_entry *ce;

	/* Get-or-Allocate PD */
	ce = __rdma_get_ce(rep);
	if (!ce)
		goto err_0;
	rep->pd = ce->pd;
	rep->ce_id = ce->ce_id;

	/* Create completion channel */
	rep->cq_channel = ibv_create_comp_channel(rep->cm_id->verbs);
	if (!rep->cq_channel) {
		LOG("RDMA: __get_comp_channel() failed\n");
		goto err_0;
	}
	DLOG("using cq_channel %p (rep %p)\n", rep->cq_channel, rep);

	ret = fd_flags_on(rep->cq_channel->fd, O_NONBLOCK);
	if (ret) {
		LOG("RDMA: fd_flags_on(cq_channel, O_NONBLOCK) failed, "
		    "rc: %d\n", ret);
		goto err_0;
	}

	/* Create a new Send Queue CQ. */
	rep->sq_cq = ibv_create_cq(rep->cm_id->verbs,
				   SQ_DEPTH + 2,
				   rep, rep->cq_channel, 0);
	if (!rep->sq_cq) {
		LOG("RDMA: ibv_create_cq failed\n");
		goto err_0;
	}
	DLOG("created sq_cq %p (rep %p)\n", rep->sq_cq, rep);

	/* Create a new Receive Queue CQ. */
	rep->rq_cq = ibv_create_cq(rep->cm_id->verbs,
				   RQ_DEPTH + 2,
				   rep, rep->cq_channel, 0);
	if (!rep->rq_cq) {
		LOG("RDMA: ibv_create_cq failed\n");
		goto err_0;
	}
	DLOG("created rq_cq %p (rep %p)\n", rep->rq_cq, rep);

	ret = ibv_req_notify_cq(rep->rq_cq, 0);
	if (ret) {
		LOG("RMDA: ibv_create_cq failed\n");
		goto err_0;
	}

	ret = ibv_req_notify_cq(rep->sq_cq, 0);
	if (ret) {
		LOG("RMDA: ibv_create_cq failed\n");
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
		LOG("RDMA: rdma_create_qp failed\n");
		goto err_0;
	}
	DLOG("created qp %p (rep %p)\n", rep->qp, rep);

	ret = __buf_pool_init(rep);
	if (ret)
		goto err_0;

	ret = z_rdma_fill_rq(rep);
	if (ret)
		goto err_0;

	return 0;

err_0:
	rep->ep.state = ZAP_EP_ERROR;
	return ret;
}

static struct z_rdma_buffer_pool * __rdma_buffer_pool_alloc(struct z_rdma_ep *rep)
{
	/* only allocate + initialize pool. Do not put into any list */
	int i;
	struct z_rdma_buffer *rb;
	struct z_rdma_buffer_pool *p = malloc(sizeof(*p));
	if (!p)
		return NULL;

	p->num_alloc = 0;

	/* need IBV_ACCESS_LOCAL_WRITE for RECV */
	p->mr = ibv_reg_mr(rep->pd, p->buffer, sizeof(p->buffer), IBV_ACCESS_LOCAL_WRITE);
	if (!p->mr) {
		LOGL("ibv_reg_mr() failed, errno: %d\n", errno);
		free(p);
		return NULL;
	}
	LIST_INIT(&p->free_buffer_list);
	for (i = 0; i < Z_RDMA_POOL_SZ; i++) {
		rb = &p->buffer[i];
		rb->pool = p;
		rb->rep = NULL;
		rb->data_len = 0;
		LIST_INSERT_HEAD(&p->free_buffer_list, rb, free_link);
	}
	return p;
}

static inline void __rdma_buffer_pool_free(struct z_rdma_buffer_pool *p)
{
	/* caller already removed the pool from the list */
	int rc;
	if (p->mr) {
		rc = ibv_dereg_mr(p->mr);
		if (rc)
			LOGL("ibv_dereg_mr() failed, rc: %d, errno: %d\n", rc, errno);
	}
	free(p);
}

/* caller must HOLD rep->ep.lock */
static struct z_rdma_buffer * __rdma_buffer_alloc(struct z_rdma_ep *rep)
{
	struct z_rdma_buffer *rb;
	struct z_rdma_buffer_pool *p;
	p = LIST_FIRST(&rep->vacant_pool);
	if (!p) {
		p = __rdma_buffer_pool_alloc(rep);
		if (!p)
			return NULL;
		LIST_INSERT_HEAD(&rep->vacant_pool, p, pool_link);
	}
	rb = LIST_FIRST(&p->free_buffer_list);
	LIST_REMOVE(rb, free_link);
	rb->data_len = 0;
	rb->rep = rep;
	p->num_alloc++;
	if (LIST_EMPTY(&p->free_buffer_list)) {
		/* move to list of full pools */
		LIST_REMOVE(p, pool_link);
		LIST_INSERT_HEAD(&rep->full_pool, p, pool_link);
	}
	return rb;
}

/* caller must HOLD rep->ep.lock */
static inline void __rdma_buffer_free(struct z_rdma_buffer *rb)
{
	struct z_rdma_ep *rep = rb->rep;
	struct z_rdma_buffer_pool *p = rb->pool;
	LIST_INSERT_HEAD(&rb->pool->free_buffer_list, rb, free_link);
	p->num_alloc--;
	switch (p->num_alloc) {
	case 0: /* total vacancy ... free the pool */
		LIST_REMOVE(p, pool_link);
		__rdma_buffer_pool_free(p);
		break;
	case Z_RDMA_POOL_SZ - 1: /* the pool was full, now it has a vacancy */
		LIST_REMOVE(p, pool_link);
		LIST_INSERT_HEAD(&rep->vacant_pool, p, pool_link);
		break;
	}
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
	ref_get(&rep->ep.ref, "_rdma_context_alloc");
	LIST_INSERT_HEAD(&rep->active_ctxt_list, ctxt, active_ctxt_link);
	return ctxt;
}

/* Must be called with the endpoint lock held */
static void _rdma_context_free(struct z_rdma_context *ctxt)
{
	assert(ctxt->is_pending == 0); /* must not be in io_q */
	LIST_REMOVE(ctxt, active_ctxt_link);
	ref_put(&ctxt->ep->ref, "_rdma_context_alloc");
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
	__atomic_fetch_add(&rep->ep.sq_sz, 1, __ATOMIC_SEQ_CST);
	if (rep->ep.thread)
		__atomic_fetch_add(&rep->ep.thread->stat->sq_sz, 1, __ATOMIC_SEQ_CST);
	ctxt->is_pending = 1;
	return 0;
}

/* Must be called with the credit lock held */
/* rep->ep.lock is not held */
static void flush_io_q(struct z_rdma_ep *rep)
{
	struct z_rdma_context *ctxt;
	struct zap_event ev = {
		.status = ZAP_ERR_FLUSH,
	};
	enum z_rdma_message_type msg_type;
	while (!TAILQ_EMPTY(&rep->io_q)) {
		ctxt = TAILQ_FIRST(&rep->io_q);
		TAILQ_REMOVE(&rep->io_q, ctxt, pending_link);
		if (rep->ep.thread)
			__atomic_fetch_sub(&rep->ep.thread->stat->sq_sz, 1, __ATOMIC_SEQ_CST);
		__atomic_fetch_sub(&rep->ep.sq_sz, 1, __ATOMIC_SEQ_CST);
		ctxt->is_pending = 0;
		switch (ctxt->op) {
		case IBV_WC_SEND:
			msg_type = ntohs(ctxt->rb->msg->hdr.msg_type);
			if (Z_RDMA_MSG_SEND == msg_type) {
				ev.type = ZAP_EVENT_SEND_COMPLETE;
			} else if (Z_RDMA_MSG_SEND_MAPPED == msg_type){
				ev.type = ZAP_EVENT_SEND_MAPPED_COMPLETE;
			} else {
				/*
				 * In this case, Zap delivers a complete event
				 * only for send or send_mapped.
				 */
				goto free;
			}
			ev.context = ctxt->usr_context;
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
		pthread_mutex_lock(&rep->ep.lock);
		if (ctxt->rb)
			__rdma_buffer_free(ctxt->rb);
		__rdma_context_free(ctxt);
		pthread_mutex_unlock(&rep->ep.lock);
	}
	pthread_cond_signal(&rep->io_q_cond);

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
		DLOG("ibv_post_send() failed, rc %d rep %p\n", rc, rep);
		DLOG("%s: error %d posting send is_rdma %d sq_credits %d "
		     "lcl_rq_credits %d rem_rq_credits %d.\n", __func__,
		     rc, is_rdma, rep->sq_credits, rep->lcl_rq_credits,
		     rep->rem_rq_credits);
		zrc = ZAP_ERR_TRANSPORT;
	} else {
		DLOG("ibv_post_send() succeeded, rep %p\n", rep);
	}
	return zrc;
}

static zap_err_t submit_wr(struct z_rdma_ep *rep, struct z_rdma_context *ctxt,
			   int is_rdma)
{
	struct ibv_send_wr *bad_wr;
	int rc;
	pthread_mutex_lock(&rep->credit_lock);
	if (!get_credits(rep, is_rdma)) {
		rc = post_send(rep, ctxt, &bad_wr, is_rdma);
		if (rc)
			LOG("RDMA: post_send failed: code %d\n", errno);
	} else {
		rc = queue_io(rep, ctxt);
	}
	pthread_mutex_unlock(&rep->credit_lock);
	if (rc)
		return ZAP_ERR_RESOURCE;
	return ZAP_ERR_OK;
}

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
 *
 * rep->ep.lock is not held
 */
static void submit_pending(struct z_rdma_ep *rep)
{
	struct ibv_send_wr *badwr;
	struct z_rdma_context *ctxt;
	int is_rdma;
	int rc;
	enum z_rdma_message_type msg_type;

	pthread_mutex_lock(&rep->credit_lock);
	while (!TAILQ_EMPTY(&rep->io_q)) {
		ctxt = TAILQ_FIRST(&rep->io_q);
		is_rdma = (ctxt->op != IBV_WC_SEND);
		if (_get_credits(rep, is_rdma))
			goto out;

		TAILQ_REMOVE(&rep->io_q, ctxt, pending_link);
		__atomic_fetch_sub(&rep->ep.sq_sz, 1, __ATOMIC_SEQ_CST);
		if (rep->ep.thread)
			__atomic_fetch_sub(&rep->ep.thread->stat->sq_sz, 1, __ATOMIC_SEQ_CST);
		ctxt->is_pending = 0;

		rc = post_send(rep, ctxt, &badwr, is_rdma);
		if (!rc)
			continue;
		/* otherwise, deliver completion with error status */
		pthread_mutex_unlock(&rep->credit_lock);
		LOG("Error posting queued I/O. post_send() error %d "
		    "rep %p ctxt %p\n", rc, rep, ctxt);
		struct zap_event ev = { .status = ZAP_ERR_FLUSH, };
		switch (ctxt->op) {
		case IBV_WC_SEND:
			msg_type = ntohs(ctxt->rb->msg->hdr.msg_type);
			if (Z_RDMA_MSG_SEND == msg_type) {
				ev.type = ZAP_EVENT_SEND_COMPLETE;
			} else if (Z_RDMA_MSG_SEND_MAPPED == msg_type) {
				ev.type = ZAP_EVENT_SEND_MAPPED_COMPLETE;
			} else {
				/*
				 * In this case, Zap delivers a complete event
				 * only for send or send_mapped.
				 */
				goto free;
			}
			ev.context = ctxt->usr_context;
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
		pthread_mutex_lock(&rep->ep.lock);
		if (ctxt->rb)
			__rdma_buffer_free(ctxt->rb);
		__rdma_context_free(ctxt);
		pthread_mutex_unlock(&rep->ep.lock);
		pthread_mutex_lock(&rep->credit_lock);
	}
	pthread_cond_signal(&rep->io_q_cond);
 out:
	pthread_mutex_unlock(&rep->credit_lock);
}

/* caller must NOT hold rep->ep.lock */
static zap_err_t __rdma_post_send(struct z_rdma_ep *rep, struct z_rdma_buffer *rbuf)
{
	int rc;

	pthread_mutex_lock(&rep->ep.lock);
	struct z_rdma_context *ctxt =
		__rdma_context_alloc(rep, NULL, IBV_WC_SEND, rbuf);
	if (!ctxt) {
		errno = ENOMEM;
		pthread_mutex_unlock(&rep->ep.lock);
		return ZAP_ERR_RESOURCE;
	}
	pthread_mutex_unlock(&rep->ep.lock);

	ctxt->sge[0].addr = (uint64_t)rbuf->msg->bytes;
	ctxt->sge[0].length = rbuf->data_len;
	assert(rep == rbuf->rep);
	ctxt->sge[0].lkey = rbuf->pool->mr->lkey;

	ctxt->wr.opcode = IBV_WR_SEND;
	ctxt->wr.next = NULL;
	ctxt->wr.send_flags = IBV_SEND_SIGNALED;
	ctxt->wr.sg_list = ctxt->sge;
	ctxt->wr.num_sge = 1;

	RDMA_SET_CONTEXT(&ctxt->wr, ctxt);

	rc = submit_wr(rep, ctxt, 0);
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
	pthread_t self = pthread_self();

	if (self != ep->thread->thread) {
		pthread_mutex_lock(&rep->credit_lock);
		while (!TAILQ_EMPTY(&rep->io_q)) {
			pthread_cond_wait(&rep->io_q_cond, &rep->credit_lock);
		}
		pthread_mutex_unlock(&rep->credit_lock);
	}

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

	zerr = zap_io_thread_ep_assign(ep); /* also assign rep->cm_channel */
	if (zerr)
		goto err_0;

	zerr = ZAP_ERR_RESOURCE;
	/* Create the connecting CM ID */
	rc = rdma_create_id(rep->cm_channel, &rep->cm_id, rep, RDMA_PS_TCP);
	if (rc)
		goto err_1;
	ref_get(&rep->ep.ref, "accept/connect"); /* Release when disconnected or conn error */

	/* Bind the provided address to the CM Id */
	rc = rdma_resolve_addr(rep->cm_id, NULL, sin, 2000);
	if (rc) {
		zerr = ZAP_ERR_ADDRESS;
		goto err_2;
	}
	return ZAP_ERR_OK;

 err_2:
	ref_put(&rep->ep.ref, "accept/connect");
 err_1:
	zap_ep_change_state(&rep->ep, ZAP_EP_CONNECTING, ZAP_EP_INIT);
	rep->cm_channel = NULL;
 err_0:
	return zerr;
}

/* caller must hold rep->ep.lock */
static int __rdma_post_recv(struct z_rdma_ep *rep, struct z_rdma_buffer *rb)
{
	struct ibv_recv_wr wr;
	struct ibv_sge sge;
	struct ibv_recv_wr *bad_wr;
	struct z_rdma_context *ctxt;
	int rc;

	switch (rep->ep.state) {
	case ZAP_EP_ERROR:
	case ZAP_EP_PEER_CLOSE:
	case ZAP_EP_CLOSE:
		rc = ZAP_ERR_ENDPOINT;
		goto out;
	default:
		/* no-op */;
	}

	ctxt = __rdma_context_alloc(rep, NULL, IBV_WC_RECV, rb);
	if (!ctxt) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}

	sge.addr = (uint64_t) rb->msg->bytes;
	sge.length = RQ_BUF_SZ;
	assert(rep == rb->rep);
	sge.lkey = rb->pool->mr->lkey;
	wr.sg_list = &sge;
	wr.next = NULL;
	wr.num_sge = 1;

	RDMA_SET_CONTEXT(&wr, ctxt);
	rc = ibv_post_recv(rep->qp, &wr, &bad_wr);
	if (rc) {
		LOG("ibv_post_recv() failed, rc %d, rep %p\n", rc, rep);
		__rdma_context_free(ctxt);
		rc = zap_errno2zerr(rc);
	} else {
		DLOG("ibv_post_recv() succeeded, rep %p\n", rep);
	}
out:
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
					struct z_rdma_context *ctxt)
{
	struct zap_event zev = {
			.context = ctxt->usr_context,
			.status = z_map_err(wc->status),
		};
	enum z_rdma_message_type msg_type;
	msg_type = ntohs(ctxt->rb->msg->hdr.msg_type);
	switch (msg_type) {
	case Z_RDMA_MSG_SEND:
		zev.type = ZAP_EVENT_SEND_COMPLETE;
		break;
	case Z_RDMA_MSG_SEND_MAPPED:
		zev.type = ZAP_EVENT_SEND_MAPPED_COMPLETE;
		break;
	default:
		/*
		 * Only send & send_mapped that Zap will need
		 * to deliver the complete event here.
		 */
		goto free;
	}
	/* send_mapped needs an application callback */
	rep->ep.cb(&rep->ep, &zev);
free:
	if (!wc->status && ctxt->rb) {
		pthread_mutex_lock(&rep->ep.lock);
		__rdma_buffer_free(ctxt->rb);
		pthread_mutex_unlock(&rep->ep.lock);
	}
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
	struct zap_map *map;
	struct z_rdma_map *zm;
	zap_err_t zerr;

	zm = calloc(1, Z_RDMA_MAP_SZ);
	if (!zm) {
		LOG("%s:%d: Out of memory\n");
		return;
	}
	zm->rkey = sh->rkey;

	zerr = zap_map(&map, (void *)(unsigned long)sh->va,
				ntohl(sh->len), ntohl(sh->acc));
	if (zerr){
		free(zm);
		LOG("%s:%d: Failed to create a map in %s (%s)\n",
			  __FILE__, __LINE__, __func__, __zap_err_str[zerr]);
		return;
	}
	map->type = ZAP_MAP_REMOTE;
	ref_get(&rep->ep.ref, "zap_map/rendezvous"); /* will be put in zap_unmap() */
	map->ep = &rep->ep;
	map->mr[ZAP_RDMA] = zm;

	memset(&zev, 0, sizeof zev);
	zev.type = ZAP_EVENT_RENDEZVOUS;
	zev.status = ZAP_ERR_OK;
	zev.map = map;
	zev.data_len = len - sizeof(*sh);
	if (zev.data_len)
		zev.data = (void*)sh->msg;
	rep->ep.cb(&rep->ep, &zev);
}

static void process_recv_wc(struct z_rdma_ep *rep, struct ibv_wc *wc,
			    struct z_rdma_buffer *rb)
{
	struct z_rdma_message_hdr *msg = &rb->msg->hdr;
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
	case Z_RDMA_MSG_SEND_MAPPED:
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
		LOG("%s(): Unknown message type '%d'\n",
				__func__, msg_type);
		assert(0);
	}

	pthread_mutex_lock(&rep->ep.lock);
	ret = __rdma_post_recv(rep, rb);
	if (ret) {
		__rdma_buffer_free(rb);
		pthread_mutex_unlock(&rep->ep.lock);
		goto out;
	}
	pthread_mutex_unlock(&rep->ep.lock);

	/* Credit updates are not counted */
	if (msg_type == Z_RDMA_MSG_CREDIT_UPDATE)
		goto out;

	pthread_mutex_lock(&rep->credit_lock);
	if (rep->lcl_rq_credits < RQ_DEPTH)
		rep->lcl_rq_credits ++;
	if (rep->lcl_rq_credits > (RQ_DEPTH >> 1)) {
		if (send_credit_update(rep))
			LOG("RDMA: credit update could not be sent.\n");
	}
	pthread_mutex_unlock(&rep->credit_lock);

 out:
	return;
 err_wrong_dsz:
	LOG("%s(): msg type '%d': Invalid data size.\n",
				__func__, msg_type);
	z_rdma_close(&rep->ep);
	return;
}

/* caller must hold rep->ep.lock */
static int z_rdma_fill_rq(struct z_rdma_ep *rep)
{
	int i;

	for (i = 0; i < RQ_DEPTH+2; i++) {
		struct z_rdma_buffer *rbuf = __rdma_buffer_alloc(rep);
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
	zap_free(&rep->ep); /* b/c zap_new() in handle_connect_request() */
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

	ref_get(&rep->ep.ref, "accept/connect"); /* Release when disconnected */

	ret = zap_io_thread_ep_assign(ep);
	if (ret)
		goto err_0;

	pthread_mutex_lock(&rep->ep.lock);
	ret = __rdma_setup_conn(rep);
	pthread_mutex_unlock(&rep->ep.lock);
	if (ret)
		goto err_0;

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
	ref_put(&rep->ep.ref, "accept/connect");
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
	memset(&wc, 0, sizeof(wc));

	while (count-- && ((rc = ibv_poll_cq(cq, 1, &wc)) == 1)) {
		struct z_rdma_context *ctxt = __rdma_get_context(&wc);
		zap_ep_t ep = ctxt->ep;
		poll_count++;

		if (wc.status) {
			DLOG("RDMA: cq_event_handler: endpoint %p, "
				"WR op %d '%s', wc.status '%s'\n", ep,
				ctxt->op, ibv_op_str(ctxt->op),
				err_msg[wc.status]);
			/*
			 * The cm_channel will deliver rejected/disconnected.
			 * The endpoint state will be changed then.
			 * After this point all send/write/read should return a
			 * synchronous error.
			 *
			 */
			if (wc.status != IBV_WC_WR_FLUSH_ERR) {
				LOG("RDMA: cq_event_handler: endpoint %p, "
					"WR op %d '%s', wc.status '%s', "
					"addr %p len %d lkey %p.\n",
					ep,
					ctxt->op, ibv_op_str(ctxt->op),
					err_msg[wc.status],
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
			process_send_wc(rep, &wc, ctxt);
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
			LOG("RDMA: Invalid completion\n");
		}

		pthread_mutex_lock(&rep->ep.lock);
		if (wc.status && ctxt->rb)
			__rdma_buffer_free(ctxt->rb);
		__rdma_context_free(ctxt);
		pthread_mutex_unlock(&rep->ep.lock);
	}
	return poll_count;
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
	rep->cq_ctxt.type = Z_RDMA_EPOLL_CQ;
	rep->cq_ctxt.cq_rep = rep;

	TAILQ_INIT(&rep->io_q);
	pthread_cond_init(&rep->io_q_cond, NULL);
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
	DLOG("addr_resolved cm_id %p (rep %p), device %s\n",
			rep->cm_id, rep, rep->cm_id->verbs->device->name);
	if (0 == strncasecmp("hfi1", rep->cm_id->verbs->device->name, 4)) {
		rep->dev_type = Z_RDMA_DEV_HFI1;
	}
	ret = rdma_resolve_route(cma_id, 2000);
	if (ret) {
		zev.type = ZAP_EVENT_CONNECT_ERROR;
		zev.status = ZAP_ERR_ADDRESS;
		zap_ep_change_state(&rep->ep, ZAP_EP_CONNECTING, ZAP_EP_ERROR);
		rep->ep.cb(&rep->ep, &zev);
		ref_put(&rep->ep.ref, "accept/connect");
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
		LOG("RDMA: zap version unsupported %hhu.%hhu.\n",
				conn_data->v.major, conn_data->v.minor);
		rdma_reject(cma_id, NULL, 0);
		return;
	}

	if (conn_data->data_len) {
		zev.data_len = conn_data->data_len;
		zev.data = (void*)conn_data->data;
	}

	new_ep = zap_new(rep->ep.z, rep->ep.cb);
	if (!new_ep) {
		LOG("RDMA: could not create a new"
		     "endpoint for a connection request.\n");
		rdma_reject(cma_id, NULL, 0);
		return;
	}
	void *ctxt = zap_get_ucontext(&rep->ep);
	zap_set_ucontext(new_ep, ctxt);
	new_rep = (struct z_rdma_ep *)new_ep;
	new_rep->cm_id = cma_id;
	new_rep->cm_channel = rep->cm_channel;
	/* NOTE: The connect-request endpoint uses the same cm_channel as the
	 *       listening endpoint. The connect-request endpoint will migrate
	 *       to the other cm_channel when it is accepted by the application
	 *       and get a thread assigned to it. */
	new_rep->parent_ep = rep;
	if (0 == strncasecmp("hfi1", new_rep->cm_id->verbs->device->name, 4)) {
		new_rep->dev_type = Z_RDMA_DEV_HFI1;
	} else {
		new_rep->dev_type = rep->dev_type;
	}
	ref_get(&rep->ep.ref, "new child ep"); /* Release when the new endpoint is destroyed */
	cma_id->context = new_rep;
	zap_ep_change_state(new_ep, ZAP_EP_INIT, ZAP_EP_ACCEPTING);
	DLOG("new passive rep %p cm_id %p, parent rep %p cm_id %p\n",
			new_ep, new_rep->cm_id, rep, rep->cm_id);
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
	pthread_mutex_lock(&rep->ep.lock);
	assert(rep->ep.state == ZAP_EP_CONNECTING);
	ret = __rdma_setup_conn(rep);
	pthread_mutex_unlock(&rep->ep.lock);
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
	zev.type = ZAP_EVENT_CONNECT_ERROR;
	zev.status = ZAP_ERR_ROUTE;
	rep->ep.state = ZAP_EP_ERROR;
	rep->ep.cb(&rep->ep, &zev);
	ref_put(&rep->ep.ref, "accept/connect");
}

static void
handle_conn_error(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id, int reason)
{
	struct zap_event zev = {0};
	zap_ep_state_t prev_state = rep->ep.state;
	zev.status = reason;
	rep->ep.state = ZAP_EP_ERROR;
	__ep_flush(rep);
	switch (prev_state) {
	case ZAP_EP_ACCEPTING:
		/* Passive side. */
		switch (rep->conn_req_decision) {
		case Z_RDMA_PASSIVE_ACCEPT:
			/*
			 * App accepted the conn req already.
			 * Deliver the error.
			 */
			zev.type = ZAP_EVENT_CONNECT_ERROR;
			rep->ep.cb(&rep->ep, &zev);
			ref_put(&rep->ep.ref, "accept/connect");
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
			LOG("Unrecognized connection request "
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
		ref_put(&rep->ep.ref, "accept/connect");
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
	DLOG("rejected rep %p\n", rep);
	zev.status = ZAP_ERR_CONNECT;

	/* State before being rejected is CONNECTING, but
	 * cq thread (we're on cm thread) can race and change endpoint state to
	 * ERROR from the posted recv. */
	assert(rep->ep.state == ZAP_EP_CONNECTING ||
			rep->ep.state == ZAP_EP_ERROR ||
			rep->ep.state == ZAP_EP_ACCEPTING);

	if (rep->ep.state == ZAP_EP_ACCEPTING) {
		/* passive side. No need to look into the rejected message */
		/* NOTE: REJECTED event occurred on the passive side when the
		 *       pasive side had accepted the connection, but then an
		 *       error occurred before the ESTABLISHED event. In
		 *       other words, the REJECTED on the passive side is a
		 *       CONN_ERROR event. */
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

	rep->ep.state = ZAP_EP_ERROR;
	__ep_flush(rep);

	rep->ep.cb(&rep->ep, &zev);
	ref_put(&rep->ep.ref, "accept/connect");
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
		DLOG("Error: handle_established: ep %p, "
				"unexpected state '%s'\n",
				rep, __zap_ep_state_str(rep->ep.state));
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

	DLOG("established rep %p\n", rep);

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
	ref_put(&rep->ep.ref, "accept/connect");
}

static void
handle_disconnected(struct z_rdma_ep *rep, struct rdma_cm_id *cma_id)
{
	DLOG("RDMA: handle_disconnected %p, state '%s'\n",
			rep, __zap_ep_state_str(rep->ep.state));
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
		LOG("RDMA: multiple disconnects on the same endpoint.\n");
		break;
	case ZAP_EP_LISTENING:
		break;
	default:
		LOG("RDMA: unexpected disconnect in state %d.\n",
		     rep->ep.state);
		assert(0);
		break;
	}
	pthread_mutex_unlock(&rep->ep.lock);
	__ep_flush(rep);
	__rdma_deliver_disconnected(rep);
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
		LOG("RDMA: Unhandled event %s, ignoring\n",
		     rdma_event_str(event->event));
		break;
	}
}

const char *_rdma_cm_event_type_str[] = {
	[RDMA_CM_EVENT_ADDR_RESOLVED]     =  "RDMA_CM_EVENT_ADDR_RESOLVED",
	[RDMA_CM_EVENT_ADDR_ERROR]        =  "RDMA_CM_EVENT_ADDR_ERROR",
	[RDMA_CM_EVENT_ROUTE_RESOLVED]    =  "RDMA_CM_EVENT_ROUTE_RESOLVED",
	[RDMA_CM_EVENT_ROUTE_ERROR]       =  "RDMA_CM_EVENT_ROUTE_ERROR",
	[RDMA_CM_EVENT_CONNECT_REQUEST]   =  "RDMA_CM_EVENT_CONNECT_REQUEST",
	[RDMA_CM_EVENT_CONNECT_RESPONSE]  =  "RDMA_CM_EVENT_CONNECT_RESPONSE",
	[RDMA_CM_EVENT_CONNECT_ERROR]     =  "RDMA_CM_EVENT_CONNECT_ERROR",
	[RDMA_CM_EVENT_UNREACHABLE]       =  "RDMA_CM_EVENT_UNREACHABLE",
	[RDMA_CM_EVENT_REJECTED]          =  "RDMA_CM_EVENT_REJECTED",
	[RDMA_CM_EVENT_ESTABLISHED]       =  "RDMA_CM_EVENT_ESTABLISHED",
	[RDMA_CM_EVENT_DISCONNECTED]      =  "RDMA_CM_EVENT_DISCONNECTED",
	[RDMA_CM_EVENT_DEVICE_REMOVAL]    =  "RDMA_CM_EVENT_DEVICE_REMOVAL",
	[RDMA_CM_EVENT_MULTICAST_JOIN]    =  "RDMA_CM_EVENT_MULTICAST_JOIN",
	[RDMA_CM_EVENT_MULTICAST_ERROR]   =  "RDMA_CM_EVENT_MULTICAST_ERROR",
	[RDMA_CM_EVENT_ADDR_CHANGE]       =  "RDMA_CM_EVENT_ADDR_CHANGE",
	[RDMA_CM_EVENT_TIMEWAIT_EXIT]     =  "RDMA_CM_EVENT_TIMEWAIT_EXIT"
};
__attribute__((unused))
static const char * rdma_cm_event_type_str(enum rdma_cm_event_type type)
{
	if (type <= RDMA_CM_EVENT_TIMEWAIT_EXIT)
		return _rdma_cm_event_type_str[type];
	return "UNKNOWN_RDMA_CM_EVENT";
}

static zap_err_t
z_rdma_listen(zap_ep_t ep, struct sockaddr *sin, socklen_t sa_len)
{
	zap_err_t zerr;
	int rc;
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	char host[1024] = "";
	char svc[256] = "";

	zerr = zap_ep_change_state(&rep->ep, ZAP_EP_INIT, ZAP_EP_LISTENING);
	if (zerr) {
		LOG("%s: %s failed. zerr %d\n", __FUNCTION__, "zap_ep_change_state", (int)zerr);
		goto out;
	}

	zerr = zap_io_thread_ep_assign(ep);
	if (zerr) {
		LOG("%s: thread assignment failed. zerr %d\n", __func__, zerr);
		goto err_0;
	}

	/* Create the event CM event channel */
	zerr = ZAP_ERR_RESOURCE;
	rep->cm_channel = Z_RDMA_EP_THR(ep)->cm_ctxt.cm_channel;

	/* Create the listening CM ID */
	rc = rdma_create_id(rep->cm_channel, &rep->cm_id, rep, RDMA_PS_TCP);
	if (rc) {
		LOG("%s: %s failed. zerr %d, rc %d\n", __FUNCTION__, "rdma_create_id", (int)zerr, rc);
		goto err_1;
	}

	/* Bind the provided address to the CM Id */
	zerr = ZAP_ERR_BUSY;
	rc = rdma_bind_addr(rep->cm_id, sin);
	if (rc) {
		LOG("%s: %s failed. zerr %d rc %d\n", __FUNCTION__, "rdma_bind_addr", (int)zerr, rc);
		goto err_2;
	}

	zerr = ZAP_ERR_RESOURCE;
	rc = rdma_listen(rep->cm_id, 3);
	if (rc) {
		LOG("%s: %s failed. zerr %d rc %d\n", __FUNCTION__, "rdma_listen", (int)zerr, rc);
		goto err_2;
	}
	getnameinfo(sin, sa_len, host, sizeof(host), svc, sizeof(svc),
			NI_NUMERICHOST|NI_NUMERICSERV);
	LOG("listening on device %s, addr %s:%s\n",
		rep->cm_id->verbs?rep->cm_id->verbs->device->name:"UNKNOWN",
		host, svc);

	return ZAP_ERR_OK;

 err_2:
	DLOG("destroying cm_id %p (rep %p)\n", rep->cm_id, rep);
	rdma_destroy_id(rep->cm_id);
	rep->cm_id = NULL;
 err_1:
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
	uint16_t credits;
	int rc;

	/* take the credits to send to peer */
	credits = rep->lcl_rq_credits;
	rep->lcl_rq_credits = 0;
	pthread_mutex_unlock(&rep->credit_lock);

	pthread_mutex_lock(&rep->ep.lock);

	rbuf = __rdma_buffer_alloc(rep);
	if (!rbuf) {
		rc = ENOMEM;
		pthread_mutex_unlock(&rep->ep.lock);
		goto out;
	}

	req = &rbuf->msg->hdr;
	req->credits = htons(credits);
	req->msg_type = htons(Z_RDMA_MSG_CREDIT_UPDATE);
	rbuf->data_len = sizeof(req);
	ctxt = __rdma_context_alloc(rep, NULL, IBV_WC_SEND, rbuf);
	pthread_mutex_unlock(&rep->ep.lock);
	pthread_mutex_lock(&rep->credit_lock);
	if (!ctxt) {
		__rdma_buffer_free(rbuf);
		rc = ENOMEM;
		goto out;
	}

	ctxt->sge[0].addr = (uint64_t)rbuf->msg->bytes;
	ctxt->sge[0].length = sizeof(*req);
	ctxt->sge[0].lkey = rbuf->pool->mr->lkey;

	memset(&ctxt->wr, 0, sizeof(ctxt->wr));
	ctxt->wr.sg_list = ctxt->sge;
	ctxt->wr.num_sge = 1;
	ctxt->wr.opcode = IBV_WR_SEND;
	ctxt->wr.send_flags = IBV_SEND_SIGNALED;

	RDMA_SET_CONTEXT(&ctxt->wr, ctxt);

	if ((rc = ibv_post_send(rep->qp, &ctxt->wr, &bad_wr))) {
		DLOG("ibv_post_send() failed, rc %d rep %p\n", rc, rep);
		LOG( "%s: sq_credits %d lcl_rq_credits %d "
		     "rem_rq_credits %d.\n",
		     __func__, rep->sq_credits, rep->lcl_rq_credits,
		     rep->rem_rq_credits);
		pthread_mutex_unlock(&rep->credit_lock);
		pthread_mutex_lock(&rep->ep.lock);
		__rdma_context_free(ctxt);
		__rdma_buffer_free(rbuf);
		pthread_mutex_unlock(&rep->ep.lock);
		pthread_mutex_lock(&rep->credit_lock);
		rc = ENOSPC;
		goto out;
	} else {
		credits = 0;
		DLOG("ibv_post_send() succeeded, rep %p\n", rep);
	}
	rc = 0;
out:
	if (credits) {
		/* credits has not been sent. put it back */
		rep->lcl_rq_credits += credits;
	}
	return rc;
}

static zap_err_t z_get_name(zap_ep_t ep, struct sockaddr *local_sa,
			    struct sockaddr *remote_sa, socklen_t *sa_len)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;

	memcpy(remote_sa, rdma_get_peer_addr(rep->cm_id), sizeof(struct sockaddr_in));
	memcpy(local_sa, rdma_get_local_addr(rep->cm_id), sizeof(struct sockaddr_in));
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

	rbuf = __rdma_buffer_alloc(rep);
	if (!rbuf) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}
	pthread_mutex_unlock(&rep->ep.lock);

	hdr = &rbuf->msg->hdr;
	hdr->msg_type = htons(Z_RDMA_MSG_SEND);

	/* copy the data, leaving room for the rdma header */
	memcpy(rbuf->msg->bytes+sizeof(struct z_rdma_message_hdr), buf, len);
	rbuf->data_len = len + sizeof(struct z_rdma_message_hdr);

	rc = __rdma_post_send(rep, rbuf);
	if (rc) {
		pthread_mutex_lock(&rep->ep.lock);
		__rdma_buffer_free(rbuf);
		pthread_mutex_unlock(&rep->ep.lock);
	}

	return rc;
 out:
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;
}

static zap_err_t z_rdma_send_mapped(zap_ep_t ep, zap_map_t map, void *buf,
				    size_t len, void *context)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct z_rdma_message_hdr *hdr;
	struct z_rdma_buffer *rbuf;
	int rc;
	struct z_rdma_context *ctxt;
	struct ibv_mr *mr;


	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc)
		goto err_0;

	mr = z_rdma_mr_get(map, ep); /* do not free mr */
	if (!mr)
		goto err_0;

	if (len > ep->z->max_msg) {
		rc = ZAP_ERR_NO_SPACE;
		goto err_0;
	}

	/* range check */
	if (z_map_access_validate(map, buf, len, 0)) {
		rc = ZAP_ERR_LOCAL_LEN;
		goto err_0;
	}

	rbuf = __rdma_buffer_alloc(rep);
	if (!rbuf) {
		rc = ZAP_ERR_RESOURCE;
		goto err_0;
	}

	ctxt = __rdma_context_alloc(rep, context, IBV_WC_SEND, rbuf);
	if (!ctxt) {
		rc = ZAP_ERR_RESOURCE;
		goto err_1;
	}
	pthread_mutex_unlock(&rep->ep.lock);

	/* NOTE: using 2 sge
	 *       sge[0] for the rbuf containing rdma msg header
	 *       sge[1] for the data payload
	 */

	hdr = &rbuf->msg->hdr;
	hdr->msg_type = htons(Z_RDMA_MSG_SEND_MAPPED);

	ctxt->sge[0].addr = (uint64_t)rbuf->msg->bytes;
	ctxt->sge[0].length = sizeof(*hdr);
	ctxt->sge[0].lkey = rbuf->pool->mr->lkey;

	ctxt->sge[1].addr = (uint64_t)buf;
	ctxt->sge[1].length = len;
	ctxt->sge[1].lkey = mr->lkey;

	ctxt->wr.opcode = IBV_WR_SEND;
	ctxt->wr.next = NULL;
	ctxt->wr.send_flags = IBV_SEND_SIGNALED;
	ctxt->wr.sg_list = ctxt->sge;
	ctxt->wr.num_sge = 2;

	RDMA_SET_CONTEXT(&ctxt->wr, ctxt);

	rc = submit_wr(rep, ctxt, 0);
	if (rc)
		goto err_2;

	return ZAP_ERR_OK;
 err_2:
	__rdma_context_free(ctxt);
 err_1:
	__rdma_buffer_free(rbuf);
 err_0:
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;

}

static zap_err_t z_rdma_share(zap_ep_t ep, zap_map_t map,
				const char *msg, size_t msg_len)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct z_rdma_share_msg *sm;
	struct z_rdma_buffer *rbuf;
	struct ibv_mr *mr;
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

	mr = z_rdma_mr_get(map, ep);
	if (!mr) {
		pthread_mutex_unlock(&rep->ep.lock);
		return ZAP_ERR_RESOURCE;
	}

	rbuf = __rdma_buffer_alloc(rep);
	if (!rbuf) {
		pthread_mutex_unlock(&rep->ep.lock);
		return ZAP_ERR_RESOURCE;
	}
	pthread_mutex_unlock(&rep->ep.lock);

	/* get the map */

	sm = &rbuf->msg->share;
	sm->hdr.msg_type = htons(Z_RDMA_MSG_RENDEZVOUS);
	sm->rkey = mr->rkey;
	sm->va = (unsigned long)map->addr;
	sm->len = htonl(map->len);
	sm->acc = htonl(map->acc);
	if (msg_len)
		memcpy(sm->msg, msg, msg_len);

	rbuf->data_len = sz;

	rc = __rdma_post_send(rep, rbuf);
	if (rc) {
		pthread_mutex_lock(&rep->ep.lock);
		__rdma_buffer_free(rbuf);
		pthread_mutex_unlock(&rep->ep.lock);
	}
	return rc;
}

static struct z_rdma_map *z_rdma_map_get(zap_map_t map)
{
	struct z_rdma_map *zm = map->mr[ZAP_RDMA];
	if (zm)
		return zm;
	zm = calloc(1, Z_RDMA_MAP_SZ);
	if (!zm)
		return NULL;
	/* race with other thread to assign mr[ZAP_RDMA] */
	if (!__sync_bool_compare_and_swap(&map->mr[ZAP_RDMA], NULL, zm)) {
		/* the other thread won the race */
		free(zm);
	}
	return map->mr[ZAP_RDMA];
}

/* needs to call with ep->lock held */
static struct ibv_mr *z_rdma_mr_get(zap_map_t map, zap_ep_t ep)
{
	struct z_rdma_ep *rep = (struct z_rdma_ep *)ep;
	struct z_rdma_map *zm = z_rdma_map_get(map);
	int acc_flags;
	struct ibv_mr *mr;
	if (!zm)
		return NULL;
	mr = zm->mr[rep->ce_id];
	if (mr)
		return mr;
	acc_flags = (ZAP_ACCESS_WRITE & map->acc ? IBV_ACCESS_REMOTE_WRITE : 0);
	acc_flags |= (ZAP_ACCESS_READ & map->acc ? IBV_ACCESS_REMOTE_READ : 0);
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
	mr = zm->mr[rep->ce_id] = ibv_reg_mr(rep->pd, map->addr, map->len, acc_flags);
	return mr;
}

static zap_err_t z_rdma_unmap(zap_map_t map)
{
	struct z_rdma_map *zm = map->mr[ZAP_RDMA];
	int i;
	if (!zm) {
		/* map has never been used with rdma xprt */
		return 0;
	}
	for (i = 0 ; i < next_ce_id; i++) {
		if (zm->mr[i])
			ibv_dereg_mr(zm->mr[i]);
	}
	if ((map->type == ZAP_MAP_REMOTE) && map->ep)
		ref_put(&map->ep->ref, "zap_map/rendezvous");
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
	struct z_rdma_map *rmap = dst_map->mr[ZAP_RDMA];
	struct z_rdma_context *ctxt;
	struct ibv_mr *mr;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc)
		goto out;

	mr = z_rdma_mr_get(src_map, ep);
	if (!mr) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}

	ctxt = __rdma_context_alloc(rep, context, IBV_WC_RDMA_WRITE, NULL);
	if (!ctxt) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}

	ctxt->sge[0].addr = (unsigned long)src;
	ctxt->sge[0].length = sz;
	ctxt->sge[0].lkey = mr->lkey;

	memset(&ctxt->wr, 0, sizeof(ctxt->wr));
	ctxt->wr.sg_list = ctxt->sge;
	ctxt->wr.num_sge = 1;
	ctxt->wr.opcode = IBV_WR_RDMA_WRITE;
	ctxt->wr.send_flags = IBV_SEND_SIGNALED;
	ctxt->wr.wr.rdma.remote_addr = (unsigned long)dst;
	ctxt->wr.wr.rdma.rkey = rmap->rkey;

	RDMA_SET_CONTEXT(&ctxt->wr, ctxt);
	rc = submit_wr(rep, ctxt, 1);
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
	struct z_rdma_map *rmap = src_map->mr[ZAP_RDMA];
	struct z_rdma_context *ctxt;
	struct ibv_mr *mr;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc) {
		pthread_mutex_unlock(&rep->ep.lock);
		return rc;
	}

	mr = z_rdma_mr_get(dst_map, ep);
	if (!mr) {
		pthread_mutex_unlock(&rep->ep.lock);
		return ZAP_ERR_RESOURCE;
	}

	ctxt = __rdma_context_alloc(rep, context, IBV_WC_RDMA_READ, NULL);
	if (!ctxt) {
		pthread_mutex_unlock(&rep->ep.lock);
		return ZAP_ERR_RESOURCE;
	}

	ctxt->sge[0].addr = (unsigned long)dst;
	ctxt->sge[0].length = sz;
	ctxt->sge[0].lkey = mr->lkey;
	memset(&ctxt->wr, 0, sizeof(ctxt->wr));
	ctxt->wr.sg_list = ctxt->sge;
	ctxt->wr.num_sge = 1;
	ctxt->wr.opcode = IBV_WR_RDMA_READ;
	ctxt->wr.send_flags = IBV_SEND_SIGNALED;
	ctxt->wr.wr.rdma.remote_addr = (unsigned long)src;
	ctxt->wr.wr.rdma.rkey = rmap->rkey;

	RDMA_SET_CONTEXT(&ctxt->wr, ctxt);
	rc = submit_wr(rep, ctxt, 1);
	if (rc)
		__rdma_context_free(ctxt);
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;
}

static void z_rdma_handle_cm_event(z_rdma_io_thread_t thr,
					struct z_rdma_epoll_ctxt *ctxt)
{
	int rc;
	struct rdma_cm_event *event;
	struct z_rdma_ep *rep;
 again:
	rc = rdma_get_cm_event(ctxt->cm_channel, &event);
	if (rc)
		return;
	rep = event->id->context;
	DLOG("cm_event cm_id %p rep %p listen_id %p event %d %s "
	     "status %d \n", event->id, rep, event->listen_id, event->event,
	     rdma_cm_event_type_str(event->event), event->status);
	if (event->event != RDMA_CM_EVENT_TIMEWAIT_EXIT) {
		/*
		 * rep could be destroyed after ERROR or DISCONNECTED event.
		 * Hence, the TIMEWAIT_EXIT event tha comes after shall be
		 * ignored.
		 *
		 * On active endpoints, TIMEWAIT_EXIT does not have a chance to
		 * reach here because cm events are disabled right after ERROR
		 * or DISCONNECTED events.
		 *
		 * The passive endpoints (created from CONN_REQ) use the same
		 * cm_channel as the listening endpoint and we have no way to
		 * remove the passive endpoints from the channel. So,
		 * the TIMEWAIT_EXIT events can reach here.
		 */
		ref_get(&rep->ep.ref, __func__);
		cma_event_handler(rep, event->id, event);
		rdma_ack_cm_event(event);
		ref_put(&rep->ep.ref, __func__);
		/* need to put after rdma_ack_cm_event(), otherwise
		 * rdma_destroy_id() could be call in the reference put and
		 * causes a dead lock. */
	} else {
		rdma_ack_cm_event(event);
	}
	goto again;
}

static void z_rdma_handle_cq_event(struct z_rdma_epoll_ctxt *ctxt)
{
	int ret;
	struct ibv_cq *ev_cq;
	struct z_rdma_ep *rep = ctxt->cq_rep;
	void *ev_ctx;

	ret = ibv_get_cq_event(rep->cq_channel, &ev_cq, &ev_ctx);
	if (ret)
		return;
	assert( rep == ev_ctx );
	ref_get(&rep->ep.ref, __func__); /* Release after process the ev_cq */
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
		LOG("RDMA: Error %d at %s:%d\n", errno, __func__, __LINE__);
	}
	pthread_mutex_lock(&rep->ep.lock);
	if (LIST_EMPTY(&rep->active_ctxt_list)) {
		pthread_mutex_unlock(&rep->ep.lock);
	} else {
		pthread_mutex_unlock(&rep->ep.lock);
		submit_pending(rep);
	}
	ref_put(&rep->ep.ref, __func__); /* Taken when getting the ev_cq */
}

static void z_rdma_io_thread_cleanup(void *arg)
{
	z_rdma_io_thread_t thr = arg;
	if (thr->efd > -1)
		close(thr->efd);
	zap_io_thread_release(&thr->zap_io_thread);
	free(thr);
}

static void *z_rdma_io_thread_proc(void *arg)
{
	z_rdma_io_thread_t thr = arg;
	struct epoll_event events[16];
	int rc, n, i, n_cm;
	sigset_t sigset;
	struct z_rdma_epoll_ctxt *ctxt;
	struct z_rdma_epoll_ctxt *cm_ctxt[16];

	sigfillset(&sigset);
	rc = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	if (rc) {
		fprintf(stderr, "zap_rdma: ERROR: pthread_sigmask errno: %d\n",
				errno);
		assert(0);
		return NULL;
	}
	pthread_cleanup_push(z_rdma_io_thread_cleanup, arg);
 loop:
	zap_thrstat_wait_start(thr->zap_io_thread.stat);
	n = epoll_wait(thr->efd, events, 16, -1);
	zap_thrstat_wait_end(thr->zap_io_thread.stat);
	n_cm = 0;
	for (i = 0; i < n; i++) {
		ctxt = events[i].data.ptr;
		switch (ctxt->type) {
		case Z_RDMA_EPOLL_CM:
			cm_ctxt[n_cm++] = ctxt;
			/* cm events will be processed later */
			break;
		case Z_RDMA_EPOLL_CQ:
			switch (ctxt->cq_rep->ep.state) {
			case ZAP_EP_ACCEPTING:
			case ZAP_EP_CONNECTING:
				/* do not process cq if ep has never been
				 * established */
				break;
			default:
				z_rdma_handle_cq_event(ctxt);
			}
			break;
		default:
			LOG("Bad Zap RDMA EPOLL ctxt type: %d\n", ctxt->type);
			assert(0 == "bad type");
			break;
		}
	}
	/* process the cm events */
	for (i = 0; i < n_cm; i++) {
		z_rdma_handle_cm_event(thr, cm_ctxt[i]);
	}
	goto loop;

	pthread_cleanup_pop(1);
	return NULL;
}

zap_io_thread_t z_rdma_io_thread_create(zap_t z)
{
	int rc;
	struct epoll_event cm_event;
	z_rdma_io_thread_t thr;

	thr = calloc(1, sizeof(*thr));
	if (!thr)
		goto err_0;

	rc = zap_io_thread_init(&thr->zap_io_thread, z, "zap_rdma_io",
				ZAP_ENV_INT(ZAP_THRSTAT_WINDOW));
	if (rc)
		goto err_1;

	thr->efd = epoll_create1(O_CLOEXEC);
	if (thr->efd < 0)
		goto err_2;

	thr->cm_ctxt.type = Z_RDMA_EPOLL_CM;
	thr->cm_ctxt.cm_channel = rdma_create_event_channel();
	if (!thr->cm_ctxt.cm_channel)
		goto err_3;

	rc = fd_flags_on(thr->cm_ctxt.cm_channel->fd, O_NONBLOCK);
	if (rc)
		goto err_4;

	cm_event.events = EPOLLIN;
	cm_event.data.ptr = &thr->cm_ctxt;
	if (epoll_ctl(thr->efd, EPOLL_CTL_ADD, thr->cm_ctxt.cm_channel->fd, &cm_event))
		goto err_4;

	thr->devices_len = devices_len;

	rc = pthread_create(&thr->zap_io_thread.thread, NULL,
			    z_rdma_io_thread_proc, thr);
	if (rc)
		goto err_4;
	pthread_setname_np(thr->zap_io_thread.thread, "zap_rdma_io");
	return &thr->zap_io_thread;
 err_4:
	rdma_destroy_event_channel(thr->cm_ctxt.cm_channel);
 err_3:
	close(thr->efd);
 err_2:
	zap_thrstat_free(thr->zap_io_thread.stat);
 err_1:
	free(thr);
 err_0:
	errno = ZAP_ERR_RESOURCE;
	return NULL;
}

zap_err_t z_rdma_io_thread_cancel(zap_io_thread_t t)
{
	int rc;
	rc = pthread_cancel(t->thread);
	switch (rc) {
	case ESRCH: /* cleaning up structure w/o running thread b/c of fork */
		((z_rdma_io_thread_t)t)->efd = -1; /* b/c of CLOEXEC */
		z_rdma_io_thread_cleanup(t);
	case 0:
		return ZAP_ERR_OK;
	default:
		return ZAP_ERR_LOCAL_OPERATION;
	}
}

zap_err_t z_rdma_io_thread_ep_assign(zap_io_thread_t t, zap_ep_t ep)
{
	struct z_rdma_ep *rep = (void*)ep;
	struct z_rdma_io_thread *thr = (void*)t;
	int rc;
	if (rep->cm_id && rep->cm_channel != thr->cm_ctxt.cm_channel) {
		/* cm_id was created with different cm_channel.
		 * So, we need to migrate it to thr->cm_channel. */
		rc = rdma_migrate_id(rep->cm_id, thr->cm_ctxt.cm_channel);
		if (rc == -1)
			return zap_errno2zerr(errno);
	}
	/* NOTE: For active connections, cm_id is not created yet. */
	rep->cm_channel = thr->cm_ctxt.cm_channel;
	return ZAP_ERR_OK;
}

zap_err_t z_rdma_io_thread_ep_release(zap_io_thread_t t, zap_ep_t ep)
{
	/* Nothing to do. */
	return 0;
}

static int init_complete = 0;

static int init_once(zap_log_fn_t log_fn)
{
	const char *s;
	int num;
	z_rdma_log_fn = log_fn;

	/* obtain all devices */
	devices = rdma_get_devices(&devices_len);
	if (!devices)
		goto err_0;

	s = getenv("ZAP_RDMA_MAX_PD");
	if (s) {
		num = atoi(s);
		if (num)
			ZAP_RDMA_MAX_PD = num;
	}

	init_complete = 1;
	return 0;

 err_0:
	return 1;
}

zap_err_t zap_transport_get(zap_t *pz, zap_log_fn_t log_fn,
			    zap_mem_info_fn_t mem_info_fn)
{
	zap_t z;
	if (!init_complete) {
		if (init_once(log_fn)) {
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
	z->send_mapped = z_rdma_send_mapped;
	z->read = z_rdma_read;
	z->write = z_rdma_write;
	z->unmap = z_rdma_unmap;
	z->share = z_rdma_share;
	z->get_name = z_get_name;
	z->io_thread_create = z_rdma_io_thread_create;
	z->io_thread_cancel = z_rdma_io_thread_cancel;
	z->io_thread_ep_assign = z_rdma_io_thread_ep_assign;
	z->io_thread_ep_release = z_rdma_io_thread_ep_release;

	*pz = z;
	return ZAP_ERR_OK;

 err_0:
	return ZAP_ERR_RESOURCE;
}

#define ZAP_RDMA_JOIN_TIMEOUT_DEFAULT 5 /* 5 sec */
static void __attribute__ ((destructor)) zap_rdma_fini(void)
{
	/* no-op */
}
