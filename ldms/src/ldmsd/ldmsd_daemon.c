/*
 * ldmsd_daemon.c
 *
 *  Created on: Apr 13, 2020
 *      Author: nnich
 */

#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
#include <libgen.h>
#include <string.h>
#include "ldmsd.h"
#include "ldmsd_request.h"

static const char *logpath = "stdout";
static enum ldmsd_loglevel loglevel = LDMSD_LERROR;
static char *global_auth_domain;
static char *ldmsd_myname;
static char *ldmsd_set_mem_sz;
static int ldmsd_worker_count;
static short is_quiet;

enum ldmsd_loglevel ldmsd_loglevel_get()
{
	return loglevel;
}

const char *ldmsd_global_auth_name_get()
{
	return global_auth_domain;
}

const char *ldmsd_myname_get()
{
	return ldmsd_myname;
}

const char *ldmsd_get_max_mem_sz_str()
{
	return ldmsd_set_mem_sz;
}

int ldmsd_worker_count_get()
{
	return ldmsd_worker_count;
}

short ldmsd_is_quiet()
{
	return is_quiet;
}

void ldmsd_daemon___del(ldmsd_cfgobj_t obj)
{
	ldmsd_daemon_t daemon = (ldmsd_daemon_t)obj;
	if (daemon->attr)
		json_entity_free(daemon->attr);
	ldmsd_cfgobj___del(obj);
}

json_entity_t log_query(ldmsd_cfgobj_t obj)
{
	json_entity_t query;
	query = ldmsd_cfgobj_query_result_new(obj);
	if (!query)
		goto oom;
	query = json_dict_build(query, JSON_STRING_VALUE, "path", logpath,
				       JSON_STRING_VALUE, "level",
						ldmsd_loglevel_names[loglevel], -1);
	if (!query)
		goto oom;
	return ldmsd_result_new(0, NULL, query);
oom:
	ldmsd_log(LDMSD_LERROR, "Out of memory\n");
	if (query)
		json_entity_free(query);
	return NULL;
}

extern FILE *log_fp;
json_entity_t log_update(ldmsd_cfgobj_t obj, short enabled, json_entity_t dft,
								json_entity_t spc)
{
	int rc;
	json_entity_t path, level, result;
	char *level_s, *path_s;
	FILE *new;
	ldmsd_daemon_t log = (ldmsd_daemon_t)obj;

	path = level = NULL;
	if (spc) {
		path = json_value_find(spc, "path");
		level = json_value_find(spc, "level");
	}
	if (!path && dft)
		path = json_value_find(dft, "path");
	if (!level && dft)
		level = json_value_find(dft, "level");
	if (path) {
		path_s = json_value_str(path)->str;
		rc = json_attr_mod(log->attr, "path", path_s);
		if (rc) {
			return ldmsd_result_new(rc, "Failed to update 'path'", NULL);
		}
		logpath = path_s;

		/* Update the log file right away */
		new = ldmsd_open_log(logpath);
		if (!new) {
			rc = errno;
			ldmsd_req_buf_t buf = ldmsd_req_buf_alloc(1024);
			if (!buf)
				return NULL;
			rc = ldmsd_req_buf_append(buf, "Failed to open the "
						"log file '%s'.", path_s);
			if (rc < 0) {
				ldmsd_req_buf_free(buf);
				return NULL;
			}
			result = ldmsd_result_new(rc, buf->buf, NULL);
			ldmsd_req_buf_free(buf);
			return result;
		}
		if (log_fp)
			fclose(log_fp);
		log_fp = new;
	}
	if (level) {
		level_s = json_value_str(level)->str;
		rc = json_attr_mod(log->attr, "level", level_s);
		if (rc) {
			return ldmsd_result_new(rc, "Failed to update 'level'.", NULL);
		}
		loglevel = ldmsd_str_to_loglevel(level_s);
		if (0 == strcasecmp("QUIET", level_s)) {
			obj->enabled = 0;
			is_quiet = 1;
		} else {
			obj->enabled = 1;
			is_quiet = 0;
		}
	}

	return ldmsd_result_new(0, NULL, NULL);
}

