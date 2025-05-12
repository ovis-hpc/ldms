/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2022 Open Grid Computing, Inc. All rights reserved.
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
/**
 * \file ldms_rail.h
 * \brief LDMS transport rail.
 *
 * This file contains structures and function prototypes for LDMS transport
 * rails, or just "rails" for short.
 *
 */

#ifndef __LDMS_RAIL_H__
#define __LDMS_RAIL_H__
#include <semaphore.h>
#include <arpa/inet.h>

#include "ovis_ref/ref.h"
#include "coll/rbt.h"
#include "ldms.h"
#include "ldms_xprt.h"

#pragma pack ( push, 4 )
struct ldms_rail_id_s {
	int ip4_addr; /* ip4_addr is resolved after zap-connected */
	int pid; /* process ID */
	uint64_t rail_gn;
};

struct ldms_rail_conn_msg_s {
	/* Compatible to existing LDMS connect message */
	struct ldms_version ver;
	char auth_name[LDMS_AUTH_NAME_MAX + 1];
	/* -------- */

	enum ldms_conn_type conn_type;

	/* The rail part */

	int64_t rate_limit;  /* send/recv rate limits in bytes/sec */
	int64_t recv_quota; /* receive limits in bytes */

	int n_eps; /* number of endpoints */
	uint32_t idx; /* endpoint index in the rail */
	int pid;
	uint64_t rail_gn;

	int msg_enabled; /* 0 if peer does not enable message service */

};
#pragma pack(pop)

typedef enum ldms_rail_ep_state_e {
	LDMS_RAIL_EP_INIT = 0,
	LDMS_RAIL_EP_LISTENING,
	LDMS_RAIL_EP_ACCEPTING,
	LDMS_RAIL_EP_CONNECTING,
	LDMS_RAIL_EP_CONNECTED,
	LDMS_RAIL_EP_PEER_CLOSE,
	LDMS_RAIL_EP_CLOSE,
	LDMS_RAIL_EP_ERROR,
	LDMS_RAIL_EP_REJECTED,
} ldms_rail_ep_state_t;

typedef struct ldms_rail_s *ldms_rail_t;

struct ldms_rail_rate_quota_s {
	uint64_t quota;    /* the bytes available in the second */
	uint64_t rate;      /* the byte/sec */
	struct timespec ts; /* timestamp of the last acquire */
};

struct __pending_sbuf_s {
	uint32_t hop_num;
	struct timespec recv_ts;
	TAILQ_ENTRY(__pending_sbuf_s) entry;
	struct __msg_buf_s *sbuf;
};

/* a structure that tracks ldms xprt in the rail */
struct ldms_rail_ep_s {
	/* track individual ep state to properly handle it */
	ldms_rail_ep_state_t state;
	ldms_t ep;
	int idx; /* index in the rail */
	ldms_rail_t rail;
	uint64_t send_quota; /* peer's recv limit */
	uint64_t send_quota_debt; /* debt due to quota reconfiguration */
	struct rbt sbuf_rbt; /* stream message buffer */
	int remote_is_rail;
	struct ldms_rail_rate_quota_s rate_quota; /* rate quota */
	uint64_t pending_ret_quota; /* pending return quota */
	int in_eps_stq;

	TAILQ_HEAD(, __pending_sbuf_s) sbuf_tq; /* pending fwd stream msgs */
	/*
	 * Array of operation context lists, indexed by `ldms_xprt_ops_e`.
	 *
	 * Each list stores operation contexts for the corresponding operation type.
	 * Operation contexts track profiling data for various operations,
	 * such as lookups, updates, and stream operations.
	 */
	struct ldms_op_ctxt_list op_ctxt_lists[LDMS_XPRT_OP_COUNT];
};

