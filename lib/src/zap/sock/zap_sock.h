/**
 * Copyright (c) 2010-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2020 Open Grid Computing, Inc. All rights reserved.
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

#ifndef __LDMS_XPRT_SOCK_H__
#define __LDMS_XPRT_SOCK_H__
#include <semaphore.h>
#include <sys/queue.h>
#include <sys/epoll.h>
#include "ovis-ldms-config.h"
#include "coll/rbt.h"
#include "zap.h"
#include "zap_priv.h"

#define SOCKBUF_SZ 1024 * 1024

/**
 * \brief Value for TCP_KEEPIDLE option for initiator side socket.
 *
 * This will make Linux send 'keep alive' probe when the socket being idle for
 * 10 seconds (only for initiator side zap connections).
 *
 * \note Default value of TCP_KEEPIDLE is 7200 sec = 2 hrs
 */
#define ZAP_SOCK_KEEPIDLE 10

/**
 * \brief Value for TCP_KEEPCNT option for initiator side socket.
 *
 * For this setting, a connection will be dropped after 3 probes.
 *
 * \note Default TCP_KEEPCNT is 9
 */
#define ZAP_SOCK_KEEPCNT 3

/**
 * \brief Value for TCP_KEEPINTVL option for initiator side socket.
 *
 * This is a time between probes after idle (set to 2 seconds).
 *
 * \note Default TCP_KEEPINTVL is 75 seconds
 */
#define ZAP_SOCK_KEEPINTVL 2

struct z_sock_key {
	struct rbn rb_node;
	struct zap_map *map; /**< reference to zap_map */
};

#define SOCK_MAP_KEY_GET(map) ((uint32_t)(uint64_t)((map)->mr[ZAP_SOCK]))
#define SOCK_MAP_KEY_SET(map, key) (map)->mr[ZAP_SOCK] = (void*)(uint64_t)(key)

typedef enum sock_msg_type {
	SOCK_MSG_CONNECT = 1,     /*  Connect     data          */
	SOCK_MSG_SENDRECV,    /*  send-receive  */
	SOCK_MSG_RENDEZVOUS,  /*  Share       zap_map       */
	SOCK_MSG_READ_REQ,    /*  Read        request       */
	SOCK_MSG_READ_RESP,   /*  Read        response      */
	SOCK_MSG_WRITE_REQ,   /*  Write       request       */
	SOCK_MSG_WRITE_RESP,  /*  Write       response      */
	SOCK_MSG_ACCEPTED,    /*  Connection  accepted      */
	SOCK_MSG_REJECTED,    /*  Reject      data */
	SOCK_MSG_ACK_ACCEPTED,/*  Acknowledge accepted msg  */
	SOCK_MSG_TYPE_LAST,   /*  Range limiter, upper  */
	SOCK_MSG_FIRST = SOCK_MSG_CONNECT /* Range limiter, lower */
} sock_msg_type_t;;

static const char *__sock_msg_type_str[SOCK_MSG_TYPE_LAST] = {
	[0]     =  "SOCK_MSG_INVALID",
	[SOCK_MSG_CONNECT]     =  "SOCK_MSG_CONNECT",
	[SOCK_MSG_SENDRECV]    =  "SOCK_MSG_SENDRECV",
	[SOCK_MSG_RENDEZVOUS]  =  "SOCK_MSG_RENDEZVOUS",
	[SOCK_MSG_READ_REQ]    =  "SOCK_MSG_READ_REQ",
	[SOCK_MSG_READ_RESP]   =  "SOCK_MSG_READ_RESP",
	[SOCK_MSG_WRITE_REQ]   =  "SOCK_MSG_WRITE_REQ",
	[SOCK_MSG_WRITE_RESP]  =  "SOCK_MSG_WRITE_RESP",
	[SOCK_MSG_ACCEPTED]    =  "SOCK_MSG_ACCEPTED",
	[SOCK_MSG_REJECTED]    =  "SOCK_MSG_REJECTED",
	[SOCK_MSG_ACK_ACCEPTED] = "SOCK_MSG_ACK_ACCEPTED",
};

static inline
const char *sock_msg_type_str(sock_msg_type_t t)
{
	if (SOCK_MSG_FIRST <= t && t < SOCK_MSG_TYPE_LAST)
		return __sock_msg_type_str[t];
	return __sock_msg_type_str[0];
}

#pragma pack(4)

/**
 * \brief Zap message header for socket transport.
 *
 * Each of the sock_msg's is an extension to ::sock_msg_hdr.
 */
struct sock_msg_hdr {
	uint16_t msg_type; /**< The request type */
	uint16_t reserved;
	uint32_t msg_len;  /**< Length of the entire message, header included. */
	uint32_t xid;	   /**< Transaction Id to check against reply */
	uint64_t ctxt;	   /**< User context to be returned in reply */
};

static char ZAP_SOCK_SIG[8] = "SOCKET";

/**
 * Connect message.
 */
struct sock_msg_connect {
	struct sock_msg_hdr hdr;
	struct zap_version ver;
	char sig[8];
	uint32_t data_len;
	char data[OVIS_FLEX];
};

/**
 * Send/Recv message.
 */
