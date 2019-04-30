/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2018 Open Grid Computing, Inc. All rights reserved.
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

#include <unistd.h>
#include <inttypes.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/un.h>
#include <ctype.h>
#include <netdb.h>
#include <dlfcn.h>
#include <assert.h>
#include <libgen.h>
#include <glob.h>
#include <time.h>
#include <coll/rbt.h>
#include <coll/str_map.h>
#include <ovis_util/util.h>
#include <mmalloc/mmalloc.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "ldms_xprt.h"
#include "ldmsd_request.h"
#include "config.h"

static int cleanup_requested = 0;

extern void cleanup(int x, char *reason);

pthread_mutex_t host_list_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(host_list_s, hostspec) host_list;
LIST_HEAD(ldmsd_store_policy_list, ldmsd_store_policy) sp_list;
pthread_mutex_t sp_list_lock = PTHREAD_MUTEX_INITIALIZER;

#define LDMSD_PLUGIN_LIBPATH_MAX	1024
struct plugin_list plugin_list;

void ldmsd_cfg_unix_cleanup(ldmsd_cfg_xprt_t xprt)
{
	unlink(((struct sockaddr_un *)(&xprt->sock.ss))->sun_path);
}

void ldmsd_cfg_sock_cleanup(ldmsd_cfg_xprt_t xprt)
{
	/* nothing to do */
}

void ldmsd_cfg_ldms_xprt_cleanup(ldmsd_cfg_xprt_t xprt)
{
	/* nothing to do */
}

struct ldmsd_plugin_cfg *ldmsd_get_plugin(const char *name)
{
	struct ldmsd_plugin_cfg *p;
	LIST_FOREACH(p, &plugin_list, entry) {
		if (0 == strcmp(p->name, name))
			return p;
	}
	return NULL;
}

struct ldmsd_plugin_cfg *new_plugin(const char *plugin_name,
				char *errstr, size_t errlen)
{
	char library_name[LDMSD_PLUGIN_LIBPATH_MAX];
	char library_path[LDMSD_PLUGIN_LIBPATH_MAX];
	struct ldmsd_plugin *lpi;
	struct ldmsd_plugin_cfg *pi = NULL;
	char *pathdir = library_path;
	char *libpath;
	char *saveptr = NULL;
	char *path = getenv("LDMSD_PLUGIN_LIBPATH");
	void *d = NULL;

	if (!path)
		path = LDMSD_PLUGIN_LIBPATH_DEFAULT;

	strncpy(library_path, path, sizeof(library_path) - 1);

	while ((libpath = strtok_r(pathdir, ":", &saveptr)) != NULL) {
		ldmsd_log(LDMSD_LDEBUG, "Checking for %s in %s\n",
			plugin_name, libpath);
		pathdir = NULL;
		snprintf(library_name, sizeof(library_name), "%s/lib%s.so",
			libpath, plugin_name);
		d = dlopen(library_name, RTLD_NOW);
		if (d != NULL) {
			break;
		}
		struct stat buf;
		if (stat(library_name, &buf) == 0) {
			char *dlerr = dlerror();
			ldmsd_log(LDMSD_LERROR, "Bad plugin "
				"'%s': dlerror %s\n", plugin_name, dlerr);
			snprintf(errstr, errlen, "Bad plugin"
				" '%s'. dlerror %s", plugin_name, dlerr);
			goto err;
		}
	}

	if (!d) {
		char *dlerr = dlerror();
		ldmsd_log(LDMSD_LERROR, "Failed to load the plugin '%s': "
				"dlerror %s\n", plugin_name, dlerr);
		snprintf(errstr, errlen, "Failed to load the plugin '%s'. "
				"dlerror %s", plugin_name, dlerr);
		goto err;
	}

	ldmsd_plugin_get_f pget = dlsym(d, "get_plugin");
	if (!pget) {
		snprintf(errstr, errlen,
			"The library, '%s',  is missing the get_plugin() "
			 "function.", plugin_name);
		goto err;
	}
	lpi = pget(ldmsd_msg_logger);
	if (!lpi) {
		snprintf(errstr, errlen, "The plugin '%s' could not be loaded.",
								plugin_name);
		goto err;
	}
	pi = calloc(1, sizeof *pi);
	if (!pi)
		goto enomem;
	pthread_mutex_init(&pi->lock, NULL);
	pi->handle = d;
	pi->name = strdup(plugin_name);
	if (!pi->name)
		goto enomem;
	pi->libpath = strdup(library_name);
	if (!pi->libpath)
		goto enomem;
	pi->plugin = lpi;
	LIST_INSERT_HEAD(&plugin_list, pi, entry);
	return pi;
enomem:
	snprintf(errstr, errlen, "No memory");
err:
	if (pi) {
		pthread_mutex_destroy(&pi->lock);
		if (pi->name)
			free(pi->name);
		if (pi->libpath)
			free(pi->libpath);
		free(pi);
	}
	return NULL;
}

