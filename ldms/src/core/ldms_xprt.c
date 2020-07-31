/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2019 Open Grid Computing, Inc. All rights reserved.
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
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <pthread.h>
#include <dlfcn.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <netdb.h>
#include <regex.h>
#include <pwd.h>
#include <unistd.h>
#include <stdarg.h>
#include <mmalloc/mmalloc.h>
#include <ovis_json/ovis_json.h>
#include "ovis_util/os_util.h"
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_private.h"

/* Global Transport Statistics */
static uint64_t xprt_connect_count;
static uint64_t xprt_connect_request_count;
static uint64_t xprt_disconnect_count;
static uint64_t xprt_reject_count;
static uint64_t xprt_auth_fail_count;
static struct timespec xprt_start;

void ldms_xprt_rate_data(struct ldms_xprt_rate_data *data)
{
	struct timespec now;
	double dur_s;
	(void)clock_gettime(CLOCK_REALTIME, &now);
	dur_s = ldms_timespec_diff_s(&xprt_start, &now);
	data->connect_rate_s = (double)xprt_connect_count / dur_s;
	data->connect_request_rate_s = (double)xprt_connect_request_count / dur_s;
	data->disconnect_rate_s = (double)xprt_disconnect_count / dur_s;
	data->reject_rate_s = (double)xprt_reject_count / dur_s;
	data->auth_fail_rate_s = (double)xprt_auth_fail_count / dur_s;
}

#define LDMS_XPRT_AUTH_GUARD(x) (((x)->auth_flag != LDMS_XPRT_AUTH_DISABLE) && \
				 ((x)->auth_flag != LDMS_XPRT_AUTH_APPROVED))

static struct ldms_rbuf_desc *ldms_lookup_rbd(struct ldms_xprt *, struct ldms_set *);
static struct ldms_rbuf_desc *__rbd_by_set_id(struct ldms_xprt *x, uint64_t id);

/**
 * zap callback function.
 */
static void ldms_zap_cb(zap_ep_t zep, zap_event_t ev);

/**
 * zap callback function for endpoints that automatically created from accepting
 * connection requests.
 */
static void ldms_zap_auto_cb(zap_ep_t zep, zap_event_t ev);

static void default_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	fflush(stdout);
}

#if 0
#define TF() default_log("%s:%d\n", __FUNCTION__, __LINE__)
#else
#define TF()
#endif

pthread_mutex_t xprt_list_lock;

#define LDMS_ZAP_XPRT_SOCK 0;
#define LDMS_ZAP_XPRT_RDMA 1;
#define LDMS_ZAP_XPRT_UGNI 2;
pthread_mutex_t ldms_zap_list_lock;
static zap_t ldms_zap_list[3] = {0};

ldms_t ldms_xprt_get(ldms_t x)
{
	if (x) {
		assert(x->ref_count > 0);
		__sync_add_and_fetch(&x->ref_count, 1);
	}
	return x;
}

LIST_HEAD(xprt_list, ldms_xprt) xprt_list;
ldms_t ldms_xprt_first()
{
	struct ldms_xprt *x = NULL;
	pthread_mutex_lock(&xprt_list_lock);
	x = LIST_FIRST(&xprt_list);
	if (!x)
		goto out;
	ldms_xprt_get(x);	/* next reference */
	x = ldms_xprt_get(x);	/* caller reference */
 out:
	pthread_mutex_unlock(&xprt_list_lock);
	return x;
}

ldms_t ldms_xprt_next(ldms_t x)
{
	ldms_t prev_x = x;
	pthread_mutex_lock(&xprt_list_lock);
	x = LIST_NEXT(x, xprt_link);
	if (!x)
		goto out;
	ldms_xprt_get(x);	/* next reference */
	x = ldms_xprt_get(x);	/* caller reference */
 out:
	pthread_mutex_unlock(&xprt_list_lock);
	ldms_xprt_put(prev_x);	/* next reference */
	return x;
}

ldms_t ldms_xprt_by_remote_sin(struct sockaddr_in *sin)
{
	struct sockaddr_storage ss_local, ss_remote;
	socklen_t socklen;

	ldms_t l, next_l;
	l = ldms_xprt_first();
	while (l) {
		zap_err_t zerr = zap_get_name(l->zap_ep,
					      (struct sockaddr *)&ss_local,
					      (struct sockaddr *)&ss_remote,
					      &socklen);
		if (zerr)
			goto next;
		struct sockaddr_in *s = (struct sockaddr_in *)&ss_remote;
		if (s->sin_addr.s_addr == sin->sin_addr.s_addr
		    && ((sin->sin_port == 0xffff) ||
			(s->sin_port == sin->sin_port)))
			return l;
next:
		next_l = ldms_xprt_next(l);
		ldms_xprt_put(l);
		l = next_l;
	}
	return 0;
}

/* Caller must call with the ldms xprt lock held */
struct ldms_context *__ldms_alloc_ctxt(struct ldms_xprt *x, size_t sz,
		ldms_context_type_t type, ...)
{
	va_list ap;
	struct ldms_context *ctxt;
	va_start(ap, type);
	ctxt = calloc(1, sz);
	if (!ctxt) {
		x->log("%s(): Out of memory\n", __func__);
		return ctxt;
	}
	ctxt->x = ldms_xprt_get(x);
	(void)clock_gettime(CLOCK_REALTIME, &ctxt->start);
#ifdef CTXT_DEBUG
	x->log("%s(): x %p: alloc ctxt %p: type %d\n", __func__, x, ctxt, type);
#endif /* CTXT_DEBUG */
	ctxt->type = type;
	TAILQ_INSERT_TAIL(&x->ctxt_list, ctxt, link);
	switch (type) {
	case LDMS_CONTEXT_LOOKUP_REQ:
		ctxt->lu_req.cb = va_arg(ap, ldms_lookup_cb_t);
		ctxt->lu_req.cb_arg = va_arg(ap, void *);
		ctxt->lu_req.path = va_arg(ap, char *);
		ctxt->lu_req.flags = va_arg(ap, enum ldms_lookup_flags);
		break;
	case LDMS_CONTEXT_LOOKUP_READ:
		ctxt->lu_read.s = va_arg(ap, ldms_set_t);
		ref_get(&ctxt->lu_read.s->ref, __func__);
		ctxt->lu_read.cb = va_arg(ap, ldms_lookup_cb_t);
		ctxt->lu_read.cb_arg = va_arg(ap, void *);
		ctxt->lu_read.more = va_arg(ap, int);
		ctxt->lu_read.flags = va_arg(ap, enum ldms_lookup_flags);
		break;
	case LDMS_CONTEXT_UPDATE:
	case LDMS_CONTEXT_UPDATE_META:
		ctxt->update.s = va_arg(ap, ldms_set_t);
		ref_get(&ctxt->update.s->ref, __func__);
		ctxt->update.cb = va_arg(ap, ldms_update_cb_t);
		ctxt->update.cb_arg = va_arg(ap, void *);
		ctxt->update.idx_from = va_arg(ap, int);
		ctxt->update.idx_to = va_arg(ap, int);
		break;
	case LDMS_CONTEXT_REQ_NOTIFY:
		ctxt->req_notify.s = va_arg(ap, ldms_set_t);
		ref_get(&ctxt->req_notify.s->ref, __func__);
		ctxt->req_notify.cb = va_arg(ap, ldms_notify_cb_t);
		ctxt->req_notify.cb_arg = va_arg(ap, void *);
		break;
	case LDMS_CONTEXT_DIR:
		ctxt->dir.cb = va_arg(ap, ldms_dir_cb_t);
		ctxt->dir.cb_arg = va_arg(ap, void *);
		break;
	case LDMS_CONTEXT_SET_DELETE:
		ctxt->set_delete.s = va_arg(ap, ldms_set_t);
		ref_get(&ctxt->set_delete.s->ref, __func__);
		ctxt->set_delete.cb = va_arg(ap, ldms_set_delete_cb_t);
		ctxt->set_delete.cb_arg = va_arg(ap, void *);
		break;
	case LDMS_CONTEXT_PUSH:
	case LDMS_CONTEXT_DIR_CANCEL:
	case LDMS_CONTEXT_SEND:
		break;
	}
	va_end(ap);
	return ctxt;
}

/* Caller must call with the ldms xprt lock held */
void __ldms_free_ctxt(struct ldms_xprt *x, struct ldms_context *ctxt)
{
	int64_t dur_us;
	struct timespec end;
	ldms_stats_entry_t e = NULL;

	(void)clock_gettime(CLOCK_REALTIME, &end);
	dur_us = ldms_timespec_diff_us(&ctxt->start, &end);

	TAILQ_REMOVE(&x->ctxt_list, ctxt, link);
	switch (ctxt->type) {
	case LDMS_CONTEXT_LOOKUP_REQ:
		free(ctxt->lu_req.path);
		break;
	case LDMS_CONTEXT_LOOKUP_READ:
		e = &x->stats.ops[LDMS_XPRT_OP_LOOKUP];
		ref_put(&ctxt->lu_read.s->ref, "__ldms_alloc_ctxt");
		break;
	case LDMS_CONTEXT_UPDATE:
	case LDMS_CONTEXT_UPDATE_META:
		e = &x->stats.ops[LDMS_XPRT_OP_UPDATE];
		ref_put(&ctxt->update.s->ref, "__ldms_alloc_ctxt");
		break;
	case LDMS_CONTEXT_REQ_NOTIFY:
		ref_put(&ctxt->req_notify.s->ref, "__ldms_alloc_ctxt");
		break;
	case LDMS_CONTEXT_SET_DELETE:
		e = &x->stats.ops[LDMS_XPRT_OP_SET_DELETE];
		ref_put(&ctxt->set_delete.s->ref, "__ldms_alloc_ctxt");
		break;
	case LDMS_CONTEXT_DIR:
		e = &x->stats.ops[LDMS_XPRT_OP_DIR];
		break;
	case LDMS_CONTEXT_SEND:
		e = &x->stats.ops[LDMS_XPRT_OP_SEND];
		break;
	case LDMS_CONTEXT_PUSH:
	case LDMS_CONTEXT_DIR_CANCEL:
		break;
	}

	if (e) {
		if (e->min_us > dur_us)
			e->min_us = dur_us;
		if (e->max_us < dur_us)
			e->max_us = dur_us;
		e->total_us += dur_us;
		e->mean_us = (e->count * e->mean_us) + dur_us;
		e->count += 1;
		e->mean_us /= e->count;
	}
	ldms_xprt_put(ctxt->x);
	free(ctxt);

}

static void send_dir_update(struct ldms_xprt *x,
			    enum ldms_dir_type t,
			    char *json, size_t json_sz)
{
	size_t hdr_len;
	size_t buf_len;
	size_t cnt;
	struct ldms_reply *reply;

	assert(t != LDMS_DIR_LIST);

	hdr_len = sizeof(struct ldms_reply_hdr)
		+ sizeof(struct ldms_dir_reply);
	buf_len = ldms_xprt_msg_max(x);
	reply = malloc(buf_len);

	cnt = snprintf(reply->dir.json_data, buf_len - hdr_len,
		       "{ \"directory\" : [ %s ]}", json);
	if (cnt >= buf_len)
		goto out;
	reply->hdr.xid = x->remote_dir_xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_UPDATE_REPLY);
	reply->hdr.rc = 0;
	reply->dir.type = htonl(t);
	reply->dir.json_data_len = htonl(cnt);
	reply->hdr.len = htonl(hdr_len + cnt);

#ifdef DEBUG
	x->log("%s(): x %p: remote dir ctxt %p\n",
			__func__, x, (void *)x->remote_dir_xid);
#endif /* DEBUG */

	zap_err_t zerr;
	zerr = zap_send(x->zap_ep, reply, hdr_len + cnt);
	if (zerr != ZAP_ERR_OK) {
		x->zerrno = zerr;
		if (x->log)
			x->log("%s: x %p: zap_send synchronously error. '%s'\n",
			       __FUNCTION__, x, zap_err_str(zerr));
		ldms_xprt_close(x);
	}
	free(reply);
	return;
 out:
	free(reply);
	x->log("Directory message is too large for the max transport message.\n");
	return;
}

static void send_req_notify_reply(struct ldms_xprt *x,
				  struct ldms_set *set,
				  uint64_t xid,
				  ldms_notify_event_t e)
{
	size_t len;
	int rc = 0;
	struct ldms_reply *reply;

	len = sizeof(struct ldms_reply_hdr) + e->len;
	reply = malloc(len);
	if (!reply) {
		x->log("Memory allocation failure "
		       "in notify of peer.\n");
		return;
	}
	reply->hdr.xid = xid;
	reply->hdr.cmd = htonl(LDMS_CMD_REQ_NOTIFY_REPLY);
	reply->hdr.rc = htonl(rc);
	reply->hdr.len = htonl(len);
	reply->req_notify.event.len = htonl(e->len);
	reply->req_notify.event.type = htonl(e->type);
	if (e->len > sizeof(struct ldms_notify_event_s))
		memcpy(reply->req_notify.event.u_data, e->u_data,
		       e->len - sizeof(struct ldms_notify_event_s));

	zap_err_t zerr = zap_send(x->zap_ep, reply, len);
	if (zerr != ZAP_ERR_OK) {
		x->zerrno = zerr;
		x->log("%s: zap_send synchronously error. '%s'\n",
				__FUNCTION__, zap_err_str(zerr));
		ldms_xprt_close(x);
	}
	free(reply);
	return;
}

char *__ldms_format_set_for_dir(struct ldms_set *set, size_t *buf_sz)
{
	size_t json_buf_sz = 4096;
	char *json_buf;
	size_t cnt;

	json_buf = malloc(json_buf_sz);
	cnt = __ldms_format_set_meta_as_json(set, 0, json_buf, json_buf_sz);
	while (cnt >= json_buf_sz) {
		free(json_buf);
		json_buf_sz += 4096;
		json_buf = malloc(json_buf_sz);
		if (!json_buf)
			return NULL;
		cnt = __ldms_format_set_meta_as_json(set, 0, json_buf, json_buf_sz);
	}
	*buf_sz = cnt;
	return json_buf;
}

static void dir_update(struct ldms_set *set, enum ldms_dir_type t)
{
	char *json_buf;
	size_t json_cnt;
	struct ldms_xprt *x;
	json_buf = __ldms_format_set_for_dir(set, &json_cnt);
	if (!json_buf)
		return;
	pthread_mutex_lock(&xprt_list_lock);
	LIST_FOREACH(x, &xprt_list, xprt_link) {
		if (x->remote_dir_xid)
			send_dir_update(x, t, json_buf, json_cnt);
	}
	pthread_mutex_unlock(&xprt_list_lock);
	free(json_buf);
}

void __ldms_dir_add_set(struct ldms_set *set)
{
	dir_update(set, LDMS_DIR_ADD);
}

void __ldms_dir_del_set(struct ldms_set *set)
{
	/*
	 * LDMS versions >= 4.3.4 do not send LDMS_DIR_DEL, instead
	 * they use the two way handshake provided by
	 * ldms_xprt_set_delete() to inform the peer and receive
	 * acknowledgment of the set's disuse.
	 *
	 * We still handle LDMS_DIR_DEL and pass it to the application
	 * so that it can interoperate with compute nodes that are
	 * older than 4.3.4.
	 *
	 * dir_update(set, LDMS_DIR_DEL);
	 */
}

void __ldms_dir_upd_set(struct ldms_set *set)
{
	dir_update(set, LDMS_DIR_UPD);
}

void ldms_xprt_close(ldms_t x)
{
#ifdef DEBUG
	x->log("%s(): closing x %p\n", __func__, x);
#endif /* DEBUG */
	x->remote_dir_xid = 0;
	__ldms_xprt_term(x);
}

