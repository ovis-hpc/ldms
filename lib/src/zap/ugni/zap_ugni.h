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

/* default ZAP_UGNI_CQ_DEPTH, ENV var can change it */
#define ZAP_UGNI_CQ_DEPTH (65536)

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
	ZAP_UGNI_MSG_TERM,        /**< DEPRECATED Connection termination request */
	ZAP_UGNI_MSG_ACK_TERM,    /**< DEPRECATED Acknowledge connection termination */
	ZAP_UGNI_MSG_SEND_MAPPED = 9, /**< Application send-receive (w/mapped) */
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
	[ZAP_UGNI_MSG_TERM]        =  "DEPRECATED_ZAP_UGNI_MSG_TERM",
	[ZAP_UGNI_MSG_ACK_TERM]    =  "DEPRECATED_ZAP_UGNI_MSG_ACK_TERM",
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
};

static char ZAP_UGNI_SIG[8] = "UGNI";

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
struct zap_ugni_msg_accept {
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
		struct zap_ugni_msg_accept accept;
		struct zap_ugni_msg_rendezvous rendezvous;
		char bytes[0]; /* access as raw bytes */
	};
} *zap_ugni_msg_t;

typedef struct zap_ugni_msg_buf {
	union {
		struct zap_ugni_msg msg;
		char buf[ZAP_UGNI_BUFSZ];
	};
} *zap_ugni_msg_buf_t;

#pragma pack(pop)

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
	int status;
	void  *ctxt; /* for send_mapped completion */
	int    flags; /* various wr flags */
	size_t hdr_off;  /* offset into the header that has been written */
	size_t data_off; /* offset into the data that has been written */
	size_t msg_len; /* entire message length */
	size_t hdr_len;  /* header length */
	size_t data_len; /* data length */
	void  *data; /* pointer to the data part of the message */
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

TAILQ_HEAD(z_ugni_msg_buf_head, z_ugni_msg_buf);

typedef struct z_ugni_ep *z_ugni_ep_t;

struct z_ugni_ep {
	struct zap_ep ep;

	struct z_ugni_ep_desc peer_ep_desc; /* keep for a reference */

	int sock;
	int node_id;

	char *conn_data;
	size_t conn_data_len;
	uint8_t sock_connected:1;
	uint8_t zap_connected:1; /* has become zap connected */
	uint8_t app_accepted:1;
	uint8_t ugni_ep_bound:1; /* can use SMSG */
	uint8_t sock_enabled:1;
	gni_ep_handle_t gni_ep;

	struct z_ugni_ev uev; /* event in thr->zq */

	LIST_ENTRY(z_ugni_ep) link; /* for z_ugni_list */

	pthread_cond_t sq_cond;

	struct epoll_event ev;

	/* epoll data for socket file descriptor */
	struct z_ugni_epoll_ctxt sock_epoll_ctxt;

	/* recv buffer for socket message */
	struct zap_ugni_msg_buf mbuf;
	int sock_off; /* recv offset into sock_buff */

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
	struct z_ugni_wrq send_wrq;
	struct z_ugni_wrq send_comp_wrq; /* send completion queue */

	/* flushed entries from thr */
	struct z_ugni_wrq flushed_wrq;

	/* track the number of WR submitted to thr, but not completed */
	int active_wr;
};

struct z_ugni_io_thread {
	struct zap_io_thread zap_io_thread;
	int efd; /* epoll file descriptor */
	gni_cq_handle_t scq; /* Submission completion queue (currently only for GNI_PostRdma) */
	gni_comp_chan_handle_t cch; /* Completion Channel */
	int cch_fd; /* completion channel file descriptor */

	struct z_ugni_epoll_ctxt cq_epoll_ctxt;

	struct rbt zq; /* zap event queue, protected by zap_io_thread.mutex */
	int zq_fd[2]; /* zap event queue fd (to wake up epoll_wait) */
	struct z_ugni_epoll_ctxt zq_epoll_ctxt; /* epoll event data for zq */

	/* These tail queues are protected by zap_io_thread.mutex. */

	/* RDMA WR is queued here before GNI_PostRdma() by the io_thread */
	struct z_ugni_wrq pending_rdma_wrq;

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

	uint64_t wr_seq; /* wr sequence number */
	/* ---------------------------------------- */
};
typedef struct z_ugni_io_thread *z_ugni_io_thread_t;

/* max application data length */
#define ZAP_UGNI_MAX_MSG (sizeof(struct zap_ugni_msg_buf) - sizeof(struct zap_ugni_msg))

#endif