void destroy_plugin(struct ldmsd_plugin_cfg *p)
{
	free(p->libpath);
	free(p->name);
	LIST_REMOVE(p, entry);
	dlclose(p->handle);
	free(p);
}

const char *prdcr_state_str(enum ldmsd_prdcr_state state)
{
	switch (state) {
	case LDMSD_PRDCR_STATE_STOPPED:
		return "STOPPED";
	case LDMSD_PRDCR_STATE_DISCONNECTED:
		return "DISCONNECTED";
	case LDMSD_PRDCR_STATE_CONNECTING:
		return "CONNECTING";
	case LDMSD_PRDCR_STATE_CONNECTED:
		return "CONNECTED";
	case LDMSD_PRDCR_STATE_STOPPING:
		return "STOPPING";
	}
	return "BAD STATE";
}


const char *match_selector_str(enum ldmsd_name_match_sel sel)
{
	switch (sel) {
	case LDMSD_NAME_MATCH_INST_NAME:
		return "INST_NAME";
	case LDMSD_NAME_MATCH_SCHEMA_NAME:
		return "SCHEMA_NAME";
	}
	return "BAD SELECTOR";
}

int ldmsd_compile_regex(regex_t *regex, const char *regex_str,
				char *errbuf, size_t errsz)
{
	memset(regex, 0, sizeof *regex);
	int rc = regcomp(regex, regex_str, REG_EXTENDED | REG_NOSUB);
	if (rc) {
		snprintf(errbuf, errsz, "22");
		(void)regerror(rc,
			       regex,
			       &errbuf[2],
			       errsz - 2);
		strcat(errbuf, "\n");
	}
	return rc;
}

/*
 * Load a plugin
 */
int ldmsd_load_plugin(const char *inst_name, const char *plugin_name,
		      char *errstr, size_t errlen)
{
	ldmsd_plugin_inst_t inst = ldmsd_plugin_inst_load(inst_name,
							  plugin_name,
							  errstr,
							  errlen);
	if (!inst) {
		if (errno == EEXIST) {
			snprintf(errstr, errlen, "Plugin '%s' already loaded",
				 inst_name);
		} else {
			snprintf(errstr, errlen, "Plugin '%s' load error: %d",
				 inst_name, errno);
		}
		return errno;
	}
	return 0;
}

/*
 * Destroy and unload the plugin
 */
int ldmsd_term_plugin(const char *inst_name)
{
	ldmsd_plugin_inst_t inst = ldmsd_plugin_inst_find(inst_name);
	if (!inst)
		return ENOENT;
	ldmsd_plugin_inst_del(inst);
	ldmsd_plugin_inst_put(inst); /* put ref from find */
	return 0;
}

int _ldmsd_set_udata(ldms_set_t set, char *metric_name, uint64_t udata,
						char err_str[LEN_ERRSTR])
{
	int i = ldms_metric_by_name(set, metric_name);
	if (i < 0) {
		snprintf(err_str, LEN_ERRSTR, "Metric '%s' not found.",
			 metric_name);
		return ENOENT;
	}

	ldms_metric_user_data_set(set, i, udata);
	return 0;
}