/*
 * Must be called with the set->lock held
 */
static void __ldms_drop_rbd_set_refs(struct ldms_rbuf_desc *rbd)
{
	struct ldms_set *set = rbd->set;
	struct ldms_rbuf_desc *tgt;
	LIST_FOREACH(tgt, &set->local_rbd_list, set_link) {
		if (tgt == rbd)
			goto drop_refs;
	}
	LIST_FOREACH(tgt, &set->remote_rbd_list, set_link) {
		if (tgt == rbd)
			goto drop_refs;
	}
	return;
 drop_refs:
	LIST_REMOVE(rbd, set_link);
	ref_put(&rbd->ref, "set_rbd_list");
}

void __ldms_xprt_resource_free(struct ldms_xprt *x)
{
	pthread_mutex_lock(&x->lock);
	x->remote_dir_xid = x->local_dir_xid = 0;

#ifdef DEBUG
	x->log("DEBUG: xprt_resource_free. zap %p: active_dir = %d.\n",
		x->zap_ep, x->active_dir);
	x->log("DEBUG: xprt_resource_free. zap %p: active_lookup = %d.\n",
		x->zap_ep, x->active_lookup);
#endif /* DEBUG */

	struct ldms_context *ctxt;
	while (!TAILQ_EMPTY(&x->ctxt_list)) {
		ctxt = TAILQ_FIRST(&x->ctxt_list);

#ifdef DEBUG
		switch (ctxt->type) {
		case LDMS_CONTEXT_DIR:
			x->active_dir--;
			break;
		case LDMS_CONTEXT_DIR_CANCEL:
			x->active_dir_cancel--;
			break;
		case LDMS_CONTEXT_LOOKUP:
			x->active_lookup--;
			break;
		case LDMS_CONTEXT_PUSH:
			x->active_push--;
			break;
		default:
			break;
		}
#endif /* DEBUG */

		__ldms_free_ctxt(x, ctxt);
	}

	struct rbn *rbn;
	struct ldms_rbuf_desc *rbd;
	struct ldms_set *set;
	while ((rbn = rbt_min(&x->rbd_rbt))) {
		rbd = RBN_RBD(rbn);
		set = rbd->set;
		if (set) {
			pthread_mutex_unlock(&x->lock);
			pthread_mutex_lock(&set->lock);
			__ldms_drop_rbd_set_refs(rbd);
			pthread_mutex_unlock(&set->lock);
			pthread_mutex_lock(&x->lock);
		}
		__ldms_rbd_xprt_release(rbd);
	}
	if (x->auth) {
		ldms_auth_free(x->auth);
		x->auth = NULL;
	}
	pthread_mutex_unlock(&x->lock);
}

void ldms_xprt_put(ldms_t x)
{
	int remove = 0;
	assert(x->ref_count);
	/*
	 * The xprt could be destroyed any time ldms_xprt_put is called.
	 * We need to take the xprt list lock to prevent the race
	 * between destroying the xprt and accessing the xprt from the list
	 * on another thread.
	 */
	pthread_mutex_lock(&xprt_list_lock);
	if (0 == __sync_sub_and_fetch(&x->ref_count, 1)) {
		remove = 1;
		LIST_REMOVE(x, xprt_link);
#ifdef DEBUG
		x->xprt_link.le_next = 0;
		x->xprt_link.le_prev = 0;
#endif /* DEBUG */
	}
	pthread_mutex_unlock(&xprt_list_lock);

	if (!remove)
		return;

	__ldms_xprt_resource_free(x);
	if (x->zap_ep)
		zap_free(x->zap_ep);
	sem_destroy(&x->sem);
	free(x);
}

static void process_set_delete_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct ldms_reply reply;
	struct ldms_set *set;
	struct ldms_rbuf_desc *r;

	__ldms_set_tree_lock();
	set = __ldms_find_local_set(req->set_delete.inst_name);
	__ldms_set_tree_unlock();
	if (!set)
		goto reply;
	r = ldms_lookup_rbd(x, set);
	if (!r)
		goto reply_1;
	if (x->event_cb) {
		struct ldms_xprt_event event;
		event.type = LDMS_XPRT_EVENT_SET_DELETE;
		event.set_delete.set = r;
		event.data_len = sizeof(ldms_set_t);
		x->event_cb(x, &event, x->event_cb_arg);
	}
 reply_1:
	ref_put(&set->ref, "__ldms_find_local_set");
 reply:
	/* Initialize the reply header */
	reply.hdr.xid = req->hdr.xid;
	reply.hdr.cmd = htonl(LDMS_CMD_SET_DELETE_REPLY);
	reply.hdr.rc = 0;
	reply.hdr.len = htonl(sizeof(reply.hdr));
	zap_err_t zerr = zap_send(x->zap_ep, &reply, sizeof(reply.hdr));
	if (zerr != ZAP_ERR_OK) {
		x->zerrno = zerr;
		x->log("%s: zap_send synchronously error. '%s'\n",
				__FUNCTION__, zap_err_str(zerr));
	}
}

static
void process_set_delete_reply(struct ldms_xprt *x, struct ldms_reply *reply,
			      struct ldms_context *ctxt)
{
	ctxt->set_delete.cb(x, reply->hdr.rc, ctxt->set_delete.s, ctxt->set_delete.cb_arg);
	__ldms_free_ctxt(x, ctxt);
}

struct make_dir_arg {
	int reply_size;		/* size of reply in total */
	struct ldms_reply *reply;
	struct ldms_xprt *x;
	int reply_count;	/* sets in this reply */
	char *set_list;		/* buffer for set names */
	ssize_t set_list_len;	/* current length of this buffer */
};

static void process_dir_request(struct ldms_xprt *x, struct ldms_request *req)
{
	size_t len;
	size_t hdrlen;
	int rc;
	zap_err_t zerr;
	struct ldms_reply reply_;
	struct ldms_reply *reply = NULL;
	struct ldms_name_list name_list;

	if (req->dir.flags)
		/* Register for directory updates */
		x->remote_dir_xid = req->hdr.xid;
	else
		/* Cancel any previous dir update */
		x->remote_dir_xid = 0;

	hdrlen = sizeof(struct ldms_reply_hdr)
		+ sizeof(struct ldms_dir_reply);

	__ldms_set_tree_lock();
	rc = __ldms_get_local_set_list(&name_list);
	__ldms_set_tree_unlock();
	if (rc)
		goto out;

	len = ldms_xprt_msg_max(x);
	reply = malloc(len);
	if (!reply) {
		rc = ENOMEM;
		len = sizeof(struct ldms_reply_hdr);
		goto out;
	}

	/* Initialize the reply header */
	reply->hdr.xid = req->hdr.xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_REPLY);
	reply->hdr.rc = 0;
	reply->dir.type = htonl(LDMS_DIR_LIST);

	size_t last_cnt, cnt = 0;
	int last_set;
	uid_t uid;
	gid_t gid;
	uint32_t perm;
	struct ldms_name_entry *name;

	if (LIST_EMPTY(&name_list)) {
		cnt = snprintf(reply->dir.json_data,
			       len - hdrlen,
			       "{ \"directory\" : []}");
		if (cnt >= len) {
			rc = ENOMEM;
			goto out;
		}
		reply->hdr.len = htonl(cnt + hdrlen);
		reply->dir.json_data_len = htonl(cnt);
		reply->dir.more = 0;
		zerr = zap_send(x->zap_ep, reply, cnt + hdrlen);
		if (zerr != ZAP_ERR_OK) {
			x->zerrno = zerr;
			x->log("%s: x %p: zap_send synchronous error. '%s'\n",
			       __FUNCTION__, x, zap_err_str(zerr));
		}
		free(reply);
		return;
	}
	struct ldms_set *set;
	LIST_FOREACH(name, &name_list, entry) {
		__ldms_set_tree_lock();
		set = __ldms_find_local_set(name->name);
		__ldms_set_tree_unlock();
		if (!set)
			continue;
		uid = __le32_to_cpu(set->meta->uid);
		gid = __le32_to_cpu(set->meta->gid);
		perm = __le32_to_cpu(set->meta->perm);
		last_set = (LIST_NEXT(name, entry) == 0);
	restart:
		last_cnt = cnt;	/* save current end of json */
		if (cnt == 0) {
			/* Start new dir message */
			cnt = snprintf(reply->dir.json_data, len - hdrlen,
				       "{ \"directory\" : [");
			if (cnt >= len - hdrlen) {
				rc = ENOMEM;
				ref_put(&set->ref, "__ldms_find_local_set");
				goto out;
			}
		}

		if (0 == ldms_access_check(x, LDMS_ACCESS_READ, uid, gid, perm)) {
			/* no access, skip it */
			cnt += __ldms_format_set_meta_as_json(set, last_cnt,
						      &reply->dir.json_data[cnt],
						      len - hdrlen - cnt - 3 /* ]}\0 */);
		}

		if (/* Too big to fit in transport message, send what we have */
		    (cnt >= len - hdrlen - 3)) {
			last_cnt += snprintf(&reply->dir.json_data[last_cnt],
					     len - hdrlen - last_cnt,
					     "]}");
			if (last_cnt >= len - hdrlen) {
				ref_put(&set->ref, "__ldms_find_local_set");
				goto out;
			}
			reply->hdr.len = htonl(last_cnt + hdrlen);
			reply->dir.json_data_len = htonl(last_cnt);
			reply->dir.more = htonl(1);
			zerr = zap_send(x->zap_ep, reply, last_cnt + hdrlen);
			if (zerr != ZAP_ERR_OK) {
				x->zerrno = zerr;
				x->log("%s: x %p: zap_send synchronous error. '%s'\n",
				       __FUNCTION__, x, zap_err_str(zerr));
			}
			cnt = 0;
			goto restart;
		}

		if (last_set) {
			/* If this is the last set, send the message */
			cnt += snprintf(&reply->dir.json_data[cnt],
				       len - hdrlen - last_cnt - 3,
				       "]}");
			if (cnt >= len - hdrlen - 3) {
				rc = ENOMEM;
				ref_put(&set->ref, "__ldms_find_local_set");
				goto out;
			}
			reply->hdr.len = htonl(cnt + hdrlen);
			reply->dir.json_data_len = htonl(cnt);
			reply->dir.more = 0;
			zerr = zap_send(x->zap_ep, reply, cnt + hdrlen);
			if (zerr != ZAP_ERR_OK) {
				x->zerrno = zerr;
				x->log("%s: x %p: zap_send synchronous error. '%s'\n",
				       __FUNCTION__, x, zap_err_str(zerr));
			}
		}
		ref_put(&set->ref, "__ldms_find_local_set");
	}
	free(reply);
	__ldms_empty_name_list(&name_list);
	return;
out:
	if (reply)
		free(reply);
	reply = &reply_;
	len = hdrlen;
	reply->hdr.xid = req->hdr.xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_REPLY);
	reply->hdr.rc = htonl(rc);
	reply->dir.more = 0;
	reply->dir.type = htonl(LDMS_DIR_LIST);
	reply->dir.json_data_len = 0;
	reply->hdr.len = htonl(len);

	zerr = zap_send(x->zap_ep, reply, len);
	if (zerr != ZAP_ERR_OK) {
		x->zerrno = zerr;
		x->log("%s: zap_send synchronously error. '%s'\n",
				__FUNCTION__, zap_err_str(zerr));
		ldms_xprt_close(x);
	}
	return;
}

static void
process_dir_cancel_request(struct ldms_xprt *x, struct ldms_request *req)
{
	x->remote_dir_xid = 0;
	struct ldms_reply_hdr hdr;
	hdr.rc = 0;
	hdr.xid = req->hdr.xid;
	hdr.cmd = htonl(LDMS_CMD_DIR_CANCEL_REPLY);
	hdr.len = htonl(sizeof(struct ldms_reply_hdr));
	zap_err_t zerr = zap_send(x->zap_ep, &hdr, sizeof(hdr));
	if (zerr != ZAP_ERR_OK) {
		x->log("%s: zap_send synchronously error. '%s'\n",
				__FUNCTION__, zap_err_str(zerr));
		ldms_xprt_close(x);
	}
}

static void
process_send_request(struct ldms_xprt *x, struct ldms_request *req)
{
	if (!x->event_cb)
		return;
	struct ldms_xprt_event event;
	event.type = LDMS_XPRT_EVENT_RECV;
	event.data = req->send.msg;
	event.data_len = ntohl(req->send.msg_len);
	x->event_cb(x, &event, x->event_cb_arg);
}

static int
process_auth_msg(struct ldms_xprt *x, struct ldms_request *req)
{
	if (!x->auth)
		return ENOENT;
	if (x->auth_flag != LDMS_XPRT_AUTH_BUSY)
		return EINVAL; /* invalid auth state */
	return x->auth->plugin->auth_xprt_recv_cb(x->auth, x,
				req->send.msg, ntohl(req->send.msg_len));
}

static struct ldms_rbuf_desc *
__rbd_by_set_id(struct ldms_xprt *x, uint64_t id)
{
	ldms_rbuf_t r = NULL;
	struct ldms_set *set;
	/* 1st check if the set still exits */
	__ldms_set_tree_lock();
	set = __ldms_set_by_id(id);
	__ldms_set_tree_unlock();
	if (!set)
		goto out;
	r = ldms_lookup_rbd(x, set);
 out:
	return r;
}

static void
process_req_notify_request(struct ldms_xprt *x, struct ldms_request *req)
{

	struct ldms_rbuf_desc *r = __rbd_by_set_id(x, req->req_notify.set_id);
	if (r) {
		r->remote_notify_xid = req->hdr.xid;
		r->notify_flags = ntohl(req->req_notify.flags);
	}
}

static void
process_cancel_notify_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct ldms_rbuf_desc *r = __rbd_by_set_id(x, req->cancel_notify.set_id);
	if (r)
		r->remote_notify_xid = 0;
}

static void
process_cancel_push_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct ldms_rbuf_desc *r;
	struct ldms_rbuf_desc *push_rbd;
	struct ldms_set *set;
	uint64_t remote_set_id;

	push_rbd = __rbd_by_set_id(x, req->cancel_push.set_id);
	if (!push_rbd) {
		x->log("%s: the specified set_id %ld no longer exits.\n",
		       __func__, req->cancel_push.set_id);
	}
	if (0 == (push_rbd->push_flags & LDMS_RBD_F_PUSH))
		return;

	set = push_rbd->set;

	/* Peer will get push notification with UPD_F_PUSH_LAST set. */
	assert(!(push_rbd->push_flags & LDMS_RBD_F_PUSH_CANCEL));
	remote_set_id = push_rbd->remote_set_id;

	pthread_mutex_lock(&xprt_list_lock);
	pthread_mutex_lock(&set->lock);

	/*
	 * If any remaining RBD still want automatic push updates,
	 * leave it on for the set, otherwise, turn it off for the set
	 */
	push_rbd->remote_set_id = 0;

	__ldms_free_rbd(push_rbd, "rendezvous_push");

	LIST_FOREACH(r, &set->remote_rbd_list, set_link) {
		if (r->push_flags & LDMS_RBD_F_PUSH_CHANGE)
			goto out;
	}
	set->flags &= ~LDMS_SET_F_PUSH_CHANGE;
