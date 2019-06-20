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
#include "config.h"

/* Red-black tree for instances. */
static struct rbt inst_rbt = {NULL, (void*)strcmp};
static pthread_mutex_t inst_rbt_lock = PTHREAD_MUTEX_INITIALIZER;

#define LDMSD_PLUGIN_LIBPATH_MAX	1024

struct ldmsd_plugin_version_s plugin_ver = {.major = 1, .minor = 0, .patch = 0};

ldmsd_plugin_inst_t __ldmsd_plugin_inst_find(const char *inst_name)
{
	/* inst_rbt_lock must be held */
	struct rbn *rbn;
	rbn = rbt_find(&inst_rbt, inst_name);
	if (rbn)
		return container_of(rbn, struct ldmsd_plugin_inst_s, rbn);
	errno = ENOENT;
	return NULL;
}

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
int __status_json_new(ldmsd_plugin_inst_t inst)
{
	json_entity_t status, n, d, v, a;
	int i = 0;
	struct ldmsd_plugin_qjson_attrs pairs[] = {
			{ "name"    , JSON_STRING_VALUE, { .s = inst->inst_name }  },
			{ "plugin"  , JSON_STRING_VALUE, { .s = inst->plugin_name }},
			{ "type"    , JSON_STRING_VALUE, { .s = inst->type_name }  },
			{ "libpath" , JSON_STRING_VALUE, { .s = inst->libpath }    },
			{NULL, 0, NULL}
	};
	d = json_entity_new(JSON_DICT_VALUE);
	if (!d)
		return ENOMEM;

	while (pairs[i].name) {
		n = json_entity_new(JSON_STRING_VALUE, pairs[i].name);
		if (!n)
			goto err0;
		v = json_entity_new(JSON_STRING_VALUE, pairs[i].s);
		if (!v) {
			json_entity_free(n);
			goto err0;
		}
		a = json_entity_new(JSON_ATTR_VALUE, n, v);
		if (!a) {
			json_entity_free(v);
			json_entity_free(n);
			goto err0;
		}
		json_attr_add(d, a);
		i++;
	}

	n = json_entity_new(JSON_STRING_VALUE, "status");
	if (!n)
		goto err0;
	status = json_entity_new(JSON_ATTR_VALUE, n, d);
	if (!status) {
		json_entity_free(n);
		goto err0;
	}
	json_attr_add(inst->json, status);
	return 0;
err0:
	json_entity_free(d);
	return ENOMEM;
}

static
int  __config_json_new(ldmsd_plugin_inst_t inst)
{
	json_entity_t cfg, n, l;

	n = json_entity_new(JSON_STRING_VALUE, "config");
	if (!n)
		return ENOMEM;
	l = json_entity_new(JSON_LIST_VALUE);
	if (!l)
		goto err0;
	cfg = json_entity_new(JSON_ATTR_VALUE, n, l);
	if (!cfg)
		goto err1;
	json_attr_add(inst->json, cfg);
	return 0;

err2:
	json_entity_free(cfg);
err1:
	json_entity_free(l);
err0:
	json_entity_free(n);
	return ENOMEM;
}

static
ldmsd_plugin_inst_t ldmsd_plugin_inst_new(const char *plugin_name, char *errstr,
					  int errlen)
{
	int rc;
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

	pthread_mutex_lock(&inst_rbt_lock);
	inst = __ldmsd_plugin_inst_find(inst_name);
	if (inst) {
		errno = EEXIST;
		if (errstr)
			snprintf(errstr, errlen, "Plugin `%s` has already "
				 "loaded", inst_name);
		goto out;
	}

	inst = ldmsd_plugin_inst_new(plugin_name, errstr, errlen);
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
	inst->json = json_entity_new(JSON_DICT_VALUE);
	if (!inst->json)
		goto err2;
	rc = __config_json_new(inst);
	if (rc)
		goto err3;
	rc = __status_json_new(inst);
	if (rc)
		goto err3;

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

	inst->ref_count = 1;
	rbn_init(&inst->rbn, inst->inst_name);
	rbt_ins(&inst_rbt, &inst->rbn);
	goto out;

 err4:
	inst->base->del(inst); /* because inst->base->init() succeeded */
 err3:
	json_entity_free(inst->json);
 err2:
	free(inst->inst_name);
 err1:
	free(inst->base);
	free(inst);
	inst = NULL;
 out:
	pthread_mutex_unlock(&inst_rbt_lock);
	return inst;
}

