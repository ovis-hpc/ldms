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

#include "ldms_private.h"

extern ovis_log_t xlog;
#define RAIL_LOG(fmt, ...) do { \
	ovis_log(xlog, OVIS_LERROR, fmt, ## __VA_ARGS__); \
} while (0);

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
static int __rail_send(ldms_t _r, char *msg_buf, size_t msg_len);
static size_t __rail_msg_max(ldms_t x);
static int __rail_dir(ldms_t _r, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags);
static int __rail_dir_cancel(ldms_t _r);
static int __rail_lookup(ldms_t _r, const char *name, enum ldms_lookup_flags flags,
	       ldms_lookup_cb_t cb, void *cb_arg);
static void __rail_stats(ldms_t _r, ldms_xprt_stats_t stats);

static ldms_t __rail_get(ldms_t _r); /* ref get */
static void __rail_put(ldms_t _r); /* ref put */
static void __rail_ctxt_set(ldms_t _r, void *ctxt, app_ctxt_free_fn fn);
static void *__rail_ctxt_get(ldms_t _r);
static uint64_t __rail_conn_id(ldms_t _r);
static const char *__rail_type_name(ldms_t _r);
static void __rail_priority_set(ldms_t _r, int prio);
static void __rail_cred_get(ldms_t _r, ldms_cred_t lcl, ldms_cred_t rmt);
static int __rail_update(ldms_t _r, struct ldms_set *set, ldms_update_cb_t cb, void *arg);
static int __rail_get_threads(ldms_t _r, pthread_t *out, int n);

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

	.get          = __rail_get,
	.put          = __rail_put,
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
};

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

