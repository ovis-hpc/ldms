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
#define _GNU_SOURCE
#include <sys/errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_rma.h>
#include <infiniband/verbs.h>
#include "zap_fabric.h"

#define Z_FI_CAPS (FI_MSG|FI_RMA)

/*
 * This is a libfabric provider for zap.
 *
 * zap_fabric has been tested with verbs and sockets providers.
 *
 * zap_fabric transport requires FI_WAIT_FD. Since GNI fabric provider does not
 * support WAIT_FD, zap_fabric.gni is not supported.
 */

/* Globals. */
static struct {
	struct z_fi_fabdom	dom[ZAP_FI_MAX_DOM];
	int			dom_count;
	int			cq_fd;
	int			cm_fd;
	uint64_t		mr_key;
	zap_log_fn_t		log_fn;
	pthread_mutex_t		lock;
} g;

/*
 * DLOG(fmt, ...)
 *    For provider debug. Calls the transport's logging function given in
 *    zap_get() if DEBUG is defined otherwise compiles out to empty.
 *
 * LOG_(rep, fmt, ...)
 *    For surfacing endpoint errors. Calls the endpoint's logging fn specified
 *    in zap_new.
 */
#ifdef DEBUG
static void dlog_(const char *func, int line, char *fmt, ...)
{
	va_list ap;
	pid_t tid;
	char *buf;
	struct timespec ts;

	tid = (pid_t) syscall (SYS_gettid);
	clock_gettime(CLOCK_REALTIME, &ts);
	va_start(ap, fmt);
	vasprintf(&buf, fmt, ap);
	g.log_fn("[%d] %d.%09d: %s:%d | %s", tid, ts.tv_sec, ts.tv_nsec, func, line, buf);
	free(buf);
}
#endif
#define LOG_(rep, fmt, ...) do { \
	if (rep && rep->ep.z && rep->ep.z->log_fn) \
		rep->ep.z->log_fn(fmt, ##__VA_ARGS__); \
} while (0)

#ifdef DEBUG
#define DLOG(fmt, ...)	dlog_(__func__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define DLOG(fmt, ...)
#endif

#ifdef EP_DEBUG
#define __zap_get_ep( _EP, _REASON ) do {			      \
	DLOG("EP_DEBUG: %s() GET %p %s, ref_count: %d\n", __func__, _EP, _REASON,  \
						( _EP )->ref_count+1); \
	zap_get_ep(_EP); \
} while (0)
#define __zap_put_ep( _EP, _REASON ) do {				      \
	DLOG("EP_DEBUG: %s() PUT %p %s, ref_count: %d\n", __func__, _EP, _REASON,  \
						( _EP )->ref_count-1); \
	zap_put_ep(_EP); \
} while (0)
#define __deliver_disconnected( _REP ) do { \
	DLOG("EP_DEBUG: %s() deliver_disconnected %p, ref_count: %d\n", \
			__func__, _REP, (_REP)->ep.ref_count); \
	_deliver_disconnected(_REP); \
} while (0)
#else
#define __zap_get_ep(_EP,_REASON) zap_get_ep(_EP)
#define __zap_put_ep(_EP,_REASON) zap_put_ep(_EP)
#define __deliver_disconnected(_REP) _deliver_disconnected(_REP)
#endif

#ifdef CTXT_DEBUG
#define __flush_io_q( _REP ) do { \
	(_REP)->ep.z->log_fn("TMP_DEBUG: %s() flush_io_q %p, state %s\n", \
			__func__, _REP, __zap_ep_state_str(_REP->ep.state)); \
	flush_io_q(_REP); \
} while (0)
#define __context_alloc( _REP, _CTXT, _OP ) ({ \
	(_REP)->ep.z->log_fn("TMP_DEBUG: %s(), context_alloc\n", __func__); \
	_context_alloc(_REP, _CTXT, _OP); \
})
#define __context_free( _CTXT ) ({ \
	(_CTXT)->ep->z->log_fn("TMP_DEBUG: %s(), context_free\n", __func__); \
	_context_free(_CTXT); \
})
#else
#define __flush_io_q(_REP) flush_io_q(_REP)
#define __context_alloc( _REP, _CTXT, _OP ) _context_alloc(_REP, _CTXT, _OP)
#define __context_free( _CTXT ) _context_free(_CTXT)
#endif

#ifdef SEND_RECV_DEBUG
#define SEND_LOG(rep, ctxt) \
	LOG_(rep, "SEND MSG: {credits: %hd, msg_type: %hd, data_len: %d}\n", \
		   ntohs(ctxt->rb->msg->credits), \
		   ntohs(ctxt->rb->msg->msg_type), \
		   ctxt->rb->data_len)
#define RECV_LOG(rep, ctxt) \
	LOG_(rep, "RECV MSG: {credits: %d, msg_type: %d, data_len: %d}\n", \
		   ntohs(ctxt->rb->msg->credits), \
		   ntohs(ctxt->rb->msg->msg_type), \
		   ctxt->rb->data_len)
#else
#define SEND_LOG(rep, ctxt)
#define RECV_LOG(rep, ctxt)
#endif

#define ZAP_FABRIC_INFO_LOG "ZAP_FABRIC_INFO_LOG"
static int z_fi_info_log_on = 1;

#define Z_FI_INFO_LOG(rep, fmt, ...) do { \
	if (z_fi_info_log_on && rep && rep->ep.z && rep->ep.z->log_fn) \
		rep->ep.z->log_fn(fmt, ##__VA_ARGS__); \
} while (0)


static char *op_str[] = {
	[ZAP_WC_SEND]        = "ZAP_WC_SEND",
	[ZAP_WC_RECV]        = "ZAP_WC_RECV",
	[ZAP_WC_RDMA_WRITE]  = "ZAP_WC_RDMA_WRITE",
	[ZAP_WC_RDMA_READ]   = "ZAP_WC_RDMA_READ",
};

static int		init_once();
static int		z_fi_fill_rq(struct z_fi_ep *ep);
static zap_err_t	z_fi_unmap(zap_ep_t ep, zap_map_t map);
static void		*cm_thread_proc(void *arg);
static void		*cq_thread_proc(void *arg);
static void		_context_free(struct z_fi_context *ctxt);
static int		_buffer_init_pool(struct z_fi_ep *ep);
static void		__buffer_free(struct z_fi_buffer *rbuf);
static int		send_credit_update(struct z_fi_ep *ep);
static void		_deliver_disconnected(struct z_fi_ep *rep);
static void		*__map_addr(struct z_fi_ep *ep, zap_map_t map, void *addr);

static inline int fi_info_dom_cmp(struct fi_info *a, struct fi_info *b)
{
	int rc;
	rc = strcmp(a->fabric_attr->name, b->fabric_attr->name);
	rc = rc?rc:strcmp(a->domain_attr->name, b->domain_attr->name);
	return rc;
}

/* SCALABLE MR mode (default) ==> remote addr is referenced by MR offset
 * Otherwise ==> remote addr is the memory address.
 */
static void *__map_addr(struct z_fi_ep *ep, zap_map_t map, void *addr)
{
	enum fi_mr_mode mr_mode = ep->fi->domain_attr->mr_mode;
	if (!mr_mode || (mr_mode & FI_MR_SCALABLE)) /* SCALABLE MR mode */
		return (void*)(addr - (void*)map->addr); /* ref by offset */
	return addr; /* reference by addr */
}

/*
 * Returns <fabric, domain> pair from the given <fabric.name, domain.name> in
 * the info. The fabric-domain pair is created once and is reused for the entire
 * lifetime of the application process.
 */
static struct z_fi_fabdom *z_fi_fabdom_get(struct fi_info *info)
{
	struct z_fi_fabdom *dom = NULL;
	int i, rc;

	for (i = 0; i < g.dom_count; i++) {
		if (!g.dom[i].info)
			break;
		if (0 == fi_info_dom_cmp(g.dom[i].info, info))
			return &g.dom[i]; /* entry found */
	}
	/* no entry found */
	pthread_mutex_lock(&g.lock);
	/* the other thread may won the race and created the z_fi_fabdom */
	for (i = 0; i < g.dom_count; i++) {
		if (!g.dom[i].info)
			break;
		if (0 == fi_info_dom_cmp(g.dom[i].info, info)) {
			dom = &g.dom[i];
			goto out;
		}
	}
	if (g.dom_count == ZAP_FI_MAX_DOM) { /* too many domains */
		errno = ENOMEM;
		goto err_0;
	}
	/* really no entry found, allocate it */
	dom = &g.dom[g.dom_count];
	dom->info = fi_dupinfo(info);
	if (!dom->info)
		goto err_0;
	rc = fi_fabric(info->fabric_attr, &dom->fabric, NULL);
	if (rc)
		goto err_1;
	rc = fi_domain(dom->fabric, info, &dom->domain, NULL);
	if (rc)
		goto err_2;
	dom->id = g.dom_count;
	g.dom_count++;
 out:
	pthread_mutex_unlock(&g.lock);
	return dom;

 err_2:
	fi_close(&dom->fabric->fid);
 err_1:
	fi_freeinfo(dom->info);
	/* reset g.dom[new] entry */
	bzero(dom, sizeof(*dom));
 err_0:
	pthread_mutex_unlock(&g.lock);
	return NULL;
}

