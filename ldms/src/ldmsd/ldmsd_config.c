/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2019 Open Grid Computing, Inc. All rights reserved.
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
#include "ldmsd_request.h"
#include "config.h"

extern void cleanup(int x, char *reason);
/* Defined in ldmsd.c */
extern ovis_log_t config_log;

pthread_mutex_t host_list_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(host_list_s, hostspec) host_list;
pthread_mutex_t sp_list_lock = PTHREAD_MUTEX_INITIALIZER;

#define LDMSD_PLUGIN_LIBPATH_MAX	1024

void ldmsd_cfg_ldms_xprt_cleanup(ldmsd_cfg_xprt_t xprt)
{
	/* nothing to do */
}

void ldmsd_sampler_cleanup(ldmsd_cfgobj_sampler_t samp)
{
	return;
}

void ldmsd_store_cleanup(ldmsd_cfgobj_store_t store)
{
	return;
}

enum ldmsd_plugin_data_e {
	LDMSD_PLUGIN_DATA_CONTEXT_SIZE = 1,
};

ldmsd_plugin_t load_plugin(const char *cfg_name, const char *plugin_name,
			   char *errstr, size_t errlen)
{
	char library_name[LDMSD_PLUGIN_LIBPATH_MAX];
	char library_path[LDMSD_PLUGIN_LIBPATH_MAX];
	char *pathdir = library_path;
	char *libpath;
	char *saveptr = NULL;
	char *path = getenv("LDMSD_PLUGIN_LIBPATH");
	void *d = NULL;

	if (!path)
		path = LDMSD_PLUGIN_LIBPATH_DEFAULT;

	strncpy(library_path, path, sizeof(library_path) - 1);
	while ((libpath = strtok_r(pathdir, ":", &saveptr)) != NULL) {
		ovis_log(config_log, OVIS_LDEBUG,
			"Checking for '%s' in '%s'\n",
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
			snprintf(errstr, errlen,
				 "Error %d configuration named '%s' "
				 "with plugin '%s': bad plugin, dlerror: %s\n",
				 errno, cfg_name, plugin_name, dlerr);
			ovis_log(config_log, OVIS_LERROR, "%s", errstr);
			goto err_0;
		}
	}

	if (!d) {
		char *dlerr = dlerror();
		snprintf(errstr, errlen,
			 "Error %d configuration named '%s' "
			 "with plugin '%s': failed to load the plugin, "
			 "dlerror: %s\n",
			 errno, cfg_name, plugin_name, dlerr);
		ovis_log(config_log, OVIS_LERROR, "%s", errstr);
		goto err_0;
	}

	ldmsd_plugin_get_f pget;
	pget = dlsym(d, "get_plugin");
	if (!pget) {
		snprintf(errstr, errlen,
			 "Error %d configuration named '%s' with plugin '%s': "
			 "get_plugin() function is missing from the library.\n",
			 errno, cfg_name, plugin_name);
		goto err_0;
	}
	struct ldmsd_plugin *pi = pget();
	if (pi)
		pi->libpath = strdup(library_name);
	return pi;

err_0:
	if (d)
		dlclose(d);
	return NULL;
}

ldmsd_cfgobj_sampler_t
ldmsd_sampler_add(const char *cfg_name,
		  struct ldmsd_sampler *api,
		  ldmsd_cfgobj_del_fn_t __del,
		  uid_t uid, gid_t gid, int perm)
{
	ldmsd_cfgobj_sampler_t sampler;
	struct ldmsd_sampler *api_inst;
	api_inst = calloc(1, sizeof (*api_inst) + api->base.context_size);
	if (!api_inst)
		return NULL;
	memcpy(api_inst, api, sizeof(*api));
	if (api->base.context_size)
		api_inst->base.context = (void *)(api_inst + 1);
	sampler = (void*)ldmsd_cfgobj_new_with_auth(cfg_name, LDMSD_CFGOBJ_SAMPLER,
						sizeof(*sampler),
						__del, uid, gid, perm);
	if (!sampler) {
		ovis_log(config_log, OVIS_LERROR,
			"Error %d creating the sampler configuration object '%s'\n",
				errno, cfg_name);
		free(api_inst);
		return NULL;
	}
	sampler->api = api_inst;
	sampler->api->base.cfg_name = strdup(cfg_name);
	sampler->thread_id = -1;	/* stopped */
#ifdef _CFG_REF_DUMP_
	ref_dump(&sampler->cfg.ref, sampler->cfg.name, stderr);
#endif
	ldmsd_cfgobj_unlock(&sampler->cfg);
	return sampler;
}

