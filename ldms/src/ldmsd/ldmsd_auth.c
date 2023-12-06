/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <netdb.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_request.h"
#include "config.h"

static
void __ldmsd_auth_del(struct ldmsd_cfgobj *obj)
{
	ldmsd_auth_t auth = (void*)obj;
	if (auth->plugin)
		free(auth->plugin);
	if (auth->attrs)
		av_free(auth->attrs);
	ldmsd_cfgobj___del(obj);
}

ldmsd_auth_t
ldmsd_auth_new_with_auth(const char *name, const char *plugin,
			 struct attr_value_list *attrs,
			 uid_t uid, gid_t gid, int perm)
{
	ldmsd_auth_t auth = NULL;

	if (!plugin) {
		errno = EINVAL;
		goto err;
	}

	auth = (ldmsd_auth_t) ldmsd_cfgobj_new_with_auth(name,
					LDMSD_CFGOBJ_AUTH,
					sizeof(struct ldmsd_auth),
					__ldmsd_auth_del, uid, gid, perm);
	if (!auth)
		goto err;
	auth->plugin = strdup(plugin);
	if (!auth->plugin)
		goto err;
	if (attrs) {
		auth->attrs = av_copy(attrs);
		if (!auth->attrs)
			goto err;
	}
	ldmsd_cfgobj_unlock(&auth->obj);
	return auth;

 err:
	if (auth) {
		ldmsd_cfgobj_unlock(&auth->obj);
		ldmsd_auth_put(auth, "init");
	}
	return NULL;
}

ldmsd_cfgobj_t __cfgobj_find(const char *name, ldmsd_cfgobj_type_t type);
extern struct rbt *cfgobj_trees[];

int ldmsd_auth_del(const char *name, ldmsd_sec_ctxt_t ctxt)
{
	int rc = 0;
	ldmsd_auth_t auth;
	ldmsd_cfg_lock(LDMSD_CFGOBJ_AUTH);
	auth = (ldmsd_auth_t)__cfgobj_find(name, LDMSD_CFGOBJ_AUTH); /* this increase ref */
	if (!auth) {
		rc = ENOENT;
		goto out;
	}
	rc = ldmsd_cfgobj_access_check(&auth->obj, 0222, ctxt);
	if (rc)
		goto out;
	/* remove from the tree */
	rbt_del(cfgobj_trees[LDMSD_CFGOBJ_AUTH], &auth->obj.rbn);
	ldmsd_auth_put(auth, "cfgobj_tree"); /* correspond to `new` */
 out:
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_AUTH);
	if (auth) /* put ref back from `find` */
		ldmsd_auth_put(auth, "find");
	return rc;
}

ldmsd_auth_t ldmsd_auth_default_get()
{
	return ldmsd_auth_find(DEFAULT_AUTH);
}

int ldmsd_auth_default_set(const char *plugin, struct attr_value_list *attrs)
{
	ldmsd_auth_t d = ldmsd_auth_default_get();
	int rc;
	if (!d) {
		/* default auth domain has not been created yet */
		d = ldmsd_auth_new_with_auth(DEFAULT_AUTH, plugin, attrs,
				geteuid(), getegid(), 0600);
		if (d)
			return 0;
		else
			return errno;
	}
	if (d->plugin) {
		free(d->plugin);
		d->plugin = NULL;
	}
	if (d->attrs) {
		av_free(d->attrs);
		d->attrs = NULL;
	}
	d->plugin = strdup(plugin);
	if (!d->plugin) {
		rc = ENOMEM;
		goto out;
	}
	if (attrs) {
		d->attrs = av_copy(attrs);
		if (!d->attrs) {
			rc = ENOMEM;
			goto out;
		}
	}
	rc = 0;
 out:
	ldmsd_auth_put(d, "find");
	return rc;
}
