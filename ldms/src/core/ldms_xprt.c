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
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldms_private.h"
#include "ldms_auth.h"

#include "coll/str_map.h"

void default_log(const char *fmt, ...)
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

const char *ldms_request_cmd_names[] = {
#define X(a) #a,
#include "ldms_xprt_cmd.h"
#undef X
};

int sizeof_ldms_request_cmd_names() {
  int result=0;
#define X(a) result++;
#include "ldms_xprt_cmd.h"
#undef X
  return result;
}

static struct ldms_rbuf_desc *lookup_rbd(struct ldms_xprt *x, struct ldms_set *set);
static struct ldms_rbuf_desc *alloc_rbd(struct ldms_xprt *x,
					struct ldms_set *s,
					void *xprt_data, size_t xprt_data_len);

int is_valid_ldms_request_cmd(int pc)
{
	return (0 <= pc && pc < sizeof_ldms_request_cmd_names()) ? 1 : 0;
}



pthread_mutex_t rbd_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t xprt_list_lock = PTHREAD_MUTEX_INITIALIZER;
str_map_t __dlmap;

static inline struct ldms_xprt *ldms_xprt_get_(struct ldms_xprt *x)
{
	assert(x->ref_count);
	x->ref_count++;
	return x;
}
ldms_t ldms_xprt_get(ldms_t _x)
{
	struct ldms_xprt *x;
	pthread_mutex_lock(&xprt_list_lock);
	x = (ldms_t)ldms_xprt_get_((struct ldms_xprt *)_x);
	pthread_mutex_unlock(&xprt_list_lock);
	return x;
}

LIST_HEAD(xprt_list, ldms_xprt) xprt_list;
ldms_t ldms_xprt_first()
{
	struct ldms_xprt *x;
	ldms_t x_ = NULL;
	pthread_mutex_lock(&xprt_list_lock);
	x = LIST_FIRST(&xprt_list);
	if (!x)
		goto out;
	assert(x->ref_count);
	x_ = (ldms_t)ldms_xprt_get_(x);
 out:
	pthread_mutex_unlock(&xprt_list_lock);
	return x_;
}

ldms_t ldms_xprt_next(ldms_t _x)
{
	struct ldms_xprt *x = _x;
	_x = NULL;
	pthread_mutex_lock(&xprt_list_lock);
	if (x->xprt_link.le_next == xprt_list.lh_first)
		goto out;
	assert(x->ref_count);
	x = x->xprt_link.le_next;
	assert(x->ref_count);
	if (!x)
		goto out;
	_x = (ldms_t)ldms_xprt_get_(x);
 out:
	pthread_mutex_unlock(&xprt_list_lock);
	return _x;
}

ldms_t ldms_xprt_find(struct sockaddr_in *sin)
{
	ldms_t l;
	for (l = ldms_xprt_first(); l; l = ldms_xprt_next(l)) {
		struct ldms_xprt *x = (struct ldms_xprt *)l;
		struct sockaddr_in *s = (struct sockaddr_in *)&x->remote_ss;
		if (s->sin_addr.s_addr == sin->sin_addr.s_addr)
			return l;
		ldms_release_xprt(l);
	}
	return 0;
}

size_t __ldms_xprt_max_msg(struct ldms_xprt *x)
{
	return x->max_msg;
}

static void send_dir_update(struct ldms_xprt *x,
			    enum ldms_dir_type t,
			    const char *set_name)
{
	size_t len;
	int set_count = 0;
	int set_list_sz = 0;
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
		strcpy(reply->dir.set_list, set_name);
		break;
	}

	reply->hdr.xid = x->remote_dir_xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_REPLY);
	reply->hdr.rc = htonl(rc);
	reply->dir.type = htonl(t);
	reply->dir.set_count = htonl(set_count);
	reply->dir.set_list_len = htonl(set_list_sz);
	reply->hdr.len = htonl(len);

	x->send(x, reply, len);
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
	if (e->len > sizeof(struct ldms_notify_event_s))
		memcpy(reply->req_notify.event.u_data, e,
		       e->len - sizeof(struct ldms_notify_event_s));

	x->send(x, reply, len);
	free(reply);
	return;
}

