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

#include <pthread.h>
#include <dlfcn.h>
#include <stdlib.h>

#include "coll/rbt.h"
#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "ldmsd_request.h"
#include "config.h"

#define LDMSD_PLUGIN_LIBPATH_MAX	1024

struct ldmsd_plugin_version_s plugin_ver = {.major = 1, .minor = 0, .patch = 0};

static void *dl_new(const char *dl_name, char *errstr, int errlen,
		    char **path_out)
{
	char library_name[LDMSD_PLUGIN_LIBPATH_MAX];
	char library_path[LDMSD_PLUGIN_LIBPATH_MAX];

	char *pathdir = library_path;
	char *libpath;
	char *saveptr = NULL;
	char *path = getenv("LDMSD_PLUGIN_LIBPATH");
	void *d = NULL;
	void *dl_obj = NULL;
	char *dlerr = NULL;

	if (!path)
		path = LDMSD_PLUGIN_LIBPATH_DEFAULT;

	strncpy(library_path, path, sizeof(library_path) - 1);

	while ((libpath = strtok_r(pathdir, ":", &saveptr)) != NULL) {
		ldmsd_log(LDMSD_LDEBUG, "Checking for %s in %s\n",
			  dl_name, libpath);
		pathdir = NULL;
		snprintf(library_name, sizeof(library_name), "%s/lib%s.so",
			libpath, dl_name);
		d = dlopen(library_name, RTLD_NOW);
		if (d != NULL) {
			break;
		}
		struct stat buf;
		if (stat(library_name, &buf) == 0) {
			dlerr = dlerror();
			ldmsd_log(LDMSD_LERROR, "Bad plugin "
				"'%s': dlerror %s\n", dl_name, dlerr);
			if (errstr)
				snprintf(errstr, errlen, "Bad plugin"
					 " '%s'. dlerror %s", dl_name, dlerr);
			errno = ELIBBAD;
			goto err;
		}
	}

	if (!d) {
		dlerr = dlerror();
		ldmsd_log(LDMSD_LERROR, "Failed to load the plugin '%s': "
				"dlerror %s\n", dl_name, dlerr);
		if (errstr)
			snprintf(errstr, errlen, "Failed to load the plugin "
				 "'%s'. dlerror %s", dl_name, dlerr);
		errno = ELIBBAD;
		goto err;
	}

	ldmsd_plugin_new_fn_t new = dlsym(d, "new");
	if (!new) {
		if (errstr)
			snprintf(errstr, errlen, "The library, '%s', is "
				 "missing the new() function.", dl_name);
		goto err;
	}

	if (path_out) {
		*path_out = strdup(library_name);
		if (!(*path_out))
			goto err;
	}
	dl_obj = new();
	return dl_obj;
 err:
	return NULL;
}

static
ldmsd_plugin_inst_t plugin_inst_new(const char *plugin_name, char *errstr,
					  int errlen)
{
	char *libpath = NULL;
	ldmsd_plugin_inst_t inst = NULL;
	ldmsd_plugin_type_t base = NULL;

	inst = dl_new(plugin_name, errstr, errlen, &libpath);
	if (!inst)
		goto err0;
	inst->libpath = libpath;
	if (0 == strcmp(plugin_name, inst->type_name)) {
		/* This is a base-class -- not a plugin implementation. */
		errno = EINVAL;
		goto err0;
	}
	base = dl_new(inst->type_name, errstr, errlen, NULL);
	if (!base)
		goto err0;

	/* bind base & instance */
	inst->base = base;
	base->inst = inst;

	/* check base-facility compatibility */
	if (base->version.major != plugin_ver.major) {
		/* Major version incompatible. */
		if (errstr)
			snprintf(errstr, errlen,
				 "Mismatch plugin type major version: "
				 "%hhd != %hhd",
				 base->version.major, plugin_ver.major);
		errno = ENOTSUP;
		goto err0;
	}
	if (base->version.minor > plugin_ver.minor) {
		/* New plugin in old facility. */
		if (errstr)
			snprintf(errstr, errlen,
				 "Plugin version (%hhd.%hhd) > facility "
				 "version (%hhd.%hhd)",
				 base->version.major, base->version.minor,
				 plugin_ver.major, plugin_ver.minor);
		errno = ENOTSUP;
		goto err0;
	}
	/* check inst-base compatibility */
	if (inst->version.major != base->version.major) {
		/* Major version incompatible. */
		if (errstr)
			snprintf(errstr, errlen,
				 "Mismatch instance major version: "
				 "%hhd != %hhd",
				 inst->version.major, base->version.major);
		errno = ENOTSUP;
		goto err0;
	}
	if (inst->version.minor > base->version.minor) {
		/* New instance in old base facility. */
		if (errstr)
			snprintf(errstr, errlen,
				 "Instance version (%hhd.%hhd) > plugin "
				 "version (%hhd.%hhd)",
				 inst->version.major, inst->version.minor,
				 base->version.major, base->version.minor);
		errno = ENOTSUP;
		goto err0;
	}

	return inst;

 err0:
	if (base) {
		free(base);
		base = NULL;
	}
	if (inst) {
		free(inst);
		inst = NULL;
	}
	return NULL;
}