static int __enable_cq_events(struct z_fi_ep *rep)
{
	/* handle CQ events */
	struct epoll_event cq_event = {
		.events = EPOLLIN,
		.data.ptr = rep,
	};

	__zap_get_ep(&rep->ep, "CQFD");
	if (epoll_ctl(g.cq_fd, EPOLL_CTL_ADD, rep->cq_fd, &cq_event)) {
		LOG_(rep, "error %d adding CQ to epoll wait set\n", errno);
		__zap_put_ep(&rep->ep, "CQFD");
		return errno;
	}

	return 0;
}

static void __teardown_conn(struct z_fi_ep *ep)
{
	int ret;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct epoll_event ignore;

	DLOG("rep %p\n", rep);

	assert (!rep->cm_fd || (rep->ep.state != ZAP_EP_CONNECTED));

	if (rep->cm_fd) {
		if (epoll_ctl(g.cm_fd, EPOLL_CTL_DEL, rep->cm_fd, &ignore))
			LOG_(rep, "error %d removing EQ from epoll wait set\n", errno);
	}

	if (ep->fi_ep) {
		ret = fi_close(&rep->fi_ep->fid);
		if (ret)
			LOG_(rep, "error %d closing libfabric endpoint\n", ret);
	}
	if (ep->fi_pep) {
		ret = fi_close(&rep->fi_pep->fid);
		if (ret)
			LOG_(rep, "error %d closing libfabric passive endpoint\n", ret);
	}
	if (ep->cq) {
		ret = fi_close(&ep->cq->fid);
		if (ret)
			LOG_(rep, "error %d closing CQ\n", ret);
	}
	if (ep->eq) {
		ret = fi_close(&ep->eq->fid);
		if (ret)
			LOG_(rep, "error %d closing EQ\n", ret);
	}
	if (ep->buf_pool_mr) {
		ret = fi_close(&ep->buf_pool_mr->fid);
		if (ret)
			LOG_(rep, "error %d closing buffer pool\n", ret);
	}
	if (rep->fi)
		fi_freeinfo(rep->fi);
	if (rep->provider_name)
		free(rep->provider_name);
	if (rep->domain_name)
		free(rep->domain_name);

	rep->fi = NULL;
	rep->fi_ep = NULL;
	rep->fi_pep = NULL;
	rep->domain = NULL;
	rep->cq = NULL;
	rep->eq = NULL;
	rep->buf_pool_mr = NULL;
	rep->buf_pool = NULL;
	rep->buf_objs = NULL;
	rep->rem_rq_credits = RQ_DEPTH;
	rep->sq_credits = SQ_DEPTH;
	rep->lcl_rq_credits = 0;
	LIST_INIT(&rep->buf_free_list);

	free(ep->buf_pool);
	free(ep->buf_objs);
}

static void z_fi_destroy(zap_ep_t zep)
{
	zap_map_t map;
	struct z_fi_ep *rep = (void*)zep;

	assert(zep->ref_count == 0);

	DLOG("rep %p has %d ctxts\n", rep, rep->num_ctxts);

	/* Do this first. */
	while (!LIST_EMPTY(&rep->ep.map_list)) {
		map = (zap_map_t)LIST_FIRST(&rep->ep.map_list);
		LIST_REMOVE(map, link);
		z_fi_unmap(zep, map);
	}

	pthread_mutex_lock(&rep->ep.lock);
	__teardown_conn(rep);
	pthread_mutex_unlock(&rep->ep.lock);

	if (rep->parent_ep)
		__zap_put_ep(&rep->parent_ep->ep, "CONNREQ");

	DLOG("rep %p freed\n", rep);
	free(rep);
}

int sockaddr_in_print(struct sockaddr *sa, char **node, char **port)
{
	char *_node = NULL;
	char *_port = NULL;
	int len;
	uint8_t *ip_addr;
	struct sockaddr_in *sin = (void*)sa;
	if (sa->sa_family != AF_INET)
		return EINVAL;
	len = asprintf(&_port, "%hu", ntohs(sin->sin_port));
	if (len < 0)
		return errno;
	if (sin->sin_addr.s_addr) {
		ip_addr = (void*)&sin->sin_addr.s_addr;
		len = asprintf(&_node, "%hhu.%hhu.%hhu.%hhu", ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3]);
		if (len < 0) {
			free(_port);
			return errno;
		}
	} else {
		len = asprintf(&_node, "*");
		if (len < 0) {
			free(_port);
			return errno;
		}
	}
	*node = _node;
	*port = _port;
	return 0;
}

static int __fi_init(struct z_fi_ep *rep, int active, struct sockaddr *sin, size_t sa_len)
{
	/* Initialize the following fabric entities in the endpoint.
	 * -> fi
	 * -> domain
	 * -> fabric
	 * -> fabdom_id
	 */
	struct fi_info *hints = NULL;
	int ret = FI_ENOMEM;
	struct z_fi_fabdom *fdom;
	struct sockaddr *_sin;
	char *node = NULL;
	char *port = NULL;
	int rc;
	uint64_t flags = 0;

	if (rep->fi)
		goto init_dom;
	hints = fi_allocinfo();
	if (!hints)
		return FI_ENOMEM;
	hints->caps		      = FI_MSG | FI_RMA;
	hints->mode		      = FI_CONTEXT | FI_CONTEXT2 | FI_MSG_PREFIX;
	hints->ep_attr->type	      = FI_EP_MSG;
	hints->domain_attr->mr_mode   = FI_MR_LOCAL |
					FI_MR_ALLOCATED | FI_MR_PROV_KEY |
					FI_MR_VIRT_ADDR;
	hints->domain_attr->threading = FI_THREAD_SAFE;
	hints->addr_format	      = FI_SOCKADDR;
	hints->rx_attr->size = RQ_DEPTH + 2;
	hints->tx_attr->size = SQ_DEPTH + 2;
	if (sin) {
		_sin     = malloc(sa_len);
		if (!_sin)
			goto err_0;
		memcpy(_sin, sin, sa_len);
		if (active) {
			hints->dest_addr = _sin;
			hints->dest_addrlen  = sa_len;
		} else {
			rc = sockaddr_in_print(_sin, &node, &port);
			if (rc == 0) {
				flags |= FI_SOURCE;
				free(_sin); /* _sin is not used */
			} else {
				/* fall back to src_addr if str not working */
				hints->src_addr = _sin;
				hints->src_addrlen  = sa_len;
			}
		}
	}
	if (rep->provider_name) {
		hints->fabric_attr->prov_name = strdup(rep->provider_name);
		if (!hints->fabric_attr->prov_name)
			goto err_0;
	}
	if (rep->domain_name) {
		hints->domain_attr->name = strdup(rep->domain_name);
		if (!hints->domain_attr->name)
			goto err_0;
	}
	ret = fi_getinfo(ZAP_FI_VERSION, node, port, flags, hints, &rep->fi);
	if (ret)
		goto err_0;
	fi_freeinfo(hints);
	hints = NULL;
 init_dom:
	if (rep->fabric)
		goto out;
	fdom = z_fi_fabdom_get(rep->fi);
	if (!fdom) {
		ret = errno;
		goto err_0;
	}
	rep->fabric = fdom->fabric;
	rep->domain = fdom->domain;
	rep->fabdom_id = fdom->id;
 out:
	if (node)
		free(node);
	if (port)
		free(port);
	return 0;

 err_0:
	if (hints)
		fi_freeinfo(hints);
	if (node)
		free(node);
	if (port)
		free(port);
	return ret;
}

static int
__setup_conn(struct z_fi_ep *rep, struct sockaddr *sin, socklen_t sa_len)
{
	int ret;
	struct fi_eq_attr eq_attr;
	struct fi_cq_attr cq_attr;

	eq_attr.wait_obj         = FI_WAIT_FD;
	eq_attr.flags            = 0;
	eq_attr.size             = 0;  // 0 means provider chooses # entries
	eq_attr.signaling_vector = 0;
	eq_attr.wait_set         = NULL;

	cq_attr.wait_obj         = FI_WAIT_FD;
	cq_attr.format           = FI_CQ_FORMAT_DATA;
	cq_attr.flags            = 0;
	cq_attr.size             = RQ_DEPTH + SQ_DEPTH + 4;
	cq_attr.signaling_vector = 0;
	cq_attr.wait_cond        = FI_CQ_COND_NONE;
	cq_attr.wait_set         = NULL;

	ret = 0;
	if (rep->parent_ep) {
		// we get here if fi_accept() called
		rep->fi->rx_attr->size = RQ_DEPTH + 2;
		rep->fi->tx_attr->size = SQ_DEPTH + 2;
		ret = ret || fi_endpoint(rep->domain, rep->fi, &rep->fi_ep, NULL);
	} else {
		// we get here if fi_connect() called
		ret = fi_endpoint(rep->domain, rep->fi, &rep->fi_ep, NULL);
		DLOG("using fabric '%s' provider '%s' domain '%s'\n",
		     rep->fi->fabric_attr->name,
		     rep->fi->fabric_attr->prov_name,
		     rep->fi->domain_attr->name);
	}
	ret = ret || fi_eq_open(rep->fabric, &eq_attr, &rep->eq, NULL);
	ret = ret || fi_cq_open(rep->domain, &cq_attr, &rep->cq, NULL);
	ret = ret || fi_control(&rep->eq->fid, FI_GETWAIT, &rep->cm_fd);
	ret = ret || fi_control(&rep->cq->fid, FI_GETWAIT, &rep->cq_fd);
	ret = ret || fi_ep_bind(rep->fi_ep, &rep->eq->fid, 0);
	ret = ret || fi_ep_bind(rep->fi_ep, &rep->cq->fid, FI_RECV|FI_TRANSMIT);
	ret = ret || fi_enable(rep->fi_ep);
	ret = ret || _buffer_init_pool(rep);
	ret = ret || z_fi_fill_rq(rep);
	if (ret) {
		rep->ep.state = ZAP_EP_ERROR;
		return ret;
	}
	return ret;
}