static void dir_update(const char *set_name, enum ldms_dir_type t)
{
	struct ldms_xprt *x;
	for (x = (struct ldms_xprt *)ldms_xprt_first(); x;
	     x = (struct ldms_xprt *)ldms_xprt_next(x)) {
		if (x->remote_dir_xid && ldms_xprt_connected(x))
			send_dir_update(x, t, set_name);
		ldms_release_xprt(x);
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

int ldms_xprt_connected(ldms_t _x)
{
	return ((struct ldms_xprt *)_x)->connected;
}

int ldms_xprt_authenticated(ldms_t _x)
{
	return ((struct ldms_xprt *)_x)->authenticated;
}

static void free_rbd(struct ldms_rbuf_desc *rbd)
{
	LIST_REMOVE(rbd, xprt_link);
	LIST_REMOVE(rbd, set_link);

	rbd->xprt->free(rbd->xprt, rbd);
}

static void release_xprt(ldms_t _x)
{
	struct ldms_xprt *x = _x;
	struct ldms_rbuf_desc *rb;

	if (!x)
		return;

	pthread_mutex_lock(&rbd_lock);
	/* x->log("%s transport %p ref_count %d.\n", __func__, _x, x->ref_count); Removed by Brandt 6-14-2014 */
	while (!LIST_EMPTY(&x->rbd_list)) {
		rb = LIST_FIRST(&x->rbd_list);
		// x->log("%s destroy rbd %p.\n", __func__, rb);
		free_rbd(rb);
	}
	pthread_mutex_unlock(&rbd_lock);

	free(x->passwordtmp);
	x->destroy(x);
}

void release_xprt_(struct ldms_xprt *x)
{
	int destroy = 0;

	assert(x->ref_count);
	x->ref_count--;
	if (!x->ref_count) {
		destroy = 1;
		LIST_REMOVE(x, xprt_link);
	}
	if (destroy)
		release_xprt(x);
}

void ldms_xprt_close(ldms_t _x)
{
	struct ldms_xprt *x = _x;
	struct sockaddr_in *sin = (struct sockaddr_in *)&x->remote_ss;
	/* x->log("%s transport %p ref_count %d.\n", __func__, _x, x->ref_count); Removed by Brandt 6-14-2014 */
	pthread_mutex_lock(&xprt_list_lock);
	assert(x->ref_count);
	x->connected = 0;
	x->authenticated = 0;
	/* Cancel any dir updates */
	x->remote_dir_xid = x->local_dir_xid = 0;
	x->close(x);
	/* pair with get in create */
	release_xprt_(x);
	pthread_mutex_unlock(&xprt_list_lock);
}

/*
 * Free all RBD associated with a metric set
 */
void ldms_free_rbd(struct ldms_set *set)
{
	struct ldms_rbuf_desc *rbd;

	pthread_mutex_lock(&rbd_lock);
	while (!LIST_EMPTY(&set->rbd_list)) {
		rbd = LIST_FIRST(&set->rbd_list);
		assert(rbd->xprt->ref_count);
		// rbd->xprt->log("%s destroy rbd %p for %s on xprt %p.\n", __func__, rbd, set->meta->name, rbd->xprt);
		free_rbd(rbd);
	}
	pthread_mutex_unlock(&rbd_lock);
}

void ldms_release_xprt(ldms_t _x)
{
	struct ldms_xprt *x = _x;

	pthread_mutex_lock(&xprt_list_lock);
	assert(x->ref_count != 0);
	release_xprt_(x);
	pthread_mutex_unlock(&xprt_list_lock);
}

struct make_dir_arg {
	int reply_size;		/* size of reply in total */
	struct ldms_reply *reply;
	struct ldms_xprt *x;
	int reply_count;	/* sets in this reply */
	int set_count;		/* total sets we have */
	char *set_list;		/* buffer for set names */
	ssize_t set_list_len;	/* current length of this buffer */
};

static int send_dir_reply_cb(struct ldms_set *set, void *arg)
{
	struct make_dir_arg *mda = arg;
	int len;

	len = strlen(set->meta->name) + 1;
	if (mda->reply_size + len < __ldms_xprt_max_msg(mda->x)) {
		mda->reply_size += len;
		strcpy(mda->set_list, set->meta->name);
		mda->set_list += len;
		mda->set_list_len += len;
		mda->reply_count ++;
		if (mda->reply_count < mda->set_count)
			return 0;
	}

	/* Update remaining set count */
	mda->set_count -= mda->reply_count;

	mda->reply->dir.more = htonl(mda->set_count != 0);
	mda->reply->dir.set_count = htonl(mda->reply_count);
	mda->reply->dir.set_list_len = htonl(mda->set_list_len);
	mda->reply->hdr.len = htonl(mda->reply_size);

	mda->x->send(mda->x, mda->reply, mda->reply_size);

	/* Change the dir type to ADD for the subsequent sends */
	mda->reply->dir.type = htonl(LDMS_DIR_ADD);

	/* Initialize arg for remainder of walk */
	mda->reply_size = sizeof(struct ldms_reply_hdr) +
		sizeof(struct ldms_dir_reply) +
		len;
	strcpy(mda->reply->dir.set_list, set->meta->name);
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
	struct ldms_reply reply_;
	struct ldms_reply *reply = &reply_;

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
		reply = &reply_;
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
	arg.set_count = set_count;

	if (req->dir.flags)
		/* Register for directory updates */
		x->remote_dir_xid = req->hdr.xid;
	else
		/* Cancel any previous dir update */
		x->remote_dir_xid = 0;

	/* Initialize the reply header */
	reply->hdr.xid = req->hdr.xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_REPLY);
	reply->dir.type = htonl(LDMS_DIR_LIST);
	(void)__ldms_for_all_sets(send_dir_reply_cb, &arg);
	free(reply);
	return;
 out:
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

	x->send(x, reply, len);
	return;
}

static void
process_dir_cancel_request(struct ldms_xprt *x, struct ldms_request *req)
{
	x->remote_dir_xid = 0;
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

static void process_lookup_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct ldms_set *set = ldms_find_local_set(req->lookup.path);
	struct ldms_rbuf_desc *rbd = lookup_rbd(x, set);
	struct ldms_reply_hdr hdr;
	struct ldms_reply *reply;
	size_t len;

	if (!set) {
		/* not found */
		hdr.rc = htonl(ENOENT);
		goto err_out;
	}

	if (!rbd) {
		rbd = alloc_rbd(x, set, NULL, 0);
		if (!rbd) {
			hdr.rc = htonl(ENOMEM);
			goto err_out;
		}
		rbd->xid = req->hdr.xid;
	}

	len = sizeof(struct ldms_reply_hdr)
	+ sizeof(struct ldms_lookup_reply)
		+ rbd->xprt_data_len;

	reply = malloc(len);
	if (!reply) {
		ldms_release_local_set(set);
		hdr.rc = htonl(ENOMEM);
		goto err_out;
	}
	reply->hdr.xid = req->hdr.xid;
	reply->hdr.cmd = htonl(LDMS_CMD_LOOKUP_REPLY);
	reply->hdr.len = htonl(len);
	reply->hdr.rc = 0;
	reply->lookup.xprt_data_len = htonl(rbd->xprt_data_len);
	memcpy(reply->lookup.xprt_data, rbd->xprt_data, rbd->xprt_data_len);
	ldms_release_local_set(set);
	reply->lookup.set_id = (uint64_t)(unsigned long)rbd;
	reply->lookup.meta_len = htonl(set->meta->meta_size);
	reply->lookup.data_len = htonl(set->meta->data_size);
	x->send(x, reply, len);
	free(reply);
	return;

 err_out:
	hdr.xid = req->hdr.xid;
	hdr.cmd = htonl(LDMS_CMD_LOOKUP_REPLY);
	hdr.len = htonl(sizeof(struct ldms_reply_hdr));
	x->send(x, &hdr, sizeof(hdr));
}

void meta_read_cb(ldms_t t, ldms_set_t s, int rc, void *arg)
{
	struct ldms_xprt *x = t;
	struct ldms_set *set = ((struct ldms_set_desc *)s)->set;
	struct ldms_context *data_ctxt = arg;

	set->flags &= ~LDMS_SET_F_DIRTY;
	if (set->meta->version == LDMS_VERSION)
		x->read_data_start(x, s, set->meta->data_size, data_ctxt);
	else
		data_ctxt->update.cb(t, data_ctxt->update.s,
				     EINVAL, data_ctxt->update.arg);
}

static int read_complete_cb(struct ldms_xprt *x, void *context)
{
	struct ldms_context *ctxt = context;
	if (ctxt->update.cb)
		ctxt->update.cb((ldms_t)x, ctxt->update.s, 0, ctxt->update.arg);
	free(ctxt);
	return 0;
}

static int do_read_meta(ldms_t t, ldms_set_t s, size_t len,
			ldms_update_cb_t cb, void *arg)
{
	struct ldms_xprt *x = t;
	TF();
	struct ldms_context *meta_ctxt = calloc(1,sizeof *meta_ctxt);
	if (!meta_ctxt)
		goto err_0;
	struct ldms_context *data_ctxt = calloc(1,sizeof *data_ctxt);
	if (!data_ctxt)
		goto err_1;

	data_ctxt->rc = 0;
	data_ctxt->update.s = s;
	data_ctxt->update.cb = cb;
	data_ctxt->update.arg = arg;

	meta_ctxt->rc = 0;
	meta_ctxt->update.s = s;
	meta_ctxt->update.cb = meta_read_cb;
	meta_ctxt->update.arg = data_ctxt;

	return x->read_meta_start(x, s, len, meta_ctxt);
 err_1:
 	 free(meta_ctxt);
 err_0:
 	 return -1;
}

static int do_read_data(ldms_t t, ldms_set_t s, size_t len, ldms_update_cb_t cb, void*arg)
{
	struct ldms_xprt *x = t;
	struct ldms_context *ctxt = calloc(1,sizeof *ctxt);
	if (!ctxt)
		goto err_0;
	TF();
	ctxt->rc = 0;
	ctxt->update.s = s;
	ctxt->update.cb = cb;
	ctxt->update.arg = arg;

	return x->read_data_start(x, s, len, ctxt);
 err_0:
 	 return -1;
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
int ldms_remote_update(ldms_t t, ldms_set_t s, ldms_update_cb_t cb, void *arg)
{
	struct ldms_set *set = ((struct ldms_set_desc *)s)->set;
	int rc;

	if (set->flags & LDMS_SET_F_DIRTY || set->meta->meta_gn == 0 ||
	    set->meta->meta_gn != set->data->meta_gn) {
		/* Update the metadata */
		rc = do_read_meta(t, s, 0, cb, arg);
	} else
		rc = do_read_data(t, s, set->data->tail_off, cb, arg);

	return rc;
}

/* @return boolean nonzero if authentication step succeeds, 0 if fail. */
static int process_auth_request(struct ldms_xprt *x, struct ldms_request *req)
{
	// get u64 from clock and send with LDMS_CMD_AUTH_REPLY
	// cache answer on transport
	struct ldms_reply_hdr hdr;
	struct ldms_reply *reply;
	size_t len;
	uint64_t challenge = 0;
	uint32_t chi, clo;
	x->log("Started process_auth_request");

	if (x->passwordtmp) {
		x->log("Authentication restarted before finished");
		return 0;
	}

	challenge = ldms_get_challenge();
	x->passwordtmp = ldms_get_auth_string(challenge);
	chi = htonl((uint32_t) (challenge >> 32));
	clo = htonl((uint32_t) (challenge));

	len = sizeof(struct ldms_reply_hdr)
	+ sizeof(struct ldms_auth_reply);

	reply = malloc(len);
	if (!reply) {
		hdr.rc = htonl(ENOMEM);
		goto err_out;
	}
	reply->hdr.xid = req->hdr.xid;
	reply->hdr.cmd = htonl(LDMS_CMD_AUTH_REPLY);
	reply->hdr.len = htonl(len);
	reply->hdr.rc = 0;
	reply->auth.challenge_hi = chi;
	reply->auth.challenge_lo = clo;

	x->send(x, reply, len);
	free(reply);
	return 1;

 err_out:
	hdr.xid = req->hdr.xid;
	hdr.cmd = htonl(LDMS_CMD_AUTH_REPLY);
	hdr.len = htonl(sizeof(struct ldms_reply_hdr));
	x->send(x, &hdr, sizeof(hdr));
	return 0;
}

/* @return boolean nonzero if authentication step succeeds, 0 if fail. */
static int process_auth_password_request(struct ldms_xprt *x, struct ldms_request *req)
{
	char pw[LDMS_PASSWORD_MAX];
	x->log("Started process_auth_password_request");
	if (NULL == x->passwordtmp) {
		x->log("Authentication password sent before challenge fetched");
		return 0;
	}
	// FIXME get password from message?
	if (req->auth.pw_len >= LDMS_PASSWORD_MAX) {
		x->log("Password too long");
		return 0;
	}
	strncpy(pw, req->auth.pw, LDMS_PASSWORD_MAX);
	pw[LDMS_PASSWORD_MAX-1] = '\0';

	if (strncmp(pw,x->passwordtmp,strlen(x->passwordtmp)) == 0) {
		x->log("Authentication succeeded.");
		return 1;
	}
	x->log("Authentication failed. Bad password.");
	return 0;
}

/* @return boolean nonzero if authentication succeeds, 0 if fail. */
static int ldms_xprt_authenticate(int cmd, struct ldms_xprt *x, struct ldms_request *req)
{
	switch (cmd) {
	case LDMS_CMD_AUTH:
		return process_auth_request(x, req);
	case LDMS_CMD_AUTH_PASSWORD:
		return process_auth_password_request(x, req);
	default:
		x->log("Request for work before authentication complete. %d\n", cmd);
		return 0;
	}
}

static int ldms_xprt_recv_request(struct ldms_xprt *x, struct ldms_request *req)
{
	int cmd = ntohl(req->hdr.cmd);

	if (0 == x->authenticated) {
#ifdef HAVE_AUTH
		/* try once and close if fail. no excuses for robots. */
		if (! ldms_xprt_authenticate(cmd, x, req)) {
			// FIXME cause disconnect somehow
			return 0;
		} else {
			x->authenticated = 1;
		}
#else
		x->authenticated = 1;
#endif
	}
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
	case LDMS_CMD_UPDATE:
		break;
	case LDMS_CMD_AUTH:
		/* FALLTHRU */
	case LDMS_CMD_AUTH_PASSWORD:
		x->log("Already authenticated %d\n", cmd);
		break;
	default:
		x->log("Unrecognized request %d\n", cmd);
		assert(0);
	}
	return 0;
}

void process_lookup_reply(struct ldms_xprt *x, struct ldms_reply *reply,
			  struct ldms_context *ctxt)
{
	struct ldms_set *set = ctxt->lookup.set;
	struct ldms_set_desc *sd = NULL;
	struct ldms_rbuf_desc *rbd;
	int rc;

	rc = ntohl(reply->hdr.rc);
	if (rc)
		goto out;

	/* Check to see if we've already looked it up */
	if (!set) {
		ldms_set_t set_t;
		/* Create a local instance of this remote metric set */
		rc = __ldms_create_set(ctxt->lookup.path,
				       ntohl(reply->lookup.meta_len),
				       ntohl(reply->lookup.data_len),
				       &set_t,
				       LDMS_SET_F_REMOTE | LDMS_SET_F_DIRTY);
		if (rc)
			goto out;
		sd = (struct ldms_set_desc *)set_t;
		set = sd->set;
	} else {
		sd = malloc(sizeof *sd);
		if (!sd) {
			rc = ENOMEM;
			goto out;
		}
		sd->set = set;
	}

	/* Bind this set to an RBD */
	rbd = alloc_rbd(x, set, reply->lookup.xprt_data,
			ntohl(reply->lookup.xprt_data_len));
	if (!rbd)
		goto out_1;

	sd->rbd = rbd;
	rbd->remote_set_id = reply->lookup.set_id;
	rc = 0;
	goto out;

 out_1:
	ldms_destroy_set(set);
	free(sd);
	sd = NULL;
 out:
	if (ctxt->lookup.cb)
		ctxt->lookup.cb(x, rc, (ldms_set_t)sd, ctxt->lookup.cb_arg);
	free(ctxt->lookup.path);
	free(ctxt);
}

void process_dir_reply(struct ldms_xprt *x, struct ldms_reply *reply,
		       struct ldms_context *ctxt)
{
	int i;
	char *src, *dst;
	enum ldms_dir_type type = ntohl(reply->dir.type);
	int rc = ntohl(reply->hdr.rc);
	int more = ntohl(reply->dir.more);
	size_t len = ntohl(reply->dir.set_list_len);
	unsigned count = ntohl(reply->dir.set_count);
	ldms_dir_t dir = NULL;
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
	/* Don't touch dir after callback because the dir.cb may have freed it. */
	if (ctxt->dir.cb)
		ctxt->dir.cb((ldms_t)x, rc, dir, ctxt->dir.cb_arg);
	pthread_mutex_lock(&x->lock);
	if (!x->local_dir_xid && !more)
		free(ctxt);
	pthread_mutex_unlock(&x->lock);
}

void process_req_notify_reply(struct ldms_xprt *x, struct ldms_reply *reply,
			      struct ldms_context *ctxt)
{
	ldms_notify_event_t event;
	size_t len = ntohl(reply->req_notify.event.len);
	event = malloc(len);
	if (!event)
		return;

	event->type = ntohl(reply->req_notify.event.type);
	event->len = ntohl(reply->req_notify.event.len);

	if (len > sizeof(struct ldms_notify_event_s))
		memcpy(event->u_data,
		       &reply->req_notify.event.u_data,
		       len - sizeof(struct ldms_notify_event_s));

	if (ctxt->req_notify.cb)
		ctxt->req_notify.cb((ldms_t)x,
				    ctxt->req_notify.s,
				    event, ctxt->dir.cb_arg);
}

size_t format_auth_password_req(struct ldms_request *req, const char *password,
                         uint64_t xid)
{
	size_t len = strlen(password) + 1;
	if (len > LDMS_PASSWORD_MAX) {
		len = LDMS_PASSWORD_MAX;
	}
	strncpy(req->auth.pw, password, LDMS_PASSWORD_MAX);
	req->auth.pw[LDMS_PASSWORD_MAX-1] = '\0';
	req->auth.pw_len = htonl(len);
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_AUTH_PASSWORD);
	len += sizeof(uint32_t) + sizeof(struct ldms_request_hdr);
	req->hdr.len = htonl(len);
	return len;
}

