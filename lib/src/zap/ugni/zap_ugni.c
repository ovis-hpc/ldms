/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014 Sandia Corporation. All rights reserved.
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
 * Author: Narate Taerat <narate@ogc.us>
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
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include <endian.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>

#include "coll/rbt.h"

#include "zap_ugni.h"

#define VERSION_FILE "/proc/version"

#define LOG_(uep, ...) do { \
	if (uep && uep->ep.z && uep->ep.z->log_fn) \
		uep->ep.z->log_fn("zap_ugni: " __VA_ARGS__); \
} while(0);

#define LOG(...) do { \
	zap_ugni_log("zap_ugni: " __VA_ARGS__); \
} while(0);

#ifdef DEBUG
#define DLOG_(uep, ...) do { \
	if (uep && uep->ep.z && uep->ep.z->log_fn) \
		uep->ep.z->log_fn("zap_ugni [DEBUG]: " __VA_ARGS__); \
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

static struct event_base *io_event_loop;
static pthread_t io_thread;
static pthread_t cq_thread;

static void *io_thread_proc(void *arg);
static void *cq_thread_proc(void *arg);

static void ugni_sock_event(struct bufferevent *buf_event, short ev, void *arg);
static void ugni_sock_read(struct bufferevent *buf_event, void *arg);
static void ugni_sock_write(struct bufferevent *buf_event, void *arg);

static void timeout_cb(int fd , short events, void *arg);
static zap_err_t __setup_connection(struct z_ugni_ep *uep);

static void z_ugni_destroy(zap_ep_t ep);

static uint32_t z_last_key;
static struct rbt z_key_tree;
static pthread_mutex_t z_key_tree_mutex;

static LIST_HEAD(, z_ugni_ep) z_ugni_list = LIST_HEAD_INITIALIZER(0);
static pthread_mutex_t z_ugni_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t ugni_lock = PTHREAD_MUTEX_INITIALIZER;