int log_init(ldmsd_daemon_t obj)
{
	obj->attr = json_dict_build(obj->attr,
			JSON_STRING_VALUE, "path", logpath,
			JSON_STRING_VALUE, "level", ldmsd_loglevel_names[loglevel], -1);
	if (!obj->attr)
		return ENOMEM;
	return 0;
}

int daemonize_enable(ldmsd_cfgobj_t obj)
{
	/* Do nothing */
	return 0;
}

int daemonize_disable(ldmsd_cfgobj_t obj)
{
	ldmsd_cfgobj_t pidfile = ldmsd_cfgobj_find("pid_file", LDMSD_CFGOBJ_DAEMON);
	pidfile->enable = 0;
	return 0;
}

json_entity_t daemonize_update(ldmsd_cfgobj_t obj, short enabled, json_entity_t dft,
					json_entity_t spc)
{
	obj->enabled = (enabled < 0)?obj->enabled:enabled;
	return ldmsd_result_new(0, NULL, NULL);
}

int daemonize_init(ldmsd_daemon_t obj)
{
	if (ldmsd_is_foreground())
		obj->obj.enabled = 0;
	else
		obj->obj.enabled = 1;
	return 0;
}

extern void cleanup(int, const char *);
int startup_disable(ldmsd_cfgobj_t obj)
{
	cleanup(0, "");
	return 0;
}

int syntax_check_enable(ldmsd_cfgobj_t obj)
{
	ldmsd_daemon_t daemonize, startup;
	daemonize = (ldmsd_daemon_t)ldmsd_cfgobj_find("daemonize",
						LDMSD_CFGOBJ_DAEMON);
	startup = (ldmsd_daemon_t)ldmsd_cfgobj_find("startup",
						LDMSD_CFGOBJ_DAEMON);
	daemonize->obj.enabled = 0;
	startup->obj.enabled = 0;
	ldmsd_log(LDMSD_LINFO, "----- Configuration File Syntax Check -----\n");
	return 0;
}

json_entity_t syntax_check_update(ldmsd_cfgobj_t obj, short enabled,
				json_entity_t dft, json_entity_t spc)
{
	obj->enabled = (enabled < 0)?obj->enabled:enabled;
	return ldmsd_result_new(0, NULL, NULL);
}

int set_memory_enable(ldmsd_cfgobj_t obj)
{
	ldmsd_daemon_t mem = (ldmsd_daemon_t)obj;
	const char *size = json_value_str(json_value_find(mem->attr, "size"))->str;
	size_t mem_sz = ovis_get_mem_size(size);
	if (ldms_init(mem_sz)) {
		ldmsd_log(LDMSD_LCRITICAL, "LDMS could not pre-allocate "
				"the memory of size %s.\n", size);
		return ENOMEM;
	}
	return 0;
}

json_entity_t set_memory_update(ldmsd_cfgobj_t obj, short enabled, json_entity_t dft,
								json_entity_t spc)
{
	ldmsd_daemon_t set_mem = (ldmsd_daemon_t)obj;
	json_entity_t size = NULL;
	size_t mem_sz;
	char *sz_s;
	if (spc)
		size = json_value_find(spc, "size");
	if (!size && dft)
		size = json_value_find(dft, "size");
	if (size) {
		sz_s = json_value_str(size)->str;
		mem_sz = ovis_get_mem_size(sz_s);
		if (!mem_sz) {
			char msg[1024];
			snprintf(msg, 1024, "Invalid memory size %s.", sz_s);
			return ldmsd_result_new(EINVAL, msg, NULL);
		}
		set_mem->attr = json_dict_build(set_mem->attr, JSON_STRING_VALUE,
								"size", sz_s, -1);
		free(ldmsd_set_mem_sz);
		ldmsd_set_mem_sz = strdup(sz_s);
		if (!ldmsd_set_mem_sz) {
			errno = ENOMEM;
			return NULL;
		}
	}
	obj->enabled = (0 <= enabled)?enabled:obj->enabled;
	return ldmsd_result_new(0, NULL, NULL);
}