void process_auth_reply(struct ldms_xprt *x, struct ldms_reply *reply,
		       struct ldms_context *ctxt)
{
#ifdef HAVE_AUTH
	x->log("Started process_auth_reply");
	int rc = ntohl(reply->hdr.rc);
	uint64_t challenge;
	if (rc)
		return;
	rc = 0;
	challenge = ldms_unpack_challenge(reply->auth.challenge_hi,
		reply->auth.challenge_lo);
	char* password = ldms_get_auth_string(challenge);
	if (!password) {
		return;
	}

 	struct ldms_request req;
	size_t len;

	len = format_auth_password_req(&req, password, (uint64_t)ctxt);
	free(password);

	x->send(x, &req, len);
#else
	x->log("Unexpected call to process_auth_reply");
#endif
}


void ldms_dir_release(ldms_t t, ldms_dir_t d)
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
	assert(sizeof(unsigned long) == sizeof(uintptr_t) &&
		"can't store pointers in unsigned longs on "
		"this platform/compiler combination");
	ctxt = (struct ldms_context *)(unsigned long)xid;
	switch (cmd) {
	case LDMS_CMD_LOOKUP_REPLY:
		process_lookup_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_DIR_REPLY:
		process_dir_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_REQ_NOTIFY_REPLY:
		process_req_notify_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_AUTH_REPLY:
		process_auth_reply(x, reply, ctxt);
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

static int local_xprt_connect(struct ldms_xprt *x, struct sockaddr *sa, socklen_t sa_len)
{
	/* Local transport connect is a no-op */
	return 0;
}
static void local_xprt_destroy(struct ldms_xprt *x)
{
	/* Local transport destroy is a no-op */
}
static int local_xprt_send(struct ldms_xprt *x, void *y, size_t z)
{
	(void)x;
	(void)y;
	(void)z;
	return EBADMSG; //x misconfigured upstream
}

struct ldms_xprt local_transport = {
	.name = "local",
	.connect = local_xprt_connect,
	.destroy = local_xprt_destroy,
	.send = local_xprt_send,
};

#if defined(__MACH__)
#define _SO_EXT ".dylib"
#undef LDMS_XPRT_LIBPATH_DEFAULT
#define LDMS_XPRT_LIBPATH_DEFAULT "/home/tom/macos/lib"
#else
#define _SO_EXT ".so"
#endif
static char _libdir[PATH_MAX];
ldms_t ldms_create_xprt(const char *name, ldms_log_fn_t log_fn)
{
	int ret = 0;
	char *libdir;
	struct ldms_xprt *x = 0;
	char *errstr;
	int len;

	if (!log_fn)
		log_fn = default_log;
	if (0 == strcmp(name, "local"))
		return &local_transport;

	libdir = getenv("LDMS_XPRT_LIBPATH");
	if (!libdir || libdir[0] == '\0')
		strcpy(_libdir, LDMS_XPRT_LIBPATH_DEFAULT);
	else
		strcpy(_libdir, libdir);

	/* Add a trailing / if one is not present in the path */
	len = strlen(_libdir);
	if (_libdir[len-1] != '/')
		strcat(_libdir, "/");

	strcat(_libdir, "libldms");
	strcat(_libdir, name);
	strcat(_libdir, _SO_EXT);
	void *d = (void*)str_map_get(__dlmap, name);
	if (!d) {
		d = dlopen(_libdir, RTLD_NOW);
		if (!d) {
			/* The library doesn't exist */
			log_fn("dlopen: %s\n", dlerror());
			ret = ENOENT;
			goto err;
		}
		dlerror();
		str_map_insert(__dlmap, name, (uint64_t)d);
	}

	ldms_xprt_get_t get = dlsym(d, "xprt_get");
	errstr = dlerror();
	if (errstr || !get) {
		log_fn("dlsym: %s\n", errstr);
		/* The library exists but doesn't export the correct
		 * symbol and is therefore likely the wrong library type */
		ret = EINVAL;
		goto err;
	}
	x = get(recv_cb, read_complete_cb, log_fn);
	if (!x) {
		/* The transport library refused the request */
		ret = ENOSYS;
		goto err;
	}
	strcpy(x->name, name);
	x->connected = 0;
	x->authenticated = 0;
	x->passwordtmp = NULL;
	x->ref_count = 1;
	x->remote_dir_xid = x->local_dir_xid = 0;

	x->log = log_fn;
	pthread_mutex_init(&x->lock, NULL);
	pthread_mutex_lock(&xprt_list_lock);
	LIST_INSERT_HEAD(&xprt_list, x, xprt_link);
	pthread_mutex_unlock(&xprt_list_lock);
	return x;
 err:
	errno = ret;
	return NULL;
}

size_t format_lookup_req(struct ldms_request *req, const char *path,
			 uint64_t xid)
{
	size_t len = strlen(path) + 1;
	strcpy(req->lookup.path, path);
	req->lookup.path_len = htonl(len);
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_LOOKUP);
	len += sizeof(uint32_t) + sizeof(struct ldms_request_hdr);
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
	req->req_notify.flags = flags;
	return len;
}

