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
#include "config.h"

#define REPLYBUF_LEN 4096
static char replybuf[REPLYBUF_LEN];
char myhostname[HOST_NAME_MAX+1];
pthread_t ctrl_thread = (pthread_t)-1;
pthread_t inet_ctrl_thread = (pthread_t)-1;
int muxr_s = -1;
int inet_sock = -1;
int inet_listener = -1;
char *sockname = NULL;

static int cleanup_requested = 0;
int bind_succeeded;

extern struct event_base *get_ev_base(int idx);
extern void release_ev_base(int idx);
int find_least_busy_thread();
int ldmsd_start_sampler(char *plugin_name, char *interval, char *offset,
			char *err_str);
int ldmsd_oneshot_sample(char *plugin_name, char *ts, char *err_str);
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

int send_reply(int sock, struct sockaddr *sa, ssize_t sa_len,
	       char *msg, ssize_t msg_len)
{
	struct msghdr reply;
	struct iovec iov;

	reply.msg_name = sa;
	reply.msg_namelen = sa_len;
	iov.iov_base = msg;
	iov.iov_len = msg_len;
	reply.msg_iov = &iov;
	reply.msg_iovlen = 1;
	reply.msg_control = NULL;
	reply.msg_controllen = 0;
	reply.msg_flags = 0;
	sendmsg(sock, &reply, 0);
	return 0;
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

struct ldmsd_plugin_cfg *new_plugin(char *plugin_name, char err_str[LEN_ERRSTR])
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
		sprintf(err_str, "dlerror %s", dlerror());
		goto err;
	}

	ldmsd_plugin_get_f pget = dlsym(d, "get_plugin");
	if (!pget) {
		sprintf(err_str,
			"The library is missing the get_plugin() function.");
		goto err;
	}
	lpi = pget(ldmsd_msg_logger);
	if (!lpi) {
		sprintf(err_str, "The plugin could not be loaded.");
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
	sprintf(err_str, "No memory");
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

void __process_info_prdcr(enum ldmsd_loglevel llevel)
{
	ldmsd_prdcr_t prdcr;
	ldmsd_log(llevel, "\n");
	ldmsd_log(llevel, "========================================================================\n");
	ldmsd_log(llevel, "%s\n", "Producers");
#ifdef LDMSD_UPDATE_TIME
	ldmsd_log(llevel, "%-20s %-20s %-8s %-12s %-10s %s\n",
		 "Name", "Host", "Port", "ConnIntrvl", "State", "Schdule update time(usec)");
#else /* LDMSD_UPDATE_TIME */
	ldmsd_log(llevel, "%-20s %-20s %-8s %-12s %s\n",
		 "Name", "Host", "Port", "ConnIntrvl", "State");
#endif /* LDMSD_UPDATE_TIME */
	ldmsd_log(llevel, "-------------------- -------------------- ---------- ---------- ----------\n");
	ldmsd_cfg_lock(LDMSD_CFGOBJ_PRDCR);
	for (prdcr = ldmsd_prdcr_first(); prdcr; prdcr = ldmsd_prdcr_next(prdcr)) {
#ifdef LDMSD_UPDATE_TIME
		ldmsd_log(llevel, "%-20s %-20s %-8hu %-12d %-10s %lf\n",
			 prdcr->obj.name, prdcr->host_name, prdcr->port_no,
			 prdcr->conn_intrvl_us,
			 prdcr_state_str(prdcr->conn_state),
			 prdcr->sched_update_time);
#else /* LDMSD_UPDATE_TIME */
		ldmsd_log(llevel, "%-20s %-20s %-8hu %-12d %s\n",
			 prdcr->obj.name, prdcr->host_name, prdcr->port_no,
			 prdcr->conn_intrvl_us,
			 prdcr_state_str(prdcr->conn_state));
#endif /* LDMSD_UPDATE_TIME */
		ldmsd_prdcr_lock(prdcr);
		ldmsd_prdcr_set_t prv_set;
#ifdef LDMSD_UPDATE_TIME
		ldmsd_log(llevel, "    %-32s %-20s %-10s %s\n",
			 "Instance Name", "Schema Name", "State", "Update time (usec)");
		for (prv_set = ldmsd_prdcr_set_first(prdcr); prv_set;
		     prv_set = ldmsd_prdcr_set_next(prv_set)) {
			ldmsd_log(llevel, "    %-32s %-20s %-10s %lf\n",
				 prv_set->inst_name,
				 prv_set->schema_name,
				 ldmsd_prdcr_set_state_str(prv_set->state),
				 prv_set->updt_duration);
		}
#else /* LDMSD_UPDATE_TIME */
		ldmsd_log(llevel, "    %-32s %-20s %s\n",
			 "Instance Name", "Schema Name", "State");
		for (prv_set = ldmsd_prdcr_set_first(prdcr); prv_set;
		     prv_set = ldmsd_prdcr_set_next(prv_set)) {
			ldmsd_log(llevel, "    %-32s %-20s %s\n",
				 prv_set->inst_name,
				 prv_set->schema_name,
				 ldmsd_prdcr_set_state_str(prv_set->state));
		}
#endif /* LDMSD_UPDATE_TIME */

		ldmsd_prdcr_unlock(prdcr);
	}
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_PRDCR);
	ldmsd_log(llevel, "-------------------- -------------------- ---------- ---------- ----------\n");
}

