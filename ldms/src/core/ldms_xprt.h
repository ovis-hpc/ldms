/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2018 Open Grid Computing, Inc. All rights reserved.
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

#ifndef __LDMS_XPRT_H__
#define __LDMS_XPRT_H__

#include <time.h>
#include <semaphore.h>
#include <sys/queue.h>
#include <sys/types.h>

#include <zap/zap.h>
#include <coll/rbt.h>
#include <ovis_ev/ev.h>
#include <ovis_ref/ref.h>

#include "ldms.h"
#include "ldms_auth.h"

#pragma pack(4)

/*
 * Callback function invoked when peer acknowledges a deleted set
 *
 * xprt   The transport handle
 * status 0 on success, errno if there was a problem sending the request
 * set    The set handle
 * cb_arg The cb_arg parameter to the ldsm_xprt_set_delete
 *        function
 */
typedef void (*ldms_set_delete_cb_t)(ldms_t xprt, int status, ldms_set_t set, void *cb_arg);

/**
 * If set in the push_flags, the set changes will be automatically
 * pushed by ldms_transaction_end()
 */
#define LDMS_RBD_F_PUSH		1	/* registered for push */
#define LDMS_RBD_F_PUSH_CHANGE	2	/* registered for changes */
#define LDMS_RBD_F_PUSH_CANCEL	4	/* cancel pending */

/* Entry of ldms_set->push_coll */
struct ldms_push_peer {
	ldms_t xprt; /* Transport to the peer */
	uint64_t remote_set_id; /* set_id of the peer */
	uint32_t push_flags;    /* PUSH flags */
	uint64_t meta_gn; /* track the meta_gn of the last push */
	struct rbn rbn;
	struct ev_s ev;
};

/* Entry of ldms_set->notify_coll */
struct ldms_lookup_peer {
	ldms_t xprt; /* Transport to peer */
	uint64_t remote_notify_xid; /* Value received in req_notify */
	uint32_t notify_flags;	    /* What events are notified */
	struct rbn rbn;
	struct ev_s ev;
};

/* Entry of ldms_xprt->set_coll */
struct xprt_set_coll_entry {
	ldms_set_t set;
	struct rbn rbn;
	struct ev_s ev;
};

enum ldms_request_cmd {
	LDMS_CMD_DIR = 0,
	LDMS_CMD_DIR_CANCEL,
	LDMS_CMD_LOOKUP,
	LDMS_CMD_REQ_NOTIFY,
	LDMS_CMD_CANCEL_NOTIFY,
	LDMS_CMD_SEND_MSG,
	LDMS_CMD_AUTH_MSG,
	LDMS_CMD_CANCEL_PUSH,
	LDMS_CMD_AUTH,
	LDMS_CMD_SET_DELETE,
	LDMS_CMD_SEND_QUOTA, /* for updaing send quota */

	/* message service requests */
	LDMS_CMD_MSG, /* for messages */
	LDMS_CMD_MSG_SUB, /* message subscribe request */
	LDMS_CMD_MSG_UNSUB, /* message unsubscribe request */

	/* qgroup messages */
	LDMS_CMD_QGROUP_ASK,
	LDMS_CMD_QGROUP_DONATE,
	LDMS_CMD_QGROUP_DONATE_BACK,

	/* rail quota re-config after connected */
	LDMS_CMD_QUOTA_RECONFIG,
	/* rail rate re-config after connected */
	LDMS_CMD_RATE_RECONFIG,

	LDMS_CMD_REPLY = 0x100,
	LDMS_CMD_DIR_REPLY,
	LDMS_CMD_DIR_CANCEL_REPLY,
	LDMS_CMD_DIR_UPDATE_REPLY,
	LDMS_CMD_LOOKUP_REPLY,
	LDMS_CMD_REQ_NOTIFY_REPLY,
	LDMS_CMD_AUTH_CHALLENGE_REPLY,
	LDMS_CMD_AUTH_APPROVAL_REPLY,
	LDMS_CMD_PUSH_REPLY,
	LDMS_CMD_AUTH_REPLY,
	LDMS_CMD_SET_DELETE_REPLY,

	/* message service replies */
	LDMS_CMD_MSG_SUB_REPLY, /* message subscribe reply (result) */
	LDMS_CMD_MSG_UNSUB_REPLY, /* message unsubscribe reply (result) */

	LDMS_CMD_LAST = LDMS_CMD_MSG_UNSUB_REPLY,

	/* Transport private requests set bit 32 */
	LDMS_CMD_XPRT_PRIVATE = 0x80000000,
};

struct ldms_conn_msg {
	struct ldms_version ver;
	char auth_name[LDMS_AUTH_NAME_MAX + 1];
};

enum ldms_conn_type {
	LDMS_CONN_TYPE_XPRT = 0,
	LDMS_CONN_TYPE_RAIL,
};

