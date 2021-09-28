/**
 * Copyright (c) 2014-2017,2019-2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2014-2017,2019-2021 Open Grid Computing, Inc. All rights reserved.
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

#ifndef __LDMS_XPRT_UGNI_H__
#define __LDMS_XPRT_UGNI_H__
#include <semaphore.h>
#include <sys/queue.h>

#include "ovis_event/ovis_event.h"

#include <sys/time.h>

#include <gni_pub.h>
#include <rca_lib.h>
#include "ovis-ldms-config.h"
#include "coll/rbt.h"

#include "zap.h"
#include "zap_priv.h"

/*
 * The length of the string
 * lcl=<local ip address:port> <--> rmt=<remote ip address:port>
 */
#define ZAP_UGNI_EP_NAME_SZ 64

/* NOTE: This is the entire message buffer length submitted to GNI_PostRdma().
 * The `max_msg` length reported to the application is ZAP_UGNI_MSG_MAX -
 * `max_hdr_len` - 8.  */
#define ZAP_UGNI_BUFSZ 1024

#define ZAP_UGNI_EP_GRAIN 2048
#define ZAP_UGNI_THREAD_EP_MAX 2048 /* max endpoints per thread */
#define ZAP_UGNI_EP_MSG_CREDIT 8
#define ZAP_UGNI_RDMA_CQ_DEPTH (4*1024*1024)
#define ZAP_UGNI_SMSG_CQ_DEPTH (4*1024*1024)
#define ZAP_UGNI_RCQ_DEPTH (4*1024*1024)
#define ZAP_UGNI_RDMA_POST_CREDIT (64)
#define ZAP_UGNI_MSG_POST_CREDIT (32)
#define ZAP_UGNI_ACK_POST_CREDIT (32)

#define ZAP_UGNI_IDX_POOL_LEN_GRAIN 4096

/* This is used by handle rendezvous */
struct zap_ugni_map {
	struct zap_map map;
	gni_mem_handle_t gni_mh;
};

/* The epoll data */
struct z_ugni_epoll_ctxt {
	enum {
		Z_UGNI_SOCK_EVENT, /* event from socket */
		Z_UGNI_CQ_EVENT, /* event from cq channel */
		Z_UGNI_ZQ_EVENT, /* event from zq (zap queue) channel */
	} type;
};

/**
 * ZAP_UGNI message types.
 */
typedef enum zap_ugni_msg_type {
	ZAP_UGNI_MSG_NONE,        /**< Dummy first type */
	ZAP_UGNI_MSG_CONNECT,     /**< Connect data */
	ZAP_UGNI_MSG_REGULAR,     /**< Application send-receive */
	ZAP_UGNI_MSG_RENDEZVOUS,  /**< Share zap_map */
	ZAP_UGNI_MSG_ACCEPTED,    /**< Connection accepted */
	ZAP_UGNI_MSG_REJECTED,    /**< Connection rejected */
	ZAP_UGNI_MSG_ACK_ACCEPTED,/**< Acknowledge accepted msg */
	ZAP_UGNI_MSG_TERM,        /**< Connection termination request */
	ZAP_UGNI_MSG_ACK_TERM,    /**< Acknowledge connection termination */
	ZAP_UGNI_MSG_SEND_MAPPED, /**< Application send-receive (w/mapped) */
	ZAP_UGNI_MSG_RECV_ACK,    /**< Recv acknowledgement */
	ZAP_UGNI_MSG_TYPE_LAST    /**< Dummy last type (for type count) */
} zap_ugni_msg_type_t;


static const char *__zap_ugni_msg_type_str[] = {
	[ZAP_UGNI_MSG_NONE]        =  "ZAP_UGNI_MSG_NONE",
	[ZAP_UGNI_MSG_REGULAR]     =  "ZAP_UGNI_MSG_REGULAR",
	[ZAP_UGNI_MSG_RENDEZVOUS]  =  "ZAP_UGNI_MSG_RENDEZVOUS",
	[ZAP_UGNI_MSG_ACCEPTED]    =  "ZAP_UGNI_MSG_ACCEPTED",
	[ZAP_UGNI_MSG_CONNECT]     =  "ZAP_UGNI_MSG_CONNECT",
	[ZAP_UGNI_MSG_REJECTED]    =  "ZAP_UGNI_MSG_REJECTED",
	[ZAP_UGNI_MSG_ACK_ACCEPTED]=  "ZAP_UGNI_MSG_ACK_ACCEPTED",
	[ZAP_UGNI_MSG_TERM]        =  "ZAP_UGNI_MSG_TERM",
	[ZAP_UGNI_MSG_ACK_TERM]    =  "ZAP_UGNI_MSG_ACK_TERM",
	[ZAP_UGNI_MSG_SEND_MAPPED] =  "ZAP_UGNI_MSG_SEND_MAPPED",
	[ZAP_UGNI_MSG_TYPE_LAST]   =  "ZAP_UGNI_MSG_TYPE_LAST"
};