size_t format_cancel_notify_req(struct ldms_request *req, uint64_t xid)
{
	size_t len = sizeof(struct ldms_request_hdr)
		+ sizeof(struct ldms_cancel_notify_cmd_param);
	req->hdr.xid = xid;
	req->hdr.cmd = htonl(LDMS_CMD_CANCEL_NOTIFY);
	req->hdr.len = htonl(len);
	return len;
}

/*
 * This is the generic allocator for both the request buffer and the
 * context buffer. A single buffer is allocated that is big enough to
 * contain one structure. When the context is freed, the associated
 * request buffer is freed as well.
 */
static int alloc_req_ctxt(struct ldms_request **req, struct ldms_context **ctxt)
{
	struct ldms_context *ctxt_;
	void *buf = malloc(sizeof(struct ldms_request) + sizeof(struct ldms_context));
	if (!buf)
		return 1;
	*ctxt = ctxt_ = buf;
	*req = (struct ldms_request *)(ctxt_+1);
	return 0;
}

int __ldms_remote_dir(ldms_t _x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
{
	struct ldms_xprt *x = _x;
 	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (alloc_req_ctxt(&req, &ctxt))
		return ENOMEM;

	pthread_mutex_lock(&x->lock);
	/* If a dir has previously been done and updates were asked
	 * for, free that cached context */
	if (x->local_dir_xid) {
		free((void *)(unsigned long)x->local_dir_xid);
		x->local_dir_xid = 0;
	}
	len = format_dir_req(req, (uint64_t)(unsigned long)ctxt, flags);
	ctxt->dir.cb = cb;
	ctxt->dir.cb_arg = cb_arg;
	if (flags)
		x->local_dir_xid = (uint64_t)ctxt;
	pthread_mutex_unlock(&x->lock);

	return x->send(x, req, len);
}

/* This request has no reply */
void __ldms_remote_dir_cancel(ldms_t _x)
{
	struct ldms_xprt *x = _x;
 	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (alloc_req_ctxt(&req, &ctxt))
		return;

	pthread_mutex_lock(&x->lock);
	if (x->local_dir_xid)
		free((void *)(unsigned long)x->local_dir_xid);
	x->local_dir_xid = 0;
	pthread_mutex_unlock(&x->lock);

	len = format_dir_cancel_req(req);
	x->send(x, req, len);
	free(ctxt);
}

int __ldms_remote_lookup(ldms_t _x, const char *path,
			 ldms_lookup_cb_t cb, void *arg)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;
	int rc;

	if (alloc_req_ctxt(&req, &ctxt))
		return ENOMEM;

	len = format_lookup_req(req, path, (uint64_t)(unsigned long)ctxt);
	ctxt->lookup.set = ldms_find_local_set(path);
	if (ctxt->lookup.set)
		ldms_release_local_set(ctxt->lookup.set);
	ctxt->lookup.cb = cb;
	ctxt->lookup.cb_arg = arg;
	ctxt->lookup.path = strdup(path);
	rc = x->send(x, req, len);
	return rc;
}