ldmsd_plugin_inst_t ldmsd_plugin_inst_find(const char *inst_name)
{
	ldmsd_plugin_inst_t inst;
	pthread_mutex_lock(&inst_rbt_lock);
	inst = __ldmsd_plugin_inst_find(inst_name);
	if (inst)
		ldmsd_plugin_inst_get(inst);
	pthread_mutex_unlock(&inst_rbt_lock);
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

void ldmsd_plugin_inst_del(ldmsd_plugin_inst_t inst)
{
	/* This only remove the instance from the tree, and decrease the
	 * ref_count. When ref_count reaches 0, the actual del() is called.
	 */
	pthread_mutex_lock(&inst_rbt_lock);
	rbt_del(&inst_rbt, &inst->rbn);
	pthread_mutex_unlock(&inst_rbt_lock);
	ldmsd_plugin_inst_put(inst);
}

int ldmsd_plugin_inst_config(ldmsd_plugin_inst_t inst,
			     json_entity_t d,
			     char *ebuf, int ebufsz)
{
	json_entity_t cfg;
	cfg = json_attr_find(inst->json, "config");
	/*
	 * cfg is created in ldmsd_plugin_inst_load()
	 * cfg is a JSON attribute "config": []
	 */
	if (d)
		json_item_add(json_attr_value(cfg), json_entity_copy(d));

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

__attribute__((format(printf, 4, 5)))
int sappendf(char **buff, int *off, int *alen, const char *fmt, ...)
{
	va_list ap;
	char *tmp;
	int plen;
	int sz;

 again:
	va_start(ap, fmt);
	plen = vsnprintf(*buff + *off, *alen, fmt, ap);
	va_end(ap);
	if (plen < *alen) {
		/* good */
		(*off) += plen;
		(*alen) -= plen;
		return plen;
	}
	/* need realloc */
	sz = ((*off + plen) | 0xFF) + 1;
	tmp = realloc(*buff, sz);
	if (!tmp)
		return -1;
	*buff = tmp;
	*alen = sz - *off;
	goto again;
}

void ldmsd_plugin_inst_get(ldmsd_plugin_inst_t inst)
{
	__sync_add_and_fetch(&inst->ref_count, 1);
}

void ldmsd_plugin_inst_put(ldmsd_plugin_inst_t inst)
{
	if (__sync_sub_and_fetch(&inst->ref_count, 1))
		return;
	/* ref_count reaches 0, delete and free the instance */
	if (inst->del) {
		inst->del(inst);
	}
	inst->base->del(inst);
	ldmsd_plugin_inst_free(inst);
}

int __inst_rbt_lock()
{
	pthread_mutex_lock(&inst_rbt_lock);
	return 0;
}

int __inst_rbt_unlock()
{
	pthread_mutex_unlock(&inst_rbt_lock);
	return 0;
}

ldmsd_plugin_inst_t __plugin_inst_first()
{
	/* inst_rbt_lock must be held */
	ldmsd_plugin_inst_t inst = NULL;
	struct rbn *rbn;
	rbn = rbt_min(&inst_rbt);
	if (rbn) {
		inst = container_of(rbn, struct ldmsd_plugin_inst_s, rbn);
		ldmsd_plugin_inst_get(inst);
	}
	return inst;
}

ldmsd_plugin_inst_t __plugin_inst_next(ldmsd_plugin_inst_t inst)
{
	/* inst_rbt_lock must be held */
	ldmsd_plugin_inst_t ret = NULL;
	struct rbn *rbn;
	rbn = rbn_succ(&inst->rbn);
	ldmsd_plugin_inst_put(inst);
	if (rbn) {
		ret = container_of(rbn, struct ldmsd_plugin_inst_s, rbn);
		ldmsd_plugin_inst_get(ret);
	}
	return ret;
}

ldmsd_plugin_qrent_coll_t ldmsd_plugin_qrent_coll_new()
{
	ldmsd_plugin_qrent_coll_t coll = calloc(1, sizeof(*coll));
	if (coll)
		ldmsd_plugin_qrent_coll_init(coll);
	return coll;
}

void ldmsd_plugin_qrent_coll_init(ldmsd_plugin_qrent_coll_t coll)
{
	rbt_init(&coll->rbt, (void*)strcmp);
}

void ldmsd_plugin_qrent_coll_cleanup(ldmsd_plugin_qrent_coll_t coll)
{
	ldmsd_plugin_qrent_t qrent;
	while ((qrent = (void*)rbt_min(&coll->rbt))) {
		rbt_del(&coll->rbt, &qrent->rbn);
		if (qrent->type == LDMSD_PLUGIN_QRENT_COLL) {
			ldmsd_plugin_qrent_coll_cleanup(qrent->coll);
		}
		free(qrent);
	}
	/* cleanup does not free the coll itself */
}

void ldmsd_plugin_qresult_free(ldmsd_plugin_qresult_t qr)
{
	ldmsd_plugin_qrent_coll_cleanup(&qr->coll);
	free(qr);
}

int ldmsd_plugin_qrent_add(ldmsd_plugin_qrent_coll_t coll,
			   const char *name,
			   ldmsd_plugin_qrent_type_t type,
			   void *val)
{
	ldmsd_plugin_qrent_t qrent;
	int rc, sz;
	struct rbn *rbn;
	sz = sizeof(*qrent) + strlen(name) + 1;
	if (type == LDMSD_PLUGIN_QRENT_STR) {
		sz += 1 + strlen(val);
	}

	qrent = calloc(1,sz);
	if (!qrent) {
		rc = ENOMEM;
		goto out;
	}

	qrent->name = qrent->_priv;
	sz = sprintf(qrent->name, "%s", name);
	if (type == LDMSD_PLUGIN_QRENT_STR) {
		qrent->str = qrent->name + sz + 1;
		sprintf(qrent->str, "%s", (char*)val);
	} else {
		qrent->coll = val;
	}

	rbn = rbt_find(&coll->rbt, name);
	if (rbn) {
		rc = EEXIST; /* duplicate keys */
		goto err1;
	}

	rbn_init(&qrent->rbn, qrent->name);
	rbt_ins(&coll->rbt, &qrent->rbn);
	rc = 0;
	goto out;

err1:
	free(qrent);
out:
	return rc;
}

int ldmsd_plugin_qrent_add_bulk(ldmsd_plugin_qrent_coll_t coll,
				struct ldmsd_plugin_qrent_bulk_s *bulk)
{
	int rc;
	while (bulk->key) {
		rc = ldmsd_plugin_qrent_add(coll, bulk->key, bulk->type,
					    bulk->val);
		bulk++;
	}
	return rc;
}

int ldmsd_plugin_qrent_config_add(ldmsd_plugin_qresult_t r,
					ldmsd_plugin_qrent_t config_ent)
{
	int rc, id = 1;
	char id_str[8]; /* 8 digit numbers is large enough for the number of config lines */
	struct rbn *rbn;
	ldmsd_plugin_qrent_t config;

	rbn = rbt_find(&r->coll.rbt, "config");
	if (!rbn) {
		config = malloc(sizeof(*config));
		if (!config)
			return ENOMEM;
		config->coll = ldmsd_plugin_qrent_coll_new();
		if (!config->coll) {
			free(config);
			return ENOMEM;
		}
		config->type = LDMSD_PLUGIN_QRENT_COLL;

		ldmsd_plugin_qrent_add(&r->coll, "config",
				LDMSD_PLUGIN_QRENT_COLL,
				config);
	} else {
		config = container_of(rbn, struct ldmsd_plugin_qrent_s, rbn);
		rbn = rbt_max(&config->coll->rbt);
		if (rbn) {
			id = atoi((char *)rbn->key);
			id++;
		}
	}
	snprintf(id_str, 7, "%d", id);
	rc = ldmsd_plugin_qrent_add(config->coll, id_str,
				LDMSD_PLUGIN_QRENT_COLL,
				config_ent);
	return rc;
}

static
int __qrent_json_print(ldmsd_plugin_qrent_t qrent,
		       char **buff, int *off, int *alen);

static
int __qrcoll_json_print(ldmsd_plugin_qrent_coll_t coll,
			char **buff, int *off, int *alen)
{
	int first, rc;
	ldmsd_plugin_qrent_t subent;
	rc = sappendf(buff, off, alen, "{");
	if (rc < 0) {
		rc = errno;
		goto out;
	}
	first = 1;
	subent = (void*)rbt_min(&coll->rbt);
	while (subent) {
		if (!first) {
			rc = sappendf(buff, off, alen, ",");
			if (rc < 0) {
				rc = errno;
				goto out;
			}
		}
		first = 0;
		rc = __qrent_json_print(subent, buff, off, alen);
		if (rc) {
			rc = errno;
			goto out;
		}
		subent = (void*)rbn_succ(&subent->rbn);
	}
	rc = sappendf(buff, off, alen, "}");
	if (rc < 0)
		goto out;
	rc = 0;
out:
	return rc;
}

int ldmsd_plugin_qrent_coll_json_print(ldmsd_plugin_qrent_coll_t coll,
				       char **buff, int *off, int *alen)
{
	return __qrcoll_json_print(coll, buff, off, alen);
}

static
int __qrent_json_print(ldmsd_plugin_qrent_t qrent,
		       char **buff, int *off, int *alen)
{
	int rc;
	rc = sappendf(buff, off, alen, "\"%s\":", qrent->name);
	if (rc < 0) {
		rc = errno;
		goto out;
	}
	if (qrent->type == LDMSD_PLUGIN_QRENT_STR) {
		rc = sappendf(buff, off, alen, "\"%s\"", qrent->str);
		if (rc >= 0) /* good */
			rc = 0;
		else
			rc = errno;
		goto out;
	}
	/* type is COLL */
	rc = __qrcoll_json_print(qrent->coll, buff, off, alen);
out:
	return rc;
}

char *ldmsd_plugin_qresult_to_json(ldmsd_plugin_qresult_t qresult)
{
	char *buff;
	int off, alen, rc;

	off = 0;
	alen = 4096;
	buff = malloc(alen);
	if (!buff)
		return NULL;
	rc = sappendf(&buff, &off, &alen,
		      "{\"rc\":%d,\"results\":", qresult->rc);
	if (rc < 0) /* errno has also been set */
		goto err;
	rc = __qrcoll_json_print(&qresult->coll, &buff, &off, &alen);
	rc = sappendf(&buff, &off, &alen, "}");
	if (rc < 0) /* errno has also been set */
		goto err;
	return buff;
err:
	free(buff);
	return NULL;
}

json_entity_t ldmsd_plugin_qjson_new(ldmsd_plugin_inst_t inst)
{
	int rc;
	json_entity_t result;
	result = json_entity_new(JSON_DICT_VALUE);
	if (!result)
		return NULL;
	struct ldmsd_plugin_qjson_attrs bulks[] = {
			{ "rc",     JSON_INT_VALUE,    0 },
			{ "errmsg", JSON_STRING_VALUE, "" },
			{ "name",   JSON_STRING_VALUE, inst->inst_name },
			{ "plugin", JSON_STRING_VALUE, inst->plugin_name },
			{ "type",   JSON_STRING_VALUE, inst->type_name },
			{0},
	};
	rc = ldmsd_plugin_qjson_attrs_add(result, bulks);
	if (rc) {
		json_entity_free(result);
		return NULL;
	}
	return result;
}

void ldmsd_plugin_qjson_err_set(json_entity_t result, int rc, char *errmsg)
{
	json_attr_mod(result, "rc", rc);
	if (errmsg)
		json_attr_mod(result, "errmsg", errmsg);
}

typedef struct qtbl_ent_s {
	const char *key;
	json_entity_t (*qfn)(ldmsd_plugin_inst_t pi, json_entity_t result);
} *qtbl_ent_t;
#define QTBL_ENT(x) ((qtbl_ent_t)(x))

json_entity_t qfn_config(ldmsd_plugin_inst_t pi, json_entity_t result);
json_entity_t qfn_status(ldmsd_plugin_inst_t pi, json_entity_t result);
json_entity_t qfn_env(ldmsd_plugin_inst_t pi, json_entity_t result);

struct qtbl_ent_s qtbl[] = {
		{ "config" , qfn_config },
		{ "env"    , qfn_env    },
		{ "status" , qfn_status },
};

int qtbl_ent_cmp(const void *k0, const void *k1)
{
	return strcmp(QTBL_ENT(k0)->key, QTBL_ENT(k1)->key);
}

json_entity_t ldmsd_plugin_query(ldmsd_plugin_inst_t pi, const char *q)
{
	struct qtbl_ent_s *ent, key = {.key = q};
	json_entity_t result = ldmsd_plugin_qjson_new(pi);
	if (!result) {
		errno = ENOMEM;
		return NULL;
	}

	ent = bsearch(&key, qtbl, sizeof(qtbl)/sizeof(*qtbl), sizeof(*qtbl),
		      qtbl_ent_cmp);
	if (ent) {
		return ent->qfn(pi, result);
	} else {
		char errmsg[64];
		snprintf(errmsg, 63, "Unsupported query '%s'", q);
		ldmsd_plugin_qjson_err_set(result, EINVAL, errmsg);
	}
	return result;
}

int ldmsd_plugin_qjson_attrs_add(json_entity_t result,
			struct ldmsd_plugin_qjson_attrs *bulks)
{
	int i;
	json_entity_t a, n, v;

	for (i = 0; bulks[i].name; i++) {
		n = json_entity_new(JSON_STRING_VALUE, bulks[i].name);
		if (!n) {
			return ENOMEM;
		}
		v = json_entity_new(bulks[i].type, bulks[i].s);
		if (!v) {
			json_entity_free(n);
			return ENOMEM;
		}
		a = json_entity_new(JSON_ATTR_VALUE, n, v);
		if (!a) {
			json_entity_free(v);
			json_entity_free(n);
			return ENOMEM;
		}
		json_attr_add(result, a);
	}
	return 0;
}

json_entity_t qfn_config(ldmsd_plugin_inst_t pi, json_entity_t result)
{
	json_entity_t dup, cfg;
	/*
	 * cfg should not be NULL. It is created when the instance is created.
	 * Its value could be an empty JSON list.
	 */
	cfg = json_attr_find(pi->json, "config");
	/* Copy out the cfg json so that the caller could clean up the result. */
	dup = json_entity_copy(cfg);
	if (!dup) {
		ldmsd_plugin_qjson_err_set(result, ENOMEM, "Out of memory");
		return result;
	}
	json_attr_add(result, dup);
	return result;
}

json_entity_t qfn_status(ldmsd_plugin_inst_t pi, json_entity_t result)
{
	json_entity_t status_attr, dup;
	int rc;

	struct ldmsd_plugin_qjson_attrs bulks[] = {
			{ "libpath", JSON_STRING_VALUE, pi->libpath },
			{0}
	};
	/*
	 * status should not be NULL. It is created when the instance is created.
	 */
	status_attr = json_attr_find(pi->json, "status");
	dup = json_entity_copy(status_attr);
	if (!dup)
		goto enomem;
	rc = ldmsd_plugin_qjson_attrs_add(json_attr_value(dup), bulks);
	if (rc)
		goto enomem;
	json_attr_add(result, dup);
	return result;
enomem:
	ldmsd_plugin_qjson_err_set(result, ENOMEM, "Out of memory");
	return result;
}

json_entity_t qfn_env(ldmsd_plugin_inst_t pi, json_entity_t result)
{
	 /* No environment variables to add */
	return result;
}

struct ldmsd_deferred_pi_config_q deferred_pi_config_q;
void ldmsd_deferred_pi_config_free(ldmsd_deferred_pi_config_t cfg)
{
	TAILQ_REMOVE(&deferred_pi_config_q, cfg, entry);
	if (cfg->d)
		json_entity_free(cfg->d);
	if (cfg->name)
		free(cfg->name);
	if (cfg->buf)
		free(cfg->buf);
	free(cfg);
}

ldmsd_deferred_pi_config_t
ldmsd_deferred_pi_config_new(const char *name, json_entity_t d)
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
	cfg->d = json_entity_copy(d);
	if (!cfg->d)
		goto err1;
	TAILQ_INSERT_TAIL(&deferred_pi_config_q, cfg, entry);
	return cfg;
err1:
	ldmsd_deferred_pi_config_free(cfg);
err:
	errno = ENOMEM;
	return NULL;
}

int ldmsd_handle_deferred_plugin_config()
{
	struct ldmsd_deferred_pi_config *cfg, *nxt_cfg;
	ldmsd_plugin_inst_t inst;
	int rc;
	cfg = TAILQ_FIRST(&deferred_pi_config_q);
	while (cfg) {
		nxt_cfg = TAILQ_NEXT(cfg, entry);
		inst = ldmsd_plugin_inst_find(cfg->name);
		rc = ldmsd_plugin_inst_config(inst, cfg->d, cfg->buf, cfg->buflen);
		if (rc) {
			jbuf_t jb = json_entity_dump(NULL, cfg->d);
			ldmsd_log(LDMSD_LERROR, "Error config instance '%s': %s\n",
					cfg->name, jb->buf);
			jbuf_free(jb);
			return rc;
		}
		ldmsd_deferred_pi_config_free(cfg);
		cfg = nxt_cfg;
	}
	return 0;
}
