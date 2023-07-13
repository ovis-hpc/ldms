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

#include <assert.h>
#include "ovis_log/ovis_log.h"
#include "../core/ldms_auth.h"

static ovis_log_t nlog = NULL;

#define LOG(_level_, _fmt_, ...) do { \
	ovis_log(nlog, _level_, _fmt_, ##__VA_ARGS__); \
} while (0);

#define LOG_ERROR(_fmt_, ...) do { \
	ovis_log(nlog, OVIS_LERROR, _fmt_, ## __VA_ARGS__); \
} while (0);

#define LOG_OOM() do { \
	ovis_log(nlog, OVIS_LCRITICAL, "Memory allocation failure.\n"); \
} while (0);

static
ldms_auth_t __auth_new(ldms_auth_plugin_t plugin,
		       struct attr_value_list *av_list);
static
ldms_auth_t __auth_clone(ldms_auth_t auth);
static
void __auth_free(ldms_auth_t auth);
static
int __auth_xprt_bind(ldms_auth_t auth, ldms_t xprt);
static
int __auth_xprt_begin(ldms_auth_t auth, ldms_t xprt);
static
int __auth_xprt_recv_cb(ldms_auth_t auth, ldms_t xprt,
		const char *data, uint32_t data_len);
static
int __auth_cred_get(ldms_auth_t auth, ldms_cred_t cred);

struct ldms_auth_plugin plugin = {
	.name = "naive",
	.auth_new = __auth_new,
	.auth_clone = __auth_clone,
	.auth_free = __auth_free,
	.auth_xprt_bind = __auth_xprt_bind,
	.auth_xprt_begin = __auth_xprt_begin,
	.auth_xprt_recv_cb = __auth_xprt_recv_cb,
	.auth_cred_get = __auth_cred_get,
};

struct ldms_auth_naive {
	struct ldms_auth base;
	uid_t luid;
	gid_t lgid;
};

ldms_auth_plugin_t __ldms_auth_plugin_get()
{
	if (!nlog) {
		nlog = ovis_log_register("auth.naive", "Messages for auth_naive");
		if (!nlog) {
			LOG_ERROR("Failed to register the auth_naive log.\n");
		}
	}
	return &plugin;
}

static
ldms_auth_t __auth_new(ldms_auth_plugin_t plugin,
		       struct attr_value_list *av_list)
{
	struct ldms_auth_naive *a = calloc(1, sizeof(*a));
	if (!a) {
		LOG_OOM();
		errno = ENOMEM;
		return NULL;
	}
	const char *val;
	val = av_value(av_list, "uid");
	if (!val)
		a->luid = 65534;
	else
		a->luid = atoi(val);
	val = av_value(av_list, "gid");
	if (val)
		a->lgid = atoi(val);
	else
		a->lgid = 65534;
	return &a->base;
}

static
ldms_auth_t __auth_clone(ldms_auth_t auth)
{
	struct ldms_auth_naive *a = calloc(1, sizeof(*a));
	if (!a) {
		LOG_OOM();
		return NULL;
	}
	memcpy(a, auth, sizeof(*a));
	return &a->base;
}

static
void __auth_free(ldms_auth_t auth)
{
	free(auth);
}

static
int __auth_xprt_bind(ldms_auth_t auth, ldms_t xprt)
{
	struct ldms_auth_naive *a = (void*)auth;
	xprt->luid = a->luid;
	xprt->lgid = a->lgid;
	return 0;
}

struct naive_cred {
	uint32_t uid;
	uint32_t gid;
};

static
int __auth_xprt_begin(ldms_auth_t auth, ldms_t xprt)
{
	struct ldms_auth_naive *a = (void*)auth;
	struct naive_cred crd = {
		.uid = htonl(a->luid),
		.gid = htonl(a->lgid)
	};
	return ldms_xprt_auth_send(xprt, (void*)&crd, sizeof(crd));
}

static
int __auth_xprt_recv_cb(ldms_auth_t auth, ldms_t xprt,
		const char *data, uint32_t data_len)
{
	struct naive_cred crd;
	if (data_len != sizeof(crd)) {
		LOG_ERROR("Received unexpected credential size.\n");
		return EINVAL;
	}
	memcpy(&crd, data, data_len);
	crd.uid = ntohl(crd.uid);
	crd.gid = ntohl(crd.gid);
	/* we naively belive what the peer said who he is */
	xprt->rgid = crd.gid;
	xprt->ruid = crd.uid;
	ldms_xprt_auth_end(xprt, 0);
	return 0;
}

static
int __auth_cred_get(ldms_auth_t auth, ldms_cred_t cred)
{
	struct ldms_auth_naive *a = (void*)auth;
	cred->uid = a->luid;
	cred->gid = a->lgid;
	return 0;
}