const char *zap_ugni_msg_type_str(zap_ugni_msg_type_t type);

typedef enum zap_ugni_type {
	ZAP_UGNI_TYPE_NONE,
	ZAP_UGNI_TYPE_GEMINI,
	ZAP_UGNI_TYPE_ARIES,
	ZAP_UGNI_TYPE_LAST,
} zap_ugni_type_t;

static const char *__zap_ugni_type_str[] = {
	[ZAP_UGNI_TYPE_NONE]    =  "ZAP_UGNI_TYPE_NONE",
	[ZAP_UGNI_TYPE_GEMINI]  =  "ZAP_UGNI_TYPE_GEMINI",
	[ZAP_UGNI_TYPE_ARIES]   =  "ZAP_UGNI_TYPE_ARIES",
	[ZAP_UGNI_TYPE_LAST]    =  "ZAP_UGNI_TYPE_LAST",
};

const char *zap_ugni_type_str(zap_ugni_type_t type);

static const char *__gni_ret_str[] = {
	[GNI_RC_SUCCESS]            =  "GNI_RC_SUCCESS",
	[GNI_RC_NOT_DONE]           =  "GNI_RC_NOT_DONE",
	[GNI_RC_INVALID_PARAM]      =  "GNI_RC_INVALID_PARAM",
	[GNI_RC_ERROR_RESOURCE]     =  "GNI_RC_ERROR_RESOURCE",
	[GNI_RC_TIMEOUT]            =  "GNI_RC_TIMEOUT",
	[GNI_RC_PERMISSION_ERROR]   =  "GNI_RC_PERMISSION_ERROR",
	[GNI_RC_DESCRIPTOR_ERROR]   =  "GNI_RC_DESCRIPTOR_ERROR",
	[GNI_RC_ALIGNMENT_ERROR]    =  "GNI_RC_ALIGNMENT_ERROR",
	[GNI_RC_INVALID_STATE]      =  "GNI_RC_INVALID_STATE",
	[GNI_RC_NO_MATCH]           =  "GNI_RC_NO_MATCH",
	[GNI_RC_SIZE_ERROR]         =  "GNI_RC_SIZE_ERROR",
	[GNI_RC_TRANSACTION_ERROR]  =  "GNI_RC_TRANSACTION_ERROR",
	[GNI_RC_ILLEGAL_OP]         =  "GNI_RC_ILLEGAL_OP",
	[GNI_RC_ERROR_NOMEM]        =  "GNI_RC_ERROR_NOMEM",
};

static const char *gni_ret_str(gni_return_t ret)
{
	if (ret < GNI_RC_SUCCESS || GNI_RC_ERROR_NOMEM < ret)
		return "GNI_RC_UNKNOWN";
	return __gni_ret_str[ret];
}

struct ugni_mh {
	unsigned long start;
	unsigned long end;
	gni_mem_handle_t mh;
	int ref_count;
	LIST_ENTRY(ugni_mh) link;
};

#pragma pack(push, 4)
/**
 * \brief Zap message header for ugni transport.
 */
struct zap_ugni_msg_hdr {
	uint16_t msg_type;
	uint16_t reserved;
	uint32_t msg_len; /** Length of the entire message, header included. */
};

/**
 * Regular message.
 */
struct zap_ugni_msg_regular {
	struct zap_ugni_msg_hdr hdr;
	uint32_t data_len;
	char data[OVIS_FLEX];
};

/**
 * Message for exporting/sharing zap_map.
 */