ldmsd_plugin_inst_t ldmsd_plugin_inst_load(const char *inst_name,
					   const char *plugin_name,
					   char *errstr, int errlen)
{
	ldmsd_plugin_inst_t inst;
	int rc;

	inst = ldmsd_plugin_inst_find(inst_name);
	if (inst) {
		errno = EEXIST;
		if (errstr)
			snprintf(errstr, errlen, "Plugin `%s` has already "
				 "loaded", inst_name);
		goto out;
	}

	inst = plugin_inst_new(plugin_name, errstr, errlen);
	if (!inst)
		goto out;

	inst->inst_name = strdup(inst_name);
	if (!inst->inst_name)
		goto err1;

	/*
	 * Create the JSON dict entity
	 * {
	 *  "cfg": [],
	 *  "status": {...}
	 * }
	 */
	inst->cfg = json_entity_new(JSON_LIST_VALUE);
	if (!inst->cfg)
		goto err2;
	/* base init */
	rc = inst->base->init(inst);
	if (rc)
		goto err3;

	/* instance init */
	if (inst->init) {
		rc = inst->init(inst);
		if (rc)
			goto err4;
	}

	goto out;

 err4:
	inst->base->del(inst); /* because inst->base->init() succeeded */
 err3:
	json_entity_free(inst->cfg);
 err2:
	free(inst->inst_name);
 err1:
	free(inst->base);
	free(inst);
	inst = NULL;
 out:
	return inst;
}

static
void ldmsd_plugin_inst_free(ldmsd_plugin_inst_t inst)
{
	if (inst->inst_name)
		free(inst->inst_name);
	if (inst->libpath)
		free(inst->libpath);
	free(inst->base);
	free(inst);
}

static int plugin_inst_config_add(ldmsd_plugin_inst_t inst,
					json_entity_t cfg)
{
	json_entity_t item, d;
	if (JSON_DICT_VALUE == json_entity_type(cfg)) {
		d = json_entity_copy(cfg);
		if (!d)
			return ENOMEM;
		json_item_add(inst->cfg, d);
	} else if (JSON_LIST_VALUE == json_entity_type(cfg)) {
		for (item = json_item_first(cfg); item; item = json_item_next(item)) {
			d = json_entity_copy(item);
			if (!d)
				return ENOMEM;
			json_item_add(inst->cfg, d);
		}
	} else {
		return EINVAL;
	}
	return 0;
}

int ldmsd_plugin_inst_config(ldmsd_plugin_inst_t inst,
			     json_entity_t d,
			     char *ebuf, int ebufsz)
{
	if (inst->config)
		return inst->config(inst, d, ebuf, ebufsz);
	return inst->base->config(inst, d, ebuf, ebufsz);
}

const char *ldmsd_plugin_inst_help(ldmsd_plugin_inst_t inst)
{
	if (inst->help) {
		return inst->help(inst);
	} else {
		return inst->base->help(inst);
	}
}


const char *ldmsd_plugin_inst_desc(ldmsd_plugin_inst_t inst)
{
	if (inst->desc) {
		return inst->desc(inst);
	}
	return NULL;
}

json_entity_t ldmsd_plugin_inst_query_env_add(json_entity_t result, const char *env_name)
{
	char *s = getenv(env_name);
	if (!s)
		s = "";
	result = json_dict_build(result,
			JSON_STRING_VALUE, env_name, s,
			-1);
	return result;
}

