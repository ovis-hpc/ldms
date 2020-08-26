#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <coll/rbt.h>

#include "ldmsd.h"
#include "ldmsd_translator.h"
#include "ldmsd_request.h"

#define PRDCR_STR "prdcr"
#define UPDTR_STR "updtr"
#define STRGP_STR "strgp"
#define LISTEN_STR "listen"
#define DAEMON_STR "daemon"
#define AUTH_STR "auth"
#define SETGRP_STR "setgroup"
#define SMPLR_STR "smplr"
#define PLUGIN_STR "plugin"


typedef struct scripting_inst_s *scripting_inst_t;
struct scripting_inst_s {
	struct ldmsd_plugin_inst_s base;
	uint16_t msg_no;
	char *path;
	uint16_t lineno;
	struct rbt obj_tree_by_key;
	struct rbt obj_tree_by_id;
};

typedef struct scripting_obj {
	char *key;
	uint64_t id;
	json_entity_t req;
	json_entity_t lines; /* list of related line numbers */
	struct rbn key_rbn;
	struct rbn id_rbn;

} *scripting_obj_t;

static uint32_t req_id_get(scripting_inst_t inst)
{
	uint32_t id;
	inst->msg_no++;
	id = (inst->lineno << 16) | inst->msg_no;
	return id;
}

static int scripting_obj_key_cmp(void *a, const void *b)
{
	return strcmp(a, b);
}

static int scripting_obj_id_cmp(void *a, const void *b)
{
	return a - b;
}

static int
scripting_syntax_log(scripting_inst_t inst, scripting_obj_t obj,
		enum ldmsd_loglevel level, const char *fmt, ...)
{
	va_list ap;
	ldmsd_req_buf_t buf, line_buf;
	size_t cnt;
	json_entity_t lineno;
	int rc;
	buf = line_buf = NULL;

	buf = ldmsd_req_buf_alloc(512);
	if (!buf)
		goto oom;
	line_buf = ldmsd_req_buf_alloc(512);
	if (!line_buf)
		goto oom;

	va_start(ap, fmt);
	cnt = vsnprintf(buf->buf, buf->len, fmt, ap);
	va_end(ap);
	va_start(ap, fmt);
	if (cnt >= buf->len) {
		buf = ldmsd_req_buf_realloc(buf, cnt + 1);
		if (!buf)
			goto oom;
	}
	(void) vsnprintf(buf->buf, buf->len, fmt, ap);
	va_end(ap);

	for (lineno = json_item_first(obj->lines); lineno;
			lineno = json_item_next(lineno)) {
		rc = ldmsd_req_buf_append(line_buf, " %ld", json_value_int(lineno));
		if (rc < 0)
			goto oom;
	}

	ldmsd_log(level, "scripting: lines %s: %s\n", line_buf->buf, buf->buf);
	ldmsd_req_buf_free(buf);
	ldmsd_req_buf_free(line_buf);
	return 0;

oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	rc = ENOMEM;
	if (buf)
		ldmsd_req_buf_free(buf);
	if (line_buf)
		ldmsd_req_buf_free(line_buf);
	return rc;
}

static int
scripting_obj_add_linenum(scripting_inst_t inst, scripting_obj_t obj);

static struct scripting_obj *
scripting_obj_new(scripting_inst_t inst, char *key, enum ldmsd_cfgobj_type type)
{
	json_entity_t spec;
	struct scripting_obj *obj = malloc(sizeof(*obj));
	if (!obj)
		return NULL;
	obj->key = strdup(key);
	if (!obj->key)
		goto err0;
	obj->id = req_id_get(inst);

	spec = json_entity_new(JSON_DICT_VALUE);
	if (!spec)
		goto err1;
	if (LDMSD_CFGOBJ_DAEMON == type) {
		obj->req = ldmsd_cfgobj_update_req_obj_new(obj->id,
						ldmsd_cfgobj_type2str(type),
						-1, NULL, NULL, spec);
	} else {
		obj->req = ldmsd_cfgobj_create_req_obj_new(obj->id,
						ldmsd_cfgobj_type2str(type),
						0, NULL, spec);
	}
	if (!obj->req)
		goto err2;
	obj->lines = json_entity_new(JSON_LIST_VALUE);
	if (!obj->lines)
		goto err3;
	scripting_obj_add_linenum(inst, obj);
	rbn_init(&obj->key_rbn, obj->key);
	rbn_init(&obj->id_rbn, (void *)obj->id);
	rbt_ins(&inst->obj_tree_by_key, &obj->key_rbn);
	rbt_ins(&inst->obj_tree_by_id, &obj->id_rbn);
	return obj;
err3:
	json_entity_free(obj->req);
err2:
	json_entity_free(spec);
err1:
	free(obj->key);
err0:
	free(obj);
	return NULL;
}

static struct scripting_obj *
scripting_obj_find(scripting_inst_t inst, const char *key)
{
	struct rbn *rbn;
	struct scripting_obj *obj;

	rbn = rbt_find(&inst->obj_tree_by_key, key);
	if (!rbn) {
		errno = ENOENT;
		goto err;
	}
	obj = container_of(rbn, struct scripting_obj, key_rbn);
	return obj;
err:
	return NULL;
}

static void scripting_obj_free(scripting_inst_t inst, scripting_obj_t obj)
{
	rbt_del(&inst->obj_tree_by_key, &obj->key_rbn);
	json_entity_free(obj->req);
	json_entity_free(obj->lines);
	free(obj->key);
}

