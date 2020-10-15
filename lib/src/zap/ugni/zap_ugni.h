/**
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

#define UGNI_SOCKBUF_SZ 1024 * 1024

/*
 * The length of the string
 * lcl=<local ip address:port> <--> rmt=<remote ip address:port>
 */
#define ZAP_UGNI_EP_NAME_SZ 64

struct zap_ugni_map {
	struct zap_map map;
	gni_mem_handle_t gni_mh; /**< GNI memory handle */
};

struct z_ugni_key {
	struct rbn rb_node;
	struct zap_ugni_map *map; /**< reference to zap_map */
};

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

#pragma pack(4)
/**
 * \brief Zap message header for socket transport.
 *
 * Each of the sock_msg's is an extension to ::zap_ugni_msg_hdr.
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
 * Remote endpoint descriptor
 */
struct z_ugni_ep_desc {
	uint32_t inst_id; /**< peer inst_id */
	uint32_t pe_addr; /**< peer address */
	uint32_t remote_event; /**< `remote_event` for `GNI_EpSetEventData()` */
	struct gni_smsg_attr smsg_attr; /**< recv buffer info */
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
 * NOTE: `ep_desc` is to be used in the near-future when we discard sockets.
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

#pragma pack()

struct zap_ugni_send_wr {
	STAILQ_ENTRY(zap_ugni_send_wr) link;
	uint32_t msg_id; /* ep_idx(16-bit)|smsg_seq(16-bit) */
	size_t msg_len;
	struct zap_ugni_msg msg[OVIS_FLEX];
};

struct zap_ugni_recv_buff {
	size_t len; /* length of the bufferred data */
	size_t alen; /* available allocated length after data + len */
	char data[OVIS_FLEX];
};

struct z_ugni_ep_idx {
	uint16_t idx; /* index to self */
	uint16_t next_idx; /* next idx in the free list */
	struct z_ugni_ep *uep;
};

struct zap_ugni_post_desc {
	gni_post_descriptor_t post;
	struct z_ugni_ep *uep;
	uint32_t ep_gn;
	char ep_name[ZAP_UGNI_EP_NAME_SZ];
	uint8_t is_stalled; /* It is in the stalled list. */
	struct timeval stalled_time;
	void *context;
	LIST_ENTRY(zap_ugni_post_desc) ep_link;
	LIST_ENTRY(zap_ugni_post_desc) stalled_link;
};

struct z_ugni_wr {
	STAILQ_ENTRY(z_ugni_wr) entry;
	enum {
		Z_UGNI_WR_RDMA,
		Z_UGNI_WR_SMSG,
	} type;
	enum {
		Z_UGNI_WR_INIT,
		Z_UGNI_WR_PENDING,
		Z_UGNI_WR_SUBMITTED,
		Z_UGNI_WR_STALLED, /* submitted, but not completed before ep destroy */
	} state;
	union {
		struct zap_ugni_post_desc post_desc[0];
		struct zap_ugni_send_wr send_wr[0];
	};
};

STAILQ_HEAD(z_ugni_wrq, z_ugni_wr);

/* zap event entry primarily for deferred event */
struct z_ugni_ev {
	struct rbn rbn;
	int in_zq; /* for debugging */
	char acq; /* acquire flag */
	uint64_t ts_msec; /* msec since the Epoch for timed event */
	struct zap_event zev;
};

struct zap_ugni_post_desc;
LIST_HEAD(zap_ugni_post_desc_list, zap_ugni_post_desc);
struct z_ugni_ep {
	struct zap_ep ep;

	int sock;
	int node_id;
	int ep_id; /* The index in the endpoint array */
	char *conn_data;
	size_t conn_data_len;
	uint8_t sock_connected:1;
	uint8_t zap_connected:1; /* has become zap connected */
	uint8_t app_owned:1;
	uint8_t app_accepted:1;
	uint8_t ugni_ep_bound:1; /* can use SMSG */
	uint8_t ugni_term_sent:1; /* TERM msg has been sent */
	uint8_t ugni_term_recv:1; /* TERM msg has been received */
	uint8_t ugni_ack_term_sent:1; /* ACK_TERM msg has been sent */
	uint8_t ugni_ack_term_recv:1; /* ACK_TERM msg has been received */
	gni_ep_handle_t gni_ep;
	gni_cq_handle_t gni_cq;

	struct z_ugni_ev uev;
	struct zap_event conn_ev;

	/*
	 * The counter of retries to unbind the GNI endpoint
	 */
	int unbind_count;

