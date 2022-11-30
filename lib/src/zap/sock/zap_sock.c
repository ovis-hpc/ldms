/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2014-2020 Open Grid Computing, Inc. All rights reserved.
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
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include <endian.h>
#include <signal.h>
#include "coll/rbt.h"
#include "ovis_util/os_util.h"

#include "zap_sock.h"

#define LOG__(ep, ...) do { \
	if ((ep) && (ep)->z && (ep)->z->log_fn) \
		(ep)->z->log_fn(__VA_ARGS__); \
} while (0);

#define LOG_(sep, ...) do { \
	if ((sep) && (sep)->ep.z && (sep)->ep.z->log_fn) \
		(sep)->ep.z->log_fn(__VA_ARGS__); \
} while(0);

#define ZLOG_(z, ...) do { \
	if (z->log_fn)\
		z->log_fn(__VA_ARGS__);\
} while (0)

#ifdef DEBUG_ZAP_SOCK
#define DEBUG_LOG(ep, ...) LOG__((zap_ep_t)(ep), __VA_ARGS__)
#define DEBUG_LOG_SEND_MSG(sep, msg) __log_sep_msg(sep, 0, (void*)(msg))
#define DEBUG_LOG_RECV_MSG(sep, msg) __log_sep_msg(sep, 1, (void*)(msg))
#define DEBUG_ZLOG(z, ...) ZLOG_(z, __VA_ARGS__)
#else
#define DEBUG_LOG(ep, ...)
#define DEBUG_LOG_SEND_MSG(sep, msg)
#define DEBUG_LOG_RECV_MSG(sep, msg)
#define DEBUG_ZLOG(z, ...)
#endif

static int init_complete = 0;

static void *io_thread_proc(void *arg);

static void sock_event(struct epoll_event *ev);
static void sock_read(struct epoll_event *ev);
static void sock_write(struct epoll_event *ev);
static void sock_send_complete(struct epoll_event *ev);
static void sock_connect(struct epoll_event *ev);

static int __disable_epoll_out(struct z_sock_ep *sep);
static int __enable_epoll_out(struct z_sock_ep *sep);

static void sock_ev_cb(struct epoll_event *ev);

static zap_err_t __sock_send_msg(struct z_sock_ep *sep, struct sock_msg_hdr *m,
				 size_t msg_size,
				 const char *data, size_t data_len);

static zap_err_t __sock_send_msg_nolock(struct z_sock_ep *sep,
					struct sock_msg_hdr *m,
					size_t msg_size,
					const char *data, size_t data_len);

static int z_sock_buff_init(z_sock_buff_t buff, size_t bytes);
static void z_sock_buff_cleanup(z_sock_buff_t buff);
static void z_sock_buff_reset(z_sock_buff_t buff);
static int z_sock_buff_extend(z_sock_buff_t buff, size_t new_sz);

static void z_sock_hdr_init(struct sock_msg_hdr *hdr, uint32_t xid,
			    uint16_t type, uint32_t len, uint64_t ctxt);

static uint32_t z_last_key = 1;
static struct rbt z_key_tree;
static pthread_mutex_t z_key_tree_mutex;

static LIST_HEAD(, z_sock_ep) z_sock_list = LIST_HEAD_INITIALIZER(0);
static pthread_mutex_t z_sock_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static int z_rbn_cmp(void *a, const void *b)
{
	uint32_t x = (uint32_t)(uint64_t)a;
	uint32_t y = (uint32_t)(uint64_t)b;
	return x - y;
}

/**
 * Allocate key for a \c map.
 *
 * This function uses \c map->mr[ZAP_SOCK] to determine if the key has already
 * been allocated for the \c map. Otherwise, it modifies \c map->mr[ZAP_SOCK] to
 * store the newly allocated key.
 *
 * \param map The map.
 *
 * \returns 0 on error.
 * \returns the key for \c map.
 */
static uint32_t z_key_alloc(struct zap_map *map)
{
	struct z_sock_key *key;
	pthread_mutex_lock(&z_key_tree_mutex);
	/*
	 * multiple threads may compete to allocate key for the map. If the key
	 * has already been allocated by the other thread, just return it.
	 */
	if (SOCK_MAP_KEY_GET(map)) {
		pthread_mutex_unlock(&z_key_tree_mutex);
		return SOCK_MAP_KEY_GET(map);
	}
	key = calloc(1, sizeof(*key));
	if (!key) {
		pthread_mutex_unlock(&z_key_tree_mutex);
		return 0;
	}
	key->map = map;
	key->rb_node.key = (void*)(uint64_t)(++z_last_key);
	if (!key->rb_node.key) /* overflow, get next key */
		key->rb_node.key = (void*)(uint64_t)(++z_last_key);
	rbt_ins(&z_key_tree, &key->rb_node);
	SOCK_MAP_KEY_SET(map, key->rb_node.key);
	pthread_mutex_unlock(&z_key_tree_mutex);
	return SOCK_MAP_KEY_GET(map);
}

/* Caller must hold the z_key_tree_mutex lock. */
static struct z_sock_key *z_sock_key_find(uint32_t key)
{
	struct rbn *krbn = rbt_find(&z_key_tree, (void*)(uint64_t)key);
	if (!krbn)
		return NULL;
	return container_of(krbn, struct z_sock_key, rb_node);
}

static void z_sock_key_delete(uint32_t key)
{
	struct z_sock_key *k;
	pthread_mutex_lock(&z_key_tree_mutex);
	k = z_sock_key_find(key);
	if (!k)
		goto out;
	rbt_del(&z_key_tree, &k->rb_node);
	free(k);
out:
	pthread_mutex_unlock(&z_key_tree_mutex);
}

/**
 * Validate access by map key.
 *
 * \param key The map key.
 * \param p The start of the accessing memory.
 * \param sz The size of the accessing memory.
 * \param acc Access flags.
 *
 * The Caller must hold the z_key_tree_mutex lock.
 */
static int z_sock_map_key_access_validate(uint32_t key, char *p, size_t sz,
				zap_access_t acc)
{
	struct z_sock_key *k = z_sock_key_find(key);
	if (!k)
		return ENOENT;
	return z_map_access_validate((zap_map_t)k->map, p, sz, acc);
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

static int z_sock_buff_init(z_sock_buff_t buff, size_t bytes)
{
	buff->data = malloc(bytes);
	if (!buff->data)
		return errno;
	buff->len = 0;
	buff->alen = bytes;
	return 0;
}

static void z_sock_buff_cleanup(z_sock_buff_t buff)
{
	free(buff->data);
	buff->data = NULL;
	buff->len = 0;
	buff->alen = 0;
}

static void z_sock_buff_reset(z_sock_buff_t buff)
{
	buff->alen += buff->len;
	buff->len = 0;
}

static int z_sock_buff_extend(z_sock_buff_t buff, size_t new_sz)
{
	void *newmem = realloc(buff->data, new_sz);
	if (!newmem)
		return errno;
	buff->alen = new_sz - buff->len;
	buff->data = newmem;
	return 0;
}

static zap_err_t z_sock_close(zap_ep_t ep)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	pthread_t self = pthread_self();

	pthread_mutex_lock(&sep->ep.lock);
	if (self != ep->thread->thread) {
		/* If we are NOT in app callback path, we can block-wait sq */
		while (!TAILQ_EMPTY(&sep->sq)) {
			pthread_cond_wait(&sep->sq_cond, &sep->ep.lock);
		}
	}
	switch (sep->ep.state) {
	case ZAP_EP_PEER_CLOSE:
	case ZAP_EP_CONNECTED:
	case ZAP_EP_LISTENING:
		sep->ep.state = ZAP_EP_CLOSE;
		shutdown(sep->sock, SHUT_RDWR);
		break;
	case ZAP_EP_ERROR:
	case ZAP_EP_ACCEPTING:
	case ZAP_EP_CONNECTING:
		shutdown(sep->sock, SHUT_RDWR);
		break;
	case ZAP_EP_CLOSE:
		break;
	default:
		ZAP_ASSERT(0, ep, "%s: Unexpected state '%s'\n",
				__func__, __zap_ep_state_str(ep->state));
		break;
	}
	pthread_mutex_unlock(&sep->ep.lock);
	return ZAP_ERR_OK;
}

static zap_err_t z_get_name(zap_ep_t ep, struct sockaddr *local_sa,
			    struct sockaddr *remote_sa, socklen_t *sa_len)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	int rc;
	*sa_len = sizeof(struct sockaddr_in);
	rc = getsockname(sep->sock, local_sa, sa_len);
	if (rc)
		goto err;
	rc = getpeername(sep->sock, remote_sa, sa_len);
	if (rc)
		goto err;
	return ZAP_ERR_OK;
 err:
	return zap_errno2zerr(errno);
}