static int
scripting_obj_add_linenum(scripting_inst_t inst, scripting_obj_t obj)
{
	json_entity_t lineno;
	lineno = json_entity_new(JSON_INT_VALUE, inst->lineno);
	if (!lineno)
		return ENOMEM;
	json_item_add(obj->lines, lineno);
	return 0;
}

static scripting_obj_t
scripting_obj_next(scripting_inst_t inst, scripting_obj_t _obj,
				char *cfgobj_type, char *regex_str)
{
	int rc;
	scripting_obj_t obj = NULL;
	struct rbn *rbn;
	char *name;
	regex_t regex;
	ldmsd_req_buf_t buf;
	size_t len = strlen(cfgobj_type);

	buf = ldmsd_req_buf_alloc(512);
	if (!buf) {
		errno = ENOMEM;
		return NULL;
	}

	if (regex_str) {
		rc = ldmsd_compile_regex(&regex, regex_str, buf->buf, buf->len);
		if (rc) {
			scripting_syntax_log(inst, obj, LDMSD_LERROR, "%s", buf->buf);
			ldmsd_req_buf_free(buf);
			errno = rc;
			return NULL;
		}
	}

	if (_obj)
		rbn = rbn_succ(&_obj->key_rbn);
	else
		rbn = rbt_min(&inst->obj_tree_by_key);
	while (rbn) {
		obj = container_of(rbn, struct scripting_obj, key_rbn);
		if (!cfgobj_type && !regex_str)
			goto out;
		if (cfgobj_type) {
			if (0 != strncmp(obj->key, cfgobj_type, len)) {
				/* Different cfgobj type */
				goto next;
			}
		}
		if (!regex_str)
			goto out;

		name = &obj->key[len + 1];
		rc = regexec(&regex, name, 0, NULL, 0);
		if (rc) {
			/* The name isn't matched the regex_str */
			goto next;
		}
		goto out;
	next:
		rbn = rbn_succ(rbn);
	}
	/* The object doesn't exist. */
	obj = NULL;

out:
	if (regex_str)
		regfree(&regex);
	ldmsd_req_buf_free(buf);
	return obj;
}

static const char *scripting_obj_cfgobj_name_get(scripting_obj_t obj)
{
	char *delim = strchr(obj->key, ':');
	return delim + 1;
}

static void
req_obj_enabled_flag_update(scripting_obj_t obj, int is_enabled)
{
	json_attr_mod(obj->req, "enabled", is_enabled);
}

/* ============== Common Plugin APIs ================= */

static
const char *scripting_desc(ldmsd_plugin_inst_t pi)
{
	return "scripting - Translate the configuration scripting syntax into a list of cfgobj request JSON requests";
}

static
const char *scripting_help(ldmsd_plugin_inst_t pi)
{
	return "scripting does not take extra options.";
}

static
void scripting_del(ldmsd_plugin_inst_t pi)
{
	struct rbn *rbn;
	scripting_obj_t obj;
	scripting_inst_t inst = (void*)pi;

	while ((rbn = rbt_min(&inst->obj_tree_by_key))) {
		obj = container_of(rbn, struct scripting_obj, key_rbn);
		scripting_obj_free(inst, obj);
	}
}

/* ============== Plugin functions ================ */

static void __get_attr_name_value(char *av, char **name, char **value)
{
	assert((av && name && value) ||
			(NULL == "__get_attr_name_value() invalid parameter"));
	if (!av || !name || !value) {
		if (name)
			*name = NULL;
		if (value)
			*value = NULL;
		return;
	}
	*name = av;
	char *delim;
	delim = strchr(av, '=');
	if (delim) {
		*value = delim;
		**value = '\0';
		(*value)++;
	} else {
		*value = NULL;
	}

}

static json_entity_t
add_attr_from_attr_str(const char *name, const char *value, json_entity_t attr_dict)
{

	attr_dict = json_dict_build(attr_dict, JSON_STRING_VALUE, name, value, -1);
	if (!attr_dict)
		return NULL;
	return attr_dict;
}

static const char *__ldmsd_cfg_delim = " \t";
static json_entity_t parse_attr_str(char *attr_str)
{
	char *ptr, *name, *value, *dummy;
	json_entity_t attr_dict = NULL;

	dummy = NULL;
	attr_str = strtok_r(attr_str, __ldmsd_cfg_delim, &ptr);
	while (attr_str) {
		dummy = strdup(attr_str); /* preserve the original string if we need to create multiple records */
		if (!dummy)
			goto err;
		__get_attr_name_value(dummy, &name, &value);
		if (!name)
			goto err;
		attr_dict = add_attr_from_attr_str(name, value, attr_dict);
		if (!attr_dict)
			goto err;
		attr_str = strtok_r(NULL, __ldmsd_cfg_delim, &ptr);
		free(dummy);
		dummy = NULL;
	}
	return attr_dict;
err:
	if (dummy)
		free(dummy);
	if (attr_dict)
		json_entity_free(attr_dict);
	return NULL;
}

static char *scripting_get_key(const char *cfgobj_type, const char *name)
{
	char *key;
	size_t str_len;
	str_len = strlen(cfgobj_type) + strlen(name) + 2;
	key = malloc(str_len);
	if (!key)
		return NULL;
	snprintf(key, str_len, "%s:%s", cfgobj_type, name);
	return key;
}

typedef int (*cfgobj_handler_fn)(scripting_inst_t inst, json_entity_t attr_dict);
struct handler_entry {
	char *verb;
	cfgobj_handler_fn handler;
};

static int handler_entry_cmp(const void *a, const void *b)
{
	struct handler_entry *a_ = (struct handler_entry *)a;
	struct handler_entry *b_ = (struct handler_entry *)b;
	return strcmp(a_->verb, b_->verb);
}

