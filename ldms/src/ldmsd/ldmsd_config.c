/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010-2016 Sandia Corporation. All rights reserved.
 *
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
#include <event2/thread.h>
#include <coll/rbt.h>
#include <coll/str_map.h>
#include <ovis_util/util.h>
#include "event.h"
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_xprt.h"
#include "ldmsd_request.h"
#include "config.h"

#define REPLYBUF_LEN 4096
char myhostname[HOST_NAME_MAX+1];
pthread_t ctrl_thread = (pthread_t)-1;
pthread_t inet_ctrl_thread = (pthread_t)-1;
int muxr_s = -1;
int inet_sock = -1;
int inet_listener = -1;
char *sockname = NULL;

static int cleanup_requested = 0;
int bind_succeeded;

extern void cleanup(int x, char *reason);

pthread_mutex_t host_list_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(host_list_s, hostspec) host_list;
LIST_HEAD(ldmsd_store_policy_list, ldmsd_store_policy) sp_list;
pthread_mutex_t sp_list_lock = PTHREAD_MUTEX_INITIALIZER;

#define LDMSD_PLUGIN_LIBPATH_MAX	1024
LIST_HEAD(plugin_list, ldmsd_plugin_cfg) plugin_list;

void ldmsd_config_cleanup()
{
	if (ctrl_thread != (pthread_t)-1) {
		void *dontcare;
		pthread_cancel(ctrl_thread);
		pthread_join(ctrl_thread, &dontcare);
	}

	if (inet_ctrl_thread != (pthread_t)-1) {
		void *dontcare;
		pthread_cancel(inet_ctrl_thread);
		pthread_join(inet_ctrl_thread, &dontcare);
	}

	if (muxr_s >= 0)
		close(muxr_s);
	if (sockname && bind_succeeded) {
		ldmsd_log(LDMSD_LINFO, "LDMS Daemon deleting socket "
						"file %s\n", sockname);
		unlink(sockname);
	}

	if (inet_listener >= 0)
		close(inet_listener);
	if (inet_sock >= 0)
		close(inet_sock);
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
	void *d;

	if (!path)
		path = LDMSD_PLUGIN_LIBPATH_DEFAULT;

	strncpy(library_path, path, sizeof(library_path) - 1);

	while ((libpath = strtok_r(pathdir, ":", &saveptr)) != NULL) {
		pathdir = NULL;
		snprintf(library_name, sizeof(library_name), "%s/lib%s.so",
			 libpath, plugin_name);
		d = dlopen(library_name, RTLD_NOW);
		if (d != NULL) {
			break;
		}
	}

	if (!d) {
		char *dlerr = dlerror();
		snprintf(errstr, errlen, "Failed to load the plugin '%s'. "
				"dlerror: %s", plugin_name, dlerr);
		goto err;
	}

	ldmsd_plugin_get_f pget = dlsym(d, "get_plugin");
	if (!pget) {
		snprintf(errstr, errlen,
			"The library of '%s' is missing the get_plugin() "
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
	pi->thread_id = -1;
	pi->handle = d;
	pi->name = strdup(plugin_name);
	if (!pi->name)
		goto enomem;
	pi->libpath = strdup(library_name);
	if (!pi->libpath)
		goto enomem;
	pi->plugin = lpi;
	pi->sample_interval_us = 1000000;
	pi->sample_offset_us = 0;
	pi->synchronous = 0;
	LIST_INSERT_HEAD(&plugin_list, pi, entry);
	return pi;
enomem:
	snprintf(errstr, errlen, "No memory");
err:
	if (pi) {
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
	int rc = regcomp(regex, regex_str, REG_NOSUB);
	if (rc) {
		(void)regerror(rc, regex, errbuf, errsz);
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

	pi = ldmsd_get_plugin(plugin_name);
	if (!pi)
		return ENOENT;

	pthread_mutex_lock(&pi->lock);
	rc = pi->plugin->config(pi->plugin, _kw_list, _av_list);
	pthread_mutex_unlock(&pi->lock);
out:
	return rc;
}

/*
 * Assign user data to a metric
 */
int ldmsd_set_udata(const char *set_name, const char *metric_name,
						const char *udata_s)
{
	ldms_set_t set;
	set = ldms_set_by_name(set_name);
	if (!set)
		return ENOENT;

	char *endptr;
	uint64_t udata = strtoull(udata_s, &endptr, 0);
	if (endptr[0] != '\0')
		return EINVAL;

	int mid = ldms_metric_by_name(set, metric_name);
	if (mid < 0)
		return -ENOENT;
	ldms_metric_user_data_set(set, mid, udata);

	ldms_set_put(set);
	return 0;
}

int ldmsd_set_udata_regex(char *set_name, char *regex_str,
		char *base_s, char *inc_s, char *errstr, size_t errsz)
{
	int rc = 0;
	ldms_set_t set;
	set = ldms_set_by_name(set_name);
	if (!set) {
		snprintf(errstr, errsz, "Set '%s' not found.", set_name);
		return ENOENT;
	}

	char *endptr;
	uint64_t base = strtoull(base_s, &endptr, 0);
	if (endptr[0] != '\0') {
		snprintf(errstr, errsz, "User data base '%s' invalid.",
								base_s);
		return EINVAL;
	}

	int inc = 0;
	if (inc_s)
		inc = atoi(inc_s);

	regex_t regex;
	rc = ldmsd_compile_regex(&regex, regex_str, errstr, errsz);
	if (rc)
		return rc;

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
	ldms_set_put(set);
	return 0;
}

void ldmsd_exit_daemon()
{
	cleanup_requested = 1;
	ldmsd_log(LDMSD_LINFO, "User requested exit.\n");
}

/* data_len is excluding the null character */
int print_config_error(struct ldmsd_req_ctxt *reqc, char *data, size_t data_len,
		int msg_flags)
{
	if (data_len + 1 > reqc->rep_len - reqc->rep_off) {
		reqc->rep_buf = realloc(reqc->rep_buf,
				reqc->rep_off + data_len + 1);
		if (!reqc->rep_buf) {
			ldmsd_log(LDMSD_LERROR, "Out of memory\n");
			return ENOMEM;
		}
		reqc->rep_len = reqc->rep_off + data_len + 1;
	}

	if (data) {
		memcpy(&reqc->rep_buf[reqc->rep_off], data, data_len);
		reqc->rep_off += data_len;
		reqc->rep_buf[reqc->rep_off] = '\0';
	}
	if (reqc->rep_off)
		ldmsd_log(LDMSD_LERROR, "%s\n", reqc->rep_buf);

	return 0;
}

extern void req_ctxt_tree_lock();
extern void req_ctxt_tree_unlock();
extern ldmsd_req_ctxt_t alloc_req_ctxt(struct req_ctxt_key *key);
extern void free_req_ctxt(ldmsd_req_ctxt_t rm);
extern int ldmsd_handle_request(ldmsd_req_hdr_t request, ldmsd_req_ctxt_t reqc);
ldmsd_req_ctxt_t process_config_line(char *line, struct ldmsd_req_hdr_s *request)
{
	static uint32_t msg_no = 0;

	request->marker = LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F;
	request->flags = LDMSD_RECORD_MARKER;
	request->msg_no = msg_no;
	request->rec_len = 0;
	request->code = -1;

	errno = 0;
	struct req_ctxt_key key = {
			.msg_no = msg_no,
			.conn_id = -1,
	};
	msg_no++;

	char *_line, *ptr, *verb, *av, *name, *value, *tmp = NULL;
	int idx, rc = 0, attr_cnt = 0;
	uint32_t req_id;
	size_t tot_attr_len = 0;
	struct ldmsd_req_ctxt *reqc = NULL;

	_line = strdup(line);
	if (!_line) {
		ldmsd_log(LDMSD_LERROR, "Out of memory\n");
		errno = ENOMEM;
		return NULL;
	}

	/* Prepare the request context */
	req_ctxt_tree_lock();
	reqc = alloc_req_ctxt(&key);
	if (!reqc) {
		ldmsd_log(LDMSD_LERROR, "Out of memory\n");
		errno = ENOMEM;
		goto out;
	}

	rc = ldmsd_process_cfg_str(request, _line, &reqc->req_buf,
				&reqc->req_off, &reqc->req_len);
	if (rc) {
		errno = rc;
		goto err;
	}

	memcpy(&reqc->rh, &request, sizeof(request));

	reqc->resp_handler = print_config_error;
	reqc->ldms = NULL;
	req_ctxt_tree_unlock();
	goto out;

err:
	if (reqc) {
		req_ctxt_tree_lock();
		free_req_ctxt(reqc);
		req_ctxt_tree_unlock();
		reqc = NULL;
	}
out:
	free(_line);
	return reqc;
}
int process_config_file(const char *path, int *errloc)
{
	int rc = 0;
	int lineno = 0;
	FILE *fin = NULL;
	char *buff = NULL;
	char *line;
	char *comment;
	ssize_t off = 0;
	size_t cfg_buf_len = LDMSD_MAX_CONFIG_STR_LEN;

	struct ldmsd_req_hdr_s request;
	struct ldmsd_req_ctxt *reqc = NULL;

	char *env = getenv("LDMSD_MAX_CONFIG_STR_LEN");
	if (env)
		cfg_buf_len = strtol(env, NULL, 0);
	fin = fopen(path, "rt");
	if (!fin) {
		rc = errno;
		goto cleanup;
	}
	buff = malloc(cfg_buf_len);
	if (!buff) {
		rc = errno;
		goto cleanup;
	}


next_line:
	line = fgets(buff + off, cfg_buf_len - off, fin);
	if (!line)
		goto cleanup;
	lineno++;

	comment = strchr(line, '#');

	if (comment) {
		*comment = '\0';
	}

	off = strlen(buff);
	while (off && isspace(line[off-1])) {
		off--;
	}

	if (!off) {
		/* empty string */
		off = 0;
		goto next_line;
	}

	buff[off] = '\0';

	if (buff[off-1] == '\\') {
		buff[off-1] = ' ';
		goto next_line;
	}

	line = buff;
	while (isspace(*line)) {
		line++;
	}

	if (!*line) {
		/* buff contain empty string */
		off = 0;
		goto next_line;
	}

	reqc = process_config_line(line, &request);
	if (!reqc) {
		ldmsd_log(LDMSD_LERROR, "Problem in line %d: %s\n", lineno,
							strerror(errno));
		goto cleanup;
	}
	rc = ldmsd_handle_request(&request, reqc);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Problem in line: %s\n", line);
		goto cleanup;
	}

	if (reqc) {
		req_ctxt_tree_lock();
		free_req_ctxt(reqc);
		req_ctxt_tree_unlock();
		reqc = NULL;
	}
	off = 0;
	goto next_line;

cleanup:
	if (reqc) {
		req_ctxt_tree_lock();
		free_req_ctxt(reqc);
		req_ctxt_tree_unlock();
	}
	if (fin)
		fclose(fin);
	if (buff)
		free(buff);
	*errloc = lineno;
	return rc;
}

extern int
process_request_unix_domain(int fd, struct msghdr *msg, size_t msg_len);
void *ctrl_thread_proc(void *v)
{
	struct sockaddr_un sun = {.sun_family = AF_UNIX, .sun_path = ""};
	socklen_t sun_len = sizeof(sun);
	int sock;

	struct msghdr msg;
	struct iovec iov;
	unsigned char *lbuf;
	struct sockaddr_storage ss;
	size_t cfg_buf_len = LDMSD_MAX_CONFIG_STR_LEN;
	char *env = getenv("LDMSD_MAX_CONFIG_STR_LEN");
	if (env)
		cfg_buf_len = strtol(env, NULL, 0);
	lbuf = malloc(cfg_buf_len);
	if (!lbuf) {
		ldmsd_log(LDMSD_LERROR,
			  "Fatal error allocating %zu bytes for config string.\n",
			  cfg_buf_len);
		cleanup(1, "ctrl thread proc out of memory");
	}

loop:
	sock = accept(muxr_s, (void *)&sun, &sun_len);
	if (sock < 0) {
		ldmsd_log(LDMSD_LERROR, "Error %d failed to accept.\n", inet_sock);
		goto loop;
	}

	iov.iov_base = lbuf;
	do {
		struct ldmsd_req_hdr_s request;
		ssize_t msglen;
		ss.ss_family = AF_UNIX;
		msg.msg_name = &ss;
		msg.msg_namelen = sizeof(ss);
		iov.iov_len = sizeof(request);
		iov.iov_base = &request;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = 0;
		msglen = recvmsg(sock, &msg, MSG_PEEK);
		if (msglen <= 0)
			break;
		if (cfg_buf_len < request.rec_len) {
			free(lbuf);
			lbuf = malloc(request.rec_len);
			if (!lbuf) {
				cfg_buf_len = 0;
				break;
			}
			cfg_buf_len = request.rec_len;
		}
		iov.iov_base = lbuf;
		iov.iov_len = request.rec_len;

		msglen = recvmsg(sock, &msg, MSG_WAITALL);
		if (msglen < request.rec_len)
			break;

		process_request_unix_domain(sock, &msg, msglen);
		if (cleanup_requested)
			break;
	} while (1);
	if (cleanup_requested) {
		/* Reset it to prevent deadlock in cleanup */
		ctrl_thread = (pthread_t)-1;
		cleanup(0,"user quit");
	}
	goto loop;
	return NULL;
}

int ldmsd_config_init(char *name)
{
	struct sockaddr_un sun;
	int ret;

	/* Create the control socket parsing structures */
	if (!name) {
		char *sockpath = getenv("LDMSD_SOCKPATH");
		if (!sockpath)
			sockpath = "/var/run";
		sockname = malloc(sizeof(LDMSD_CONTROL_SOCKNAME) + strlen(sockpath) + 2);
		if (!sockname) {
			ldmsd_log(LDMSD_LERROR, "Our of memory\n");
			return -1;
		}
		sprintf(sockname, "%s/%s", sockpath, LDMSD_CONTROL_SOCKNAME);
	} else {
		sockname = strdup(name);
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strncpy(sun.sun_path, sockname,
			sizeof(struct sockaddr_un) - sizeof(short));

	/* Create listener */
	muxr_s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (muxr_s < 0) {
		ldmsd_log(LDMSD_LERROR, "Error %d creating muxr socket.\n",
				muxr_s);
		return -1;
	}

	/* Bind to our public name */
	ret = bind(muxr_s, (struct sockaddr *)&sun, sizeof(struct sockaddr_un));
	if (ret < 0) {
		ldmsd_log(LDMSD_LERROR, "Error %d binding to socket "
				"named '%s'.\n", errno, sockname);
		return -1;
	}
	bind_succeeded = 1;

	ret = listen(muxr_s, 1);
	if (ret < 0) {
		ldmsd_log(LDMSD_LERROR, "Error %d listen to sock named '%s'.\n",
				errno, sockname);
	}

	ret = pthread_create(&ctrl_thread, NULL, ctrl_thread_proc, 0);
	if (ret) {
		ldmsd_log(LDMSD_LERROR, "Error %d creating "
				"the control pthread'.\n");
		return -1;
	}
	return 0;
}

const char * blacklist[] = {
	"liblustre_sampler.so",
	"libzap.so",
	"libzap_rdma.so",
	"libzap_sock.so",
	NULL
};

#define APP "ldmsd"

/* Dump plugin names and usages (where available) before ldmsd redirects
 * io. Loads and terms all plugins, which provides a modest check on some
 * coding and deployment issues.
 * \param plugname: list usage only for plugname. If NULL, list all plugins.
 */
int ldmsd_plugins_usage(const char *plugname)
{
	struct stat buf;
	glob_t pglob;

	char *path = getenv("LDMSD_PLUGIN_LIBPATH");
	if (!path)
		path = PLUGINDIR;

	if (! path ) {
		fprintf(stderr, "%s: need plugin path input.\n", APP);
		fprintf(stderr, "Did not find env(LDMSD_PLUGIN_LIBPATH).\n");
		return EINVAL;
	}

	if (stat(path, &buf) < 0) {
		int err = errno;
		fprintf(stderr, "%s: unable to stat library path %s (%d).\n",
			APP, path, err);
		return err;
	}

	int rc = 0;

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
		fprintf(stderr, "%s: no libraries in %s\n", APP, path);
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
