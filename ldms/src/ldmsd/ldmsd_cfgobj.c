/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015,2018 Open Grid Computing, Inc. All rights reserved.
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
#include <coll/rbt.h>
#include "ldmsd.h"

int cfgobj_cmp(void *a, const void *b)
{
	return strcmp(a, b);
}

int ldmsd_cfgobj_access_check(ldmsd_cfgobj_t obj, int acc, ldmsd_sec_ctxt_t ctxt)
{
	return ovis_access_check(ctxt->crd.uid, ctxt->crd.gid, acc,
				 obj->uid, obj->gid, obj->perm);
}

struct rbt prdcr_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t prdcr_tree_lock = PTHREAD_MUTEX_INITIALIZER;

struct rbt updtr_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t updtr_tree_lock = PTHREAD_MUTEX_INITIALIZER;

struct rbt strgp_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t strgp_tree_lock = PTHREAD_MUTEX_INITIALIZER;

struct rbt smplr_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t smplr_tree_lock = PTHREAD_MUTEX_INITIALIZER;

struct rbt listen_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t listen_tree_lock = PTHREAD_MUTEX_INITIALIZER;

struct rbt setgrp_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t setgrp_tree_lock = PTHREAD_MUTEX_INITIALIZER;

struct rbt auth_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t auth_tree_lock = PTHREAD_MUTEX_INITIALIZER;

struct rbt env_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t env_tree_lock = PTHREAD_MUTEX_INITIALIZER;

struct rbt daemon_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t daemon_tree_lock = PTHREAD_MUTEX_INITIALIZER;

struct rbt plugin_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t plugin_tree_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t *cfgobj_locks[] = {
	[LDMSD_CFGOBJ_PRDCR] = &prdcr_tree_lock,
	[LDMSD_CFGOBJ_UPDTR] = &updtr_tree_lock,
	[LDMSD_CFGOBJ_STRGP] = &strgp_tree_lock,
	[LDMSD_CFGOBJ_SMPLR] = &smplr_tree_lock,
	[LDMSD_CFGOBJ_LISTEN] = &listen_tree_lock,
	[LDMSD_CFGOBJ_SETGRP] = &setgrp_tree_lock,
	[LDMSD_CFGOBJ_AUTH]   = &auth_tree_lock,
	[LDMSD_CFGOBJ_ENV]   = &env_tree_lock,
	[LDMSD_CFGOBJ_DAEMON] = &daemon_tree_lock,
	[LDMSD_CFGOBJ_PLUGIN] = &plugin_tree_lock,
};

struct rbt *cfgobj_trees[] = {
	[LDMSD_CFGOBJ_PRDCR] = &prdcr_tree,
	[LDMSD_CFGOBJ_UPDTR] = &updtr_tree,
	[LDMSD_CFGOBJ_STRGP] = &strgp_tree,
	[LDMSD_CFGOBJ_SMPLR] = &smplr_tree,
	[LDMSD_CFGOBJ_LISTEN] = &listen_tree,
	[LDMSD_CFGOBJ_SETGRP] = &setgrp_tree,
	[LDMSD_CFGOBJ_AUTH]   = &auth_tree,
	[LDMSD_CFGOBJ_ENV]   = &env_tree,
	[LDMSD_CFGOBJ_DAEMON] = &daemon_tree,
	[LDMSD_CFGOBJ_PLUGIN] = &plugin_tree,
};

struct cfgobj_type_entry {
	const char *s;
	enum ldmsd_cfgobj_type e;
};

static int cfgobj_type_entry_comp(const void *a, const void *b)
{
	struct cfgobj_type_entry *a_, *b_;
	a_ = (struct cfgobj_type_entry *)a;
	b_ = (struct cfgobj_type_entry *)b;
	return strcmp(a_->s, b_->s);
}