/* Allocate and register an endpoint's send and recv buffers. */
static int
_buffer_init_pool(struct z_fi_ep *rep)
{
	int			i, ret;
	char			*p;
	struct z_fi_buffer	*rb;

	rep->num_bufs = RQ_DEPTH + SQ_DEPTH + 4;  // +4 for credit updates
	rep->buf_sz   = RQ_BUF_SZ;
	rep->buf_pool = calloc(1, rep->num_bufs * rep->buf_sz);
	rep->buf_objs = calloc(1, rep->num_bufs * sizeof(struct z_fi_buffer));
	if (!rep->buf_pool || !rep->buf_objs)
		return -ENOMEM;
	ret = fi_mr_reg(rep->domain, rep->buf_pool, rep->num_bufs*rep->buf_sz, FI_SEND|FI_RECV, 0,
			++g.mr_key, 0, &rep->buf_pool_mr, NULL);
	if (ret) {
		free(rep->buf_pool);
		free(rep->buf_objs);
		return -ENOMEM;
	}

	LIST_INIT(&rep->buf_free_list);
	pthread_mutex_init(&rep->buf_free_list_lock, NULL);
	p  = rep->buf_pool;
	rb = rep->buf_objs;
	for (i = 0; i < rep->num_bufs; ++i) {
		rb->rep      = rep;
		rb->buf      = p;
		rb->msg      = (struct z_fi_message_hdr *)(p + rep->fi->ep_attr->msg_prefix_size);
		rb->buf_len  = rep->buf_sz;  /* total size of buffer */
		rb->data_len = 0;            /* # bytes of buffer used */
		LIST_INSERT_HEAD(&rep->buf_free_list, rb, free_link);
		++rb;
		p += rep->buf_sz;
	}
	return 0;
}

static struct z_fi_buffer *
__buffer_alloc(struct z_fi_ep *rep)
{
	struct z_fi_buffer	*rb;

	pthread_mutex_lock(&rep->buf_free_list_lock);
	if (LIST_EMPTY(&rep->buf_free_list)) {
		rb = NULL;
	} else {
		rb = LIST_FIRST(&rep->buf_free_list);
		LIST_REMOVE(rb, free_link);
		rb->data_len = 0;  /* # bytes of buffer used */
	}
	pthread_mutex_unlock(&rep->buf_free_list_lock);

	return rb;
}

static void
__buffer_free(struct z_fi_buffer *rb)
{
	struct z_fi_ep	*rep = rb->rep;

	pthread_mutex_lock(&rep->buf_free_list_lock);
	LIST_INSERT_HEAD(&rep->buf_free_list, rb, free_link);
	pthread_mutex_unlock(&rep->buf_free_list_lock);
}

static inline int
_buffer_fits(struct z_fi_buffer *rb, size_t len)
{
	return rb->buf_len >= (len + rb->rep->fi->ep_attr->msg_prefix_size);
}

static inline int
__ep_state_check(struct z_fi_ep *rep)
{
	int rc = 0;

	if (rep->ep.state != ZAP_EP_CONNECTED)
		rc = ZAP_ERR_NOT_CONNECTED;
	return rc;
}

/* Must be called with the endpoint lock held */
static struct z_fi_context *
_context_alloc(struct z_fi_ep *rep, void *usr_context, enum z_fi_op op)
{
	struct z_fi_context *ctxt;

	ctxt = calloc(1, sizeof *ctxt);
	if (!ctxt)
		return NULL;
	ctxt->usr_context = usr_context;
	ctxt->op = op;
	ctxt->ep = rep;
	__zap_get_ep(&rep->ep, "CONTEXT");
	LIST_INSERT_HEAD(&rep->active_ctxt_list, ctxt, active_ctxt_link);
	++rep->num_ctxts;
	return ctxt;
}

/* Must be called with the endpoint lock held */
static void _context_free(struct z_fi_context *ctxt)
{
	--ctxt->ep->num_ctxts;
	LIST_REMOVE(ctxt, active_ctxt_link);
	__zap_put_ep(&ctxt->ep->ep, "CONTEXT");
	free(ctxt);
}

/* Must be called with the credit lock held */
static void flush_io_q(struct z_fi_ep *rep)
{
	struct z_fi_context *ctxt;
	struct zap_event ev = {
		.status = ZAP_ERR_FLUSH,
	};

	while (!TAILQ_EMPTY(&rep->io_q)) {
		ctxt = TAILQ_FIRST(&rep->io_q);
		TAILQ_REMOVE(&rep->io_q, ctxt, pending_link);
		switch (ctxt->op) {
		    case ZAP_WC_SEND:
			/*
			 * Zap version 1.3.0.0 doesn't have the SEND_COMPLETE event
			 */
			if (ctxt->u.send.rb)
				__buffer_free(ctxt->u.send.rb);
			__context_free(ctxt);
			return;
		    case ZAP_WC_RDMA_WRITE:
			ev.type = ZAP_EVENT_WRITE_COMPLETE;
			ev.context = ctxt->usr_context;
			break;
		    case ZAP_WC_RDMA_READ:
			ev.type = ZAP_EVENT_READ_COMPLETE;
			ev.context = ctxt->usr_context;
			break;
		    case ZAP_WC_RECV:
		    default:
			LOG_(rep, "invalid op type %d in queued i/o\n", ctxt->op);
			break;
		}
		rep->ep.cb(&rep->ep, &ev);
		__context_free(ctxt);
	}
}

/*
 * Must be called with the credit_lock held.
 */
static zap_err_t
post_wr(struct z_fi_ep *rep, struct z_fi_context *ctxt)
{
	int rc;
	size_t len;
	struct z_fi_buffer *rb;

	switch (ctxt->op) {
	    case ZAP_WC_SEND:
		rb = ctxt->u.send.rb;
		rb->msg->credits = htons(rep->lcl_rq_credits);
		rep->lcl_rq_credits = 0;
		SEND_LOG(rep, ctxt);
		len = rb->data_len + rep->fi->ep_attr->msg_prefix_size;
		rc = fi_send(rep->fi_ep, rb->buf, len, fi_mr_desc(rb->rep->buf_pool_mr), 0, ctxt);

		DLOG("ZAP_WC_SEND rep %p ctxt %p rb %p len %d with %d credits rc %d\n",
		     rep, ctxt, rb, len, rep->lcl_rq_credits, rc);
		break;
	    case ZAP_WC_RDMA_WRITE:
		rc = fi_write(rep->fi_ep, ctxt->u.rdma.src_addr, ctxt->u.rdma.len,
			      fi_mr_desc(ctxt->u.rdma.src_map->u.local.mr), 0,
			      (uint64_t)ctxt->u.rdma.dst_addr,
			      ctxt->u.rdma.dst_map->u.remote.rkey, ctxt);

		DLOG("ZAP_WC_RDMA_WRITE rep %p ctxt %p src %p dst %p len %d\n",
		     rep, ctxt, ctxt->u.rdma.src_addr, ctxt->u.rdma.dst_addr, ctxt->u.rdma.len);
		break;
	    case ZAP_WC_RDMA_READ:
		rc = fi_read(rep->fi_ep, ctxt->u.rdma.dst_addr, ctxt->u.rdma.len,
			     fi_mr_desc(ctxt->u.rdma.dst_map->u.local.mr), 0,
			     (uint64_t)ctxt->u.rdma.src_addr,
			     ctxt->u.rdma.src_map->u.remote.rkey, ctxt);

		DLOG("ZAP_WC_RDMA_READ rep %p ctxt %p src %p dst %p len %d\n",
		     rep, ctxt, ctxt->u.rdma.src_addr, ctxt->u.rdma.dst_addr, ctxt->u.rdma.len);
		break;
	    default:
		DLOG("Invalid Operation %d rep %p ctxt %p src %p dst %p len %d\n",
		     ctxt->op, rep, ctxt, ctxt->u.rdma.src_addr, ctxt->u.rdma.dst_addr, ctxt->u.rdma.len);
		rc = ZAP_ERR_TRANSPORT;
		break;
	}
	if (rc) {
		if (rc < 0) rc = -rc;
		LOG_(rep,
		     "error %d (%s) posting %s sq_credits %d lcl_rq_credits %d rem_rq_credits %d\n",
		     rc, fi_strerror(rc), op_str[ctxt->op],
		     rep->sq_credits, rep->lcl_rq_credits, rep->rem_rq_credits);
		rc = ZAP_ERR_TRANSPORT;
	}
	return rc;
}

/*
 * This version is called from within get_credits, after checking for
 * previously queued I/O, and from submit_pending. It requires that the
 * credit_lock is held by the caller.
 */
static int _get_credits(struct z_fi_ep *rep, int is_rdma)
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
static int get_credits(struct z_fi_ep *rep, int is_rdma)
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

static void put_sq(struct z_fi_ep *rep)
{
	pthread_mutex_lock(&rep->credit_lock);
	if (rep->sq_credits < SQ_DEPTH)
		rep->sq_credits++;
	pthread_mutex_unlock(&rep->credit_lock);
}

/*
 * Walk the list of pending I/O and submit if sufficient credits are
 * available.
 */