static void __rail_ref_free(void *arg)
{
	ldms_rail_t r = arg;
	ldms_rail_dir_ctxt_t dc;
	int i;
	struct ldms_rail_ep_s *rep;

	assert( r->connecting_eps == 0 );
	assert( r->connected_eps  == 0 );

	for (i = 0; i < r->n_eps; i++) {
		rep = &r->eps[i];
		if (rep->ep)
			ldms_xprt_put(rep->ep);
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
/* implementation in ldms_stream.c */
int __stream_buf_cmp(void *tree_key, const void *key);
int __str_rbn_cmp(void *tree_key, const void *key);

/* The implementation is in ldms_xprt.c. */
zap_t __ldms_zap_get(const char *xprt);

ldms_t ldms_xprt_rail_new(const char *xprt_name,
			  int n, int64_t recv_limit, int64_t rate_limit,
			  const char *auth_name,
			  struct attr_value_list *auth_av_list)
{
	ldms_rail_t r;
	zap_t zap;
	int i;

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
	r->recv_limit = recv_limit;
	r->recv_rate_limit = rate_limit;
	rbt_init(&r->stream_client_rbt, __str_rbn_cmp);
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
		r->eps[i].send_credit = __RAIL_UNLIMITED;
		r->eps[i].rate_credit.credit = __RAIL_UNLIMITED;
		r->eps[i].rate_credit.rate   = __RAIL_UNLIMITED;
		r->eps[i].rate_credit.ts.tv_sec    = 0;
		r->eps[i].rate_credit.ts.tv_nsec   = 0;
		r->eps[i].remote_is_rail = -1;
		rbt_init(&r->eps[i].sbuf_rbt, __stream_buf_cmp);
	}

	zap = __ldms_zap_get(xprt_name);
	if (!zap) {
		goto err_1;
	}
	r->max_msg = zap_max_msg(zap);

//	r->eps[0].ep = __ldms_xprt_new_with_auth(r->name, r->auth_name, r->auth_av_list);
//	if (!r->eps[0].ep)
//		goto err_1;
//	ldms_xprt_ctxt_set(r->eps[0].ep, &r->eps[0], NULL);
//	r->max_msg = r->eps[0].ep->max_msg;

	/* The other endpoints will be created later in connect() or
	 * __rail_zap_handle_conn_req() */


	r->conn_id = __atomic_fetch_add(&rail_gn, n, __ATOMIC_SEQ_CST);
	r->rail_id.rail_gn = r->conn_id;
	r->rail_id.pid = getpid();

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
			__RAIL_UNLIMITED, __RAIL_UNLIMITED,
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

/* implementation in ldms_stream.c */
void __stream_on_rail_disconnected(struct ldms_rail_s *r);

/* return send credit to peer */
void __rail_ep_credit_return(struct ldms_rail_ep_s *rep, int credit)
{
	/* give back send credit */
	int len = sizeof(struct ldms_request_hdr) +
		sizeof(struct ldms_send_credit_param);
	struct ldms_request req = {
		.hdr = {
			.cmd = htonl(LDMS_CMD_SEND_CREDIT),
			.len = htonl(len),
		},
		.send_credit = {
			.send_credit = htonl(credit),
		}};
	zap_send(rep->ep->zap_ep, &req, len);
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
	 *   - legacy passive endpoint
	 */

	if (r->xtype == LDMS_XTYPE_PASSIVE_RAIL && r->state == LDMS_RAIL_EP_LISTENING) {
		/* x is a legacy xprt accepted by a listening rail */
		if (e->type == LDMS_XPRT_EVENT_CONNECTED) {
			/* remove the rail interposer */
			x->event_cb = r->event_cb;
			x->event_cb_arg = r->event_cb_arg;
			x->event_cb(x, e, x->event_cb_arg);
		} else {
			/* bad passive legacy xprt does not notify the app */
			ldms_xprt_put(x);
		}
		return;
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
		__rail_cut(r);
		call_cb = __rail_ev_prep(r, &ev);
		passive_rail_rm_trigger = r->xtype == LDMS_XTYPE_PASSIVE_RAIL &&
					  r->connecting_eps == 0 &&
					  r->connected_eps  == 0;
		pthread_mutex_unlock(&r->mutex);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		pthread_mutex_lock(&r->mutex);
		if (r->state == LDMS_RAIL_EP_CONNECTED)
			r->state = LDMS_RAIL_EP_ERROR;
		r->connected_eps--;
		switch (rep->state) {
		case LDMS_RAIL_EP_CONNECTED:
			rep->state = LDMS_RAIL_EP_CLOSE;
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
		__stream_on_rail_disconnected(r);
		break;
	case LDMS_XPRT_EVENT_RECV:
	case LDMS_XPRT_EVENT_SET_DELETE:
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
	case LDMS_XPRT_EVENT_SEND_CREDIT_DEPOSITED:
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

	if (e->type == LDMS_XPRT_EVENT_RECV && __rail_is_connected((void*)r)
			&& r->recv_limit != __RAIL_UNLIMITED) {
		/* give back send credit */
		__rail_ep_credit_return(rep, e->data_len);
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
	m->recv_limit = be64toh(m->recv_limit);
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
		int64_t recv_limit;
		int64_t rate_limit;
		ldms_event_cb_t cb;
		void *cb_arg;
		if (lr) {
			xprt_name = lr->name;
			auth_name = lr->auth_name;
			auth_av_list = lr->auth_av_list;
			recv_limit = lr->recv_limit;
			rate_limit = lr->recv_rate_limit;
			cb = lr->event_cb;
			cb_arg = lr->event_cb_arg;
		} else {
			xprt_name = lx->name;
			auth_name = lx->auth?lx->auth->plugin->name:NULL;
			recv_limit = __RAIL_UNLIMITED;
			rate_limit = __RAIL_UNLIMITED;
			cb = lx->event_cb;
			cb_arg = lx->event_cb_arg;
		}
		r = (void*)ldms_xprt_rail_new(xprt_name, m->n_eps,
				recv_limit, rate_limit, auth_name, auth_av_list);
		if (!r) {
			snprintf(rej_msg, sizeof(rej_msg), "passive rail create error: %d", errno);
			pthread_mutex_unlock(&__rail_mutex);
			goto err_0;
		}
//		/* drop the unused first initial endpoint */
//		ldms_xprt_put(r->eps[0].ep);
//		r->eps[0].ep = NULL;

		r->xtype = LDMS_XTYPE_PASSIVE_RAIL;
		r->state = LDMS_RAIL_EP_ACCEPTING;
		r->send_limit = m->recv_limit;
		r->send_rate_limit = m->rate_limit;
		r->event_cb = cb;
		r->event_cb_arg = cb_arg;
		r->rail_id = rail_id;
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
	r->eps[m->idx].send_credit = r->send_limit;
	r->eps[m->idx].rate_credit.credit = r->send_rate_limit;
	r->eps[m->idx].rate_credit.rate   = r->send_rate_limit;
	r->eps[m->idx].rate_credit.ts.tv_sec  = 0;
	r->eps[m->idx].rate_credit.ts.tv_nsec = 0;
	ldms_xprt_ctxt_set(_x, &r->eps[m->idx], NULL);
	pthread_mutex_unlock(&r->mutex);

	ldms_xprt_get(_x);
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
	ldms_xprt_get(_x);

	ref_get(&r->ref, "ldms_accepting");
	zerr = zap_accept2(zep, ldms_zap_auto_cb, (void*)&msg, sizeof(msg), m->idx);
	if (zerr) {
		RAIL_LOG("ERROR: %d accepting connection from %s.\n", zerr, name);
		goto err_3;
	}

	return;
 err_3:
	ref_put(&r->ref, "ldms_accepting");
	ldms_xprt_put(_x); /* drop 'connect' reference */
 err_2:
	ldms_auth_free(auth);
 err_1:
	pthread_mutex_lock(&r->mutex);
	r->connecting_eps--;
	r->eps[m->idx].ep = NULL;
	r->eps[m->idx].state = LDMS_RAIL_EP_ERROR;
	pthread_mutex_unlock(&r->mutex);
	zap_set_ucontext(_x->zap_ep, NULL);
	ldms_xprt_put(_x);	/* context reference */
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
	m->recv_limit = htobe64(r->recv_limit);
	m->n_eps = htonl(r->n_eps);
	m->idx = htonl(idx);
	m->pid = htonl(getpid());
	m->rail_gn = htobe64(r->rail_id.rail_gn);
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
	ldms_xprt_get(x);
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
		ldms_xprt_put(x);
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
 * \retval ENOBUFS if not enough credit
 */
int __credit_acquire(uint64_t *credit, uint64_t n)
{
	uint64_t v0, v1;

	__atomic_load(credit, &v0, __ATOMIC_SEQ_CST);
	if (v0 == __RAIL_UNLIMITED)
		return 0;
 again:
	if (v0 < n)
		return ENOBUFS;
	v1 = v0 - n;
	if (__atomic_compare_exchange(credit, &v0, &v1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		return 0;
	/* The other thread won the credit modification race; try again. */
	goto again;
}

/*
 * Release `n` credits, and returns the new credit value.
 */
uint64_t __credit_release(uint64_t *credit, uint64_t n)
{
	uint64_t v0;

	__atomic_load(credit, &v0, __ATOMIC_SEQ_CST);
	if (v0 == __RAIL_UNLIMITED)
		return v0;
	return __atomic_add_fetch(credit, n, __ATOMIC_SEQ_CST);
}

int __rate_credit_acquire(struct ldms_rail_rate_credit_s *c, uint64_t n)
{
	int rc;
	time_t tv_sec;
	struct timespec ts;
	uint64_t v0, v1;

	if (c->rate == __RAIL_UNLIMITED)
		return 0;

 again:
	__atomic_load(&c->ts.tv_sec, &tv_sec, __ATOMIC_SEQ_CST);
	__atomic_load(&c->credit, &v0, __ATOMIC_SEQ_CST);
	rc = clock_gettime(CLOCK_REALTIME, &ts);
	if (rc)
		return errno;
	if (tv_sec < ts.tv_sec) {
		/* the second has changed, reset credit */
		if (0 == __atomic_compare_exchange(
				&c->credit, &v0, &c->rate,
				0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			goto again;
		/* update ts */
		if (0 == __atomic_compare_exchange(
				&c->ts.tv_sec, &tv_sec, &ts.tv_sec,
				0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			goto again;
		/* credit reset; proceed */
		v0 = c->rate;
	}

	if (v0 < n)
		return ENOBUFS;
	v1 = v0 - n;
	if (0 == __atomic_compare_exchange(
			&c->credit, &v0, &v1,
			0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		/* The other thread won the credit modification race; try again. */
		goto again;
	return 0;
}

void __rate_credit_release(struct ldms_rail_rate_credit_s *c, uint64_t n)
{
	int rc;
	struct timespec ts;
	if (c->rate == __RAIL_UNLIMITED)
		return;
	rc = clock_gettime(CLOCK_REALTIME, &ts);
	if (rc)
		return ;
	if (0 == __atomic_compare_exchange( &c->ts.tv_sec, &ts.tv_sec, &ts.tv_sec,
				0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		return; /* tv_sec changed, no need to return */
	__atomic_fetch_add(&c->credit, n, __ATOMIC_SEQ_CST);
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

static int __rail_send(ldms_t _r, char *msg_buf, size_t msg_len)
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
	rc = __credit_acquire(&rep->send_credit, msg_len);
	if (rc)
		goto out;
	rc = __rate_credit_acquire(&rep->rate_credit, msg_len);
	if (rc) {
		__credit_release(&rep->send_credit, msg_len);
		goto out;
	}
	rc = ldms_xprt_send(rep->ep, msg_buf, msg_len);
	if (rc) {
		/* release the acquired credit if send failed */
		__credit_release(&rep->send_credit, msg_len);
	}
 out:
	pthread_mutex_unlock(&r->mutex);
	return rc;
}

static size_t __rail_msg_max(ldms_t _r)
{
	ldms_rail_t r = (ldms_rail_t)_r;
	return r->max_msg;
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
} *ldms_rail_lookup_ctxt_t;

void __rail_lookup_cb(ldms_t x, enum ldms_lookup_status status,
			int more, ldms_set_t s, void *arg)
{
	ldms_rail_lookup_ctxt_t lc = arg;
	lc->app_cb((void*)lc->r, status, more, s, lc->cb_arg);
	if (!more)
		free(lc);
}

static int __rail_lookup(ldms_t _r, const char *name, enum ldms_lookup_flags flags,
	       ldms_lookup_cb_t cb, void *cb_arg)
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
	rep = &r->eps[r->lookup_rr++];
	r->lookup_rr %= r->n_eps;
	rc = ldms_xprt_lookup(rep->ep, name, flags, __rail_lookup_cb, lc);
	if (rc) {
		/* synchronous error */
		free(lc);
	}
 out:
	pthread_mutex_unlock(&r->mutex);
	return rc;
}

static void __rail_stats(ldms_t _r, ldms_xprt_stats_t stats)
{
	/* TODO IMPLEMENT ME */
	assert(0 == "Not Implemented");
}

static ldms_t __rail_get(ldms_t _r)
{
	assert(XTYPE_IS_RAIL(_r->xtype));
	ldms_rail_t r = (ldms_rail_t)_r;
	ref_get(&r->ref, "rail_ref");
	return (ldms_t)r;
}

static void __rail_put(ldms_t _r)
{
	assert(XTYPE_IS_RAIL(_r->xtype));
	ldms_rail_t r = (ldms_rail_t)_r;
	ref_put(&r->ref, "rail_ref");
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
	ldms_rail_t r = ldms_xprt_ctxt_get(x);
	ldms_rail_update_ctxt_t uc = arg;
	uc->app_cb((ldms_t)r, s, flags, uc->cb_arg);
	if (!(flags & LDMS_UPD_F_MORE)) {
		free(uc);
	}
}

static int __rail_update(ldms_t _r, struct ldms_set *set,
			 ldms_update_cb_t cb, void *arg)
{
	ldms_rail_t r = (void*)_r;
	ldms_rail_update_ctxt_t uc;
	int rc;

	uc = calloc(1, sizeof(*uc));
	if (!uc)
		return errno;
	uc->r = r;
	uc->app_cb = cb;
	uc->cb_arg = arg;
	rc = ldms_xprt_update(set, __rail_update_cb, uc);
	if (rc) {
		/* synchronously error, clean up the context */
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
	rep->rail->send_limit = be64toh(conn_msg->recv_limit);
	rep->rail->send_rate_limit = be64toh(conn_msg->rate_limit);

	rep->send_credit = be64toh(conn_msg->recv_limit);
	rep->rate_credit.credit = be64toh(conn_msg->rate_limit);
	rep->rate_credit.rate   = be64toh(conn_msg->rate_limit);
	rep->rate_credit.ts.tv_sec  = 0;
	rep->rate_credit.ts.tv_nsec = 0;
	rep->remote_is_rail = 1;
	return;
 unlimited:
	rep->send_credit = __RAIL_UNLIMITED;
	rep->rate_credit.credit = __RAIL_UNLIMITED;
	rep->rate_credit.rate   = __RAIL_UNLIMITED;
	rep->remote_is_rail = 0;
}

void __rail_process_send_credit(ldms_t x, struct ldms_request *req)
{
	uint32_t sc = ntohl(req->send_credit.send_credit);
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);
	struct ldms_xprt_event ev = {0};
	ev.credit.credit = __credit_release(&rep->send_credit, sc);
	ev.credit.ep_idx = rep->idx;
	ev.type = LDMS_XPRT_EVENT_SEND_CREDIT_DEPOSITED;
	rep->rail->event_cb((ldms_t)rep->rail, &ev, rep->rail->event_cb_arg);
}

int ldms_xprt_rail_send_credit_get(ldms_t _r, uint64_t *credits, int n)
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
		credits[i] = r->eps[i].send_credit;
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

ldms_t __ldms_xprt_to_rail(ldms_t x)
{
	if (XTYPE_IS_RAIL(x->xtype)) {
		return x;
	}

	struct ldms_rail_ep_s *ep = ldms_xprt_ctxt_get(x);
	return ((ep)?(ldms_t)ep->rail:NULL);
}
