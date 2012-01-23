/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

pthread_spinlock_t xprt_list_lock;

int do_wait(struct ldms_context *ctxt)
{
	int rc;
	do {
		rc = sem_wait(ctxt->sem_p);
	} while (rc && errno == EINTR);
	if (!rc)
		rc = ctxt->rc;
	return rc;	
}

void do_wakeup(struct ldms_context *ctxt)
{
	sem_post(ctxt->sem_p);
}

static inline struct ldms_xprt *ldms_xprt_get_(struct ldms_xprt *x)
{
	x->ref_count++;
	return x;
}
ldms_t ldms_xprt_get(ldms_t _x)
{
	struct ldms_xprt *x;
	pthread_spin_lock(&xprt_list_lock);
	x = (ldms_t)ldms_xprt_get_((struct ldms_xprt *)_x);
	pthread_spin_unlock(&xprt_list_lock);
	return x;
}

LIST_HEAD(xprt_list, ldms_xprt) xprt_list;
ldms_t ldms_xprt_first()
{
	struct ldms_xprt *x;
	ldms_t x_ = NULL;
	pthread_spin_lock(&xprt_list_lock);
	x = LIST_FIRST(&xprt_list);
	if (!x)
		goto out;
	x_ = (ldms_t)ldms_xprt_get_(x);
 out:
	pthread_spin_unlock(&xprt_list_lock);
	return x_;
}

ldms_t ldms_xprt_next(ldms_t _x)
{
	struct ldms_xprt *x = _x;
	_x = NULL;
	pthread_spin_lock(&xprt_list_lock);
	if (x->xprt_link.le_next == xprt_list.lh_first)
		goto out;
	x = x->xprt_link.le_next;
	if (!x)
		goto out;
	_x = (ldms_t)ldms_xprt_get_(x);
 out:
	pthread_spin_unlock(&xprt_list_lock);
	return _x;
}

ldms_t ldms_xprt_find(struct sockaddr_in *sin)
{
	ldms_t l;
	for (l = ldms_xprt_first(); l; l = ldms_xprt_next(l)) {
		struct ldms_xprt *x = (struct ldms_xprt *)l;
		struct sockaddr_in *s = (struct sockaddr_in *)&x->ss;
		if (s->sin_addr.s_addr == sin->sin_addr.s_addr)
			return l;
		ldms_release_xprt(l);
	}
	return 0;
}