static void submit_pending(struct z_fi_ep *rep)
{
	int is_rdma;
	struct z_fi_context *ctxt;

	pthread_mutex_lock(&rep->credit_lock);
	while (!TAILQ_EMPTY(&rep->io_q)) {
		ctxt = TAILQ_FIRST(&rep->io_q);
		is_rdma = (ctxt->op != ZAP_WC_SEND);
		if (_get_credits(rep, is_rdma))
			goto out;

		TAILQ_REMOVE(&rep->io_q, ctxt, pending_link);
		DLOG("rep %p ctxt %p\n", rep, ctxt);

		if (post_wr(rep, ctxt))
			__context_free(ctxt);
	}
 out:
	pthread_mutex_unlock(&rep->credit_lock);
}

/*
 * Post a send, rdma read, or rdma write if credits are available now,
 * otherwise queue the I/O for when credits are available later.
 */
static zap_err_t submit_wr(struct z_fi_ep *rep, struct z_fi_context *ctxt, int is_rdma)
{
	int rc = 0;

	pthread_mutex_lock(&rep->credit_lock);
	if (!get_credits(rep, is_rdma))
		rc = post_wr(rep, ctxt);
	else
		TAILQ_INSERT_TAIL(&rep->io_q, ctxt, pending_link);
	pthread_mutex_unlock(&rep->credit_lock);
	return rc;
}

static zap_err_t __post_send(struct z_fi_ep *rep, struct z_fi_buffer *rbuf)
{
	int rc;
	struct z_fi_context *ctxt;

	DLOG("rep %p rbuf %p\n", rep, rbuf);

	pthread_mutex_lock(&rep->ep.lock);
	ctxt = __context_alloc(rep, NULL, ZAP_WC_SEND);
	if (!ctxt) {
		errno = ENOMEM;
		pthread_mutex_unlock(&rep->ep.lock);
		return ZAP_ERR_RESOURCE;
	}
	pthread_mutex_unlock(&rep->ep.lock);
	ctxt->u.send.rb = rbuf;

	rc = submit_wr(rep, ctxt, 0);
	if (rc) {
		pthread_mutex_lock(&rep->ep.lock);
		__context_free(ctxt);
		pthread_mutex_unlock(&rep->ep.lock);
	}
	return rc;
}

static zap_err_t z_fi_close(zap_ep_t ep)
{
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;

	pthread_mutex_lock(&rep->ep.lock);
	if (!rep->cm_fd)
		goto out;
	switch (rep->ep.state) {
	    case ZAP_EP_CONNECTED:
	    case ZAP_EP_ERROR:
		if (rep->fi_ep)
			fi_shutdown(rep->fi_ep, 0);
		rep->ep.state = rep->ep.state==ZAP_EP_ERROR ? ZAP_EP_ERROR : ZAP_EP_CLOSE;
		break;
	    case ZAP_EP_PEER_CLOSE:
	    case ZAP_EP_LISTENING:
	    default:
		break;
	}
 out:
	pthread_mutex_unlock(&rep->ep.lock);
	return ZAP_ERR_OK;
}

static zap_err_t z_fi_connect(zap_ep_t ep,
				struct sockaddr *sin, socklen_t sa_len,
				char *data, size_t data_len)
{
	int rc;
	zap_err_t zerr;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct zap_event zev;
	struct epoll_event cm_event = {
		.events = EPOLLIN,
		.data.ptr = rep,
	};

	memset(&rep->conn_data, 0, sizeof(rep->conn_data));
	ZAP_VERSION_SET(rep->conn_data.v);

	if (data_len > ZAP_CONN_DATA_MAX) {
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

	zerr = ZAP_ERR_RESOURCE;
	rc = __fi_init(rep, 1, sin, sa_len);
	if (rc)
		goto err_1;
	rc = __setup_conn(rep, sin, sa_len);
	if (rc)
		goto err_1;

	__zap_get_ep(&rep->ep, "CONNECT");
	rc = epoll_ctl(g.cm_fd, EPOLL_CTL_ADD, rep->cm_fd, &cm_event);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_2;
	}

	rc = fi_connect(rep->fi_ep, rep->fi->dest_addr,
			&rep->conn_data,
			rep->conn_data.data_len + sizeof(rep->conn_data));
	if (rc) {
		(void)epoll_ctl(g.cm_fd, EPOLL_CTL_DEL, rep->cm_fd, NULL);
		zev.type = ZAP_EVENT_CONNECT_ERROR;
		zev.status = ZAP_ERR_ROUTE;
		rep->ep.state = ZAP_EP_ERROR;
		rep->ep.cb(&rep->ep, &zev);
		__zap_put_ep(&rep->ep, "CONNECT");
	}

	return ZAP_ERR_OK;

	/* These are all synchronous errors. */
 err_2:
	__zap_put_ep(&rep->ep, "CONNECT");
 err_1:
	zap_ep_change_state(&rep->ep, ZAP_EP_CONNECTING, ZAP_EP_INIT);
 err_0:
	return zerr;
}

static int __post_recv(struct z_fi_ep *rep, struct z_fi_buffer *rb)
{
	struct z_fi_context *ctxt;
	int rc;

	pthread_mutex_lock(&rep->ep.lock);
	ctxt = __context_alloc(rep, NULL, ZAP_WC_RECV);
	if (!ctxt) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}
	ctxt->u.recv.rb = rb;

	rc = fi_recv(rep->fi_ep, rb->buf, rb->buf_len, fi_mr_desc(rb->rep->buf_pool_mr), 0, ctxt);
	if (rc) {
		__context_free(ctxt);
		rc = zap_errno2zerr(rc);
	}
	DLOG("fi_recv %d, rep %p ctxt %p rb %p buf %p len %d\n", rc, rep, ctxt, rb, rb->buf, rb->buf_len);
out:
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;
}

/*
 * Map a verbs provider errno to a zap error type.
 */