int ldmsd_set_access_check(ldms_set_t set, int acc, ldmsd_sec_ctxt_t ctxt)
{
	uid_t uid;
	gid_t gid;
	int perm;

	uid = ldms_set_uid_get(set);
	gid = ldms_set_gid_get(set);
	perm = ldms_set_perm_get(set);

	return ovis_access_check(ctxt->crd.uid, ctxt->crd.gid, acc,
				 uid, gid, perm);
}

/*
 * Assign user data to a metric
 */
int ldmsd_set_udata(const char *set_name, const char *metric_name,
		    const char *udata_s, ldmsd_sec_ctxt_t sctxt)
{
	ldms_set_t set;
	int rc = 0;

	set = ldms_set_by_name(set_name);
	if (!set) {
		rc = ENOENT;
		goto out;
	}

	rc = ldmsd_set_access_check(set, 0222, sctxt);
	if (rc)
		goto out;

	char *endptr;
	uint64_t udata = strtoull(udata_s, &endptr, 0);
	if (endptr[0] != '\0') {
		rc = EINVAL;
		goto out;
	}

	int mid = ldms_metric_by_name(set, metric_name);
	if (mid < 0) {
		rc = -ENOENT;
		goto out;
	}

	ldms_metric_user_data_set(set, mid, udata);

out:
	if (set)
		ldms_set_put(set);
	return rc;
}

int ldmsd_set_udata_regex(char *set_name, char *regex_str,
		char *base_s, char *inc_s, char *errstr, size_t errsz,
		ldmsd_sec_ctxt_t sctxt)
{
	int rc = 0;
	ldms_set_t set;
	set = ldms_set_by_name(set_name);
	if (!set) {
		snprintf(errstr, errsz, "Set '%s' not found.", set_name);
		rc = ENOENT;
		goto out;
	}

	rc = ldmsd_set_access_check(set, 0222, sctxt);
	if (rc) {
		snprintf(errstr, errsz, "Permission denied.");
		goto out;
	}

	char *endptr;
	uint64_t base = strtoull(base_s, &endptr, 0);
	if (endptr[0] != '\0') {
		snprintf(errstr, errsz, "User data base '%s' invalid.",
								base_s);
		rc = EINVAL;
		goto out;
	}

	int inc = 0;
	if (inc_s)
		inc = atoi(inc_s);

	regex_t regex;
	rc = ldmsd_compile_regex(&regex, regex_str, errstr, errsz);
	if (rc)
		goto out;

	int i;
	uint64_t udata = base;
	char *mname;
	for (i = 0; i < ldms_set_card_get(set); i++) {
		mname = (char *)ldms_metric_name_get(set, i);
		if (0 == regexec(&regex, mname, 0, NULL, 0)) {
			ldms_metric_user_data_set(set, i, udata);
			udata += inc;
		}
	}
	regfree(&regex);

out:
	if (set)
		ldms_set_put(set);
	return rc;
}

void ldmsd_exit_daemon()
{
	cleanup_requested = 1;
	ldmsd_log(LDMSD_LINFO, "User requested exit.\n");
}

static int log_response_fn(ldmsd_cfg_xprt_t xprt, char *data, size_t data_len)
{
	ldmsd_req_attr_t attr;
	ldmsd_req_hdr_t req_reply = (ldmsd_req_hdr_t)data;
	ldmsd_ntoh_req_msg(req_reply);

	attr = ldmsd_first_attr(req_reply);

	/* We don't dump attributes to the log */
	ldmsd_log(LDMSD_LDEBUG, "msg_no %d flags %x rec_len %d rsp_err %d\n",
		  req_reply->msg_no, req_reply->flags, req_reply->rec_len,
		  req_reply->rsp_err);

	if (req_reply->rsp_err && (attr->attr_id == LDMSD_ATTR_STRING)) {
		/* Print the error message to the log */
		ldmsd_log(LDMSD_LERROR, "msg_no %d: error %d: %s\n",
				req_reply->msg_no, req_reply->rsp_err, attr->attr_value);
	}
	xprt->rsp_err = req_reply->rsp_err;
	return 0;
}