void __process_info_updtr(enum ldmsd_loglevel llevel)
{
	char offset_s[15];
	ldmsd_updtr_t updtr;
	ldmsd_log(llevel, "\n");
	ldmsd_log(llevel, "========================================================================\n");
	ldmsd_log(llevel, "%s\n", "Updaters");
#ifdef LDMSD_UPDATE_TIME
	ldmsd_log(llevel, "%-20s %-14s %-14s %-10s %-10s %s\n",
		 "Name", "Update Intrvl", "Offset", "State", "Submitting time (usec)", "Update time (usec)");
#else /* LDMSD_UDPATE_TIME */
	ldmsd_log(llevel, "%-20s %-14s %-14s %s\n",
		 "Name", "Update Intrvl", "Offset", "State");
#endif /* LDMSD_UPDATE_TIME */

	ldmsd_log(llevel, "-------------------- -------------- -------------- ----------\n");
	ldmsd_cfg_lock(LDMSD_CFGOBJ_UPDTR);
	for (updtr = ldmsd_updtr_first(); updtr; updtr = ldmsd_updtr_next(updtr)) {
		if (updtr->updt_task_flags & LDMSD_TASK_F_SYNCHRONOUS)
			sprintf(offset_s, "%d", updtr->updt_offset_us);
		else
			sprintf(offset_s, "ASYNC");
#ifdef LDMSD_UPDATE_TIME
		ldmsd_log(llevel, "%-20s %-14d %-14s %-10s %lf %lf\n",
			 updtr->obj.name, updtr->updt_intrvl_us,
			 offset_s,
			 ldmsd_updtr_state_str(updtr->state),
			 updtr->sched_duration,
			 updtr->duration);
#else /* LDMSD_UPDATE_TIME */
		ldmsd_log(llevel, "%-20s %-14d %-14s %s\n",
			 updtr->obj.name, updtr->updt_intrvl_us,
			 offset_s,
			 ldmsd_updtr_state_str(updtr->state));
#endif /* LDMSD_UPDATE_TIME */

		ldmsd_updtr_lock(updtr);
		ldmsd_name_match_t match;
		ldmsd_log(llevel, "    Metric Set Match Specifications (empty == All)\n");
		ldmsd_log(llevel, "    %-10s %s\n", "Compare To", "Value");
		ldmsd_log(llevel, "    ----------------------------------------\n");
		for (match = ldmsd_updtr_match_first(updtr); match;
		     match = ldmsd_updtr_match_next(match)) {
			ldmsd_log(llevel, "    %-10s %s\n",
				 match_selector_str(match->selector),
				 match->regex_str);
		}
		ldmsd_log(llevel, "    ----------------------------------------\n");
		ldmsd_prdcr_ref_t ref;
		ldmsd_prdcr_t prdcr;
		ldmsd_log(llevel, "    Producers (empty == None)\n");
		ldmsd_log(llevel, "    %-10s %-10s %-10s %s\n", "Name", "Transport", "Host", "Port");
		ldmsd_log(llevel, "    ----------------------------------------\n");
		for (ref = ldmsd_updtr_prdcr_first(updtr); ref;
		     ref = ldmsd_updtr_prdcr_next(ref)) {
			prdcr = ref->prdcr;
			ldmsd_log(llevel, "    %-10s %-10s %-10s %hu\n",
				 prdcr->obj.name,
				 prdcr->xprt_name,
				 prdcr->host_name,
				 prdcr->port_no);
		}
		ldmsd_log(llevel, "    ----------------------------------------\n");
		ldmsd_updtr_unlock(updtr);
	}
	ldmsd_log(llevel, "-------------------- -------------- ----------\n");
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_UPDTR);
}

void __process_info_strgp(enum ldmsd_loglevel llevel)
{
	ldmsd_strgp_t strgp;
	ldmsd_log(llevel, "\n");
	ldmsd_log(llevel, "========================================================================\n");
	ldmsd_log(llevel, "%s\n", "Storage Policies");
	ldmsd_log(llevel, "%-15s %-15s %-15s %-15s %-s\n",
		 "Name", "Container", "Schema", "Back End", "State");
	ldmsd_log(llevel, "--------------- --------------- --------------- --------------- --------\n");
	ldmsd_cfg_lock(LDMSD_CFGOBJ_STRGP);
	for (strgp = ldmsd_strgp_first(); strgp; strgp = ldmsd_strgp_next(strgp)) {
		ldmsd_log(llevel, "%-15s %-15s %-15s %-15s %-8s\n",
			 strgp->obj.name,
			 strgp->container, strgp->schema, strgp->plugin_name,
			 ldmsd_strgp_state_str(strgp->state));
		ldmsd_strgp_lock(strgp);
		ldmsd_name_match_t match;
		ldmsd_log(llevel, "    Producer Match Specifications (empty == All)\n");
		ldmsd_log(llevel, "    %s\n", "Name");
		ldmsd_log(llevel, "    ----------------------------------------\n");
		for (match = ldmsd_strgp_prdcr_first(strgp); match;
		     match = ldmsd_strgp_prdcr_next(match)) {
			ldmsd_log(llevel, "    %s\n", match->regex_str);
		}
		ldmsd_log(llevel, "    ----------------------------------------\n");

		ldmsd_log(llevel, "    Metrics (empty == All)\n");
		ldmsd_log(llevel, "    %s\n", "Name");
		ldmsd_log(llevel, "    ----------------------------------------\n");
		ldmsd_strgp_metric_t metric;
		for (metric = ldmsd_strgp_metric_first(strgp); metric;
		     metric = ldmsd_strgp_metric_next(metric)) {
			ldmsd_log(llevel, "    %s\n", metric->name);
		}
		ldmsd_log(llevel, "    ----------------------------------------\n");
		ldmsd_strgp_unlock(strgp);
	}
	ldmsd_log(llevel, "--------------- --------------- --------------- --------------- ---------------\n");
	ldmsd_cfg_unlock(LDMSD_CFGOBJ_STRGP);
}