static void send_dir_update(struct ldms_xprt *x,
			    enum ldms_dir_type t,
			    const char *set_name)
{
	size_t len;
	int set_count;
	int set_list_sz;
	int rc;
	struct ldms_reply *reply;

	switch (t) {
	case LDMS_DIR_LIST:
		ldms_get_local_set_list_sz(&set_count, &set_list_sz);
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

	reply = malloc(strlen(set_name) + 1
		       + sizeof(struct ldms_reply_hdr)
		       + sizeof(struct ldms_dir_reply));
	if (!reply) {
		printf("Memory allocation failure "
		       "in dir update of peer.\n");
		return;
	}

	switch (t) {
	case LDMS_DIR_LIST:
		rc = ldms_get_local_set_list(reply->dir.set_list,
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
	reply->dir.type = t;
	reply->dir.set_count = htonl(set_count);
	reply->dir.set_list_len = htonl(set_list_sz);
	reply->hdr.len = htonl(len);

	x->send(x, reply, len);
	free(reply);
	return;
}

static void _dir_update(const char *set_name, enum ldms_dir_type t)
{
	struct ldms_xprt *x;
	for (x = (struct ldms_xprt *)ldms_xprt_first(); x;
	     x = (struct ldms_xprt *)ldms_xprt_next(x)) {
		if (ldms_xprt_closed(x))
			continue;
		if (x->remote_dir_xid)
			send_dir_update(x, t, set_name);
	}
}

void ldms_dir_add_set(const char *set_name)
{
	_dir_update(set_name, LDMS_DIR_ADD);
}

void ldms_dir_del_set(const char *set_name)
{
	_dir_update(set_name, LDMS_DIR_DEL);
}

int ldms_xprt_connected(ldms_t _x)
{
	struct ldms_xprt *x = _x;
	if (x)
		return x->connected;
	return 0;
}

void ldms_xprt_close(ldms_t _x)
{
	struct ldms_xprt *x = _x;
	int close = 0;
	pthread_spin_lock(&xprt_list_lock);
	if (!x->closed) {
		close = 1;
		x->connected = 0;
		x->closed = 1;
	}
	/* Cancel any dir updates */ 
	x->remote_dir_xid = x->local_dir_xid = 0;
	pthread_spin_unlock(&xprt_list_lock);

	if (close) {
		if (x->close)
			x->close(x);
		do_wakeup(&x->io_ctxt);
	}
}

int ldms_xprt_closed(ldms_t _x)
{
	struct ldms_xprt *x = _x;
	if (x)
		return x->closed;
	return 1;
}

void ldms_release_xprt(ldms_t _x)
{
	struct ldms_xprt *x = _x;
	struct ldms_rbuf_desc *rb;
	int destroy = 0;

	pthread_spin_lock(&xprt_list_lock);
	assert(x->ref_count);
	x->ref_count--;
	if (!x->ref_count) {
		destroy = 1;
		LIST_REMOVE(x, xprt_link);
	}
	pthread_spin_unlock(&xprt_list_lock);
	if (!destroy)
		return;

	while (!LIST_EMPTY(&x->rbd_list)) {
		rb = LIST_FIRST(&x->rbd_list);
		ldms_free_rbd(rb);
	}
	x->destroy(x);
	sem_close(x->io_ctxt.sem_p);
	free(x);
}

static void process_dir_request(struct ldms_xprt *x, struct ldms_request *req)
{
	size_t len;
	int set_count;
	int set_list_sz;
	int rc;
	struct ldms_reply reply_;
	struct ldms_reply *reply;

	ldms_get_local_set_list_sz(&set_count, &set_list_sz);
	reply = malloc(set_list_sz
		       + sizeof(struct ldms_reply_hdr)
		       + sizeof(struct ldms_dir_reply));
	if (!reply) {
		rc = ENOMEM;
		reply = &reply_;
		len = sizeof(struct ldms_reply_hdr);
		goto err_out;
	}
	rc = ldms_get_local_set_list(reply->dir.set_list,
				     set_list_sz,
				     &set_count, &set_list_sz);
	len = sizeof(struct ldms_reply_hdr)
		+ sizeof(struct ldms_dir_reply)
		+ set_list_sz;

	if (req->dir.flags)
		/* Register for directory updates */
		x->remote_dir_xid = req->hdr.xid;
	else
		/* Cancel any previous dir update */
		x->remote_dir_xid = 0;
 err_out:
	reply->hdr.xid = req->hdr.xid;
	reply->hdr.cmd = htonl(LDMS_CMD_DIR_REPLY);
	reply->hdr.rc = htonl(rc);
	reply->dir.type = htonl(LDMS_DIR_LIST);
	reply->dir.set_count = htonl(set_count);
	reply->dir.set_list_len = htonl(set_list_sz);
	reply->hdr.len = htonl(len);

	x->send(x, reply, len);
	free(reply);
	return;
}

static void process_dir_cancel_request(struct ldms_xprt *x, struct ldms_request *req)
{
	x->remote_dir_xid = 0;
	return;
}

static void process_lookup_request(struct ldms_xprt *x, struct ldms_request *req)
{
	struct ldms_set *set = ldms_find_local_set(req->lookup.path);
	struct ldms_rbuf_desc *rbd = ldms_lookup_rbd(x, set);
	struct ldms_reply_hdr hdr;
	struct ldms_reply *reply;
	size_t len;

	if (!set) {
		/* not found */
		hdr.rc = htonl(ENOENT);
		goto err_out;
	}

	if (!rbd) {
		rbd = ldms_alloc_rbd(x, set, LDMS_RBUF_LOCAL, NULL, 0);
		rbd->xid = req->hdr.xid;
		if (!rbd) {
			hdr.rc = htonl(ENOMEM);
			goto err_out;
		}
	}

	len = sizeof(struct ldms_reply_hdr)
		+ sizeof(struct ldms_lookup_reply)
		+ rbd->xprt_data_len;

	reply = malloc(len);
	if (!reply) {
		hdr.rc = htonl(ENOMEM);
		goto err_out;
	}		
	reply->hdr.xid = req->hdr.xid;
	reply->hdr.cmd = htonl(LDMS_CMD_LOOKUP_REPLY);
	reply->hdr.len = htonl(len);
	reply->hdr.rc = 0;
	reply->lookup.xprt_data_len = htonl(rbd->xprt_data_len);
	memcpy(reply->lookup.xprt_data, rbd->xprt_data, rbd->xprt_data_len);
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

void sync_read_cb(ldms_t t, ldms_set_t s, int rc, void *arg)
{
	struct ldms_xprt *x = t;
	do_wakeup(&x->io_ctxt);
}

static int do_read_meta(ldms_t t, ldms_set_t s, size_t len)
{
	struct ldms_xprt *x = t;
	int rc;

	x->io_ctxt.update.s = s;
	x->io_ctxt.update.cb = sync_read_cb;
	x->io_ctxt.rc = 0;
	rc = x->read_meta_start(x, s, len, &x->io_ctxt);
	if (rc)
		goto out_0;

	rc = do_wait(&x->io_ctxt);
	if (rc)
		goto out_0;

 out_0:
	return rc;
}

static int do_read_data(ldms_t t, ldms_set_t s, size_t len, ldms_update_cb_t cb, void*arg)
{
	struct ldms_xprt *x = t;
	struct ldms_context *ctxt = malloc(sizeof *ctxt);

	ctxt->rc = 0;
	ctxt->update.s = s;
	ctxt->update.cb = cb;
	ctxt->update.arg = arg;

	return x->read_data_start(x, s, len, ctxt);
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
	size_t len;
	int retry_max = 24;
	int rc;

	len = set->data->tail_off;
	if (set->flags & LDMS_SET_F_DIRTY ||
	    set->meta->meta_gn == 0 ||
	    set->meta->meta_gn != set->data->meta_gn) {
		//	retry:
		while (retry_max--) {
			if (retry_max < 23)
				printf("Meta data read fail...retrying.\n");
			/* Update the metadata */
			rc = do_read_meta(t, s, 0);
			if (rc)
				goto out_0;
			if (set->meta->meta_gn) {
				set->flags &= ~LDMS_SET_F_DIRTY;
				break;
			}
		}
		len = 0;
	}
	while (retry_max) {
		rc = do_read_data(t, s, len, cb, arg);
		if (rc)
			goto out_0;
#if 0
		if (set->meta->meta_gn == set->data->meta_gn)
			break;

		goto retry;
#endif
		break;
	}
	if (!retry_max)
		rc = ENOENT;
 out_0:
	if (rc)
		ldms_xprt_close(t);
	return rc;
}

static int read_complete_cb(struct ldms_xprt *x, void *context)
{
	struct ldms_context *ctxt = context;
	if (ctxt->update.cb)
		ctxt->update.cb((ldms_t)x, ctxt->update.s, 0, ctxt->update.arg);
	if (context != &x->io_ctxt)
		free(ctxt);
	return 0;
}

static int ldms_xprt_recv_request(struct ldms_xprt *x, struct ldms_request *req)
{
	int cmd = ntohl(req->hdr.cmd);

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
	case LDMS_CMD_UPDATE:
		break;
	default:
		printf("Unrecognized request %d\n", cmd);
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
		rc = _ldms_create_set(ctxt->lookup.path,
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
	rbd = ldms_alloc_rbd(x, set, LDMS_RBUF_REMOTE,
			     reply->lookup.xprt_data,
			     ntohl(reply->lookup.xprt_data_len));
	if (!rbd)
		goto out_1;

	sd->rbd = rbd;
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
	size_t len = ntohl(reply->dir.set_list_len);
	unsigned count = ntohl(reply->dir.set_count);
	ldms_dir_t dir = NULL;
	if (rc)
		goto out;
	dir = malloc(sizeof (*dir) +
		     (count * sizeof(char *)) +
		     len);
	rc = ENOMEM;
	if (!dir)
		goto out;
	rc = 0;
	dir->type = type;
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
	if (ctxt->dir.cb)
		ctxt->dir.cb((ldms_t)x, rc, dir, ctxt->dir.cb_arg);
	pthread_spin_lock(&x->lock);
	if (!x->local_dir_xid)
		free(ctxt);
	pthread_spin_unlock(&x->lock);
}

void ldms_dir_release(ldms_t t, ldms_dir_t d)
{
	free(d);
}

static int ldms_xprt_recv_reply(struct ldms_xprt *x, struct ldms_reply *reply)
{
	int cmd = ntohl(reply->hdr.cmd);
	uint64_t xid = reply->hdr.xid;
	struct ldms_context *ctxt;
	ctxt = (struct ldms_context *)(unsigned long)xid;
	switch (cmd) {
	case LDMS_CMD_LOOKUP_REPLY:
		process_lookup_reply(x, reply, ctxt);
		break;
	case LDMS_CMD_DIR_REPLY:
		process_dir_reply(x, reply, ctxt);
		break;
	default:
		printf("Unrecognized reply %d\n", cmd);
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

struct ldms_xprt local_transport = {
	.name = "local",
	.connect = local_xprt_connect,
	.destroy = local_xprt_destroy,
};

#if defined(__MACH__)
#define _SO_EXT ".dylib"
#undef LDMS_XPRT_LIBPATH_DEFAULT
#define LDMS_XPRT_LIBPATH_DEFAULT "/home/tom/macos/lib"
#else
#define _SO_EXT ".so"
#endif
static char _libdir[PATH_MAX];
ldms_t ldms_create_xprt(const char *name)
{
	int ret = 0;
	char *libdir;
	struct ldms_xprt *x = 0;
	char *errstr;
	int len;

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
	void *d = dlopen(_libdir, RTLD_NOW);
	if (!d) {
		printf("dlopen: %s\n", dlerror());
		/* The library doesn't exist */
		ret = ENOENT;
		goto err;
	}
	dlerror();
	ldms_xprt_get_t get = dlsym(d, "xprt_get");
	errstr = dlerror();
	if (errstr || !get) {
		printf("dlsym: %s\n", errstr);
		/* The library exists but doesn't export the correct
		 * symbol and is therefore likely the wrong library type */
		ret = EINVAL;
		goto err;
	}
	x = get(recv_cb, read_complete_cb);
	if (!x) {
		/* The transport library refused the request */
		ret = ENOSYS;
		goto err;
	}
	strcpy(x->name, name);
	x->connected = 0;
	x->ref_count = 1;
	x->remote_dir_xid = x->local_dir_xid = 0;
	char tmp[32] = "io_ctxt.XXXXXX";
	x->io_ctxt.sem_p = sem_open(mktemp(tmp), O_CREAT, 0666, 0);
	if (!x->io_ctxt.sem_p) {
		perror("Could not create semaphore");
		exit(1);
	}
	pthread_spin_init(&x->lock, 0);
	pthread_spin_lock(&xprt_list_lock);
	LIST_INSERT_HEAD(&xprt_list, x, xprt_link);
	pthread_spin_unlock(&xprt_list_lock);
	return x;
 err:
	if (x)
		free(x);
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

int ldms_remote_dir(ldms_t _x, ldms_dir_cb_t cb, void *cb_arg, uint32_t flags)
{
	struct ldms_xprt *x = _x;
 	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (alloc_req_ctxt(&req, &ctxt))
		return ENOMEM;

	pthread_spin_lock(&x->lock);
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
	pthread_spin_unlock(&x->lock);

	return x->send(x, req, len);
}

/* This request has no reply */
void ldms_remote_dir_cancel(ldms_t _x)
{
	struct ldms_xprt *x = _x;
 	struct ldms_request *req;
	struct ldms_context *ctxt;
	size_t len;

	if (alloc_req_ctxt(&req, &ctxt))
		return;

	pthread_spin_lock(&x->lock);
	if (x->local_dir_xid)
		free((void *)(unsigned long)x->local_dir_xid);
	x->local_dir_xid = 0;
	pthread_spin_unlock(&x->lock);

	len = format_dir_cancel_req(req);
	x->send(x, req, len);
	free(ctxt);
}

int ldms_remote_lookup(ldms_t _x, const char *path,
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
	ctxt->lookup.cb = cb;
	ctxt->lookup.cb_arg = arg;
	ctxt->lookup.path = strdup(path);
	rc = x->send(x, req, len);
	if (rc)
		ldms_xprt_close(x);
	return rc;
}

int ldms_connect(ldms_t _x, struct sockaddr *sa, socklen_t sa_len)
{
	int rc = -1;
	struct ldms_xprt *x = _x;

	pthread_spin_lock(&x->lock);
	if (x->connected) {
		errno = EBUSY;
		goto out;
	}
	memcpy(&x->ss, sa, sa_len);
	x->ss_len = sa_len;
	rc = x->connect(x, sa, sa_len);
	if (!rc)
		x->connected = 1;

 out:
	pthread_spin_unlock(&x->lock);
	return rc;
}

int ldms_listen(ldms_t _x, struct sockaddr *sa, socklen_t sa_len)
{
	struct ldms_xprt *x = _x;
	memcpy(&x->ss, sa, sa_len);
	x->ss_len = sa_len;
	return x->listen(x, sa, sa_len);
}

struct ldms_rbuf_desc *ldms_alloc_rbd(struct ldms_xprt *x,
				      struct ldms_set *s,
				      enum ldms_rbuf_type type,
				      void *xprt_data, size_t xprt_data_len)
{
	struct ldms_rbuf_desc *rbd = x->alloc(x, s, type, xprt_data, xprt_data_len);
	if (!rbd)
		goto out_0;

	rbd->xprt = x;
	rbd->set = s;

	/* Add RBD to set list */
	LIST_INSERT_HEAD(&s->rbd_list, rbd, set_link);
	LIST_INSERT_HEAD(&x->rbd_list, rbd, xprt_link);

 out_0:
	return rbd;
}

void ldms_free_rbd(struct ldms_rbuf_desc *rbd)
{
	LIST_REMOVE(rbd, xprt_link);
	LIST_REMOVE(rbd, set_link);

	rbd->xprt->free(rbd->xprt, rbd);
}

struct ldms_rbuf_desc *ldms_lookup_rbd(struct ldms_xprt *x, struct ldms_set *set)
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

void __attribute__ ((constructor)) cs_init(void)
{
	pthread_spin_init(&xprt_list_lock, 0);
}
