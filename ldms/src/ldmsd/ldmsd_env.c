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

#include "ldmsd.h"
#include "ldmsd_request.h"

void ldmsd_env___del(ldmsd_cfgobj_t obj)
{
	ldmsd_env_t env = (ldmsd_env_t)obj;
	if (env->name)
		free(env->name);
	if (env->value)
		free(env->value);
	ldmsd_cfgobj___del(obj);
}

static int ldmsd_env_disable(ldmsd_cfgobj_t obj)
{
	/* Do nothing */
	return 0;
}

static int ldmsd_env_enable(ldmsd_cfgobj_t obj)
{
	/* Do nothing */
	return 0;
}

static json_entity_t __add_value(ldmsd_env_t env, json_entity_t result)
{
	return json_dict_build(result, JSON_STRING_VALUE, "value", env->value);
}

json_entity_t ldmsd_env_query(ldmsd_cfgobj_t obj)
{
	json_entity_t query;
	ldmsd_env_t env = (ldmsd_env_t)obj;
	query = ldmsd_cfgobj_query_result_new(obj);
	if (!query)
		return NULL;
	query = __add_value(env, query);
	return ldmsd_result_new(0, NULL, query);
}

json_entity_t ldmsd_env_update(ldmsd_cfgobj_t obj, short enabled,
				json_entity_t dft, json_entity_t spc)
{
	return ldmsd_result_new(EINVAL,
			"Environment variable cannot be updated.", NULL);
}

json_entity_t ldmsd_env_create(const char *name, short enable, json_entity_t dft,
					json_entity_t spc, uid_t uid, gid_t gid)
{
	int rc;
	json_entity_t value;
	char *value_s;
	value = json_value_find(spc, "value");
	value_s = json_value_str(value)->str;
	ldmsd_env_t env = (ldmsd_env_t)ldmsd_cfgobj_new(name, LDMSD_CFGOBJ_ENV,
				sizeof(*env), ldmsd_env___del,
				ldmsd_env_update,
				ldmsd_cfgobj_delete,
				ldmsd_env_query,
				ldmsd_env_query,
				ldmsd_env_enable,
				ldmsd_env_disable,
				uid, gid, 0770, enable);
	if (env) {
		env->name = strdup(name);
		if (!env->name)
			goto oom;
		env->value = strdup(value_s);
		if (!env->value)
			goto oom;
		rc = setenv(name, value_s, 1);
		if (rc) {
			char msg[1024];
			snprintf(msg, 1024, "Failed to export "
				"the environment variable '%s'\n", name);
			ldmsd_log(LDMSD_LERROR, "%s", msg);
			return ldmsd_result_new(rc, msg, NULL);
		}
	}
	return ldmsd_result_new(0, NULL, NULL);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}