/**
 * Return information about the state of the daemon
 */
int process_info(char *replybuf, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	int llevel = LDMSD_LALL;

	char *name;
	name = av_value(avl, "name");
	if (name) {
		if (0 == strcmp(name, "prdcr")) {
			__process_info_prdcr(llevel);
		} else if (0 == strcmp(name, "updtr")) {
			__process_info_updtr(llevel);
		} else if (0 == strcmp(name, "strgp")) {
			__process_info_strgp(llevel);
		} else {
			sprintf(replybuf, "%dInvalid name '%s'. "
					"The choices are prdcr, updtr, strgp.",
					-EINVAL, name);
		}
		sprintf(replybuf, "0");
		return 0;
	}

	extern int ev_thread_count;
	extern pthread_t *ev_thread;
	extern int *ev_count;
	int i;
	struct hostspec *hs;
	int verbose = 0;
	char *vb = av_value(avl, "verbose");
	if (vb && (strcasecmp(vb, "true") == 0 ||
			strcasecmp(vb, "t") == 0))
		verbose = 1;

	ldmsd_log(llevel, "Event Thread Info:\n");
	ldmsd_log(llevel, "%-16s %s\n", "----------------", "------------");
	ldmsd_log(llevel, "%-16s %s\n", "Thread", "Task Count");
	ldmsd_log(llevel, "%-16s %s\n", "----------------", "------------");
	for (i = 0; i < ev_thread_count; i++) {
		ldmsd_log(llevel, "%-16p %d\n",
			 (void *)ev_thread[i], ev_count[i]);
	}

	ldmsd_log(llevel, "========================================================================\n");
	__process_info_prdcr(llevel);

	__process_info_updtr(llevel);

	__process_info_strgp(llevel);

	sprintf(replybuf, "0");
	return 0;
}

int process_version(char *replybuf, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	struct ldms_version version;
	ldms_version_get(&version);
	sprintf(replybuf, "0LDMS Version: %hhu.%hhu.%hhu.%hhu\n",
					version.major,
					version.minor,
					version.patch,
					version.flags);
	return 0;
}

int process_verbosity_change(char *replybuf, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	char *level_s;
	char err_str[LEN_ERRSTR];

	level_s = av_value(avl, "level");
	if (!level_s) {
		sprintf(replybuf, "%d The level was not specified\n", -EINVAL);
		goto out;
	}

	err_str[0] = '\0';
	int rc = ldmsd_loglevel_set(level_s);
	if (rc < 0) {
		snprintf(err_str, LEN_ERRSTR, "Invalid verbosity level, "
				"expecting DEBUG, INFO, ERROR, CRITICAL and QUIET\n");
	}
	sprintf(replybuf, "%d%s", -rc, err_str);

#ifdef DEBUG
	ldmsd_log(LDMSD_LDEBUG, "TEST DEBUG\n");
	ldmsd_log(LDMSD_LINFO, "TEST INFO\n");
	ldmsd_log(LDMSD_LERROR, "TEST ERROR\n");
	ldmsd_log(LDMSD_LCRITICAL, "TEST CRITICAL\n");
	ldmsd_log(LDMSD_LALL, "TEST SUPREME\n");
#endif /* DEBUG */

out:
	return 0;
}

