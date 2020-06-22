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

static void __setgrp_member_list_free(struct ldmsd_str_list *list)
{
	struct ldmsd_str_ent *str;
	str = LIST_FIRST(list);
	while (str) {
		LIST_REMOVE(str, entry);
		free(str->str);
		free(str);
		str = LIST_FIRST(list);
	}
	free(list);
}

void ldmsd_setgrp___del(ldmsd_cfgobj_t obj)
{
	ldmsd_setgrp_t grp = (ldmsd_setgrp_t)obj;
	if (grp->producer)
		free(grp->producer);
	__setgrp_member_list_free(grp->member_list);
	if (grp->grp)
		ldms_grp_delete(grp->grp);
	ldmsd_cfgobj___del(obj);
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
	int rc = 0;
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
	LIST_INSERT_HEAD(grp->member_list, str, entry);

	if (grp->obj.perm & LDMSD_PERM_DSTART)
		goto out;

out:
	ldmsd_setgrp_unlock(grp);
	ldmsd_setgrp_put(grp); /* `find` reference */
	return rc;
}

int __ldmsd_setgrp_rm(ldmsd_setgrp_t grp, const char *instance)
{
	/* caller must hold setgrp_lock */
	int rc;
	struct ldmsd_str_ent *str;

	LIST_FOREACH(str, grp->member_list, entry) {
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
				rc = ldms_grp_rm(grp->grp, instance);
			}
			goto out;
		}
	}
	/* The set member not found */
	rc = ENOENT;
out:
	return rc;
}
int ldmsd_setgrp_rm(const char *name, const char *instance)
{
	int rc;
	ldmsd_setgrp_t grp;

	grp = ldmsd_setgrp_find(name);
	if (!grp)
		return ENOENT;
	ldmsd_setgrp_lock(grp);
	rc = __ldmsd_setgrp_rm(grp, instance);
	ldmsd_setgrp_unlock(grp);
	ldmsd_setgrp_put(grp); /* `find` reference */
	return rc;
}
int ldmsd_group_set_add(ldms_set_t grp, const char *set_name)
{
	int rc = 0;
	char buff[512]; /* should be enough for setname */
	rc = snprintf(buff, sizeof(buff), GRP_KEY_PREFIX "%s", set_name);
	if (rc >= sizeof(buff))
		return ENAMETOOLONG;
	rc = ldms_set_info_set(grp, buff, "-");
	return rc;
}

int ldmsd_group_set_rm(ldms_set_t grp, const char *set_name)
{
	int rc;
	char buff[512]; /* should be enough for setname */
	rc = snprintf(buff, sizeof(buff), GRP_KEY_PREFIX "%s", set_name);
	if (rc >= sizeof(buff))
		return ENAMETOOLONG;
	ldms_set_info_unset(grp, buff);
	return 0;
}