out:
	pthread_mutex_unlock(&set->lock);
	pthread_mutex_unlock(&xprt_list_lock);

	struct ldms_reply reply;

	ldms_xprt_get(x);
	size_t len = sizeof(struct ldms_reply_hdr) +
			sizeof(struct ldms_push_reply);
	reply.hdr.xid = 0;
	reply.hdr.cmd = htonl(LDMS_CMD_PUSH_REPLY);
	reply.hdr.len = htonl(len);
	reply.hdr.rc = 0;
	reply.push.set_id = remote_set_id;
	reply.push.data_len = 0;
	reply.push.data_off = 0;
	reply.push.flags = htonl(LDMS_UPD_F_PUSH | LDMS_UPD_F_PUSH_LAST);
	zap_err_t zerr = zap_send(x->zap_ep, &reply, len);
	if (zerr) {
		x->zerrno = zerr;
		x->log("%s(): x %p: error %s sending PUSH_REPLY\n",
		       __func__, x, zap_err_str(zerr));
	}
	ldms_xprt_put(x);
	return;
}

/* Caller should hold the set lock */
static void __copy_set_info_to_lookup_msg(char *buffer, ldms_name_t schema,
						ldms_name_t inst_name,
						struct ldms_set *set)
{
	struct ldms_set_info_pair *pair;
	ldms_name_t str = (ldms_name_t)buffer;

	/* schema name */
	str->len = schema->len;
	strcpy(str->name, schema->name);
	str = (ldms_name_t)&(str->name[str->len]);

	/* instance name */
	str->len = inst_name->len;
	strcpy(str->name, inst_name->name);
	str = (ldms_name_t)&(str->name[str->len]);

	/* Local set information */
	LIST_FOREACH(pair, &set->local_info, entry) {
		/* Copy the key string */
		str->len = strlen(pair->key) + 1;
		strcpy(str->name, pair->key);
		str = (ldms_name_t)&(str->name[str->len]);

		/* Copy the value string */
		str->len = strlen(pair->value) + 1;
		strcpy(str->name, pair->value);
		str = (ldms_name_t)&(str->name[str->len]);
	}

	/* Remote set information */
	LIST_FOREACH(pair, &set->remote_info, entry) {
		if (__ldms_set_info_find(&set->local_info, pair->key)) {
			/*
			 * The local set info supersedes the remote set info.
			 * Skip if the key exists in the local set info list.
			 */
			continue;
		}

		/* Copy the key string */
		str->len = strlen(pair->key) + 1;
		strcpy(str->name, pair->key);
		str = (ldms_name_t)&(str->name[str->len]);

		/* Copy the value string */
		str->len = strlen(pair->value) + 1;
		strcpy(str->name, pair->value);
		str = (ldms_name_t)&(str->name[str->len]);
	}
	str->len = 0;
}

struct lu_set_info {
	struct ldms_set *set;
	int flag; /* local/remote set info */
	int count;
	size_t len;
};
static int __get_set_info_sz(struct ldms_set *set, int *count, size_t *len)
{
	struct ldms_set_info_pair *pair;
	int cnt = 0;
	size_t l = 0;
	LIST_FOREACH(pair, &set->local_info, entry) {
		cnt++;
		l += strlen(pair->key) + strlen(pair->value) + 2;
	}
	LIST_FOREACH(pair, &set->remote_info, entry) {
		if (__ldms_set_info_find(&set->local_info, pair->key))
			continue;
		cnt++;
		l += strlen(pair->key) + strlen(pair->value) + 2;
	}
	*count = cnt;
	*len = l;
	return 0;
}

static int __send_lookup_reply(struct ldms_xprt *x, struct ldms_set *set,
			       uint64_t xid, int more)
{
	struct ldms_rbuf_desc *rbd;
	int rc = ENOENT;
	if (!set)
		goto err_0;
	/* Search the transport to see if there is already an RBD for
	 * this set on this transport */
	pthread_mutex_lock(&x->lock);
	rbd = ldms_lookup_rbd(x, set);
	if (!rbd) {
		rc = ENOMEM;
		/* Allocate a new RBD for this set, ref put in __set_delete_cb() */
		rbd = __ldms_alloc_rbd(x, set, LDMS_RBD_LOCAL, "share_lookup");
		if (!rbd)
			goto err_0;
	}
	ldms_name_t name = get_instance_name(set->meta);
	ldms_name_t schema = get_schema_name(set->meta);
	/*
	 * The lookup.set_info encodes schema name, instance name
	 * and the set info key value pairs as follows.
	 *
	 * +---------------------------+
	 * | schema name length        |
	 * +---------------------------+
	 * | schema name string        |
	 * S                           S
	 * +---------------------------+
	 * | instance name length      |
	 * +---------------------------+
	 * | instance name string      |
	 * S                           S
	 * +---------------------------+
	 * | first key string length   |
	 * +---------------------------+
	 * | first key string          |
	 * S                           S
	 * +---------------------------+
	 * | first value string length |
	 * +---------------------------+
	 * | first value string        |
	 * S                           S
	 * +---------------------------+
	 * S                           S
	 * +---------------------------+
	 * | last key string length    |
	 * +---------------------------+
	 * | last key string           |
	 * S                           S
	 * +---------------------------+
	 * | last value string length  |
	 * +---------------------------+
	 * | last value string         |
	 * S                           S
	 * +---------------------------+
	 */
	int set_info_cnt;
	size_t set_info_len;
	size_t msg_len;
	struct ldms_rendezvous_msg *msg;

	pthread_mutex_lock(&set->lock);
	__get_set_info_sz(set, &set_info_cnt, &set_info_len);
	msg_len = sizeof(struct ldms_rendezvous_hdr)
			+ sizeof(struct ldms_rendezvous_lookup_param)
			/*
			 * +2 for schema name and instance name
			 * +1 for the terminating string of length 0
			 */
			+ sizeof(struct ldms_name) * (2 + (set_info_cnt) * 2 + 1)
			+ name->len + schema->len + set_info_len;
	msg = calloc(1, msg_len);
	if (!msg) {
		rc = ENOMEM;
		goto err_0;
	}

	__copy_set_info_to_lookup_msg(msg->lookup.set_info, schema, name, set);
	pthread_mutex_unlock(&set->lock);
	msg->hdr.xid = xid;
	msg->hdr.cmd = htonl(LDMS_XPRT_RENDEZVOUS_LOOKUP);
	msg->hdr.len = msg_len;
	msg->lookup.set_id = set->set_id;
	msg->lookup.more = htonl(more);
	msg->lookup.data_len = htonl(__le32_to_cpu(set->meta->data_sz));
	msg->lookup.meta_len = htonl(__le32_to_cpu(set->meta->meta_sz));
	msg->lookup.card = htonl(__le32_to_cpu(set->meta->card));
	msg->lookup.array_card = htonl(__le32_to_cpu(set->meta->array_card));

#ifdef DEBUG
	x->log("%s(): x %p: sharing ... remote lookup ctxt %p\n",
			__func__, x, (void *)xid);
#endif /* DEBUG */
	zap_err_t zerr = zap_share(x->zap_ep, rbd->lmap, (const char *)msg, msg_len);
	if (zerr != ZAP_ERR_OK) {
		x->zerrno = zerr;
		rc = zap_zerr2errno(zerr);
		x->log("%s: x %p: zap_share synchronously error. '%s'\n",
				__FUNCTION__, x, zap_err_str(zerr));
		free(msg);
		goto err_0;
	}
	pthread_mutex_unlock(&x->lock);
	free(msg);
	return 0;
 err_0:
	pthread_mutex_unlock(&x->lock);
	return rc;
}

static int __re_match(struct ldms_set *set, regex_t *regex, const char *regex_str, int flags)
{
	ldms_name_t name;
	int rc;

	if (flags & LDMS_LOOKUP_BY_SCHEMA)
		name = get_schema_name(set->meta);
	else
		name = get_instance_name(set->meta);

	if (flags & LDMS_LOOKUP_RE)
		rc = regexec(regex, name->name, 0, NULL, 0);
	else
		rc = strcmp(regex_str, name->name);

	return (rc == 0);
}

static struct ldms_set *__next_re_match(struct ldms_set *set,
					regex_t *regex, const char *regex_str, int flags)
{
	for (; set; set = __ldms_local_set_next(set)) {
		if (__re_match(set, regex, regex_str, flags))
			break;
	}
	return set;
}

int __xprt_set_access_check(struct ldms_xprt *x, struct ldms_set *set,
			    uint32_t acc)
{
	uid_t uid = __le32_to_cpu(set->meta->uid);
	gid_t gid = __le32_to_cpu(set->meta->gid);
	uint32_t perm = __le32_to_cpu(set->meta->perm);
	return ldms_access_check(x, acc, uid, gid, perm);
}

static void process_lookup_request_re(struct ldms_xprt *x, struct ldms_request *req, uint32_t flags)
{
	regex_t regex;
	struct ldms_reply_hdr hdr;
	struct ldms_set *set, *nxt_set;
	int rc, more;
	int matched = 0;

	if (flags & LDMS_LOOKUP_RE) {
		rc = regcomp(&regex, req->lookup.path, REG_EXTENDED | REG_NOSUB);
		if (rc) {
			char errstr[512];
			(void)regerror(rc, &regex, errstr, sizeof(errstr));
			x->log(errstr);
			rc = EINVAL;
			goto err_0;
		}
	} else if (0 == (flags & LDMS_LOOKUP_BY_SCHEMA)) {
		__ldms_set_tree_lock();
		set = __ldms_find_local_set(req->lookup.path);
		if (!set) {
			rc = ENOENT;
			goto err_1;
		}
		__ldms_set_tree_unlock();
		rc = __send_lookup_reply(x, set, req->hdr.xid, 0);
		ref_put(&set->ref, "__ldms_find_local_set");
		if (rc)
			goto err_1;
		return;
	}

	/* Get the first match */
	__ldms_set_tree_lock();
	set = __ldms_local_set_first();
	set = __next_re_match(set, &regex, req->lookup.path, flags);
	if (!set) {
		rc = ENOENT;
		goto err_1;
	}
	while (set) {
		/* Get the next match if any */
		nxt_set = __next_re_match(__ldms_local_set_next(set),
					  &regex, req->lookup.path, flags);
		rc = __xprt_set_access_check(x, set, LDMS_ACCESS_READ);
		if (rc)
			goto skip;
		if (nxt_set)
			more = 1;
		else
			more = 0;
		rc = __send_lookup_reply(x, set, req->hdr.xid, more);
		if (rc)
			goto err_1;
		matched = 1;
	skip:
		set = nxt_set;
	}
	__ldms_set_tree_unlock();
	if (!matched) {
		rc = ENOENT;
		goto err_1;
	}
	if (flags & LDMS_LOOKUP_RE)
		regfree(&regex);
	return;
 err_1:
	__ldms_set_tree_unlock();
	if (flags & LDMS_LOOKUP_RE)
		regfree(&regex);
 err_0:
	hdr.rc = htonl(rc);
	hdr.xid = req->hdr.xid;
	hdr.cmd = htonl(LDMS_CMD_LOOKUP_REPLY);
	hdr.len = htonl(sizeof(struct ldms_reply_hdr));
	zap_err_t zerr = zap_send(x->zap_ep, &hdr, sizeof(hdr));
	if (zerr != ZAP_ERR_OK) {
		x->zerrno = zerr;
		x->log("%s: x %p: zap_send synchronously errors '%s'\n",
				__func__, x, zap_err_str(rc));
		ldms_xprt_close(x);
	}
}

/**
 * This function processes the lookup request from another peer.
 *
 * In the case of lookup OK, do ::zap_share().
 * In the case of lookup error, reply lookup error message.
 */
static void process_lookup_request(struct ldms_xprt *x, struct ldms_request *req)
{
	uint32_t flags = ntohl(req->lookup.flags);
	process_lookup_request_re(x, req, flags);
}

static int do_read_all(ldms_t x, ldms_set_t s, ldms_update_cb_t cb, void *arg)
{
	/* Read metadata and the first set in the set array in 1 RDMA read. */
	TF();
	struct ldms_context *ctxt;
	int rc;
	uint32_t len = __le32_to_cpu(s->set->meta->meta_sz)
			+ __le32_to_cpu(s->set->meta->data_sz);

	ctxt = __ldms_alloc_ctxt(x, sizeof(*ctxt), LDMS_CONTEXT_UPDATE,
				 s, cb, arg, 0, 0);
	if (!ctxt) {
		rc = ENOMEM;
		goto out;
	}
	rc = zap_read(x->zap_ep, s->rmap, zap_map_addr(s->rmap),
		      s->lmap, zap_map_addr(s->lmap), len, ctxt);
	if (rc) {
		x->zerrno = rc;
		__ldms_free_ctxt(x, ctxt);
	}
out:
	return zap_zerr2errno(rc);
}

static int do_read_meta(ldms_t x, ldms_set_t s, ldms_update_cb_t cb, void *arg)
{
	/* Read only the metadata; the data will be updated separately when the
	 * metadata read completed. */
	TF();
	struct ldms_context *ctxt;
	int rc;
	uint32_t meta_sz = __le32_to_cpu(s->set->meta->meta_sz);

	ctxt = __ldms_alloc_ctxt(x, sizeof(*ctxt), LDMS_CONTEXT_UPDATE_META,
							s, cb, arg, 0, 0);
	if (!ctxt) {
		rc = ENOMEM;
		goto out;
	}
	rc = zap_read(x->zap_ep, s->rmap, zap_map_addr(s->rmap),
			s->lmap, zap_map_addr(s->lmap), meta_sz, ctxt);
	if (rc) {
		x->zerrno = rc;
		__ldms_free_ctxt(x, ctxt);
	}
out:
	return zap_zerr2errno(rc);
}

static int do_read_data(ldms_t x, ldms_set_t s, int idx_from, int idx_to,
			ldms_update_cb_t cb, void*arg)
{
	/* Read multiple set data in the set array from `idx_from` to `idx_to`
	 * (inclusive) in 1 RDMA read. */
	int rc;
	uint32_t data_sz;
	struct ldms_context *ctxt;
	size_t doff, dlen;
	TF();

	ctxt = __ldms_alloc_ctxt(x, sizeof(*ctxt), LDMS_CONTEXT_UPDATE,
							s, cb, arg, idx_from, idx_to);

	if (!ctxt) {
		rc = ENOMEM;
		goto out;
	}
	data_sz = __le32_to_cpu(s->set->meta->data_sz);
	doff = (uint8_t *)s->set->data_array - (uint8_t *)s->set->meta
							+ idx_from * data_sz;
	dlen = (idx_to - idx_from + 1) * data_sz;

	rc = zap_read(x->zap_ep, s->rmap, zap_map_addr(s->rmap) + doff,
		      s->lmap, zap_map_addr(s->lmap) + doff, dlen, ctxt);
	if (rc) {
		x->zerrno = rc;
		rc = zap_zerr2errno(rc);
		__ldms_free_ctxt(x, ctxt);
	}
out:
	return rc;
}

/*
 * The meta data and the data are updated separately. The assumption
 * is that the meta data rarely (if ever) changes. The GN (generation
 * number) of the meta data is checked. If it is zero, then the meta
 * data has never been updated and it is fetched. If it is non-zero,
 * then the data is fetched. The meta data GN from the data is checked
 * against the GN returned in the data. If it matches, we're done. If
 * they don't match, then the meta data is fetched and then the data
 * is fetched again.
 */
int __ldms_remote_update(ldms_t x, ldms_set_t s, ldms_update_cb_t cb, void *arg)
{
	assert(x == s->xprt);
	if (!s->lmap || !s->rmap)
		return EINVAL;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return EPERM;
	int rc;
	struct ldms_set *set = s->set;
	uint32_t meta_meta_gn = __le32_to_cpu(set->meta->meta_gn);
	uint32_t data_meta_gn = __le32_to_cpu(set->data->meta_gn);
	uint32_t n = __le32_to_cpu(set->meta->array_card);
	int idx_from, idx_to, idx_next, idx_curr;
	zap_get_ep(x->zap_ep);		/* Released in handle_zap_read_complete() */
	if (meta_meta_gn == 0 || meta_meta_gn != data_meta_gn) {
		if (set->curr_idx == (n-1)) {
			/* We can update the metadata along with the data */
			rc = do_read_all(x, s, cb, arg);
		} else {
			/* Otherwise, need to update metadata and data
			 * separately */
			rc = do_read_meta(x, s, cb, arg);
		}
	} else {
		idx_from = (s->set->curr_idx + 1) % n;
		idx_curr = __le32_to_cpu(s->set->data->curr_idx);
		idx_next = (idx_curr + 1) % n;
		if (idx_next == idx_from)
			idx_to = idx_next;
		else
			idx_to = (idx_curr < idx_from)?(n - 1):(idx_curr);
		rc = do_read_data(x, s, idx_from, idx_to, cb, arg);
	}
	if (rc)
		zap_put_ep(x->zap_ep);
	return rc;
}