static int __eexist(const char *key)
{
	ldmsd_log(LDMSD_LERROR, "'%s' already exists.\n", key);
	return EEXIST;
}

static int __enoent(const char *key)
{
	ldmsd_log(LDMSD_LERROR, "%s cannot be found.\n", key);
	return ENOENT;
}

static int __enomem()
{
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return ENOMEM;
}

static int
req_obj_spec_update(scripting_obj_t obj, char *cfgobj_name, json_entity_t attr_dict)
{
	json_entity_t spc, attr, a;

	spc = json_value_find(json_value_find(obj->req, "spec"), cfgobj_name);
	if (!spc) {
		/* Create the spec of cfgobj_name */
		a = json_entity_copy(attr_dict);
		if (!a)
			return ENOMEM;
		attr = json_entity_new(JSON_ATTR_VALUE, cfgobj_name, attr_dict);
		if (!attr) {
			json_entity_free(a);
			return ENOMEM;
		}
		json_attr_add(json_value_find(obj->req, "spec"), attr);
	} else {
		/* Update the spec of cfgobj_name */
		for (attr = json_attr_first(attr_dict); attr; attr = json_attr_next(attr)) {
			a = json_entity_copy(attr);
			if (!a)
				return __enomem();
			json_attr_add(spc, a);
		}
	}
	return 0;
}

static int auth_add_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *key, *name;
	struct scripting_obj *obj;
	key = NULL;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(AUTH_STR, name);
	obj = scripting_obj_find(inst, key);
	if (obj) {
		rc = __eexist(key);
		goto err;
	}
	obj = scripting_obj_new(inst, key, LDMSD_CFGOBJ_AUTH);
	if (!obj)
		goto oom;
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto oom;
	req_obj_enabled_flag_update(obj, 1);
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	if (key)
		free(key);
	return rc;
}

static int listen_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc = 0;
	scripting_obj_t obj;
	ldmsd_req_buf_t buf = NULL;
	char *xprt, *port;
	char *key = NULL;

	buf = ldmsd_req_buf_alloc(128);
	if (!buf)
		goto oom;

	xprt = json_value_str(json_value_find(attr_dict, "xprt"))->str;
	port = json_value_str(json_value_find(attr_dict, "port"))->str;
	rc = ldmsd_req_buf_append(buf, "%s:%s", xprt, port);
	if (rc < 0)
		goto oom;

	key = scripting_get_key(LISTEN_STR, buf->buf);
	obj = scripting_obj_find(inst, key);
	if (obj) {
		rc = __eexist(key);
		goto err;
	}

	obj = scripting_obj_new(inst, key, LDMSD_CFGOBJ_LISTEN);
	if (!obj)
		goto oom;
	rc = req_obj_spec_update(obj, buf->buf, attr_dict);
	if (rc)
		goto oom;
	req_obj_enabled_flag_update(obj, 1);
	ldmsd_req_buf_free(buf);
	return 0;
oom:
	rc = __enomem();
err:
	if (key)
		free(key);
	if (buf)
		ldmsd_req_buf_free(buf);
	return rc;
}

struct cli_opts {
	const char *name;
	char short_opt;
	const char *cfgobj_name;
	const char *attr_name;
	enum json_value_e attr_type;
};
static int cli_opts_cmp(const void *_a, const void *_b)
{
	struct cli_opts *a = (struct cli_opts *)_a;
	struct cli_opts *b = (struct cli_opts *)_b;
	return strcmp(a->name, b->name);
}
struct cli_opts opts[] = {
		{ "B",			'B',	"pid_file",
		  "banner",		JSON_INT_VALUE
		},
		{ "H",			'H',	NULL,
		  NULL,			-1
		},
		{ "P",			'P',	"workers",
		  "count",		JSON_INT_VALUE
		},
		{ "a",			'a',	"global_auth",
		  "name",		JSON_STRING_VALUE
		},
		{ "banner",		'B',	"pid_file",
		  "banner",		JSON_INT_VALUE
		},
		{ "daemon-name",	'n',	"ldmsd-id",
		  "name",		JSON_STRING_VALUE
		},
		{ "default-auth",	'a',	"global_auth",
		  "name",		JSON_STRING_VALUE
		},
		{ "hostname",		'H', 	NULL,
		  NULL,			-1
		},
		{ "k",			'k',	"kernel",
		  "enabled",		JSON_BOOL_VALUE
		},
		{ "kernel-file",	's',	"kernel",
		  "path",		JSON_STRING_VALUE
		},
		{ "l",			'l',	"log",
		  "path",		JSON_STRING_VALUE
		},
		{ "logfile",		'l',	"log",
		  "path",		JSON_STRING_VALUE
		},
		{ "loglevel",		'v',	"log",
		  "level",		JSON_STRING_VALUE
		},
		{ "m",			'm',	"set_memory",
		  "size",		JSON_STRING_VALUE
		},
		{ "mem",		'm',	"set_memory",
		  "size",		JSON_STRING_VALUE
		},
		{ "n",			'n',	"ldmsd-id",
		  "name",		JSON_STRING_VALUE
		},
		{ "num-threads",	'P',	"workers",
		  "count",		JSON_INT_VALUE
		},
		{ "pidfile",		'r',	"pid_file",
		  "path",		JSON_STRING_VALUE
		},
		{ "publish-kernel",	'k',	"kernel",
		  "enabled",		JSON_BOOL_VALUE
		},
		{ "r",			'r',	"pid_file",
		  "path",		JSON_STRING_VALUE
		},
		{ "s",			's',	"kernel",
		  "path",		JSON_STRING_VALUE
		},
		{ "v",			'v',	"log",
		  "level",		JSON_STRING_VALUE
		},
		{ "x",			'x',	"listen",
		  NULL,			-1
		},
		{ "xprt",		'x',	"listen",
		  NULL,			-1
		},
};

