/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2015,2017,2019-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2015,2017,2019-2020 Open Grid Computing, Inc. All rights reserved.
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
#ifndef __ZAP_FABRIC_H__
#define __ZAP_FABRIC_H__

#include <sys/queue.h>
#include <semaphore.h>
#include <rdma/fabric.h>
#include <sys/epoll.h>
#include "../zap.h"
#include "../zap_priv.h"

#define SQ_DEPTH 4
#define RQ_DEPTH 4
#define RQ_BUF_SZ 2048

/* Libfabric version required by zap. */
#define ZAP_FI_VERSION_MAJOR	1
#define ZAP_FI_VERSION_MINOR	5
#define ZAP_FI_VERSION		FI_VERSION(ZAP_FI_VERSION_MAJOR,ZAP_FI_VERSION_MINOR)

#define ZAP_FI_MAX_DOM 128

/* structure to hold <fabric, domain> pair */
struct z_fi_fabdom {
	struct fi_info *info;
	struct fid_fabric *fabric;
	struct fid_domain *domain;
	int id;
};

#pragma pack(4)
enum z_fi_message_type {
	Z_FI_MSG_CREDIT_UPDATE = 1,
	Z_FI_MSG_SEND,
	Z_FI_MSG_RENDEZVOUS,
	Z_FI_MSG_ACCEPT,
	Z_FI_MSG_REJECT,
};

struct z_fi_message_hdr {
	uint16_t credits;
	uint16_t msg_type;
};

struct z_fi_share_msg {
	struct z_fi_message_hdr hdr;
	uint32_t acc;
	uint32_t len;
	uint64_t rkey;
	uint64_t va;
	char msg[OVIS_FLEX];
};

struct z_fi_accept_msg {
	struct z_fi_message_hdr hdr;
	uint32_t len;
	char data[OVIS_FLEX];
};

struct z_fi_reject_msg {
	struct z_fi_message_hdr hdr;
	uint32_t len;
	char msg[OVIS_FLEX];
};

#pragma pack()

struct z_fi_map {
	pthread_mutex_t lock;
	struct fid_mr	*mr[ZAP_FI_MAX_DOM];
};

struct z_fi_buffer {
	char *buf;
	struct z_fi_message_hdr *msg;
	size_t buf_len;
	size_t data_len;
	struct z_fi_ep *rep;
	struct z_fi_buffer_pool *pool;
	LIST_ENTRY(z_fi_buffer) free_link;  /* for per-ep free list */
};

/*
 * buffer_pool memory allocation format:
 *     z_fi_buffer_pool|[buf_obj ...]|<pad (if needed)>|[buf ...]
 *
 * When the buffer poll is fully allocated, the pool is moved into the
 * `full_pool` list. When it has at least one available slot, it is moved back
 * to the `vacant_pool` list.
 *
 */
struct z_fi_buffer_pool {
	char *buf_pool;
	int num_bufs;   /* total number of buf */
	int num_alloc; /* number of allocated buf */
	size_t buf_sz; /* size of each buf */
	struct fid_mr *buf_pool_mr;
	LIST_ENTRY(z_fi_buffer_pool) entry; /* for `full_pool` or `vacant_pool` */
	LIST_HEAD(, z_fi_buffer) buf_free_list; /* to maintain free buf in the pool */
	struct z_fi_buffer buf_obj[];
};

enum z_fi_op {
	Z_FI_WC_SEND,
	Z_FI_WC_SHARE,
	Z_FI_WC_RECV,
	Z_FI_WC_RDMA_READ,
	Z_FI_WC_RDMA_WRITE,
	Z_FI_WC_SEND_MAPPED,
};

struct z_fi_context {
	struct fi_context fi_context[2];   /* libfabric context; must be first */
	void *usr_context;                 /* user context if any */
	enum z_fi_op op;                   /* type of work request */
	struct z_fi_ep *ep;                /* transport endpoint */
	union {
		struct {
			struct z_fi_buffer *rb;
		} send;
		struct {
			struct z_fi_buffer *rb;
		} recv;
		struct {
			struct zap_map *src_map;
			struct zap_map	*dst_map;
			void		*src_addr;
			void		*dst_addr;
			size_t		len;
		} rdma;
		struct {
			struct z_fi_buffer *rb; /* to hold fi_prefix + z_fi hdr */
			struct iovec iov[2]; /* for SEND_MAPPED, iov[0]:prov_prefix+msghdr,
					      * iov[1]:payload */
			void *mr_desc[2];    /* MR desc corresponding to iov */
		} send_mapped;
	} u;
	TAILQ_ENTRY(z_fi_context) pending_link; /* pending i/o */
	LIST_ENTRY(z_fi_context) active_ctxt_link;
	int pending; /* pending in rep->io_q */
};

