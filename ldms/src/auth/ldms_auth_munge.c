/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018,2023 Open Grid Computing, Inc. All rights reserved.
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ovis_log/ovis_log.h"
#include "../core/ldms_auth.h"

static ovis_log_t munge_log = NULL;

#define LOG(_level_, _fmt_, ...) do { \
	ovis_log(munge_log, _level_, _fmt_, ##__VA_ARGS__); \
} while (0);

#define LOG_OOM() do { \
	ovis_log(munge_log, OVIS_LCRITICAL, "Memory allocation failure.\n"); \
} while (0);

#define LOG_ERROR(_fmt_, ...) do { \
	ovis_log(munge_log, OVIS_LERROR, _fmt_, ##__VA_ARGS__); \
} while (0);

#define LOG_INFO(_fmt_, ...) do { \
	ovis_log(munge_log, OVIS_LINFO, _fmt_, ##__VA_ARGS__); \
} while (0);

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
	const char *value;
	struct ldms_auth_munge *a;
	munge_ctx_t mctx;
	munge_err_t merr;
	char *test_cred;

	a = calloc(1, sizeof(*a));
	if (!a) {
		LOG_OOM();
		goto err0;
	}

	mctx = munge_ctx_create();
	if (!mctx) {
		LOG_ERROR("Failed to create MUNGE context.\n");
		goto err1;
	}

	value = av_value(av_list, "socket");
	if (value) {
		merr = munge_ctx_set(mctx, MUNGE_OPT_SOCKET, value);
		if (merr != EMUNGE_SUCCESS) {
			LOG_ERROR("Failed to set MUNGE context. %s\n",
				   munge_strerror(merr));
			goto err2;
		}
	}

	/* Test munge connection */
	merr = munge_encode(&test_cred, mctx, NULL, 0);
	if (merr != EMUNGE_SUCCESS) {
		LOG_ERROR("Failed to encode MUNGE. %s\n", munge_strerror(merr));
		goto err2;
	}
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
	if (!a) {
		LOG_OOM();
		return NULL;
	}
	memcpy(a, auth, sizeof(*a));
	a->local_cred = NULL;
	a->mctx = munge_ctx_copy(_a->mctx);
	if (!a->mctx) {
		LOG_ERROR("Failed to copy the MUNGE context.\n");
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
	if (rc) {
		LOG_ERROR("Failed to get the socket addresses. Error %d\n", rc);
		return rc;
	}
	/*
	 * zap_rdma from OVIS-4.3.7 and earlier has a bug that swaps
	 * local/remote addresses. Since the old peers expect to receive the
	 * swapped address, in order to be compatible with them, we have to send
	 * the swapped address in the case of rdma transport.
	 */
	merr = (strncmp(xprt->name, "rdma", 4) == 0)?
		munge_encode(&a->local_cred, a->mctx, &a->rsin, a->sin_len):
		munge_encode(&a->local_cred, a->mctx, &a->lsin, a->sin_len);
	if (merr) {
		LOG_ERROR("munge_encode() failed. %s\n", munge_strerror(merr));
		return EBADR;
	}
	len = strlen(a->local_cred);
	rc = ldms_xprt_auth_send(xprt, a->local_cred, len + 1);
	if (rc) {
		LOG_ERROR("Failed to send the authentication info. Error %d\n", rc);
		return rc;
	}
	return 0;
}

static
int __auth_munge_xprt_recv_cb(ldms_auth_t auth, ldms_t xprt,
		const char *data, uint32_t data_len)
{
	struct ldms_auth_munge *a = (void*)auth;
	struct sockaddr_in *payload_sin;
	struct sockaddr_in *xprt_sin;
	void *payload = NULL;
	uid_t uid;
	gid_t gid;
	munge_err_t merr;
	int len;
	int cmp;
	int rc = EINVAL;

	merr = munge_decode(data, a->mctx, &payload, &len, &uid, &gid);
	if (merr != EMUNGE_SUCCESS) {
		LOG_ERROR("munge_decode() failed. %s\n", munge_strerror(merr));
		goto out;
	}

	/* Check the expected peer address (compatability mode) */
	payload_sin = payload;
	/*
	 * zap_rdma from OVIS-4.3.7 and earlier has a bug that swaps
	 * local/remote addresses. Since the old peers send the swapped
	 * address, in order to be compatible with them, we have to expect the
	 * swapped address in the case of rdma transport.
	 */
	xprt_sin = (strncmp(xprt->name, "rdma", 4) == 0 ?
		    (struct sockaddr_in *)&a->lsin :
		    (struct sockaddr_in *)&a->rsin);
	cmp = memcmp(payload_sin, xprt_sin, sizeof(*payload_sin));
	if (cmp != 0) {
		char ipa[16];
		char ipb[16];
		strcpy(ipa, inet_ntoa(payload_sin->sin_addr));
		strcpy(ipb, inet_ntoa(xprt_sin->sin_addr));
		LOG_INFO("Unexpected authentication message payload "
			  "'%s' != '%s'.\n", ipa, ipb);
	}
	rc = 0;
 out:
	/* Cache the peer's verified uid and gid in the transport handle. */
	if (!rc) {
		xprt->ruid = uid;
		xprt->rgid = gid;
	}
	free(payload);
	ldms_xprt_auth_end(xprt, rc);
	return 0;
}

static
int __auth_munge_cred_get(ldms_auth_t auth, ldms_cred_t cred)
{
	struct ldms_auth_munge *a = (void*)auth;
	char *tmp;
	munge_err_t merr;
	merr = munge_encode(&tmp, a->mctx, NULL, 0);
	if (merr) {
		LOG_ERROR("munge_encode() failed. %s\n", munge_strerror(merr));
		return EBADR;
	}
	merr = munge_decode(tmp, a->mctx, NULL, NULL, &cred->uid, &cred->gid);
	free(tmp);
	if (merr) {
		LOG_ERROR("munge_decode() failed. %s\n", munge_strerror(merr));
		return EBADR;
	}
	return 0;
}

ldms_auth_plugin_t __ldms_auth_plugin_get()
{
	if (!munge_log) {
		munge_log = ovis_log_register("auth.munge",
					      "Messages for ldms_auth_munge");
		if (!munge_log) {
			LOG_ERROR("Failed to register auth_munge's log. "
				  "Error %d\n", errno);
		}
	}
	return &plugin;
}
