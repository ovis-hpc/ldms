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
#include "../core/ldms_auth.h"

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
	.name = "none",
	.auth_new = __auth_new,
	.auth_clone = __auth_clone,
	.auth_free = __auth_free,
	.auth_xprt_bind = __auth_xprt_bind,
	.auth_xprt_begin = __auth_xprt_begin,
	.auth_xprt_recv_cb = __auth_xprt_recv_cb,
	.auth_cred_get = __auth_cred_get,
};

struct ldms_auth auth_obj = { .plugin = &plugin };

static
ldms_auth_t __auth_new(ldms_auth_plugin_t plugin,
		       struct attr_value_list *av_list)
{
	return &auth_obj;
}

static
ldms_auth_t __auth_clone(ldms_auth_t auth)
{
	return auth;
}

static
void __auth_free(ldms_auth_t auth)
{
	/* do nothing */
}

static
int __auth_xprt_bind(ldms_auth_t auth, ldms_t xprt)
{
	assert(auth == &auth_obj);
	/* do nothing */
	return 0;
}

static
int __auth_xprt_begin(ldms_auth_t auth, ldms_t xprt)
{
	/* accepts everything */
	xprt->luid = 0;
	xprt->lgid = 0;
	xprt->ruid = 0;
	xprt->rgid = 0;
	ldms_xprt_auth_end(xprt, 0);
	return 0;
}

static
int __auth_xprt_recv_cb(ldms_auth_t auth, ldms_t xprt,
		const char *data, uint32_t data_len)
{
	/* do nothing */
	return 0;
}

static
int __auth_cred_get(ldms_auth_t auth, ldms_cred_t cred)
{
	cred->uid = 0;
	cred->gid = 0;
	return 0;
}

ldms_auth_plugin_t __ldms_auth_plugin_get()
{
	return &plugin;
}
