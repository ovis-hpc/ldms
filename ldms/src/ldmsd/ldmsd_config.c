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
#include "ldmsd_translator.h"
#include "config.h"

pthread_mutex_t host_list_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(host_list_s, hostspec) host_list;
pthread_mutex_t sp_list_lock = PTHREAD_MUTEX_INITIALIZER;

#define LDMSD_PLUGIN_LIBPATH_MAX	1024

void ldmsd_cfg_xprt_cleanup(ldmsd_cfg_xprt_t xprt)
{
	free(xprt);
}

void ldmsd_cfg_xprt_cfgfile_cleanup(ldmsd_cfg_xprt_t xprt)
{
	ldmsd_plugin_inst_put(xprt->file.t);
	ldmsd_cfg_xprt_cleanup(xprt);
}

void ldmsd_cfg_xprt_ldms_cleanup(ldmsd_cfg_xprt_t xprt)
{
	ldms_xprt_put(xprt->ldms.ldms);
	ldmsd_cfg_xprt_cleanup(xprt);
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
	size_t cnt;
	memset(regex, 0, sizeof *regex);
	int rc = regcomp(regex, regex_str, REG_EXTENDED | REG_NOSUB);
	if (rc) {
		cnt = snprintf(errbuf, errsz, "Failed to compile '%s'.", regex_str);
		(void)regerror(rc,
			       regex,
			       &errbuf[cnt],
			       errsz - cnt);
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
		    int64_t udata, ldmsd_sec_ctxt_t sctxt)
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
		int64_t base, int64_t inc, char *errstr, size_t errsz,
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

static inline void __log_sent_req(ldmsd_cfg_xprt_t xprt, ldmsd_rec_hdr_t req)
{
	if (!ldmsd_req_debug) /* defined in ldmsd_request.c */
		return;
	/* req is in network byte order */
	struct ldmsd_rec_hdr_s hdr;
	hdr.type = ntohl(req->type);
	hdr.flags = ntohl(req->flags);
	hdr.msg_no = ntohl(req->msg_no);
	hdr.rec_len = ntohl(req->rec_len);

	ldmsd_lall("sending msg_no: %d:%lu, type: %d, flags: %#o, "
		   "rec_len: %u\n",
		   hdr.msg_no, (uint64_t)(unsigned long)xprt->xprt,
		   hdr.type, hdr.flags, hdr.rec_len);
}

static int send_ldms_fn(ldmsd_cfg_xprt_t xprt, char *data, size_t data_len)
{
	__log_sent_req(xprt, (void*)data);
	return ldms_xprt_send(xprt->ldms.ldms, data, data_len);
}

void ldmsd_recv_msg(ldms_t x, char *data, size_t data_len)
{
	ldmsd_rec_hdr_t rec = (ldmsd_rec_hdr_t)data;
	char *errstr;
	ldmsd_cfg_xprt_t xprt;
	ldmsd_req_ctxt_t reqc;
	int rc;

	xprt = ldmsd_cfg_xprt_ldms_new(x);
	if (!xprt) {
		ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
		return;
	}
	ldmsd_ntoh_rec_hdr(rec);

	if (rec->rec_len > xprt->max_msg) {
		/* Send the record length advice */
		rc = ldmsd_send_err_rec_adv(xprt, rec->msg_no, xprt->max_msg);
		if (rc)
			goto oom;
		goto out;
	}

	errno = 0;
	reqc = ldmsd_handle_record(rec, xprt);
	if (!reqc)
		goto out;

	ldmsd_req_ctxt_ref_get(reqc, "handle");
	switch (rec->type) {
	case LDMSD_MSG_TYPE_REQ:
		rc = ldmsd_process_msg_request(reqc);
		break;
	case LDMSD_MSG_TYPE_RESP:
		rc = ldmsd_process_msg_response(reqc);
		break;
	case LDMSD_MSG_TYPE_STREAM:
		rc = ldmsd_process_msg_stream(reqc);
		break;
	default:
		errstr = "ldmsd received an unrecognized request type";
		ldmsd_log(LDMSD_LERROR, "%s\n", errstr);
		rc = EINVAL;
		goto err;
	}
	ldmsd_req_ctxt_ref_put(reqc, "handle");
	if (rc) {
		/*
		 * Do nothing.
		 *
		 * Assume that the error message was logged and/or sent
		 * to the peer.
		 */
	}

out:
	ldmsd_cfg_xprt_ref_put(xprt, "create");
	return;

oom:
	errstr = "ldmsd out of memory";
	ldmsd_log(LDMSD_LCRITICAL, "%s\n", errstr);
err:
	__ldmsd_send_error(xprt, rec->msg_no, NULL, rc, errstr);
	ldmsd_cfg_xprt_ref_put(xprt, "create");
	return;
}

ldmsd_cfg_xprt_t ldmsd_cfg_xprt_new()
{
	ldmsd_cfg_xprt_t xprt = calloc(1, sizeof(*xprt));
	if (!xprt) {
		ldmsd_log(LDMSD_LCRITICAL, "Out of memory\n");
		return NULL;
	}
	return xprt;
}

static int cli_error_report(ldmsd_cfg_xprt_t xprt, char *data, size_t data_len)
{
	json_entity_t results, key, v, msg;
	json_entity_t reply = (json_entity_t)data;
	int status;

	status = json_value_int(json_value_find(reply, "status"));
	if (status) {
		ldmsd_log(LDMSD_LCRITICAL, "%s\n",
			json_value_str(json_value_find(reply, "msg"))->str);
		return 0;
	}
	results = json_value_find(reply, "result");
	if (!results)
		return 0;
	for (key = json_attr_first(results); key; key = json_attr_next(key)) {
		v = json_attr_value(key);
		status = json_value_int(json_value_find(v, "status"));
		if (status) {
			msg = json_value_find(v, "msg");
			if (msg) {
				/* TODO: Think about how to report the error.
				 * It is possible that only the status is provided,
				 * no error messages.
				 */
				ldmsd_log(LDMSD_LCRITICAL, "Error %d: %s\n",
					status, json_value_str(msg)->str);
			}
		}
	}
	return 0;
}

static int cfgfile_error_report(ldmsd_cfg_xprt_t xprt, char *data, size_t data_len)
{
	int rc;
	json_entity_t reply = (json_entity_t)data;
	ldmsd_translator_type_t t = (ldmsd_translator_type_t)xprt->file.t->base;

	rc = t->error_report(xprt->file.t, reply);
	if (rc) {
		ldmsd_log(LDMSD_LCRITICAL, "Error %d: Failed to report "
					"configuration error of the request ID %ld "
					"from translator '%s'\n",
					rc,
					json_value_int(json_value_find(reply, "id")),
					t->base.inst->inst_name);
	}
	return 0;
}

void ldmsd_cfg_xprt_cli_init(ldmsd_cfg_xprt_t xprt)
{
	xprt->type = LDMSD_CFG_XPRT_CLI;
	xprt->send_fn = cli_error_report;
	xprt->max_msg = LDMSD_CFG_FILE_XPRT_MAX_REC;
	xprt->trust = 1;
	ref_init(&xprt->ref, "create", (ref_free_fn_t)ldmsd_cfg_xprt_cleanup, xprt);
}

void ldmsd_cfg_xprt_cfgfile_init(ldmsd_cfg_xprt_t xprt, ldmsd_plugin_inst_t t)
{
	xprt->type = LDMSD_CFG_XPRT_CLI;
	xprt->send_fn = cfgfile_error_report;
	xprt->max_msg = LDMSD_CFG_FILE_XPRT_MAX_REC;
	xprt->trust = 1;
	ldmsd_plugin_inst_get(t);
	xprt->file.t = t;
	ref_init(&xprt->ref, "create",
			(ref_free_fn_t)ldmsd_cfg_xprt_cfgfile_cleanup, xprt);
}

void ldmsd_cfg_xprt_ldms_init(ldmsd_cfg_xprt_t xprt, ldms_t ldms)
{
	ldms_xprt_get(ldms);
	xprt->type = LDMSD_CFG_XPRT_LDMS;
	xprt->ldms.ldms = ldms;
	xprt->send_fn = send_ldms_fn;
	xprt->max_msg = ldms_xprt_msg_max(ldms);
	xprt->trust = 0; /* don't trust any network for CMD expansion */
	ref_init(&xprt->ref, "create", (ref_free_fn_t)ldmsd_cfg_xprt_ldms_cleanup, xprt);
}

ldmsd_cfg_xprt_t ldmsd_cfg_xprt_ldms_new(ldms_t x)
{
	ldmsd_cfg_xprt_t xprt = ldmsd_cfg_xprt_new();
	if (!xprt)
		return NULL;
	ldmsd_cfg_xprt_ldms_init(xprt, x);
	return xprt;
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
				/* TODO: Should we call ldmsd_plugin_inst_put()
				 *
				 */
				ldmsd_plugin_inst_put(inst);
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