static int send_req_notify(ldms_t _x, ldms_set_t s, uint32_t flags,
			   ldms_notify_cb_t cb_fn, void *cb_arg)
{
	struct ldms_rbuf_desc *r =
		(struct ldms_rbuf_desc *)
		((struct ldms_set_desc *)s)->rbd;
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (alloc_req_ctxt(&req, &ctxt))
		return ENOMEM;

	if (r->local_notify_xid) {
		free((void *)(unsigned long)r->local_notify_xid);
		r->local_notify_xid = 0;
	}
	len = format_req_notify_req(req, (uint64_t)(unsigned long)ctxt,
				    r->remote_set_id, flags);
	ctxt->req_notify.cb = cb_fn;
	ctxt->req_notify.arg = cb_arg;
	ctxt->req_notify.s = s;
	r->local_notify_xid = (uint64_t)ctxt;

	return x->send(x, req, len);
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
	struct ldms_rbuf_desc *r =
		(struct ldms_rbuf_desc *)
		((struct ldms_set_desc *)s)->rbd;
	struct ldms_xprt *x = _x;
 	struct ldms_request req;
	size_t len;

	len = format_cancel_notify_req
		(&req, (uint64_t)(unsigned long)r->local_notify_xid);
	r->local_notify_xid = 0;

	return x->send(x, &req, len);
}

