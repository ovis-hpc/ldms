/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015,2018,2023 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015,2018,2023 Open Grid Computing, Inc. All rights reserved.
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
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <coll/rbt.h>
#include "ldmsd.h"

static int __cfgobj_ref_debug = 0;
static ovis_log_t __cfgobj_log;

#define __CFGOBJ_REF_DEBUG(FMT, ...) do {\
		if (__cfgobj_ref_debug) \
			ovis_log(__cfgobj_log, OVIS_LDEBUG, FMT, ##__VA_ARGS__); \
	} while (0)


__attribute__((constructor))
static void __cfgobj_once()
{
	const char *v;
	v = getenv("LDMSD_CFGOBJ_REF_DEBUG");
	if (!v)
		return;
	__cfgobj_ref_debug = atoi(v);
	if (!__cfgobj_ref_debug)
		return;
	__cfgobj_log = ovis_log_register("ldmsd_cfgobj_ref_debug", "cfgobj reference debug log");
	ovis_log_set_level(__cfgobj_log, OVIS_LDEBUG);
}

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

struct rbt listen_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t listen_tree_lock = PTHREAD_MUTEX_INITIALIZER;

struct rbt auth_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t auth_tree_lock = PTHREAD_MUTEX_INITIALIZER;

struct rbt plugin_tree = RBT_INITIALIZER(cfgobj_cmp);
pthread_mutex_t plugin_tree_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t *cfgobj_locks[] = {
	[LDMSD_CFGOBJ_PRDCR] = &prdcr_tree_lock,
	[LDMSD_CFGOBJ_UPDTR] = &updtr_tree_lock,
	[LDMSD_CFGOBJ_STRGP] = &strgp_tree_lock,
	[LDMSD_CFGOBJ_LISTEN] = &listen_tree_lock,
	[LDMSD_CFGOBJ_AUTH]   = &auth_tree_lock,
	[LDMSD_CFGOBJ_PLUGIN] = &plugin_tree_lock,
};

struct rbt *cfgobj_trees[] = {
	[LDMSD_CFGOBJ_PRDCR] = &prdcr_tree,
	[LDMSD_CFGOBJ_UPDTR] = &updtr_tree,
	[LDMSD_CFGOBJ_STRGP] = &strgp_tree,
	[LDMSD_CFGOBJ_LISTEN] = &listen_tree,
	[LDMSD_CFGOBJ_AUTH]   = &auth_tree,
	[LDMSD_CFGOBJ_PLUGIN] = &plugin_tree,
};