zap_err_t z_map_err(struct z_fi_ep *rep, struct fi_cq_err_entry *entry)
{
	if (!entry->err)
		return ZAP_ERR_OK;

	if (0 == strcmp(rep->fi->fabric_attr->prov_name, "verbs"))
		goto verbs;

	return zap_errno2zerr(entry->err);

 verbs:
	switch (entry->prov_errno) {
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

static void process_send_wc(struct z_fi_ep *rep, struct fi_cq_err_entry *entry)
{
	struct z_fi_context *ctxt = entry->op_context;
	if (!entry->err && ctxt->u.send.rb)
		__buffer_free(ctxt->u.send.rb);
}

static void process_read_wc(struct z_fi_ep *rep, struct fi_cq_err_entry *entry)
{
	struct zap_event zev;
	struct z_fi_context *ctxt;

	ctxt = (struct z_fi_context *)entry->op_context;
	zev.type = ZAP_EVENT_READ_COMPLETE;
	zev.context = ctxt->usr_context;
	zev.status = z_map_err(rep, entry);
	rep->ep.cb(&rep->ep, &zev);
}

static void process_write_wc(struct z_fi_ep *rep, struct fi_cq_err_entry *entry)
{
	struct zap_event zev;
	struct z_fi_context *ctxt;

	zev.type = ZAP_EVENT_WRITE_COMPLETE;
	ctxt = (struct z_fi_context *)entry->op_context;
	zev.context = ctxt->usr_context;
	zev.status = z_map_err(rep, entry);
	rep->ep.cb(&rep->ep, &zev);
}

static void handle_recv(struct z_fi_ep *rep,
			struct z_fi_message_hdr *msg, size_t len)
{
	struct zap_event zev;

	memset(&zev, 0, sizeof zev);
	zev.type = ZAP_EVENT_RECV_COMPLETE;
	zev.status = ZAP_ERR_OK;
	zev.data = (unsigned char *)(msg + 1);
	zev.data_len = len - sizeof(*msg);
	rep->ep.cb(&rep->ep, &zev);
}

static void handle_rendezvous(struct z_fi_ep *rep,
			      struct z_fi_message_hdr *msg, size_t len)
{
	struct zap_event zev;
	struct z_fi_share_msg *sh = (struct z_fi_share_msg *)msg;
	struct z_fi_map *map;

	map = calloc(1, sizeof(*map));
	if (!map)
		return;

	map->map.ref_count = 1;
	map->map.type = ZAP_MAP_REMOTE;
	map->map.ep = &rep->ep;
	map->map.addr = (void *)(unsigned long)sh->va;
	map->map.len = ntohl(sh->len);
	map->map.acc = ntohl(sh->acc);
	map->u.remote.rkey = sh->rkey;

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

static void process_recv_wc(struct z_fi_ep *rep, struct fi_cq_err_entry *entry)
{
	struct z_fi_context *ctxt;
	struct z_fi_message_hdr *msg;
	struct z_fi_buffer *rb;
	uint16_t msg_type;
	int ret;

	ctxt = entry->op_context;
	rb = ctxt->u.recv.rb;
	rb->data_len = entry->len - rep->fi->ep_attr->msg_prefix_size;
	msg = rb->msg;

	pthread_mutex_lock(&rep->credit_lock);
	rep->rem_rq_credits += ntohs(msg->credits);
	pthread_mutex_unlock(&rep->credit_lock);

	RECV_LOG(rep, ctxt);

	/*
	 * If this was a credit update, there is no data to deliver
	 * to the application and it did not consume a request slot.
	 */
	msg_type = ntohs(msg->msg_type);
	switch (msg_type) {
	case Z_FI_MSG_SEND:
		DLOG("got Z_FI_MSG_SEND rep %p msg %p len %d with %d credits\n",
		     rep, msg, rb->data_len, ntohs(msg->credits));
		assert(rb->data_len >= sizeof(*msg) + 1);
		if (rb->data_len < sizeof(*msg) + 1)
			goto err_wrong_dsz;
		handle_recv(rep, msg, rb->data_len);
		break;

	case Z_FI_MSG_RENDEZVOUS:
		DLOG("got Z_FI_MSG_RENDEZVOUS rep %p msg %p len %d with %d credits\n",
		     rep, msg, rb->data_len, ntohs(msg->credits));
		assert(rb->data_len >= sizeof(struct z_fi_share_msg));
		if (rb->data_len < sizeof(struct z_fi_share_msg))
			goto err_wrong_dsz;
		handle_rendezvous(rep, msg, rb->data_len);
		break;

	case Z_FI_MSG_CREDIT_UPDATE:
		DLOG("Z_FI_MSG_CREDIT_UPDATE rep %p msg %p len %d with %d credits\n",
		     rep, msg, rb->data_len, ntohs(msg->credits));
		assert(rb->data_len == sizeof(struct z_fi_message_hdr));
		if (rb->data_len != sizeof(struct z_fi_message_hdr))
			goto err_wrong_dsz;
		break;
	default:
		LOG_(rep, "unknown message type %d\n", msg_type);
		break;
	}

	ret = __post_recv(rep, rb);
	if (ret) {
		LOG_(rep, "error %d (%s) posting recv buffers\n", ret, zap_err_str(ret));
		__buffer_free(rb);
		goto out;
	}

	/* Credit updates are not counted */
	if (msg_type == Z_FI_MSG_CREDIT_UPDATE)
		goto out;

	pthread_mutex_lock(&rep->credit_lock);
	if (rep->lcl_rq_credits < RQ_DEPTH)
		rep->lcl_rq_credits ++;
	if (rep->lcl_rq_credits > (RQ_DEPTH >> 1)) {
		if (send_credit_update(rep))
			LOG_(rep, "credit update could not be sent\n");
	}
	pthread_mutex_unlock(&rep->credit_lock);

 out:
	return;
 err_wrong_dsz:
	LOG_(rep, "msg type %d has invalid data len %d\n", msg_type, rb->data_len);
	z_fi_close(&rep->ep);
	return;
}

static int z_fi_fill_rq(struct z_fi_ep *rep)
{
	int i, rc;
	struct z_fi_buffer *rbuf;

	for (i = 0; i < RQ_DEPTH+2; i++) {
		rbuf = __buffer_alloc(rep);
		if (!rbuf)
			return ENOMEM;
		rc = __post_recv(rep, rbuf);
		if (rc) {
			__buffer_free(rbuf);
			return rc;
		}
	}
	return 0;
}

static zap_err_t z_fi_reject(zap_ep_t ep, char *data, size_t data_len)
{
	int rc;
	size_t len;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct z_fi_reject_msg *msg;

	len = sizeof(*msg) + data_len;
	msg = calloc(1, len);
	if (!msg) {
		return ZAP_ERR_RESOURCE;
	}

	msg->hdr.msg_type = htons(Z_FI_MSG_REJECT);
	msg->len = data_len;
	memcpy(msg->msg, data, data_len);
	rep->conn_req_decision = Z_FI_PASSIVE_REJECT;
	rc = fi_reject(rep->parent_ep->fi_pep, rep->fi->handle, (void *)msg, len);
	free(msg);
	if (rc)
		return zap_errno2zerr(errno);
	zap_free(ep);
	return ZAP_ERR_OK;
}

static zap_err_t z_fi_accept(zap_ep_t ep, zap_cb_fn_t cb,
				char *data, size_t data_len)
{
	int ret;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct z_fi_accept_msg *msg;
	struct epoll_event cm_event = {
		.events = EPOLLIN,
		.data.ptr = rep,
	};

	if (data_len > ZAP_ACCEPT_DATA_MAX - sizeof(*msg)) {
		return ZAP_ERR_PARAMETER;
	}

	size_t len = sizeof(*msg) + data_len;
	msg = calloc(1, len);
	if (!msg) {
		return ZAP_ERR_RESOURCE;
	}
	msg->hdr.msg_type = htons(Z_FI_MSG_ACCEPT);
	msg->len = data_len;
	memcpy(msg->data, data, data_len);

	/* Replace the callback with the one provided by the caller */
	rep->ep.cb = cb;

	__zap_get_ep(&rep->ep, "ACCEPT");
	ret = __setup_conn(rep, NULL, 0);
	ret = ret || epoll_ctl(g.cm_fd, EPOLL_CTL_ADD, rep->cm_fd, &cm_event);
	if (ret)
		goto err_0;

	ret = fi_accept(rep->fi_ep, msg, len);
	if (ret) {
		ret = zap_errno2zerr(errno);
		goto err_0;
	}

	rep->conn_req_decision = Z_FI_PASSIVE_ACCEPT;
	free(msg);
	return ZAP_ERR_OK;
err_0:
	__zap_put_ep(&rep->ep, "ACCEPT");
	free(msg);
	return ret;
}

static void scrub_cq(struct z_fi_ep *rep)
{
	int			ret;
	struct z_fi_context	*ctxt;
	struct fi_cq_err_entry	entry;

	DLOG("rep %p\n", rep);
	while (1) {
		memset(&entry, 0, sizeof(entry));
		ret = fi_cq_read(rep->cq, &entry, 1);
		if ((ret == 0) || (ret == -FI_EAGAIN))
			break;
		if (ret == -FI_EAVAIL) {
			fi_cq_readerr(rep->cq, &entry, 0);
			if (entry.err != FI_ECANCELED) {
				/* We got an error status, not flush. */
				pthread_mutex_lock(&rep->ep.lock);
				rep->ep.state = ZAP_EP_ERROR;
				pthread_mutex_unlock(&rep->ep.lock);
				z_fi_close(&rep->ep);
				continue;
			}
			assert(entry.err);
		} else if (ret < 0) {
			DLOG("fi_cq_read error %d\n", ret);
			break;
		}
		ctxt = entry.op_context;
		DLOG("fi_cq_read %d, entry.err %d ctxt %p\n", ret, entry.err, ctxt);
		switch (ctxt->op) {
		    case ZAP_WC_SEND:
			DLOG("got ZAP_WC_SEND rep %p ctxt %p rb %p err %d proverr %d\n",
			     rep, ctxt, ctxt->u.send.rb, entry.err, entry.prov_errno);
			process_send_wc(rep, &entry);
			put_sq(rep);
			if (entry.err && ctxt->u.send.rb)
				__buffer_free(ctxt->u.send.rb);
			break;
		    case ZAP_WC_RDMA_WRITE:
			DLOG("got ZAP_WC_RDMA_WRITE rep %p ctxt %p src %p dst %p err %d proverr %d\n",
			     rep, ctxt, ctxt->u.rdma.src_addr, ctxt->u.rdma.dst_addr,
			     entry.err, entry.prov_errno);
			process_write_wc(rep, &entry);
			put_sq(rep);
			break;
		    case ZAP_WC_RDMA_READ:
			DLOG("got ZAP_WC_RDMA_READ rep %p ctxt %p src %p dst %p err %d proverr %d\n",
			     rep, ctxt, ctxt->u.rdma.src_addr, ctxt->u.rdma.dst_addr,
			     entry.err, entry.prov_errno);
			process_read_wc(rep, &entry);
			put_sq(rep);
			break;
		    case ZAP_WC_RECV:
			DLOG("got ZAP_WC_RECV rep %p ctxt %p rb %p len %d err %d proverr %d\n",
			     rep, ctxt, ctxt->u.recv.rb, entry.len, entry.err, entry.prov_errno);
			if (!entry.err)
				process_recv_wc(rep, &entry);
			break;
		    default:
			LOG_(rep,"invalid completion op %d\n", ctxt->op);
			break;
		}

		pthread_mutex_lock(&rep->ep.lock);
		__context_free(ctxt);
		pthread_mutex_unlock(&rep->ep.lock);
	}
	DLOG("done with rep %p\n", rep);
}

static void *cq_thread_proc(void *arg)
{
	int ret, i, n;
	struct z_fi_ep *rep;
	struct epoll_event cq_events[16], ignore;
	sigset_t sigset;

	sigfillset(&sigset);
	ret = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	assert(ret == 0);

	while (1) {
		n = epoll_wait(g.cq_fd, cq_events, 16, -1);
		DLOG("got %d events\n", n);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		for (i = 0; i < n; i++) {
			rep = cq_events[i].data.ptr;
			__zap_get_ep(&rep->ep, "CQE");
			scrub_cq(rep);
			pthread_mutex_lock(&rep->ep.lock);
			/*
			 * We know an endpoint is shut down when the
			 * active ctxt list becomes empty. This is the
			 * only condition under which *all* posted
			 * wr's are completed (flushed), given how
			 * SQ and RQ credits are exchanged.
			 */
			if (LIST_EMPTY(&rep->active_ctxt_list)) {
				DLOG("rep %p ctxts drained\n", rep);
				if (epoll_ctl(g.cq_fd, EPOLL_CTL_DEL, rep->cq_fd, &ignore)) {
					LOG_(rep, "error %d removing CQ from epoll wait set\n", errno);
				} else {
					__zap_put_ep(&rep->ep, "CQFD");
				}
				if (rep->deferred_disconnected == 1) {
					pthread_mutex_unlock(&rep->ep.lock);
					__deliver_disconnected(rep);
					rep->deferred_disconnected = -1;
				} else {
					pthread_mutex_unlock(&rep->ep.lock);
				}
			} else {
				pthread_mutex_unlock(&rep->ep.lock);
				submit_pending(rep);
			}
			__zap_put_ep(&rep->ep, "CQE");
		}
	}
	return NULL;
}

static zap_ep_t z_fi_new(zap_t z, zap_cb_fn_t cb)
{
	struct z_fi_ep *rep;

	rep = calloc(1, sizeof *rep);
	if (!rep) {
		errno = ZAP_ERR_RESOURCE;
		return NULL;
	}
	DLOG("new rep %p\n", rep);

	rep->rem_rq_credits = RQ_DEPTH;
	rep->lcl_rq_credits = 0;
	rep->sq_credits = SQ_DEPTH;

	TAILQ_INIT(&rep->io_q);
	LIST_INIT(&rep->active_ctxt_list);
	pthread_mutex_init(&rep->credit_lock, NULL);
	return (zap_ep_t)&rep->ep;
}

static void handle_connect_request(struct z_fi_ep *rep, struct fi_eq_cm_entry *entry, int len)
{
	zap_ep_t new_ep;
	struct z_fi_ep *new_rep;
	struct z_fi_conn_data *conn_data = (void*)entry->data;
	struct fi_info *newfi = entry->info;
	struct zap_event zev = {0};
	void *ctxt;

	assert(rep->ep.state == ZAP_EP_LISTENING);
	zev.type = ZAP_EVENT_CONNECT_REQUEST;
	zev.status = ZAP_ERR_OK;

	len -= sizeof(*entry);  // get len of connect private data

	/* Check version */
	if (!len || !zap_version_check(&conn_data->v)) {
		LOG_(rep, "zap version unsupported %hhu.%hhu\n",
		     conn_data->v.major, conn_data->v.minor);
		fi_reject(rep->fi_pep, newfi->handle, NULL, 0);
		return;
	}

	if (conn_data->data_len) {
		zev.data_len = conn_data->data_len;
		zev.data = (void*)conn_data->data;
	}

	new_ep = zap_new(rep->ep.z, rep->ep.app_cb);
	if (!new_ep) {
		LOG_(rep, "error creating new endpoint\n");
		fi_reject(rep->fi_pep, newfi->handle, NULL, 0);
		return;
	}

	ctxt = zap_get_ucontext(&rep->ep);
	zap_set_ucontext(new_ep, ctxt);
	new_rep = (struct z_fi_ep *)new_ep;
	new_rep->parent_ep = rep;
	new_rep->fi = newfi;
	new_rep->fabric = rep->fabric;
	new_rep->domain = rep->domain;
	new_rep->fabdom_id = rep->fabdom_id;
	__zap_get_ep(&rep->ep, "CONNREQ");
	zap_ep_change_state(new_ep, ZAP_EP_INIT, ZAP_EP_ACCEPTING);
	new_rep->ep.cb(new_ep, &zev);

	/*
	 * Don't use the new endpoint after this because the app's callback function
	 * (just called) could destroy the endpoint, reject the connection request,
	 * or there could be an error accepting it.
	 */
}

static void handle_conn_error(struct z_fi_ep *rep, zap_err_t err)
{
	struct zap_event zev = {0};
	zap_ep_state_t oldstate;

	zev.status = err;
	if (rep->cq_fd != -1)
		__enable_cq_events(rep);

	oldstate = rep->ep.state;
	rep->ep.state = ZAP_EP_ERROR;

	switch (oldstate) {
	    case ZAP_EP_ACCEPTING:
		/* Passive side. */
		if (rep->conn_req_decision == Z_FI_PASSIVE_ACCEPT) {
			/*
			 * App accepted the conn req already.
			 * Deliver the error.
			 */
			zev.type = ZAP_EVENT_DISCONNECTED;
			rep->ep.cb(&rep->ep, &zev);
			__zap_put_ep(&rep->ep, "ACCEPT");
		}
		break;
	    case ZAP_EP_CONNECTING:
	    case ZAP_EP_ERROR:
		zev.type = ZAP_EVENT_CONNECT_ERROR;
		rep->ep.cb(&rep->ep, &zev);
		__zap_put_ep(&rep->ep, "CONNERR");
		break;
	    default:
		LOG_(rep, "handling zap err %d unexpected ep state %d\n", err, oldstate);
		break;
	}
}

static void handle_rejected(struct z_fi_ep *rep, struct fi_eq_err_entry *entry)
{
	struct z_fi_reject_msg *rej_msg = NULL;
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
		handle_conn_error(rep, ZAP_ERR_CONNECT);
		return;
	}

	/* Active side */
	if (entry->err_data_size)
		rej_msg = (struct z_fi_reject_msg *)entry->err_data;

	if (!rej_msg || ntohs(rej_msg->hdr.msg_type) != Z_FI_MSG_REJECT) {
#ifdef ZAP_DEBUG
		rep->rejected_conn_error_count++;
#endif
		/* The server doesn't exist. */
		handle_conn_error(rep, ZAP_ERR_CONNECT);
		return;
	}
#ifdef ZAP_DEBUG
	rep->rejected_count++;
#endif
	zev.data_len = rej_msg->len;
	zev.data = (uint8_t *)rej_msg->msg;
	zev.type = ZAP_EVENT_REJECTED;

	__enable_cq_events(rep);
	rep->ep.state = ZAP_EP_ERROR;
	rep->ep.cb(&rep->ep, &zev);
	__zap_put_ep(&rep->ep, "CONNECT");
}

static void handle_established(struct z_fi_ep *rep, struct fi_eq_cm_entry *entry, int len)
{
	struct z_fi_accept_msg *msg = NULL;
	struct zap_event zev = {
		.type = ZAP_EVENT_CONNECTED,
		.status = ZAP_ERR_OK,
	};

	DLOG("rep %p len %d\n", rep, len);

	switch (rep->ep.state) {
	    case ZAP_EP_CONNECTING:
	    case ZAP_EP_ACCEPTING:
		zap_ep_change_state(&rep->ep, rep->ep.state, ZAP_EP_CONNECTED);
		break;
	    default:
		LOG_(rep, "unexpected state %s\n", __zap_ep_state_str(rep->ep.state));
		break;
	}

	/*
	 * iwarp and ib behave differently. The private data len for IB will
	 * always be non-zero.
	 */
	len -= sizeof(struct fi_eq_cm_entry);
	msg = (struct z_fi_accept_msg *)entry->data;
	if (len && (ntohs(msg->hdr.msg_type) == Z_FI_MSG_ACCEPT) && (msg->len > 0)) {
		zev.data_len = msg->len;
		zev.data = (void*)msg->data;
	}

	rep->ep.cb(&rep->ep, &zev);
	__enable_cq_events(rep);
}

static void _deliver_disconnected(struct z_fi_ep *rep)
{
	struct zap_event zev = {
		.type = ZAP_EVENT_DISCONNECTED,
		.status = ZAP_ERR_OK,
	};
	rep->ep.cb(&rep->ep, &zev);

#ifdef EP_DEBUG
	if (rep->conn_req_decision != Z_FI_PASSIVE_NONE)
		__zap_put_ep(&rep->ep, "ACCEPT");
	else
		__zap_put_ep(&rep->ep, "CONNECT");
#else
	__zap_put_ep(&rep->ep, "CONNECT");
#endif
}

static void handle_disconnected(struct z_fi_ep *rep, struct fi_eq_cm_entry *entry)
{
	DLOG("rep %p, state '%s'\n", rep, __zap_ep_state_str(rep->ep.state));

	pthread_mutex_lock(&rep->ep.lock);
	switch (rep->ep.state) {
	    case ZAP_EP_CONNECTED:
		/* Peer close while connected */
		rep->ep.state = ZAP_EP_PEER_CLOSE;
		break;
	    case ZAP_EP_CLOSE:
		/*
		 * Peer has received the disconnected event
		 * and shut down its endpoint. This is expected.
		 */
		break;
	    case ZAP_EP_ERROR:
		/* We closed and the peer has too. */
		break;
	    case ZAP_EP_PEER_CLOSE:
		LOG_(rep, "multiple disconnects on the same endpoint\n");
		break;
	    case ZAP_EP_LISTENING:
		break;
	    default:
		LOG_(rep, "unexpected disconnect in state %d\n", rep->ep.state);
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
		__deliver_disconnected(rep);
	}
}

static void cm_event_handler(struct z_fi_ep *rep,
			     uint32_t event,
			     struct fi_eq_err_entry *entry,
			     ssize_t len)
{
	struct fi_eq_cm_entry *cm_entry = (struct fi_eq_cm_entry *)entry;

	DLOG("rep %p event %d err %d len %d\n", rep, event, entry->err, len);
	switch (event) {
	    case FI_CONNREQ:
		handle_connect_request(rep, cm_entry, len);
		break;
	    case FI_CONNECTED:
		handle_established(rep, cm_entry, len);
		break;
	    case FI_SHUTDOWN:
		handle_disconnected(rep, cm_entry);
		break;
	    case -FI_EAVAIL:
		if (entry->err == ECONNREFUSED)
			handle_rejected(rep, entry);
		else
			handle_conn_error(rep, entry->prov_errno);
		break;
	    case FI_NOTIFY:
		break;
	    case FI_MR_COMPLETE:
	    case FI_AV_COMPLETE:
	    case FI_JOIN_COMPLETE:
		break;
	    default:
		DLOG("unhandled event\n");
		break;
	}
}

static void scrub_eq(struct z_fi_ep *rep)
{
	ssize_t			ret;
	uint32_t		event;
	struct fi_eq_err_entry	entry;

	DLOG("rep %p\n", rep);
	__zap_get_ep(&rep->ep, "EQE");
	while (1) {
		memset(&entry, 0, sizeof(entry));
		ret = fi_eq_read(rep->eq, &event, &entry, sizeof(entry), 0);
		if ((ret == 0) || (ret == -FI_EAGAIN))
			break;
		if (ret == -FI_EAVAIL) {
			fi_eq_readerr(rep->eq, &entry, 0);
			event = -FI_EAVAIL;
		} else if (ret < 0) {
			DLOG("fi_eq_read error %d\n", ret);
			break;
		}
		cm_event_handler(rep, event, &entry, ret);
	}
	__zap_put_ep(&rep->ep, "EQE");
	DLOG("done with rep %p\n", rep);
}

static void *cm_thread_proc(void *arg)
{
	int ret, i, n;
	struct epoll_event cm_events[16];
	sigset_t sigset;

	sigfillset(&sigset);
	ret = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	assert(ret == 0);

	while (1) {
		n = epoll_wait(g.cm_fd, cm_events, 16, -1);
		DLOG("got %d events\n", n);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		for (i = 0; i < n; ++i)
			scrub_eq(cm_events[i].data.ptr);
	}
	return NULL;
}

#define MIN(a,b) ((a)<(b)?(a):(b))

/* NOTE: From libfabric src/fi_tostr.c, ofi_straddr() is the internal function
 *       (not exported) that yields the string address. fi_tostr() with
 *       FI_TYPE_INFO seems to be the only one calling that function and giving
 *       us src_addr string along with a lot longer description of fi_info. In
 *       addition, fi_tostr() is NOT thread-safe. So, we borrow `ofi_straddr()`
 *       from libfabric and put it in this file in order to get the printable
 *       `fi_info.src_addr`.
 */
static
const char *ofi_straddr(char *buf, size_t *len,
			uint32_t addr_format, const void *addr)
{
/* from libfabric src/common.c */
/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2006-2017 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2013-2018 Intel Corp., Inc.  All rights reserved.
 * Copyright (c) 2015 Los Alamos Nat. Security, LLC. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
	const struct sockaddr *sock_addr;
	const struct sockaddr_in6 *sin6;
	const struct sockaddr_in *sin;
	char str[INET6_ADDRSTRLEN + 8];
	size_t size;

	if (!addr || !len)
		return NULL;

	switch (addr_format) {
	case FI_SOCKADDR:
		sock_addr = addr;
		switch (sock_addr->sa_family) {
		case AF_INET:
			goto sa_sin;
		case AF_INET6:
			goto sa_sin6;
		default:
			return NULL;
		}
		break;
	case FI_SOCKADDR_IN:
sa_sin:
		sin = addr;
		if (!inet_ntop(sin->sin_family, &sin->sin_addr, str,
			       sizeof(str)))
			return NULL;

		size = snprintf(buf, MIN(*len, sizeof(str)),
				"fi_sockaddr_in://%s:%" PRIu16, str,
				ntohs(sin->sin_port));
		break;
	case FI_SOCKADDR_IN6:
sa_sin6:
		sin6 = addr;
		if (!inet_ntop(sin6->sin6_family, &sin6->sin6_addr, str,
			       sizeof(str)))
			return NULL;

		size = snprintf(buf, MIN(*len, sizeof(str)),
				"fi_sockaddr_in6://[%s]:%" PRIu16, str,
				ntohs(sin6->sin6_port));
		break;
	case FI_SOCKADDR_IB:
		size = snprintf(buf, *len, "fi_sockaddr_ib://%p", addr);
		break;
	case FI_ADDR_PSMX:
		size = snprintf(buf, *len, "fi_addr_psmx://%" PRIx64,
				*(uint64_t *)addr);
		break;
	case FI_ADDR_PSMX2:
		size =
		    snprintf(buf, *len, "fi_addr_psmx2://%" PRIx64 ":%" PRIx64,
			     *(uint64_t *)addr, *((uint64_t *)addr + 1));
		break;
	case FI_ADDR_GNI:
		size = snprintf(buf, *len, "fi_addr_gni://%" PRIx64,
				*(uint64_t *)addr);
		break;
	case FI_ADDR_BGQ:
		size = snprintf(buf, *len, "fi_addr_bgq://%p", addr);
		break;
	case FI_ADDR_MLX:
		size = snprintf(buf, *len, "fi_addr_mlx://%p", addr);
		break;
	case FI_ADDR_IB_UD:
		memset(str, 0, sizeof(str));
		if (!inet_ntop(AF_INET6, addr, str, INET6_ADDRSTRLEN))
			return NULL;
		size = snprintf(buf, *len, "fi_addr_ib_ud://"
				"%s" /* GID */ ":%" PRIx32 /* QPN */
				"/%" PRIx16 /* LID */ "/%" PRIx16 /* P_Key */
				"/%" PRIx8 /* SL */,
				str, *((uint32_t *)addr + 4),
				*((uint16_t *)addr + 10),
				*((uint16_t *)addr + 11),
				*((uint8_t *)addr + 26));
		break;
	case FI_ADDR_STR:
		size = snprintf(buf, *len, "%s", (const char *) addr);
		break;
	default:
		return NULL;
	}

	/* Make sure that possibly truncated messages have a null terminator. */
	if (buf && *len)
		buf[*len - 1] = '\0';
	*len = size + 1;
	return buf;
}

static zap_err_t z_fi_listen(zap_ep_t ep, struct sockaddr *saddr, socklen_t sa_len)
{
	zap_err_t zerr;
	int rc;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct epoll_event cm_event;
	struct fi_eq_attr eq_attr = { .wait_obj = FI_WAIT_FD };
	char buf[512] = "";
	size_t len = sizeof(buf);

	zerr = zap_ep_change_state(&rep->ep, ZAP_EP_INIT, ZAP_EP_LISTENING);
	if (zerr)
		goto out;

	zerr = ZAP_ERR_RESOURCE;
	rc = __fi_init(rep, 0, saddr, sa_len);
	if (rc)
		goto err_0;
	rc = fi_eq_open(rep->fabric, &eq_attr, &rep->eq, NULL);
	if (rc)
		goto err_0;
	rc = fi_control(&rep->eq->fid, FI_GETWAIT, &rep->cm_fd);
	if (rc)
		goto err_0;
	rc = fi_passive_ep(rep->fabric, rep->fi, &rep->fi_pep, NULL);
	if (rc)
		goto err_0;
	rc = fi_pep_bind(rep->fi_pep, &rep->eq->fid, 0);
	if (rc)
		goto err_0;

	Z_FI_INFO_LOG(rep, "zap_fabric listening on %s "
			   "using fabric '%s' provider '%s' domain '%s'\n",
			   ofi_straddr(buf, &len, rep->fi->addr_format,
				       rep->fi->src_addr),
			   rep->fi->fabric_attr->name,
			   rep->fi->fabric_attr->prov_name,
			   rep->fi->domain_attr->name);

	cm_event.events = EPOLLIN | EPOLLOUT;
	cm_event.data.ptr = rep;
	rc = epoll_ctl(g.cm_fd, EPOLL_CTL_ADD, rep->cm_fd, &cm_event);
	if (rc)
		goto err_1;

	rc = fi_listen(rep->fi_pep);
	if (rc)
		goto err_1;

	return ZAP_ERR_OK;

 err_1:
	rc = epoll_ctl(g.cm_fd, EPOLL_CTL_DEL, rep->cm_fd, NULL);
	fi_close(&rep->cq->fid);
	fi_close(&rep->eq->fid);
	fi_close(&rep->fi_pep->fid);
	rep->fi_ep = NULL;
	rep->fi_pep = NULL;
	rep->cq = NULL;
	rep->eq = NULL;
	rep->cq_fd = -1;
	rep->cm_fd = -1;
 err_0:
	zap_ep_change_state(&rep->ep, ZAP_EP_LISTENING, ZAP_EP_ERROR);
 out:
	return zerr;
}

/*
 * Best-effort credit update. The receipt of this message kicks the i/o
 * on the deferred credit queue in the peer. Called with the credit_lock held.
 */
static int send_credit_update(struct z_fi_ep *rep)
{
	struct z_fi_buffer *rbuf;
	struct z_fi_context *ctxt;
	int rc;

	DLOG("rep %p\n", rep);
	rbuf = __buffer_alloc(rep);
	if (!rbuf)
		return ENOMEM;

	rbuf->msg->msg_type = htons(Z_FI_MSG_CREDIT_UPDATE);
	rbuf->data_len = sizeof(struct z_fi_message_hdr);
	pthread_mutex_unlock(&rep->credit_lock);

	pthread_mutex_lock(&rep->ep.lock);
	ctxt = __context_alloc(rep, NULL, ZAP_WC_SEND);
	if (!ctxt) {
		__buffer_free(rbuf);
		pthread_mutex_unlock(&rep->ep.lock);
		rc = ENOMEM;
		goto out;
	}
	ctxt->u.send.rb = rbuf;
	rc = post_wr(rep, ctxt);
	if (rc) {
		__buffer_free(rbuf);
		rc = ENOSPC;
		pthread_mutex_unlock(&rep->ep.lock);
		goto out;
	}
	pthread_mutex_unlock(&rep->ep.lock);
out:
	pthread_mutex_lock(&rep->credit_lock);
	return rc;
}

static zap_err_t z_get_name(zap_ep_t ep, struct sockaddr *local_sa,
			    struct sockaddr *remote_sa, socklen_t *sa_len)
{
	int ret = 0;
	size_t sz;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;

	if (!rep->fi_ep) {
		return ZAP_ERR_NOT_CONNECTED;
	}
	if (0 == *sa_len) {
		/*
		 * fi_getname() returns -FI_ETOOSHORT if *sa_len is 0.
		 * Set it now.
		 */
		*sa_len = sizeof(struct sockaddr);
	}
	sz = *sa_len;
	ret = fi_getname(&rep->fi_ep->fid, local_sa, &sz);
	if (ret)
		return ZAP_ERR_NO_SPACE;

	sz = *sa_len;
	ret = fi_getpeer(rep->fi_ep, remote_sa, &sz);
	if (ret)
		return ZAP_ERR_NO_SPACE;

	*sa_len = sz;
	return ret;
}

static zap_err_t z_fi_send(zap_ep_t ep, char *buf, size_t len)
{
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct z_fi_buffer *rbuf;
	int rc;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc)
		goto out;
	rbuf = __buffer_alloc(rep);
	if (!rbuf) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}
	pthread_mutex_unlock(&rep->ep.lock);

	if (!_buffer_fits(rbuf, len+sizeof(struct z_fi_message_hdr))) {
		__buffer_free(rbuf);
		rc = ZAP_ERR_NO_SPACE;
		goto out_2;
	}

	rbuf->msg->msg_type = htons(Z_FI_MSG_SEND);

	/* copy the data, leaving room for the rdma header */
	memcpy((char *)rbuf->msg + sizeof(struct z_fi_message_hdr), buf, len);
	rbuf->data_len = len + sizeof(struct z_fi_message_hdr);

	rc = __post_send(rep, rbuf);
	if (rc)
		__buffer_free(rbuf);

	return rc;
 out:
	pthread_mutex_unlock(&rep->ep.lock);
 out_2:
	return rc;
}

