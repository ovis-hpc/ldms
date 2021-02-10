/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014-2017,2019-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2014-2017,2019-2020 Open Grid Computing, Inc. All rights reserved.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include <endian.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>

#include "ovis_util/os_util.h"
#include "coll/rbt.h"
#include "mmalloc/mmalloc.h"

#include "zap_ugni.h"

#define VERSION_FILE "/proc/version"

#define ZUGNI_LIST_REMOVE(elm, link) do { \
	LIST_REMOVE((elm), link); \
	(elm)->link.le_next = 0; \
	(elm)->link.le_prev = 0; \
} while(0)

static char *format_4tuple(struct zap_ep *ep, char *str, size_t len)
{
	struct sockaddr la = {0};
	struct sockaddr ra = {0};
	char addr_str[INET_ADDRSTRLEN];
	struct sockaddr_in *l = (struct sockaddr_in *)&la;
	struct sockaddr_in *r = (struct sockaddr_in *)&ra;
	socklen_t sa_len = sizeof(la);
	size_t sz;

	(void) zap_get_name(ep, &la, &ra, &sa_len);
	sz = snprintf(str, len, "lcl=%s:%hu <--> ",
		inet_ntop(AF_INET, &l->sin_addr, addr_str, INET_ADDRSTRLEN),
		ntohs(l->sin_port));
	if (sz + 1 > len)
		return NULL;
	len -= sz;
	sz = snprintf(&str[sz], len, "rem=%s:%hu",
		inet_ntop(AF_INET, &r->sin_addr, addr_str, INET_ADDRSTRLEN),
		ntohs(r->sin_port));
	if (sz + 1 > len)
		return NULL;
	return str;
}

#define LOG_(uep, fmt, ...) do { \
	if ((uep) && (uep)->ep.z && (uep)->ep.z->log_fn) { \
		char name[ZAP_UGNI_EP_NAME_SZ]; \
		format_4tuple(&(uep)->ep, name, ZAP_UGNI_EP_NAME_SZ); \
		uep->ep.z->log_fn("zap_ugni: %s " fmt, name, ##__VA_ARGS__); \
	} \
} while(0);

#define LOG(...) do { \
	zap_ugni_log("zap_ugni: " __VA_ARGS__); \
} while(0);

#ifdef DEBUG
#define DLOG_(uep, fmt, ...) do { \
	if ((uep) && (uep)->ep.z && (uep)->ep.z->log_fn) { \
		char name[ZAP_UGNI_EP_NAME_SZ]; \
		format_4tuple(&(uep)->ep, name, ZAP_UGNI_EP_NAME_SZ); \
		uep->ep.z->log_fn("zap_ugni [DEBUG]: %s " fmt, name, ##__VA_ARGS__); \
	} \
} while(0);

#define DLOG(...) do { \
	zap_ugni_log("zap_ugni [DEBUG]: " __VA_ARGS__); \
} while(0);
#else
#define DLOG_(UEP, ...)
#define DLOG(...)
#endif

int init_complete = 0;

static void zap_ugni_default_log(const char *fmt, ...);
static zap_log_fn_t zap_ugni_log = zap_ugni_default_log;
zap_mem_info_fn_t __mem_info_fn = NULL;

/* 100000 because the Cray node names have only 5 digits, e.g, nid00000  */
#define ZAP_UGNI_MAX_NUM_NODE 100000

/* objects for checking node states */
#define ZAP_UGNI_NODE_GOOD 7
static ovis_scheduler_t node_state_sched;
static pthread_t node_state_thread;

#ifdef DEBUG
#define ZAP_UGNI_RCA_LOG_THS 1
#else
#define ZAP_UGNI_RCA_LOG_THS 1
#endif /* DEBUG */
struct zap_ugni_node_state {
	unsigned long state_interval_us;
	unsigned long state_offset_us;
	int state_ready;
	int check_state;
	int rca_log_thresh;
	int rca_get_failed;
	int *node_state;
} _node_state = {0};

struct zap_ugni_defer_disconn_ev {
	struct z_ugni_ep *uep;
	struct event *disconn_ev;
	struct timeval retry_count; /* Retry unbind counter */
};

#define ZAP_UGNI_DISCONNECT_TIMEOUT	10
static int zap_ugni_disconnect_timeout;

/*
 * Maximum number of endpoints zap_ugni will handle
 */
#define ZAP_UGNI_MAX_NUM_EP 32000
static int zap_ugni_max_num_ep;
static uint32_t *zap_ugni_ep_id;

static LIST_HEAD(mh_list, ugni_mh) mh_list;
static pthread_mutex_t ugni_mh_lock;

static ovis_scheduler_t io_sched;
static pthread_t io_thread;
static pthread_t cq_thread;
static pthread_t error_thread;

static void *io_thread_proc(void *arg);
static void *cq_thread_proc(void *arg);
static void *error_thread_proc(void *arg);

static void ugni_sock_event(ovis_event_t ev);
static void ugni_sock_read(ovis_event_t ev);
static void ugni_sock_write(ovis_event_t ev);
static void ugni_sock_connect(ovis_event_t ev);

static void stalled_timeout_cb(ovis_event_t ev);
static zap_err_t __setup_connection(struct z_ugni_ep *uep);
static zap_err_t z_ugni_close(zap_ep_t ep);

static int __get_nodeid(struct sockaddr *sa, socklen_t sa_len);
static int __check_node_state(int node_id);

static void z_ugni_destroy(zap_ep_t ep);

static LIST_HEAD(, z_ugni_ep) z_ugni_list = LIST_HEAD_INITIALIZER(0);
static pthread_mutex_t z_ugni_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct zap_ugni_post_desc_list stalled_desc_list = LIST_HEAD_INITIALIZER(0);
#define ZAP_UGNI_STALLED_TIMEOUT	60 /* 1 minute */
static int zap_ugni_stalled_timeout;

#ifdef DEBUG
static LIST_HEAD(, z_ugni_ep) deferred_list = LIST_HEAD_INITIALIZER(0);
static pthread_mutex_t deferred_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t ugni_io_count = 0;
#endif /* DEBUG */
static uint32_t ugni_post_count;
static uint32_t ugni_leaked_count;
static uint32_t ugni_post_max;
static uint32_t ugni_post_id;
static gni_cq_handle_t ugni_old_cq; /* CQ replaced due to leaking descriptors */

static pthread_mutex_t ugni_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t inst_id_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t cq_full_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cq_full_cond = PTHREAD_COND_INITIALIZER;

static int zap_ugni_dom_initialized = 0;
static struct zap_ugni_dom {
	zap_ugni_type_t type;
	uid_t euid;
	uint8_t ptag;
	uint32_t cookie;
	uint32_t pe_addr;
	uint32_t inst_id;
	uint32_t cq_depth;
	gni_job_limits_t limits;
	gni_cdm_handle_t cdm;
	gni_nic_handle_t nic;
	gni_cq_handle_t cq;
} _dom = {0};

static void zap_ugni_default_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

int z_rbn_cmp(void *a, void *b)
{
	uint32_t x = (uint32_t)(uint64_t)a;
	uint32_t y = (uint32_t)(uint64_t)b;
	return x - y;
}

const char *zap_ugni_msg_type_str(zap_ugni_msg_type_t type)
{
	if (type < ZAP_UGNI_MSG_NONE || ZAP_UGNI_MSG_TYPE_LAST < type)
		return "ZAP_UGNI_MSG_OUT_OF_RANGE";
	return __zap_ugni_msg_type_str[type];
}

const char *zap_ugni_type_str(zap_ugni_type_t type)
{
	if (type < ZAP_UGNI_TYPE_NONE || ZAP_UGNI_TYPE_LAST < type)
		return "ZAP_UGNI_TYPE_OUT_OF_RANGE";
	return __zap_ugni_type_str[type];
}

/*
 * Use KEEP-ALIVE packets to shut down a connection if the remote peer fails
 * to respond for 10 minutes
 */
#define ZAP_SOCK_KEEPCNT	3	/* Give up after 3 failed probes */
#define ZAP_SOCK_KEEPIDLE	10	/* Start probing after 10s of inactivity */
#define ZAP_SOCK_KEEPINTVL	2	/* Probe couple seconds after idle */

static int __set_keep_alive(int sock)
{
	int rc;
	int optval;

	optval = ZAP_SOCK_KEEPCNT;
	rc = setsockopt(sock, SOL_TCP, TCP_KEEPCNT, &optval, sizeof(int));

	optval = ZAP_SOCK_KEEPIDLE;
	rc = setsockopt(sock, SOL_TCP, TCP_KEEPIDLE, &optval, sizeof(int));

	optval = ZAP_SOCK_KEEPINTVL;
	rc = setsockopt(sock, SOL_TCP, TCP_KEEPINTVL, &optval, sizeof(int));

	optval = 1;
	rc = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(int));
	return rc;
}

static int __set_sockbuf_sz(int sockfd)
{
	int rc;
	size_t optval = UGNI_SOCKBUF_SZ;
	rc = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));
	if (rc)
		return rc;
	rc = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
	return rc;
}

static zap_err_t __node_state_check(struct z_ugni_ep *uep)
{
	/* node state validation */
	if (!_node_state.check_state)
		return ZAP_ERR_OK;
	if (uep->node_id == -1) {
		struct sockaddr lsa, sa;
		socklen_t sa_len;
		zap_err_t zerr;
		zerr = zap_get_name(&uep->ep, &lsa, &sa, &sa_len);
		if (zerr) {
			DLOG("zap_get_name() error: %d\n", zerr);
			return ZAP_ERR_ENDPOINT;
		}
		uep->node_id = __get_nodeid(&sa, sa_len);
	}
	if (uep->node_id != -1) {
		if (__check_node_state(uep->node_id)) {
			DLOG("Node %d is in a bad state\n", uep->node_id);
			z_ugni_close(&uep->ep);
			return ZAP_ERR_ENDPOINT;
		}
	}
	return ZAP_ERR_OK;
}

static int __sock_nonblock(int fd)
{
	int rc;
	int fl;
	fl = fcntl(fd, F_GETFL);
	if (fl == -1)
		return errno;
	rc = fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	if (rc)
		return errno;
	return 0;
}

/* caller must have uep->ep.lock held */
static int __enable_epoll_out(struct z_ugni_ep *uep)
{
	int rc;
	if (uep->io_ev.param.epoll_events & EPOLLOUT)
		return 0; /* already enabled */
	rc = ovis_scheduler_epoll_event_mod(io_sched, &uep->io_ev, EPOLLIN|EPOLLOUT);
	return rc;
}

/* caller must have uep->ep.lock held */
static int __disable_epoll_out(struct z_ugni_ep *uep)
{
	int rc;
	if ((uep->io_ev.param.epoll_events & EPOLLOUT) == 0)
		return 0; /* already disabled */
	rc = ovis_scheduler_epoll_event_mod(io_sched, &uep->io_ev, EPOLLIN);
	return rc;
}

uint32_t zap_ugni_get_ep_gn(int id)
{
	return zap_ugni_ep_id[id];
}

int zap_ugni_is_ep_gn_matched(int id, uint32_t gn)
{
	if (zap_ugni_ep_id[id] == gn)
		return 1;
	return 0;
}

/*
 * Caller must hold the z_ugni_list_mutex lock;
 */
int zap_ugni_get_ep_id()
{
	static uint32_t current_gn = 0;
	static int idx = -1;
	int count = -1;
	do {
		++count;
		if (count == zap_ugni_max_num_ep) {
			/*
			 * All slots have been occupied.
			 */
			LOG("Not enough endpoint slots. "
				"Considering setting the ZAP_UGNI_MAX_NUM_EP"
				"environment variable to a larger number.\n");
			return -1;
		}

		++idx;
		if (idx >= zap_ugni_max_num_ep)
			idx = 0;
	} while (zap_ugni_ep_id[idx]);
	zap_ugni_ep_id[idx] = ++current_gn;
	return idx;
}

/* Must be called with the endpoint lock held */
static struct zap_ugni_post_desc *__alloc_post_desc(struct z_ugni_ep *uep)
{
	struct zap_ugni_post_desc *d = calloc(1, sizeof(*d));
	if (!d)
		return NULL;
	d->uep = uep;
	ref_get(&uep->ep.ref, "alloc post desc");
#ifdef DEBUG
	d->ep_gn = zap_ugni_get_ep_gn(uep->ep_id);
#endif /* DEBUG */
	format_4tuple(&uep->ep, d->ep_name, ZAP_UGNI_EP_NAME_SZ);
	LIST_INSERT_HEAD(&uep->post_desc_list, d, ep_link);
	return d;
}

/* Must be called with the endpoint lock held */
static void __free_post_desc(struct zap_ugni_post_desc *d)
{
	struct z_ugni_ep *uep = d->uep;
	ZUGNI_LIST_REMOVE(d, ep_link);
	ref_put(&uep->ep.ref, "alloc post desc");
	free(d);
}

gni_mem_handle_t *__mh = NULL; /* the global memory handle ptr */
gni_mem_handle_t __mh_obj;