static
int ldms_xprt_recv_request(struct ldms_xprt *x, struct ldms_request *req)
{
	int cmd = ntohl(req->hdr.cmd);
	int rc;

	switch (cmd) {
	case LDMS_CMD_LOOKUP:
		process_lookup_request(x, req);
		break;
	case LDMS_CMD_DIR:
		process_dir_request(x, req);
		break;
	case LDMS_CMD_DIR_CANCEL:
		process_dir_cancel_request(x, req);
		break;
	case LDMS_CMD_REQ_NOTIFY:
		process_req_notify_request(x, req);
		break;
	case LDMS_CMD_CANCEL_NOTIFY:
		process_cancel_notify_request(x, req);
		break;
	case LDMS_CMD_CANCEL_PUSH:
		process_cancel_push_request(x, req);
		break;
	case LDMS_CMD_SEND_MSG:
		process_send_request(x, req);
		break;
	case LDMS_CMD_AUTH_MSG:
		/* auth message */
		rc = process_auth_msg(x, req);
		if (rc) {
			x->auth_flag = LDMS_XPRT_AUTH_FAILED;
			__sync_fetch_and_add(&xprt_auth_fail_count, 1);
			__ldms_xprt_term(x);
		}
		break;
	case LDMS_CMD_SET_DELETE:
		process_set_delete_request(x, req);
		break;
	default:
		x->log("Unrecognized request %d\n", cmd);
		assert(0 == "Unrecognized LDMS_CMD request type");
	}
	return 0;
}

static
void process_lookup_reply(struct ldms_xprt *x, struct ldms_reply *reply,
			  struct ldms_context *ctxt)
{
	int rc = ntohl(reply->hdr.rc);
	if (!rc) {
		/* A peer should only receive error in lookup_reply.
		 * A successful lookup is handled by rendezvous. */
		x->log("WARNING: Receive lookup reply error with rc: 0\n");
		goto out;
	}
	if (ctxt->lu_req.cb)
		ctxt->lu_req.cb(x, rc, 0, NULL, ctxt->lu_req.cb_arg);

out:
	zap_put_ep(x->zap_ep);	/* Taken in __ldms_remote_lookup() */
	pthread_mutex_lock(&x->lock);
#ifdef DEBUG
	assert(x->active_lookup);
	x->active_lookup--;
	x->log("DEBUG: lookup_reply: put ref %p: active_lookup = %d\n",
			x->zap_ep, x->active_lookup);
#endif /* DEBUG */
	__ldms_free_ctxt(x, ctxt);
	pthread_mutex_unlock(&x->lock);
}

static
void process_dir_cancel_reply(struct ldms_xprt *x, struct ldms_reply *reply,
		struct ldms_context *ctxt)
{
	struct ldms_context *dir_ctxt;

	pthread_mutex_lock(&x->lock);
	if (x->local_dir_xid) {
		dir_ctxt = (struct ldms_context *)(unsigned long)x->local_dir_xid;
		__ldms_free_ctxt(x, dir_ctxt);
	}
	x->local_dir_xid = 0;
#ifdef DEBUG
	x->active_dir_cancel--;
#endif /* DEBUG */
	pthread_mutex_unlock(&x->lock);
	zap_put_ep(x->zap_ep);
}

static int __process_dir_set_info(struct ldms_set *lset, enum ldms_dir_type type,
				ldms_dir_set_t dset, json_entity_t info_list)
{
	json_entity_t info_entity, e;
	int j, rc;
	int dir_upd = 0;
	struct ldms_set_info_pair *pair, *nxt_pair;

	if (lset)
		pthread_mutex_lock(&lset->lock);
	for (j = 0, info_entity = json_item_first(info_list); info_entity;
	     info_entity = json_item_next(info_entity), j++) {
		e = json_value_find(info_entity, "key");
		dset->info[j].key = strdup(json_value_str(e)->str);
		if (!dset->info[j].key)
			return ENOMEM;
		e = json_value_find(info_entity, "value");
		dset->info[j].value = strdup(json_value_str(e)->str);
		if (!dset->info[j].value)
			return ENOMEM;

		if (lset) {
			const char *key = dset->info[j].key;
			const char *val = dset->info[j].value;
			rc = __ldms_set_info_set(&lset->remote_info, key, val);
			if (rc > 0)
				return rc;
			if (rc == 0)
				dir_upd = 1;
		}
	}
	if (!lset)
		return 0;

	pair = LIST_FIRST(&lset->remote_info);
	while (pair) {
		nxt_pair = LIST_NEXT(pair, entry);
		for (j = 0, info_entity = json_item_first(info_list); info_entity;
				info_entity = json_item_next(info_entity)) {
			e = json_value_find(info_entity, "key");
			if (0 == strcmp(pair->key, json_value_str(e)->str))
				break;
		}
		if (!info_entity) {
			__ldms_set_info_unset(pair);
			dir_upd = 1;
		}
		pair = nxt_pair;
	}
	if (lset) {
		pthread_mutex_unlock(&lset->lock);
		if ((type == LDMS_DIR_UPD) && dir_upd &&
				(lset->flags & LDMS_SET_F_PUBLISHED)) {
			__ldms_dir_upd_set(lset);
		}
	}
	return 0;
}

static
void __process_dir_reply(struct ldms_xprt *x, struct ldms_reply *reply,
		       struct ldms_context *ctxt, int more)
{
	enum ldms_dir_type type = ntohl(reply->dir.type);
	int i, rc = ntohl(reply->hdr.rc);
	size_t count, json_data_len = ntohl(reply->dir.json_data_len);
	ldms_dir_t dir = NULL;
	json_parser_t p = NULL;
	json_entity_t dir_attr, dir_list, set_entity, info_list;
	json_entity_t dir_entity = NULL;
	struct ldms_set *lset;

	if (!ctxt->dir.cb)
		return;

	if (rc)
		goto out;

	p = json_parser_new(0);
	if (!p) {
		rc = ENOMEM;
		goto out;
	}

	rc = json_parse_buffer(p, reply->dir.json_data, json_data_len, &dir_entity);
	if (rc)
		goto out;

	dir_attr = json_attr_find(dir_entity, "directory");
	if (!dir_attr) {
		rc = EINVAL;
		goto out;
	}

	dir_list = json_attr_value(dir_attr);
	if (!dir_list) {
		rc = EINVAL;
		goto out;
	}
	count = json_list_len(dir_list);

	dir = malloc(sizeof (*dir) +
		     (count * sizeof(void *)) +
		     (count * sizeof(struct ldms_dir_set_s)));
	rc = ENOMEM;
	if (!dir)
		goto out;
	rc = 0;
	dir->type = type;
	dir->more = more;
	dir->set_count = count;

	for (i = 0, set_entity = json_item_first(dir_list); set_entity;
	     set_entity = json_item_next(set_entity), i++) {
		json_entity_t e = json_value_find(set_entity, "name");
		dir->set_data[i].inst_name = strdup(json_value_str(e)->str);

		e = json_value_find(set_entity, "schema");
		dir->set_data[i].schema_name = strdup(json_value_str(e)->str);

		e = json_value_find(set_entity, "flags");
		dir->set_data[i].flags = strdup(json_value_str(e)->str);

		e = json_value_find(set_entity, "meta_size");
		dir->set_data[i].meta_size = json_value_int(e);

		e = json_value_find(set_entity, "data_size");
		dir->set_data[i].data_size = json_value_int(e);

		e = json_value_find(set_entity, "uid");
		dir->set_data[i].uid = json_value_int(e);

		e = json_value_find(set_entity, "gid");
		dir->set_data[i].gid = json_value_int(e);

		e = json_value_find(set_entity, "perm");
		dir->set_data[i].perm = strdup(json_value_str(e)->str);

		e = json_value_find(set_entity, "card");
		dir->set_data[i].card = json_value_int(e);

		e = json_value_find(set_entity, "array_card");
		dir->set_data[i].array_card = json_value_int(e);

		e = json_value_find(set_entity, "meta_gn");
		dir->set_data[i].meta_gn = json_value_int(e);

		e = json_value_find(set_entity, "data_gn");
		dir->set_data[i].data_gn = json_value_int(e);

		e = json_value_find(set_entity, "timestamp");
		json_entity_t s, u;
		s = json_value_find(e, "sec");
		u = json_value_find(e, "usec");
		dir->set_data[i].timestamp.sec = json_value_int(s);
		dir->set_data[i].timestamp.usec = json_value_int(u);

		e = json_value_find(set_entity, "duration");
		s = json_value_find(e, "sec");
		u = json_value_find(e, "usec");
		dir->set_data[i].duration.sec = json_value_int(s);
		dir->set_data[i].duration.usec = json_value_int(u);

		info_list = json_value_find(set_entity, "info");
		size_t info_count = json_list_len(info_list);
		if (!info_count) {
			dir->set_data[i].info_count = 0;
			dir->set_data[i].info = NULL;
			continue;
		}
		dir->set_data[i].info = malloc(sizeof(struct ldms_key_value_s) * info_count);
		if (!dir->set_data[i].info) {
			rc = ENOMEM;
			goto out;
		}

		/* If this set is in our local set tree, update it's set info */
		__ldms_set_tree_lock();
		dir->set_data[i].info_count = info_count;
		lset = __ldms_find_local_set(dir->set_data[i].inst_name);
		rc = __process_dir_set_info(lset, type, &dir->set_data[i], info_list);
		if (lset)
			ref_put(&lset->ref, "__ldms_find_local_set");
		__ldms_set_tree_unlock();
		if (rc)
			break;
	}

out:
	/* Callback owns dir memory. */
	ctxt->dir.cb((ldms_t)x, rc, rc ? NULL : dir, ctxt->dir.cb_arg);
	json_entity_free(dir_entity);
	json_parser_free(p);
	if (rc && dir)
		ldms_xprt_dir_free(x, dir);
}

static
void process_dir_reply(struct ldms_xprt *x, struct ldms_reply *reply,
		       struct ldms_context *ctxt)
{
	int more = ntohl(reply->dir.more);
	__process_dir_reply(x, reply, ctxt, more);
	pthread_mutex_lock(&x->lock);
	if (!x->local_dir_xid && !more) {
		__ldms_free_ctxt(x, ctxt);
	}

	if (!more) {
		zap_put_ep(x->zap_ep);	/* Taken in __ldms_remote_dir() */
#ifdef DEBUG
		assert(x->active_dir);
		x->active_dir--;
		x->log("DEBUG: ..dir_reply: put ref %p. active_dir = %d.\n",
				x->zap_ep, x->active_dir);
#endif /* DEBUG */
	}
	pthread_mutex_unlock(&x->lock);
}

static
void process_dir_update(struct ldms_xprt *x, struct ldms_reply *reply,
		       struct ldms_context *ctxt)
{
	__process_dir_reply(x, reply, ctxt, 0);
}

static void process_req_notify_reply(struct ldms_xprt *x, struct ldms_reply *reply,
			      struct ldms_context *ctxt)
{
	ldms_notify_event_t event;
	size_t len = ntohl(reply->req_notify.event.len);
	if (!ctxt->req_notify.cb)
		return;

	event = malloc(len);
	if (!event)
		return;

	event->type = ntohl(reply->req_notify.event.type);
	event->len = ntohl(reply->req_notify.event.len);

	if (len > sizeof(struct ldms_notify_event_s))
		memcpy(event->u_data,
		       &reply->req_notify.event.u_data,
		       len - sizeof(struct ldms_notify_event_s));

	ctxt->req_notify.cb((ldms_t)x,
			    ctxt->req_notify.s,
			    event, ctxt->req_notify.cb_arg);
}

static void process_push_reply(struct ldms_xprt *x, struct ldms_reply *reply,
			      struct ldms_context *ctxt)
{
	struct ldms_rbuf_desc *push_rbd;
	uint32_t data_off = ntohl(reply->push.data_off);
	uint32_t data_len = ntohl(reply->push.data_len);
	int rc;

	push_rbd = __rbd_by_set_id(x, reply->push.set_id);
	if (!push_rbd) {
		x->log("%s: set_id %ld not found\n", __func__, reply->push.set_id);
		return;
	}
	rc = __xprt_set_access_check(x, push_rbd->set, LDMS_ACCESS_WRITE);
	if (rc)
		return; /* NOTE should we terminate the xprt? */

	/* Copy the data to the metric set */
	if (data_len) {
		memcpy((char *)push_rbd->push_s->set->meta + data_off,
					reply->push.data, data_len);
	}
	if (push_rbd->push_cb &&
		(0 == (ntohl(reply->push.flags) & LDMS_CMD_PUSH_REPLY_F_MORE))) {
		push_rbd->push_cb((ldms_t)x, push_rbd->push_s,
					ntohl(reply->push.flags),
					push_rbd->push_cb_arg);
	}
}

void ldms_xprt_dir_free(ldms_t t, ldms_dir_t dir)
{
	int i, j;
	for (i = 0; i < dir->set_count; i++) {
		free(dir->set_data[i].inst_name);
		free(dir->set_data[i].schema_name);
		free(dir->set_data[i].flags);
		free(dir->set_data[i].perm);
		if (NULL == dir->set_data[i].info)
			continue;
		for (j = 0; j < dir->set_data[i].info_count; j++) {
			ldms_key_value_t kv = &dir->set_data[i].info[j];
			free(kv->key);
			free(kv->value);
		}
		free(dir->set_data[i].info);
	}
	free(dir);
}

void ldms_event_release(ldms_t t, ldms_notify_event_t e)
{
	free(e);
}

static int ldms_xprt_recv_reply(struct ldms_xprt *x, struct ldms_reply *reply)
{
	int cmd = ntohl(reply->hdr.cmd);
	uint64_t xid = reply->hdr.xid;
	struct ldms_context *ctxt;
	ctxt = (struct ldms_context *)(unsigned long)xid;
	switch (cmd) {
	case LDMS_CMD_PUSH_REPLY:
		process_push_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_LOOKUP_REPLY:
		process_lookup_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_DIR_REPLY:
		process_dir_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_DIR_CANCEL_REPLY:
		process_dir_cancel_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_DIR_UPDATE_REPLY:
		process_dir_update(x, reply, ctxt);
		break;
	case LDMS_CMD_REQ_NOTIFY_REPLY:
		process_req_notify_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_SET_DELETE_REPLY:
		process_set_delete_reply(x, reply, ctxt);
		break;
	default:
		x->log("Unrecognized reply %d\n", cmd);
	}
	return 0;
}

static int recv_cb(struct ldms_xprt *x, void *r)
{
	struct ldms_request_hdr *h = r;
	int cmd = ntohl(h->cmd);
	if (cmd > LDMS_CMD_REPLY)
		return ldms_xprt_recv_reply(x, r);

	return ldms_xprt_recv_request(x, r);
}

#if defined(__MACH__)
#define _SO_EXT ".dylib"
#undef LDMS_XPRT_LIBPATH_DEFAULT
#define LDMS_XPRT_LIBPATH_DEFAULT "/home/tom/macos/lib"
#else
#define _SO_EXT ".so"
#endif