typedef struct qtbl_ent_s {
	const char *key;
	json_entity_t (*query_fn)(ldmsd_plugin_inst_t pi);
} *qtbl_ent_t;
#define QTBL_ENT(x) ((qtbl_ent_t)(x))

static json_entity_t query_config(ldmsd_plugin_inst_t pi);
static json_entity_t query_status(ldmsd_plugin_inst_t pi);
static json_entity_t query_env(ldmsd_plugin_inst_t pi);

struct qtbl_ent_s qtbl[] = {
		{ "config" , query_config },
		{ "env"    , query_env    },
		{ "status" , query_status },
};

int qtbl_ent_cmp(const void *k0, const void *k1)
{
	return strcmp(QTBL_ENT(k0)->key, QTBL_ENT(k1)->key);
}

json_entity_t ldmsd_plugin_inst_query(ldmsd_plugin_inst_t pi, const char *q)
{
	struct qtbl_ent_s *ent, key = {.key = q};

	ent = bsearch(&key, qtbl, sizeof(qtbl)/sizeof(*qtbl), sizeof(*qtbl),
		      qtbl_ent_cmp);
	if (!ent) {
		errno = EINVAL;
		return NULL;
	}
	return ent->query_fn(pi);
}

static json_entity_t query_config(ldmsd_plugin_inst_t pi)
{
	json_entity_t result;
	/* Copy out the cfg json so that the caller could clean up the result. */
	result = json_entity_copy(pi->cfg);
	if (!result) {
		errno = ENOMEM;
		return NULL;
	}
	return result;
}

static json_entity_t query_status(ldmsd_plugin_inst_t pi)
{
	json_entity_t result;
	result = json_dict_build(NULL,
				JSON_STRING_VALUE, "libpath", pi->libpath,
				-1);
	if (!result) {
		errno = ENOMEM;
		return NULL;
	}
	return result;
}

static json_entity_t query_env(ldmsd_plugin_inst_t pi)
{
	json_entity_t result = json_entity_new(JSON_DICT_VALUE);
	if (!result) {
		errno = ENOMEM;
		return NULL;
	}
	/* No environment variables to add */
	return result;
}

struct ldmsd_deferred_pi_config_q deferred_pi_config_q;
struct ldmsd_deferred_pi_config *ldmsd_deferred_pi_config_first()
{
	return TAILQ_FIRST(&deferred_pi_config_q);
}

struct ldmsd_deferred_pi_config *
ldmsd_deffered_pi_config_next(struct ldmsd_deferred_pi_config *cfg)
{
	return TAILQ_NEXT(cfg, entry);
}

void ldmsd_deferred_pi_config_free(ldmsd_deferred_pi_config_t cfg)
{
	TAILQ_REMOVE(&deferred_pi_config_q, cfg, entry);
	if (cfg->config)
		json_entity_free(cfg->config);
	if (cfg->name)
		free(cfg->name);
	if (cfg->buf)
		free(cfg->buf);
	if (cfg->config_file)
		free(cfg->config_file);
	free(cfg);
}

ldmsd_deferred_pi_config_t
ldmsd_deferred_pi_config_new(const char *name, json_entity_t d,
			uint32_t msg_no, const char *config_file)
{
	struct ldmsd_deferred_pi_config *cfg = calloc(1, sizeof(*cfg));
	errno = 0;
	if (!cfg)
		goto err;
	cfg->buflen = 1024;
	cfg->buf = malloc(cfg->buflen);
	if (!cfg->buf)
		goto err1;
	cfg->name = strdup(name);
	if (!cfg->name)
		goto err1;
	cfg->config = json_entity_copy(d);
	if (!cfg->config)
		goto err1;
	cfg->msg_no = msg_no;
	/*
	 * The only way the config command will be deferred is that it must be
	 * given in a config file at the start time.
	 */
	cfg->config_file = strdup(config_file);
	if (!cfg->config_file)
		goto err1;
	TAILQ_INSERT_TAIL(&deferred_pi_config_q, cfg, entry);
	return cfg;
err1:
	ldmsd_deferred_pi_config_free(cfg);
err:
	errno = ENOMEM;
	return NULL;
}