gni_mem_handle_t *ugni_get_mh()
{
	gni_return_t grc = GNI_RC_SUCCESS;
	zap_mem_info_t mmi;

	if (__mh)
		return __mh;

	pthread_mutex_lock(&ugni_mh_lock);
	/* multiple threads race to create the memory handle */
	if (__mh) {
		/* mh has already been created by the other thread */
		pthread_mutex_unlock(&ugni_mh_lock);
		return __mh;
	}
	mmi = __mem_info_fn();

	grc = GNI_MemRegister(_dom.nic, (uint64_t)mmi->start, mmi->len, NULL,
			      GNI_MEM_READWRITE | GNI_MEM_RELAXED_PI_ORDERING,
			      -1, &__mh_obj);
	if (grc != GNI_RC_SUCCESS) {
		LOG("GNI_MemRegister() error, rc: %d\n", grc);
	} else {
		__mh = &__mh_obj;
	}
	pthread_mutex_unlock(&ugni_mh_lock);
	return __mh;
}

gni_mem_handle_t *map_mh(zap_map_t map)
{
	if (map->mr[ZAP_UGNI])
		return map->mr[ZAP_UGNI];
	if (map->type == ZAP_MAP_LOCAL)
		return map->mr[ZAP_UGNI] = ugni_get_mh();
	return NULL;
}

/* The caller must hold the endpoint lock */
static void __shutdown_on_error(struct z_ugni_ep *uep)
{
	DLOG_(uep, "%s\n", __func__);
	if (uep->ep.state == ZAP_EP_CONNECTED)
		uep->ep.state = ZAP_EP_CLOSE;
	shutdown(uep->sock, SHUT_RDWR);
}

void z_ugni_cleanup(void)
{
	if (io_sched)
		ovis_scheduler_term(io_sched);
	if (io_thread) {
		pthread_cancel(io_thread);
		pthread_join(io_thread, NULL);
	}
	if (io_sched) {
		ovis_scheduler_free(io_sched);
		io_sched = NULL;
	}

	if (node_state_sched)
		ovis_scheduler_term(node_state_sched);

	if (node_state_thread) {
		pthread_cancel(node_state_thread);
		pthread_join(node_state_thread, NULL);
	}

	if (node_state_sched) {
		ovis_scheduler_free(node_state_sched);
		node_state_sched = NULL;
	}

	if (_node_state.node_state)
		free(_node_state.node_state);

	if (zap_ugni_ep_id)
		free(zap_ugni_ep_id);
}

static void __ep_release(struct z_ugni_ep *uep)
{
	gni_return_t grc;
	if (uep->gni_ep) {
		grc = GNI_EpUnbind(uep->gni_ep);
		if (grc)
			LOG_(uep, "GNI_EpUnbind() error: %s\n", gni_ret_str(grc));
		grc = GNI_EpDestroy(uep->gni_ep);
		if (grc != GNI_RC_SUCCESS)
			LOG_(uep, "GNI_EpDestroy() error: %s\n", gni_ret_str(grc));
		uep->gni_ep = NULL;
	}
}

static zap_err_t z_ugni_close(zap_ep_t ep)
{
	pthread_t self = pthread_self();
	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;

	DLOG_(uep, "Closing xprt: %p, state: %s\n", uep,
			__zap_ep_state_str(uep->ep.state));
	pthread_mutex_lock(&uep->ep.lock);
	if (self != ep->event_queue->thread) {
		while (!STAILQ_EMPTY(&uep->sq)) {
			pthread_cond_wait(&uep->sq_cond, &uep->ep.lock);
		}
	}
	switch (uep->ep.state) {
	case ZAP_EP_LISTENING:
	case ZAP_EP_CONNECTED:
	case ZAP_EP_PEER_CLOSE:
	case ZAP_EP_ERROR:
	case ZAP_EP_CONNECTING:
	case ZAP_EP_ACCEPTING:
		shutdown(uep->sock, SHUT_RDWR);
		break;
	case ZAP_EP_CLOSE:
		break;
	default:
		ZAP_ASSERT(0, ep, "%s: Unexpected state '%s'\n",
				__func__, __zap_ep_state_str(ep->state));
	}
	uep->ep.state = ZAP_EP_CLOSE;
	pthread_mutex_unlock(&uep->ep.lock);
	return ZAP_ERR_OK;
}

static zap_err_t z_get_name(zap_ep_t ep, struct sockaddr *local_sa,
			    struct sockaddr *remote_sa, socklen_t *sa_len)
{
	struct z_ugni_ep *uep = (void*)ep;
	int rc;
	*sa_len = sizeof(struct sockaddr_in);
	rc = getsockname(uep->sock, local_sa, sa_len);
	if (rc)
		goto err;
	rc = getpeername(uep->sock, remote_sa, sa_len);
	if (rc)
		goto err;
	return ZAP_ERR_OK;
err:
	return zap_errno2zerr(errno);
}

static zap_err_t z_ugni_connect(zap_ep_t ep,
				struct sockaddr *sa, socklen_t sa_len,
				char *data, size_t data_len)
{
	int rc;
	zap_err_t zerr;
	struct z_ugni_ep *uep = (void*)ep;
	zerr = zap_ep_change_state(&uep->ep, ZAP_EP_INIT, ZAP_EP_CONNECTING);
	if (zerr)
		goto out;

	if (_node_state.check_state) {
		if (uep->node_id == -1)
			uep->node_id = __get_nodeid(sa, sa_len);
		if (uep->node_id != -1) {
			if (__check_node_state(uep->node_id)) {
				DLOG("Node %d is in a bad state\n", uep->node_id);
				zerr = ZAP_ERR_CONNECT;
				goto out;
			}
		}
	}

	uep->sock = socket(sa->sa_family, SOCK_STREAM, 0);
	if (uep->sock == -1) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}

	if (__set_sockbuf_sz(uep->sock)) {
		zerr = ZAP_ERR_TRANSPORT;
		LOG_(uep, "Error %d: setting the sockbuf sz in %s.\n",
				errno, __func__);
		goto out;
	}

	if (__set_keep_alive(uep->sock)) {
		zerr = ZAP_ERR_TRANSPORT;
		LOG_(uep, "Error %d: enabling keep-alive in %s.\n",
				errno, __func__);
		goto out;
	}

	rc = __sock_nonblock(uep->sock);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}
	if (data_len) {
		uep->conn_data = malloc(data_len);
		if (uep->conn_data) {
			memcpy(uep->conn_data, data, data_len);
		} else {
			zerr = ZAP_ERR_RESOURCE;
			goto out;
		}
		uep->conn_data_len = data_len;
	}
	ref_get(&uep->ep.ref, "accept/connect");
	rc = connect(uep->sock, sa, sa_len);
	if (rc && errno != EINPROGRESS) {
		zerr = ZAP_ERR_CONNECT;
	} else {
		zerr = ZAP_ERR_OK;
	}

	zerr = __setup_connection(uep);
	if (!zerr)
		return ZAP_ERR_OK;
 out:
	if (uep->sock >= 0) {
		close(uep->sock);
		uep->sock = -1;
	}
	return zerr;
}

static void __ugni_wr_complete(struct z_ugni_ep *uep, struct zap_ugni_send_wr *wr,
				enum zap_err_e zerr)
{
	struct zap_event ev = {0};
	if (wr->flags & UGNI_WR_F_COMPLETE) {
		if (wr->flags & UGNI_WR_F_SEND) {
			ev.type = ZAP_EVENT_SEND_COMPLETE;
		} else if (wr->flags & UGNI_WR_F_SEND_MAPPED) {
			ev.type = ZAP_EVENT_SEND_MAPPED_COMPLETE;
			ev.context = wr->ctxt;
		} else {
			/* do nothing */
		}
		ev.status = zerr;
		pthread_mutex_unlock(&uep->ep.lock);
		uep->ep.cb(&uep->ep, &ev);
		pthread_mutex_lock(&uep->ep.lock);
	}
}

static void ugni_sock_write(ovis_event_t ev)
{
	struct z_ugni_ep *uep = ev->param.ctxt;

	ssize_t wsz;
	struct zap_ugni_send_wr *wr;

	pthread_mutex_lock(&uep->ep.lock);
 next:
	wr = STAILQ_FIRST(&uep->sq);
	if (!wr) {
		/* sq empty, disable epoll out */
		__disable_epoll_out(uep);
		pthread_cond_signal(&uep->sq_cond);
		goto out;
	}

	/* msg part */
	while (wr->moff < wr->msz) {
		wsz = write(uep->sock, wr->msg.bytes + wr->moff, wr->msz - wr->moff);
		if (wsz < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				goto out;
			/* bad error */
			goto err;
		}
		wr->moff += wsz;
	}
	/* data part */
	while (wr->doff < wr->dsz) {
		wsz = write(uep->sock, wr->data + wr->doff, wr->dsz - wr->doff);
		if (wsz < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				goto out;
			/* bad error */
			goto err;
		}
		wr->doff += wsz;
	}

	/* wr completed */
	STAILQ_REMOVE_HEAD(&uep->sq, link);
	__ugni_wr_complete(uep, wr, ZAP_ERR_OK);
	free(wr);
	goto next;

 out:
	pthread_mutex_unlock(&uep->ep.lock);
	return;

 err:
	__shutdown_on_error(uep);
	pthread_mutex_unlock(&uep->ep.lock);
}

/**
 * Process an unknown message in the end point.
 */
static void process_uep_msg_unknown(struct z_ugni_ep *uep)
{
	LOG_(uep, "zap_ugni: Unknown zap message.\n");
	pthread_mutex_lock(&uep->ep.lock);
	__shutdown_on_error(uep);
	pthread_mutex_unlock(&uep->ep.lock);
}

/**
 * Receiving a regular message.
 */
static void process_uep_msg_regular(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_regular *msg;
	struct zap_event ev = {
			.type = ZAP_EVENT_RECV_COMPLETE,
	};

	msg = (void*)uep->rbuff->data;
	ev.data = (void*)msg->data;
	ev.data_len = ntohl(msg->data_len);
	uep->ep.cb(&uep->ep, &ev);
	return;
}

/**
 * Receiving a rendezvous (share) message.
 */
static void process_uep_msg_rendezvous(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_rendezvous *msg;
	struct zap_map *map;
	zap_err_t zerr;

	msg = (void*)uep->rbuff->data;

	msg->hdr.msg_len = ntohl(msg->hdr.msg_len);
	msg->hdr.reserved = 0;
	msg->hdr.msg_type = ntohs(msg->hdr.msg_type);
	msg->addr = be64toh(msg->addr);
	msg->acc = ntohl(msg->acc);
	msg->data_len = ntohl(msg->data_len);
	msg->gni_mh.qword1 = be64toh(msg->gni_mh.qword1);
	msg->gni_mh.qword2 = be64toh(msg->gni_mh.qword2);

	zerr = zap_map(&map, (void*)msg->addr, msg->data_len, msg->acc);
	if (zerr) {
		LOG_(uep, "%s:%d: Failed to create a map in %s (%s)\n",
			__FILE__, __LINE__, __func__, __zap_err_str[zerr]);
		goto err0;
	}
	map->type = ZAP_MAP_REMOTE;
	ref_get(&uep->ep.ref, "zap_map/rendezvous");
	map->ep = (void*)uep;
	map->mr[ZAP_UGNI] = &msg->gni_mh;

	char *amsg = NULL;
	size_t amsg_len = msg->hdr.msg_len - sizeof(msg);
	if (amsg_len) {
		amsg = msg->msg; /* attached message from rendezvous */
	}
	struct zap_event ev = {
		.type = ZAP_EVENT_RENDEZVOUS,
		.map = map,
		.data_len = amsg_len,
		.data = (void*)amsg
	};

	uep->ep.cb((void*)uep, &ev);
	return;

err0:
	return;
}

static zap_err_t
__ugni_send(struct z_ugni_ep *uep, enum zap_ugni_msg_type type,
						char *buf, size_t len);