/* find # standing alone in a line, indicating rest of line is comment.
 * e.g. ^# rest is comment
 * or: ^         # indented comment
 * or: ^dosomething foo a=b c=d #rest is comment
 * or: ^dosomething foo a=b c=d # rest is comment
 * but not: ^dosomething foo a=#channel c=foo#disk1"
 * \return pointer of first # comment or NULL.
 */
char *find_comment(const char *line)
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

/*
 * rc = 0, filter applied OK
 * rc > 0, rc == -errno, error
 * rc = -1, filter not applied (but not an error)
 */
int __req_filter(ldmsd_req_ctxt_t reqc, void *ctxt)
{
	int rc = 0;

	switch (reqc->req_id) {
	case LDMSD_FAILOVER_START_REQ:
		ldmsd_use_failover = 1;
		break;
	case LDMSD_PLUGN_CONFIG_REQ:
	case LDMSD_PRDCR_START_REGEX_REQ:
	case LDMSD_PRDCR_START_REQ:
	case LDMSD_UPDTR_START_REQ:
	case LDMSD_STRGP_START_REQ:
	case LDMSD_SMPLR_START_REQ:
		reqc->flags |= LDMSD_REQ_DEFER_FLAG;
		break;
	default:
		break;
	}
	return rc;
}

int process_config_file(const char *path, int *lno, int trust)
{
	static uint32_t msg_no = 0;
	int rc = 0;
	int lineno = 0;
	int i;
	FILE *fin = NULL;
	char *buff = NULL;
	char *line = NULL;
	char *tmp;
	size_t line_sz = 0;
	char *comment;
	ssize_t off = 0;
	ssize_t cnt;
	size_t buf_len = 0;
	struct ldmsd_cfg_xprt_s xprt;
	ldmsd_req_hdr_t request;
	struct ldmsd_req_array *req_array = NULL;
	ldmsd_req_filter_fn req_filter_fn;

	if (ldmsd_is_initialized())
		req_filter_fn = NULL;
	else
		req_filter_fn = __req_filter;

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

	xprt.xprt = NULL;
	xprt.send_fn = log_response_fn;
	xprt.max_msg = LDMSD_CFG_FILE_XPRT_MAX_REC;
	xprt.trust = trust;
	xprt.rsp_err = 0;

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

	req_array = ldmsd_parse_config_str(line, msg_no, xprt.max_msg, ldmsd_log);
	if (!req_array) {
		rc = errno;
		ldmsd_log(LDMSD_LERROR, "Process config file error at line %d "
				"(%s). %s\n", lineno, path, strerror(rc));
		goto cleanup;
	}
	for (i = 0; i < req_array->num_reqs; i++) {
		request = req_array->reqs[i];
		rc = ldmsd_process_config_request(&xprt, request, req_filter_fn);
		if (rc || xprt.rsp_err) {
			if (!rc)
				rc = xprt.rsp_err;
			ldmsd_log(LDMSD_LERROR, "Configuration error at line %d (%s)\n",
					lineno, path);
			goto cleanup;
		}
	next_req:
		free(request);
	}
	msg_no += 1;

	off = 0;
	free(req_array);
	req_array = NULL;
	goto next_line;

cleanup:
	if (fin)
		fclose(fin);
	if (buff)
		free(buff);
	if (line)
		free(line);
	if (lno)
		*lno = lineno;
	if (req_array) {
		while (i < req_array->num_reqs) {
			free(req_array->reqs[i]);
			i++;
		}
		free(req_array);
	}
	return rc;
}