int set_memory_init(ldmsd_daemon_t obj)
{
	char *sz;
	sz = getenv(LDMSD_MEM_SIZE_ENV);
	if (!sz)
		sz = LDMSD_MEM_SIZE_DEFAULT;
	/*
	 * Store as string instead of int because the main purpose of this is for query
	 * and the string is convert to int at most 2 times:
	 * 1 verify whether the string is valid or not
	 * 2 calling ldms_init() when the cfgobj is enabled.
	 */
	obj->attr = json_dict_build(obj->attr, JSON_STRING_VALUE, "size", sz, -1);
	if (!obj->attr)
		return ENOMEM;
	ldmsd_set_mem_sz = strdup(sz);
	if (!ldmsd_set_mem_sz)
		return ENOMEM;
	return 0;
}

extern int worker_threads_create(int worker_count);
int worker_count_enable(ldmsd_cfgobj_t obj)
{
	ldmsd_daemon_t count = (ldmsd_daemon_t)obj;
	json_entity_t cnt = json_value_find(count->attr, "count");
	return worker_threads_create(json_value_int(cnt));
}

json_entity_t worker_count_update(ldmsd_cfgobj_t obj, short enabled, json_entity_t dft,
								json_entity_t spc)
{
	json_entity_t count = NULL;
	int rc;

	ldmsd_daemon_t worker_count = (ldmsd_daemon_t)obj;
	if (spc)
		count = json_value_find(spc, "count");
	if (!count && dft)
		count = json_value_find(spc, "count");
	if (count) {
		rc = json_attr_mod(worker_count->attr, "count", json_value_int(count));
		if (rc)
			return ldmsd_result_new(rc, NULL, NULL);
		ldmsd_worker_count = json_value_int(count);
	}
	return ldmsd_result_new(0, NULL, NULL);
}

int worker_count_init(ldmsd_daemon_t obj)
{
	obj->attr = json_dict_build(NULL, JSON_INT_VALUE, "count",
					LDMSD_EV_THREAD_CNT_DEFAULT, -1);
	if (!obj->attr)
		return ENOMEM;
	ldmsd_worker_count = LDMSD_EV_THREAD_CNT_DEFAULT;
	return 0;
}

extern int publish_kernel(const char *setfile);
int kernel_enable(ldmsd_cfgobj_t obj)
{
	char *file;
	ldmsd_daemon_t kernel = (ldmsd_daemon_t)obj;
	file = json_value_str(json_value_find(kernel->attr, "path"))->str;
	return publish_kernel(file);
}

json_entity_t kernel_update(ldmsd_cfgobj_t obj, short enabled, json_entity_t dft,
								json_entity_t spc)
{
	int rc;
	json_entity_t path = NULL;
	ldmsd_daemon_t kernel = (ldmsd_daemon_t)obj;

	if (spc)
		path = json_value_find(spc, "path");
	if (!path && dft)
		path = json_value_find(dft, "path");
	if (path) {
		rc = json_attr_mod(kernel->attr, "path", json_value_str(path)->str);
		if (rc) {
			ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
			return NULL;
		}
	}
	obj->enabled = 1;
	return ldmsd_result_new(0, NULL, NULL);
}

int kernel_init(ldmsd_daemon_t obj)
{
	obj->attr = json_dict_build(NULL, JSON_STRING_VALUE, "path", LDMSD_SETFILE, -1);
	if (!obj->attr)
		return ENOMEM;
	return 0;
}