static int cli_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	struct scripting_obj *obj;
	char *n, *v;
	json_entity_t attr, da, spc;
	struct cli_opts *map;
	ldmsd_req_buf_t buf = NULL;

	buf = ldmsd_req_buf_alloc(512);
	if (!buf)
		goto oom;
	for (attr = json_attr_first(attr_dict); attr; attr = json_attr_next(attr)) {
		da = json_attr_find(attr_dict, "default-auth");
		if (da)
			attr = da;
		n = json_attr_name(attr)->str;
		v = json_value_str(json_attr_value(attr))->str;
		map = bsearch(&n, &opts, ARRAY_SIZE(opts), sizeof(*map), cli_opts_cmp);
		if (!map) {
			ldmsd_log(LDMSD_LERROR, "Unknown '%s' cli attribute\n", n);
			rc = ENOTSUP;
			goto err;
		}
		if (map->cfgobj_name) {
			rc = ldmsd_req_buf_append(buf, "%s:%s", DAEMON_STR,
							map->cfgobj_name);
			if (rc < 0)
				goto oom;
			obj = scripting_obj_find(inst, buf->buf);
			if (!obj) {
				obj = scripting_obj_new(inst, buf->buf,
						LDMSD_CFGOBJ_DAEMON);
				if (!obj)
					goto oom;
			} else {
				rc = scripting_obj_add_linenum(inst, obj);
				if (rc)
					goto oom;
			}
		}
		switch (map->short_opt) {
		case 'H':
			ldmsd_log(LDMSD_LWARNING, "'hostname' is obsolete. "
						"Please use daemon-name.\n");
			break;
		case 'x':
			/* It is a listen cfgobj, not a daemon cfgobj */
			scripting_obj_free(inst, obj);
			spc = json_dict_build(NULL,
					JSON_STRING_VALUE, "xprt", strtok(v, ":"),
					JSON_STRING_VALUE, "port", strtok(v, ":"),
					-1);
			if (!spc)
				goto oom;
			rc = listen_handler(inst, spc);
			if (rc)
				goto err;
			break;
		case 'a':
			attr_dict = json_dict_build(attr_dict,
					JSON_STRING_VALUE, "plugin", v,
					JSON_STRING_VALUE, "name", v,
					-1);
			if (!attr_dict)
				goto oom;
			json_attr_rem(attr_dict, n);
			rc = auth_add_handler(inst, attr_dict);
			if (rc)
				goto err;
			v = json_value_str(json_value_find(attr_dict, "name"))->str;
			spc = json_dict_build(NULL,
					JSON_STRING_VALUE, "name", v,
					-1);
			if (!spc)
				goto oom;
			rc = req_obj_spec_update(obj, (char *)map->cfgobj_name, spc);
			if (rc)
				goto oom;
			break;
		case 'k':
			json_attr_mod(obj->req, "enabled", 1);
			break;
		case 'B':
		case 'P':
			spc = json_dict_build(NULL, map->attr_type,
						map->attr_name, atoi(v),
						-1);
			if (!spc)
				goto oom;
			rc = req_obj_spec_update(obj, (char *)map->cfgobj_name, spc);
			if (rc)
				goto oom;
			break;
		case 'l':
		case 'm':
		case 'n':
		case 'r':
		case 's':
		case 'v':
			spc = json_dict_build(NULL, map->attr_type,
						map->attr_name, v, -1);
			if (!spc)
				goto oom;
			rc = req_obj_spec_update(obj, (char *)map->cfgobj_name, spc);
			if (rc)
				goto oom;
			break;
		default:
			break;
		}
		ldmsd_req_buf_reset(buf);
		if ('a' == map->short_opt)
			break;
	}
	return 0;
oom:
	rc = __enomem();
err:
	if (buf)
		ldmsd_req_buf_free(buf);
	return rc;
}

static int prdcr_add_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key = NULL;
	scripting_obj_t obj;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(PRDCR_STR, name);
	obj = scripting_obj_find(inst, key);
	if (obj) {
		rc = __eexist(key);
		goto err;
	}
	obj = scripting_obj_new(inst, key, LDMSD_CFGOBJ_PRDCR);
	if (!obj)
		goto oom;
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto oom;
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	if (key)
		free(key);
	return rc;
}

static int prdcr_start_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key = NULL;
	scripting_obj_t obj;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(PRDCR_STR, name);
	obj = scripting_obj_find(inst, key);
	if (!obj) {
		rc = __enoent(key);
		goto err;
	}
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto err;
	req_obj_enabled_flag_update(obj, 1);
	free(key);
	return 0;
err:
	if (key)
		free(key);
	return rc;
}

static int
prdcr_start_regex_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *regex_str, *name;
	scripting_obj_t obj;
	ldmsd_req_buf_t buf;

	buf = ldmsd_req_buf_alloc(512);
	if (!buf)
		goto oom;
	regex_str = json_value_str(json_value_find(attr_dict, "regex"))->str;

	obj = scripting_obj_next(inst, NULL, PRDCR_STR, regex_str);
	while (obj) {
		name = (char *)scripting_obj_cfgobj_name_get(obj);
		rc = req_obj_spec_update(obj, name, attr_dict);
		if (rc)
			goto err;
		req_obj_enabled_flag_update(obj, 1);
		obj = scripting_obj_next(inst, obj, PRDCR_STR, regex_str);
	}
	ldmsd_req_buf_free(buf);
	return 0;

