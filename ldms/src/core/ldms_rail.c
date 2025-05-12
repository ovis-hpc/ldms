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
 * \file ldms_rail.c
 *
 */

/*
 * NOTE TO SELF
 * ------------
 * The rail uses existing LDMS transport and interpose the callbacks.
 */

#include <errno.h>
#include <assert.h>

#include <netdb.h>
#include <arpa/inet.h>

#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_rail.h"
#include "ldms_msg.h"

#include "ldms_private.h"

extern ovis_log_t xlog;
#define RAIL_LOG(fmt, ...) do { \
	ovis_log(xlog, OVIS_LERROR, fmt, ## __VA_ARGS__); \
} while (0);

extern int ldms_msg_enabled; /* see ldms_msg.c */

/* The definition is in ldms.c. */
extern int __enable_profiling[LDMS_XPRT_OP_COUNT];

static int __rail_connect(ldms_t _r, struct sockaddr *sa, socklen_t sa_len,
		ldms_event_cb_t cb, void *cb_arg);
static int __rail_is_connected(ldms_t _r);
static int __rail_listen(ldms_t _r, struct sockaddr *sa, socklen_t sa_len,
	ldms_event_cb_t cb, void *cb_arg);
static void __rail_event_cb_set(ldms_t _r, ldms_event_cb_t cb, void *cb_arg);
static int __rail_sockaddr(ldms_t _r, struct sockaddr *local_sa,
	       struct sockaddr *remote_sa,
	       socklen_t *sa_len);
static void __rail_close(ldms_t _r);
static int __rail_send(ldms_t _r, char *msg_buf, size_t msg_len,
				  struct ldms_op_ctxt *op_ctxt);
static size_t __rail_msg_max(ldms_t x);
static int __rail_dir(ldms_t _r, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags);
static int __rail_dir_cancel(ldms_t _r);
static int __rail_lookup(ldms_t _r, const char *name, enum ldms_lookup_flags flags,
	       ldms_lookup_cb_t cb, void *cb_arg, struct ldms_op_ctxt *op_ctxt);
static int __rail_stats(ldms_t _r, ldms_xprt_stats_t stats, int mask, int is_reset);

#define __rail_get(_r_, _n_) ___rail_get((_r_), (_n_), __func__, __LINE__)
#define __rail_put(_r_, _n_) ___rail_put((_r_), (_n_), __func__, __LINE__)
static ldms_t ___rail_get(ldms_t _r, const char *name, const char *func, int line); /* ref get */
static void ___rail_put(ldms_t _r, const char *name, const char *func, int line); /* ref put */
static void __rail_ctxt_set(ldms_t _r, void *ctxt, app_ctxt_free_fn fn);
static void *__rail_ctxt_get(ldms_t _r);
static uint64_t __rail_conn_id(ldms_t _r);
static const char *__rail_type_name(ldms_t _r);
static void __rail_priority_set(ldms_t _r, int prio);
static void __rail_cred_get(ldms_t _r, ldms_cred_t lcl, ldms_cred_t rmt);
static int __rail_update(ldms_t _r, struct ldms_set *set, ldms_update_cb_t cb, void *arg,
                                                           struct ldms_op_ctxt *op_ctxt);
static int __rail_get_threads(ldms_t _r, pthread_t *out, int n);
static ldms_set_t __rail_set_by_name(ldms_t x, const char *set_name);

zap_ep_t __rail_get_zap_ep(ldms_t x);

static struct ldms_xprt_ops_s __rail_ops = {
	.connect      = __rail_connect,
	.is_connected = __rail_is_connected,
	.listen       = __rail_listen,
	.sockaddr     = __rail_sockaddr,
	.close        = __rail_close,
	.send         = __rail_send,
	.msg_max      = __rail_msg_max,
	.dir          = __rail_dir,
	.dir_cancel   = __rail_dir_cancel,
	.lookup       = __rail_lookup,
	.stats        = __rail_stats,

	.get          = ___rail_get,
	.put          = ___rail_put,
	.ctxt_set     = __rail_ctxt_set,
	.ctxt_get     = __rail_ctxt_get,
	.conn_id      = __rail_conn_id,
	.type_name    = __rail_type_name,
	.priority_set = __rail_priority_set,
	.cred_get     = __rail_cred_get,

	.update       = __rail_update,
	.get_threads  = __rail_get_threads,
	.get_zap_ep   = __rail_get_zap_ep,

	.event_cb_set = __rail_event_cb_set,
	.set_by_name  = __rail_set_by_name,
};

void __rail_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg);
int __rep_quota_release(struct ldms_rail_ep_s *rep, uint64_t q);

static int __rail_id_cmp(void *k, const void *tk)
{
	return memcmp(k, tk, sizeof(struct ldms_rail_id_s));
}

/* collection of passive rail (created by accept()) */
static struct rbt __passive_rail_rbt = RBT_INITIALIZER(__rail_id_cmp);

/* global rail mutex for:
 *   - __passive_rail_rbt
 *   - ... (somethig else?)
 */
static pthread_mutex_t __rail_mutex = PTHREAD_MUTEX_INITIALIZER;

LIST_HEAD(rail_list, ldms_rail_s) rail_list;
pthread_mutex_t rail_list_lock = PTHREAD_MUTEX_INITIALIZER;
static void __rail_ref_free(void *arg)
{
	ldms_rail_t r = arg;
	ldms_rail_dir_ctxt_t dc;
	int i;
	struct ldms_rail_ep_s *rep;

	assert( r->connecting_eps == 0 );
	assert( r->connected_eps  == 0 );

	pthread_mutex_lock(&rail_list_lock);
	LIST_REMOVE(r, rail_link);
	pthread_mutex_unlock(&rail_list_lock);

	for (i = 0; i < r->n_eps; i++) {
		rep = &r->eps[i];
		if (rep->ep) {
			ldms_xprt_ctxt_set(rep->ep, NULL, NULL);
			ldms_xprt_put(rep->ep, "init");
		}

	}

	while ((dc = TAILQ_FIRST(&r->dir_notify_tq))) {
		TAILQ_REMOVE(&r->dir_notify_tq, dc, tqe);
		free(dc);
	}

	if (r->app_ctxt && r->app_ctxt_free) {
		r->app_ctxt_free(r->app_ctxt);
	}

	free(r);
}

uint64_t rail_gn = 1;
/* implementation in ldms_msg.c */
int __msg_buf_cmp(void *tree_key, const void *key);
int __str_rbn_cmp(void *tree_key, const void *key);

/* The implementation is in ldms_xprt.c. */
zap_t __ldms_zap_get(const char *xprt);

ldms_rail_t rail_first()
{
	ldms_rail_t r = NULL;
	pthread_mutex_lock(&rail_list_lock);
	r = LIST_FIRST(&rail_list);
	if (!r)
		goto out;
	__rail_get((ldms_t)r, "iter_next"); /* next reference */
	r = (ldms_rail_t)__rail_get((ldms_t)r, "inter_caller"); /* caller reference */
out:
	pthread_mutex_unlock(&rail_list_lock);
	return r;
}

ldms_rail_t rail_next(ldms_rail_t r)
{
	ldms_rail_t prev_r = r;
	pthread_mutex_lock(&rail_list_lock);
	r = LIST_NEXT(r, rail_link);
	if (!r)
		goto out;
	__rail_get((ldms_t)r, "iter_next");	/* next reference */
	r = (ldms_rail_t)__rail_get((ldms_t)r, "iter_caller");	/* caller reference */
 out:
	pthread_mutex_unlock(&rail_list_lock);
	__rail_put((ldms_t)prev_r, "iter_next"); /* next reference */
	return r;
}

ldms_t ldms_xprt_rail_new(const char *xprt_name,
			  int n, int64_t recv_quota, int64_t rate_limit,
			  const char *auth_name,
			  struct attr_value_list *auth_av_list)
{
	ldms_rail_t r;
	zap_t zap;
	int i;
	enum ldms_xprt_ops_e op_e;

	if (n <= 0) {
		errno = EINVAL;
		goto err_0;
	}

	if (strlen(auth_name) > LDMS_AUTH_NAME_MAX) {
		errno = EINVAL;
		goto err_0;
	}

	if (strlen(xprt_name) >= LDMS_MAX_TRANSPORT_NAME_LEN) {
		errno = EINVAL;
		goto err_0;
	}

	r = calloc(1, sizeof(*r) + n*sizeof(r->eps[0]));
	if (!r)
		goto err_0;

	ref_init(&r->ref, "rail_ref", __rail_ref_free, r);
	TAILQ_INIT(&r->dir_notify_tq);
	pthread_mutex_init(&r->mutex, NULL);
	sem_init(&r->sem, 0, 0);
	r->xtype = LDMS_XTYPE_ACTIVE_RAIL; /* change to passive in listen() */
	r->n_eps = n;
	r->recv_quota = recv_quota;
	r->recv_rate_limit = rate_limit;
	rbt_init(&r->ch_cli_rbt, __str_rbn_cmp);

	snprintf(r->name, sizeof(r->name), "%s", xprt_name);
	snprintf(r->auth_name, sizeof(r->auth_name), "%s", auth_name);
	if (auth_av_list) {
		r->auth_av_list = av_copy(auth_av_list);
		if (!r->auth_av_list)
			goto err_1;
	}