struct sock_msg_sendrecv {
	struct sock_msg_hdr hdr;
	uint32_t data_len;
	char data[OVIS_FLEX];
};

/**
 * Read request (src_addr --> dst_addr)
 */
struct sock_msg_read_req {
	struct sock_msg_hdr hdr;
	uint32_t src_map_key; /**< Source map reference (on non-initiator) */
	uint64_t src_ptr; /**< Source memory */
	uint32_t data_len; /**< Data length */
};

/**
 * Read response
 */
struct sock_msg_read_resp {
	struct sock_msg_hdr hdr;
	uint16_t status; /**< Return status */
	uint64_t dst_ptr; /**< Destination memory addr (on initiator) */
	uint32_t data_len; /**< Response data length */
	char data[OVIS_FLEX]; /**< Response data */
};

/**
 * Write request
 */
struct sock_msg_write_req {
	struct sock_msg_hdr hdr;
	uint32_t dst_map_key; /**< Destination map key */
	uint64_t dst_ptr; /**< Destination address */
	uint32_t data_len; /**< Data length */
	char data[OVIS_FLEX]; /**< data for SOCK_MSG_WRITE_REQ */
};

/**
 * Write response
 */
struct sock_msg_write_resp {
	struct sock_msg_hdr hdr;
	uint16_t status; /**< Return status */
};

/**
 * Message for exporting/sharing zap_map.
 */
struct sock_msg_rendezvous {
	struct sock_msg_hdr hdr;
	uint32_t rmap_key; /**< Remote map reference */
	uint32_t acc; /**< Access */
	uint64_t addr; /**< Address in the map */
	uint32_t data_len; /**< Length */
	char msg[OVIS_FLEX]; /**< Context */
};

/* convenient union of message structures */
typedef union sock_msg_u {
	struct sock_msg_hdr hdr;
	struct sock_msg_sendrecv sendrecv;
	struct sock_msg_connect connect;
	struct sock_msg_rendezvous rendezvous;
	struct sock_msg_read_req read_req;
	struct sock_msg_read_resp read_resp;
	struct sock_msg_write_req write_req;
	struct sock_msg_write_resp write_resp;
	char bytes[0]; /* access as bytes */
} *sock_msg_t;

#define Z_SOCK_WR_COMPLETION 0x1 /* A completion should be delivered when WR is
				    done. This also implies that WR is a member
				    of io structure. */
#define Z_SOCK_WR_SEND        0x2
#define Z_SOCK_WR_SEND_MAPPED 0x4

typedef struct z_sock_send_wr_s {
	TAILQ_ENTRY(z_sock_send_wr_s) link;
	struct z_sock_io *io;
	size_t msg_len; /* remaining msg len */
	size_t data_len; /* remaining data len */
	size_t off; /* offset of msg or data */
	const char *data;
	int flags; /* various wr flags */
	union sock_msg_u msg; /* The message */
} *z_sock_send_wr_t;

/**
 * Keeps track of outstanding I/O so that it can be cleaned up when
 * the endpoint shuts down. A z_sock_io is either on the free_q or the
 * io_q for the endpoint.
 */
struct z_sock_io {
	TAILQ_ENTRY(z_sock_io) q_link;
	zap_map_t dst_map; /**< Destination map for RDMA_READ */
	char *dst_ptr; /**< Destination address for RDMA_READ */
	struct z_sock_send_wr_s *wr;
	enum zap_event_type comp_type; /**< completion type */
	void *ctxt; /**< Application context */
	uint32_t xid;	   /**< Transaction Id to check against reply */
};

#pragma pack()

typedef struct z_sock_buff_s {
	/* NOTE: total allocated data length is alen + len */
	size_t alen; /* available data length */
	size_t len; /* current data length */
	void *data;
} *z_sock_buff_t;

typedef struct z_sock_io_thread *z_sock_io_thread_t;

struct z_sock_ep {
	struct zap_ep ep;

	int sock;
	char *conn_data;
	size_t conn_data_len;
	uint8_t rejecting;

	int sock_connected;
	int app_accepted;

	struct epoll_event ev;
	void (*ev_fn)(struct z_sock_io_thread *, struct epoll_event *);
	struct z_sock_buff_s buff;

	pthread_mutex_t q_lock;
	TAILQ_HEAD(, z_sock_io) io_q; /* manages ops from app (read/write/send) */
	TAILQ_HEAD(, z_sock_io) io_cq; /* completion queue, currently serves only send completion */
	TAILQ_HEAD(, z_sock_send_wr_s) sq; /* send queue */
	LIST_ENTRY(z_sock_ep) link;
	pthread_cond_t sq_cond;
};

#define ZAP_SOCK_EV_SIZE 4096

struct z_sock_io_thread {
	struct zap_io_thread zap_io_thread;
	int efd; /* epoll fd */
	struct epoll_event ev[ZAP_SOCK_EV_SIZE];
};

static inline struct z_sock_ep *z_sock_from_ep(zap_ep_t *ep)
{
	return (struct z_sock_ep *)ep;
}

/* For debugging */
void __log_sep_msg(struct z_sock_ep *sep, int is_recv,
			  const struct sock_msg_hdr *hdr);

#endif