ldmsd_cfgobj_store_t ldmsd_store_add(const char *cfg_name,
		struct ldmsd_store *api,
		ldmsd_cfgobj_del_fn_t __del,
		uid_t uid, gid_t gid, int perm)
{
	ldmsd_cfgobj_store_t store;
	struct ldmsd_store *api_inst;
	api_inst = calloc(1, sizeof (*api_inst) + api->base.context_size);
	if (!api_inst)
		return NULL;
	memcpy(api_inst, api, sizeof(*api));
	if (api->base.context_size)
		api_inst->base.context = (void *)(api_inst + 1);
	store = (void*)ldmsd_cfgobj_new_with_auth(cfg_name, LDMSD_CFGOBJ_STORE,
						sizeof(*store),
						__del, uid, gid, perm);
	if (!store) {
		ovis_log(config_log, OVIS_LERROR,
			"Error %d creating the store configuration object '%s'\n",
				errno, cfg_name);
		free(api_inst);
		return NULL;
	}
	store->api = api_inst;
	store->api->base.cfg_name = strdup(cfg_name);
	ldmsd_cfgobj_unlock(&store->cfg);
	return store;
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
	case LDMSD_PRDCR_STATE_STANDBY:
		return "STANDBY";
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

void ldmsd_sampler___del(ldmsd_cfgobj_t obj)
{
	ldmsd_cfgobj_sampler_t samp = (void*)obj;
	struct ldmsd_sampler_set *_set;

	while (( _set = LIST_FIRST(&samp->set_list))) {
		LIST_REMOVE(_set, entry);
		ldms_set_delete(_set->set);
		free(_set);
	}
	free((void*)samp->api->base.cfg_name);
	free(samp->api);
	ldmsd_cfgobj___del(obj);
}

void ldmsd_store___del(ldmsd_cfgobj_t obj)
{
	ldmsd_cfgobj_store_t store = (void*)obj;
	free((void*)store->api->base.cfg_name);
	free(store->api);
	ldmsd_cfgobj___del(obj);
}

/*
 * Load a plugin
 */
int ldmsd_load_plugin(char* cfg_name, char *plugin_name,
		      char *errstr, size_t errlen)
{
	struct ldmsd_plugin *api;
	if (!plugin_name || !cfg_name)
		return EINVAL;
	api = load_plugin(cfg_name, plugin_name, errstr, errlen);
	if (!api)
		goto err; /* load_plugin() already filled errstr */
	switch (api->type) {
	case LDMSD_PLUGIN_SAMPLER:
		if (NULL == ldmsd_sampler_add(cfg_name, (struct ldmsd_sampler *)api,
				ldmsd_sampler___del, geteuid(), getegid(), 0660)) {
			snprintf(errstr, errlen,
				 "Error %d adding sampler named '%s' "
				 "with plugin '%s'\n", errno, cfg_name,
				 plugin_name);
			goto err;
		}
		break;
	case LDMSD_PLUGIN_STORE:
		if (NULL == ldmsd_store_add(cfg_name, (struct ldmsd_store *)api,
				ldmsd_store___del, geteuid(), getegid(), 0660)) {
			snprintf(errstr, errlen,
				 "Error %d adding store named '%s' "
				 "with plugin '%s'\n", errno, cfg_name,
				 plugin_name);
			goto err;
		}
		break;
	default:
		errno = EINVAL;
		snprintf(errstr, errlen,
			 "Error %d, the '%s' plugin is not a valid plugin type.\n",
			 errno, plugin_name);
		ovis_log(config_log, OVIS_LERROR, "%s", errstr);
		goto err;
	}
	return 0;

 err:
	return errno;
}

/*
 * Destroy and unload the plugin
 */
int ldmsd_term_plugin(char *plugin_name)
{
	int rc = EINVAL;
	ldmsd_cfgobj_sampler_t sampler = NULL;
	ldmsd_cfgobj_store_t store = NULL;
	ldmsd_plugin_t plug = NULL;
	ldmsd_cfgobj_t cfgobj;

	sampler = ldmsd_sampler_find(plugin_name);
	if (sampler) {
		plug = &sampler->api->base;
		cfgobj = &sampler->cfg;
	} else {
		store = ldmsd_store_find(plugin_name);
		if (!store) {
			rc = ENOENT;
			goto out;
		}
		plug = &store->api->base;
		cfgobj = &store->cfg;
	}
	plug->term(plug);
	ldmsd_cfgobj_put(cfgobj, "find");
	rc = 0;
 out:
	return rc;
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

static int log_response_fn(void *_xprt, char *data, size_t data_len)
{
	ldmsd_req_attr_t attr;
	ldmsd_cfg_xprt_t xprt = (ldmsd_cfg_xprt_t)_xprt;
	ldmsd_req_hdr_t req_reply = (ldmsd_req_hdr_t)data;
	ldmsd_ntoh_req_msg(req_reply);

	attr = ldmsd_first_attr(req_reply);

	/* We don't dump attributes to the log */
	ovis_log(config_log, OVIS_LDEBUG, "msg_no %d flags %x rec_len %d rsp_err %d\n",
		  req_reply->msg_no, req_reply->flags, req_reply->rec_len,
		  req_reply->rsp_err);

	if (req_reply->rsp_err && (attr->attr_id == LDMSD_ATTR_STRING)) {
		/* Print the error message to the log */
		ovis_log(config_log, OVIS_LERROR, "msg_no %d: error %d: %s\n",
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

static ldmsd_req_hdr_t __aggregate_records(struct ldmsd_req_array *rec_array)
{
	ldmsd_req_hdr_t req, rec;
	char *buf;
	size_t buf_len, buf_off, hdr_sz, data_len;
	int i;

	hdr_sz = sizeof(*req);
	buf = malloc(LDMSD_CFG_FILE_XPRT_MAX_REC);
	if (!buf)
		goto oom;
	buf_len = LDMSD_CFG_FILE_XPRT_MAX_REC;
	buf_off = 0;

	for (i = 0; i < rec_array->num_reqs; i++) {
		rec = rec_array->reqs[i];
		if (0 == i) {
			memcpy(buf, rec, hdr_sz);
			buf_off = hdr_sz;
		}
		data_len = ntohl(rec->rec_len) - hdr_sz;
		while (buf_len - buf_off < data_len) {
			buf = realloc(buf, buf_len * 2 + data_len);
			if (!buf)
				goto oom;
			buf_len = buf_len * 2 + data_len;
		}
		memcpy(&buf[buf_off], (void *)(rec + 1), data_len);
		buf_off += data_len;
	}
	req = (ldmsd_req_hdr_t)buf;
	req->flags =htonl(LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F);
	req->rec_len = htonl(buf_off);
	return req;
oom:
	errno = ENOMEM;
	ovis_log(config_log, OVIS_LCRITICAL, "Out of memory\n");
	return NULL;
}

static uint64_t __get_cfgfile_id()
{
	static uint64_t id = 1;
	return __sync_fetch_and_add(&id, 1);
}

extern int is_req_id_priority(enum ldmsd_request req_id);
static
int __process_config_file(const char *path, int *lno, int trust,
				req_filter_fn req_filter, void *ctxt)
{
	int rc = 0;
	int lineno = 0;
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
	ldmsd_req_hdr_t request = NULL;
	struct ldmsd_req_array *req_array = NULL;
	if (!path)
		return EINVAL;
	line = malloc(LDMSD_CFG_FILE_XPRT_MAX_REC);
	if (!line) {
		rc = errno;
		ovis_log(config_log, OVIS_LERROR, "Out of memory\n");
		goto cleanup;
	}
	line_sz = LDMSD_CFG_FILE_XPRT_MAX_REC;

	fin = fopen(path, "rt");
	if (!fin) {
		rc = errno;
		ovis_log(config_log, OVIS_LERROR, "Failed to open the config file '%s'. %s\n",
				path, STRERROR(rc));
		goto cleanup;
	}

	xprt.type = LDMSD_CFG_TYPE_FILE;
	xprt.file.path = path;
	xprt.file.cfgfile_id = __get_cfgfile_id();
	xprt.send_fn = log_response_fn;
	xprt.max_msg = LDMSD_CFG_FILE_XPRT_MAX_REC;
	xprt.trust = trust;
	xprt.rsp_err = 0;
	xprt.cleanup_fn = NULL;

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
		char *nline = realloc(line, ((cnt + off)/line_sz + 1) * line_sz);
		if (!nline) {
			rc = errno;
			ovis_log(config_log, OVIS_LERROR, "Out of memory\n");
			goto cleanup;
		}
		line = nline;
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

	if (ldmsd_is_initialized()) {
		if ((0 == strncmp(line, "prdcr_add", 9)) ||
				(0 == strncmp(line, "prdcr_start", 11))) {
			if (strstr(line, "interval")) {
				ovis_log(config_log, OVIS_LWARN,
						"'interval' is begin deprecated. "
						"Please use 'reconnect' with 'prdcr_add' or 'prdcr_start*' "
						"in the future.\n");
			}
		}
	}

	/*
	 * The message number is the line number.
	 */
	req_array = ldmsd_parse_config_str(line, lineno, xprt.max_msg);
	if (!req_array) {
		rc = errno;
		ovis_log(config_log, OVIS_LERROR, "Process config file error at line %d "
				"(%s). %s\n", lineno, path, STRERROR(rc));
		goto cleanup;
	}

	request = __aggregate_records(req_array);
	if (!request) {
		rc = errno;
		goto cleanup;
	}
	ldmsd_req_array_free(req_array);
	req_array = NULL;

	if (!ldmsd_is_initialized()) {
		/* Process only the priority commands, e.g., cmd-line options */
		if (!is_req_id_priority(ntohl(request->req_id)))
			goto next_req;
	} else {
		/* Process non-priority commands, e.g., cfgobj config commands */
		if (is_req_id_priority(ntohl(request->req_id)))
			goto next_req;
	}

	/*
	 * Make sure that LDMSD will create large enough buffer to receive
	 * the config data.
	 */
	if (xprt.max_msg < ntohl(request->rec_len))
		xprt.max_msg = ntohl(request->rec_len);

	rc = ldmsd_process_config_request(&xprt, request, req_filter, ctxt);
	if (rc || xprt.rsp_err) {
		if (!rc)
			rc = xprt.rsp_err;
		ovis_log(config_log, OVIS_LERROR, "Configuration error at line %d (%s)\n",
				lineno, path);
		goto cleanup;
	}
next_req:
	free(request);
	request = NULL;
	off = 0;
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
	ldmsd_req_array_free(req_array);
	if (request)
		free(request);
	return rc;
}

static
int __process_config_str(char *cfg_str, int *lno, int trust,
				req_filter_fn req_filter, void *ctxt)
{
	int rc = 0;
	int lineno = 0;
	char *buff = NULL;
	char *line = NULL;
	char *tmp;
	size_t line_sz = 0;
	char *comment;
	ssize_t off = 0;
	ssize_t cnt;
	size_t buf_len = 0;
	struct ldmsd_cfg_xprt_s xprt;
	ldmsd_req_hdr_t request = NULL;
	struct ldmsd_req_array *req_array = NULL;
	if (!cfg_str)
		return EINVAL;
	line = malloc(LDMSD_CFG_FILE_XPRT_MAX_REC);
	if (!line) {
		rc = errno;
		ovis_log(config_log, OVIS_LERROR, "Out of memory\n");
		goto cleanup;
	}
	line_sz = LDMSD_CFG_FILE_XPRT_MAX_REC;
	xprt.type = LDMSD_CFG_TYPE_FILE;
	xprt.send_fn = log_response_fn;
	xprt.max_msg = LDMSD_CFG_FILE_XPRT_MAX_REC;
	xprt.trust = trust;
	xprt.rsp_err = 0;
	xprt.cleanup_fn = NULL;

next_line:
	errno = 0;
	if (buff) {
		memset(buff, 0, buf_len);
		buff = strtok(NULL, "\n");
	} else
		buff = strtok(cfg_str, "\n");
	if (!buff)
		goto cleanup;
	buf_len = sizeof(buff);
	cnt = strlen(buff);
	lineno++;
	tmp = buff;
	comment = find_comment(tmp);

	if (comment)
		*comment = '\0';

	while (cnt && isspace(tmp[cnt-1]))
		cnt --;

	if (!buff) {
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
		char *nline = realloc(line, ((cnt + off)/line_sz + 1) * line_sz);
		if (!nline) {
			rc = errno;
			ovis_log(config_log, OVIS_LERROR, "Out of memory\n");
			goto cleanup;
		}
		line = nline;
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

	if (ldmsd_is_initialized()) {
		if ((0 == strncmp(line, "prdcr_add", 9)) ||
				(0 == strncmp(line, "prdcr_start", 11))) {
			if (strstr(line, "interval")) {
				ovis_log(config_log, OVIS_LWARNING,
					"'interval' is being deprecated. "
					"Please use 'reconnect' with 'prdcr_add' or 'prdcr_start*' "
					"in the future.\n");
			}
		}
	}
	req_array = ldmsd_parse_config_str(line, lineno, xprt.max_msg);
	if (!req_array) {
		rc = errno;
		ovis_log(config_log, OVIS_LERROR, "Process config string error in line %d. %s\n ",
				lineno, STRERROR(rc));
		goto cleanup;
	}

	request = __aggregate_records(req_array);
	if (!request) {
		rc = errno;
		goto cleanup;
	}
	ldmsd_req_array_free(req_array);
	req_array = NULL;

	if (!ldmsd_is_initialized()) {
		/* Process only the priority commands, e.g., cmd-line options */
		if (!is_req_id_priority(ntohl(request->req_id)))
			goto next_req;
	} else {
		/* Process non-priority commands, e.g., cfgobj config commands */
		if (is_req_id_priority(ntohl(request->req_id)))
			goto next_req;
	}

	/*
	 * Make sure that LDMSD will create large enough buffer to receive
	 * the config data.
	 */
	if (xprt.max_msg < ntohl(request->rec_len))
		xprt.max_msg = ntohl(request->rec_len);

	rc = ldmsd_process_config_request(&xprt, request, req_filter, ctxt);
	if (rc || xprt.rsp_err) {
		if (!rc)
			rc = xprt.rsp_err;
		ovis_log(config_log, OVIS_LERROR, "Configuration error at line %d (%s)\n",
				lineno, cfg_str);
		goto cleanup;
	}

next_req:
	free(request);
	request = NULL;
	off = 0;
	goto next_line;

cleanup:
	if (line)
		free(line);
	if (lno)
		*lno = lineno;
	ldmsd_req_array_free(req_array);
	if (request)
		free(request);
	return rc;
}

char *__process_yaml_config_file(const char *path, const char *dname)
{
	FILE *fp;
	char command[512];
	size_t buf_sz = 4096;
	char *cfg_str = malloc(buf_sz);
	snprintf(command, sizeof(command), "ldmsd_yaml_parser --ldms_config %s --daemon_name %s", path, dname);
	fp = popen(command, "r");
	if (!fp) {
		ovis_log(config_log, OVIS_LERROR, "Error opening pipe to ldmsd_yaml_parser.\n");
		goto err;
	}
	size_t bytes_read;
	size_t tbytes = 0;
	while ((bytes_read = fread(&cfg_str[tbytes], 1, 4096, fp)) > 0) {
		tbytes += bytes_read;
		if (bytes_read == 4096) {
			cfg_str = (char *)realloc(cfg_str, tbytes + bytes_read + 1);
			if (!cfg_str) {
				ovis_log(config_log, OVIS_LERROR, "Error allocating memory\n");
				goto err;
			}
		}
	}
	cfg_str[tbytes] = '\0';
	int status;
	status = pclose(fp);
	if (status) {
		ovis_log(config_log, OVIS_LERROR, "Error occured processing configuration file %s.\n", path);
		goto err;
	}
	return cfg_str;
err:
	if (cfg_str)
		free(cfg_str);
	return NULL;
}

int __req_deferred_start_regex(ldmsd_req_ctxt_t reqc, ldmsd_cfgobj_type_t type)
{
	regex_t regex = {0};
	ldmsd_cfgobj_t obj;
	int rc;
	char *val;
	val = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_REGEX);
	if (!val) {
		ovis_log(NULL, OVIS_LERROR, "`regex` attribute is required.\n");
		return EINVAL;
	}
	rc = regcomp(&regex, val, REG_NOSUB);
	if (rc) {
		ovis_log(NULL, OVIS_LERROR, "Bad regex: %s\n", val);
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

int __req_deferred_start(ldmsd_req_ctxt_t reqc, ldmsd_cfgobj_type_t type)
{
	ldmsd_cfgobj_t obj;
	char *name;
	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		ovis_log(NULL, OVIS_LERROR, "`name` attribute is required.\n");
		return EINVAL;
	}
	obj = ldmsd_cfgobj_find(name, type);
	if (!obj) {
		ovis_log(NULL, OVIS_LERROR, "Config object not found: %s\n", name);
		free(name);
		return ENOENT;
	}
	free(name);
	obj->perm |= LDMSD_PERM_DSTART;
	ldmsd_cfgobj_put(obj, "find");
	return 0;
}

/* The implementation is in ldmsd_request.c. */
extern ldmsd_prdcr_t
__prdcr_add_handler(ldmsd_req_ctxt_t reqc, char *verb, char *obj_name);
int __req_deferred_advertiser_start(ldmsd_req_ctxt_t reqc)
{
	int rc = 0;
	ldmsd_prdcr_t prdcr;
	char *name;

	name = ldmsd_req_attr_str_value_get_by_id(reqc, LDMSD_ATTR_NAME);
	if (!name) {
		ovis_log(config_log, OVIS_LERROR, "`name` attribute is required.\n");
		return EINVAL;
	}

	prdcr = ldmsd_prdcr_find(name);
	if (!prdcr) {
		prdcr = __prdcr_add_handler(reqc, "advertiser_start", "advertiser");
		if (!prdcr) {
			ovis_log(config_log, OVIS_LERROR, "%s", reqc->line_buf);
			rc = reqc->errcode;
			goto out;
		}
	}

	prdcr->obj.perm |= LDMSD_PERM_DSTART;
out:
	ldmsd_prdcr_put(prdcr, "find");
	free(name);
	return rc;
}

/*
 * rc = 0, filter applied OK
 * rc > 0, rc == -errno, error
 * rc = -1, filter not applied (but not an error)
 */
int __req_filter_failover(ldmsd_req_ctxt_t reqc, void *ctxt)
{
	int *use_failover = ctxt;
	int rc;

	switch (reqc->req_id) {
	case LDMSD_FAILOVER_START_REQ:
		*use_failover = 1;
		rc = 0;
		break;
	case LDMSD_PRDCR_START_REGEX_REQ:
		rc = __req_deferred_start_regex(reqc, LDMSD_CFGOBJ_PRDCR);
		break;
	case LDMSD_ADVERTISER_START_REQ:
		rc = __req_deferred_advertiser_start(reqc);
		break;
	case LDMSD_PRDCR_START_REQ:
		rc = __req_deferred_start(reqc, LDMSD_CFGOBJ_PRDCR);
		break;
	case LDMSD_UPDTR_START_REQ:
		rc = __req_deferred_start(reqc, LDMSD_CFGOBJ_UPDTR);
		break;
	case LDMSD_STRGP_START_REQ:
		rc = __req_deferred_start(reqc, LDMSD_CFGOBJ_STRGP);
		break;
	case LDMSD_PRDCR_LISTEN_START_REQ:
		rc = __req_deferred_start(reqc, LDMSD_CFGOBJ_PRDCR_LISTEN);
		break;
	default:
		rc = -1;
	}
	return rc;
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

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	LDMSD_CFGOBJ_FOREACH(obj, LDMSD_CFGOBJ_PRDCR) {
		if (filter && filter(obj))
			continue;
		rc = __ldmsd_prdcr_start((ldmsd_prdcr_t)obj, &sctxt);
		if (rc) {
			ovis_log(NULL, OVIS_LERROR,
				  "prdcr_start failed, name: %s, rc: %d\n",
				  obj->name, rc);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
			goto out;
		}
		__dlog(DLOG_CFGOK, "prdcr_start name=%s interval=%ld # delay\n",
			obj->name, ((ldmsd_prdcr_t)obj)->conn_intrvl_us);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);

	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	LDMSD_CFGOBJ_FOREACH(obj, LDMSD_CFGOBJ_UPDTR) {
		if (filter && filter(obj))
			continue;
		rc = __ldmsd_updtr_start((ldmsd_updtr_t)obj, &sctxt);
		if (rc) {
			ovis_log(NULL, OVIS_LERROR,
				  "updtr_start failed, name: %s, rc: %d\n",
				  obj->name, rc);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
			goto out;
		}
		__dlog(DLOG_CFGOK, "updtr_start name=%s interval=%ld"
			" offset=%ld auto_interval=%d # delayed\n",
			obj->name,
			((ldmsd_updtr_t)obj)->default_task.sched.intrvl_us,
			((ldmsd_updtr_t)obj)->default_task.sched.offset_us,
			((ldmsd_updtr_t)obj)->is_auto_task);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);

	ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
	LDMSD_CFGOBJ_FOREACH(obj, LDMSD_CFGOBJ_STRGP) {
		if (filter && filter(obj))
			continue;
		rc = __ldmsd_strgp_start((ldmsd_strgp_t)obj, &sctxt);
		if (rc) {
			ovis_log(NULL, OVIS_LERROR,
				  "strgp_start failed, name: %s, rc: %d\n",
				  obj->name, rc);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
			goto out;
		}
		__dlog(DLOG_CFGOK, "strgp_start name=%s # delayed \n",
			obj->name);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);

	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR_LISTEN);
	LDMSD_CFGOBJ_FOREACH(obj, LDMSD_CFGOBJ_PRDCR_LISTEN) {
		if (filter && filter(obj))
			continue;
		((ldmsd_prdcr_listen_t)obj)->state = LDMSD_PRDCR_LISTEN_STATE_RUNNING;
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR_LISTEN);

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

int process_config_file(const char *path, int *lno, int trust)
{
	int rc;
	rc = __process_config_file(path, lno, trust,
				   __req_filter_failover, &ldmsd_use_failover);
	return rc;
}

int process_config_str(char *config_str, int *lno, int trust)
{
	int rc;
	char *cfg_str = strdup(config_str);
	rc = __process_config_str(cfg_str, lno, trust,
				__req_filter_failover, &ldmsd_use_failover);
	return rc;
}

char *process_yaml_config_file(const char *path, const char *dname)
{
	char *cstr;
	cstr = __process_yaml_config_file(path, dname);
	return cstr;
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
		ovis_log(config_log, OVIS_LDEBUG, "sending %s msg_no: %d:%lu, flags: %#o, "
			   "rec_len: %u\n",
			   ldmsd_req_id2str(hdr.req_id),
			   hdr.msg_no, ldms_xprt_conn_id(xprt->ldms.ldms),
			   hdr.flags, hdr.rec_len);
		break;
	case LDMSD_REQ_TYPE_CONFIG_RESP:
		ovis_log(config_log, OVIS_LDEBUG, "sending RESP msg_no: %d, rsp_err: %d, flags: %#o, "
			   "rec_len: %u\n",
			   hdr.msg_no,
			   hdr.rsp_err, hdr.flags, hdr.rec_len);
		break;
	default:
		ovis_log(config_log, OVIS_LDEBUG, "sending BAD REQUEST\n");
	}
}

static int send_ldms_fn(void *_xprt, char *data, size_t data_len)
{
	ldmsd_cfg_xprt_t xprt = (ldmsd_cfg_xprt_t)_xprt;
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
	xprt.type = LDMSD_CFG_TYPE_LDMS;
	xprt.cleanup_fn = NULL;

	if (ntohl(request->rec_len) > xprt.max_msg) {
		/* Send the record length advice */
		ldmsd_send_cfg_rec_adv(&xprt, ntohl(request->msg_no), xprt.max_msg);
		return;
	}

	switch (ntohl(request->type)) {
	case LDMSD_REQ_TYPE_CONFIG_CMD:
		(void)ldmsd_process_config_request(&xprt, request, NULL, NULL);
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
		ldmsd_xprt_term(x);
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_ERROR:
		ldms_xprt_put(x);
		break;
	case LDMS_XPRT_EVENT_RECV:
		ldmsd_recv_msg(x, e->data, e->data_len);
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
	case LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED:
		break;
	default:
		assert(0);
		break;
	}
}

int listen_on_ldms_xprt(ldmsd_listen_t listen)
{
	int rc = 0;
	char port_buff[8];

	assert(listen->x);

	snprintf(port_buff, sizeof(port_buff), "%hu", listen->port_no);
	rc = ldms_xprt_listen_by_name(listen->x, listen->host, port_buff,
					__listen_connect_cb, NULL);
	if (rc) {
		ovis_log(NULL, OVIS_LERROR, "Error %d Listening on %s:%d using `%s` transport and "
			  "`%s` authentication\n", rc, listen->xprt,
			  listen->port_no, listen->xprt, listen->auth_name);
		return rc;
	}
	ovis_log(NULL, OVIS_LINFO, "Listening on %s:%d using `%s` transport and "
		  "`%s` authentication\n",
		  listen->xprt, listen->port_no, listen->xprt,
		  listen->auth_name);
	return 0;
}

void ldmsd_cfg_ldms_init(ldmsd_cfg_xprt_t xprt, ldms_t ldms)
{
	xprt->ldms.ldms = ldms_xprt_get(ldms);
	xprt->send_fn = send_ldms_fn;
	xprt->max_msg = ldms_xprt_msg_max(ldms);
	xprt->cleanup_fn = ldmsd_cfg_ldms_xprt_cleanup;
	xprt->trust = 0;
	xprt->type = LDMSD_CFG_TYPE_LDMS;
}

void ldmsd_mm_status(int level, const char *prefix)
{
	struct mm_stat s;
	mm_stats(&s);
	/* compute bound based on current usage */
	size_t used = s.size - s.grain*s.largest;
	ovis_log(NULL, level, "%s: mm_stat: size=%zu grain=%zu chunks_free=%zu grains_free=%zu grains_largest=%zu grains_smallest=%zu bytes_free=%zu bytes_largest=%zu bytes_smallest=%zu bytes_used+holes=%zu\n",
	prefix,
	s.size, s.grain, s.chunks, s.bytes, s.largest, s.smallest,
	s.grain*s.bytes, s.grain*s.largest, s.grain*s.smallest, used);
}