	r->ops = __rail_ops;
	for (i = 0; i < n; i++) {
		r->eps[i].rail = r;
		r->eps[i].idx = i;
		r->eps[i].send_quota = LDMS_UNLIMITED;
		r->eps[i].rate_quota.quota = LDMS_UNLIMITED;
		r->eps[i].rate_quota.rate   = LDMS_UNLIMITED;
		r->eps[i].rate_quota.ts.tv_sec    = 0;
		r->eps[i].rate_quota.ts.tv_nsec   = 0;
		r->eps[i].remote_is_rail = -1;
		rbt_init(&r->eps[i].sbuf_rbt, __msg_buf_cmp);
		TAILQ_INIT(&r->eps[i].sbuf_tq);
		for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++) {
			TAILQ_INIT(&(r->eps[i].op_ctxt_lists[op_e]));
		}
	}

	zap = __ldms_zap_get(xprt_name);
	if (!zap) {
		goto err_1;
	}
	r->max_msg = zap_max_msg(zap);

	/* The other endpoints will be created later in connect() or
	 * __rail_zap_handle_conn_req() */


	r->conn_id = __atomic_fetch_add(&rail_gn, n, __ATOMIC_SEQ_CST);
	r->rail_id.rail_gn = r->conn_id;
	r->rail_id.pid = getpid();

	pthread_mutex_lock(&rail_list_lock);
	LIST_INSERT_HEAD(&rail_list, r, rail_link);
	pthread_mutex_unlock(&rail_list_lock);
	return (ldms_t)r;

 err_1:
	free(r);
 err_0:
	return NULL;
}

ldms_t ldms_xprt_new_with_auth(const char *xprt_name,
			       const char *auth_name,
			       struct attr_value_list *auth_av_list)
{
	return ldms_xprt_rail_new(xprt_name, 1,
			LDMS_UNLIMITED, LDMS_UNLIMITED,
			auth_name,  auth_av_list);
}

/* r->mutex MUST be held */
void __rail_cut(struct ldms_rail_s *r)
{
	int i;
	struct ldms_rail_ep_s *rep;
	for (i = 0; i < r->n_eps; i++) {
		rep = &r->eps[i];
		switch (rep->state) {
		case LDMS_RAIL_EP_CONNECTED:
			rep->state = LDMS_RAIL_EP_ERROR;
			ldms_xprt_close(rep->ep);
			break;
		default:
			/* no-op */
			break;
		}
	}
}

/* r->mutex MUST be held */
int __rail_ev_prep(struct ldms_rail_s *r, ldms_xprt_event_t ev)
{
	switch (r->state) {
	case LDMS_RAIL_EP_CONNECTING:
	case LDMS_RAIL_EP_ACCEPTING:
		if (r->connected_eps == r->n_eps) {
			r->state = LDMS_RAIL_EP_CONNECTED;
			ev->type = LDMS_XPRT_EVENT_CONNECTED;
			return 1;
		}
		break;
	case LDMS_RAIL_EP_REJECTED:
		if (r->xtype == LDMS_XTYPE_PASSIVE_RAIL) {
			return 0;
		}
		if (r->connected_eps == 0 && r->connecting_eps == 0) {
			ev->type = LDMS_XPRT_EVENT_REJECTED;
			return 1;
		}
		break;
	case LDMS_RAIL_EP_ERROR:
		if (r->connected_eps == 0 && r->connecting_eps == 0) {
			if (r->conn_error_eps) {
				ev->type = LDMS_XPRT_EVENT_ERROR;
			} else if (r->rejected_eps) {
				ev->type = LDMS_XPRT_EVENT_REJECTED;
			} else {
				ev->type = LDMS_XPRT_EVENT_DISCONNECTED;
			}
			return 1;
		}
		break;
	default:
		/* no-op */
		break;
	}
	return 0;
}

/* implementation in ldms_msg.c */
void __msg_on_rail_disconnected(struct ldms_rail_s *r);

/* return send quota to peer */
void __rail_ep_quota_return(struct ldms_rail_ep_s *rep, int quota, int rc)
{
	/* give back send quota */
	int len = sizeof(struct ldms_request_hdr) +
		sizeof(struct ldms_send_quota_param);
	struct ldms_request req = {
		.hdr = {
			.cmd = htonl(LDMS_CMD_SEND_QUOTA),
			.len = htonl(len),
		},
		.send_quota = {
			.send_quota = htonl(quota),
			.rc         = htonl(rc),
		}};
	zap_send(rep->ep->zap_ep, &req, len);
}

ldms_rail_t __rail_passive_legacy_wrap(ldms_t x, ldms_event_cb_t cb, void *cb_arg)
{
	assert(XTYPE_IS_LEGACY(x->xtype));
	const char *xprt_name = ldms_xprt_type_name(x);
	ldms_rail_t r;
	union ldms_sockaddr self_addr, peer_addr;
	socklen_t addr_len = sizeof(self_addr);
	r = (ldms_rail_t)ldms_xprt_rail_new(xprt_name, 1,
					    LDMS_UNLIMITED, LDMS_UNLIMITED,
					    x->auth->plugin->name, NULL);
	if (!r)
		goto out;

	x->event_cb = __rail_cb;
	x->event_cb_arg = &r->eps[0];

	zap_get_name(x->zap_ep, (void*)&self_addr, (void*)&peer_addr, &addr_len);

	/* Replace rail_id w/ information from x */
	r->rail_id.ip4_addr = peer_addr.sin.sin_addr.s_addr;
	r->rail_id.pid= 0;
	r->rail_id.rail_gn = (uint64_t)r;

	r->event_cb = cb;
	r->event_cb_arg = cb_arg;

	r->xtype = LDMS_XTYPE_PASSIVE_RAIL;
	r->state = LDMS_RAIL_EP_ACCEPTING;

	rbn_init(&r->rbn, &r->rail_id);
	pthread_mutex_lock(&__rail_mutex);
	rbt_ins(&__passive_rail_rbt, &r->rbn);
	ref_get(&r->ref, "__passive_rail_rbt");
	pthread_mutex_unlock(&__rail_mutex);
	r->connecting_eps = 1;

	r->eps[0].ep = x;
	r->eps[0].state = LDMS_RAIL_EP_ACCEPTING;
	r->eps[0].send_quota = r->send_quota;
	r->eps[0].rate_quota.quota = r->send_rate_limit;
	r->eps[0].rate_quota.rate   = r->send_rate_limit;
	r->eps[0].rate_quota.ts.tv_sec  = 0;
	r->eps[0].rate_quota.ts.tv_nsec = 0;
	r->eps[0].remote_is_rail = 0; /* remote is legacy */
	ldms_xprt_ctxt_set(x, &r->eps[0], NULL);

	ref_get(&r->ref, "ldms_accepting");

 out:
	return r;
}

/* rail interposer */
void __rail_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	struct ldms_rail_ep_s *rep = cb_arg;
	ldms_rail_t r = rep->rail;
	int call_cb = 0;
	struct ldms_xprt_event ev = {0};
	const char *ref_name;
	int passive_rail_rm_trigger = 0;

	/*
	 * NOTE: `x` is an xprt that could be:
	 *   - legacy passive endpoint (e.g. old remote peer using 4.3.11)
	 *   - ldms xprt member in a rail
	 */

	if (r->xtype == LDMS_XTYPE_PASSIVE_RAIL && r->state == LDMS_RAIL_EP_LISTENING) {
		/*
		 * This condition is only for `x` with legacy LDMS remote peer.
		 * In this case, we shall wrap `x` with a new rail before
		 * continuing.
		 *
		 * NOTE: If the remote is rail, it will use rail connect message
		 * which is handled by `__rail_zap_handle_conn_req()`, which
		 * bundles xprt members into the associated rail object.
		 */
		if (e->type != LDMS_XPRT_EVENT_CONNECTED) {
			/* bad passive legacy xprt does not notify the app */
			ldms_xprt_put(x, "init");
			return;

		}
		/* Wrap it in new rail transport and continue */
		r = __rail_passive_legacy_wrap(x, r->event_cb, r->event_cb_arg);
		if (!r) {
			ldms_xprt_put(x, "init");
			return;
		}
		rep = &r->eps[0];
	}

	ref_get(&r->ref, "rail_cb");
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		if (XTYPE_IS_PASSIVE(r->xtype)) {
			ref_name = "ldms_accepting";
		} else {
			ref_name = "ldms_connect";
		}
		ref_put(&r->ref, ref_name);
		ref_get(&r->ref, "ldms_connected");

		pthread_mutex_lock(&r->mutex);
		rep->state = LDMS_RAIL_EP_CONNECTED;
		r->connected_eps++;
		r->connecting_eps--;
		switch (r->state) {
		case LDMS_RAIL_EP_CONNECTING:
		case LDMS_RAIL_EP_ACCEPTING:
			/* no-op */
			break;
		case LDMS_RAIL_EP_ERROR:
			rep->state = LDMS_RAIL_EP_ERROR;
			ldms_xprt_close(rep->ep);
			break;
		default:
			assert(0 == "Unexpected state");
			break;
		}
		call_cb = __rail_ev_prep(r, &ev);
		if (ev.type == LDMS_XPRT_EVENT_CONNECTED) {
			/*
			 * The connected event of this endpoint (rep)
			 * moves the rail's state to connected.
			 *
			 * Set the rail's connected timestamp to be the same
			 * as the endpoint's connected timestamp.
			 */
			r->connected_ts = rep->ep->stats.connected;
		}
		pthread_mutex_unlock(&r->mutex);
		break;
	case LDMS_XPRT_EVENT_REJECTED:
		if (XTYPE_IS_PASSIVE(r->xtype)) {
			ref_name = "ldms_accepting";
		} else {
			ref_name = "ldms_connect";
		}
		ref_put(&r->ref, ref_name);

		pthread_mutex_lock(&r->mutex);
		assert( rep->state == LDMS_RAIL_EP_CONNECTING || rep->state == LDMS_RAIL_EP_ACCEPTING );
		r->connecting_eps--;
		r->rejected_eps++;
		r->state = LDMS_RAIL_EP_REJECTED;
		r->disconnected_ts = rep->ep->stats.disconnected;
		rep->state = LDMS_RAIL_EP_REJECTED;
		__rail_cut(r);
		call_cb = __rail_ev_prep(r, &ev);
		passive_rail_rm_trigger = r->xtype == LDMS_XTYPE_PASSIVE_RAIL &&
					  r->connecting_eps == 0 &&
					  r->connected_eps  == 0;
		pthread_mutex_unlock(&r->mutex);
		break;
	case LDMS_XPRT_EVENT_ERROR:
		if (XTYPE_IS_PASSIVE(r->xtype)) {
			ref_name = "ldms_accepting";
		} else {
			ref_name = "ldms_connect";
		}
		ref_put(&r->ref, ref_name);
		pthread_mutex_lock(&r->mutex);
		assert( rep->state == LDMS_RAIL_EP_CONNECTING || rep->state == LDMS_RAIL_EP_ACCEPTING );
		rep->state = LDMS_RAIL_EP_ERROR;
		r->connecting_eps--;
		r->conn_error_eps++;
		r->state = LDMS_RAIL_EP_ERROR;
		r->disconnected_ts = rep->ep->stats.disconnected;
		__rail_cut(r);
		call_cb = __rail_ev_prep(r, &ev);
		passive_rail_rm_trigger = r->xtype == LDMS_XTYPE_PASSIVE_RAIL &&
					  r->connecting_eps == 0 &&
					  r->connected_eps  == 0;
		pthread_mutex_unlock(&r->mutex);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		pthread_mutex_lock(&r->mutex);
		if (r->state == LDMS_RAIL_EP_CONNECTED) {
			r->state = LDMS_RAIL_EP_ERROR;
			r->disconnected_ts = rep->ep->stats.disconnected;
		}
		r->connected_eps--;
		switch (rep->state) {
		case LDMS_RAIL_EP_CONNECTED:
			rep->state = LDMS_RAIL_EP_CLOSE;
			r->disconnected_ts = rep->ep->stats.disconnected;
			break;
		case LDMS_RAIL_EP_ERROR:
			/* no-op */
			break;
		default:
			assert(0 == "Bad state");
			break;
		}
		__rail_cut(r);
		call_cb = __rail_ev_prep(r, &ev);
		ref_put(&r->ref, "ldms_connected");
		passive_rail_rm_trigger = r->xtype == LDMS_XTYPE_PASSIVE_RAIL &&
					  r->connecting_eps == 0 &&
					  r->connected_eps  == 0;
		pthread_mutex_unlock(&r->mutex);
		__msg_on_rail_disconnected(r);
		break;
	case LDMS_XPRT_EVENT_RECV:
	case LDMS_XPRT_EVENT_SET_DELETE:
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
	case LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED:
		ev = *e;
		call_cb = 1;
		break;
	default:
		assert(0 == "Unexpected event");
		goto out;
	}

	if (call_cb && r->event_cb) {
		r->event_cb((void*)r, &ev, r->event_cb_arg);
	}

	if (passive_rail_rm_trigger) {
		/* remove it from the passive rail tree */
		pthread_mutex_lock(&__rail_mutex);
		rbt_del(&__passive_rail_rbt, &r->rbn);
		bzero(&r->rbn, sizeof(r->rbn));
		pthread_mutex_unlock(&__rail_mutex);
		ref_put(&r->ref, "__passive_rail_rbt");
	}

 out:
	ref_put(&r->ref, "rail_cb");
}

