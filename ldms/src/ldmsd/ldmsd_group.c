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
#include "ldmsd.h"

ldms_schema_t grp_schema;

#define GRP_SCHEMA_NAME "ldmsd_grp_schema"
#define GRP_KEY_PREFIX "    grp_member: "
#define GRP_GN_NAME "ldmsd_grp_gn"

void ldmsd_setgrp___del(ldmsd_cfgobj_t obj)
{
	ldmsd_str_ent_t str;
	ldmsd_setgrp_t grp = (ldmsd_setgrp_t)obj;
	if (grp->producer)
		free(grp->producer);
	str = LIST_FIRST(&grp->member_list);
	while (str) {
		LIST_REMOVE(str, entry);
		free(str->str);
		free(str);
		str = LIST_FIRST(&grp->member_list);
	}
	if (grp->set)
		ldms_set_delete(grp->set);
	ldmsd_cfgobj___del(obj);
}

ldms_set_t __setgrp_start(const char *name, uid_t uid, gid_t gid, mode_t perm,
			const char *producer, long interval_us, long offset_us)
{
	int rc;
	ldms_set_t set;
	set = ldms_set_new_with_auth(name, grp_schema, uid, gid, perm);
	if (!set) {
		errno = ENOMEM;
		return NULL;
	}

	if (producer) {
		rc = ldms_set_producer_name_set(set, producer);
		if (rc)
			goto err;
	}
	if (interval_us > 0) {
		rc = ldmsd_set_update_hint_set(set, interval_us, offset_us);
		if (rc)
			goto err;
	}
	ldms_transaction_begin(set);
	ldms_metric_set_u32(set, 0, 0);
	ldms_set_info_set(set, GRP_GN_NAME, "0");
	ldms_transaction_end(set);
	rc = ldms_set_publish(set);
	if (rc)
		goto err;
	return set;
err:
	errno = rc;
	ldms_set_delete(set);
	return NULL;
}

/* Caller must hold the setgroup lock */
int __ldmsd_setgrp_start(ldmsd_setgrp_t grp)
{
	int rc;
	ldmsd_str_ent_t str;
	grp->set = __setgrp_start(grp->obj.name, grp->obj.uid, grp->obj.gid,
					grp->obj.perm, grp->producer,
					grp->interval_us, grp->offset_us);
	if (!grp->set) {
		rc = errno;
		return rc;
	}
	LIST_FOREACH(str, &grp->member_list, entry) {
		rc = ldmsd_group_set_add(grp->set, str->str);
		if (rc)
			return rc;
	}
	grp->obj.perm &= ~LDMSD_PERM_DSTART;
	return 0;
}

ldms_set_t
ldmsd_group_new_with_auth(const char *name, uid_t uid, gid_t gid, mode_t perm)
{
	return __setgrp_start(name, uid, gid, perm, NULL, 0, 0);
}

ldms_set_t ldmsd_group_new(const char *name)
{
	struct ldmsd_sec_ctxt sctxt;
	ldmsd_sec_ctxt_get(&sctxt);
	return ldmsd_group_new_with_auth(name, sctxt.crd.uid, sctxt.crd.gid, 0777);
}

ldmsd_setgrp_t
ldmsd_setgrp_new_with_auth(const char *name, const char *producer,
				long interval_us, long offset_us,
				uid_t uid, gid_t gid, mode_t perm, int flags)
{
	int rc;
	ldmsd_setgrp_t grp;

	rc = ENOMEM;
	grp = (ldmsd_setgrp_t)ldmsd_cfgobj_new_with_auth(name,
			LDMSD_CFGOBJ_SETGRP, sizeof(*grp),
			ldmsd_setgrp___del, uid, gid, perm);
	if (!grp)
		return NULL;

	if (!producer)
		producer = ldmsd_myname_get();
	grp->producer = strdup(producer);
	if (!grp->producer)
		goto err1;
	grp->interval_us = interval_us;
	grp->offset_us = offset_us;
	LIST_INIT(&grp->member_list);
	if (flags & LDMSD_PERM_DSTART) {
		grp->obj.perm |= LDMSD_PERM_DSTART;
	} else {
		rc = __ldmsd_setgrp_start(grp);
		if (rc)
			goto err1;
	}

	ldmsd_setgrp_unlock(grp);
	return grp;
err1:
	ldmsd_setgrp_unlock(grp);
err:
	ldmsd_setgrp_put(grp);
	return NULL;
}

