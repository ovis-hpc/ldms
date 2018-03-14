/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2017 Sandia Corporation. All rights reserved.
 *
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
#include <mmalloc/mmalloc.h>

#include "ovis_util/os_util.h"
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_private.h"

#define LDMS_XPRT_AUTH_GUARD(x) (((x)->auth_flag != LDMS_XPRT_AUTH_DISABLE) && \
				 ((x)->auth_flag != LDMS_XPRT_AUTH_APPROVED))

static struct ldms_rbuf_desc *ldms_lookup_rbd(struct ldms_xprt *, struct ldms_set *);

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
	assert(x->ref_count > 0);
	__sync_add_and_fetch(&x->ref_count, 1);
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
	x = ldms_xprt_get(x);
 out:
	pthread_mutex_unlock(&xprt_list_lock);
	return x;
}

ldms_t ldms_xprt_next(ldms_t x)
{
	pthread_mutex_lock(&xprt_list_lock);
	x = LIST_NEXT(x, xprt_link);
	if (!x)
		goto out;
	x = ldms_xprt_get(x);
 out:
	pthread_mutex_unlock(&xprt_list_lock);
	return x;
}

ldms_t ldms_xprt_by_remote_sin(struct sockaddr_in *sin)
{
	struct sockaddr_storage ss_local, ss_remote;
	socklen_t socklen;

	ldms_t l, next_l;
	l = ldms_xprt_first();
	while (l) {
		int rc = zap_get_name(l->zap_ep,
				      (struct sockaddr *)&ss_local,
				      (struct sockaddr *)&ss_remote,
				      &socklen);
		if (rc)
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

size_t __ldms_xprt_max_msg(struct ldms_xprt *x)
{
	return zap_max_msg(x->zap);
}

/* Caller must call with the ldms xprt lock held */
struct ldms_context *__ldms_alloc_ctxt(struct ldms_xprt *x, size_t sz,
		ldms_context_type_t type)
{
	struct ldms_context *ctxt;
	ctxt = calloc(1, sz);
	if (!ctxt) {
		x->log("%s(): Out of memory\n", __func__);
		return ctxt;
	}
#ifdef CTXT_DEBUG
	x->log("%s(): x %p: alloc ctxt %p: type %d\n", __func__, x, ctxt, type);
#endif /* CTXT_DEBUG */
	ctxt->type = type;
	TAILQ_INSERT_TAIL(&x->ctxt_list, ctxt, link);
	return ctxt;
}

/* Caller must call with the ldms xprt lock held */
void __ldms_free_ctxt(struct ldms_xprt *x, struct ldms_context *ctxt)
{
	TAILQ_REMOVE(&x->ctxt_list, ctxt, link);
	if (ctxt->type == LDMS_CONTEXT_LOOKUP) {
		if (ctxt->lookup.path) {
			free(ctxt->lookup.path);
			ctxt->lookup.path = NULL;
		}
	}
	free(ctxt);
}

static void send_dir_update(struct ldms_xprt *x,
			    enum ldms_dir_type t,
			    const char *set_name)
{
	size_t len;
	int set_count;
	int set_list_sz;
	int rc = 0;
	struct ldms_reply *reply;

	switch (t) {
	case LDMS_DIR_LIST:
		__ldms_get_local_set_list_sz(&set_count, &set_list_sz);
		break;
	case LDMS_DIR_DEL:
	case LDMS_DIR_ADD:
		set_count = 1;
		set_list_sz = strlen(set_name) + 1;
		break;
	}

	len = sizeof(struct ldms_reply_hdr)
		+ sizeof(struct ldms_dir_reply)
		+ set_list_sz;

	reply = malloc(len);
	if (!reply) {
		x->log("Memory allocation failure "
		       "in dir update of peer.\n");
		return;
	}

	switch (t) {
	case LDMS_DIR_LIST:
		rc = __ldms_get_local_set_list(reply->dir.set_list,
					       set_list_sz,
					       &set_count, &set_list_sz);
		break;
	case LDMS_DIR_DEL:
	case LDMS_DIR_ADD:
		memcpy(reply->dir.set_list, set_name, set_list_sz);
		break;
	}

	reply->hdr.xid = x->remote_dir_xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_UPDATE_REPLY);
	reply->hdr.rc = htonl(rc);
	reply->dir.type = htonl(t);
	reply->dir.set_count = htonl(set_count);
	reply->dir.set_list_len = htonl(set_list_sz);
	reply->hdr.len = htonl(len);

#ifdef DEBUG
	x->log("%s(): x %p: remote dir ctxt %p\n",
			__func__, x, (void *)x->remote_dir_xid);
#endif /* DEBUG */

	zap_err_t zerr;
	zerr = zap_send(x->zap_ep, reply, len);
	if (zerr != ZAP_ERR_OK) {
		x->log("%s: x %p: zap_send synchronously error. '%s'\n",
				__FUNCTION__, x, zap_err_str(zerr));
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
		x->log("%s: zap_send synchronously error. '%s'\n",
				__FUNCTION__, zap_err_str(zerr));
		ldms_xprt_close(x);
	}
	free(reply);
	return;
}

static void dir_update(const char *set_name, enum ldms_dir_type t)
{
	struct ldms_xprt *x, *next_x;
	x = (struct ldms_xprt *)ldms_xprt_first();
	while (x) {
		if (x->remote_dir_xid)
			send_dir_update(x, t, set_name);
		next_x = (struct ldms_xprt *)ldms_xprt_next(x);
		ldms_xprt_put(x);
		x = next_x;
	}
}

void __ldms_dir_add_set(const char *set_name)
{
	dir_update(set_name, LDMS_DIR_ADD);
}

void __ldms_dir_del_set(const char *set_name)
{
	dir_update(set_name, LDMS_DIR_DEL);
}

void ldms_xprt_close(ldms_t x)
{
#ifdef DEBUG
	x->log("%s(): closing x %p\n", __func__, x);
#endif /* DEBUG */
	x->remote_dir_xid = 0;
	__ldms_xprt_term(x);
}

void __ldms_xprt_resource_free(struct ldms_xprt *x)
{
	pthread_mutex_lock(&x->lock);

	struct ldms_context *dir_ctxt;
	if (x->local_dir_xid) {
		dir_ctxt = (struct ldms_context *)(unsigned long)x->local_dir_xid;
		__ldms_free_ctxt(x, dir_ctxt);
	}
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

	struct ldms_rbuf_desc *rbd;
	while (!LIST_EMPTY(&x->rbd_list)) {
		rbd = LIST_FIRST(&x->rbd_list);
		if (rbd->type == LDMS_RBD_LOCAL || rbd->type == LDMS_RBD_TARGET)
			__ldms_free_rbd(rbd);
		else
			__ldms_rbd_xprt_release(rbd);
	}
	if (x->auth)
		ldms_auth_free(x->auth);
	pthread_mutex_unlock(&x->lock);
}

void ldms_xprt_put(ldms_t x)
{
	assert(x->ref_count);
	/*
	 * The xprt could be destroyed any time ldms_xprt_put is called.
	 * We need to take the xprt list lock to prevent the race
	 * between destroying the xprt and accessing the xprt from the list
	 * on another thread.
	 */
	pthread_mutex_lock(&xprt_list_lock);
	if (0 == __sync_sub_and_fetch(&x->ref_count, 1)) {
		LIST_REMOVE(x, xprt_link);
#ifdef DEBUG
		x->xprt_link.le_next = 0;
		x->xprt_link.le_prev = 0;
#endif /* DEBUG */
		__ldms_xprt_resource_free(x);
		if (x->zap_ep)
			zap_free(x->zap_ep);
		sem_destroy(&x->sem);
		free(x);
	}
	pthread_mutex_unlock(&xprt_list_lock);
}

struct make_dir_arg {
	int reply_size;		/* size of reply in total */
	struct ldms_reply *reply;
	struct ldms_xprt *x;
	int reply_count;	/* sets in this reply */
	char *set_list;		/* buffer for set names */
	ssize_t set_list_len;	/* current length of this buffer */
};

static int send_dir_reply_cb(struct ldms_set *set, void *arg)
{
	struct make_dir_arg *mda = arg;
	int len;
	zap_err_t zerr;
	uid_t set_uid = __le32_to_cpu(set->meta->uid);
	gid_t set_gid = __le32_to_cpu(set->meta->gid);
	uint32_t set_perm = __le32_to_cpu(set->meta->perm);

	if (0 != ldms_access_check(mda->x, LDMS_ACCESS_READ,
				  set_uid, set_gid, set_perm))
		return 0; /* access denied, skip it */

	/* keep filling the buffer until it is full */
	len = strlen(get_instance_name(set->meta)->name) + 1;
	if (mda->reply_size + len < __ldms_xprt_max_msg(mda->x)) {
		mda->reply_size += len;
		strcpy(mda->set_list, get_instance_name(set->meta)->name);
		mda->set_list += len;
		mda->set_list_len += len;
		mda->reply_count ++;
		return 0;
	}

	/* buffer full, need a flush */
	mda->reply->dir.more = 1;
	mda->reply->dir.set_count = htonl(mda->reply_count);
	mda->reply->dir.set_list_len = htonl(mda->set_list_len);
	mda->reply->hdr.len = htonl(mda->reply_size);

	zerr = zap_send(mda->x->zap_ep, mda->reply, mda->reply_size);
	if (zerr != ZAP_ERR_OK) {
		mda->x->log("%s: zap_send synchronously error. '%s'\n",
				__FUNCTION__, zap_err_str(zerr));
		ldms_xprt_close(mda->x);
		return zerr;
	}

	/* Change the dir type to ADD for the subsequent sends */
	mda->reply->dir.type = htonl(LDMS_DIR_ADD);

	/* Initialize arg for remainder of walk */
	mda->reply_size = sizeof(struct ldms_reply_hdr) +
		sizeof(struct ldms_dir_reply) +
		len;
	strcpy(mda->reply->dir.set_list, get_instance_name(set->meta)->name);
	mda->set_list = mda->reply->dir.set_list + len;
	mda->set_list_len = len;
	mda->reply_count = 1;
	return 0;
}

static void process_dir_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct make_dir_arg arg;
	size_t len;
	int set_count;
	int set_list_sz;
	int rc;
	zap_err_t zerr;
	struct ldms_reply reply_;
	struct ldms_reply *reply = NULL;

	__ldms_set_tree_lock();
	if (req->dir.flags)
		/* Register for directory updates */
		x->remote_dir_xid = req->hdr.xid;
	else
		/* Cancel any previous dir update */
		x->remote_dir_xid = 0;

	__ldms_get_local_set_list_sz(&set_count, &set_list_sz);
	if (!set_count) {
		rc = 0;
		goto out;
	}

	len = sizeof(struct ldms_reply_hdr)
		+ sizeof(struct ldms_dir_reply)
		+ set_list_sz;
	if (len > __ldms_xprt_max_msg(x))
		len = __ldms_xprt_max_msg(x);
	reply = malloc(len);
	if (!reply) {
		rc = ENOMEM;
		len = sizeof(struct ldms_reply_hdr);
		goto out;
	}

	/* Initialize the set_list walking callback argument */
	arg.reply_size = sizeof(struct ldms_reply_hdr) +
		sizeof(struct ldms_dir_reply);
	arg.reply = reply;
	memset(reply, 0, arg.reply_size);
	arg.x = x;
	arg.reply_count = 0;
	arg.set_list = reply->dir.set_list;
	arg.set_list_len = 0;

	/* Initialize the reply header */
	reply->hdr.xid = req->hdr.xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_REPLY);
	reply->dir.type = htonl(LDMS_DIR_LIST);

	/* go fill the reply buffer ... flushed it if full along the way */
	rc = __ldms_for_all_sets(send_dir_reply_cb, &arg);
	if (rc)
		goto out;
	if (0 == arg.reply_count)
		goto out; /* nothing to send back */

	__ldms_set_tree_unlock();

	/* last chunk of unflushed buffer */
	arg.reply->dir.more = 0;
	arg.reply->dir.set_count = htonl(arg.reply_count);
	arg.reply->dir.set_list_len = htonl(arg.set_list_len);
	arg.reply->hdr.len = htonl(arg.reply_size);

	zerr = zap_send(x->zap_ep, arg.reply, arg.reply_size);
	if (zerr != ZAP_ERR_OK) {
		x->log("%s: zap_send synchronously error. '%s'\n",
				__FUNCTION__, zap_err_str(zerr));
		ldms_xprt_close(arg.x);
	}

	free(reply);
	return;

 out:
	__ldms_set_tree_unlock();
	if (reply)
		free(reply);
	reply = &reply_;
	len = sizeof(struct ldms_reply_hdr)
		+ sizeof(struct ldms_dir_reply);
	reply->hdr.xid = req->hdr.xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_REPLY);
	reply->hdr.rc = htonl(rc);
	reply->dir.more = 0;
	reply->dir.type = htonl(LDMS_DIR_LIST);
	reply->dir.set_count = 0;
	reply->dir.set_list_len = 0;
	reply->hdr.len = htonl(len);

	zerr = zap_send(x->zap_ep, reply, len);
	if (zerr != ZAP_ERR_OK) {
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


static void
process_req_notify_request(struct ldms_xprt *x, struct ldms_request *req)
{

	struct ldms_rbuf_desc *r =
		(struct ldms_rbuf_desc *)req->req_notify.set_id;

	r->remote_notify_xid = req->hdr.xid;
	r->notify_flags = ntohl(req->req_notify.flags);
}

static void
process_cancel_notify_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct ldms_rbuf_desc *r =
		(struct ldms_rbuf_desc *)req->cancel_notify.set_id;
	r->remote_notify_xid = 0;
}