void ldmsd_cfgobj___del(ldmsd_cfgobj_t obj)
{
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

/*
 * Add the obj to the corresponding CFGOBJ tree.
 *
 * The caller must hold appropriate CFGOBJ lock.
 * This function does not check existing entry.
 */
void __cfgobj_add(ldmsd_cfgobj_t obj)
{
	rbn_init(&obj->rbn, obj->name);
	rbt_ins(cfgobj_trees[obj->type], &obj->rbn);
	ldmsd_cfgobj_get(obj, "cfgobj_tree"); /* put in `rm` */
}

/*
 * Remove the obj from the corresponding CFGOBJ tree.
 *
 * The caller must hold appropriate CFGOBJ lock.
 */
void __cfgobj_rm(ldmsd_cfgobj_t obj)
{
	rbt_del(cfgobj_trees[obj->type], &obj->rbn);
	ldmsd_cfgobj_put(obj, "cfgobj_tree"); /* from `add` */
}

int ldmsd_cfgobj_add(ldmsd_cfgobj_t obj)
{
	int rc = EEXIST;
	struct rbn *n;
	if (obj->type < LDMSD_CFGOBJ_FIRST || LDMSD_CFGOBJ_LAST < obj->type)
		return EINVAL;
	pthread_mutex_lock(cfgobj_locks[obj->type]);
	n = rbt_find(cfgobj_trees[obj->type], obj->name);
	if (n)
		goto out;
	rc = 0;
	__cfgobj_add(obj);
 out:
	pthread_mutex_unlock(cfgobj_locks[obj->type]);
	return rc;
}

void ldmsd_cfgobj_rm(ldmsd_cfgobj_t obj)
{
	pthread_mutex_lock(cfgobj_locks[obj->type]);
	rbt_del(cfgobj_trees[obj->type], &obj->rbn);
	pthread_mutex_unlock(cfgobj_locks[obj->type]);
	ldmsd_cfgobj_put(obj, "cfgobj_tree"); /* from `add` */
}

/* an interposer to call obj->__del() */
static void __cfgobj_ref_free(void *arg)
{
	ldmsd_cfgobj_t obj = arg;
	obj->__del(obj);
	__CFGOBJ_REF_DEBUG("cfgobj ref_free: %p\n", obj);
}

void ldmsd_cfgobj_plugin_cleanup(struct ldmsd_cfgobj *obj)
{
	struct ldmsd_plugin *pi = container_of(obj, struct ldmsd_plugin, cfgobj);
	struct avl_q_item *avl;
	struct avl_q_item *kwl;

	free(pi->cfgobj.name);

	free(pi->libpath);
	pi->libpath = NULL;

	while ((avl = TAILQ_FIRST(&pi->avl_q))) {
		TAILQ_REMOVE(&pi->avl_q, avl, entry);
		free(avl->av_list);
		free(avl);
	}

	while ((kwl = TAILQ_FIRST(&pi->kwl_q))) {
		TAILQ_REMOVE(&pi->kwl_q, kwl, entry);
		free(kwl->av_list);
		free(kwl);
	}
}

static const char *__cfgobj_type_str[] = {
	[LDMSD_CFGOBJ_PRDCR]  = "prdcr",
	[LDMSD_CFGOBJ_UPDTR]  = "updtr",
	[LDMSD_CFGOBJ_STRGP]  = "strgp",
	[LDMSD_CFGOBJ_LISTEN] = "listen",
	[LDMSD_CFGOBJ_AUTH]   = "auth",
	[LDMSD_CFGOBJ_PLUGIN] = "plugin",
};

const char *ldmsd_cfgobj_type_str(ldmsd_cfgobj_type_t t)
{
	if (t < LDMSD_CFGOBJ_FIRST || LDMSD_CFGOBJ_LAST < t)
		return "UNKNOWN";
	return __cfgobj_type_str[t];
}

static int __cfgobj_init(ldmsd_cfgobj_t obj, const char *name,
			 ldmsd_cfgobj_type_t type, ldmsd_cfgobj_del_fn_t __del,
			 uid_t uid, gid_t gid, int perm)
{
	obj->name = strdup(name);
	if (!obj->name)
		return ENOMEM;

	obj->gid = gid;
	obj->uid = uid;
	obj->perm = perm;

	pthread_mutex_init(&obj->lock, NULL);
	ref_init(&obj->ref, "init", __cfgobj_ref_free, obj);
	obj->type = type;

	obj->__del = __del;

	__CFGOBJ_REF_DEBUG("cfgobj ref_init: %p %s (%s)\n",
			   obj, ldmsd_cfgobj_type_str(type), name);

	return 0;
}

/*
 * For old-style plugin that does not initalize cfgobj
 */
int ldmsd_plugin_cfgobj_init(struct ldmsd_plugin *pi, const char *inst_name)
{
	return __cfgobj_init(&pi->cfgobj, inst_name, LDMSD_CFGOBJ_PLUGIN,
			ldmsd_cfgobj_plugin_cleanup, getegid(), geteuid(), 0770);
}

ldmsd_cfgobj_t ldmsd_cfgobj_new_with_auth(const char *name,
					  ldmsd_cfgobj_type_t type,
					  size_t obj_size,
					  ldmsd_cfgobj_del_fn_t __del,
					  uid_t uid,
					  gid_t gid,
					  int perm)
{
	ldmsd_cfgobj_t obj = NULL;
	int rc;
	struct rbn *n;

	pthread_mutex_lock(cfgobj_locks[type]);

	n = rbt_find(cfgobj_trees[type], name);
	if (n) {
		errno = EEXIST;
		goto out_1;
	}

	obj = calloc(1, obj_size);
	if (!obj)
		goto out_1;
	if (!__del)
		__del = ldmsd_cfgobj___del;
	rc = __cfgobj_init(obj, name, type, __del, uid, gid, perm);
	if (rc) {
		errno = rc;
		goto out_2;
	}
	__cfgobj_add(obj);
	pthread_mutex_lock(&obj->lock);

	goto out_1;

out_2:
	free(obj);
	obj = NULL;

out_1:
	pthread_mutex_unlock(cfgobj_locks[type]);
	return obj;
}

/**
 * Allocate a configuration object of the requested size. A
 * configuration object with the same name and type must not already
 * exist. On success, the object is returned locked.
 */
ldmsd_cfgobj_t ldmsd_cfgobj_new(const char *name, ldmsd_cfgobj_type_t type,
				size_t obj_size, ldmsd_cfgobj_del_fn_t __del)
{
	return ldmsd_cfgobj_new_with_auth(name, type, obj_size, __del,
					  getuid(), getgid(), 0777);
}

/** This function is only useful if the cfgobj lock is held when the function is called. */
int ldmsd_cfgobj_refcount(ldmsd_cfgobj_t obj)
{
	return obj->ref.ref_count;
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
	return ldmsd_cfgobj_get(obj, "find");
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
	if (obj) {
		rbt_del(cfgobj_trees[type], &obj->rbn);
		ldmsd_cfgobj_put(obj, "cfgobj_tree");
	}
	pthread_mutex_unlock(cfgobj_locks[type]);
	if (obj) {
		ldmsd_cfgobj_put(obj, "find");
		ldmsd_cfgobj_put(obj, "init");
	}
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
		return ldmsd_cfgobj_get(container_of(n, struct ldmsd_cfgobj, rbn), "iter");
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
	nobj = ldmsd_cfgobj_get(container_of(n, struct ldmsd_cfgobj, rbn), "iter");
out:
	ldmsd_cfgobj_put(obj, "iter");	/* Drop the next reference */
	return nobj;
}
