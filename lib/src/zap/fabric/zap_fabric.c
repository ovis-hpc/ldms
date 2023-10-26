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
#include <sys/types.h>
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
#include "ovis_log/ovis_log.h"
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
	uint64_t		mr_key;
	pthread_mutex_t		lock;
} g;

__attribute__((unused))
static const char *z_fi_op_str[] = {
	[Z_FI_WC_SEND]        =  "ZAP_WC_SEND",
	[Z_FI_WC_RECV]        =  "ZAP_WC_RECV",
	[Z_FI_WC_RDMA_READ]   =  "ZAP_WC_RDMA_READ",
	[Z_FI_WC_RDMA_WRITE]  =  "ZAP_WC_RDMA_WRITE",
};

static ovis_log_t zflog;

/*
 * DLOG(fmt, ...)
 *    For provider debug. Calls the transport's logging function given in
 *    zap_get() if DEBUG is defined otherwise compiles out to empty.
 *
 * LOG_(rep, fmt, ...)
 *    For surfacing endpoint errors. Calls the endpoint's logging fn specified
 *    in zap_new.
 */
#if defined(DEBUG) || defined(EP_DEBUG)
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
	va_end(ap);
	ovis_log(zflog, OVIS_LDEBUG, "[%d] %d.%09d: %s:%d | %s", tid, ts.tv_sec, ts.tv_nsec, func, line, buf);
	free(buf);
}
#endif

#define LOG(FMT, ...) ovis_log(zflog, OVIS_LERROR, "[zap_fabric] " FMT, ##__VA_ARGS__)
#define LLOG(FMT, ...) ovis_log(zflog, OVIS_LERROR, "[zap_fabric:%s():%d] " FMT, __func__, __LINE__, ##__VA_ARGS__)
#define TLOG(FMT, ...) do { \
	struct timespec _t; \
	pid_t _tid = (pid_t) syscall (SYS_gettid); \
	clock_gettime(CLOCK_REALTIME, &_t); \
	ovis_log(zflog, OVIS_LERROR, "[zap_fabric:%s():%d %ld.%09ld %d] " FMT, __func__, __LINE__, \
			_t.tv_sec, _t.tv_nsec, _tid, ##__VA_ARGS__); \
} while(0)

#define LOG_(rep, fmt, ...) do { \
	ovis_log(zflog, OVIS_LERROR, fmt, ##__VA_ARGS__); \
} while (0)

#if defined(DEBUG) || defined(EP_DEBUG)
#define DLOG(fmt, ...)	dlog_(__func__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define DLOG(fmt, ...)
#endif

/*
#ifndef EP_DEBUG
#define EP_DEBUG
#endif
*/

#ifdef EP_DEBUG
#define __deliver_disconnected( _REP ) do { \
	DLOG("EP_DEBUG: %s() deliver_disconnected %p, ref_count: %d\n", \
			__func__, _REP, (_REP)->ep.ref_count); \
	_deliver_disconnected(_REP); \
} while (0)
#else
#define __deliver_disconnected(_REP) _deliver_disconnected(_REP)
#endif

#ifdef CTXT_DEBUG
#define __context_alloc( _REP, _CTXT, _OP ) ({ \
	void *_ctxt; \
	_ctxt = _context_alloc(_REP, _CTXT, _OP); \
	LOG_(_REP, "TMP_DEBUG: %s(), context_alloc %p rep %p\n", __func__, _ctxt, _REP); \
	_ctxt; \
})
#define __context_free( _CTXT ) ({ \
	LOG_((_CTXT)->ep, "TMP_DEBUG: %s(), context_free %p %p\n", __func__, _CTXT, (_CTXT)->ep); \
	_context_free(_CTXT); \
})
#else
#define __context_alloc( _REP, _CTXT, _OP ) _context_alloc(_REP, _CTXT, _OP)
#define __context_free( _CTXT ) _context_free(_CTXT)
#endif

#ifdef SEND_RECV_DEBUG
#define SEND_LOG(rep, ctxt) \
	LOG_(rep, "SEND MSG: {credits: %hd, msg_type: %hd, data_len: %d}\n", \
		   ntohs(ctxt->u.send.rb->msg->credits), \
		   ntohs(ctxt->u.send.rb->msg->msg_type), \
		   ctxt->u.send.rb->data_len)
#define RECV_LOG(rep, ctxt) \
	LOG_(rep, "RECV MSG: {credits: %d, msg_type: %d, data_len: %d}\n", \
		   ntohs(ctxt->u.recv.rb->msg->credits), \
		   ntohs(ctxt->u.recv.rb->msg->msg_type), \
		   ctxt->u.recv.rb->data_len)
#else
#define SEND_LOG(rep, ctxt)
#define RECV_LOG(rep, ctxt)
#endif

#define ZAP_FABRIC_INFO_LOG "ZAP_FABRIC_INFO_LOG"
static int z_fi_info_log_on = 1;

#define Z_FI_INFO_LOG(rep, fmt, ...) do { \
	ovis_log(zflog, OVIS_LINFO, fmt, ##__VA_ARGS__); \
} while (0)


static char *op_str[] = {
	[Z_FI_WC_SEND]        = "ZAP_WC_SEND",
	[Z_FI_WC_RECV]        = "ZAP_WC_RECV",
	[Z_FI_WC_RDMA_WRITE]  = "ZAP_WC_RDMA_WRITE",
	[Z_FI_WC_RDMA_READ]   = "ZAP_WC_RDMA_READ",
};

static int		init_once();
static int		z_fi_fill_rq(struct z_fi_ep *ep);
static zap_err_t	z_fi_unmap(zap_map_t map);
static void		_context_free(struct z_fi_context *ctxt);
static int		_buffer_init_pool(struct z_fi_ep *ep);
static void		__buffer_free(struct z_fi_buffer *rbuf);
static int		send_credit_update(struct z_fi_ep *ep);
static void		_deliver_disconnected(struct z_fi_ep *rep);
static struct fid_mr	*z_fi_mr_get(struct z_fi_ep *ep, struct zap_map *map);
static void		*__map_addr(struct z_fi_ep *ep, zap_map_t map, void *addr);
static void		z_fi_flush(struct z_fi_ep *rep);

static void _buffer_pool_free(struct z_fi_ep *rep, struct z_fi_buffer_pool *p);
static struct z_fi_buffer_pool * _buffer_pool_new(struct z_fi_ep *rep, int num_bufs);

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

static struct z_fi_map *z_fi_map_get(struct zap_map *map)
{
	struct z_fi_map *zm;
	if (map->type != ZAP_MAP_LOCAL) {
		errno = EINVAL;
		return NULL;
	}
	if (map->mr[ZAP_FABRIC])
		return map->mr[ZAP_FABRIC];
	zm = calloc(1, sizeof(*zm));
	if (!zm)
		return NULL;
	pthread_mutex_init(&zm->lock, NULL);
	/* race with other threads to assign mr[ZAP_FABRIC] */
	if (!__sync_bool_compare_and_swap(&map->mr[ZAP_FABRIC], NULL, zm)) {
		/* we lose the race */
		pthread_mutex_destroy(&zm->lock);
		free(zm);
	}
	return map->mr[ZAP_FABRIC];
}

