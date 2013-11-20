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
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include <endian.h>

#include "zap_sock.h"

#define LOG__(ep, ...) do { \
	if (ep && ep->z && ep->z->log_fn) \
		ep->z->log_fn(__VA_ARGS__); \
} while (0);

#define LOG_(sep, ...) do { \
	if (sep && sep->ep.z && sep->ep.z->log_fn) \
		sep->ep.z->log_fn(__VA_ARGS__); \
} while(0);

int init_complete = 0;

static struct event_base *io_event_loop;
static pthread_t io_thread;

static void *io_thread_proc(void *arg);

static void sock_event(struct bufferevent *buf_event, short ev, void *arg);
static void sock_read(struct bufferevent *buf_event, void *arg);
static void sock_write(struct bufferevent *buf_event, void *arg);

static void timeout_cb(int fd , short events, void *arg);
static zap_err_t __setup_connection(struct z_sock_ep *sep);

static void release_buf_event(struct z_sock_ep *r);

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

void __convert_sock_msg_regular(struct sock_msg_hdr *hdr,
				struct __convert_fns *fns)
{
	struct sock_msg_regular *msg = (void*) hdr;
	__APPLY(msg->data_len, fns->u32);
}

void __convert_sock_msg_read_req(struct sock_msg_hdr *hdr,
				 struct __convert_fns *fns)
{
	struct sock_msg_read_req *msg = (void*) hdr;
	__APPLY(msg->ctxt, fns->u64);
	__APPLY(msg->src_map_ref, fns->u64);
	__APPLY(msg->src_ptr, fns->u64);
	__APPLY(msg->dst_map_ref, fns->u64);
	__APPLY(msg->dst_ptr, fns->u64);
	__APPLY(msg->data_len, fns->u32);
}

void __convert_sock_msg_read_resp(struct sock_msg_hdr *hdr,
				  struct __convert_fns *fns)
{
	struct sock_msg_read_resp *msg = (void*) hdr;
	__APPLY(msg->status, fns->u16);
	__APPLY(msg->ctxt, fns->u64);
	__APPLY(msg->dst_ptr, fns->u64);
	__APPLY(msg->data_len, fns->u32);
}

void __convert_sock_msg_write_req(struct sock_msg_hdr *hdr,
				  struct __convert_fns *fns)
{
	struct sock_msg_write_req *msg = (void*) hdr;
	__APPLY(msg->dst_map_ref, fns->u64);
	__APPLY(msg->dst_ptr, fns->u64);
	__APPLY(msg->ctxt, fns->u64);
	__APPLY(msg->data_len, fns->u32);
}

void __convert_sock_msg_write_resp(struct sock_msg_hdr *hdr,
				   struct __convert_fns *fns)
{
	struct sock_msg_write_resp *msg = (void*) hdr;
	__APPLY(msg->status, fns->u16);
	__APPLY(msg->ctxt, fns->u64);
}

void __convert_sock_msg_rendezvous(struct sock_msg_hdr *hdr,
				   struct __convert_fns *fns)
{
	struct sock_msg_rendezvous *msg = (void*) hdr;
	__APPLY(msg->rmap_ref, fns->u64);
	__APPLY(msg->acc, fns->u32);
	__APPLY(msg->addr, fns->u64);
	__APPLY(msg->data_len, fns->u32);
}

typedef void (*__convert_fn_t)(struct sock_msg_hdr*, struct __convert_fns*);
__convert_fn_t __sock_msg_convert_fn[] = {
	[SOCK_MSG_REGULAR] = __convert_sock_msg_regular,
	[SOCK_MSG_RENDEZVOUS] = __convert_sock_msg_rendezvous,
	[SOCK_MSG_READ_REQ] = __convert_sock_msg_read_req,
	[SOCK_MSG_READ_RESP] = __convert_sock_msg_read_resp,
	[SOCK_MSG_WRITE_REQ] = __convert_sock_msg_write_req,
	[SOCK_MSG_WRITE_RESP] = __convert_sock_msg_write_resp,
};