int __req_deferred_start_regex(ldmsd_req_hdr_t req, ldmsd_cfgobj_type_t type)
{
	regex_t regex = {0};
	ldmsd_req_attr_t attr;
	ldmsd_cfgobj_t obj;
	int rc;
	char *val;
	attr = ldmsd_req_attr_get_by_id((void*)req, LDMSD_ATTR_REGEX);
	if (!attr) {
		ldmsd_log(LDMSD_LERROR, "`regex` attribute is required.\n");
		return EINVAL;
	}
	val = str_repl_env_vars((char *)attr->attr_value);
	if (!val) {
		ldmsd_log(LDMSD_LERROR, "Not enough memory.\n");
		return ENOMEM;
	}
	rc = regcomp(&regex, val, REG_NOSUB);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Bad regex: %s\n", val);
		free(val);
		return EBADMSG;
	}
	free(val);
	ldmsd_cfg_lock(type);
	LDMSD_CFGOBJ_FOREACH(obj, type) {
		rc = regexec(&regex, obj->name, 0, NULL, 0);
		if (rc == 0) {
			obj->perm |= LDMSD_PERM_DSTART;
		}
	}
	ldmsd_cfg_unlock(type);
	return 0;
}

int __req_deferred_start(ldmsd_req_hdr_t req, ldmsd_cfgobj_type_t type)
{
	ldmsd_req_attr_t attr;
	ldmsd_cfgobj_t obj;
	char *name;
	attr = ldmsd_req_attr_get_by_id((void*)req, LDMSD_ATTR_NAME);
	if (!attr) {
		ldmsd_log(LDMSD_LERROR, "`name` attribute is required.\n");
		return EINVAL;
	}
	name = str_repl_env_vars((char *)attr->attr_value);
	if (!name) {
		ldmsd_log(LDMSD_LERROR, "Not enough memory.\n");
		return ENOMEM;
	}
	obj = ldmsd_cfgobj_find(name, type);
	if (!obj) {
		ldmsd_log(LDMSD_LERROR, "Config object not found: %s\n", name);
		free(name);
		return ENOENT;
	}
	free(name);
	obj->perm |= LDMSD_PERM_DSTART;
	ldmsd_cfgobj_put(obj);
	return 0;
}

/*
 * Start all cfgobjs for aggregators that `filter(obj) == 0`.
 */