oom:
	rc = __enomem();
err:
	if (buf)
		ldmsd_req_buf_free(buf);
	return rc;
}

static int
prdcr_subscribe_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	char *regex_str, *stream, *name;
	scripting_obj_t obj;
	json_entity_t spec, streams, strm;

	regex_str = json_value_str(json_value_find(attr_dict, "regex"))->str;
	stream = json_value_str(json_value_find(attr_dict, "stream"))->str;

	obj = scripting_obj_next(inst, NULL, PRDCR_STR, regex_str);
	while (obj) {
		name = (char *)scripting_obj_cfgobj_name_get(obj);
		spec = json_value_find(json_value_find(obj->req, "spec"), name);
		streams = json_value_find(spec, "streams");
		if (!streams) {
			spec = json_dict_build(spec,
					JSON_LIST_VALUE, "streams",
						JSON_STRING_VALUE, stream,
						-2,
					-1);
			if (!spec)
				goto oom;
		} else {
			strm = json_entity_new(JSON_STRING_VALUE, stream);
			if (!strm)
				goto oom;
			json_item_add(streams, strm);
		}
		obj = scripting_obj_next(inst, obj, PRDCR_STR, regex_str);
	}
	return 0;
oom:
	return __enomem();
}

static int
smplr_add_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key = NULL, *pi_inst;
	scripting_obj_t obj;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(SMPLR_STR, name);
	obj = scripting_obj_find(inst, key);
	if (obj) {
		rc = __eexist(key);
		goto err;
	}
	obj = scripting_obj_new(inst, key, LDMSD_CFGOBJ_SMPLR);
	if (!obj)
		goto oom;
	pi_inst = json_value_str(json_value_find(attr_dict, "instance"))->str;
	attr_dict = json_dict_build(attr_dict,
				JSON_STRING_VALUE, "plugin_instance", pi_inst,
				-1);
	if (!attr_dict)
		goto oom;
	json_attr_rem(attr_dict, "instance");
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto oom;
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	free(key);
	return rc;
}

static int
smplr_start_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key = NULL;
	scripting_obj_t obj;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(SMPLR_STR, name);
	obj = scripting_obj_find(inst, key);
	if (!obj) {
		rc = __enoent(key);
		goto err;
	}
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto err;
	req_obj_enabled_flag_update(obj, 1);
	free(key);
	return 0;
err:
	if (key)
		free(key);
	return rc;
}

static int
updtr_add_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key = NULL;
	scripting_obj_t obj;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(UPDTR_STR, name);
	obj = scripting_obj_find(inst, key);
	if (obj) {
		rc = __eexist(key);
		goto err;
	}
	obj = scripting_obj_new(inst, key, LDMSD_CFGOBJ_UPDTR);
	if (!obj)
		goto oom;
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto oom;
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	free(key);
	return rc;
}

static int
updtr_prdcr_add_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key, *regex_str;
	scripting_obj_t obj;
	json_entity_t prdcrs, spc, regex;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	regex_str = json_value_str(json_value_find(attr_dict, "regex"))->str;
	key = scripting_get_key(UPDTR_STR, name);
	obj = scripting_obj_find(inst, key);
	if (!obj) {
		rc = __enoent(key);
		goto err;
	}
	spc = json_value_find(json_value_find(obj->req, "spec"), name);
	prdcrs = json_value_find(spc, "producer_filters");
	if (!prdcrs) {
		spc = json_dict_build(spc,
				JSON_LIST_VALUE, "producer_filters",
					JSON_STRING_VALUE, regex_str,
					-2,
				-1);
		if (!spc)
			goto oom;
	} else {
		regex = json_entity_new(JSON_STRING_VALUE, regex_str);
		if (!regex)
			goto oom;
		json_item_add(prdcrs, regex);
	}
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	free(key);
	return rc;
}

static int
updtr_match_add_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key, *match_str, *regex_str;
	scripting_obj_t obj;
	json_entity_t sets, schemas, spc, regex;


	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	match_str = json_value_str(json_value_find(attr_dict, "match"))->str;
	regex_str = json_value_str(json_value_find(attr_dict, "regex"))->str;
	key = scripting_get_key(UPDTR_STR, name);
	obj = scripting_obj_find(inst, key);
	if (!obj) {
		rc = __enoent(key);
		goto err;
	}
	spc = json_value_find(json_value_find(obj->req, "spec"), name);
	if (0 == strcmp(match_str, "inst")) {
		sets = json_value_find(spc, "set_instance_filters");
		if (!sets) {
			spc = json_dict_build(spc,
					JSON_LIST_VALUE, "set_instance_filters",
						JSON_STRING_VALUE, regex_str,
						-2,
					-1);
			if (!spc)
				goto oom;
		} else {
			regex = json_entity_new(JSON_STRING_VALUE, regex_str);
			if (!regex)
				goto oom;
			json_item_add(sets, regex);
		}
	} else {
		schemas = json_value_find(spc, "set_schema_filters");
		if (!schemas) {
			spc = json_dict_build(spc,
					JSON_LIST_VALUE, "set_schema_filters",
						JSON_STRING_VALUE, regex_str,
						-2,
					-1);
			if (!spc)
				goto oom;
		} else {
			regex = json_entity_new(JSON_STRING_VALUE, regex_str);
			if (!regex)
				goto oom;
			json_item_add(schemas, regex);
		}
	}
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	free(key);
	return rc;
}