void hton_sock_msg(struct sock_msg_hdr *hdr)
{
	__sock_msg_convert_fn[hdr->msg_type](hdr, &__hton_fns);
	__APPLY(hdr->msg_type, htons);
	__APPLY(hdr->msg_len, htonl);
}

void ntoh_sock_msg(struct sock_msg_hdr *hdr)
{
	__APPLY(hdr->msg_type, ntohs);
	__APPLY(hdr->msg_len, ntohl);
	__sock_msg_convert_fn[hdr->msg_type](hdr, &__ntoh_fns);
}

/*** (END) Host-Network conversion utilities ***/


void z_sock_cleanup(void)
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

static zap_err_t z_sock_close(zap_ep_t ep)
{
	struct z_sock_ep *sep = (void*)ep;

	pthread_mutex_lock(&sep->ep.lock);
	switch (sep->ep.state) {
	case ZAP_EP_CONNECTED:
		sep->ep.state = ZAP_EP_CLOSE;
		shutdown(sep->sock, SHUT_RDWR);
		break;
	case ZAP_EP_LISTENING:
	case ZAP_EP_PEER_CLOSE:
		close(sep->sock);
		sep->sock = 0;
		sep->ep.state = ZAP_EP_ERROR;
		break;
	}
	pthread_mutex_unlock(&sep->ep.lock);
	zap_put_ep(&sep->ep);
	return ZAP_ERR_OK;
}

static zap_err_t z_get_name(zap_ep_t ep, struct sockaddr *local_sa,
			    struct sockaddr *remote_sa, socklen_t *sa_len)
{
	struct z_sock_ep *sep = (void*)ep;
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
	return errno2zaperr(errno);
}

static zap_err_t z_sock_connect(zap_ep_t ep,
				struct sockaddr *sa, socklen_t sa_len)
{
	int rc;
	zap_err_t zerr;
	struct z_sock_ep *sep = (void*)ep;
	zerr = zap_ep_change_state(&sep->ep, ZAP_EP_INIT, ZAP_EP_CONNECTING);
	if (zerr)
		goto out;

	sep->sock = socket(sa->sa_family, SOCK_STREAM, 0);
	if (sep->sock == -1) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}
	rc = evutil_make_socket_nonblocking(sep->sock);
	if (rc) {
		zerr = ZAP_ERR_RESOURCE;
		goto out;
	}
	zerr = __setup_connection(sep);
	if (zerr)
		goto out;

	zap_get_ep(&sep->ep);
	if (bufferevent_socket_connect(sep->buf_event, sa, sa_len)) {
		/* Error starting connection */
		bufferevent_free(sep->buf_event);
		sep->buf_event = NULL;
		zerr = ZAP_ERR_CONNECT;
		zap_put_ep(&sep->ep);
		goto out;
	}

 out:
	return zerr;
}

static void sock_write(struct bufferevent *buf_event, void *arg)
{
	/* Do nothing */
}

/**
 * Process an unknown message in the end point.
 */
static void process_sep_msg_unknown(struct z_sock_ep *sep)
{
	/* Decide what to do and IMPLEMENT ME */
}

/**
 * Receiving a regular message.
 */
static void process_sep_msg_regular(struct z_sock_ep *sep)
{
	struct sock_msg_regular msg;
	bufferevent_read(sep->buf_event, &msg, sizeof(msg));
	ntoh_sock_msg((void*)&msg);
	char *data = malloc(msg.data_len);
	if (!data) {
		LOG_(sep, "ENOMEM at %s in %s:%d\n",
		     __func__, __FILE__, __LINE__);
		return;
	}
	if (msg.data_len)
		bufferevent_read(sep->buf_event, data, msg.data_len);
	struct zap_event ev = {
		.type = ZAP_EVENT_RECV_COMPLETE,
		.data = data,
		.data_len = msg.data_len,
	};
	sep->ep.cb((void*)sep, &ev);
	free(data);
}

