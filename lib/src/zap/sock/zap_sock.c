/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2014-2019 Open Grid Computing, Inc. All rights reserved.
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
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include <endian.h>
#include <signal.h>
#include "coll/rbt.h"
#include "ovis_util/os_util.h"
#include "ovis_event/ovis_event.h"

#include "zap_sock.h"

#define LOG__(ep, ...) do { \
	if ((ep) && (ep)->z && (ep)->z->log_fn) \
		(ep)->z->log_fn(__VA_ARGS__); \
} while (0);

#define LOG_(sep, ...) do { \
	if ((sep) && (sep)->ep.z && (sep)->ep.z->log_fn) \
		(sep)->ep.z->log_fn(__VA_ARGS__); \
} while(0);

#ifdef DEBUG_ZAP_SOCK
#define DEBUG_LOG(ep, ...) LOG__((zap_ep_t)(ep), __VA_ARGS__)
#define DEBUG_LOG_SEND_MSG(sep, msg) __log_sep_msg(sep, 0, (void*)(msg))
#define DEBUG_LOG_RECV_MSG(sep, msg) __log_sep_msg(sep, 1, (void*)(msg))
#else
#define DEBUG_LOG(ep, ...)
#define DEBUG_LOG_SEND_MSG(sep, msg)
#define DEBUG_LOG_RECV_MSG(sep, msg)
#endif

static int init_complete = 0;

static pthread_t io_thread;

static ovis_scheduler_t sched;

static void *io_thread_proc(void *arg);

static void sock_event(ovis_event_t ev);
static void sock_read(ovis_event_t ev);
static void sock_write(ovis_event_t ev);
static void sock_connect(ovis_event_t ev);

static int __disable_epoll_out(struct z_sock_ep *sep);
static int __enable_epoll_out(struct z_sock_ep *sep);

static void sock_ev_cb(ovis_event_t ev);

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

static uint32_t z_last_key;
static struct rbt z_key_tree;
static pthread_mutex_t z_key_tree_mutex;

static LIST_HEAD(, z_sock_ep) z_sock_list = LIST_HEAD_INITIALIZER(0);
static pthread_mutex_t z_sock_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static int __set_sockbuf_sz(int sockfd)
{
	int rc;
	size_t optval = SOCKBUF_SZ;
	rc = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));
	if (rc)
		return rc;
	rc = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
	return rc;
}

static int z_rbn_cmp(void *a, const void *b)
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
static struct z_sock_key *z_key_alloc(struct zap_sock_map *map)
{
	struct z_sock_key *key = calloc(1, sizeof(*key));
	if (!key)
		return NULL;
	key->map = map;
	pthread_mutex_lock(&z_key_tree_mutex);
	key->rb_node.key = (void*)(uint64_t)(++z_last_key);
	rbt_ins(&z_key_tree, &key->rb_node);
	pthread_mutex_unlock(&z_key_tree_mutex);
	return key;
}

/* Caller must hold the z_key_tree_mutex lock. */
static struct z_sock_key *z_sock_key_find(uint32_t key)
{
	struct rbn *krbn = rbt_find(&z_key_tree, (void*)(uint64_t)key);
	if (!krbn)
		return NULL;
	return container_of(krbn, struct z_sock_key, rb_node);
}

static void z_key_delete(uint32_t key)
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
	bzero(buff->data, buff->alen);
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

	pthread_mutex_lock(&sep->ep.lock);
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