static int zap_ugni_dom_initialized = 0;
static struct zap_ugni_dom {
	zap_ugni_type_t type;
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

/**
 * Allocate key for a \c map.
 *
 * \param map The map.
 *
 * \returns NULL on error.
 * \returns Allocated key structure for \c map.
 */
struct z_ugni_key *z_key_alloc(struct zap_ugni_map *map)
{
	struct z_ugni_key *key = calloc(1, sizeof(*key));
	if (!key)
		return NULL;
	key->map = map;
	pthread_mutex_lock(&z_key_tree_mutex);
	key->rb_node.key = (void*)(uint64_t)(++z_last_key);
	rbt_ins(&z_key_tree, &key->rb_node);
	pthread_mutex_unlock(&z_key_tree_mutex);
	return key;
}

struct z_ugni_key *__z_key_find(uint32_t key)
{
	struct rbn *krbn = rbt_find(&z_key_tree, (void*)(uint64_t)key);
	if (!krbn)
		return NULL;
	return container_of(krbn, struct z_ugni_key, rb_node);
}

struct z_ugni_key *z_key_find(uint32_t key)
{
	struct z_ugni_key *k;
	pthread_mutex_lock(&z_key_tree_mutex);
	k = __z_key_find(key);
	pthread_mutex_unlock(&z_key_tree_mutex);
	return k;
}

void z_key_delete(uint32_t key)
{
	struct z_ugni_key *k;
	pthread_mutex_lock(&z_key_tree_mutex);
	k = __z_key_find(key);
	if (!k)
		goto out;
	rbt_del(&z_key_tree, &k->rb_node);
	free(k);
out:
	pthread_mutex_unlock(&z_key_tree_mutex);
}

static struct zap_ugni_post_desc *__alloc_post_desc(struct z_ugni_ep *uep)
{
	struct zap_ugni_post_desc *d = calloc(1, sizeof(*d));
	if (!d)
		return NULL;
	d->uep = uep;
	return d;
}

static void __free_post_desc(struct zap_ugni_post_desc *d)
{
	free(d);
}

static void release_buf_event(struct z_ugni_ep *r);

/**
 * validate map access.
 *
 * \param map The map.
 * \param p The start of the accessing memory.
 * \param sz The size of the accessing memory.
 * \param acc Access flags.
 *
 * \returns 0 for valid access.
 * \returns ERANGE For invalid range access.
 * \returns EACCES For invalid access permission.
 */
int __z_map_access_validate(zap_map_t map, void *p, size_t sz, zap_access_t acc)
{
	if (p < map->addr || (map->addr + map->len) < (p + sz))
		return ERANGE;
	if ((map->acc & acc) != acc)
		return EACCES;
	return 0;
}

/**
 * Validate access by map key.
 *
 * \param key The map key.
 * \param p The start of the accessing memory.
 * \param sz The size of the accessing memory.
 * \param acc Access flags.
 */
int __z_map_key_access_validate(uint32_t key, void *p, size_t sz,
				zap_access_t acc)
{
	struct z_ugni_key *k = z_key_find(key);
	if (!k)
		return ENOENT;
	return __z_map_access_validate((zap_map_t)k->map, p, sz, acc);
}

/*** (BEGIN) Host-Network conversion utilities ***/

uint16_t __htobe16(uint16_t x)
{
	return htobe16(x);
}

uint32_t __htobe32(uint32_t x)
{
	return htobe32(x);
}

uint64_t __htobe64(uint64_t x)
{
	return htobe64(x);
}

uint16_t __be16toh(uint16_t x)
{
	return be16toh(x);
}

uint32_t __be32toh(uint32_t x)
{
	return be32toh(x);
}

uint64_t __be64toh(uint64_t x)
{
	return be64toh(x);
}

struct __convert_fns {
	uint16_t (*u16)(uint16_t);
	uint32_t (*u32)(uint32_t);
	uint64_t (*u64)(uint64_t);
};

struct __convert_fns __hton_fns = {
	.u16 = __htobe16,
	.u32 = __htobe32,
	.u64 = __htobe64,
};

struct __convert_fns __ntoh_fns = {
	.u16 = __be16toh,
	.u32 = __be32toh,
	.u64 = __be64toh,
};

#define __APPLY(var, func) (var = func(var))

void __convert_zap_ugni_msg_regular(struct zap_ugni_msg_hdr *hdr,
				struct __convert_fns *fns)
{
	struct zap_ugni_msg_regular *msg = (void*) hdr;
	__APPLY(msg->data_len, fns->u32);
}

void __convert_zap_ugni_msg_rendezvous(struct zap_ugni_msg_hdr *hdr,
				   struct __convert_fns *fns)
{
	struct zap_ugni_msg_rendezvous *msg = (void*) hdr;
	__APPLY(msg->addr, fns->u64);
	__APPLY(msg->data_len, fns->u32);
	__APPLY(msg->gni_mh.qword1, fns->u64);
	__APPLY(msg->gni_mh.qword2, fns->u64);
}

void __convert_zap_ugni_msg_accepted(struct zap_ugni_msg_hdr *hdr,
					struct __convert_fns *fns)
{
	struct zap_ugni_msg_accepted *msg = (void*) hdr;
	__APPLY(msg->pe_addr, fns->u32);
	__APPLY(msg->inst_id, fns->u32);
}

void __convert_zap_ugni_msg_conn_req(struct zap_ugni_msg_hdr *hdr,
					struct __convert_fns *fns)
{
	struct zap_ugni_msg_conn_req *msg = (void*) hdr;
	__APPLY(msg->pe_addr, fns->u32);
	__APPLY(msg->inst_id, fns->u32);
}

typedef void (*__convert_fn_t)(struct zap_ugni_msg_hdr*, struct __convert_fns*);
__convert_fn_t __zap_ugni_msg_convert_fn[] = {
	[ZAP_UGNI_MSG_REGULAR]     =  __convert_zap_ugni_msg_regular,
	[ZAP_UGNI_MSG_RENDEZVOUS]  =  __convert_zap_ugni_msg_rendezvous,
	[ZAP_UGNI_MSG_ACCEPTED]    =  __convert_zap_ugni_msg_accepted,
	[ZAP_UGNI_MSG_CONN_REQ]    =  __convert_zap_ugni_msg_conn_req,
};

void hton_zap_ugni_msg(struct zap_ugni_msg_hdr *hdr)
{
	__zap_ugni_msg_convert_fn[hdr->msg_type](hdr, &__hton_fns);
	__APPLY(hdr->msg_type, htons);
	__APPLY(hdr->msg_len, htonl);
}

void ntoh_zap_ugni_msg(struct zap_ugni_msg_hdr *hdr)
{
	__APPLY(hdr->msg_type, ntohs);
	__APPLY(hdr->msg_len, ntohl);
	__zap_ugni_msg_convert_fn[hdr->msg_type](hdr, &__ntoh_fns);
}

/*** (END) Host-Network conversion utilities ***/


void z_ugni_cleanup(void)
{
	void *dontcare;

	if (io_event_loop)
		event_base_loopbreak(io_event_loop);
	if (io_thread) {
		pthread_cancel(io_thread);
		pthread_join(io_thread, &dontcare);
	}
	if (io_event_loop)
		event_base_free(io_event_loop);
}

static zap_err_t z_ugni_close(zap_ep_t ep)
{
	struct z_ugni_ep *uep = (void*)ep;

	DLOG_(uep, "Closing xprt: %p, state: %s\n", uep,
			zap_ep_state_str(uep->ep.state));
	pthread_mutex_lock(&uep->ep.lock);
	switch (uep->ep.state) {
	case ZAP_EP_CONNECTED:
		uep->ep.state = ZAP_EP_CLOSE;
		shutdown(uep->sock, SHUT_RDWR);
		break;
	case ZAP_EP_LISTENING:
	case ZAP_EP_CONNECTING:
	case ZAP_EP_PEER_CLOSE:
		uep->ep.state = ZAP_EP_ERROR;
		break;
	default:
		/* do nothing (suppressing compiler warning) */
		break;
	}
	uep->sock = -1; /* buffer event will close fd */
	pthread_mutex_unlock(&uep->ep.lock);
	zap_put_ep(&uep->ep);
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

static int __set_keep_alive(struct z_ugni_ep *uep)
{
	int rc;
	int optval;
	rc = setsockopt(uep->sock, SOL_SOCKET, SO_KEEPALIVE, &optval,
			sizeof(int));
	if (rc) {
		LOG_(uep, "WARNING: set SO_KEEPALIVE error: %d\n", errno);
		return errno;
	}
	optval = ZAP_UGNI_SOCK_KEEPCNT;
	rc = setsockopt(uep->sock, SOL_TCP, TCP_KEEPCNT, &optval, sizeof(int));
	if (rc) {
		LOG_(uep, "WARNING: set TCP_KEEPCNT error: %d\n", errno);
		return errno;
	}
	optval = ZAP_UGNI_SOCK_KEEPIDLE;
	rc = setsockopt(uep->sock, SOL_TCP, TCP_KEEPIDLE, &optval, sizeof(int));
	if (rc) {
		LOG_(uep, "WARNING: set TCP_KEEPIDLE error: %d\n", errno);
		return errno;
	}
	optval = ZAP_UGNI_SOCK_KEEPINTVL;
	rc = setsockopt(uep->sock, SOL_TCP, TCP_KEEPINTVL, &optval, sizeof(int));
	if (rc) {
		LOG_(uep, "WARNING: set TCP_KEEPINTVL error: %d\n", errno);
		return errno;
	}
	return 0;
}

static zap_err_t z_ugni_connect(zap_ep_t ep,
				struct sockaddr *sa, socklen_t sa_len)
{
	int rc;
	zap_err_t zerr;
	struct z_ugni_ep *uep = (void*)ep;
	zerr = zap_ep_change_state(&uep->ep, ZAP_EP_INIT, ZAP_EP_CONNECTING);
	if (zerr)
		goto out;

	uep->sock = socket(sa->sa_family, SOCK_STREAM, 0);
	if (uep->sock == -1) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}
	rc = evutil_make_socket_nonblocking(uep->sock);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}
	rc = __set_keep_alive(uep);
	if (rc) {
		LOG_(uep, "WARNING: __set_keep_alive() rc: %d\n", rc);
	}
	zerr = __setup_connection(uep);
	if (zerr)
		goto out;