struct zap_ugni_msg_rendezvous {
	struct zap_ugni_msg_hdr hdr;
	gni_mem_handle_t gni_mh;
	zap_access_t acc;
	uint64_t map_addr; /**< Address in the map */
	uint32_t map_len; /**< Length */
	char msg[OVIS_FLEX]; /**< Context */
};

/**
 * Instance ID.
 */
typedef union z_ugni_inst_id_u {
	struct {
		uint32_t node_id : 18;
		uint32_t pid     : 14;
	};
	uint32_t u32; /* access as uint32_t */
} z_ugni_inst_id_t;

/**
 * Remote endpoint descriptor
 */
struct z_ugni_ep_desc {
	uint32_t inst_id; /**< peer inst_id */
	uint32_t pe_addr; /**< peer address */
	uint32_t remote_event; /**< `remote_event` for `GNI_EpSetEventData()` */
	uint32_t mbuf_sz; /**< size of mbuf structure (for verification) */
	uint64_t mbuf_addr; /**< pointer to message buffer structure */
	gni_mem_handle_t mbuf_mh; /**< memory handle for message buffer */
};

static char ZAP_UGNI_SIG[8] = "UGNI_2";

/**
 * Message for zap connection.
 *
 * NOTE: Even though the `ver`, `sig`, and `ep_desc` are redundant to
 *       those of `z_ugni_sock_msg_conn_req` that was sent at the beginning of
 *       the connect operation, we keep these field around so that when we
 *       discard the socket in the near future, we can use the connect message
 *       right away.
 */
struct zap_ugni_msg_connect {
	struct zap_ugni_msg_hdr hdr;
	char pad[12];  /**< padding so that ugni ver,xprt are aligned with sock ver,xprt */
	struct zap_version ver; /* zap version */
	char sig[8];     /**< transport type signature. */
	struct z_ugni_ep_desc ep_desc; /**< Endpoint descriptor. */
	uint32_t data_len; /**< Size of connection data */
	char data[OVIS_FLEX];      /**< Connection data */
};

/**
 * Message for zap connection accepted.
 *
 * NOTE: `ep_desc` is to be used in the future when we discard sockets.
 */
struct zap_ugni_msg_accepted {
	struct zap_ugni_msg_hdr hdr;
	struct z_ugni_ep_desc ep_desc; /**< Endpoint descriptor. */
	uint32_t data_len;
	char data[OVIS_FLEX];
};

typedef struct zap_ugni_msg {
	union {
		struct zap_ugni_msg_hdr hdr;
		struct zap_ugni_msg_regular regular;
		struct zap_ugni_msg_connect connect;
		struct zap_ugni_msg_accepted accepted;
		struct zap_ugni_msg_rendezvous rendezvous;
	};
} *zap_ugni_msg_t;

/**
 * Connection request socket message for setting up uGNI endpoints.
 *
 * We still need to be zap_sock aware to refuse the incorrect connection request
 * from zap_sock.
 */
struct z_ugni_sock_msg_conn_req {
	struct zap_ugni_msg_hdr hdr;
	char pad[12];  /**< padding so that ugni ver,xprt are aligned with sock ver,xprt */
	struct zap_version ver;
	char sig[8];     /**< transport type signature. */
	struct z_ugni_ep_desc ep_desc;
};

/**
 * Conection accept socket message for setting up uGNI endpoints.
 */
struct z_ugni_sock_msg_conn_accept {
	struct zap_ugni_msg_hdr hdr;
	struct z_ugni_ep_desc ep_desc;
};

#pragma pack(pop)

/* Endpoint index structure */
struct z_ugni_ep_idx {
	uint32_t idx; /* index to self */
	uint32_t next_free_idx; /* next idx in the free list */
	struct z_ugni_ep *uep;
};

/* RDMA work request */
struct zap_ugni_rdma_wr {
	gni_post_descriptor_t post;
	uint32_t ep_gn;
	char ep_name[ZAP_UGNI_EP_NAME_SZ];
	uint8_t is_stalled; /* It is in the stalled list. */
	struct timeval stalled_time;
	void *context;
	LIST_ENTRY(zap_ugni_rdma_wr) ep_link;
	LIST_ENTRY(zap_ugni_rdma_wr) stalled_link;
};