typedef struct ldms_rail_dir_ctxt_s {
	struct ldms_rail_s *r;
	ldms_dir_cb_t app_cb;
	void *cb_arg;
	uint32_t flags;
	TAILQ_ENTRY(ldms_rail_dir_ctxt_s) tqe; /* for rail->dir_notify_tq */
} *ldms_rail_dir_ctxt_t;

struct ldms_rail_s {
	enum ldms_xtype_e xtype;
	struct ldms_xprt_ops_s ops;
	int disconnected;

	/* Semaphore and return code for synchronous xprt calls */
	sem_t sem;
	int sem_rc;

	char name[LDMS_MAX_TRANSPORT_NAME_LEN];
	struct ref_s ref;
	char auth_name[LDMS_AUTH_NAME_MAX + 1];
	struct attr_value_list *auth_av_list;
	ldms_log_fn_t log;
	LIST_ENTRY(ldms_rail_s) rail_link;

	/* These are informational. The actual quotas are in eps[idx]. */
	uint64_t recv_quota;      /* 0xffffffffffffffff is unlimited */
	uint64_t recv_rate_limit; /* 0xffffffffffffffff is unlimited */
	uint64_t send_quota;      /* 0xffffffffffffffff is unlimited */
	uint64_t send_rate_limit; /* 0xffffffffffffffff is unlimited */

	ldms_event_cb_t event_cb;
	void *event_cb_arg;

	void *app_ctxt;
	app_ctxt_free_fn app_ctxt_free;

	pthread_mutex_t mutex; /* mainly for state */

	struct rbt ch_cli_rbt; /* stream clients from the peer */

	ldms_rail_ep_state_t state;

	/* List of contexts of `dir` that requested NOTIFY. These will be freed
	 * when the rail is destroyed. */
	TAILQ_HEAD(, ldms_rail_dir_ctxt_s) dir_notify_tq;

	/* rbn and rail_id for passive rail */
	struct rbn rbn;
	struct ldms_rail_id_s rail_id;

	int max_msg; /* max msg of the underlying zap endpoints */

	int legacy_peer; /* 0 if peer is rail, 1 if peer is legacy LDMS */

	uint64_t conn_id;

	int peer_msg_enabled;

	// struct ldms_xprt_stats_s stats;
	struct timespec connected_ts;
	struct timespec disconnected_ts;

	uint32_t lookup_rr; /* lookup round-robin index */

	int    connected_eps; /* track the number of connected endpoints */
	int    connecting_eps; /* track the number of outstanding connecting/accepting endpoints */
	int    rejected_eps; /* track the number of rejected endpoints */
	int    connreq_eps; /* track the number of EXPECTING conn req */
	int    conn_error_eps; /* track the number of connect error endpoints */
	int    n_eps;          /* number of endpoints */
	struct ldms_rail_ep_s eps[OVIS_FLEX];
};

struct rail_proc_id_s {
	uint32_t ip4_addr;
	uint32_t pid;
};

int get_rail_proc_id(struct rail_proc_id_s *out);

/**
 * Copy sockaddr \c sa address into \c la.
 *
 * The sa.sa_family is in host-endian.
 * The address and port is in little endian.
 *
 * \note Only support sockaddr_in and sockaddr_in6.
 */
int sockaddr2ldms_addr(struct sockaddr *sa, struct ldms_addr *la);

/**
 * Wrapper of 'inet_ntop()' for sockaddr.
 */
const char *sockaddr_ntop(struct sockaddr *sa, char *buff, size_t sz);

void __rail_ep_quota_return(struct ldms_rail_ep_s *rep, int quota, int rc);

int __rep_flush_sbuf_tq(struct ldms_rail_ep_s *rep);
int __rep_quota_acquire(struct ldms_rail_ep_s *rep, uint64_t q);

/**
 * For debugging ...
 */
int ldms_xprt_rail_pending_ret_quota_get(ldms_t x, uint64_t *out, int n);
int ldms_xprt_rail_in_eps_stq_get(ldms_t x, uint64_t *out, int n);

#endif /* __LDMS_RAIL_H__ */