	zap_get_ep(&uep->ep);
	if (bufferevent_socket_connect(uep->buf_event, sa, sa_len)) {
		/* Error starting connection */
		bufferevent_free(uep->buf_event);
		uep->buf_event = NULL;
		zerr = ZAP_ERR_CONNECT;
		zap_put_ep(&uep->ep);
		goto out;
	}

 out:
	return zerr;
}

static void ugni_sock_write(struct bufferevent *buf_event, void *arg)
{
	/* Do nothing */
}

/**
 * Process an unknown message in the end point.
 */
static void process_sep_msg_unknown(struct z_ugni_ep *uep)
{
	/* Decide what to do and IMPLEMENT ME */
	LOG_(uep, "WARNING: Unknown zap message.\n");
	struct zap_event ev = {
		.type = ZAP_EVENT_CONNECT_ERROR,
	};
	uep->ep.cb((void*)uep, &ev);
}

/**
 * Receiving a regular message.
 */
static void process_sep_msg_regular(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_regular msg;
	bufferevent_read(uep->buf_event, &msg, sizeof(msg));
	ntoh_zap_ugni_msg((void*)&msg);
	char *data = malloc(msg.data_len);
	if (!data) {
		LOG_(uep, "ENOMEM at %s in %s:%d\n",
		     __func__, __FILE__, __LINE__);
		return;
	}
	if (msg.data_len)
		bufferevent_read(uep->buf_event, data, msg.data_len);
	struct zap_event ev = {
		.type = ZAP_EVENT_RECV_COMPLETE,
		.data = data,
		.data_len = msg.data_len,
	};
	uep->ep.cb((void*)uep, &ev);
	free(data);
}

/**
 * Receiving a rendezvous (share) message.
 */
static void process_sep_msg_rendezvous(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_rendezvous msg;
	bufferevent_read(uep->buf_event, &msg, sizeof(msg));
	ntoh_zap_ugni_msg((void*)&msg);
	struct zap_ugni_map *map = calloc(1, sizeof(*map));
	if (!map) {
		LOG_(uep, "ENOMEM in %s at %s:%d\n",
				__func__, __FILE__, __LINE__);
		goto err0;
	}

	char *amsg = NULL;
	size_t amsg_len = msg.hdr.msg_len - sizeof(msg);
	if (amsg_len) {
		amsg = malloc(amsg_len);
		if (!amsg) {
			LOG_(uep, "ENOMEM in %s at %s:%d\n",
					__func__, __FILE__, __LINE__);
			goto err1;
		}
		size_t rb = bufferevent_read(uep->buf_event, amsg, amsg_len);
		if (rb != amsg_len) {
			/* read error */
			goto err2;
		}
	}

	map->map.ep = (void*)uep;
	map->map.acc = msg.acc;
	map->map.type = ZAP_MAP_REMOTE;
	map->map.addr = (void*)msg.addr;
	map->map.len = msg.data_len;
	map->gni_mh = msg.gni_mh;

	struct zap_event ev = {
		.type = ZAP_EVENT_RENDEZVOUS,
		.map = (void*)map,
		.data_len = amsg_len,
		.data = amsg
	};

	uep->ep.cb((void*)uep, &ev);

	free(amsg); /* map is owned by cb() function, but amsg is not. */
	return;
err2:
	free(amsg);
err1:
	free(map);
err0:
	return;
}