/**
 * Receiving a read request message.
 */
static void process_sep_msg_read_req(struct z_sock_ep *sep)
{
	/* unpack received message */
	struct sock_msg_read_req msg;
	bufferevent_read(sep->buf_event, &msg, sizeof(msg));
	ntoh_sock_msg((void*)&msg);
	struct zap_sock_map *map = (void*) msg.src_map_ref;
	void *ptr = (void*) msg.src_ptr;

	/* preparing response message */
	struct sock_msg_read_resp rmsg;
	rmsg.hdr.msg_type = htons(SOCK_MSG_READ_RESP);
	rmsg.dst_ptr = htobe64(msg.dst_ptr);
	rmsg.ctxt = htobe64(msg.ctxt);

	int rc = __z_map_access_validate((void*)map, ptr, msg.data_len,
							ZAP_ACCESS_READ);
	switch (rc) {
	case 0:
		/* OK */
		rmsg.status = 0;
		rmsg.data_len = htonl(msg.data_len);
		break;
	case EACCES:
		rmsg.status = htons(ZAP_ERR_REMOTE_PERMISSION);
		rmsg.data_len = 0;
		break;
	case ERANGE:
		rmsg.status = htons(ZAP_ERR_REMOTE_LEN);
		rmsg.data_len = 0;
		break;
	}

	struct evbuffer *ebuf = evbuffer_new();
	if (!ebuf)
		goto res_err;

	rmsg.hdr.msg_len = htonl(sizeof(rmsg) + msg.data_len);
	if (evbuffer_add(ebuf, &rmsg, sizeof(rmsg)) != 0)
		goto res_err;
	if (rmsg.data_len &&
			evbuffer_add(ebuf, ptr, msg.data_len) != 0)
		goto res_err;
	if (bufferevent_write_buffer(sep->buf_event, ebuf) != 0)
		LOG_(sep, "bufferevent_write_buffer error in %s at %s:%d\n",
						__func__, __FILE__, __LINE__);
	evbuffer_free(ebuf);
	return;
res_err:
	if (ebuf)
		evbuffer_free(ebuf);
	rmsg.status = htons(ZAP_ERR_RESOURCE);
	rmsg.data_len = 0;
	rmsg.hdr.msg_len = htonl(sizeof(rmsg) + msg.data_len);
	if (bufferevent_write(sep->buf_event, &rmsg, sizeof(rmsg)) != 0)
		LOG_(sep, "bufferevent_write error in %s at %s:%d\n",
						__func__, __FILE__, __LINE__);
}

/**
 * Receiving a read response message.
 */
static void process_sep_msg_read_resp(struct z_sock_ep *sep)
{
	/* Unpack the message */
	struct sock_msg_read_resp msg;
	bufferevent_read(sep->buf_event, &msg, sizeof(msg));
	ntoh_sock_msg((void*)&msg);
	void *ptr = (void*) msg.dst_ptr;

	struct zap_event ev = {
		.type = ZAP_EVENT_READ_COMPLETE,
		.status = msg.status,
		.context = (void*) msg.ctxt
	};

	if (msg.status == 0)
		/* put the read data into the memory region */
		bufferevent_read(sep->buf_event, ptr, msg.data_len);

	sep->ep.cb((void*)sep, &ev);
}

/**
 * Receiving a write request message.
 */