zap_mem_info_t ldms_zap_mem_info()
{
	static struct mm_info mmi;
	static struct zap_mem_info zmmi;
	mm_get_info(&mmi);
	zmmi.start = mmi.start;
	zmmi.len = mmi.size;
	return &zmmi;
}

void __ldms_passive_connect_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
 		__ldms_xprt_resource_free(x);
		ldms_xprt_put(x);
		break;
	case LDMS_XPRT_EVENT_RECV:
		/* Do nothing */
		break;
	default:
		x->log("__ldms_passive_connect_cb: unexpected ldms_xprt event value %d\n",
			(int) e->type);
		assert(0 == "__ldms_passive_connect_cb: unexpected ldms_xprt event value");
	}
}

static
void __ldms_xprt_conn_msg_init(ldms_t _x, struct ldms_conn_msg *msg)
{
	struct ldms_xprt *x = _x;
	bzero(msg, sizeof(*msg));
	LDMS_VERSION_SET(msg->ver);
	if (x->auth)
		strncpy(msg->auth_name,
			x->auth->plugin->name, sizeof(msg->auth_name));
}

void __ldms_xprt_init(struct ldms_xprt *x, const char *name,
					ldms_log_fn_t log_fn);
static void ldms_zap_handle_conn_req(zap_ep_t zep)
{
	static char rej_msg[64] = "Insufficient resources";
	struct sockaddr lcl, rmt;
	socklen_t xlen;
	struct ldms_conn_msg msg;
#define RMT_NM_SZ 256
	char rmt_name[RMT_NM_SZ];
	int rc;
	zap_err_t zerr;
	zerr = zap_get_name(zep, &lcl, &rmt, &xlen);
	rc = getnameinfo(&rmt, xlen, rmt_name, RMT_NM_SZ, NULL, 0, NI_NUMERICHOST);
	if (rc)
		rmt_name[0] = '\0';
	struct ldms_xprt *x = zap_get_ucontext(zep);
	struct ldms_auth *auth;
	/*
	 * Accepting zep inherit ucontext from the listening endpoint.
	 * Hence, x is of listening endpoint, not of accepting zep,
	 * and we have to create new ldms_xprt for the accepting zep.
	 */
	struct ldms_xprt *_x = calloc(1, sizeof(*_x));
	if (!_x) {
		x->log("ERROR: Cannot create new ldms_xprt for connection"
				" from %s.\n", rmt_name);
		goto err0;
	}
	__ldms_xprt_init(_x, x->name, x->log);
	_x->zap = x->zap;
	_x->zap_ep = zep;
	_x->max_msg = zap_max_msg(x->zap);
	_x->event_cb = x->event_cb;
	_x->event_cb_arg = x->event_cb_arg;
	if (!_x->event_cb)
		_x->event_cb = __ldms_passive_connect_cb;
	zap_set_ucontext(zep, _x);

	auth = ldms_auth_clone(x->auth);
	if (!auth) {
		/* clone failed */
		goto err0;
	}
	_x->auth_flag = LDMS_XPRT_AUTH_INIT;
	rc = ldms_xprt_auth_bind(_x, auth);
	if (rc)
		goto err1;

	__ldms_xprt_conn_msg_init(x, &msg);

	zerr = zap_accept(zep, ldms_zap_auto_cb, (void*)&msg, sizeof(msg));
	if (zerr) {
		x->log("ERROR: %d accepting connection from %s.\n", zerr, rmt_name);
		goto err1;
	}

	/* Take a 'connect' reference. Dropped in ldms_xprt_close() */
	ldms_xprt_get(_x);

	return;
err1:
	ldms_auth_free(auth);
err0:
	zap_reject(zep, rej_msg, strlen(rej_msg)+1);
}

static
int __ldms_data_ts_cmp(struct ldms_data_hdr *a, struct ldms_data_hdr *b)
{
	if (__le32_to_cpu(a->trans.ts.sec) < __le32_to_cpu(b->trans.ts.sec))
		return -1;
	if (__le32_to_cpu(a->trans.ts.sec) > __le32_to_cpu(b->trans.ts.sec))
		return 1;
	if (__le32_to_cpu(a->trans.ts.usec) < __le32_to_cpu(b->trans.ts.usec))
		return -1;
	if (__le32_to_cpu(a->trans.ts.usec) > __le32_to_cpu(b->trans.ts.usec))
		return 1;
	return 0;
}

static void __handle_update_data(ldms_t x, struct ldms_context *ctxt,
				 zap_event_t ev)
{
	int i, rc;
	ldms_set_t s = ctxt->update.s;
	struct ldms_set *set = s->set;
	int n;
	struct ldms_data_hdr *data, *prev_data;
	int flags = 0, upd_curr_idx;

	assert(ctxt->update.cb);
	rc = LDMS_UPD_ERROR(ev->status);
	if (rc || (set == NULL)) {
		x->zerrno = rc;
		rc = zap_zerr2errno(rc);
		/* READ ERROR */
		if (!rc)
			rc = ENOENT;
		ctxt->update.cb(x, s, rc, ctxt->update.cb_arg);
		goto cleanup;
	}
	n = __le32_to_cpu(set->meta->array_card);
	/* update current index from the update */
	data = __ldms_set_array_get(s, ctxt->update.idx_from);
	upd_curr_idx = __le32_to_cpu(data->curr_idx);
	for (i = 0; i < n; i++) {
		data = __ldms_set_array_get(s, i);
		data->curr_idx = upd_curr_idx;
	}

	prev_data = __ldms_set_array_get(s, set->curr_idx);
	for (i = ctxt->update.idx_from;i <= ctxt->update.idx_to; i++) {
		data = __ldms_set_array_get(s, i);
		if (data != prev_data &&
				__ldms_data_ts_cmp(prev_data, data) >= 0) {
			/* This can happen if the remote set is not from the
			 * data sampler. */
			break;
		}
		set->curr_idx = i;
		set->data = data;
		if (i == ctxt->update.idx_to
				&& i == __le32_to_cpu(data->curr_idx)) {
			/* our update is current. */
			flags = 0;
		} else {
			flags = LDMS_UPD_F_MORE;
		}
		ctxt->update.cb(x, s, flags, ctxt->update.cb_arg);
		prev_data = data;
	}

	if (flags == 0) /* our update is current */
		goto cleanup;

	/* the updated set is not current */
	rc = __ldms_remote_update(x, s, ctxt->update.cb,
			ctxt->update.cb_arg);
	if (rc)
		ctxt->update.cb(x, s, LDMS_UPD_ERROR(rc), ctxt->update.cb_arg);

cleanup:
	zap_put_ep(x->zap_ep); /* from __ldms_remote_update() */
	pthread_mutex_lock(&x->lock);
	__ldms_free_ctxt(x, ctxt);
	pthread_mutex_unlock(&x->lock);
}

static void __handle_update_meta(ldms_t x, struct ldms_context *ctxt,
				 zap_event_t ev)
{
	int  rc;
	ldms_set_t s = ctxt->update.s;
	struct ldms_set *set = s->set;
	int idx = (set->curr_idx + 1) % __le32_to_cpu(set->meta->array_card);

	rc = do_read_data(x, s, idx, idx, ctxt->update.cb, ctxt->update.cb_arg);
	if (rc) {
		ctxt->update.cb(x, s, LDMS_UPD_ERROR(rc), ctxt->update.cb_arg);
		zap_put_ep(x->zap_ep);
	}
	/* do_read_data has its own context */
	pthread_mutex_lock(&x->lock);
	__ldms_free_ctxt(x, ctxt);
	pthread_mutex_unlock(&x->lock);
}

static void __handle_lookup(ldms_t x, struct ldms_context *ctxt,
			    zap_event_t ev)
{
	int status = 0;
	if (!ctxt->lu_read.cb)
		goto ctxt_cleanup;
	if (ev->status != ZAP_ERR_OK) {
		status = EREMOTEIO;
#if 1 // def DEBUG
		x->log("DEBUG: %s: lookup read error: zap error %d. "
			"ldms lookup status %d\n",
			ldms_set_instance_name_get(ctxt->lu_read.s),
			ev->status, status);
#endif /* DEBUG */
		/*
		 * Destroy the set, the ctxt still has a reference on
		 * it, but that will be dropped when the ctxt is
		 * freed.
		 */
		ldms_set_delete(ctxt->lu_read.s);
	} else {
		ldms_set_publish(ctxt->lu_read.s);
	}
	ctxt->lu_read.cb((ldms_t)x, status, ctxt->lu_read.more,
			 ev->status ? NULL : ctxt->lu_read.s,
			 ctxt->lu_read.cb_arg);
	if (!ctxt->lu_read.more) {
		zap_put_ep(x->zap_ep);	/* Taken in __ldms_remote_lookup() */
#ifdef DEBUG
		assert(x->active_lookup > 0);
		x->active_lookup--;
		x->log("DEBUG: read_complete: put ref %p: "
			"active_lookup = %d\n", x->zap_ep, x->active_lookup);
#endif /* DEBUG */
	}

ctxt_cleanup:
	/* each `read` for lookup has its own context */
	pthread_mutex_lock(&x->lock);
	__ldms_free_ctxt(x, ctxt);
	pthread_mutex_unlock(&x->lock);
}

static void handle_zap_read_complete(zap_ep_t zep, zap_event_t ev)
{
	struct ldms_context *ctxt = ev->context;
	struct ldms_xprt *x = zap_get_ucontext(zep);

	switch (ctxt->type) {
	case LDMS_CONTEXT_UPDATE:
		__handle_update_data(x, ctxt, ev);
		break;
	case LDMS_CONTEXT_UPDATE_META:
		__handle_update_meta(x, ctxt, ev);
		break;
	case LDMS_CONTEXT_LOOKUP_READ:
		__handle_lookup(x, ctxt, ev);
		break;
	default:
		assert(0 == "Invalid context type in zap read completion.");
	}
}

static void handle_zap_write_complete(zap_ep_t zep, zap_event_t ev)
{
}

#ifdef DEBUG
int __is_lookup_name_good(struct ldms_xprt *x,
			  struct ldms_rendezvous_lookup_param *lu,
			  struct ldms_context *ctxt)
{
	regex_t regex;
	ldms_name_t name;
	int rc = 0;

	name = (ldms_name_t)lu->set_info;
	if (!(ctxt->lu_req.flags & LDMS_LOOKUP_BY_SCHEMA)) {
		name = (ldms_name_t)(&name->name[name->len]);
	}
	if (ctxt->lu_req.flags & LDMS_LOOKUP_RE) {
		rc = regcomp(&regex, ctxt->lu_req.path, REG_EXTENDED | REG_NOSUB);
		if (rc) {
			char errstr[512];
			(void)regerror(rc, &regex, errstr, sizeof(errstr));
			x->log("%s(): %s\n", __func__, errstr);
			assert(0 == "bad regcomp in __is_lookup_name_good");
		}

		rc = regexec(&regex, name->name, 0, NULL, 0);
	} else {
		rc = strncmp(ctxt->lu_req.path, name->name, name->len);
	}

	return (rc == 0);
}
#endif /* DEBUG */

static const ldms_name_t __lookup_set_info_find(const char *set_info,
						const char *key_)
{
	ldms_name_t key = (ldms_name_t)set_info;
	ldms_name_t value = (ldms_name_t)(&key->name[key->len]);
	while (key->len) {
		if (0 == strcmp(key_, key->name))
			return key;
		key = (ldms_name_t)(&value->name[value->len]);
		value = (ldms_name_t)(&key->name[key->len]);
	}
	return NULL;
}

static int __process_lookup_set_info(struct ldms_set *lset, char *set_info)
{
	int rc = 0;
	ldms_name_t key, value;
	struct ldms_set_info_pair *pair, *nxt_pair;
	int dir_upd = 0;

	/* Check whether the value of a key is changed or not */
	key = (ldms_name_t)(set_info);
	value = (ldms_name_t)(&key->name[key->len]);
	while (key->len) {
		rc = __ldms_set_info_set(&lset->remote_info,
					key->name, value->name);
		if (rc > 0) {
			/* error */
			goto out;
		}
		if (rc == 0) {
			/* There is a change in the set info data */
			dir_upd = 1;
		}
		if (rc < 0)
			rc = 0;
		key = (ldms_name_t)(&value->name[value->len]);
		value = (ldms_name_t)(&key->name[key->len]);
	}
	if (!dir_upd) {
		/* Check if a key-value pair is removed from the set info or not */
		pair = LIST_FIRST(&lset->remote_info);
		while (pair) {
			nxt_pair = LIST_NEXT(pair, entry);
			key = __lookup_set_info_find(set_info, pair->key);
			if (!key) {
				/* Remove the key-value pair from the set info list */
				__ldms_set_info_unset(pair);
				dir_upd = 1;
			}
			pair = nxt_pair;
		}
	}
	if (dir_upd && (lset->flags & LDMS_SET_F_PUBLISHED))
		__ldms_dir_upd_set(lset);
out:
	return rc;
}

static void handle_rendezvous_lookup(zap_ep_t zep, zap_event_t ev,
				     struct ldms_xprt *x,
				     struct ldms_rendezvous_msg *lm)
{

	struct ldms_rendezvous_lookup_param *lu = &lm->lookup;
	struct ldms_context *ctxt = (void*)lm->hdr.xid;
	struct ldms_context *rd_ctxt;
	ldms_set_t rbd = NULL;
	int rc;
	ldms_name_t schema_name, inst_name;

#ifdef DEBUG
	if (!__is_lookup_name_good(x, lu, ctxt)) {
		x->log("%s(): The schema or instance name in the lookup "
				"message sent by the peer does not "
				"match the lookup request\n", __func__);
		assert(0);
	}
#endif /* DEBUG */

	schema_name = (ldms_name_t)lu->set_info;
	inst_name = (ldms_name_t)&(schema_name->name[schema_name->len]);

	__ldms_set_tree_lock();
	struct ldms_set *lset = __ldms_find_local_set(inst_name->name);
	__ldms_set_tree_unlock();
	if (!lset) {
		lset = __ldms_create_set(inst_name->name, schema_name->name,
				       ntohl(lu->meta_len), ntohl(lu->data_len),
				       ntohl(lu->card),
				       ntohl(lu->array_card),
				       LDMS_SET_F_REMOTE);
		if (!lset) {
			rc = errno;
			goto callback;
		}
		pthread_mutex_lock(&lset->lock);
		(void)__process_lookup_set_info(lset, &inst_name->name[inst_name->len]);
		pthread_mutex_unlock(&lset->lock);
	} else {
		rbd = ldms_lookup_rbd(x, lset);
		if (rbd) {
			if (!(ctxt->lu_req.flags & LDMS_LOOKUP_SET_INFO)) {
				rc = EEXIST;
			} else {
				pthread_mutex_lock(&lset->lock);
				rc = __process_lookup_set_info(lset,
					&inst_name->name[inst_name->len]);
				pthread_mutex_unlock(&lset->lock);
			}
		} else {
			/*
			 * Another transport has looked up the set.
			 */
			rc = EEXIST;
		}
		ref_put(&lset->ref, "__ldms_find_local_set");
		goto callback;
	}

	/* Bind this set to a new RBD. We will initiate RDMA_READ */
	pthread_mutex_lock(&x->lock);
	/* Dropped in process_set_delete_request() */
	rbd = __ldms_alloc_rbd(x, lset, LDMS_RBD_INITIATOR, "rendezvous_lookup");
	if (!rbd) {
		rc = ENOMEM;
		goto callback_with_lock;
	}
	ref_get(&rbd->set->ref, "set_new"); /* Dropped in ldms_set_delete() */
	ref_get(&rbd->ref, "set_new"); /* Dropped in ldms_set_delete() */

