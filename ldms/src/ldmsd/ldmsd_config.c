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

typedef struct cfg_clnt_s {
	struct ldmsd_cfg_xprt_s xprt;
	struct sockaddr_storage ss;
	char recbuf[LDMSD_MAX_CONFIG_REC_LEN];
	char *secretword;
	pthread_t thread;
	LIST_ENTRY(cfg_clnt_s) entry;
} *cfg_clnt_t;

pthread_mutex_t clnt_list_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(cfg_clnt_list, cfg_clnt_s) clnt_list;

static int cleanup_requested = 0;

extern void cleanup(int x, char *reason);

pthread_mutex_t host_list_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(host_list_s, hostspec) host_list;
LIST_HEAD(ldmsd_store_policy_list, ldmsd_store_policy) sp_list;
pthread_mutex_t sp_list_lock = PTHREAD_MUTEX_INITIALIZER;

#define LDMSD_PLUGIN_LIBPATH_MAX	1024
LIST_HEAD(plugin_list, ldmsd_plugin_cfg) plugin_list;

void ldmsd_config_cleanup()
{
	cfg_clnt_t clnt;
	pthread_mutex_lock(&clnt_list_lock);
	while (!LIST_EMPTY(&clnt_list)) {
		clnt = LIST_FIRST(&clnt_list);
		LIST_REMOVE(clnt, entry);
		if (clnt->xprt.cleanup_fn)
			clnt->xprt.cleanup_fn(&clnt->xprt);
		pthread_cancel(clnt->thread);
	}
	pthread_mutex_unlock(&clnt_list_lock);
}

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

	pi = ldmsd_get_plugin(plugin_name);
	if (!pi)
		return ENOENT;

	pthread_mutex_lock(&pi->lock);
	rc = pi->plugin->config(pi->plugin, _kw_list, _av_list);
	pthread_mutex_unlock(&pi->lock);
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

static int log_response_fn(ldmsd_cfg_xprt_t xprt, char *data, size_t data_len)
{
	ldmsd_req_attr_t attr;
	ldmsd_req_hdr_t req_reply = (ldmsd_req_hdr_t)data;

	attr = ldmsd_first_attr(req_reply);
	if (attr->discrim == 0 || attr->discrim == 1) {
		/* We don't dump attributes to the log */
		ldmsd_log(LDMSD_LERROR, "msg_no %d flags %x rec_len %d rsp_err %d msg\n",
			  req_reply->msg_no, req_reply->flags, req_reply->rec_len,
			  req_reply->rsp_err);
	} else {
		data[data_len] = '\0';
		attr = ldmsd_first_attr(req_reply);
		ldmsd_log(LDMSD_LERROR, "msg_no %d flags %x rec_len %d rsp_err %d msg %s\n",
			  req_reply->msg_no, req_reply->flags, req_reply->rec_len,
			  req_reply->rsp_err, attr);
	}
	return 0;
}

extern void req_ctxt_tree_lock();
extern void req_ctxt_tree_unlock();
extern ldmsd_req_ctxt_t alloc_req_ctxt(struct req_ctxt_key *key);
extern void free_req_ctxt(ldmsd_req_ctxt_t rm);

int process_config_file(const char *path)
{
	static uint32_t msg_no = 0;
	int rc = 0;
	int lineno = 0;
	FILE *fin = NULL;
	char *buff = NULL;
	char *line;
	char *comment;
	ssize_t off = 0;
	size_t cfg_buf_len = LDMSD_MAX_CONFIG_STR_LEN;
	struct ldmsd_cfg_xprt_s xprt;
	ldmsd_req_hdr_t request;

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

	xprt.xprt = NULL;
	xprt.send_fn = log_response_fn;

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

	request = ldmsd_parse_config_str(line, msg_no);
	msg_no += 1;
	if (!request) {
		ldmsd_log(LDMSD_LERROR, "Process config file error at line %d "
				"(%s). %s\n", lineno, path, strerror(errno));
		goto cleanup;
	}
	rc = ldmsd_process_config_request(&xprt, request, request->rec_len);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Configuration error at line %d (%s)\n",
				lineno, path);
		free(request);
		goto cleanup;
	}
	free(request);
	off = 0;
	goto next_line;

cleanup:
	if (fin)
		fclose(fin);
	if (buff)
		free(buff);
	return rc;
}

static int send_sock_fn(ldmsd_cfg_xprt_t xprt, char *data, size_t data_len)
{
	return send(xprt->sock.fd, data, data_len, 0);
}

