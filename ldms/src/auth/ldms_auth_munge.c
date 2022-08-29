/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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

#include <munge.h>
#include <assert.h>

#include "../core/ldms_auth.h"

static
ldms_auth_t __auth_munge_new(ldms_auth_plugin_t plugin,
			     struct attr_value_list *av_list);
static
ldms_auth_t __auth_munge_clone(ldms_auth_t auth);
static
void __auth_munge_free(ldms_auth_t auth);
static
int __auth_munge_xprt_bind(ldms_auth_t auth, ldms_t xprt);
static
int __auth_munge_xprt_begin(ldms_auth_t auth, ldms_t xprt);
static
int __auth_munge_xprt_recv_cb(ldms_auth_t auth, ldms_t xprt,
			      const char *data, uint32_t data_len);
static
int __auth_munge_cred_get(ldms_auth_t auth, ldms_cred_t cred);

struct ldms_auth_plugin plugin = {
	.name = "munge",
	.auth_new = __auth_munge_new,
	.auth_clone = __auth_munge_clone,
	.auth_free = __auth_munge_free,
	.auth_xprt_bind = __auth_munge_xprt_bind,
	.auth_xprt_begin = __auth_munge_xprt_begin,
	.auth_xprt_recv_cb = __auth_munge_xprt_recv_cb,
	.auth_cred_get = __auth_munge_cred_get,
};

struct ldms_auth_munge {
	struct ldms_auth base;
	munge_ctx_t mctx;
	char *local_cred;
	struct sockaddr_storage lsin;
	struct sockaddr_storage rsin;
	socklen_t sin_len;
};

static
ldms_auth_t __auth_munge_new(ldms_auth_plugin_t plugin,
		       struct attr_value_list *av_list)
{
	const char *munge_sock;
	struct ldms_auth_munge *a;
	munge_ctx_t mctx;
	munge_err_t merr;
	char *test_cred;

	a = calloc(1, sizeof(*a));
	if (!a)
		goto err0;

	mctx = munge_ctx_create();
	if (!mctx)
		goto err1;

	munge_sock = av_value(av_list, "socket");
	if (munge_sock) {
		merr = munge_ctx_set(mctx, MUNGE_OPT_SOCKET, munge_sock);
		if (merr != EMUNGE_SUCCESS)
			goto err2;
	}

	/* Test munge connection */
	merr = munge_encode(&test_cred, mctx, NULL, 0);
	if (merr != EMUNGE_SUCCESS)
		goto err2;
	free(test_cred);

	a->mctx = mctx;
	return &a->base;

err2:
	munge_ctx_destroy(mctx);
err1:
	free(a);
err0:
	return NULL;
}

static
ldms_auth_t __auth_munge_clone(ldms_auth_t auth)
{
	struct ldms_auth_munge *_a = (void*)auth;
	struct ldms_auth_munge *a = calloc(1, sizeof(*a));
	if (!a)
		return NULL;
	memcpy(a, auth, sizeof(*a));
	a->local_cred = NULL;
	a->mctx = munge_ctx_copy(_a->mctx);
	if (!a->mctx) {
		free(a);
		return NULL;
	}
	return &a->base;
}

static
void __auth_munge_free(ldms_auth_t auth)
{
	struct ldms_auth_munge *a = (void*)auth;
	if (a->local_cred)
		free(a->local_cred);
	if (a->mctx)
		munge_ctx_destroy(a->mctx);
	free(a);
}

static
int __auth_munge_xprt_bind(ldms_auth_t auth, ldms_t xprt)
{
	xprt->luid = getuid();
	xprt->lgid = getgid();
	return 0;
}

static
int __auth_munge_xprt_begin(ldms_auth_t auth, ldms_t xprt)
{
	struct ldms_auth_munge *a = (void*)auth;
	munge_err_t merr;
	int rc;
	int len;
	a->sin_len = sizeof(a->lsin);
	rc = ldms_xprt_sockaddr(xprt, (void*)&a->lsin,
				(void*)&a->rsin, &a->sin_len);
	if (rc)
		return rc;
	/*
	 * zap_rdma from OVIS-4.3.7 and earlier has a bug that swaps
	 * local/remote addresses. Since the old peers expect to receive the
	 * swapped address, in order to be compatible with them, we have to send
	 * the swapped address in the case of rdma transport.
	 */
	merr = (strncmp(xprt->name, "rdma", 4) == 0)?
		munge_encode(&a->local_cred, a->mctx, &a->rsin, a->sin_len):
		munge_encode(&a->local_cred, a->mctx, &a->lsin, a->sin_len);

	if (merr)
		return EBADR; /* bad request */
	len = strlen(a->local_cred);
	rc = ldms_xprt_auth_send(xprt, a->local_cred, len + 1);
	if (rc)
		return rc;
	return 0;
}

static
int __auth_munge_xprt_recv_cb(ldms_auth_t auth, ldms_t xprt,
		const char *data, uint32_t data_len)
{
	struct ldms_auth_munge *a = (void*)auth;
	struct sockaddr_in *sin;
	void *payload = NULL;
	uid_t uid;
	gid_t gid;
	int len;
	int cmp;
	munge_err_t merr;
	if (data[data_len-1] != 0)
		goto invalid;
	merr = munge_decode(data, a->mctx, &payload, &len, &uid, &gid);
	if (merr != EMUNGE_SUCCESS)
		goto invalid;
	if (len != sizeof(*sin))
		goto invalid; /* bad payload */

	/* check if addr match */
	sin = payload;
	/*
	 * zap_rdma from OVIS-4.3.7 and earlier has a bug that swaps
	 * local/remote addresses. Since the old peers send the swapped
	 * address, in order to be compatible with them, we have to expect the
	 * swapped address in the case of rdma transport.
	 */
	cmp = (strncmp(xprt->name, "rdma", 4) == 0)?
			memcmp(sin, &a->lsin, sizeof(*sin)):
			memcmp(sin, &a->rsin, sizeof(*sin));
	if (cmp != 0)
		goto invalid; /* bad addr */
	/* verified */
	xprt->ruid = uid;
	xprt->rgid = gid;

	free(payload);
	ldms_xprt_auth_end(xprt, 0);
	return 0;

invalid:
	if (payload)
		free(payload);
	ldms_xprt_auth_end(xprt, EINVAL);
	return 0;
}

static
int __auth_munge_cred_get(ldms_auth_t auth, ldms_cred_t cred)
{
	struct ldms_auth_munge *a = (void*)auth;
	char *tmp;
	munge_err_t merr;
	merr = munge_encode(&tmp, a->mctx, NULL, 0);
	if (merr)
		return EBADR;
	merr = munge_decode(tmp, a->mctx, NULL, NULL, &cred->uid, &cred->gid);
	free(tmp);
	if (merr)
		return EBADR;
	return 0;
}

ldms_auth_plugin_t __ldms_auth_plugin_get()
{
	return &plugin;
}