static int
updtr_start_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key;
	scripting_obj_t obj;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(UPDTR_STR, name);
	obj = scripting_obj_find(inst, key);
	if (!obj) {
		rc = __enoent(key);
		goto err;
	}
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto err;
	req_obj_enabled_flag_update(obj, 1);
	free(key);
	return 0;
err:
	free(key);
	return rc;
}

static int
strgp_add_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key;
	scripting_obj_t obj;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(STRGP_STR, name);
	obj = scripting_obj_find(inst, key);
	if (obj) {
		rc = __eexist(key);
		goto err;
	}
	obj = scripting_obj_new(inst, key, LDMSD_CFGOBJ_STRGP);
	if (!obj)
		goto oom;
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto oom;
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	free(key);
	return rc;
}

static int
strgp_prdcr_add_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key, *regex_str;
	scripting_obj_t obj;
	json_entity_t prdcrs, spc, regex;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	regex_str = json_value_str(json_value_find(attr_dict, "regex"))->str;
	key = scripting_get_key(STRGP_STR, name);
	obj = scripting_obj_find(inst, key);
	if (!obj) {
		rc = __enoent(key);
		goto err;
	}
	spc = json_value_find(json_value_find(obj->req, "spec"), name);
	prdcrs = json_value_find(spc, "producer_filters");
	if (!prdcrs) {
		spc = json_dict_build(spc,
				JSON_LIST_VALUE, "producers",
					JSON_STRING_VALUE, regex_str,
					-2,
				-1);
		if (!spc)
			goto oom;
	} else {
		regex = json_entity_new(JSON_STRING_VALUE, regex_str);
		if (!regex)
			goto oom;
		json_item_add(prdcrs, regex);
	}
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	free(key);
	return rc;
}

static int
strgp_metric_add_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key, *metric_str;
	scripting_obj_t obj;
	json_entity_t spc, metrics, metric;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	metric_str = json_value_str(json_value_find(attr_dict, "metric"))->str;
	key = scripting_get_key(STRGP_STR, name);
	obj = scripting_obj_find(inst, key);
	if (!obj) {
		rc = __enoent(key);
		goto err;
	}
	spc = json_value_find(json_value_find(obj->req, "spec"), name);
	metrics = json_value_find(spc, "metrics");
	if (!metrics) {
		spc = json_dict_build(spc,
				JSON_LIST_VALUE, "metrics",
					JSON_STRING_VALUE, metric_str,
					-2,
				-1);
		if (!spc)
			goto oom;
	} else {
		metric = json_entity_new(JSON_STRING_VALUE, metric_str);
		if (!metric)
			goto oom;
		json_item_add(metrics, metric);
	}
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	free(key);
	return rc;
}

static int
strgp_start_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key;
	scripting_obj_t obj;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(STRGP_STR, name);
	obj = scripting_obj_find(inst, key);
	if (!obj) {
		rc = __enoent(key);
		goto err;
	}
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto err;
	req_obj_enabled_flag_update(obj, 1);
	free(key);
	return 0;
err:
	free(key);
	return rc;
}

static int load_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key;
	json_entity_t pi, n;
	scripting_obj_t obj;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(PLUGIN_STR, name);
	obj = scripting_obj_find(inst, key);
	if (obj) {
		rc = __eexist(key);
		goto err;
	}
	obj = scripting_obj_new(inst, key, LDMSD_CFGOBJ_PLUGIN);
	if (!obj)
		goto oom;

	pi = json_attr_find(attr_dict, "plugin");
	if (!pi) {
		n = json_entity_new(JSON_STRING_VALUE, name);
		if (!n)
			goto oom;
		pi = json_entity_new(JSON_ATTR_VALUE, "plugin", n);
		if (!pi) {
			json_entity_free(n);
			goto oom;
		}
		json_attr_add(attr_dict, pi);
	}
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto oom;
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	free(key);
	return rc;
}

static int config_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key;
	scripting_obj_t obj;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(PLUGIN_STR, name);
	obj = scripting_obj_find(inst, key);
	if (!obj) {
		rc = __enoent(key);
		goto err;
	}
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto err;
	req_obj_enabled_flag_update(obj, 1);
	free(key);
	return 0;
err:
	free(key);
	return rc;
}

static int setgroup_add_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key;
	scripting_obj_t obj;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	key = scripting_get_key(SETGRP_STR, name);
	obj = scripting_obj_find(inst, key);
	if (obj) {
		rc = __eexist(key);
		goto err;
	}

	obj = scripting_obj_new(inst, key, LDMSD_CFGOBJ_SETGRP);
	if (!obj)
		goto oom;
	rc = req_obj_spec_update(obj, name, attr_dict);
	if (rc)
		goto oom;
	req_obj_enabled_flag_update(obj, 1);
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	free(key);
	return rc;
}

static int setgroup_ins_handler(scripting_inst_t inst, json_entity_t attr_dict)
{
	int rc;
	char *name, *key, *inst_name;
	scripting_obj_t obj;
	json_entity_t spec, members, member;

	name = json_value_str(json_value_find(attr_dict, "name"))->str;
	inst_name = json_value_str(json_value_find(attr_dict, "instance"))->str;
	key = scripting_get_key(SETGRP_STR, name);
	obj = scripting_obj_find(inst, key);
	if (!obj) {
		rc = __enoent(key);
		goto err;
	}

	spec = json_value_find(json_value_find(obj->req, "spec"), name);
	members = json_value_find(spec, "members");
	if (!members) {
		spec = json_dict_build(spec,
				JSON_LIST_VALUE, "members",
					JSON_STRING_VALUE, inst_name,
					-2,
				-1);
		if (!spec)
			goto oom;
	} else {
		member = json_entity_new(JSON_STRING_VALUE, inst_name);
		if (!member)
			goto oom;
		json_item_add(members, member);
	}
	free(key);
	return 0;
oom:
	rc = __enomem();
err:
	free(key);
	return rc;
}