static void process_sep_msg_accepted(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_accepted msg;
	gni_return_t grc;
	struct zap_event ev = {
		.type = ZAP_EVENT_CONNECTED
	};
	bufferevent_read(uep->buf_event, &msg, sizeof(msg));
	assert(uep->ep.state == ZAP_EP_CONNECTING);
	ntoh_zap_ugni_msg((void*)&msg);
	DLOG_(uep, "ACCEPTED received: pe_addr: %#x, inst_id: %#x\n",
			msg.pe_addr, msg.inst_id);
	grc = GNI_EpBind(uep->gni_ep, msg.pe_addr, msg.inst_id);
	if (grc) {
		LOG_(uep, "GNI_EpBind() error: %s\n", gni_ret_str(grc));
		return;
	}
	if (!zap_ep_change_state(&uep->ep, ZAP_EP_CONNECTING, ZAP_EP_CONNECTED))
		uep->ep.cb((void*)uep, &ev);
	else
		LOG_(uep, "'Accept' message received in unexpected state %d.\n",
		     uep->ep.state);
}

static void process_sep_msg_conn_req(struct z_ugni_ep *uep)
{
	struct zap_ugni_msg_conn_req msg;
	gni_return_t grc;
	struct zap_event ev = {
		.type = ZAP_EVENT_CONNECT_REQUEST
	};
	bufferevent_read(uep->buf_event, &msg, sizeof(msg));
	assert(uep->ep.state == ZAP_EP_CONNECTING);
	ntoh_zap_ugni_msg((void*)&msg);
	DLOG_(uep, "CONN_REQ received: pe_addr: %#x, inst_id: %#x\n",
			msg.pe_addr, msg.inst_id);
	grc = GNI_EpBind(uep->gni_ep, msg.pe_addr, msg.inst_id);
	if (grc) {
		LOG_(uep, "GNI_EpBind() error: %s\n", gni_ret_str(grc));
		return;
	}
	uep->ep.cb((void*)uep, &ev);
}

typedef void(*process_sep_msg_fn_t)(struct z_ugni_ep*);
process_sep_msg_fn_t process_sep_msg_fns[] = {
	[ZAP_UGNI_MSG_REGULAR]     =  process_sep_msg_regular,
	[ZAP_UGNI_MSG_RENDEZVOUS]  =  process_sep_msg_rendezvous,
	[ZAP_UGNI_MSG_ACCEPTED]    =  process_sep_msg_accepted,
	[ZAP_UGNI_MSG_CONN_REQ]    =  process_sep_msg_conn_req,
};

#define min_t(t, x, y) (t)((t)x < (t)y?(t)x:(t)y)
static void ugni_sock_read(struct bufferevent *buf_event, void *arg)
{
	struct z_ugni_ep *uep = (struct z_ugni_ep *)arg;
	struct evbuffer *evb;
	struct zap_ugni_msg_hdr hdr;
	size_t reqlen;
	size_t buflen;
	zap_ugni_msg_type_t msg_type;
	do {
		evb = bufferevent_get_input(buf_event);
		buflen = evbuffer_get_length(evb);
		if (buflen < sizeof(hdr))
			break;
		evbuffer_copyout(evb, &hdr, sizeof(hdr));
		reqlen = ntohl(hdr.msg_len);
		if (buflen < reqlen)
			break;
		msg_type = ntohs(hdr.msg_type);
		DLOG_(uep, "Receiving msg: %s\n",
				zap_ugni_msg_type_str(msg_type));
		if (msg_type < ZAP_UGNI_MSG_TYPE_LAST)
			process_sep_msg_fns[msg_type](uep);
		else /* unknown type */
			process_sep_msg_unknown(uep);

	} while (1);
}

static void *io_thread_proc(void *arg)
{
	/* Zap thread will not handle any signal */
	int rc;
	sigset_t sigset;
	sigfillset(&sigset);
	rc = pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	assert(rc == 0 && "pthread_sigmask error");
	event_base_dispatch(io_event_loop);
	return NULL;
}