static void process_sep_msg_write_req(struct z_sock_ep *sep)
{
	/* Unpack the message */
	struct sock_msg_write_req msg;
	bufferevent_read(sep->buf_event, &msg, sizeof(msg));
	ntoh_sock_msg((void*)&msg);
	struct zap_sock_map *map = (void*) msg.dst_map_ref;
	void *ptr = (void*) msg.dst_ptr;

	/* Prepare the response message */
	struct sock_msg_write_resp rmsg;
	rmsg.hdr.msg_type = htons(SOCK_MSG_WRITE_RESP);
	rmsg.hdr.msg_len = htonl(sizeof(rmsg));
	rmsg.ctxt = htobe64(msg.ctxt);

	/* Validate */
	int rc = __z_map_access_validate((void*)map, ptr, msg.data_len,
							ZAP_ACCESS_WRITE);
	switch (rc) {
	case 0: /* OK */
		rmsg.status = 0;
		rc = bufferevent_read(sep->buf_event, ptr, msg.data_len);
		if (rc)
			rmsg.status = htons(ZAP_ERR_RESOURCE);
		break;
	case EACCES:
		rmsg.status = htons(ZAP_ERR_REMOTE_PERMISSION);
		break;
	case ERANGE:
		rmsg.status = htons(ZAP_ERR_REMOTE_LEN);
		break;
	}

	bufferevent_write(sep->buf_event, &rmsg, sizeof(rmsg));
}

/**
 * Receiving a write response message.
 */
static void process_sep_msg_write_resp(struct z_sock_ep *sep)
{
	struct sock_msg_write_resp msg;
	bufferevent_read(sep->buf_event, &msg, sizeof(msg));
	ntoh_sock_msg((void*)&msg);

	struct zap_event ev = {
		.type = ZAP_EVENT_WRITE_COMPLETE,
		.status = msg.status,
		.context = (void*) msg.ctxt
	};

	sep->ep.cb(&sep->ep, &ev);
}

/**
 * Receiving a rendezvous (share) message.
 */
static void process_sep_msg_rendezvous(struct z_sock_ep *sep)
{
	struct sock_msg_rendezvous msg;
	bufferevent_read(sep->buf_event, &msg, sizeof(msg));
	ntoh_sock_msg((void*)&msg);
	struct zap_sock_map *map = calloc(1, sizeof(*map));
	if (!map) {
		LOG_(sep, "ENOMEM in %s at %s:%d\n",
				__func__, __FILE__, __LINE__);
		return;
	}

	map->rmap_ref = msg.rmap_ref;
	map->map.ep = (void*)sep;
	map->map.acc = msg.acc;
	map->map.type = ZAP_MAP_REMOTE;
	map->map.addr = (void*)msg.addr;
	map->map.len = msg.data_len;

	struct zap_event ev = {
		.type = ZAP_EVENT_RENDEZVOUS,
		.map = (void*)map
	};
	sep->ep.cb((void*)sep, &ev);
}

static void process_sep_msg_accepted(struct z_sock_ep *sep)
{
	struct sock_msg_accepted msg;
	struct zap_event ev = {
		.type = ZAP_EVENT_CONNECTED
	};
	bufferevent_read(sep->buf_event, &msg, sizeof(msg));
	assert(sep->ep.state == ZAP_EP_CONNECTING);
	if (!zap_ep_change_state(&sep->ep, ZAP_EP_CONNECTING, ZAP_EP_CONNECTED))
		sep->ep.cb((void*)sep, &ev);
	else
		LOG_(sep, "'Accept' message received in unexpected state %d.\n",
		     sep->ep.state);
}

typedef void(*process_sep_msg_fn_t)(struct z_sock_ep*);
process_sep_msg_fn_t process_sep_msg_fns[] = {
	[SOCK_MSG_REGULAR] = process_sep_msg_regular,
	[SOCK_MSG_READ_REQ] = process_sep_msg_read_req,
	[SOCK_MSG_READ_RESP] = process_sep_msg_read_resp,
	[SOCK_MSG_WRITE_REQ] = process_sep_msg_write_req,
	[SOCK_MSG_WRITE_RESP] = process_sep_msg_write_resp,
	[SOCK_MSG_RENDEZVOUS] = process_sep_msg_rendezvous,
	[SOCK_MSG_ACCEPTED] = process_sep_msg_accepted,
};