	rbd->rmap = ev->map;
	rbd->remote_set_id = lm->lookup.set_id;

	rd_ctxt = __ldms_alloc_ctxt(x, sizeof(*rd_ctxt),
				LDMS_CONTEXT_LOOKUP_READ,
				rbd,
				ctxt->lu_req.cb, ctxt->lu_req.cb_arg,
				htonl(lu->more), ctxt->lu_req.flags);
	if (!rd_ctxt) {
		x->log("%s(): Out of memory\n", __func__);
		rc = ENOMEM;
		pthread_mutex_unlock(&x->lock);
		goto callback_with_lock;
	}
	rd_ctxt->sem = ctxt->sem;
	rd_ctxt->sem_p = ctxt->sem_p;
	rd_ctxt->rc = ctxt->rc;
	pthread_mutex_unlock(&x->lock);

	rc = zap_read(zep,
		      rbd->rmap, zap_map_addr(rbd->rmap),
		      rbd->lmap, zap_map_addr(rbd->lmap),
		      __le32_to_cpu(rbd->set->meta->meta_sz),
		      rd_ctxt);
	if (rc) {
		x->zerrno = rc;
		rc = zap_zerr2errno(rc);
		pthread_mutex_lock(&x->lock);
		goto callback_with_lock;
	}
	__ldms_free_ctxt(x, ctxt);
	return;

 callback:
	pthread_mutex_lock(&x->lock);
 callback_with_lock:
#ifdef DEBUG
	x->log("DEBUG: %s: lookup error while ldms_xprt is processing the rendezvous "
			"with error %d. NOTE: error %d indicates that it is "
			"a synchronous error of zap_read\n", inst_name, rc, EIO);
#endif /* DEBUG */
	if (rbd)
		ref_get(&rbd->ref, "rendezvous_error");
	if (ctxt->lu_req.cb)
		ctxt->lu_req.cb(x, rc, 0, rc ? NULL : rbd, ctxt->lu_req.cb_arg);
	zap_put_ep(x->zap_ep);	/* Taken in __ldms_remote_lookup() */
	__ldms_free_ctxt(x, ctxt);
#ifdef DEBUG
	assert(x->active_lookup);
	x->active_lookup--;
	x->log("DEBUG: rendezvous error: put ref %p: "
	       "active_lookup = %d\n",
	       x->zap_ep, x->active_lookup);
#endif /* DEBUG */
	pthread_mutex_unlock(&x->lock);
	if (rbd) {
		ldms_set_delete(rbd);
		ref_put(&rbd->ref, "rendezvous_error");
	}
}

static void handle_rendezvous_push(zap_ep_t zep, zap_event_t ev,
				   struct ldms_xprt *x,
				   struct ldms_rendezvous_msg *lm)
{
	struct ldms_rendezvous_push_param *push = &lm->push;
	struct ldms_rbuf_desc *push_rbd;
	struct ldms_set *set;

	set = __ldms_set_by_id(push->lookup_set_id);
	if (!set) {
		/*
		 * The set has been deleted.
		 */
		return;
	}

	/* See if we already have a push RBD for this set and
	 * transport.
	 */
	push_rbd = ldms_lookup_rbd(x, set);
	if (push_rbd) {
		if (push_rbd->push_flags & LDMS_RBD_F_PUSH) {
			ref_get(&push_rbd->set->ref, "rendezvous_push");
			/* Update the push flags, but otherwise, do nothing */
			goto out;
		}
	}

	/* We will be the target of RDMA_WRITE */
	push_rbd = __ldms_alloc_rbd(x, set, LDMS_RBD_TARGET, "rendezvous_push");
	if (!push_rbd) {
		struct ldms_xprt *x = zap_get_ucontext(zep);
		x->log("handle_rendezvous_push: __ldms_alloc_rbd out of memory\n");
		return;
	}

	push_rbd->rmap = ev->map;
	push_rbd->remote_set_id = push->push_set_id;
 out:
	push_rbd->push_flags = ntohl(push->flags) | LDMS_RBD_F_PUSH;
	return;
}

/*
 * The daemon will receive a Zap Rendezvous in two circumstances:
 * - A lookup is outstanding and the peer is giving the daemon the
 *   remote buffer needed to RMDA_READ the data.
 * - A register_push was requested and the peer is giving the daemon
 *   the RBD need to RDMA_WRITE the data.
 */
static void handle_zap_rendezvous(zap_ep_t zep, zap_event_t ev)
{
	struct ldms_xprt *x = zap_get_ucontext(zep);

	if (LDMS_XPRT_AUTH_GUARD(x))
		return;

	struct ldms_rendezvous_msg *lm = (typeof(lm))ev->data;
	switch (ntohl(lm->hdr.cmd)) {
	case LDMS_XPRT_RENDEZVOUS_LOOKUP:
		handle_rendezvous_lookup(zep, ev, x, lm);
		break;
	case LDMS_XPRT_RENDEZVOUS_PUSH:
		handle_rendezvous_push(zep, ev, x, lm);
		break;
	default:
#ifdef DEBUG
		assert(0);
#endif
		x->log("Unexpected rendezvous message %d received.\n",
		       lm->hdr.cmd);
	}
}

static int __ldms_conn_msg_verify(struct ldms_xprt *x, const void *data,
				  int data_len, char *err_msg, int err_msg_len)
{
	const struct ldms_conn_msg *msg = data;
	const struct ldms_version *ver = &msg->ver;
	if (data_len < sizeof(*ver)) {
		snprintf(err_msg, err_msg_len, "Bad conn msg");
		return EINVAL;
	}
	if (!ldms_version_check(&msg->ver)) {
		snprintf(err_msg, err_msg_len, "Version mismatch");
		return EINVAL;
	}
	if (strncmp(msg->auth_name, x->auth->plugin->name, LDMS_AUTH_NAME_MAX+1)){
		snprintf(err_msg, err_msg_len, "Auth mismatch");
		return EINVAL;
	}
	return 0;
}

/**
 * ldms-zap event handling function.
 */
static void ldms_zap_cb(zap_ep_t zep, zap_event_t ev)
{
	struct ldms_xprt_event event = {
			.type = LDMS_XPRT_EVENT_LAST,
			.data = NULL,
			.data_len = 0};
	char rej_msg[128];
	struct ldms_xprt *x = zap_get_ucontext(zep);
#ifdef DEBUG
	x->log("DEBUG: ldms_zap_cb: receive %s. %p: ref_count %d\n",
			zap_event_str(ev->type), x, x->ref_count);
#endif /* DEBUG */
	switch(ev->type) {
	case ZAP_EVENT_RECV_COMPLETE:
		recv_cb(x, ev->data);
		break;
	case ZAP_EVENT_READ_COMPLETE:
		handle_zap_read_complete(zep, ev);
		break;
	case ZAP_EVENT_WRITE_COMPLETE:
		handle_zap_write_complete(zep, ev);
		break;
	case ZAP_EVENT_RENDEZVOUS:
		handle_zap_rendezvous(zep, ev);
		break;
	case ZAP_EVENT_CONNECT_REQUEST:
		__sync_fetch_and_add(&xprt_connect_request_count, 1);
		if (0 != __ldms_conn_msg_verify(x, ev->data, ev->data_len,
					   rej_msg, sizeof(rej_msg))) {
			zap_reject(zep, rej_msg, strlen(rej_msg)+1);
			break;
		}
		ldms_zap_handle_conn_req(zep);
		break;
	case ZAP_EVENT_REJECTED:
		__sync_fetch_and_add(&xprt_reject_count, 1);
		event.type = LDMS_XPRT_EVENT_REJECTED;
		if (x->event_cb)
			x->event_cb(x, &event, x->event_cb_arg);
		/* Put the reference taken in ldms_xprt_connect() */
		ldms_xprt_put(x);
		break;
	case ZAP_EVENT_CONNECTED:
		__sync_fetch_and_add(&xprt_connect_count, 1);
		/* actively connected -- expecting conn_msg */
		if (0 != __ldms_conn_msg_verify(x, ev->data, ev->data_len,
					   rej_msg, sizeof(rej_msg))) {
			__ldms_xprt_term(x);
			break;
		}
		/* then, proceed to authentication */
		ldms_xprt_auth_begin(x);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		event.type = LDMS_XPRT_EVENT_ERROR;
		if (x->event_cb)
			x->event_cb(x, &event, x->event_cb_arg);
		/* Put the reference taken in ldms_xprt_connect() */
		ldms_xprt_put(x);
		break;
	case ZAP_EVENT_DISCONNECTED:
		__sync_fetch_and_add(&xprt_disconnect_count, 1);
		/* deliver only if CONNECTED has been delivered. */
		/* i.e. auth_flag == APPROVED */
		switch (x->auth_flag) {
		case LDMS_XPRT_AUTH_DISABLE:
			assert(0); /* auth always enabled */
			break;
		case LDMS_XPRT_AUTH_APPROVED:
			event.type = LDMS_XPRT_EVENT_DISCONNECTED;
			break;
		case LDMS_XPRT_AUTH_FAILED:
			event.type = LDMS_XPRT_EVENT_REJECTED;
			break;
		case LDMS_XPRT_AUTH_INIT:
		case LDMS_XPRT_AUTH_BUSY:
			/* disconnected while authenticating */
			event.type = LDMS_XPRT_EVENT_ERROR;
			break;
		}
		pthread_mutex_lock(&x->lock);
		struct ldms_context *dir_ctxt = NULL;
		if (x->local_dir_xid) {
			dir_ctxt = (struct ldms_context *)(unsigned long)
				x->local_dir_xid;
			x->local_dir_xid = 0;
		}
		pthread_mutex_unlock(&x->lock);
		if (dir_ctxt)
			__ldms_free_ctxt(x, dir_ctxt);
		if (x->event_cb)
			x->event_cb(x, &event, x->event_cb_arg);
		#ifdef DEBUG
		x->log("DEBUG: ldms_zap_cb: DISCONNECTED %p: ref_count %d. "
				"after callback\n",
			 x, x->ref_count);
		#endif /* DEBUG */
		/* Put the reference taken in ldms_xprt_connect() or accept() */
		__ldms_xprt_resource_free(x);
		ldms_xprt_put(x);
		break;
	default:
		x->log("ldms_zap_cb: unexpected zap event value %d from network\n",
			(int) ev->type);
		assert(0 == "network sent bad zap event value to ldms_zap_cb");
	}
}

static void ldms_zap_auto_cb(zap_ep_t zep, zap_event_t ev)
{
	struct ldms_xprt *x = zap_get_ucontext(zep);
	switch(ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		assert(0 == "Illegal connect request.");
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		ldms_xprt_put(x);
		break;
	case ZAP_EVENT_CONNECTED:
		/* passively connected, proceed to authentication */
		/* the conn_msg has already been checked */
		ldms_xprt_auth_begin(x);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
	case ZAP_EVENT_DISCONNECTED:
	case ZAP_EVENT_READ_COMPLETE:
	case ZAP_EVENT_WRITE_COMPLETE:
	case ZAP_EVENT_RENDEZVOUS:
		ldms_zap_cb(zep, ev);
		break;
	default:
		x->log("ldms_zap_auto_cb: unexpected zap event value %d from network\n",
			(int) ev->type);
		assert(0 == "network sent bad zap event value to ldms_zap_auto_cb");
	}
}

zap_t __ldms_zap_get(const char *xprt, ldms_log_fn_t log_fn)
{
	int zap_type = -1;
	if (0 == strcmp(xprt, "sock")) {
		zap_type = LDMS_ZAP_XPRT_SOCK;
	} else if (0 == strcmp(xprt, "ugni")) {
		zap_type = LDMS_ZAP_XPRT_UGNI;
	} else if (0 == strcmp(xprt, "rdma")) {
		zap_type = LDMS_ZAP_XPRT_RDMA;
	} else {
		log_fn("ldms: Unrecognized xprt '%s'\n", xprt);
		errno = EINVAL;
		return NULL;
	}

	pthread_mutex_lock(&ldms_zap_list_lock);
	zap_t zap = ldms_zap_list[zap_type];
	if (zap)
		goto out;

	zap = zap_get(xprt, log_fn, ldms_zap_mem_info);
	if (!zap) {
		log_fn("ldms: Cannot get zap plugin: %s\n", xprt);
		errno = ENOENT;
		goto out;
	}
	ldms_zap_list[zap_type] = zap;
out:
	pthread_mutex_unlock(&ldms_zap_list_lock);
	return zap;
}

int __ldms_xprt_zap_new(struct ldms_xprt *x, const char *name,
					ldms_log_fn_t log_fn)
{
	int ret = 0;
	errno = 0;
	x->zap = __ldms_zap_get(name, log_fn);
	if (!x->zap) {
		ret = errno;
		goto err0;
	}

	x->zap_ep = zap_new(x->zap, ldms_zap_cb);
	if (!x->zap_ep) {
		log_fn("ERROR: Cannot create zap endpoint.\n");
		ret = ENOMEM;
		goto err0;
	}
	x->max_msg = zap_max_msg(x->zap);
	zap_set_ucontext(x->zap_ep, x);
	return 0;
err0:
	return ret;
}

static int __rbd_rbt_key_comp(void *tree_key, const void *key)
{
	uint64_t k0 = (uint64_t)tree_key;
	uint64_t k1 = (uint64_t)key;
	if (k0 < k1)
		return -1;
	if (k0 > k1)
		return 1;
	return 0;
}

void __ldms_xprt_init(struct ldms_xprt *x, const char *name,
					ldms_log_fn_t log_fn)
{
	strncpy(x->name, name, LDMS_MAX_TRANSPORT_NAME_LEN);
	x->ref_count = 1;
	x->remote_dir_xid = x->local_dir_xid = 0;

	x->luid = -1;
	x->lgid = -1;
	x->ruid = -1;
	x->rgid = -1;

	ldms_xprt_ops_t op_e;
	for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++)
		x->stats.ops[op_e].min_us = LLONG_MAX;

	x->log = log_fn;
	TAILQ_INIT(&x->ctxt_list);
	sem_init(&x->sem, 0, 0);
	rbt_init(&x->rbd_rbt, __rbd_rbt_key_comp);
	pthread_mutex_init(&x->lock, NULL);
	pthread_mutex_lock(&xprt_list_lock);
	LIST_INSERT_HEAD(&xprt_list, x, xprt_link);
	pthread_mutex_unlock(&xprt_list_lock);
}

void ldms_xprt_priority_set(ldms_t x, int prio)
{
	zap_set_priority(x->zap_ep, prio);
}

ldms_t ldms_xprt_new_with_auth(const char *xprt_name, ldms_log_fn_t log_fn,
			       const char *auth_name,
			       struct attr_value_list *auth_av_list)
{
	int ret = 0;
	ldms_auth_plugin_t auth_plugin;
	ldms_auth_t auth;
	struct ldms_xprt *x = calloc(1, sizeof(*x));
	if (!x) {
		ret = ENOMEM;
		goto err0;
	}
	if (!log_fn)
		log_fn = default_log;
	__ldms_xprt_init(x, xprt_name, log_fn);

	ret = __ldms_xprt_zap_new(x, xprt_name, log_fn);
	if (ret)
		goto err1;

	auth_plugin = ldms_auth_plugin_get(auth_name);
	if (!auth_plugin) {
		ret = errno;
		goto err1;
	}

	auth = ldms_auth_new(auth_plugin, auth_av_list);
	if (!auth) {
		ret = errno;
		goto err1;
	}
	x->auth_flag = LDMS_XPRT_AUTH_INIT;

	ret = ldms_xprt_auth_bind(x, auth);
	if (ret)
		goto err2;
	return x;

err2:
	ldms_auth_free(auth);
err1:
	ldms_xprt_put(x);
err0:
	errno = ret;
	return NULL;
}