const char *ldmsd_cfgobj_types[] = {
		[LDMSD_CFGOBJ_PRDCR]	= "prdcr",
		[LDMSD_CFGOBJ_UPDTR]	= "updtr",
		[LDMSD_CFGOBJ_STRGP]	= "strgp",
		[LDMSD_CFGOBJ_SMPLR]	= "smplr",
		[LDMSD_CFGOBJ_LISTEN]	= "listen",
		[LDMSD_CFGOBJ_SETGRP]	= "setgrp",
		[LDMSD_CFGOBJ_AUTH]	= "auth",
		[LDMSD_CFGOBJ_ENV]	= "env",
		[LDMSD_CFGOBJ_DAEMON]	= "daemon",
		[LDMSD_CFGOBJ_PLUGIN]	= "plugin",
		NULL,
};

static struct cfgobj_type_entry cfgobj_type_tbl[] = {
		{ "auth",	LDMSD_CFGOBJ_AUTH },
		{ "daemon",	LDMSD_CFGOBJ_DAEMON },
		{ "env",	LDMSD_CFGOBJ_ENV },
		{ "listen",	LDMSD_CFGOBJ_LISTEN },
		{ "plugin",	LDMSD_CFGOBJ_PLUGIN },
		{ "prdcr",	LDMSD_CFGOBJ_PRDCR },
		{ "setgrp",	LDMSD_CFGOBJ_SETGRP },
		{ "smplr",	LDMSD_CFGOBJ_SMPLR },
		{ "strgp",	LDMSD_CFGOBJ_STRGP },
		{ "updtr",	LDMSD_CFGOBJ_UPDTR },
};

enum ldmsd_cfgobj_type ldmsd_cfgobj_type_str2enum(const char *s)
{
	struct cfgobj_type_entry *entry;

	entry = bsearch(&s, cfgobj_type_tbl, ARRAY_SIZE(cfgobj_type_tbl),
				sizeof(*entry), cfgobj_type_entry_comp);
	if (!entry)
		return -1;
	return entry->e;
}

const char *ldmsd_cfgobj_type2str(enum ldmsd_cfgobj_type type)
{
	return ldmsd_cfgobj_types[type];
}

void ldmsd_cfgobj___del(ldmsd_cfgobj_t obj)
{
	ev_put(obj->enabled_ev);
	ev_put(obj->disabled_ev);
	free(obj->name);
	free(obj);
}

void ldmsd_cfg_lock(ldmsd_cfgobj_type_t type)
{
	pthread_mutex_lock(cfgobj_locks[type]);
}

void ldmsd_cfg_unlock(ldmsd_cfgobj_type_t type)
{
	pthread_mutex_unlock(cfgobj_locks[type]);
}

void ldmsd_cfgobj_lock(ldmsd_cfgobj_t obj)
{
	pthread_mutex_lock(&obj->lock);
}

void ldmsd_cfgobj_unlock(ldmsd_cfgobj_t obj)
{
	pthread_mutex_unlock(&obj->lock);
}

int ldmsd_cfgobj_init(ldmsd_cfgobj_t obj, const char *name,
				ldmsd_cfgobj_type_t type,
				ldmsd_cfgobj_del_fn_t __del,
				ldmsd_cfgobj_update_fn_t update,
				ldmsd_cfgobj_delete_fn_t delete,
				ldmsd_cfgobj_query_fn_t query,
				ldmsd_cfgobj_export_fn_t export,
				ldmsd_cfgobj_enable_fn_t enable,
				ldmsd_cfgobj_disable_fn_t disable,
				uid_t uid,
				gid_t gid,
				int perm,
				short enabled)
{
	struct rbn *n;
	ldmsd_cfg_lock(type);

	n = rbt_find(cfgobj_trees[type], name);
	if (n) {
		ldmsd_cfg_unlock(type);
		return EEXIST;
	}

	obj->name = strdup(name);
	if (!obj->name)
		goto err0;

	obj->enabled_ev = ev_new(cfgobj_enabled_type);
	if (!obj->enabled_ev)
		goto err1;
	EV_DATA(obj->enabled_ev, struct start_data)->entity = obj;
	obj->disabled_ev = ev_new(cfgobj_disabled_type);
	if (!obj->disabled_ev)
		goto err2;
	EV_DATA(obj->disabled_ev, struct stop_data)->entity = obj;
	obj->perm = perm;
	obj->enabled = enabled;
	obj->update = update;
	obj->delete = delete;
	obj->query = query;
	obj->export = export;
	obj->enable = enable;
	obj->disable = disable;
	obj->type = type;
	obj->ref_count = 1; /* for obj->rbn inserting into the tree */
	if (__del)
		obj->__del = __del;
	else
		obj->__del = ldmsd_cfgobj___del;

	pthread_mutex_init(&obj->lock, NULL);
	rbn_init(&obj->rbn, obj->name);
	rbt_ins(cfgobj_trees[type], &obj->rbn);
	ldmsd_cfg_unlock(type);
	return 0;
err2:
	ev_put(obj->enabled_ev);
err1:
	free(obj->name);
err0:
	ldmsd_cfg_unlock(type);
	return ENOMEM;
}

