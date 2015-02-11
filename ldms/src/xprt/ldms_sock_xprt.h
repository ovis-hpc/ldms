/*
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
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#ifndef __LDMS_XPRT_SOCK_H__
#define __LDMS_XPRT_SOCK_H__
#include <semaphore.h>
#include <sys/queue.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/thread.h>

/*
 * This structure is provided to the client in lookup and returned by
 * the client in the update request.
 */
struct sock_key {
	uint32_t key;
	uint32_t size;		/* size of the buffer */
	void *buf;		/* data buffer for this key */
	struct rbn rb_node;
};

#pragma pack(4)
struct sock_buf_remote_data {
	uint32_t rkey;		/* remote key */
	uint32_t lkey;		/* local key */
	uint32_t size;
};

struct sock_buf_xprt_data {
	struct sock_buf_remote_data meta;
	struct sock_buf_remote_data data;
};

#define SOCK_READ_REQ_CMD (LDMS_CMD_XPRT_PRIVATE | 0x1)
#define SOCK_READ_RSP_CMD (LDMS_CMD_XPRT_PRIVATE | 0x2)

struct sock_read_req {
	struct ldms_request_hdr hdr;
	struct sock_buf_remote_data buf_info;
};
struct sock_read_rsp {
	struct ldms_request_hdr hdr;
	struct sock_buf_remote_data buf_info;
	uint32_t status;
};
#pragma pack()

enum sock_conn_status {
	CONN_ERROR = -1,
	CONN_IDLE = 0,
	CONN_CONNECTING,
	CONN_CONNECTED,
	CONN_CLOSING,
	CONN_CLOSED
};

/**
 * SOCK Transport private data
 */
struct ldms_sock_xprt {
	struct ldms_xprt *xprt;
	enum sock_conn_status conn_status;

	enum ldms_sock_xprt_type {
		LDMS_SOCK_PASSIVE,
		LDMS_SOCK_ACTIVE
	} type;

	int sock;
	struct bufferevent *buf_event;
	struct evconnlistener *listen_ev;

	LIST_ENTRY(ldms_sock_xprt) client_link;
};

extern int sock_register(struct sockaddr *s, struct ldms_sock_xprt *x);

#endif
