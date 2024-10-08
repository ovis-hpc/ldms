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
#include "ldms_xprt.h"
#include "ldmsd_request.h"
#include "config.h"

extern void cleanup(int x, char *reason);

pthread_mutex_t host_list_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(host_list_s, hostspec) host_list;
pthread_mutex_t sp_list_lock = PTHREAD_MUTEX_INITIALIZER;

#define LDMSD_PLUGIN_LIBPATH_MAX	1024
struct plugin_list plugin_list;

void ldmsd_cfg_ldms_xprt_cleanup(ldmsd_cfg_xprt_t xprt)
{
	/* nothing to do */
}

struct ldmsd_plugin_cfg *ldmsd_get_plugin(char *name)
{
	struct ldmsd_plugin_cfg *p;
	LIST_FOREACH(p, &plugin_list, entry) {
		if (0 == strcmp(p->name, name))
			return p;
	}
	return NULL;
}

struct ldmsd_plugin_cfg *new_plugin(char *plugin_name,
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
	lpi = pget(ldmsd_log);
	if (!lpi) {
		snprintf(errstr, errlen, "The plugin '%s' could not be loaded.",
								plugin_name);
		goto err;
	}
	pi = calloc(1, sizeof *pi);
	if (!pi)
		goto enomem;
	pthread_mutex_init(&pi->lock, NULL);
	pi->thread_id = -1;
	pi->handle = d;
	pi->name = strdup(plugin_name);
	if (!pi->name)
		goto enomem;
	pi->libpath = strdup(library_name);
	if (!pi->libpath)
		goto enomem;
	pi->plugin = lpi;
	TAILQ_INIT(&lpi->avl_q);
	TAILQ_INIT(&lpi->kwl_q);
	lpi->pi = pi;
	pi->sample_interval_us = 1000000;
	pi->sample_offset_us = 0;
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
	if (d)
		dlclose(d);
	return NULL;
}