static gni_return_t process_cq(gni_cq_handle_t cq, gni_cq_entry_t cqe)
{
	gni_return_t grc;
	gni_post_descriptor_t *post;
	do {
		if (GNI_CQ_GET_TYPE(cqe) != GNI_CQ_EVENT_TYPE_POST) {
			zap_ugni_log("Unexepcted cqe type %d cqe"
					" %08x on CQ %p\n",
					GNI_CQ_GET_TYPE(cqe), cqe, cq);
			goto skip;
		}
		pthread_mutex_lock(&ugni_lock);
		post = NULL;
		grc = GNI_GetCompleted(cq, cqe, &post);
		pthread_mutex_unlock(&ugni_lock);
		if (grc) {
			if (!(grc == GNI_RC_SUCCESS ||
			      grc == GNI_RC_TRANSACTION_ERROR))
				continue;
		}
		struct zap_ugni_post_desc *desc = (void*) post;
		if (!desc) {
			zap_ugni_log("Post descriptor is Null!\n");
			goto skip;
		}

		struct zap_event zev = {0};
		switch (desc->post.type) {
		case GNI_POST_RDMA_GET:
			if (grc) {
				zev.status = ZAP_ERR_RESOURCE;
				zap_ugni_log("%s update completing "
						"with error %d.\n",
						__func__, grc);
			}
			zev.type = ZAP_EVENT_READ_COMPLETE;
			zev.context = desc->context;
			break;
		case GNI_POST_RDMA_PUT:
			if (grc) {
				zev.status = ZAP_ERR_RESOURCE;
				zap_ugni_log("%s update completing "
						"with error %d.\n",
						__func__, grc);
			}
			zev.type = ZAP_EVENT_WRITE_COMPLETE;
			zev.context = desc->context;
			break;
		default:
			zap_ugni_log("Unknown completion "
					     "type %d on transport %p.\n",
					     desc->post.type, desc->uep);
		}
		desc->uep->ep.cb(&desc->uep->ep, &zev);
		__free_post_desc(desc);
	skip:
		pthread_mutex_lock(&ugni_lock);
		grc = GNI_CqGetEvent(cq, &cqe);
		pthread_mutex_unlock(&ugni_lock);
	} while (grc == GNI_RC_SUCCESS);

	return GNI_RC_SUCCESS;
}

#define WAIT_20SECS 20000
static void *cq_thread_proc(void *arg)
{
	gni_return_t grc;
	gni_cq_entry_t event_data;
	gni_cq_entry_t cqe;
	uint32_t which;
	int oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
	while (1) {
		uint64_t timeout = WAIT_20SECS;
		grc = GNI_CqWaitEvent(_dom.cq, timeout, &cqe);
		if (grc == GNI_RC_TIMEOUT)
			continue;
		if ((grc = process_cq(_dom.cq, cqe)))
			zap_ugni_log("Error %d processing CQ %p.\n",
					grc, _dom.cq);
	}
	return NULL;
}


static void release_buf_event(struct z_ugni_ep *uep)
{
	if (uep->listen_ev) {
		DLOG_(uep, "Destroying listen_ev\n");
		evconnlistener_free(uep->listen_ev);
		uep->listen_ev = NULL;
	}
	if (uep->buf_event) {
		DLOG_(uep, "Destroying buf_event\n");
		bufferevent_free(uep->buf_event);
		uep->buf_event = NULL;
	}
}

static void ugni_sock_event(struct bufferevent *buf_event, short bev, void *arg)
{

	struct z_ugni_ep *uep = arg;
	pthread_mutex_lock(&uep->ep.lock);
	static const short bev_mask = BEV_EVENT_EOF | BEV_EVENT_ERROR |
				     BEV_EVENT_TIMEOUT;
	if (!(bev & bev_mask)) {
		/*
		 * This is BEV_EVENT_CONNECTED on initiator side.
		 *
		 * The underlying socket is connected, but we won't call it
		 * zap-connected yet. The other end can choose to either
		 * zap-accept the connection by sending an ACCEPT message or
		 * reject this connection by immediately close it (see case
		 * ZAP_EP_CONNECTING above). Zap_socket active-side endpoint is
		 * zap-connected when it receives ACCEPT message.
		 *
		 * Hence, we just ignore this BEV_EVENT_CONNECTED and wait for
		 * next event (ACCEPT or REJECT) to occur. Thus, no callback
		 * function call in this case, just notify libevent that the
		 * underlying socket is ready to work.
		 */
		if (bufferevent_enable(uep->buf_event, EV_READ | EV_WRITE)) {
			LOG_(uep, "Error enabling buffered I/O event for fd %d.\n",
					uep->sock);
		}

		struct zap_ugni_msg_conn_req msg;
		msg.hdr.msg_len = sizeof(msg);
		msg.hdr.msg_type = ZAP_UGNI_MSG_CONN_REQ;
		msg.inst_id = _dom.inst_id;
		msg.pe_addr = _dom.pe_addr;
		DLOG_(uep, "Sending CONN_REQ ...\n");
		hton_zap_ugni_msg((void*)&msg);
		bufferevent_write(uep->buf_event, &msg, sizeof(msg));
		pthread_mutex_unlock(&uep->ep.lock);
		return;
	}

	/* Reaching here means bev is one of the EOF, ERROR or TIMEOUT */

	struct zap_event ev = { 0 };

	release_buf_event(uep);

	switch (uep->ep.state) {
		case ZAP_EP_CONNECTING:
			if ((bev & bev_mask) == BEV_EVENT_EOF)
				ev.type = ZAP_EVENT_REJECTED;
			else
				ev.type = ZAP_EVENT_CONNECT_ERROR;
			uep->ep.state = ZAP_EP_ERROR;
			break;
		case ZAP_EP_CONNECTED:
			/* Peer close (passive close) */
			ev.type = ZAP_EVENT_DISCONNECTED;
			uep->ep.state = ZAP_EP_PEER_CLOSE;
			break;
		case ZAP_EP_CLOSE:
			/* Active close */
			uep->sock = -1;
			ev.type = ZAP_EVENT_DISCONNECTED;
			uep->ep.state = ZAP_EP_ERROR;
			break;
		default:
			LOG_(uep, "Unexpected state for EOF %d.\n",
					uep->ep.state);
			uep->ep.state = ZAP_EP_ERROR;
			break;
	}
	pthread_mutex_unlock(&uep->ep.lock);
	uep->ep.cb((void*)uep, &ev);
	zap_put_ep(&uep->ep);
}