static int __set_sock_opts(struct z_sock_ep *sep)
{
	int rc;
	int i;
	size_t sz;

	/* nonblock */
	rc = __sock_nonblock(sep->sock);
	if (rc)
		return rc;

	/* keepalive */
	i = 1;
	rc = setsockopt(sep->sock, SOL_SOCKET, SO_KEEPALIVE, &i,
			sizeof(int));
	if (rc) {
		LOG_(sep, "zap_sock: WARNING: set SO_KEEPALIVE error: %d\n", errno);
		return errno;
	}
	i = ZAP_SOCK_KEEPCNT;
	rc = setsockopt(sep->sock, SOL_TCP, TCP_KEEPCNT, &i, sizeof(int));
	if (rc) {
		LOG_(sep, "zap_sock: WARNING: set TCP_KEEPCNT error: %d\n", errno);
		return errno;
	}
	i = ZAP_SOCK_KEEPIDLE;
	rc = setsockopt(sep->sock, SOL_TCP, TCP_KEEPIDLE, &i, sizeof(int));
	if (rc) {
		LOG_(sep, "zap_sock: WARNING: set TCP_KEEPIDLE error: %d\n", errno);
		return errno;
	}
	i = ZAP_SOCK_KEEPINTVL;
	rc = setsockopt(sep->sock, SOL_TCP, TCP_KEEPINTVL, &i, sizeof(int));
	if (rc) {
		LOG_(sep, "zap_sock: WARNING: set TCP_KEEPINTVL error: %d\n", errno);
		return errno;
	}

	/* send/recv bufsiz */
	sz = SOCKBUF_SZ;
	rc = setsockopt(sep->sock, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
	if (rc) {
		LOG_(sep, "zap_sock: WARNING: set SO_SNDBUF error: %d\n", errno);
		return errno;
	}
	sz = SOCKBUF_SZ;
	rc = setsockopt(sep->sock, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
	if (rc) {
		LOG_(sep, "zap_sock: WARNING: set SO_RCVBUF error: %d\n", errno);
		return errno;
	}
	return 0;
}

static zap_err_t z_sock_connect(zap_ep_t ep,
				struct sockaddr *sa, socklen_t sa_len,
				char *data, size_t data_len)
{
	int rc;
	zap_err_t zerr;
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	zerr = zap_ep_change_state(&sep->ep, ZAP_EP_INIT, ZAP_EP_CONNECTING);
	if (zerr)
		goto err1;

	sep->sock = socket(sa->sa_family, SOCK_STREAM, 0);
	if (sep->sock == -1) {
		zerr = ZAP_ERR_RESOURCE;
		goto err1;
	}

	rc = __set_sock_opts(sep);
	if (rc)  {
		zerr = ZAP_ERR_TRANSPORT;
		goto err1;
	}

	if (data_len) {
		sep->conn_data = malloc(data_len);
		if (sep->conn_data)
			memcpy(sep->conn_data, data, data_len);
		else {
			zerr = ZAP_ERR_RESOURCE;
			goto err1;
		}
		sep->conn_data_len = data_len;
	}

	rc = connect(sep->sock, sa, sa_len);
	if (rc && errno != EINPROGRESS) {
		zerr = ZAP_ERR_RESOURCE;
		goto err2;
	}

	ref_get(&sep->ep.ref, "accept/connect");

	sep->ev_fn = sock_ev_cb;
	sep->ev.data.ptr = sep;
	sep->ev.events = EPOLLIN|EPOLLOUT;

	zerr = zap_io_thread_ep_assign(&sep->ep);
	if (zerr)
		goto err3;
	return ZAP_ERR_OK;

 err3:
	ref_put(&sep->ep.ref, "accept/connect");
 err2:
	free(sep->conn_data);
	sep->conn_data = NULL;
 err1:
	if (sep->sock >= 0) {
		close(sep->sock);
		sep->sock = -1;
	}
	return zerr;
}

/**
 * Process an unknown message in the end point.
 */
static void process_sep_read_error(struct z_sock_ep *sep)
{
	pthread_mutex_lock(&sep->ep.lock);
	if (sep->ep.state == ZAP_EP_CONNECTED)
		sep->ep.state = ZAP_EP_CLOSE;
	shutdown(sep->sock, SHUT_RDWR);
	pthread_mutex_unlock(&sep->ep.lock);
}

/**
 * Process connect message.
 */
static void process_sep_msg_connect(struct z_sock_ep *sep)
{
	struct sock_msg_connect *msg;

	msg = sep->buff.data;

	if (!zap_version_check(&msg->ver)) {
		LOG_(sep, "Connection request from an unsupported Zap version "
				"%hhu.%hhu.%hhu.%hhu\n",
				msg->ver.major, msg->ver.minor,
				msg->ver.patch, msg->ver.flags);
		shutdown(sep->sock, SHUT_RDWR);
		return;
	}

	if (memcmp(msg->sig, ZAP_SOCK_SIG, sizeof(msg->sig))) {
		LOG_(sep, "Expecting sig '%s', but got '%.*s'.\n",
				ZAP_SOCK_SIG, sizeof(msg->sig), msg->sig);
		shutdown(sep->sock, SHUT_RDWR);
		return;
	}

	struct zap_event ev = {
		.type = ZAP_EVENT_CONNECT_REQUEST,
		.data = (void*)msg->data,
		.data_len = ntohl(msg->data_len),
	};

	sep->ep.cb(&sep->ep, &ev);

	return;
}

static zap_err_t __sock_send(struct z_sock_ep *sep, uint16_t msg_type,
						char *buf, size_t len);
/**
 * Process accept msg
 */
static void process_sep_msg_accepted(struct z_sock_ep *sep)
{
	struct sock_msg_sendrecv *msg;
	struct zap_event ev;
	zap_err_t zerr;

	pthread_mutex_lock(&sep->ep.lock);
	zerr = __sock_send(sep, SOCK_MSG_ACK_ACCEPTED, NULL, 0);
	pthread_mutex_unlock(&sep->ep.lock);
	if (zerr)
		goto err;

	msg = sep->buff.data;

	ev.type = ZAP_EVENT_CONNECTED;
	ev.status = ZAP_ERR_OK;
	ev.data = (void*)msg->data;
	ev.data_len = ntohl(msg->data_len);

	zerr = zap_ep_change_state(&sep->ep, ZAP_EP_CONNECTING, ZAP_EP_CONNECTED);
	if (zerr != ZAP_ERR_OK) {
		LOG_(sep, "'Accept' message received in unexpected state %d.\n",
				sep->ep.state);
		goto err;
	}
	sep->ep.cb((void*)sep, &ev);
	return;
err:
	shutdown(sep->sock, SHUT_RDWR);
}

/**
 * Process reject msg
 */
static void process_sep_msg_rejected(struct z_sock_ep *sep)
{
	zap_err_t zerr;
	struct sock_msg_sendrecv *msg;
	struct zap_event ev;

	msg = sep->buff.data;

	ev.type = ZAP_EVENT_REJECTED;
	ev.status = ZAP_ERR_OK;
	ev.data = (void*)msg->data;
	ev.data_len = ntohl(msg->data_len);

	zerr = zap_ep_change_state(&sep->ep, ZAP_EP_CONNECTING, ZAP_EP_ERROR);
	if (zerr != ZAP_ERR_OK) {
		LOG_(sep, "'reject' message received in unexpected state %d.\n",
				sep->ep.state);
		return;
	}

	sep->ep.cb((void*)sep, &ev);
	shutdown(sep->sock, SHUT_RDWR);
	return;
}

/*
 * Process the acknowledge message sent by the active side
 */

static void process_sep_msg_ack_accepted(struct z_sock_ep *sep)
{
	zap_err_t zerr;

	zerr = zap_ep_change_state(&sep->ep, ZAP_EP_ACCEPTING, ZAP_EP_CONNECTED);
	if (zerr != ZAP_ERR_OK) {
		LOG_(sep, "'Acknowledged' message received in unexpected state %d.\n",
				sep->ep.state);
		shutdown(sep->sock, SHUT_RDWR);
		return;
	}
	struct zap_event ev = {
		.type = ZAP_EVENT_CONNECTED,
		.status = ZAP_ERR_OK,
	};
	sep->ep.cb(&sep->ep, &ev);
}

/**
 * Process send/recv message.
 */
static void process_sep_msg_sendrecv(struct z_sock_ep *sep)
{
	struct sock_msg_sendrecv *msg;
	struct zap_event ev = {
			.type = ZAP_EVENT_RECV_COMPLETE,
	};

	msg = sep->buff.data;

	ev.status = ZAP_ERR_OK;
	ev.data = (unsigned char *)msg->data;
	ev.data_len = ntohl(msg->data_len);

	sep->ep.cb(&sep->ep, &ev);
}

/**
 * Receiving a read request message.
 */
static void process_sep_msg_read_req(struct z_sock_ep *sep)
{
	/* unpack received message */
	struct sock_msg_read_req *msg;
	uint32_t data_len;
	char *src;
	struct sock_msg_read_resp rmsg;
	memset(&(rmsg), 0, sizeof(rmsg));

	msg = sep->buff.data;

	/* Need to swap locally interpreted values */
	data_len = ntohl(msg->data_len);
	src = (char *)be64toh(msg->src_ptr);

	int rc = 0;
	pthread_mutex_lock(&z_key_tree_mutex);
	rc = z_sock_map_key_access_validate(msg->src_map_key, src, data_len,
				       ZAP_ACCESS_READ);
	pthread_mutex_unlock(&z_key_tree_mutex);
	/*
	 * The data the other side receives could be garbage
	 * if the map is deleted after this point.
	 */
	switch (rc) {
	case 0:	/* OK */
		rmsg.status = 0;
		break;
	case EACCES:
		rmsg.status = htons(ZAP_ERR_REMOTE_PERMISSION);
		break;
	case ERANGE:
		rmsg.status = htons(ZAP_ERR_REMOTE_LEN);
		break;
	case ENOENT:
		rmsg.status = htons(ZAP_ERR_REMOTE_MAP);
		break;
	default:
		rmsg.status = htons(ZAP_ERR_PARAMETER);
		break;
	}
	if (rc)
		rmsg.data_len = data_len = 0;
	else
		rmsg.data_len = msg->data_len; /* Still in BE */

	z_sock_hdr_init(&rmsg.hdr, msg->hdr.xid, SOCK_MSG_READ_RESP, sizeof(rmsg) + data_len, msg->hdr.ctxt);
	if (__sock_send_msg(sep, &rmsg.hdr, sizeof(rmsg), src, data_len))
		shutdown(sep->sock, SHUT_RDWR);
}

struct z_sock_send_wr_s *__sock_wr_alloc(size_t data_len, struct z_sock_io *io)
{
	struct z_sock_send_wr_s *wr;
	wr = calloc(1, sizeof(*wr) + data_len);
	if (!wr)
		return NULL;
	wr->io = io;
	return wr;
}

void __sock_wr_free(struct z_sock_send_wr_s *wr)
{
	free(wr);
}

/* caller must hold sep->ep.lock */
static inline
struct z_sock_io *__sock_io_alloc(struct z_sock_ep *sep)
{
	return calloc(1, sizeof(struct z_sock_io));
}

/* caller must hold sep->ep.lock */
static inline
void __sock_io_free(struct z_sock_ep *sep, struct z_sock_io *io)
{
	free(io);
}

/**
 * Receiving a read response message.
 */
static void process_sep_msg_read_resp(struct z_sock_ep *sep)
{
	struct z_sock_io *io;
	struct sock_msg_read_resp *msg;
	uint32_t data_len;
	int rc;

	msg = sep->buff.data;

	/* Get the matching request from the io_q */
	pthread_mutex_lock(&sep->ep.lock);
	io = TAILQ_FIRST(&sep->io_q);
	ZAP_ASSERT(io, (&sep->ep), "%s: The io_q is empty.\n", __func__);
	ZAP_ASSERT(msg->hdr.xid == io->xid, (&sep->ep),
			"%s: The transaction IDs mismatched between the "
			"IO entry %d and message %d.\n", __func__,
			io->xid, msg->hdr.xid);
	TAILQ_REMOVE(&sep->io_q, io, q_link);

	data_len = ntohl(msg->data_len);

	if (msg->status == 0) {
		/* Read the data into the local memory after
		 * validating the map. We only need validate base and
		 * bounds because this is local access which is always
		 * allowed. */
		rc = z_map_access_validate(io->dst_map, io->dst_ptr,
					   data_len, 0);
		switch (rc) {
		case 0:
			memcpy(io->dst_ptr, msg->data, data_len);
			break;
		case EACCES:
			rc = ZAP_ERR_LOCAL_PERMISSION;
			break;
		case ERANGE:
			rc = ZAP_ERR_LOCAL_LEN;
			break;
		}
	} else {
		rc = ntohs(msg->status);
	}
	assert( io->ctxt == (void*)msg->hdr.ctxt );
	__sock_io_free(sep, io);
	pthread_mutex_unlock(&sep->ep.lock);

	struct zap_event ev = {
		.type = ZAP_EVENT_READ_COMPLETE,
		.status = rc,
		.context = (void*) msg->hdr.ctxt
	};
	sep->ep.cb((void*)sep, &ev);
}

static uint32_t g_xid = 0;
static void
z_sock_hdr_init(struct sock_msg_hdr *hdr, uint32_t xid,
	   uint16_t type, uint32_t len, uint64_t ctxt)
{
	if (!xid)
		hdr->xid = __sync_add_and_fetch(&g_xid, 1);
	else
		hdr->xid = xid;
	hdr->reserved = 0;
	hdr->msg_type = htons(type);
	hdr->msg_len = htonl(len);
	hdr->ctxt = ctxt;
}

/**
 * Receiving a write request message.
 */
static void process_sep_msg_write_req(struct z_sock_ep *sep)
{
	char *dst;
	uint32_t data_len;
	struct sock_msg_write_req *msg;
	zap_err_t zerr;

	msg = sep->buff.data;

	dst = (void *)(unsigned long)be64toh(msg->dst_ptr);
	data_len = ntohl(msg->data_len);

	/* Prepare the response message */
	struct sock_msg_write_resp rmsg;
	z_sock_hdr_init(&rmsg.hdr, msg->hdr.xid, SOCK_MSG_WRITE_RESP,
			sizeof(rmsg), msg->hdr.ctxt);

	/* Validate */
	pthread_mutex_lock(&z_key_tree_mutex);
	int rc = z_sock_map_key_access_validate(msg->dst_map_key, dst, data_len,
					     ZAP_ACCESS_WRITE);
	pthread_mutex_unlock(&z_key_tree_mutex);

	switch (rc) {
	case 0:
		/* Write into the destination address */
		memcpy(dst, msg->data, data_len);
		rmsg.status = htons(ZAP_ERR_OK);
		break;
	case EACCES:
		rmsg.status = htons(ZAP_ERR_REMOTE_PERMISSION);
		break;
	case ERANGE:
		rmsg.status = htons(ZAP_ERR_REMOTE_LEN);
		break;
	case ENOENT:
		rmsg.status = htons(ZAP_ERR_REMOTE_MAP);
		break;
	}

	zerr = __sock_send_msg(sep, &rmsg.hdr, sizeof(rmsg), NULL, 0);
	if (zerr != ZAP_ERR_OK) {
		shutdown(sep->sock, SHUT_RDWR);
	}
}

/**
 * Receiving a write response message.
 */
static void process_sep_msg_write_resp(struct z_sock_ep *sep)
{
	struct z_sock_io *io;
	struct sock_msg_write_resp *msg;

	msg = sep->buff.data;

	/* Our request should be on the head of the ep->io_q queue. */
	pthread_mutex_lock(&sep->ep.lock);
	/* Take it off the I/O q */
	io = TAILQ_FIRST(&sep->io_q);
	ZAP_ASSERT(io, &sep->ep, "%s: The io_q is empty\n", __func__);
	TAILQ_REMOVE(&sep->io_q, io, q_link);
	ZAP_ASSERT(io->xid == msg->hdr.xid, &sep->ep,
			"%s: The transaction IDs mismatched "
			"between the IO entry %d and message %d.\n",
			__func__, io->xid, msg->hdr.xid);
	assert( io->ctxt == (void*)msg->hdr.ctxt );
	/* Put it back on the free_q */
	__sock_io_free(sep, io);
	pthread_mutex_unlock(&sep->ep.lock);

	struct zap_event ev = {
		.type = ZAP_EVENT_WRITE_COMPLETE,
		.status = ntohs(msg->status),
		.context = (void*) msg->hdr.ctxt
	};
	sep->ep.cb(&sep->ep, &ev);
}

/**
 * Receiving a rendezvous (share) message.
 */
static void process_sep_msg_rendezvous(struct z_sock_ep *sep)
{
	struct sock_msg_rendezvous *msg;
	zap_err_t zerr;
	struct zap_map *map;

	msg = sep->buff.data;

	zerr = zap_map(&map, (void *)(uint64_t)be64toh((uint64_t)msg->addr),
					ntohl(msg->data_len), ntohl(msg->acc));
	if (zerr) {
		LOG_(sep, "%s:%d: Failed to create a map in %s (%s)\n",
			__FILE__, __LINE__, __func__, __zap_err_str[zerr]);
		goto err0;
	}
	map->type = ZAP_MAP_REMOTE;
	ref_get(&sep->ep.ref, "zap_map/rendezvous");
	map->ep = &sep->ep;
	SOCK_MAP_KEY_SET(map, msg->rmap_key);

	char *amsg = NULL;
	size_t amsg_len = ntohl(msg->hdr.msg_len) - sizeof(*msg);
	if (amsg_len)
		amsg = msg->msg;

	struct zap_event ev = {
		.type = ZAP_EVENT_RENDEZVOUS,
		.map = (void*)map,
		.data_len = amsg_len,
		.data = (void*)amsg
	};

	sep->ep.cb((void*)sep, &ev);
	return;

err0:
	return;
}

static int __recv_msg(struct z_sock_ep *sep)
{
	int rc;
	ssize_t rsz, rqsz;
	struct sock_msg_hdr *hdr;
	z_sock_buff_t buff = &sep->buff;
	uint32_t mlen;
	int mtype;
	int from_line = 0; /* for debugging */

	if (buff->len < sizeof(struct sock_msg_hdr)) {
		/* need to fill the header first */
		rqsz = sizeof(struct sock_msg_hdr) - buff->len;
		rsz = read(sep->sock, buff->data + buff->len, rqsz);
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

	hdr = buff->data;
	mlen = ntohl(hdr->msg_len);
	mtype = ntohs(hdr->msg_type);

	if (mtype == SOCK_MSG_WRITE_REQ || mtype == SOCK_MSG_READ_RESP) {
		/* allow big message */
	} else {
		if (mlen > SOCKBUF_SZ) {
			DEBUG_LOG(sep, "ep: %p, RECV invalid message length: %ld\n",
				  sep, mlen);
			rc = EINVAL;
			from_line = __LINE__;
			goto err;
		}
	}

	if (mlen > buff->len + buff->alen) {
		/* Buffer extension is needed */
		rqsz = ((mlen - 1) | 0xFFFF) + 1;
		rc = z_sock_buff_extend(buff, rqsz);
		if (rc) {
			from_line = __LINE__;
			goto err;
		}
	}

	if (buff->len < mlen) {
		rqsz = mlen - buff->len;
		rsz = read(sep->sock, buff->data + buff->len, rqsz);
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
	from_line += 0; /* Avoid gcc's set-but-not-used warning */
	return rc;
}

/* For debugging */
void __log_sep_msg(struct z_sock_ep *sep, int is_recv,
			  const struct sock_msg_hdr *hdr)
{
	char _buff[128];
	const char *lbl;
	enum sock_msg_type mtype;
	sock_msg_t msg;
	if (is_recv)
		snprintf(_buff, sizeof(_buff), "ZAP_SOCK DEBUG: RECV "
					       "ep: %p, msg", sep);
	else
		snprintf(_buff, sizeof(_buff), "ZAP_SOCK DEBUG: SEND "
					       "ep: %p, msg", sep);
	lbl = _buff;
	msg = (void*)hdr;
	mtype = ntohs(hdr->msg_type);
	switch (mtype) {
	case SOCK_MSG_CONNECT:
		LOG_(sep, "%s: %s, len: %u, xid: %#x, ctxt: %#lx, "
			"zap_ver: %hhu.%hhu.%hhu.%hhu, sig: %8s, data_len: %d"
			"\n",
			lbl,
			sock_msg_type_str(mtype),
			ntohl(hdr->msg_len),
			hdr->xid,
			hdr->ctxt,
			msg->connect.ver.major,
			msg->connect.ver.minor,
			msg->connect.ver.patch,
			msg->connect.ver.flags,
			msg->connect.sig,
			ntohl(msg->connect.data_len)
		    );
		break;
	case SOCK_MSG_SENDRECV:
	case SOCK_MSG_ACCEPTED:
	case SOCK_MSG_REJECTED:
		LOG_(sep, "%s: %s, len: %u, xid: %#x, ctxt: %#lx, "
			"data_len: %d"
			"\n",
			lbl,
			sock_msg_type_str(mtype),
			ntohl(hdr->msg_len),
			hdr->xid,
			hdr->ctxt,
			ntohl(msg->sendrecv.data_len)
		    );
		break;
	case SOCK_MSG_RENDEZVOUS:
		LOG_(sep, "%s: %s, len: %u, xid: %#x, ctxt: %#lx, "
			"rmap_key: %#x, acc: %#x, addr: %#lx, data_len: %d"
			"\n",
			lbl,
			sock_msg_type_str(mtype),
			ntohl(hdr->msg_len),
			hdr->xid,
			hdr->ctxt,
			ntohl(msg->rendezvous.rmap_key),
			ntohl(msg->rendezvous.acc),
			be64toh(msg->rendezvous.addr),
			ntohl(msg->rendezvous.data_len)
		    );
		break;
	case SOCK_MSG_READ_REQ:
		LOG_(sep, "%s: %s, len: %u, xid: %#x, ctxt: %#lx, "
			"src_map_key: %#x, src_ptr: %#lx, data_len: %d"
			"\n",
			lbl,
			sock_msg_type_str(mtype),
			ntohl(hdr->msg_len),
			hdr->xid,
			hdr->ctxt,
			msg->read_req.src_map_key,
			be64toh(msg->read_req.src_ptr),
			ntohl(msg->read_req.data_len)
		    );
		break;
	case SOCK_MSG_READ_RESP:
		LOG_(sep, "%s: %s, len: %u, xid: %#x, ctxt: %#lx, "
			"status: %hd, data_len: %d"
			"\n",
			lbl,
			sock_msg_type_str(mtype),
			ntohl(hdr->msg_len),
			hdr->xid,
			hdr->ctxt,
			ntohs(msg->read_resp.status),
			ntohl(msg->read_resp.data_len)
		    );
		break;
	case SOCK_MSG_WRITE_REQ:
		LOG_(sep, "%s: %s, len: %u, xid: %#x, ctxt: %#lx, "
			"dst_map_key: %#x, dst_ptr: %#lx, data_len: %d"
			"\n",
			lbl,
			sock_msg_type_str(mtype),
			ntohl(hdr->msg_len),
			hdr->xid,
			hdr->ctxt,
			msg->write_req.dst_map_key,
			be64toh(msg->write_req.dst_ptr),
			ntohl(msg->write_req.data_len)
		    );
		break;
	case SOCK_MSG_WRITE_RESP:
		LOG_(sep, "%s: %s, len: %u, xid: %#x, ctxt: %#lx, "
			"status: %hd"
			"\n",
			lbl,
			sock_msg_type_str(mtype),
			ntohl(hdr->msg_len),
			hdr->xid,
			hdr->ctxt,
			ntohs(msg->write_resp.status)
		    );
		break;
	default:
		LOG_(sep, "%s: BAD TYPE %d\n", lbl, mtype);
		break;
	}
}

typedef void(*process_sep_msg_fn_t)(struct z_sock_ep*);
static process_sep_msg_fn_t process_sep_msg_fns[SOCK_MSG_TYPE_LAST] = {
	[SOCK_MSG_SENDRECV] = process_sep_msg_sendrecv,
	[SOCK_MSG_READ_REQ] = process_sep_msg_read_req,
	[SOCK_MSG_READ_RESP] = process_sep_msg_read_resp,
	[SOCK_MSG_WRITE_REQ] = process_sep_msg_write_req,
	[SOCK_MSG_WRITE_RESP] = process_sep_msg_write_resp,
	[SOCK_MSG_RENDEZVOUS] = process_sep_msg_rendezvous,
	[SOCK_MSG_CONNECT] = process_sep_msg_connect,
	[SOCK_MSG_ACCEPTED] = process_sep_msg_accepted,
	[SOCK_MSG_REJECTED] = process_sep_msg_rejected,
	[SOCK_MSG_ACK_ACCEPTED] = process_sep_msg_ack_accepted,
};

static zap_err_t __sock_send_connect(struct z_sock_ep *sep, char *buf, size_t len);

/*
 * This is the callback function for connecting/connected endpoints.
 *
 * This function queues events to the interpose event queues. The
 * priority of events queued is write, read, disconnect. This order is
 * important to avoid queuing a disconnect prior to the last
 * send/recv.
 */
static void sock_ev_cb(struct epoll_event *ev)
{
	struct z_sock_ep *sep = ev->data.ptr;

	ref_get(&sep->ep.ref, "zap_sock:sock_ev_cb");
	DEBUG_LOG(sep, "ep: %p, sock_ev_cb(), ev:%04x -- BEGIN --\n", sep, ev->events);
	DEBUG_LOG(sep, "ep: %p, state: %s\n", sep, __zap_ep_state_str(sep->ep.state));

	/* Handle write */
	if (ev->events & EPOLLOUT) {
		pthread_mutex_lock(&sep->ep.lock);
		if (sep->sock_connected) {
			sock_send_complete(ev);
			sock_write(ev);
			pthread_mutex_unlock(&sep->ep.lock);
		} else if (sep->ep.state == ZAP_EP_CONNECTING) {
			sep->sock_connected = 1;
			pthread_mutex_unlock(&sep->ep.lock);
			sock_connect(ev);
		} else {
			pthread_mutex_unlock(&sep->ep.lock);
			assert(0 == "BAD STATE");
		}
	}

	/* Handle read */
	if (ev->events & EPOLLIN) {
		sock_read(ev);
	}

	/* Handle disconnect
	 *
	 * This must be last to avoid disconnecting before
	 * sending/receiving all data
	 */
	if (ev->events & (EPOLLERR|EPOLLHUP)) {
		int err;
		socklen_t err_len = sizeof(err);
		getsockopt(sep->sock, SOL_SOCKET, SO_ERROR, &err, &err_len);
		DEBUG_LOG(sep, "ep: %p, sock_ev_cb() events %04x err %d\n",
			  sep, ev->events, err);
		sock_event(ev);
		goto out;
	}
 out:
	DEBUG_LOG(sep, "ep: %p, state: %s\n", sep, __zap_ep_state_str(sep->ep.state));
	DEBUG_LOG(sep, "ep: %p, sock_ev_cb() -- END --\n", sep);
	ref_put(&sep->ep.ref, "zap_sock:sock_ev_cb");
}

static void sock_connect(struct epoll_event *ev)
{
	/* sock connect routine on the initiator side */
	struct z_sock_ep *sep = ev->data.ptr;
	zap_err_t zerr;

	zerr = __sock_send_connect(sep, sep->conn_data, sep->conn_data_len);
	if (sep->conn_data)
		free(sep->conn_data);
	sep->conn_data = NULL;
	sep->conn_data_len = 0;
	if (zerr) {
		shutdown(sep->sock, SHUT_RDWR);
	}

	return;
}

/* process sock send completion, must hold sep->ep.lock */
static void sock_send_complete(struct epoll_event *ev)
{
	struct z_sock_ep *sep = ev->data.ptr;
	struct zap_event zev = {
		.status = ZAP_ERR_OK,
	};
	struct z_sock_io *io;
	while (( io = TAILQ_FIRST(&sep->io_cq) )) {
		TAILQ_REMOVE(&sep->io_cq, io, q_link);
		zev.context = io->ctxt;
		zev.type = io->comp_type;
		__sock_io_free(sep, io);
		pthread_mutex_unlock(&sep->ep.lock);
		sep->ep.cb(&sep->ep, &zev);
		pthread_mutex_lock(&sep->ep.lock);
	}
}

/* sep->ep.lock is held */
static void sock_write(struct epoll_event *ev)
{
	struct z_sock_ep *sep = ev->data.ptr;
	ssize_t wsz;
	z_sock_send_wr_t wr;

 next:
	wr = TAILQ_FIRST(&sep->sq);
	if (!wr) {
		if (TAILQ_EMPTY(&sep->io_cq)) /* also no completion, disable epoll out */
			__disable_epoll_out(sep);
		else /* has completion to process by the io thread */
			__enable_epoll_out(sep);
		pthread_cond_signal(&sep->sq_cond);
		goto out;
	}

	/* msg */
	while (wr->msg_len) {
		wsz = send(sep->sock, wr->msg.bytes + wr->off, wr->msg_len, MSG_NOSIGNAL);
		if (wsz < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				__enable_epoll_out(sep);
				goto out;
			}
			/* otherwise, bad error */
			goto err;
		}
		DEBUG_LOG(sep, "ep: %p, wrote %ld bytes\n", sep, wsz);
		wr->msg_len -= wsz;
		if (!wr->msg_len)
			wr->off = 0; /* reset off for data */
		else
			wr->off += wsz;
	}

	/* data, wr->msg_len is already 0 */
	assert(wr->msg_len == 0);
	while (wr->data_len) {
		wsz = send(sep->sock, wr->data + wr->off, wr->data_len, MSG_NOSIGNAL);
		if (wsz < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				__enable_epoll_out(sep);
				goto out;
			}
			/* otherwise bad error */
			goto err;
		}
		DEBUG_LOG(sep, "ep: %p, wrote %ld bytes\n", sep, wsz);
		wr->data_len -= wsz;
		wr->off += wsz;
	}

	/* reaching here means wr->data_len and wr->msg_len are 0 */
	assert(0 == wr->data_len);
	assert(0 == wr->msg_len);
	TAILQ_REMOVE(&sep->sq, wr, link);
	if (sep->ep.thread)
		__atomic_fetch_sub(&sep->ep.thread->stat->sq_sz, 1, __ATOMIC_SEQ_CST);
	__atomic_fetch_sub(&sep->ep.sq_sz, 1, __ATOMIC_SEQ_CST);
	if (wr->flags & Z_SOCK_WR_COMPLETION) {
		/* right now we have only SEND_COMPLETE delivering by WR */
		assert(ntohs(wr->msg.hdr.msg_type) == SOCK_MSG_SENDRECV);
		TAILQ_REMOVE(&sep->io_q, wr->io, q_link);
		TAILQ_INSERT_TAIL(&sep->io_cq, wr->io, q_link);
	}
	if (wr->io) {
		/* record xid */
		wr->io->xid = wr->msg.hdr.xid;
		wr->io->wr = NULL;
	}
	__sock_wr_free(wr);
	goto next;

 out:
	return;

 err:
	shutdown(sep->sock, SHUT_RDWR);
}

#define min_t(t, x, y) (t)((t)x < (t)y?(t)x:(t)y)
static void sock_read(struct epoll_event *ev)
{
	struct z_sock_ep *sep = ev->data.ptr;
	struct sock_msg_hdr *hdr;
	enum sock_msg_type msg_type;
	struct zap_version ver;
	int rc;
	do {
		rc = __recv_msg(sep);
		if (rc == EAGAIN)
			break;
		if (rc) {
			/* ENOTCONN or other errors */
			goto bad;
		}

		/* message receive complete */
		hdr = sep->buff.data;
		msg_type = ntohs(hdr->msg_type);

		/* validate by ep state */
		switch (sep->ep.state) {
		case ZAP_EP_ACCEPTING:
			/* expecting `connect` or `ack_accepted` message */
			if (msg_type != SOCK_MSG_CONNECT &&
					msg_type != SOCK_MSG_ACK_ACCEPTED) {
				/* invalid */
				goto protocol_error;
			}
			break;
		case ZAP_EP_CONNECTING:
			/* Expecting accept or reject */
			if (msg_type != SOCK_MSG_ACCEPTED &&
					msg_type != SOCK_MSG_REJECTED) {
				/* invalid */
				goto protocol_error;
			}
			break;
		case ZAP_EP_ERROR:
			ZAP_ASSERT(0, &(sep->ep), "%s bad ep state (ZAP_EP_ERROR)", __func__);
			goto protocol_error;
		case ZAP_EP_CONNECTED:
			/* good */
			break;
		case ZAP_EP_CLOSE:
			/*
			 * We can arrive in this case when
			 * the EPOLLIN events arrived between
			 * the endpoint state moving to ZAP_EP_CLOSE and
			 * the connection shutdown. Thus, we should not call
			 * assert() here.
			 *
			 * The ZAP_EP_CLOSE state does not distinguish
			 * before and after Zap has delivered the DISCONNECTED
			 * event to the application. We do not process
			 * the EPOLLIN events to be on the safe side if
			 * Zap has delivered the DISCONNECTED event already
			 * and we arrived here unexpectedly.
			 */
			return;
		case ZAP_EP_INIT:
			/* No connection. Impossible to reach this. */
			ZAP_ASSERT(0, &(sep->ep), "%s bad ep state (ZAP_EP_INIT)", __func__);
			return;
		case ZAP_EP_LISTENING:
			/* No connection. Impossible to reach this. */
			ZAP_ASSERT(0, &(sep->ep), "%s bad ep state (ZAP_EP_LISTENING)", __func__);
			return;
		case ZAP_EP_PEER_CLOSE:
			/* The connection is gone already. Impossible to reach this. */
			ZAP_ASSERT(0, &(sep->ep), "%s bad ep state (ZAP_EP_PEER_CLOSE)", __func__);
			return;
		}
		/* Then call the process function accordingly */
		DEBUG_LOG_RECV_MSG(sep, sep->buff.data);
		if (msg_type >= SOCK_MSG_FIRST
				&& msg_type < SOCK_MSG_TYPE_LAST) {
			process_sep_msg_fns[msg_type](sep);
		} else {
			process_sep_read_error(sep);
		}
		z_sock_buff_reset(&sep->buff);
	} while (1);
	return;

 protocol_error:
	ZAP_VERSION_SET(ver);
	LOG_(sep, "Protocol error: version = %hhu.%hhu.%hhu.%hhu\n",
			ver.major, ver.minor, ver.patch, ver.flags);
 bad:
	/* shutdown the connection */
	process_sep_read_error(sep);
}

void io_thread_cleanup(void *arg)
{
	z_sock_io_thread_t thr = arg;
	if (thr->efd > -1)
		close(thr->efd);
	zap_io_thread_release(&thr->zap_io_thread);
	free(thr);
}

static void *io_thread_proc(void *arg)
{
	/* Zap thread will not handle any signal */
	z_sock_io_thread_t thr = arg;
	int rc, n, i;
	sigset_t sigset;
	struct z_sock_ep *sep;

	pthread_cleanup_push(io_thread_cleanup, arg);

	sigfillset(&sigset);
	rc = sigprocmask(SIG_SETMASK, &sigset, NULL);
	assert(rc == 0 && "pthread_sigmask error");

	while (1) {
		zap_thrstat_wait_start(thr->zap_io_thread.stat);
		n = epoll_wait(thr->efd, thr->ev, ZAP_SOCK_EV_SIZE, -1);
		zap_thrstat_wait_end(thr->zap_io_thread.stat);
		if (n < 0) {
			if (errno == EINTR)
				continue; /* EINTR is OK */
			break;
		}
		for (i = 0; i < n; i++) {
			sep = thr->ev[i].data.ptr;
			sep->ev_fn(&thr->ev[i]);
		}
	}

	pthread_cleanup_pop(1);
	return NULL;
}

zap_event_type_t ev_type_cvt[SOCK_MSG_TYPE_LAST] = {
	[SOCK_MSG_SENDRECV] = ZAP_EVENT_RECV_COMPLETE,
	[SOCK_MSG_RENDEZVOUS] = ZAP_EVENT_BAD,
	[SOCK_MSG_READ_REQ] = ZAP_EVENT_READ_COMPLETE,
	[SOCK_MSG_READ_RESP] = ZAP_EVENT_BAD,
	[SOCK_MSG_WRITE_REQ] = ZAP_EVENT_WRITE_COMPLETE,
	[SOCK_MSG_WRITE_RESP] = ZAP_EVENT_BAD,
	[SOCK_MSG_ACCEPTED] = ZAP_EVENT_BAD
};

static zap_err_t __sock_send_connect(struct z_sock_ep *sep, char *buf, size_t len)
{
	zap_err_t zerr;
	struct sock_msg_connect msg;
	z_sock_hdr_init(&msg.hdr, 0, SOCK_MSG_CONNECT, (uint32_t)(sizeof(msg) + len), 0);
	msg.data_len = htonl(len);
	ZAP_VERSION_SET(msg.ver);
	memcpy(&msg.sig, ZAP_SOCK_SIG, sizeof(msg.sig));

	zerr = __sock_send_msg(sep, &msg.hdr, sizeof(msg), buf, len);
	if (zerr)
		return zerr;

	return ZAP_ERR_OK;
}

static zap_err_t __sock_send(struct z_sock_ep *sep, uint16_t msg_type,
		char *buf, size_t len)
{
	struct sock_msg_sendrecv msg;

	z_sock_hdr_init(&msg.hdr, 0, msg_type, (uint32_t)(sizeof(msg) + len), 0);
	msg.data_len = htonl(len);

	return __sock_send_msg_nolock(sep, &msg.hdr, sizeof(msg), buf, len);
}

/* caller must have sep->ep.lock held */
static int __enable_epoll_out(struct z_sock_ep *sep)
{
	int rc;
	z_sock_io_thread_t thr = (z_sock_io_thread_t)sep->ep.thread;
	if (sep->ev.events & EPOLLOUT)
		return 0; /* already enabled */
	DEBUG_LOG(sep, "ep: %p, Enabling EPOLLOUT\n", sep);
	sep->ev.events = EPOLLIN|EPOLLOUT;
	rc = epoll_ctl(thr->efd, EPOLL_CTL_MOD, sep->sock, &sep->ev);
	return rc;
}

/* caller must have sep->ep.lock held */
static int __disable_epoll_out(struct z_sock_ep *sep)
{
	int rc;
	z_sock_io_thread_t thr = (z_sock_io_thread_t)sep->ep.thread;
	if ((sep->ev.events & EPOLLOUT) == 0)
		return 0; /* already disabled */
	DEBUG_LOG(sep, "ep: %p, Disabling EPOLLOUT\n", sep);
	sep->ev.events = EPOLLIN;
	rc = epoll_ctl(thr->efd, EPOLL_CTL_MOD, sep->sock, &sep->ev);
	return rc;
}

/*
 * Caller must acquire `sep->ep.lock` before calling this function.
 */
static void __wr_post(struct z_sock_ep *sep, z_sock_send_wr_t wr)
{
	struct epoll_event ev = { .events = EPOLLOUT, .data.ptr = sep };
	TAILQ_INSERT_TAIL(&sep->sq, wr, link);
	__atomic_fetch_add(&sep->ep.sq_sz, 1, __ATOMIC_SEQ_CST);
	if (sep->ep.thread)
		__atomic_fetch_add(&sep->ep.thread->stat->sq_sz, 1, __ATOMIC_SEQ_CST);
	sock_write(&ev);
}

/*
 * Caller must acquire `sep->ep.lock` before calling this function.
 */
static zap_err_t __sock_send_msg_nolock(struct z_sock_ep *sep,
					struct sock_msg_hdr *m,
					size_t msg_size,
					const char *data, size_t data_len)
{
	z_sock_send_wr_t wr;
	sock_msg_type_t mtype = ntohs(m->msg_type);
	DEBUG_LOG_SEND_MSG(sep, m);
	assert(mtype != SOCK_MSG_WRITE_REQ); /* WRITE_REQ uses __wr_post() */
	/* allocate send wr */
	if (mtype == SOCK_MSG_READ_RESP) {
		/* allow big message, and do not copy `data`  */
		wr = __sock_wr_alloc(0, NULL);
		if (!wr)
			return ZAP_ERR_RESOURCE;
		wr->msg_len = msg_size;
		wr->data_len = data_len;
		wr->data = data;
		wr->off = 0;
		memcpy(wr->msg.bytes, m, msg_size);
	} else {
		if (data_len > sep->ep.z->max_msg) {
			DEBUG_LOG(sep, "ep: %p, SEND invalid message length: %ld\n",
				  sep, data_len);
			return ZAP_ERR_NO_SPACE;
		}
		wr = __sock_wr_alloc(data_len, NULL);
		if (!wr)
			return ZAP_ERR_RESOURCE;
		wr->msg_len = msg_size + data_len;
		wr->data_len = 0;
		wr->data = NULL;
		wr->off = 0;
		memcpy(wr->msg.bytes, m, msg_size);
		memcpy(wr->msg.bytes + msg_size, data, data_len);
	}
	__wr_post(sep, wr);
	return ZAP_ERR_OK;
}

static zap_err_t __sock_send_msg(struct z_sock_ep *sep, struct sock_msg_hdr *m,
				 size_t msg_size, const char *data, size_t data_len)
{
	zap_err_t zerr;
	pthread_mutex_lock(&sep->ep.lock);
	zerr = __sock_send_msg_nolock(sep, m, msg_size, data, data_len);
	pthread_mutex_unlock(&sep->ep.lock);
	return zerr;
}

/* Handling error or disconnection events */
static void sock_event(struct epoll_event *ev)
{
	struct z_sock_ep *sep = ev->data.ptr;
	struct zap_event zev = { 0 };

	int do_cb = 0;
	int drop_conn_ref = 0;

	pthread_mutex_lock(&sep->ep.lock);
	zap_io_thread_ep_release(&sep->ep);

	sock_send_complete(ev);

	/* Complete all outstanding I/O with ZEP_ERR_FLUSH */
	while (!TAILQ_EMPTY(&sep->io_q)) {
		struct z_sock_io *io = TAILQ_FIRST(&sep->io_q);
		TAILQ_REMOVE(&sep->io_q, io, q_link);

		/* Call the completion routine */
		struct zap_event zev = {
			.type = io->comp_type,
			.status = ZAP_ERR_FLUSH,
			.context = io->ctxt,
		};
		free(io);	/* Don't put back on free_q, we're closing */
		pthread_mutex_unlock(&sep->ep.lock);
		sep->ep.cb(&sep->ep, &zev);
		pthread_mutex_lock(&sep->ep.lock);
	}

	switch (sep->ep.state) {
	case ZAP_EP_ACCEPTING:
		sep->ep.state = ZAP_EP_ERROR;
		if (sep->app_accepted) {
			zev.type = ZAP_EVENT_CONNECT_ERROR;
			do_cb = 1;
		}
		drop_conn_ref = 1;
		break;
	case ZAP_EP_CONNECTING:
		zev.type = ZAP_EVENT_CONNECT_ERROR;
		sep->ep.state = ZAP_EP_ERROR;
		do_cb = drop_conn_ref = 1;
		break;
	case ZAP_EP_CONNECTED:	/* Peer closed. */
		sep->ep.state = ZAP_EP_PEER_CLOSE;
	case ZAP_EP_CLOSE:	/* App called close. */
		zev.type = ZAP_EVENT_DISCONNECTED;
		do_cb = drop_conn_ref = 1;
		break;
	case ZAP_EP_ERROR:
		do_cb = 0;
		drop_conn_ref = 1;
		break;
	default:
		LOG_(sep, "Unexpected state for EOF %d.\n",
		     sep->ep.state);
		sep->ep.state = ZAP_EP_ERROR;
		do_cb = 0;
		break;
	}

	pthread_mutex_unlock(&sep->ep.lock);
	if (do_cb) {
		sep->ep.cb((void*)sep, &zev);
	}

	if (drop_conn_ref) {
		/* Taken in z_sock_connect and process_sep_msg_accepted */
		ref_put(&sep->ep.ref, "accept/connect");
	}
	return;
}

static void __z_sock_conn_request(struct epoll_event *ev)
{
	struct z_sock_ep *sep = ev->data.ptr;
	zap_ep_t new_ep;
	struct z_sock_ep *new_sep;
	zap_err_t zerr;
	int sockfd;
	int rc;
	struct sockaddr sa;
	socklen_t sa_len = sizeof(sa);

	assert(ev->events == EPOLLIN);
	sockfd = accept(sep->sock, &sa, &sa_len);

	if (sockfd == -1) {
		LOG_(sep, "sock accept() error %d: in %s at %s:%d\n",
				errno , __func__, __FILE__, __LINE__);
		return;
	}

	new_ep = zap_new(sep->ep.z, sep->ep.cb);
	if (!new_ep) {
		zerr = errno;
		LOG_(sep, "Zap Error %d (%s): in %s at %s:%d\n",
				zerr, zap_err_str(zerr) , __func__, __FILE__,
				__LINE__);
		goto err_0;
	}

	void *uctxt = zap_get_ucontext(&sep->ep);
	zap_set_ucontext(new_ep, uctxt);
	new_sep = (void*) new_ep;
	new_sep->sock = sockfd;
	new_sep->ep.state = ZAP_EP_ACCEPTING;
	new_sep->sock_connected = 1;

	new_sep->ev_fn = sock_ev_cb;
	new_sep->ev.data.ptr = new_sep;
	new_sep->ev.events = EPOLLIN;

	ref_get(&new_sep->ep.ref, "accept/connect"); /* Release when receive disconnect/error event. */
	rc = __set_sock_opts(new_sep);
	if (rc)
		goto err_1;

	zerr = zap_io_thread_ep_assign(&new_sep->ep);
	if (zerr) {
		/* synchronous error & app doesn't know about this new
		 * endpoint yet ... so just log and cleanup. */
		LOG_(sep, "zap_io_thread_ep_assign() error %d on fd %d", rc,
					new_sep->sock);
		goto err_1;
	}

	return;

 err_1:
	free(new_ep);
 err_0:
	close(sockfd);
}

static zap_err_t z_sock_listen(zap_ep_t ep, struct sockaddr *sa,
				socklen_t sa_len)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	zap_err_t zerr;
	int flags, rc;

	zerr = zap_ep_change_state(&sep->ep, ZAP_EP_INIT, ZAP_EP_LISTENING);
	if (zerr)
		goto err_0;

	/* create a socket */
	sep->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sep->sock == -1) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_0;
	}
	rc = __sock_nonblock(sep->sock);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}
	flags = 1;
	rc = setsockopt(sep->sock, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));
	if (rc == -1) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}

	/* bind - listen */
	rc = bind(sep->sock, sa, sa_len);
	if (rc) {
		if (errno == EADDRINUSE)
			zerr = ZAP_ERR_BUSY;
		else
			zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}
	rc = listen(sep->sock, 1024);
	if (rc) {
		if (errno == EADDRINUSE)
			zerr = ZAP_ERR_BUSY;
		else
			zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}

	/* setup event */
	sep->ev_fn = __z_sock_conn_request;
	sep->ev.data.ptr = sep;
	sep->ev.events = EPOLLIN;

	/* assign the endpoint to a thread */
	zerr = zap_io_thread_ep_assign(&sep->ep);
	if (zerr)
		goto err_1;

	return ZAP_ERR_OK;

 err_1:
	close(sep->sock);
 err_0:
	return zerr;
}