/* These are implemented in ldms_xprt.c */
void __ldms_xprt_resource_free(struct ldms_xprt *x);
void __ldms_xprt_conn_msg_init(ldms_t _x, struct ldms_conn_msg *msg);
void __ldms_xprt_init(struct ldms_xprt *x, const char *name, int is_active);
void ldms_zap_auto_cb(zap_ep_t zep, zap_event_t ev);

#define _MIN(a, b) ( (a)<(b)?(a):(b) )

static void __ldms_rail_conn_msg_init(struct ldms_rail_s *r, int idx, struct ldms_rail_conn_msg_s *m);

void __rail_conn_msg_ntoh(struct ldms_rail_conn_msg_s *m)
{
	m->conn_type = ntohl(m->conn_type);
	m->idx = ntohl(m->idx);
	m->n_eps = ntohl(m->n_eps);
	m->pid = ntohl(m->pid);
	m->rail_gn = be64toh(m->rail_gn);
	m->rate_limit = be64toh(m->rate_limit);
	m->recv_quota = be64toh(m->recv_quota);
}

void __rail_zap_handle_conn_req(zap_ep_t zep, zap_event_t ev)
{
	/*
	 * Handling rail conenct request. zep is the NEW zap endpoint. Its
	 * context is the LDMS endpoint in the rail.
	 */
	struct ldms_rail_conn_msg_s *m = (void*)ev->data;
	static char rej_msg[64] = "Insufficient resources";
	struct ldms_rail_conn_msg_s msg;
	int rc;
	char name[128]; /* for debug / error */
	zap_err_t zerr;
	struct ldms_xprt *lx = zap_get_ucontext(zep);
	struct ldms_auth *auth;
	union ldms_sockaddr  self_addr, peer_addr;
	socklen_t addr_len = sizeof(self_addr);
	struct ldms_rail_id_s rail_id;
	struct ldms_rail_s *r, *lr = NULL;
	struct ldms_rail_ep_s *rep;
	struct rbn *rbn;

	rep = ldms_xprt_ctxt_get(lx);
	if (rep)
		lr = rep->rail;

	zerr = zap_get_name(zep, (void*)&self_addr, (void*)&peer_addr, &addr_len);
	if (zerr) {
		snprintf(rej_msg, sizeof(rej_msg), "zap_get_name() error: %d", zerr);
		goto err_0;
	}
	switch (peer_addr.sa.sa_family) { /* family is in host-endian */
	case AF_INET:
		inet_ntop(peer_addr.sa.sa_family, &peer_addr.sin.sin_addr,
				name, sizeof(name));
		break;
	case AF_INET6:
		inet_ntop(peer_addr.sa.sa_family, &peer_addr.sin6.sin6_addr,
				name, sizeof(name));
		break;
	default:
		break;
	}

	__rail_conn_msg_ntoh(m);

	rail_id.ip4_addr = peer_addr.sin.sin_addr.s_addr;
	rail_id.pid = m->pid;
	rail_id.rail_gn = m->rail_gn;
	pthread_mutex_lock(&__rail_mutex);
	rbn = rbt_find(&__passive_rail_rbt, &rail_id);
	if (!rbn) {
		const char *xprt_name;
		const char *auth_name;
		struct attr_value_list *auth_av_list = NULL;
		int64_t recv_quota;
		int64_t rate_limit;
		ldms_event_cb_t cb;
		void *cb_arg;
		if (lr) {
			xprt_name = lr->name;
			auth_name = lr->auth_name;
			auth_av_list = lr->auth_av_list;
			recv_quota = lr->recv_quota;
			rate_limit = lr->recv_rate_limit;
			cb = lr->event_cb;
			cb_arg = lr->event_cb_arg;
		} else {
			xprt_name = lx->name;
			auth_name = lx->auth?lx->auth->plugin->name:NULL;
			recv_quota = LDMS_UNLIMITED;
			rate_limit = LDMS_UNLIMITED;
			cb = lx->event_cb;
			cb_arg = lx->event_cb_arg;
		}
		r = (void*)ldms_xprt_rail_new(xprt_name, m->n_eps,
				recv_quota, rate_limit, auth_name, auth_av_list);
		if (!r) {
			snprintf(rej_msg, sizeof(rej_msg), "passive rail create error: %d", errno);
			pthread_mutex_unlock(&__rail_mutex);
			goto err_0;
		}

		r->xtype = LDMS_XTYPE_PASSIVE_RAIL;
		r->state = LDMS_RAIL_EP_ACCEPTING;
		r->send_quota = m->recv_quota;
		r->send_rate_limit = m->rate_limit;
		r->event_cb = cb;
		r->event_cb_arg = cb_arg;
		r->rail_id = rail_id;
		r->peer_msg_enabled = m->msg_enabled;
		rbn_init(&r->rbn, &r->rail_id);
		rbt_ins(&__passive_rail_rbt, &r->rbn);
		ref_get(&r->ref, "__passive_rail_rbt");
		r->connreq_eps = r->n_eps;
	} else {
		r = container_of(rbn, struct ldms_rail_s, rbn);
	}
	pthread_mutex_unlock(&__rail_mutex);

	/* some sanity check */
	if (m->n_eps != r->n_eps) {
		snprintf(rej_msg, sizeof(rej_msg),
			"bad request, expecting rail of %d eps, but got %d",
			r->n_eps, m->n_eps);
		goto err_0;
	}
	if (m->idx >= r->n_eps) {
		snprintf(rej_msg, sizeof(rej_msg),
			"bad ep_idx %d (should be in 0..%d range)",
			m->idx, r->n_eps-1);
		goto err_0;
	}
	pthread_mutex_lock(&r->mutex);
	if (r->eps[m->idx].ep) {
		snprintf(rej_msg, sizeof(rej_msg),
			"bad ep_idx %d, the endpoint already existed",
			m->idx);
		pthread_mutex_unlock(&r->mutex);
		goto err_0;
	}
	if (!r->connreq_eps) {
		snprintf(rej_msg, sizeof(rej_msg),
			"Unexpected connection request on rail %#x-%d-%lu",
			r->rail_id.ip4_addr,
			r->rail_id.pid,
			r->rail_id.rail_gn
			);
		pthread_mutex_unlock(&r->mutex);
		goto err_0;
	}

	r->connreq_eps--;

	/* create passive-side endpoint */
	struct ldms_xprt *_x = calloc(1, sizeof(*_x));
	if (!_x) {
		RAIL_LOG("ERROR: Cannot create new ldms_xprt for connection"
			 " from %s.\n", name);
		snprintf(rej_msg, sizeof(rej_msg),
			"Cannot create new ldms_xprt: Not enough memory");
		pthread_mutex_unlock(&r->mutex);
		goto err_0;
	}
	r->connecting_eps++;
	__ldms_xprt_init(_x, r->name, 0);
	_x->zap = lx->zap;
	_x->zap_ep = zep;
	_x->max_msg = zap_max_msg(lx->zap);

	/* interpose */
	_x->event_cb = __rail_cb;
	_x->event_cb_arg = &r->eps[m->idx];
	r->eps[m->idx].ep = _x;
	r->eps[m->idx].state = LDMS_RAIL_EP_ACCEPTING;
	r->eps[m->idx].send_quota = r->send_quota;
	r->eps[m->idx].rate_quota.quota = r->send_rate_limit;
	r->eps[m->idx].rate_quota.rate   = r->send_rate_limit;
	r->eps[m->idx].rate_quota.ts.tv_sec  = 0;
	r->eps[m->idx].rate_quota.ts.tv_nsec = 0;
	r->eps[m->idx].remote_is_rail = 1;
	ldms_xprt_ctxt_set(_x, &r->eps[m->idx], NULL);
	pthread_mutex_unlock(&r->mutex);

	ldms_xprt_get(_x, "zap_uctxt");
	zap_set_ucontext(zep, _x);

	auth = ldms_auth_clone(lx->auth);
	if (!auth) {
		/* clone failed */
		goto err_1;
	}
	_x->auth_flag = LDMS_XPRT_AUTH_INIT;
	rc = ldms_xprt_auth_bind(_x, auth);
	if (rc)
		goto err_2;
	__ldms_rail_conn_msg_init(r, m->idx, &msg);

	/* Take a 'connect' reference. Dropped in ldms_xprt_close() */
	ldms_xprt_get(_x, "connect");

	ref_get(&r->ref, "ldms_accepting");
	zerr = zap_accept2(zep, ldms_zap_auto_cb, (void*)&msg, sizeof(msg), m->idx);
	if (zerr) {
		RAIL_LOG("ERROR: %d accepting connection from %s.\n", zerr, name);
		goto err_3;
	}

	return;
 err_3:
	ref_put(&r->ref, "ldms_accepting");
	ldms_xprt_put(_x, "connect"); /* drop 'connect' reference */
 err_2:
	ldms_auth_free(auth);
 err_1:
	pthread_mutex_lock(&r->mutex);
	r->connecting_eps--;
	r->eps[m->idx].ep = NULL;
	r->eps[m->idx].state = LDMS_RAIL_EP_ERROR;
	pthread_mutex_unlock(&r->mutex);
	zap_set_ucontext(_x->zap_ep, NULL);
	ldms_xprt_put(_x, "zap_uctxt");	/* context reference */
 err_0:
	zap_reject(zep, rej_msg, strlen(rej_msg)+1);
}