static void
process_cancel_push_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct ldms_rbuf_desc *r;
	struct ldms_rbuf_desc *push_rbd;
	struct ldms_set *set;
	uint64_t remote_set_id;
	int rc;

	r = (struct ldms_rbuf_desc *)req->cancel_push.set_id;
	push_rbd = (struct ldms_rbuf_desc *)r->remote_set_id;
	if (!push_rbd || 0 == (push_rbd->push_flags & LDMS_RBD_F_PUSH))
		return;
	set = r->set;

	/* Peer will get push notification with UPD_F_PUSH_LAST set. */
	assert(!(push_rbd->push_flags & LDMS_RBD_F_PUSH_CANCEL));
	remote_set_id = push_rbd->remote_set_id;

	pthread_mutex_lock(&xprt_list_lock);
	pthread_mutex_lock(&set->lock);

	/*
	 * If any remaining RBD still want automatic push updates,
	 * leave it on for the set, otherwise, turn it off for the set
	 */
	r->remote_set_id = 0;

	__ldms_free_rbd(push_rbd);

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
	rc = zap_send(x->zap_ep, &reply, len);
	ldms_xprt_put(x);
	return;
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
		/* Allocate a new RBD for this set */
		rbd = __ldms_alloc_rbd(x, set, LDMS_RBD_LOCAL);
		if (!rbd)
			goto err_0;
	}
	ldms_name_t name = get_instance_name(set->meta);
	ldms_name_t schema = get_schema_name(set->meta);
	size_t msg_len =
		sizeof(struct ldms_rendezvous_hdr)
		+ sizeof(struct ldms_rendezvous_lookup_param)
		+ name->len + schema->len;
	struct ldms_rendezvous_msg *msg = malloc(msg_len);
	if (!msg)
		goto err_1;

	msg->hdr.xid = xid;
	msg->hdr.cmd = htonl(LDMS_XPRT_RENDEZVOUS_LOOKUP);
	msg->hdr.len = msg_len;
	msg->lookup.set_id = (uint64_t)(unsigned long)rbd;
	strcpy(msg->lookup.schema_inst_name, schema->name);
	strcpy(msg->lookup.schema_inst_name + schema->len, name->name);
	msg->lookup.schema_len = htonl(schema->len);
	msg->lookup.inst_name_len = htonl(name->len);
	msg->lookup.more = htonl(more);
	msg->lookup.data_len = htonl(__le32_to_cpu(set->meta->data_sz));
	msg->lookup.meta_len = htonl(__le32_to_cpu(set->meta->meta_sz));
	msg->lookup.card = htonl(__le32_to_cpu(set->meta->card));