int ldmsd_compile_regex(regex_t *regex, const char *regex_str, char *errbuf, size_t errsz)
{
	memset(regex, 0, sizeof *regex);
	int rc = regcomp(regex, regex_str, REG_NOSUB);
	if (rc) {
		sprintf(errbuf, "22");
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
int ldmsd_load_plugin(char *plugin_name, char err_str[LEN_ERRSTR])
{

	int rc = 0;
	err_str[0] = '\0';

	struct ldmsd_plugin_cfg *pi = ldmsd_get_plugin(plugin_name);
	if (pi) {
		snprintf(err_str, LEN_ERRSTR, "Plugin already loaded");
		rc = EEXIST;
		goto out;
	}
	pi = new_plugin(plugin_name, err_str);
	if (!pi)
		rc = 1;
out:
	return rc;
}

/*
 * Destroy and unload the plugin
 */
int ldmsd_term_plugin(char *plugin_name, char err_str[LEN_ERRSTR])
{
	int rc = 0;
	struct ldmsd_plugin_cfg *pi;
	err_str[0] = '\0';

	pi = ldmsd_get_plugin(plugin_name);
	if (!pi) {
		rc = ENOENT;
		snprintf(err_str, LEN_ERRSTR, "plugin not found.");
		return rc;
	}
	pthread_mutex_lock(&pi->lock);
	if (pi->ref_count) {
		snprintf(err_str, LEN_ERRSTR, "The specified plugin has "
				"active users and cannot be terminated.");
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
			struct attr_value_list *_kw_list,
			char err_str[LEN_ERRSTR])
{
	int rc = 0;
	struct ldmsd_plugin_cfg *pi;
	err_str[0] = '\0';

	pi = ldmsd_get_plugin(plugin_name);
	if (!pi) {
		rc = ENOENT;
		snprintf(err_str, LEN_ERRSTR, "The plugin was not found.");
		goto out;
	}
	pthread_mutex_lock(&pi->lock);
	rc = pi->plugin->config(pi->plugin, _kw_list, _av_list);
	if (rc)
		snprintf(err_str, LEN_ERRSTR, "Plugin configuration error.");
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
int ldmsd_set_udata(char *set_name, char *metric_name,
		char *udata_s, char err_str[LEN_ERRSTR])
{
	ldms_set_t set;
	err_str[0] = '\0';
	set = ldms_set_by_name(set_name);
	if (!set) {
		snprintf(err_str, LEN_ERRSTR, "Set '%s' not found.", set_name);
		return ENOENT;
	}

	char *endptr;
	uint64_t udata = strtoull(udata_s, &endptr, 0);
	if (endptr[0] != '\0') {
		snprintf(err_str, LEN_ERRSTR, "User data '%s' invalid.",
								udata_s);
		return EINVAL;
	}

	int rc =_ldmsd_set_udata(set, metric_name, udata, err_str);
	ldms_set_delete(set);
	return rc;
}

int ldmsd_set_udata_regex(char *set_name, char *regex_str,
		char *base_s, char *inc_s, char err_str[LEN_ERRSTR])
{
	int rc = 0;
	ldms_set_t set;
	err_str[0] = '\0';
	set = ldms_set_by_name(set_name);
	if (!set) {
		snprintf(err_str, LEN_ERRSTR, "Set '%s' not found.", set_name);
		return ENOENT;
	}

	char *endptr;
	uint64_t base = strtoull(base_s, &endptr, 0);
	if (endptr[0] != '\0') {
		snprintf(err_str, LEN_ERRSTR, "User data base '%s' invalid.",
								base_s);
		return EINVAL;
	}

	int inc = 0;
	if (inc_s)
		inc = atoi(inc_s);

	regex_t regex;
	rc = ldmsd_compile_regex(&regex, regex_str, err_str, LEN_ERRSTR);
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
	ldms_set_delete(set);
	return 0;
}

int resolve(const char *hostname, struct sockaddr_in *sin)
{
	struct hostent *h;

	h = gethostbyname(hostname);
	if (!h) {
		printf("Error resolving hostname '%s'\n", hostname);
		return -1;
	}

	if (h->h_addrtype != AF_INET) {
		printf("Hostname '%s' resolved to an unsupported address family\n",
		       hostname);
		return -1;
	}

	memset(sin, 0, sizeof *sin);
	sin->sin_addr.s_addr = *(unsigned int *)(h->h_addr_list[0]);
	sin->sin_family = h->h_addrtype;
	return 0;
}

/*
 * Functions to process ldmsctl commands
 */
/* load plugin */
int process_load_plugin(char *replybuf, struct attr_value_list *av_list,
			struct attr_value_list *kw_list)
{
	char *plugin_name;
	char err_str[LEN_ERRSTR];

	plugin_name = av_value(av_list, "name");
	if (!plugin_name) {
		sprintf(replybuf, "%d The plugin name was not specified\n",
								-EINVAL);
		goto out;
	}

	int rc = ldmsd_load_plugin(plugin_name, err_str);
	sprintf(replybuf, "%d%s", -rc, err_str);
out:
	return 0;
}

/* terminate a plugin */
int process_term_plugin(char *replybuf, struct attr_value_list *av_list,
			struct attr_value_list *kw_list)
{
	char *plugin_name;
	char err_str[LEN_ERRSTR];

	plugin_name = av_value(av_list, "name");
	if (!plugin_name) {
		sprintf(replybuf, "%d The plugin name must be specified.",
								-EINVAL);
		goto out;
	}

	int rc = ldmsd_term_plugin(plugin_name, err_str);
	sprintf(replybuf, "%d%s", -rc, err_str);
out:
	return 0;
}

/* configure a plugin */
int process_config_plugin(char *replybuf, struct attr_value_list *av_list,
			  struct attr_value_list *kw_list)
{
	char *plugin_name;
	char err_str[LEN_ERRSTR];

	plugin_name = av_value(av_list, "name");
	if (!plugin_name) {
		sprintf(replybuf, "%d The plugin name must be specified.",
								-EINVAL);
		goto out;
	}

	int rc = ldmsd_config_plugin(plugin_name, av_list, kw_list, err_str);
	sprintf(replybuf, "%d%s", -rc, err_str);
out:
	return 0;
}

/* Assign user data to a metric */
int process_set_udata(char *replybuf, struct attr_value_list *av_list,
		struct attr_value_list *kw_list)
{
	char *set_name, *metric_name, *udata;
	char err_str[LEN_ERRSTR];
	char *attr;
	int rc = 0;

	attr = "set";
	set_name = av_value(av_list, attr);
	if (!set_name)
		goto einval;

	attr = "metric";
	metric_name = av_value(av_list, attr);
	if (!metric_name)
		goto einval;

	attr = "udata";
	udata = av_value(av_list, attr);
	if (!udata)
		goto einval;

	rc = ldmsd_set_udata(set_name, metric_name, udata, err_str);
	sprintf(replybuf, "%d%s", -rc, err_str);
	return 0;
einval:
	sprintf(replybuf, "%dThe attribute '%s' is required.\n", -EINVAL, attr);
	return 0;
}

int process_set_udata_regex(char *replybuf, struct attr_value_list *av_list,
		struct attr_value_list *kw_list)
{
	char *set_name, *regex, *base_s, *inc_s;
	char err_str[LEN_ERRSTR];
	char *attr;
	int rc = 0;

	attr = "set";
	set_name = av_value(av_list, attr);
	if (!set_name)
		goto einval;

	attr = "regex";
	regex = av_value(av_list, attr);
	if (!regex)
		goto einval;

	attr = "base";
	base_s = av_value(av_list, attr);
	if (!base_s)
		goto einval;

	attr = "incr";
	inc_s = av_value(av_list, attr);

	rc = ldmsd_set_udata_regex(set_name, regex, base_s, inc_s, err_str);
	sprintf(replybuf, "%d%s", -rc, err_str);
	return 0;

einval:
	sprintf(replybuf, "%dThe attribute '%s' is required.\n", -EINVAL, attr);
	return 0;
}

/* Start a sampler */
int process_start_sampler(char *replybuf, struct attr_value_list *av_list,
			  struct attr_value_list *kw_list)
{
	char *plugin_name, *interval, *offset;
	char err_str[LEN_ERRSTR];
	char *attr;

	attr = "name";
	plugin_name = av_value(av_list, "name");
	if (!plugin_name)
		goto einval;

	attr = "interval";
	interval = av_value(av_list, attr);
	if (!interval)
		goto einval;

	attr = "offset";
	offset = av_value(av_list, attr);

	int rc = ldmsd_start_sampler(plugin_name, interval, offset, err_str);
	sprintf(replybuf, "%d%s", -rc, err_str);
	return 0;
einval:
	sprintf(replybuf, "%dThe attribute '%s' is required.\n", -EINVAL, attr);
	return 0;
}

int process_oneshot_sample(char *replybuf, struct attr_value_list *av_list,
			   struct attr_value_list *kw_list)
{
	char *attr;
	char *plugin_name, *ts;
	char err_str[LEN_ERRSTR];

	attr = "name";
	plugin_name = av_value(av_list, attr);
	if (!plugin_name)
		goto einval;

	attr = "time";
	ts = av_value(av_list, attr);
	if (!ts)
		goto einval;

	int rc = ldmsd_oneshot_sample(plugin_name, ts, err_str);
	sprintf(replybuf, "%d%s", -rc, err_str);
	goto out;

einval:
	sprintf(replybuf, "%dThe attribute '%s' is required.\n", -EINVAL, attr);
out:
	return 0;
}

/* stop a sampler */
int process_stop_sampler(char *replybuf, struct attr_value_list *av_list,
			 struct attr_value_list *kw_list)
{
	char *plugin_name;
	char err_str[LEN_ERRSTR];

	plugin_name = av_value(av_list, "name");
	if (!plugin_name) {
		sprintf(replybuf, "%d The plugin name must be specified.",
								-EINVAL);
		goto out;
	}

	int rc = ldmsd_stop_sampler(plugin_name, err_str);
	sprintf(replybuf, "%d%s", -rc, err_str);
out:
	return 0;
}

int process_ls_plugins(char *replybuf, struct attr_value_list *av_list,
		       struct attr_value_list *kw_list)
{
	struct ldmsd_plugin_cfg *p;
	sprintf(replybuf, "0");
	LIST_FOREACH(p, &plugin_list, entry) {
		strcat(replybuf, p->name);
		strcat(replybuf, "\n");
		if (p->plugin->usage)
			strcat(replybuf, p->plugin->usage(p->plugin));
	}
	return 0;
}

int process_exit(char *replybuf, struct attr_value_list *av_list,
					struct attr_value_list *kw_list)
{
	cleanup_requested = 1;
	/* set flag for bottom of message handler loops to check for quit. */
	ldmsd_log(LDMSD_LINFO, "User requested exit.\n");
	sprintf(replybuf, "0cleanup request received.\n");
	return 0;
}

static int command_id(const char *command);

int process_config_line(char *line)
{
	char *cmd_s;
	long cmd_id;
	int tokens, rc, n, buf_rc;
	struct attr_value_list *av_list = NULL;
	struct attr_value_list *kw_list = NULL;
	/* skip leading spaces */
	while (isspace(*line)) {
		line++;
	}
	for (tokens = 0, cmd_s = line; cmd_s[0] != '\0';) {
		tokens++;
		/* find whitespace */
		while (cmd_s[0] != '\0' && !isspace(cmd_s[0]))
			cmd_s++;
		/* Now skip whitepace to next token */
		while (cmd_s[0] != '\0' && isspace(cmd_s[0]))
			cmd_s++;
	}
	rc = ENOMEM;
	av_list = av_new(tokens);
	kw_list = av_new(tokens);
	if (!av_list || !kw_list)
		goto cleanup;

	rc = tokenize(line, kw_list, av_list);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Memory allocation failure "
				"processing '%s'\n", line);
		rc = ENOMEM;
		goto cleanup;
	}

	cmd_s = av_name(kw_list, 0);
	if (!cmd_s) {
		ldmsd_log(LDMSD_LERROR, "Request is missing Id '%s'\n", line);
		rc = EINVAL;
		goto cleanup;
	}

	cmd_id = command_id(cmd_s);
	if (cmd_id >= 0 && cmd_id <= LDMSCTL_LAST_COMMAND
			&& cmd_table[cmd_id]) {
		replybuf[0] = 0; /* reset replybuf */
		rc = cmd_table[cmd_id](replybuf, av_list, kw_list);
		if (rc) {
			ldmsd_log(LDMSD_LERROR,
					"config '%s' error rc: %d\n",
					cmd_s, rc);
		}
		n = 0;
		sscanf(replybuf, "%d%n", &buf_rc, &n);
		if (buf_rc) {
			ldmsd_log(LDMSD_LERROR,
					"config '%s' error: %s\n",
					cmd_s, replybuf + n);
			rc = buf_rc;
		}
	} else {
		rc = EINVAL;
	}
cleanup:
	if (av_list)
		av_free(av_list);
	if (kw_list)
		av_free(kw_list);
	return rc;
}

int process_config_file(const char *path)
{
	int rc = 0;
	FILE *fin = NULL;
	char *buff = NULL;
	char *line;
	char *comment;
	ssize_t off = 0;
	size_t cfg_buf_len = LDMSD_MAX_CONFIG_STR_LEN;
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

	rc = process_config_line(line);
	if (rc)
		goto cleanup;

	off = 0;

	goto next_line;

cleanup:
	if (fin)
		fclose(fin);
	if (buff)
		free(buff);
	return rc;
}

int process_include(char *replybuf, struct attr_value_list *av_list,
					struct attr_value_list * kw_list)
{
	int rc;
	const char *path;
	path = av_name(kw_list, 1);
	if (!path)
		return EINVAL;
	rc = process_config_file(path);
	return rc;
}

int process_env(char *replybuf, struct attr_value_list *av_list,
					struct attr_value_list * kw_list)
{
	int rc = 0;
	int i;
	for (i = 0; i < av_list->count; i++) {
		struct attr_value *v = &av_list->list[i];
		rc = setenv(v->name, v->value, 1);
		if (rc)
			return rc;
	}
	return 0;
}

int process_log_rotate(char *replybuf, struct attr_value_list *av_list,
					struct attr_value_list *kw_list)
{
	int rc = ldmsd_logrotate();
	if (rc)
		sprintf(replybuf, "%d Failed to rotate the log file", -rc);
	else
		sprintf(replybuf, "%d", -rc);
	return 0;
}

extern int cmd_prdcr_add(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_prdcr_del(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_prdcr_start(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_prdcr_stop(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_prdcr_start_regex(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_prdcr_stop_regex(char *, struct attr_value_list *, struct attr_value_list *);

extern int cmd_updtr_add(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_updtr_del(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_updtr_match_add(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_updtr_match_del(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_updtr_prdcr_add(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_updtr_prdcr_del(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_updtr_start(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_updtr_stop(char *, struct attr_value_list *, struct attr_value_list *);

extern int cmd_strgp_add(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_strgp_del(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_strgp_prdcr_add(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_strgp_prdcr_del(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_strgp_metric_add(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_strgp_metric_del(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_strgp_start(char *, struct attr_value_list *, struct attr_value_list *);
extern int cmd_strgp_stop(char *, struct attr_value_list *, struct attr_value_list *);

struct cmd_str_id {
	const char *str;
	int id;
};

const struct cmd_str_id cmd_str_id_table[] = {
	/* This table need to be sorted by keyword for bsearch() */
	{  "config",             3   },
	{  "env",                18  },
	{  "exit",               12  },
	{  "include",            17  },
	{  "info",               9   },
	{  "load",               1   },
	{  "loglevel",           16  },
	{  "oneshot",            13  },
	{  "prdcr_add",          20  },
	{  "prdcr_del",          21  },
	{  "prdcr_start",        22  },
	{  "prdcr_start_regex",  24  },
	{  "prdcr_stop",         23  },
	{  "prdcr_stop_regex",   25  },
	{  "remove",             7   },
	{  "start",              4   },
	{  "stop",               5   },
	{  "strgp_add",          40  },
	{  "strgp_del",          41  },
	{  "strgp_metric_add",   44  },
	{  "strgp_metric_del",   45  },
	{  "strgp_prdcr_add",    42  },
	{  "strgp_prdcr_del",    43  },
	{  "strgp_start",        48  },
	{  "strgp_stop",         49  },
	{  "term",               2   },
	{  "udata",              10  },
	{  "udata_regex",        14  },
	{  "updtr_add",          30  },
	{  "updtr_del",          31  },
	{  "updtr_match_add",    32  },
	{  "updtr_match_del",    33  },
	{  "updtr_prdcr_add",    34  },
	{  "updtr_prdcr_del",    35  },
	{  "updtr_start",        38  },
	{  "updtr_stop",         39  },
	{  "usage",              0   },
	{  "version",            15  },
};

int cmd_str_id_cmp(const struct cmd_str_id *a, const struct cmd_str_id *b)
{
	return strcmp(a->str, b->str);
}

static int command_id(const char *command)
{
	struct cmd_str_id key = {command, -1};
	struct cmd_str_id *x = bsearch(&key, cmd_str_id_table,
			sizeof(cmd_str_id_table)/sizeof(*cmd_str_id_table),
			sizeof(*cmd_str_id_table), (void*)cmd_str_id_cmp);
	if (!x)
		return -1;
	return x->id;
}

ldmsctl_cmd_fn_t cmd_table[LDMSCTL_LAST_COMMAND+1] = {
	[LDMSCTL_LIST_PLUGINS] = process_ls_plugins,
	[LDMSCTL_LOAD_PLUGIN] =	process_load_plugin,
	[LDMSCTL_TERM_PLUGIN] =	process_term_plugin,
	[LDMSCTL_CFG_PLUGIN] =	process_config_plugin,
	[LDMSCTL_START_SAMPLER] = process_start_sampler,
	[LDMSCTL_STOP_SAMPLER] = process_stop_sampler,
	[LDMSCTL_INFO_DAEMON] = process_info,
	[LDMSCTL_SET_UDATA] = process_set_udata,
	[LDMSCTL_SET_UDATA_REGEX] = process_set_udata_regex,
	[LDMSCTL_EXIT_DAEMON] = process_exit,
	[LDMSCTL_ONESHOT_SAMPLE] = process_oneshot_sample,
	[LDMSCTL_VERSION] = process_version,
	[LDMSCTL_VERBOSE] = process_verbosity_change,
	[LDMSCTL_INCLUDE] = process_include,
	[LDMSCTL_ENV] = process_env,
	[LDMSCTL_LOGROTATE] = process_log_rotate,
	[LDMSCTL_PRDCR_ADD] = cmd_prdcr_add,
	[LDMSCTL_PRDCR_DEL] = cmd_prdcr_del,
	[LDMSCTL_PRDCR_START] = cmd_prdcr_start,
	[LDMSCTL_PRDCR_STOP] = cmd_prdcr_stop,
	[LDMSCTL_PRDCR_START_REGEX] = cmd_prdcr_start_regex,
	[LDMSCTL_PRDCR_STOP_REGEX] = cmd_prdcr_stop_regex,
	[LDMSCTL_UPDTR_ADD] = cmd_updtr_add,
	[LDMSCTL_UPDTR_DEL] = cmd_updtr_del,
	[LDMSCTL_UPDTR_MATCH_ADD] = cmd_updtr_match_add,
	[LDMSCTL_UPDTR_MATCH_DEL] = cmd_updtr_match_del,
	[LDMSCTL_UPDTR_PRDCR_ADD] = cmd_updtr_prdcr_add,
	[LDMSCTL_UPDTR_PRDCR_DEL] = cmd_updtr_prdcr_del,
	[LDMSCTL_UPDTR_START] = cmd_updtr_start,
	[LDMSCTL_UPDTR_STOP] = cmd_updtr_stop,
	[LDMSCTL_STRGP_ADD] = cmd_strgp_add,
	[LDMSCTL_STRGP_DEL] = cmd_strgp_del,
	[LDMSCTL_STRGP_PRDCR_ADD] = cmd_strgp_prdcr_add,
	[LDMSCTL_STRGP_PRDCR_DEL] = cmd_strgp_prdcr_del,
	[LDMSCTL_STRGP_METRIC_ADD] = cmd_strgp_metric_add,
	[LDMSCTL_STRGP_METRIC_DEL] = cmd_strgp_metric_del,
	[LDMSCTL_STRGP_START] = cmd_strgp_start,
	[LDMSCTL_STRGP_STOP] = cmd_strgp_stop,
};

int process_record(int fd,
		   struct sockaddr *sa, ssize_t sa_len,
		   char *command, ssize_t cmd_len)
{
	char *cmd_s;
	long cmd_id;
	struct attr_value_list *av_list;
	struct attr_value_list *kw_list;
	int tokens, rc;

	/*
	 * Count the number of spaces. That's the maximum number of
	 * tokens that could be present
	 */
	for (tokens = 0, cmd_s = command; cmd_s[0] != '\0';) {
		tokens++;
		/* find whitespace */
		while (cmd_s[0] != '\0' && !isspace(cmd_s[0]))
			cmd_s++;
		/* Now skip whitepace to next token */
		while (cmd_s[0] != '\0' && isspace(cmd_s[0]))
			cmd_s++;
	}
	rc = ENOMEM;
	av_list = av_new(tokens);
	kw_list = av_new(tokens);
	if (!av_list || !kw_list)
		goto out;
	rc = tokenize(command, kw_list, av_list);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Memory allocation failure "
				"processing '%s'\n", command);
		rc = ENOMEM;
		goto out;
	}

	cmd_s = av_name(kw_list, 0);
	if (!cmd_s) {
		ldmsd_log(LDMSD_LERROR, "Request is missing Id '%s'\n", command);
		rc = EINVAL;
		goto out;
	}

	cmd_id = strtoul(cmd_s, NULL, 0);
	if (cmd_id >= 0 && cmd_id <= LDMSCTL_LAST_COMMAND) {
		if (cmd_table[cmd_id]) {
			rc = cmd_table[cmd_id](replybuf, av_list, kw_list);
			goto out;
		}
	}
	sprintf(replybuf, "22Invalid command id %ld\n", cmd_id);
	rc = EINVAL;
 out:
	if (fd >= 0)
		send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	if (kw_list)
		av_free(kw_list);
	if (av_list)
		av_free(av_list);
	return rc;
}

int process_message(int sock, struct msghdr *msg, ssize_t msglen)
{
	return process_record(sock,
			      msg->msg_name, msg->msg_namelen,
			      msg->msg_iov->iov_base, msglen);
}

void *ctrl_thread_proc(void *v)
{
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
	iov.iov_base = lbuf;
	do {
		ssize_t msglen;
		ss.ss_family = AF_UNIX;
		msg.msg_name = &ss;
		msg.msg_namelen = sizeof(ss);
		iov.iov_len = cfg_buf_len;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = 0;
		msglen = recvmsg(muxr_s, &msg, 0);
		if (msglen <= 0)
			break;
		process_message(muxr_s, &msg, msglen);
		if (cleanup_requested)
			break;
	} while (1);
	if (cleanup_requested) {
		/* Reset it to prevent deadlock in cleanup */
		ctrl_thread = (pthread_t)-1;
		cleanup(0,"user quit");
	}
	free(lbuf);
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
	muxr_s = socket(AF_UNIX, SOCK_DGRAM, 0);
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

	ret = pthread_create(&ctrl_thread, NULL, ctrl_thread_proc, 0);
	if (ret) {
		ldmsd_log(LDMSD_LERROR, "Error %d creating "
				"the control pthread'.\n");
		return -1;
	}
	return 0;
}

extern int
process_request(int fd, struct msghdr *msg, size_t msg_len);

void *inet_ctrl_thread_proc(void *args)
{
#if OVIS_LIB_HAVE_AUTH
	const char *secretword = (const char *)args;
#endif
	struct msghdr msg;
	struct iovec iov;
	unsigned char *lbuf;
	struct sockaddr_in sin;
	struct sockaddr rem_sin;
	socklen_t addrlen;
	size_t cfg_buf_len = LDMSD_MAX_CONFIG_STR_LEN;
	char *env = getenv("LDMSD_MAX_CONFIG_STR_LEN");
	if (env)
		cfg_buf_len = strtol(env, NULL, 0);
	lbuf = malloc(cfg_buf_len);
	if (!lbuf) {
		ldmsd_log(LDMSD_LERROR,
			  "Fatal error allocating %zu bytes for config string.\n",
			  cfg_buf_len);
		cleanup(1, "inet ctrl thread out of memory");
	}
	iov.iov_base = lbuf;
loop:
	inet_sock = accept(inet_listener, &rem_sin, &addrlen);
	if (inet_sock < 0) {
		ldmsd_log(LDMSD_LERROR, "Error %d failed to setting up the config "
				"listener.\n", inet_sock);
		goto loop;
	}

#if OVIS_LIB_HAVE_AUTH

#include <string.h>
#include "ovis_auth/auth.h"

#define _str(x) #x
#define str(x) _str(x)
	struct ovis_auth_challenge auth_ch;
	int rc;
	if (secretword && secretword[0] != '\0') {
		uint64_t ch = ovis_auth_gen_challenge();
		char *psswd = ovis_auth_encrypt_password(ch, secretword);
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
		rc = send(inet_sock, (char *)&auth_ch, sizeof(auth_ch), 0);
		if (rc == -1) {
			ldmsd_log(LDMSD_LERROR, "Error %d failed to send "
					"the challenge to the controller.\n",
					errno);
			free(psswd_buf);
			free(psswd);
			goto loop;
		}
		rc = recv(inet_sock, psswd_buf, len - 1, 0);
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
			shutdown(inet_sock, SHUT_RDWR);
			close(inet_sock);
			free(psswd_buf);
			free(psswd);
			goto loop;
		}
		free(psswd);
		free(psswd_buf);
		int approved = 1;
		rc = send(inet_sock, (void *)&approved, sizeof(int), 0);
		if (rc == -1) {
			ldmsd_log(LDMSD_LERROR, "Error %d failed to send "
				"the init message to the controller.\n", errno);
			goto loop;
		}
	} else {
		/* Don't do authetication */
		auth_ch.hi = auth_ch.lo = 0;
		rc = send(inet_sock, (char *)&auth_ch, sizeof(auth_ch), 0);
		if (rc == -1) {
			ldmsd_log(LDMSD_LERROR, "Error %d failed to send "
					"the greeting to the controller.\n",
					errno);
			goto loop;
		}
	}
#else /* OVIS_LIB_HAVE_AUTH */
	uint64_t greeting = 0;
	int rc = send(inet_sock, (char *)&greeting, sizeof(uint64_t), 0);
	if (rc == -1) {
		ldmsd_log(LDMSD_LERROR, "Error %d failed to send "
				"the greeting to the controller.\n",
				errno);
		goto loop;
	}
#endif /* OVIS_LIB_HAVE_AUTH */
	do {
		struct ldmsd_req_hdr_s request;
		ssize_t msglen;
		sin.sin_family = AF_INET;
		msg.msg_name = &sin;
		msg.msg_namelen = sizeof(sin);
		iov.iov_len = sizeof(request);
		iov.iov_base = &request;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = 0;
		/* Read the message header */
		msglen = recvmsg(inet_sock, &msg, MSG_PEEK);
		if (msglen <= 0)
			break;
		if (request.marker != LDMSD_RECORD_MARKER || (msglen < sizeof(request))) {
			iov.iov_len = cfg_buf_len;
			iov.iov_base = lbuf;
			msglen = recvmsg(inet_sock, &msg, 0);
			if (msglen <= 0)
				break;
			/* Process old style message */
			process_message(inet_sock, &msg, msglen);
			if (cleanup_requested)
				break;
		} else {
			if (cfg_buf_len < request.rec_len) {
				cfg_buf_len = request.rec_len;
				free(lbuf);
				lbuf = malloc(cfg_buf_len);
				if (!lbuf)
					break;
				iov.iov_base = lbuf;
			}
			iov.iov_base = lbuf;
			iov.iov_len = request.rec_len;
			msglen = recvmsg(inet_sock, &msg, MSG_WAITALL);
			if (msglen < request.rec_len)
				break;
			process_request(inet_sock, &msg, msglen);
			if (cleanup_requested)
				break;
		}
	} while (1);
	ldmsd_log(LDMSD_LINFO,
		  "Closing configuration socket. cfg_buf_len %d\n",
		  cfg_buf_len);
	close(inet_sock);
	inet_sock = -1;
	if (cleanup_requested) {
		/* Reset it to prevent deadlock in cleanup */
		inet_ctrl_thread = (pthread_t)-1;
		cleanup(0,"user quit");
		return NULL;
	}

	goto loop;
	return NULL;
}

int ldmsd_inet_config_init(const char *port, const char *secretword)
{
	int rc;
	struct sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(atoi(port));

	inet_listener = socket(AF_INET, SOCK_STREAM, 0);
	if (inet_listener < 0) {
		ldmsd_log(LDMSD_LERROR, "Error %d creating socket on port "
				"'%s'\n", errno, port);
		return errno;
	}

	/* Bind to our public name */
	rc = bind(inet_listener, (struct sockaddr *)&sin, sizeof(sin));
	if (rc < 0) {
		ldmsd_log(LDMSD_LERROR, "Error %d binding to socket on port '%s'.\n",
						errno, port);
		goto err;
	}

	rc = listen(inet_listener, 10);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Error %d failed to setting up the config "
				"listener.\n", rc);
		goto err;
	}

	rc = pthread_create(&inet_ctrl_thread, NULL, inet_ctrl_thread_proc,
			(void *)secretword);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "Error %d creating the control pthread'.\n");
		goto err;
	}
	return 0;
err:
	close(inet_listener);
	return rc;
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
			if (ldmsd_load_plugin(b, err_str)) {
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
			if (ldmsd_term_plugin(b, err_str)) {
				fprintf(stderr, "%s\n", err_str);
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