int ldmsd_cfgobjs_start(int (*filter)(ldmsd_cfgobj_t))
{
	int rc = 0;
	ldmsd_cfgobj_t obj;
	struct ldmsd_sec_ctxt sctxt;

	ldmsd_sec_ctxt_get(&sctxt);

	ldmsd_cfg_lock(LDMSD_CFGOBJ_SMPLR);
	LDMSD_CFGOBJ_FOREACH(obj, LDMSD_CFGOBJ_SMPLR) {
		if (filter && filter(obj))
			continue;
		rc = __ldmsd_start_smplr((ldmsd_smplr_t)obj, 0);
		if (rc) {
			ldmsd_log(LDMSD_LERROR,
				"smplr_start failed, name %s, rc: %d\n",
				obj->name, rc);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_SMPLR);
			goto out;
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_SMPLR);

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	LDMSD_CFGOBJ_FOREACH(obj, LDMSD_CFGOBJ_PRDCR) {
		if (filter && filter(obj))
			continue;
		rc = __ldmsd_prdcr_start((ldmsd_prdcr_t)obj, &sctxt);
		if (rc) {
			ldmsd_log(LDMSD_LERROR,
				  "prdcr_start failed, name: %s, rc: %d\n",
				  obj->name, rc);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
			goto out;
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);

	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	LDMSD_CFGOBJ_FOREACH(obj, LDMSD_CFGOBJ_UPDTR) {
		if (filter && filter(obj))
			continue;
		rc = __ldmsd_updtr_start((ldmsd_updtr_t)obj, &sctxt);
		if (rc) {
			ldmsd_log(LDMSD_LERROR,
				  "updtr_start failed, name: %s, rc: %d\n",
				  obj->name, rc);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
			goto out;
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);

	ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
	LDMSD_CFGOBJ_FOREACH(obj, LDMSD_CFGOBJ_STRGP) {
		if (filter && filter(obj))
			continue;
		rc = __ldmsd_strgp_start((ldmsd_strgp_t)obj, &sctxt);
		if (rc) {
			ldmsd_log(LDMSD_LERROR,
				  "strgp_start failed, name: %s, rc: %d\n",
				  obj->name, rc);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
			goto out;
		}
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);

out:
	return rc;
}

int __our_cfgobj_filter(ldmsd_cfgobj_t obj)
{
	if (!cfgobj_is_failover(obj) && (obj->perm & LDMSD_PERM_DSTART))
		return 0;
	return -1;
}

/*
 * ldmsd config start prodcedure
 */
int ldmsd_ourcfg_start_proc()
{
	int rc;
	rc = ldmsd_cfgobjs_start(__our_cfgobj_filter);
	if (rc) {
		exit(100);
	}
	return 0;
}

static inline void __log_sent_req(ldmsd_cfg_xprt_t xprt, ldmsd_req_hdr_t req)
{
	if (!ldmsd_req_debug) /* defined in ldmsd_request.c */
		return;
	/* req is in network byte order */
	struct ldmsd_req_hdr_s hdr;
	hdr.marker = htonl(req->marker);
	hdr.type = htonl(req->type);
	hdr.flags = ntohl(req->flags);
	hdr.msg_no = ntohl(req->msg_no);
	hdr.req_id = ntohl(req->req_id);
	hdr.rec_len = ntohl(req->rec_len);
	switch (hdr.type) {
	case LDMSD_REQ_TYPE_CONFIG_CMD:
		ldmsd_lall("sending %s msg_no: %d:%lu, flags: %#o, "
			   "rec_len: %u\n",
			   ldmsd_req_id2str(hdr.req_id),
			   hdr.msg_no, (uint64_t)xprt->xprt,
			   hdr.flags, hdr.rec_len);
		break;
	case LDMSD_REQ_TYPE_CONFIG_RESP:
		ldmsd_lall("sending RESP msg_no: %d, rsp_err: %d, flags: %#o, "
			   "rec_len: %u\n",
			   hdr.msg_no,
			   hdr.rsp_err, hdr.flags, hdr.rec_len);
		break;
	default:
		ldmsd_lall("sending BAD REQUEST\n");
	}
}

static int send_ldms_fn(ldmsd_cfg_xprt_t xprt, char *data, size_t data_len)
{
	__log_sent_req(xprt, (void*)data);
	return ldms_xprt_send(xprt->ldms.ldms, data, data_len);
}

void ldmsd_recv_msg(ldms_t x, char *data, size_t data_len)
{
	ldmsd_req_hdr_t request = (ldmsd_req_hdr_t)data;
	struct ldmsd_cfg_xprt_s xprt;
	xprt.ldms.ldms = x;
	xprt.send_fn = send_ldms_fn;
	xprt.max_msg = ldms_xprt_msg_max(x);
	xprt.trust = 0; /* don't trust any network for CMD expansion */

	if (ntohl(request->rec_len) > xprt.max_msg) {
		/* Send the record length advice */
		ldmsd_send_cfg_rec_adv(&xprt, ntohl(request->msg_no), xprt.max_msg);
		return;
	}

	switch (ntohl(request->type)) {
	case LDMSD_REQ_TYPE_CONFIG_CMD:
		(void)ldmsd_process_config_request(&xprt, request, NULL);
		break;
	case LDMSD_REQ_TYPE_CONFIG_RESP:
		(void)ldmsd_process_config_response(&xprt, request);
		break;
	default:
		break;
	}
}

static void __listen_connect_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_ERROR:
		ldms_xprt_put(x);
		break;
	case LDMS_XPRT_EVENT_RECV:
		ldmsd_recv_msg(x, e->data, e->data_len);
		break;
	default:
		assert(0);
		break;
	}
}

