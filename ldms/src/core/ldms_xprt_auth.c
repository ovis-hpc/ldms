/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2018 Sandia Corporation. All rights reserved.
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

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include <assert.h>

#include "ldms_xprt.h"
#include "ldms_auth.h"

/* functions from ldms_xprt.c */
struct ldms_context *__ldms_alloc_ctxt(struct ldms_xprt *x, size_t sz,
		ldms_context_type_t type);
void __ldms_free_ctxt(struct ldms_xprt *x, struct ldms_context *ctxt);

/* ========================
 * ==== LDMS_XPRT_AUTH ====
 * ========================
 */

int ldms_xprt_auth_bind(ldms_t _x, ldms_auth_t a)
{
	struct ldms_xprt *x = _x;
	if (x->auth_flag != LDMS_XPRT_AUTH_INIT)
		return EINVAL;
	if (x->auth)
		return EEXIST;
	x->auth = a;
	/* also notify the auth object about the binding */
	return x->auth->plugin->auth_xprt_bind(a, _x);
}

int ldms_xprt_auth_send(ldms_t _x, const char *msg_buf, size_t msg_len)
{
	struct ldms_xprt *x = _x;
	struct ldms_request *req;
	size_t len;
	struct ldms_context *ctxt;
	int rc;

	if (!msg_buf)
		return EINVAL;

	if (x->auth_flag != LDMS_XPRT_AUTH_BUSY)
		return EINVAL;

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
	req->hdr.cmd = htonl(LDMS_CMD_AUTH_MSG);
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

void ldms_xprt_auth_begin(ldms_t xprt)
{
	int rc;
	struct ldms_xprt *x = xprt;
	if (x->auth_flag != LDMS_XPRT_AUTH_INIT)
		return;
	x->auth_flag = LDMS_XPRT_AUTH_BUSY;
	rc = xprt->auth->plugin->auth_xprt_begin(xprt->auth, xprt);
	if (rc) {
		__ldms_xprt_term(x);
	}
}

void ldms_xprt_auth_end(ldms_t xprt, int result)
{
	struct ldms_xprt *x = xprt;
	struct ldms_xprt_event event = {0};
	assert(x->auth_flag == LDMS_XPRT_AUTH_BUSY);
	if (result) {
		xprt->auth_flag = LDMS_XPRT_AUTH_FAILED;
		event.type = LDMS_XPRT_EVENT_REJECTED;
		/* app callback deferred: see ldms_zap_cb() DISCONNECTED */
		__ldms_xprt_term(x);
	} else {
		xprt->auth_flag = LDMS_XPRT_AUTH_APPROVED;
		event.type = LDMS_XPRT_EVENT_CONNECTED;
		x->event_cb(x, &event, x->event_cb_arg);
	}
}

/*
 * \retval 0 OK
 * \retval errno for error
 */
int __uid_gid_check(uid_t uid, gid_t gid)
{
	struct passwd pw;
	struct passwd *p;
	int rc;
	char *buf;
	int i;
	int n = 128;
	gid_t gid_list[n];

	buf = malloc(BUFSIZ);
	if (!buf) {
		rc = ENOMEM;
		goto out;
	}

	rc = getpwuid_r(uid, &pw, buf, BUFSIZ, &p);
	if (!p) {
		rc = rc?rc:ENOENT;
		goto out;
	}

	rc = getgrouplist(p->pw_name, p->pw_gid, gid_list, &n);
	if (rc == -1) {
		rc = ENOBUFS;
		goto out;
	}

	for (i = 0; i < n; i++) {
		if (gid == gid_list[i]) {
			rc = 0;
			goto out;
		}
	}

	rc = ENOENT;

out:
	if (buf)
		free(buf);
	return rc;
}

int ldms_access_check(ldms_t x, uint32_t acc, uid_t obj_uid, gid_t obj_gid,
		      int obj_perm)
{
	int macc = acc & obj_perm;
	/* other */
	if (07 & macc) {
		return 0;
	}
	/* owner */
	if (0700 & macc) {
		if (x->ruid == obj_uid)
			return 0;
	}
	/* group  */
	if (070 & macc) {
		if (x->rgid == obj_gid)
			return 0;
		/* else need to check x->ruid group list */
		if (0 == __uid_gid_check(x->ruid, obj_gid))
			return 0;
	}
	return EACCES;
}