static struct fid_mr *z_fi_mr_get(struct z_fi_ep *rep, struct zap_map *map)
{
	struct z_fi_map *zm;
	struct fid_mr *mr;
	int rc;
	uint64_t acc_flags;
	zm = z_fi_map_get(map);
	if (!zm)
		return NULL;
	mr = zm->mr[rep->fabdom_id];
	if (mr)
		return mr;
	/* else, need create */
	pthread_mutex_lock(&zm->lock);
	mr = zm->mr[rep->fabdom_id];
	if (mr) {
		/* other thread won the mr creation */
		pthread_mutex_unlock(&zm->lock);
		return mr;
	}
	acc_flags = (ZAP_ACCESS_WRITE & map->acc ? FI_REMOTE_WRITE : 0);
	acc_flags |= (ZAP_ACCESS_READ & map->acc ? FI_REMOTE_READ : 0);
	acc_flags |= (map->acc != ZAP_ACCESS_NONE) ? FI_READ | FI_WRITE : 0;
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
	rc = fi_mr_reg(rep->domain, map->addr, map->len, acc_flags, 0,
			__sync_add_and_fetch(&g.mr_key, 1), 0, &mr, NULL);
	if (rc) {
		pthread_mutex_unlock(&zm->lock);
		return NULL;
	}
	/*
	rc = fi_mr_enable(mr);
	if (rc) {
		fi_close(&mr->fid);
		pthread_mutex_unlock(&zm->lock);
		return NULL;
	}
	*/
	zm->mr[rep->fabdom_id] = mr;
	pthread_mutex_unlock(&zm->lock);
	return mr;
}

static uint64_t z_fi_rkey_get(struct zap_map *map)
{
	if (map->type != ZAP_MAP_REMOTE)
		return -1;
	return (uint64_t)map->mr[ZAP_FABRIC];
}

static void __teardown_conn(struct z_fi_ep *rep)
{
	int ret;
	struct z_fi_buffer_pool *p;

	DLOG("rep %p\n", rep);

	assert (!rep->cm_fd || (rep->ep.state != ZAP_EP_CONNECTED));

	z_fi_flush(rep);

	if (rep->fi_ep) {
		ret = fi_close(&rep->fi_ep->fid);
		if (ret)
			LOG_(rep, "error %d closing libfabric endpoint\n", ret);
	}
	if (rep->fi_pep) {
		ret = fi_close(&rep->fi_pep->fid);
		if (ret)
			LOG_(rep, "error %d closing libfabric passive endpoint\n", ret);
	}
	if (rep->cq) {
		ret = fi_close(&rep->cq->fid);
		if (ret) {
			/* LOG_(rep, "error %d closing CQ\n", ret); */
			/* Try to clear the cq and try close again */
			struct fi_cq_err_entry	entry;
			while (fi_cq_read(rep->cq, &entry, 1) > 0) {
			}
			ret = fi_close(&rep->cq->fid);
			if (ret) {
				LOG_(rep, "error %d closing CQ\n", ret);
			}
		}
	}
	if (rep->eq) {
		ret = fi_close(&rep->eq->fid);
		if (ret)
			LOG_(rep, "error %d closing EQ\n", ret);
	}
	pthread_mutex_lock(&rep->buf_free_list_lock);
	while ((p = LIST_FIRST(&rep->vacant_pool))) {
		_buffer_pool_free(rep, p);
	}
	while ((p = LIST_FIRST(&rep->full_pool))) {
		_buffer_pool_free(rep, p);
	}
	pthread_mutex_unlock(&rep->buf_free_list_lock);
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
	rep->rem_rq_credits = RQ_DEPTH;
	rep->sq_credits = SQ_DEPTH;
	rep->lcl_rq_credits = 0;
}

static void z_fi_destroy(zap_ep_t zep)
{
	zap_map_t map;
	struct z_fi_ep *rep = (void*)zep;

	DLOG("rep %p has %d ctxts\n", rep, rep->num_ctxts);

	/* Do this first. */
	while (!LIST_EMPTY(&rep->ep.map_list)) {
		map = (zap_map_t)LIST_FIRST(&rep->ep.map_list);
		LIST_REMOVE(map, link);
		z_fi_unmap(map);
	}

	pthread_mutex_lock(&rep->ep.lock);
	__teardown_conn(rep);
	pthread_mutex_unlock(&rep->ep.lock);

	if (rep->parent_ep)
		ref_put(&rep->parent_ep->ep.ref, "conn req");

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

/* check ret before calling `ret = fn_call`, and log if ret != 0 */
#define _XCALL(ret, fn_call) if (ret == 0) { \
	ret = fn_call;\
	if (ret) \
		LLOG("%s returns %d\n", #fn_call, ret); \
}

/* ep->lock must NOT be held */
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

	ret  = 0;
	if (rep->parent_ep) {
		// we get here if fi_accept() called
		rep->fi->rx_attr->size = RQ_DEPTH + 2;
		rep->fi->tx_attr->size = SQ_DEPTH + 2;
		_XCALL(ret, fi_endpoint(rep->domain, rep->fi, &rep->fi_ep, NULL));
	} else {
		// we get here if fi_connect() called
		_XCALL(ret, fi_endpoint(rep->domain, rep->fi, &rep->fi_ep, NULL));
		DLOG("using fabric '%s' provider '%s' domain '%s'\n",
		     rep->fi->fabric_attr->name,
		     rep->fi->fabric_attr->prov_name,
		     rep->fi->domain_attr->name);
	}
	_XCALL(ret, fi_eq_open(rep->fabric, &eq_attr, &rep->eq, NULL));
	_XCALL(ret, fi_cq_open(rep->domain, &cq_attr, &rep->cq, NULL));
	_XCALL(ret, fi_control(&rep->eq->fid, FI_GETWAIT, &rep->cm_fd));
	_XCALL(ret, fi_control(&rep->cq->fid, FI_GETWAIT, &rep->cq_fd));
	_XCALL(ret, fi_ep_bind(rep->fi_ep, &rep->eq->fid, 0));
	_XCALL(ret, fi_ep_bind(rep->fi_ep, &rep->cq->fid, FI_RECV|FI_TRANSMIT));
	_XCALL(ret, fi_enable(rep->fi_ep));
	_XCALL(ret, _buffer_init_pool(rep));
	_XCALL(ret, z_fi_fill_rq(rep));
	if (ret) {
		rep->ep.state = ZAP_EP_ERROR;
		return ret;
	}
	return ret;
}