void ldmsd_plugin___del(ldmsd_cfgobj_t obj)
{
	ldmsd_plugin_inst_t inst;
	inst = (ldmsd_plugin_inst_t)obj;
	if (inst->del)
		inst->del(inst);
	inst->base->del(inst);
	ldmsd_plugin_inst_free(inst);
	ldmsd_cfgobj___del(obj);
}

json_entity_t __plugin_attr_get(json_entity_t dft, json_entity_t spc,
				char **_plugin, json_entity_t *_cfg, int *_perm)
{
	int rc;
	json_entity_t err = NULL;
	json_entity_t plugin, perm, cfg = NULL;

	if (spc) {
		plugin = json_value_find(spc, "plugin");
		perm = json_value_find(spc, "perm");
	}
	if (!plugin && dft)
		plugin = json_value_find(dft, "plugin");
	if (!perm && dft)
		perm = json_value_find(dft, "perm");

	/* plugin */
	if (plugin) {
		if (JSON_STRING_VALUE != json_entity_type(plugin)) {
			err = json_dict_build(err, JSON_STRING_VALUE,
						"'plugin' is not a string.", -1);
			if (!err)
				goto oom;
		}
		*_plugin = json_value_str(plugin)->str;
	} else {
		*_plugin = NULL;
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
		*_perm = 0770;
	}

	if (dft) {
		cfg = json_entity_copy(dft);
		if (!cfg)
			goto oom;
	}
	if (spc) {
		if (cfg) {
			rc = json_dict_merge(cfg, spc);
			if (rc)
				goto oom;
		} else {
			cfg = json_entity_copy(spc);
			if (!cfg)
				goto oom;
		}
	}

	(void)json_attr_rem(cfg, "plugin");
	(void)json_attr_rem(cfg, "perm");

	/* plugin instance configuration attributes */
	if (cfg) {
		if (JSON_LIST_VALUE == json_entity_type(cfg)) {
			*_cfg = cfg;
			if (!*_cfg)
				goto oom;
		} else if (JSON_DICT_VALUE == json_entity_type(cfg)) {
			*_cfg = json_entity_new(JSON_LIST_VALUE);
			if (!*_cfg)
				goto oom;
			json_item_add(*_cfg, cfg);
		} else {
			err = json_dict_build(err, JSON_STRING_VALUE,
					"'config' is not a dictionary or a list.",
					-1);
			if (!err)
				goto oom;
		}
	} else {
		*_cfg = NULL;
	}

	return err;
oom:
	errno = ENOMEM;
	return NULL;
}

int ldmsd_plugin_disable(ldmsd_cfgobj_t obj)
{
	/* TODO: complete this */
	return 0;
}

int ldmsd_plugin_enable(ldmsd_cfgobj_t obj)
{
	int rc;
	json_entity_t cfg;
	ldmsd_req_buf_t buf;
	ldmsd_plugin_inst_t pi = (ldmsd_plugin_inst_t)obj;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf) {
		rc = ENOMEM;
		goto out;
	}

	for (cfg = json_item_first(pi->cfg); cfg; cfg = json_item_next(cfg)) {
		rc = ldmsd_plugin_inst_config(pi, cfg, buf->buf, buf->len);
		if (rc)
			goto out;
	}
out:
	if (buf)
		ldmsd_req_buf_free(buf);
	return rc;

}

json_entity_t __plugin_export_config(ldmsd_plugin_inst_t inst)
{
	json_entity_t query, cfg, env;
	query = ldmsd_cfgobj_query_result_new(&inst->obj);
	if (!query)
		goto oom;
	query = json_dict_build(query,
			JSON_STRING_VALUE, "plugin", inst->plugin_name,
			JSON_DICT_VALUE, "config",
				-2,
			JSON_DICT_VALUE, "env",
				-2,
			-1);
	if (!query)
		goto oom;

	cfg = inst->base->query(inst, "config");
	if (!cfg && errno)
		goto oom;
	if (cfg)
		json_attr_mod(query, "config", cfg);

	env = inst->base->query(inst, "env");
	if (!env && errno)
		goto oom;
	if (env)
		json_attr_mod(query, "env", env);
	return query;
oom:
	if (query)
		json_entity_free(query);
	errno = ENOMEM;
	return NULL;
}

