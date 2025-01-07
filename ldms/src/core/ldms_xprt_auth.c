/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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
#include "ldms_private.h"

/* ========================
 * ==== LDMS_XPRT_AUTH ====
 * ========================
 */

/* Defined in ldms.c */
extern ovis_log_t xlog;

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
	if (rc) {
		ovis_log(xlog, OVIS_LDEBUG, "send: error. put ref %p.\n", x->zap_ep);
	}
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

int ldms_access_check(ldms_t x, uint32_t acc, uid_t obj_uid, gid_t obj_gid,
		      int obj_perm)
{
	int rc;
	struct ldms_cred rmt;
	ldms_xprt_cred_get(x, NULL, &rmt);
	rc = ovis_access_check(rmt.uid, rmt.gid, acc,
				obj_uid, obj_gid, obj_perm);
	if (rc) {
		ovis_log(xlog, OVIS_LINFO,
			"Access denied to user %d:%d:%o for object %d:%d:%o.\n",
			rmt.uid, rmt.gid, acc, obj_uid, obj_gid, obj_perm);
	}
	return rc;
}