int global_auth_disable(ldmsd_cfgobj_t obj)
{
	ldmsd_auth_t auth;

	auth = ldmsd_auth_new(global_auth_domain, "none", NULL,
				geteuid(), getegid(), 0770, 1);
	if (!auth)
		return ENOMEM;
	return 0;
}

json_entity_t global_auth_update(ldmsd_cfgobj_t obj, short enabled, json_entity_t dft,
								json_entity_t spc)
{
	int rc;
	json_entity_t name = NULL;
	ldmsd_daemon_t auth = (ldmsd_daemon_t)obj;
	if (spc)
		name = json_value_find(spc, "name");
	if (!name && dft)
		name = json_value_find(dft, "name");
	if (name) {
		rc = json_attr_mod(auth->attr, "name", json_value_str(name)->str);
		if (rc) {
			ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
			return NULL;
		}
	}
	obj->enabled = 1;
	global_auth_domain = json_value_str(json_value_find(auth->attr, "name"))->str;
	return ldmsd_result_new(0, NULL, NULL);
}

int global_auth_init(ldmsd_daemon_t obj)
{
	obj->attr = json_dict_build(NULL, JSON_STRING_VALUE, "name",
						LDMSD_DEFAULT_GLOBAL_AUTH, -1);
	if (!obj->attr)
		return ENOMEM;
	global_auth_domain = json_value_str(json_value_find(obj->attr, "name"))->str;
	return 0;
}

extern char *progname;
extern int handle_pidfile_banner(ldmsd_daemon_t pf);
int pidfile_enable(ldmsd_cfgobj_t obj)
{
	return handle_pidfile_banner((ldmsd_daemon_t)obj);
}

json_entity_t pidfile_update(ldmsd_cfgobj_t obj, short enabled, json_entity_t dft,
								json_entity_t spc)
{
	int rc;
	json_entity_t path, banner;
	path = banner = NULL;
	ldmsd_daemon_t pidfile = (ldmsd_daemon_t)obj;
	if (spc) {
		path = json_value_find(spc, "path");
		banner = json_value_find(spc, "banner");
	}

	if (dft) {
		if (!path)
			path = json_value_find(dft, "path");
		if (!banner)
			banner = json_value_find(dft, "banner");
	}

	if (path) {
		rc = json_attr_mod(pidfile->attr, "path", json_value_str(path)->str);
		if (rc)
			goto oom;
	}
	if (banner) {
		rc = json_attr_mod(pidfile->attr, "banner", json_value_bool(banner));
		if (rc)
			goto oom;
	}

	return ldmsd_result_new(0, NULL, NULL);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}

int pidfile_init(ldmsd_daemon_t obj)
{
	char *env_path, *path, *progname, *base;

	env_path= getenv("LDMSD_PIDFILE");
	if (!env_path) {
		progname = strdup(ldmsd_progname_get());
		if (!progname)
			goto oom;
		base = basename(progname);
		path = malloc(strlen(LDMSD_PIDFILE_FMT) + strlen(base) + 1);
		if (!path)
			goto oom;
		sprintf(path, LDMSD_PIDFILE_FMT, base);
		free(progname);
	} else {
		path = strdup(env_path);
		if (!path)
			goto oom;
	}

	obj->attr = json_dict_build(NULL,
			JSON_STRING_VALUE, "path", path,
			JSON_BOOL_VALUE, "banner", LDMSD_BANNER_DEFAULT,
			-1);
	free(path);
	if (!obj->attr)
		goto oom;
	return 0;
oom:
	return ENOMEM;
}