#pragma pack(push, 1)
struct z_fi_conn_data {
	struct zap_version v;
	uint8_t data_len;
	char data[OVIS_FLEX];
};
#pragma pack(pop)

#define CONN_DATA_MAX (56)
#define ZAP_CONN_DATA_MAX (CONN_DATA_MAX - sizeof(struct z_fi_conn_data))

#define ACCEPT_DATA_MAX (196)
#define ZAP_ACCEPT_DATA_MAX ACCEPT_DATA_MAX

typedef struct z_fi_io_thread *z_fi_io_thread_t;

struct z_fi_epoll_ctxt {
	enum {
		Z_FI_EPOLL_CM,
		Z_FI_EPOLL_CQ,
	} type;
	struct z_fi_ep *rep;
};

struct z_fi_ep {
	struct zap_ep ep;

	int fabdom_id;
	char *provider_name;
	char *domain_name;

	/* libfabric objects */
	struct fi_info *fi;
	struct fid_fabric *fabric;
	struct fid_domain *domain;
	struct fid_ep *fi_ep;
	struct fid_pep *fi_pep;
	struct fid_cq *cq;
	int cq_fd;
	int cq_fids_idx;
	int eq_fids_idx;
	int num_ctxts;

	/*
	int num_bufs;
	size_t buf_sz;
	char *buf_pool;
	struct z_fi_buffer *buf_objs;
	struct fid_mr *buf_pool_mr;
	LIST_HEAD(buf_free_list, z_fi_buffer) buf_free_list;
	*/

	pthread_mutex_t	buf_free_list_lock;

	LIST_HEAD(, z_fi_buffer_pool) vacant_pool;
	LIST_HEAD(, z_fi_buffer_pool) full_pool;
	int num_empty_pool; /* number of pools with full vacancy */

	union {
		struct z_fi_conn_data conn_data; /* flexi */
		char ___[CONN_DATA_MAX];
	};

	/**
	 * An endpoint has a parent endpoint when it is created from
	 * ::handle_connect_request(). The idea is that the parent endpoint
	 * should be destroyed after the child endpoint. When a child endpoint
	 * is created, the refcount in \c parent_ep will be increased by 1. When
	 * a child endpoint is destroyed, \c parent_ep refcount will be
	 * decreased by 1.
	 *
	 * If an endpoint is not an endpoint created from
	 * ::handle_connect_request(), parent_ep is NULL.
	 */
	struct z_fi_ep *parent_ep;

	enum {
		Z_FI_PASSIVE_NONE = 0,
		Z_FI_PASSIVE_ACCEPT = 1,
		Z_FI_PASSIVE_REJECT,
	} conn_req_decision;

	/* Flag for deferred disconnected event */
	int deferred_disconnected;

	/* CM stuff */
	sem_t sem;
	pthread_t server_thread;
	struct fid_eq *eq;
	int cm_fd;

	uint16_t rem_rq_credits;	/* peer's RQ available credits */
	uint16_t lcl_rq_credits;	/* local RQ available credits */
	uint16_t sq_credits;		/* local SQ credits */

	pthread_mutex_t credit_lock;
	TAILQ_HEAD(xprt_credit_list, z_fi_context) io_q;
	LIST_HEAD(active_ctxt_list, z_fi_context) active_ctxt_list;

#ifdef ZAP_DEBUG
	int rejected_count;
	int rejected_conn_error_count;
#endif /* ZAP_DEBUG */

	LIST_ENTRY(z_fi_ep) ep_link;
	pthread_cond_t io_q_cond;

	struct z_fi_epoll_ctxt cm_epoll_ctxt;
	struct z_fi_epoll_ctxt cq_epoll_ctxt;
};

struct z_fi_io_thread {
	struct zap_io_thread zap_io_thread;
	int efd; /* epoll fd */
};

#define Z_FI_THR(p) ((struct z_fi_io_thread *)(p))

#endif