static void __ldms_rail_conn_msg_init(struct ldms_rail_s *r, int idx, struct ldms_rail_conn_msg_s *m)
{
	/* a convenient routine to construct conn_msg for rail */
	assert(r->eps[idx].ep);
	__ldms_xprt_conn_msg_init(r->eps[idx].ep, (void*)m);
	m->conn_type = htonl(LDMS_CONN_TYPE_RAIL);
	m->rate_limit = htobe64(r->recv_rate_limit);
	m->recv_quota = htobe64(r->recv_quota);
	m->n_eps = htonl(r->n_eps);
	m->idx = htonl(idx);
	m->pid = htonl(getpid());
	m->rail_gn = htobe64(r->rail_id.rail_gn);
	m->msg_enabled = htonl(ldms_msg_enabled);
}

static int __rail_ep_connect(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
			ldms_event_cb_t cb, void *cb_arg)
{
	int rc;
	struct ldms_rail_conn_msg_s msg;
	struct ldms_rail_ep_s *rep = cb_arg;
	ldms_rail_t r = rep->rail;

	__ldms_rail_conn_msg_init(r, rep->idx, &msg);

	x->event_cb = cb;
	x->event_cb_arg = cb_arg;
	ldms_xprt_get(x, "connect");
	rc = zap_connect2(x->zap_ep, sa, sa_len, (void*)&msg, sizeof(msg), rep->idx);
	return rc;
}

static int __rail_connect(ldms_t _r, struct sockaddr *sa, socklen_t sa_len,
		ldms_event_cb_t cb, void *cb_arg)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	ldms_t x;
	struct ldms_rail_ep_s *rep;
	int i, rc;

	pthread_mutex_lock(&r->mutex);
	if (r->state != LDMS_RAIL_EP_INIT) {
		pthread_mutex_unlock(&r->mutex);
		return EINVAL;
	}

	r->state = LDMS_RAIL_EP_CONNECTING;
	r->event_cb = cb;
	r->event_cb_arg = cb_arg;

	for (i = 0; i < r->n_eps; i++) {
		rep = &r->eps[i];
		x = __ldms_xprt_new_with_auth(r->name, r->auth_name, r->auth_av_list);
		if (!x) {
			rc = errno;
			goto err;
		}
		ldms_xprt_ctxt_set(x, &r->eps[i], NULL);
		rep->ep = x;
		rep->state = LDMS_RAIL_EP_CONNECTING;
		ref_get(&r->ref, "ldms_connect");
		rc = __rail_ep_connect(r->eps[i].ep, sa, sa_len, __rail_cb, &r->eps[i]);
		if (rc) {
			/* NOTE: Cleanup only the last endpoint that
			 *       failed to connect synchronously. The endpoints
			 *       that do not failed synchronously will receive a
			 *       callabck and we will clean up there.
			 */
			ref_put(&r->ref, "ldms_connect");
			rep->state = LDMS_RAIL_EP_ERROR;
			goto err;
		}
		r->connecting_eps++;
	}
	pthread_mutex_unlock(&r->mutex);
	return 0;

 err:
	r->state = LDMS_RAIL_EP_ERROR;
	pthread_mutex_unlock(&r->mutex);
	return rc;
}

static int __rail_is_connected(struct ldms_xprt *_r)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	return r->state == LDMS_RAIL_EP_CONNECTED;
}

static int __rail_listen(ldms_t _r, struct sockaddr *sa, socklen_t sa_len,
	ldms_event_cb_t cb, void *cb_arg)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	ldms_t x;
	int rc;
	if (r->state != LDMS_RAIL_EP_INIT)
		return EINVAL;
	/* 1 listening endpoint */
	r->eps[0].ep = __ldms_xprt_new_with_auth(r->name, r->auth_name, r->auth_av_list);
	if (!r->eps[0].ep) {
		r->state = LDMS_RAIL_EP_ERROR;
		rc = errno;
		return rc;
	}
	ldms_xprt_ctxt_set(r->eps[0].ep, &r->eps[0], NULL);
	x = r->eps[0].ep;
	r->xtype = LDMS_XTYPE_PASSIVE_RAIL;
	r->state = LDMS_RAIL_EP_LISTENING;
	r->event_cb = cb;
	r->event_cb_arg = cb_arg;
	r->eps[0].state = LDMS_RAIL_EP_LISTENING;
	rc = ldms_xprt_listen(x, sa, sa_len, __rail_cb, &r->eps[0]);
	if (rc) {
		r->eps[0].ep = NULL;
		r->eps[0].state = LDMS_RAIL_EP_ERROR;
		ldms_xprt_put(x, "init");
		return rc;
	}
	return 0;
}

static void __rail_event_cb_set(ldms_t _r, ldms_event_cb_t cb, void *cb_arg)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	r->event_cb = cb;
	r->event_cb_arg = cb_arg;
}

static int __rail_sockaddr(ldms_t _r, struct sockaddr *local_sa,
	       struct sockaddr *remote_sa,
	       socklen_t *sa_len)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	int rc;
	if (!__rail_is_connected(_r)) {
		return ENOTCONN;
	}
	rc = ldms_xprt_sockaddr(r->eps[0].ep, local_sa, remote_sa, sa_len);
	return rc;
}

static void __rail_close(ldms_t _r)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	int i;
	pthread_mutex_lock(&r->mutex);
	r->state = LDMS_RAIL_EP_ERROR;
	for (i = 0; i < r->n_eps; i++) {
		/* close only the connected ones */
		if (r->eps[i].state == LDMS_RAIL_EP_CONNECTED) {
			r->eps[i].state = LDMS_RAIL_EP_ERROR;
			ldms_xprt_close(r->eps[i].ep);
		}
	}
	pthread_mutex_unlock(&r->mutex);
}

/*
 * \retval 0      if success
 * \retval ENOBUFS if not enough quota
 */