/* Message send work request */
struct zap_ugni_send_wr {
	gni_post_descriptor_t post;
	TAILQ_ENTRY(zap_ugni_send_wr) link;
	struct z_ugni_msg_buf_ent *sbuf;
	int buf_idx;
	void  *ctxt; /* for send_mapped completion */
	int    flags; /* various wr flags */
	uint32_t msg_id; /* ep_idx(16-bit)|smsg_seq(16-bit) */
	size_t msg_len; /* entire message length */
	size_t hdr_len;  /* header length */
	size_t data_len; /* data length */
	void *data; /* pointer to the data part of the message */
	struct zap_ugni_msg msg[OVIS_FLEX];
};

/* Work request */
struct z_ugni_wr {
	TAILQ_ENTRY(z_ugni_wr) entry;
	uint64_t seq; /* wr sequence number */
	struct z_ugni_ep *uep;
	enum {
		Z_UGNI_WR_RDMA,
		Z_UGNI_WR_SEND,
		Z_UGNI_WR_SEND_MAPPED,
		Z_UGNI_WR_RECV_ACK,
	} type;
	enum {
		Z_UGNI_WR_INIT,
		Z_UGNI_WR_PENDING,
		Z_UGNI_WR_SUBMITTED,
		Z_UGNI_WR_STALLED, /* submitted, but not completed before ep destroy */
	} state;
	union {
		gni_post_descriptor_t post[0];
		struct zap_ugni_rdma_wr rdma_wr[0];
		struct zap_ugni_send_wr send_wr[0];
	};
};

TAILQ_HEAD(z_ugni_wrq, z_ugni_wr);

/* zap event entry primarily for deferred event */
struct z_ugni_ev {
	struct rbn rbn;
	int in_zq; /* for debugging */
	char acq; /* acquire flag */
	uint64_t ts_msec; /* msec since the Epoch for timed event */
	struct zap_event zev;
};

struct zap_ugni_rdma_wr;
LIST_HEAD(zap_ugni_post_desc_list, zap_ugni_rdma_wr);

/* Message buffer entry, for sending/receiving a single message. */
struct z_ugni_msg_buf_ent {
	union {
		struct zap_ugni_msg msg;
		char bytes[ZAP_UGNI_BUFSZ - 8];
	};
	struct {
		uint8_t  processed:1;
		uint64_t reserved:63;
	} status;
};

/* represents recv buffer status */
struct z_ugni_rbuf_status {
	uint8_t  avail:1;
	uint64_t reserved:63;
};

/* Tracking ACK status, so that we won't double ACK on the same entry. */
struct z_ugni_ack_status {
	uint8_t outstanding:1;
	uint8_t pending:1;
	uint64_t reserved:62;
};

/** send/recv buffer for an endpoint */
struct z_ugni_msg_buf {
	struct z_ugni_msg_buf_chunk *chunk; /* the chunk this msg_buf belongs to */
	int curr_rbuf_idx; /* current recv buffer index */
	int curr_sbuf_idx; /* current send buffer index */
	TAILQ_ENTRY(z_ugni_msg_buf) entry; /* next buf in the free list */

	/* Peer will write messages to rbuf using RDMA PUT */
	struct z_ugni_msg_buf_ent rbuf[ZAP_UGNI_EP_MSG_CREDIT];

	/* Our sbuf will be copied to peer's rbuf using RDMA PUT. sbuf[i] will
	 * be copied to the peer's rbuf[i]. */
	struct z_ugni_msg_buf_ent sbuf[ZAP_UGNI_EP_MSG_CREDIT];

	/* Peer will write (peer's ACK) to peer_rbuf_status[i] to let us know if
	 * peer's rbuf[i] is available. */
	struct z_ugni_rbuf_status peer_rbuf_status[ZAP_UGNI_EP_MSG_CREDIT];

	/* our_rbuf_status[i] is used as a source buffer to RDMA PUT to peer's
	 * peer_rbuf_status[i] (our ACK to peer). */
	struct z_ugni_rbuf_status our_rbuf_status[ZAP_UGNI_EP_MSG_CREDIT];

	/* track the ACK we send to peer. When `outstanding` is 1, the previous
	 * ACK has not been completed yet. Hence, we should not post a new ACK
	 * of the same cell. This can happen by peer receiving the ACK (that has
	 * not been completed yet on our end), and send the new message (this is
	 * totally legit since the recv cell on our end has become available).
	 * The new message then arrived but the old ACK has not completed yet.
	 * In this case, the `pending` will be switch to 1. So that when the
	 * previous ACK completed, we will submit another ACK and reset
	 * everything in the corresponding cell to 0.
	 */
	struct z_ugni_ack_status ack_status[ZAP_UGNI_EP_MSG_CREDIT];
};

