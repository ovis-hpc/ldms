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

ldms_set_t ldmsd_group_new(const char *grp_name)
{
	ldms_set_t grp;
	grp = ldms_set_new(grp_name, grp_schema);
	if (!grp)
		return NULL;
	return grp;
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
	int flags = 0;
	sname = ldms_set_schema_name_get(set);
	if (0 != strcmp(sname, GRP_SCHEMA_NAME))
		return 0; /* not a group */
	flags |= LDMSD_GROUP_IS_GROUP;
	return flags;
}

__attribute__((constructor))
static void __ldmsd_grp_init()
{
	grp_schema = ldms_schema_new(GRP_SCHEMA_NAME);
	assert(grp_schema);
}
