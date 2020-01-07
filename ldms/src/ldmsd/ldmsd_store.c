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

#include "ldmsd_store.h"



int ldmsd_store_open(ldmsd_plugin_inst_t i, ldmsd_strgp_t strgp)
{
	ldmsd_store_type_t store = (void*)i->base;
	return store->open(i, strgp);
}

int ldmsd_store_close(ldmsd_plugin_inst_t i)
{
	ldmsd_store_type_t store = (void*)i->base;
	return store->close(i);
}

int ldmsd_store_store(ldmsd_plugin_inst_t i, ldms_set_t set,
		      ldmsd_strgp_t strgp)
{
	ldmsd_store_type_t store = (void*)i->base;
	return store->store(i, set, strgp);
}

static
const char *store_desc(ldmsd_plugin_inst_t i)
{
	return "Base storage implementation";
}

static
const char *store_help(ldmsd_plugin_inst_t i)
{
	return "N/A";
}

static
int store_init(ldmsd_plugin_inst_t i)
{
	/* do nothing */
	return 0;
}

static
void store_del(ldmsd_plugin_inst_t i)
{
	/* do nothing */
}

static
int store_config(ldmsd_plugin_inst_t i, json_entity_t json,
					char *ebuf, int ebufsz)
{
	ldmsd_store_type_t store = (void*)i->base;
	json_entity_t val;

	val = json_value_find(json, "perm");
	if (!val) {
		store->perm = 0660;
	} else {
		if (val->type != JSON_STRING_VALUE) {
			ldmsd_log(LDMSD_LERROR, "%s: The given 'perm' value is "
					"not a string.\n", i->inst_name);
			return EINVAL;
		}
		errno = 0;
		store->perm = strtol(json_value_str(val)->str, NULL, 8);
		if (errno) {
			snprintf(ebuf, ebufsz, "%s: bad `perm` value: %s\n",
				i->inst_name, json_value_str(val)->str);
			ldmsd_lerror("%s: bad `perm` value: %s\n",
				i->inst_name, json_value_str(val)->str);
			return errno;
		}
	}
	return 0;
}

const char *ldmsd_store_help()
{
	return "\
Parameters:\
  [perm=]     The store plugin instance access permission. The default is 0664.\n\
";
}

json_entity_t ldmsd_store_query(ldmsd_plugin_inst_t inst, const char *q)
{
	ldmsd_store_type_t store = (void*)inst->base;
	json_entity_t result;
	int rc;

	/* call `super` query first -- to handle the common plugin part */
	result = ldmsd_plugin_query(inst, q);
	if (!result) {
		/*
		 * No query result found.
		 */
		goto out;
	}

	if (0 != strcmp(q, "status")) {
		/*
		 * No additional attribute to add to the query result.
		 */
		goto out;
	}

	/* currently, store_query only handle `status` */
	char perm[8];
	sprintf(perm, "%#o", store->perm);
	struct ldmsd_plugin_qjson_attrs bulk[] = {
		{ "perm" , JSON_STRING_VALUE , {.s = perm} },
		{0},
	};
	rc = ldmsd_plugin_qjson_attrs_add(result, bulk);
	if (rc)
		ldmsd_plugin_qjson_err_set(result, ENOMEM, "Out of memory");
out:
	return result;
}
struct ldmsd_store_type_s __store = {
	.base = {
		.version.version = LDMSD_PLUGIN_VERSION,
		.type_name = LDMSD_STORE_TYPENAME,
		.desc = store_desc,
		.help = store_help,
		.init = store_init,
		.del = store_del,
		.config = store_config,
		.query = ldmsd_store_query,
	},
};

void *new()
{
	ldmsd_store_type_t store = malloc(sizeof(*store));
	if (store)
		*store = __store;
	return store;
}
