/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2024 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2024 Open Grid Computing, Inc. All rights reserved.
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

#define _GNU_SOURCE

#include "ldms_qgroup.h"
#include "ldms_rail.h"

int __qgroup_rbt_cmp(void *tk, const void *k);

struct ldms_qgroup_s __qg; /* global qgroup */

/* implementation in ldms_rail.c */
int __quota_acquire(uint64_t *quota, uint64_t n);
uint64_t __quota_release(uint64_t *quota, uint64_t n);

#define __MIN(a, b) (((a)<(b))?(a):(b))
#define __MAX(a, b) (((a)>(b))?(a):(b))

#define QG_LOCK(qg) pthread_mutex_lock(&(qg)->mutex)
#define QG_UNLOCK(qg) pthread_mutex_unlock(&(qg)->mutex)
#define QG_COND_WAIT(qg) pthread_cond_wait(&(qg)->cond, &(qg)->mutex)

static inline uint64_t __qgroup_usec_adj(ldms_qgroup_t qg, uint64_t usec);

/* Implementation in ldms_rail.c */
int __rail_rep_send_raw(struct ldms_rail_ep_s *rep, void *data, int len);

static inline uint64_t __clock_getusec()
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec*1000000 + ts.tv_nsec/1000;
}

int __qgroup_rbt_cmp(void *tk, const void *k)
{
	int rc;
	const struct ldms_qgroup_member_s *tm, *m;
	tm = tk;
	m = k;
	rc = strcmp(tm->c_host, m->c_host);
	if (rc)
		return rc;
	rc = strcmp(tm->c_port, m->c_port);
	if (rc)
		return rc;
	return 0;
}

void __qgroup_free(void *ptr)
{
	ldms_qgroup_t qg = ptr;
	assert(rbt_empty(&qg->rbt));
	free(qg);
}

void __qgroup_member_free(void *ptr)
{
	ldms_qgroup_member_t m = ptr;
	if (m->c_auth_av_list)
		av_free(m->c_auth_av_list);
	free(ptr);
}

void __qgroup_init(ldms_qgroup_t qg, const char *ref_str,
				     ref_free_fn_t ref_free, void *ref_arg)
{
	qg->state = LDMS_QGROUP_STATE_STOPPED;
	bzero(&qg->cfg, sizeof(qg->cfg));
	rbt_init(&qg->rbt, __qgroup_rbt_cmp);
	pthread_mutex_init(&qg->mutex, NULL);
	ref_init(&qg->ref, ref_str, ref_free, ref_arg);
	STAILQ_INIT(&qg->eps_stq);
}

ldms_qgroup_t ldms_qgroup_new(ldms_qgroup_cfg_t cfg)
{
	ldms_qgroup_t qg = NULL;
	if (!cfg) {
		errno = EINVAL;
		goto out;
	}
	qg = malloc(sizeof(*qg));
	if (!qg)
		goto out;
	__qgroup_init(qg, "new", __qgroup_free, qg);
 out:
	return qg;
}

int ldms_qgroup_free(ldms_qgroup_t qg)
{
	int rc = 0;
	ref_get(&qg->ref, "ldms_qgroup_free");
	QG_LOCK(qg);
	if (qg->state != LDMS_QGROUP_STATE_STOPPED) {
		rc = EINVAL;
		goto out;
	}
	ref_put(&qg->ref, "new");
	rc = 0;
 out:
	QG_UNLOCK(qg);
	ref_put(&qg->ref, "ldms_qgroup_free");
	return rc;
}