static zap_err_t z_fi_share(zap_ep_t ep, zap_map_t map,
				const char *msg, size_t msg_len)
{
	struct z_fi_map *rmap = (struct z_fi_map *)map;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct z_fi_share_msg *sm;
	struct z_fi_buffer *rbuf;
	int rc;
	size_t sz = sizeof(*sm) + msg_len;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc) {
		pthread_mutex_unlock(&rep->ep.lock);
		return rc;
	}
	rbuf = __buffer_alloc(rep);
	if (!rbuf) {
		pthread_mutex_unlock(&rep->ep.lock);
		return ZAP_ERR_RESOURCE;
	}
	pthread_mutex_unlock(&rep->ep.lock);

	if (!_buffer_fits(rbuf, sz)) {
		__buffer_free(rbuf);
		return ZAP_ERR_NO_SPACE;
	}

	sm = (struct z_fi_share_msg *)rbuf->msg;
	sm->hdr.msg_type = htons(Z_FI_MSG_RENDEZVOUS);
	sm->rkey = fi_mr_key(rmap->u.local.mr);
	sm->va = (uint64_t)rmap->map.addr;
	sm->len = htonl(rmap->map.len);
	sm->acc = htonl(rmap->map.acc);
	if (msg_len)
		memcpy(sm->msg, msg, msg_len);

	rbuf->data_len = sz;

	rc = __post_send(rep, rbuf);
	if (rc)
		__buffer_free(rbuf);
	return rc;
}