#define min_t(t, x, y) (t)((t)x < (t)y?(t)x:(t)y)
static void sock_read(struct bufferevent *buf_event, void *arg)
{
	struct z_sock_ep *sep = (struct z_sock_ep *)arg;
	struct evbuffer *evb;
	struct sock_msg_hdr hdr;
	struct ldms_request *req;
	size_t len;
	size_t reqlen;
	size_t buflen;
	enum sock_msg_type msg_type;
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
		if (msg_type < SOCK_MSG_TYPE_LAST)
			process_sep_msg_fns[msg_type](sep);
		else /* unknown type */
			process_sep_msg_unknown(sep);

	} while (1);
}

static void *io_thread_proc(void *arg)
{
	event_base_dispatch(io_event_loop);
	return NULL;
}

static void release_buf_event(struct z_sock_ep *sep)
{
	if (sep->listen_ev) {
		evconnlistener_free(sep->listen_ev);
		sep->listen_ev = NULL;
	}
	if (sep->buf_event) {
		bufferevent_free(sep->buf_event);
		sep->buf_event = NULL;
	}
}

static void sock_event(struct bufferevent *buf_event, short bev, void *arg)
{

	struct z_sock_ep *sep = arg;
	pthread_mutex_lock(&sep->ep.lock);
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
		if (bufferevent_enable(sep->buf_event, EV_READ | EV_WRITE)) {
			LOG_(sep, "Error enabling buffered I/O event for fd %d.\n",
					sep->sock);
		}
		pthread_mutex_unlock(&sep->ep.lock);
		return;
	}

	/* Reaching here means bev is one of the EOF, ERROR or TIMEOUT */

	struct zap_event ev = { 0 };

	release_buf_event(sep);

	switch (sep->ep.state) {
		case ZAP_EP_CONNECTING:
			if ((bev & bev_mask) == BEV_EVENT_EOF)
				ev.type = ZAP_EVENT_REJECTED;
			else
				ev.type = ZAP_EVENT_CONNECT_ERROR;
			sep->ep.state = ZAP_EP_ERROR;
			break;
		case ZAP_EP_CONNECTED:
			/* Peer close (passive close) */
			ev.type = ZAP_EVENT_DISCONNECTED;
			sep->ep.state = ZAP_EP_PEER_CLOSE;
			break;
		case ZAP_EP_CLOSE:
			/* Active close */
			close(sep->sock);
			sep->sock = -1;
			ev.type = ZAP_EVENT_DISCONNECTED;
			sep->ep.state = ZAP_EP_ERROR;
			break;
		default:
			LOG_(sep, "Unexpected state for EOF %d.\n",
					sep->ep.state);
			sep->ep.state = ZAP_EP_ERROR;
			break;
	}
	pthread_mutex_unlock(&sep->ep.lock);
	sep->ep.cb((void*)sep, &ev);
	zap_put_ep(&sep->ep);
}

static zap_err_t
__setup_connection(struct z_sock_ep *sep)
{
#ifdef DEBUG
	sep->ep.z->log_fn("SOCK: setting up endpoint %p\n", &sep->ep);
#endif
	/* Initialize send and recv I/O events */
	sep->buf_event = bufferevent_socket_new(io_event_loop, sep->sock,
						BEV_OPT_THREADSAFE);
	if(!sep->buf_event) {
		LOG_(sep, "Error initializing buffered I/O event for "
		     "fd %d.\n", sep->sock);
		return ZAP_ERR_RESOURCE;
	}

	bufferevent_setcb(sep->buf_event, sock_read, NULL, sock_event, sep);
	return ZAP_ERR_OK;
}

/**
 * This is a callback function for evconnlistener_new_bind (in z_sock_listen).
 */
static void __z_sock_conn_request(struct evconnlistener *listener,
			 evutil_socket_t sockfd,
			 struct sockaddr *address, int socklen, void *arg)
{
	struct z_sock_ep *sep = arg;
	zap_ep_t new_ep;
	struct z_sock_ep *new_sep;
	zap_err_t zerr;