static void process_uep_msg_accepted(struct z_ugni_ep *uep)
{
	ZAP_ASSERT(uep->ep.state == ZAP_EP_CONNECTING, &uep->ep,
			"%s: Unexpected state '%s'. "
			"Expected state 'ZAP_EP_CONNECTING'\n",
			__func__, __zap_ep_state_str(uep->ep.state));
	struct zap_ugni_msg_accepted *msg;
	zap_err_t zerr;

	msg = (void*)uep->rbuff->data;

	msg->hdr.msg_len = ntohl(msg->hdr.msg_len);
	msg->hdr.reserved = 0;
	msg->hdr.msg_type = ntohs(msg->hdr.msg_type);
	msg->data_len = ntohl(msg->data_len);
	msg->inst_id = ntohl(msg->inst_id);
	msg->pe_addr = ntohl(msg->pe_addr);

	DLOG_(uep, "ACCEPTED received: pe_addr: %#x, inst_id: %#x\n",
			msg->pe_addr, msg->inst_id);

#ifdef ZAP_UGNI_DEBUG
	char *is_exit = getenv("ZAP_UGNI_CONN_EST_BEFORE_ACK_N_BINDING_TEST");
	if (is_exit)
		exit(0);
#endif /* ZAP_UGNI_DEBUG */

	pthread_mutex_lock(&uep->ep.lock);
	zerr = __ugni_send(uep, ZAP_UGNI_MSG_ACK_ACCEPTED, NULL, 0);
	pthread_mutex_unlock(&uep->ep.lock);
	if (zerr)
		goto err;

	gni_return_t grc;
	grc = GNI_EpBind(uep->gni_ep, msg->pe_addr, msg->inst_id);
	if (grc) {
		LOG_(uep, "GNI_EpBind() error: %s\n", gni_ret_str(grc));
		goto err;
	}

#ifdef ZAP_UGNI_DEBUG
	is_exit = getenv("ZAP_UGNI_CONN_EST_BEFORE_ACK_AFTER_BINDING_TEST");
	if (is_exit)
		exit(0);
#endif /* ZAP_UGNI_DEBUG */

	uep->ugni_ep_bound = 1;
	struct zap_event ev = {
		.type = ZAP_EVENT_CONNECTED,
		.data_len = msg->data_len,
		.data = (msg->data_len ? (void*)msg->data : NULL)
	};
	if (!zap_ep_change_state(&uep->ep, ZAP_EP_CONNECTING, ZAP_EP_CONNECTED)) {
		uep->ep.cb((void*)uep, &ev);
	} else {
		LOG_(uep, "'Accept' message received in unexpected state %d.\n",
		     uep->ep.state);
		goto err;
	}
	return;

err:
	shutdown(uep->sock, SHUT_RDWR);
	return;
}

static void process_uep_msg_connect(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_connect *msg;

	msg = (void*)uep->rbuff->data;

	pthread_mutex_lock(&uep->ep.lock);

	if (!ZAP_VERSION_EQUAL(msg->ver)) {
		LOG_(uep, "zap_ugni: Receive conn request "
				"from an unsupported version "
				"%hhu.%hhu.%hhu.%hhu\n",
				msg->ver.major, msg->ver.minor,
				msg->ver.patch, msg->ver.flags);
		goto err1;
	}

	if (memcmp(msg->sig, ZAP_UGNI_SIG, sizeof(msg->sig))) {
		LOG_(uep, "Expecting sig '%s', but got '%.*s'.\n",
				ZAP_UGNI_SIG, sizeof(msg->sig), msg->sig);
		goto err1;

	}

	msg->hdr.msg_len = ntohl(msg->hdr.msg_len);
	msg->hdr.reserved = 0;
	msg->hdr.msg_type = ntohs(msg->hdr.msg_type);
	msg->data_len = ntohl(msg->data_len);
	msg->inst_id = ntohl(msg->inst_id);
	msg->pe_addr = ntohl(msg->pe_addr);

	DLOG_(uep, "CONN_REQ received: pe_addr: %#x, inst_id: %#x\n",
			msg->pe_addr, msg->inst_id);
	gni_return_t grc;
	grc = GNI_EpBind(uep->gni_ep, msg->pe_addr, msg->inst_id);
	if (grc) {
		LOG_(uep, "GNI_EpBind() error: %s\n", gni_ret_str(grc));
		goto err1;
	}
	uep->ugni_ep_bound = 1;
	pthread_mutex_unlock(&uep->ep.lock);

	struct zap_event ev = {
		.type = ZAP_EVENT_CONNECT_REQUEST,
		.data_len = msg->data_len,
		.data = (msg->data_len)?((void*)msg->data):(NULL)
	};
	uep->ep.cb(&uep->ep, &ev);

	return;
err1:
	pthread_mutex_unlock(&uep->ep.lock);
	shutdown(uep->sock, SHUT_RDWR);
	return;
}

static void process_uep_msg_rejected(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_regular *msg;
	int rc;
	size_t data_len;

	msg = (void*)uep->rbuff->data;
	data_len = ntohl(msg->data_len);
	struct zap_event ev = {
		.type = ZAP_EVENT_REJECTED,
		.data_len = data_len,
		.data = (data_len ? (void *)msg->data : NULL)
	};
	rc = zap_ep_change_state(&uep->ep, ZAP_EP_CONNECTING, ZAP_EP_ERROR);
	if (rc != ZAP_ERR_OK) {
		return;
	}
	uep->ep.cb(&uep->ep, &ev);
	shutdown(uep->sock, SHUT_RDWR);
	return;
}

static void process_uep_msg_ack_accepted(struct z_ugni_ep *uep)
{
	int rc;

	rc = zap_ep_change_state(&uep->ep, ZAP_EP_ACCEPTING, ZAP_EP_CONNECTED);
	if (rc != ZAP_ERR_OK) {
		shutdown(uep->sock, SHUT_RDWR);
		return;
	}
	struct zap_event ev = {
		.type = ZAP_EVENT_CONNECTED
	};
	ref_get(&uep->ep.ref, "accept/connect");
	uep->ep.cb(&uep->ep, &ev);
	return;
}

typedef void(*process_uep_msg_fn_t)(struct z_ugni_ep*);
process_uep_msg_fn_t process_uep_msg_fns[] = {
	[ZAP_UGNI_MSG_REGULAR]      =  process_uep_msg_regular,
	[ZAP_UGNI_MSG_RENDEZVOUS]   =  process_uep_msg_rendezvous,
	[ZAP_UGNI_MSG_ACCEPTED]     =  process_uep_msg_accepted,
	[ZAP_UGNI_MSG_CONNECT]      =  process_uep_msg_connect,
	[ZAP_UGNI_MSG_REJECTED]     =  process_uep_msg_rejected,
	[ZAP_UGNI_MSG_ACK_ACCEPTED] =  process_uep_msg_ack_accepted,
};

static int __recv_msg(struct z_ugni_ep *uep)
{
	int rc;
	ssize_t rsz, rqsz;
	struct zap_ugni_msg_hdr *hdr;
	struct zap_ugni_recv_buff *buff = uep->rbuff;
	uint32_t mlen;
	int from_line = 0; /* for debugging */

	if (buff->len < sizeof(struct zap_ugni_msg_hdr)) {
		/* need to fill the header first */
		rqsz = sizeof(struct zap_ugni_msg_hdr) - buff->len;
		rsz = read(uep->sock, buff->data + buff->len, rqsz);
		if (rsz == 0) {
			/* peer close */
			rc = ENOTCONN;
			from_line = __LINE__;
			goto err;
		}
		if (rsz < 0) {
			if (errno == EAGAIN)
				return errno;
			/* error */
			rc = errno;
			from_line = __LINE__;
			goto err;
		}
		buff->len += rsz;
		buff->alen -= rsz;
		if (rsz < rqsz) {
			rc = EAGAIN;
			from_line = __LINE__;
			goto err;
		}
	}

	hdr = (void*)buff->data;
	mlen = ntohl(hdr->msg_len);

	if (mlen > UGNI_SOCKBUF_SZ) {
		rc = EINVAL;
		from_line = __LINE__;
		goto err;
	}

	if (mlen > buff->len + buff->alen) {
		/* Buffer extension is needed */
		rqsz = ((sizeof(*buff) + mlen - 1) | 0xFFFF) + 1;
		buff = realloc(buff, rqsz);
		if (!buff) {
			from_line = __LINE__;
			rc = ENOMEM;
			goto err;
		}
		buff->alen = rqsz - buff->len - sizeof(*buff);
		/* don't forget to update uep->rbuff pointer */
		uep->rbuff = buff;
	}

	if (buff->len < mlen) {
		rqsz = mlen - buff->len;
		rsz = read(uep->sock, buff->data + buff->len, rqsz);
		if (rsz == 0) {
			/* peer close */
			rc = ENOTCONN;
			from_line = __LINE__;
			goto err;
		}
		if (rsz < 0) {
			if (errno == EAGAIN)
				return errno;
			rc = errno;
			from_line = __LINE__;
			goto err;
		}
		buff->len += rsz;
		buff->alen -= rsz;
		if (rsz < rqsz) {
			rc = EAGAIN;
			from_line = __LINE__;
			goto err;
		}
	}
	return 0;

 err:
	from_line += 0; /* avoid gcc's set-but-not-used warning*/
	return rc;
}

#define min_t(t, x, y) (t)((t)x < (t)y?(t)x:(t)y)
static void ugni_sock_read(ovis_event_t ev)
{
	struct z_ugni_ep *uep = ev->param.ctxt;
	struct zap_ugni_msg_hdr *hdr;
	zap_ugni_msg_type_t msg_type;

	int rc;

	do {
		rc = __recv_msg(uep);
		if (rc == EAGAIN)
			break;
		if (rc) {
			/* ENOTCONN or other errors */
			goto bad;
		}

		/* message receive complete */
		hdr = (void*)uep->rbuff->data;
		msg_type = ntohs(hdr->msg_type);

		/* validate by ep state */
		pthread_mutex_lock(&uep->ep.lock);
		switch (uep->ep.state) {
		case ZAP_EP_ACCEPTING:
			/* expecting only `connect` message */
			if ((msg_type != ZAP_UGNI_MSG_CONNECT) &&
			      (msg_type != ZAP_UGNI_MSG_ACK_ACCEPTED)) {
				/* invalid */
				pthread_mutex_unlock(&uep->ep.lock);
				goto bad;
			}
			break;
		case ZAP_EP_CONNECTING:
			/* Expecting accept or reject */
			if (msg_type != ZAP_UGNI_MSG_ACCEPTED &&
					msg_type != ZAP_UGNI_MSG_REJECTED) {
				/* invalid */
				pthread_mutex_unlock(&uep->ep.lock);
				goto bad;
			}
			break;
		case ZAP_EP_CONNECTED:
			/* good */
			break;
		case ZAP_EP_CLOSE:
		case ZAP_EP_ERROR:
			/*
			 * This could happened b/c the read event has not been
			 * disabled yet. Process the received message as usual.
			 */
			break;
		case ZAP_EP_INIT:
			assert(0 == "bad state (ZAP_EP_INIT)");
			break;
		case ZAP_EP_LISTENING:
			assert(0 == "bad state (ZAP_EP_LISTENING)");
			break;
		case ZAP_EP_PEER_CLOSE:
			assert(0 == "bad state (ZAP_EP_CLOSE)");
			break;
		}
		pthread_mutex_unlock(&uep->ep.lock);
		/* Then call the process function accordingly */
		if (ZAP_UGNI_MSG_NONE < msg_type
				&& msg_type < ZAP_UGNI_MSG_TYPE_LAST) {
			process_uep_msg_fns[msg_type](uep);
		} else {
			assert(0);
			process_uep_msg_unknown(uep);
		}
		/* reset recv buffer */
		uep->rbuff->alen += uep->rbuff->len;
		uep->rbuff->len = 0;
	} while (1);
	return;
 bad:
	pthread_mutex_lock(&uep->ep.lock);
	__shutdown_on_error(uep);
	pthread_mutex_unlock(&uep->ep.lock);
}

static void *io_thread_proc(void *arg)
{
	/* Zap thread will not handle any signal */
	int rc;
	sigset_t sigset;
	sigfillset(&sigset);
	rc = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	assert(rc == 0 && "pthread_sigmask error");
	ovis_scheduler_loop(io_sched, 0);
	return NULL;
}

int zap_ugni_err_handler(gni_cq_handle_t cq, gni_cq_entry_t cqe,
			 struct zap_ugni_post_desc *desc)
{
	uint32_t recoverable = 0;
	char errbuf[512];
	gni_return_t grc = GNI_CqErrorRecoverable(cqe, &recoverable);
	if (grc) {
		zap_ugni_log("GNI_CqErrorRecoverable returned %d\n", grc);
		recoverable = 0;
	}
	grc = GNI_CqErrorStr(cqe, errbuf, sizeof(errbuf));
	if (grc)
		zap_ugni_log("GNI_CqErrorStr returned %d\n", grc);
	else
		zap_ugni_log("GNI cqe Error : %s\n", errbuf);
	return recoverable;
}