	LIST_ENTRY(z_ugni_ep) link;
	LIST_ENTRY(z_ugni_ep) deferred_link;

#if defined(ZAP_DEBUG) || defined(DEBUG)

#define UEP_EPOLL_RECORD_SZ 12
	uint32_t epoll_record[UEP_EPOLL_RECORD_SZ];
	uint32_t epoll_record_curr;
#endif /* ZAP_DEBUG || DEBUG */

	uint64_t next_msg_id;
	uint16_t next_msg_seq;

	gni_smsg_attr_t local_smsg_attr;
	gni_smsg_attr_t remote_smsg_attr;

	struct z_ugni_epoll_ctxt sock_epoll_ctxt;
	union {
		char buff[0];
		struct zap_ugni_msg_hdr hdr;
		struct z_ugni_sock_msg_conn_req conn_req;
		struct z_ugni_sock_msg_conn_accept conn_accept;
	} sock_buff;
	int sock_off; /* offset into sock_buff */

	struct z_ugni_ep_idx *ep_idx;
	struct z_ugni_wrq pending_wrq;
	struct z_ugni_wrq submitted_wrq;
	int post_credit; /* post credit */

	struct zap_ugni_msg *rmsg; /* current recv msg */
};

/* NOTE: This is the maximum entire message length submitted to GNI_SmsgSend().
 * The `max_msg` length reported to the application is ZAP_UGNI_MSG_MAX -
 * `max_hdr_len`. */
#define ZAP_UGNI_MSG_SZ_MAX 2048

#define ZAP_UGNI_THREAD_EP_MAX 8192 /* max endpoints per thread */
#define ZAP_UGNI_EP_SQ_DEPTH 4
#define ZAP_UGNI_EP_RQ_DEPTH 4
#define ZAP_UGNI_SCQ_DEPTH (ZAP_UGNI_EP_SQ_DEPTH * ZAP_UGNI_THREAD_EP_MAX)
#define ZAP_UGNI_RCQ_DEPTH (ZAP_UGNI_EP_RQ_DEPTH * ZAP_UGNI_THREAD_EP_MAX)
#define ZAP_UGNI_EP_MBOX_SZ (ZAP_UGNI_EP_RQ_DEPTH * ZAP_UGNI_MSG_SZ_MAX)
#define ZAP_UGNI_THR_MBOX_SZ (ZAP_UGNI_EP_MBOX_SZ * ZAP_UGNI_THREAD_EP_MAX)

struct z_ugni_io_thread {
	struct zap_io_thread zap_io_thread;
	int efd; /* epoll file descriptor */
	gni_cq_handle_t scq; /* Send completion queue: PostRdma, SmsgSend */
	gni_cq_handle_t rcq; /* Recv completion queue: SmsgGetNext (recv) */
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
	 *   GNI_CQ_GET_TYPE() only reports POST, SMSG, DMAPP, or MSGQ which
	 *   does not tell whether the cqe is for a send or recv.
	 *
	 *   gni_cq_entry_t is uint64_t. For SMSG SEND completion on the SENDER
	 *   the first 32-bit is the `msg_id` supplied to `GNI_SmsgSend()`. For
	 *   the SMSG RECV completion, the same first 32-bit is the `inst_id` of
	 *   the sender.
	 *
	 *   Since `GNI_GetComleted()` is only for GNI_Post(RDMA/FMA)
	 *   completions, it looks like the `GNI_SmsgSend()` completion event
	 *   has to rely on `inst_id/msg_id` from the cq entry.
	 *
	 *   REMARK: Not sure if GNI_CQ_GET_SOURCE() would help us differentiate
	 *           the SEND/RECV. There is little documentation about it.
	 */
	gni_comp_chan_handle_t cch; /* Completion Channel */
	int cch_fd; /* cq channel file descriptor */

	struct z_ugni_epoll_ctxt cq_epoll_ctxt;

	/* endpoint index resources, used for mapping GNI SMSG event to uep */
	struct z_ugni_ep_idx ep_idx[ZAP_UGNI_THREAD_EP_MAX];
	struct z_ugni_ep_idx *ep_idx_head, *ep_idx_tail;

	/* mailboxes for endpoints in this thread */
	uint32_t mbox_sz; /* size per mbox */
	gni_mem_handle_t mbox_mh; /* memory handle for mbox */
	char *mbox; /* mailboxes memory */

	struct z_ugni_wrq stalled_wrq;

	struct rbt zq; /* zap event queue, protected by zap_io_thread.mutex */
	int zq_fd[2]; /* zap event queue fd (for epoll notification) */
	struct z_ugni_epoll_ctxt zq_epoll_ctxt;
};

#endif