static zap_err_t z_sock_send(zap_ep_t ep, char *buf, size_t len)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	struct z_sock_io *io;
	zap_err_t zerr;

	pthread_mutex_lock(&sep->ep.lock);

	if (ep->state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		goto err0;
	}

	io = __sock_io_alloc(sep);
	if (!io) {
		zerr = ZAP_ERR_RESOURCE;
		goto err0;
	}

	io->comp_type = ZAP_EVENT_SEND_COMPLETE;
	io->ctxt = NULL;

	io->wr = __sock_wr_alloc(len, io);
	if (!io->wr) {
		zerr = ZAP_ERR_RESOURCE;
		goto err1;
	}

	io->wr->flags = Z_SOCK_WR_COMPLETION | Z_SOCK_WR_SEND;
	io->wr->data = 0;
	io->wr->data_len = 0;
	io->wr->msg_len = sizeof(io->wr->msg.sendrecv) + len;
	z_sock_hdr_init(&io->wr->msg.sendrecv.hdr, 0, SOCK_MSG_SENDRECV,
			sizeof(io->wr->msg.sendrecv) + len, 0);
	io->wr->msg.sendrecv.data_len = htonl((uint32_t)len);
	memcpy(io->wr->msg.bytes + sizeof(io->wr->msg.sendrecv), buf, len);

	TAILQ_INSERT_TAIL(&sep->io_q, io, q_link);
	/* Post the work request */
	__wr_post(sep, io->wr);
	pthread_mutex_unlock(&sep->ep.lock);
	return ZAP_ERR_OK;