void *config_proc(void *arg)
{
	cfg_clnt_t clnt = arg;
	ssize_t msglen;
	struct ldmsd_req_hdr_s request;

	do {
		/* Read the message header */
		msglen = recv(clnt->xprt.sock.fd, &request, sizeof(request), MSG_PEEK);
		if (msglen <= 0)
			/* closing */
			break;
		/* Verify the marker */
		if (request.marker != LDMSD_RECORD_MARKER
		    || (msglen < sizeof(request))) {
			ldmsd_log(LDMSD_LERROR, "Invalid request: missing record marker.\n");
			break;
		}
		/* If the record length is greater than what we accept, send record
		 * length advice and close */
		if (LDMSD_MAX_CONFIG_REC_LEN < request.rec_len) {
			ldmsd_send_cfg_rec_adv(&clnt->xprt, -1, LDMSD_MAX_CONFIG_REC_LEN);
			break;
		}
		/* Receive the request record */
		msglen = recv(clnt->xprt.sock.fd, clnt->recbuf, request.rec_len, MSG_WAITALL);
		if (msglen < request.rec_len) {
			ldmsd_log(LDMSD_LERROR, "Received short configuration record.\n");
			break;
		}
		/* Process the request record */
		ldmsd_process_config_request(&clnt->xprt,
					     (ldmsd_req_hdr_t)clnt->recbuf,
					     request.rec_len);
		if (cleanup_requested)
			break;
	} while (1);

	ldmsd_log(LDMSD_LDEBUG, "Closing configuration socket %d\n", clnt->xprt.sock.fd);

	pthread_mutex_lock(&clnt_list_lock);
	LIST_REMOVE(clnt, entry);
	pthread_mutex_unlock(&clnt_list_lock);

	close(clnt->xprt.sock.fd);
	free(clnt);

	if (cleanup_requested) {
		cleanup(0, "user quit");
	}
	return NULL;
}

void *config_cm_proc(void *args)
{
	cfg_clnt_t listen_clnt = args;
	int sock;
	int accept_err;
	struct iovec iov;
	struct sockaddr_storage rem_ss;
	socklen_t rem_salen = sizeof(rem_ss);
	accept_err = 0;
loop:
	sock = accept(listen_clnt->xprt.sock.fd, (struct sockaddr *)&rem_ss, &rem_salen);
	if (sock < 0) {
		/* Don't flood the log with accept errors */
		if (accept_err < 10) {
			ldmsd_log(LDMSD_LERROR, "Error %d accepting "
				  "config connection.\n", sock);
			accept_err++;
		}
		/* This sleep avoids spinning on accept error */
		sleep(1);
		goto loop;
	}
	/* Reset accept error count after successful connect */
	accept_err = 0;

#if OVIS_LIB_HAVE_AUTH

#include <string.h>
#include "ovis_auth/auth.h"

#define _str(x) #x
#define str(x) _str(x)
	struct ovis_auth_challenge auth_ch;
	int rc;
	if (listen_clnt->secretword && listen_clnt->secretword[0] != '\0') {
		uint64_t ch = ovis_auth_gen_challenge();
		char *psswd = ovis_auth_encrypt_password(ch, listen_clnt->secretword);
		if (!psswd) {
			ldmsd_log(LDMSD_LERROR, "Failed to generate "
					"the password for the controller\n");
			goto loop;
		}
		size_t len = strlen(psswd) + 1;
		char *psswd_buf = malloc(len);
		if (!psswd_buf) {
			ldmsd_log(LDMSD_LERROR, "Failed to authenticate "
					"the controller. Out of memory");
			free(psswd);
			goto loop;
		}

		ovis_auth_pack_challenge(ch, &auth_ch);
		rc = send(sock, (char *)&auth_ch, sizeof(auth_ch), 0);
		if (rc == -1) {
			ldmsd_log(LDMSD_LERROR, "Error %d failed to send "
					"the challenge to the controller.\n",
					errno);
			free(psswd_buf);
			free(psswd);
			goto loop;
		}
		rc = recv(sock, psswd_buf, len - 1, 0);
		if (rc == -1) {
			ldmsd_log(LDMSD_LERROR, "Error %d. Failed to receive "
					"the password from the controller.\n",
					errno);
			free(psswd_buf);
			free(psswd);
			goto loop;
		}
		psswd_buf[rc] = '\0';
		if (0 != strcmp(psswd, psswd_buf)) {
			shutdown(sock, SHUT_RDWR);
			close(sock);
			free(psswd_buf);
			free(psswd);
			goto loop;
		}
		free(psswd);
		free(psswd_buf);
		int approved = 1;
		rc = send(sock, (void *)&approved, sizeof(int), 0);
		if (rc == -1) {
			ldmsd_log(LDMSD_LERROR, "Error %d failed to send "
				"the init message to the controller.\n", errno);
			goto loop;
		}
	} else {
		/* Don't do authentication */
		auth_ch.hi = auth_ch.lo = 0;
		rc = send(sock, (char *)&auth_ch, sizeof(auth_ch), 0);
		if (rc == -1) {
			ldmsd_log(LDMSD_LERROR, "Error %d failed to send "
					"the greeting to the controller.\n",
					errno);
			goto loop;
		}
	}
#else /* OVIS_LIB_HAVE_AUTH */
	uint64_t greeting = 0;
	int rc = send(sock, (char *)&greeting, sizeof(uint64_t), 0);
	if (rc == -1) {
		ldmsd_log(LDMSD_LINFO,
			  "Error %d failed to send "
			  "the greeting to the controller.\n",
			  errno);
		goto loop;
	}
#endif /* OVIS_LIB_HAVE_AUTH */
	/* Each connection gets it's own thread. Otherwise an
	 * ldmsd_controller client will hang the GUI.
	 */
	cfg_clnt_t clnt = malloc(sizeof *clnt);
	if (!clnt) {
		close(sock);
		ldmsd_log(LDMSD_LERROR,
			  "Memory allocation failure in config connect.\n");
		goto loop;
	}
	clnt->xprt.sock.fd = sock;
	clnt->xprt.send_fn = send_sock_fn;
	clnt->xprt.sock.ss = rem_ss;
	clnt->xprt.cleanup_fn = NULL;

	pthread_mutex_lock(&clnt_list_lock);
	LIST_INSERT_HEAD(&clnt_list, clnt, entry);
	pthread_mutex_unlock(&clnt_list_lock);

	rc = pthread_create(&clnt->thread, NULL, config_proc, clnt);
	if (rc) {
		close(sock);
		ldmsd_log(LDMSD_LERROR,
			  "Error creating thread in config connect.\n");
		pthread_mutex_lock(&clnt_list_lock);
		LIST_REMOVE(clnt, entry);
		pthread_mutex_unlock(&clnt_list_lock);
		free(clnt);
	}
	goto loop;
	return NULL;
}