/**
 * A configuration object with the same name and type must not already
 * exist. On success, the object is returned locked.
 */
ldmsd_cfgobj_t ldmsd_cfgobj_new(const char *name, ldmsd_cfgobj_type_t type,
				size_t obj_size, ldmsd_cfgobj_del_fn_t __del,
				ldmsd_cfgobj_update_fn_t update,
				ldmsd_cfgobj_delete_fn_t delete,
				ldmsd_cfgobj_query_fn_t query,
				ldmsd_cfgobj_export_fn_t export,
				ldmsd_cfgobj_enable_fn_t enable,
				ldmsd_cfgobj_disable_fn_t disable,
				uid_t uid,
				gid_t gid,
				int perm,
				short enabled)
{
	int rc;
	ldmsd_cfgobj_t obj = NULL;

	obj = calloc(1, obj_size);
	if (!obj) {
		errno = ENOMEM;
		return NULL;
	}

	rc = ldmsd_cfgobj_init(obj, name, type, __del,
				update, delete, query, export,
				enable, disable, uid, gid,
				perm, enabled);
	if (rc) {
		errno = rc;
		free(obj);
		obj = NULL;
	}
	return obj;
}

ldmsd_cfgobj_t ldmsd_cfgobj_get(ldmsd_cfgobj_t obj)
{
	if (obj)
		__sync_fetch_and_add(&obj->ref_count, 1);
	return obj;
}

void ldmsd_cfgobj_put(ldmsd_cfgobj_t obj)
{
	if (!obj)
		return;
	if (0 == __sync_sub_and_fetch(&obj->ref_count, 1))
		obj->__del(obj);
}

/** This function is only useful if the cfgobj lock is held when the function is called. */
int ldmsd_cfgobj_refcount(ldmsd_cfgobj_t obj)
{
	return obj->ref_count;
}

/*
 * *** Must be called with `cfgobj_locks[type]` held.
 */
ldmsd_cfgobj_t __cfgobj_find(const char *name, ldmsd_cfgobj_type_t type)
{
	ldmsd_cfgobj_t obj = NULL;
	struct rbn *n = rbt_find(cfgobj_trees[type], name);
	if (!n)
		goto out;
	obj = container_of(n, struct ldmsd_cfgobj, rbn);
out:
	return ldmsd_cfgobj_get(obj);
}

ldmsd_cfgobj_t ldmsd_cfgobj_find(const char *name, ldmsd_cfgobj_type_t type)
{
	ldmsd_cfgobj_t obj;
	pthread_mutex_lock(cfgobj_locks[type]);
	obj = __cfgobj_find(name, type);
	pthread_mutex_unlock(cfgobj_locks[type]);
	return obj;
}