static gni_return_t process_cq(gni_cq_handle_t cq, gni_cq_entry_t cqe_)
{
	struct zap_event zev;
	gni_cq_entry_t cqe = cqe_;
	gni_return_t grc;
	gni_post_descriptor_t *post;
	int count = 0;
	do {
		memset(&zev, 0, sizeof(zev));
		count++;
		if (GNI_CQ_GET_TYPE(cqe) != GNI_CQ_EVENT_TYPE_POST) {
			zap_ugni_log("Unexepcted cqe type %d cqe"
					" %08x on CQ %p\n",
					GNI_CQ_GET_TYPE(cqe), cqe, cq);
			goto skip;
		}
		pthread_mutex_lock(&ugni_lock);
		post = NULL;

#ifdef DEBUG
		assert(ugni_io_count >= 0);
		__sync_sub_and_fetch(&ugni_io_count, 1);
#endif /* DEBUG */
		grc = GNI_GetCompleted(cq, cqe, &post);
		pthread_mutex_unlock(&ugni_lock);
		if (!post) {
			DLOG("process_cq: post is NULL\n");
			goto skip;
		}
		assert((int)ugni_post_count >= 0);
		struct zap_ugni_post_desc *desc = (void*) post;
		if (grc) {
			zap_ugni_log("GNI_GetCompleted returned %s\n", gni_ret_str(grc));
			if (0 == zap_ugni_err_handler(cq, cqe, desc))
				__shutdown_on_error(desc->uep);
			else
				assert(0 == "uh oh...how do we restart this");
		}
		pthread_mutex_lock(&z_ugni_list_mutex);
		if (desc->is_stalled == 1) {
			/*
			 * The descriptor is in the stalled state.
			 *
			 * The completion corresponding to the descriptor
			 * has been flushed. The corresponding endpoint
			 * might have been freed already.
			 */
			LOG("%s: Received a CQ event for a stalled post "
						"desc.\n", desc->ep_name);
			ZUGNI_LIST_REMOVE(desc, stalled_link);
			free(desc);
			pthread_mutex_unlock(&z_ugni_list_mutex);
			goto skip;
		}

		struct z_ugni_ep *uep = desc->uep;
		if (!uep) {
			/*
			 * This should not happen. The code is put in to prevent
			 * the segmentation fault and to record the situation.
			 */
			LOG("%s: %s: desc->uep = NULL. Drop the descriptor.\n", __func__,
				desc->ep_name);
			pthread_mutex_unlock(&z_ugni_list_mutex);
			goto skip;
		}
		pthread_mutex_lock(&uep->ep.lock);
#ifdef DEBUG
		if (uep->deferred_link.le_prev)
			LOG_(uep, "uep %p: Doh!! I'm on the deferred list.\n", uep);
#endif /* DEBUG */
		switch (desc->post.type) {
		case GNI_POST_RDMA_GET:
			DLOG_(uep, "RDMA_GET: Read complete %p with %s\n", desc, gni_ret_str(grc));
			if (grc) {
				zev.status = ZAP_ERR_RESOURCE;
				LOG_(uep, "RDMA_GET: completing "
					"with error %s.\n",
					gni_ret_str(grc));
			}
			zev.type = ZAP_EVENT_READ_COMPLETE;
			zev.context = desc->context;
			break;
		case GNI_POST_RDMA_PUT:
			DLOG_(uep, "RDMA_PUT: Write complete %p with %s\n",
						desc, gni_ret_str(grc));
			if (grc) {
				zev.status = ZAP_ERR_RESOURCE;
				DLOG_(uep, "RDMA_PUT: completing "
					"with error %s.\n",
					gni_ret_str(grc));
			}
			zev.type = ZAP_EVENT_WRITE_COMPLETE;
			zev.context = desc->context;
			break;
		default:
			LOG_(uep, "Unknown completion type %d.\n",
					 desc->post.type);
			__shutdown_on_error(uep);
		}

		__free_post_desc(desc);
		pthread_mutex_unlock(&uep->ep.lock);
		pthread_mutex_unlock(&z_ugni_list_mutex);

		/* Wake up threads blocked trying to post descriptors */
		pthread_mutex_lock(&cq_full_lock);
		if (uep->gni_cq == _dom.cq) {
			__sync_sub_and_fetch(&ugni_post_count, 1);
			assert((int)ugni_post_count >= 0);
		}
		pthread_cond_broadcast(&cq_full_cond);
		pthread_mutex_unlock(&cq_full_lock);

		uep->ep.cb(&uep->ep, &zev);
	skip:
		pthread_mutex_lock(&ugni_lock);
		grc = GNI_CqGetEvent(cq, &cqe);
		pthread_mutex_unlock(&ugni_lock);
		if (grc == GNI_RC_ERROR_RESOURCE) {
			zap_ugni_log("CQ overrun!\n");
			break;
		}
	} while (grc != GNI_RC_NOT_DONE);
	if (count > 1)
		DLOG("process_cq: count %d\n", count);
	return GNI_RC_SUCCESS;
}

/* Caller must hold the endpoint list lock */
void __stall_post_desc(struct zap_ugni_post_desc *d, struct timeval time)
{
	ref_put(&d->uep->ep.ref, "alloc post desc");
	d->is_stalled = 1;
	d->uep = NULL;
	d->stalled_time = time;
	__sync_fetch_and_add(&ugni_leaked_count, 1);
	LIST_INSERT_HEAD(&stalled_desc_list, d, stalled_link);
}

/* Caller must hold the endpoint lock. */
void __flush_post_desc_list(struct z_ugni_ep *uep)
{
	struct timeval time;
	gettimeofday(&time, NULL);
	struct zap_ugni_post_desc *d;
	d = LIST_FIRST(&uep->post_desc_list);
	while (d) {
		ZUGNI_LIST_REMOVE(d, ep_link);
		struct zap_event zev = {0};
		switch (d->post.type) {
		case GNI_POST_RDMA_GET:
			zev.type = ZAP_EVENT_READ_COMPLETE;
			break;
		case GNI_POST_RDMA_PUT:
			zev.type = ZAP_EVENT_WRITE_COMPLETE;
			break;
		default:
			zap_ugni_log("Unknown RDMA post "
				     "type %d on transport %p.\n",
				     d->post.type, uep);
		}
		zev.status = ZAP_ERR_FLUSH;
		zev.context = d->context;
		pthread_mutex_unlock(&uep->ep.lock);
		uep->ep.cb(&uep->ep, &zev);
		if (uep->gni_cq == _dom.cq) {
			__sync_sub_and_fetch(&ugni_post_count, 1);
			assert((int)ugni_post_count >= 0);
		}
		pthread_mutex_lock(&uep->ep.lock);
		__stall_post_desc(d, time);
		d = LIST_FIRST(&uep->post_desc_list);
	}
}

static zap_thrstat_t ugni_stats;
#define WAIT_5SECS 5000
static void *cq_thread_proc(void *arg)
{
	gni_return_t grc;
	gni_cq_entry_t cqe;
	int oldtype;
	int drain = 0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
	zap_thrstat_reset(ugni_stats);
	while (1) {
		uint64_t timeout = WAIT_5SECS;
		zap_thrstat_wait_start(ugni_stats);
		grc = GNI_CqWaitEvent(_dom.cq, timeout, &cqe);
		zap_thrstat_wait_end(ugni_stats);
		switch (grc) {
		case GNI_RC_SUCCESS:
			grc = process_cq(_dom.cq, cqe);
			if (grc)
				zap_ugni_log("Error %d processing CQ %p.\n",
					     grc, _dom.cq);
		case GNI_RC_TIMEOUT:
			if (drain) {
				gni_cq_handle_t new_cq;
				grc = GNI_CqCreate(_dom.nic, _dom.cq_depth, 0, GNI_CQ_BLOCKING,
						   NULL, NULL, &new_cq);
				if (grc == GNI_RC_SUCCESS) {
					ugni_old_cq = _dom.cq;
					_dom.cq = new_cq;
					ugni_post_count = 0;
					ugni_leaked_count = 0;
					drain = 0;
					zap_ugni_log("%s: Created a new CQ...resuming I/O\n", __func__);
				} else {
					zap_ugni_log("%s: Creating a new CQ failed with error \"%s\". "
						     "The daemon needs to be restarted\n",
						     __func__, gni_ret_str(grc));
					pthread_mutex_unlock(&cq_full_lock);
				}
				pthread_mutex_unlock(&cq_full_lock);
			}
			break;
		default:
			zap_ugni_log("%s:%d GNI_CqWaitEvent returned %s, errno %d\n",
				     __func__, __LINE__, gni_ret_str(grc), errno);
			break;
		}
		/*
		 * Check if the leaked descriptor count is > 1/2 the
		 * cq depth. At that point:
		 * - Stall all I/O (by holding the cq_full_lock)
		 * - Wait up to 5 seconds to drain the CQ
		 * - Create a new CQ and continue
		 */
		if ((drain == 0)
		    && (ugni_leaked_count > (_dom.cq_depth / 2))) {
			pthread_mutex_lock(&cq_full_lock);
			drain = 1;
			zap_ugni_log("%s: CQ has leaked %d descriptors...stalling I/O\n", __func__,
				     ugni_leaked_count);
		}
	}
	return NULL;
}

static void *error_thread_proc(void *args)
{
	gni_err_handle_t err_hndl;
	gni_error_event_t ev;
	gni_return_t status;
	uint32_t num;

	gni_error_mask_t err =
		GNI_ERRMASK_CORRECTABLE_MEMORY |
		GNI_ERRMASK_CRITICAL |
		GNI_ERRMASK_TRANSACTION |
		GNI_ERRMASK_ADDRESS_TRANSLATION |
		GNI_ERRMASK_TRANSIENT |
		GNI_ERRMASK_INFORMATIONAL;

	status = GNI_SubscribeErrors(_dom.nic, 0, err, 64, &err_hndl);
	/* Test for subscription to error events */
	if (status != GNI_RC_SUCCESS) {
		zap_ugni_log("FAIL:GNI_SubscribeErrors returned error %s\n", gni_err_str[status]);
	}
	while (1) {
		memset(&ev, 0, sizeof(ev));
		status = GNI_WaitErrorEvents(err_hndl,&ev,1,0,&num);
		if (status != GNI_RC_SUCCESS || (num != 1)) {
			zap_ugni_log("FAIL:GNI_WaitErrorEvents returned error %d %s\n",
				num, gni_err_str[status]);
			continue;
		}
		zap_ugni_log("%u %u %u %u %lu %lu %lu %lu %lu\n",
			     ev.error_code, ev.error_category, ev.ptag,
			     ev.serial_number, ev.timestamp,
			     ev.info_mmrs[0], ev.info_mmrs[1], ev.info_mmrs[2],
			     ev.info_mmrs[3]);
	}
	return NULL;
}

static struct zap_ugni_send_wr *__wr_alloc(enum zap_ugni_msg_type mtype,
					   size_t data_len,
					   int buf_alloc)
{
	/* buf_alloc:  0 - do not allocate data buffer,
	 * buf_alloc: !0 - allocate data buffer */
	size_t msz;
	struct zap_ugni_send_wr *wr;

	switch (mtype) {
	case ZAP_UGNI_MSG_CONNECT:
		msz = sizeof(struct zap_ugni_msg_connect);
		break;
	case ZAP_UGNI_MSG_REGULAR:
	case ZAP_UGNI_MSG_REJECTED:     /* use regular msg */
	case ZAP_UGNI_MSG_ACK_ACCEPTED: /* use regular msg */
		msz = sizeof(struct zap_ugni_msg_regular);
		break;
	case ZAP_UGNI_MSG_RENDEZVOUS:
		msz = sizeof(struct zap_ugni_msg_rendezvous);
		break;
	case ZAP_UGNI_MSG_ACCEPTED:
		msz = sizeof(struct zap_ugni_msg_accepted);
		break;
	default:
		errno = EINVAL;
		return NULL;
	}

	wr = malloc(sizeof(*wr) + (!!buf_alloc)*data_len);
	if (!wr)
		return NULL;

	wr->flags = 0;
	wr->moff = 0;
	wr->msz = msz;
	wr->doff = 0;
	wr->dsz = data_len;
	wr->data = (char*)&wr[1];
	wr->msg.hdr.msg_type = htons(mtype);
	wr->msg.hdr.msg_len  = htonl((uint32_t)(msz + data_len));
	return wr;
}

/* caller must held uep->ep.lock */
static zap_err_t __wr_post(struct z_ugni_ep *uep, struct zap_ugni_send_wr *wr)
{
	int rc;
	switch (uep->ep.state) {
	case ZAP_EP_LISTENING:
	case ZAP_EP_ACCEPTING:
	case ZAP_EP_CONNECTING:
	case ZAP_EP_CONNECTED:
		/* OK */
		break;
	case ZAP_EP_PEER_CLOSE:
	case ZAP_EP_CLOSE:
	case ZAP_EP_ERROR:
	default:
		return ZAP_ERR_NOT_CONNECTED;
	}

	STAILQ_INSERT_TAIL(&uep->sq, wr, link);
	rc = __enable_epoll_out(uep);
	if (rc)
		return ZAP_ERR_RESOURCE;
	return ZAP_ERR_OK;
}

/* caller must held uep->ep.lock */
static zap_err_t __ugni_send_connect(struct z_ugni_ep *uep, char *buf, size_t len)
{
	zap_err_t zerr;
	struct zap_ugni_msg_connect *msg;
	struct zap_ugni_send_wr *wr = __wr_alloc(ZAP_UGNI_MSG_CONNECT, len, 1);
	if (!wr)
		return ZAP_ERR_RESOURCE;
	msg = &wr->msg.connect;
	msg->data_len     = htonl(len);
	msg->inst_id      = htonl(_dom.inst_id);
	msg->pe_addr      = htonl(_dom.pe_addr);
	ZAP_VERSION_SET(msg->ver);
	memcpy(&msg->sig, ZAP_UGNI_SIG, sizeof(msg->sig));
	if (buf && len)
		memcpy(wr->data, buf, len);
	zerr = __wr_post(uep, wr);
	if (zerr)
		free(wr);
	return zerr;
}

