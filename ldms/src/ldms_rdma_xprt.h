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
#ifndef __LDMS_XPRT_RDMA_H__
#define __LDMS_XPRT_RDMA_H__
#include <sys/queue.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <semaphore.h>
#include "ldms_xprt.h"

#define SQ_DEPTH 4
#define RQ_DEPTH 4
#define RQ_BUF_SZ 2048
#define SQ_SGE 1
#define RQ_SGE 1

#pragma pack(4)
struct rdma_request_hdr {
	uint32_t credits;
};

#define RDMA_CREDIT_UPDATE_CMD (LDMS_CMD_XPRT_PRIVATE | 0x1)
struct rdma_credit_update_req {
	struct rdma_request_hdr rdma_hdr;
	struct ldms_request_hdr hdr;
};
#pragma pack()

struct rdma_buffer {
	char *data;
	size_t data_len;
	struct ibv_mr *mr;
	LIST_ENTRY(rdma_buffer) link; /* linked list entry */
};

struct rdma_buf_remote_data {
	uint64_t meta_buf;
	uint32_t meta_rkey;
	uint32_t meta_size;
	uint64_t data_buf;
	uint32_t data_rkey;
	uint32_t data_size;
};

struct rdma_buf_local_data {
	void *meta;
	size_t meta_size;
	struct ibv_mr *meta_mr;
	void *data;
	size_t data_size;
	struct ibv_mr *data_mr;
};

enum rdma_conn_status {
	CONN_IDLE = 0,
	CONN_CONNECTING,
	CONN_CONNECTED,
	CONN_CLOSED,
	CONN_ERROR
};

/**
 * RDMA Transport private data
 */

struct ldms_rdma_xprt;
struct rdma_context {
	void *usr_context;      /* user context if any */

	struct ldms_rdma_xprt *x;
	struct ibv_send_wr wr;
	struct ibv_sge sge;

	enum ibv_wc_opcode op;  /* work-request op (can't be trusted
				in wc on error */
	struct rdma_buffer *rb; /* RDMA buffer if any */
	
	TAILQ_ENTRY(rdma_context) pending_link; /* pending i/o */
};

LIST_HEAD(rdma_buffer_list, rdma_buffer);

struct ldms_rdma_xprt {
	struct ldms_xprt *xprt;
	int server;			/* 0 iff client */
	enum rdma_conn_status conn_status;
	struct ibv_comp_channel *cq_channel;
	struct ibv_cq *rq_cq;
	struct ibv_cq *sq_cq;
	struct ibv_pd *pd;
	struct ibv_qp *qp;

	/* CM stuff */
	sem_t sem;
	pthread_t server_thread;
	struct rdma_event_channel *cm_channel;
	struct rdma_cm_id *cm_id; /* connection on client side,
				   * listener on service side. */

	int rem_rq_credits;	/* peer's RQ available credits */
	int lcl_rq_credits;	/* local RQ available credits */
	int sq_credits;		/* local SQ credits */

	pthread_mutex_t credit_lock;
	TAILQ_HEAD(xprt_credit_list, rdma_context) io_q;

	LIST_ENTRY(ldms_rdma_xprt) client_link;
};

#endif