err1:
	__sock_io_free(sep, io);
err0:
	pthread_mutex_unlock(&sep->ep.lock);
	return zerr;
}

void z_sock_atfork()
{
	/* reset at fork */
	__atomic_store_n(&init_complete, 0, __ATOMIC_SEQ_CST);
}

zap_err_t z_sock_send_mapped(zap_ep_t ep, zap_map_t map, void *buf,
			     size_t len, void *context)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	struct z_sock_io *io;
	zap_err_t zerr;

	pthread_mutex_lock(&sep->ep.lock);

	if (sep->ep.state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		goto err0;
	}

	/* validate */
	if (z_map_access_validate(map, buf, len, ZAP_ACCESS_NONE) != 0) {
		zerr = ZAP_ERR_LOCAL_LEN;
		goto err0;
	}

	io = __sock_io_alloc(sep);
	if (!io) {
		zerr = ZAP_ERR_RESOURCE;
		goto err0;
	}

	io->comp_type = ZAP_EVENT_SEND_MAPPED_COMPLETE;
	io->ctxt = context;

	io->wr = __sock_wr_alloc(0, io);
	if (!io->wr) {
		zerr = ZAP_ERR_RESOURCE;
		goto err1;
	}

	/* prepare wr and message */
	zerr = ZAP_ERR_RESOURCE;
	io->wr->flags = Z_SOCK_WR_COMPLETION | Z_SOCK_WR_SEND_MAPPED;
	io->wr->data = buf;
	io->wr->data_len = len;
	io->wr->msg_len = sizeof(io->wr->msg.sendrecv);
	z_sock_hdr_init(&io->wr->msg.sendrecv.hdr, 0, SOCK_MSG_SENDRECV,
			io->wr->msg_len + len, (uint64_t)context);
	io->wr->msg.sendrecv.data_len = htonl((uint32_t) len);

	TAILQ_INSERT_TAIL(&sep->io_q, io, q_link);
	/* write message */
	__wr_post(sep, io->wr);

	pthread_mutex_unlock(&sep->ep.lock);
	return ZAP_ERR_OK;