/* caller must held uep->ep.lock */
static zap_err_t
__ugni_send(struct z_ugni_ep *uep, enum zap_ugni_msg_type type,
						char *buf, size_t len)
{
	zap_err_t zerr;
	struct zap_ugni_msg_regular *msg;
	struct zap_ugni_send_wr *wr = __wr_alloc(type, len, 1);
	if (!wr)
		return ZAP_ERR_RESOURCE;
	msg = &wr->msg.sendrecv;
	msg->data_len = htonl(len);
	if (buf && len)
		memcpy(wr->data, buf, len);
	zerr = __wr_post(uep, wr);
	if (zerr) {
		free(wr);
		return zerr;
	}
	return ZAP_ERR_OK;
}

static void __deliver_disconnect_ev(struct z_ugni_ep *uep)
{
	/* Deliver the disconnected event */
	pthread_mutex_lock(&z_ugni_list_mutex);
#ifdef DEBUG
	zap_ugni_ep_id[uep->ep_id] = -1;
#endif /* DEBUG */
	pthread_mutex_lock(&uep->ep.lock);
#ifdef DEBUG
	/* It is in the queue already. */
	pthread_mutex_lock(&deferred_list_mutex);
	if (uep->deferred_link.le_prev) {
		/* It is in the deferred list ... remove it. */
		ZUGNI_LIST_REMOVE(uep, deferred_link);
		uep->deferred_link.le_next = 0;
		uep->deferred_link.le_prev = 0;
		zap_put_ep(&uep->ep);
	}
	pthread_mutex_unlock(&deferred_list_mutex);
#endif /* DEBUG */
	if (!LIST_EMPTY(&uep->post_desc_list)) {
		__flush_post_desc_list(uep);
		DLOG("%s: after cleanup all rdma"
			"post: ep %p: ref_count %d\n",
			__func__, uep, uep->ep.ref_count);
	}
	pthread_mutex_unlock(&uep->ep.lock);
	pthread_mutex_unlock(&z_ugni_list_mutex);
	ZAP_ASSERT(uep->conn_ev.type == ZAP_EVENT_DISCONNECTED ||
			uep->conn_ev.type == ZAP_EVENT_CONNECT_ERROR, &uep->ep,
			"%s: uep->conn_ev.type (%s) is neither ZAP_EVENT_"
			"DISCONNECTED nor ZAP_EVENT_CONNECT_ERROR\n", __func__,
			zap_event_str(uep->conn_ev.type));
	/*
	 * If we reach here with conn_ev, we have a deferred disconnect event
	 * the disconnect path in ugni_sock_event() has already prep conn_ev for us.
	 */
	/* Sending DISCONNECTED event to application */
	close(uep->sock);
	uep->sock = -1;
	uep->ep.cb((void*)uep, &uep->conn_ev);
	ref_put(&uep->ep.ref, "accept/connect");
}

void __deferred_disconnect_cb(ovis_event_t ev)
{
	struct z_ugni_ep *uep = ev->param.ctxt;
	pthread_mutex_lock(&uep->ep.lock);
	__flush_post_desc_list(uep);
	pthread_mutex_unlock(&uep->ep.lock);
	ovis_scheduler_event_del(io_sched, ev);
	__deliver_disconnect_ev(uep);
}

void __ugni_defer_disconnect_event(struct z_ugni_ep *uep)
{
	int rc;
	DLOG("defer_disconnected: uep %p\n", uep);
#ifdef DEBUG
	pthread_mutex_lock(&uep->ep.lock);
	pthread_mutex_lock(&deferred_list_mutex);
	if (uep->deferred_link.le_prev == 0) {
		/* It is not in the deferred list yet ... add it in. */
		zap_get_ep(&uep->ep);
		LIST_INSERT_HEAD(&deferred_list, uep, deferred_link);
	}
	pthread_mutex_unlock(&deferred_list_mutex);
	pthread_mutex_unlock(&uep->ep.lock);
#endif /* DEBUG */
	OVIS_EVENT_INIT(&uep->deferred_ev);
	uep->deferred_ev.param.type = OVIS_EVENT_TIMEOUT;
	uep->deferred_ev.param.timeout.tv_sec = zap_ugni_disconnect_timeout;
	uep->deferred_ev.param.cb_fn = __deferred_disconnect_cb;
	uep->deferred_ev.param.ctxt = uep;
	rc = ovis_scheduler_event_add(io_sched, &uep->deferred_ev);
	assert(rc == 0);
}

static void ugni_sock_ev_cb(ovis_event_t ev)
{
	struct z_ugni_ep *uep = ev->param.ctxt;
#if defined(ZAP_DEBUG) || defined(DEBUG)
	uep->epoll_record[uep->epoll_record_curr++] = ev->cb.epoll_events;
	uep->epoll_record_curr %= UEP_EPOLL_RECORD_SZ;
#endif /* ZAP_DEBUG || DEBUG */

	if (ev->cb.epoll_events & EPOLLOUT) {
		if (uep->sock_connected) {
			ugni_sock_write(ev);
		} else {
			/* just become sock-connected */
			uep->sock_connected = 1;
			ugni_sock_connect(ev);
		}
	}

	if (ev->cb.epoll_events & EPOLLIN) {
		ugni_sock_read(ev);
	}

	if ((ev->cb.epoll_events & (EPOLLERR|EPOLLHUP))) {
		ugni_sock_event(ev);
	}
}

static void ugni_sock_connect(ovis_event_t ev)
{
	zap_err_t zerr;
	struct z_ugni_ep *uep = ev->param.ctxt;
#ifdef ZAP_UGNI_DEBUG
	char *is_exit = getenv("ZAP_UGNI_CONN_EST_BEFORE_CONNECT_MSG_TEST");
	if (is_exit)
		exit(0);
#endif /* ZAP_UGNI_DEBUG */
	pthread_mutex_lock(&uep->ep.lock);
	zerr = __ugni_send_connect(uep, uep->conn_data, uep->conn_data_len);
	if (zerr)
		__shutdown_on_error(uep);
	pthread_mutex_unlock(&uep->ep.lock);
#ifdef ZAP_UGNI_DEBUG
	is_exit = getenv("ZAP_UGNI_CONN_EST_AFTER_CONNECT_MSG_TEST");
	if (is_exit)
		exit(0);
#endif /* ZAP_UGNI_DEBUG */
	if (uep->conn_data)
		free(uep->conn_data);
	uep->conn_data = NULL;
	uep->conn_data_len = 0;
	return;
}

static void ugni_sock_event(ovis_event_t ev)
{
	int rc;
	struct z_ugni_ep *uep = ev->param.ctxt;
	struct zap_event *zev = &uep->conn_ev;
	struct zap_ugni_send_wr *wr;

	/* Reaching here means bev is one of the EOF, ERROR or TIMEOUT */
	pthread_mutex_lock(&uep->ep.lock);
	int defer = 0;
	if (!LIST_EMPTY(&uep->post_desc_list))
		defer = 1;
	rc = ovis_scheduler_event_del(io_sched, ev);
	assert(rc == 0); /* ev must be in the scheduler, otherwise it is a bug */
	switch (uep->ep.state) {
	case ZAP_EP_ACCEPTING:
		uep->ep.state = ZAP_EP_ERROR;
		if (uep->app_accepted) {
			zev->type = ZAP_EVENT_CONNECT_ERROR;
		} else {
			/* app has rejected this, or doesn't know about this */
			goto no_cb;
		}
		break;
	case ZAP_EP_CONNECTING:
		zev->type = ZAP_EVENT_CONNECT_ERROR;
		uep->ep.state = ZAP_EP_ERROR;
		shutdown(uep->sock, SHUT_RDWR);
		break;
	case ZAP_EP_CONNECTED:
		/* Peer closed */
		uep->ep.state = ZAP_EP_PEER_CLOSE;
		shutdown(uep->sock, SHUT_RDWR);
	case ZAP_EP_CLOSE:
		/* Active close */
		zev->type = ZAP_EVENT_DISCONNECTED;
		break;
	case ZAP_EP_ERROR:
		goto no_cb;
	default:
		LOG_(uep, "Unexpected state for EOF %d.\n",
				uep->ep.state);
		uep->ep.state = ZAP_EP_ERROR;
		break;
	}
	DLOG_(uep, "%s: ep %p: state %s\n", __func__, uep,
				__zap_ep_state_str[uep->ep.state]);

	/* flush wr */
	while ((wr = STAILQ_FIRST(&uep->sq))) {
		STAILQ_REMOVE_HEAD(&uep->sq, link);
		__ugni_wr_complete(uep, wr, ZAP_ERR_FLUSH);
		free(wr);
	}

	pthread_mutex_unlock(&uep->ep.lock);
	if (defer) {
		/*
		* Allow time for uGNI to flush outstanding
		* completion events
		*/
		__ugni_defer_disconnect_event(uep);
	} else {
		__deliver_disconnect_ev(uep);
	}
	return;
no_cb:
	pthread_mutex_unlock(&uep->ep.lock);
	ref_put(&uep->ep.ref, "zap_new");
	return;
}

static zap_err_t
__setup_connection(struct z_ugni_ep *uep)
{
	int rc;

	DLOG_(uep, "setting up endpoint %p, fd: %d\n", uep, uep->sock);

	/* Initialize send and recv I/O events */
	OVIS_EVENT_INIT(&uep->io_ev);
	uep->io_ev.param.type = OVIS_EVENT_EPOLL;
	uep->io_ev.param.fd = uep->sock;
	uep->io_ev.param.cb_fn = ugni_sock_ev_cb;
	uep->io_ev.param.epoll_events = EPOLLIN|EPOLLOUT;
	uep->io_ev.param.ctxt = uep;

	rc = ovis_scheduler_event_add(io_sched, &uep->io_ev);
	if (rc) {
		return ZAP_ERR_RESOURCE;
	}

	return ZAP_ERR_OK;
}

/**
 * This is a callback function for evconnlistener_new_bind (in z_ugni_listen).
 */
static void __z_ugni_conn_request(ovis_event_t ev)
{
	int rc;
	struct z_ugni_ep *uep = ev->param.ctxt;
	zap_ep_t new_ep;
	struct z_ugni_ep *new_uep;
	zap_err_t zerr;
	int sockfd;
	struct sockaddr sa;
	socklen_t sa_len = sizeof(sa);

	assert(ev->cb.epoll_events & EPOLLIN);
	sockfd = accept(ev->param.fd, &sa, &sa_len);
	if (sockfd < 0) {
		LOG_(uep, "accept() error %d: in %s at %s:%d\n",
				errno , __func__, __FILE__, __LINE__);
		return;
	}

	rc = __set_sockbuf_sz(sockfd);
	if (rc) {
		close(sockfd);
		zerr = ZAP_ERR_TRANSPORT;
		LOG_(uep, "Error %d: fail to set the sockbuf sz in %s.\n",
				errno, __func__);
		return;
	}

	new_ep = zap_new(uep->ep.z, uep->ep.app_cb);
	if (!new_ep) {
		close(sockfd);
		zerr = errno;
		LOG_(uep, "Zap Error %d (%s): in %s at %s:%d\n",
				zerr, zap_err_str(zerr) , __func__, __FILE__,
				__LINE__);
		return;
	}

	void *uctxt = zap_get_ucontext(&uep->ep);
	zap_set_ucontext(new_ep, uctxt);
	new_uep = (void*) new_ep;
	new_uep->sock = sockfd;
	new_uep->ep.state = ZAP_EP_ACCEPTING;
	new_uep->sock_connected = 1;

	rc = __sock_nonblock(new_uep->sock);
	if (rc) {
		LOG_(uep, "__sock_nonblock() error %d:"
			  " in %s at %s:%d\n",
				rc, __func__, __FILE__, __LINE__);
		/* synchronous error */
		zap_free(new_ep);
		return;
	}

	OVIS_EVENT_INIT(&new_uep->io_ev);
	new_uep->io_ev.param.type = OVIS_EVENT_EPOLL;
	new_uep->io_ev.param.fd = new_uep->sock;
	new_uep->io_ev.param.cb_fn = ugni_sock_ev_cb;
	new_uep->io_ev.param.epoll_events = EPOLLIN;
	new_uep->io_ev.param.ctxt = new_uep;

	rc = ovis_scheduler_event_add(io_sched, &new_uep->io_ev);
	if (rc) {
		LOG_(uep, "ovis_scheduler_event_add() error %d:"
			  " in %s at %s:%d\n",
				rc, __func__, __FILE__, __LINE__);
		/* synchronous error */
		zap_free(new_ep);
	}

	/*
	 * NOTE: At this point, the connection is socket-connected.  It is not
	 * yet zap-connected. The passive side does not yet have enough GNI
	 * information.  The active side will send a ZAP_UGNI_MSG_CONN_REQ
	 * message to the passive side to share its GNI address information.
	 * Then, the ZAP_EVENT_CONNECT_REQUEST will be generated. The passive
	 * side can become zap-connected by calling zap_accept() in the zap
	 * event call back.
	 */

	return;
}