const char *ldms_xprt_type_name(ldms_t x)
{
	return x->name;
}

ldms_t ldms_xprt_new(const char *name, ldms_log_fn_t log_fn)
{
	return ldms_xprt_new_with_auth(name, log_fn, "none", NULL);
}

size_t format_lookup_req(struct ldms_request *req, enum ldms_lookup_flags flags,
			 const char *path, uint64_t xid)
{
	size_t len = strlen(path) + 1;
	strcpy(req->lookup.path, path);
	req->lookup.path_len = htonl(len);
	req->lookup.flags = htonl(flags);
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_LOOKUP);
	len += sizeof(uint32_t) + sizeof(uint32_t) + sizeof(struct ldms_request_hdr);
	req->hdr.len = htonl(len);
	return len;
}

size_t format_dir_req(struct ldms_request *req, uint64_t xid,
		      uint32_t flags)
{
	size_t len;
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_DIR);
	req->dir.flags = htonl(flags);
	len = sizeof(struct ldms_request_hdr) +
		sizeof(struct ldms_dir_cmd_param);
	req->hdr.len = htonl(len);
	return len;
}

size_t format_dir_cancel_req(struct ldms_request *req)
{
	size_t len;
	req->hdr.xid = 0;
	req->hdr.cmd = htonl(LDMS_CMD_DIR_CANCEL);
	len = sizeof(struct ldms_request_hdr);
	req->hdr.len = htonl(len);
	return len;
}

size_t format_req_notify_req(struct ldms_request *req,
			     uint64_t xid,
			     uint64_t set_id,
			     uint64_t flags)
{
	size_t len = sizeof(struct ldms_request_hdr)
		+ sizeof(struct ldms_req_notify_cmd_param);
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_REQ_NOTIFY);
	req->hdr.len = htonl(len);
	req->req_notify.set_id = set_id;
	req->req_notify.flags = htonl(flags);
	return len;
}

size_t format_set_delete_req(struct ldms_request *req, uint64_t xid, const char *inst_name)
{
	int32_t name_len = strlen(inst_name) + 1;
	size_t len = sizeof(struct ldms_request_hdr) +
		sizeof(struct ldms_set_delete_cmd_param) +
		name_len;
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_SET_DELETE);
	req->hdr.len = htonl(len);
	strcpy(req->set_delete.inst_name, inst_name);
	req->set_delete.inst_name_len = htonl(name_len);
	return len;
}

size_t format_cancel_notify_req(struct ldms_request *req, uint64_t xid,
		uint64_t set_id)
{
	size_t len = sizeof(struct ldms_request_hdr)
		+ sizeof(struct ldms_cancel_notify_cmd_param);
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_CANCEL_NOTIFY);
	req->hdr.len = htonl(len);
	req->cancel_notify.set_id = set_id;
	return len;
}

int ldms_xprt_send(ldms_t _x, char *msg_buf, size_t msg_len)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	size_t len;
	struct ldms_context *ctxt;
	int rc;

	assert(msg_len > 4);
	if (!msg_buf)
		return EINVAL;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return EPERM;

	ldms_xprt_get(x);
	pthread_mutex_lock(&x->lock);
	size_t sz = sizeof(struct ldms_request) + sizeof(struct ldms_context) + msg_len;
	ctxt = __ldms_alloc_ctxt(x, sz, LDMS_CONTEXT_SEND);
	if (!ctxt) {
		rc = ENOMEM;
		goto err_0;
	}
	req = (struct ldms_request *)(ctxt + 1);
	req->hdr.xid = 0;
	req->hdr.cmd = htonl(LDMS_CMD_SEND_MSG);
	req->send.msg_len = htonl(msg_len);
	memcpy(req->send.msg, msg_buf, msg_len);
	len = sizeof(struct ldms_request_hdr) +
		sizeof(struct ldms_send_cmd_param) + msg_len;
	req->hdr.len = htonl(len);

	rc = zap_send(x->zap_ep, req, len);
#ifdef DEBUG
	if (rc) {
		x->log("DEBUG: send: error. put ref %p.\n", x->zap_ep);
	}
#endif
	__ldms_free_ctxt(x, ctxt);
 err_0:
	pthread_mutex_unlock(&x->lock);
	ldms_xprt_put(x);
	return rc;
}

size_t ldms_xprt_msg_max(ldms_t x)
{
	return	x->max_msg - (sizeof(struct ldms_request_hdr) +
			sizeof(struct ldms_send_cmd_param));
}

int __ldms_remote_dir(ldms_t _x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (!zap_ep_connected(x->zap_ep))
		return ENOTCONN;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return EPERM;

	/* If a dir has previously been done and updates were asked
	 * for, return EBUSY. No need for application to do dir request again. */
	if (x->local_dir_xid)
		return EBUSY;

	/* Prevent x being destroyed if DISCONNECTED is delivered in another thread */
	ldms_xprt_get(x);
	pthread_mutex_lock(&x->lock);
	ctxt = __ldms_alloc_ctxt(x, sizeof(*ctxt) + sizeof(*req),
					LDMS_CONTEXT_DIR, cb, cb_arg);
	if (!ctxt) {
		pthread_mutex_unlock(&x->lock);
		ldms_xprt_put(x);
		return ENOMEM;
	}
	req = (struct ldms_request *)(ctxt + 1);
	len = format_dir_req(req, (uint64_t)(unsigned long)ctxt, flags);
	if (flags)
		x->local_dir_xid = (uint64_t)ctxt;
	pthread_mutex_unlock(&x->lock);

	zap_get_ep(x->zap_ep);	/* Released in process_dir_reply() */

#ifdef DEBUG
	x->active_dir++;
	x->log("DEBUG: remote_dir. get ref %p. active_dir = %d. xid %p\n",
			x->zap_ep, x->active_dir, (void *)req->hdr.xid);
#endif /* DEBUG */
	zap_err_t zerr = zap_send(x->zap_ep, req, len);
	if (zerr) {
		zap_put_ep(x->zap_ep);
		pthread_mutex_lock(&x->lock);
		__ldms_free_ctxt(x, ctxt);
		x->local_dir_xid = 0;
#ifdef DEBUG
		x->log("DEBUG: remote_dir: error. put ref %p. "
				"active_dir = %d.\n",
				x->zap_ep, x->active_dir);
		x->active_dir--;
#endif /* DEBUG */
		pthread_mutex_unlock(&x->lock);
	}
	ldms_xprt_put(x);
	return zap_zerr2errno(zerr);
}

/* This request has no reply */
int __ldms_remote_dir_cancel(ldms_t _x)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return EPERM;

	/* Prevent x being destroyed if DISCONNECTED is delivered in another thread */
	ldms_xprt_get(x);
	pthread_mutex_lock(&x->lock);
	ctxt = __ldms_alloc_ctxt(x, sizeof(*req) + sizeof(*ctxt), LDMS_CONTEXT_DIR_CANCEL);
	if (!ctxt) {
		pthread_mutex_unlock(&x->lock);
		ldms_xprt_put(x);
		return ENOMEM;
	}
	req = (struct ldms_request *)(ctxt + 1);
	len = format_dir_cancel_req(req);
	zap_get_ep(x->zap_ep);

#ifdef DEBUG
	x->active_dir_cancel++;
#endif /* DEBUG */

	pthread_mutex_unlock(&x->lock);
	zap_err_t zerr = zap_send(x->zap_ep, req, len);
	if (zerr) {
#ifdef DEBUG
		pthread_mutex_lock(&x->lock);
		x->active_dir_cancel--;
		pthread_mutex_unlock(&x->lock);
#endif /* DEBUG */
		zap_put_ep(x->zap_ep);
	}

	pthread_mutex_lock(&x->lock);
	__ldms_free_ctxt(x, ctxt);
	pthread_mutex_unlock(&x->lock);
	ldms_xprt_put(x);
	return zap_zerr2errno(zerr);
}

int __ldms_remote_lookup(ldms_t _x, const char *path,
			 enum ldms_lookup_flags flags,
			 ldms_lookup_cb_t cb, void *arg)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	struct ldms_rbuf_desc *rbd;
	size_t len;

	if (!zap_ep_connected(x->zap_ep))
		return ENOTCONN;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return EPERM;

	__ldms_set_tree_lock();
	struct ldms_set *set = __ldms_find_local_set(path);
	__ldms_set_tree_unlock();
	if (set) {
		rbd = ldms_lookup_rbd(x, set);
		if (!rbd || (rbd && !(flags & LDMS_LOOKUP_SET_INFO))) {
			/*
			 * Another transport has looked up the set
			 * or
			 * the set has been looked up by this transport and
			 * this is not a set_info lookup request.
			 */
			ref_put(&set->ref, "__ldms_find_local_set");
			return EEXIST;
		}
		ref_put(&set->ref, "__ldms_find_local_set");
	}

	char *lu_path = strdup(path);
	if (!lu_path)
		return ENOMEM;

	/* Prevent x being destroyed if DISCONNECTED is delivered in another thread */
	ldms_xprt_get(x);
	pthread_mutex_lock(&x->lock);
	ctxt = __ldms_alloc_ctxt(x,
				sizeof(struct ldms_request) + sizeof(*ctxt),
				LDMS_CONTEXT_LOOKUP_REQ,
				cb, arg, lu_path, flags);
	if (!ctxt) {
		free(lu_path);
		pthread_mutex_unlock(&x->lock);
		ldms_xprt_put(x);
		return ENOMEM;
	}
	req = (struct ldms_request *)(ctxt + 1);

	len = format_lookup_req(req, flags, path, (uint64_t)(unsigned long)ctxt);

#ifdef DEBUG
	x->active_lookup++;
#endif /* DEBUG */
	pthread_mutex_unlock(&x->lock);
	zap_get_ep(x->zap_ep);	/* Released in either ...lookup_reply() or ..rendezvous() */


#ifdef DEBUG
	x->log("DEBUG: remote_lookup: get ref %p: active_lookup = %d\n",
		x->zap_ep, x->active_lookup);
#endif /* DEBUG */
	zap_err_t zerr = zap_send(x->zap_ep, req, len);
	if (zerr) {
		zap_put_ep(x->zap_ep);
		pthread_mutex_lock(&x->lock);
		__ldms_free_ctxt(x, ctxt);
		pthread_mutex_unlock(&x->lock);
#ifdef DEBUG
		x->active_lookup--;
		x->log("DEBUG: lookup_reply: error %d. put ref %p: "
		       "active_lookup = %d. path = %s\n",
		       zerr, x->zap_ep, x->active_lookup,
		       path);
#endif /* DEBUG */
	}
	ldms_xprt_put(x);
	return zap_zerr2errno(zerr);
}

static int send_req_notify(ldms_t _x, ldms_set_t s, uint32_t flags,
			   ldms_notify_cb_t cb_fn, void *cb_arg)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	ldms_xprt_get(x);
	pthread_mutex_lock(&x->lock);
	ctxt = __ldms_alloc_ctxt(x, sizeof(*req) + sizeof(*ctxt),
					LDMS_CONTEXT_REQ_NOTIFY,
					s, cb_fn, cb_arg);
	if (!ctxt) {
		pthread_mutex_unlock(&x->lock);
		ldms_xprt_put(x);
		return ENOMEM;
	}
	req = (struct ldms_request *)(ctxt + 1);

	if (s->local_notify_xid) {
		free((void *)(unsigned long)s->local_notify_xid);
		s->local_notify_xid = 0;
	}
	len = format_req_notify_req(req, (uint64_t)(unsigned long)ctxt,
				    s->remote_set_id, flags);
	s->local_notify_xid = (uint64_t)ctxt;

	pthread_mutex_unlock(&x->lock);
	zap_err_t zerr = zap_send(x->zap_ep, req, len);
	if (zerr) {
		x->zerrno = zerr;
		x->log("%s(): x %p: error %s sending REQ_NOTIFY\n",
		       __func__, x, zap_err_str(zerr));
	}
	ldms_xprt_put(x);
	return zap_zerr2errno(zerr);
}

int ldms_register_notify_cb(ldms_t x, ldms_set_t s, int flags,
			    ldms_notify_cb_t cb_fn, void *cb_arg)
{
	if (!cb_fn)
		goto err;
	return send_req_notify(x, s, (uint32_t)flags, cb_fn, cb_arg);
 err:
	errno = EINVAL;
	return -1;
}

/*
 * Tell all peers that have an RBD for this set that it is being
 * deleted. When they all reply, we can delete the set.
 */
void ldms_xprt_set_delete(ldms_set_t s, ldms_set_delete_cb_t cb_fn, void *cb_arg)
{
	struct ldms_request *req;
	struct ldms_rbuf_desc *rbd, *next_rbd;
	ldms_t xprt;
	struct ldms_context *ctxt;
	struct ldms_set *set = s->set;
	size_t len;

	/*
	 * The lock order for lookup_re, etc... is x->lock, then
	 * set->lock. Create a local list of all of the local RBD,
	 * after removing them from the set.
	 */
	LIST_HEAD(rbuf_desc_list, ldms_rbuf_desc) rbd_list;
	LIST_INIT(&rbd_list);

	pthread_mutex_lock(&set->lock);
	rbd = LIST_FIRST(&set->local_rbd_list);
	while (rbd) {
		xprt = rbd->xprt;
		next_rbd = LIST_NEXT(rbd, set_link);
		LIST_REMOVE(rbd, set_link);
		if (!xprt)
			goto next;

		ref_get(&rbd->ref, "xprt_set_delete");
		LIST_INSERT_HEAD(&rbd_list, rbd, set_link);

	next:
		ref_put(&rbd->ref, "set_rbd_list");
		rbd = next_rbd;
	}
	pthread_mutex_unlock(&set->lock);

	/* Release the set->lock, and walk the local list */
	rbd = LIST_FIRST(&rbd_list);
	while (rbd) {
		xprt = rbd->xprt;
		next_rbd = LIST_NEXT(rbd, set_link);
		LIST_REMOVE(rbd, set_link);

		pthread_mutex_lock(&xprt->lock);

		/* Make certain we didn't lose a disconnect race */
		if (rbd->xprt) {
			/* Destroy the map so we don't accept remote read requests */
			__ldms_rbd_xprt_release(rbd);
		} else {
			pthread_mutex_unlock(&xprt->lock);
			goto next_1;
		}

		ctxt = __ldms_alloc_ctxt
			(xprt,
			 sizeof(struct ldms_request) + sizeof(struct ldms_context),
			 LDMS_CONTEXT_SET_DELETE,
			 rbd, cb_fn, cb_arg);
		req = (struct ldms_request *)(ctxt + 1);
		len = format_set_delete_req(req, (uint64_t)(unsigned long)ctxt,
					    ldms_set_instance_name_get(s));
		zap_err_t zerr = zap_send(xprt->zap_ep, req, len);
		pthread_mutex_unlock(&xprt->lock);
		if (zerr) {
			xprt->zerrno = zerr;
			__ldms_free_ctxt(xprt, ctxt);
		}
	next_1:
		ref_put(&rbd->ref, "xprt_set_delete");
		rbd = next_rbd;
	}
}

static int send_cancel_notify(ldms_t _x, ldms_set_t s)
{
	struct ldms_xprt *x = _x;
	struct ldms_request req;
	size_t len;

	len = format_cancel_notify_req
		(&req, (uint64_t)(unsigned long)s->local_notify_xid,
		 s->remote_set_id);
	s->local_notify_xid = 0;

	zap_err_t zerr = zap_send(x->zap_ep, &req, len);
	if (zerr) {
		x->zerrno = zerr;
	}
	return zap_zerr2errno(zerr);
}