err1:
	__sock_io_free(sep, io);
err0:
	pthread_mutex_unlock(&sep->ep.lock);
	return zerr;
}

static int init_once()
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&mutex);
	/* check if we lose the race */
	if (__atomic_load_n(&init_complete, __ATOMIC_SEQ_CST)) {
		pthread_mutex_unlock(&mutex);
		return 0;
	}

	pthread_atfork(NULL, NULL, (void*)z_sock_atfork);

	z_key_tree.root = NULL;
	z_key_tree.comparator = z_rbn_cmp;
	pthread_mutex_init(&z_key_tree_mutex, NULL);
	__atomic_store_n(&init_complete, 1, __ATOMIC_SEQ_CST);
	pthread_mutex_unlock(&mutex);

	return 0;
}

static zap_ep_t z_sock_new(zap_t z, zap_cb_fn_t cb)
{
	int rc;

	if (!__atomic_load_n(&init_complete, __ATOMIC_SEQ_CST) && init_once())
		return NULL;

	struct z_sock_ep *sep = calloc(1, sizeof(*sep));
	if (!sep) {
		errno = ZAP_ERR_RESOURCE;
		return NULL;
	}
	TAILQ_INIT(&sep->io_q);
	TAILQ_INIT(&sep->io_cq);
	TAILQ_INIT(&sep->sq);
	sep->sock = -1;
	pthread_cond_init(&sep->sq_cond, NULL);

	rc = z_sock_buff_init(&sep->buff, 65536); /* 64 KB initial size buff */
	if (rc) {
		/* errno has been set */
		free(sep);
		return NULL;
	}

	pthread_mutex_lock(&z_sock_list_mutex);
	LIST_INSERT_HEAD(&z_sock_list, sep, link);
	pthread_mutex_unlock(&z_sock_list_mutex);

	DEBUG_ZLOG(z, "z_sock_new(%p)\n", sep);

	return (zap_ep_t)sep;
}