int __quota_acquire(uint64_t *quota, uint64_t n)
{
	uint64_t v0, v1;

	__atomic_load(quota, &v0, __ATOMIC_SEQ_CST);
	if (v0 == LDMS_UNLIMITED)
		return 0;
 again:
	if (v0 < n)
		return ENOBUFS;
	v1 = v0 - n;
	if (__atomic_compare_exchange(quota, &v0, &v1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		return 0;
	/* The other thread won the quota modification race; try again. */
	goto again;
}

/*
 * Release `n` quotas, and returns the new quota value.
 */
uint64_t __quota_release(uint64_t *quota, uint64_t n)
{
	uint64_t v0;

	__atomic_load(quota, &v0, __ATOMIC_SEQ_CST);
	if (v0 == LDMS_UNLIMITED)
		return v0;
	return __atomic_add_fetch(quota, n, __ATOMIC_SEQ_CST);
}

int __rate_quota_acquire(struct ldms_rail_rate_quota_s *c, uint64_t n)
{
	int rc;
	time_t tv_sec;
	struct timespec ts;
	uint64_t v0, v1;

	if (c->rate == LDMS_UNLIMITED)
		return 0;

 again:
	__atomic_load(&c->ts.tv_sec, &tv_sec, __ATOMIC_SEQ_CST);
	__atomic_load(&c->quota, &v0, __ATOMIC_SEQ_CST);
	rc = clock_gettime(CLOCK_REALTIME, &ts);
	if (rc)
		return errno;
	if (tv_sec < ts.tv_sec) {
		/* the second has changed, reset quota */
		if (0 == __atomic_compare_exchange(
				&c->quota, &v0, &c->rate,
				0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			goto again;
		/* update ts */
		if (0 == __atomic_compare_exchange(
				&c->ts.tv_sec, &tv_sec, &ts.tv_sec,
				0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			goto again;
		/* quota reset; proceed */
		v0 = c->rate;
	}

	if (v0 < n)
		return ENOBUFS;
	v1 = v0 - n;
	if (0 == __atomic_compare_exchange(
			&c->quota, &v0, &v1,
			0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		/* The other thread won the quota modification race; try again. */
		goto again;
	return 0;
}

void __rate_quota_release(struct ldms_rail_rate_quota_s *c, uint64_t n)
{
	int rc;
	struct timespec ts;
	if (c->rate == LDMS_UNLIMITED)
		return;
	rc = clock_gettime(CLOCK_REALTIME, &ts);
	if (rc)
		return ;
	if (0 == __atomic_compare_exchange( &c->ts.tv_sec, &ts.tv_sec, &ts.tv_sec,
				0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		return; /* tv_sec changed, no need to return */
	__atomic_fetch_add(&c->quota, n, __ATOMIC_SEQ_CST);
}

int ldms_xprt_connected(struct ldms_xprt *x);

int __rail_rep_send_raw(struct ldms_rail_ep_s *rep, void *data, int len)
{
	ldms_t x = rep->ep;
	int rc;
	/* mimicking ldms_xprt_send */
	pthread_mutex_lock(&x->lock);
	if (!ldms_xprt_connected(x))
		return ENOTCONN;
	rc = zap_send(x->zap_ep, data, len);
	pthread_mutex_unlock(&x->lock);
	return rc;
}

static int __rail_send(ldms_t _r, char *msg_buf, size_t msg_len,
				struct ldms_op_ctxt *op_ctxt)
{
	/* send over ep0 for now */
	ldms_rail_t r = (ldms_rail_t)_r;
	int rc;
	struct ldms_rail_ep_s *rep; /* an endpoint inside the rail */

	pthread_mutex_lock(&r->mutex);
	if (r->eps[0].state != LDMS_RAIL_EP_CONNECTED) {
		rc = ENOTCONN;
		goto out;
	}
	rep = &r->eps[0];

	if (ENABLED_PROFILING(LDMS_XPRT_OP_SEND)) {
		TAILQ_INSERT_TAIL(&(rep->op_ctxt_lists[LDMS_XPRT_OP_SEND]),
		                                             op_ctxt, ent);
	}
	rc = rep->ep->ops.send(rep->ep, msg_buf, msg_len, op_ctxt);
	if (rc) {
		if (ENABLED_PROFILING(LDMS_XPRT_OP_SEND)) {
			TAILQ_REMOVE(&(rep->op_ctxt_lists[LDMS_XPRT_OP_SEND]),
			                                        op_ctxt, ent);
		}
		/* release the acquired quota if send failed */
		__rep_quota_release(rep, msg_len);
	}
 out:
	pthread_mutex_unlock(&r->mutex);
	return rc;
}

static size_t __rail_msg_max(ldms_t _r)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	return	r->max_msg - (sizeof(struct ldms_request_hdr) +
			sizeof(struct ldms_send_cmd_param));
}

/* interposer */
void __rail_dir_cb(ldms_t x, int status, ldms_dir_t dir, void *cb_arg)
{
	ldms_rail_dir_ctxt_t dc = cb_arg;
	int free_dc = 0;
	if (!(dc->flags & LDMS_DIR_F_NOTIFY) && !dir->more) {
		free_dc = 1;
	}
	/* the application owns `dir` after the callback */
	dc->app_cb((void*)dc->r, status, dir, dc->cb_arg);
	if (free_dc)
		free(dc);
}

static int __rail_dir(ldms_t _r, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	ldms_rail_dir_ctxt_t dc = calloc(1, sizeof(*dc));
	int rc;
	if (!dc)
		return ENOMEM;
	dc->r = r;
	dc->app_cb = cb;
	dc->cb_arg = cb_arg;
	dc->flags = flags;
	if (dc->flags & LDMS_DIR_F_NOTIFY) {
		pthread_mutex_lock(&r->mutex);
		TAILQ_INSERT_TAIL(&r->dir_notify_tq, dc, tqe);
		pthread_mutex_unlock(&r->mutex);
	}
	rc = ldms_xprt_dir(r->eps[0].ep, __rail_dir_cb, dc, flags);
	if (rc) {
		/* synchronous error, clean up the context */
		free(dc);
	}
	return rc;
}

static int __rail_dir_cancel(ldms_t _r)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	return ldms_xprt_dir_cancel(r->eps[0].ep);
}

typedef
struct ldms_rail_lookup_ctxt_s {
	ldms_rail_t r;
	ldms_lookup_cb_t app_cb;
	void *cb_arg;
	enum ldms_lookup_flags flags;
	struct ldms_op_ctxt *op_ctxt;
} *ldms_rail_lookup_ctxt_t;

void __rail_lookup_cb(ldms_t x, enum ldms_lookup_status status,
			int more, ldms_set_t s, void *arg)
{
	ldms_rail_lookup_ctxt_t lc = arg;
	if (ENABLED_PROFILING(LDMS_XPRT_OP_LOOKUP)) {
		(void)clock_gettime(CLOCK_REALTIME, &lc->op_ctxt->lookup_profile.deliver_ts);
	}
	lc->app_cb((void*)lc->r, status, more, s, lc->cb_arg);
	if (!more)
		free(lc);
}

static int __rail_lookup(ldms_t _r, const char *name, enum ldms_lookup_flags flags,
                   ldms_lookup_cb_t cb, void *cb_arg, struct ldms_op_ctxt *op_ctxt)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	int rc;
	struct ldms_rail_ep_s *rep;
	ldms_rail_lookup_ctxt_t lc;

	pthread_mutex_lock(&r->mutex);
	if (r->state != LDMS_RAIL_EP_CONNECTED) {
		rc = ENOTCONN;
		goto out;
	}
	lc = calloc(1, sizeof(*lc));
	if (!lc) {
		rc = ENOMEM;
		goto out;
	}
	lc->r = r;
	lc->app_cb = cb;
	lc->cb_arg = cb_arg;
	lc->flags = flags;
	lc->op_ctxt = op_ctxt;
	rep = &r->eps[r->lookup_rr++];
	r->lookup_rr %= r->n_eps;

	if (ENABLED_PROFILING(LDMS_XPRT_OP_LOOKUP)) {
		TAILQ_INSERT_TAIL(&(rep->op_ctxt_lists[LDMS_XPRT_OP_LOOKUP]), op_ctxt, ent);
	}
	rc = rep->ep->ops.lookup(rep->ep, name, flags, __rail_lookup_cb, lc, op_ctxt);
	if (rc) {
		/* synchronous error */
		free(lc);
		if (ENABLED_PROFILING(LDMS_XPRT_OP_LOOKUP)) {
			TAILQ_REMOVE(&rep->op_ctxt_lists[LDMS_XPRT_OP_LOOKUP], op_ctxt, ent);
		}
	}
 out:
	pthread_mutex_unlock(&r->mutex);
	return rc;
}

static int __rail_stats(ldms_t _r, struct ldms_xprt_stats_s *stats, int mask, int is_reset)
{
	int i, rc;
	ldms_rail_t r = (ldms_rail_t)_r;
	struct ldms_rail_ep_s *rep;
	memset(stats, 0, sizeof(*stats));

	if (!stats) {
		/* Only reset */
		for (i = 0; i < r->n_eps; i++) {
			rep = &r->eps[i];
			rc = ldms_xprt_stats(rep->ep, NULL, mask, is_reset);
			if (rc)
				return rc;
		}
		return 0;
	}

	/* Get the statistics */
	stats->rail.eps_stats = malloc(r->n_eps * sizeof(*stats->rail.eps_stats));
	if (!stats->rail.eps_stats) {
		return ENOMEM;
	}

	stats->name = strdup(ldms_xprt_type_name(_r));
	if (!stats->name) {
		free(stats->rail.eps_stats);
		return ENOMEM;
	}

	rc = ldms_xprt_names((ldms_t)r, stats->lhostname, sizeof(stats->lhostname),
				   stats->lport_no, sizeof(stats->lport_no),
				   stats->rhostname, sizeof(stats->rhostname),
				   stats->rport_no, sizeof(stats->rport_no),
				   NI_NAMEREQD | NI_NUMERICSERV);
	if (rc) {
		memset(&stats->lhostname, 0, sizeof(stats->lhostname));
		memset(&stats->lport_no, 0, sizeof(stats->lport_no));
		memset(&stats->rhostname, 0, sizeof(stats->rhostname));
		memset(&stats->rport_no, 0, sizeof(stats->rport_no));
	}

	switch (r->state) {
	case LDMS_RAIL_EP_LISTENING:
		stats->state = LDMS_XPRT_STATS_S_LISTEN;
		break;
	case LDMS_RAIL_EP_ACCEPTING:
	case LDMS_RAIL_EP_CONNECTING:
		stats->state = LDMS_XPRT_STATS_S_CONNECTING;
		break;
	case LDMS_RAIL_EP_CONNECTED:
		stats->state = LDMS_XPRT_STATS_S_CONNECT;
		break;
	case LDMS_RAIL_EP_INIT:
	case LDMS_RAIL_EP_PEER_CLOSE:
	case LDMS_RAIL_EP_CLOSE:
	case LDMS_RAIL_EP_ERROR:
	case LDMS_RAIL_EP_REJECTED:
		stats->state = LDMS_XPRT_STATS_S_CLOSE;
		break;
	}

	stats->xflags = LDMS_STATS_XFLAGS_RAIL;
	if (r->xtype == LDMS_XTYPE_ACTIVE_RAIL)
		stats->xflags |= LDMS_STATS_XFLAGS_ACTIVE;
	else
		stats->xflags |= LDMS_STATS_XFLAGS_PASSIVE;
	stats->connected = r->connected_ts;
	stats->disconnected = r->disconnected_ts;
	stats->rail.n_eps = r->n_eps;

	for (i = 0; i < r->n_eps; i++) {
		rep = &r->eps[i];
		rc = ldms_xprt_stats(rep->ep, &stats->rail.eps_stats[i], mask, is_reset);
		if (rc)
			goto err;
	}

	if (is_reset) {
		/* No statistics to be reset for rails */
	}

	return 0;
err:
	if (stats->rail.eps_stats) {
		for (i = 0; i < r->n_eps; i++) {

		}
	}
	free(stats->rail.eps_stats);
	return rc;
}

static ldms_t ___rail_get(ldms_t _r, const char *name, const char *func, int line)
{
	assert(XTYPE_IS_RAIL(_r->xtype));
	ldms_rail_t r = (ldms_rail_t)_r;
	_ref_get(&r->ref, name, func, line);
	return (ldms_t)r;
}

static void ___rail_put(ldms_t _r, const char *name, const char *func, int line)
{
	assert(XTYPE_IS_RAIL(_r->xtype));
	ldms_rail_t r = (ldms_rail_t)_r;
	_ref_put(&r->ref, name, func, line);
}

static void __rail_ctxt_set(ldms_t _r, void *ctxt, app_ctxt_free_fn fn)
{
	assert(XTYPE_IS_RAIL(_r->xtype));
	ldms_rail_t r = (ldms_rail_t)_r;
	r->app_ctxt = ctxt;
	r->app_ctxt_free = fn;
}

static void *__rail_ctxt_get(ldms_t _r)
{
	assert(XTYPE_IS_RAIL(_r->xtype));
	ldms_rail_t r = (ldms_rail_t)_r;
	return r->app_ctxt;
}

static uint64_t __rail_conn_id(ldms_t _r)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	return r->conn_id;
}

static const char *__rail_type_name(ldms_t _r)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	return r->name;
}

static void __rail_priority_set(ldms_t _r, int prio)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	int i;
	for (i = 0; i < r->n_eps; i++) {
		ldms_xprt_priority_set(r->eps[i].ep, prio);
	}
}

static void __rail_cred_get(ldms_t _r, ldms_cred_t lcl, ldms_cred_t rmt)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	ldms_xprt_cred_get(r->eps[0].ep, lcl, rmt);
}

typedef
struct ldms_rail_update_ctxt_s {
	ldms_rail_t r;
	ldms_update_cb_t app_cb;
	void *cb_arg;
} *ldms_rail_update_ctxt_t;

void __rail_update_cb(ldms_t x, ldms_set_t s, int flags, void *arg)
{
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);
	ldms_rail_update_ctxt_t uc = arg;
	if (ENABLED_PROFILING(LDMS_XPRT_OP_UPDATE)) {
		struct ldms_op_ctxt *op_ctxt = s->curr_updt_ctxt;

		(void)clock_gettime(CLOCK_REALTIME, &op_ctxt->update_profile.deliver_ts);
		s->curr_updt_ctxt = NULL;
	}
	uc->app_cb((ldms_t)rep->rail, s, flags, uc->cb_arg);
	if (!(flags & LDMS_UPD_F_MORE)) {
		free(uc);
	}
}

static int __rail_update(ldms_t _r, struct ldms_set *set, ldms_update_cb_t cb,
									  void *arg, struct ldms_op_ctxt *op_ctxt)
{
	ldms_rail_t r = (void*)_r;
	ldms_rail_update_ctxt_t uc;
	struct ldms_rail_ep_s *rep;
	int rc;

	uc = calloc(1, sizeof(*uc));
	if (!uc)
		return errno;
	uc->r = r;
	uc->app_cb = cb;
	uc->cb_arg = arg;

	rep = ldms_xprt_ctxt_get(set->xprt);
	if (ENABLED_PROFILING(LDMS_XPRT_OP_UPDATE)) {
		TAILQ_INSERT_TAIL(&(rep->op_ctxt_lists[LDMS_XPRT_OP_UPDATE]), op_ctxt, ent);
		set->curr_updt_ctxt = op_ctxt;
	}
	rc = set->xprt->ops.update(set->xprt, set, __rail_update_cb, uc, op_ctxt);
	if (rc) {
		/* synchronously error, clean up the context */
		set->curr_updt_ctxt = NULL;

		if (ENABLED_PROFILING(LDMS_XPRT_OP_UPDATE))
			TAILQ_REMOVE(&(rep->op_ctxt_lists[LDMS_XPRT_OP_UPDATE]), op_ctxt, ent);
		free(uc);
	}
	return rc;
}

static int __rail_get_threads(ldms_t _r, pthread_t *out, int n)
{
	ldms_rail_t r = (void*)_r;
	int i;
	if (n < 1)
		return -EINVAL;
	if (r->n_eps > n)
		return -ENOMEM;
	for (i = 0; i < n; i++) {
		ldms_t x = r->eps[i].ep;
		if (x) {
			ldms_xprt_get_threads(x, &out[i], 1);
		} else {
			out[i] = 0;
		}
	}
	return r->n_eps;
}

int ldms_xprt_is_rail(ldms_t x)
{
	return XTYPE_IS_RAIL(x->xtype);
}

int ldms_xprt_is_remote_rail(ldms_t x)
{
	if (!XTYPE_IS_RAIL(x->xtype))
		return 0;

	ldms_rail_t r = (void*)x;
	return r->eps[0].remote_is_rail == 1;
}

int ldms_xprt_rail_eps(ldms_t _r)
{
	ldms_rail_t r = (void*)_r;
	if (!_r)
		return -EINVAL;
	if (!XTYPE_IS_RAIL(_r->xtype))
		return -EINVAL;
	return r->n_eps;
}

int64_t ldms_xprt_rail_recv_quota_get(ldms_t _r)
{
	ldms_rail_t r = (void*)_r;
	if (!_r)
		return -EINVAL;
	if (!XTYPE_IS_RAIL(_r->xtype))
		return -EINVAL;
	return r->recv_quota;
}

int64_t ldms_xprt_rail_recv_rate_limit_get(ldms_t _r)
{
	ldms_rail_t r = (void*)_r;
	if (!_r)
		return -EINVAL;
	if (!XTYPE_IS_RAIL(_r->xtype))
		return -EINVAL;
	return r->recv_rate_limit;
}

int64_t ldms_xprt_rail_send_rate_limit_get(ldms_t _r)
{
	ldms_rail_t r = (void*)_r;
	if (!_r)
		return -EINVAL;
	if (!XTYPE_IS_RAIL(_r->xtype))
		return -EINVAL;
	return r->send_rate_limit;
}

void __rail_ep_limit(ldms_t x, void *msg, int msg_len)
{
	/* x is the legacy ldms xprt in the rail, its context is the assocated
	 * rail_ep structure. */
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);
	struct ldms_rail_conn_msg_s *conn_msg = msg;

	if (msg_len != sizeof(*conn_msg))
		goto unlimited;
	if (conn_msg->conn_type != htonl(LDMS_CONN_TYPE_RAIL))
		goto unlimited;
	/* This does not race; during end point setup */
	rep->rail->send_quota = be64toh(conn_msg->recv_quota);
	rep->rail->send_rate_limit = be64toh(conn_msg->rate_limit);

	rep->send_quota = be64toh(conn_msg->recv_quota);
	rep->rate_quota.quota = be64toh(conn_msg->rate_limit);
	rep->rate_quota.rate   = be64toh(conn_msg->rate_limit);
	rep->rate_quota.ts.tv_sec  = 0;
	rep->rate_quota.ts.tv_nsec = 0;
	rep->remote_is_rail = 1;
	rep->rail->peer_msg_enabled = ntohl(conn_msg->msg_enabled);
	return;
 unlimited:
	rep->send_quota = LDMS_UNLIMITED;
	rep->rate_quota.quota = LDMS_UNLIMITED;
	rep->rate_quota.rate   = LDMS_UNLIMITED;
	rep->remote_is_rail = 0;
	rep->rail->peer_msg_enabled = 0;
}

void __rail_process_send_quota(ldms_t x, struct ldms_request *req)
{
	uint32_t sc = ntohl(req->send_quota.send_quota);
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);
	struct ldms_xprt_event ev = {0};
	ev.quota.quota = __rep_quota_release(rep, sc);
	ev.quota.ep_idx = rep->idx;
	ev.quota.rc = ntohl(req->send_quota.rc);
	if (ev.quota.rc == ENOTSUP) {
		rep->rail->peer_msg_enabled = 0;
	}
	ev.type = LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED;
	rep->rail->event_cb((ldms_t)rep->rail, &ev, rep->rail->event_cb_arg);
	__rep_flush_sbuf_tq(rep);
}