int __qgroup_cfg_set(ldms_qgroup_t qg, ldms_qgroup_cfg_t cfg)
{
	int rc;
	QG_LOCK(qg);
	if (qg->state != LDMS_QGROUP_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	qg->cfg = *cfg;
	rc = 0;
 out:
	QG_UNLOCK(qg);
	return rc;
}

int ldms_qgroup_cfg_set(ldms_qgroup_cfg_t cfg)
{
	return __qgroup_cfg_set(&__qg, cfg);
}

int __qgroup_cfg_quota_set(ldms_qgroup_t qg, uint64_t quota)
{
	int rc;
	QG_LOCK(qg);
	if (qg->state != LDMS_QGROUP_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	qg->cfg.quota = quota;
	rc = 0;
 out:
	QG_UNLOCK(qg);
	return rc;
}

int ldms_qgroup_cfg_quota_set(uint64_t quota)
{
	return __qgroup_cfg_quota_set(&__qg, quota);
}

int __qgroup_cfg_ask_usec_set(ldms_qgroup_t qg, uint64_t usec)
{
	int rc;
	QG_LOCK(qg);
	if (qg->state != LDMS_QGROUP_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	qg->cfg.ask_usec = usec;
	rc = 0;
 out:
	QG_UNLOCK(qg);
	return rc;
}

int ldms_qgroup_cfg_ask_usec_set(uint64_t usec)
{
	return __qgroup_cfg_ask_usec_set(&__qg, usec);
}

int __qgroup_cfg_reset_usec_set(ldms_qgroup_t qg, uint64_t usec)
{
	int rc;
	QG_LOCK(qg);
	if (qg->state != LDMS_QGROUP_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	qg->cfg.reset_usec = usec;
	rc = 0;
 out:
	QG_UNLOCK(qg);
	return rc;
}

int ldms_qgroup_cfg_reset_usec_set(uint64_t usec)
{
	return __qgroup_cfg_reset_usec_set(&__qg, usec);
}

int __qgroup_cfg_ask_mark_set(ldms_qgroup_t qg, uint64_t ask_mark)
{
	int rc;
	QG_LOCK(qg);
	if (qg->state != LDMS_QGROUP_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	qg->cfg.ask_mark = ask_mark;
	rc = 0;
 out:
	QG_UNLOCK(qg);
	return rc;
}

int ldms_qgroup_cfg_ask_mark_set(uint64_t ask_mark)
{
	return __qgroup_cfg_ask_mark_set(&__qg, ask_mark);
}

int __qgroup_cfg_ask_amount_set(ldms_qgroup_t qg, uint64_t ask_amount)
{
	int rc;
	QG_LOCK(qg);
	if (qg->state != LDMS_QGROUP_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	qg->cfg.ask_amount = ask_amount;
	rc = 0;
 out:
	QG_UNLOCK(qg);
	return rc;
}

int ldms_qgroup_cfg_ask_amount_set(uint64_t ask_amount)
{
	return __qgroup_cfg_ask_amount_set(&__qg, ask_amount);
}

struct ldms_qgroup_cfg_s __qgroup_cfg_get(ldms_qgroup_t qg)
{
	return qg->cfg; /* copy out */
}

struct ldms_qgroup_cfg_s ldms_qgroup_cfg_get()
{
	return __qgroup_cfg_get(&__qg);
}

int __qgroup_member_add(ldms_qgroup_t qg, const char *xprt_name,
			   const char *host, const char *port,
			   const char *auth_name,
			   struct attr_value_list *auth_av_list)
{
	int rc, len;
	ldms_qgroup_member_t m = NULL;
	struct rbn *rbn;

	QG_LOCK(qg);
	if (qg->state != LDMS_QGROUP_STATE_STOPPED) {
		rc = EBUSY;
		goto err;
	}

	m = malloc(sizeof(*m));
	if (!m) {
		rc = ENOMEM;
		goto err;
	}

	pthread_mutex_init(&m->mutex, NULL);
	pthread_cond_init(&m->cond, NULL);
	rbn_init(&m->rbn, m);
	m->x = NULL;
	m->qg = qg;
	m->state = LDMS_QGROUP_MEMBER_STATE_DISCONNECTED;

	/* xprt_name */
	if (!xprt_name) {
		rc = EINVAL;
		goto err;
	}
	len = snprintf(m->c_xprt, sizeof(m->c_xprt), "%s", xprt_name);
	if (len >= sizeof(m->c_xprt)) {
		rc = ENAMETOOLONG;
		goto err;
	}

	/* host */
	if (!host) {
		rc = EINVAL;
		goto err;
	}
	len = snprintf(m->c_host, sizeof(m->c_host), "%s", host);
	if (len >= sizeof(m->c_host)) {
		rc = ENAMETOOLONG;
		goto err;
	}

	/* port */
	if (!port) {
		/* default to 411 */
		len = snprintf(m->c_port, sizeof(m->c_port), "411");
	} else {
		len = snprintf(m->c_port, sizeof(m->c_port), "%s", port);
		if (len >= sizeof(m->c_port)) {
			rc = ENAMETOOLONG;
			goto err;
		}
	}

	/* auth_name */
	if (!auth_name) {
		/* default to 'none' */
		len = snprintf(m->c_auth, sizeof(m->c_auth), "none");
	} else {
		len = snprintf(m->c_auth, sizeof(m->c_auth), "%s", auth_name);
		if (len >= sizeof(m->c_auth)) {
			rc = ENAMETOOLONG;
			goto err;
		}
	}

	/* auth_av_list */
	if (auth_av_list) {
		m->c_auth_av_list = av_copy(auth_av_list);
		if (!m->c_auth_av_list) {
			rc = errno;
			goto err;
		}
	} else {
		m->c_auth_av_list = NULL;
	}

	/* add */
	rbn = rbt_find(&qg->rbt, m);
	if (rbn) {
		rc = EEXIST;
		goto err;
	}
	rbt_ins(&qg->rbt, &m->rbn);
	ref_init(&m->ref, "new", __qgroup_member_free, m);
	ref_get(&m->ref, "qg->rbt");
	QG_UNLOCK(qg);
	return 0;

 err:
	QG_UNLOCK(qg);
	if (m)
		free(m);
	return rc;
}

int ldms_qgroup_member_add(const char *xprt_name,
			   const char *host, const char *port,
			   const char *auth_name,
			   struct attr_value_list *auth_av_list)
{
	return __qgroup_member_add(&__qg, xprt_name, host, port,
				   auth_name, auth_av_list);
}

int __qgroup_member_del(ldms_qgroup_t qg, const char *host, const char *port)
{
	int rc = 0;
	struct ldms_qgroup_member_s _m; /* for rbt_find */
	ldms_qgroup_member_t m = NULL;
	struct rbn *rbn = NULL;

	QG_LOCK(qg);
	if (qg->state != LDMS_QGROUP_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	snprintf(_m.c_host, sizeof(_m.c_host), "%s", host);
	snprintf(_m.c_port, sizeof(_m.c_port), "%s", port?port:"411");
	rbn_init(&_m.rbn, &_m);
	rbn = rbt_find(&qg->rbt, &_m);
	if (!rbn) {
		rc = ENOENT;
		goto out;
	}
	/* found, remove it */
	rbt_del(&qg->rbt, rbn);
	m = container_of(rbn, struct ldms_qgroup_member_s, rbn);
	ref_put(&m->ref, "qg->rbt");
 out:
	QG_UNLOCK(qg);
	if (m)
		ref_put(&m->ref, "new");
	return rc;
}

int ldms_qgroup_member_del(const char *host, const char *port)
{
	return __qgroup_member_del(&__qg, host, port);
}

static pthread_t __qgroup_thread;
static pthread_mutex_t __qgroup_mutex = PTHREAD_MUTEX_INITIALIZER;
static ovis_scheduler_t __qgroup_sched;

static void *__qgroup_proc(void *arg)
{
	ovis_scheduler_loop(__qgroup_sched, 0);
	return NULL;
}

static int __qgroup_once()
{
	int rc = 0;
	pthread_mutex_lock(&__qgroup_mutex);
	if (__qgroup_thread)
		goto out;
	__qgroup_sched = ovis_scheduler_new();
	if (!__qgroup_sched) {
		rc = errno;
		goto out;
	}
	rc = pthread_create(&__qgroup_thread, NULL, __qgroup_proc, NULL);
	if (rc) {
		__qgroup_thread = 0;
		ovis_scheduler_free(__qgroup_sched);
	}
	pthread_setname_np(__qgroup_thread, "ldms_qgroup");
 out:
	pthread_mutex_unlock(&__qgroup_mutex);
	return rc;
}

/* qg is locked */
static void __qgroup_reset(ldms_qgroup_t qg)
{
	/* Reset quota and flush the pending quotas */
	struct __qgroup_rep_s *ent, *next_ent;
	uint64_t q;
	int p;
	__atomic_store_n(&qg->quota, 0, __ATOMIC_SEQ_CST);
	q = qg->cfg.quota;
	ent = STAILQ_FIRST(&qg->eps_stq);
	while (ent) {
		next_ent = STAILQ_NEXT(ent, entry);
		p = ent->rep->pending_ret_quota;
		assert(p > 0);
		if (p > q)
			goto next;
		STAILQ_REMOVE(&qg->eps_stq, ent, __qgroup_rep_s, entry);
		__atomic_store_n(&ent->rep->in_eps_stq, 0, __ATOMIC_SEQ_CST);
		__rail_ep_quota_return(ent->rep, p, 0);
		q -= p;
		__atomic_add_fetch(&ent->rep->pending_ret_quota, -p, __ATOMIC_SEQ_CST);
		ref_put(&ent->rep->rail->ref, "qgroup_rep");
		free(ent);
	next:
		ent = next_ent;
	}
	__atomic_store_n(&qg->quota, q, __ATOMIC_SEQ_CST);
}

void __qgroup_on_ask_event(ldms_qgroup_t qg, struct ldms_rail_ep_s *rep,
			   ldms_xprt_event_t e)
{
	uint64_t q;
	struct ldms_request *req = (void*)e->data;
	uint64_t ask;
	uint64_t usec, usec_adj, req_usec;
	uint64_t max_donate;
	uint64_t donate;
	struct ldms_request dreq;
	int rc, len;

	ask = be64toh(req->qgroup_ask.q);

	usec = __clock_getusec();
	usec_adj = __qgroup_usec_adj(qg, usec);
	req_usec = be64toh(req->qgroup_ask.usec);

	if (req_usec != usec_adj) /* Ignore 'ask' of wrong window */
		return;

	q = __atomic_load_n(&qg->quota, __ATOMIC_SEQ_CST);

	if (q <= qg->cfg.ask_mark)
		return; /* we don't have anything to donate */

	/* NOTE: Don't share too much that we have to ask others for quotas to
	 * fill to the mark. */
	max_donate = q - qg->cfg.ask_mark;
	donate = __MIN(max_donate, ask);

	rc = __quota_acquire(&qg->quota, donate);
	if (rc)
		return;

	len = sizeof(dreq.hdr) + sizeof(dreq.qgroup_donate);
	dreq.hdr.len = htonl(len);
	dreq.hdr.cmd = htonl(LDMS_CMD_QGROUP_DONATE);
	dreq.hdr.xid = req->hdr.xid;
	dreq.qgroup_donate.q = htobe64(donate);
	dreq.qgroup_donate.usec = htobe64(usec_adj);
	__rail_rep_send_raw(rep, &dreq, len);
}

void __qgroup_on_donate_event(ldms_qgroup_t qg, struct ldms_rail_ep_s *rep,
			      ldms_xprt_event_t e)
{
	uint64_t q, new_q;
	struct ldms_request *req = (void*)e->data;
	uint64_t donate;
	uint64_t fill;
	uint64_t usec, usec_adj, req_usec;
	int len, succeed;

	req_usec = be64toh(req->qgroup_donate.usec);

 again:
	usec = __clock_getusec();
	usec_adj = __qgroup_usec_adj(qg, usec);
	if (usec_adj != req_usec)
		return;
	donate = be64toh(req->qgroup_donate.q);
	q = __atomic_load_n(&qg->quota, __ATOMIC_SEQ_CST);
	if (q >= qg->cfg.quota)
		goto release;
	fill = qg->cfg.quota - q;
	if (fill > donate) {
		fill = donate;
	}
	donate -= fill;
	new_q = q + fill;
	succeed = __atomic_compare_exchange(&qg->quota, &q, &new_q, 0,
					__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	if (!succeed)
		goto again; /* other thread won the race */

 release:
	if (donate) {
		/* return the unused donation */
		len = sizeof(req->hdr) + sizeof(req->qgroup_donate);
		req->hdr.cmd = htobe32(LDMS_CMD_QGROUP_DONATE_BACK);
		req->qgroup_donate.q = htobe64(donate);
		req->qgroup_donate.usec = htobe64(usec_adj);
		__rail_rep_send_raw(rep, req, len);
	}
}

void __qgroup_on_donate_back_event(ldms_qgroup_t qg, struct ldms_rail_ep_s *rep,
				   ldms_xprt_event_t e)
{
	struct ldms_request *req = (void*)e->data;
	uint64_t donate;
	uint64_t usec, usec_adj, req_usec;

	req_usec = be64toh(req->qgroup_donate.usec);
	usec = __clock_getusec();
	usec_adj = __qgroup_usec_adj(qg, usec);
	if (usec_adj != req_usec)
		return;

	donate = be64toh(req->qgroup_donate.q);
	__atomic_add_fetch(&qg->quota, donate, __ATOMIC_SEQ_CST);
}

static void __qgroup_member_do_ask(ldms_qgroup_member_t m);

static void __qgroup_member_xprt_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	assert(LDMS_IS_RAIL(x));

	ldms_qgroup_member_t m = cb_arg;
	struct ldms_rail_ep_s *rep = &LDMS_RAIL(x)->eps[0];

	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		pthread_mutex_lock(&m->mutex);
		assert(m->state == LDMS_QGROUP_MEMBER_STATE_CONNECTING);
		m->state = LDMS_QGROUP_MEMBER_STATE_CONNECTED;
		pthread_mutex_unlock(&m->mutex);
		__qgroup_member_do_ask(m);
		break;
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_ERROR:
	case LDMS_XPRT_EVENT_DISCONNECTED:
		pthread_mutex_lock(&m->mutex);
		m->state = LDMS_QGROUP_MEMBER_STATE_DISCONNECTED;
		pthread_cond_signal(&m->cond);
		pthread_mutex_unlock(&m->mutex);
		ref_put(&m->ref, "connection");
		break;
	case LDMS_XPRT_EVENT_RECV:
	case LDMS_XPRT_EVENT_SET_DELETE:
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
	case LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED:
		/* ignored ; no-op */
		break;
	case LDMS_XPRT_EVENT_QGROUP_ASK:
		__qgroup_on_ask_event(&__qg, rep, e);
		break;
	case LDMS_XPRT_EVENT_QGROUP_DONATE:
		__qgroup_on_donate_event(&__qg, rep, e);
		break;
	case LDMS_XPRT_EVENT_QGROUP_DONATE_BACK:
		__qgroup_on_donate_back_event(&__qg, rep, e);
		break;
	default:
		assert(0 == "Unexpexted event type");
	}
}

void __qgroup_req_recv(ldms_t x, int cmd, struct ldms_request *req)
{
	assert(LDMS_IS_LEGACY(x));

	struct ldms_xprt_event event;
	struct ldms_rail_ep_s *rep = ldms_xprt_ctxt_get(x);

	event.data = (void*)req;
	event.data_len = ntohl(req->hdr.len);

	switch (cmd) {
	case LDMS_CMD_QGROUP_ASK:
		event.type = LDMS_XPRT_EVENT_QGROUP_ASK;
		__qgroup_on_ask_event(&__qg, rep, &event);
		break;
	case LDMS_CMD_QGROUP_DONATE:
		event.type = LDMS_XPRT_EVENT_QGROUP_DONATE;
		__qgroup_on_donate_event(&__qg, rep, &event);
		break;
	case LDMS_CMD_QGROUP_DONATE_BACK:
		event.type = LDMS_XPRT_EVENT_QGROUP_DONATE_BACK;
		__qgroup_on_donate_back_event(&__qg, rep, &event);
		break;
	default:
		assert(0 == "Unexpected event");
	}
}

static void __qgroup_member_do_connect(ldms_qgroup_member_t m)
{
	int rc;
	if (m->x) {
		ldms_xprt_put((ldms_t)m->x, "init");
		m->x = NULL;
	}
	m->x = (ldms_rail_t)ldms_xprt_new_with_auth(m->c_xprt, m->c_auth, m->c_auth_av_list);
	if (!m->x)
		return;
	ref_get(&m->ref, "do_connect");
	pthread_mutex_lock(&m->mutex);
	m->state = LDMS_QGROUP_MEMBER_STATE_CONNECTING;
	rc = ldms_xprt_connect_by_name((ldms_t)m->x, m->c_host, m->c_port,
						__qgroup_member_xprt_cb, m);
	if (rc) {
		ldms_xprt_put((ldms_t)m->x, "init");
		m->x = NULL;
		m->state = LDMS_QGROUP_MEMBER_STATE_DISCONNECTED;
	} else {
		ref_get(&m->ref, "connection");
	}
	pthread_mutex_unlock(&m->mutex);
	ref_put(&m->ref, "do_connect");
}

/* Adjust usec to multiples os reset interval (starting usec of each window) */
static inline uint64_t __qgroup_usec_adj(ldms_qgroup_t qg, uint64_t usec)
{
	return (usec/qg->cfg.reset_usec)*qg->cfg.reset_usec;
}

static void __qgroup_member_do_ask(ldms_qgroup_member_t m)
{
	uint64_t q;
	struct ldms_request req;
	uint64_t usec, usec_adj;
	int len = sizeof(req.hdr) + sizeof(req.qgroup_ask);
	ldms_qgroup_t qg = m->qg;

	q = __atomic_load_n(&qg->quota, __ATOMIC_SEQ_CST);
	if (q >= qg->cfg.ask_mark)
		return; /* no need to do anything */

	req.hdr.cmd = htonl(LDMS_CMD_QGROUP_ASK);
	req.hdr.len = htonl(len);

	usec = __clock_getusec();
	usec_adj = __qgroup_usec_adj(m->qg, usec);
	req.hdr.xid = htobe64(usec);
	req.qgroup_ask.q = htobe64(m->qg->cfg.ask_amount);
	req.qgroup_ask.usec = htobe64(usec_adj);

	__rail_rep_send_raw(&m->x->eps[0], &req, len);
}

static void __qgroup_member_routine(ldms_qgroup_member_t m)
{
	switch (m->state) {
	case LDMS_QGROUP_MEMBER_STATE_DISCONNECTED:
		__qgroup_member_do_connect(m);
		break;
	case LDMS_QGROUP_MEMBER_STATE_CONNECTING:
		/* no-op */
		break;
	case LDMS_QGROUP_MEMBER_STATE_CONNECTED:
		__qgroup_member_do_ask(m);
		break;
	default:
		assert(0 == "Unexpected state");
		break;
	}
}

static void __qgroup_ask_routine(ovis_event_t ev)
{
	ldms_qgroup_t qg = ev->param.ctxt;
	struct rbn *rbn;
	ldms_qgroup_member_t m;

	/* race with ldms_qgroup_stop() */
	QG_LOCK(qg);
	switch (qg->state) {
	case LDMS_QGROUP_STATE_STARTED:
		qg->state = LDMS_QGROUP_STATE_BUSY;
		break;
	case LDMS_QGROUP_STATE_STOPPED:
	case LDMS_QGROUP_STATE_STOPPING:
		QG_UNLOCK(qg);
		return;
	case LDMS_QGROUP_STATE_BUSY:
		assert(0 == "This should not happen ...");
		break;
	default:
		assert(0 == "Unexpected state");
	}
	QG_UNLOCK(qg);

	/* no need to lock qg as rbn cannot be added or removed while qg state
	 * is not in STOPPED. */
	RBT_FOREACH(rbn, &qg->rbt) {
		m = container_of(rbn, struct ldms_qgroup_member_s, rbn);
		__qgroup_member_routine(m);
	}

	QG_LOCK(qg);
	assert( qg->state == LDMS_QGROUP_STATE_BUSY );
	qg->state = LDMS_QGROUP_STATE_STARTED;
	pthread_cond_broadcast(&qg->cond);
	QG_UNLOCK(qg);
}

static void __qgroup_reset_routine(ovis_event_t ev)
{
	ldms_qgroup_t qg = ev->param.ctxt;
	QG_LOCK(qg);
	__qgroup_reset(qg);
	QG_UNLOCK(qg);
}

int __qgroup_start(ldms_qgroup_t qg)
{
	int rc;
	QG_LOCK(qg);
	if (qg->state != LDMS_QGROUP_STATE_STOPPED) {
		rc = EBUSY;
		goto out;
	}
	rc = __qgroup_once();
	if (rc)
		goto out;

	/* sanity check */
	if (!qg->cfg.reset_usec) {
		rc = EINVAL;
		goto out;
	}
	if (!qg->cfg.ask_usec) {
		rc = EINVAL;
		goto out;
	}
	if (!qg->cfg.ask_amount) {
		rc = EINVAL;
		goto out;
	}
	if (!qg->cfg.ask_mark) {
		rc = EINVAL;
		goto out;
	}
	if (!qg->cfg.quota) {
		rc = EINVAL;
		goto out;
	}

	__qgroup_reset(qg);

	/* ask */
	OVIS_EVENT_INIT(&qg->ask_oev);
	qg->ask_oev.param.type = OVIS_EVENT_PERIODIC;
	qg->ask_oev.param.periodic.period_us = qg->cfg.ask_usec;
	qg->ask_oev.param.periodic.phase_us = 0;
	qg->ask_oev.param.cb_fn = __qgroup_ask_routine;
	qg->ask_oev.param.ctxt = qg;

	/* reset */
	OVIS_EVENT_INIT(&qg->reset_oev);
	OVIS_EVENT_INIT(&qg->reset_oev);
	qg->reset_oev.param.type = OVIS_EVENT_PERIODIC;
	qg->reset_oev.param.periodic.period_us = qg->cfg.reset_usec;
	qg->reset_oev.param.periodic.phase_us = 0;
	qg->reset_oev.param.cb_fn = __qgroup_reset_routine;
	qg->reset_oev.param.ctxt = qg;

	rc = ovis_scheduler_event_add(__qgroup_sched, &qg->ask_oev);
	if (rc)
		goto out;
	rc = ovis_scheduler_event_add(__qgroup_sched, &qg->reset_oev);
	if (rc) {
		ovis_scheduler_event_del(__qgroup_sched, &qg->ask_oev);
		goto out;
	}

	qg->state = LDMS_QGROUP_STATE_STARTED;
 out:
	QG_UNLOCK(qg);
	return rc;
}

int ldms_qgroup_start()
{
	return __qgroup_start(&__qg);
}

void __qgroup_member_disconnect(ldms_qgroup_member_t m)
{
	pthread_mutex_lock(&m->mutex);
	switch (m->state) {
	case LDMS_QGROUP_MEMBER_STATE_CONNECTED:
	case LDMS_QGROUP_MEMBER_STATE_CONNECTING:
		ldms_xprt_close((ldms_t)m->x);
		break;
	case LDMS_QGROUP_MEMBER_STATE_DISCONNECTED:
		break;
	default:
		assert(0 == "Unexpected state");
	}
	pthread_mutex_unlock(&m->mutex);
}

void __qgroup_member_join_disconnected(ldms_qgroup_member_t m)
{
	pthread_mutex_lock(&m->mutex);
	while (m->state != LDMS_QGROUP_MEMBER_STATE_DISCONNECTED) {
		pthread_cond_wait(&m->cond, &m->mutex);
	}
	pthread_mutex_unlock(&m->mutex);
}

int __qgroup_stop(ldms_qgroup_t qg)
{
	int rc;
	struct rbn *rbn;
	ldms_qgroup_member_t m;
	QG_LOCK(qg);
 again:
	switch (qg->state) {
	case LDMS_QGROUP_STATE_STOPPED:
	case LDMS_QGROUP_STATE_STOPPING:
		rc = EINVAL;
		goto out;
	case LDMS_QGROUP_STATE_BUSY:
		QG_COND_WAIT(qg);
		goto again;
	case LDMS_QGROUP_STATE_STARTED:
		qg->state = LDMS_QGROUP_STATE_STOPPING;
		break;
	default:
		assert(0 == "Unexpected event");
		goto out;
	}
	ovis_scheduler_event_del(__qgroup_sched, &qg->reset_oev);
	ovis_scheduler_event_del(__qgroup_sched, &qg->ask_oev);
	QG_UNLOCK(qg);

	/* disconnect */
	RBT_FOREACH(rbn, &qg->rbt) {
		m = container_of(rbn, struct ldms_qgroup_member_s, rbn);
		__qgroup_member_disconnect(m);
	}

	/* join */
	RBT_FOREACH(rbn, &qg->rbt) {
		m = container_of(rbn, struct ldms_qgroup_member_s, rbn);
		__qgroup_member_join_disconnected(m);
	}

	QG_LOCK(qg);
	qg->state = LDMS_QGROUP_STATE_STOPPED;
	rc = 0;
 out:
	QG_UNLOCK(qg);
	return rc;
}

int ldms_qgroup_stop()
{
	return __qgroup_stop(&__qg);
}

int __qgroup_quota_acquire(ldms_qgroup_t qg, uint64_t q)
{
	if (qg->state == LDMS_QGROUP_STATE_STOPPED)
		return 0;
	return __quota_acquire(&qg->quota, q);
}

int ldms_qgroup_quota_acquire(uint64_t q)
{
	return __qgroup_quota_acquire(&__qg, q);
}

int __qgroup_quota_release(ldms_qgroup_t qg, uint64_t q)
{
	return __quota_release(&qg->quota, q);
}

int ldms_qgroup_quota_release(uint64_t q)
{
	return __qgroup_quota_release(&__qg, q);
}

uint64_t ldms_qgroup_quota_probe()
{
	return __qg.quota;
}

int __qgroup_add_rep(ldms_qgroup_t qg, struct ldms_rail_ep_s *rep)
{
	int rc = 0;
	struct __qgroup_rep_s *ent;
	QG_LOCK(qg);
	ent = malloc(sizeof(*ent));
	if (!ent) {
		rc = errno;
		goto out;
	}
	ref_get(&rep->rail->ref, "qgroup_rep");
	ent->rep = rep;
	STAILQ_INSERT_TAIL(&qg->eps_stq, ent, entry);
 out:
	QG_UNLOCK(qg);
	return rc;
}

int ldms_qgroup_add_rep(struct ldms_rail_ep_s *rep)
{
	return __qgroup_add_rep(&__qg, rep);
}

ldms_qgroup_info_t __qgroup_info_get(ldms_qgroup_t qg)
{
	ldms_qgroup_info_t qinfo;
	ldms_qgroup_member_info_t minfo;
	ldms_qgroup_member_t m;
	struct rbn *rbn;

	QG_LOCK(qg);
	qinfo = calloc(1, sizeof(*qinfo));
	if (!qinfo)
		goto out;
	STAILQ_INIT(&qinfo->member_stq);
	qinfo->quota = qg->quota;
	qinfo->state = qg->state;
	qinfo->cfg = qg->cfg;

	RBT_FOREACH(rbn, &qg->rbt) {
		m = container_of(rbn, struct ldms_qgroup_member_s, rbn);
		minfo = calloc(1, sizeof(*minfo));
		if (!minfo)
			goto err;
		if (m->c_auth_av_list) {
			minfo->c_auth_av_list = av_copy(m->c_auth_av_list);
			if (!minfo->c_auth_av_list) {
				free(minfo);
				goto err;
			}
		}
		minfo->state = m->state;
		memcpy(minfo->c_host, m->c_host, sizeof(minfo->c_host));
		memcpy(minfo->c_port, m->c_port, sizeof(minfo->c_port));
		memcpy(minfo->c_xprt, m->c_xprt, sizeof(minfo->c_xprt));
		memcpy(minfo->c_auth, m->c_auth, sizeof(minfo->c_auth));
		STAILQ_INSERT_TAIL(&qinfo->member_stq, minfo, entry);
	}

	goto out;

 err:
	ldms_qgroup_info_free(qinfo);
	qinfo = NULL;

 out:
	QG_UNLOCK(qg);
	return qinfo;
}

ldms_qgroup_info_t ldms_qgroup_info_get()
{
	return __qgroup_info_get(&__qg);
}

void ldms_qgroup_info_free(ldms_qgroup_info_t qinfo)
{
	ldms_qgroup_member_info_t minfo;
	while ((minfo = STAILQ_FIRST(&qinfo->member_stq))) {
		STAILQ_REMOVE_HEAD(&qinfo->member_stq, entry);
		if (minfo->c_auth_av_list)
			av_free(minfo->c_auth_av_list);
		free(minfo);
	}
	free(qinfo);
}

static const char *__qgroup_state_str[] = {
	[LDMS_QGROUP_STATE_STOPPED]  = "STOPPED",
	[LDMS_QGROUP_STATE_STOPPING] = "STOPPING",
	[LDMS_QGROUP_STATE_STARTED]  = "STARTED",
	[LDMS_QGROUP_STATE_BUSY]     = "BUSY",
};

const char *ldms_qgroup_state_str(ldms_qgroup_state_t state)
{
	if (0 <= state && state < LDMS_QGROUP_STATE_LAST)
		return __qgroup_state_str[state];
	return "__UNKNOWN_STATE__";
}

static const char *__qgroup_member_state_str[] = {
	[LDMS_QGROUP_MEMBER_STATE_DISCONNECTED] = "DISCONNECTED",
	[LDMS_QGROUP_MEMBER_STATE_CONNECTING]   = "CONNECTING",
	[LDMS_QGROUP_MEMBER_STATE_CONNECTED]    = "CONNECTED",
};

const char *ldms_qgroup_member_state_str(ldms_qgroup_member_state_t state)
{
	if (0 <= state && state < LDMS_QGROUP_MEMBER_STATE_LAST)
		return __qgroup_member_state_str[state];
	return "__UNKNOWN_STATE__";
}

void __qgroup_static_free(void *ptr)
{
	assert(0 == "This should not be called!");
}

__attribute__((constructor))
void __qg_init()
{
	/* Initialize the global '__qg' */
	ldms_qgroup_t qg = &__qg;
	__qgroup_init(qg, "new", __qgroup_static_free, qg);
}
