/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014-2017,2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2014-2017,2019 Open Grid Computing, Inc. All rights reserved.
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

static zap_log_fn_t zap_ugni_log = NULL;

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

/* Timeout before trying to unbind a gni endpoint again. */
#define ZAP_UGNI_UNBIND_TIMEOUT 5
/*
 * Deliver the disconnected event if zap_ugni has been
 * trying to unbind the gni bind for ZAP_UGNI_DISCONNECT_WALLTIME seconds.
 */
#define ZAP_UGNI_DISC_EV_TIMEOUT 3600
static int zap_ugni_unbind_timeout;
static int zap_ugni_disc_ev_timeout;

/*
 * Maximum number of endpoints zap_ugni will handle
 */
#define ZAP_UGNI_MAX_NUM_EP 32000
static int zap_ugni_max_num_ep;
static uint32_t *zap_ugni_ep_id;

static int reg_count;
static LIST_HEAD(mh_list, ugni_mh) mh_list;
static pthread_mutex_t ugni_mh_lock;

static ovis_scheduler_t io_sched;
static pthread_t io_thread;
static pthread_t cq_thread;

static void *io_thread_proc(void *arg);
static void *cq_thread_proc(void *arg);

static void ugni_sock_event(ovis_event_t ev);
static void ugni_sock_read(ovis_event_t ev);
static void ugni_sock_write(ovis_event_t ev);
static void ugni_sock_connect(ovis_event_t ev);

static void stalled_timeout_cb(ovis_event_t ev);
static zap_err_t __setup_connection(struct z_ugni_ep *uep);

static int __get_nodeid(struct sockaddr *sa, socklen_t sa_len);
static int __check_node_state(int node_id);

static void z_ugni_destroy(zap_ep_t ep);

static LIST_HEAD(, z_ugni_ep) z_ugni_list = LIST_HEAD_INITIALIZER(0);
static pthread_mutex_t z_ugni_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct zap_ugni_post_desc_list stalled_desc_list = LIST_HEAD_INITIALIZER(0);
#define ZAP_UGNI_STALLED_TIMEOUT 86400 /* 24 hours */
static int zap_ugni_stalled_timeout;

#ifdef DEBUG
static LIST_HEAD(, z_ugni_ep) deferred_list = LIST_HEAD_INITIALIZER(0);
static pthread_mutex_t deferred_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t ugni_io_count = 0;
static uint32_t ugni_post_count = 0;
#endif /* DEBUG */

static pthread_mutex_t ugni_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t inst_id_cond = PTHREAD_COND_INITIALIZER;

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
	zap_get_ep(&uep->ep);
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
	zap_put_ep(&uep->ep);
	free(d);
}