struct ldms_op_ctxt_list *
__rail_op_ctxt_list(struct ldms_xprt *x, enum ldms_xprt_ops_e op_e)
{
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);
	return &(rep->op_ctxt_lists[op_e]);
}

int ldms_xprt_rail_send_quota_get(ldms_t _r, uint64_t *quotas, int n)
{
	ldms_rail_t r;
	int i;
	if (!_r)
		return -EINVAL;
	if (!XTYPE_IS_RAIL(_r->xtype))
		return -EINVAL;
	r = (void*)_r;
	if (n < r->n_eps)
		return -ENOMEM;
	for (i = 0; i < r->n_eps; i++) {
		quotas[i] = r->eps[i].send_quota;
	}
	return 0;
}

int ldms_xprt_rail_pending_ret_quota_get(ldms_t _r, uint64_t *out, int n)
{
	ldms_rail_t r;
	int i;
	if (!_r)
		return -EINVAL;
	if (!XTYPE_IS_RAIL(_r->xtype))
		return -EINVAL;
	r = (void*)_r;
	if (n < r->n_eps)
		return -ENOMEM;
	for (i = 0; i < r->n_eps; i++) {
		out[i] = r->eps[i].pending_ret_quota;
	}
	return 0;
}

int ldms_xprt_rail_in_eps_stq_get(ldms_t _r, uint64_t *out, int n)
{
	ldms_rail_t r;
	int i;
	if (!_r)
		return -EINVAL;
	if (!XTYPE_IS_RAIL(_r->xtype))
		return -EINVAL;
	r = (void*)_r;
	if (n < r->n_eps)
		return -ENOMEM;
	for (i = 0; i < r->n_eps; i++) {
		out[i] = r->eps[i].in_eps_stq;
	}
	return 0;
}