json_entity_t __setgrp_attrs_get(json_entity_t dft, json_entity_t spc,
				char **_producer, struct ldmsd_str_list **_member_list,
				long *_interval_us, long *_offset_us, int *_perm,
				int *_max_members)
{
	json_entity_t producer, members, interval, offset, perm, max_members;
	json_entity_t err = NULL;
	if (spc) {
		producer = json_value_find(spc, "producer");
		interval = json_value_find(spc, "interval");
		offset = json_value_find(spc, "offset");
		members = json_value_find(spc, "members");
		perm = json_value_find(spc, "perm");
		max_members = json_value_find(spc, "max_members");
	}

	if (dft) {
		if (!producer)
			producer = json_value_find(dft, "producer");
		if (!interval)
			interval = json_value_find(dft, "interval");
		if (!offset)
			offset = json_value_find(dft, "offset");
		if (!members)
			members = json_value_find(dft, "members");
		if (!perm)
			perm = json_value_find(dft, "perm");
		if (!max_members)
			max_members = json_value_find(spc, "max_members");
	}

	/* producer */
	if (producer) {
		if (JSON_STRING_VALUE != json_entity_type(producer)) {
			err = json_dict_build(err, JSON_STRING_VALUE,
					"'producer' is not a JSON string.", -1);
			if (!err)
				goto oom;
		} else {
			*_producer = strdup(json_value_str(producer)->str);
			if (*_producer)
				goto oom;
		}
	}

	/* interval */
	*_interval_us = LDMSD_ATTR_NA;
	if (interval) {
		if (JSON_STRING_VALUE == json_entity_type(interval)) {
			*_interval_us = ldmsd_time_str2us(json_value_str(interval)->str);
		} else if (JSON_INT_VALUE == json_entity_type(interval)) {
			*_interval_us = json_value_int(interval);
		} else {
			*_interval_us = LDMSD_ATTR_INVALID;
			err = json_dict_build(err, JSON_STRING_VALUE, "interval",
					"'interval' is neither a string or an integer.", -1);
			if (!err)
				goto oom;
		}
	}

	/* offset */
	*_offset_us = LDMSD_ATTR_NA;
	if (offset) {
		if (JSON_STRING_VALUE == json_entity_type(offset)) {
			char *offset_s = json_value_str(offset)->str;
			if (0 == strcasecmp("none", offset_s)) {
				*_offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
			} else {
				*_offset_us = ldmsd_time_str2us(offset_s);
			}
		} else if (JSON_INT_VALUE == json_entity_type(offset)) {
			*_offset_us = json_value_int(offset);
		} else {
			*_offset_us = LDMSD_ATTR_INVALID;
			err = json_dict_build(err, JSON_STRING_VALUE, "offset",
					"'offset' is neither a string or an integer.", -1);
			if (!err)
				goto oom;
		}
	}

	/* permission */
	if (perm) {
		if (JSON_STRING_VALUE != json_entity_type(perm)) {
			err = json_dict_build(err, JSON_STRING_VALUE,
						"'perm' is not a string.", -1);
			if (!err)
				goto oom;
		}
		*_perm = strtol(json_value_str(perm)->str, NULL, 0);
	} else {
		*_perm = LDMSD_ATTR_NA;
	}

	/* members */
	if (members) {
		*_member_list = malloc(sizeof(*_member_list));
		if (!*_member_list)
			goto oom;
		LIST_INIT(*_member_list);
		struct ldmsd_str_ent *ent;
		json_entity_t member;
		for (member = json_item_first(members); member;
				member = json_item_next(member)) {
			if (JSON_STRING_VALUE != json_entity_type(member)) {
				err = json_dict_build(err, JSON_STRING_VALUE,
						"members",
						"A 'member' is not a string.", -1);
				if (!err)
					goto oom;
			}
			ent = malloc(sizeof(*ent));
			if (!ent)
				goto oom;
			ent->str = strdup(json_value_str(member)->str);
			if (!ent) {
				free(ent);
				goto oom;
			}
			LIST_INSERT_HEAD(*_member_list, ent, entry);
		}
	}

	/* max_members */
	if (max_members) {
		switch (json_entity_type(max_members)) {
		case JSON_STRING_VALUE:
			*_max_members = atoi(json_value_str(max_members)->str);
			break;
		case JSON_INT_VALUE:
			*_max_members = json_value_int(max_members);
			break;
		default:
			break;
		}
	} else {
		*_max_members = 64;
	}

	return err;
oom:
	if (*_producer)
		free(*_producer);
	if (*_member_list)
		__setgrp_member_list_free(*_member_list);
	errno = ENOMEM;
	return NULL;
}