struct ldms_conn_msg2 {
	struct ldms_version ver;
	char auth_name[LDMS_AUTH_NAME_MAX + 1];
	enum ldms_conn_type conn_type;
};

struct ldms_send_cmd_param {
	uint32_t msg_len;
	char msg[OVIS_FLEX];
};

struct ldms_send_quota_param {
	uint32_t send_quota;
	int32_t  rc;
};

struct ldms_lookup_cmd_param {
	uint32_t flags;
	uint32_t path_len;
	char path[LDMS_LOOKUP_PATH_MAX+1];
};

struct ldms_dir_cmd_param {
	uint32_t flags;		/*! Directory update flags */
};

struct ldms_set_delete_cmd_param {
	uint32_t inst_name_len;
	char inst_name[OVIS_FLEX];
};

struct ldms_req_notify_cmd_param {
	uint64_t set_id;	/*! The set we want notifications for  */
	uint32_t flags;		/*! Events we want  */
};

struct ldms_cancel_notify_cmd_param {
	uint64_t set_id;	/*! The set we want to cancel notifications for  */
};

struct ldms_cancel_push_cmd_param {
	uint64_t set_id;	/*! The set we want to cancel push updates for  */
};

/* partial message (in message service); see ldms_msg.h for a full message */
struct ldms_msg_part_param {
	struct ldms_addr src;
	uint64_t msg_gn;
	uint32_t more:1; /* more partial message */
	uint32_t first:1; /* first partial message */
	uint32_t reserved:30;
	char     part_msg[OVIS_FLEX];	/* partial message; its length
					 * can be inferred from req->hdr.len. */
};

struct ldms_msg_sub_param {
	uint32_t is_regex:1;
	uint32_t match_len:31;
	int64_t  rate;
	char match[OVIS_FLEX];
};

struct ldms_qgroup_ask {
	uint64_t q;
	uint64_t usec; /* like 'sec' since epoch, but in usec */
};

struct ldms_qgroup_donate {
	uint64_t q;
	uint64_t usec; /* like 'sec' since epoch, but in usec */
};

struct ldms_quota_reconfig_param {
	uint64_t q; /* the new quota */
};

struct ldms_rate_reconfig_param {
	uint64_t rate; /* the new rate (bytes/sec) */
};

struct ldms_request_hdr {
	uint64_t xid;		/*! Transaction id returned in reply */
	uint32_t cmd;		/*! The operation being requested  */
	uint32_t len;		/*! The length of the request  */
};

struct ldms_request {
	struct ldms_request_hdr hdr;
	union {
		struct ldms_send_cmd_param send;
		struct ldms_send_quota_param send_quota;
		struct ldms_dir_cmd_param dir;
		struct ldms_set_delete_cmd_param set_delete;
		struct ldms_lookup_cmd_param lookup;
		struct ldms_req_notify_cmd_param req_notify;
		struct ldms_cancel_notify_cmd_param cancel_notify;
		struct ldms_cancel_push_cmd_param cancel_push;
		struct ldms_msg_part_param msg_part;
		struct ldms_msg_sub_param msg_sub;
		struct ldms_qgroup_ask qgroup_ask;
		struct ldms_qgroup_donate  qgroup_donate;
		struct ldms_quota_reconfig_param quota_reconfig;
		struct ldms_rate_reconfig_param rate_reconfig;
	};
};

struct ldms_rendezvous_lookup_param {
	uint64_t set_id;
	uint32_t more;
	uint32_t meta_len;
	uint32_t data_len;
	uint32_t card; /* card of dict */
	uint32_t schema_len;
	uint32_t array_card; /* card of array */
	// struct timespec req_recv; /* Timestamp when server has received the lookup request. */
	// struct timespec share; /* Timestamp when server has called zap_share(). */
	/* schema name, then instance name, and then set_info key value pairs */
	char set_info[OVIS_FLEX];
};

struct ldms_rendezvous_push_param {
	uint64_t lookup_set_id;	/* set_id provided in the lookup */
	uint64_t push_set_id;	/* set_id to provide in push notifications */
	uint32_t flags;
};

#define LDMS_XPRT_RENDEZVOUS_LOOKUP	1
#define LDMS_XPRT_RENDEZVOUS_PUSH	2

struct ldms_rendezvous_hdr {
	uint64_t xid;
	uint32_t cmd;
	uint32_t len;
};

struct ldms_rendezvous_msg {
	struct ldms_rendezvous_hdr hdr;
	union {
		struct ldms_rendezvous_lookup_param lookup;
		struct ldms_rendezvous_push_param push;
	};
};

struct ldms_dir_reply {
	uint32_t type;
	uint32_t more;
	uint32_t json_data_len; /* This is not used but left for compatibility with V4.3.3 */
	char json_data[OVIS_FLEX];
};