void destroy_plugin(struct ldmsd_plugin_cfg *p)
{
	struct avl_q_item *avl;
	struct avl_q_item *kwl;
	free(p->libpath);
	free(p->name);

	/*
	 * Assume that the length of av_list_q and
	 * the length of kw_list_q are equal.
	 */
	while ((avl = TAILQ_FIRST(&p->plugin->avl_q)) &&
			(kwl = TAILQ_FIRST(&p->plugin->kwl_q))) {
		TAILQ_REMOVE(&p->plugin->avl_q, avl, entry);
		TAILQ_REMOVE(&p->plugin->kwl_q, kwl, entry);
		free(avl->av_list);
		free(avl);
		free(kwl->av_list);
		free(kwl);
	}
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
int ldmsd_load_plugin(char *plugin_name, char *errstr, size_t errlen)
{
	struct ldmsd_plugin_cfg *pi = ldmsd_get_plugin(plugin_name);
	if (pi) {
		snprintf(errstr, errlen, "Plugin '%s' already loaded",
							plugin_name);
		return EEXIST;
	}
	pi = new_plugin(plugin_name, errstr, errlen);
	if (!pi)
		return -1;
	return 0;
}

/*
 * Destroy and unload the plugin
 */
int ldmsd_term_plugin(char *plugin_name)
{
	int rc = 0;
	struct ldmsd_plugin_cfg *pi;

	pi = ldmsd_get_plugin(plugin_name);
	if (!pi)
		return ENOENT;

	pthread_mutex_lock(&pi->lock);
	if (pi->ref_count) {
		rc = EINVAL;
		pthread_mutex_unlock(&pi->lock);
		goto out;
	}
	pi->plugin->term(pi->plugin);
	pthread_mutex_unlock(&pi->lock);
	destroy_plugin(pi);
out:
	return rc;
}

/*
 * Configure a plugin
 */
int ldmsd_config_plugin(char *plugin_name,
			struct attr_value_list *_av_list,
			struct attr_value_list *_kw_list)
{
	int rc = 0;
	struct ldmsd_plugin_cfg *pi;
	struct avl_q_item *avl;
	struct avl_q_item *kwl;

	avl = calloc(1, sizeof(*avl));
	kwl = calloc(1, sizeof(*kwl));
	if (!avl || !kwl) {
		rc = ENOMEM;
		goto err;
	}
	avl->av_list = av_copy(_av_list);
	kwl->av_list = av_copy(_kw_list);
	if (!avl->av_list || !kwl->av_list) {
		rc = ENOMEM;
		goto err;
	}

	pi = ldmsd_get_plugin(plugin_name);
	if (!pi) {
		rc = ENOENT;
		goto err;
	}

	pthread_mutex_lock(&pi->lock);
	rc = pi->plugin->config(pi->plugin, _kw_list, _av_list);
	TAILQ_INSERT_TAIL(&pi->plugin->kwl_q, kwl, entry);
	TAILQ_INSERT_TAIL(&pi->plugin->avl_q, avl, entry);
	pthread_mutex_unlock(&pi->lock);
	return rc;
err:
	if (avl) {
		if (avl->av_list)
			av_free(avl->av_list);
		free(avl);
	}
	if (kwl) {
		if (kwl->av_list)
			av_free(kwl->av_list);
		free(kwl);
	}
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
	ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
	return NULL;
}

static uint64_t __get_cfgfile_id()
{
	static uint64_t id = 1;
	return __sync_fetch_and_add(&id, 1);
}

extern int is_req_id_priority(enum ldmsd_request req_id);
/*
 * \param req_filter is a function that returns zero if we want to process the
 *                   request, and returns non-zero otherwise.
 */
static
int __process_config_file(const char *path, int *lno, int trust,
		int (*req_filter)(ldmsd_cfg_xprt_t, ldmsd_req_hdr_t, void *),
		void *ctxt)
{
	static uint32_t msg_no = 0;
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
		ldmsd_log(LDMSD_LERROR, "Out of memory\n");
		goto cleanup;
	}
	line_sz = LDMSD_CFG_FILE_XPRT_MAX_REC;

	fin = fopen(path, "rt");
	if (!fin) {
		rc = errno;
		ldmsd_log(LDMSD_LERROR, "Failed to open the config file '%s'. %s\n",
				path, STRERROR(rc));
		goto cleanup;
	}

	xprt.type = LDMSD_CFG_TYPE_FILE;
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
			ldmsd_log(LDMSD_LERROR, "Out of memory\n");
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
				ldmsd_log(LDMSD_LWARNING,
						"'interval' is being deprecated. "
						"Please use 'reconnect' with 'prdcr_add' or 'prdcr_start*' "
						"in the future.\n");
			}
		}
	}

	req_array = ldmsd_parse_config_str(line, msg_no, xprt.max_msg, ldmsd_log);
	if (!req_array) {
		rc = errno;
		ldmsd_log(LDMSD_LERROR, "Process config file error at line %d "
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

	if (req_filter) {
		rc = req_filter(&xprt, request, ctxt);
		/* rc = 0, filter OK */
		if (rc == 0) {
			__dlog(DLOG_CFGOK, "# deferring line %d (%s): %s\n",
				lineno, path, line);
			goto next_req;
		}
		/* rc == errno */
		if (rc > 0) {
			ldmsd_log(LDMSD_LERROR,
				  "Configuration error at "
				  "line %d (%s)\n", lineno, path);
			goto cleanup;
		} else {
			/* rc < 0, filter not applied */
			rc = 0;
		}
	}

	rc = ldmsd_process_config_request(&xprt, request);
	if (rc || xprt.rsp_err) {
		if (!rc)
			rc = xprt.rsp_err;
		ldmsd_log(LDMSD_LERROR, "Configuration error at line %d (%s)\n",
				lineno, path);
		goto cleanup;
	}
next_req:
	free(request);
	request = NULL;
	msg_no += 1;
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
		int (*req_filter)(ldmsd_cfg_xprt_t, ldmsd_req_hdr_t, void *),
		void *ctxt)
{
	static uint32_t msg_no = 0;
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
		ldmsd_log(LDMSD_LERROR, "Out of memory\n");
		goto cleanup;
	}
	line_sz = LDMSD_CFG_FILE_XPRT_MAX_REC;
	xprt.type = LDMSD_CFG_TYPE_FILE;
	xprt.file.cfgfile_id = __get_cfgfile_id();
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

	/* Get rid of trailing spaces */
	while (cnt && isspace(tmp[cnt-1]))
		cnt--;

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
			ldmsd_log(LDMSD_LERROR, "Out of memory\n");
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
				ldmsd_log(LDMSD_LWARNING,
						"'interval' is being deprecated. "
						"Please use 'reconnect' with 'prdcr_add' or 'prdcr_start*' "
						"in the future.\n");
			}
		}
	}

	req_array = ldmsd_parse_config_str(line, msg_no, xprt.max_msg, ldmsd_log);
	if (!req_array) {
		rc = errno;
		ldmsd_log(LDMSD_LERROR, "Process config string error at line %d "
				"(%s). %s\n", lineno, cfg_str, STRERROR(rc));
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

	if (req_filter) {
		rc = req_filter(&xprt, request, ctxt);
		/* rc = 0, filter OK */
		if (rc == 0) {
			__dlog(DLOG_CFGOK, "# deferring line %d (%s): %s\n",
				lineno, cfg_str, line);
			goto next_req;
		}
		/* rc == errno */
		if (rc > 0) {
			ldmsd_log(LDMSD_LERROR,
				  "Configuration error at "
				  "line %d (%s)\n", lineno, cfg_str);
			goto cleanup;
		} else {
			/* rc < 0, filter not applied */
			rc = 0;
		}
	}

	rc = ldmsd_process_config_request(&xprt, request);
	if (rc || xprt.rsp_err) {
		if (!rc)
			rc = xprt.rsp_err;
		ldmsd_log(LDMSD_LERROR, "Configuration error at line %d (%s)\n",
				lineno, cfg_str);
		goto cleanup;
	}
next_req:
	free(request);
	request = NULL;
	msg_no += 1;
	off = 0;
	goto next_line;

cleanup:
	if (cfg_str)
		free(cfg_str);
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

char *__process_yaml_config_file(const char *path, const char *dname)
{
	FILE *fp;
	char command[256];
	char cstr[256];
        char *cfg_str = malloc(256);
	snprintf(command, sizeof(command), "ldmsd_yaml_parser --ldms_config %s --daemon_name %s 2>&1", path, dname);
	fp = popen(command, "r");
	if (!fp)
		printf("Error in yaml_parser\n");
	int lineno = 0;
	size_t char_cnt = 0;
	while (fgets(cstr, sizeof(cstr), fp) != NULL) {
		printf("%s", cstr);
		char_cnt += sizeof(cstr);
		if (char_cnt >= 1024)
			cfg_str = (char *)realloc(cfg_str, char_cnt - 256);
		if (lineno)
			strcat(cfg_str, cstr);
		else
			snprintf(cfg_str, sizeof(cstr), cstr);
		lineno++;
	}
	pclose(fp);
	char *config_str = strdup(cfg_str);
	if (cfg_str)
		free(cfg_str);
	return config_str;
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
 * rc = 0, filter applied OK
 * rc > 0, rc == -errno, error
 * rc = -1, filter not applied (but not an error)
 */
int __req_filter_failover(ldmsd_cfg_xprt_t x, ldmsd_req_hdr_t req, void *ctxt)
{
	int *use_failover = ctxt;
	int rc;

	/* req is in network byte order */
	ldmsd_ntoh_req_msg(req);

	switch (req->req_id) {
	case LDMSD_FAILOVER_START_REQ:
		*use_failover = 1;
		rc = 0;
		break;
	case LDMSD_PRDCR_START_REGEX_REQ:
		rc = __req_deferred_start_regex(req, LDMSD_CFGOBJ_PRDCR);
		break;
	case LDMSD_PRDCR_START_REQ:
		rc = __req_deferred_start(req, LDMSD_CFGOBJ_PRDCR);
		break;
	case LDMSD_UPDTR_START_REQ:
		rc = __req_deferred_start(req, LDMSD_CFGOBJ_UPDTR);
		break;
	case LDMSD_STRGP_START_REQ:
		rc = __req_deferred_start(req, LDMSD_CFGOBJ_STRGP);
		break;
	default:
		rc = -1;
	}
	/* convert req back to network byte order */
	ldmsd_hton_req_msg(req);
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
			ldmsd_log(LDMSD_LERROR,
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
			ldmsd_log(LDMSD_LERROR,
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
			ldmsd_log(LDMSD_LERROR,
				  "strgp_start failed, name: %s, rc: %d\n",
				  obj->name, rc);
			ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
			goto out;
		}
                __dlog(DLOG_CFGOK, "strgp_start name=%s # delayed \n",
                        obj->name);
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
		ldmsd_lall("sending %s msg_no: %d:%lu, flags: %#o, "
			   "rec_len: %u\n",
			   ldmsd_req_id2str(hdr.req_id),
			   hdr.msg_no, ldms_xprt_conn_id(xprt->ldms.ldms),
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
		(void)ldmsd_process_config_request(&xprt, request);
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
		break;
	default:
		assert(0);
		break;
	}
}

int listen_on_ldms_xprt(ldmsd_listen_t listen)
{
	int rc = 0;
	struct sockaddr_in sin;
	struct addrinfo *ai = NULL;
	struct addrinfo ai_hint = { .ai_family = AF_INET,
				    .ai_flags = AI_PASSIVE };
	char port_buff[8];

	assert(listen->x);

	sin.sin_family = AF_INET;
	if (listen->host) {
		snprintf(port_buff, sizeof(port_buff), "%hu", listen->port_no);
		rc = getaddrinfo(listen->host, port_buff, &ai_hint, &ai);
		if (rc) {
			ldmsd_lerror("xprt listen error, getaddrinfo(%s, %s) error: %d\n", listen->host, port_buff, rc);
			return rc;
		}
		memcpy(&sin, ai->ai_addr, ai->ai_addrlen);
		freeaddrinfo(ai);
	} else {
		sin.sin_addr.s_addr = 0;
		sin.sin_port = htons(listen->port_no);
	}
	rc = ldms_xprt_listen(listen->x, (struct sockaddr *)&sin, sizeof(sin),
			       __listen_connect_cb, NULL);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Error %d Listening on %s:%d using `%s` transport and "
			  "`%s` authentication\n", rc, listen->xprt,
			  listen->port_no, listen->xprt, listen->auth_name);
		return rc;
	}
	ldmsd_log(LDMSD_LINFO, "Listening on %s:%d using `%s` transport and "
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
	"libpapi_sampler.so",
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

/* check path for existence, and if verbose != 0, whine if missing. */
static int ldmd_plugins_check_dir(const char *path, int verbose) {
	struct stat buf;
	memset(&buf, 0, sizeof(buf));
	int serr = stat(path, &buf);
	int err = 0;
	if (serr < 0) {
		err = errno;
		fprintf(stderr, "%s: unable to stat plugin library path %s (%d).\n",
			APP, path, err);
		return err;
	}
	if ( !S_ISDIR(buf.st_mode)) {
		err = ENOTDIR;
		fprintf(stderr, "%s: plugin library path %s is not a directory.\n",
			APP, path);
		return err;
	}
	return 0;
}

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

	if (plugname && 0 == strcmp(plugname, "all"))
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
	int plugfound = 0;
	while ((libpath = strtok_r(pathdir, ":", &saveptr)) != NULL) {
		pathdir = NULL;
		trc = ldmsd_plugins_usage_dir(libpath, plugname);
		if (trc)
			rc = trc;
		else
			if (plugname)
				plugfound = 1;
	}
	if (plugname && !plugfound) {
		fprintf(stderr, "%s: no library in %s for %s\n", APP, path, plugname);
		strncpy(library_path, path, sizeof(library_path) - 1);
		while ((libpath = strtok_r(pathdir, ":", &saveptr)) != NULL) {
			pathdir = NULL;
			(void)ldmd_plugins_check_dir(libpath, 1);
			fprintf(stderr, "%s: no library in %s for %s\n", APP, path, plugname);
		}
	}
	return rc;
}


static int ldmsd_plugins_usage_dir(const char *path, const char *plugname)
{
	assert( path || "null dir name in ldmsd_plugins_usage" == NULL);
	glob_t pglob;

	int ckdir = ldmd_plugins_check_dir(path, 0);
	if (ckdir)
		return ckdir;

	int rc = 0;
	enum ldmsd_plugin_type tmatch = LDMSD_PLUGIN_OTHER;
	bool matchtype = false;
	if (plugname && strcmp(plugname,"store") == 0) {
		matchtype = true;
		tmatch = LDMSD_PLUGIN_STORE;
		plugname = NULL;
	}
	if (plugname && strcmp(plugname,"sampler") == 0) {
		matchtype = true;
		tmatch = LDMSD_PLUGIN_SAMPLER;
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
			char err_str[LEN_ERRSTR];
			if (ldmsd_load_plugin(b, err_str, LEN_ERRSTR)) {
				fprintf(stderr, "Unable to load plugin %s: %s\n",
					b, err_str);
				goto next;
			}
			struct ldmsd_plugin_cfg *pi = ldmsd_get_plugin(b);
			if (!pi) {
				fprintf(stderr, "Unable to get plugin %s\n",
					b);
				goto next;
			}
			const char *ptype;
			switch (pi->plugin->type) {
			case LDMSD_PLUGIN_OTHER:
				ptype = "OTHER";
				break;
			case LDMSD_PLUGIN_STORE:
				ptype = "STORE";
				break;
			case LDMSD_PLUGIN_SAMPLER:
				ptype = "SAMPLER";
				break;
			default:
				ptype = "BAD plugin";
				break;
			}
			if (matchtype && tmatch != pi->plugin->type)
				goto next;
			printf("======= %s %s:\n", ptype, b);
			const char *u = pi->plugin->usage(pi->plugin);
			printf("%s\n", u);
			printf("=========================\n");
			rc = ldmsd_term_plugin(b);
			if (rc == ENOENT) {
				fprintf(stderr, "plugin '%s' not found\n", b);
			} else if (rc == EINVAL) {
				fprintf(stderr, "The specified plugin '%s' has "
					"active users and cannot be "
					"terminated.\n", b);
			} else if (rc) {
				fprintf(stderr, "Failed to terminate "
						"the plugin '%s'\n", b);
			}
 next:
			free(tmp);
		}

	}


 out2:
	globfree(&pglob);
	free(pat);
 out:
	return rc;
}