int ldmsd_setgrp_enable(ldmsd_cfgobj_t obj)
{
	int rc;
	ldmsd_str_ent_t str;
	ldmsd_setgrp_t setgrp = (ldmsd_setgrp_t)obj;
	if (!setgrp->grp) {
		setgrp->grp = ldms_grp_new_with_auth(obj->name,
				setgrp->max_members,
				obj->uid, obj->gid, obj->perm);
		if (!setgrp->grp)
			return ENOMEM;
		if (!setgrp->member_list)
			goto skip;
		ldms_transaction_begin((void*)setgrp->grp);
		LIST_FOREACH(str, setgrp->member_list, entry) {
			rc = ldms_grp_ins(setgrp->grp, str->str);
			if (rc)
				goto err;
		}
		ldms_transaction_end((void*)setgrp->grp);
	skip:
		/* no-op */;
	}

	if (setgrp->interval_us > 0) {
		rc = ldmsd_set_update_hint_set(
				(ldms_set_t)setgrp->grp, setgrp->interval_us,
				setgrp->offset_us);
		if (rc)
			goto err;
	}
	rc = ldms_set_publish((ldms_set_t)setgrp->grp);
	if (rc)
		goto err;
	return 0;
err:
	ldms_grp_delete(setgrp->grp);
	setgrp->grp = NULL;
	return rc;
}

int ldmsd_setgrp_disable(ldmsd_cfgobj_t obj)
{
	ldmsd_setgrp_t setgrp = (ldmsd_setgrp_t)obj;
	if (setgrp->grp)
		ldms_set_unpublish((ldms_set_t)setgrp->grp);
	return 0;
}

json_entity_t __setgrp_export_config(ldmsd_setgrp_t setgrp)
{
	json_entity_t export, l, s;
	struct ldmsd_str_ent *ent;

	export = ldmsd_cfgobj_query_result_new(&setgrp->obj);
	if (!export)
		goto oom;
	export = json_dict_build(export,
				JSON_STRING_VALUE, "producer", setgrp->producer,
				JSON_INT_VALUE, "interval", setgrp->interval_us,
				JSON_INT_VALUE, "offset", setgrp->offset_us,
				JSON_LIST_VALUE, "members", -2,
				-1);
	if (!export)
		goto oom;
	if (!LIST_EMPTY(setgrp->member_list)) {
		l = json_value_find(export, "members");
		LIST_FOREACH(ent, setgrp->member_list, entry) {
			s = json_entity_new(JSON_STRING_VALUE, ent->str);
			if (!s)
				goto oom;
			json_item_add(l, s);
		}
	}
	return export;
oom:
	errno = ENOMEM;
	return NULL;
}

json_entity_t ldmsd_setgrp_export(ldmsd_cfgobj_t obj)
{
	json_entity_t export;
	ldmsd_setgrp_t setgrp = (ldmsd_setgrp_t)obj;

	export = __setgrp_export_config(setgrp);
	if (!export)
		goto oom;
	return ldmsd_result_new(0, NULL, export);
oom:
	errno = ENOMEM;
	return NULL;
}