static zap_err_t z_fi_map(zap_ep_t ep, zap_map_t *pm,
			  void *buf, size_t len, zap_access_t acc)
{
	int ret;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct z_fi_map *map;
	uint64_t acc_flags;

	map = calloc(1, sizeof(*map));
	if (!map)
		goto err_0;

	acc_flags = (ZAP_ACCESS_WRITE & acc ? FI_REMOTE_WRITE : 0);
	acc_flags |= (ZAP_ACCESS_READ & acc ? FI_REMOTE_READ : 0);
	acc_flags |= (acc != ZAP_ACCESS_NONE) ? FI_READ | FI_WRITE : 0;

	if (rep->fi->ep_attr->protocol == FI_PROTO_IWARP) {
		/*
		 * iwarp requires the sink map of the read operation
		 * to be remotely writable. In the current zap API
		 * version 1.3.0.0, the zap transport library doesn't
		 * know the purpose of the map. Thus, we assume that
		 * all maps can be a sink map for a read operation.
		 */
		acc_flags |= FI_REMOTE_WRITE;
	}

	ret = fi_mr_reg(rep->domain, buf, len, acc_flags, 0, ++g.mr_key, 0, &map->u.local.mr, NULL);
	if (ret)
		goto err_1;

	*pm = &map->map;
	return ZAP_ERR_OK;
 err_1:
	free(map);
 err_0:
	return ZAP_ERR_RESOURCE;
}

