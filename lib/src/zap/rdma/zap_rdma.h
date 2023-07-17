/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2015,2017,2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2015,2017,2020 Open Grid Computing, Inc. All rights reserved.
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
#ifndef __ZAP_RDMA_H__
#define __ZAP_RDMA_H__
#include <sys/queue.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <semaphore.h>
#include "../zap.h"
#include "../zap_priv.h"

#define SQ_DEPTH 4
#define RQ_DEPTH 4
#define RQ_BUF_SZ 2048
#define SQ_SGE 1
#define RQ_SGE 1

/* number of buffers in a pool */
#define Z_RDMA_POOL_SZ 256

#pragma pack(4)
enum z_rdma_message_type {
	Z_RDMA_MSG_CREDIT_UPDATE = 1,
	Z_RDMA_MSG_SEND,
	Z_RDMA_MSG_RENDEZVOUS,
	Z_RDMA_MSG_ACCEPT,
	Z_RDMA_MSG_REJECT,
	Z_RDMA_MSG_SEND_MAPPED,
};

struct z_rdma_message_hdr {
	uint16_t credits;
	uint16_t msg_type;
};

struct z_rdma_share_msg {
	struct z_rdma_message_hdr hdr;
	uint32_t acc;
	uint32_t len;
	uint32_t rkey;
	uint64_t va;
	char msg[OVIS_FLEX];
};

struct z_rdma_accept_msg {
	struct z_rdma_message_hdr hdr;
	uint32_t len;
	char data[OVIS_FLEX];
};

struct z_rdma_reject_msg {
	struct z_rdma_message_hdr hdr;
	uint32_t len;
	char msg[OVIS_FLEX];
};

/* union of all messages */
union z_rdma_msg {
	struct z_rdma_message_hdr hdr;   /* header access */
	struct z_rdma_share_msg   share;
	struct z_rdma_accept_msg  accept;
	struct z_rdma_reject_msg  reject;
	char bytes[0]; /* bytes access */
};

#pragma pack()

struct z_rdma_map {
	uint32_t rkey;
	struct ibv_mr *mr[OVIS_FLEX];
};

#define Z_RDMA_MAP_SZ (sizeof(struct z_rdma_map) + ZAP_RDMA_MAX_PD*sizeof(struct ibv_mr *))

struct z_rdma_buffer {
	size_t data_len; /* data written to the buffer */
	struct z_rdma_ep *rep; /* the endpoint who owns the buffer */
	struct z_rdma_buffer_pool *pool; /* pool that this buffer resides */
	LIST_ENTRY(z_rdma_buffer) free_link; /* link to next free entry in the pool */
	union {
		union z_rdma_msg msg[0]; /* message buffer */
		char bytes[RQ_BUF_SZ];
	};
};

struct z_rdma_buffer_pool {
	struct ibv_mr *mr; /* MR for the memory pool */
	int num_alloc; /* number of buffers allocated */
	int _pad; /* padding */
	LIST_HEAD(, z_rdma_buffer) free_buffer_list; /* free buffer list */
	LIST_ENTRY(z_rdma_buffer_pool) pool_link; /* link to other pools */
	struct z_rdma_buffer buffer[Z_RDMA_POOL_SZ];
};

/**
 * RDMA Transport private data
 */

struct z_rdma_context {
	void *usr_context;      /* user context if any */

	zap_ep_t ep;
	struct ibv_send_wr wr;
	struct ibv_sge sge[2];

	enum ibv_wc_opcode op;  /* work-request op (can't be trusted
				in wc on error */
	struct z_rdma_buffer *rb; /* RDMA buffer if any */

	TAILQ_ENTRY(z_rdma_context) pending_link; /* pending i/o */
	LIST_ENTRY(z_rdma_context) active_ctxt_link;

	int is_pending; /* for debugging */
};

#pragma pack(push, 1)
struct z_rdma_conn_data {
	struct zap_version v;
	uint8_t data_len;
	char data[OVIS_FLEX];
};
#pragma pack(pop)