zap_ep_t __rail_get_zap_ep(ldms_t x)
{
	ldms_rail_t r = (void*)x;
	if (!r->eps[0].ep)
		return NULL;
	return r->eps[0].ep->zap_ep;
}

void timespec_hton(struct timespec *ts)
{
	ts->tv_nsec = htobe64(ts->tv_nsec);
	ts->tv_sec = htobe64(ts->tv_sec);
}

void timespec_ntoh(struct timespec *ts)
{
	ts->tv_nsec = be64toh(ts->tv_nsec);
	ts->tv_sec = be64toh(ts->tv_sec);
}

int sockaddr2ldms_addr(struct sockaddr *sa, struct ldms_addr *la)
{
	union ldms_sockaddr *lsa = (void*)sa;
	switch (sa->sa_family) {
	case AF_INET:
		la->sa_family = sa->sa_family;
		la->sin_port = lsa->sin.sin_port;
		memcpy(&la->addr, &lsa->sin.sin_addr, sizeof(lsa->sin.sin_addr));
		break;
	case AF_INET6:
		la->sa_family = sa->sa_family;
		la->sin_port = lsa->sin6.sin6_port;
		memcpy(&la->addr, &lsa->sin6.sin6_addr, sizeof(lsa->sin6.sin6_addr));
		break;
	default:
		return ENOTSUP;
	}
	return 0;
}

const char *sockaddr_ntop(struct sockaddr *sa, char *buff, size_t sz)
{
	union ldms_sockaddr *lsa = (void*)sa;
	char tmp[128];
	switch (sa->sa_family) {
	case 0:
		snprintf(buff, sz, "0.0.0.0:0");
		break;
	case AF_INET:
		inet_ntop(AF_INET, &lsa->sin.sin_addr, tmp, sizeof(tmp));
		snprintf(buff, sz, "%s:%d", tmp, ntohs(lsa->sin.sin_port));
		break;
	case AF_INET6:
		inet_ntop(AF_INET6, &lsa->sin6.sin6_addr, tmp, sizeof(tmp));
		snprintf(buff, sz, "[%s]:%d", tmp, ntohs(lsa->sin6.sin6_port));
		break;
	default:
		snprintf(buff, sz, "__UNSUPPORTED__");
	}
	return buff;
}

const char *ldms_addr_ntop(struct ldms_addr *addr, char *buff, size_t sz)
{
	char tmp[128];
	switch (addr->sa_family) {
	case 0:
		snprintf(buff, sz, "0.0.0.0:0");
		break;
	case AF_INET:
		inet_ntop(AF_INET, &addr->addr, tmp, sizeof(tmp));
		snprintf(buff, sz, "%s:%d", tmp, ntohs(addr->sin_port));
		break;
	case AF_INET6:
		inet_ntop(AF_INET6, &addr->addr, tmp, sizeof(tmp));
		snprintf(buff, sz, "[%s]:%d", tmp, ntohs(addr->sin_port));
		break;
	default:
		snprintf(buff, sz, "__UNSUPPORTED__");
	}
	return buff;
}

/* *2 for the two hex digits needed for each 16-bit value, * 8 for the 8 groups of values, + 7 for the 7 colons */
#define MAX_IPV6_STR_LEN (sizeof(uint16_t) * 2 * 8 + 7)
int ldms_cidr2addr(const char *cdir_str, struct ldms_addr *addr, int *prefix_len)
{
	int rc;
	int is_ipv6 = 0;
	char netaddr_str[MAX_IPV6_STR_LEN];
	int _prefix_len;

	if (strchr(cdir_str, ':') != NULL)
		is_ipv6 = 1;

	rc = sscanf(cdir_str, "%[^/]/%d", netaddr_str, &_prefix_len);
	if (rc != 2) {
		return EINVAL;
	}

	if (prefix_len)
		*prefix_len = _prefix_len;

	if (addr) {
		if (is_ipv6) {
			addr->sa_family = AF_INET6;
			rc = inet_pton(AF_INET6, netaddr_str, &addr->addr);
		} else {
			addr->sa_family = AF_INET;
			rc = inet_pton(AF_INET, netaddr_str, &addr->addr);
		}
	}

	if (rc != 1)
		return rc;
	return 0;
}

int ldms_addr_in_network_addr(struct ldms_addr *ip_addr,
				struct ldms_addr *net_addr, int prefix_len)
{
	if (ip_addr->sa_family != net_addr->sa_family)
		return 0;

	int i;
	int masked_bytes = prefix_len/8;
	int residue_bits = prefix_len % 8;

	for (i = 0; i < masked_bytes; i++) {
		if (ip_addr->addr[i] != net_addr->addr[i])
			return 0;
	}

	if (residue_bits) {
		uint8_t mask_bits = 0xff << (8 - residue_bits);
		if ( (ip_addr->addr[i] & mask_bits) != (net_addr->addr[i] & mask_bits))
			return 0;
	}

	return 1;
}

ldms_t __ldms_xprt_to_rail(ldms_t x)
{
	if (XTYPE_IS_RAIL(x->xtype)) {
		return x;
	}

	struct ldms_rail_ep_s *ep = ldms_xprt_ctxt_get(x);
	return ((ep)?(ldms_t)ep->rail:NULL);
}

static ldms_set_t __rail_set_by_name(ldms_t _x, const char *set_name)
{
	struct ldms_set *set;
	struct rbn *rbn = NULL;
	int i;
	struct ldms_rail_s *r = (void*)_x;
	struct ldms_xprt *x;

	assert(XTYPE_IS_RAIL(r->xtype));

	__ldms_set_tree_lock();
	set = __ldms_find_local_set(set_name);
	__ldms_set_tree_unlock();
	if (!set)
		return NULL;
	for (i = 0; i < r->n_eps; i++) {
		x = r->eps[i].ep;
		pthread_mutex_lock(&x->lock);
		rbn = rbt_find(&x->set_coll, set);
		pthread_mutex_unlock(&x->lock);
		if (rbn) {
			/* found */
			break;
		}
	}
	if (!rbn) {
		/* no set found in any of the xprt */
		ref_put(&set->ref, "__ldms_find_local_set");
		set = NULL;
	}
	return set;
}

/* defined in ldms_xprt.c */
size_t format_set_delete_req(struct ldms_request *req, uint64_t xid,
			     const char *inst_name);

/* Called from ldms_xprt.c.
 * Tell the peer that have an RBD for this set that it is being
 * deleted. When they all reply, we can delete the set.
 */
void __rail_on_set_delete(ldms_t _r, struct ldms_set *s,
			      ldms_set_delete_cb_t cb_fn)
{
	assert(XTYPE_IS_RAIL(_r->xtype));

	ldms_rail_t r = (ldms_rail_t)_r;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;
	struct rbn *rbn;
	struct xprt_set_coll_entry *ent;
	int i;
	ldms_t x;
	struct ldms_rail_ep_s *rep;
	struct ldms_op_ctxt *op_ctxt;

	x = NULL;

	/* for each x in r */
	for (i = 0; i < r->n_eps; i++) {
		x = r->eps[i].ep;
		pthread_mutex_lock(&x->lock);
		rbn = rbt_find(&x->set_coll, s);
		if (rbn)
			goto found;
		pthread_mutex_unlock(&x->lock);
	}

	/* No rbn found in any x in r. Just use the first x to notify with
	 * ctxt->set_delete.lookup being 0. */
	x = r->eps[0].ep;

	/* let through */

	pthread_mutex_lock(&x->lock);
 found:
	ctxt = __ldms_alloc_ctxt
		(x,
		 sizeof(struct ldms_request) + sizeof(struct ldms_context),
		 LDMS_CONTEXT_SET_DELETE,
		 s,
		 cb_fn);
	if (!ctxt) {
		ovis_log(xlog, OVIS_LCRIT, "%s:%s:%d Memory allocation failure\n",
				__FILE__, __func__, __LINE__);
		pthread_mutex_unlock(&x->lock);
		return;
	}
	if (rbn) {
		/* We'll put set ref when we receive the reply. */
		ctxt->set_delete.lookup = 1;
		rbt_del(&x->set_coll, rbn);
		ent = container_of(rbn, struct xprt_set_coll_entry, rbn);
		free(ent);
	} else {
		/* We won't put ref on receiving reply. */
		ctxt->set_delete.lookup = 0;
	}
	req = (struct ldms_request *)(ctxt + 1);
	len = format_set_delete_req(req, (uint64_t)(unsigned long)ctxt,
					ldms_set_instance_name_get(s));
	if (ENABLED_PROFILING(LDMS_XPRT_OP_SET_DELETE)) {
		rep = (struct ldms_rail_ep_s *)ldms_xprt_ctxt_get(x);
		op_ctxt = calloc(1, sizeof(*op_ctxt));
		if (!op_ctxt) {
			ovis_log(xlog, OVIS_LCRIT, "%s:%s:%d Memory allocation failure\n",
					__FILE__, __func__, __LINE__);
			/* Let the routine continue */
		} else {
			ctxt->op_ctxt = op_ctxt;
			TAILQ_INSERT_TAIL(&rep->op_ctxt_lists[LDMS_XPRT_OP_SET_DELETE], op_ctxt, ent);
			(void)clock_gettime(CLOCK_REALTIME, &op_ctxt->set_del_profile.send_ts);
		}
	}
	zap_err_t zerr = zap_send(x->zap_ep, req, len);
	if (zerr) {
		char name[128];
		(void) ldms_xprt_names(x, NULL, 0, NULL, 0, name, 128,
					     NULL, 0, NI_NUMERICHOST);
		ovis_log(xlog, OVIS_LERROR, "%s:%s:%d Error %d sending "
				"the LDMS_SET_DELETE message to '%s'\n",
				__FILE__, __func__, __LINE__, zerr, name);
		x->zerrno = zerr;
		__ldms_free_ctxt(x, ctxt);
		if (ENABLED_PROFILING(LDMS_XPRT_OP_SET_DELETE))
			TAILQ_REMOVE(&rep->op_ctxt_lists[LDMS_XPRT_OP_SET_DELETE], op_ctxt, ent);
	}
	pthread_mutex_unlock(&x->lock);
}