int listen_on_ldms_xprt(ldmsd_listen_t listen)
{
	unsigned short port_no;
	int ret;
	struct sockaddr_in sin;

	listen->x = ldms_xprt_new_with_auth(listen->xprt, ldmsd_linfo,
			ldmsd_auth_name_get(listen), ldmsd_auth_attr_get(listen));
	if (!listen->x) {
		ldmsd_log(LDMSD_LERROR,
			  "'%s' transport creation with auth '%s' "
			  "failed, error: %s(%d). Please check transpot "
			  "configuration, authentication configuration, "
			  "ZAP_LIBPATH (env var), and LD_LIBRARY_PATH.\n",
			  listen->xprt,
			  ldmsd_auth_name_get(listen),
			  ovis_errno_abbvr(errno),
			  errno);
		return 6; /* legacy error code */
	}
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = 0;
	sin.sin_port = htons(listen->port_no);
	ret = ldms_xprt_listen(listen->x, (struct sockaddr *)&sin, sizeof(sin),
			       __listen_connect_cb, NULL);
	if (ret) {
		ldmsd_log(LDMSD_LERROR, "Error %d listening on the '%s' "
				"transport.\n", ret, listen->xprt);
		return 7; /* legacy error code */
	}
	ldmsd_log(LDMSD_LINFO, "Listening on transport %s:%hu\n",
			listen->xprt, listen->port_no);
	return 0;
}

void ldmsd_cfg_ldms_init(ldmsd_cfg_xprt_t xprt, ldms_t ldms)
{
	ldms_xprt_get(ldms);
	xprt->ldms.ldms = ldms;
	xprt->send_fn = send_ldms_fn;
	xprt->max_msg = ldms_xprt_msg_max(ldms);
	xprt->cleanup_fn = ldmsd_cfg_ldms_xprt_cleanup;
	xprt->trust = 0;
}

void ldmsd_mm_status(enum ldmsd_loglevel level, const char *prefix)
{
	struct mm_stat s;
	mm_stats(&s);
	/* compute bound based on current usage */
	size_t used = s.size - s.grain*s.largest;
	ldmsd_log(level, "%s: mm_stat: size=%zu grain=%zu chunks_free=%zu grains_free=%zu grains_largest=%zu grains_smallest=%zu bytes_free=%zu bytes_largest=%zu bytes_smallest=%zu bytes_used+holes=%zu\n",
	prefix,
	s.size, s.grain, s.chunks, s.bytes, s.largest, s.smallest,
	s.grain*s.bytes, s.grain*s.largest, s.grain*s.smallest, used);
}

const char * blacklist[] = {
	"libtsampler.so",
	"libtimer_base.so",
	"liblustre_sampler.so",
	"libzap.so",
	"libzap_rdma.so",
	"libzap_sock.so",
	NULL
};

#define APP "ldmsd"

static int ldmsd_plugins_usage_dir(const char *dir, const char *plugname);

/* Dump plugin names and usages (where available) before ldmsd redirects
 * io. Loads and terms all plugins, which provides a modest check on some
 * coding and deployment issues.
 * \param plugname: list usage only for plugname. If NULL, list all plugins.
 */
int ldmsd_plugins_usage(const char *plugname)
{
	char library_path[LDMSD_PLUGIN_LIBPATH_MAX];
	char *pathdir = library_path;
	char *libpath;
	char *saveptr = NULL;

	if (0 == strcasecmp(plugname, "all"))
		plugname = NULL;

	char *path = getenv("LDMSD_PLUGIN_LIBPATH");
	if (!path)
		path = PLUGINDIR;

	if (! path ) {
		fprintf(stderr, "%s: need plugin path input.\n", APP);
		fprintf(stderr, "Did not find env(LDMSD_PLUGIN_LIBPATH).\n");
		return EINVAL;
	}
	strncpy(library_path, path, sizeof(library_path) - 1);

	int trc=0, rc = 0;
	while ((libpath = strtok_r(pathdir, ":", &saveptr)) != NULL) {
		pathdir = NULL;
		trc = ldmsd_plugins_usage_dir(libpath, plugname);
		if (trc)
			rc = trc;
	}
	return rc;
}