void ldmsd_cfgobj_del(const char *name, ldmsd_cfgobj_type_t type)
{
	ldmsd_cfgobj_t obj;
	pthread_mutex_lock(cfgobj_locks[type]);
	obj = __cfgobj_find(name, type);
	if (obj)
		rbt_del(cfgobj_trees[type], &obj->rbn);
	pthread_mutex_unlock(cfgobj_locks[type]);
}

/**
 * Return the first configuration object of the given type
 *
 * This function must be called with the cfgobj_type lock held
 */
ldmsd_cfgobj_t ldmsd_cfgobj_first(ldmsd_cfgobj_type_t type)
{
	struct rbn *n;
	n = rbt_min(cfgobj_trees[type]);
	if (n)
		return ldmsd_cfgobj_get(container_of(n, struct ldmsd_cfgobj, rbn));
	return NULL;
}

/**
 * Return the next configuration object of the given type
 *
 * This function must be called with the cfgobj_type lock held
 */
ldmsd_cfgobj_t ldmsd_cfgobj_next(ldmsd_cfgobj_t obj)
{
	struct rbn *n;
	ldmsd_cfgobj_t nobj = NULL;

	n = rbn_succ(&obj->rbn);
	if (!n)
		goto out;
	nobj = ldmsd_cfgobj_get(container_of(n, struct ldmsd_cfgobj, rbn));
out:
	ldmsd_cfgobj_put(obj);	/* Drop the next reference */
	return nobj;
}

/*
 * *** Must be called with `cfgobj_locks[type]` held.
 */
ldmsd_cfgobj_t ldmsd_cfgobj_next_re(ldmsd_cfgobj_t obj, regex_t regex)
{
	int rc;
	for ( ; obj; obj = ldmsd_cfgobj_next(obj)) {
		rc = regexec(&regex, obj->name, 0, NULL, 0);
		if (!rc)
			break;
	}
	return obj;
}

/*
 * *** Must be called with `cfgobj_locks[type]` held.
 */
ldmsd_cfgobj_t ldmsd_cfgobj_first_re(ldmsd_cfgobj_type_t type, regex_t regex)
{
	ldmsd_cfgobj_t obj = ldmsd_cfgobj_first(type);
	if (!obj)
		return NULL;
	return ldmsd_cfgobj_next_re(obj, regex);
}

json_entity_t ldmsd_cfgobj_query_result_new(ldmsd_cfgobj_t obj)
{
	char perm_s[16];
	snprintf(perm_s, 16, "%o", obj->perm);
	return json_dict_build(NULL,
			JSON_STRING_VALUE, "schema", ldmsd_cfgobj_types[obj->type],
			JSON_BOOL_VALUE, "enabled", obj->enabled,
			JSON_STRING_VALUE, "perm", perm_s,
			-1);
}

/*
 * The caller must hold the cfgobj tree lock.
 */
json_entity_t ldmsd_cfgobj_delete(ldmsd_cfgobj_t obj)
{
	if (obj->enabled)
		return ldmsd_result_new(EBUSY, 0, NULL);
	rbt_del(cfgobj_trees[obj->type], &obj->rbn);
	ldmsd_cfgobj_put(obj);
	return ldmsd_result_new(0, NULL, NULL);
}

int cfgobj_enabled_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	int rc = 0;
	ldmsd_cfgobj_t obj = (ldmsd_cfgobj_t)EV_DATA(ev, struct start_data)->entity;
	ldmsd_cfgobj_lock(obj);
	if (obj->enable)
		rc = obj->enable(obj);
	else
		rc = ENOSYS;
	ldmsd_cfgobj_unlock(obj);
	return rc;
}

int cfgobj_disabled_actor(ev_worker_t src, ev_worker_t dst, ev_status_t status, ev_t ev)
{
	int rc = 0;
	ldmsd_cfgobj_t obj = (ldmsd_cfgobj_t)EV_DATA(ev, struct start_data)->entity;
	ldmsd_cfgobj_lock(obj);
	if (obj->disable)
		rc = obj->disable(obj);
	ldmsd_cfgobj_unlock(obj);
	return rc;
}