int listen_on_cfg_xprt(char *xprt_str, char *port_str, char *secretword)
{
	struct sockaddr_storage ss;
	socklen_t sa_len;
	struct sockaddr_un *sun;
	struct sockaddr_in *sin;
	ldmsd_cfg_cleanup_fn_t cleanup_fn;
	int rc;

	if (0 == strcasecmp(xprt_str, "unix")) {
		char *sockname = port_str;
		sun = (struct sockaddr_un *)&ss;
		sa_len = sizeof(*sun);
		if (!sockname) {
			char *sockpath = getenv("LDMSD_SOCKPATH");
			if (!sockpath)
				sockpath = "/var/run";
			sockname = malloc(sizeof(LDMSD_CONTROL_SOCKNAME) + strlen(sockpath) + 2);
			if (!sockname) {
				ldmsd_log(LDMSD_LERROR, "Out of memory\n");
				return -1;
			}
			sprintf(sockname, "%s/%s", sockpath, LDMSD_CONTROL_SOCKNAME);
		}
		memset(sun, 0, sizeof(*sun));
		sun->sun_family = AF_UNIX;
		strncpy(sun->sun_path, sockname,
			sizeof(struct sockaddr_un) - sizeof(short));
		cleanup_fn = ldmsd_cfg_unix_cleanup;
	} else if (0 == strcasecmp(xprt_str, "sock")) {
		sin = (struct sockaddr_in *)&ss;
		sa_len = sizeof(*sin);
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = 0;
		sin->sin_port = htons(atoi(port_str));
		cleanup_fn = ldmsd_cfg_sock_cleanup;
	} else {
		ldmsd_log(LDMSD_LERROR,
			  "Unrecognized configuration transport string %s\n",
			  xprt_str);
		return -1;
	}
	cfg_clnt_t clnt = calloc(1, sizeof(*clnt));
	if (!clnt) {
		ldmsd_log(LDMSD_LERROR,
			  "Memory allocation failure creating configuration client.\n");
		return -1;
	}
	clnt->xprt.cleanup_fn = cleanup_fn;
	clnt->secretword = strdup(secretword);
	clnt->xprt.sock.ss = ss;
	clnt->xprt.sock.fd = socket(ss.ss_family, SOCK_STREAM, 0);
	if (clnt->xprt.sock.fd < 0) {
		ldmsd_log(LDMSD_LERROR, "A configuration socket could not be created, "
			  "error = %d.\n", errno);
		goto err;
	}
	rc = bind(clnt->xprt.sock.fd, (struct sockaddr *)&ss, sa_len);
	if (rc < 0) {
		ldmsd_log(LDMSD_LERROR, "Error %d binding to configuration socket %s:%s\n",
			  errno, xprt_str, port_str);
		goto err;
	}
	rc = listen(clnt->xprt.sock.fd, 10);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Error %d listening on configuration socket %s:%s\n",
			  errno, xprt_str, port_str);
		goto err;
	}

	pthread_mutex_lock(&clnt_list_lock);
	LIST_INSERT_HEAD(&clnt_list, clnt, entry);
	pthread_mutex_unlock(&clnt_list_lock);

	rc = pthread_create(&clnt->thread, NULL, config_cm_proc, clnt);
	if (rc) {
		ldmsd_log(LDMSD_LERROR,
			  "Error creating configuration connection management thread.\n");
		goto err_1;
	}
	return 0;
 err_1:
	pthread_mutex_lock(&clnt_list_lock);
	LIST_REMOVE(clnt, entry);
	pthread_mutex_unlock(&clnt_list_lock);

 err:
	close(clnt->xprt.sock.fd);
	free(clnt);
	return -1;
}