/* caller MUST hold rep->buf_free_list_lock */
static struct z_fi_buffer_pool *
_buffer_pool_new(struct z_fi_ep *rep, int num_bufs)
{
	struct z_fi_buffer_pool *p;
	size_t sz;
	int i, ret;
	char *b;
	struct z_fi_buffer *rb;

	/* sz of each buf */
	sz = rep->fi->ep_attr->msg_prefix_size + RQ_BUF_SZ;
	sz = ( (sz-1) | 0x3F ) + 1; /* 64-byte aligned */
	/* total size */
	sz = sizeof(*p) + num_bufs*sizeof(p->buf_obj[0]) + num_bufs*sz;
	p = malloc(sz);
	if (!p)
		goto err_0;

	/* initialize pool structure */
	bzero(p, sizeof(*p));
	p->num_bufs = num_bufs;
	p->buf_sz = rep->fi->ep_attr->msg_prefix_size + RQ_BUF_SZ;
	p->buf_pool = (void*)&p->buf_obj[num_bufs];
	LIST_INIT(&p->buf_free_list);

	/* register memory */
	ret = fi_mr_reg(rep->domain, p->buf_pool, p->num_bufs*p->buf_sz,
			FI_SEND|FI_RECV, 0,
			__atomic_add_fetch(&g.mr_key, 1, __ATOMIC_RELAXED),
			0, &p->buf_pool_mr, NULL);
	if (ret) {
		LLOG("fi_reg_mr() failed: %d, sz: %ld\n", ret, p->num_bufs*p->buf_sz);
		goto err_1;
	}

	/* initialize buffers */
	b = p->buf_pool;
	rb = p->buf_obj;
	for (i = 0; i < num_bufs; i++) {
		rb->buf = b;
		rb->rep = rep;
		rb->pool = p;
		rb->msg = (struct z_fi_message_hdr *)(b + rep->fi->ep_attr->msg_prefix_size);
		rb->buf_len = p->buf_sz; /* total size of buffer */
		rb->data_len = 0; /* # bytes of buffer used */
		LIST_INSERT_HEAD(&p->buf_free_list, rb, free_link);
		rb++;
		b += p->buf_sz;
	}

	/* insert into the vacant_pool list */
	LIST_INSERT_HEAD(&rep->vacant_pool, p, entry);
	rep->num_empty_pool++;

	return p;

 err_1:
	free(p);
 err_0:
	return NULL;
}

/* caller MUST hold buf_free_list_lock */
static void
_buffer_pool_free(struct z_fi_ep *rep, struct z_fi_buffer_pool *p)
{
	int ret;
	LIST_REMOVE(p, entry);
	ret = fi_close(&p->buf_pool_mr->fid);
	if (ret) {
		LLOG("fi_close(mr) failed: %d\n", ret);
		assert(0 == "fi_close(mr) failed");
		return;
	}
	free(p);
}

/* Allocate and register an endpoint's send and recv buffers. */
static int
_buffer_init_pool(struct z_fi_ep *rep)
{
	struct z_fi_buffer_pool *p;
	pthread_mutex_lock(&rep->buf_free_list_lock);
	p = _buffer_pool_new(rep, RQ_DEPTH + SQ_DEPTH + 4);
	pthread_mutex_unlock(&rep->buf_free_list_lock);
	if (!p)
		return errno;
	return 0;
}

static struct z_fi_buffer *
__buffer_alloc(struct z_fi_ep *rep)
{
	struct z_fi_buffer *rb;
	struct z_fi_buffer_pool *p;

	pthread_mutex_lock(&rep->buf_free_list_lock);
	p = LIST_FIRST(&rep->vacant_pool);
	if (!p) {
		p = _buffer_pool_new(rep, 64);
		if (!p) {
			rb = NULL;
			goto out;
		}
	}
	rb = LIST_FIRST(&p->buf_free_list);
	assert(rb);
	LIST_REMOVE(rb, free_link);
	rb->data_len = 0;  /* # bytes of buffer used */
	if (!p->num_alloc) {
		/* the pool was empty, and now it is not */
		rep->num_empty_pool--;
	}
	p->num_alloc++;
	if (LIST_EMPTY(&p->buf_free_list)) {
		/* pool is full (no vacancy), move it to `full_pool` list.  */
		LIST_REMOVE(p, entry);
		LIST_INSERT_HEAD(&rep->full_pool, p, entry);
	}
 out:
	pthread_mutex_unlock(&rep->buf_free_list_lock);
	return rb;
}

static void
__buffer_free(struct z_fi_buffer *rb)
{
	struct z_fi_ep *rep = rb->rep;
	struct z_fi_buffer_pool *p;

	pthread_mutex_lock(&rep->buf_free_list_lock);
	p = rb->pool;
	if (LIST_EMPTY(&p->buf_free_list)) {
		/* pool was full, now it has a vacancy */
		LIST_REMOVE(p, entry);
		LIST_INSERT_HEAD(&rep->vacant_pool, p, entry);
	}
	LIST_INSERT_HEAD(&p->buf_free_list, rb, free_link);
	p->num_alloc--;
	if (p->num_alloc == 0) {
		/* the pool becomes empty */
		if (rep->num_empty_pool >= 1) {
			/* we have enough empty pools */
			_buffer_pool_free(rep, p);
		} else {
			rep->num_empty_pool++;
		}
	}
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
	ref_get(&rep->ep.ref, "alloc context");
	LIST_INSERT_HEAD(&rep->active_ctxt_list, ctxt, active_ctxt_link);
	++rep->num_ctxts;
	return ctxt;
}

/* Must be called with the endpoint lock held */
static void _context_free(struct z_fi_context *ctxt)
{
	--ctxt->ep->num_ctxts;
	LIST_REMOVE(ctxt, active_ctxt_link);
	ref_put(&ctxt->ep->ep.ref, "alloc context");
	free(ctxt);
}

