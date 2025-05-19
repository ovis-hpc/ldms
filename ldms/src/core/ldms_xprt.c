/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2019 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
` * COPYING in the main directory of this source tree, or the BSD-type
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
#define _GNU_SOURCE
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

/* Defined in ldms.c */
extern ovis_log_t xlog;

#define LDMS_XPRT_AUTH_GUARD(x) (((x)->auth_flag != LDMS_XPRT_AUTH_DISABLE) && \
				 ((x)->auth_flag != LDMS_XPRT_AUTH_APPROVED))

#define XPRT_LOG(x, level, fmt, ...) do { \
	ovis_log(xlog, level, fmt, ## __VA_ARGS__); \
} while (0);

/* The definition is in ldms_xprt.c. */
extern int __enable_profiling[LDMS_XPRT_OP_COUNT];

/**
 * zap callback function.
 */
static void ldms_zap_cb(zap_ep_t zep, zap_event_t ev);

/**
 * zap callback function for endpoints that automatically created from accepting
 * connection requests.
 */
void ldms_zap_auto_cb(zap_ep_t zep, zap_event_t ev);

pthread_mutex_t xprt_list_lock;

/* The implementation is is ldms_rail.c. */
struct ldms_op_ctxt_list *
__rail_op_ctxt_list(ldms_t x, enum ldms_xprt_ops_e op_e);

pthread_mutex_t ldms_zap_list_lock;
static struct {
	char *name;
	zap_t zap;
} ldms_zap_tbl[16] = {{0}};
static int ldms_zap_tbl_n = 0;

static char *xprt_event_type_names[] = {
	[LDMS_XPRT_EVENT_CONNECTED] = "CONNECTED",
	[LDMS_XPRT_EVENT_REJECTED] = "REJECTED",
	[LDMS_XPRT_EVENT_ERROR] = "ERROR",
	[LDMS_XPRT_EVENT_DISCONNECTED] = "DISCONNECTED",
	[LDMS_XPRT_EVENT_RECV] = "RECV",
	[LDMS_XPRT_EVENT_SET_DELETE] = "SET_DELETE",
	[LDMS_XPRT_EVENT_SEND_COMPLETE] = "SEND_COMPLETE",
};

const char *ldms_xprt_event_type_to_str(enum ldms_xprt_event_type t)
{
	if (t > LDMS_XPRT_EVENT_LAST)
		return "INVALID";
	return xprt_event_type_names[t];
}

static inline ldms_t __ldms_xprt_get(ldms_t x, const char *name, const char *func, int line)
{
	if (x) {
		_ref_get(&x->ref, name, func, line);
	}
	return x;
}

ldms_t _ldms_xprt_get(ldms_t x, const char *name, const char *func, int line)
{
	return x->ops.get(x, name, func, line);
}

static int __ldms_xprt_is_connected(struct ldms_xprt *x)
{
	assert(x && x->ref.ref_count);
	return (x->disconnected == 0 && x->zap_ep && zap_ep_connected(x->zap_ep));
}

int ldms_xprt_connected(struct ldms_xprt *x)
{
	return x->ops.is_connected(x);
}

LIST_HEAD(xprt_list, ldms_xprt) xprt_list;
ldms_t ldms_xprt_first()
{
	struct ldms_xprt *x = NULL;
	pthread_mutex_lock(&xprt_list_lock);
	x = LIST_FIRST(&xprt_list);
	if (!x)
		goto out;
	ldms_xprt_get(x, "iter_next");	/* next reference */
	x = ldms_xprt_get(x, "iter_caller");	/* caller reference */
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
	ldms_xprt_get(x, "iter_next");	/* next reference */
	x = ldms_xprt_get(x, "iter_caller");	/* caller reference */
 out:
	pthread_mutex_unlock(&xprt_list_lock);
	ldms_xprt_put(prev_x, "iter_next");	/* next reference */
	return x;
}

static void __ldms_xprt_ctxt_set(ldms_t x, void *ctxt, app_ctxt_free_fn fn)
{
	x->app_ctxt = ctxt;
	x->app_ctxt_free_fn = fn;
}

void ldms_xprt_ctxt_set(ldms_t x, void *ctxt, app_ctxt_free_fn fn)
{
	x->ops.ctxt_set(x, ctxt, fn);
}

static void *__ldms_xprt_ctxt_get(ldms_t x)
{
	return x->app_ctxt;
}

void *ldms_xprt_ctxt_get(ldms_t x)
{
	return x->ops.ctxt_get(x);
}

/* Global Transport Statistics */
static uint64_t xprt_connect_count;
static uint64_t xprt_connect_request_count;
static uint64_t xprt_disconnect_count;
static uint64_t xprt_reject_count;
static uint64_t xprt_auth_fail_count;
static struct timespec xprt_start;

static void __xprt_stats_reset(ldms_t _x, int mask, struct timespec *ts);

void ldms_xprt_rate_data(struct ldms_xprt_rate_data *data, int reset)
{
	struct timespec now;
	double dur_s;
	(void)clock_gettime(CLOCK_REALTIME, &now);
	if (data) {
		dur_s = ldms_timespec_diff_s(&xprt_start, &now);
		data->connect_rate_s = (double)xprt_connect_count / dur_s;
		data->connect_request_rate_s = (double)xprt_connect_request_count / dur_s;
		data->disconnect_rate_s = (double)xprt_disconnect_count / dur_s;
		data->reject_rate_s = (double)xprt_reject_count / dur_s;
		data->auth_fail_rate_s = (double)xprt_auth_fail_count / dur_s;
		data->duration = dur_s;
	}
	if (reset) {
		struct ldms_xprt *x;
		pthread_mutex_lock(&xprt_list_lock);
		LIST_FOREACH(x, &xprt_list, xprt_link) {
			__xprt_stats_reset((ldms_t)x, LDMS_PERF_M_STATS, NULL);
		}
		xprt_connect_count = 0;
		xprt_connect_request_count = 0;
		xprt_disconnect_count = 0;
		xprt_reject_count = 0;
		xprt_auth_fail_count = 0;
		(void)clock_gettime(CLOCK_REALTIME, &xprt_start);
		pthread_mutex_unlock(&xprt_list_lock);
	}
}

/* implemented in ldms_rail.c */
ldms_t __ldms_xprt_to_rail(ldms_t x);

int __to_ipv6_addr(struct sockaddr *sa, struct sockaddr_in6 *sin6)
{
	if (sa->sa_family == AF_INET6) {
		memcpy(sin6, (struct sockaddr_in6 *)sa, sizeof(*sin6));
	} else {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = sin->sin_port;
		memset(&sin6->sin6_addr, 0, sizeof(sin6->sin6_addr));
		sin6->sin6_addr.__in6_u.__u6_addr8[10] = 0xff;
		sin6->sin6_addr.__in6_u.__u6_addr8[11] = 0xff;
		memcpy(&sin6->sin6_addr.__in6_u.__u6_addr32[3], &sin->sin_addr, 4);
	}
	return 0;
}

int __is_same_addr(struct sockaddr *a, struct sockaddr *b)
{
	struct sockaddr_in6 sin6_a, sin6_b;

	/* Convert to IPv6 for easier comparison */
	(void)__to_ipv6_addr(a, &sin6_a);
	(void)__to_ipv6_addr(b, &sin6_b);

	if (!(memcmp(&sin6_a.sin6_addr, &sin6_b.sin6_addr, sizeof(struct in6_addr)))
		&& ((sin6_a.sin6_port == 0xffff || sin6_b.sin6_port == 0xffff)
			|| (sin6_a.sin6_port == sin6_b.sin6_port))) {
		return 1;
	}
	return 0;
}

ldms_t ldms_xprt_by_remote_sin(struct sockaddr *sa)
{
	struct sockaddr_storage ss_local, ss_remote;
	socklen_t socklen;
	ldms_t r;

	ldms_t l, next_l;
	l = ldms_xprt_first();
	while (l) {
		if (!l->zap_ep)
			goto next;
		socklen = sizeof(ss_local);
		zap_err_t zerr = zap_get_name(l->zap_ep,
					      (struct sockaddr *)&ss_local,
					      (struct sockaddr *)&ss_remote,
					      &socklen);
		if (zerr)
			goto next;

		if (__is_same_addr(sa, (struct sockaddr *)&ss_remote)) {
			/*
			 * Put the next ref back (taken in ldms_xprt_first()
			 * or ldms_xprt_next()).
			 */
			ldms_xprt_put(l, "iter_next");
			r = __ldms_xprt_to_rail(l);
			ldms_xprt_get(r, "iter_caller");
			/*
			 * Put back the caller reference taken in
			 * ldms_xprt_first() or ldms_xprt_next().
			 *
			 * The rail hold a reference on the ldms_xprt object already.
			 */
			ldms_xprt_put(l, "iter_caller");
			return r;
		}
next:
		next_l = ldms_xprt_next(l);
		ldms_xprt_put(l, "iter_next");
		l = next_l;
	}
	return NULL;
}

/* Must be called with the xprt lock held */
struct ldms_context *__ldms_alloc_ctxt(struct ldms_xprt *x, size_t sz,
		ldms_context_type_t type, ...)
{
	va_list ap;
	struct ldms_context *ctxt;
	va_start(ap, type);
	ctxt = calloc(1, sz);
	if (!ctxt) {
		XPRT_LOG(x, OVIS_LCRITICAL, "%s(): Out of memory\n", __func__);
		return ctxt;
	}
	ctxt->x = ldms_xprt_get(x, "alloc_ctxt");
	(void)clock_gettime(CLOCK_REALTIME, &ctxt->start);
#ifdef CTXT_DEBUG
	XPRT_LOG(x, OVIS_LALWAYS, "%s(): x %p: alloc ctxt %p: type %d\n",
						  __func__, x, ctxt, type);
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
		ref_get(&ctxt->lu_read.s->ref, "__ldms_alloc_ctxt");
		ctxt->lu_read.cb = va_arg(ap, ldms_lookup_cb_t);
		ctxt->lu_read.cb_arg = va_arg(ap, void *);
		ctxt->lu_read.more = va_arg(ap, int);
		ctxt->lu_read.flags = va_arg(ap, enum ldms_lookup_flags);
		break;
	case LDMS_CONTEXT_UPDATE:
	case LDMS_CONTEXT_UPDATE_META:
		ctxt->update.s = va_arg(ap, ldms_set_t);
		ref_get(&ctxt->update.s->ref, "__ldms_alloc_ctxt");
		ctxt->update.cb = va_arg(ap, ldms_update_cb_t);
		ctxt->update.cb_arg = va_arg(ap, void *);
		ctxt->update.idx_from = va_arg(ap, int);
		ctxt->update.idx_to = va_arg(ap, int);
		break;
	case LDMS_CONTEXT_REQ_NOTIFY:
		ctxt->req_notify.s = va_arg(ap, ldms_set_t);
		ref_get(&ctxt->req_notify.s->ref, "__ldms_alloc_ctxt");
		ctxt->req_notify.cb = va_arg(ap, ldms_notify_cb_t);
		ctxt->req_notify.cb_arg = va_arg(ap, void *);
		break;
	case LDMS_CONTEXT_DIR:
		ctxt->dir.cb = va_arg(ap, ldms_dir_cb_t);
		ctxt->dir.cb_arg = va_arg(ap, void *);
		break;
	case LDMS_CONTEXT_SET_DELETE:
		ctxt->set_delete.s = va_arg(ap, ldms_set_t);
		ref_get(&ctxt->set_delete.s->ref, "__ldms_alloc_ctxt");
		ctxt->set_delete.cb = va_arg(ap, ldms_set_delete_cb_t);
		ctxt->set_delete.cb_arg = ctxt;
		break;
	case LDMS_CONTEXT_PUSH:
	case LDMS_CONTEXT_DIR_CANCEL:
	case LDMS_CONTEXT_SEND:
		break;
	}
	va_end(ap);
	return ctxt;
}

/* Must be called with the ldms xprt lock held */
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
		if (ctxt->lu_read.s)
			ref_put(&ctxt->lu_read.s->ref, "__ldms_alloc_ctxt");
		break;
	case LDMS_CONTEXT_UPDATE:
	case LDMS_CONTEXT_UPDATE_META:
		e = &x->stats.ops[LDMS_XPRT_OP_UPDATE];
		if (ctxt->update.s)
			ref_put(&ctxt->update.s->ref, "__ldms_alloc_ctxt");
		break;
	case LDMS_CONTEXT_REQ_NOTIFY:
		if (ctxt->req_notify.s)
			ref_put(&ctxt->req_notify.s->ref, "__ldms_alloc_ctxt");
		break;
	case LDMS_CONTEXT_SET_DELETE:
		e = &x->stats.ops[LDMS_XPRT_OP_SET_DELETE];
		if (ctxt->set_delete.s)
			ref_put(&ctxt->set_delete.s->ref, "__ldms_alloc_ctxt");
		break;
	case LDMS_CONTEXT_DIR:
		break;
	case LDMS_CONTEXT_SEND:
		e = &x->stats.ops[LDMS_XPRT_OP_SEND];
		break;
	case LDMS_CONTEXT_PUSH:
	case LDMS_CONTEXT_DIR_CANCEL:
		break;
	}
	(void)clock_gettime(CLOCK_REALTIME, &x->stats.last_op);
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
	ldms_xprt_put(ctxt->x, "alloc_ctxt");
	free(ctxt);
}

static void send_dir_update(struct ldms_xprt *x,
			    enum ldms_dir_type t,
			    char *json, size_t json_sz)
{
	size_t hdr_len;
	size_t buf_len;
	size_t json_data_len;
	struct ldms_reply *reply = NULL;

	if (!ldms_xprt_connected(x))
		return;
	assert(t != LDMS_DIR_LIST);

	hdr_len = sizeof(struct ldms_reply_hdr)
		+ sizeof(struct ldms_dir_reply);
	json_data_len = strlen("{ \"directory\" : [  ]}") + strlen(json) + 1;
	buf_len = hdr_len + json_data_len;

	if (buf_len >= ldms_xprt_msg_max(x)) {
		XPRT_LOG(x, OVIS_LERROR, "Directory message is too large (%lu) "
				"for the max transport message (%lu).\n",
				buf_len, ldms_xprt_msg_max(x));
		return;
	}

	reply = malloc(buf_len);
	if (!reply) {
		XPRT_LOG(x, OVIS_LCRIT, "Out of memory\n");
		return;
	}

	reply->hdr.xid = x->remote_dir_xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_UPDATE_REPLY);
	reply->hdr.rc = 0;
	reply->dir.more = 0;
	reply->dir.type = htonl(t);
	reply->dir.json_data_len = htonl(json_data_len);
	reply->hdr.len = htonl(buf_len);
	(void)snprintf(reply->dir.json_data, json_data_len,
				"{ \"directory\" : [ %s ]}", json);

	XPRT_LOG(x, OVIS_LDEBUG, "%s(): x %p: remote dir ctxt %p\n",
				__func__, x, (void *)x->remote_dir_xid);

	zap_err_t zerr;
	zerr = zap_send(x->zap_ep, reply, buf_len);
	if (zerr != ZAP_ERR_OK) {
		x->zerrno = zerr;
		XPRT_LOG(x, OVIS_LERROR, "%s: x %p: "
				"zap_send synchronously error. '%s'\n",
				__func__, x, zap_err_str(zerr));
		ldms_xprt_close(x);
	}
	free(reply);
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

	if (!ldms_xprt_connected(x))
		return;

	len = sizeof(struct ldms_reply_hdr) + e->len;
	reply = malloc(len);
	if (!reply) {
		XPRT_LOG(x, OVIS_LCRITICAL, "Memory allocation failure "
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
		XPRT_LOG(x, OVIS_LERROR, "%s. zap_send synchronously error. '%s'\n",
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
	if (!json_buf)
		return NULL;
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
	char *json_buf = NULL;
	size_t json_cnt;
	struct ldms_xprt *x;

	pthread_mutex_lock(&xprt_list_lock);
	LIST_FOREACH(x, &xprt_list, xprt_link) {
		if (x->remote_dir_xid) {
			if (!json_buf) {
				/* Only build the JSON resonse if there is a
				 * transport that has registered for updates */
				json_buf = __ldms_format_set_for_dir(set, &json_cnt);
				if (!json_buf) {
					XPRT_LOG(x, OVIS_LCRIT, "%s: memory allocation error\n", __func__);
					goto out;
				}
			}
			send_dir_update(x, t, json_buf, json_cnt);
		}
	}
	free(json_buf);
out:
	pthread_mutex_unlock(&xprt_list_lock);
}

void __ldms_dir_add_set(struct ldms_set *set)
{
	dir_update(set, LDMS_DIR_ADD);
}

static void __set_delete_cb(ldms_t xprt, int status, ldms_set_t rbd, void *cb_arg)
{
	struct ldms_context *ctxt = cb_arg;
	struct ldms_set *set = ctxt->set_delete.s;
	/* If the set was successfully looked up, it will be be put into
	 * `x->set_coll` and a reference taken. So, we have to put back the
	 * reference only in this case. If the set was not in the `x->set_coll`,
	 * the `ctxt->set_delete.lookup` will be 0 and we must not put the
	 * reference we have not taken. */
	if (ctxt->set_delete.lookup)
		ref_put(&set->ref, "xprt_set_coll");
}

/* implementation in ldms_rail.c */
void __rail_on_set_delete(ldms_t _r, struct ldms_set *s,
			      ldms_set_delete_cb_t cb_fn);

void __ldms_dir_del_set(struct ldms_set *set)
{
	/*
	 * LDMS versions >= 4.3.4 do not send LDMS_DIR_DEL, instead
	 * they use the two way handshake provided by
	 * __rail_on_set_delete() (previously __xprt_set_delete() before rail)
	 * to inform the peer and receive acknowledgment of the set's disuse.
	 *
	 * We still handle LDMS_DIR_DEL and pass it to the application
	 * so that it can interoperate with compute nodes that are
	 * older than 4.3.4.
	 *
	 * dir_update(set, LDMS_DIR_DEL);
	 */
	struct ldms_xprt *x;
	ldms_t r;
	pthread_mutex_lock(&xprt_list_lock);
	LIST_FOREACH(x, &xprt_list, xprt_link) {
		if (x->remote_dir_xid) {
			/* NOTE:
			 * There will be only one `x` in `r` that has
			 * `x->remote_dir_xid != 0`.
			 */
			r = __ldms_xprt_to_rail(x);
			__rail_on_set_delete(r, set, __set_delete_cb);
		}
	}
	pthread_mutex_unlock(&xprt_list_lock);
}

void __ldms_dir_upd_set(struct ldms_set *set)
{
	dir_update(set, LDMS_DIR_UPD);
}

static void __ldms_xprt_close(ldms_t x)
{
	XPRT_LOG(x, OVIS_LDEBUG, "%s(): closing x %p\n", __func__, x);
	x->remote_dir_xid = 0;
	__ldms_xprt_term(x);
}

void ldms_xprt_close(ldms_t x)
{
	return x->ops.close(x);
}

void __ldms_xprt_resource_free(struct ldms_xprt *x)
{
	int drop_ep_ref = 0;
	pthread_mutex_lock(&x->lock);
	x->remote_dir_xid = x->local_dir_xid = 0;

#ifdef DEBUG
	XPRT_LOG(x, OVIS_LALWAYS, "xprt_resource_free. zap %p: active_dir = %d.\n",
							 x->zap_ep, x->active_dir);
	XPRT_LOG(x, OVIS_LALWAYS, "xprt_resource_free. zap %p: active_lookup = %d.\n",
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
		case LDMS_CONTEXT_LOOKUP_REQ:
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

	if (x->auth) {
		ldms_auth_free(x->auth);
		x->auth = NULL;
	}
	if (x->zap_ep) {
		zap_set_ucontext(x->zap_ep, NULL);
		zap_free(x->zap_ep);
		x->zap_ep = NULL;
		drop_ep_ref = 1;
	}
	pthread_mutex_unlock(&x->lock);
	if (drop_ep_ref)
		ldms_xprt_put(x, "zap_uctxt");
}

static void __xprt_ref_free(void *arg)
{
	ldms_t x = (ldms_t)arg;

	pthread_mutex_lock(&xprt_list_lock);
	LIST_REMOVE(x, xprt_link);
	pthread_mutex_unlock(&xprt_list_lock);
	__ldms_xprt_resource_free(x);
	sem_destroy(&x->sem);
	if (x->app_ctxt && x->app_ctxt_free_fn)
		x->app_ctxt_free_fn(x->app_ctxt);
	free(x);
}

static inline void __ldms_xprt_put(ldms_t x, const char *name, const char *func, int line)
{
	_ref_put(&x->ref, name, func, line);
}

void _ldms_xprt_put(ldms_t x, const char *name, const char *func, int line)
{
	x->ops.put(x, name, func, line);
}

/* The implementations are in ldms_rail.c. */
extern void timespec_hton(struct timespec *ts);
extern void timespec_ntoh(struct timespec *ts);

static void process_set_delete_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct ldms_reply reply;
	struct ldms_set *set;
	size_t len;

	/*
	 * Always notify the application about peer set delete. If we happened
	 * not to have the set yet, `event.set_delete.set` will be NULL.
	 */
	__ldms_set_tree_lock();
	set = __ldms_find_local_set(req->set_delete.inst_name);
	__ldms_set_tree_unlock();
	if (set) {
		if (set->xprt != x) {
			assert(set->xprt != x);
			goto reply_1;
		}
	}
	if (x->event_cb) {
		struct ldms_xprt_event event;
		event.type = LDMS_XPRT_EVENT_SET_DELETE;
		event.set_delete.set = set;
		event.set_delete.name = req->set_delete.inst_name;
		event.data_len = sizeof(ldms_set_t);
		x->event_cb(x, &event, x->event_cb_arg);
	}
 reply_1:
	if (set)
		ref_put(&set->ref, "__ldms_find_local_set");
	/* Initialize the reply header */
	reply.hdr.xid = req->hdr.xid;
	reply.hdr.cmd = htonl(LDMS_CMD_SET_DELETE_REPLY);
	reply.hdr.rc = 0;
	len = sizeof(reply.hdr) + sizeof(reply.set_del);
	reply.hdr.len = htonl(len);
	(void)clock_gettime(CLOCK_REALTIME, &reply.set_del.recv_ts);
	timespec_hton(&reply.set_del.recv_ts);
	zap_err_t zerr = zap_send(x->zap_ep, &reply, len);
	if (zerr != ZAP_ERR_OK) {
		x->zerrno = zerr;
		XPRT_LOG(x, OVIS_LERROR, "%s: zap_send synchronously error. "
				"'%s'\n", __FUNCTION__, zap_err_str(zerr));
	}
}

static
void process_set_delete_reply(struct ldms_xprt *x, struct ldms_reply *reply,
			      struct ldms_context *ctxt)
{
	if (ENABLED_PROFILING(LDMS_XPRT_OP_SET_DELETE)) {
		struct ldms_thrstat *thrstat = zap_thrstat_ctxt_get(x->zap_ep);
		memcpy(&ctxt->op_ctxt->set_del_profile.ack_ts, &thrstat->last_op_start, sizeof(struct timespec));
		timespec_ntoh(&reply->set_del.recv_ts);
		memcpy(&ctxt->op_ctxt->set_del_profile.recv_ts, &reply->set_del.recv_ts, sizeof(struct timespec));
	}
	ctxt->set_delete.cb(x, reply->hdr.rc, ctxt->set_delete.s, ctxt->set_delete.cb_arg);
	pthread_mutex_lock(&x->lock);
	__ldms_free_ctxt(x, ctxt);
	pthread_mutex_unlock(&x->lock);
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
	ldms_stats_entry_t e = &x->stats.ops[LDMS_XPRT_OP_DIR_REP];
	int64_t dur_us;
	struct timespec end, start;

	(void)clock_gettime(CLOCK_REALTIME, &start);

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
			XPRT_LOG(x, OVIS_LERROR,
				 "%s: x %p: zap_send synchronous error. '%s'\n",
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
			pthread_mutex_lock(&set->lock);
			cnt += __ldms_format_set_meta_as_json(set, last_cnt,
						      &reply->dir.json_data[cnt],
						      len - hdrlen - cnt - 3 /* ]}\0 */);
			pthread_mutex_unlock(&set->lock);
		} else {
			ovis_log(xlog, OVIS_LINFO,
				"Access %o denied to user %d:%d for set '%s'.\n",
				perm, uid, gid, name->name);
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
				XPRT_LOG(x, OVIS_LERROR, "%s: x %p: "
					"zap_send synchronous error. '%s'\n",
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
				XPRT_LOG(x, OVIS_LERROR, "%s: x %p: "
					"zap_send synchronous error. '%s'\n",
				       __FUNCTION__, x, zap_err_str(zerr));
			}
		}
		ref_put(&set->ref, "__ldms_find_local_set");
	}
	free(reply);
	__ldms_empty_name_list(&name_list);
	(void)clock_gettime(CLOCK_REALTIME, &end);
	dur_us = ldms_timespec_diff_us(&start, &end);
	if (e->min_us > dur_us)
		e->min_us = dur_us;
	if (e->max_us < dur_us)
		e->max_us = dur_us;
	e->total_us += dur_us;
	e->mean_us = (e->count * e->mean_us) + dur_us;
	e->count += 1;
	e->mean_us /= e->count;
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
		XPRT_LOG(x, OVIS_LERROR, "%s: zap_send synchronously error."
				" '%s'\n", __FUNCTION__, zap_err_str(zerr));
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
		XPRT_LOG(x, OVIS_LERROR, "%s: zap_send synchronously error. "
				"'%s'\n", __FUNCTION__, zap_err_str(zerr));
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

static void
process_req_notify_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct rbn *rbn;
	struct ldms_lookup_peer *np;
	struct xprt_set_coll_entry *ent;
	ldms_set_t set = __ldms_set_by_id(req->req_notify.set_id);
	if (!set)
		return;
	/* enlist or update the peer */
	pthread_mutex_lock(&set->lock);
	rbn = rbt_find(&set->lookup_coll, x);
	if (rbn) {
		/* entry existed: updates and returns */
		np = container_of(rbn, struct ldms_lookup_peer, rbn);
		np->remote_notify_xid = req->hdr.xid;
		np->notify_flags = ntohl(req->req_notify.flags);
		pthread_mutex_unlock(&set->lock);
		return;
	}
	np = calloc(1, sizeof(*np));
	if (!np) {
		XPRT_LOG(x, OVIS_LCRITICAL, "%s:%s:%d Not enough memory\n",
				__FILE__, __func__, __LINE__);
		pthread_mutex_unlock(&set->lock);
		return;
	}
	rbn_init(&np->rbn, x);
	np->xprt = ldms_xprt_get(x, "notify_peer");
	rbt_ins(&set->lookup_coll, &np->rbn);
	np->remote_notify_xid = req->hdr.xid;
	np->notify_flags = ntohl(req->req_notify.flags);
	pthread_mutex_unlock(&set->lock);

	/* also enlist the set to xprt for xprt_term notification */
	pthread_mutex_lock(&x->lock);
	rbn = rbt_find(&x->set_coll, set);
	if (!rbn) {
		ent = calloc(1, sizeof(*ent));
		if (!ent) {
			XPRT_LOG(x, OVIS_LCRITICAL,
					"%s:%s:%d Not enough memory\n",
					__FILE__, __func__, __LINE__);
			pthread_mutex_unlock(&x->lock);
			return;
		}
		rbn_init(&ent->rbn, set);
		ent->set = set;
		ref_get(&set->ref, "xprt_set_coll");
		rbt_ins(&x->set_coll, &ent->rbn);
	}
	pthread_mutex_unlock(&x->lock);
}

static void
process_cancel_notify_request(struct ldms_xprt *x, struct ldms_request *req)
{
	ldms_set_t set = __ldms_set_by_id(req->cancel_notify.set_id);
	struct rbn *rbn;
	struct ldms_lookup_peer *np;
	if (!set)
		return;
	pthread_mutex_lock(&set->lock);
	rbn = rbt_find(&set->lookup_coll, x);
	if (rbn) {
		np = container_of(rbn, struct ldms_lookup_peer, rbn);
		rbt_del(&set->lookup_coll, rbn);
	} else {
		np = NULL;
	}
	pthread_mutex_unlock(&set->lock);
	if (np) {
		ldms_xprt_put(np->xprt, "notify_peer");
		free(np);
	}
}

extern struct ldms_set *__ldms_set_by_id(uint64_t id);
static void
process_cancel_push_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct rbn *rbn;
	struct ldms_push_peer *pp;
	struct ldms_reply reply;
	size_t len;
	ldms_set_t set = __ldms_set_by_id(req->cancel_push.set_id);
	if (!set)
		return;
	pthread_mutex_lock(&set->lock);
	rbn = rbt_find(&set->push_coll, x);
	if (rbn) {
		pp = container_of(rbn, struct ldms_push_peer, rbn);
		rbt_del(&set->push_coll, rbn);
	} else {
		pp = NULL;
	}
	pthread_mutex_unlock(&set->lock);
	if (!pp)
		return; /* nothing to do */
	len = sizeof(struct ldms_reply_hdr) + sizeof(struct ldms_push_reply);
	reply.hdr.xid = 0;
	reply.hdr.cmd = htonl(LDMS_CMD_PUSH_REPLY);
	reply.hdr.len = htonl(len);
	reply.hdr.rc = 0;
	reply.push.set_id = pp->remote_set_id;
	reply.push.data_len = 0;
	reply.push.data_off = 0;
	reply.push.flags = htonl(LDMS_UPD_F_PUSH | LDMS_UPD_F_PUSH_LAST);
	(void)zap_send(x->zap_ep, &reply, len);

	ldms_xprt_put(pp->xprt, "push_set");
	free(pp);

	return;
}

static void *__copy_set_info_to_lookup_msg(char *buffer, ldms_name_t schema,
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
	return (void*)str + sizeof(str->len);
}

/* Caller should hold the set lock */
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

static int __add_lookup_peer(struct ldms_xprt *x, struct ldms_set *set)
{
	int rc;
	struct rbn *rbn;
	struct ldms_lookup_peer *lp;
	struct xprt_set_coll_entry *ent;

	/* Add the peer to the set's lookup collection */
	pthread_mutex_lock(&set->lock);
	rbn = rbt_find(&set->lookup_coll, x);
	if (rbn) {
		/*
		 * The peer already looked up the set.
		 * Nothing else to do here.
		 *
		 * On the peer side, handle_rendezvous_lookup() handles the issue
		 * that the set has already existed in the peer's set tree.
		 */
		pthread_mutex_unlock(&set->lock);
		return 0;
	}

	lp = calloc(1, sizeof(*lp));
	if (!lp) {
		pthread_mutex_unlock(&set->lock);
		XPRT_LOG(x, OVIS_LCRITICAL,
				"%s:%s:%d Not enough memory\n",
				__FILE__, __func__, __LINE__);
		return ENOMEM;
	}
	lp->xprt = ldms_xprt_get(x, "lookup_peer");
	rbn_init(&lp->rbn, x);
	rbt_ins(&set->lookup_coll, &lp->rbn);
	pthread_mutex_unlock(&set->lock);

	/* Add the set to the transport's set collection */
	pthread_mutex_lock(&x->lock);
	rbn = rbt_find(&x->set_coll, set);
	if (rbn) {
		/*
		 * Reaching here means the peer has not looked up the set yet.
		 * It is impossible that the set is in the transport set collection.
		 */
		assert(rbn);
		pthread_mutex_unlock(&x->lock);
		return 0; /* Share the map, let the peer handles it */
	}

	ent = calloc(1, sizeof(*ent));
	if (!ent) {
		XPRT_LOG(x, OVIS_LCRITICAL,
				"%s:%s:%d Not enough memory\n",
				__FILE__, __func__, __LINE__);
		pthread_mutex_unlock(&x->lock);
		rc = ENOMEM;
		goto remove_peer;
	}
	/* Put back when the peer sent the set_delete ack */
	ref_get(&set->ref, "xprt_set_coll");
	rbn_init(&ent->rbn, set);
	ent->set = set;
	rbt_ins(&x->set_coll, &ent->rbn);
	pthread_mutex_unlock(&x->lock);
	return 0;

remove_peer:
	pthread_mutex_lock(&set->lock);
	rbt_del(&set->lookup_coll, &lp->rbn);
	pthread_mutex_unlock(&set->lock);
	ldms_xprt_put(lp->xprt, "lookup_peer");
	free(lp);
	return rc;
}

#define LU_PARAM_PRFL_MARKER "lu_prflng"
static int __send_lookup_reply(struct ldms_xprt *x, struct ldms_set *set,
			       uint64_t xid, int more)
{
	int rc = 0;
	if (!set)
		return ENOENT;

	ldms_name_t name = get_instance_name(set->meta);
	ldms_name_t schema = get_schema_name(set->meta);
	struct ldms_thrstat *thrstat = zap_thrstat_ctxt_get(x->zap_ep);
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
	 * | 0                         |
	 * +---------------------------+
	 * | LU_PARAM_PRFL_MARKER      |
	 * +---------------------------+
	 * | struct timespec           |
	 * | (request receiving ts)    |
	 * +---------------------------+
	 * | struct timespec           |
	 * | (sharing ts)              |
	 * +---------------------------+
	 */
	int set_info_cnt;
	size_t set_info_len;
	size_t msg_len;
	struct ldms_rendezvous_msg *msg;
	struct timespec *req_recv_ts, *share_ts;
	char *prfl_marker;
	size_t prfl_marker_len = strlen(LU_PARAM_PRFL_MARKER) + 1;

	pthread_mutex_lock(&set->lock);
	__get_set_info_sz(set, &set_info_cnt, &set_info_len);
	msg_len = sizeof(struct ldms_rendezvous_hdr)
			+ sizeof(struct ldms_rendezvous_lookup_param)
			/*
			 * +2 for schema name and instance name
			 * +1 for the terminating string of length 0
			 */
			+ sizeof(struct ldms_name) * (2 + (set_info_cnt) * 2 + 1)
			+ name->len + schema->len + set_info_len
			/*
			 * Encode the request receiving timestamp
			 * and the sharing timestamp
			 */
			+ prfl_marker_len + sizeof(struct timespec) * 2;

	msg = calloc(1, msg_len);
	if (!msg) {
		pthread_mutex_unlock(&set->lock);
		return ENOMEM;
	}
	prfl_marker = __copy_set_info_to_lookup_msg(msg->lookup.set_info, schema, name, set);
	/* Embed the profiling timestamps in the lookup reply message */
	strcpy(prfl_marker, LU_PARAM_PRFL_MARKER);
	req_recv_ts = (struct timespec *)(prfl_marker + prfl_marker_len);
	memcpy(req_recv_ts, &thrstat->last_op_start, sizeof(struct timespec));
	share_ts = req_recv_ts+1;
	(void)clock_gettime(CLOCK_REALTIME, share_ts);
	/* Fill the set details */
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
	XPRT_LOG(x, OVIS_LDEBUG, "%s(): x %p: sharing ... remote lookup ctxt %p\n",
							   __func__, x, (void *)xid);
	zap_err_t zerr = zap_share(x->zap_ep, set->lmap, (const char *)msg, msg_len);
	if (zerr != ZAP_ERR_OK) {
		x->zerrno = zerr;
		rc = zap_zerr2errno(zerr);
		XPRT_LOG(x, OVIS_LERROR, "%s: x %p: "
				"zap_share synchronously error. '%s'\n",
				__FUNCTION__, x, zap_err_str(zerr));
	} else {
		rc = __add_lookup_peer(x, set);
	}
	free(msg);
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
			XPRT_LOG(x, OVIS_LERROR, "%s", errstr);
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
		XPRT_LOG(x, OVIS_LERROR, "%s: x %p: "
			"zap_send synchronously failed with '%s' "
			"while trying to send local error code %d (%s)\n",
			__func__, x, zap_err_str(zerr), rc, STRERROR(rc));
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
	struct ldms_context *ctxt;
	int rc;
	uint32_t len = __le32_to_cpu(s->meta->meta_sz)
			+ __le32_to_cpu(s->meta->data_sz);

	ctxt = __ldms_alloc_ctxt(x, sizeof(*ctxt), LDMS_CONTEXT_UPDATE,
				 s, cb, arg, 0, 0);
	if (!ctxt) {
		rc = ENOMEM;
		goto out;
	}
	assert(x == ctxt->x);
	if (ENABLED_PROFILING(LDMS_XPRT_OP_UPDATE)) {
		ctxt->op_ctxt = s->curr_updt_ctxt;
		if (0 == ctxt->op_ctxt->update_profile.read_ts.tv_sec) {
			/*
			 * If the data read timestamp is not set,
			 * record the current time as the start of the read operation.
			 *
			 * The read operation may involve reading the entire set at once,
			 * reading the meta followed by data,
			 * or reading multiple times to obtain the updated copy of the set.
			 */
			(void)clock_gettime(CLOCK_REALTIME, &ctxt->op_ctxt->update_profile.read_ts);
		} else {
			/*
			 * Continue reading the set. The read operation has already started.
			 */
		}
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
	struct ldms_context *ctxt;
	int rc;
	uint32_t meta_sz = __le32_to_cpu(s->meta->meta_sz);

	ctxt = __ldms_alloc_ctxt(x, sizeof(*ctxt), LDMS_CONTEXT_UPDATE_META,
							s, cb, arg, 0, 0);
	if (!ctxt) {
		rc = ENOMEM;
		goto out;
	}
	assert(x == ctxt->x);
	if (ENABLED_PROFILING(LDMS_XPRT_OP_UPDATE)) {
		ctxt->op_ctxt = s->curr_updt_ctxt;
		if (0 == ctxt->op_ctxt->update_profile.read_ts.tv_sec) {
			/*
			 * If the data read timestamp is not set,
			 * record the current time as the start of the read operation.
			 */
			(void)clock_gettime(CLOCK_REALTIME, &ctxt->op_ctxt->update_profile.read_ts);
		} else {
			/*
			 * Continue reading the set. The read operation has already started.
			 */
		}
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

	ctxt = __ldms_alloc_ctxt(x, sizeof(*ctxt), LDMS_CONTEXT_UPDATE,
						s, cb, arg, idx_from, idx_to);
	if (!ctxt) {
		rc = ENOMEM;
		goto out;
	}
	data_sz = __le32_to_cpu(s->meta->data_sz);
	doff = (uint8_t *)s->data_array - (uint8_t *)s->meta
							+ idx_from * data_sz;
	dlen = (idx_to - idx_from + 1) * data_sz;

	assert(x == ctxt->x);
	if (ENABLED_PROFILING(LDMS_XPRT_OP_UPDATE)) {
		ctxt->op_ctxt = s->curr_updt_ctxt;
		if (0 == ctxt->op_ctxt->update_profile.read_ts.tv_sec) {
			/*
			 * If the data read timestamp is not set,
			 * record the current time as the start of the read operation.
			 */
			(void)clock_gettime(CLOCK_REALTIME, &ctxt->op_ctxt->update_profile.read_ts);
		} else {
			/*
			 * Continue reading the set. The read operation has already started.
			 */
		}
	}
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
	if (!ldms_xprt_connected(x))
		return ENOTCONN;

	if (!s->lmap || !s->rmap)
		return EINVAL;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return EPERM;
	int rc;
	uint32_t meta_meta_gn = __le32_to_cpu(s->meta->meta_gn);
	uint32_t data_meta_gn = __le32_to_cpu(s->data->meta_gn);
	uint32_t n = __le32_to_cpu(s->meta->array_card);
	if (n == 0) {
		XPRT_LOG(x, OVIS_LINFO, "%s: Set %s has 0 cardinality\n",
				__func__, ldms_set_instance_name_get(s));
		return EINVAL;
	}
	int idx_from, idx_to, idx_next, idx_curr;
	zap_get_ep(x->zap_ep, "ldms_xprt:set_update", __func__, __LINE__);		/* Released in handle_zap_read_complete() */
	if (meta_meta_gn == 0 || meta_meta_gn != data_meta_gn) {
		if (s->curr_idx == (n-1)) {
			/* We can update the metadata along with the data */
			rc = do_read_all(x, s, cb, arg);
		} else {
			/* Otherwise, need to update metadata and data
			 * separately */
			rc = do_read_meta(x, s, cb, arg);
		}
	} else {
		idx_from = (s->curr_idx + 1) % n;
		idx_curr = __le32_to_cpu(s->data->curr_idx);
		idx_next = (idx_curr + 1) % n;
		if (idx_next == idx_from)
			idx_to = idx_next;
		else
			idx_to = (idx_curr < idx_from)?(n - 1):(idx_curr);
		rc = do_read_data(x, s, idx_from, idx_to, cb, arg);
	}
	if (rc) {
		zap_put_ep(x->zap_ep, "ldms_xprt:set_update", __func__, __LINE__);
	}
	return rc;
}

void __rail_process_send_quota(ldms_t x, struct ldms_request *req);
void __rail_process_quota_reconfig(ldms_t x, struct ldms_request *req);
void __rail_process_rate_reconfig(ldms_t x, struct ldms_request *req);

/* implementation is in ldms_msg.c */
void __msg_req_recv(ldms_t x, int cmd, struct ldms_request *req);
void __qgroup_req_recv(ldms_t x, int cmd, struct ldms_request *req);

enum ldms_thrstat_op_e req2thrstat_op_tbl[];
static
int ldms_xprt_recv_request(struct ldms_xprt *x, struct ldms_request *req)
{
	int cmd = ntohl(req->hdr.cmd);
	struct ldms_thrstat *thrstat = zap_thrstat_ctxt_get(x->zap_ep);
	int rc;

	thrstat->last_op = req2thrstat_op_tbl[cmd];
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
	case LDMS_CMD_SEND_QUOTA:
		__rail_process_send_quota(x, req);
		break;
	case LDMS_CMD_MSG:
	case LDMS_CMD_MSG_SUB:
	case LDMS_CMD_MSG_UNSUB:
		__msg_req_recv(x, cmd, req);
		break;
	case LDMS_CMD_QGROUP_ASK:
	case LDMS_CMD_QGROUP_DONATE:
	case LDMS_CMD_QGROUP_DONATE_BACK:
		__qgroup_req_recv(x, cmd, req);
		break;
	case LDMS_CMD_QUOTA_RECONFIG:
		__rail_process_quota_reconfig(x, req);
		break;
	case LDMS_CMD_RATE_RECONFIG:
		__rail_process_rate_reconfig(x, req);
		break;
	default:
		XPRT_LOG(x, OVIS_LERROR, "Unrecognized request %d\n", cmd);
		assert(0 == "Unrecognized LDMS_CMD request type");
	}
	return 0;
}

static
void process_lookup_reply(struct ldms_xprt *x, struct ldms_reply *reply,
			  struct ldms_context *ctxt)
{
	struct ldms_thrstat *thrstat;

	thrstat = zap_thrstat_ctxt_get(x->zap_ep);
	memcpy(&ctxt->op_ctxt->lookup_profile.complete_ts, &thrstat->last_op_start,
					    sizeof(struct timespec));

	int rc = ntohl(reply->hdr.rc);
	if (!rc) {
		/* A peer should only receive error in lookup_reply.
		 * A successful lookup is handled by rendezvous. */
		XPRT_LOG(x, OVIS_LWARN, "Receive lookup reply error with rc: 0\n");
		goto out;
	}
	if (ctxt->lu_req.cb)
		ctxt->lu_req.cb(x, rc, 0, NULL, ctxt->lu_req.cb_arg);

out:
	pthread_mutex_lock(&x->lock);
#ifdef DEBUG
	assert(x->active_lookup);
	x->active_lookup--;
	XPRT_LOG(x, OVIS_LALWAYS, "lookup_reply: put ref %p: active_lookup = %d\n",
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
}

static int __process_dir_set_info(struct ldms_set *lset, enum ldms_dir_type type,
				ldms_dir_set_t dset, json_entity_t info_list)
{
	json_entity_t info_entity, k, v;
	int j, rc = 0  ;
	int dir_upd = 0;
	struct ldms_set_info_pair *pair, *nxt_pair;

	if (lset)
		pthread_mutex_lock(&lset->lock);
	for (j = 0, info_entity = json_item_first(info_list); info_entity;
	     info_entity = json_item_next(info_entity), j++) {
		k = json_value_find(info_entity, "key");
		v = json_value_find(info_entity, "value");
		char *nkey = strdup(json_value_str(k)->str);
		char *nvalue = strdup(json_value_str(v)->str);
		if (!nkey || !nvalue) {
			free(nkey);
			free(nvalue);
			rc = ENOMEM;
			goto out;
		}
		dset->info[j].key = nkey;
		dset->info[j].value = nvalue;

		if (lset) {
			const char *key = dset->info[j].key;
			const char *val = dset->info[j].value;
			rc = __ldms_set_info_set(&lset->remote_info, key, val);
			if (rc > 0)
				goto out;
			else if (rc == 0)
				dir_upd = 1;
			else
				rc = 0; /* no change */
		}
	}
	if (!lset)
		return 0;

	pair = LIST_FIRST(&lset->remote_info);
	while (pair) {
		nxt_pair = LIST_NEXT(pair, entry);
		for (j = 0, info_entity = json_item_first(info_list); info_entity;
				info_entity = json_item_next(info_entity)) {
			k = json_value_find(info_entity, "key");
			if (0 == strcmp(pair->key, json_value_str(k)->str))
				break;
		}
		if (!info_entity) {
			__ldms_set_info_unset(pair);
			dir_upd = 1;
		}
		pair = nxt_pair;
	}
out:
	if (lset) {
		pthread_mutex_unlock(&lset->lock);
		if (!rc) {
			if ((type == LDMS_DIR_UPD) && dir_upd &&
					(lset->flags & LDMS_SET_F_PUBLISHED)) {
				__ldms_dir_upd_set(lset);
			}
		}
	}
	return rc;
}

static
void __process_dir_reply(struct ldms_xprt *x, struct ldms_reply *reply,
		       struct ldms_context *ctxt, int more)
{
	enum ldms_dir_type type = ntohl(reply->dir.type);
	int i, rc = ntohl(reply->hdr.rc);
	size_t count, json_data_len;
	ldms_dir_t dir = NULL;
	json_parser_t p = NULL;
	json_entity_t dir_attr, dir_list, set_entity, info_list;
	json_entity_t dir_entity = NULL;
	struct ldms_set *lset;
	ldms_stats_entry_t e = &x->stats.ops[LDMS_XPRT_OP_DIR_REQ];
	int64_t dur_us;
	struct timespec end, start;

	json_data_len = ntohl(reply->hdr.len) - sizeof(struct ldms_reply_hdr)
				- sizeof(struct ldms_dir_reply);

	if (!ctxt->dir.cb)
		return;

	if (rc)
		goto out;

	(void)clock_gettime(CLOCK_REALTIME, &start);

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

		e = json_value_find(set_entity, "digest");
		if (e)
			dir->set_data[i].digest_str = strdup(json_value_str(e)->str);
		else
			dir->set_data[i].digest_str = strdup("");

		e = json_value_find(set_entity, "flags");
		dir->set_data[i].flags = strdup(json_value_str(e)->str);

		e = json_value_find(set_entity, "meta_size");
		dir->set_data[i].meta_size = json_value_int(e);

		e = json_value_find(set_entity, "data_size");
		dir->set_data[i].data_size = json_value_int(e);

		e = json_value_find(set_entity, "heap_size");
		if (e)
			dir->set_data[i].heap_size = json_value_int(e);
		else
			dir->set_data[i].heap_size = 0;

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
	(void)clock_gettime(CLOCK_REALTIME, &end);
	dur_us = ldms_timespec_diff_us(&start, &end);
	if (e->min_us > dur_us)
		e->min_us = dur_us;
	if (e->max_us < dur_us)
		e->max_us = dur_us;
	e->total_us += dur_us;
	e->mean_us = (e->count * e->mean_us) + dur_us;
	e->count += 1;
	e->mean_us /= e->count;
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
#ifdef DEBUG
		assert(x->active_dir);
		x->active_dir--;
		XPRT_LOG(x, OVIS_LALWAYS, "dir_reply: put ref %p. active_dir = %d.\n",
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
	uint32_t data_off = ntohl(reply->push.data_off);
	uint32_t data_len = ntohl(reply->push.data_len);
	int rc;
	ldms_set_t set;

	set = __ldms_set_by_id(reply->push.set_id);
	if (!set) {
		XPRT_LOG(x, OVIS_LERROR, "%s: set_id %ld not found\n", __func__, reply->push.set_id);
		return;
	}
	rc = __xprt_set_access_check(x, set, LDMS_ACCESS_WRITE);
	if (rc)
		return; /* NOTE should we terminate the xprt? */

	/* Copy the data to the metric set */
	if (data_len) {
		memcpy((char *)set->meta + data_off,
					reply->push.data, data_len);
	}
	if (set->push_cb &&
		(0 == (ntohl(reply->push.flags) & LDMS_CMD_PUSH_REPLY_F_MORE))) {
		set->push_cb(x, set, ntohl(reply->push.flags), set->push_cb_arg);
	}
}

void ldms_xprt_dir_free(ldms_t t, ldms_dir_t dir)
{
	(void)t;
	if (!dir)
		return;
	int i, j;
	for (i = 0; i < dir->set_count; i++) {
		free(dir->set_data[i].inst_name);
		free(dir->set_data[i].schema_name);
		free(dir->set_data[i].flags);
		free(dir->set_data[i].digest_str);
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

/* implementation is in ldms_msg.c */
int __msg_reply_recv(ldms_t x, int cmd, struct ldms_reply *reply);

static int ldms_xprt_recv_reply(struct ldms_xprt *x, struct ldms_reply *reply)
{
	int cmd = ntohl(reply->hdr.cmd);
	uint64_t xid = reply->hdr.xid;
	struct ldms_context *ctxt;
	struct ldms_thrstat *thrstat;
	ctxt = (struct ldms_context *)(unsigned long)xid;
	thrstat = zap_thrstat_ctxt_get(x->zap_ep);
	thrstat->last_op = req2thrstat_op_tbl[cmd];
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
	case LDMS_CMD_MSG_SUB_REPLY:
	case LDMS_CMD_MSG_UNSUB_REPLY:
		__msg_reply_recv(x, cmd, reply);
		break;
	default:
		XPRT_LOG(x, OVIS_LERROR, "Unrecognized reply %d\n", cmd);
	}
	return 0;
}

static int recv_cb(struct ldms_xprt *x, void *r)
{
	int rc;
	struct ldms_request_hdr *h = r;
	int64_t dur_us;
	struct timespec start, end;
	ldms_stats_entry_t e = NULL;
	(void)clock_gettime(CLOCK_REALTIME, &start);
	e = &x->stats.ops[LDMS_XPRT_OP_RECV];

	int cmd = ntohl(h->cmd);
	if (cmd > LDMS_CMD_REPLY)
		rc = ldms_xprt_recv_reply(x, r);
	else
		rc = ldms_xprt_recv_request(x, r);

	(void)clock_gettime(CLOCK_REALTIME, &end);
	dur_us = ldms_timespec_diff_us(&start, &end);
	(void)clock_gettime(CLOCK_REALTIME, &x->stats.last_op);
	if (e->min_us > dur_us)
		e->min_us = dur_us;
	if (e->max_us < dur_us)
		e->max_us = dur_us;
	e->total_us += dur_us;
	e->mean_us = (e->count * e->mean_us) + dur_us;
	e->count += 1;
	e->mean_us /= e->count;
	return rc;
}

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
		(void)clock_gettime(CLOCK_REALTIME, &x->stats.connected);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		(void)clock_gettime(CLOCK_REALTIME, &x->stats.disconnected);
 		__ldms_xprt_resource_free(x);
		ldms_xprt_put(x, "connect");
		break;
	case LDMS_XPRT_EVENT_RECV:
		/* Do nothing */
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		/* Do nothing */
		break;
	default:
		XPRT_LOG(x, OVIS_LERROR, "__ldms_passive_connect_cb: "
				"unexpected ldms_xprt event value %d\n",
				(int) e->type);
		assert(0 == "__ldms_passive_connect_cb: unexpected ldms_xprt event value");
	}
}

void __ldms_xprt_conn_msg_init(ldms_t _x, struct ldms_conn_msg *msg)
{
	struct ldms_xprt *x = _x;
	bzero(msg, sizeof(*msg));
	LDMS_VERSION_SET(msg->ver);
	if (x->auth)
		strncpy(msg->auth_name,
			x->auth->plugin->name, sizeof(msg->auth_name));
}

void __ldms_xprt_init(struct ldms_xprt *x, const char *name, int is_active);

int ldms_xprt_names(ldms_t x, char *lcl_name, size_t lcl_name_sz,
				char *lcl_port, size_t lcl_port_sz,
				char *rem_name, size_t rem_name_sz,
				char *rem_port, size_t rem_port_sz,
				int flags)
{
	struct sockaddr_storage lcl, rmt;
	socklen_t xlen = sizeof(lcl);
	int rc;

	if (lcl_name)
		lcl_name[0] = '\0';
	if (lcl_port)
		lcl_port[0] = '\0';
	if (rem_name)
		rem_name[0] = '\0';
	if (rem_port)
		rem_port[0] = '\0';

	memset(&rmt, 0, sizeof(rmt));
	memset(&lcl, 0, sizeof(rmt));
	rc = ldms_xprt_sockaddr(x, (void*)&lcl, (void*)&rmt, &xlen);
	if (rc)
		return rc;

	if (lcl_name || lcl_port) {
		(void) getnameinfo((void*)&lcl, xlen, lcl_name, lcl_name_sz,
					lcl_port, lcl_port_sz, flags);
	}

	if (rem_name || rem_port) {
		(void) getnameinfo((void*)&rmt, xlen, rem_name, rem_name_sz,
					rem_port, rem_port_sz, flags);
	}
	return 0;
}

static void ldms_zap_handle_conn_req(zap_ep_t zep)
{
	static char rej_msg[64] = "Insufficient resources";
	struct ldms_conn_msg msg;
	int rc;
	char name[128];
	zap_err_t zerr;
	struct ldms_xprt *x = zap_get_ucontext(zep);
	struct ldms_auth *auth;
	(void) ldms_xprt_names(x, NULL, 0, NULL, 0, name, 128,
				     NULL, 0, NI_NUMERICHOST);
	/*
	 * Accepting zep inherit ucontext from the listening endpoint.
	 * Hence, x is of listening endpoint, not of accepting zep,
	 * and we have to create new ldms_xprt for the accepting zep.
	 */
	struct ldms_xprt *_x = calloc(1, sizeof(*_x));
	if (!_x) {
		XPRT_LOG(x, OVIS_LCRITICAL, "Memory allocation failure in "
				"creating new ldms_xprt for connection"
				" from %s.\n", name);
		goto err0;
	}
	__ldms_xprt_init(_x, x->name, 0);
	_x->zap = x->zap;
	_x->zap_ep = zep;
	_x->max_msg = zap_max_msg(x->zap);
	_x->event_cb = x->event_cb;
	_x->event_cb_arg = x->event_cb_arg;
	if (!_x->event_cb)
		_x->event_cb = __ldms_passive_connect_cb;
	ldms_xprt_get(_x, "zap_uctxt");
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

	/* Take a 'connect' reference. Dropped in ldms_xprt_close() */
	ldms_xprt_get(_x, "connect");

	zerr = zap_accept(zep, ldms_zap_auto_cb, (void*)&msg, sizeof(msg));
	if (zerr) {
		XPRT_LOG(x, OVIS_LERROR, "%d accepting connection from %s.\n",
								zerr, name);
		goto err2;
	}

	return;
err2:
	ldms_xprt_put(_x, "connect");	/* drop 'connect' reference */
err1:
	ldms_auth_free(auth);
err0:
	zap_set_ucontext(_x->zap_ep, NULL);
	ldms_xprt_put(_x, "zap_uctxt");	/* context reference */
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
	ldms_set_t set = ctxt->update.s;
	int n;
	struct ldms_data_hdr *data, *prev_data;
	int flags = 0, upd_curr_idx;
	void *base;

	assert(x == ctxt->x);
	assert(ctxt->update.cb);
	rc = LDMS_UPD_ERROR(ev->status);
	if (rc || (set == NULL)) {
		x->zerrno = rc;
		rc = zap_zerr2errno(rc);
		/* READ ERROR */
		if (!rc)
			rc = ENOENT;
		ctxt->update.cb(x, set, rc, ctxt->update.cb_arg);
		goto cleanup;
	}
	n = __le32_to_cpu(set->meta->array_card);

	data = __ldms_set_array_get(set, ctxt->update.idx_from);
	prev_data = __ldms_set_array_get(set, set->curr_idx);

	if (data != prev_data &&
			__ldms_data_ts_cmp(prev_data, data) >= 0) {
		/* special case, no new data */
		ctxt->update.cb(x, set, flags, ctxt->update.cb_arg);
		goto cleanup;
	}

	/* update current index from the update */
	upd_curr_idx = __le32_to_cpu(data->curr_idx);
	for (i = 0; i < n; i++) {
		data = __ldms_set_array_get(set, i);
		data->curr_idx = upd_curr_idx;
	}

	for (i = ctxt->update.idx_from;i <= ctxt->update.idx_to; i++) {
		data = __ldms_set_array_get(set, i);
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
		if (set->meta->heap_sz) {
			base = ((void*)set->data) + set->data->size - set->meta->heap_sz;
			if (ldms_set_is_consistent(set))
				set->heap = ldms_heap_get(&set->heap_inst, &set->data->heap, base);
			else
				set->heap = NULL;
		}
		ctxt->update.cb(x, set, flags, ctxt->update.cb_arg);
		prev_data = data;
	}

	if (flags == 0) /* our update is current */
		goto cleanup;

	/* the updated set is not current */
	rc = __ldms_remote_update(x, set, ctxt->update.cb,
			ctxt->update.cb_arg);
	if (rc)
		ctxt->update.cb(x, set, LDMS_UPD_ERROR(rc), ctxt->update.cb_arg);

cleanup:
	zap_put_ep(x->zap_ep, "ldms_xprt:set_update", __func__, __LINE__); /* from __ldms_remote_update() */
	pthread_mutex_lock(&x->lock);
	__ldms_free_ctxt(x, ctxt);
	pthread_mutex_unlock(&x->lock);
}

static void __handle_update_meta(ldms_t x, struct ldms_context *ctxt,
				 zap_event_t ev)
{
	int  rc;
	ldms_set_t set = ctxt->update.s;
	int idx = (set->curr_idx + 1) % __le32_to_cpu(set->meta->array_card);

	rc = do_read_data(x, set, idx, idx, ctxt->update.cb, ctxt->update.cb_arg);
	if (rc) {
		ctxt->update.cb(x, set, LDMS_UPD_ERROR(rc), ctxt->update.cb_arg);
		zap_put_ep(x->zap_ep, "ldms_xprt:set_update", __func__, __LINE__);
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
#ifdef DEBUG
		XPRT_LOG(x, OVIS_LALWAYS, "%s: lookup read error: zap error %d. "
			"ldms lookup status %d\n",
			ldms_set_instance_name_get(ctxt->lu_read.s),
			ev->status, status);
#endif /* DEBUG */
		/*
		 * Destroy the set, the ctxt still has a reference on
		 * it, but that will be dropped when the ctxt is
		 * freed.
		 */
		__ldms_set_delete(ctxt->lu_read.s, 0);
	} else {
		ldms_set_publish(ctxt->lu_read.s);
	}
	ctxt->lu_read.cb((ldms_t)x, status, ctxt->lu_read.more,
			 ev->status ? NULL : ctxt->lu_read.s,
			 ctxt->lu_read.cb_arg);
	if (!ctxt->lu_read.more) {
#ifdef DEBUG
		assert(x->active_lookup > 0);
		x->active_lookup--;
		XPRT_LOG(x, OVIS_LALWAYS, "read_complete: put ref %p: "
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
	struct ldms_thrstat *thrstat = zap_thrstat_ctxt_get(x->zap_ep);

	switch (ctxt->type) {
	case LDMS_CONTEXT_UPDATE:
		thrstat->last_op = LDMS_THRSTAT_OP_UPDATE_REPLY;
		if (ENABLED_PROFILING(LDMS_XPRT_OP_UPDATE)) {
			/*
			 * If read complete timestamp is already set,
			 * we replace it with a new timestamp.
			 *
			 * We collect the timestamp of the beginning of thr first read and
			 * the timestamp of the completion of the last read.
			 */
			memcpy(&ctxt->op_ctxt->update_profile.read_complete_ts,
			                                &thrstat->last_op_start,
			                                sizeof(struct timespec));
		}
		__handle_update_data(x, ctxt, ev);
		break;
	case LDMS_CONTEXT_UPDATE_META:
		if (ENABLED_PROFILING(LDMS_XPRT_OP_UPDATE)) {
			/*
			 * With the same reason as in the LDMS_CONTEXT_UPDATE case,
			 * we set or reset the read complete timestamp.
			 */
			memcpy(&ctxt->op_ctxt->update_profile.read_complete_ts,
			                                &thrstat->last_op_start,
			                                sizeof(struct timespec));
		}
		__handle_update_meta(x, ctxt, ev);
		break;
	case LDMS_CONTEXT_LOOKUP_READ:
		thrstat->last_op = LDMS_THRSTAT_OP_LOOKUP_REPLY;
		if (ENABLED_PROFILING(LDMS_XPRT_OP_LOOKUP)) {
			memcpy(&ctxt->op_ctxt->lookup_profile.complete_ts,
			                          &thrstat->last_op_start,
			                          sizeof(struct timespec));
		}

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
			XPRT_LOG(x, OVIS_LALWAYS, "%s(): %s\n", __func__, errstr);
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

static int __process_lookup_set_info(struct ldms_set *lset, char *set_info, char **_buf)
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
	*_buf = (char *)value;
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
	struct ldms_context *rd_ctxt = NULL;
	struct ldms_set *lset;
	int rc;
	ldms_name_t schema_name, inst_name;
	char *prfl_maker = 0;
	struct timespec *req_recv_ts;
	struct timespec *share_ts;

	struct ldms_thrstat *thrstat = zap_thrstat_ctxt_get(zep);
	struct ldms_op_ctxt *op_ctxt = ctxt->op_ctxt;

#ifdef DEBUG
	if (!__is_lookup_name_good(x, lu, ctxt)) {
		XPRT_LOG(x, OVIS_LALWAYS,
				"%s(): The schema or instance name in the lookup "
				"message sent by the peer does not "
				"match the lookup request\n", __func__);
		assert(0);
	}
#endif /* DEBUG */

	schema_name = (ldms_name_t)lu->set_info;
	inst_name = (ldms_name_t)&(schema_name->name[schema_name->len]);

	__ldms_set_tree_lock();
	lset = __ldms_find_local_set(inst_name->name);
	__ldms_set_tree_unlock();

	if (lset) {
		rc = EEXIST;
		ref_put(&lset->ref, "__ldms_find_local_set");
		lset = NULL;	/* So error path won't try to delete it */
		/* unmap ev->map, it is not used */
		zap_unmap(ev->map);
		goto callback;
	}

	/* Create the set */
	lset = __ldms_create_set(inst_name->name, schema_name->name,
				 ntohl(lu->meta_len), ntohl(lu->data_len),
				 ntohl(lu->card), ntohl(lu->array_card),
				 LDMS_SET_F_REMOTE);
	if (!lset) {
		rc = errno;
		goto callback;
	}
	lset->xprt = ldms_xprt_get(x, "lu_set");
	lset->rmap = ev->map; /* lset now owns ev->map */
	lset->remote_set_id = lm->lookup.set_id;

	pthread_mutex_lock(&lset->lock);
	(void)__process_lookup_set_info(lset, &inst_name->name[inst_name->len], &prfl_maker);

	if (ENABLED_PROFILING(LDMS_XPRT_OP_LOOKUP)) {
		if (prfl_maker < (char *)lm + lm->hdr.len) {
			/* The message is from v4.5.1+ version,
			 * which includes the lookup profiling timestamps.
			 */
			if (0 == strcmp(prfl_maker, LU_PARAM_PRFL_MARKER)) {
				req_recv_ts = (struct timespec *)(prfl_maker + strlen(LU_PARAM_PRFL_MARKER) + 1);
				share_ts = req_recv_ts+1;
				memcpy(&op_ctxt->lookup_profile.rendzv_ts, &thrstat->last_op_start, sizeof(struct timespec));
				memcpy(&op_ctxt->lookup_profile.req_recv_ts, req_recv_ts, sizeof(struct timespec));
				memcpy(&op_ctxt->lookup_profile.share_ts, share_ts, sizeof(struct timespec));
			}
		}
	}

	pthread_mutex_unlock(&lset->lock);

	pthread_mutex_lock(&x->lock);
	rd_ctxt = __ldms_alloc_ctxt(x, sizeof(*rd_ctxt),
				LDMS_CONTEXT_LOOKUP_READ,
				lset,
				ctxt->lu_req.cb, ctxt->lu_req.cb_arg,
				htonl(lu->more), ctxt->lu_req.flags);
	if (!rd_ctxt) {
		XPRT_LOG(x, OVIS_LCRITICAL, "%s(): Out of memory\n", __func__);
		rc = ENOMEM;
		pthread_mutex_unlock(&x->lock);
		goto callback_with_lock;
	}
	rd_ctxt->sem = ctxt->sem;
	rd_ctxt->sem_p = ctxt->sem_p;
	rd_ctxt->rc = ctxt->rc;
	rd_ctxt->op_ctxt = ctxt->op_ctxt;
	pthread_mutex_unlock(&x->lock);
	assert((zep == x->zap_ep) && (x == rd_ctxt->x));
	if (ENABLED_PROFILING(LDMS_XPRT_OP_LOOKUP)) {
		(void)clock_gettime(CLOCK_REALTIME, &op_ctxt->lookup_profile.read_ts);
	}
	rc = zap_read(zep,
		      lset->rmap, zap_map_addr(lset->rmap),
		      lset->lmap, zap_map_addr(lset->lmap),
		      __le32_to_cpu(lset->meta->meta_sz),
		      rd_ctxt);
	if (rc) {
		x->zerrno = rc;
		rc = zap_zerr2errno(rc);
		goto callback;
	}
	if (!lm->lookup.more) {
		pthread_mutex_lock(&x->lock);
		__ldms_free_ctxt(x, ctxt);
		pthread_mutex_unlock(&x->lock);
	}
	return;

 callback:
	pthread_mutex_lock(&x->lock);
 callback_with_lock:
#ifdef DEBUG
	XPRT_LOG(x, OVIS_LDEBUG, "%s: lookup error while ldms_xprt is processing the rendezvous "
			"with error %d. NOTE: error %d indicates that it is "
			"a synchronous error of zap_read\n", inst_name->name, rc, EIO);
#endif /* DEBUG */
	if (ctxt->lu_req.cb)
		ctxt->lu_req.cb(x, rc, 0, rc ? NULL : lset, ctxt->lu_req.cb_arg);
	__ldms_free_ctxt(x, ctxt);
	if (rd_ctxt)
		__ldms_free_ctxt(x, rd_ctxt);
#ifdef DEBUG
	assert(x->active_lookup);
	x->active_lookup--;
	XPRT_LOG(x, OVIS_LDEBUG, "rendezvous error: put ref %p: "
	       "active_lookup = %d\n",
	       x->zap_ep, x->active_lookup);
#endif /* DEBUG */
	pthread_mutex_unlock(&x->lock);

	/* If there was a lookup error, destroy the local set */
	if (lset && rc)
		__ldms_set_delete(lset, 0);
}

static void handle_rendezvous_push(zap_ep_t zep, zap_event_t ev,
				   struct ldms_xprt *x,
				   struct ldms_rendezvous_msg *lm)
{
	struct ldms_rendezvous_push_param *push = &lm->push;
	struct ldms_set *set;
	struct rbn *rbn;
	struct ldms_push_peer *pp;
	struct xprt_set_coll_entry *ent;

	set = __ldms_set_by_id(push->lookup_set_id);
	if (!set) {
		/* The set has been deleted.*/
		return;
	}

	pthread_mutex_lock(&set->lock);
	rbn = rbt_find(&set->push_coll, x);
	if (rbn) {
		/* exists, just update */
		pp = container_of(rbn, struct ldms_push_peer, rbn);
		pp->push_flags = ntohl(push->flags);
		pp->remote_set_id = push->push_set_id;
		pthread_mutex_unlock(&set->lock);
		return;
	}
	pp = calloc(1, sizeof(*pp));
	if (!pp) {
		pthread_mutex_unlock(&set->lock);
		XPRT_LOG(X, OVIS_LCRIT, "%s:%s:%d Not enough memory\n",
				__FILE__, __func__, __LINE__);
		return;
	}
	rbn_init(&pp->rbn, x);
	pp->xprt = ldms_xprt_get(x, "push_set");
	pp->push_flags = ntohl(push->flags);
	pp->remote_set_id = push->push_set_id;
	rbt_ins(&set->push_coll, &pp->rbn);
	pthread_mutex_unlock(&set->lock);

	/* also enlist the set to xprt notification list (on xprt_term) */
	pthread_mutex_lock(&x->lock);
	rbn = rbt_find(&x->set_coll, set);
	if (rbn) {
		pthread_mutex_unlock(&x->lock);
		return;
	}
	ent = calloc(1, sizeof(*ent));
	if (!ent) {
		pthread_mutex_unlock(&x->lock);
		XPRT_LOG(x, OVIS_LCRIT, "%s:%s:%d Not enough memory\n",
				__FILE__, __func__, __LINE__);
		return;
	}
	rbn_init(&ent->rbn, set);
	ent->set = set;
	ref_get(&set->ref, "xprt_set_coll");
	rbt_ins(&x->set_coll, &ent->rbn);
	pthread_mutex_unlock(&x->lock);

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
	struct ldms_thrstat *thrstat;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return;

	thrstat = zap_thrstat_ctxt_get(x->zap_ep);
	struct ldms_rendezvous_msg *lm = (typeof(lm))ev->data;
	switch (ntohl(lm->hdr.cmd)) {
	case LDMS_XPRT_RENDEZVOUS_LOOKUP:
		thrstat->last_op = LDMS_THRSTAT_OP_LOOKUP_REPLY;
		handle_rendezvous_lookup(zep, ev, x, lm);
		break;
	case LDMS_XPRT_RENDEZVOUS_PUSH:
		thrstat->last_op = LDMS_THRSTAT_OP_PUSH_REPLY;
		handle_rendezvous_push(zep, ev, x, lm);
		break;
	default:
#ifdef DEBUG
		assert(0);
#endif
		XPRT_LOG(x, OVIS_LERROR, "Unexpected rendezvous message %d "
						"received.\n", lm->hdr.cmd);
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

/* Callers must _not_ hold the transport lock */
static void __ldms_xprt_release_sets(ldms_t x, struct rbt *set_coll)
{
	struct rbn *rbn;
	struct xprt_set_coll_entry *ent;
	struct ldms_lookup_peer *lp = NULL;
	struct ldms_push_peer *pp = NULL;

	rbn = rbt_min(set_coll);
	while (rbn) {
		lp = NULL;
		pp = NULL;
		rbt_del(set_coll, rbn);
		ent = container_of(rbn, struct xprt_set_coll_entry, rbn);
		pthread_mutex_lock(&ent->set->lock);
		rbn = rbt_find(&ent->set->lookup_coll, x);
		if (rbn) {
			rbt_del(&ent->set->lookup_coll, rbn);
			lp = container_of(rbn, struct ldms_lookup_peer, rbn);
		}
		rbn = rbt_find(&ent->set->push_coll, x);
		if (rbn) {
			rbt_del(&ent->set->push_coll, rbn);
			pp = container_of(rbn, struct ldms_push_peer, rbn);
		}
		pthread_mutex_unlock(&ent->set->lock);
		if (lp) {
			ldms_xprt_put(lp->xprt, "lookup_peer");
			free(lp);
		}
		if (pp) {
			ldms_xprt_put(pp->xprt, "push_peer");
			free(pp);
		}
		ref_put(&ent->set->ref, "xprt_set_coll");
		free(ent);
		rbn = rbt_min(set_coll);
	}
}

/* implemented in ldms_rail.c */
void __rail_zap_handle_conn_req(zap_ep_t zep, zap_event_t ev);
void __rail_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg);
void __rail_ep_limit(ldms_t x, void *msg, int msg_len);

void __thrstats_reset(void *ctxt, struct timespec *reset_ts)
{
	int i;
	struct ldms_thrstat *thrstat = (struct ldms_thrstat *)ctxt;
	thrstat->last_op_start = *reset_ts;
	for (i = 0; i < LDMS_THRSTAT_OP_COUNT; i++)
		thrstat->ops[i].count = thrstat->ops[i].total = 0;
}

/**
 * ldms-zap event handling function.
 */
static void ldms_zap_cb(zap_ep_t zep, zap_event_t ev)
{
	struct ldms_xprt_event event;
	memset(&event, 0, sizeof(event));
	event.type = LDMS_XPRT_EVENT_LAST;
	char rej_msg[128];
	struct rbt set_coll;
	struct ldms_xprt *x = zap_get_ucontext(zep);
	struct ldms_thrstat *thrstat;
	struct ldms_thrstat_entry *thrstat_e = NULL;
	struct ldms_op_ctxt *op_ctxt = NULL;

	if (x == NULL)
		return;
#ifdef DEBUG
	XPRT_LOG(x, OVIS_LDEBUG, "ldms_zap_cb: receive %s. %p: ref_count %d\n",
			zap_event_str(ev->type), x, x->ref.ref_count);
#endif /* DEBUG */

	errno = 0;
	thrstat = zap_thrstat_ctxt_get(zep);
	if (!thrstat) {
		if ((errno == 0) || (ev->type == ZAP_EVENT_CONNECT_REQUEST)) {
			thrstat = calloc(1, sizeof(*thrstat));
			if (!thrstat) {
				ovis_log(xlog, OVIS_LCRIT,
						"Memory allocation failure.\n");
				return;
			}
			zap_thrstat_ctxt_set(zep, thrstat, __thrstats_reset);
		} else {
			ovis_log(xlog, OVIS_LCRIT, "Cannot retrieve thread stats "
					"from Zap endpoint. Error %d\n", errno);
			assert(0);
			return;
		}
	}
	assert(thrstat->last_op < LDMS_THRSTAT_OP_COUNT);
	(void)clock_gettime(CLOCK_REALTIME, &thrstat->last_op_start);
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
		thrstat->last_op = LDMS_THRSTAT_OP_CONNECT_SETUP;
		__sync_fetch_and_add(&xprt_connect_request_count, 1);
		if (0 != __ldms_conn_msg_verify(x, ev->data, ev->data_len,
					   rej_msg, sizeof(rej_msg))) {
			zap_reject(zep, rej_msg, strlen(rej_msg)+1);
			break;
		}
		struct ldms_conn_msg2 *m = (void*)ev->data;
		if (x->event_cb == __rail_cb &&
				ev->data_len >= sizeof(struct ldms_conn_msg2) &&
				m->conn_type == htonl(LDMS_CONN_TYPE_RAIL)) { /* rail ep */
			__rail_zap_handle_conn_req(zep, ev);
		} else {
			ldms_zap_handle_conn_req(zep);
		}
		break;
	case ZAP_EVENT_REJECTED:
		thrstat->last_op = LDMS_THRSTAT_OP_CONNECT_SETUP;
		(void)clock_gettime(CLOCK_REALTIME, &x->stats.disconnected);
		__sync_fetch_and_add(&xprt_reject_count, 1);
		event.type = LDMS_XPRT_EVENT_REJECTED;
		if (x->event_cb)
			x->event_cb(x, &event, x->event_cb_arg);
		__ldms_xprt_resource_free(x);
		/* Put the reference taken in ldms_xprt_connect() */
		ldms_xprt_put(x, "connect");
		break;
	case ZAP_EVENT_CONNECTED:
		thrstat->last_op = LDMS_THRSTAT_OP_CONNECT_SETUP;
		(void)clock_gettime(CLOCK_REALTIME, &x->stats.connected);
		__sync_fetch_and_add(&xprt_connect_count, 1);
		/* actively connected -- expecting conn_msg */
		if (0 != __ldms_conn_msg_verify(x, ev->data, ev->data_len,
					   rej_msg, sizeof(rej_msg))) {
			__ldms_xprt_term(x);
			break;
		}
		/* rail limits need to be setup here */
		if (x->event_cb == __rail_cb) {
			__rail_ep_limit(x, ev->data, ev->data_len);
		}

		/* then, proceed to authentication */
		ldms_xprt_auth_begin(x);
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		thrstat->last_op = LDMS_THRSTAT_OP_DISCONNECTED;
		(void)clock_gettime(CLOCK_REALTIME, &x->stats.disconnected);
		event.type = LDMS_XPRT_EVENT_ERROR;
		if (x->event_cb)
			x->event_cb(x, &event, x->event_cb_arg);
		__ldms_xprt_resource_free(x);
		/* Put the reference taken in ldms_xprt_connect() */
		ldms_xprt_put(x, "connect");
		break;
	case ZAP_EVENT_DISCONNECTED:
		thrstat->last_op = LDMS_THRSTAT_OP_DISCONNECTED;
		(void)clock_gettime(CLOCK_REALTIME, &x->stats.disconnected);
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
		__sync_fetch_and_or(&x->term, 1);
		pthread_mutex_lock(&x->lock);
		struct ldms_context *dir_ctxt = NULL;
		if (x->local_dir_xid) {
			dir_ctxt = (struct ldms_context *)(unsigned long)
				x->local_dir_xid;
			x->local_dir_xid = 0;
		}
		if (dir_ctxt)
			__ldms_free_ctxt(x, dir_ctxt);

		set_coll = x->set_coll;
		x->set_coll.root = NULL;
		pthread_mutex_unlock(&x->lock);
		if (x->event_cb == __rail_cb) { /* TODO revise this .. */
			x->event_cb(x, &event, x->event_cb_arg);
		} else if (XTYPE_IS_PASSIVE(x->xtype) &&
				event.type != LDMS_XPRT_EVENT_DISCONNECTED) {
			/* don't call event_cb(), the application does not
			 * know about this transport yet, simply put the
			 * transport reference.*/
			ldms_xprt_put(x, "init"); /* TODO: verify the ref name */
		} else {
			if (x->event_cb)
				x->event_cb(x, &event, x->event_cb_arg);
		}
		#ifdef DEBUG
		XPRT_LOG(x, OVIS_LDEBUG, "ldms_zap_cb: DISCONNECTED %p: ref_count %d. "
						"after callback\n", x, x->ref.ref_count);
		#endif /* DEBUG */
		__ldms_xprt_release_sets(x, &set_coll);
		__ldms_xprt_resource_free(x);
		/* Put the reference taken in ldms_xprt_connect() or accept() */
		ldms_xprt_put(x, "connect");
		break;
	case ZAP_EVENT_SEND_COMPLETE:
		thrstat->last_op = LDMS_THRSTAT_OP_SEND_MSG;
		if (x->auth_flag != LDMS_XPRT_AUTH_APPROVED) {
			/*
			 * Do not forward the send_complete to applications
			 * if the authentication is not approved.
			 * Applications know only the connection is connecting.
			 */
		} else {
			if (x->event_cb && ev->context) {
				if (ENABLED_PROFILING(LDMS_XPRT_OP_SEND)) {
					op_ctxt = (struct ldms_op_ctxt *)ev->context;
					memcpy(&op_ctxt->send_profile.complete_ts, &thrstat->last_op_start,
					                                          sizeof(struct timespec));
					(void)clock_gettime(CLOCK_REALTIME, &op_ctxt->send_profile.deliver_ts);
				}
				event.type = LDMS_XPRT_EVENT_SEND_COMPLETE;
				x->event_cb(x, &event, x->event_cb_arg);
			}
		}
		break;
	default:
		XPRT_LOG(x, OVIS_LERROR, "ldms_zap_cb: unexpected zap event "
				"value %d from network\n", (int) ev->type);
		assert(0 == "network sent bad zap event value to ldms_zap_cb");
	}
	(void)clock_gettime(CLOCK_REALTIME, &thrstat->last_op_end);
	thrstat_e = &thrstat->ops[thrstat->last_op];
	thrstat_e->total += ldms_timespec_diff_us(&thrstat->last_op_start, &thrstat->last_op_end);
	thrstat_e->count += 1;
}

void ldms_zap_auto_cb(zap_ep_t zep, zap_event_t ev)
{
	struct ldms_xprt *x = zap_get_ucontext(zep);
	struct ldms_xprt_event event = {0};

	switch(ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		assert(0 == "Illegal connect request.");
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		(void)clock_gettime(CLOCK_REALTIME, &x->stats.disconnected);
		event.type = LDMS_XPRT_EVENT_ERROR;
		if (x->event_cb)
			x->event_cb(x, &event, x->event_cb_arg);
		__ldms_xprt_resource_free(x);
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
	case ZAP_EVENT_SEND_COMPLETE:
		ldms_zap_cb(zep, ev);
		break;
	default:
		XPRT_LOG(x, OVIS_LERROR, "ldms_zap_auto_cb: unexpected "
				"zap event value %d from network\n",
				(int) ev->type);
		assert(0 == "network sent bad zap event value to ldms_zap_auto_cb");
	}
}

zap_t __ldms_zap_get(const char *xprt)
{
	int i;
	zap_t zap = NULL;
	char *name;

	pthread_mutex_lock(&ldms_zap_list_lock);
	for (i = 0; i < ldms_zap_tbl_n; i++) {
		if (0 == strcmp(xprt, ldms_zap_tbl[i].name)) {
			zap = ldms_zap_tbl[i].zap;
			goto out;
		}
	}
	if (ldms_zap_tbl_n == sizeof(ldms_zap_tbl)/sizeof(*ldms_zap_tbl)) {
		errno = ENOMEM;
		goto out;
	}
	zap = zap_get(xprt, ldms_zap_mem_info);
	if (!zap) {
		XPRT_LOG(x, OVIS_LERROR, "ldms: Cannot get zap plugin: %s\n", xprt);
		errno = ENOENT;
		goto out;
	}
	name = strdup(xprt);
	if (!name)
		goto out;
	ldms_zap_tbl[ldms_zap_tbl_n].name = name;
	ldms_zap_tbl[ldms_zap_tbl_n].zap = zap;
	ldms_zap_tbl_n++;
out:
	pthread_mutex_unlock(&ldms_zap_list_lock);
	return zap;
}

int __ldms_xprt_zap_new(struct ldms_xprt *x, const char *name)
{
	int ret = 0;
	errno = 0;
	x->zap = __ldms_zap_get(name);
	if (!x->zap) {
		ret = errno;
		goto err0;
	}

	x->zap_ep = zap_new(x->zap, ldms_zap_cb);
	if (!x->zap_ep) {
		XPRT_LOG(x, OVIS_LERROR, "ERROR: Cannot create zap endpoint.\n");
		ret = ENOMEM;
		goto err0;
	}
	x->max_msg = zap_max_msg(x->zap);
	ldms_xprt_get(x, "zap_uctxt");
	zap_set_ucontext(x->zap_ep, x);
	return 0;
err0:
	return ret;
}

static uint64_t __ldms_conn_id;
static uint64_t __ldms_xprt_conn_id(ldms_t ldms)
{
	return ldms->conn_id;
}

uint64_t ldms_xprt_conn_id(ldms_t x)
{
	return x->ops.conn_id(x);
}

static int __ldms_xprt_connect(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
				ldms_event_cb_t cb, void *cb_arg);
static int __ldms_xprt_is_connected(struct ldms_xprt *x);
static int __ldms_xprt_listen(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
				ldms_event_cb_t cb, void *cb_arg);
static int __ldms_xprt_sockaddr(ldms_t x, struct sockaddr *local_sa,
	       struct sockaddr *remote_sa,
	       socklen_t *sa_len);
static void __ldms_xprt_close(ldms_t x);
static int __ldms_xprt_send(ldms_t x, char *msg_buf, size_t msg_len,
					struct ldms_op_ctxt *op_ctxt);
static size_t __ldms_xprt_msg_max(ldms_t x);
static int __ldms_xprt_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags);
static int __ldms_xprt_lookup(ldms_t x, const char *path, enum ldms_lookup_flags flags,
		     ldms_lookup_cb_t cb, void *cb_arg, struct ldms_op_ctxt *op_ctxt);
static int __ldms_xprt_stats(ldms_t x, ldms_xprt_stats_t stats, int mask, int is_reset);
static int __ldms_xprt_dir_cancel(ldms_t x);

static ldms_t __ldms_xprt_get(ldms_t x, const char *name, const char *func, int line); /* ref get */
static void __ldms_xprt_put(ldms_t x, const char *name, const char *func, int line); /* ref put */
static void __ldms_xprt_ctxt_set(ldms_t x, void *ctxt, app_ctxt_free_fn fn);
static void *__ldms_xprt_ctxt_get(ldms_t x);
static uint64_t __ldms_xprt_conn_id(ldms_t x);
static const char *__ldms_xprt_type_name(ldms_t x);
static void __ldms_xprt_priority_set(ldms_t x, int prio);
static void __ldms_xprt_cred_get(ldms_t x, ldms_cred_t lcl, ldms_cred_t rmt);
static void __ldms_xprt_event_cb_set(ldms_t x, ldms_event_cb_t cb, void *cb_arg);
int __ldms_xprt_update(ldms_t x, struct ldms_set *set, ldms_update_cb_t cb, void *arg,
                                                         struct ldms_op_ctxt *op_ctxt);
int __ldms_xprt_get_threads(ldms_t x, pthread_t *out, int n);
zap_ep_t __ldms_xprt_get_zap_ep(ldms_t x);
static ldms_set_t __ldms_xprt_set_by_name(ldms_t x, const char *set_name);

static const struct ldms_xprt_ops_s ldms_xprt_ops = {
	.connect      = __ldms_xprt_connect,
	.is_connected = __ldms_xprt_is_connected,
	.listen       = __ldms_xprt_listen,
	.sockaddr     = __ldms_xprt_sockaddr,
	.close        = __ldms_xprt_close,
	.send         = __ldms_xprt_send,
	.msg_max      = __ldms_xprt_msg_max,
	.dir          = __ldms_xprt_dir,
	.dir_cancel   = __ldms_xprt_dir_cancel,
	.lookup       = __ldms_xprt_lookup,
	.stats        = __ldms_xprt_stats,

	.get          = __ldms_xprt_get,
	.put          = __ldms_xprt_put,
	.ctxt_set     = __ldms_xprt_ctxt_set,
	.ctxt_get     = __ldms_xprt_ctxt_get,
	.conn_id      = __ldms_xprt_conn_id,
	.type_name    = __ldms_xprt_type_name,
	.priority_set = __ldms_xprt_priority_set,
	.cred_get     = __ldms_xprt_cred_get,

	.update       = __ldms_xprt_update,

	.get_threads  = __ldms_xprt_get_threads,
	.get_zap_ep   = __ldms_xprt_get_zap_ep,

	.event_cb_set = __ldms_xprt_event_cb_set,
	.set_by_name  = __ldms_xprt_set_by_name,
};

void __ldms_xprt_init(struct ldms_xprt *x, const char *name, int is_active)
{
	x->conn_id = __sync_add_and_fetch(&__ldms_conn_id, 1);
	x->name[LDMS_MAX_TRANSPORT_NAME_LEN - 1] = 0;
	memccpy(x->name, name, 0, LDMS_MAX_TRANSPORT_NAME_LEN - 1);
	ref_init(&x->ref, "init", __xprt_ref_free, x);
	x->remote_dir_xid = x->local_dir_xid = 0;

	x->luid = -1;
	x->lgid = -1;
	x->ruid = -1;
	x->rgid = -1;

	x->xtype = is_active?LDMS_XTYPE_ACTIVE_XPRT:LDMS_XTYPE_PASSIVE_XPRT;

	x->ops = ldms_xprt_ops;

	ldms_xprt_ops_t op_e;
	for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++)
		x->stats.ops[op_e].min_us = LLONG_MAX;

	TAILQ_INIT(&x->ctxt_list);
	sem_init(&x->sem, 0, 0);
	rbt_init(&x->set_coll, rbn_ptr_cmp);
	pthread_mutex_init(&x->lock, NULL);
	pthread_mutex_lock(&xprt_list_lock);
	LIST_INSERT_HEAD(&xprt_list, x, xprt_link);
	pthread_mutex_unlock(&xprt_list_lock);
}

static void __ldms_xprt_priority_set(ldms_t x, int prio)
{
	zap_set_priority(x->zap_ep, prio);
}

void ldms_xprt_priority_set(ldms_t x, int prio)
{
	x->ops.priority_set(x, prio);
}

static void __ldms_xprt_cred_get(ldms_t x, ldms_cred_t lcl, ldms_cred_t rmt)
{
	if (lcl) {
		lcl->uid = x->luid;
		lcl->gid = x->lgid;
	}

	if (rmt) {
		rmt->uid = x->ruid;
		rmt->gid = x->rgid;
	}
}

void ldms_xprt_cred_get(ldms_t x, ldms_cred_t lcl, ldms_cred_t rmt)
{
	x->ops.cred_get(x, lcl, rmt);
}

static void __ldms_xprt_event_cb_set(ldms_t x, ldms_event_cb_t cb, void *cb_arg)
{
	struct ldms_xprt *_x = x;
	_x->event_cb = cb;
	_x->event_cb_arg = cb_arg;
}

void ldms_xprt_event_cb_set(ldms_t x, ldms_event_cb_t cb, void *cb_arg)
{
	x->ops.event_cb_set(x, cb, cb_arg);
}

/*
 * This is the legacy ldms xprt interface. It is still used to create xprt for
 * rails.
 *
 * The new ldms_xprt_new_with_auth() creates a rail with one xprt. Its
 * implementation is in `ldms_rail.c`.
 */
ldms_t __ldms_xprt_new_with_auth(const char *xprt_name, const char *auth_name,
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
	__ldms_xprt_init(x, xprt_name, 1);

	ret = __ldms_xprt_zap_new(x, xprt_name);
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
	ldms_xprt_put(x, "init");
err0:
	errno = ret;
	return NULL;
}

static const char *__ldms_xprt_type_name(ldms_t x)
{
	return x->name;
}

const char *ldms_xprt_type_name(ldms_t x)
{
	return x->ops.type_name(x);
}

ldms_t ldms_xprt_new(const char *name)
{
	return ldms_xprt_new_with_auth(name, "none", NULL);
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

static int __ldms_xprt_send(ldms_t _x, char *msg_buf, size_t msg_len,
					struct ldms_op_ctxt *op_ctxt)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	size_t len;
	struct ldms_context *ctxt;
	int rc;

	if (!ldms_xprt_connected(x))
		return ENOTCONN;

	if (!msg_buf)
		return EINVAL;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return EPERM;

	ldms_xprt_get(x, "send");
	pthread_mutex_lock(&x->lock);
	size_t sz = sizeof(struct ldms_request) + sizeof(struct ldms_context) + msg_len;
	ctxt = __ldms_alloc_ctxt(x, sz, LDMS_CONTEXT_SEND);
	if (!ctxt) {
		rc = ENOMEM;
		goto err_0;
	}
		ctxt->op_ctxt = op_ctxt;
	req = (struct ldms_request *)(ctxt + 1);
	req->hdr.xid = 0;
	req->hdr.cmd = htonl(LDMS_CMD_SEND_MSG);
	req->send.msg_len = htonl(msg_len);
	memcpy(req->send.msg, msg_buf, msg_len);
	len = sizeof(struct ldms_request_hdr) +
		sizeof(struct ldms_send_cmd_param) + msg_len;
	req->hdr.len = htonl(len);

	if (ENABLED_PROFILING(LDMS_XPRT_OP_SEND)) {
		(void)clock_gettime(CLOCK_REALTIME, &op_ctxt->send_profile.send_ts);
	}
	rc = zap_send2(x->zap_ep, req, len, (void*)op_ctxt);
#ifdef DEBUG
	if (rc) {
		XPRT_LOG(x, OVIS_LDEBUG, "send: error. put ref %p.\n", x->zap_ep);
	}
#endif
	__ldms_free_ctxt(x, ctxt);
 err_0:
	pthread_mutex_unlock(&x->lock);
	ldms_xprt_put(x, "send");
	return rc;
}

int ldms_xprt_send(ldms_t _x, char *msg_buf, size_t msg_len)
{
	int rc;
	struct ldms_op_ctxt *op_ctxt = NULL;
	if (ENABLED_PROFILING(LDMS_XPRT_OP_SEND)) {
		op_ctxt = calloc(1, sizeof(*op_ctxt));
		if (!op_ctxt)
			return ENOMEM;
		op_ctxt->op_type = LDMS_XPRT_OP_SEND;
		(void)clock_gettime(CLOCK_REALTIME, &(op_ctxt->send_profile.app_req_ts));
	}
	rc = _x->ops.send(_x, msg_buf, msg_len, op_ctxt);
	if (rc)
		free(op_ctxt);
	return rc;
}

static size_t __ldms_xprt_msg_max(ldms_t x)
{
	return	x->max_msg - (sizeof(struct ldms_request_hdr) +
			sizeof(struct ldms_send_cmd_param));
}

size_t ldms_xprt_msg_max(ldms_t x)
{
	return x->ops.msg_max(x);
}

int __ldms_remote_dir(ldms_t _x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (!ldms_xprt_connected(x))
		return ENOTCONN;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return EPERM;

	/* If a dir has previously been done and updates were asked
	 * for, return EBUSY. No need for application to do dir request again. */
	if (x->local_dir_xid)
		return EBUSY;

	/* Prevent x being destroyed if DISCONNECTED is delivered in another thread */
	ldms_xprt_get(x, "dir");
	pthread_mutex_lock(&x->lock);
	ctxt = __ldms_alloc_ctxt(x, sizeof(*ctxt) + sizeof(*req),
					LDMS_CONTEXT_DIR, cb, cb_arg);
	if (!ctxt) {
		pthread_mutex_unlock(&x->lock);
		ldms_xprt_put(x, "dir");
		return ENOMEM;
	}
	req = (struct ldms_request *)(ctxt + 1);
	len = format_dir_req(req, (uint64_t)(unsigned long)ctxt, flags);
	if (flags)
		x->local_dir_xid = (uint64_t)ctxt;
	pthread_mutex_unlock(&x->lock);

#ifdef DEBUG
	x->active_dir++;
	XPRT_LOG(x, OVIS_LDEBUG, "remote_dir. get ref %p. active_dir = %d. xid %p\n",
				x->zap_ep, x->active_dir, (void *)req->hdr.xid);
#endif /* DEBUG */
	zap_err_t zerr = zap_send(x->zap_ep, req, len);
	if (zerr) {
		pthread_mutex_lock(&x->lock);
		__ldms_free_ctxt(x, ctxt);
		x->local_dir_xid = 0;
#ifdef DEBUG
		XPRT_LOG(x, OVIS_LDEBUG, "remote_dir: error. put ref %p. "
				"active_dir = %d.\n",
				x->zap_ep, x->active_dir);
		x->active_dir--;
#endif /* DEBUG */
		pthread_mutex_unlock(&x->lock);
	}
	ldms_xprt_put(x, "dir");
	return zap_zerr2errno(zerr);
}

static int __ldms_xprt_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
{
	return __ldms_remote_dir(x, cb, cb_arg, flags);
}

int ldms_xprt_dir(ldms_t x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
{
	return x->ops.dir(x, cb, cb_arg, flags);
}

/* This request has no reply */
int __ldms_remote_dir_cancel(ldms_t _x)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (!ldms_xprt_connected(x))
		return ENOTCONN;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return EPERM;

	/* Prevent x being destroyed if DISCONNECTED is delivered in another thread */
	ldms_xprt_get(x, "dir_cancel");
	pthread_mutex_lock(&x->lock);
	ctxt = __ldms_alloc_ctxt(x, sizeof(*req) + sizeof(*ctxt), LDMS_CONTEXT_DIR_CANCEL);
	if (!ctxt) {
		pthread_mutex_unlock(&x->lock);
		ldms_xprt_put(x, "dir_cancel");
		return ENOMEM;
	}
	req = (struct ldms_request *)(ctxt + 1);
	len = format_dir_cancel_req(req);

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
	}

	pthread_mutex_lock(&x->lock);
	__ldms_free_ctxt(x, ctxt);
	pthread_mutex_unlock(&x->lock);
	ldms_xprt_put(x, "dir_cancel");
	return zap_zerr2errno(zerr);
}

static int __ldms_xprt_dir_cancel(ldms_t x)
{
	return __ldms_remote_dir_cancel(x);
}

int ldms_xprt_dir_cancel(ldms_t x)
{
	return x->ops.dir_cancel(x);
}

int __ldms_remote_lookup(ldms_t _x, const char *path,
			 enum ldms_lookup_flags flags,
			 ldms_lookup_cb_t cb, void *arg,
			 struct ldms_op_ctxt *op_ctxt)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (!ldms_xprt_connected(x))
		return ENOTCONN;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return EPERM;

	__ldms_set_tree_lock();
	struct ldms_set *set = __ldms_find_local_set(path);
	__ldms_set_tree_unlock();
	if (set) {
		ldms_set_put(set);
		return EEXIST;
	}
	char *lu_path = strdup(path);
	if (!lu_path)
		return ENOMEM;

	/* Prevent x being destroyed if DISCONNECTED is delivered in another thread */
	ldms_xprt_get(x, "lookup");
	pthread_mutex_lock(&x->lock);
	ctxt = __ldms_alloc_ctxt(x,
				sizeof(struct ldms_request) + sizeof(*ctxt),
				LDMS_CONTEXT_LOOKUP_REQ,
				cb, arg, lu_path, flags);
	if (!ctxt) {
		free(lu_path);
		pthread_mutex_unlock(&x->lock);
		ldms_xprt_put(x, "lookup");
		return ENOMEM;
	}
	req = (struct ldms_request *)(ctxt + 1);

	len = format_lookup_req(req, flags, path, (uint64_t)(unsigned long)ctxt);

#ifdef DEBUG
	x->active_lookup++;
#endif /* DEBUG */
	pthread_mutex_unlock(&x->lock);

#ifdef DEBUG
	XPRT_LOG(x, OVIS_LDEBUG, "remote_lookup: get ref %p: active_lookup = %d\n",
		x->zap_ep, x->active_lookup);
#endif /* DEBUG */
	if (ENABLED_PROFILING(LDMS_XPRT_OP_LOOKUP)) {
		ctxt->op_ctxt = op_ctxt;
		(void)clock_gettime(CLOCK_REALTIME, &op_ctxt->lookup_profile.req_send_ts);
	}
	zap_err_t zerr = zap_send(x->zap_ep, req, len);
	if (zerr) {
		pthread_mutex_lock(&x->lock);
		__ldms_free_ctxt(x, ctxt);
		pthread_mutex_unlock(&x->lock);
#ifdef DEBUG
		x->active_lookup--;
		XPRT_LOG(x, OVIS_LDEBUG, "lookup_reply: error %d. put ref %p: "
		       "active_lookup = %d. path = %s\n",
		       zerr, x->zap_ep, x->active_lookup,
		       path);
#endif /* DEBUG */
	}
	ldms_xprt_put(x, "lookup");
	return zap_zerr2errno(zerr);
}

static  void sync_lookup_cb(ldms_t x, enum ldms_lookup_status status, int more,
			    ldms_set_t s, void *arg)
{
	ldms_set_t *ps = arg;
	x->sem_rc = status;
	if (ps)
		*ps = s;
	sem_post(&x->sem);
}

static int __ldms_xprt_lookup(ldms_t x, const char *path, enum ldms_lookup_flags flags,
		               ldms_lookup_cb_t cb, void *cb_arg, struct ldms_op_ctxt *op_ctxt)
{
	int rc;
	if ((flags & !cb)
	    || strlen(path) > LDMS_LOOKUP_PATH_MAX)
		return EINVAL;
	if (!cb) {
		rc = __ldms_remote_lookup(x, path, flags, sync_lookup_cb, cb_arg, op_ctxt);
		if (rc)
			return rc;
		sem_wait(&x->sem);
		rc = x->sem_rc;
	} else
		rc = __ldms_remote_lookup(x, path, flags, cb, cb_arg, op_ctxt);
	return rc;
}

int ldms_xprt_lookup(ldms_t x, const char *path, enum ldms_lookup_flags flags,
		     ldms_lookup_cb_t cb, void *cb_arg)
{
	int rc;
	struct ldms_op_ctxt *op_ctxt = NULL;

	if (ENABLED_PROFILING(LDMS_XPRT_OP_LOOKUP)) {
		op_ctxt = calloc(1, sizeof(*op_ctxt));
		if (!op_ctxt)
			return ENOMEM;
		op_ctxt->op_type = LDMS_XPRT_OP_LOOKUP;
		(void)clock_gettime(CLOCK_REALTIME, &op_ctxt->lookup_profile.app_req_ts);
	}
	rc = x->ops.lookup(x, path, flags, cb, cb_arg, op_ctxt);
	if (rc)
		free(op_ctxt);
	return rc;
}

static void __stats_ep_clear(ldms_xprt_stats_t stats)
{
	enum ldms_xprt_ops_e op_e;
	struct ldms_op_ctxt *op_ctxt;

	free((char*)stats->name);
	for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++){
		while ((op_ctxt = TAILQ_FIRST(&stats->ep.op_ctxt_lists[op_e]))) {
			TAILQ_REMOVE(&stats->ep.op_ctxt_lists[op_e], op_ctxt, ent);
			free(op_ctxt);
		}
	}
}

static void __xprt_stats_reset(ldms_t _x, int mask, struct timespec *ts)
{
	enum ldms_xprt_ops_e op_e;
	struct ldms_op_ctxt_list *ctxt_list;
	struct ldms_op_ctxt *op_ctxt;
	struct ldms_xprt *x = (struct ldms_xprt *)_x;

	if (mask & LDMS_PERF_M_STATS) {
		/* last_op and ops could also be reset by ldms_xprt_rate_data(). */
		/* don't reset the connect/disconnect time */
		memset(&_x->stats.last_op, 0, sizeof(_x->stats.last_op));
		memset(&_x->stats.ops, 0, sizeof(_x->stats.ops));
		for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++)
			x->stats.ops[op_e].min_us = LLONG_MAX;
	}
	if (mask & LDMS_PERF_M_PROFILNG) {
		for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++) {
			ctxt_list = __rail_op_ctxt_list(_x, op_e);
			while ((op_ctxt = TAILQ_FIRST(ctxt_list))) {
				TAILQ_REMOVE(ctxt_list, op_ctxt, ent);
				free(op_ctxt);
			}
		}
	}
}

static int __ldms_xprt_stats(ldms_t _x, ldms_xprt_stats_t stats, int mask, int is_reset)
{
	int rc;
	struct ldms_op_ctxt_list *src_list, *dst_list;
	struct ldms_op_ctxt *src, *dst;
	enum ldms_xprt_ops_e op_e;
	struct ldms_xprt *x = _x;
	zap_ep_t zep = ldms_xprt_get_zap_ep(x);
	zap_ep_state_t zep_state;
	socklen_t socklen;

	if (!stats)
		goto reset;

	memset(stats, 0, sizeof(*stats));
	stats->name = strdup(ldms_xprt_type_name(_x));
	if (!stats->name) {
		return ENOMEM;
	}

	for (op_e = 0; op_e < LDMS_XPRT_OP_COUNT; op_e++) {
		TAILQ_INIT(&stats->ep.op_ctxt_lists[op_e]);
		dst_list = &stats->ep.op_ctxt_lists[op_e];
		src_list = __rail_op_ctxt_list(_x, op_e);

		TAILQ_FOREACH(src, src_list, ent) {
			dst = malloc(sizeof(*dst));
			if (!dst) {
				ovis_log(NULL, OVIS_LCRIT, "Memory allocation failure.\n");
				__stats_ep_clear(stats);
				return ENOMEM;
			}
			memcpy(dst, src, sizeof(*dst));
			dst->ent.tqe_next = NULL;
			dst->ent.tqe_prev = NULL;
			TAILQ_INSERT_TAIL(dst_list, dst, ent);
		}
	}
	stats->connected = x->stats.connected;
	stats->disconnected = x->stats.disconnected;
	stats->xflags = LDMS_STATS_XFLAGS_EP;
	if (x->xtype == LDMS_XTYPE_PASSIVE_XPRT)
		stats->xflags |= LDMS_STATS_XFLAGS_PASSIVE;
	else
		stats->xflags |= LDMS_STATS_XFLAGS_ACTIVE;
	stats->ep.last_op = x->stats.last_op;
	memcpy(&stats->ep.ops, x->stats.ops, sizeof(x->stats.ops));

	if (zep) {
		zep_state = zap_ep_state(zep);
		if (zep_state == ZAP_EP_CONNECTED) {
			rc = ldms_xprt_names(x, stats->lhostname, sizeof(stats->lhostname),
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

			socklen = sizeof(stats->ep.ss_local);
			memset(&stats->ep.ss_local, 0, sizeof(stats->ep.ss_local));
			memset(&stats->ep.ss_remote, 0, sizeof(stats->ep.ss_remote));
			rc = __ldms_xprt_sockaddr(_x, (struct sockaddr *)&stats->ep.ss_local,
					(struct sockaddr *)&stats->ep.ss_remote, &socklen);
			if (rc) {
				memset(&stats->ep.ss_local, 0, sizeof(stats->ep.ss_local));
				memset(&stats->ep.ss_remote, 0 , sizeof(stats->ep.ss_remote));
			}

			stats->ep.sq_sz = zap_ep_sq_sz(zep);
			stats->state = LDMS_XPRT_STATS_S_CONNECT;
		} else {
			/* connected case is handled in the if condition. */
			switch (zep_state) {
			case ZAP_EP_ACCEPTING:
			case ZAP_EP_CONNECTING:
				stats->state = LDMS_XPRT_STATS_S_CONNECTING;
				break;
			case ZAP_EP_LISTENING:
				stats->state = LDMS_XPRT_STATS_S_LISTEN;
				break;
			case ZAP_EP_INIT:
			case ZAP_EP_PEER_CLOSE:
			case ZAP_EP_CLOSE:
			case ZAP_EP_ERROR:
				stats->state = LDMS_XPRT_STATS_S_CLOSE;
				break;
			default:
				stats->state = 0;
			}
		}
	}

 reset:
	if (!is_reset)
		return 0;
	__xprt_stats_reset(_x, mask, NULL);
	return 0;
}

int ldms_xprt_stats(ldms_t _x, ldms_xprt_stats_t stats, int mask, int is_reset)
{
	return _x->ops.stats(_x, stats, mask, is_reset);
}

const char *ldms_xprt_stats_state(enum ldms_xprt_stats_state state)
{
	switch (state) {
	case LDMS_XPRT_STATS_S_CLOSE:
		return "close";
	case LDMS_XPRT_STATS_S_CONNECT:
		return "connect";
	case LDMS_XPRT_STATS_S_CONNECTING:
		return "connecting";
	case LDMS_XPRT_STATS_S_LISTEN:
		return "listen";
	default:
		return "";
	}
}

static void __stats_rail_clear(struct ldms_xprt_stats_s *stats)
{
	int i;
	free((char*)stats->name);
	for(i = 0; i < stats->rail.n_eps; i++) {
		ldms_xprt_stats_clear(&stats->rail.eps_stats[i]);
	}
	free(stats->rail.eps_stats);
}

void ldms_xprt_stats_clear(ldms_xprt_stats_t stats)
{
	if (stats->xflags & LDMS_STATS_XFLAGS_EP)
		__stats_ep_clear(stats);
	else
		__stats_rail_clear(stats);
}

static int send_req_notify(ldms_t _x, ldms_set_t s, uint32_t flags,
			   ldms_notify_cb_t cb_fn, void *cb_arg)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (!ldms_xprt_connected(x))
		return ENOTCONN;

	ldms_xprt_get(x, "send_notify");
	pthread_mutex_lock(&x->lock);
	ctxt = __ldms_alloc_ctxt(x, sizeof(*req) + sizeof(*ctxt),
					LDMS_CONTEXT_REQ_NOTIFY,
					s, cb_fn, cb_arg);
	pthread_mutex_unlock(&x->lock);
	if (!ctxt) {
		ldms_xprt_put(x, "send_notify");
		return ENOMEM;
	}
	req = (struct ldms_request *)(ctxt + 1);

	pthread_mutex_lock(&s->lock);
	if (s->notify_ctxt) {
		/* Drop the old ctxt and use the new onw */
		__ldms_free_ctxt(x, s->notify_ctxt);
	}
	s->notify_ctxt = ctxt;
	pthread_mutex_unlock(&s->lock);
	len = format_req_notify_req(req, (uint64_t)(unsigned long)ctxt,
				    s->remote_set_id, flags);

	zap_err_t zerr = zap_send(x->zap_ep, req, len);
	if (zerr) {
		x->zerrno = zerr;
		XPRT_LOG(x, OVIS_LERROR, "%s(): x %p: error %s sending REQ_NOTIFY\n",
		       __func__, x, zap_err_str(zerr));
	}
	ldms_xprt_put(x, "send_notify");
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

static int send_cancel_notify(ldms_t _x, ldms_set_t s)
{
	struct ldms_xprt *x = _x;
	struct ldms_request req;
	size_t len;

	if (!ldms_xprt_connected(x))
		return ENOTCONN;

	len = format_cancel_notify_req(&req,
			(uint64_t)(unsigned long)s->notify_ctxt, s->remote_set_id);
	s->notify_ctxt = 0;

	zap_err_t zerr = zap_send(x->zap_ep, &req, len);
	if (zerr) {
		x->zerrno = zerr;
	}
	return zap_zerr2errno(zerr);
}

int ldms_cancel_notify(ldms_t t, ldms_set_t s)
{
	if (!s->notify_ctxt)
		goto err;
	return send_cancel_notify(t, s);
 err:
	errno = EINVAL;
	return -1;
}

void ldms_notify(ldms_set_t s, ldms_notify_event_t e)
{
	if (!s)
		return;
	struct rbn *rbn;
	struct ldms_lookup_peer *p;
	pthread_mutex_lock(&s->lock);
	RBT_FOREACH(rbn, &s->lookup_coll) {
		p = container_of(rbn, struct ldms_lookup_peer, rbn);
		if (0 == p->notify_flags || (p->notify_flags & e->type)) {
			send_req_notify_reply(p->xprt, s,
					      p->remote_notify_xid, e);
		}
	}
	pthread_mutex_unlock(&s->lock);
}

static int send_req_register_push(ldms_set_t s, uint32_t push_change)
{
	struct ldms_xprt *x = s->xprt;
	struct ldms_rendezvous_msg req;
	size_t len;
	int rc;

	if (!ldms_xprt_connected(x))
		return ENOTCONN;

	ldms_xprt_get(x, "register_push");
	rc = __xprt_set_access_check(x, s, LDMS_ACCESS_WRITE);
	/* check if the remote can write to us */
	if (rc)
		goto out;
	len = sizeof(struct ldms_rendezvous_hdr)
		+ sizeof(struct ldms_rendezvous_push_param);
	req.hdr.xid = 0;
	req.hdr.cmd = htonl(LDMS_XPRT_RENDEZVOUS_PUSH);
	req.hdr.len = htonl(len);
	req.push.lookup_set_id = s->remote_set_id;
	req.push.push_set_id = s->set_id;
	req.push.flags = htonl(LDMS_RBD_F_PUSH);
	if (push_change)
		req.push.flags |= htonl(LDMS_RBD_F_PUSH_CHANGE);
	zap_err_t zerr = zap_share(x->zap_ep, s->lmap, (const char *)&req, len);
	if (zerr) {
		x->zerrno = zerr;
		rc = zap_zerr2errno(zerr);
	}
 out:
	ldms_xprt_put(x, "register_push");
	return rc;
}

int ldms_xprt_register_push(ldms_set_t s, int push_flags,
			    ldms_update_cb_t cb_fn, void *cb_arg)
{
	if (!s->xprt || !cb_fn)
		return EINVAL;
	s->push_cb = cb_fn;
	s->push_cb_arg = cb_arg;
	return send_req_register_push(s, push_flags);
}

static int send_req_cancel_push(ldms_set_t s)
{
	struct ldms_xprt *x = s->xprt;
	struct ldms_request req;
	size_t len;
	int rc = 0;

	if (!ldms_xprt_connected(x))
		return ENOTCONN;

	x = ldms_xprt_get(s->xprt, "cancel_push");
	len = sizeof(struct ldms_request_hdr)
		+ sizeof(struct ldms_cancel_push_cmd_param);
	req.hdr.xid = 0;
	req.hdr.cmd = htonl(LDMS_CMD_CANCEL_PUSH);
	req.hdr.len = htonl(len);
	req.cancel_push.set_id = s->remote_set_id;
	zap_err_t zerr = zap_send(x->zap_ep, &req, len);
	if (zerr) {
		rc = zap_zerr2errno(zerr);
		x->zerrno = zerr;
	}
	ldms_xprt_put(x, "cancel_push");
	return rc;
}

int ldms_xprt_cancel_push(ldms_set_t s)
{
	if (!s->xprt)
		return EINVAL;
	/* We may/will continue to receive push notifications after this */
	return send_req_cancel_push(s);
}

int __ldms_xprt_push(ldms_set_t set, int push_flags)
{
	int rc = 0;
	struct ldms_reply *reply;
	uint32_t meta_meta_gn = __le32_to_cpu(set->meta->meta_gn);
	uint32_t meta_meta_sz = __le32_to_cpu(set->meta->meta_sz);
	uint32_t meta_data_heap_sz = __le32_to_cpu(set->meta->data_sz);
	struct rbn *rbn;
	struct ldms_push_peer *p;

	pthread_mutex_lock(&set->lock);
	RBT_FOREACH(rbn, &set->push_coll) {
		p = container_of(rbn, struct ldms_push_peer, rbn);
		rc = 0;
		if (!p->xprt || !(p->push_flags & push_flags))
			continue;

		ldms_t x = p->xprt;

		pthread_mutex_lock(&x->lock);
		if (LDMS_XPRT_AUTH_GUARD(x))
			goto skip;

		size_t doff;
		size_t len;
		size_t max_len = zap_max_msg(x->zap);

		if (p->meta_gn != meta_meta_gn) {
			p->meta_gn = meta_meta_gn;
			len = meta_meta_sz + meta_data_heap_sz;
			doff = 0;
		} else {
			len = meta_data_heap_sz;
			doff = (uint8_t *)set->data - (uint8_t *)set->meta;
		}
		size_t hdr_len = sizeof(struct ldms_reply_hdr)
			+ sizeof(struct ldms_push_reply);
		reply = malloc(max_len);
		if (!reply) {
			rc = ENOMEM;
			goto skip;
		}
#ifdef PUSH_DEBUG
		XPRT_LOG(x, OVIS_LDEBUG, "Push set %s to endpoint %p\n",
		       ldms_set_instance_name_get(set),
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
			reply->push.set_id = p->remote_set_id;
			reply->push.data_len = htonl(data_len);
			reply->push.data_off = htonl(doff);
			reply->push.flags |= htonl(LDMS_UPD_F_PUSH);
			if (p->push_flags & LDMS_RBD_F_PUSH_CANCEL)
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
		if (rc)
			break;
	}
	pthread_mutex_unlock(&set->lock);
	return rc;
}

int ldms_xprt_push(ldms_set_t s)
{
	return __ldms_xprt_push(s, LDMS_RBD_F_PUSH);
}

static int __ldms_xprt_connect(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
			ldms_event_cb_t cb, void *cb_arg)
{
	int rc;
 	struct ldms_xprt *_x = x;
	struct ldms_conn_msg msg;
	__ldms_xprt_conn_msg_init(x, &msg);
	_x->event_cb = cb;
	_x->event_cb_arg = cb_arg;
	ldms_xprt_get(x, "connect");
	rc = zap_connect(_x->zap_ep, sa, sa_len,
			 (void*)&msg, sizeof(msg.ver) + strlen(msg.auth_name) + 1);
	if (rc) {
		__ldms_xprt_resource_free(x);
		ldms_xprt_put(x, "connect");
	}
	return rc;
}

int ldms_xprt_connect(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
			ldms_event_cb_t cb, void *cb_arg)
{
	return x->ops.connect(x, sa, sa_len, cb, cb_arg);
}

int ldms_rail_connect_by_name(ldms_t x, const char *host, const char *port,
			      ldms_event_cb_t cb, void *cb_arg);

int ldms_xprt_connect_by_name(ldms_t x, const char *host, const char *port,
			      ldms_event_cb_t cb, void *cb_arg)
{
	/* `x` from the application is actually a rail handle. Moving
	 * the implementation into ldms_rail.c */
	return ldms_rail_connect_by_name(x, host, port, cb, cb_arg);
}

static int __ldms_xprt_listen(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
		ldms_event_cb_t cb, void *cb_arg)
{
	x->xtype = LDMS_XTYPE_PASSIVE_XPRT;
	x->event_cb = cb;
	x->event_cb_arg = cb_arg;
	return zap_listen(x->zap_ep, sa, sa_len);
}


int ldms_xprt_listen(ldms_t x, struct sockaddr *sa, socklen_t sa_len,
		ldms_event_cb_t cb, void *cb_arg)
{
	return x->ops.listen(x, sa, sa_len, cb, cb_arg);
}

int ldms_xprt_listen_by_name(ldms_t x, const char *host, const char *port_no,
		ldms_event_cb_t cb, void *cb_arg)
{
	int rc;
	struct addrinfo *ai_list, *ai, *aitr;
	struct addrinfo hints = {
		.ai_socktype = SOCK_STREAM,
		.ai_flags = AI_PASSIVE,
	};
	rc = getaddrinfo(host, port_no, &hints, &ai_list);
	if (rc)
		return EHOSTUNREACH;
	ai = NULL;
	/* Prefer the first IPv6 address */
	for (aitr = ai_list; aitr; aitr = aitr->ai_next) {
		if (aitr->ai_family == AF_INET6) {
			ai = aitr;
			break;
		}
	}
	if (!ai)
		ai = ai_list;
	rc = ldms_xprt_listen(x, ai->ai_addr, ai->ai_addrlen, cb, cb_arg);
	freeaddrinfo(ai);
	return rc;
}

static ldms_set_t __ldms_xprt_set_by_name(ldms_t x, const char *set_name)
{
	struct ldms_set *set;
	struct rbn *rbn;

	assert(XTYPE_IS_LEGACY(x->xtype));

	__ldms_set_tree_lock();
	set = __ldms_find_local_set(set_name);
	__ldms_set_tree_unlock();
	if (!set)
		return NULL;
	pthread_mutex_lock(&x->lock);
	rbn = rbt_find(&x->set_coll, set);
	if (!rbn) {
		ref_put(&set->ref, "__ldms_find_local_set");
		set = NULL;
	}
	pthread_mutex_unlock(&x->lock);
	return set;
}

extern ldms_set_t ldms_xprt_set_by_name(ldms_t x, const char *set_name)
{
	return x->ops.set_by_name(x, set_name);
}

void __ldms_xprt_term(struct ldms_xprt *x)
{
	/* this is so that we call zap_close() exactly once */
	if (0 != __sync_fetch_and_or(&x->term, 1))
		return;
	if (x->zap_ep)
		zap_close(x->zap_ep);
}

int ldms_xprt_term(int sec)
{
	int i, rc, tmp;
	zap_t z;
	rc = 0;
	for (i = 0; i < ldms_zap_tbl_n; i++) {
		z = ldms_zap_tbl[i].zap;
		tmp = zap_term(z, sec);
		if (tmp)
			rc = tmp;
	}
	return rc;
}

static int __ldms_xprt_sockaddr(ldms_t x, struct sockaddr *local_sa,
		       struct sockaddr *remote_sa,
		       socklen_t *sa_len)
{
	zap_err_t zerr;
	if (!x->zap_ep)
		return ENOTCONN;
	zerr = zap_get_name(x->zap_ep, (struct sockaddr *)local_sa,
				(struct sockaddr *)remote_sa, sa_len);
	return zap_zerr2errno(zerr);
}

int ldms_xprt_sockaddr(ldms_t x, struct sockaddr *local_sa,
		       struct sockaddr *remote_sa,
		       socklen_t *sa_len)
{
	return x->ops.sockaddr(x, local_sa, remote_sa, sa_len);
}

/* The implementation is in ldms_rail.c. */
extern int sockaddr2ldms_addr(struct sockaddr *sa, struct ldms_addr *la);
int ldms_xprt_addr(ldms_t x, struct ldms_addr *local_addr,
			    struct ldms_addr *remote_addr)
{
	int rc;
	struct sockaddr_storage local_so, remote_so;
	socklen_t so_len = sizeof(local_so);

	rc = ldms_xprt_sockaddr(x, (void*)&local_so, (void*)&remote_so, &so_len);
	if (rc)
		return rc;
	if (local_addr) {
		rc = sockaddr2ldms_addr((void*)&local_so, local_addr);
		if (rc)
			return rc;
	}
	if (remote_addr) {
		rc = sockaddr2ldms_addr((void*)&remote_so, remote_addr);
		if (rc)
			return rc;
	}
	return 0;
}

int __ldms_xprt_get_threads(ldms_t x, pthread_t *out, int n)
{
	if (n < 1)
		return -EINVAL;
	if (x->zap_ep) {
		out[0] = zap_ep_thread(x->zap_ep);
	}
	return 1;
}

zap_ep_t __ldms_xprt_get_zap_ep(ldms_t x)
{
	return x->zap_ep;
}

zap_ep_t ldms_xprt_get_zap_ep(ldms_t x)
{
	return x->ops.get_zap_ep(x);
}

int ldms_xprt_get_threads(ldms_t x, pthread_t *out, int n)
{
	return x->ops.get_threads(x, out, n);
}

enum ldms_thrstat_op_e req2thrstat_op_tbl[] = {
	[LDMS_CMD_DIR]                = LDMS_THRSTAT_OP_DIR_REQ,
	[LDMS_CMD_DIR_CANCEL]         = LDMS_THRSTAT_OP_DIR_REQ,
	[LDMS_CMD_LOOKUP]             = LDMS_THRSTAT_OP_LOOKUP_REQ,
	[LDMS_CMD_REQ_NOTIFY]         = LDMS_THRSTAT_OP_OTHER,
	[LDMS_CMD_CANCEL_NOTIFY]      = LDMS_THRSTAT_OP_OTHER,
	[LDMS_CMD_SEND_MSG]           = LDMS_THRSTAT_OP_SEND_MSG,
	[LDMS_CMD_AUTH_MSG]           = LDMS_THRSTAT_OP_AUTH,
	[LDMS_CMD_CANCEL_PUSH]        = LDMS_THRSTAT_OP_OTHER,
	[LDMS_CMD_AUTH]               = LDMS_THRSTAT_OP_AUTH,
	[LDMS_CMD_SET_DELETE]         = LDMS_THRSTAT_OP_SET_DELETE_REQ,
	[LDMS_CMD_SEND_QUOTA]         = LDMS_THRSTAT_OP_OTHER,

	[LDMS_CMD_DIR_REPLY]          = LDMS_THRSTAT_OP_DIR_REPLY,
	[LDMS_CMD_DIR_UPDATE_REPLY]   = LDMS_THRSTAT_OP_UPDATE_REPLY,
	[LDMS_CMD_LOOKUP_REPLY]       = LDMS_THRSTAT_OP_LOOKUP_REPLY,
	[LDMS_CMD_AUTH_CHALLENGE_REPLY] = LDMS_THRSTAT_OP_AUTH,
	[LDMS_CMD_AUTH_APPROVAL_REPLY]  = LDMS_THRSTAT_OP_AUTH,
	[LDMS_CMD_AUTH_REPLY]           = LDMS_THRSTAT_OP_AUTH,
	[LDMS_CMD_LAST]			= LDMS_THRSTAT_OP_OTHER
};

char *ldms_thrstat_op_str_tbl[] = {
	[LDMS_THRSTAT_OP_OTHER]			= "Other",
	[LDMS_THRSTAT_OP_CONNECT_SETUP]		= "Connecting",
	[LDMS_THRSTAT_OP_DIR_REQ]		= "Dir Requests",
	[LDMS_THRSTAT_OP_DIR_REPLY]		= "Dir Replies",
	[LDMS_THRSTAT_OP_LOOKUP_REQ]		= "Lookup Requests",
	[LDMS_THRSTAT_OP_LOOKUP_REPLY]		= "Lookup Replies",
	[LDMS_THRSTAT_OP_UPDATE_REQ]		= "Update Requests",
	[LDMS_THRSTAT_OP_UPDATE_REPLY]		= "Update Completes",
	[LDMS_THRSTAT_OP_MSG_DATA]		= "Message Service Data",
	[LDMS_THRSTAT_OP_MSG_CLIENT]		= "Message Service Client",
	[LDMS_THRSTAT_OP_PUSH_REQ]		= "Push Requests",
	[LDMS_THRSTAT_OP_PUSH_REPLY]		= "Push Replies",
	[LDMS_THRSTAT_OP_SET_DELETE_REQ]	= "Set Delete Requests",
	[LDMS_THRSTAT_OP_SET_DELETE_REPLY]	= "Set Delete Replies",
	[LDMS_THRSTAT_OP_SEND_MSG]		= "Send Messages",
	[LDMS_THRSTAT_OP_RECV_MSG]		= "Receive Messages",
	[LDMS_THRSTAT_OP_AUTH]			= "Authentication",
	[LDMS_THRSTAT_OP_DISCONNECTED]		= "Disconnecting",
};
char *ldms_thrstat_op_str(enum ldms_thrstat_op_e e)
{
	return ldms_thrstat_op_str_tbl[e];
}

struct ldms_thrstat_result *ldms_thrstat_result_get(uint64_t interval_s)
{
	struct ldms_thrstat *lstats;
	struct zap_thrstat_result *zres;
	struct ldms_thrstat_result *lres;
	struct ovis_thrstats_result *res;
	struct timespec now;
	uint64_t ldms_xprt_time;
	int i, j;

	(void)clock_gettime(CLOCK_REALTIME, &now);

	zres = zap_thrstat_get_result(interval_s);

	lres = calloc(1, sizeof(*lres) +
		zres->count * sizeof(struct ldms_thrstat_result_entry));
	if (!lres)
		goto out;
	lres->_zres = zres;
	lres->count = zres->count;
	for (i = 0; i < zres->count; i++) {
		lres->entries[i].zap_res = &zres->entries[i];
		res = &zres->entries[i].res;
		lres->entries[i].idle = res->idle_tot;

		lstats = (struct ldms_thrstat *)res->app_ctxt;
		if (!lstats)
			continue;
		ldms_xprt_time = 0;
		for (j = 0; j < LDMS_THRSTAT_OP_COUNT; j++) {
			lres->entries[i].ops[j] = lstats->ops[j].total;
			ldms_xprt_time += lstats->ops[j].total;
		}
		lres->entries[i].zap_time = res->active_tot;
		if (!res->waiting) {
			/* The thread is active. */
			lres->entries[i].zap_time += ldms_timespec_diff_us(
							&res->wait_end, &now);
		}
		lres->entries[i].zap_time -= ldms_xprt_time;
	}
out:
	return lres;
}

void ldms_thrstat_result_free(struct ldms_thrstat_result *res)
{
	if (!res)
		return;
	zap_thrstat_free_result(res->_zres);
	free(res);
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