struct ldms_req_notify_reply {
	struct ldms_notify_event_s event;
};

#define LDMS_PASSWORD_MAX 128

struct ldms_auth_challenge_reply {
	char s[LDMS_PASSWORD_MAX];
};

#define LDMS_CMD_PUSH_REPLY_F_MORE	0x80000000 /* !0 if this push message has more data */
struct ldms_push_reply {
	uint64_t set_id;	/*! The RBD of the set that has been updated */
	uint32_t flags;
	uint32_t data_off;
	uint32_t data_len;
	char data[OVIS_FLEX];
};

struct ldms_reply_hdr {
	uint64_t xid;
	uint32_t cmd;
	uint32_t len;
	uint32_t rc;
};

struct ldms_msg_sub_reply {
	int msg_len;
	char msg[0];
};

struct ldms_set_delete_reply {
	struct timespec recv_ts;
};

struct ldms_reply {
	struct ldms_reply_hdr hdr;
	union {
		struct ldms_dir_reply dir;
		struct ldms_req_notify_reply req_notify;
		struct ldms_auth_challenge_reply auth_challenge;
		struct ldms_push_reply push;
		struct ldms_msg_sub_reply sub;
		struct ldms_set_delete_reply set_del;
	};
};
#pragma pack()

typedef enum ldms_context_type {
	LDMS_CONTEXT_DIR,
	LDMS_CONTEXT_DIR_CANCEL,
	LDMS_CONTEXT_LOOKUP_REQ,
	LDMS_CONTEXT_LOOKUP_READ,
	LDMS_CONTEXT_UPDATE,
	LDMS_CONTEXT_REQ_NOTIFY,
	LDMS_CONTEXT_SEND,
	LDMS_CONTEXT_PUSH,
	LDMS_CONTEXT_UPDATE_META,
	LDMS_CONTEXT_SET_DELETE,
} ldms_context_type_t;

struct ldms_context {
	sem_t sem;
	sem_t *sem_p;
	int rc;
	struct ldms_xprt *x;
	ldms_context_type_t type;
	struct ldms_op_ctxt *op_ctxt;
	union {
		struct {
			ldms_dir_cb_t cb;
			void *cb_arg;
		} dir;
		struct {
			ldms_lookup_cb_t cb;
			void *cb_arg;
			char *path;
			enum ldms_lookup_flags flags;
		} lu_req;
		struct {
			ldms_set_t s;
			ldms_lookup_cb_t cb;
			void *cb_arg;
			int more;
			enum ldms_lookup_flags flags;
		} lu_read;
		struct {
			ldms_set_t s;
			ldms_update_cb_t cb;
			void *cb_arg;
			int idx_from;
			int idx_to;
		} update;
		struct {
			ldms_set_t s;
			ldms_notify_cb_t cb;
			void *cb_arg;
		} req_notify;
		struct {
			ldms_set_t s;
			ldms_set_delete_cb_t cb;
			void *cb_arg;
			int lookup;
		} set_delete;
	};
	struct timespec start;
	TAILQ_ENTRY(ldms_context) link;
};

#define LDMS_MAX_TRANSPORT_NAME_LEN 16

typedef enum ldms_xtype_e {
	/* legacy xprt */
	LDMS_XTYPE_ACTIVE_XPRT  = 0x0,
	LDMS_XTYPE_PASSIVE_XPRT = 0x1,
	/* rail */
	LDMS_XTYPE_ACTIVE_RAIL  = 0x2,
	LDMS_XTYPE_PASSIVE_RAIL = 0x3,
} ldms_xtype_t;

#define XTYPE_IS_PASSIVE(t) ((t) & 0x1)
#define XTYPE_IS_LEGACY(t) (((t) & (~0x1)) == 0)
#define XTYPE_IS_RAIL(t) ((t) & 0x2)

#define LDMS_IS_RAIL(x) XTYPE_IS_RAIL((x)->xtype)
#define LDMS_IS_LEGACY(x) XTYPE_IS_LEGACY((x)->xtype)
#define LDMS_IS_PASSIVE(x) XTYPE_IS_PASSIVE((x)->xtype)

#define LDMS_RAIL(x) ((ldms_rail_t)(x))

struct ldms_xprt_ops_s {
	int (*connect)(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
			ldms_event_cb_t cb, void *cb_arg);
	int (*is_connected)(struct ldms_xprt *x);
	int (*listen)(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
		ldms_event_cb_t cb, void *cb_arg);
	int (*sockaddr)(ldms_t x, struct sockaddr *local_sa,
		       struct sockaddr *remote_sa,
		       socklen_t *sa_len);
	void (*close)(ldms_t x);
	int (*send)(ldms_t x, char *msg_buf, size_t msg_len, struct ldms_op_ctxt *op_ctxt);
	size_t (*msg_max)(ldms_t x);
	int (*dir)(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags);
	int (*dir_cancel)(ldms_t x);
	int (*lookup)(ldms_t t, const char *name, enum ldms_lookup_flags flags,
		       ldms_lookup_cb_t cb, void *cb_arg, struct ldms_op_ctxt *op_ctxt);
	int (*stats)(ldms_t x, ldms_xprt_stats_t stats, int mask, int is_reset);