gni_return_t ugni_get_mh(struct z_ugni_ep *uep, void *addr,
				size_t size, gni_mem_handle_t *mh)
{
	gni_return_t grc = GNI_RC_SUCCESS;
	struct ugni_mh *umh;
	int need_mh = 0;
	unsigned long start;
	unsigned long end;

	pthread_mutex_lock(&ugni_mh_lock);
	umh = LIST_FIRST(&mh_list);
	if (!umh) {
		zap_mem_info_t mmi;
		mmi = uep->ep.z->mem_info_fn();
		start = (unsigned long)mmi->start;
		end = start + mmi->len;
		need_mh = 1;
	}
	if (!need_mh)
		goto out;

	umh = malloc(sizeof *umh);
	umh->start = start;
	umh->end = end;
	umh->ref_count = 0;

	grc = GNI_MemRegister(_dom.nic, umh->start, end - start,
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

static zap_err_t z_ugni_close(zap_ep_t ep)
{
	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;

	DLOG_(uep, "Closing xprt: %p, state: %s\n", uep,
			__zap_ep_state_str(uep->ep.state));
	pthread_mutex_lock(&uep->ep.lock);
	switch (uep->ep.state) {
	case ZAP_EP_LISTENING:
	case ZAP_EP_CONNECTED:
	case ZAP_EP_PEER_CLOSE:
		uep->ep.state = ZAP_EP_CLOSE;
		shutdown(uep->sock, SHUT_RDWR);
		break;
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
	zap_get_ep(&uep->ep); /* Release when disconnect/conn_error/rejected */
	rc = connect(uep->sock, sa, sa_len);
	if (rc && errno != EINPROGRESS) {
		zerr = ZAP_ERR_CONNECT;
	} else {
		zerr = ZAP_ERR_OK;
	}

	zerr = __setup_connection(uep);
	if (zerr)
		goto out;
 out:
	return zerr;
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
		goto out;
	}

	wsz = write(uep->sock, wr->data + wr->off, wr->alen);
	if (wsz < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			goto out;
		/* bad error */
		goto err;
	}

	if (wsz < wr->alen) {
		wr->alen -= wsz;
		wr->off += wsz;
		goto out;
	}

	/* reaching here means wr->alen == 0 */
	STAILQ_REMOVE_HEAD(&uep->sq, link);
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

	msg = (void*)uep->rbuff->data;

	msg->hdr.msg_len = ntohl(msg->hdr.msg_len);
	msg->hdr.reserved = 0;
	msg->hdr.msg_type = ntohs(msg->hdr.msg_type);
	msg->addr = be64toh(msg->addr);
	msg->acc = ntohl(msg->acc);
	msg->data_len = ntohl(msg->data_len);
	msg->gni_mh.qword1 = be64toh(msg->gni_mh.qword1);
	msg->gni_mh.qword2 = be64toh(msg->gni_mh.qword2);

	struct zap_ugni_map *map = calloc(1, sizeof(*map));
	if (!map) {
		LOG_(uep, "ENOMEM in %s at %s:%d\n",
				__func__, __FILE__, __LINE__);
		goto err0;
	}

	char *amsg = NULL;
	size_t amsg_len = msg->hdr.msg_len - sizeof(msg);
	if (amsg_len) {
		amsg = msg->msg; /* attached message from rendezvous */
	}

	map->map.ref_count = 1;
	map->map.ep = (void*)uep;
	map->map.acc = msg->acc;
	map->map.type = ZAP_MAP_REMOTE;
	map->map.addr = (void*)msg->addr;
	map->map.len = msg->data_len;
	map->gni_mh = msg->gni_mh;

	zap_get_ep(&uep->ep);
	pthread_mutex_lock(&uep->ep.lock);
	LIST_INSERT_HEAD(&uep->ep.map_list, &map->map, link);
	pthread_mutex_unlock(&uep->ep.lock);

	struct zap_event ev = {
		.type = ZAP_EVENT_RENDEZVOUS,
		.map = (void*)map,
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
	struct zap_event ev;
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
	ev.type = ZAP_EVENT_CONNECTED;
	ev.data_len = msg->data_len;
	ev.data = (msg->data_len)?((void*)msg->data):(NULL);
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
	struct zap_event ev;

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

	ev.type = ZAP_EVENT_CONNECT_REQUEST;
	ev.data_len = msg->data_len;
	ev.data = (msg->data_len)?((void*)msg->data):(NULL);
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
	struct zap_event ev;
	int rc;

	msg = (void*)uep->rbuff->data;

	ev.type = ZAP_EVENT_REJECTED;
	ev.status = ZAP_ERR_OK;
	ev.data_len = ntohl(msg->data_len);
	ev.data = (ev.data_len)?((void*)msg->data):(NULL);
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
		.type = ZAP_EVENT_CONNECTED,
		.status = ZAP_ERR_OK,
	};
	zap_get_ep(&uep->ep); /* Release when receive disconnect/error event */
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

	if (mlen - sizeof(struct zap_ugni_msg_hdr) > uep->ep.z->max_msg) {
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

static gni_return_t process_cq(gni_cq_handle_t cq, gni_cq_entry_t cqe_)
{
	gni_cq_entry_t cqe = cqe_;
	gni_return_t grc;
	gni_post_descriptor_t *post;
	int count = 0;
	do {
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
#ifdef DEBUG
		assert(ugni_post_count >= 0);
		__sync_sub_and_fetch(&ugni_post_count, 1);
#endif /* DEBUG */
		struct zap_ugni_post_desc *desc = (void*) post;
		if (grc) {
			if (!(grc == GNI_RC_SUCCESS ||
			      grc == GNI_RC_TRANSACTION_ERROR)) {
				DLOG("process_cq: grc %d\n", grc);
			}
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
			LOG("%s: Received a complete event of a stalled post "
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
		struct zap_event zev = {0};
		switch (desc->post.type) {
		case GNI_POST_RDMA_GET:
			DLOG_(uep, "RDMA_GET: Read complete %p with %s\n",
						desc, gni_ret_str(grc));
			if (grc) {
				zev.status = ZAP_ERR_RESOURCE;
				DLOG_(uep, "RDMA_GET: completing "
					"with error %s.\n",
					gni_ret_str(grc));
				__shutdown_on_error(uep);
			}
			zev.type = ZAP_EVENT_READ_COMPLETE;
			zev.context = desc->context;
			break;
		case GNI_POST_RDMA_PUT:
			DLOG_(uep, "RDMA_GET: Read complete %p with %s\n",
						desc, gni_ret_str(grc));
			if (grc) {
				zev.status = ZAP_ERR_RESOURCE;
				DLOG_(uep, "RDMA_PUT: completing "
					"with error %s.\n",
					gni_ret_str(grc));
				__shutdown_on_error(uep);
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

		uep->ep.cb(&uep->ep, &zev);
	skip:
		pthread_mutex_lock(&ugni_lock);
		grc = GNI_CqGetEvent(cq, &cqe);
		pthread_mutex_unlock(&ugni_lock);
	} while (grc != GNI_RC_NOT_DONE);
	if (count > 1)
		DLOG("process_cq: count %d\n", count);
	return GNI_RC_SUCCESS;
}

/* Caller must hold the endpoint list lock */
void __stall_post_desc(struct zap_ugni_post_desc *d, struct timeval time)
{
	zap_put_ep(&d->uep->ep);
	d->is_stalled = 1;
	d->uep = NULL;
	d->stalled_time = time;
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
		pthread_mutex_lock(&uep->ep.lock);
		__stall_post_desc(d, time);
		d = LIST_FIRST(&uep->post_desc_list);
	}
}

#define WAIT_5SECS 5000
static void *cq_thread_proc(void *arg)
{
	gni_return_t grc;
	gni_cq_entry_t cqe;
	int oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
	while (1) {
		uint64_t timeout = WAIT_5SECS;
		grc = GNI_CqWaitEvent(_dom.cq, timeout, &cqe);
		if (grc == GNI_RC_TIMEOUT) {
			DLOG("CqWaitEvent: TIMEOUT\n");
			continue;
		}
		if ((grc = process_cq(_dom.cq, cqe)))
			zap_ugni_log("Error %d processing CQ %p.\n",
					grc, _dom.cq);
	}
	return NULL;
}


/* msg hdr fields are in network-byte-order */
/* caller must held uep->ep.lock */
static zap_err_t __ugni_send_msg(struct z_ugni_ep *uep,
				 struct zap_ugni_msg_hdr *msg,
				 size_t msg_len,
				 const void *data,
				 size_t data_len)
{
	int rc;
	struct zap_ugni_send_wr *wr = malloc(sizeof(*wr) + msg_len + data_len);
	if (!wr)
		return ZAP_ERR_RESOURCE;
	wr->alen = msg_len + data_len;
	wr->off = 0;
	memcpy(wr->data, msg, msg_len);
	if (data && data_len)
		memcpy(wr->data + msg_len, data, data_len);
	STAILQ_INSERT_TAIL(&uep->sq, wr, link);
	rc = __enable_epoll_out(uep);
	if (rc) {
		return ZAP_ERR_RESOURCE;
	}
	return ZAP_ERR_OK;
}

/* caller must held uep->ep.lock */
static zap_err_t __ugni_send_connect(struct z_ugni_ep *uep, char *buf, size_t len)
{
	zap_err_t zerr;
	struct zap_ugni_msg_connect msg = {
		.hdr = {
			.msg_type = htons(ZAP_UGNI_MSG_CONNECT),
			.msg_len = htonl((uint32_t)(sizeof(msg) + len)),
		},
		.data_len = htonl(len),
		.inst_id = htonl(_dom.inst_id),
		.pe_addr = htonl(_dom.pe_addr),
	};

	ZAP_VERSION_SET(msg.ver);
	memcpy(&msg.sig, ZAP_UGNI_SIG, sizeof(msg.sig));

	zerr = __ugni_send_msg(uep, &msg.hdr, sizeof(msg), buf, len);
	if (zerr)
		return zerr;
	return ZAP_ERR_OK;
}

/* caller must held uep->ep.lock */
static zap_err_t
__ugni_send(struct z_ugni_ep *uep, enum zap_ugni_msg_type type,
						char *buf, size_t len)
{
	zap_err_t zerr;
	struct zap_ugni_msg_regular msg = {
		.hdr = {
			.msg_type = htons(type),
			.msg_len = htonl((uint32_t)(sizeof(msg) + len)),
		},
		.data_len = htonl(len),
	};

	zerr = __ugni_send_msg(uep, &msg.hdr, sizeof(msg), buf, len);
	if (zerr)
		return zerr;
	return ZAP_ERR_OK;
}

static void __deliver_ev(struct z_ugni_ep *uep)
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
	uep->ep.cb((void*)uep, &uep->conn_ev);
	zap_put_ep(&uep->ep);
}

void __deferred_delivering_cb(ovis_event_t ev)
{
	struct z_ugni_ep *uep = ev->param.ctxt;
	if (!LIST_EMPTY(&uep->post_desc_list))
		return; /* There are still outstanding rdma ops.
			 * NOTE: The timeout event is automatically re-armed
			 * until it is deleted from the scheduler. */
	ovis_scheduler_event_del(io_sched, ev);
	__deliver_ev(uep);
}

void __ugni_defer_delivering_event(struct z_ugni_ep *uep)
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
	uep->deferred_ev.param.timeout.tv_sec = zap_ugni_unbind_timeout;
	uep->deferred_ev.param.cb_fn = __deferred_delivering_cb;
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
	int call_cb = 0;
	int rc;
	struct z_ugni_ep *uep = ev->param.ctxt;
	struct zap_event *zev = &uep->conn_ev;

	/* Reaching here means bev is one of the EOF, ERROR or TIMEOUT */
	pthread_mutex_lock(&uep->ep.lock);
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
	if (uep->ugni_ep_bound) {
		gni_return_t grc = GNI_EpUnbind(uep->gni_ep);
		if (grc)
			DLOG_(uep, "GNI_EpUnbind() error: %s\n", gni_ret_str(grc));
	}
	pthread_mutex_unlock(&uep->ep.lock);
	if (call_cb) {
		__deliver_ev(uep);
	} else {
		/* Defer to give time to uGNI to flush the outstanding
		 * completion events */
		__ugni_defer_delivering_event(uep);
	}
	return;
no_cb:
	pthread_mutex_unlock(&uep->ep.lock);
	zap_put_ep(&uep->ep);
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
	zap_err_t zerr;

	/* node state validation */
	if (_node_state.check_state) {
		if (uep->node_id == -1) {
			struct sockaddr lsa, sa;
			socklen_t sa_len;
			zap_err_t zerr;
			zerr = zap_get_name(ep, &lsa, &sa, &sa_len);
			if (zerr) {
				DLOG("zap_get_name() error: %d\n", zerr);
				return ZAP_ERR_ENDPOINT;
			}
			uep->node_id = __get_nodeid(&sa, sa_len);
		}
		if (uep->node_id != -1) {
			if (__check_node_state(uep->node_id)) {
				DLOG("Node %d is in a bad state\n", uep->node_id);
				z_ugni_close(ep);
				return ZAP_ERR_ENDPOINT;
			}
		}
	}

	pthread_mutex_lock(&uep->ep.lock);
	if (ep->state != ZAP_EP_CONNECTED) {
		pthread_mutex_unlock(&uep->ep.lock);
		return ZAP_ERR_NOT_CONNECTED;
	}

	zerr = __ugni_send(uep, ZAP_UGNI_MSG_REGULAR, buf, len);
	pthread_mutex_unlock(&uep->ep.lock);
	return zerr;
}

static struct ovis_event_s stalled_timeout_ev;
#define KEEPALIVE_TIMEOUT 86400 /* 24 hours */
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

static int __get_unbind_timeout()
{
	const char *str = getenv("ZAP_UGNI_UNBIND_TIMEOUT");
	if (!str)
		return ZAP_UGNI_UNBIND_TIMEOUT;
	return atoi(str);
}

static int __get_disconnect_event_timeout()
{
	const char *str = getenv("ZAP_UGNI_DISCONNECT_EV_TIMEOUT");
	if (!str)
		return ZAP_UGNI_DISC_EV_TIMEOUT;
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

		grc = GNI_CdmCreate(_dom.inst_id, _dom.ptag, _dom.cookie,
				GNI_CDM_MODE_FMA_SHARED, &_dom.cdm);
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
	zap_ugni_dom_initialized = 1;
out:
	pthread_mutex_unlock(&ugni_lock);
	return rc;
}

int init_once()
{
	int rc = ENOMEM;

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

	/*
	 * Get the timeout for calling unbind and delivering a disconnected event.
	 */
	zap_ugni_disc_ev_timeout = __get_disconnect_event_timeout();
	zap_ugni_unbind_timeout = __get_unbind_timeout();
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

	uep->rbuff = malloc(ZAP_UGNI_INIT_RECV_BUFF_SZ);
	if (!uep->rbuff)
		goto err1;
	uep->rbuff->len = 0;
	uep->rbuff->alen = ZAP_UGNI_INIT_RECV_BUFF_SZ - sizeof(*uep->rbuff);

	STAILQ_INIT(&uep->sq);
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
	gni_return_t grc;
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
	if (uep->gni_ep) {
		DLOG_(uep, "Destroying gni_ep: %p\n", uep->gni_ep);
		grc = GNI_EpDestroy(uep->gni_ep);
		if (grc)
			LOG_(uep, "GNI_EpDestroy() error: %s\n", gni_ret_str(grc));
	}
	free(ep);
}

/* caller must hold uep->ep.lock */
static zap_err_t __ugni_send_accept(struct z_ugni_ep *uep, char *buf, size_t len)
{
	zap_err_t zerr;
	struct zap_ugni_msg_accepted msg = {
		.hdr = {
			.msg_type = htons(ZAP_UGNI_MSG_ACCEPTED),
			.msg_len = htonl((uint32_t)(sizeof(msg) + len)),
		},
		.data_len = htonl(len),
		.inst_id = htonl(_dom.inst_id),
		.pe_addr = htonl(_dom.pe_addr),
	};

	DLOG_(uep, "Sending ZAP_UGNI_MSG_ACCEPTED\n");

	zerr = __ugni_send_msg(uep, &msg.hdr, sizeof(msg), buf, len);
	if (zerr)
		return zerr;
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
	uep->ep.state = ZAP_EP_ERROR;
	zerr = __ugni_send(uep, ZAP_UGNI_MSG_REJECTED, data, data_len);
	if (zerr)
		goto err;
	pthread_mutex_unlock(&uep->ep.lock);
	return ZAP_ERR_OK;
err:
	shutdown(uep->sock, SHUT_RDWR);
	pthread_mutex_unlock(&uep->ep.lock);
	return zerr;
}

static zap_err_t
z_ugni_map(zap_ep_t ep, zap_map_t *pm, void *buf, size_t len, zap_access_t acc)
{
	struct zap_ugni_map *map = calloc(1, sizeof(*map));
	gni_return_t grc;
	zap_err_t zerr = ZAP_ERR_OK;
	if (!map) {
		zerr = ZAP_ERR_RESOURCE;
		goto err0;
	}

	grc = ugni_get_mh((void*)ep, buf, len, &map->gni_mh);
	if (grc) {
		zerr = ZAP_ERR_RESOURCE;
		goto err1;
	}

	*pm = (void*)map;
	goto out;
err1:
	free(map);
err0:
out:
	return zerr;
}

static zap_err_t z_ugni_unmap(zap_ep_t ep, zap_map_t map)
{
	struct zap_ugni_map *m = (void*) map;
	free(m);
	return ZAP_ERR_OK;
}

static zap_err_t z_ugni_share(zap_ep_t ep, zap_map_t map,
				const char *msg, size_t msg_len)
{
	zap_err_t rc;

	/* validate */
	if (ep->state != ZAP_EP_CONNECTED)
		return ZAP_ERR_NOT_CONNECTED;

	if (map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;

	struct z_ugni_ep *uep = (void*) ep;

	/* node state validation */
	if (_node_state.check_state) {
		if (uep->node_id == -1) {
			struct sockaddr lsa, sa;
			socklen_t sa_len;
			zap_err_t zerr;
			zerr = zap_get_name(ep, &lsa, &sa, &sa_len);
			if (zerr) {
				DLOG("zap_get_name() error: %d\n", zerr);
				return ZAP_ERR_ENDPOINT;
			}
			uep->node_id = __get_nodeid(&sa, sa_len);
		}
		if (uep->node_id != -1) {
			if (__check_node_state(uep->node_id)) {
				DLOG("Node %d is in a bad state\n", uep->node_id);
				z_ugni_close(ep);
				return ZAP_ERR_ENDPOINT;
			}
		}
	}

	/* prepare message */
	struct zap_ugni_map *smap = (struct zap_ugni_map *)map;
	struct zap_ugni_msg_rendezvous msgr = {
		.hdr = {
			.msg_type = htons(ZAP_UGNI_MSG_RENDEZVOUS),
			.msg_len = htonl(sizeof(msgr) + msg_len),
		},
		.gni_mh = {
			.qword1 = htobe64(smap->gni_mh.qword1),
			.qword2 = htobe64(smap->gni_mh.qword2),
		},
		.addr = htobe64((uint64_t)map->addr),
		.data_len = htonl(map->len),
		.acc = htonl(map->acc),

	};

	pthread_mutex_lock(&uep->ep.lock);
	rc = __ugni_send_msg(uep, &msgr.hdr, sizeof(msgr), msg, msg_len);
	pthread_mutex_unlock(&uep->ep.lock);

	return rc;
}

static zap_err_t z_ugni_read(zap_ep_t ep, zap_map_t src_map, char *src,
			     zap_map_t dst_map, char *dst, size_t sz,
			     void *context)
{
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

	struct z_ugni_ep *uep = (struct z_ugni_ep *)ep;
	struct zap_ugni_map *smap = (struct zap_ugni_map *)src_map;
	struct zap_ugni_map *dmap = (struct zap_ugni_map *)dst_map;

	/* node state validation */
	if (_node_state.check_state) {
		if (uep->node_id == -1) {
			struct sockaddr lsa, sa;
			socklen_t sa_len;
			zap_err_t zerr;
			zerr = zap_get_name(ep, &lsa, &sa, &sa_len);
			if (zerr) {
				DLOG("zap_get_name() error: %d\n", zerr);
				return ZAP_ERR_ENDPOINT;
			}
			uep->node_id = __get_nodeid(&sa, sa_len);
		}
		if (uep->node_id != -1) {
			if (__check_node_state(uep->node_id)) {
				DLOG("Node %d is in a bad state\n", uep->node_id);
				z_ugni_close(ep);
				return ZAP_ERR_ENDPOINT;
			}
		}
	}

	pthread_mutex_lock(&ep->lock);
	if (ep->state != ZAP_EP_CONNECTED) {
		pthread_mutex_unlock(&ep->lock);
		return ZAP_ERR_ENDPOINT;
	}

	gni_return_t grc;
	struct zap_ugni_post_desc *desc = __alloc_post_desc(uep);
	if (!desc) {
		pthread_mutex_unlock(&ep->lock);
		return ZAP_ERR_RESOURCE;
	}

	desc->post.type = GNI_POST_RDMA_GET;
	desc->post.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->post.dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	desc->post.local_addr = (uint64_t)dst;
	desc->post.local_mem_hndl = dmap->gni_mh;
	desc->post.remote_addr = (uint64_t)src;
	desc->post.remote_mem_hndl = smap->gni_mh;
	desc->post.length = sz;
	/*
	 * We can track the posted rdma using
	 * the returned gni_post_descriptor_t address.
	 *
	 * We abuse the post_id field to store the endpoint context
	 * so that we can check at the completion time
	 * whether the endpoint still exists or not.
	 */
#ifdef DEBUG
	desc->post.post_id = uep->ep_id;
#endif /* DEBUG */
	desc->context = context;
	pthread_mutex_unlock(&ep->lock);

	pthread_mutex_lock(&ugni_lock);
#ifdef DEBUG
	__sync_fetch_and_add(&ugni_io_count, 1);
	__sync_fetch_and_add(&ugni_post_count, 1);
#endif /* DEBUG */
	grc = GNI_PostRdma(uep->gni_ep, &desc->post);
	if (grc != GNI_RC_SUCCESS) {
		pthread_mutex_lock(&uep->ep.lock);
		__shutdown_on_error(uep);
		pthread_mutex_unlock(&uep->ep.lock);
		LOG_(uep, "%s: GNI_PostRdma() failed, grc: %s\n",
				__func__, gni_ret_str(grc));
#ifdef DEBUG
		__sync_sub_and_fetch(&ugni_io_count, 1);
		__sync_sub_and_fetch(&ugni_post_count, 1);
#endif /* DEBUG */
		__free_post_desc(desc);
		pthread_mutex_unlock(&ugni_lock);
		return ZAP_ERR_RESOURCE;
	}
	pthread_mutex_unlock(&ugni_lock);
	return ZAP_ERR_OK;
}

static zap_err_t z_ugni_write(zap_ep_t ep, zap_map_t src_map, char *src,
			      zap_map_t dst_map, char *dst, size_t sz,
			      void *context)
{
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

	struct z_ugni_ep *uep = (void*)ep;
	struct zap_ugni_map *smap = (void*)src_map;
	struct zap_ugni_map *dmap = (void*)dst_map;
	gni_return_t grc;

	/* node state validation */
	if (_node_state.check_state) {
		if (uep->node_id == -1) {
			struct sockaddr lsa, sa;
			socklen_t sa_len;
			zap_err_t zerr;
			zerr = zap_get_name(ep, &lsa, &sa, &sa_len);
			if (zerr) {
				DLOG("zap_get_name() error: %d\n", zerr);
				return ZAP_ERR_ENDPOINT;
			}
			uep->node_id = __get_nodeid(&sa, sa_len);
		}
		if (uep->node_id != -1) {
			if (__check_node_state(uep->node_id)) {
				DLOG("Node %d is in a bad state\n", uep->node_id);
				z_ugni_close(ep);
				return ZAP_ERR_ENDPOINT;
			}
		}
	}

	pthread_mutex_lock(&ep->lock);
	if (ep->state != ZAP_EP_CONNECTED) {
		pthread_mutex_unlock(&ep->lock);
		return ZAP_ERR_ENDPOINT;
	}

	struct zap_ugni_post_desc *desc = __alloc_post_desc(uep);
	if (!desc) {
		pthread_mutex_unlock(&ep->lock);
		return ZAP_ERR_ENDPOINT;
	}

	desc->post.type = GNI_POST_RDMA_PUT;
	desc->post.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->post.dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	desc->post.local_addr = (uint64_t)src;
	desc->post.local_mem_hndl = smap->gni_mh;
	desc->post.remote_addr = (uint64_t)dst;
	desc->post.remote_mem_hndl = dmap->gni_mh;
	desc->post.length = sz;
	desc->post.post_id = (uint64_t)(unsigned long)desc;
	desc->context = context;
	pthread_mutex_unlock(&ep->lock);

	pthread_mutex_lock(&ugni_lock);
#ifdef DEBUG
	__sync_fetch_and_add(&ugni_io_count, 1);
	__sync_fetch_and_add(&ugni_post_count, 1);
#endif /* DEBUG */
	grc = GNI_PostRdma(uep->gni_ep, &desc->post);
	if (grc != GNI_RC_SUCCESS) {
		pthread_mutex_lock(&uep->ep.lock);
		__shutdown_on_error(uep);
		pthread_mutex_unlock(&uep->ep.lock);
		LOG_(uep, "%s: GNI_PostRdma() failed, grc: %s\n",
				__func__, gni_ret_str(grc));
#ifdef DEBUG
		__sync_sub_and_fetch(&ugni_io_count, 1);
		__sync_sub_and_fetch(&ugni_post_count, 1);
#endif /* DEBUG */
		__free_post_desc(desc);
		pthread_mutex_unlock(&ugni_lock);
		return ZAP_ERR_RESOURCE;
	}
	pthread_mutex_unlock(&ugni_lock);
	return ZAP_ERR_OK;
}

zap_err_t zap_transport_get(zap_t *pz, zap_log_fn_t log_fn,
			    zap_mem_info_fn_t mem_info_fn)
{
	zap_t z;

	if (!zap_ugni_log) {
		if (log_fn)
			zap_ugni_log = log_fn;
		else
			zap_ugni_log = zap_ugni_default_log;
	}

	if (!init_complete && init_once())
		goto err;

	z = calloc(1, sizeof (*z));
	if (!z)
		goto err;

	/* max_msg is unused (since RDMA) ... */
	z->max_msg = (1024 * 1024) - sizeof(struct zap_ugni_msg_hdr);
	z->new = z_ugni_new;
	z->destroy = z_ugni_destroy;
	z->connect = z_ugni_connect;
	z->accept = z_ugni_accept;
	z->reject = z_ugni_reject;
	z->listen = z_ugni_listen;
	z->close = z_ugni_close;
	z->send = z_ugni_send;
	z->read = z_ugni_read;
	z->write = z_ugni_write;
	z->map = z_ugni_map;
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

static void __attribute__ ((destructor)) ugni_fini(void);
static void ugni_fini()
{
	struct ugni_mh *mh;
	while (!LIST_EMPTY(&mh_list)) {
		mh = LIST_FIRST(&mh_list);
		ZUGNI_LIST_REMOVE(mh, link);
		(void)GNI_MemDeregister(_dom.nic, &mh->mh);
		free(mh);
	}
}