static zap_err_t z_ugni_listen(zap_ep_t ep, struct sockaddr *sa,
				socklen_t sa_len)
{
	struct z_ugni_ep *uep = (void*)ep;
	zap_err_t zerr;
	int rc, flags;

	zerr = zap_ep_change_state(&uep->ep, ZAP_EP_INIT, ZAP_EP_LISTENING);
	if (zerr)
		goto err_0;

	uep->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (uep->sock == -1) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_0;
	}
	rc = __sock_nonblock(uep->sock);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}
	flags = 1;
	rc = setsockopt(uep->sock, SOL_SOCKET, SO_REUSEADDR,
			&flags, sizeof(flags));
	if (rc == -1) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}

	/* bind - listen */
	rc = bind(uep->sock, sa, sa_len);
	if (rc) {
		if (errno == EADDRINUSE)
			zerr = ZAP_ERR_BUSY;
		else
			zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}
	rc = listen(uep->sock, 1024);
	if (rc) {
		if (errno == EADDRINUSE)
			zerr = ZAP_ERR_BUSY;
		else
			zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}

	/* setup ovis event */
	OVIS_EVENT_INIT(&uep->io_ev);
	uep->io_ev.param.type = OVIS_EVENT_EPOLL;
	uep->io_ev.param.fd = uep->sock;
	uep->io_ev.param.epoll_events = EPOLLIN;
	uep->io_ev.param.cb_fn = __z_ugni_conn_request;
	uep->io_ev.param.ctxt = uep;
	rc = ovis_scheduler_event_add(io_sched, &uep->io_ev);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}

	return ZAP_ERR_OK;

 err_1:
	close(uep->sock);
 err_0:
	return zerr;
}

static zap_err_t z_ugni_send(zap_ep_t ep, char *buf, size_t len)
{
	struct z_ugni_ep *uep = (void*)ep;
	struct zap_ugni_send_wr *wr;
	struct zap_ugni_msg_regular *msg;
	zap_err_t zerr;

	/* node state validation */
	zerr = __node_state_check(uep);
	if (zerr)
		return zerr;

	wr = __wr_alloc(ZAP_UGNI_MSG_REGULAR, len, 1);
	if (!wr)
		return ZAP_ERR_RESOURCE;
	wr->ctxt = 0;
	wr->flags = UGNI_WR_F_COMPLETE | UGNI_WR_F_SEND;
	msg = &wr->msg.sendrecv;
	msg->data_len = htonl(len);
	if (buf && len)
		memcpy(wr->data, buf, len);

	pthread_mutex_lock(&uep->ep.lock);
	if (!uep->gni_ep || ep->state != ZAP_EP_CONNECTED) {
		pthread_mutex_unlock(&uep->ep.lock);
		return ZAP_ERR_NOT_CONNECTED;
	}

	zerr = __wr_post(uep, wr);
	if (zerr)
		free(wr);
	pthread_mutex_unlock(&uep->ep.lock);
	return zerr;
}