static void z_sock_destroy(zap_ep_t ep)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	z_sock_send_wr_t wr;

	DEBUG_LOG(sep, "z_sock_destroy(%p)\n", sep);

	while (!TAILQ_EMPTY(&sep->sq)) {
		wr = TAILQ_FIRST(&sep->sq);
		TAILQ_REMOVE(&sep->sq, wr, link);
		free(wr);
	}

	if (sep->conn_data)
		free(sep->conn_data);
	if (sep->sock > -1) {
		close(sep->sock);
		sep->sock = -1;
	}
	/* all pending I/O should have been flushed */
	ZAP_ASSERT(TAILQ_EMPTY(&sep->io_q), ep, "%s: The io_q is not empty "
			"when the reference count reaches 0.\n", __func__);
	z_sock_buff_cleanup(&sep->buff);
	pthread_mutex_lock(&z_sock_list_mutex);
	LIST_REMOVE(sep, link);
	pthread_mutex_unlock(&z_sock_list_mutex);
	free(ep);
}

zap_err_t z_sock_accept(zap_ep_t ep, zap_cb_fn_t cb, char *data, size_t data_len)
{
	/* ep is the newly created ep from __z_sock_conn_request */
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	zap_err_t zerr;

	pthread_mutex_lock(&sep->ep.lock);

	if (sep->ep.state != ZAP_EP_ACCEPTING) {
		zerr = ZAP_ERR_ENDPOINT;
		goto err_0;
	}

	/* Replace the callback with the one provided by the caller */
	sep->ep.cb = cb;

	zerr = __sock_send(sep, SOCK_MSG_ACCEPTED, data, data_len);
	if (zerr)
		goto err_1;
	sep->app_accepted = 1;
	pthread_mutex_unlock(&sep->ep.lock);

	return ZAP_ERR_OK;

err_1:
	sep->ep.state = ZAP_EP_ERROR;
	shutdown(sep->sock, SHUT_RDWR);
err_0:
	pthread_mutex_unlock(&sep->ep.lock);
	return zerr;
}