static int __set_keep_alive(struct z_sock_ep *sep)
{
	int rc;
	int optval = 0;
	rc = setsockopt(sep->sock, SOL_SOCKET, SO_KEEPALIVE, &optval,
			sizeof(int));
	if (rc) {
		LOG_(sep, "zap_sock: WARNING: set SO_KEEPALIVE error: %d\n", errno);
		return errno;
	}
	optval = ZAP_SOCK_KEEPCNT;
	rc = setsockopt(sep->sock, SOL_TCP, TCP_KEEPCNT, &optval, sizeof(int));
	if (rc) {
		LOG_(sep, "zap_sock: WARNING: set TCP_KEEPCNT error: %d\n", errno);
		return errno;
	}
	optval = ZAP_SOCK_KEEPIDLE;
	rc = setsockopt(sep->sock, SOL_TCP, TCP_KEEPIDLE, &optval, sizeof(int));
	if (rc) {
		LOG_(sep, "zap_sock: WARNING: set TCP_KEEPIDLE error: %d\n", errno);
		return errno;
	}
	optval = ZAP_SOCK_KEEPINTVL;
	rc = setsockopt(sep->sock, SOL_TCP, TCP_KEEPINTVL, &optval, sizeof(int));
	if (rc) {
		LOG_(sep, "zap_sock: WARNING: set TCP_KEEPINTVL error: %d\n", errno);
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

	rc = __set_sockbuf_sz(sep->sock);
	if (rc) {
		zerr = ZAP_ERR_TRANSPORT;
		goto err1;
	}

	rc = __sock_nonblock(sep->sock);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto err1;
	}
	rc = __set_keep_alive(sep);
	if (rc) {
		LOG_(sep, "zap_sock: WARNING: __set_keep_alive() rc: %d\n", rc);
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

	zap_get_ep(&sep->ep); /* Release when disconnect */

	OVIS_EVENT_INIT(&sep->ev);
	sep->ev.param.type = OVIS_EVENT_EPOLL;
	sep->ev.param.cb_fn = sock_ev_cb;
	sep->ev.param.ctxt = sep;
	sep->ev.param.fd = sep->sock;
	sep->ev.param.epoll_events = EPOLLIN|EPOLLOUT;
	rc = ovis_scheduler_event_add(sched, &sep->ev);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto err3;
	}
	return ZAP_ERR_OK;

 err3:
	zap_put_ep(&sep->ep);
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
	zap_get_ep(&sep->ep); /* Release when receive disconnect/error event. */
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

struct z_sock_io *__sock_io_alloc(struct z_sock_ep *sep)
{
	struct z_sock_io *io;
	pthread_mutex_lock(&sep->ep.lock);
	if (!TAILQ_EMPTY(&sep->free_q)) {
		io = TAILQ_FIRST(&sep->free_q);
		TAILQ_REMOVE(&sep->free_q, io, q_link);
	} else
		io = calloc(1, sizeof(*io));
	pthread_mutex_unlock(&sep->ep.lock);
	return io;
}

void __sock_io_free(struct z_sock_ep *sep, struct z_sock_io *io)
{
	pthread_mutex_lock(&sep->ep.lock);
	TAILQ_INSERT_TAIL(&sep->free_q, io, q_link);
	pthread_mutex_unlock(&sep->ep.lock);
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
	ZAP_ASSERT(msg->hdr.xid == io->hdr.xid, (&sep->ep),
			"%s: The transaction IDs mismatched between the "
			"IO entry %d and message %d.\n", __func__,
			io->hdr.xid, msg->hdr.xid);
	TAILQ_REMOVE(&sep->io_q, io, q_link);
	pthread_mutex_unlock(&sep->ep.lock);

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
	__sock_io_free(sep, io);

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
	ZAP_ASSERT(io, &sep->ep, "%s: The transaction IDs mismatched "
			"between the IO entry %d and message %d.\n",
			__func__, io->hdr.xid, msg->hdr.xid);
	/* Put it back on the free_q */
	pthread_mutex_unlock(&sep->ep.lock);
	__sock_io_free(sep, io);

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

	msg = sep->buff.data;

	struct zap_sock_map *map = calloc(1, sizeof(*map));
	if (!map) {
		LOG_(sep, "ENOMEM in %s at %s:%d\n",
				__func__, __FILE__, __LINE__);
		goto err0;
	}

	char *amsg = NULL;
	size_t amsg_len = ntohl(msg->hdr.msg_len) - sizeof(*msg);
	if (amsg_len) {
		amsg = msg->msg;
	}

	map->map.ref_count = 1;
	map->map.ep = &sep->ep;
	map->key = msg->rmap_key;
	map->map.acc = ntohl(msg->acc);
	map->map.type = ZAP_MAP_REMOTE;
	map->map.addr = (void *)(uint64_t)be64toh((uint64_t)msg->addr);
	map->map.len = ntohl(msg->data_len);

	zap_get_ep(&sep->ep); /* Release when app calls zap_unmap(). */
	pthread_mutex_lock(&sep->ep.lock);
	LIST_INSERT_HEAD(&sep->ep.map_list, &map->map, link);
	pthread_mutex_unlock(&sep->ep.lock);

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
static void sock_ev_cb(ovis_event_t ev)
{
	struct z_sock_ep *sep = ev->param.ctxt;

	zap_get_ep(&sep->ep);
	DEBUG_LOG(sep, "ep: %p, sock_ev_cb() -- BEGIN --\n", sep);

	/* Handle write */
	if (ev->cb.epoll_events & EPOLLOUT) {
		pthread_mutex_lock(&sep->ep.lock);
		if (sep->sock_connected) {
			pthread_mutex_unlock(&sep->ep.lock);
			sock_write(ev);
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
	if (ev->cb.epoll_events & EPOLLIN) {
		sock_read(ev);
	}

	/* Handle disconnect
	 *
	 * This must be last to avoid disconnecting before
	 * sending/receiving all data
	 */
	if (ev->cb.epoll_events & (EPOLLERR|EPOLLHUP)) {
		int err;
		socklen_t err_len = sizeof(err);
		getsockopt(ev->param.fd, SOL_SOCKET, SO_ERROR, &err, &err_len);
		DEBUG_LOG(sep, "ep: %p, sock_ev_cb() events %04x err %d\n",
			  sep, ev->cb.epoll_events, err);
		sock_event(ev);
		goto out;
	}
 out:
	DEBUG_LOG(sep, "ep: %p, sock_ev_cb() -- END --\n", sep);
	zap_put_ep(&sep->ep);
}

static void sock_connect(ovis_event_t ev)
{
	/* sock connect routine on the initiator side */
	struct z_sock_ep *sep = ev->param.ctxt;
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

static void sock_write(ovis_event_t ev)
{
	struct z_sock_ep *sep = ev->param.ctxt;
	ssize_t wsz;
	z_sock_send_wr_t wr;

	pthread_mutex_lock(&sep->ep.lock);
 next:
	wr = TAILQ_FIRST(&sep->sq);
	if (!wr) {
		/* sq empty, disable epoll out */
		__disable_epoll_out(sep);
		goto out;
	}

	/* msg */
	if (wr->msg_len) {
		wsz = write(sep->sock, wr->msg + wr->off, wr->msg_len);
		if (wsz < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				goto out;
			/* bad error */
			goto err;
		}
		DEBUG_LOG(sep, "ep: %p, wrote %ld bytes\n", sep, wsz);
		if (wsz < wr->msg_len) {
			wr->msg_len -= wsz;
			wr->off += wsz;
			goto out;
		}
		wr->msg_len = 0;
		wr->off = 0;
	}

	if (wr->data_len) {
		wsz = write(sep->sock, wr->data + wr->off, wr->data_len);
		if (wsz < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				goto out;
			/* bad error */
			goto err;
		}
		DEBUG_LOG(sep, "ep: %p, wrote %ld bytes\n", sep, wsz);
		if (wsz < wr->data_len) {
			wr->data_len -= wsz;
			wr->off += wsz;
			goto out;
		}
		wr->data_len = 0;
		wr->off = 0;
	}

	/* reaching here means wr->data_len and wr->msg_len are 0 */
	TAILQ_REMOVE(&sep->sq, wr, link);
	free(wr);
	goto next;

 out:
	pthread_mutex_unlock(&sep->ep.lock);
	return;

 err:
	shutdown(sep->sock, SHUT_RDWR);
	pthread_mutex_unlock(&sep->ep.lock);
}

#define min_t(t, x, y) (t)((t)x < (t)y?(t)x:(t)y)
static void sock_read(ovis_event_t ev)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ev->param.ctxt;
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
			/* shutdown is called already. No need to shut it down agian. */
			ZAP_ASSERT(0, &(sep->ep), "%s bad ep state (ZAP_EP_CLOSE)", __func__);
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

#ifdef NDEBUG
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
static void *io_thread_proc(void *arg)
{
	/* Zap thread will not handle any signal */
	int rc;
	sigset_t sigset;
	sigfillset(&sigset);
	rc = sigprocmask(SIG_SETMASK, &sigset, NULL);
	assert(rc == 0 && "pthread_sigmask error");
	rc = ovis_scheduler_loop(sched, 0);
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
	if (sep->ev.param.epoll_events & EPOLLOUT)
		return 0; /* already enabled */
	DEBUG_LOG(sep, "ep: %p, Enabling EPOLLOUT\n", sep);
	rc = ovis_scheduler_epoll_event_mod(sched, &sep->ev, EPOLLIN|EPOLLOUT);
	return rc;
}

/* caller must have sep->ep.lock held */
static int __disable_epoll_out(struct z_sock_ep *sep)
{
	int rc;
	if ((sep->ev.param.epoll_events & EPOLLOUT) == 0)
		return 0; /* already disabled */
	DEBUG_LOG(sep, "ep: %p, Disabling EPOLLOUT\n", sep);
	rc = ovis_scheduler_epoll_event_mod(sched, &sep->ev, EPOLLIN);
	return rc;
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
	/* allocate send wr */
	if (mtype == SOCK_MSG_READ_RESP || mtype == SOCK_MSG_WRITE_REQ) {
		/* allow big message, and do not copy `data`  */
		wr = malloc(sizeof(*wr) + msg_size);
		if (!wr)
			return ZAP_ERR_RESOURCE;
		wr->msg_len = msg_size;
		wr->data_len = data_len;
		wr->data = data;
		wr->off = 0;
		memcpy(wr->msg, m, msg_size);
	} else {
		if (data_len > sep->ep.z->max_msg) {
			DEBUG_LOG(sep, "ep: %p, SEND invalid message length: %ld\n",
				  sep, data_len);
			return ZAP_ERR_NO_SPACE;
		}
		wr = malloc(sizeof(*wr) + msg_size + data_len);
		if (!wr)
			return ZAP_ERR_RESOURCE;
		wr->msg_len = msg_size + data_len;
		wr->data_len = 0;
		wr->data = NULL;
		wr->off = 0;
		memcpy(wr->msg, m, msg_size);
		memcpy(wr->msg + msg_size, data, data_len);
	}
	TAILQ_INSERT_TAIL(&sep->sq, wr, link);
	if (__enable_epoll_out(sep))
		return ZAP_ERR_RESOURCE;
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
static void sock_event(ovis_event_t ev)
{
	struct z_sock_ep *sep = ev->param.ctxt;
	struct zap_event zev = { 0 };

	int do_cb = 0;
	int drop_conn_ref = 0;

	pthread_mutex_lock(&sep->ep.lock);

	assert(&sep->ev == ev);
	ovis_scheduler_event_del(sched, &sep->ev);

	/* Complete all outstanding I/O with ZEP_ERR_FLUSH */
	while (!TAILQ_EMPTY(&sep->io_q)) {
		zap_event_type_t ev_type;
		sock_msg_type_t msg_type;
		struct z_sock_io *io = TAILQ_FIRST(&sep->io_q);
		TAILQ_REMOVE(&sep->io_q, io, q_link);

		msg_type = ntohs(io->hdr.msg_type);
		if (msg_type >= SOCK_MSG_FIRST && msg_type < SOCK_MSG_TYPE_LAST)
			ev_type = ev_type_cvt[msg_type];
		else
			ev_type = ZAP_EVENT_BAD;

		/* Call the completion routine */
		struct zap_event zev = {
			.type = ev_type,
			.status = ZAP_ERR_FLUSH,
			.context = (void *)io->hdr.ctxt
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
			do_cb = drop_conn_ref = 1;
		}
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
		break;
	default:
		LOG_(sep, "Unexpected state for EOF %d.\n",
		     sep->ep.state);
		sep->ep.state = ZAP_EP_ERROR;
		do_cb = 0;
		break;
	}

	pthread_mutex_unlock(&sep->ep.lock);
	if (do_cb)
		sep->ep.cb((void*)sep, &zev);

	if (drop_conn_ref)
		/* Taken in z_sock_connect and process_sep_msg_accepted */
		zap_put_ep(&sep->ep);
	return;
}

static void __z_sock_conn_request(ovis_event_t ev)
{
	struct z_sock_ep *sep = ev->param.ctxt;
	zap_ep_t new_ep;
	struct z_sock_ep *new_sep;
	zap_err_t zerr;
	int sockfd;
	int rc;
	struct sockaddr sa;
	socklen_t sa_len = sizeof(sa);

	assert(ev->cb.epoll_events == EPOLLIN);
	sockfd = accept(sep->sock, &sa, &sa_len);

	if (sockfd == -1) {
		LOG_(sep, "sock accept() error %d: in %s at %s:%d\n",
				errno , __func__, __FILE__, __LINE__);
		return;
	}

	rc = __set_sockbuf_sz(sockfd);
	if (rc) {
		close(sockfd);
		return;
	}

	rc = __sock_nonblock(sockfd);
	if (rc) {
		close(sockfd);
		return;
	}

	new_ep = zap_new(sep->ep.z, sep->ep.app_cb);
	if (!new_ep) {
		zerr = errno;
		LOG_(sep, "Zap Error %d (%s): in %s at %s:%d\n",
				zerr, zap_err_str(zerr) , __func__, __FILE__,
				__LINE__);
		close(sockfd);
		return;
	}

	void *uctxt = zap_get_ucontext(&sep->ep);
	zap_set_ucontext(new_ep, uctxt);
	new_sep = (void*) new_ep;
	new_sep->sock = sockfd;
	new_sep->ep.state = ZAP_EP_ACCEPTING;
	new_sep->sock_connected = 1;

	OVIS_EVENT_INIT(&new_sep->ev);
	new_sep->ev.param.type = OVIS_EVENT_EPOLL;
	new_sep->ev.param.cb_fn = sock_ev_cb;
	new_sep->ev.param.ctxt = new_sep;
	new_sep->ev.param.epoll_events = EPOLLIN;
	new_sep->ev.param.fd = sockfd;

	rc = ovis_scheduler_event_add(sched, &new_sep->ev);
	if (rc) {
		/* synchronous error & app doesn't know about this new
		 * endpoint yet ... so just log and cleanup. */
		LOG_(sep, "ovis_scheduler_event_add() error %d on fd %d", rc,
					new_sep->sock);
		free(new_ep);
		close(sockfd);
	}
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

	/* setup ovis event */
	OVIS_EVENT_INIT(&sep->ev);
	sep->ev.param.type = OVIS_EVENT_EPOLL;
	sep->ev.param.fd = sep->sock;
	sep->ev.param.epoll_events = EPOLLIN;
	sep->ev.param.cb_fn = __z_sock_conn_request;
	sep->ev.param.ctxt = sep;
	rc = ovis_scheduler_event_add(sched, &sep->ev);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto err_1;
	}

	return ZAP_ERR_OK;

 err_1:
	close(sep->sock);
 err_0:
	return zerr;
}

static zap_err_t z_sock_send(zap_ep_t ep, char *buf, size_t len)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	zap_err_t zerr = ZAP_ERR_OK;
	pthread_mutex_lock(&sep->ep.lock);
	if (ep->state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		goto out;
	}

	zerr = __sock_send(sep, SOCK_MSG_SENDRECV, buf, len);
out:
	pthread_mutex_unlock(&sep->ep.lock);
	return zerr;
}

static int init_once()
{
	int rc = ENOMEM;

	sched = ovis_scheduler_new();
	if (!sched)
		return errno;

	rc = pthread_create(&io_thread, NULL, io_thread_proc, 0);
	if (rc)
		goto err_1;

	init_complete = 1;

	z_key_tree.root = NULL;
	z_key_tree.comparator = z_rbn_cmp;
	pthread_mutex_init(&z_key_tree_mutex, NULL);
	// atexit(z_sock_cleanup);
	return 0;

 err_1:
	ovis_scheduler_free(sched);
	sched = NULL;
	return rc;
}

static zap_ep_t z_sock_new(zap_t z, zap_cb_fn_t cb)
{
	int rc;
	struct z_sock_ep *sep = calloc(1, sizeof(*sep));
	if (!sep) {
		errno = ZAP_ERR_RESOURCE;
		return NULL;
	}
	TAILQ_INIT(&sep->free_q);
	TAILQ_INIT(&sep->io_q);
	TAILQ_INIT(&sep->sq);
	sep->sock = -1;

	rc = z_sock_buff_init(&sep->buff, 65536); /* 64 KB initial size buff */
	if (rc) {
		/* errno has been set */
		free(sep);
		return NULL;
	}

	pthread_mutex_lock(&z_sock_list_mutex);
	LIST_INSERT_HEAD(&z_sock_list, sep, link);
	pthread_mutex_unlock(&z_sock_list_mutex);

	return (zap_ep_t)sep;
}

static void z_sock_destroy(zap_ep_t ep)
{
	struct z_sock_io *io;
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	z_sock_send_wr_t wr;

#ifdef DEBUG
	ep->z->log_fn("SOCK: destroying endpoint %p\n", ep);
#endif

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
	while (!TAILQ_EMPTY(&sep->free_q)) {
		io = TAILQ_FIRST(&sep->free_q);
		TAILQ_REMOVE(&sep->free_q, io, q_link);
		free(io);
	}
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

static zap_err_t
z_sock_map(zap_ep_t ep, zap_map_t *pm, void *buf, size_t len, zap_access_t acc)
{
	struct zap_sock_map *map = calloc(1, sizeof(*map));
	zap_err_t zerr = ZAP_ERR_OK;
	if (!map) {
		zerr = ZAP_ERR_RESOURCE;
		goto err0;
	}
	/* Just point *pm to map and do nothing. zap_map in zap.c will fill
	 * in map->map (base) details */
	struct z_sock_key *k = z_key_alloc(map);
	if (!k) {
		zerr = ZAP_ERR_RESOURCE;
		goto err1;
	}
	map->key = (uint32_t)(uint64_t)k->rb_node.key;
	*pm = (void*)map;
	goto out;
err1:
	free(map);
err0:
out:
	return zerr;
}

static zap_err_t z_sock_unmap(zap_ep_t ep, zap_map_t map)
{
	/* Just free the map */
	struct zap_sock_map *m = (void*) map;
	if (map->type == ZAP_MAP_LOCAL) {
		z_key_delete(m->key);
	}
	free(m);
	return ZAP_ERR_OK;
}

static zap_err_t z_sock_share(zap_ep_t ep, zap_map_t map,
				const char *msg, size_t msg_len)
{
	struct z_sock_ep *sep = (void*) ep;
	zap_err_t zerr;
	struct zap_sock_map *smap = (void*)map;
	struct sock_msg_rendezvous msgr;

	/* validate */
	if (ep->state != ZAP_EP_CONNECTED)
		return ZAP_ERR_NOT_CONNECTED;

	if (map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;

	/* prepare message */
	z_sock_hdr_init(&msgr.hdr, 0, SOCK_MSG_RENDEZVOUS,
			sizeof(struct sock_msg_rendezvous) + msg_len, 0);
	msgr.rmap_key = smap->key;
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
	struct z_sock_io *io = __sock_io_alloc(sep);
	zap_err_t zerr = ZAP_ERR_OK;

	if (!io)
		return ZAP_ERR_RESOURCE;

	/* validate */
	if (z_map_access_validate(src_map, src, sz, ZAP_ACCESS_READ) != 0) {
		zerr = ZAP_ERR_REMOTE_PERMISSION;
		goto err;
	}

	if (z_map_access_validate(dst_map, dst, sz, ZAP_ACCESS_NONE) != 0) {
		zerr = ZAP_ERR_LOCAL_LEN;
		goto err;
	}

	/* prepare message */
	z_sock_hdr_init(&io->read.hdr, 0, SOCK_MSG_READ_REQ,
		   sizeof(io->read), (uint64_t)context);
	struct zap_sock_map *src_smap = (void*) src_map;
	io->read.src_map_key = src_smap->key;
	io->read.src_ptr = htobe64((uint64_t) src);
	io->read.data_len = htonl((uint32_t)sz);
	io->dst_map = dst_map;
	io->dst_ptr = dst;

	pthread_mutex_lock(&sep->ep.lock);
	if (sep->ep.state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		pthread_mutex_unlock(&sep->ep.lock);
		goto err;
	}

	/* write message */
	zerr = __sock_send_msg_nolock(sep, &io->read.hdr, sizeof(io->read),
				      NULL, 0);
	if (zerr)
		goto err1;

	TAILQ_INSERT_TAIL(&sep->io_q, io, q_link);
	pthread_mutex_unlock(&sep->ep.lock);
	zerr = ZAP_ERR_OK;
	return zerr;
err1:
	pthread_mutex_unlock(&sep->ep.lock);
err:
	__sock_io_free(sep, io);
	return zerr;
}

static zap_err_t z_sock_write(zap_ep_t ep, zap_map_t src_map, char *src,
			      zap_map_t dst_map, char *dst, size_t sz,
			      void *context)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)ep;
	struct z_sock_io *io = __sock_io_alloc(sep);
	zap_err_t zerr;

	if (!io)
		return ZAP_ERR_RESOURCE;

	/* validate */
	if (z_map_access_validate(src_map, src, sz, ZAP_ACCESS_NONE) != 0) {
		zerr = ZAP_ERR_LOCAL_LEN;
		goto err0;
	}

	if (z_map_access_validate(dst_map, dst, sz, ZAP_ACCESS_WRITE) != 0) {
		zerr = ZAP_ERR_REMOTE_PERMISSION;
		goto err0;
	}

	/* prepare message */

	z_sock_hdr_init(&io->write.hdr, 0, SOCK_MSG_WRITE_REQ,
		   sizeof(io->write) + sz, (uint64_t)context);
	struct zap_sock_map *sdst_map = (void*)dst_map;
	io->write.dst_map_key = sdst_map->key;
	io->write.dst_ptr = htobe64((uint64_t) dst);
	io->write.data_len = htonl((uint32_t) sz);


	pthread_mutex_lock(&sep->ep.lock);
	if (sep->ep.state != ZAP_EP_CONNECTED) {
		zerr = ZAP_ERR_NOT_CONNECTED;
		goto err1;
	}

	/* write message */
	zerr = __sock_send_msg_nolock(sep, &io->write.hdr, sizeof(io->write),
				      src, sz);
	if (zerr) {
		goto err1;
	}

	TAILQ_INSERT_TAIL(&sep->io_q, io, q_link);
	pthread_mutex_unlock(&sep->ep.lock);
	return ZAP_ERR_OK;

err1:
	pthread_mutex_unlock(&sep->ep.lock);
err0:
	__sock_io_free(sep, io);
	return zerr;
}

zap_err_t zap_transport_get(zap_t *pz, zap_log_fn_t log_fn,
			    zap_mem_info_fn_t mem_info_fn)
{
	zap_t z;
	size_t sendrecv_sz, rendezvous_sz, hdr_sz;
	if (!init_complete && init_once())
		goto err;

	pthread_atfork(NULL, NULL, (void*)init_once);

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
	z->map = z_sock_map;
	z->unmap = z_sock_unmap;
	z->share = z_sock_share;
	z->get_name = z_get_name;

	/* is it needed? */
	z->mem_info_fn = mem_info_fn;

	*pz = z;
	return ZAP_ERR_OK;

 err:
	return ZAP_ERR_RESOURCE;
}