	ldms_t (*get)(ldms_t x, const char *name, const char *func, int line); /* ref get */
	void (*put)(ldms_t x, const char *name, const char *func, int line); /* ref put */
	void (*ctxt_set)(ldms_t x, void *ctxt, app_ctxt_free_fn fn);
	void *(*ctxt_get)(ldms_t x);
	uint64_t (*conn_id)(ldms_t x);
	const char *(*type_name)(ldms_t x);
	void (*priority_set)(ldms_t x, int prio);
	void (*cred_get)(ldms_t x, ldms_cred_t lcl, ldms_cred_t rmt);
	int (*update)(ldms_t x, struct ldms_set *set, ldms_update_cb_t cb, void *arg,
	                                               struct ldms_op_ctxt *op_ctxt);

	int (*get_threads)(ldms_t x, pthread_t *out, int n);

	void (*event_cb_set)(ldms_t x, ldms_event_cb_t cb ,void *cb_arg);

	zap_ep_t (*get_zap_ep)(ldms_t x);

	ldms_set_t (*set_by_name)(ldms_t x, const char *set_name);
};

typedef struct xprt_stats_s {
	struct timespec connected;
	struct timespec disconnected;
	struct timespec last_op;
	struct ldms_stats_entry ops[LDMS_XPRT_OP_COUNT];
} *xprt_stats_t;

struct ldms_xprt {
	enum ldms_xtype_e xtype;
	struct ldms_xprt_ops_s ops;
	int disconnected;

	/* Semaphore and return code for synchronous xprt calls */
	sem_t sem;
	int sem_rc;

	char name[LDMS_MAX_TRANSPORT_NAME_LEN];
	struct ref_s ref;

	pthread_mutex_t lock;
	zap_err_t zerrno;
	struct xprt_stats_s stats;

	/* Maximum size of the underlying transport send/recv message */
	int max_msg;
	/* Points to local ctxt expected when dir updates returned to this endpoint */
	uint64_t local_dir_xid;
	/* This is the peers local_dir_xid that we provide when providing dir updates */
	uint64_t remote_dir_xid;

#ifdef DEBUG
	int active_dir; /* Number of outstanding dir requests */
	int active_dir_cancel; /* Number of outstanding dir cancel requests */
	int active_lookup; /* Number of outstanding lookup requests */
	int active_push; /* Number of outstanding push ctxt */
#endif /* DEBUG */
	TAILQ_HEAD(, ldms_context) ctxt_list;

	/* Callback that receives the connection event and receive event */
	ldms_event_cb_t event_cb;
	void *event_cb_arg;

	zap_t zap;
	zap_ep_t zap_ep;

	/* Authentication */
	const char *password;
	enum {
		LDMS_XPRT_AUTH_FAILED = -1, /* authentication failed */
		LDMS_XPRT_AUTH_DISABLE = 0, /* authentication not is used */
		LDMS_XPRT_AUTH_INIT = 1, /* Use authentication */
		LDMS_XPRT_AUTH_BUSY = 2, /* Authentication in operation */
		LDMS_XPRT_AUTH_APPROVED = 3 /* authentication approved */
	} auth_flag;

	ldms_auth_t auth; /* authentication object */

	/* Unique connection id */
	uint64_t conn_id;

	/* Remote Credential */
	uid_t ruid;
	gid_t rgid;
	/* Local Credential */
	uid_t luid;
	gid_t lgid;

	int term;

	struct rbt set_coll;

	/** Application's context */
	void *app_ctxt;
	app_ctxt_free_fn app_ctxt_free_fn;

	LIST_ENTRY(ldms_xprt) xprt_link;
};

void __ldms_xprt_term(struct ldms_xprt *x);

/* ====================
 * xprt_auth operations
 * ====================
 *
 * These functions are implemented in `ldms_xprt_auth.c`.
 */
int ldms_xprt_auth_bind(ldms_t xprt, ldms_auth_t auth);
void ldms_xprt_auth_begin(ldms_t xprt);
int ldms_xprt_auth_send(ldms_t _x, const char *msg_buf, size_t msg_len);
void ldms_xprt_auth_end(ldms_t xprt, int result);

/* ========================
 * LDMS operation profiling
 * ========================
 */
#define ENABLED_PROFILING(_OP_) (__enable_profiling[_OP_] == 1)

#endif