json_entity_t ldmsd_id_update(ldmsd_cfgobj_t obj, short enabled, json_entity_t dft,
								json_entity_t spc)
{
	int rc;
	json_entity_t name = NULL;
	ldmsd_daemon_t ldmsd_id = (ldmsd_daemon_t)obj;

	if (spc)
		name = json_value_find(spc, "name");
	if (!name && dft)
		name = json_value_find(dft, "name");
	if (name) {
		rc = json_attr_mod(ldmsd_id->attr, "name", json_value_str(name)->str);
		if (rc)
			goto oom;
	}
	free(ldmsd_myname);
	ldmsd_myname = strdup(json_value_str(name)->str);
	if (!ldmsd_myname)
		goto oom;
	/* the enabled flag cannot be changed */
	return ldmsd_result_new(0, NULL, NULL);
oom:
	errno = ENOMEM;
	return NULL;
}

int ldmsd_id_init(ldmsd_daemon_t obj)
{
	char hostname[HOST_NAME_MAX + 1];
	int rc;
	rc = gethostname(hostname, sizeof(hostname));
	if (rc) {
		ldmsd_log(LDMSD_LCRITICAL, "Error %d: failed to get hostname.\n", rc);
		return rc;
	}
	obj->attr = json_dict_build(NULL, JSON_STRING_VALUE, "name", hostname, -1);
	if (!obj->attr)
		return ENOMEM;
	ldmsd_myname = strdup(hostname);
	if (!ldmsd_myname)
		return ENOMEM;
	return 0;
}

json_entity_t
daemon_generic_update(ldmsd_cfgobj_t obj, short enabled, json_entity_t dft,
							json_entity_t spc)
{
	obj->enabled = (enabled != -1)?enabled:obj->enabled;
	return ldmsd_result_new(0, NULL, NULL);
}

int daemon_generic_init(ldmsd_daemon_t obj)
{
	/* only enabled flag is used */
	return 0;
}

typedef int (*daemon_init_fn_t)(ldmsd_daemon_t obj);
struct daemon_obj_entry {
	const char *name;
	short enabled;
	daemon_init_fn_t init;
	ldmsd_cfgobj_update_fn_t update;
	ldmsd_cfgobj_enable_fn_t enable;
	ldmsd_cfgobj_disable_fn_t disable;
};

int daemon_obj_entry_cmp(const void *a, const void *b)
{
	struct daemon_obj_entry *a_ = (struct daemon_obj_entry *)a;
	struct daemon_obj_entry *b_ = (struct daemon_obj_entry *)b;
	return strcmp(a_->name, b_->name);
}

static struct daemon_obj_entry handler_tbl[] = {
	{ "daemonize",		1,			daemonize_init,
	  daemonize_update,	daemonize_enable,	daemonize_disable
	},
	{ "global_auth",	0,			global_auth_init,
	  global_auth_update,	NULL,			global_auth_disable
	},
	{ "kernel",		0,			kernel_init,
	  kernel_update,	kernel_enable,		NULL
	},
	{ "ldmsd-id",		1,			ldmsd_id_init,
	  ldmsd_id_update,	NULL,			NULL
	},
	{ "log",		0,			log_init,
	  log_update,		NULL,			NULL
	},
	{ "pid_file",		1,			pidfile_init,
	  pidfile_update,	pidfile_enable,		NULL
	},
	{ "set_memory",		1,			set_memory_init,
	  set_memory_update,	set_memory_enable,	NULL
	},
	{ "startup",		1,			daemon_generic_init,
	  daemon_generic_update,	NULL,		startup_disable
	},
	{ "syntax_check",	0,			daemon_generic_init,
	  syntax_check_update,	syntax_check_enable,	NULL
	},
	{ "workers",		1,			worker_count_init,
	  worker_count_update,	worker_count_enable,	NULL
	},
};

json_entity_t ldmsd_daemon_query(ldmsd_cfgobj_t obj)
{
	json_entity_t query;
	int rc;
	ldmsd_daemon_t daemon = (ldmsd_daemon_t)obj;
	query = ldmsd_cfgobj_query_result_new(obj);
	if (!query)
		goto oom;
	rc = json_dict_merge(query, daemon->attr);
	if (rc)
		goto oom;
	return ldmsd_result_new(0, NULL, query);
oom:
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}