TAILQ_HEAD(z_ugni_msg_buf_head, z_ugni_msg_buf);

/* a chunk consists of multiple buffer */
struct z_ugni_msg_buf_chunk {
	TAILQ_ENTRY(z_ugni_msg_buf_chunk) entry;
	gni_mem_handle_t mbuf_mh; /* mem registration for `buf` */
	int free_count;
	struct z_ugni_msg_buf_head free_list;
	struct z_ugni_msg_buf buf[ZAP_UGNI_EP_GRAIN];
};

TAILQ_HEAD(z_ugni_msg_buf_chunk_head, z_ugni_msg_buf_chunk);

/* a pool consists of many chunks */
struct z_ugni_msg_buf_pool {
	int vacant_count; /* number of chunks in vacant_list */
	/* chunks that have vacancies are in vacant_list */
	struct z_ugni_msg_buf_chunk_head vacant_list;
	/* chunks that have NO vacancies are in full_list */
	struct z_ugni_msg_buf_chunk_head full_list;

	/* NOTE a fully vacant chunk shall be freed unless it is the last chunk
	 * in the vacant list */
};

struct z_ugni_ep {
	struct zap_ep ep;

	struct z_ugni_ep_desc peer_ep_desc; /* keep for a reference */

	int sock;
	int node_id;
	int ep_id; /* The index in the endpoint array */
	char *conn_data;
	size_t conn_data_len;
	uint8_t sock_connected:1;
	uint8_t zap_connected:1; /* has become zap connected */
	uint8_t app_accepted:1;
	uint8_t ugni_ep_bound:1; /* can use SMSG */
	uint8_t ugni_term_sent:1; /* TERM msg has been sent */
	uint8_t ugni_term_recv:1; /* TERM msg has been received */
	uint8_t ugni_ack_term_sent:1; /* ACK_TERM msg has been sent */
	uint8_t ugni_ack_term_recv:1; /* ACK_TERM msg has been received */
	gni_ep_handle_t gni_ep;

	struct z_ugni_ev uev; /* event in thr->zq */

	LIST_ENTRY(z_ugni_ep) link; /* for z_ugni_list */

	pthread_cond_t sq_cond;

	uint16_t next_msg_seq; /* message sequence number, for debugging */

	/* epoll data for socket file descriptor */
	struct z_ugni_epoll_ctxt sock_epoll_ctxt;

	/* recv buffer for socket message */
	union {
		char buff[0];
		struct zap_ugni_msg_hdr hdr;
		struct z_ugni_sock_msg_conn_req conn_req;
		struct z_ugni_sock_msg_conn_accept conn_accept;
	} sock_buff;
	int sock_off; /* recv offset into sock_buff */

	/* assigned endpoint index (in the thread) */
	struct z_ugni_ep_idx *ep_idx;
	struct zap_ugni_msg *rmsg; /* current recv msg */

#ifdef EP_LOG_ENABLED
	/* endpoint log for debugging */
	FILE *log;
#endif
	/* cached socket address */
	struct sockaddr_storage local_addr;
	struct sockaddr_storage remote_addr;
	socklen_t addr_len;

	/* Pending message WR in the endpoint, waiting for the available send
	 * buffer */
	struct z_ugni_wrq pending_msg_wrq;

	/* flushed entries from thr */
	struct z_ugni_wrq flushed_wrq;

	/* track the number of WR submitted to thr, but not completed */
	int active_wr;

	/* Send/recv message buffer assigned to the endpoint (part of thr->mbuf) */
	struct z_ugni_msg_buf *mbuf;
};

struct z_ugni_ep_idx_pool {
	/*
	 * pool[0] contains idx 0..ZAP_UGNI_EP_GRAIN-1
	 * pool[1] contains idx ZAP_UGNI_EP_GRAIN..2*ZAP_UGNI_EP_GRAIN-1
	 * ...
	 */
	struct z_ugni_ep_idx **pool;
	int pool_len; /* length of pool */
	int pool_last; /* pool[pool_last] is the last pool allocated */
	struct z_ugni_ep_idx *free_head;
	struct z_ugni_ep_idx *free_tail;
	int free_count;
};