static int ldmsd_plugins_usage_dir(const char *path, const char *plugname)
{
	assert( path || "null dir name in ldmsd_plugins_usage" == NULL);
	struct stat buf;
	const char *type_name = NULL;
	glob_t pglob;

	if (stat(path, &buf) < 0) {
		int err = errno;
		fprintf(stderr, "%s: unable to stat library path %s (%d).\n",
			APP, path, err);
		return err;
	}

	int rc = 0;
	bool matchtype = false;
	if (plugname && strcmp(plugname,"store") == 0) {
		matchtype = true;
		type_name = plugname;
		plugname = NULL;
	}
	if (plugname && strcmp(plugname,"sampler") == 0) {
		matchtype = true;
		type_name = plugname;
		plugname = NULL;
	}


	const char *match1 = "/lib";
	const char *match2 = ".so";
	size_t patsz = strlen(path) + strlen(match1) + strlen(match2) + 2;
	if (plugname) {
		patsz += strlen(plugname);
	}
	char *pat = malloc(patsz);
	if (!pat) {
		fprintf(stderr, "%s: out of memory?!\n", APP);
		rc = ENOMEM;
		goto out;
	}
	snprintf(pat, patsz, "%s%s%s%s", path, match1,
		(plugname ? plugname : "*"), match2);
	int flags = GLOB_ERR |  GLOB_TILDE | GLOB_TILDE_CHECK;

	int err = glob(pat, flags, NULL, &pglob);
	switch(err) {
	case 0:
		break;
	case GLOB_NOSPACE:
		fprintf(stderr, "%s: out of memory!?\n", APP);
		rc = ENOMEM;
		break;
	case GLOB_ABORTED:
		fprintf(stderr, "%s: error reading %s\n", APP, path);
		rc = 1;
		break;
	case GLOB_NOMATCH:
		fprintf(stderr, "%s: no libraries in %s for %s\n", APP, path, pat);
		rc = 1;
		break;
	default:
		fprintf(stderr, "%s: unexpected glob error for %s\n", APP, path);
		rc = 1;
		break;
	}
	if (err)
		goto out2;

	size_t i = 0;
	if (pglob.gl_pathc > 0) {
		printf("LDMSD plugins in %s : \n", path);
	}
	for ( ; i  < pglob.gl_pathc; i++) {
		ldmsd_plugin_inst_t inst = NULL;
		char * library_name = pglob.gl_pathv[i];
		char *tmp = strdup(library_name);
		if (!tmp) {
			rc = ENOMEM;
			goto out2;
		} else {
			char *b = basename(tmp);
			int j = 0;
			int blacklisted = 0;
			while (blacklist[j]) {
				if (strcmp(blacklist[j], b) == 0) {
					blacklisted = 1;
					break;
				}
				j++;
			}
			if (blacklisted)
				goto next;
			/* strip lib prefix and .so suffix*/
			b+= 3;
			char *suff = rindex(b, '.');
			assert(suff != NULL || NULL == "plugin glob match means . will be found always");
			*suff = '\0';
			inst = ldmsd_plugin_inst_load(b, b, NULL, 0);
			if (!inst) {
				/* EINVAL suggests non-inst load */
				if (errno != EINVAL)
					fprintf(stderr, "Unable to load plugin %s\n", b);
				goto next;
			}

			if (matchtype && strcmp(type_name, inst->type_name))
				goto next;

			printf("======= %s %s:\n", inst->type_name, b);
			const char *u = ldmsd_plugin_inst_help(inst);
			printf("%s\n", u);
			printf("=========================\n");
 next:
			if (inst) {
				ldmsd_plugin_inst_del(inst);
				inst = NULL;
			}
			free(tmp);
		}

	}


 out2:
	globfree(&pglob);
	free(pat);
 out:
	return rc;
}