static int send_ldms_fn(ldmsd_cfg_xprt_t xprt, char *data, size_t data_len)
{
        return ldms_xprt_send(xprt->ldms.ldms, data, data_len);
}

static void __recv_msg(ldms_t x, char *data, size_t data_len)
{
        ldmsd_req_hdr_t request = (ldmsd_req_hdr_t)data;
        struct ldmsd_cfg_xprt_s xprt;
        xprt.ldms.ldms = x;
        xprt.send_fn = send_ldms_fn;
        switch (request->type) {
        case LDMSD_REQ_TYPE_CONFIG_CMD:
                (void)ldmsd_process_config_request(&xprt, request, data_len);
                break;
        case LDMSD_REQ_TYPE_CONFIG_RESP:
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
                ldms_xprt_put(x);
                break;
        case LDMS_XPRT_EVENT_RECV:
                __recv_msg(x, e->data, e->data_len);
                break;
        default:
                assert(0);
                break;
        }
}

int listen_on_ldms_xprt(char *xprt_str, char *port_str, char *secretword)
{
        int port_no;
        ldms_t l = NULL;
        int ret;
        struct sockaddr_in sin;

        if (!port_str || port_str[0] == '\0')
                port_no = LDMS_DEFAULT_PORT;
        else
                port_no = atoi(port_str);
#if OVIS_LIB_HAVE_AUTH
        l = ldms_xprt_with_auth_new(xprt_str, ldmsd_linfo,
                secretword);
#else
        l = ldms_xprt_new(xprt_str, ldmsd_linfo);
#endif /* OVIS_LIB_HAVE_AUTH */
        if (!l) {
                ldmsd_log(LDMSD_LERROR, "The transport specified, "
                                "'%s', is invalid.\n", xprt_str);
                cleanup(6, "error creating transport");
        }
	cfg_clnt_t clnt = calloc(1, sizeof(*clnt));
	if (!clnt) {
		ldmsd_log(LDMSD_LERROR,
			  "Memory allocation failure creating configuration client.\n");
		return -1;
	}
	clnt->xprt.cleanup_fn = ldmsd_cfg_ldms_xprt_cleanup;
	clnt->secretword = strdup(secretword);
        clnt->xprt.ldms.ldms = l;
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = 0;
        sin.sin_port = htons(port_no);
        ret = ldms_xprt_listen(l, (struct sockaddr *)&sin, sizeof(sin),
			       __listen_connect_cb, NULL);
        if (ret) {
                ldmsd_log(LDMSD_LERROR, "Error %d listening on the '%s' "
                                "transport.\n", ret, xprt_str);
                cleanup(7, "error listening on transport");
        }
        ldmsd_log(LDMSD_LINFO, "Listening on transport %s:%s\n",
                        xprt_str, port_str);
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
	glob_t pglob;

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