extern struct rbt *cfgobj_trees[];
ldmsd_cfgobj_t __cfgobj_find(const char *name, ldmsd_cfgobj_type_t type);

int ldmsd_setgrp_del(const char *name, ldmsd_sec_ctxt_t ctxt)
{
	int rc;
	ldmsd_setgrp_t grp;

	ldmsd_cfg_lock(LDMSD_CFGOBJ_SETGRP);
	grp = (ldmsd_setgrp_t) __cfgobj_find(name, LDMSD_CFGOBJ_SETGRP);
	if (!grp) {
		rc = ENOENT;
		goto out;
	}
	ldmsd_setgrp_lock(grp);
	rc = ldmsd_cfgobj_access_check(&grp->obj, 0222, ctxt);
	if (rc)
		goto out1;
	rbt_del(cfgobj_trees[LDMSD_CFGOBJ_SETGRP], &grp->obj.rbn);
	ldmsd_setgrp_put(grp); /* put down reference from the tree */
	rc = 0;
out1:
	ldmsd_setgrp_unlock(grp);
	ldmsd_setgrp_put(grp); /* `find` reference */
out:
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_SETGRP);
	return rc;
}

int ldmsd_setgrp_ins(const char *name, const char *instance)
{
	int rc;
	ldmsd_setgrp_t grp;
	struct ldmsd_str_ent *str;

	grp = ldmsd_setgrp_find(name);
	if (!grp)
		return ENOENT;

	ldmsd_setgrp_lock(grp);
	/* add a member */
	str = malloc(sizeof(*str));
	if (!str) {
		rc = ENOMEM;
		goto out;
	}

	str->str = strdup(instance);
	if (!str->str) {
		rc = ENOMEM;
		free(str);
		goto out;
	}
	LIST_INSERT_HEAD(&grp->member_list, str, entry);

	if (grp->obj.perm & LDMSD_PERM_DSTART)
		goto out;

	/*
	 * Since \c grp has been started, \c grp->set must be created already.
	 */
	rc = ldmsd_group_set_add(grp->set, instance);

out:
	ldmsd_setgrp_unlock(grp);
	ldmsd_setgrp_put(grp); /* `find` reference */
	return rc;
}

int ldmsd_setgrp_rm(const char *name, const char *instance)
{
	int rc;
	ldmsd_setgrp_t grp;
	struct ldmsd_str_ent *str;

	grp = ldmsd_setgrp_find(name);
	if (!grp)
		return ENOENT;

	ldmsd_setgrp_lock(grp);
	LIST_FOREACH(str, &grp->member_list, entry) {
		if (0 == strcmp(str->str, instance)) {
			LIST_REMOVE(str, entry);
			free(str->str);
			free(str);
			if (grp->obj.perm & LDMSD_PERM_DSTART) {
				/*
				 * The setgrp has never been started.
				 *
				 * Nothing to be done.
				 */
				rc = 0;
			} else {
				rc = ldmsd_group_set_rm(grp->set, instance);
			}
			goto out;
		}
	}
	/* The set member not found */
	rc = ENOENT;
out:
	ldmsd_setgrp_unlock(grp);
	ldmsd_setgrp_put(grp); /* `find` reference */
	return rc;
}