static zap_err_t z_sock_reject(zap_ep_t ep, char *data, size_t data_len)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	zap_err_t zerr;

	pthread_mutex_lock(&sep->ep.lock);
	zerr = __sock_send(sep, SOCK_MSG_REJECTED, data, data_len);
	if (zerr)
		goto err;
	pthread_mutex_unlock(&sep->ep.lock);
	return ZAP_ERR_OK;
err:
	sep->ep.state = ZAP_EP_ERROR;
	shutdown(sep->sock, SHUT_RDWR);
	pthread_mutex_unlock(&sep->ep.lock);
	return zerr;
}

static zap_err_t z_sock_unmap(zap_map_t map)
{
	if (map->type == ZAP_MAP_LOCAL) {
		if (SOCK_MAP_KEY_GET(map))
			z_sock_key_delete(SOCK_MAP_KEY_GET(map));
	} else {
		if (map->ep)
			ref_put(&map->ep->ref, "zap_map/rendezvous");
	}
	return ZAP_ERR_OK;
}

static zap_err_t z_sock_share(zap_ep_t ep, zap_map_t map,
				const char *msg, size_t msg_len)
{
	struct z_sock_ep *sep = (void*) ep;
	zap_err_t zerr;
	struct sock_msg_rendezvous msgr;

	/* validate */
	if (ep->state != ZAP_EP_CONNECTED)
		return ZAP_ERR_NOT_CONNECTED;

	if (map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;

	/*
	 * NOTE: For sock transport, the map key is only used for
	 *       serving/validating the peers' read/write requests. In
	 *       `read(peer_map, our_map)`, the key of our_map is not involved.
	 *       Also, `write(our_map, peer_map)`, the key of our_map is not
	 *       involved. Hence, allocating/assigning the key in
	 *       `zap_share(our_map)` is suffice.
	 */
	if (!SOCK_MAP_KEY_GET(map)) {
		if (!z_key_alloc(map))  /* this modifies map->mr[ZAP_SOCK] */
			return ZAP_ERR_RESOURCE;
	}

	/* prepare message */
	z_sock_hdr_init(&msgr.hdr, 0, SOCK_MSG_RENDEZVOUS,
			sizeof(struct sock_msg_rendezvous) + msg_len, 0);
	msgr.rmap_key = SOCK_MAP_KEY_GET(map);
	msgr.acc = htonl(map->acc);
	msgr.addr = htobe64((uint64_t)map->addr);
	msgr.data_len = htonl(map->len);

	/* write message with data */
	zerr = __sock_send_msg(sep, &msgr.hdr, sizeof(msgr), msg, msg_len);
	return zerr;
}

static zap_err_t z_sock_read(zap_ep_t ep, zap_map_t src_map, char *src,
			     zap_map_t dst_map, char *dst, size_t sz,
			     void *context)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	struct z_sock_io *io;
	zap_err_t zerr = ZAP_ERR_OK;

	pthread_mutex_lock(&sep->ep.lock);
	if (sep->ep.state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		pthread_mutex_unlock(&sep->ep.lock);
		goto err0;
	}

	/* validate */
	if (z_map_access_validate(src_map, src, sz, ZAP_ACCESS_READ) != 0) {
		zerr = ZAP_ERR_REMOTE_PERMISSION;
		goto err0;
	}

	if (z_map_access_validate(dst_map, dst, sz, ZAP_ACCESS_NONE) != 0) {
		zerr = ZAP_ERR_LOCAL_LEN;
		goto err0;
	}

	io = __sock_io_alloc(sep);
	if (!io) {
		zerr = ZAP_ERR_RESOURCE;
		goto err0;
	}

	io->comp_type = ZAP_EVENT_READ_COMPLETE;
	io->ctxt = context;

	io->wr = __sock_wr_alloc(0, io);
	if (!io->wr) {
		zerr = ZAP_ERR_RESOURCE;
		goto err1;
	}

	/* prepare wr and message */
	io->wr->msg_len = sizeof(io->wr->msg.read_req);
	z_sock_hdr_init(&io->wr->msg.hdr, 0, SOCK_MSG_READ_REQ,
		   sizeof(io->wr->msg.read_req), (uint64_t)context);
	io->wr->msg.read_req.src_map_key = SOCK_MAP_KEY_GET(src_map);
	io->wr->msg.read_req.src_ptr = htobe64((uint64_t) src);
	io->wr->msg.read_req.data_len = htonl((uint32_t)sz);
	io->dst_map = dst_map;
	io->dst_ptr = dst;

	TAILQ_INSERT_TAIL(&sep->io_q, io, q_link);
	/* write message */
	__wr_post(sep, io->wr);

	pthread_mutex_unlock(&sep->ep.lock);
	return ZAP_ERR_OK;

err1:
	__sock_io_free(sep, io);
err0:
	pthread_mutex_unlock(&sep->ep.lock);
	return zerr;
}