static zap_err_t
z_ugni_send_mapped(zap_ep_t ep, zap_map_t map, void *buf, size_t len,
		   void *context)
{
	struct z_ugni_ep *uep = (void*)ep;
	zap_err_t zerr;
	struct zap_ugni_msg_regular *msg;
	struct zap_ugni_send_wr *wr;

	/* map validation */
	if (map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;
	if (z_map_access_validate(map, buf, len, 0))
		return ZAP_ERR_LOCAL_LEN;

	/* node state validation */
	zerr = __node_state_check(uep);
	if (zerr)
		return zerr;

	wr = __wr_alloc(ZAP_UGNI_MSG_REGULAR, len, 0);
	if(!wr)
		return ZAP_ERR_TRANSPORT;
	wr->ctxt = context;
	wr->flags = UGNI_WR_F_COMPLETE | UGNI_WR_F_SEND_MAPPED;
	wr->data = buf;
	msg = &wr->msg.sendrecv;
	msg->data_len = htonl(len);
	pthread_mutex_lock(&uep->ep.lock);
	zerr = __wr_post(uep, wr);
	if (zerr)
		free(wr);
	pthread_mutex_unlock(&uep->ep.lock);

	return zerr;
}

static struct ovis_event_s stalled_timeout_ev;
static void stalled_timeout_cb(ovis_event_t ev)
{
	struct zap_ugni_post_desc *desc;
	struct timeval now;

	pthread_mutex_lock(&z_ugni_list_mutex);
	gettimeofday(&now, NULL);
	desc = LIST_FIRST(&stalled_desc_list);
	while (desc) {
		zap_ugni_log("%s: %s: Freeing stalled post desc:\n",
			__func__, desc->ep_name);
		if (zap_ugni_stalled_timeout <= now.tv_sec - desc->stalled_time.tv_sec) {
			ZUGNI_LIST_REMOVE(desc, stalled_link);
			free(desc);
		} else {
			break;
		}
		desc = LIST_FIRST(&stalled_desc_list);
	}
	if (ugni_old_cq) {
		/*
		 * Attempt to cleanup the CQ that was replaced. This
		 * will fail until all endpoints associated with the
		 * old CQ are destroyed
		 */
		gni_return_t grc = GNI_CqDestroy(ugni_old_cq);
		if (grc == GNI_RC_SUCCESS) {
			zap_ugni_log("%s: Successfully destroyed old CQ\n",
				     __func__);
			ugni_old_cq = NULL;
		} else {
			zap_ugni_log("%s: Error %s destroying old CQ\n",
				     __func__, gni_ret_str(grc));
		}
	}
	pthread_mutex_unlock(&z_ugni_list_mutex);
}

static uint8_t __get_ptag()
{
	const char *tag = getenv("ZAP_UGNI_PTAG");
	if (!tag)
		return 0;
	return atoi(tag);
}

static uint32_t __get_cookie()
{
	const char *str = getenv("ZAP_UGNI_COOKIE");
	if (!str)
		return 0;
	return strtoul(str, NULL, 0);
}

static uint32_t __get_cq_depth()
{
	const char *str = getenv("ZAP_UGNI_CQ_DEPTH");
	if (!str)
		return ZAP_UGNI_CQ_DEPTH;
	return strtoul(str, NULL, 0);
}

static int __get_disconnect_timeout()
{
	const char *str = getenv("ZAP_UGNI_DISCONNECT_TIMEOUT");
	if (!str) {
		str = getenv("ZAP_UGNI_UNBIND_TIMEOUT");
		if (!str)
			return ZAP_UGNI_DISCONNECT_TIMEOUT;
	}
	return atoi(str);
}

static int __get_stalled_timeout()
{
	const char *str = getenv("ZAP_UGNI_STALLED_TIMEOUT");
	if (!str)
		return ZAP_UGNI_STALLED_TIMEOUT;
	return atoi(str);
}

static int __get_max_num_ep()
{
	const char *str = getenv("ZAP_UGNI_MAX_NUM_EP");
	if (!str)
		return ZAP_UGNI_MAX_NUM_EP;
	return atoi(str);
}

#define UGNI_NODE_PREFIX "nid"
static int __get_nodeid(struct sockaddr *sa, socklen_t sa_len)
{
	int rc = 0;
	char host[HOST_NAME_MAX];
	rc = getnameinfo(sa, sa_len, host, HOST_NAME_MAX,
					NULL, 0, NI_NAMEREQD);
	if (rc)
		return -1;

	char *ptr = strstr(host, UGNI_NODE_PREFIX);
	if (!ptr) {
		return -1;
	}
	ptr = 0;
	int id = strtol(host + strlen(UGNI_NODE_PREFIX), &ptr, 10);
	if (ptr[0] != '\0') {
		return -1;
	}
	return id;
}

static int __get_node_state()
{
	int i, node_id;
	rs_node_array_t nodelist;
	if (rca_get_sysnodes(&nodelist)) {
		_node_state.rca_get_failed++;
		if ((_node_state.rca_get_failed %
				_node_state.rca_log_thresh) == 0) {
			LOG("ugni: rca_get_sysnodes failed.\n");
		}

		for (i = 0; i < ZAP_UGNI_MAX_NUM_NODE; i++)
			_node_state.node_state[i] = ZAP_UGNI_NODE_GOOD;

		_node_state.state_ready = -1;
		return -1;
	}

	_node_state.rca_get_failed = 0;
	if (nodelist.na_len >= ZAP_UGNI_MAX_NUM_NODE) {
		zap_ugni_log("Number of nodes %d exceeds ZAP_UGNI_MAX_NUM_NODE "
				"%d.\n", nodelist.na_len, ZAP_UGNI_MAX_NUM_NODE);
	}
	for (i = 0; i < nodelist.na_len && i < ZAP_UGNI_MAX_NUM_NODE; i++) {
		node_id = nodelist.na_ids[i].rs_node_s._node_id;
		_node_state.node_state[node_id] =
			nodelist.na_ids[i].rs_node_s._node_state;
	}
	free(nodelist.na_ids);
	_node_state.state_ready = 1;
	return 0;
}

/*
 * return 0 if the state is good. Otherwise, 1 is returned.
 */
static int __check_node_state(int node_id)
{
	while (_node_state.state_ready != 1) {
		/* wait for the state to be populated. */
		if (_node_state.state_ready == -1) {
			/*
			 * XXX: FIXME: Handle this case
			 * For now, when rca_get_sysnodes fails,
			 * the node states are set to UGNI_NODE_GOOD.
			 */
			break;
		}
	}

	if (node_id != -1){
		if (node_id >= ZAP_UGNI_MAX_NUM_NODE) {
			zap_ugni_log("node_id %d exceeds ZAP_UGNI_MAX_NUM_NODE "
					"%d.\n", node_id, ZAP_UGNI_MAX_NUM_NODE);
			return 1;
		}
		if (_node_state.node_state[node_id] != ZAP_UGNI_NODE_GOOD)
			return 1; /* not good */
	}

	return 0; /* good */
}

void ugni_node_state_cb(ovis_event_t ev)
{
	__get_node_state(); /* FIXME: what if this fails? */
}

void *node_state_proc(void *args)
{
	rs_node_t node;
	int rc;
	struct ovis_event_s ns_ev;

	/* Initialize the inst_id here. */
	pthread_mutex_lock(&ugni_lock);
	rc = rca_get_nodeid(&node);
	if (rc) {
		_dom.inst_id = -1;
	} else {
		_dom.inst_id = (node.rs_node_s._node_id << 16) | (uint32_t)getpid();
	}
	pthread_cond_signal(&inst_id_cond);
	pthread_mutex_unlock(&ugni_lock);
	if (rc)
		return NULL;

	OVIS_EVENT_INIT(&ns_ev);
	ns_ev.param.type = OVIS_EVENT_PERIODIC;
	ns_ev.param.cb_fn = ugni_node_state_cb;
	ns_ev.param.periodic.period_us = _node_state.state_interval_us;
	ns_ev.param.periodic.phase_us = _node_state.state_offset_us;

	__get_node_state(); /* FIXME: what if this fails? */

	rc = ovis_scheduler_event_add(node_state_sched, &ns_ev);
	assert(rc == 0);

	rc = ovis_scheduler_loop(node_state_sched, 0);
	DLOG("Exiting the node state thread, rc: %d\n", rc);

	return NULL;
}

int __get_state_interval()
{
	int interval, offset;
	char *thr = getenv("ZAP_UGNI_STATE_INTERVAL");
	if (!thr) {
		DLOG("Note: no envvar ZAP_UGNI_STATE_INTERVAL.\n");
		goto err;
	}

	char *ptr;
	int tmp = strtol(thr, &ptr, 10);
	if (ptr[0] != '\0') {
		LOG("Invalid ZAP_UGNI_STATE_INTERVAL value (%s)\n", thr);
		goto err;
	}
	if (tmp < 100000) {
		LOG("Invalid ZAP_UGNI_STATE_INTERVAL value (%s). "
				"Using 100ms.\n", thr);
		interval = 100000;
	} else {
		interval = tmp;
	}

	thr = getenv("ZAP_UGNI_STATE_OFFSET");
	if (!thr) {
		DLOG("Note: no envvar ZAP_UGNI_STATE_OFFSET.\n");
		offset = 0;
		goto out;
	}

	tmp = strtol(thr, &ptr, 10);
	if (ptr[0] != '\0') {
		LOG("Invalid ZAP_UGNI_STATE_OFFSET value (%s)\n", thr);
		goto err;
	}

	offset = tmp;
	if (!(interval >= labs(offset) * 2)){ /* FIXME: What should this check be ? */
		LOG("Invalid ZAP_UGNI_STATE_OFFSET value (%s)."
				" Using 0ms.\n", thr);
		offset = 0;
	}
out:
	_node_state.state_interval_us = interval;
	_node_state.state_offset_us = offset;
	_node_state.check_state = 1;
	return 0;
err:
	_node_state.state_interval_us = 0;
	_node_state.state_offset_us = 0;
	_node_state.check_state = 0;
	return -1;
}

static int ugni_node_state_thread_init()
{
	int rc = 0;
	rc = __get_state_interval();
	if (rc) {
		/* Don't check node states if failed to get the interval */
		return 0;
	}

	_node_state.state_ready = 0;
	_node_state.rca_get_failed = 0;
	_node_state.rca_log_thresh = ZAP_UGNI_RCA_LOG_THS;
	_node_state.rca_get_failed = 0;

	_node_state.node_state = malloc(ZAP_UGNI_MAX_NUM_NODE * sizeof(int));
	if (!_node_state.node_state) {
		LOG("Failed to create node state array. Out of memory\n");
		errno = ENOMEM;
		return -1;
	}
	if (!node_state_sched) {
		node_state_sched = ovis_scheduler_new();
		if (!node_state_sched)
			return errno;
	}
	rc = pthread_create(&node_state_thread, NULL, node_state_proc, NULL);
	if (rc)
		return rc;
	pthread_setname_np(node_state_thread, "ugni:node_state");
	return 0;
}

static int z_ugni_init()
{
	int rc = 0;
	gni_return_t grc;
	static char buff[256];
	int fd;
	ssize_t rdsz;

	pthread_mutex_lock(&ugni_lock);
	if (zap_ugni_dom_initialized)
		goto out;

	fd = open(VERSION_FILE, O_RDONLY);
	if (fd < 0) {
		LOG("ERROR: Cannot open version file: %s\n",
				VERSION_FILE);
		rc = errno;
		goto out;
	}
	rdsz = read(fd, buff, sizeof(buff) - 1);
	if (rdsz < 0) {
		LOG("version file read error (errno %d): %m\n", errno);
		close(fd);
		rc = errno;
		goto out;
	}
	buff[rdsz] = 0;
	close(fd);

	if (strstr(buff, "cray_ari")) {
		_dom.type = ZAP_UGNI_TYPE_ARIES;
	}

	if (strstr(buff, "cray_gem")) {
		_dom.type = ZAP_UGNI_TYPE_GEMINI;
	}

	if (_dom.type == ZAP_UGNI_TYPE_NONE) {
		LOG("ERROR: cannot determine ugni type\n");
		rc = EINVAL;
		goto out;
	}

	_dom.euid = geteuid();
	_dom.cookie = __get_cookie();
	DLOG("cookie: %#x\n", _dom.cookie);

	switch (_dom.type) {
	case ZAP_UGNI_TYPE_ARIES:
#ifdef GNI_FIND_ALLOC_PTAG
		_dom.ptag = GNI_FIND_ALLOC_PTAG;
		DLOG("ugni_type: aries\n");
#else
		DLOG("ERROR: This library has not been compiled"
			" with ARIES support\n");
		rc = EINVAL;
		goto out;
#endif
		break;
	case ZAP_UGNI_TYPE_GEMINI:
		_dom.ptag = __get_ptag();
		DLOG("ugni_type: gemini\n");
		break;
	default:
		rc = EINVAL;
		goto out;
	}

	DLOG("ptag: %#hhx\n", _dom.ptag);

	_dom.limits.mdd_limit = GNI_JOB_INVALID_LIMIT;
	_dom.limits.a.mrt_limit = GNI_JOB_INVALID_LIMIT;
	_dom.limits.b.gart_limit = GNI_JOB_INVALID_LIMIT;
	_dom.limits.fma_limit = GNI_JOB_INVALID_LIMIT;
	_dom.limits.bte_limit = GNI_JOB_INVALID_LIMIT;
	_dom.limits.cq_limit = GNI_JOB_INVALID_LIMIT;

	_dom.limits.ntt_ctrl = GNI_JOB_CTRL_NTT_CLEANUP;

	_dom.limits.ntt_size = 0;
	_dom.limits.ntt_base = 0;

	if (!_dom.cdm) {
		if (((int)_dom.euid == 0) ||
			(_dom.type == ZAP_UGNI_TYPE_GEMINI)) {
			/* Do this if run as root or of type Gemini */
			grc = GNI_ConfigureJob(0, 0, _dom.ptag, _dom.cookie,
							&_dom.limits);
			if (grc) {
				LOG("ERROR: GNI_ConfigureJob() failed: %s\n",
						gni_ret_str(grc));
				rc = grc;
				goto out;
			}
		}
		uint32_t fma_mode;
		int dedicated;
		char *dedicated_s = getenv("ZAP_UGNI_FMA_DEDICATED");
		if (dedicated_s)
			dedicated = atoi(dedicated_s);
		else
			dedicated = 0;
		if (dedicated)
			fma_mode = GNI_CDM_MODE_FMA_DEDICATED;
		else
			fma_mode = GNI_CDM_MODE_FMA_SHARED;
		grc = GNI_CdmCreate(_dom.inst_id, _dom.ptag, _dom.cookie,
				    fma_mode, &_dom.cdm);
		if (grc) {
			LOG("ERROR: GNI_CdmCreate() failed: %s\n",
					gni_ret_str(grc));
			rc = grc;
			goto out;
		}
	}

	if (!_dom.nic) {
		grc = GNI_CdmAttach(_dom.cdm, 0, &_dom.pe_addr, &_dom.nic);
		if (grc) {
			LOG("ERROR: GNI_CdmAttach() failed: %s\n",
					gni_ret_str(grc));
			rc = grc;
			goto out;
		}
	}
	if (!_dom.cq) {
		_dom.cq_depth = __get_cq_depth();
		grc = GNI_CqCreate(_dom.nic, _dom.cq_depth, 0, GNI_CQ_BLOCKING,
				NULL, NULL, &_dom.cq);
		if (grc) {
			zap_ugni_log("ERROR: GNI_CqCreate() failed: %s\n",
					gni_ret_str(grc));
			rc = grc;
			goto out;
		}
	}
	rc = pthread_create(&cq_thread, NULL, cq_thread_proc, NULL);
	if (rc) {
		LOG("ERROR: pthread_create() failed: %d\n", rc);
		goto out;
	}
	pthread_setname_np(cq_thread, "ugni:cq_proc");
	zap_ugni_dom_initialized = 1;
	rc = pthread_create(&error_thread, NULL, error_thread_proc, NULL);
	if (rc) {
		LOG("ERROR: pthread_create() failed: %d\n", rc);
	}
	pthread_setname_np(error_thread, "ugni:error");
out:
	pthread_mutex_unlock(&ugni_lock);
	return rc;
}

int init_once()
{
	int rc = ENOMEM;

	ugni_stats = zap_thrstat_new("ugni:cq_proc", ZAP_ENV_INT(ZAP_THRSTAT_WINDOW));
	rc = ugni_node_state_thread_init();
	if (rc)
		return rc;

	/*
	 * We cannot call the rca APIs from different threads.
	 * The node_state_thread calls rca_get_sysnodes to get the node states.
	 * To construct a unique ID to attach CM, rca_get_nodeid is called.
	 */
	pthread_mutex_lock(&ugni_lock);
	if (!_node_state.check_state) {
		rs_node_t node;
		/*
		 * The node_state_thread isn't created, so the nodeid isn't
		 * initilized. Do it here.
		 */
		rc = rca_get_nodeid(&node);
		if (rc) {
			pthread_mutex_unlock(&ugni_lock);
			goto err;
		}

		_dom.inst_id = (node.rs_node_s._node_id << 16) | (uint32_t)getpid();
	} else {
		/*
		 * The node_state_thread is created and the node id will be
		 * initialized in there. Wait until it is done.
		 */
		while (_dom.inst_id == 0) {
			pthread_cond_wait(&inst_id_cond, &ugni_lock);
			if (_dom.inst_id == 0)
				continue;

			if (_dom.inst_id == -1) {
				/* Error getting the node ID */
				pthread_mutex_unlock(&ugni_lock);
				goto err;
			}
		}
	}

	zap_ugni_disconnect_timeout = __get_disconnect_timeout();
	zap_ugni_stalled_timeout = __get_stalled_timeout();

	/*
	 * Get the number of maximum number of endpoints zap_ugni will handle.
	 */
	zap_ugni_max_num_ep = __get_max_num_ep();
#ifdef DEBUG
	zap_ugni_ep_id = calloc(zap_ugni_max_num_ep, sizeof(uint32_t));
	if (!zap_ugni_ep_id)
		goto err;
#endif /* DEBUG */
	pthread_mutex_unlock(&ugni_lock);

	rc = z_ugni_init();
	if (rc)
		goto err;

	if (!io_sched) {
		io_sched = ovis_scheduler_new();
		if (!io_sched) {
			rc = errno;
			goto err;
		}
	}

	OVIS_EVENT_INIT(&stalled_timeout_ev);
	stalled_timeout_ev.param.type = OVIS_EVENT_TIMEOUT;
	stalled_timeout_ev.param.timeout.tv_sec = zap_ugni_stalled_timeout;
	stalled_timeout_ev.param.cb_fn = stalled_timeout_cb;
	stalled_timeout_ev.param.ctxt = NULL;
	rc = ovis_scheduler_event_add(io_sched, &stalled_timeout_ev);

	rc = pthread_create(&io_thread, NULL, io_thread_proc, 0);
	if (rc)
		goto err;

	init_complete = 1;

	return 0;
err:
	z_ugni_cleanup();
	return rc;
}

zap_ep_t z_ugni_new(zap_t z, zap_cb_fn_t cb)
{
	gni_return_t grc;
	struct z_ugni_ep *uep = calloc(1, sizeof(*uep));
	DLOG("Creating ep: %p\n", uep);
	if (!uep) {
		errno = ZAP_ERR_RESOURCE;
		goto err0;
	}
	uep->sock = -1;
	uep->ep_id = -1;
	uep->gni_cq = _dom.cq;

	uep->rbuff = malloc(ZAP_UGNI_INIT_RECV_BUFF_SZ);
	if (!uep->rbuff)
		goto err1;
	uep->rbuff->len = 0;
	uep->rbuff->alen = ZAP_UGNI_INIT_RECV_BUFF_SZ - sizeof(*uep->rbuff);

	STAILQ_INIT(&uep->sq);
	pthread_cond_init(&uep->sq_cond, NULL);
	LIST_INIT(&uep->post_desc_list);
	grc = GNI_EpCreate(_dom.nic, _dom.cq, &uep->gni_ep);
	if (grc) {
		LOG("GNI_EpCreate() failed: %s\n", gni_ret_str(grc));
		errno = ZAP_ERR_RESOURCE;
		goto err2;
	}
	uep->node_id = -1;
	pthread_mutex_lock(&z_ugni_list_mutex);
#ifdef DEBUG
	uep->ep_id = zap_ugni_get_ep_id();
	if (uep->ep_id < 0) {
		LOG_(uep, "%s: %p: Failed to get the zap endpoint ID\n",
				__func__, uep);
		errno = ZAP_ERR_RESOURCE;
		pthread_mutex_unlock(&z_ugni_list_mutex);
		grc = GNI_EpDestroy(uep->gni_ep);
		if (grc) {
			LOG_(uep, "%s: %p: GNI_EpDestroy() failed: %s\n",
					__func__, uep, gni_ret_str(grc));
		}
		goto err2;
	}
#endif /* DEBUG */
	LIST_INSERT_HEAD(&z_ugni_list, uep, link);
	pthread_mutex_unlock(&z_ugni_list_mutex);
	DLOG_(uep, "Created gni_ep: %p\n", uep->gni_ep);
	return (zap_ep_t)uep;
err2:
	free(uep->rbuff);
err1:
	free(uep);
err0:
	return NULL;
}

static void z_ugni_destroy(zap_ep_t ep)
{
	struct z_ugni_ep *uep = (void*)ep;
	struct zap_ugni_send_wr *wr;
	DLOG_(uep, "destroying endpoint %p\n", uep);
	pthread_mutex_lock(&z_ugni_list_mutex);
	ZUGNI_LIST_REMOVE(uep, link);
#ifdef DEBUG
	if (uep->ep_id >= 0)
		zap_ugni_ep_id[uep->ep_id] = 0;
#endif /* DEBUG */
	pthread_mutex_unlock(&z_ugni_list_mutex);

	while (!STAILQ_EMPTY(&uep->sq)) {
		wr = STAILQ_FIRST(&uep->sq);
		STAILQ_REMOVE_HEAD(&uep->sq, link);
		free(wr);
	}

	if (uep->rbuff)
		free(uep->rbuff);

	if (uep->conn_data) {
		free(uep->conn_data);
		uep->conn_data = 0;
	}
	if (uep->sock > -1) {
		close(uep->sock);
		uep->sock = -1;
	}
	__ep_release(uep);
	free(ep);
}

/* caller must hold uep->ep.lock */
static zap_err_t __ugni_send_accept(struct z_ugni_ep *uep, char *buf, size_t len)
{
	zap_err_t zerr;
	struct zap_ugni_msg_accepted *msg;
	struct zap_ugni_send_wr *wr = __wr_alloc(ZAP_UGNI_MSG_ACCEPTED, len, 1);
	if (!wr)
		return ZAP_ERR_RESOURCE;
	msg = &wr->msg.accept;
	msg->data_len = htonl(len);
	msg->inst_id = htonl(_dom.inst_id);
	msg->pe_addr = htonl(_dom.pe_addr);
	if (buf && len)
		memcpy(wr->data, buf, len);
	DLOG_(uep, "Sending ZAP_UGNI_MSG_ACCEPTED\n");
	zerr = __wr_post(uep, wr);
	if (zerr) {
		free(wr);
		return zerr;
	}
	return ZAP_ERR_OK;
}

zap_err_t z_ugni_accept(zap_ep_t ep, zap_cb_fn_t cb, char *data, size_t data_len)
{
	/* ep is the newly created ep from __z_ugni_conn_request */
	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;
	zap_err_t zerr;

	pthread_mutex_lock(&uep->ep.lock);

	if (uep->ep.state != ZAP_EP_ACCEPTING) {
		zerr = ZAP_ERR_ENDPOINT;
		goto err;
	}

	uep->ep.cb = cb;

	zerr = __ugni_send_accept(uep, data, data_len);
	if (zerr) {
		/*
		 * shutdown() and return ZAP_ERR_OK
		 * and let the ugni_sock_event()
		 * deliver the CONNECT_ERROR event and cleanup and unbind the endpoint.
		 * This is to avoid having multiple cleanup path.
		 */
		shutdown(uep->sock, SHUT_RDWR);
	}
	uep->app_accepted = 1;
	pthread_mutex_unlock(&uep->ep.lock);
	return ZAP_ERR_OK;
err:
	pthread_mutex_unlock(&uep->ep.lock);
	return zerr;
}

static zap_err_t z_ugni_reject(zap_ep_t ep, char *data, size_t data_len)
{
	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;
	zap_err_t zerr;

	pthread_mutex_lock(&uep->ep.lock);
	zerr = __ugni_send(uep, ZAP_UGNI_MSG_REJECTED, data, data_len);
	if (zerr)
		goto err;
	uep->ep.state = ZAP_EP_ERROR;
	pthread_mutex_unlock(&uep->ep.lock);
	return ZAP_ERR_OK;
err:
	shutdown(uep->sock, SHUT_RDWR);
	pthread_mutex_unlock(&uep->ep.lock);
	return zerr;
}

static zap_err_t z_ugni_unmap(zap_map_t map)
{
	if ((map->type == ZAP_MAP_REMOTE) && map->ep)
		ref_put(&map->ep->ref, "zap_map/rendezvous");
	return ZAP_ERR_OK;
}

static zap_err_t z_ugni_share(zap_ep_t ep, zap_map_t map,
				const char *msg, size_t msg_len)
{
	zap_err_t rc;
	gni_mem_handle_t *mh;
	struct z_ugni_ep *uep = (void*) ep;

	/* validate */
	if (ep->state != ZAP_EP_CONNECTED)
		return ZAP_ERR_NOT_CONNECTED;

	if (map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;

	mh = map_mh(map);
	if (!mh)
		return ZAP_ERR_RESOURCE;

	/* node state validation */
	rc = __node_state_check(uep);
	if (rc)
		return rc;

	/* prepare message */
	struct zap_ugni_msg_rendezvous *msgr;
	struct zap_ugni_send_wr *wr = __wr_alloc(ZAP_UGNI_MSG_RENDEZVOUS, msg_len, 1);
	if (!wr)
		return ZAP_ERR_RESOURCE;
	wr->dsz = msg_len;
	wr->msz = sizeof(*msgr);
	msgr = &wr->msg.rendezvous;
	msgr->gni_mh.qword1  =  htobe64(mh->qword1);
	msgr->gni_mh.qword2  =  htobe64(mh->qword2);
	msgr->addr           =  htobe64((uint64_t)map->addr);
	msgr->data_len       =  htonl(map->len);
	msgr->acc            =  htonl(map->acc);
	if (msg && msg_len)
		memcpy(wr->data, msg, msg_len);

	pthread_mutex_lock(&uep->ep.lock);
	rc = __wr_post(uep, wr);
	pthread_mutex_unlock(&uep->ep.lock);
	if (rc)
		free(wr);
	return rc;
}

static zap_err_t z_ugni_read(zap_ep_t ep, zap_map_t src_map, char *src,
			     zap_map_t dst_map, char *dst, size_t sz,
			     void *context)
{
	zap_err_t zerr;

	if (((uint64_t)src) & 3)
		return ZAP_ERR_PARAMETER;
	if (((uint64_t)dst) & 3)
		return ZAP_ERR_PARAMETER;
	if (sz & 3)
		return ZAP_ERR_PARAMETER;

	if (z_map_access_validate(src_map, src, sz, ZAP_ACCESS_READ) != 0)
		return ZAP_ERR_REMOTE_PERMISSION;
	if (z_map_access_validate(dst_map, dst, sz, ZAP_ACCESS_READ) != 0)
		return ZAP_ERR_LOCAL_LEN;

	gni_mem_handle_t *src_mh = map_mh(src_map);
	gni_mem_handle_t *dst_mh = map_mh(dst_map);
	if (!src_mh || !dst_mh)
		return ZAP_ERR_RESOURCE;

	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;

	/* node state validation */
	zerr = __node_state_check(uep);
	if (zerr)
		return zerr;

	pthread_mutex_lock(&cq_full_lock);
	while (ugni_post_count > _dom.cq_depth / 2) {
		struct timespec timeout;
		clock_gettime(CLOCK_REALTIME, &timeout);
		timeout.tv_sec += 1;
		pthread_cond_timedwait(&cq_full_cond, &cq_full_lock, &timeout);
	}
	__sync_fetch_and_add(&ugni_post_count, 1);
	if (ugni_post_count > ugni_post_max)
		ugni_post_max = ugni_post_count;
	pthread_mutex_unlock(&cq_full_lock);

	pthread_mutex_lock(&ep->lock);
	if (!uep->gni_ep || ep->state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		goto out;
	}

	gni_return_t grc;
	struct zap_ugni_post_desc *desc = __alloc_post_desc(uep);
	if (!desc) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}

	desc->post.type = GNI_POST_RDMA_GET;
	desc->post.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->post.dlvr_mode = GNI_DLVMODE_IN_ORDER;
	desc->post.local_addr = (uint64_t)dst;
	desc->post.local_mem_hndl = *dst_mh;
	desc->post.remote_addr = (uint64_t)src;
	desc->post.remote_mem_hndl = *src_mh;
	desc->post.length = sz;
	desc->post.post_id = __sync_fetch_and_add(&ugni_post_id, 1);

	desc->context = context;
#ifdef DEBUG
	__sync_fetch_and_add(&ugni_io_count, 1);
#endif /* DEBUG */

	pthread_mutex_lock(&ugni_lock);
	if (uep->gni_cq == _dom.cq)
		grc = GNI_PostRdma(uep->gni_ep, &desc->post);
	else
		grc = GNI_RC_ERROR_RESOURCE;
	if (grc != GNI_RC_SUCCESS) {
		LOG_(uep, "%s: GNI_PostRdma() failed, grc: %s\n",
				__func__, gni_ret_str(grc));
		__shutdown_on_error(uep);
#ifdef DEBUG
		__sync_sub_and_fetch(&ugni_io_count, 1);
#endif /* DEBUG */
		__free_post_desc(desc);
		zerr = ZAP_ERR_RESOURCE;
		pthread_mutex_unlock(&ugni_lock);
		goto out;
	}
	pthread_mutex_unlock(&ugni_lock);
	zerr = ZAP_ERR_OK;
 out:
	pthread_mutex_unlock(&ep->lock);
	if (zerr) {
		/* Return the CQ credit */
		pthread_mutex_lock(&cq_full_lock);
		__sync_sub_and_fetch(&ugni_post_count, 1);
		assert((int)ugni_post_count >= 0);
		pthread_cond_broadcast(&cq_full_cond);
		pthread_mutex_unlock(&cq_full_lock);
	}
	return zerr;
}

static zap_err_t z_ugni_write(zap_ep_t ep, zap_map_t src_map, char *src,
			      zap_map_t dst_map, char *dst, size_t sz,
			      void *context)
{
	gni_return_t grc;
	zap_err_t zerr;

	if (((uint64_t)src) & 3)
		return ZAP_ERR_PARAMETER;
	if (((uint64_t)dst) & 3)
		return ZAP_ERR_PARAMETER;
	if (sz & 3)
		return ZAP_ERR_PARAMETER;

	if (z_map_access_validate(src_map, src, sz, ZAP_ACCESS_NONE) != 0)
		return ZAP_ERR_LOCAL_LEN;
	if (z_map_access_validate(dst_map, dst, sz, ZAP_ACCESS_WRITE) != 0)
		return ZAP_ERR_REMOTE_PERMISSION;


	gni_mem_handle_t *src_mh = map_mh(src_map);
	gni_mem_handle_t *dst_mh = map_mh(dst_map);
	if (!src_mh || !dst_mh)
		return ZAP_ERR_RESOURCE;

	struct z_ugni_ep *uep = (void*)ep;

	/* node state validation */
	zap_err_t zerr;
	zerr = __node_state_check(uep);
	if (zerr)
		return zerr;

	pthread_mutex_lock(&cq_full_lock);
	while (ugni_post_count > _dom.cq_depth / 2) {
		struct timespec timeout;
		clock_gettime(CLOCK_REALTIME, &timeout);
		timeout.tv_sec += 1;
		pthread_cond_timedwait(&cq_full_cond, &cq_full_lock, &timeout);
	}
	__sync_fetch_and_add(&ugni_post_count, 1);
	if (ugni_post_count > ugni_post_max)
		ugni_post_max = ugni_post_count;
	pthread_mutex_unlock(&cq_full_lock);

	pthread_mutex_lock(&ep->lock);
	if (!uep->gni_ep || ep->state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		goto out;
	}

	struct zap_ugni_post_desc *desc = __alloc_post_desc(uep);
	if (!desc) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}

	desc->post.type = GNI_POST_RDMA_PUT;
	desc->post.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->post.dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	desc->post.local_addr = (uint64_t)src;
	desc->post.local_mem_hndl = *src_mh;
	desc->post.remote_addr = (uint64_t)dst;
	desc->post.remote_mem_hndl = *dst_mh;
	desc->post.length = sz;
	desc->post.post_id = __sync_fetch_and_add(&ugni_post_id, 1);
	desc->context = context;

#ifdef DEBUG
	__sync_fetch_and_add(&ugni_io_count, 1);
#endif /* DEBUG */
	pthread_mutex_lock(&ugni_lock);
	if (uep->gni_cq == _dom.cq)
		grc = GNI_PostRdma(uep->gni_ep, &desc->post);
	else
		grc = GNI_RC_ERROR_RESOURCE;
	if (grc != GNI_RC_SUCCESS) {
		LOG_(uep, "%s: GNI_PostRdma() failed, grc: %s\n",
				__func__, gni_ret_str(grc));
		__shutdown_on_error(uep);
#ifdef DEBUG
		__sync_sub_and_fetch(&ugni_io_count, 1);
#endif /* DEBUG */
		__free_post_desc(desc);
		zerr = ZAP_ERR_RESOURCE;
		pthread_mutex_unlock(&ugni_lock);
		goto out;
	}
	pthread_mutex_unlock(&ugni_lock);
	zerr = ZAP_ERR_OK;
out:
	pthread_mutex_unlock(&ep->lock);
	if (zerr) {
		/* Return the CQ credit */
		pthread_mutex_lock(&cq_full_lock);
		__sync_sub_and_fetch(&ugni_post_count, 1);
		assert((int)ugni_post_count >= 0);
		pthread_cond_broadcast(&cq_full_cond);
		pthread_mutex_unlock(&cq_full_lock);
	}
	return zerr;
}