struct handler_entry handler_entry_tbl[] = {
		{ "auth_add",		auth_add_handler },
		{ "config",		config_handler },
//		{ "env",		env_handler },
		{ "listen",		listen_handler },
		{ "load",		load_handler },
		{ "prdcr_add",		prdcr_add_handler },
		{ "prdcr_start",	prdcr_start_handler },
		{ "prdcr_start_regex",	prdcr_start_regex_handler },
		{ "prdcr_subscribe",	prdcr_subscribe_handler },
		{ "set",		cli_handler },
		{ "setgroup_add",	setgroup_add_handler },
		{ "setgroup_ins",	setgroup_ins_handler },
		{ "smplr_add",		smplr_add_handler },
		{ "smplr_start",	smplr_start_handler },
		{ "strgp_add",		strgp_add_handler },
		{ "strgp_metric_add",	strgp_metric_add_handler },
		{ "strgp_prdcr_add",	strgp_prdcr_add_handler },
		{ "strgp_start",	strgp_start_handler },
		{ "updtr_add",		updtr_add_handler },
		{ "updtr_match_add",	updtr_match_add_handler },
		{ "updtr_prdcr_add",	updtr_prdcr_add_handler },
		{ "updtr_start",	updtr_start_handler },
};

/* find # standing alone in a line, indicating rest of line is comment.
 * e.g. ^# rest is comment
 * or: ^         # indented comment
 * or: ^dosomething foo a=b c=d #rest is comment
 * or: ^dosomething foo a=b c=d # rest is comment
 * but not: ^dosomething foo a=#channel c=foo#disk1"
 * \return pointer of first # comment or NULL.
 */
static char *find_comment(const char *line)
{
	char *s = (char *)line;
	int leadingspc = 1;
	while (*s != '\0') {
		if (*s == '#' && leadingspc)
			return s;
		if (isspace(*s))
			leadingspc = 1;
		else
			leadingspc = 0;
		s++;
	}
	return NULL;
}

static int parse_line(scripting_inst_t inst, char *l)
{
	char *v, *attr_str, *dummy, *ptr;
	json_entity_t attr_dict;
	struct handler_entry *handler;
	int rc = 0;

	dummy = strdup(l);
	if (!dummy)
		return ENOMEM;
	v = strtok_r(dummy, __ldmsd_cfg_delim, &ptr);
	attr_str = strtok_r(NULL, "\n", &ptr);
	attr_dict = parse_attr_str(attr_str);

	handler = bsearch(&v, &handler_entry_tbl, ARRAY_SIZE(handler_entry_tbl),
				sizeof(*handler), handler_entry_cmp);
	if (!handler) {
		rc = ENOTSUP;
		goto out;
	}
	rc = handler->handler(inst, attr_dict);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Error %d: %s\n", rc, l);
		goto out;
	}

out:
	free(dummy);
	return rc;
}

static int parse_file(scripting_inst_t inst, const char *path)
{
	uint16_t lineno = 0; /* The max number of lines is 65536. */
	int rc = 0;
	FILE *fin = NULL;
	char *buff = NULL;
	char *line = NULL;
	char *tmp;
	size_t line_sz = 0;
	char *comment;
	ssize_t off = 0;
	ssize_t cnt;
	size_t buf_len = 0;

	line = malloc(LDMSD_CFG_FILE_XPRT_MAX_REC);
	if (!line) {
		rc = errno;
		ldmsd_log(LDMSD_LERROR, "Out of memory\n");
		goto cleanup;
	}
	line_sz = LDMSD_CFG_FILE_XPRT_MAX_REC;

	fin = fopen(path, "rt");
	if (!fin) {
		rc = errno;
		strerror_r(rc, line, line_sz - 1);
		ldmsd_log(LDMSD_LERROR, "Failed to open the config file '%s'. %s\n",
				path, buff);
		goto cleanup;
	}

next_line:
	errno = 0;
	if (buff)
		memset(buff, 0, buf_len);
	cnt = getline(&buff, &buf_len, fin);
	if ((cnt == -1) && (0 == errno))
		goto cleanup;

	lineno++;
	tmp = buff;
	comment = find_comment(tmp);

	if (comment)
		*comment = '\0';

	/* Get rid of trailing spaces */
	while (cnt && isspace(tmp[cnt-1]))
		cnt--;

	if (!cnt) {
		/* empty string */
		goto parse;
	}

	tmp[cnt] = '\0';

	/* Get rid of leading spaces */
	while (isspace(*tmp)) {
		tmp++;
		cnt--;
	}

	if (!cnt) {
		/* empty buffer */
		goto parse;
	}

	if (tmp[cnt-1] == '\\') {
		if (cnt == 1)
			goto parse;
	}

	if (cnt + off > line_sz) {
		line = realloc(line, ((cnt + off)/line_sz + 1) * line_sz);
		if (!line) {
			rc = errno;
			ldmsd_log(LDMSD_LERROR, "Out of memory\n");
			goto cleanup;
		}
		line_sz = ((cnt + off)/line_sz + 1) * line_sz;
	}
	off += snprintf(&line[off], line_sz, "%s", tmp);

	/* attempt to merge multiple lines together */
	if (off > 0 && line[off-1] == '\\') {
		line[off-1] = ' ';
		goto next_line;
	}

parse:
	if (!off)
		goto next_line;

	inst->lineno = lineno;
	rc = parse_line(inst, line);
	if (rc)
		goto cleanup;
	off = 0;
	goto next_line;

cleanup:
	if (fin)
		fclose(fin);
	if (buff)
		free(buff);
	if (line)
		free(line);
	return rc;
}