int ldms_cancel_notify(ldms_t t, ldms_set_t s)
{
	if (!s->set)
		goto err;
	return send_cancel_notify(t, s);
 err:
	errno = EINVAL;
	return -1;
}

void ldms_notify(ldms_set_t s, ldms_notify_event_t e)
{
	ldms_rbuf_t r;
	if (!s)
		return;
	if (!s->set)
		return;
	pthread_mutex_lock(&s->set->lock);
	LIST_FOREACH(r, &s->set->local_rbd_list, set_link) {
		if (r->remote_notify_xid &&
			(0 == r->notify_flags || (r->notify_flags & e->type))) {
			send_req_notify_reply(r->xprt,
					      s->set, r->remote_notify_xid,
					      e);
		}
	}
	pthread_mutex_unlock(&s->set->lock);
}

static int send_req_register_push(struct ldms_rbuf_desc *r, uint32_t push_change)
{
	struct ldms_xprt *x = r->xprt;
	struct ldms_rendezvous_msg req;
	size_t len;
	int rc;

	ldms_xprt_get(x);
	rc = __xprt_set_access_check(x, r->set, LDMS_ACCESS_WRITE);
	/* check if the remote can write to us */
	if (rc)
		goto out;
	len = sizeof(struct ldms_rendezvous_hdr)
		+ sizeof(struct ldms_rendezvous_push_param);
	req.hdr.xid = 0;
	req.hdr.cmd = htonl(LDMS_XPRT_RENDEZVOUS_PUSH);
	req.hdr.len = htonl(len);
	req.push.lookup_set_id = r->remote_set_id;
	req.push.push_set_id = r->set->set_id;
	req.push.flags = htonl(LDMS_RBD_F_PUSH);
	if (push_change)
		req.push.flags |= htonl(LDMS_RBD_F_PUSH_CHANGE);
	zap_err_t zerr = zap_share(x->zap_ep, r->lmap, (const char *)&req, len);
	if (zerr) {
		x->zerrno = zerr;
		rc = zap_zerr2errno(zerr);
	}
 out:
	ldms_xprt_put(x);
	return rc;
}

int ldms_xprt_register_push(ldms_set_t s, int push_flags,
			    ldms_update_cb_t cb_fn, void *cb_arg)
{
	if (!s->xprt || !cb_fn)
		return EINVAL;
	s->push_s = s;
	s->push_cb = cb_fn;
	s->push_cb_arg = cb_arg;
	return send_req_register_push(s, push_flags);
}

static int send_req_cancel_push(struct ldms_rbuf_desc *r)
{
	struct ldms_xprt *x = r->xprt;
	struct ldms_request req;
	size_t len;
	int rc;

	ldms_xprt_get(x);
	len = sizeof(struct ldms_request_hdr)
		+ sizeof(struct ldms_cancel_push_cmd_param);
	req.hdr.xid = 0;
	req.hdr.cmd = htonl(LDMS_CMD_CANCEL_PUSH);
	req.hdr.len = htonl(len);
	req.cancel_push.set_id = r->remote_set_id;
	zap_err_t zerr = zap_send(x->zap_ep, &req, len);
	if (zerr) {
		rc = zap_zerr2errno(zerr);
		x->zerrno = zerr;
	}
	ldms_xprt_put(x);
	return rc;
}

int ldms_xprt_cancel_push(ldms_set_t s)
{
	if (!s->xprt)
		return EINVAL;
	/* We may/will continue to receive push notifications after this */
	return send_req_cancel_push(s);
}

int __ldms_xprt_push(ldms_set_t s, int push_flags)
{
	int rc = 0;
	struct ldms_reply *reply;
	struct ldms_set *set = s->set;
	uint32_t meta_meta_gn = __le32_to_cpu(set->meta->meta_gn);
	uint32_t meta_meta_sz = __le32_to_cpu(set->meta->meta_sz);
	uint32_t meta_data_sz = __le32_to_cpu(set->meta->data_sz);
	struct ldms_rbuf_desc *rbd;

	pthread_mutex_lock(&set->lock);
	rbd = LIST_FIRST(&set->remote_rbd_list);
	while (rbd) {
		if ((rbd->xprt == NULL) || (0 == (rbd->push_flags & push_flags))) {
			rbd = LIST_NEXT(rbd, set_link);
			continue;
		}
		rc = 0;
		ldms_t x = rbd->xprt;
		pthread_mutex_lock(&x->lock);

		if (LDMS_XPRT_AUTH_GUARD(x)) {
			pthread_mutex_unlock(&x->lock);
			goto skip;
		}

		size_t doff;
		size_t len;

		if (rbd->meta_gn != meta_meta_gn) {
			rbd->meta_gn = meta_meta_gn;
			len = meta_meta_sz + meta_data_sz;
			doff = 0;
		} else {
			len = meta_data_sz;
			doff = (uint8_t *)set->data - (uint8_t *)set->meta;
		}
		size_t hdr_len = sizeof(struct ldms_reply_hdr)
			+ sizeof(struct ldms_push_reply);
		size_t max_len = zap_max_msg(x->zap);
		reply = malloc(max_len);
		if (!reply) {
			rc = ENOMEM;
			pthread_mutex_unlock(&x->lock);
			goto skip;
		}
#ifdef PUSH_DEBUG
		x->log("DEBUG: Push set %s to endpoint %p\n",
		       ldms_set_instance_name_get(rbd),
		       x->zap_ep);
#endif /* PUSH_DEBUG */
		while (len) {
			size_t data_len;

			if ((len + hdr_len) > max_len) {
				data_len = max_len - hdr_len;
				reply->push.flags = htonl(LDMS_CMD_PUSH_REPLY_F_MORE);
			} else {
				reply->push.flags = 0;
				data_len = len;
			}
			reply->hdr.xid = 0;
			reply->hdr.cmd = htonl(LDMS_CMD_PUSH_REPLY);
			reply->hdr.len = htonl(hdr_len + data_len);
			reply->hdr.rc = 0;
			reply->push.set_id = rbd->remote_set_id;
			reply->push.data_len = htonl(data_len);
			reply->push.data_off = htonl(doff);
			reply->push.flags |= htonl(LDMS_UPD_F_PUSH);
			if (rbd->push_flags & LDMS_RBD_F_PUSH_CANCEL)
				reply->push.flags |= htonl(LDMS_UPD_F_PUSH_LAST);
			memcpy(reply->push.data, (unsigned char *)set->meta + doff, data_len);
			rc = zap_send(x->zap_ep, reply, hdr_len + data_len);
			if (rc)
				break;
			doff += data_len;
			len -= data_len;
		}
		free(reply);
	skip:
		pthread_mutex_unlock(&x->lock);
#ifdef DEBUG
		x->active_push++;
#endif /* DEBUG */
		rbd = LIST_NEXT(rbd, set_link);
	}
	pthread_mutex_unlock(&set->lock);
	return rc;
}

int ldms_xprt_push(ldms_set_t s)
{
	return __ldms_xprt_push(s, LDMS_RBD_F_PUSH);
}

int ldms_xprt_connect(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
			ldms_event_cb_t cb, void *cb_arg)
{
	int rc;
 	struct ldms_xprt *_x = x;
	struct ldms_conn_msg msg;
	__ldms_xprt_conn_msg_init(x, &msg);
	_x->event_cb = cb;
	_x->event_cb_arg = cb_arg;
	ldms_xprt_get(x);
	rc = zap_connect(_x->zap_ep, sa, sa_len,
			 (void*)&msg, sizeof(msg.ver) + strlen(msg.auth_name) + 1);
	if (rc)
		ldms_xprt_put(x);
	return rc;
}

static void sync_connect_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		x->sem_rc = 0;
		break;
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_ERROR:
	case LDMS_XPRT_EVENT_DISCONNECTED:
		x->sem_rc = ECONNREFUSED;
		break;
	case LDMS_XPRT_EVENT_RECV:
		break;
	default:
		x->log("sync_connect_cb: unexpected ldms_xprt event value %d\n",
			(int) e->type);
		assert(0 == "sync_connect_cb: unexpected ldms_xprt event value");
	}
	sem_post(&x->sem);
}

int ldms_xprt_connect_by_name(ldms_t x, const char *host, const char *port,
			      ldms_event_cb_t cb, void *cb_arg)
{
	struct addrinfo *ai;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};
	int rc = getaddrinfo(host, port, &hints, &ai);
	if (rc)
		return EHOSTUNREACH;
	if (!cb) {
		rc = ldms_xprt_connect(x, ai->ai_addr, ai->ai_addrlen, sync_connect_cb, cb_arg);
		if (rc)
			goto out;
		sem_wait(&x->sem);
		rc = x->sem_rc;
	} else {
		rc = ldms_xprt_connect(x, ai->ai_addr, ai->ai_addrlen, cb, cb_arg);
	}
out:
	freeaddrinfo(ai);
	return rc;
}

int ldms_xprt_listen(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
		ldms_event_cb_t cb, void *cb_arg)
{
	x->event_cb = cb;
	x->event_cb_arg = cb_arg;
	return zap_listen(x->zap_ep, sa, sa_len);
}

int ldms_xprt_listen_by_name(ldms_t x, const char *host, const char *port_no,
		ldms_event_cb_t cb, void *cb_arg)
{
	int rc;
	struct sockaddr_in sin;
	struct addrinfo *ai;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};
	if (host) {
		rc = getaddrinfo(host, port_no, &hints, &ai);
		if (rc)
			return EHOSTUNREACH;
		rc = ldms_xprt_listen(x, ai->ai_addr, ai->ai_addrlen, cb, cb_arg);
		freeaddrinfo(ai);
	} else {
		long ptmp;
		ptmp = atol(port_no);
		if (ptmp < 1 || ptmp > USHRT_MAX) {
			return EINVAL;
		}
		unsigned short port = ptmp;
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = 0;
		sin.sin_port = htons(port);
		rc = ldms_xprt_listen(x, (struct sockaddr *)&sin, sizeof(sin),
								cb, cb_arg);
	}
	return rc;
}

static void __destroy_rbd(void *v)
{
	struct ldms_rbuf_desc *rbd = v;
	ref_put(&rbd->set->ref, "rbd_set");
	free(rbd);
}

/*
 * If 'x' is not NULL, must be called holding x->lock
 * Cannot be called holding the set->lock
 */
struct ldms_rbuf_desc *
___ldms_alloc_rbd(struct ldms_xprt *x, struct ldms_set *s, enum ldms_rbd_type type,
		  const char *name, const char *func, int line)
{
	zap_err_t zerr;
	struct ldms_rbuf_desc *rbd = calloc(1, sizeof(*rbd));
	if (!rbd)
		goto err0;

	rbd->xprt = ldms_xprt_get(x);
	rbd->set = s;
	ref_get(&s->ref, "rbd_set");
	_ref_init(&rbd->ref, name, __destroy_rbd, rbd, func, line);
	rbn_init(&rbd->xprt_rbn, s);
	size_t set_sz = __ldms_set_size_get(s);
	if (x) {
		zerr = zap_map(x->zap_ep, &rbd->lmap, s->meta, set_sz,
					 ZAP_ACCESS_READ | ZAP_ACCESS_WRITE);
		if (zerr)
			goto err1;
	}
	ref_get(&s->ref, name);
	rbd->type = type;
	/* Add RBD to set list */
	ref_get(&rbd->ref, "set_rbd_list");
	pthread_mutex_lock(&s->lock);
	if (type == LDMS_RBD_LOCAL)
		LIST_INSERT_HEAD(&s->local_rbd_list, rbd, set_link);
	else
		LIST_INSERT_HEAD(&s->remote_rbd_list, rbd, set_link);
	pthread_mutex_unlock(&s->lock);
	if (x) {
		rbt_ins(&x->rbd_rbt, &rbd->xprt_rbn);
		ref_get(&rbd->ref, "xprt_rbd_tree");
	}

	goto out;

err1:
	free(rbd);
	rbd = NULL;
err0:
out:
	return rbd;
}

void __ldms_rbd_xprt_release(struct ldms_rbuf_desc *rbd)
{
	ldms_t xprt = rbd->xprt;
	if (rbd->lmap) {
#ifdef DEBUG
		xprt->log("DEBUG: zap %p: unmap local\n", rbd->xprt->zap_ep);
#endif
		zap_unmap(xprt->zap_ep, rbd->lmap);
		rbd->lmap = NULL;
	}
	if (rbd->rmap) {
#ifdef DEBUG
		xprt->log("DEBUG: zap %p: unmap remote\n", rbd->xprt->zap_ep);
#endif
		zap_unmap(rbd->xprt->zap_ep, rbd->rmap);
		rbd->rmap = NULL;
	}
	rbt_del(&xprt->rbd_rbt, &rbd->xprt_rbn);
	rbd->xprt = NULL;
	ldms_xprt_put(xprt);
	ref_put(&rbd->ref, "xprt_rbd_tree");
}

/*
 * Must be called holding rbd->set->lock
 */
void ___ldms_free_rbd(struct ldms_rbuf_desc *rbd, const char *name, const char *func, int line)
{
	struct ldms_set *set = rbd->set;
	struct ldms_xprt *x = rbd->xprt;
	if (x) {
		pthread_mutex_lock(&x->lock);
		__ldms_rbd_xprt_release(rbd);
		pthread_mutex_unlock(&x->lock);
	}
	if (rbd->push_flags & LDMS_RBD_F_PUSH) {
		_ref_put(&set->ref, "rendezvous_push", func, line);
		_ref_put(&rbd->ref, "rendezvous_push", func, line);
	}
	_ref_put(&set->ref, name, func, line);
	_ref_put(&rbd->ref, name, func, line);
}

static struct ldms_rbuf_desc *ldms_lookup_rbd(struct ldms_xprt *x,
					      struct ldms_set *set)
{
	struct rbn *rbn;
	struct ldms_rbuf_desc *r;
	if (!set)
		return NULL;

	if (rbt_empty(&x->rbd_rbt))
		return NULL;

	rbn = rbt_find(&x->rbd_rbt, set);
	if (rbn) {
		r = RBN_RBD(rbn);
		return r;
	}

	return NULL;
}

extern ldms_set_t ldms_xprt_set_by_name(ldms_t x, const char *set_name)
{
        struct ldms_set *set;
        struct ldms_rbuf_desc *rbd;

	__ldms_set_tree_lock();
	set = __ldms_find_local_set(set_name);
	__ldms_set_tree_unlock();
        if (!set)
                return NULL;
	pthread_mutex_lock(&x->lock);
	rbd = ldms_lookup_rbd(x, set);
	if (rbd)
		ref_get(&rbd->ref, "ldms_xprt_set_by_name");
	pthread_mutex_unlock(&x->lock);
	ref_put(&set->ref, "__ldms_find_local_set");
        return rbd;
}

void __ldms_xprt_term(struct ldms_xprt *x)
{
	/* this is so that we call zap_close() exactly once */
	if (0 != __sync_fetch_and_or(&x->term, 1))
		return;
	zap_close(x->zap_ep);
}

int ldms_xprt_term(int sec)
{
	return zap_term(sec);
}

int ldms_xprt_sockaddr(ldms_t _x, struct sockaddr *local_sa,
		       struct sockaddr *remote_sa,
		       socklen_t *sa_len)
{
	struct ldms_xprt *x = _x;
	zap_err_t zerr;
	zerr = zap_get_name(x->zap_ep, local_sa, remote_sa, sa_len);
	if (zerr)
		return -1;
	return 0;
}

static void __attribute__ ((constructor)) cs_init(void)
{
	pthread_mutex_init(&xprt_list_lock, 0);
	pthread_mutex_init(&ldms_zap_list_lock, 0);
	(void)clock_gettime(CLOCK_REALTIME, &xprt_start);
}

static void __attribute__ ((destructor)) cs_term(void)
{
}