/* MUST hold rep->ep.lock . */
static void z_fi_flush(struct z_fi_ep *rep)
{
	/* Flush all outstanding requests. */
	struct z_fi_context *ctxt;
	struct zap_event ev = {
		.status = ZAP_ERR_FLUSH,
	};
	while ((ctxt = LIST_FIRST(&rep->active_ctxt_list))) {
		switch (ctxt->op) {
		case Z_FI_WC_SHARE:
			if (ctxt->u.send.rb)
				__buffer_free(ctxt->u.send.rb);
			goto ctxt_free;
		case Z_FI_WC_SEND_MAPPED:
			ev.type = ZAP_EVENT_SEND_MAPPED_COMPLETE;
			goto do_cb;
		case Z_FI_WC_SEND:
			ev.type = ZAP_EVENT_SEND_COMPLETE;
			if (ctxt->u.send.rb)
				__buffer_free(ctxt->u.send.rb);
			goto do_cb;
		case Z_FI_WC_RECV:
			/* RECV buffer is handled by the xprt, no callbacks. */
			goto ctxt_free;
		case Z_FI_WC_RDMA_WRITE:
			ev.type = ZAP_EVENT_WRITE_COMPLETE;
			goto do_cb;
		case Z_FI_WC_RDMA_READ:
			ev.type = ZAP_EVENT_READ_COMPLETE;
			goto do_cb;
		default:
			LOG_(rep, "invalid op type %d in queued i/o\n", ctxt->op);
			assert(0 == "Invalid op type\n");
			goto ctxt_free;
		}
	do_cb:
		ev.context = ctxt->usr_context;
		pthread_mutex_unlock(&rep->ep.lock);
		rep->ep.cb(&rep->ep, &ev);
		pthread_mutex_lock(&rep->ep.lock);
	ctxt_free:
		if (ctxt->pending) {
			pthread_mutex_lock(&rep->credit_lock);
			TAILQ_REMOVE(&rep->io_q, ctxt, pending_link);
			pthread_mutex_unlock(&rep->credit_lock);
		}
		__context_free(ctxt);
	}
	pthread_cond_signal(&rep->io_q_cond);
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
	struct fid_mr *mr;

	switch (ctxt->op) {
	    case Z_FI_WC_SEND:
	    case Z_FI_WC_SHARE:
		rb = ctxt->u.send.rb;
		rb->msg->credits = htons(rep->lcl_rq_credits);
		rep->lcl_rq_credits = 0;
		SEND_LOG(rep, ctxt);
		len = rb->data_len + rep->fi->ep_attr->msg_prefix_size;
		rc = fi_send(rep->fi_ep, rb->buf, len, fi_mr_desc(rb->pool->buf_pool_mr), 0, ctxt);

		DLOG("ZAP_WC_SEND rep %p ctxt %p rb %p len %d with %d credits rc %d\n",
		     rep, ctxt, rb, len, rep->lcl_rq_credits, rc);
		break;
	    case Z_FI_WC_SEND_MAPPED:
		rb = ctxt->u.send_mapped.rb;
		rb->msg->credits = htons(rep->lcl_rq_credits);
		rep->lcl_rq_credits = 0;
		SEND_LOG(rep, ctxt);
		/* NOTE: iov[0] contains prov_prefix and z_fi msg hdr using
		 *              rb->buf,
		 *       iov[1] contains application payload. */
		rc = fi_sendv(rep->fi_ep, ctxt->u.send_mapped.iov,
			      ctxt->u.send_mapped.mr_desc, 2, 0, ctxt);

		DLOG("ZAP_WC_SEND_MAPPED rep %p ctxt %p rb %p hdr_len %d "
				"payload_len %d with %d credits rc %d\n",
		     rep, ctxt, rb, ctxt->u.send_mapped.iov[0].iov_len,
		     ctxt->u.send_mapped.iov[1].iov_len, rep->lcl_rq_credits,
		     rc);
		break;
	    case Z_FI_WC_RDMA_WRITE:
		mr = z_fi_mr_get(rep, ctxt->u.rdma.src_map);
		if (!mr) {
			rc = errno;
			break;
		}
		rc = fi_write(rep->fi_ep, ctxt->u.rdma.src_addr, ctxt->u.rdma.len,
			      fi_mr_desc(mr), 0,
			      (uint64_t)ctxt->u.rdma.dst_addr,
			      z_fi_rkey_get(ctxt->u.rdma.dst_map), ctxt);

		DLOG("ZAP_WC_RDMA_WRITE rep %p ctxt %p src %p dst %p len %d\n",
		     rep, ctxt, ctxt->u.rdma.src_addr, ctxt->u.rdma.dst_addr, ctxt->u.rdma.len);
		break;
	    case Z_FI_WC_RDMA_READ:
		mr = z_fi_mr_get(rep, ctxt->u.rdma.dst_map);
		if (!mr) {
			rc = errno;
			break;
		}
		rc = fi_read(rep->fi_ep, ctxt->u.rdma.dst_addr, ctxt->u.rdma.len,
			     fi_mr_desc(mr), 0,
			     (uint64_t)ctxt->u.rdma.src_addr,
			     z_fi_rkey_get(ctxt->u.rdma.src_map), ctxt);

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
 *
 * Caller must hold rep->ep.lock.
 */
static void submit_pending(struct z_fi_ep *rep)
{
	int is_rdma;
	struct z_fi_context *ctxt;

	if (rep->ep.state != ZAP_EP_CONNECTED) {
		return;
	}

	pthread_mutex_lock(&rep->credit_lock);
	while (!TAILQ_EMPTY(&rep->io_q)) {
		ctxt = TAILQ_FIRST(&rep->io_q);
		is_rdma = (ctxt->op != Z_FI_WC_SEND);
		if (_get_credits(rep, is_rdma))
			goto out;

		TAILQ_REMOVE(&rep->io_q, ctxt, pending_link);
		DLOG("rep %p ctxt %p\n", rep, ctxt);
		ctxt->pending = 0;

		if (post_wr(rep, ctxt))
			__context_free(ctxt);
	}
	pthread_cond_signal(&rep->io_q_cond);
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
	if (!get_credits(rep, is_rdma)) {
		rc = post_wr(rep, ctxt);
	} else {
		ctxt->pending = 1;
		TAILQ_INSERT_TAIL(&rep->io_q, ctxt, pending_link);
		DLOG("pending op %s rep %p ctxt %p\n", z_fi_op_str[ctxt->op], rep, ctxt);
	}
	pthread_mutex_unlock(&rep->credit_lock);
	return rc;
}

static zap_err_t
__post_send(struct z_fi_ep *rep, struct z_fi_buffer *rbuf, enum z_fi_op op, void *cb_arg)
{
	int rc;
	struct z_fi_context *ctxt;

	DLOG("rep %p rbuf %p\n", rep, rbuf);

	pthread_mutex_lock(&rep->ep.lock);
	ctxt = __context_alloc(rep, cb_arg, op);
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
	pthread_t self = pthread_self();

	if (self != ep->thread->thread) {
		pthread_mutex_lock(&rep->credit_lock);
		while (!TAILQ_EMPTY(&rep->io_q)) {
			pthread_cond_wait(&rep->io_q_cond, &rep->credit_lock);
		}
		pthread_mutex_unlock(&rep->credit_lock);
	}

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
				char *data, size_t data_len, int tpi)
{
	int rc;
	zap_err_t zerr;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;

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

	ref_get(&rep->ep.ref, "connect");

	rc = fi_connect(rep->fi_ep, rep->fi->dest_addr,
			&rep->conn_data,
			rep->conn_data.data_len + sizeof(rep->conn_data));
	if (rc) {
		/* rc is -errno */
		zerr = zap_errno2zerr(-rc);
		goto err_2;
	}
	zerr = zap_io_thread_ep_assign(&rep->ep, tpi);
	if (zerr)
		goto err_2;

	return ZAP_ERR_OK;

	/* These are all synchronous errors. */
 err_2:
	pthread_mutex_lock(&rep->ep.lock);
	__teardown_conn(rep);
	pthread_mutex_unlock(&rep->ep.lock);
	ref_put(&rep->ep.ref, "connect");
 err_1:
	zap_ep_change_state(&rep->ep, ZAP_EP_CONNECTING, ZAP_EP_INIT);
 err_0:
	return zerr;
}

/* rep->ep.lock MUST be held */
static int __post_recv(struct z_fi_ep *rep, struct z_fi_buffer *rb)
{
	struct z_fi_context *ctxt;
	int rc;

	ctxt = __context_alloc(rep, NULL, Z_FI_WC_RECV);
	if (!ctxt) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}
	ctxt->u.recv.rb = rb;

	rc = fi_recv(rep->fi_ep, rb->buf, rb->buf_len, fi_mr_desc(rb->pool->buf_pool_mr), 0, ctxt);
	if (rc) {
		__context_free(ctxt);
		rc = zap_errno2zerr(rc);
	}
	DLOG("fi_recv %d, rep %p ctxt %p rb %p buf %p len %d\n", rc, rep, ctxt, rb, rb->buf, rb->buf_len);
out:
	return rc;
}

/*
 * Map a verbs provider errno to a zap error type.
 */
zap_err_t z_map_err(struct z_fi_ep *rep, struct fi_cq_err_entry *entry)
{
	if (!entry->err)
		return ZAP_ERR_OK;

	if (rep->fi->fabric_attr->prov_name &&
			0 == strcmp(rep->fi->fabric_attr->prov_name, "verbs"))
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

static void process_share_wc(struct z_fi_ep *rep, struct fi_cq_err_entry *entry)
{
	struct z_fi_context *ctxt = entry->op_context;
	if (!entry->err && ctxt->u.send.rb)
		__buffer_free(ctxt->u.send.rb);
}

static void process_send_wc(struct z_fi_ep *rep, struct fi_cq_err_entry *entry)
{
	struct z_fi_context *ctxt = entry->op_context;
	struct zap_event zev = {
			.type = ZAP_EVENT_SEND_COMPLETE,
			.context = ctxt->usr_context,
			.status = z_map_err(rep, entry),
	};
	rep->ep.cb(&rep->ep, &zev);
	if (!entry->err && ctxt->u.send.rb)
		__buffer_free(ctxt->u.send.rb);
}

static void process_send_mapped_wc(struct z_fi_ep *rep,
				   struct fi_cq_err_entry *entry)
{
	struct z_fi_context *ctxt = entry->op_context;
	struct zap_event zev = {
			.type = ZAP_EVENT_SEND_MAPPED_COMPLETE,
			.context = ctxt->usr_context,
			.status = z_map_err(rep, entry),
		};
	rep->ep.cb(&rep->ep, &zev);
	if (!entry->err && ctxt->u.send_mapped.rb)
		__buffer_free(ctxt->u.send_mapped.rb);
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
	struct zap_map *map;
	zap_err_t zerr;

	zerr = zap_map(&map, (void *)(unsigned long)sh->va,
				ntohl(sh->len), ntohl(sh->acc));
	if (zerr) {
		LOG_(rep, "%s:%d: Failed to create a map in %s (%s)\n",
			__FILE__, __LINE__, __func__, __zap_err_str[zerr]);
		return;
	}
	map->type = ZAP_MAP_REMOTE;
	map->ep = &rep->ep;
	ref_get(&map->ep->ref, "zap_map/rendezvous"); /* put by z_fi_unmap() */
	map->mr[ZAP_FABRIC] = (void*)sh->rkey;

	memset(&zev, 0, sizeof zev);
	zev.type = ZAP_EVENT_RENDEZVOUS;
	zev.status = ZAP_ERR_OK;
	zev.map = map;
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

	pthread_mutex_lock(&rep->ep.lock);
	switch (rep->ep.state) {
	case ZAP_EP_CONNECTED:
	case ZAP_EP_CONNECTING:
	case ZAP_EP_ACCEPTING:
		ret = __post_recv(rep, rb);
		pthread_mutex_unlock(&rep->ep.lock);
		if (ret) {
			LOG_(rep, "error %d (%s) posting recv buffers\n", ret, zap_err_str(ret));
			__buffer_free(rb);
			goto out;
		}
		break;
	default:
		pthread_mutex_unlock(&rep->ep.lock);
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
	LOG_(rep, "msg type %d has invalid data len %zd\n", msg_type, rb->data_len);
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
		pthread_mutex_lock(&rep->ep.lock);
		rc = __post_recv(rep, rbuf);
		pthread_mutex_unlock(&rep->ep.lock);
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
				char *data, size_t data_len, int tpi)
{
	int ret;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct z_fi_accept_msg *msg;

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

	ref_get(&rep->ep.ref, "accept");
	ret = __setup_conn(rep, NULL, 0);
	if (ret)
		goto err_0;

	ret = fi_accept(rep->fi_ep, msg, len);
	if (ret) {
		ret = zap_errno2zerr(errno);
		goto err_1;
	}

	ret = zap_io_thread_ep_assign(&rep->ep, tpi);
	if (ret)
		goto err_1;

	rep->conn_req_decision = Z_FI_PASSIVE_ACCEPT;
	free(msg);
	return ZAP_ERR_OK;
err_1:
	pthread_mutex_lock(&rep->ep.lock);
	__teardown_conn(rep);
	pthread_mutex_unlock(&rep->ep.lock);
err_0:
	ref_put(&rep->ep.ref, "accept");
	free(msg);
	return ret;
}

static void scrub_cq(struct z_fi_ep *rep)
{
	int			ret;
	struct z_fi_context	*ctxt;
	struct fi_cq_err_entry	entry;
	struct fid		*fid[1];

	DLOG("rep %p\n", rep);
	while (1) {
		memset(&entry, 0, sizeof(entry));
		ret = fi_cq_read(rep->cq, &entry, 1);
		if ((ret == 0) || (ret == -FI_EAGAIN)) {
			fid[0] = &rep->cq->fid;
			ret = fi_trywait(rep->fabric, fid, 1);
			if (ret == -FI_EAGAIN)
				continue;
			break;
		}
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
		case Z_FI_WC_SHARE:
			DLOG("got ZAP_WC_SEND rep %p ctxt %p rb %p err %d proverr %d\n",
			     rep, ctxt, ctxt->u.send.rb, entry.err, entry.prov_errno);
			process_share_wc(rep, &entry);
			put_sq(rep);
			if (entry.err && ctxt->u.send.rb)
				__buffer_free(ctxt->u.send.rb);
			break;
		case Z_FI_WC_SEND:
			DLOG("got ZAP_WC_SEND rep %p ctxt %p rb %p err %d proverr %d\n",
			     rep, ctxt, ctxt->u.send.rb, entry.err, entry.prov_errno);
			process_send_wc(rep, &entry);
			put_sq(rep);
			if (entry.err && ctxt->u.send.rb)
				__buffer_free(ctxt->u.send.rb);
			break;
		case Z_FI_WC_SEND_MAPPED:
			DLOG("got ZAP_WC_SEND rep %p ctxt %p rb %p err %d proverr %d\n",
			     rep, ctxt, ctxt->u.send_mapped.rb, entry.err, entry.prov_errno);
			process_send_mapped_wc(rep, &entry);
			put_sq(rep);
			if (entry.err && ctxt->u.send.rb)
				__buffer_free(ctxt->u.send.rb);
			break;
		case Z_FI_WC_RDMA_WRITE:
			DLOG("got ZAP_WC_RDMA_WRITE rep %p ctxt %p src %p dst %p err %d proverr %d\n",
			     rep, ctxt, ctxt->u.rdma.src_addr, ctxt->u.rdma.dst_addr,
			     entry.err, entry.prov_errno);
			process_write_wc(rep, &entry);
			put_sq(rep);
			break;
		case Z_FI_WC_RDMA_READ:
			DLOG("got ZAP_WC_RDMA_READ rep %p ctxt %p src %p dst %p err %d proverr %d\n",
			     rep, ctxt, ctxt->u.rdma.src_addr, ctxt->u.rdma.dst_addr,
			     entry.err, entry.prov_errno);
			process_read_wc(rep, &entry);
			put_sq(rep);
			break;
		case Z_FI_WC_RECV:
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

	rep->cm_epoll_ctxt.rep = rep;
	rep->cm_epoll_ctxt.type = Z_FI_EPOLL_CM;
	rep->cq_epoll_ctxt.rep = rep;
	rep->cq_epoll_ctxt.type = Z_FI_EPOLL_CQ;

	rep->cm_fd = -1;
	rep->cq_fd = -1;

	TAILQ_INIT(&rep->io_q);
	pthread_cond_init(&rep->io_q_cond, NULL);
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

	new_ep = zap_new(rep->ep.z, rep->ep.cb);
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
	ref_get(&rep->ep.ref, "conn req");
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

	zap_io_thread_ep_release(&rep->ep);

	pthread_mutex_lock(&rep->ep.lock);
	oldstate = rep->ep.state;
	rep->ep.state = ZAP_EP_ERROR;
	pthread_mutex_unlock(&rep->ep.lock);

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
			ref_put(&rep->ep.ref, "accept");
		}
		break;
	    case ZAP_EP_CONNECTING:
	    case ZAP_EP_ERROR:
		zev.type = ZAP_EVENT_CONNECT_ERROR;
		rep->ep.cb(&rep->ep, &zev);
		ref_put(&rep->ep.ref, "connect");
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

	pthread_mutex_lock(&rep->ep.lock);
	rep->ep.state = ZAP_EP_ERROR;
	pthread_mutex_unlock(&rep->ep.lock);
	rep->ep.cb(&rep->ep, &zev);
	ref_put(&rep->ep.ref, "connect");
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
}

static void _deliver_disconnected(struct z_fi_ep *rep)
{
	struct zap_event zev = {
		.type = ZAP_EVENT_DISCONNECTED,
		.status = ZAP_ERR_OK,
	};
	rep->ep.cb(&rep->ep, &zev);

	if (rep->conn_req_decision != Z_FI_PASSIVE_NONE)
		ref_put(&rep->ep.ref, "accept");
	else
		ref_put(&rep->ep.ref, "connect");
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
	/* Flush outstanding requests. */
	z_fi_flush(rep);
	pthread_mutex_unlock(&rep->ep.lock);

	zap_io_thread_ep_release(&rep->ep);
	__deliver_disconnected(rep);
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
	struct fid		*fid[1];

	DLOG("rep %p\n", rep);
	ref_get(&rep->ep.ref, "handle eq event");
	while (1) {
		memset(&entry, 0, sizeof(entry));
		ret = fi_eq_read(rep->eq, &event, &entry, sizeof(entry), 0);
		if ((ret == 0) || (ret == -FI_EAGAIN)) {
			fid[0] = &rep->eq->fid;
			ret = fi_trywait(rep->fabric, fid, 1);
			if (ret == -FI_EAGAIN)
				continue;
			break;
		}
		if (ret == -FI_EAVAIL) {
			fi_eq_readerr(rep->eq, &entry, 0);
			event = -FI_EAVAIL;
		} else if (ret < 0) {
			DLOG("fi_eq_read error %d\n", ret);
			break;
		}
		cm_event_handler(rep, event, &entry, ret);
	}
	ref_put(&rep->ep.ref, "handle eq event");
	DLOG("done with rep %p\n", rep);
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

	rc = fi_listen(rep->fi_pep);
	if (rc)
		goto err_1;

	zerr = zap_io_thread_ep_assign(&rep->ep, -1);
	if (zerr != ZAP_ERR_OK)
		goto err_1;

	return ZAP_ERR_OK;

 err_1:
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
	ctxt = __context_alloc(rep, NULL, Z_FI_WC_SEND);
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
	size_t sz = *sa_len;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;

	if (!rep->fi_ep) {
		return ZAP_ERR_NOT_CONNECTED;
	}
	ret = fi_getname(&rep->fi_ep->fid, local_sa, &sz);
	ret = ret || fi_getpeer(rep->fi_ep, remote_sa, &sz);
	*sa_len = sz;
	return ret;
}

static zap_err_t z_fi_send2(zap_ep_t ep, char *buf, size_t len, void *cb_arg)
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

	rc = __post_send(rep, rbuf, Z_FI_WC_SEND, cb_arg);
	if (rc)
		__buffer_free(rbuf);

	return rc;
 out:
	pthread_mutex_unlock(&rep->ep.lock);
 out_2:
	return rc;
}

static zap_err_t z_fi_send(zap_ep_t ep, char *buf, size_t len)
{
	return z_fi_send2(ep, buf, len, NULL);
}

static zap_err_t z_fi_send_mapped(zap_ep_t ep, zap_map_t map,
				  void *buf, size_t len, void *context)
{
	int rc, hdr_len;
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct z_fi_context *ctxt;
	struct z_fi_buffer *rbuf;
	struct fid_mr *mr;

	if (map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;
	if (z_map_access_validate(map, buf, len, 0))
		return ZAP_ERR_LOCAL_LEN;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc)
		goto out;
	mr = z_fi_mr_get(rep, map); /* do not free mr */
	if (!mr)
		goto out;
	rbuf = __buffer_alloc(rep);
	if (!rbuf) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}
	if (!_buffer_fits(rbuf, len+sizeof(struct z_fi_message_hdr))) {
		rc = ZAP_ERR_LOCAL_LEN;
		__buffer_free(rbuf);
		goto out;
	}
	ctxt = __context_alloc(rep, context, Z_FI_WC_SEND_MAPPED);
	if (!ctxt) {
		__buffer_free(rbuf);
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}
	/* prefix + hdr */
	hdr_len = rep->fi->ep_attr->msg_prefix_size + sizeof(struct z_fi_message_hdr);
	rbuf->msg->msg_type = htons(Z_FI_MSG_SEND);
	ctxt->u.send_mapped.rb = rbuf;
	ctxt->u.send_mapped.iov[0].iov_base = rbuf->buf;
	ctxt->u.send_mapped.iov[0].iov_len = hdr_len;
	ctxt->u.send_mapped.mr_desc[0] = fi_mr_desc(rbuf->pool->buf_pool_mr);
	/* payload */
	ctxt->u.send_mapped.iov[1].iov_base = buf;
	ctxt->u.send_mapped.iov[1].iov_len = len;
	ctxt->u.send_mapped.mr_desc[1] = fi_mr_desc(mr);

	rc = submit_wr(rep, ctxt, 0);
	if (rc) {
		__context_free(ctxt);
		__buffer_free(rbuf);
	}
out:
	pthread_mutex_unlock(&rep->ep.lock);
	return rc;
}

static zap_err_t z_fi_share(zap_ep_t ep, zap_map_t map,
				const char *msg, size_t msg_len)
{
	struct z_fi_ep *rep = (struct z_fi_ep *)ep;
	struct z_fi_share_msg *sm;
	struct z_fi_buffer *rbuf;
	struct fid_mr *mr;
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
	mr = z_fi_mr_get(rep, map);
	if (!mr)
		return ZAP_ERR_RESOURCE;

	if (!_buffer_fits(rbuf, sz)) {
		__buffer_free(rbuf);
		return ZAP_ERR_NO_SPACE;
	}

	sm = (struct z_fi_share_msg *)rbuf->msg;
	sm->hdr.msg_type = htons(Z_FI_MSG_RENDEZVOUS);
	sm->rkey = fi_mr_key(mr);
	sm->va = (uint64_t)map->addr;
	sm->len = htonl(map->len);
	sm->acc = htonl(map->acc);
	if (msg_len)
		memcpy(sm->msg, msg, msg_len);

	rbuf->data_len = sz;

	rc = __post_send(rep, rbuf, Z_FI_WC_SHARE, NULL);
	if (rc)
		__buffer_free(rbuf);
	return rc;
}

static zap_err_t z_fi_unmap(zap_map_t map)
{
	struct z_fi_map *zm = (struct z_fi_map *)map->mr[ZAP_FABRIC];
	int i;

	switch (map->type) {
	case ZAP_MAP_LOCAL:
		for (i = 0; i < ZAP_FI_MAX_DOM; i++) {
			if (zm->mr[i])
				fi_close(&zm->mr[i]->fid);
		}
		free(zm);
		break;
	case ZAP_MAP_REMOTE:
		ref_put(&map->ep->ref, "zap_map/rendezvous");
		/* map->mr[ZAP_FABRIC] is rkey. Don't free it. */
		break;
	default:
		assert(0 == "bad map type");
	}

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
	struct z_fi_context *ctxt;

	if (src_map->type != ZAP_MAP_LOCAL || dst_map->type != ZAP_MAP_REMOTE)
		return ZAP_ERR_INVALID_MAP_TYPE;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc)
		goto out;

	ctxt = __context_alloc(rep, context, Z_FI_WC_RDMA_WRITE);
	if (!ctxt) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}
	ctxt->u.rdma.src_map  = src_map;
	ctxt->u.rdma.dst_map  = dst_map;
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
	struct z_fi_context *ctxt;

	if (src_map->type != ZAP_MAP_REMOTE || dst_map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;

	pthread_mutex_lock(&rep->ep.lock);
	rc = __ep_state_check(rep);
	if (rc)
		goto out;

	ctxt = __context_alloc(rep, context, Z_FI_WC_RDMA_READ);
	if (!ctxt) {
		rc = ZAP_ERR_RESOURCE;
		goto out;
	}
	ctxt->u.rdma.src_map  = src_map;
	ctxt->u.rdma.dst_map  = dst_map;
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

__attribute__((unused))
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

static void z_fi_io_thread_cleanup(void *arg)
{
	struct z_fi_io_thread *thr = arg;
	if (thr->efd >= 0)
		close(thr->efd);
	zap_io_thread_release(&thr->zap_io_thread);
	free(thr);
}

static void *z_fi_io_thread_proc(void *arg)
{
	struct z_fi_io_thread *thr = arg;
	int rc, n, i;
	sigset_t sigset;
	const int N_EV = 16;
	int n_cq, n_cm;
	struct epoll_event ev[N_EV];
	struct z_fi_ep *cq_reps[N_EV], *rep;
	struct z_fi_ep *cm_reps[N_EV];
	struct z_fi_epoll_ctxt *ctxt;

	thr->zap_io_thread.stat->tid = syscall(SYS_gettid);

	sigfillset(&sigset);
	rc = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	if (rc) {
		LOG("ERROR: pthread_sigmask errno: %d\n", errno);
		assert(0);
		return NULL;
	}

	pthread_cleanup_push(z_fi_io_thread_cleanup, arg);
 loop:
	zap_thrstat_wait_start(thr->zap_io_thread.stat);
	n = epoll_wait(thr->efd, ev, N_EV, -1);
	zap_thrstat_wait_end(thr->zap_io_thread.stat);
	n_cq = 0;
	n_cm = 0;

	/* CQ events */
	for (i = 0; i < n; i++) {
		ctxt = ev[i].data.ptr;
		switch (ctxt->type) {
		case Z_FI_EPOLL_CM:
			/* CM events will be processed after CQ events */
			ref_get(&ctxt->rep->ep.ref, "CM_Q");
			cm_reps[n_cm++] = ctxt->rep;
			break;
		case Z_FI_EPOLL_CQ:
			ref_get(&ctxt->rep->ep.ref, "PENDING_Q");
			cq_reps[n_cq++] = ctxt->rep;
			if (ctxt->rep->ep.state == ZAP_EP_CONNECTING ||
					ctxt->rep->ep.state == ZAP_EP_ACCEPTING)
				break;
			scrub_cq(ctxt->rep);
			break;
		default:
			assert(0 == "Unexpected epoll event type");
			break;
		}
	}

	/* CM events */
	for (i = 0; i < n_cm; i++) {
		scrub_eq(cm_reps[i]);
		ref_put(&cm_reps[i]->ep.ref, "CM_Q");
	}

	/* Submit pending  */
	for (i = 0; i < n_cq; i++) {
		rep = cq_reps[i];
		pthread_mutex_lock(&rep->ep.lock);
		if (rep->ep.state == ZAP_EP_ERROR) {
			z_fi_flush(rep); /* Flush the RECV posted in __setup_conn() */
			if (rep->cq_fd >= 0) {
				struct epoll_event ev; /* ignored */
				rc = epoll_ctl(thr->efd, EPOLL_CTL_DEL, rep->cq_fd, &ev);
				if (rc)
					LLOG("Warning: EPOLL_CTL_DEL cq_fd error: %d\n", errno);
				rep->cq_fd = -1;
			}

		} else {
			submit_pending(rep);
		}
		pthread_mutex_unlock(&rep->ep.lock);
		ref_put(&rep->ep.ref, "PENDING_Q");
	}
	goto loop;

	pthread_cleanup_pop(1);
	return NULL;
}

static zap_io_thread_t z_fi_io_thread_create(zap_t z)
{
	zap_err_t zerr;
	int rc;
	struct z_fi_io_thread *thr;

	thr = malloc(sizeof(*thr));
	if (!thr) {
		LLOG("malloc() failed: %d\n", errno);
		goto out;
	}
	zerr = zap_io_thread_init(&thr->zap_io_thread, z, "z_fi_io", ZAP_ENV_INT(ZAP_THRSTAT_WINDOW));
	if (zerr) {
		LLOG("zap_io_thread_init() failed, zerr: %d\n", zerr);
		goto err1;
	}
	thr->efd = epoll_create1(EPOLL_CLOEXEC);
	if (thr->efd < 0) {
		LLOG("epoll_create1() failed, errno: %d\n", errno);
		goto err2;
	}
	rc = pthread_create(&thr->zap_io_thread.thread, NULL, z_fi_io_thread_proc, thr);
	if (rc) {
		LLOG("pthread_create() failed, errno: %d\n", rc);
		goto err3;
	}
	pthread_setname_np(thr->zap_io_thread.thread, "z_fi_io");
	goto out;

 err3:
	close(thr->efd);
 err2:
	zap_io_thread_release(&thr->zap_io_thread);
 err1:
	free(thr);
 out:
	return &thr->zap_io_thread;
}

static zap_err_t z_fi_io_thread_cancel(zap_io_thread_t t)
{
	int rc;
	rc = pthread_cancel(t->thread);
	switch (rc) {
	case ESRCH: /* cleaning up structure w/o running thread b/c of fork */
		((z_fi_io_thread_t)t)->efd = -1; /* b/c of CLOEXEC */
		z_fi_io_thread_cleanup(t);
	case 0:
		return ZAP_ERR_OK;
	default:
		return ZAP_ERR_LOCAL_OPERATION;
	}
}

static zap_err_t z_fi_io_thread_ep_assign(zap_io_thread_t t, zap_ep_t ep)
{
	/* listening endpoint will only have cm_fd, while active/passive
	 * endpoints will have both cm_fd and cq_fd */
	struct z_fi_io_thread *thr = (void*)t;
	struct z_fi_ep *rep = (void*)ep;
	int rc;
	zap_err_t zerr = ZAP_ERR_OK;
	struct epoll_event ev = { .events = EPOLLIN };

	ref_get(&ep->ref, "IO_THREAD");

	/* Add cm_fd to thread's epoll */
	assert(rep->cm_fd >= 0);
	ev.data.ptr = &rep->cm_epoll_ctxt;
	rc = epoll_ctl(thr->efd, EPOLL_CTL_ADD, rep->cm_fd, &ev);
	if (rc) {
		LLOG("epoll_ctl() error: %d\n", errno);
		zerr = zap_errno2zerr(errno);
		goto err_0;
	}

	/* Add cq_fd to thread's epoll */
	if (rep->cq_fd < 0) /* skip cq for listening endpoint */
		goto out;
	ev.data.ptr = &rep->cq_epoll_ctxt;
	rc = epoll_ctl(thr->efd, EPOLL_CTL_ADD, rep->cq_fd, &ev);
	if (rc) {
		LLOG("epoll_ctl() error: %d\n", errno);
		zerr = zap_errno2zerr(errno);
		goto err_1;
	}
 out:
	return ZAP_ERR_OK;

 err_1:
	epoll_ctl(thr->efd, EPOLL_CTL_DEL, rep->cm_fd, &ev);
 err_0:
	ref_put(&ep->ref, "IO_THREAD");
	return zerr;
}

static zap_err_t z_fi_io_thread_ep_release(zap_io_thread_t t, zap_ep_t ep)
{
	struct z_fi_io_thread *thr = (void*)t;
	struct z_fi_ep *rep = (void*)ep;
	int rc;
	struct epoll_event ev = { .events = EPOLLIN }; /* ignored */
	assert(rep->cm_fd >= 0);
	rc = epoll_ctl(thr->efd, EPOLL_CTL_DEL, rep->cm_fd, &ev);
	if (rc)
		LLOG("Warning: EPOLL_CTL_DEL cm_fd error: %d\n", errno);
	if (rep->cq_fd >= 0) {
		rc = epoll_ctl(thr->efd, EPOLL_CTL_DEL, rep->cq_fd, &ev);
		if (rc)
			LLOG("Warning: EPOLL_CTL_DEL cq_fd error: %d\n", errno);
	}
	ref_put(&ep->ref, "IO_THREAD");
	return ZAP_ERR_OK;
}

static int init_once()
{
	static int init_complete = 0;
	const char *env;

	if (init_complete)
		return 0;

	env = getenv(ZAP_FABRIC_INFO_LOG);
	if (env)
		z_fi_info_log_on = atoi(env);

	init_complete = 1;

	zflog = ovis_log_register("xprt.zap.fabric", "Messages for zap_fabric");
	if (!zflog) {
		ovis_log(NULL, OVIS_LWARN, "Failed to create zap_fabric's "
				"log subsystem. Error %d.\n", errno);
	}
//	atexit(z_fi_cleanup);
	return 0;

}

zap_err_t zap_transport_get(zap_t *pz, zap_mem_info_fn_t mem_info_fn)
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
	z->send2 = z_fi_send2;
	z->send_mapped = z_fi_send_mapped;
	z->read = z_fi_read;
	z->write = z_fi_write;
	z->unmap = z_fi_unmap;
	z->share = z_fi_share;
	z->get_name = z_get_name;
	z->io_thread_create = z_fi_io_thread_create;
	z->io_thread_cancel = z_fi_io_thread_cancel;
	z->io_thread_ep_assign = z_fi_io_thread_ep_assign;
	z->io_thread_ep_release = z_fi_io_thread_ep_release;

	*pz = z;
	return ZAP_ERR_OK;

 err_0:
	return ZAP_ERR_RESOURCE;
}
