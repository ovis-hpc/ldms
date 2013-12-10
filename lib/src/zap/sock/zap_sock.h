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
 * Author: Narate Taerat <narate@ogc.us>
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

#include "zap.h"
#include "zap_priv.h"

struct zap_sock_map {
	struct zap_map map;
	uint64_t rmap_ref; /**< Remote map reference. This is used only in
			     *   case that \c map is a remote map.
			     */
};

#define SOCK_MSG_TYPE_LIST(ZAP_WRAP) \
	ZAP_WRAP(SOCK_MSG_REGULAR),     /*  Regular     send-receive  */  \
	ZAP_WRAP(SOCK_MSG_RENDEZVOUS),  /*  Share       zap_map       */  \
	ZAP_WRAP(SOCK_MSG_READ_REQ),    /*  Read        request       */  \
	ZAP_WRAP(SOCK_MSG_READ_RESP),   /*  Read        response      */  \
	ZAP_WRAP(SOCK_MSG_WRITE_REQ),   /*  Write       request       */  \
	ZAP_WRAP(SOCK_MSG_WRITE_RESP),  /*  Write       response      */  \
	ZAP_WRAP(SOCK_MSG_ACCEPTED),    /*  Connection  accepted      */  \
	ZAP_WRAP(SOCK_MSG_TYPE_LAST)

static const char *__sock_msg_type_str[] = {
	SOCK_MSG_TYPE_LIST(ZAP_STR_WRAP)
};

enum sock_msg_type {
	SOCK_MSG_TYPE_LIST(ZAP_ENUM_WRAP)
};

#pragma pack(4)

/**
 * \brief Zap message header for socket transport.
 *
 * Each of the sock_msg's is an extension to ::sock_msg_hdr.
 */
struct sock_msg_hdr {
	uint16_t msg_type;
	uint32_t msg_len; /** Length of the entire message, header included. */
};

/**
 * Regular message.
 */
struct sock_msg_regular {
	struct sock_msg_hdr hdr;
	uint32_t data_len;
	char data[FLEXIBLE_ARRAY_MEMBER];
};

/**
 * Read request (src_addr --> dst_addr)
 */
struct sock_msg_read_req {
	struct sock_msg_hdr hdr;
	uint64_t ctxt; /**< User context */
	uint64_t src_map_ref; /**< Source map reference (on non-initiator) */
	uint64_t src_ptr; /**< Source memory */
	uint64_t dst_map_ref; /**< Destination map reference */
	uint64_t dst_ptr; /**< Destination memory addr (on initiator) */
	uint32_t data_len; /**< Data length */
};

/**
 * Read response
 */
struct sock_msg_read_resp {
	struct sock_msg_hdr hdr;
	uint16_t status; /**< Return status */
	uint64_t ctxt; /**< User context */
	uint64_t dst_ptr; /**< Destination memory addr (on initiator) */
	uint32_t data_len; /**< Response data length */
	char data[FLEXIBLE_ARRAY_MEMBER]; /**< Response data */
};

/**
 * Write request
 */
struct sock_msg_write_req {
	struct sock_msg_hdr hdr;
	uint64_t ctxt; /**< User context */
	uint64_t dst_map_ref; /**< Destination map reference */
	uint64_t dst_ptr; /**< Destination address */
	uint32_t data_len; /**< Data length */
	char data[FLEXIBLE_ARRAY_MEMBER]; /**< data for SOCK_MSG_WRITE_REQ */
};

/**
 * Write response
 */
struct sock_msg_write_resp {
	struct sock_msg_hdr hdr;
	uint64_t ctxt; /**< User context */
	uint16_t status; /**< Return status */
};

/**
 * Message for exporting/sharing zap_map.
 */
struct sock_msg_rendezvous {
	struct sock_msg_hdr hdr;
	uint64_t rmap_ref; /**< Remote map reference */
	uint32_t acc; /**< Access */
	uint64_t addr; /**< Address in the map */
	uint32_t data_len; /**< Length */
};

/**
 * Message for connection accepted.
 */
struct sock_msg_accepted {
	struct sock_msg_hdr hdr;
};

#pragma pack()

struct z_sock_ep {
	struct zap_ep ep;

	int sock;
	struct bufferevent *buf_event;
	struct evconnlistener *listen_ev;

	LIST_ENTRY(z_sock_ep) link;
};

static inline struct z_sock_ep *z_sock_from_ep(zap_ep_t *ep)
{
	return (struct z_sock_ep *)ep;
}

#endif