	zerr = zap_new(sep->ep.z, &new_ep, sep->ep.cb);
	if (zerr) {
		LOG_(sep, "Zap Error %d (%s): in %s at %s:%d\n",
				zerr, zap_err_str(zerr) , __func__, __FILE__,
				__LINE__);
		return;
	}
	void *uctxt = zap_get_ucontext(&sep->ep);
	zap_set_ucontext(new_ep, uctxt);
	new_sep = (void*) new_ep;
	new_sep->sock = sockfd;
	new_sep->ep.state = ZAP_EP_CONNECTING;
	/* new_sep->buf_event will be set in accept */
	struct zap_event zev;
	memset(&zev, 0, sizeof(zev));
	zev.type = ZAP_EVENT_CONNECT_REQUEST;
	/* The callback will decide whether it should accept this connection */
	new_ep->cb(new_ep, &zev);
}

static zap_err_t z_sock_listen(zap_ep_t ep, struct sockaddr *sa,
				socklen_t sa_len)
{
	struct z_sock_ep *sep = (void*)ep;
	zap_err_t zerr;

	zerr = zap_ep_change_state(&sep->ep, ZAP_EP_INIT, ZAP_EP_LISTENING);
	if (zerr)
		goto err_0;

	zerr = ZAP_ERR_RESOURCE;
	sep->listen_ev = evconnlistener_new_bind(io_event_loop,
					       __z_sock_conn_request, sep,
					       LEV_OPT_THREADSAFE |
					       LEV_OPT_REUSEABLE, 1024, sa,
					       sa_len);
	if (!sep->listen_ev)
		goto err_0;

	sep->sock = evconnlistener_get_fd(sep->listen_ev);
	return ZAP_ERR_OK;

 err_0:
	z_sock_close(ep);
	zap_put_ep(ep);
	return zerr;
}

