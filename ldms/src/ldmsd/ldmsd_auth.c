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

static struct attr_value_list *json_dict2avl(json_entity_t d)
{
	struct attr_value_list *avl;
	json_entity_t a;
	json_str_t n, v;

	avl = av_new(json_attr_count(d));
	if (!avl)
		return NULL;
	for (a = json_attr_first(d); a; a = json_attr_next(a)) {
		n = json_attr_name(a);
		v = json_value_str(json_attr_value(a));
		if (av_add(avl, n->str, v->str)) {
			av_free(avl);
			return NULL;
		}
	}
	return avl;
}

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

static json_entity_t __auth_get_attr(const char *name, json_entity_t dft,
					json_entity_t spc, char **_plugin,
					json_entity_t *_attrs)
{
	int rc;
	json_entity_t plugin, attrs = NULL;
	json_entity_t err = NULL;

	if (spc)
		plugin = json_value_find(spc, "plugin");

	if (!plugin && dft)
		plugin = json_value_find(dft, "plugin");

	if (dft) {
		attrs = json_entity_copy(dft);
		if (!attrs)
			goto oom;
		if (spc) {
			rc = json_dict_merge(attrs, spc);
			if (rc)
				goto oom;
		}
	} else {
		if (spc) {
			attrs = json_entity_copy(spc);
			if (!attrs)
				goto oom;
		}
	}

	/* Attributes */
	if (attrs) {
		json_attr_rem(attrs, "plugin");
		*_attrs = attrs;
	}

	/* plugin */
	if (plugin) {
		if (JSON_STRING_VALUE != json_entity_type(plugin)) {
			err = json_dict_build(err, JSON_STRING_VALUE, "plugin",
					"'plugin' is not a JSON string.");
			if (!err)
				goto oom;
		} else {
			*_plugin = json_value_str(plugin)->str;
		}
	}

	return err;
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	errno = ENOMEM;
	if (err)
		json_entity_free(err);
	return NULL;
}

ldmsd_cfgobj_t __cfgobj_find(const char *name, ldmsd_cfgobj_type_t type);
struct rbt **cfgobj_trees;

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
	ldmsd_cfgobj_put(&auth->obj); /* correspond to `new` */
 out:
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_AUTH);
	if (auth) /* put ref back from `find` */
		ldmsd_cfgobj_put(&auth->obj);
	return rc;
}

static int ldmsd_auth_enable(ldmsd_cfgobj_t obj)
{
	/* Nothing to do */
	return 0;
}

static int ldmsd_auth_disable(ldmsd_cfgobj_t obj)
{
	/* nothing to do */
	return 0;
}

json_entity_t ldmsd_auth_query(ldmsd_cfgobj_t obj)
{
	json_entity_t query, a, args = NULL;
	ldmsd_auth_t auth = (ldmsd_auth_t)obj;
	int i;
	char *name, *value;

	query = ldmsd_cfgobj_query_result_new(obj);
	if (!query)
		goto oom;
	query = json_dict_build(query,
			JSON_STRING_VALUE, "plugin", auth->plugin, -1);
	if (!query)
		goto oom;
	if (auth->attrs) {
		for (i = 0, name = av_name(auth->attrs, i); name; i++) {
			value = av_value_at_idx(auth->attrs, i);
			args = json_dict_build(args, JSON_STRING_VALUE, name, value);
			if (!args)
				goto oom;
		}
		a = json_entity_new(JSON_ATTR_VALUE, "args", args);
		if (!a)
			goto oom;
		json_attr_add(query, a);
	}
	return ldmsd_result_new(0, NULL, query);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}

json_entity_t ldmsd_auth_update(ldmsd_cfgobj_t obj, short enabled,
					json_entity_t dft, json_entity_t spc)
{
	/* Not allow */
	return ldmsd_result_new(ENOTSUP, "Authentication object cannot be updated.", NULL);
}

ldmsd_auth_t
ldmsd_auth_new(const char *name, const char *plugin, json_entity_t attrs,
		uid_t uid, gid_t gid, int perm, short enabled)
{
	ldmsd_auth_t auth;
	auth = (ldmsd_auth_t) ldmsd_cfgobj_new(name,
					LDMSD_CFGOBJ_AUTH,
					sizeof(struct ldmsd_auth),
					__ldmsd_auth_del,
					ldmsd_auth_update,
					ldmsd_cfgobj_delete,
					ldmsd_auth_query,
					ldmsd_auth_query,
					ldmsd_auth_enable,
					ldmsd_auth_disable,
					uid, gid, perm, enabled);
	if (!auth)
		goto oom;
	auth->plugin = strdup(plugin);
	if (!auth->plugin)
		goto oom;
	if (attrs) {
		auth->attrs = json_dict2avl(attrs);
		if (!auth->attrs)
			goto oom;
	}
	ldmsd_cfgobj_unlock(&auth->obj);
	return auth;
oom:
	errno = ENOMEM;
	return NULL;
}

json_entity_t ldmsd_auth_create(const char *name, short enabled, json_entity_t dft,
					json_entity_t spc, uid_t uid, gid_t gid)
{
	json_entity_t err, attrs = NULL;
	char *plugin = NULL;
	ldmsd_auth_t auth = NULL;

	err = __auth_get_attr(name, dft, spc, &plugin, &attrs);
	if (!err) {
		if (errno == ENOMEM)
			goto oom;
	} else {
		return ldmsd_result_new(EINVAL, NULL, err);
	}

	if (!plugin) {
		err = json_dict_build(NULL, JSON_STRING_VALUE, "plugin",
							"'plugin' is missing.");
		if (!err)
			goto oom;
		return ldmsd_result_new(EINVAL, NULL, err);
	}

	auth = ldmsd_auth_new(name, plugin, attrs, uid, gid, 0770, enabled);
	if (!auth) {
		goto oom;
	}
	return ldmsd_result_new(0, NULL, NULL);
oom:
	if (auth) {
		ldmsd_cfgobj_unlock(&auth->obj);
		ldmsd_cfgobj_put(&auth->obj);
	}
	if (attrs)
		json_entity_free(attrs);
	if (err)
		json_entity_free(err);
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}