int ldmsd_daemon_create(const char *name)
{
	int rc;
	struct daemon_obj_entry *ent;
	ldmsd_daemon_t obj;
	uid_t proc_uid = geteuid();
	gid_t proc_gid = getegid();

	ent = bsearch(&name, handler_tbl, ARRAY_SIZE(handler_tbl),
				sizeof(*ent), daemon_obj_entry_cmp);
	if (!ent)
		return ENOENT;

	obj = (ldmsd_daemon_t)ldmsd_cfgobj_new(ent->name, LDMSD_CFGOBJ_DAEMON,
					sizeof(*obj), ldmsd_daemon___del,
					ent->update, NULL,
					ldmsd_daemon_query,
					ldmsd_daemon_query,
					ent->enable,
					ent->disable,
					proc_uid, proc_gid,
					0500, ent->enabled);
	if (!obj)
		goto oom;
	obj->attr = json_dict_build(NULL, JSON_STRING_VALUE, "name", name, -1);
	if (!obj->attr)
		goto oom;
	rc = ent->init(obj);
	if (rc)
		goto err;

	ldmsd_cfgobj_unlock(&obj->obj);
	return 0;
oom:
	rc = ENOMEM;
err:
	ldmsd_cfgobj_unlock(&obj->obj);
	ldmsd_cfgobj_put(&obj->obj);
	return rc;
}

int ldmsd_daemon_create_cfgobjs()
{
	int i, rc, cnt;
	struct daemon_obj_entry *entry;

	cnt = ARRAY_SIZE(handler_tbl);

	for (i = 0; i < cnt; i++) {
		entry = &handler_tbl[i];
		rc = ldmsd_daemon_create(entry->name);
		if (rc)
			return rc;
	}
	return 0;
}

struct order_map_entry {
	char *key;
	char *name;
};

int cfgobj_entry_cmp(void *a_, void *b_)
{
	struct order_map_entry *a = (struct order_map_entry *)a_;
	struct order_map_entry *b = (struct order_map_entry *)b_;
	return strcmp(a->key, b->key);
}

/*
 * The numbers in the key fields are the starting order. The start order of the cfgobjs
 * with the same number does not matter.
 *
 * syntax_check and daemonize were taken care of beforehand.
 */
static struct order_map_entry order_tbl[] = {
		{ "0_ldmsd-id",		"ldmsd-id" },
		{ "1_syntax_check",	"syntax_check" },
		{ "2_daemonize",	"daemonize" },
		{ "1_log",		"log" },
		{ "2_pid_file",		"pid_file" },
		{ "3_set_memory",	"set_memory" },
		{ "4_startup",		"startup" },
		{ "5_auth",		"global_auth" },
		{ "5_kernel",		"kernel" },
		{ "5_workers",		"workers" },
		{ NULL },
};

int ldmsd_daemon_cfgobjs()
{
	struct order_map_entry *entry;
	ldmsd_cfgobj_t obj;
	int rc;
	int i = 0;

	do {
		entry = &order_tbl[i++];
		if (!entry->key)
			break;
		obj = ldmsd_cfgobj_find(entry->name, LDMSD_CFGOBJ_DAEMON);
		if (!obj) {
			/*
			 * This must not happen as the cfgobj were
			 * pre-created by LDMSD.
			 */
			assert(0);
		}
		if (obj->enabled && obj->enable) {
			rc = obj->enable(obj);
			if (rc) {
				ldmsd_log(LDMSD_LCRITICAL,
						"Failed to enable %s\n",
						entry->name);
				return rc;
			}
		} else if (!obj->enabled && obj->disable) {
			rc = obj->disable(obj);
			if (rc) {
				ldmsd_log(LDMSD_LCRITICAL,
						"Failed to disable %s\n",
						entry->name);
				return 0;
			}
		}
	} while (1);
	return 0;
}