#define RDMA_CONN_DATA_MAX (196)
#define ZAP_RDMA_CONN_DATA_MAX (RDMA_CONN_DATA_MAX - sizeof(struct z_rdma_conn_data))

#define RDMA_ACCEPT_DATA_MAX (196)
#define ZAP_RDMA_ACCEPT_DATA_MAX RDMA_ACCEPT_DATA_MAX

struct z_rdma_epoll_ctxt {
	enum {
		Z_RDMA_EPOLL_CM = 1,
		Z_RDMA_EPOLL_CQ,
	} type;
	union {
		struct rdma_event_channel *cm_channel;
		struct z_rdma_ep *cq_rep;
	};
};

struct z_rdma_ep {
	struct zap_ep ep;
	struct ibv_comp_channel *cq_channel;
	struct ibv_cq *rq_cq;
	struct ibv_cq *sq_cq;
	struct ibv_pd *pd;
	struct ibv_qp *qp;

	int ce_id;

	union {
		struct z_rdma_conn_data conn_data;
		char ___[RDMA_CONN_DATA_MAX];
	};

	/**
	 * An endpoint has a parent endpoint when it is created from
	 * ::handle_connect_request(). The idea is that the parent endpoint
	 * should be destroyed after the child endpoint. When a child endpoint
	 * is created, rhe refcount in \c parent_ep will be increased by 1. When
	 * a child endpoint is destroyed, \c parent_ep refcount will be
	 * decreased by 1.
	 *
	 * If an endpoint is not an endpoint created from
	 * ::handle_connect_request(), parent_ep is NULL.
	 */
	struct z_rdma_ep *parent_ep;

	enum {
		Z_RDMA_PASSIVE_NONE = 0,
		Z_RDMA_PASSIVE_ACCEPT = 1,
		Z_RDMA_PASSIVE_REJECT,
	} conn_req_decision;

	/* Flag for deferred disconnected event */
	int deferred_disconnected;

	/* CM stuff */
	sem_t sem;
	pthread_t server_thread;
	struct rdma_event_channel *cm_channel;
	struct rdma_cm_id *cm_id; /* connection on client side,
				   * listener on service side. */

	uint16_t rem_rq_credits;	/* peer's RQ available credits */
	uint16_t lcl_rq_credits;	/* local RQ available credits */
	uint16_t sq_credits;		/* local SQ credits */

	pthread_mutex_t credit_lock;
	TAILQ_HEAD(xprt_credit_list, z_rdma_context) io_q;
	LIST_HEAD(active_ctxt_list, z_rdma_context) active_ctxt_list;

	/* send/recv buffers */
#if 0
	int num_bufs;  /* total buffers: 4 + SQ_DEPTH + RQ_DEPTH */
	size_t buf_sz; /* size of each buffer */
	char *buf_pool;
	struct z_rdma_buffer *buf_objs;
	struct ibv_mr *buf_pool_mr;
	LIST_HEAD(buf_free_list, z_rdma_buffer) buf_free_list;
#endif
	LIST_HEAD(, z_rdma_buffer_pool) vacant_pool;
	LIST_HEAD(, z_rdma_buffer_pool) full_pool;

#ifdef ZAP_DEBUG
	int rejected_count;
	int rejected_conn_error_count;
#endif /* ZAP_DEBUG */

	TAILQ_ENTRY(z_rdma_ep) ep_link;

	enum {
		Z_RDMA_DEV_OTHER,
		Z_RDMA_DEV_HFI1,  /* omnipath */
	} dev_type;
	int cm_channel_enabled;
	int cq_channel_enabled;
	pthread_cond_t io_q_cond;

	struct z_rdma_epoll_ctxt cq_ctxt;
};

typedef struct z_rdma_io_thread {
	struct zap_io_thread zap_io_thread;
	int efd; /* epoll fd */
	struct z_rdma_epoll_ctxt cm_ctxt;
	int devices_len; /* number of devices */
} *z_rdma_io_thread_t;

/* Get z_rdma_io_thread from struct z_rdma_ep */
#define Z_RDMA_EP_THR(ep) ((z_rdma_io_thread_t)((struct zap_ep *)(ep))->thread)

#endif