json_entity_t ldmsd_setgrp_update(ldmsd_cfgobj_t obj, short enabled,
				json_entity_t dft, json_entity_t spc)
{
	char *producer;
	struct ldmsd_str_list *member_list;
	long interval_us, offset_us;
	int max_members;
	int perm;
	json_entity_t err;
	ldmsd_setgrp_t setgrp = (ldmsd_setgrp_t)obj;

	err = __setgrp_attrs_get(dft, spc, &producer, &member_list,
				&interval_us, &offset_us, &perm, &max_members);

	if (!err) {
		if (ENOMEM == errno)
			goto oom;
	} else {
		return ldmsd_result_new(EINVAL, NULL, err);
	}

	if (producer) {
		err = json_dict_build(err, JSON_STRING_VALUE, "producer",
				"'producer' cannot be changed.", -1);
		if (!err)
			goto oom;
	}
	if (LDMSD_ATTR_NA != interval_us) {
		err = json_dict_build(err, JSON_STRING_VALUE, "interval",
				"'interval' cannot be changed", -1);
		if (!err)
			goto oom;
	}
	if (LDMSD_ATTR_NA != offset_us) {
		err = json_dict_build(err, JSON_STRING_VALUE, "offset",
				"'offset' cannot be changed", -1);
		if (!err)
			goto oom;
	}

	/* Done checking the given attributes */
	if (member_list) {
		__setgrp_member_list_free(setgrp->member_list);
		setgrp->member_list = member_list;
		if (setgrp->grp) {
			ldms_transaction_begin((void*)setgrp->grp);
			ldmsd_str_ent_t str;
			int rc = 0;
			ldms_grp_rm_all(setgrp->grp);
			LIST_FOREACH(str, setgrp->member_list, entry) {
				rc = ldms_grp_ins(setgrp->grp, str->str);
				if (rc)
					break;
			}
			if (rc) {
				char estr[128];
				snprintf(estr, sizeof(estr),
					 "ldms_grp_ins() error rc: %d", rc);
				err = json_dict_build(err, JSON_STRING_VALUE,
						"members", estr, -1);
				if (!err) {
					ldms_transaction_end((void*)setgrp->grp);
					goto oom;
				}
			}
			ldms_transaction_end((void*)setgrp->grp);
		}
	}

	if (err)
		return ldmsd_result_new(EINVAL, 0, err);

	obj->enabled = (enabled < 0)?obj->enabled:enabled;
	return ldmsd_result_new(0, NULL, NULL);
oom:
	errno = ENOMEM;
	return NULL;
}

json_entity_t ldmsd_setgrp_create(const char *name, short enabled,
				json_entity_t dft, json_entity_t spc,
				uid_t uid, gid_t gid)
{
	json_entity_t err;
	char *producer = NULL;
	struct ldmsd_str_list *member_list = NULL;
	long interval_us, offset_us;
	int perm;
	int max_members;
	ldmsd_setgrp_t setgrp;

	err = __setgrp_attrs_get(dft, spc, &producer, &member_list,
					&interval_us, &offset_us, &perm,
					&max_members);
	if (!err) {
		if (ENOMEM == errno)
			goto oom;
	} else {
		return ldmsd_result_new(EINVAL, NULL, err);
	}

	if (!producer) {
		producer = strdup(ldmsd_myname_get());
		if (!producer)
			goto oom;
	}

	if (LDMSD_ATTR_NA == interval_us) {
		interval_us = 0;
		if (LDMSD_ATTR_NA != offset_us) {
			err = json_dict_build(err, JSON_STRING_VALUE, "offset",
					"No interval is given, so the offset is ignored.",
					-1);
			if (!err)
				goto oom;
		}
	} else {
		if (LDMSD_ATTR_NA == offset_us)
			offset_us = LDMSD_UPDT_HINT_OFFSET_NONE;
	}

	if (LDMSD_ATTR_NA == perm)
		perm = 0777;

	if (err)
		return ldmsd_result_new(EINVAL, 0, err);

	/* All attributes are valid, create the cfgobj */

	setgrp = (ldmsd_setgrp_t)ldmsd_cfgobj_new(name, LDMSD_CFGOBJ_SETGRP,
						sizeof(*setgrp),
						ldmsd_setgrp___del,
						ldmsd_setgrp_update,
						ldmsd_cfgobj_delete,
						ldmsd_setgrp_export,
						ldmsd_setgrp_export,
						ldmsd_setgrp_enable,
						ldmsd_setgrp_disable,
						uid, gid, perm, enabled);
	if (!setgrp)
		goto oom;
	setgrp->producer = producer;
	setgrp->member_list = member_list;
	setgrp->interval_us = interval_us;
	setgrp->offset_us = offset_us;
	setgrp->max_members = max_members;
	return ldmsd_result_new(0, NULL, NULL);
oom:
	if (producer)
		free(producer);
	if (member_list)
		__setgrp_member_list_free(member_list);
	errno = ENOMEM;
	return NULL;
}

__attribute__((constructor))
static void __ldmsd_setgrp_init()
{
	grp_schema = ldms_schema_new(GRP_SCHEMA_NAME);
	assert(grp_schema);
}