static zap_err_t z_fi_unmap(zap_ep_t ep, zap_map_t map)
{
	struct z_fi_map *zm = (struct z_fi_map *)map;

	if (map->type == ZAP_MAP_LOCAL) {
		assert(zm->u.local.mr);
		fi_close(&zm->u.local.mr->fid);
		zm->u.local.mr = NULL;
	}
	free(zm);
	return ZAP_ERR_OK;
}

static zap_err_t z_fi_write(zap_ep_t ep,
			    zap_map_t src_map, char *src,
			    zap_map_t dst_map, char *dst,
			    size_t sz,
			    void *context)
{
	int rc;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct z_fi_map *rmap = (struct z_fi_map *)dst_map;
	struct z_fi_map *lmap = (struct z_fi_map *)src_map;
	struct z_fi_context *ctxt;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc)
		goto out;

	ctxt = __context_alloc(rep, context, ZAP_WC_RDMA_WRITE);
	if (!ctxt) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}
	ctxt->u.rdma.src_map  = lmap;
	ctxt->u.rdma.dst_map  = rmap;
	ctxt->u.rdma.src_addr = src;
	ctxt->u.rdma.dst_addr = __map_addr(rep, dst_map, dst);
	ctxt->u.rdma.len      = sz;

	rc = submit_wr(rep, ctxt, 1);
	if (rc)
		__context_free(ctxt);
out:
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;
}

static zap_err_t z_fi_read(zap_ep_t ep,
			   zap_map_t src_map, char *src,
			   zap_map_t dst_map, char *dst,
			   size_t sz,
			   void *context)
{
	zap_err_t rc;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct z_fi_map *lmap = (struct z_fi_map *)dst_map;
	struct z_fi_map *rmap = (struct z_fi_map *)src_map;
	struct z_fi_context *ctxt;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc)
		goto out;

	ctxt = __context_alloc(rep, context, ZAP_WC_RDMA_READ);
	if (!ctxt) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}
	ctxt->u.rdma.src_map  = rmap;
	ctxt->u.rdma.dst_map  = lmap;
	ctxt->u.rdma.src_addr = __map_addr(rep, src_map, src);
	ctxt->u.rdma.dst_addr = dst;
	ctxt->u.rdma.len      = sz;

	rc = submit_wr(rep, ctxt, 1);
	if (rc)
		__context_free(ctxt);
out:
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;
}

static pthread_t cm_thread, cq_thread;

static void z_fi_cleanup(void)
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
	static int init_complete = 0;
	const char *env;

	if (init_complete)
		return 0;

	g.cq_fd = epoll_create(512);
	if (!g.cq_fd)
		goto err_0;

	g.cm_fd = epoll_create(512);
	if (!g.cm_fd)
		goto err_1;

	rc = pthread_create(&cq_thread, NULL, cq_thread_proc, NULL);
	if (rc)
		goto err_2;
	pthread_setname_np(cq_thread, "z_fi_cq");

	rc = pthread_create(&cm_thread, NULL, cm_thread_proc, NULL);
	if (rc)
		goto err_3;
	pthread_setname_np(cm_thread, "z_fi_cm");

	env = getenv(ZAP_FABRIC_INFO_LOG);
	if (env)
		z_fi_info_log_on = atoi(env);

	init_complete = 1;
	atexit(z_fi_cleanup);
	return 0;

 err_3:
	pthread_cancel(cq_thread);
 err_2:
	close(g.cm_fd);
 err_1:
	close(g.cq_fd);
 err_0:
	return 1;
}

zap_err_t zap_transport_get(zap_t *pz, zap_log_fn_t log_fn,
			    zap_mem_info_fn_t mem_info_fn)
{
	zap_t z;

	if (init_once())
		goto err_0;

	z = calloc(1, sizeof (*z));
	if (!z) {
		errno = ENOMEM;
		goto err_0;
	}
	z->max_msg = RQ_BUF_SZ - sizeof(struct z_fi_share_msg);
	z->new = z_fi_new;
	z->destroy = z_fi_destroy;
	z->connect = z_fi_connect;
	z->accept = z_fi_accept;
	z->reject = z_fi_reject;
	z->listen = z_fi_listen;
	z->close = z_fi_close;
	z->send = z_fi_send;
	z->read = z_fi_read;
	z->write = z_fi_write;
	z->map = z_fi_map;
	z->unmap = z_fi_unmap;
	z->share = z_fi_share;
	z->get_name = z_get_name;

	if (!g.log_fn && log_fn)
		g.log_fn = log_fn;

	*pz = z;
	return ZAP_ERR_OK;

 err_0:
	return ZAP_ERR_RESOURCE;
}