#define LDMSD_CFG_FILE_XPRT_MAX_REC 8192
static json_entity_t
scripting_translate(ldmsd_plugin_inst_t pi, const char *path, json_entity_t req_list)
{
	scripting_obj_t obj;
	struct rbn *rbn;
	scripting_inst_t inst = (void *)pi;
	int rc;

	inst->path = strdup(path);
	if (!inst->path) {
		ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
		return NULL;
	}

	rc = parse_file(inst, path);
	if (rc) {
		errno = rc;
		return NULL;
	}

	for (rbn = rbt_min(&inst->obj_tree_by_key); rbn; rbn = rbn_succ(rbn)) {
		obj = container_of(rbn, struct scripting_obj, key_rbn);
		json_item_add(req_list, obj->req);
	}
	return req_list;
}

static
int scripting_error_report(ldmsd_plugin_inst_t pi, json_entity_t reply)
{
	int rc, status;
	char *err_str;
	uint64_t id;
	scripting_obj_t obj;
	struct rbn *rbn;
	json_entity_t results, result, lineno, msg, v, err;
	ldmsd_req_buf_t err_msg_buf = NULL;
	ldmsd_req_buf_t lineno_buf = NULL;
	scripting_inst_t inst = (void *)pi;

	err_msg_buf = ldmsd_req_buf_alloc(512);
	if (!err_msg_buf)
		goto oom;
	lineno_buf = ldmsd_req_buf_alloc(64);
	if (!lineno_buf)
		goto oom;
	id = json_value_int(json_value_find(reply, "id"));
	rbn = rbt_find(&inst->obj_tree_by_id, (void*)id);
	if (!rbn) {
		ldmsd_log(LDMSD_LERROR, "scripting: Unrecognized request %ld\n", id);
		rc = ENOENT;
		goto err;
	}
	obj = container_of(rbn, struct scripting_obj, id_rbn);
	rc = ldmsd_req_buf_append(lineno_buf, "Lines");
	if (rc < 0)
		goto oom;
	for (lineno = json_item_first(obj->lines); lineno;
			lineno = json_item_next(lineno)) {
		rc = ldmsd_req_buf_append(lineno_buf, " %ld", json_value_int(lineno));
		if (rc < 0)
			goto oom;
	}

	results = json_value_find(reply, "result");
	for (result = json_attr_first(results); result; result = json_attr_next(result)) {
		err = json_attr_value(result);
		status = json_value_int(json_value_find(err, "status"));
		if (!status)
			continue;

		rc = ldmsd_req_buf_append(err_msg_buf, "[%s]:", obj->key);
		if (rc < 0)
			goto oom;
		msg = json_value_find(err, "msg");
		v = json_value_find(err, "value");

		if (!msg && !v) {
			rc = ldmsd_req_buf_append(err_msg_buf, " error %d", status);
			if (rc < 0)
				goto oom;
			goto print;
		}

		if (msg) {
			err_str = json_value_str(msg)->str;
			rc = ldmsd_req_buf_append(err_msg_buf, " %s", err_str);
			if (rc < 0)
				goto oom;
		}

		if (v) {
			json_entity_t av;
			int cnt = 0;
			for (msg = json_attr_first(v); msg; msg = json_attr_next(msg)) {
				av = json_attr_value(msg);
				if (JSON_STRING_VALUE == json_entity_type(av)) {
					if (cnt > 0) {
						rc = ldmsd_req_buf_append(err_msg_buf, ",");
						if (rc < 0)
							goto oom;
					}
					rc = ldmsd_req_buf_append(err_msg_buf, " %s",
							json_value_str(av)->str);
					if (rc < 0)
						goto oom;
				}
			}
			rc = ldmsd_req_buf_append(err_msg_buf, "\n");
			if (rc < 0)
				goto oom;
		}
	print:
		ldmsd_log(LDMSD_LERROR, "%s: %s\n", lineno_buf->buf, err_msg_buf->buf);
		ldmsd_req_buf_reset(err_msg_buf);
	}
	ldmsd_req_buf_free(err_msg_buf);
	ldmsd_req_buf_free(lineno_buf);
	return 0;
oom:
	rc = ENOMEM;
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
err:
	if (err_msg_buf)
		ldmsd_req_buf_free(err_msg_buf);
	if (lineno_buf)
		ldmsd_req_buf_free(lineno_buf);
	return rc;
}

static
int scripting_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_translator_type_t translator = (void*)pi->base;
	scripting_inst_t inst = (void *)pi;

	translator->translate = scripting_translate;
	translator->config_file_export = NULL; /* TODO: implement this */
	translator->error_report = scripting_error_report;

	rbt_init(&inst->obj_tree_by_key, scripting_obj_key_cmp);
	rbt_init(&inst->obj_tree_by_id, scripting_obj_id_cmp);
	return 0;
}

static
struct scripting_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "translator",
		.plugin_name = "scripting",

		/* Common Plugin APIs */
		.desc   = scripting_desc,
		.help   = scripting_help,
		.init   = scripting_init,
		.del    = scripting_del,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	scripting_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