json_entity_t ldmsd_plugin_export(ldmsd_cfgobj_t obj)
{
	json_entity_t export;
	ldmsd_plugin_inst_t inst = (ldmsd_plugin_inst_t)obj;
	export = __plugin_export_config(inst);
	if (!export)
		goto oom;
	return ldmsd_result_new(0, NULL, export);
oom:
	errno = ENOMEM;
	return NULL;
}

json_entity_t ldmsd_plugin_query(ldmsd_cfgobj_t obj)
{
	json_entity_t query, status;
	ldmsd_plugin_inst_t inst = (ldmsd_plugin_inst_t)obj;
	query = __plugin_export_config(inst);
	if (!query)
		goto err;
	query = json_dict_build(query,
			JSON_DICT_VALUE, "status",
				-2,
			-1);
	if (!query) {
		errno = ENOMEM;
		goto err;
	}
	status = ldmsd_plugin_inst_query(inst, "status");
	if (!status && errno)
		goto err;
	if (status)
		json_attr_mod(query, "status", status);
	return ldmsd_result_new(0, NULL, query);
err:
	if (query)
		json_entity_free(query);
	return NULL;


}

json_entity_t ldmsd_plugin_update(ldmsd_cfgobj_t obj, short enabled,
				json_entity_t dft, json_entity_t spc)
{
	char *pi_name;
	json_entity_t cfg_list;
	int perm;
	json_entity_t err;
	ldmsd_plugin_inst_t inst;

	err = __plugin_attr_get(dft, spc, &pi_name, &cfg_list, &perm);
	if (!err) {
		if (ENOMEM == errno)
			goto oom;
	} else {
		return ldmsd_result_new(EINVAL, 0, err);
	}

	if (pi_name) {
		err = json_dict_build(err, JSON_STRING_VALUE, "plugin",
					"'plugin' cannot be changed.", -1);
		if (!err)
			goto oom;
	}

	if (err)
		return ldmsd_result_new(EINVAL, 0, err);

	inst = (ldmsd_plugin_inst_t)obj;
	if (cfg_list)
		plugin_inst_config_add(inst, cfg_list);
	obj->enabled = (enabled < 0)?obj->enabled:enabled;
	return ldmsd_result_new(0, NULL, NULL);
oom:
	errno = ENOMEM;
	return NULL;
}

json_entity_t ldmsd_plugin_create(const char *name, short enabled,
				json_entity_t dft, json_entity_t spc,
				uid_t uid, gid_t gid)
{
	int rc;
	json_entity_t err = NULL;
	char *pi_name;
	int perm;
	json_entity_t cfg_list;
	ldmsd_req_buf_t buf;
	ldmsd_plugin_inst_t inst;

	buf = ldmsd_req_buf_alloc(1024);
	if (!buf)
		goto oom;

	err = __plugin_attr_get(dft, spc, &pi_name, &cfg_list, &perm);
	if (!err) {
		if (ENOMEM == errno)
			goto oom;
	} else {
		return ldmsd_result_new(EINVAL, 0, err);
	}

	if (!pi_name) {
		err = json_dict_build(err, JSON_STRING_VALUE, "plugin",
					"'plugin' is missing.", -1);
		if (!err)
			goto oom;
	}

	if (LDMSD_ATTR_NA == perm)
		perm = 0770;

	if (err)
		return ldmsd_result_new(EINVAL, 0, err);

	/* All attributes were verified */
	inst = ldmsd_plugin_inst_load(name, pi_name, buf->buf, buf->len);
	if (!inst)
		goto oom;

	rc = ldmsd_cfgobj_init(&inst->obj, name, LDMSD_CFGOBJ_PLUGIN,
				ldmsd_plugin___del,
				ldmsd_plugin_update,
				ldmsd_cfgobj_delete,
				ldmsd_plugin_query,
				ldmsd_plugin_export,
				ldmsd_plugin_enable,
				ldmsd_plugin_disable,
				uid, gid, perm ,enabled);
	if (rc)
		goto err;

	if (cfg_list)
		plugin_inst_config_add(inst, cfg_list);

	ldmsd_req_buf_free(buf);
	return ldmsd_result_new(0, NULL, NULL);
oom:
	rc = ENOMEM;
err:
	if (buf)
		ldmsd_req_buf_free(buf);
	if (inst)
		ldmsd_plugin_inst_free(inst); /* TODO: change this */
	if (cfg_list)
		json_entity_free(cfg_list);
	errno = rc;
	return NULL;
}