static zap_err_t z_sock_send(zap_ep_t ep, void *buf, size_t len)
{
	struct z_sock_ep *sep = (void*)ep;
	int rc;

	if (ep->state != ZAP_EP_CONNECTED)
		return ZAP_ERR_NOT_CONNECTED;

	/* create ebuf for message */
	struct evbuffer *ebuf = evbuffer_new();
	if (!ebuf)
		return ZAP_ERR_RESOURCE;

	struct sock_msg_regular msg;
	msg.hdr.msg_type = htons(SOCK_MSG_REGULAR);
	msg.hdr.msg_len =  htonl((uint32_t)(sizeof(msg) + len));

	msg.data_len = htonl(len);

	if (evbuffer_add(ebuf, &msg, sizeof(msg)) != 0)
		goto err;
	if (evbuffer_add(ebuf, buf, len) != 0)
		goto err;

	/* this write will drain ebuf, appending data to sep->buf_event
	 * without unnecessary memory copying. */
	if (bufferevent_write_buffer(sep->buf_event, ebuf) != 0)
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

int init_once()
{
	int rc = ENOMEM;

	evthread_use_pthreads();
	io_event_loop = event_base_new();
	if (!io_event_loop)
		return errno;

	keepalive = evtimer_new(io_event_loop, timeout_cb, NULL);
	if (!keepalive)
		goto err_1;

	to.tv_sec = 1;
	to.tv_usec = 0;
	evtimer_add(keepalive, &to);

	rc = pthread_create(&io_thread, NULL, io_thread_proc, 0);
	if (rc)
		goto err_1;

	init_complete = 1;
	//atexit(z_sock_cleanup);
	return 0;

 err_1:
	event_base_free(io_event_loop);
	return rc;
}

zap_err_t z_sock_new(zap_t z, zap_ep_t *pep, zap_cb_fn_t cb)
{
	struct z_sock_ep *sep = calloc(1, sizeof(*sep));
	if (!sep)
		return ZAP_ERR_RESOURCE;
	*pep = (void*)sep;
	/* buf_event, listen_ev and sock will be created in connect, listen
	 * or accept. */
	return 0;
}

static void z_sock_destroy(zap_ep_t ep)
{
	struct z_sock_ep *sep = (void*)ep;
	release_buf_event(sep);
	if (sep->sock > -1)
		close(sep->sock);
#ifdef DEBUG
	ep->z->log_fn("SOCK: destroying endpoint %p\n", ep);
#endif
	free(ep);
}

zap_err_t z_sock_accept(zap_ep_t ep, zap_cb_fn_t cb)
{
	/* ep is the newly created ep from __z_sock_conn_request */
	struct z_sock_ep *sep = (void*)ep;
	struct zap_event ev;
	int rc;
	zap_err_t zerr;
	struct sock_msg_accepted msg;

	pthread_mutex_lock(&sep->ep.lock);
	if (sep->ep.state != ZAP_EP_CONNECTING) {
		zerr = ZAP_ERR_ENDPOINT;
		goto err_0;
	}

	sep->ep.cb = cb;
	zerr = __setup_connection(sep);
	if (zerr)
		goto err_1;

	if (bufferevent_enable(sep->buf_event, EV_READ | EV_WRITE)) {
		LOG_(sep, "Error enabling buffered I/O event for fd %d.\n",
		     sep->sock);
		goto err_1;
	}

	msg.hdr.msg_type = htons(SOCK_MSG_ACCEPTED);
	msg.hdr.msg_len = htonl(sizeof(msg));

	/* use blocking write here to synchronously send ACCEPTED message
	 * to the peer. Otherwise, the message might be pending (very shortly)
	 * and we might get BROKEN_PIPE error if we try to immediately close it
	 * after CONNECTED. In this situation, the peer will get REJECTED event
	 * instead of CONNECTED and DISCONNECTED. Synchronous send of ACCEPTED
	 * message helps preventing this. */
	rc = write(sep->sock, &msg, sizeof(msg));
	if (rc != sizeof(msg)) {
		LOG_(sep, "Cannot send SOCK_MSG_ACCEPTED to peer.");
		zerr = ZAP_ERR_CONNECT;
		goto err_1;
	}

	sep->ep.state = ZAP_EP_CONNECTED;
	pthread_mutex_unlock(&sep->ep.lock);
	zap_get_ep(&sep->ep);
	ev.type = ZAP_EVENT_CONNECTED;
	cb(ep, &ev);
	return ZAP_ERR_OK;

 err_1:
	sep->ep.state = ZAP_EP_ERROR;
	pthread_mutex_unlock(&sep->ep.lock);
 err_0:
	return zerr;
}

static zap_err_t z_sock_reject(zap_ep_t ep)
{
	zap_put_ep(ep);
	return ZAP_ERR_OK;
}

static zap_err_t
z_sock_map(zap_ep_t ep, zap_map_t *pm, void *buf, size_t len, zap_access_t acc)
{
	struct zap_sock_map *map = calloc(1, sizeof(*map));
	if (!map)
		return ZAP_ERR_RESOURCE;
	/* Just point *pm to map and do nothing. zap_map in zap.c will fill
	 * in map->map (base) details */
	*pm = (void*)map;
	return ZAP_ERR_OK;
}

static zap_err_t z_sock_unmap(zap_ep_t ep, zap_map_t map)
{
	/* Just free the map */
	free(map);
	return ZAP_ERR_OK;
}

static zap_err_t z_sock_share(zap_ep_t ep, zap_map_t map, uint64_t ctxt)
{

	/* validate */
	if (ep->state != ZAP_EP_CONNECTED)
		return ZAP_ERR_NOT_CONNECTED;

	if (map->type != ZAP_MAP_LOCAL)
		return ZAP_ERR_INVALID_MAP_TYPE;

	/* prepare message */
	struct sock_msg_rendezvous msg;
	msg.hdr.msg_type = htons(SOCK_MSG_RENDEZVOUS);
	msg.hdr.msg_len = htonl(sizeof(msg));
	msg.rmap_ref = htobe64((uint64_t)map);
	msg.acc = htonl(map->acc);
	msg.addr = htobe64((uint64_t)map->addr);
	msg.data_len = htonl(map->len);

	/* write message */
	struct z_sock_ep *sep = (void*) ep;
	if (bufferevent_write(sep->buf_event, &msg, sizeof(msg)) != 0)
		return ZAP_ERR_RESOURCE;
	return ZAP_ERR_OK;
}

static zap_err_t z_sock_read(zap_ep_t ep, zap_map_t src_map, void *src,
			     zap_map_t dst_map, void *dst, size_t sz,
			     void *context)
{
	/* validate */
	if (__z_map_access_validate(src_map, src, sz, ZAP_ACCESS_READ) != 0)
		return ZAP_ERR_REMOTE_PERMISSION;
	if (__z_map_access_validate(dst_map, dst, sz, ZAP_ACCESS_NONE) != 0)
		return ZAP_ERR_LOCAL_LEN;

	/* prepare message */
	struct zap_sock_map *src_smap = (void*) src_map;
	struct sock_msg_read_req msg;
	msg.hdr.msg_type = htons(SOCK_MSG_READ_REQ);
	msg.hdr.msg_len = htonl(sizeof(msg));
	msg.ctxt = htobe64((uint64_t) context);
	msg.src_map_ref = htobe64(src_smap->rmap_ref);
	msg.src_ptr = htobe64((uint64_t) src);
	msg.dst_map_ref = htobe64((uint64_t) dst_map);
	msg.dst_ptr = htobe64((uint64_t) dst);
	msg.data_len = htonl((uint32_t)sz);

	/* write message */
	struct z_sock_ep *sep = (void*)ep;
	if (bufferevent_write(sep->buf_event, &msg, sizeof(msg)) != 0)
		return ZAP_ERR_RESOURCE;
	return ZAP_ERR_OK;
}

static zap_err_t z_sock_write(zap_ep_t ep, zap_map_t src_map, void *src,
			      zap_map_t dst_map, void *dst, size_t sz,
			      void *context)
{
	/* validate */
	if (__z_map_access_validate(src_map, src, sz, ZAP_ACCESS_NONE) != 0)
		return ZAP_ERR_LOCAL_LEN;
	if (__z_map_access_validate(dst_map, dst, sz, ZAP_ACCESS_WRITE) != 0)
		return ZAP_ERR_REMOTE_PERMISSION;

	/* prepare message */
	struct evbuffer *ebuf = evbuffer_new();
	if (!ebuf)
		return ZAP_ERR_RESOURCE;
	struct zap_sock_map *sdst_map = (void*)dst_map;
	struct sock_msg_write_req msg;
	msg.hdr.msg_type = htons(SOCK_MSG_WRITE_REQ);
	msg.hdr.msg_len = htonl((uint32_t)(sizeof(msg)+sz));
	msg.ctxt = htobe64((uint64_t) context);
	msg.dst_map_ref = htobe64(sdst_map->rmap_ref);
	msg.dst_ptr = htobe64((uint64_t) dst);
	msg.data_len = htonl((uint32_t) sz);
	if (evbuffer_add(ebuf, &msg, sizeof(msg)) != 0)
		goto err;
	if (evbuffer_add(ebuf, src, sz) != 0)
		goto err;

	/* write message */
	struct z_sock_ep *sep = (void*) ep;
	if (bufferevent_write_buffer(sep->buf_event, ebuf) != 0)
		goto err;

	evbuffer_free(ebuf);
	return ZAP_ERR_OK;

err:
	evbuffer_free(ebuf);
	return ZAP_ERR_RESOURCE;
}

zap_err_t zap_transport_get(zap_t *pz, zap_log_fn_t log_fn,
			    zap_mem_info_fn_t mem_info_fn)
{
	zap_t z;

	if (!init_complete && init_once())
		goto err;

	z = calloc(1, sizeof (*z));
	if (!z)
		goto err;

	/* max_msg is unused (since RDMA) ... */
	z->max_msg = (1024 * 1024) - sizeof(struct sock_msg_hdr);
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