static
int __update_group_gn(ldms_set_t grp)
{
	uint32_t gn;
	char buff[32];
	gn = ldms_metric_get_u32(grp, 0);
	gn += 1;
	ldms_metric_set_u32(grp, 0, gn);
	sprintf(buff, "%u", gn);
	return ldms_set_info_set(grp, GRP_GN_NAME, buff);
}

int ldmsd_group_set_add(ldms_set_t grp, const char *set_name)
{
	int rc = 0;
	uint32_t gn;
	char buff[512]; /* should be enough for setname */
	rc = snprintf(buff, sizeof(buff), GRP_KEY_PREFIX "%s", set_name);
	if (rc >= sizeof(buff))
		return ENAMETOOLONG;
	ldms_transaction_begin(grp);
	rc = ldms_set_info_set(grp, buff, "-");
	if (rc)
		goto out;
	rc = __update_group_gn(grp);
out:
	ldms_transaction_end(grp);
	return rc;
}

int ldmsd_group_set_rm(ldms_set_t grp, const char *set_name)
{
	int rc;
	uint32_t gn;
	char buff[512]; /* should be enough for setname */
	rc = snprintf(buff, sizeof(buff), GRP_KEY_PREFIX "%s", set_name);
	if (rc >= sizeof(buff))
		return ENAMETOOLONG;
	ldms_transaction_begin(grp);
	ldms_set_info_unset(grp, buff);
	rc = __update_group_gn(grp);
	ldms_transaction_end(grp);
	return rc;
}

const char *ldmsd_group_member_name(const char *info_key)
{
	if (0 != strncmp(GRP_KEY_PREFIX, info_key, sizeof(GRP_KEY_PREFIX)-1))
		return NULL;
	return info_key + sizeof(GRP_KEY_PREFIX) - 1;
}

struct __grp_traverse_ctxt {
	ldms_set_t grp;
	ldmsd_group_iter_cb_t cb;
	void *arg;
};

static int
__grp_traverse(const char *key, const char *value, void *arg)
{
	const char *name;
	struct __grp_traverse_ctxt *ctxt = arg;
	name = ldmsd_group_member_name(key);
	if (!name)
		return 0; /* continue */
	return ctxt->cb(ctxt->grp, name, ctxt->arg);
}

int ldmsd_group_iter(ldms_set_t grp, ldmsd_group_iter_cb_t cb, void *arg)
{
	int rc;
	struct __grp_traverse_ctxt ctxt = {grp, cb, arg};
	rc = ldms_set_info_traverse(grp, __grp_traverse, LDMS_SET_INFO_F_LOCAL,
				    &ctxt);
	if (rc)
		return rc;
	rc = ldms_set_info_traverse(grp, __grp_traverse, LDMS_SET_INFO_F_REMOTE,
				    &ctxt);
	return rc;
}

int ldmsd_group_check(ldms_set_t set)
{
	const char *sname;
	char *s_gn;
	uint32_t info_gn;
	uint32_t set_gn;
	int flags = 0;
	sname = ldms_set_schema_name_get(set);
	if (0 != strcmp(sname, GRP_SCHEMA_NAME))
		return 0; /* not a group */
	flags |= LDMSD_GROUP_IS_GROUP;
	s_gn = ldms_set_info_get(set, GRP_GN_NAME);
	if (!s_gn)
		return LDMSD_GROUP_ERROR;
	info_gn = atoi(s_gn);
	free(s_gn);
	set_gn = ldms_metric_get_u32(set, 0);
	if (info_gn != set_gn)
		flags |= LDMSD_GROUP_MODIFIED;
	return flags;
}

__attribute__((constructor))
static void __ldmsd_setgrp_init()
{
	int rc;
	grp_schema = ldms_schema_new(GRP_SCHEMA_NAME);
	assert(grp_schema);

	rc = ldms_schema_metric_add(grp_schema, GRP_GN_NAME, LDMS_V_U32, "");
	assert(rc == 0);
}