int __rep_flush_sbuf_tq(struct ldms_rail_ep_s *rep)
{
	int rc;
	struct ldms_op_ctxt *op_ctxt = NULL;
	struct __pending_sbuf_s *p;
	while ((p = TAILQ_FIRST(&rep->sbuf_tq))) {
		rc = __rep_quota_acquire(rep, p->sbuf->msg->msg_len);
		if (rc)
			goto out;

		op_ctxt = calloc(1, sizeof(*op_ctxt));
		if (!op_ctxt) {
			rc = ENOMEM;
			goto out;
		}
		op_ctxt->op_type = LDMS_XPRT_OP_MSG_PUBLISH;
		op_ctxt->msg_pub_profile.hop_num = p->hop_num;
		op_ctxt->msg_pub_profile.recv_ts = p->recv_ts;

		if (ENABLED_PROFILING(LDMS_XPRT_OP_MSG_PUBLISH)) {
			TAILQ_INSERT_TAIL(&(rep->op_ctxt_lists[LDMS_XPRT_OP_MSG_PUBLISH]),
										op_ctxt, ent);
		}
		rc = __rep_publish(rep, p->sbuf->name,
				p->sbuf->msg->name_hash,
				p->sbuf->msg->msg_type,
			     &p->sbuf->msg->src, p->sbuf->msg->msg_gn,
			     &p->sbuf->msg->cred, p->sbuf->msg->perm,
				 p->sbuf->msg->hop_cnt,
				 p->sbuf->msg->hops,
			     p->sbuf->data, p->sbuf->data_len,
				 &(op_ctxt->msg_pub_profile));
		if (rc) {
			__rep_quota_release(rep, p->sbuf->msg->msg_len);
			if (ENABLED_PROFILING(LDMS_XPRT_OP_MSG_PUBLISH)) {
				TAILQ_REMOVE(&(rep->op_ctxt_lists[LDMS_XPRT_OP_MSG_PUBLISH]),
										op_ctxt, ent);
				free(op_ctxt);
			}
			goto out;
		}
		if (!ENABLED_PROFILING(LDMS_XPRT_OP_MSG_PUBLISH)) {
			free(op_ctxt);
		}
		TAILQ_REMOVE(&rep->sbuf_tq, p, entry);
		ref_put(&p->sbuf->ref, "pending");
		free(p);
	}
	rc = 0;
 out:
	return rc;
}

int __rep_quota_acquire(struct ldms_rail_ep_s *rep, uint64_t q)
{
	int rc;
	rc = __quota_acquire(&rep->send_quota, q);
	if (rc)
		return rc;
	rc = __rate_quota_acquire(&rep->rate_quota, q);
	if (rc) {
		__rep_quota_release(rep, q);
		return rc;
	}
	return 0;
}

int __rep_quota_release(struct ldms_rail_ep_s *rep, uint64_t q)
{
	uint64_t v0, v1, vx;
	/* take care of your debt first */
 again:
	__atomic_load(&rep->send_quota_debt, &v0, __ATOMIC_SEQ_CST);
	if (!v0)
		goto release;
	if (v0 <= q) {
		vx = v0;
		v1 = 0;
		if (!__atomic_compare_exchange(&rep->send_quota_debt, &vx, &v1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			goto again;
		q -= v0;
	} else {
		vx = v0;
		v1 = v0 - q;
		if (!__atomic_compare_exchange(&rep->send_quota_debt, &vx, &v1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			goto again;
		q = 0;
	}
 release:
	/* then release what you have left */
	return __quota_release(&rep->send_quota, q);
}

int ldms_xprt_rail_recv_quota_set(ldms_t x, uint64_t q)
{
	ldms_rail_t r;
	struct ldms_rail_ep_s *rep;
	int len;
	zap_err_t zerr;

	if (!LDMS_IS_RAIL(x))
		return -EINVAL;

	r = (void*)x;

	if (q == r->recv_quota)
		return 0; /* no need to continue */

	r->recv_quota = q;

	rep = &r->eps[0];

	len = sizeof(struct ldms_request_hdr) +
		sizeof(struct ldms_quota_reconfig_param);
	struct ldms_request req = {
		.hdr = {
			.cmd = htonl(LDMS_CMD_QUOTA_RECONFIG),
			.len = htonl(len),
		},
		.quota_reconfig = {
			.q = htobe64(q),
		}};
	zerr = zap_send(rep->ep->zap_ep, &req, len);
	return -zap_zerr2errno(zerr);
}

void __rail_process_quota_reconfig(ldms_t x, struct ldms_request *req)
{
	int64_t add;
	int i, rc;
	uint64_t q = be64toh(req->quota_reconfig.q);
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);
	ldms_rail_t r = rep->rail;
	add = q - r->send_quota;
	r->send_quota = q;
	if (!add)
		return; /* value not changing, nothing else to do */
	for (i = 0; i < r->n_eps; i++) {
		rep = &r->eps[i];
		if (add > 0) {
			__rep_quota_release(rep, add);
		} else {
			rc = __quota_acquire(&rep->send_quota, -add);
			if (rc) {
				/* not enough quota to take, now we're in debt */
				__atomic_add_fetch(&rep->send_quota_debt, -add, __ATOMIC_SEQ_CST);
			}
		}
	}
}

int ldms_xprt_rail_recv_rate_limit_set(ldms_t x, uint64_t rate)
{
	ldms_rail_t r;
	struct ldms_rail_ep_s *rep;
	int len;
	zap_err_t zerr;

	if (!LDMS_IS_RAIL(x))
		return -EINVAL;

	r = (void*)x;

	if (rate == r->recv_rate_limit)
		return 0; /* no need to continue */

	r->recv_rate_limit = rate;

	rep = &r->eps[0];

	len = sizeof(struct ldms_request_hdr) +
		sizeof(struct ldms_quota_reconfig_param);
	struct ldms_request req = {
		.hdr = {
			.cmd = htonl(LDMS_CMD_RATE_RECONFIG),
			.len = htonl(len),
		},
		.rate_reconfig = {
			.rate = htobe64(rate),
		}};
	zerr = zap_send(rep->ep->zap_ep, &req, len);
	return -zap_zerr2errno(zerr);
}

void __rail_process_rate_reconfig(ldms_t x, struct ldms_request *req)
{
	int i;
	uint64_t rate = be64toh(req->rate_reconfig.rate);
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);
	ldms_rail_t r = rep->rail;

	if (r->send_rate_limit == rate)
		return;
	r->send_rate_limit = rate;
	/* We can just update the rate as the rate_quota will be reset in the
	 * next second anyway. */
	for (i = 0; i < r->n_eps; i++) {
		rep = &r->eps[i];
		rep->rate_quota.rate = rate;
	}
}

void sync_connect_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	ldms_rail_t r = (ldms_rail_t)x;
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		r->sem_rc = 0;
		sem_post(&r->sem);
		break;
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_ERROR:
	case LDMS_XPRT_EVENT_DISCONNECTED:
		r->sem_rc = ECONNREFUSED;
		sem_post(&r->sem);
		break;
	case LDMS_XPRT_EVENT_RECV:
		break;
	case LDMS_XPRT_EVENT_SET_DELETE:
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
	case LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED:
		/* Don't post */
		return;
	default:
		RAIL_LOG("sync_connect_cb: unexpected "
				"ldms_xprt event value %d\n", (int) e->type);
		assert(0 == "sync_connect_cb: unexpected ldms_xprt event value");
	}
}

int ldms_rail_connect_by_name(ldms_t x, const char *host, const char *port,
			      ldms_event_cb_t cb, void *cb_arg)
{
	ldms_rail_t r = (ldms_rail_t)x;
	struct addrinfo *ai;
	struct addrinfo hints = {
		.ai_socktype = SOCK_STREAM
	};
	int rc = getaddrinfo(host, port, &hints, &ai);
	if (rc)
		return EHOSTUNREACH;
	if (!cb) {
		rc = ldms_xprt_connect(x, ai->ai_addr, ai->ai_addrlen, sync_connect_cb, cb_arg);
		if (rc)
			goto out;
		sem_wait(&r->sem);
		rc = r->sem_rc;
	} else {
		rc = ldms_xprt_connect(x, ai->ai_addr, ai->ai_addrlen, cb, cb_arg);
	}
out:
	freeaddrinfo(ai);
	return rc;
}

void ldms_xprt_stats_result_free(struct ldms_xprt_stats_result *list)
{
	struct ldms_xprt_stats_s *rstats;

	while ((rstats = LIST_FIRST(list))) {
		LIST_REMOVE(rstats, ent);
		ldms_xprt_stats_clear(rstats);
		free(rstats);
	}
	free(list);
}

struct ldms_xprt_stats_result *ldms_xprt_stats_result_get(int mask, int reset)
{
	int rc;
	struct ldms_xprt_stats_result *list;
	struct ldms_xprt_stats_s *rstats;
	ldms_rail_t r;

	errno = 0;
	list = malloc(sizeof(*list));
	if (!list) {
		ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
		errno = ENOMEM;
		return NULL;
	}
	LIST_INIT(list);

	for (r = rail_first(); r; r = rail_next(r)) {
		rstats = calloc(1, sizeof(*rstats));
		if (!rstats) {
			ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
			errno = ENOMEM;
			goto err;
		}
		rc = ldms_xprt_stats((ldms_t)r, rstats, mask, reset);
		if (rc) {
			errno = rc;
			goto err;
		}
		LIST_INSERT_HEAD(list, rstats, ent);
	}
	return list;
err:
	ldms_xprt_stats_result_free(list);
	return NULL;
}

int ldms_xprt_peer_msg_is_enabled(ldms_t x)
{
	if (!LDMS_IS_RAIL(x)) {
		return 0;
	}
	ldms_rail_t r = LDMS_RAIL(x);
	return r->peer_msg_enabled;
}