static zap_err_t z_sock_write(zap_ep_t ep, zap_map_t src_map, char *src,
			      zap_map_t dst_map, char *dst, size_t sz,
			      void *context)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	struct z_sock_io *io;
	zap_err_t zerr;

	pthread_mutex_lock(&sep->ep.lock);
	if (sep->ep.state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		goto err0;
	}

	/* validate */
	if (z_map_access_validate(src_map, src, sz, ZAP_ACCESS_NONE) != 0) {
		zerr = ZAP_ERR_LOCAL_LEN;
		goto err0;
	}

	if (z_map_access_validate(dst_map, dst, sz, ZAP_ACCESS_WRITE) != 0) {
		zerr = ZAP_ERR_REMOTE_PERMISSION;
		goto err0;
	}

	io = __sock_io_alloc(sep);
	if (!io) {
		zerr = ZAP_ERR_RESOURCE;
		goto err0;
	}

	io->comp_type = ZAP_EVENT_WRITE_COMPLETE;
	io->ctxt = context;

	io->wr = __sock_wr_alloc(0, io);
	if (!io->wr) {
		zerr = ZAP_ERR_RESOURCE;
		goto err1;
	}

	/* prepare wr and message */
	zerr = ZAP_ERR_RESOURCE;
	io->wr->data = src;
	io->wr->data_len = sz;
	io->wr->msg_len = sizeof(io->wr->msg.write_req);
	z_sock_hdr_init(&io->wr->msg.write_req.hdr, 0, SOCK_MSG_WRITE_REQ,
			io->wr->msg_len + sz, (uint64_t)context);
	io->wr->msg.write_req.dst_map_key = SOCK_MAP_KEY_GET(dst_map);
	io->wr->msg.write_req.dst_ptr = htobe64((uint64_t) dst);
	io->wr->msg.write_req.data_len = htonl((uint32_t) sz);

	TAILQ_INSERT_TAIL(&sep->io_q, io, q_link);
	/* write message */
	__wr_post(sep, io->wr);

	pthread_mutex_unlock(&sep->ep.lock);
	return ZAP_ERR_OK;

err1:
	__sock_io_free(sep, io);
err0:
	pthread_mutex_unlock(&sep->ep.lock);
	return zerr;
}

zap_io_thread_t z_sock_io_thread_create(zap_t z)
{
	int rc;
	z_sock_io_thread_t thr = calloc(1, sizeof(*thr));
	if (!thr)
		goto err0;
	rc = zap_io_thread_init(&thr->zap_io_thread, z, "zap_sock_io",
				ZAP_ENV_INT(ZAP_THRSTAT_WINDOW));
	if (rc)
		goto err1;
	thr->efd = epoll_create1(O_CLOEXEC);
	if (thr->efd < 1)
		goto err2;
	rc = pthread_create(&thr->zap_io_thread.thread, NULL, io_thread_proc, thr);
	if (rc)
		goto err3;
	pthread_setname_np(thr->zap_io_thread.thread, "zap_sock_io");
	return &thr->zap_io_thread;
 err3:
	close(thr->efd);
 err2:
	zap_io_thread_release(&thr->zap_io_thread);
 err1:
	free(thr);
 err0:
	errno = ZAP_ERR_RESOURCE;
	return NULL;
}

zap_err_t z_sock_io_thread_cancel(zap_io_thread_t t)
{
	int rc;
	rc = pthread_cancel(t->thread);
	switch (rc) {
	case ESRCH: /* cleaning up structure w/o running thread b/c of fork */
		((z_sock_io_thread_t)t)->efd = -1; /* b/c of CLOEXEC */
		io_thread_cleanup(t);
	case 0:
		return ZAP_ERR_OK;
	default:
		return ZAP_ERR_LOCAL_OPERATION;
	}
}

zap_err_t z_sock_io_thread_ep_assign(zap_io_thread_t t, zap_ep_t ep)
{
	z_sock_io_thread_t thr = (void*)t;
	struct z_sock_ep *sep = (void*)ep;
	int rc;
	rc = epoll_ctl(thr->efd, EPOLL_CTL_ADD, sep->sock, &sep->ev);
	return rc ? ZAP_ERR_RESOURCE : ZAP_ERR_OK;
}

zap_err_t z_sock_io_thread_ep_release(zap_io_thread_t t, zap_ep_t ep)
{
	z_sock_io_thread_t thr = (void*)t;
	struct z_sock_ep *sep = (void*)ep;
	int rc;
	rc = epoll_ctl(thr->efd, EPOLL_CTL_DEL, sep->sock, &sep->ev);
	return rc ? ZAP_ERR_RESOURCE : ZAP_ERR_OK;
}

zap_err_t zap_transport_get(zap_t *pz, zap_log_fn_t log_fn,
			    zap_mem_info_fn_t mem_info_fn)
{
	zap_t z;
	size_t sendrecv_sz, rendezvous_sz, hdr_sz;
	if (!__atomic_load_n(&init_complete, __ATOMIC_SEQ_CST) && init_once())
		goto err;

	z = calloc(1, sizeof (*z));
	if (!z)
		goto err;

	sendrecv_sz = sizeof(struct sock_msg_sendrecv);
	rendezvous_sz = sizeof(struct sock_msg_rendezvous);
	hdr_sz = (sendrecv_sz<rendezvous_sz)?rendezvous_sz:sendrecv_sz;

	/* max_msg is used only by the send/receive operations */
	z->max_msg = SOCKBUF_SZ - hdr_sz;
	z->new = z_sock_new;
	z->destroy = z_sock_destroy;
	z->connect = z_sock_connect;
	z->accept = z_sock_accept;
	z->reject = z_sock_reject;
	z->listen = z_sock_listen;
	z->close = z_sock_close;
	z->send = z_sock_send;
	z->read = z_sock_read;
	z->write = z_sock_write;
	z->unmap = z_sock_unmap;
	z->share = z_sock_share;
	z->get_name = z_get_name;
	z->send_mapped = z_sock_send_mapped;
	z->io_thread_create = z_sock_io_thread_create;
	z->io_thread_cancel = z_sock_io_thread_cancel;
	z->io_thread_ep_assign = z_sock_io_thread_ep_assign;
	z->io_thread_ep_release = z_sock_io_thread_ep_release;

	/* is it needed? */
	z->mem_info_fn = mem_info_fn;

	*pz = z;
	return ZAP_ERR_OK;

 err:
	return ZAP_ERR_RESOURCE;
}