zap_err_t zap_transport_get(zap_t *pz, zap_log_fn_t log_fn,
			    zap_mem_info_fn_t mem_info_fn)
{
	zap_t z;
	size_t sendrecv_sz, rendezvous_sz;
	if (log_fn)
		zap_ugni_log = log_fn;
	if (!init_complete && init_once())
		goto err;

	z = calloc(1, sizeof (*z));
	if (!z)
		goto err;

	__mem_info_fn = mem_info_fn;

	sendrecv_sz = sizeof(struct zap_ugni_msg_regular);
	rendezvous_sz = sizeof(struct zap_ugni_msg_rendezvous);

	/* max_msg is unused (since RDMA) ... */
	z->max_msg = UGNI_SOCKBUF_SZ -
			(sendrecv_sz<rendezvous_sz?rendezvous_sz:sendrecv_sz);
	z->new = z_ugni_new;
	z->destroy = z_ugni_destroy;
	z->connect = z_ugni_connect;
	z->accept = z_ugni_accept;
	z->reject = z_ugni_reject;
	z->listen = z_ugni_listen;
	z->close = z_ugni_close;
	z->send = z_ugni_send;
	z->send_mapped = z_ugni_send_mapped;
	z->read = z_ugni_read;
	z->write = z_ugni_write;
	z->unmap = z_ugni_unmap;
	z->share = z_ugni_share;
	z->get_name = z_get_name;

	/* is it needed? */
	z->mem_info_fn = mem_info_fn;

	*pz = z;
	return ZAP_ERR_OK;

 err:
	return ZAP_ERR_RESOURCE;
}