static zap_err_t
__setup_connection(struct z_ugni_ep *uep)
{
	DLOG_(uep, "setting up endpoint %p, fd: %d\n", uep, uep->sock);
	/* Initialize send and recv I/O events */
	uep->buf_event = bufferevent_socket_new(io_event_loop, uep->sock,
						BEV_OPT_THREADSAFE|
						BEV_OPT_CLOSE_ON_FREE);
	if(!uep->buf_event) {
		LOG_(uep, "Error initializing buffered I/O event for "
		     "fd %d.\n", uep->sock);
		return ZAP_ERR_RESOURCE;
	}

	bufferevent_setcb(uep->buf_event, ugni_sock_read, NULL,
						ugni_sock_event, uep);
	return ZAP_ERR_OK;
}

/**
 * This is a callback function for evconnlistener_new_bind (in z_ugni_listen).
 */
static void __z_ugni_conn_request(struct evconnlistener *listener,
			 evutil_socket_t sockfd,
			 struct sockaddr *address, int socklen, void *arg)
{
	struct z_ugni_ep *uep = arg;
	zap_ep_t new_ep;
	struct z_ugni_ep *new_uep;
	zap_err_t zerr;

	zerr = zap_new(uep->ep.z, &new_ep, uep->ep.cb);
	if (zerr) {
		LOG_(uep, "Zap Error %d (%s): in %s at %s:%d\n",
				zerr, zap_err_str(zerr) , __func__, __FILE__,
				__LINE__);
		return;
	}
	void *uctxt = zap_get_ucontext(&uep->ep);
	zap_set_ucontext(new_ep, uctxt);
	new_uep = (void*) new_ep;
	new_uep->sock = sockfd;
	new_uep->ep.state = ZAP_EP_CONNECTING;

	zerr = __setup_connection(new_uep);
	if (zerr)
		goto err_1;