int ldms_cancel_notify(ldms_t t, ldms_set_t s)
{
	struct ldms_set *set = ((struct ldms_set_desc *)s)->set;
	if (!set)
		goto err;
	return send_cancel_notify(t, s);
 err:
	errno = EINVAL;
	return -1;
}

void ldms_notify(ldms_set_t s, ldms_notify_event_t e)
{
	struct ldms_set *set;
	struct ldms_rbuf_desc *r;
	if (!s)
		return;
	set = ((struct ldms_set_desc *)s)->set;
	if (!set)
		return;

	pthread_mutex_lock(&rbd_lock);
	LIST_FOREACH(r, &set->rbd_list, set_link) {
		if (!ldms_xprt_connected(r->xprt))
			continue;
		if (r->remote_notify_xid)
			send_req_notify_reply(r->xprt,
					      set, r->remote_notify_xid,
					      e);
	}
	pthread_mutex_unlock(&rbd_lock);
}

int ldms_connect(ldms_t _x, struct sockaddr *sa, socklen_t sa_len)
{
	int rc = -1;
	struct ldms_xprt *x = _x;

	pthread_mutex_lock(&x->lock);
	if (x->connected) {
		errno = EBUSY;
		goto out;
	}
	memcpy(&x->remote_ss, sa, sa_len);
	x->ss_len = sa_len;
	rc = x->connect(x, sa, sa_len);
	if (!rc)
		x->connected = 1;

#ifdef HAVE_AUTH
//	now authenticate or disconnect with auth error
	x->log("Connect starting auth exchange");
	struct ldms_request_hdr req;
	size_t len;
	len = sizeof(req);
	req.cmd = LDMS_CMD_AUTH;
	req.len = len;
	req.xid = 0; // FIXME do we need something here?
	x->send(x, &req, len);
	// FIXME  now we expect the reply handling to manage the rest?
#endif

 out:
	pthread_mutex_unlock(&x->lock);
	return rc;
}