/* Get idx entry from the pool */
static inline
struct z_ugni_ep_idx * __pool_idx(struct z_ugni_ep_idx_pool *pool, int idx)
{
	int p_idx = idx / ZAP_UGNI_EP_GRAIN;
	int o_idx = idx % ZAP_UGNI_EP_GRAIN;
	if (p_idx >= pool->pool_len || !pool->pool[p_idx])
		return NULL;
	return &pool->pool[p_idx][o_idx];
}

struct z_ugni_io_thread {
	struct zap_io_thread zap_io_thread;
	int efd; /* epoll file descriptor */
	gni_cq_handle_t scq; /* Submission completion queue (currently only for GNI_PostRdma) */
	gni_cq_handle_t rcq; /* Recv completion queue (for mbuf RDMA PUT notification) */
	/* Remark on 2 CQs:
	 *
	 *   The FMA Short Messaging Overview page on Cray website stated the
	 *   following:
	 *
	 *   > An application should use a dedicated CQ for remote events and
	 *   > cannot rely on GNI_CQ_GET_TYPE to determine the type of the
	 *   > incoming remote event. A receiving peer should use
	 *   > GNI_CQ_GET_REM_INST_ID(event) to obtain the sender's instance ID.
	 *   > See gni_cq_entry.
	 *
	 *   Ref:
	 *   https://pubs.cray.com/bundle/XC_Series_GNI_and_DMAPP_API_User_Guide_CLE70UP02_S-2446/page/FMA_Short_Messaging_Overview.html
	 *
	 *   The smsg example (`smsg_send_pmi_example.c` in
	 *   /opt/cray/ugni/default/examples/) also shows that the send
	 *   completions are delivered on the endpoint's cq, and the recv
	 *   completions are delivered on the mbox memory registration's cq. It
	 *   looks like there is no way to differentiate the send completion
	 *   from the recv completion if they were to register to the same cq.
	 *
	 */
	gni_comp_chan_handle_t cch; /* Completion Channel */
	int cch_fd; /* completion channel file descriptor */

	struct z_ugni_epoll_ctxt cq_epoll_ctxt;

	/* Each endpoint is assigned an index. The peers use this index set
	 * REMOTE_DATA so that we (the local process) know who (which endpoint)
	 * RDMA PUT into the recv buffer memory. */
	struct z_ugni_ep_idx_pool idx_pool;

	struct rbt zq; /* zap event queue, protected by zap_io_thread.mutex */
	int zq_fd[2]; /* zap event queue fd (to wake up epoll_wait) */
	struct z_ugni_epoll_ctxt zq_epoll_ctxt; /* epoll event data for zq */

	/* These tail queues are protected by zap_io_thread.mutex. */

	/* Message WR is queued here before GNI_PostRdma() by the io_thread */
	struct z_ugni_wrq pending_msg_wrq;

	/* RDMA WR is queued here before GNI_PostRdma() by the io_thread */
	struct z_ugni_wrq pending_rdma_wrq;

	/* ACK WR (another kind of Message) is queued here before GNI_PostRdma()
	 * by the io_thread. */
	struct z_ugni_wrq pending_ack_wrq;

	/* This queue contains WR submitted to GNI */
	struct z_ugni_wrq submitted_wrq;

	/* This queue contains WR that was submitted (GNI_PostRdma), but has not
	 * completed before the enpoint was destroyed. We keep the WR around
	 * because the completion may be delivered afterward. If the WR was
	 * freed, the completion handler will access the freed WR -- a
	 * use-after-free. */
	struct z_ugni_wrq stalled_wrq;

	/* rdma_post_credit controls the number of outstanding GNI_PostRdma()
	 * for RDMA WR */
	int rdma_post_credit;
	/* msg_post_credit controls the number of outstanding GNI_PostRdma()
	 * for Message WR */
	int msg_post_credit;
	/* ack_post_credit controls the number of outstanding GNI_PostRdma()
	 * for ACK WR */
	int ack_post_credit;

	uint64_t wr_seq; /* wr sequence number */
	/* ---------------------------------------- */

	/* messgae buffer pool for endpoints assigned to the thread */
	struct z_ugni_msg_buf_pool mbuf_pool;
};

#endif
