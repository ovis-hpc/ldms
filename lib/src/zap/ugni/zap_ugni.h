/**
 * Copyright (c) 2014-2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2014-2017 Open Grid Computing, Inc. All rights reserved.
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
#include "ovis-lib-config.h"
#include "coll/rbt.h"

#include "zap.h"
#include "zap_priv.h"

/**
 * \brief Value for TCP_KEEPIDLE option for initiator side socket.
 *
 * This will make Linux send 'keep alive' probe when the socket being idle for
 * 10 seconds (only for initiator side zap connections).
 *
 * \note Default value of TCP_KEEPIDLE is 7200 sec = 2 hrs
 */
#define ZAP_UGNI_SOCK_KEEPIDLE 10

/**
 * \brief Value for TCP_KEEPCNT option for initiator side socket.
 *
 * For this setting, a connection will be dropped after 3 probes.
 *
 * \note Default TCP_KEEPCNT is 9
 */
#define ZAP_UGNI_SOCK_KEEPCNT 3

/**
 * \brief Value for TCP_KEEPINTVL option for initiator side socket.
 *
 * This is a time between probes after idle (set to 2 seconds).
 *
 * \note Default TCP_KEEPINTVL is 75 seconds
 */
#define ZAP_UGNI_SOCK_KEEPINTVL 2

/**
 * \brief CQ size.
 */
#define ZAP_UGNI_CQ_DEPTH 2048

/**
 * This limits the fan-out of the aggregator.
 */
#define ZAP_UGNI_MAX_BTE 8192

/*
 * The length of the string
 * lcl=<local ip address:port> <--> rmt=<remote ip address:port>
 */
#define ZAP_UGNI_EP_NAME_SZ 64

#define ZAP_UGNI_INIT_RECV_BUFF_SZ 4096

struct zap_ugni_map {
	struct zap_map map;
	gni_mem_handle_t gni_mh; /**< GNI memory handle */
};

struct z_ugni_key {
	struct rbn rb_node;
	struct zap_ugni_map *map; /**< reference to zap_map */
};

/**
 * ZAP_UGNI message types.
 */
typedef enum zap_ugni_msg_type {
	ZAP_UGNI_MSG_NONE,        /**< Dummy first type */
	ZAP_UGNI_MSG_CONNECT,     /**< Connect data */
	ZAP_UGNI_MSG_REGULAR,     /**< Regular send-receive */
	ZAP_UGNI_MSG_RENDEZVOUS,  /**< Share zap_map */
	ZAP_UGNI_MSG_ACCEPTED,    /**< Connection accepted */
	ZAP_UGNI_MSG_REJECTED,    /**< Connection rejected */
	ZAP_UGNI_MSG_ACK_ACCEPTED,/**< Acknowledge accepted msg */
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
	[ZAP_UGNI_MSG_TYPE_LAST]   =  "ZAP_UGNI_MSG_TYPE_LAST"
};

static const char *zap_ugni_msg_type_str(zap_ugni_msg_type_t type)
{
	if (type < ZAP_UGNI_MSG_NONE || ZAP_UGNI_MSG_TYPE_LAST < type)
		return "ZAP_UGNI_MSG_OUT_OF_RANGE";
	return __zap_ugni_msg_type_str[type];
}

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

static const char *zap_ugni_type_str(zap_ugni_type_t type)
{
	if (type < ZAP_UGNI_TYPE_NONE || ZAP_UGNI_TYPE_LAST < type)
		return "ZAP_UGNI_TYPE_OUT_OF_RANGE";
	return __zap_ugni_type_str[type];
}

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
	uint64_t addr; /**< Address in the map */
	uint32_t data_len; /**< Length */
	char msg[OVIS_FLEX]; /**< Context */
};

/**
 * Message for zap connection accepted.
 */
struct zap_ugni_msg_accepted {
	struct zap_ugni_msg_hdr hdr;
	uint32_t inst_id; /**< inst_id of the accepter (passive side). */
	uint32_t pe_addr; /**< peer address of the accepter (passive side). */
	uint32_t data_len;
	char data[OVIS_FLEX];
};

static char ZAP_UGNI_SIG[8] = "UGNI";

/**
 * Message for zap connection.
 */
struct zap_ugni_msg_connect {
	struct zap_ugni_msg_hdr hdr;
	char pad[12];  /**< padding so that ugni ver,xprt are aligned with sock ver,xprt */
	struct zap_version ver;
	char sig[8];     /**< transport type signature. */
	uint32_t inst_id; /**< inst_id of the requester (active side). */
	uint32_t pe_addr; /**< peer address of the requester (active side). */
	uint32_t data_len; /**< Connection data*/
	char data[OVIS_FLEX];      /**< Size of connection data */
};

#pragma pack()

struct zap_ugni_send_wr {
	STAILQ_ENTRY(zap_ugni_send_wr) link;
	off_t off; /* offset of to be written */
	size_t alen; /* remaining length after data + off */
	char data[OVIS_FLEX];
};

struct zap_ugni_recv_buff {
	size_t len; /* length of the bufferred data */
	size_t alen; /* available allocated length after data + len */
	char data[OVIS_FLEX];
};

struct zap_ugni_post_desc;
LIST_HEAD(zap_ugni_post_desc_list, zap_ugni_post_desc);
struct z_ugni_ep {
	struct zap_ep ep;

	int sock;
	int node_id;
	int ep_id; /* The index in the endpoint array */
	struct ovis_event_s io_ev;
	struct ovis_event_s deferred_ev;
	char *conn_data;
	size_t conn_data_len;
	uint8_t rejecting;
	uint8_t sock_connected;
	uint8_t app_accepted;
	uint8_t ugni_ep_bound;
	gni_ep_handle_t gni_ep;

	struct zap_ugni_post_desc_list post_desc_list;
	struct zap_event conn_ev;

	/*
	 * The counter of retries to unbind the GNI endpoint
	 */
	int unbind_count;

	STAILQ_HEAD(, zap_ugni_send_wr) sq; /* send queue */
	struct zap_ugni_recv_buff *rbuff; /* recv buffer */

	LIST_ENTRY(z_ugni_ep) link;
	LIST_ENTRY(z_ugni_ep) deferred_link;

#if defined(ZAP_DEBUG) || defined(DEBUG)

#define UEP_EPOLL_RECORD_SZ 12
	uint32_t epoll_record[UEP_EPOLL_RECORD_SZ];
	uint32_t epoll_record_curr;
#endif /* ZAP_DEBUG || DEBUG */
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

static inline struct z_ugni_ep *z_sock_from_ep(zap_ep_t *ep)
{
	return (struct z_ugni_ep *)ep;
}

#endif