	if (bufferevent_enable(new_uep->buf_event, EV_READ | EV_WRITE)) {
		LOG_(new_uep, "Error enabling buffered I/O event for fd %d.\n",
		     new_uep->sock);
		goto err_1;
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

err_1:
	/* clean up bad ep */
	z_ugni_destroy(new_ep);
}

static void __z_ugni_listener_err_cb(struct evconnlistener *listen_ev, void *args)
{
#ifdef DEBUG
	struct z_sock_ep *sep = (struct z_sock_ep *)args;
	sep->ep.z->log_fn("UGNI: libevent error '%s'\n", strerror(errno));
#endif
}

static zap_err_t z_ugni_listen(zap_ep_t ep, struct sockaddr *sa,
				socklen_t sa_len)
{
	struct z_ugni_ep *uep = (void*)ep;
	zap_err_t zerr;

	zerr = zap_ep_change_state(&uep->ep, ZAP_EP_INIT, ZAP_EP_LISTENING);
	if (zerr)
		goto err_0;

	zerr = ZAP_ERR_RESOURCE;
	uep->listen_ev = evconnlistener_new_bind(io_event_loop,
					       __z_ugni_conn_request, uep,
					       LEV_OPT_THREADSAFE |
					       LEV_OPT_REUSEABLE, 1024, sa,
					       sa_len);
	if (!uep->listen_ev)
		goto err_0;

	evconnlistener_set_error_cb(uep->listen_ev, __z_ugni_listener_err_cb);

	uep->sock = evconnlistener_get_fd(uep->listen_ev);
	return ZAP_ERR_OK;

 err_0:
	z_ugni_close(ep);
	zap_put_ep(ep);
	return zerr;
}

static zap_err_t z_ugni_send(zap_ep_t ep, void *buf, size_t len)
{
	struct z_ugni_ep *uep = (void*)ep;

	if (ep->state != ZAP_EP_CONNECTED)
		return ZAP_ERR_NOT_CONNECTED;

	/* create ebuf for message */
	struct evbuffer *ebuf = evbuffer_new();
	if (!ebuf)
		return ZAP_ERR_RESOURCE;

	struct zap_ugni_msg_regular msg;
	msg.hdr.msg_type = htons(ZAP_UGNI_MSG_REGULAR);
	msg.hdr.msg_len =  htonl((uint32_t)(sizeof(msg) + len));

	msg.data_len = htonl(len);

	if (evbuffer_add(ebuf, &msg, sizeof(msg)) != 0)
		goto err;
	if (evbuffer_add(ebuf, buf, len) != 0)
		goto err;

	/* this write will drain ebuf, appending data to uep->buf_event
	 * without unnecessary memory copying. */
	if (bufferevent_write_buffer(uep->buf_event, ebuf) != 0)
		goto err;

	/* we don't need ebuf anymore */
	evbuffer_free(ebuf);
	return ZAP_ERR_OK;
err:
	evbuffer_free(ebuf);
	return ZAP_ERR_RESOURCE;
}

static struct timeval to;
static struct event *keepalive;
static void timeout_cb(int s, short events, void *arg)
{
	to.tv_sec = 10;
	to.tv_usec = 0;
	evtimer_add(keepalive, &to);
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

static int z_ugni_init()
{
	int rc = 0;
	gni_return_t grc;
	rs_node_t node;
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
		return errno;
	}
	rdsz = read(fd, buff, sizeof(buff) - 1);
	if (rdsz < 0) {
		LOG("version file read error (errno %d): %m\n", errno);
		return errno;
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
		return EINVAL;
	}

	rc = rca_get_nodeid(&node);
	if (rc)
		goto out;

	_dom.inst_id = (node.rs_node_s._node_id << 16) | (uint32_t)getpid();

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
		return EINVAL;
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
		if (_dom.type == ZAP_UGNI_TYPE_GEMINI) {
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

	evthread_use_pthreads();

	rc = z_ugni_init();
	if (rc)
		return rc;

	if (!io_event_loop) {
		io_event_loop = event_base_new();
		if (!io_event_loop)
			return errno;
	}

	if (!keepalive) {
		keepalive = evtimer_new(io_event_loop, timeout_cb, NULL);
		if (!keepalive)
			return errno;
	}

	to.tv_sec = 1;
	to.tv_usec = 0;
	evtimer_add(keepalive, &to);

	rc = pthread_create(&io_thread, NULL, io_thread_proc, 0);
	if (rc)
		return rc;

	init_complete = 1;

	z_key_tree.root = NULL;
	z_key_tree.comparator = z_rbn_cmp;
	pthread_mutex_init(&z_key_tree_mutex, NULL);
	return 0;
}

zap_ep_t z_ugni_new(zap_t z, zap_cb_fn_t cb)
{
	gni_return_t grc;
	struct z_ugni_ep *uep = calloc(1, sizeof(*uep));
	DLOG("Creating ep: %p\n", uep);
	if (!uep) {
		errno = ZAP_ERR_RESOURCE;
		return NULL;
	}
	uep->sock = -1;
	pthread_mutex_lock(&z_ugni_list_mutex);
	LIST_INSERT_HEAD(&z_ugni_list, uep, link);
	pthread_mutex_unlock(&z_ugni_list_mutex);
	grc = GNI_EpCreate(_dom.nic, _dom.cq, &uep->gni_ep);
	if (grc) {
		LOG("GNI_EpCreate() failed: %s\n", gni_ret_str(grc));
		free(uep);
		errno = ZAP_ERR_RESOURCE;
		return NULL;
	}
	DLOG_(uep, "Created gni_ep: %p\n", uep->gni_ep);
	return (zap_ep_t)uep;
}

static void z_ugni_destroy(zap_ep_t ep)
{
	struct z_ugni_ep *uep = (void*)ep;
	gni_return_t grc;
	DLOG_(uep, "destroying endpoint %p\n", uep);
	pthread_mutex_lock(&z_ugni_list_mutex);
	LIST_REMOVE(uep, link);
	pthread_mutex_unlock(&z_ugni_list_mutex);
	release_buf_event(uep);
	if (uep->gni_ep) {
		DLOG_(uep, "Destroying gni_ep: %p\n", uep->gni_ep);
		grc = GNI_EpDestroy(uep->gni_ep);
		if (grc) {
			LOG_(uep, "GNI_EpDestroy() error: %s\n", gni_ret_str(grc));
		}
	}
	if (uep->sock > -1) {
		/* should not reach this. */
		LOG_(uep, "WARNING: %s: closing %d\n", __func__, uep->sock);
		close(uep->sock);
	}
	free(ep);
}

zap_err_t z_ugni_accept(zap_ep_t ep, zap_cb_fn_t cb)
{
	/* ep is the newly created ep from __z_ugni_conn_request */
	struct z_ugni_ep *uep = (void*)ep;
	struct zap_event ev;
	int rc;
	zap_err_t zerr;
	struct zap_ugni_msg_accepted msg;

	pthread_mutex_lock(&uep->ep.lock);
	if (uep->ep.state != ZAP_EP_CONNECTING) {
		zerr = ZAP_ERR_ENDPOINT;
		goto err_0;
	}

	uep->ep.cb = cb;

	msg.hdr.msg_type = ZAP_UGNI_MSG_ACCEPTED;
	msg.hdr.msg_len = sizeof(msg);
	msg.inst_id = _dom.inst_id;
	msg.pe_addr = _dom.pe_addr;
	hton_zap_ugni_msg(&msg.hdr);

	DLOG_(uep, "Sending ZAP_UGNI_MSG_ACCEPTED\n");

	uep->ep.state = ZAP_EP_CONNECTED;
	rc = bufferevent_write(uep->buf_event, &msg, sizeof(msg));
	if (rc) {
		LOG_(uep, "Cannot send ZAP_UGNI_MSG_ACCEPTED to peer.");
		zerr = ZAP_ERR_CONNECT;
		goto err_1;
	}

	pthread_mutex_unlock(&uep->ep.lock);
	zap_get_ep(&uep->ep);
	ev.type = ZAP_EVENT_CONNECTED;
	cb(ep, &ev);
	return ZAP_ERR_OK;

 err_1:
	uep->ep.state = ZAP_EP_ERROR;
	pthread_mutex_unlock(&uep->ep.lock);
 err_0:
	return zerr;
}

static zap_err_t z_ugni_reject(zap_ep_t ep)
{
	zap_close(ep);
	return ZAP_ERR_OK;
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

	grc = GNI_MemRegister(_dom.nic, (uint64_t)buf, len, NULL,
			GNI_MEM_READWRITE | GNI_MEM_RELAXED_PI_ORDERING,
			-1, &map->gni_mh);
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
	GNI_MemDeregister(_dom.nic, &m->gni_mh);
	free(m);
	return ZAP_ERR_OK;
}

static zap_err_t z_ugni_share(zap_ep_t ep, zap_map_t map,
				const char *msg, size_t msg_len)
{

	/* validate */
	if (ep->state != ZAP_EP_CONNECTED)
		return ZAP_ERR_NOT_CONNECTED;

	if (map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;

	/* prepare message */
	struct zap_ugni_map *smap = (void*)map;
	size_t sz = sizeof(struct zap_ugni_msg_rendezvous) + msg_len;
	struct zap_ugni_msg_rendezvous *msgr = malloc(sz);
	if (!msgr)
		return ZAP_ERR_RESOURCE;
	msgr->hdr.msg_type = ZAP_UGNI_MSG_RENDEZVOUS;
	msgr->hdr.msg_len = sz;
	msgr->gni_mh = smap->gni_mh;
	msgr->addr = (uint64_t)map->addr;
	msgr->data_len = map->len;
	if (msg_len)
		memcpy(msgr->msg, msg, msg_len);
	hton_zap_ugni_msg(&msgr->hdr);

	zap_err_t rc = ZAP_ERR_OK;

	/* write message */
	struct z_ugni_ep *uep = (void*) ep;
	if (bufferevent_write(uep->buf_event, msgr, sz) != 0)
		rc = ZAP_ERR_RESOURCE;

	free(msgr);
	return rc;
}

static zap_err_t z_ugni_read(zap_ep_t ep, zap_map_t src_map, void *src,
			     zap_map_t dst_map, void *dst, size_t sz,
			     void *context)
{
	if (((uint64_t)src) & 3)
		return ZAP_ERR_PARAMETER;
	if (((uint64_t)dst) & 3)
		return ZAP_ERR_PARAMETER;
	if (sz & 3)
		return ZAP_ERR_PARAMETER;
	struct z_ugni_ep *uep = (void*)ep;
	struct zap_ugni_map *smap = (void*)src_map;
	struct zap_ugni_map *dmap = (void*)dst_map;
	gni_return_t grc;
	struct zap_ugni_post_desc *desc = __alloc_post_desc(uep);
	if (!desc)
		return ZAP_ERR_RESOURCE;

	desc->post.type = GNI_POST_RDMA_GET;
	desc->post.cq_mode = GNI_CQMODE_GLOBAL_EVENT;
	desc->post.dlvr_mode = GNI_DLVMODE_PERFORMANCE;
	desc->post.local_addr = (uint64_t)dst;
	desc->post.local_mem_hndl = dmap->gni_mh;
	desc->post.remote_addr = (uint64_t)src;
	desc->post.remote_mem_hndl = smap->gni_mh;
	desc->post.length = sz;
	desc->post.post_id = (uint64_t)(unsigned long)desc;
	desc->context = context;
	pthread_mutex_lock(&ugni_lock);
	grc = GNI_PostRdma(uep->gni_ep, &desc->post);
	pthread_mutex_unlock(&ugni_lock);
	if (grc != GNI_RC_SUCCESS) {
		LOG_(uep, "%s: GNI_PostRdma() failed, grc: %s\n",
				__func__, gni_ret_str(grc));
		__free_post_desc(desc);
		return ZAP_ERR_RESOURCE;
	}
	return ZAP_ERR_OK;
}

static zap_err_t z_ugni_write(zap_ep_t ep, zap_map_t src_map, void *src,
			      zap_map_t dst_map, void *dst, size_t sz,
			      void *context)
{
	if (((uint64_t)src) & 3)
		return ZAP_ERR_PARAMETER;
	if (((uint64_t)dst) & 3)
		return ZAP_ERR_PARAMETER;
	if (sz & 3)
		return ZAP_ERR_PARAMETER;
	struct z_ugni_ep *uep = (void*)ep;
	struct zap_ugni_map *smap = (void*)src_map;
	struct zap_ugni_map *dmap = (void*)dst_map;
	gni_return_t grc;
	struct zap_ugni_post_desc *desc = __alloc_post_desc(uep);
	if (!desc)
		return ZAP_ERR_RESOURCE;

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
	pthread_mutex_lock(&ugni_lock);
	grc = GNI_PostRdma(uep->gni_ep, &desc->post);
	pthread_mutex_unlock(&ugni_lock);
	if (grc != GNI_RC_SUCCESS) {
		LOG_(uep, "%s: GNI_PostRdma() failed, grc: %s\n",
				__func__, gni_ret_str(grc));
		__free_post_desc(desc);
		return ZAP_ERR_RESOURCE;
	}
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