int ldms_listen(ldms_t _x, struct sockaddr *sa, socklen_t sa_len)
{
	struct ldms_xprt *x = _x;
	memcpy(&x->local_ss, sa, sa_len);
	x->ss_len = sa_len;
	return x->listen(x, sa, sa_len);
}

static struct ldms_rbuf_desc *alloc_rbd(struct ldms_xprt *x,
					struct ldms_set *s,
					void *xprt_data, size_t xprt_data_len)
{
	struct ldms_rbuf_desc *rbd = x->alloc(x, s, xprt_data, xprt_data_len);
	if (!rbd)
		goto out_0;

	pthread_mutex_lock(&rbd_lock);
	rbd->xprt = x;
	rbd->set = s;

	/* Add RBD to set list */
	LIST_INSERT_HEAD(&s->rbd_list, rbd, set_link);
	LIST_INSERT_HEAD(&x->rbd_list, rbd, xprt_link);
	pthread_mutex_unlock(&rbd_lock);

 out_0:
	return rbd;
}

static struct ldms_rbuf_desc *lookup_rbd(struct ldms_xprt *x, struct ldms_set *set)
{
	struct ldms_rbuf_desc *r = NULL;
	if (!set)
		return NULL;

	pthread_mutex_lock(&rbd_lock);
	LIST_FOREACH(r, &x->rbd_list, xprt_link) {
		if (r->set == set)
			goto out;
	}
	r = NULL;
out:
	pthread_mutex_unlock(&rbd_lock);
	return r;
}

void __attribute__ ((constructor)) cs_init(void)
{
	pthread_mutex_init(&xprt_list_lock, 0);
	__dlmap = str_map_create(4091);
	assert(__dlmap);
}

void __attribute__ ((destructor)) cs_term(void)
{
}