#ifdef DEBUG
	x->log("%s(): x %p: sharing ... remote lookup ctxt %p\n",
			__func__, x, (void *)xid);
#endif /* DEBUG */
	zap_err_t zerr = zap_share(x->zap_ep, rbd->lmap, (const char *)msg, msg_len);
	if (zerr != ZAP_ERR_OK) {
		x->log("%s: x %p: zap_share synchronously error. '%s'\n",
				__FUNCTION__, x, zap_err_str(zerr));
		free(msg);
		rc = zerr;
		goto err_1;
	}
	pthread_mutex_unlock(&x->lock);
	free(msg);
	return 0;
 err_1:
	__ldms_free_rbd(rbd);
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
	rc = zap_send(x->zap_ep, &hdr, sizeof(hdr));
	if (rc != ZAP_ERR_OK) {
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

static int do_read_all(ldms_t x, ldms_set_t s, size_t len,
			ldms_update_cb_t cb, void *arg)
{
	TF();
	struct ldms_context *ctxt;
	int rc;
	if (!len)
		len = __ldms_set_size_get(s->set);

	/* Prevent x being destroyed if DISCONNECTED is delivered in another thread */
	ldms_xprt_get(x);
	pthread_mutex_lock(&x->lock);
	ctxt = __ldms_alloc_ctxt(x, sizeof(*ctxt), LDMS_CONTEXT_UPDATE);
	if (!ctxt) {
		rc = ENOMEM;
		goto out;
	}
	ctxt->update.s = s;
	ctxt->update.cb = cb;
	ctxt->update.arg = arg;

	rc = zap_read(x->zap_ep, s->rmap, zap_map_addr(s->rmap),
			s->lmap, zap_map_addr(s->lmap), len, ctxt);
	if (rc)
		__ldms_free_ctxt(x, ctxt);
out:
	pthread_mutex_unlock(&x->lock);
	ldms_xprt_put(x);
	return rc;
}

static int do_read_data(ldms_t x, ldms_set_t s, size_t len, ldms_update_cb_t cb, void*arg)
{
	int rc;
	struct ldms_context *ctxt;
	TF();

	/* Prevent x being destroyed if DISCONNECTED is delivered in another thread */
	assert(x == s->xprt);
	ldms_xprt_get(x);

	pthread_mutex_lock(&x->lock);
	ctxt = __ldms_alloc_ctxt(x, sizeof(*ctxt), LDMS_CONTEXT_UPDATE);
	if (!ctxt) {
		rc = ENOMEM;
		goto out;
	}
	ctxt->update.s = s;
	ctxt->update.cb = cb;
	ctxt->update.arg = arg;
	size_t doff = (uint8_t *)s->set->data - (uint8_t *)s->set->meta;

	rc = zap_read(x->zap_ep, s->rmap, zap_map_addr(s->rmap) + doff,
		      s->lmap, zap_map_addr(s->lmap) + doff, len, ctxt);
	if (rc)
		__ldms_free_ctxt(x, ctxt);
out:
	pthread_mutex_unlock(&x->lock);
	ldms_xprt_put(x);
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
	uint32_t meta_meta_sz = __le32_to_cpu(set->meta->meta_sz);
	uint32_t meta_data_sz = __le32_to_cpu(set->meta->data_sz);

	zap_get_ep(x->zap_ep);	/* Released in handle_zap_read_complete() */
	if (meta_meta_gn == 0 || meta_meta_gn != data_meta_gn) {
		/* Update the metadata along with the data */
		rc = do_read_all(x, s, meta_meta_sz +
				 meta_data_sz, cb, arg);
	} else {
		rc = do_read_data(x, s, meta_data_sz, cb, arg);
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
			__ldms_xprt_term(x);
		}
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
	if (ctxt->lookup.cb)
		ctxt->lookup.cb(x, rc, 0, NULL, ctxt->lookup.cb_arg);

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

static
void __process_dir_reply(struct ldms_xprt *x, struct ldms_reply *reply,
		       struct ldms_context *ctxt, int more)
{
	int i;
	char *src, *dst;
	enum ldms_dir_type type = ntohl(reply->dir.type);
	int rc = ntohl(reply->hdr.rc);
	size_t len = ntohl(reply->dir.set_list_len);
	unsigned count = ntohl(reply->dir.set_count);
	ldms_dir_t dir = NULL;
	if (!ctxt->dir.cb)
		return;
	if (rc)
		goto out;
	dir = malloc(sizeof (*dir) +
		     (count * sizeof(char *)) + len);
	rc = ENOMEM;
	if (!dir)
		goto out;
	rc = 0;
	dir->type = type;
	dir->more = more;
	dir->set_count = count;
	src = reply->dir.set_list;
	dst = (char *)&dir->set_names[count];
	for (i = 0; i < count; i++) {
		dir->set_names[i] = dst;
		strcpy(dst, src);
		len = strlen(src) + 1;
		dst += len;
		src += len;
	}
out:
	/* Callback owns dir memory. */
	ctxt->dir.cb((ldms_t)x, rc, dir, ctxt->dir.cb_arg);
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
			    event, ctxt->req_notify.arg);
}

static void process_push_reply(struct ldms_xprt *x, struct ldms_reply *reply,
			      struct ldms_context *ctxt)
{
	struct ldms_rbuf_desc *push_rbd =
		(typeof(push_rbd))(unsigned long)reply->push.set_id;
	uint32_t data_off = ntohl(reply->push.data_off);
	uint32_t data_len = ntohl(reply->push.data_len);
	int rc;

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

void ldms_xprt_dir_free(ldms_t t, ldms_dir_t d)
{
	free(d);
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
	if (x->auth) {
		memcpy(msg->auth_name, x->auth->plugin->name, LDMS_AUTH_NAME_MAX);
	}
}

void __ldms_xprt_init(struct ldms_xprt *x, const char *name,
					ldms_log_fn_t log_fn);
static void ldms_zap_handle_conn_req(zap_ep_t zep)
{
	static char rej_msg[64] = "Not enough resources";
	struct sockaddr lcl, rmt;
	socklen_t xlen;
	struct ldms_conn_msg msg;
	char rmt_name[16];
	const char *tmp;
	int rc;
	zap_err_t zerr;
	zap_get_name(zep, &lcl, &rmt, &xlen);
	getnameinfo(&rmt, sizeof(rmt), rmt_name, 128, NULL, 0, NI_NUMERICHOST);

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

	char *data = 0;
	size_t datalen = 0;

	__ldms_xprt_conn_msg_init(x, &msg);

	zerr = zap_accept(zep, ldms_zap_auto_cb, (void*)&msg, sizeof(msg));
	if (zerr) {
		x->log("ERROR: cannot accept connection from %s.\n", rmt_name);
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

static void handle_zap_read_complete(zap_ep_t zep, zap_event_t ev)
{
	struct ldms_context *ctxt = ev->context;
	struct ldms_xprt *x = zap_get_ucontext(zep);
	switch (ctxt->type) {
	case LDMS_CONTEXT_UPDATE:
		if (ctxt->update.cb) {
			ctxt->update.cb((ldms_t)x, ctxt->update.s, ev->status,
					ctxt->update.arg);
			zap_put_ep(x->zap_ep); /* Taken in ldms_remote_update() */
		}
		break;
	case LDMS_CONTEXT_LOOKUP:
		if (ctxt->lookup.cb) {
			if (ev->status != ZAP_ERR_OK) {
				/*
				 * Application doesn't have the set handle yet,
				 * so delete the set.
				 */
				ldms_set_delete(ctxt->lookup.s);
				ctxt->lookup.s = NULL;
			} else {
				ldms_set_publish(ctxt->lookup.s);
			}
			ctxt->lookup.cb((ldms_t)x, ev->status, ctxt->lookup.more, ctxt->lookup.s,
					ctxt->lookup.cb_arg);
			if (!ctxt->lookup.more) {
				zap_put_ep(x->zap_ep);	/* Taken in __ldms_remote_lookup() */
#ifdef DEBUG
				pthread_mutex_lock(&x->lock);
				assert(x->active_lookup > 0);
				x->active_lookup--;
				pthread_mutex_unlock(&x->lock);
				x->log("DEBUG: read_complete: put ref %p: "
						"active_lookup = %d\n",
						x->zap_ep, x->active_lookup);
#endif /* DEBUG */
			}

		}
		break;
	default:
		assert(0 == "Invalid context type in zap read completion.");
	}
	pthread_mutex_lock(&x->lock);
	__ldms_free_ctxt(x, ctxt);
	pthread_mutex_unlock(&x->lock);
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
	char *name;
	int rc = 0;

	if (ctxt->lookup.flags & LDMS_LOOKUP_BY_SCHEMA)
		name = lu->schema_inst_name;
	else
		name = lu->schema_inst_name + lu->schema_len;

	if (ctxt->lookup.flags & LDMS_LOOKUP_RE) {
		rc = regcomp(&regex, ctxt->lookup.path, REG_EXTENDED | REG_NOSUB);
		if (rc) {
			char errstr[512];
			(void)regerror(rc, &regex, errstr, sizeof(errstr));
			x->log("%s(): %s\n", __func__, errstr);
			assert(0 == "bad regcomp in __is_lookup_name_good");
		}

		rc = regexec(&regex, name, 0, NULL, 0);
	} else {
		rc = strcmp(ctxt->lookup.path, name);
	}

	return (rc == 0);
}
#endif /* DEBUG */


static void handle_rendezvous_lookup(zap_ep_t zep, zap_event_t ev,
				     struct ldms_xprt *x,
				     struct ldms_rendezvous_msg *lm)
{
	struct ldms_rendezvous_lookup_param *lu = &lm->lookup;
	struct ldms_context *ctxt = (void*)lm->hdr.xid;
	ldms_set_t rbd = NULL;
	int rc;

	lu->schema_len = ntohl(lu->schema_len);
	lu->inst_name_len = ntohl(lu->inst_name_len);
#ifdef DEBUG
	if (!__is_lookup_name_good(x, lu, ctxt)) {
		x->log("%s(): The schema or instance name in the lookup "
				"message sent by the peer does not "
				"match the lookup request\n", __func__);
		assert(0);
	}
#endif /* DEBUG */

	const char *schema_name = lu->schema_inst_name;
	const char *inst_name = lu->schema_inst_name + lu->schema_len;

	__ldms_set_tree_lock();
	struct ldms_set *lset = __ldms_find_local_set(inst_name);
	if (lset) {
		ldms_name_t lschema = get_schema_name(lset->meta);
		if (0 != strcmp(schema_name, lschema->name)) {
			/* Two sets have the same name but different schema */
			rc = EINVAL;
			goto unlock_out;
		}

		LIST_FOREACH(rbd, &x->rbd_list, xprt_link) {
			if (rbd->set == lset) {
				rc = EEXIST;
				goto unlock_out;
			}
		}

	} else {
		lset = __ldms_create_set(inst_name, schema_name,
				       ntohl(lu->meta_len), ntohl(lu->data_len),
				       ntohl(lu->card),
				       LDMS_SET_F_REMOTE);
		if (!lset) {
			rc = errno;
			goto unlock_out;
		}
	}
	__ldms_set_tree_unlock();

	/* Bind this set to a new RBD. We will initiate RDMA_READ */
	rbd = __ldms_alloc_rbd(x, lset, LDMS_RBD_INITIATOR);
	if (!rbd) {
		rc = ENOMEM;
		goto out_1;
	}

	rbd->rmap = ev->map;
	rbd->remote_set_id = lm->lookup.set_id;

	pthread_mutex_lock(&x->lock);
	struct ldms_context *rd_ctxt;
	if (lu->more) {
		rd_ctxt = __ldms_alloc_ctxt(x, sizeof(*rd_ctxt),
				LDMS_CONTEXT_LOOKUP);
		if (!rd_ctxt) {
			x->log("%s(): Out of memory\n", __func__);
			rc = ENOMEM;
			pthread_mutex_unlock(&x->lock);
			goto out_1;
		}

		rd_ctxt->sem = ctxt->sem;
		rd_ctxt->sem_p = ctxt->sem_p;
		rd_ctxt->rc = ctxt->rc;
		rd_ctxt->type = ctxt->type;
		rd_ctxt->lookup = ctxt->lookup;
		rd_ctxt->lookup.path = strdup(ctxt->lookup.path);
		if (!rd_ctxt->lookup.path) {
			rc = ENOMEM;
			__ldms_free_ctxt(x, rd_ctxt);
			pthread_mutex_unlock(&x->lock);
			goto out_1;
		}
	} else {
		rd_ctxt = ctxt;
	}
	rd_ctxt->lookup.s = rbd;
	rd_ctxt->lookup.more = ntohl(lu->more);
	pthread_mutex_unlock(&x->lock);

	if (zap_read(zep,
		     rbd->rmap, zap_map_addr(rbd->rmap),
		     rbd->lmap, zap_map_addr(rbd->lmap),
		     __le32_to_cpu(rbd->set->meta->meta_sz),
		     rd_ctxt)) {
		rc = EIO;
		goto out_2;
	}
	return;

 unlock_out:
	__ldms_set_tree_unlock();
	goto out;
 out_2:
	if (lu->more) {
		pthread_mutex_lock(&x->lock);
		__ldms_free_ctxt(x, ctxt);
		pthread_mutex_unlock(&x->lock);
	}

 out_1:
	ldms_set_delete(rbd);
 out:
	if (ctxt->lookup.cb)
		ctxt->lookup.cb(x, rc, 0, rbd, ctxt->lookup.cb_arg);
	if (!lu->more) {
		zap_put_ep(x->zap_ep);	/* Taken in __ldms_remote_lookup() */
		pthread_mutex_lock(&x->lock);
		__ldms_free_ctxt(x, ctxt);
#ifdef DEBUG
		assert(x->active_lookup);
		x->active_lookup--;
		x->log("DEBUG: rendezvous error: put ref %p: "
				"active_lookup = %d\n",
				x->zap_ep, x->active_lookup);
#endif /* DEBUG */
		pthread_mutex_unlock(&x->lock);
	}
}

static void handle_rendezvous_push(zap_ep_t zep, zap_event_t ev,
				   struct ldms_xprt *x,
				   struct ldms_rendezvous_msg *lm)
{
	struct ldms_rendezvous_push_param *push = &lm->push;
	struct ldms_rbuf_desc *my_rbd, *push_rbd;
	int rc;
	ldms_set_t set_t;

	/* Get the RBD provided in lookup to the peer */
	my_rbd = (void *)push->lookup_set_id;

	/* See if we already have a push RBD for this set and
	 * transport.
	 */
	LIST_FOREACH(push_rbd, &x->rbd_list, xprt_link) {
		if (push_rbd->set == my_rbd->set) {
			if (push_rbd->push_flags & LDMS_RBD_F_PUSH) {
				/* Update the push flags, but otherwise, do nothing */
				goto out;
			}
		}
	}

	/* We will be the target of RDMA_WRITE */
	push_rbd = __ldms_alloc_rbd(x, my_rbd->set, LDMS_RBD_TARGET);
	if (!push_rbd) {
		struct ldms_xprt *x = zap_get_ucontext(zep);
		x->log("handle_rendezvous_push: __ldms_alloc_rbd out of memory\n");
		return;
	}
	push_rbd->rmap = ev->map;
	push_rbd->remote_set_id = push->push_set_id;
	/* When the peer cancels the push, it will be my_rbd, not the push_rbd */
	my_rbd->remote_set_id = (uint64_t)(unsigned long)push_rbd;
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
	int rc = 0;
	if (data_len < sizeof(*ver)) {
		snprintf(err_msg, err_msg_len, "Bad conn msg");
		return EINVAL;
	}
	if (!ldms_version_check(&msg->ver)) {
		snprintf(err_msg, err_msg_len, "Version mismatch");
		return EINVAL;
	}
	if (strncmp(msg->auth_name, x->auth->plugin->name, LDMS_AUTH_NAME_MAX)){
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
	int rc;
	struct ldms_xprt_event event = {
			.type = LDMS_XPRT_EVENT_LAST,
			.data = NULL,
			.data_len = 0};
	char rej_msg[128];
	struct ldms_xprt *x = zap_get_ucontext(zep);
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
		if (0 != __ldms_conn_msg_verify(x, ev->data, ev->data_len,
					   rej_msg, sizeof(rej_msg))) {
			zap_reject(zep, rej_msg, strlen(rej_msg)+1);
			break;
		}
		ldms_zap_handle_conn_req(zep);
		break;
	case ZAP_EVENT_REJECTED:
		event.type = LDMS_XPRT_EVENT_REJECTED;
		if (x->event_cb)
			x->event_cb(x, &event, x->event_cb_arg);
		/* Put the reference taken in ldms_xprt_connect() */
		ldms_xprt_put(x);
		break;
	case ZAP_EVENT_CONNECTED:
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
		#ifdef DEBUG
		x->log("DEBUG: ldms_zap_cb: receive DISCONNECTED %p: "
				"ref_count %d\n", x, x->ref_count);
		#endif /* DEBUG */
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
		if (x->event_cb)
			x->event_cb(x, &event, x->event_cb_arg);
		#ifdef DEBUG
		x->log("DEBUG: ldms_zap_cb: DISCONNECTED %p: ref_count %d. "
				"after callback\n",
			 x, x->ref_count);
		#endif /* DEBUG */
		/* Put the reference taken in ldms_xprt_connect() or accept() */
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
	struct ldms_xprt_event event = {
			.data = NULL,
			.data_len = 0
	};
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
		goto err1;
	}
	x->max_msg = zap_max_msg(x->zap);
	zap_set_ucontext(x->zap_ep, x);
	return 0;
err1:
	free(x->zap);
err0:
	return ret;
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

	x->log = log_fn;
	TAILQ_INIT(&x->ctxt_list);
	sem_init(&x->sem, 0, 0);
	pthread_mutex_init(&x->lock, NULL);
	pthread_mutex_lock(&xprt_list_lock);
	LIST_INSERT_HEAD(&xprt_list, x, xprt_link);
	pthread_mutex_unlock(&xprt_list_lock);
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
		goto err2;
	}

	auth = ldms_auth_new(auth_plugin, auth_av_list);
	if (!auth) {
		ret = errno;
		goto err2;
	}
	x->auth_flag = LDMS_XPRT_AUTH_INIT;

	ret = ldms_xprt_auth_bind(x, auth);
	if (ret)
		goto err3;
	return x;

err3:
	ldms_auth_free(auth);
err2:
	zap_free(x->zap_ep);
err1:
	free(x);
err0:
	errno = ret;
	return NULL;
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

/*
 * This is the generic allocator for both the request buffer and the
 * context buffer. A single buffer is allocated that is big enough to
 * contain one structure. When the context is freed, the associated
 * request buffer is freed as well.
 *
 * Caller must call with the ldms xprt lock held.
 */
static int alloc_req_ctxt(struct ldms_xprt *x,
			struct ldms_request **req,
			struct ldms_context **ctxt,
			ldms_context_type_t type)
{
	struct ldms_context *ctxt_;
	size_t sz = sizeof(struct ldms_request) + sizeof(struct ldms_context);
	void *buf = __ldms_alloc_ctxt(x, sz, type);
	if (!buf)
		return 1;
	*ctxt = ctxt_ = buf;
	*req = (struct ldms_request *)(ctxt_+1);
	return 0;
}

int ldms_xprt_send(ldms_t _x, char *msg_buf, size_t msg_len)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	size_t len;
	struct ldms_context *ctxt;
	int rc;

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

int __ldms_remote_dir(ldms_t _x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
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
	if (alloc_req_ctxt(x, &req, &ctxt, LDMS_CONTEXT_DIR)) {
		pthread_mutex_unlock(&x->lock);
		ldms_xprt_put(x);
		return ENOMEM;
	}

	/* If a dir has previously been done and updates were asked
	 * for, return EBUSY. No need for application to do dir request again. */
	if (x->local_dir_xid)
		goto ebusy;

	len = format_dir_req(req, (uint64_t)(unsigned long)ctxt, flags);
	ctxt->dir.cb = cb;
	ctxt->dir.cb_arg = cb_arg;
	if (flags)
		x->local_dir_xid = (uint64_t)ctxt;
	pthread_mutex_unlock(&x->lock);

	zap_get_ep(x->zap_ep);	/* Released in process_dir_reply() */

#ifdef DEBUG
	x->active_dir++;
	x->log("DEBUG: remote_dir. get ref %p. active_dir = %d. xid %p\n",
			x->zap_ep, x->active_dir, (void *)req->hdr.xid);
#endif /* DEBUG */
	int rc = zap_send(x->zap_ep, req, len);
	if (rc) {
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
	return rc;
ebusy:
	__ldms_free_ctxt(x, ctxt);
	x->local_dir_xid = 0;
	pthread_mutex_unlock(&x->lock);
	ldms_xprt_put(x);
	return EBUSY;
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
	if (alloc_req_ctxt(x, &req, &ctxt, LDMS_CONTEXT_DIR_CANCEL)) {
		pthread_mutex_unlock(&x->lock);
		ldms_xprt_put(x);
		return ENOMEM;
	}

	len = format_dir_cancel_req(req);
	zap_get_ep(x->zap_ep);

#ifdef DEBUG
	x->active_dir_cancel++;
#endif /* DEBUG */

	pthread_mutex_unlock(&x->lock);
	int rc = zap_send(x->zap_ep, req, len);
	if (rc) {
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
	return rc;
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
	int rc;

	if (LDMS_XPRT_AUTH_GUARD(x))
		return EPERM;

	__ldms_set_tree_lock();
	struct ldms_set *set = __ldms_find_local_set(path);
	if (set) {
		LIST_FOREACH(rbd, &x->rbd_list, xprt_link) {
			if (rbd->set == set) {
				/*
				 * The set has been already looked up
				 * from the host corresponding to
				 * the transport.
				 */
				__ldms_set_tree_unlock();
				return EEXIST;
			}
		}
	}
	__ldms_set_tree_unlock();

	/* Prevent x being destroyed if DISCONNECTED is delivered in another thread */
	ldms_xprt_get(x);
	pthread_mutex_lock(&x->lock);
	if (alloc_req_ctxt(x, &req, &ctxt, LDMS_CONTEXT_LOOKUP)) {
		pthread_mutex_unlock(&x->lock);
		ldms_xprt_put(x);
		return ENOMEM;
	}

	len = format_lookup_req(req, flags, path, (uint64_t)(unsigned long)ctxt);
	ctxt->lookup.s = NULL;
	ctxt->lookup.cb = cb;
	ctxt->lookup.cb_arg = arg;
	ctxt->lookup.flags = flags;
	ctxt->lookup.path = strdup(path);

#ifdef DEBUG
	x->active_lookup++;
#endif /* DEBUG */
	pthread_mutex_unlock(&x->lock);
	zap_get_ep(x->zap_ep);	/* Released in either ...lookup_reply() or ..rendezvous() */


#ifdef DEBUG
	x->log("DEBUG: remote_lookup: get ref %p: active_lookup = %d\n",
		x->zap_ep, x->active_lookup);
#endif /* DEBUG */
	rc = zap_send(x->zap_ep, req, len);
	if (rc) {
		zap_put_ep(x->zap_ep);

		pthread_mutex_lock(&x->lock);
		__ldms_free_ctxt(x, ctxt);
		pthread_mutex_unlock(&x->lock);
#ifdef DEBUG
		x->active_lookup--;
		x->log("DEBUG: lookup_reply: error. put ref %p: "
				"active_lookup = %d. path = %s\n",
				x->zap_ep, x->active_lookup,
				path);
#endif /* DEBUG */

	}
	ldms_xprt_put(x);
	return rc;
}

static int send_req_notify(ldms_t _x, ldms_set_t s, uint32_t flags,
			   ldms_notify_cb_t cb_fn, void *cb_arg)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;
	int rc;

	ldms_xprt_get(x);
	pthread_mutex_lock(&x->lock);
	if (alloc_req_ctxt(x, &req, &ctxt, LDMS_CONTEXT_REQ_NOTIFY)) {
		pthread_mutex_unlock(&x->lock);
		ldms_xprt_put(x);
		return ENOMEM;
	}

	if (s->local_notify_xid) {
		free((void *)(unsigned long)s->local_notify_xid);
		s->local_notify_xid = 0;
	}
	len = format_req_notify_req(req, (uint64_t)(unsigned long)ctxt,
				    s->remote_set_id, flags);
	ctxt->req_notify.cb = cb_fn;
	ctxt->req_notify.arg = cb_arg;
	ctxt->req_notify.s = s;
	s->local_notify_xid = (uint64_t)ctxt;

	pthread_mutex_unlock(&x->lock);
	rc = zap_send(x->zap_ep, req, len);
	ldms_xprt_put(x);
	return rc;
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

	len = format_cancel_notify_req
		(&req, (uint64_t)(unsigned long)s->local_notify_xid,
		 s->remote_set_id);
	s->local_notify_xid = 0;

	return zap_send(x->zap_ep, &req, len);
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
	pthread_mutex_lock(&xprt_list_lock);
	LIST_FOREACH(r, &s->set->local_rbd_list, set_link) {
		if (r->remote_notify_xid &&
			(0 == r->notify_flags || (r->notify_flags & e->type))) {
			send_req_notify_reply(r->xprt,
					      s->set, r->remote_notify_xid,
					      e);
		}
	}
	pthread_mutex_unlock(&xprt_list_lock);
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
	req.push.push_set_id = (uint64_t)(unsigned long)r;
	req.push.flags = htonl(LDMS_RBD_F_PUSH);
	if (push_change)
		req.push.flags |= htonl(LDMS_RBD_F_PUSH_CHANGE);
	rc = zap_share(x->zap_ep, r->lmap, (const char *)&req, len);
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
	rc = zap_send(x->zap_ep, &req, len);
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
	struct ldms_rbuf_desc *rbd, *next_rbd;
	ldms_t x;
	zap_map_t rmap, lmap;

	/*
	 * We have to lock all the transports since we are racing with
	 * disconnect and this function operates on more than one
	 * transport, i.e. the remote_rbd_list
	 */
	pthread_mutex_lock(&xprt_list_lock);

	/* Run through all RBD for the set and push to registered peers */
	pthread_mutex_lock(&set->lock);
	rbd = LIST_FIRST(&set->remote_rbd_list);
	while (rbd) {
		rc = 0;
		next_rbd = LIST_NEXT(rbd, set_link);
		ldms_t x = rbd->xprt;
		if (!x) {
#ifdef PUSH_DEBUG
			x->log("DEBUG: Push set '%s'. Skipping RBD with no "
						"transport %p. RBD type: %d\n",
						ldms_set_instance_name_get(rbd),
								rbd, rbd->type);
#endif /* PUSH_DEBUG */
			goto skip;
		}
		pthread_mutex_lock(&x->lock);
		if (LDMS_XPRT_AUTH_GUARD(x)) {
			pthread_mutex_unlock(&x->lock);
			goto skip;
		}

		if (!(rbd->push_flags & push_flags)) {
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
		ldms_xprt_get(x);
		while (len) {
			size_t data_len;

			if ((len + hdr_len) > max_len) {
				data_len = htonl(max_len - hdr_len);
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
		pthread_mutex_unlock(&x->lock);
		pthread_mutex_unlock(&set->lock);
		pthread_mutex_unlock(&xprt_list_lock);
		ldms_xprt_put(x);
		pthread_mutex_lock(&set->lock);
		pthread_mutex_lock(&xprt_list_lock);
		free(reply);
	skip:
#ifdef DEBUG
		x->active_push++;
#endif /* DEBUG */
		if (rc)
			break;
		rbd = next_rbd;
	}
 out:
	pthread_mutex_unlock(&set->lock);
	pthread_mutex_unlock(&xprt_list_lock);
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
	rc = zap_connect(_x->zap_ep, sa, sa_len, (void*)&msg, sizeof(msg));
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
		short port = atoi(port_no);
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = 0;
		sin.sin_port = htons(port);
		rc = ldms_xprt_listen(x, (struct sockaddr *)&sin, sizeof(sin),
								cb, cb_arg);
	}
	return rc;
}

struct ldms_rbuf_desc *
__ldms_alloc_rbd(struct ldms_xprt *x, struct ldms_set *s, enum ldms_rbd_type type)
{
	zap_err_t zerr;
	struct ldms_rbuf_desc *rbd = calloc(1, sizeof(*rbd));
	if (!rbd)
		goto err0;

	rbd->xprt = x;
	rbd->set = s;
	size_t set_sz = __ldms_set_size_get(s);
	if (x) {
		zerr = zap_map(x->zap_ep, &rbd->lmap, s->meta, set_sz,
					 ZAP_ACCESS_READ | ZAP_ACCESS_WRITE);
		if (zerr)
			goto err1;
	}

	rbd->type = type;
	/* Add RBD to set list */
	pthread_mutex_lock(&s->lock);
	if (type == LDMS_RBD_LOCAL)
		LIST_INSERT_HEAD(&s->local_rbd_list, rbd, set_link);
	else
		LIST_INSERT_HEAD(&s->remote_rbd_list, rbd, set_link);
	pthread_mutex_unlock(&s->lock);
	if (x)
		LIST_INSERT_HEAD(&x->rbd_list, rbd, xprt_link);

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
#ifdef DEBUG
	if (rbd->lmap) {
		rbd->xprt->log("DEBUG: zap %p: unmap local\n", rbd->xprt->zap_ep);
		zap_unmap(rbd->xprt->zap_ep, rbd->lmap);
	}
	rbd->lmap = NULL;
	if (rbd->rmap) {
		rbd->xprt->log("DEBUG: zap %p: unmap remote\n", rbd->xprt->zap_ep);
		zap_unmap(rbd->xprt->zap_ep, rbd->rmap);
	}
	rbd->rmap = NULL;
#else
	if (rbd->lmap)
		zap_unmap(rbd->xprt->zap_ep, rbd->lmap);
	rbd->lmap = NULL;
	if (rbd->rmap)
		zap_unmap(rbd->xprt->zap_ep, rbd->rmap);
	rbd->rmap = NULL;
#endif /* DEBUG */
	LIST_REMOVE(rbd, xprt_link);
	rbd->xprt = NULL;
}

void __ldms_free_rbd(struct ldms_rbuf_desc *rbd)
{
	if (rbd->xprt)
		__ldms_rbd_xprt_release(rbd);
	LIST_REMOVE(rbd, set_link);
	free(rbd);
}

static struct ldms_rbuf_desc *ldms_lookup_rbd(struct ldms_xprt *x, struct ldms_set *set)
{
	struct ldms_rbuf_desc *r;
	if (!set)
		return NULL;

	if (LIST_EMPTY(&x->rbd_list))
		return NULL;

	LIST_FOREACH(r, &x->rbd_list, xprt_link) {
		if (r->set == set)
			return r;
	}

	return NULL;
}

void __ldms_xprt_term(struct ldms_xprt *x)
{
	/* this is so that we call zap_close() exactly once */
	if (0 != __sync_fetch_and_or(&x->term, 1))
		return;
	zap_close(x->zap_ep);
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
}

static void __attribute__ ((destructor)) cs_term(void)
{
}
