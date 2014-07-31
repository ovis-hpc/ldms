/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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

#include <sys/un.h>
#include <sys/errno.h>
#include <sys/queue.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <pthread.h>
#include <assert.h>
#include <ocm/ocm.h>

#include "me_priv.h"


#define VERSION "0.0.1"
#define DEFAULT_ME_LOGFILE "/var/log/me.log"
#define DEFAULT_ME_HASH_RBT_SZ 1000
#define FMT "l:x:S:H:O:i:F"

char myhostname[80];
char *sockname = NULL;
int foreground = 0;
char *logfile = NULL;
FILE *log_fp;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
int num_worker_thread = 1;
int bind_succeeded;
char *transport = NULL;
uint16_t ocm_port = 0;
int hash_rbt_sz = DEFAULT_ME_HASH_RBT_SZ;
int max_sem_input = 1024;

int muxr_s = -1;
pthread_t ctrl_thread = (pthread_t) - 1;

struct attr_value_list *av_list;
struct attr_value_list *kw_list;

static char msg_buf[4096];
static void msg_logger(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsprintf(msg_buf, fmt, ap);
	me_log(msg_buf);
}

void me_cleanup(int x)
{
	me_log("Model Evaluator Daemon exiting ... status %d\n", x);
	if (ctrl_thread != (pthread_t) - 1) {
		void *dontcare;
		pthread_cancel(ctrl_thread);
		pthread_join(ctrl_thread, &dontcare);
	}

	if (muxr_s >= 0)
		close(muxr_s);
	if (sockname && bind_succeeded)
		unlink(sockname);

	close_all_store();
	exit(x);
}

FILE *me_open_log() {
	FILE *f;
	if (logfile) {
		f = fopen(logfile, "a");
		if (!f) {
			me_log("Could not open the log file named '%s'\n",
								logfile);
			me_cleanup(9);
		} else {
			int fd = fileno(f);
			if (dup2(fd, 1) < 0) {
				me_log("Cannot redirect log to %s\n",
								logfile);
				me_cleanup(10);
			}
			if (dup2(fd, 2) < 0) {
				me_log("Cannot redirect log to %s\n",
								logfile);
				me_cleanup(11);
			}
			stdout = f;
			stderr = f;
		}
	}
	return f;
}

void me_logrotate_act(int x)
{
	pthread_mutex_lock(&log_lock);
	FILE *new_log = me_open_log();
	fflush(log_fp);
	fclose(log_fp);
	log_fp = new_log;
	pthread_mutex_unlock(&log_lock);
}

void me_usage(char *argv[])
{
	printf("%s: [%s]\n", argv[0], FMT);
	printf("	-F		Foreground mode, don't daemonize the program.\n"
		"			[default: false].\n");
	printf("	-S sockname	Specifies the unix domain socket name to\n"
		"			use for mectl access.\n");
	printf("	-p thr_count	Number of threads to evaluate inputs.\n");
	printf("	-l log_file	The path to the log file for status messages.\n");
	printf("	-x xprt:port_no	The transport and the port number.\n");
	printf("	-O port		The listener port for communicating with ocmd.\n");
	printf("	-i number inputs	The maximum number of inputs in the input queue.\n");
}

void me_log(const char *fmt, ...)
{
	va_list ap;
	time_t t;
	struct tm *tm;
	char dtsz[200];

	t = time(NULL);
	tm = localtime(&t);
	pthread_mutex_lock(&log_lock);
	if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", tm))
		fprintf(log_fp, "%s: ", dtsz);
	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	fflush(log_fp);
	pthread_mutex_unlock(&log_lock);
}

char *av_value(struct attr_value_list *av_list, char *name)
{
	int i;
	for (i = 0; i < av_list->count; i++)
		if (0 == strcmp(name, av_list->list[i].name))
			return av_list->list[i].value;
	return NULL;
}

static char replybuf[4096];
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

LIST_HEAD(me_plugin_list, me_plugin) me_plugin_list;

struct me_plugin *me_find_plugin(char *plugin_name)
{
	struct me_plugin *pi = NULL;
	LIST_FOREACH(pi, &me_plugin_list, entry) {
		if (strcmp(plugin_name, pi->name) == 0)
			return pi;
	}
	return NULL;
}

static char library_name[ME_PATH_MAX];
struct me_plugin *me_new_plugin(char *plugin_name)
{
	struct me_plugin *pi;
	char *path = getenv("ME_PLUGIN_LIBPATH");
	if (!path)
		path = ME_PLUGIN_LIBPATH_DEFAULT;
	sprintf(library_name, "%s/lib%s.so", path, plugin_name);
	void *d = dlopen(library_name, RTLD_NOW);
	if (!d) {
		me_log("%s: %s\n", plugin_name, dlerror());
		goto err;
	}

	me_plugin_get_f pi_get = dlsym(d, "get_plugin");
	if (!pi_get) {
		me_log("The library '%s' is missing the get_plugin() "
				"function.", plugin_name);
		goto err;
	}

	pi = pi_get(msg_logger); /* Get the plugin */
	if (!pi) {
		me_log("Failed to get a new plugin '%s'. "
				"ERROR '%d'", plugin_name, ENOMEM);
		goto err;
	}
	pthread_mutex_init(&pi->lock, NULL);
	pi->refcount = 0;
	pi->handle = d;
	LIST_INSERT_HEAD(&me_plugin_list, pi, entry);
	me_log("Loaded '%s'\n", plugin_name);
	return pi;
err:
	return NULL;
}

void destroy_plugin(struct me_plugin *p)
{
	LIST_REMOVE(p, entry);
	dlclose(p->handle);
	free(p);
}

/* Temporary function */
void print_model_policy(struct model *m)
{
	printf("model name: %s\n", m->policy->model_pi->base.name);
	printf("'%" PRIu16 "' inputs:\n", m->num_inputs);
	int i;
	for (i = 0; i < m->num_inputs; i++) {
		printf("	%lu\n", m->metric_ids[i]);
	}
	printf("thresholds: %lf	 %lf	%lf\n",
			m->policy->cfg->thresholds[ME_SEV_INFO],
			m->policy->cfg->thresholds[ME_SEV_WARNING],
			m->policy->cfg->thresholds[ME_SEV_CRITICAL]);
}

struct model_policy_list mp_list;
pthread_mutex_t mp_list_lock = PTHREAD_MUTEX_INITIALIZER;

struct model_policy *find_model_policy(uint16_t model_id)
{
	struct model_policy *mp;
	pthread_mutex_lock(&mp_list_lock);
	LIST_FOREACH(mp, &mp_list, link) {
		if (mp->model_id == model_id) {
			pthread_mutex_unlock(&mp_list_lock);
			return mp;
		}
	}
	pthread_mutex_unlock(&mp_list_lock);
	return NULL;
}

int create_model_policy(char *model_name, char *model_id_s,
				char *thresholds, char *param,
				char report_flags[ME_NUM_SEV_LEVELS], char *reply_buf)
{
	int rc, model_id;
	struct me_plugin *pi = me_find_plugin(model_name);
	if (!pi) {
		pi = me_new_plugin(model_name);
		if (!pi) {
			sprintf(reply_buf, "%dThe model plugin '%s' doesn't "
					"exist.", -EEXIST, model_name);
			rc = EEXIST;
			goto err;
		}
	}

	if (pi->type != ME_MODEL_PLUGIN) {
		sprintf(reply_buf, "%d'%s' is not a model plugin.",
						-EINVAL, model_name);
		rc = EINVAL;
		goto err;
	}

	struct me_model_plugin *model_pi = pi->model_pi;
	struct me_model_cfg *cfg;
	cfg = cfg_new(model_pi, model_id_s, thresholds, param,
					report_flags, reply_buf);
	if (!cfg)
		goto err;

	model_id = atoi(model_id_s);
	struct model_policy *mp = find_model_policy(model_id);
	if (mp) {
		sprintf(reply_buf, "%dThe model id '%d' already "
				"exists.", -EINVAL, model_id);
		goto err;
	}

	mp = mp_new(model_pi);
	if (!mp) {
		sprintf(reply_buf, "-1Could not create model policy %s.",
						model_pi->base.name);
		rc = ENOMEM;
		goto err;
	}

	mp->cfg = cfg;
	mp->model_id = model_id;

	pthread_mutex_lock(&mp_list_lock);
	LIST_INSERT_HEAD(&mp_list, mp, link);
	pthread_mutex_unlock(&mp_list_lock);
	pi->refcount++;
	return 0;

err:
	return rc;
}

/**
 * \brief Parse the input variables
 *
 * \param[in]	input_vars	String of input variables
 * \param[out]	list_h		List of input definitions
 * \param[out]	num_inputs	The number of inputs in the list
 */
int parse_input_ids(char *input_ids_s, struct model *model)
{
	char *tok, *end;
	tok = strtok(input_ids_s, ",");
	int i = 0;
	while (tok) {
		model->metric_ids[i] = strtoull(tok, &end, 10);
		if (!*tok || *end)
			return -1;
		i++;
		tok = strtok(NULL, ",");
	}
	return 0;
}

int count_input_ids(char *input_ids_s) {
	char *tok, *end;
	int num_inputs = 0;
	tok = strtok(input_ids_s, ",");
	uint64_t input_id;

	while (tok) {
		input_id = strtoull(tok, &end, 10);
		if (!*tok || *end)
			return -1;
		num_inputs++;
		tok = strtok(NULL, ",");
	}
	return num_inputs;
}

void ls_plugins(char *reply_buf)
{
	struct me_plugin *p;
	reply_buf[0] = '\0';
	LIST_FOREACH(p, &me_plugin_list, entry) {
		reply_buf += sprintf(reply_buf, "%s\n", p->name);
		if (p->usage)
			reply_buf += sprintf(reply_buf, "%s", (char *)p->usage);
	}
}

int create_model(char *model_id_s, char *input_ids, char *reply_buf)
{
	int rc = 0;
	int model_id = atoi(model_id_s);

	struct model_policy *mp = find_model_policy(model_id);
	if (!mp) {
		sprintf(reply_buf, "%dCould not find model_id '%" PRIu16 "'",
							-EINVAL, model_id);
		rc =  EINVAL;
		goto err;
	}

	char *input_tmp = strdup(input_ids);
	int num_inputs = count_input_ids(input_tmp);

	struct model *model = calloc(1, sizeof(*model) + (num_inputs *
							sizeof(uint64_t)));
	if (!model) {
		sprintf(reply_buf, "%dCould not create a model. "
			"(model ID '%d).", -ENOMEM, model_id);
		rc = ENOMEM;
		goto err1;
	}
	free(input_tmp);
	model->num_inputs = num_inputs;

	model->policy = mp;
	if (parse_input_ids(input_ids, model)) {
		sprintf(reply_buf, "%dFailed to parse the input IDs "
			"(model ID '%d).", -EPERM, model_id);
		rc = EPERM;
		goto err2;
	}

	pthread_mutex_lock(&mp->refcount_lock);
	mp->refcount++;
	pthread_mutex_unlock(&mp->refcount_lock);
	add_model(model, reply_buf);
	return 0;

err2:
	free(model);
err1:
	free(input_tmp);
err:
	return rc;
}

int start_consumer(char *name, char *reply_buf)
{
	int rc = 0;
	struct me_plugin *pi = me_find_plugin(name);
	if (!pi) {
		sprintf(reply_buf, "%dThe plugin '%s' doesn't exist.",
				-EEXIST, name);
		rc = EEXIST;
		goto err;
	}

	if (pi->intf_pi->type != ME_CONSUMER) {
		sprintf(reply_buf, "%d'%s' is not a consumer.", -EINVAL, name);
		rc = EINVAL;
		goto err;
	}
	struct me_consumer *csm;
	csm = (struct me_consumer *) pi->intf_pi->get_instance(av_list,
							pi->intf_pi);
	if (!csm) {
		sprintf(reply_buf, "-1Failed to start '%s'.\n", name);
		rc = EPERM;
		goto err;
	}
	pi->refcount++;
	csm->consumer_id = get_consumer_counts();
	add_consumer_counts();
	init_consumer_output_queue(csm);
	add_consumer(csm);

	pthread_t thread;
	pthread_create(&thread, NULL, evaluate_complete_consumer_cb,
			(void *)(unsigned long) csm);
	return 0;
err:
	return rc;
}

int start_store(char *name, char *container, char *reply_buf)
{
	int rc = 0;
	reply_buf[0] = '\0';
	struct me_plugin *pi = me_find_plugin(name);
	if (!pi) {
		sprintf(reply_buf, "%dThe plugin '%s' doesn't exist.",
				-EEXIST, name);
		rc = EEXIST;
		goto err;
	}

	if (pi->intf_pi->type != ME_STORE) {
		sprintf(reply_buf, "%d'%s' is not a storage.", -EINVAL, name);
		rc = EINVAL;
		goto err;
	}
	struct me_store *store;
	store = (struct me_store *) pi->intf_pi->get_instance(av_list,
							pi->intf_pi);
	if (!store) {
		sprintf(reply_buf, "-1Failed to start '%s'.\n",
				name);
		rc = -1;
		goto err;
	}
	store->container = container;

	pi->refcount++;
	store->store_id = get_store_counts();
	add_store_counts();
	init_store_output_queue(store);
	add_store(store);

	pthread_t thread;
	pthread_create(&thread, NULL, evaluate_complete_store_cb,
			(void *)(unsigned long) store);
	return 0;
err:
	return rc;
}

int process_ls_plugins(int fd,
		       struct sockaddr *sa, ssize_t sa_len,
		       char *command)
{
	struct me_plugin *p;
	sprintf(replybuf, "0");
	LIST_FOREACH(p, &me_plugin_list, entry) {
		strcat(replybuf, p->name);
		strcat(replybuf, "\n");
		if (p->usage)
			strcat(replybuf, p->usage());
	}
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return 0;
}

int process_load_plugin(int fd, struct sockaddr *sa,
		ssize_t sa_len, char *commnad)
{
	char *plugin_name;
	char err_str[128];
	char reply[128];
	int rc = 0;

	err_str[0] = '\0';

	plugin_name = av_value(av_list, "name");
	if (!plugin_name) {
		sprintf(err_str, "The plugin name was not specified\n");
		rc = EINVAL;
		goto out;
	}
	struct me_plugin *pi = me_find_plugin(plugin_name);
	if (pi) {
		sprintf(err_str, "Plugin already loaded");
		rc = EEXIST;
		goto out;
	}
	pi = me_new_plugin(plugin_name);
	if (!pi) {
		sprintf(err_str, "Failed to load the plugin");
		rc = ENOMEM;
	}
out:
	sprintf(reply, "%d%s", -rc, err_str);
	send_reply(fd, sa, sa_len, reply, strlen(reply)+1);
	return rc;
}

int process_config_plugin(int fd, struct sockaddr *sa,
		ssize_t sa_len, char *command)
{
	char *plugin_name;
	char *err_str = "";
	int rc = 0;
	struct me_plugin *pi;

	plugin_name = av_value(av_list, "name");
	if (!plugin_name) {
		err_str = "The plugin name must be specified.";
		rc = EINVAL;
		goto out;
	}
	pi = me_find_plugin(plugin_name);
	if (!pi) {
		rc = ENOENT;
		err_str = "The plugin was not found.";
		goto out;
	}
	pthread_mutex_lock(&pi->lock);
	rc = pi->config(kw_list, av_list);
	if (rc)
		err_str = "Plugin configuration error.";
	pthread_mutex_unlock(&pi->lock);
 out:
	sprintf(replybuf, "%d%s", rc, err_str);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return 0;
}

/*
 * Destroy and unload the plugin
 */
int process_term_plugin(int fd,
			struct sockaddr *sa, ssize_t sa_len,
			char *command)
{
	char *plugin_name;
	char *err_str = "";
	int rc = 0;
	struct me_plugin *pi;

	plugin_name = av_value(av_list, "name");
	if (!plugin_name) {
		err_str = "The plugin name must be specified.";
		rc = EINVAL;
		goto out;
	}
	pi = me_find_plugin(plugin_name);
	if (!pi) {
		rc = ENOENT;
		err_str = "Plugin not found.";
		goto out;
	}
	pthread_mutex_lock(&pi->lock);
	if (pi->refcount) {
		rc = EPERM;
		err_str = "The specified plugin has active users "
			"and cannot be terminated.\n";
		goto out;
	}
	pi->term();
	pthread_mutex_unlock(&pi->lock);
	destroy_plugin(pi);
	rc = 0;
 out:
	sprintf(replybuf, "%d%s", rc, err_str);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return 0;
}

int process_list_models(int fd, struct sockaddr *sa, ssize_t sa_len,
						char *command)
{
	int rc = 0;
	char msg[1024];
	char *tmp = msg;
	model_policy_t mp;
	if (LIST_EMPTY(&mp_list)) {
		sprintf(replybuf, "0No model policy.\n");
		goto out;
	}
	tmp += sprintf(tmp, "%-20s %-10s %-20s %-15s\n",
			"Model_Name", "Model_ID",
			"Thresholds", "Parameters");
	char thresholds_s[512];
	char id_s[512];
	LIST_FOREACH(mp, &mp_list, link) {
		sprintf(id_s, "%" PRIu16, mp->model_id);
		sprintf(thresholds_s, "%.2f,%.2f,%.2f",
				mp->cfg->thresholds[ME_SEV_INFO],
				mp->cfg->thresholds[ME_SEV_WARNING],
				mp->cfg->thresholds[ME_SEV_CRITICAL]);
		tmp += sprintf(tmp, "%-20s %-10" PRIu16 "%-20s %-15s\n",
				mp->model_pi->base.name, mp->model_id,
				thresholds_s,
				mp->model_pi->print_param(mp->cfg->params));
	}

	sprintf(replybuf, "0%s", msg);
out:
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return 0;
err:
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return rc;
}

int process_create_model_policy(int fd, struct sockaddr *sa,
				ssize_t sa_len, char *command)
{
	int rc = 0;
	char *attr;
	char *model_name;
	char *model_id_s;
	char *thresholds;
	char *param;
	char *report_flags;

	attr = "name";
	model_name = av_value(av_list, attr);
	if (!model_name)
		goto einval;

	attr = "model_id";
	model_id_s = av_value(av_list, attr);
	if (!model_id_s)
		goto einval;
	char *end;
	uint16_t model_id = (unsigned short) strtoul(model_id_s, &end, 10);
	if (!*model_id_s || *end) {
		sprintf(replybuf, "%dmodel_id '%s' is not an integer.\n",
						-EINVAL, model_id_s);
		rc = EINVAL;
		goto err;
	}

	attr = "thresholds";
	thresholds = av_value(av_list, attr);
	if (!thresholds)
		goto einval;

	param = av_value(av_list, "param");

	report_flags = av_value(av_list, "report_flags");
	if (report_flags && strlen(report_flags) > 4) {
		sprintf(replybuf, "%dInvalid report_flags '%s'",
				-EINVAL, report_flags);
		rc = EINVAL;
		goto err;
	}

	struct me_plugin *pi = me_find_plugin(model_name);
	if (!pi) {
		sprintf(replybuf, "%dThe model plugin '%s' doesn't exist.",
				-EEXIST, model_name);
		rc = EEXIST;
		goto err;
	}

	if (pi->type != ME_MODEL_PLUGIN) {
		sprintf(replybuf, "%d'%s' is not a model plugin.",
				-EINVAL, model_name);
		rc = EINVAL;
		goto err;
	}

	struct me_model_plugin *model_pi = pi->model_pi;
	struct me_model_cfg *cfg;
	cfg = cfg_new(model_pi, model_id_s, thresholds, param,
					report_flags, replybuf);
	if (!cfg)
		goto err;

	struct model_policy *mp = find_model_policy(model_id);
	if (mp) {
		sprintf(replybuf, "%dThe model id '%" PRIu16 "' already "
				"exists.", -EINVAL, model_id);
		goto err;
	}

	mp = mp_new(model_pi);
	mp->cfg = cfg;
	mp->model_id = model_id;
	if (!mp) {
		sprintf(replybuf, "-1Could not create model policy %s.",
						model_pi->base.name);
		goto err;
	}

	pthread_mutex_lock(&mp_list_lock);
	LIST_INSERT_HEAD(&mp_list, mp, link);
	pthread_mutex_unlock(&mp_list_lock);
	pi->refcount++;
out:
	sprintf(replybuf, "0");
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return 0;

einval:
	sprintf(replybuf, "%d The %s attribute must be specified\n",
						-EINVAL, attr);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return EINVAL;
err:
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return rc;
}

int process_model(int fd, struct sockaddr *sa, ssize_t sa_len, char *command)
{
	int rc = 0;
	char *attr;
	char *model_id_s;
	char *metric_ids;

	attr = "model_id";
	model_id_s = av_value(av_list, attr);
	if (!model_id_s)
		goto einval;
	char *end;
	int model_id = (unsigned short) strtol(model_id_s, &end, 10);
	if (!*model_id_s || *end) {
		sprintf(replybuf, "%dmodel_id '%s' is not an integer.\n",
						-EINVAL, model_id_s);
		rc = EINVAL;
		goto err;
	}

	attr = "metric_ids";
	metric_ids = av_value(av_list, attr);
	if (!metric_ids)
		goto einval;

	struct model_policy *mp = find_model_policy(model_id);
	if (!mp) {
		sprintf(replybuf, "%dCould not find model_id '%" PRIu16 "'",
							-EINVAL, model_id);
		goto err;
	}

	char *input_tmp = strdup(metric_ids);
	int num_inputs = count_input_ids(input_tmp);

	struct model *model = calloc(1, sizeof(*model) + (num_inputs *
							sizeof(uint64_t)));
	if (!model) {
		sprintf(replybuf, "%dCould not create a model. "
			"(model ID '%" PRIu16 ").", -ENOMEM, model_id);
		goto err;
	}
	free(input_tmp);
	model->num_inputs = num_inputs;

	model->policy = mp;
	if (parse_input_ids(metric_ids, model)) {
		sprintf(replybuf, "%dFailed to parse the input IDs "
			"(model ID '%" PRIu16 ").", -EPERM, model_id);
		goto err_2;
	}

	pthread_mutex_lock(&mp->refcount_lock);
	mp->refcount++;
	pthread_mutex_unlock(&mp->refcount_lock);
	add_model(model, replybuf);
out:
	sprintf(replybuf, "0");
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return 0;
einval:
	sprintf(replybuf, "%d The %s attribute must be specified\n",
			-EINVAL, attr);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return EINVAL;
err_2:
	free(model->engine);
err_1:
	free(model);
err:
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return rc;
}

int process_start_consumer(int fd, struct sockaddr *sa,
			ssize_t sa_len, char *command)
{
	int rc = 0;
	char *attr;
	char *name;

	attr = "name";
	name = av_value(av_list, attr);
	if (!name)
		goto einval;

	struct me_plugin *pi = me_find_plugin(name);
	if (!pi) {
		sprintf(replybuf, "%dThe plugin '%s' doesn't exist.",
				-EEXIST, name);
		rc = EEXIST;
		goto err;
	}

	if (pi->intf_pi->type != ME_CONSUMER) {
		sprintf(replybuf, "%d'%s' is not a consumer.", -EINVAL, name);
		rc = EINVAL;
		goto err;
	}
	struct me_consumer *csm;
	csm = (struct me_consumer *) pi->intf_pi->get_instance(av_list,
							pi->intf_pi);
	if (!csm) {
		sprintf(replybuf, "-1Failed to start '%s'.\n", name);
		goto err;
	}
	pi->refcount++;
	csm->consumer_id = get_consumer_counts();
	add_consumer_counts();
	init_consumer_output_queue(csm);
	add_consumer(csm);

	pthread_t thread;
	pthread_create(&thread, NULL, evaluate_complete_consumer_cb,
			(void *)(unsigned long) csm);
	sprintf(replybuf, "0\n");
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return 0;
einval:
	sprintf(replybuf, "%d The %s attribute must be specified\n",
			-EINVAL, attr);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return EINVAL;
err:
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
	return rc;
}

int process_store(int fd, struct sockaddr *sa, ssize_t sa_len, char *command)
{
	int rc = 0;
	char *attr;
	char *name;
	char *container;

	attr = "name";
	name = av_value(av_list, attr);
	if (!name)
		goto einval;

	attr = "container";
	container = av_value(av_list, attr);
	if (!container)
		goto einval;

	struct me_plugin *pi = me_find_plugin(name);
	if (!pi) {
		sprintf(replybuf, "%dThe plugin '%s' doesn't exist.",
				-EEXIST, name);
		rc = EEXIST;
		goto err;
	}

	if (pi->intf_pi->type != ME_STORE) {
		sprintf(replybuf, "%d'%s' is not a storage.", -EINVAL, name);
		rc = EINVAL;
		goto err;
	}
	struct me_store *store;
	store = (struct me_store *) pi->intf_pi->get_instance(av_list,
							pi->intf_pi);

	if (!store) {
		sprintf(replybuf, "-1Failed to start '%s'.\n",
				name);
		rc = -1;
		goto err;
	}
	store->container = container;

	pi->refcount++;
	store->store_id = get_store_counts();
	add_store_counts();
	init_store_output_queue(store);
	add_store(store);

	pthread_t thread;
	pthread_create(&thread, NULL, evaluate_complete_store_cb,
			(void *)(unsigned long) store);
	sprintf(replybuf, "0");
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return 0;
einval:
	sprintf(replybuf, "%dThe %s attribute must be specified\n",
			-EINVAL, attr);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return EINVAL;
err:
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf) + 1);
	return rc;
}

int process_exit(int fd,
		 struct sockaddr *sa, ssize_t sa_len,
		 char *command)
{
	me_cleanup(0);
	return 0;
}

mectl_cmd_fn cmd_table[] = {
	[MECTL_LIST_PLUGINS] = process_ls_plugins,
	[MECTL_LIST_MODELS] = process_list_models,
	[MECTL_LOAD_PLUGIN] =	process_load_plugin,
	[MECTL_TERM_PLUGIN] =	process_term_plugin,
	[MECTL_CFG_PLUGIN] =	process_config_plugin,
	[MECTL_STORE] = process_store,
	[MECTL_CREATE] = process_create_model_policy,
	[MECTL_MODEL] = process_model,
	[MECTL_START_CONSUMER] = process_start_consumer,
	[MECTL_EXIT_DAEMON] = process_exit,
};

int process_record(int fd,
		   struct sockaddr *sa, ssize_t sa_len,
		   char *command, ssize_t cmd_len)
{
	char *cmd_s;
	long cmd_id;
	int rc = tokenize(command, kw_list, av_list);
	if (rc) {
		me_log("Memory allocation failure processing '%s'\n",
			 command);
		rc = ENOMEM;
		goto out;
	}

	cmd_s = av_name(kw_list, 0);
	if (!cmd_s) {
		me_log("Request is missing Id '%s'\n", command);
		rc = EINVAL;
		goto out;
	}

	cmd_id = strtoul(cmd_s, NULL, 0);
	if (cmd_id >= 0 && cmd_id <= MECTL_LAST_COMMAND) {
		rc = cmd_table[cmd_id](fd, sa, sa_len, cmd_s);
		goto out;
	}

	sprintf(replybuf, "-22Invalid command Id %ld\n", cmd_id);
	send_reply(fd, sa, sa_len, replybuf, strlen(replybuf)+1);
 out:
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
	static unsigned char lbuf[256];
	struct sockaddr_storage ss;
	iov.iov_base = lbuf;
	do {
		ssize_t msglen;
		ss.ss_family = AF_UNIX;
		msg.msg_name = &ss;
		msg.msg_namelen = sizeof(ss);
		iov.iov_len = sizeof(lbuf);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = 0;
		msglen = recvmsg(muxr_s, &msg, 0);
		if (msglen <= 0)
			break;
		process_message(muxr_s, &msg, msglen);
	} while (1);
	return NULL;
}

int setup_control(char *sockname)
{
	struct sockaddr_un sun;
	int ret;

	if (!sockname) {
		char *sockpath = getenv("ME_SOCKPATH");
		if (!sockpath)
			sockpath = "/var/run";
		sockname = malloc(sizeof(ME_CONTROL_SOCKNAME) + strlen(sockpath) + 2);
		sprintf(sockname, "%s/%s", sockpath, ME_CONTROL_SOCKNAME);
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strncpy(sun.sun_path, sockname,
		sizeof(struct sockaddr_un) - sizeof(short));

	/* Create listener */
	muxr_s = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (muxr_s < 0) {
		me_log("Error %d creating muxr socket.\n", muxr_s);
		return -1;
	}

	/* Bind to our public name */
	ret = bind(muxr_s, (struct sockaddr *)&sun, sizeof(struct sockaddr_un));
	if (ret < 0) {
		me_log("Error %d binding to socket named '%s'.\n",
			 errno, sockname);
		return -1;
	}
	bind_succeeded = 1;

	ret = pthread_create(&ctrl_thread, NULL, ctrl_thread_proc, 0);
	if (ret) {
		me_log("Error %d creating the control pthread'.\n");
		return -1;
	}
	return 0;
}

/* --- configure --- */

void free_av_list_value(struct attr_value_list *av_list)
{
	int i;
	for (i = 0; i < av_list->count; i++)
		free(av_list->list[i].value);
}

struct attr_value_list *ocm_cmd_to_attr_value_list(ocm_cfg_cmd_t cmd)
{
	struct attr_value_list *av_list = av_new(128);
	const struct ocm_value *v;
	struct ocm_av_iter iter;
	ocm_av_iter_init(&iter, cmd);
	const char *attr;
	char *_value;
	int count = 0;

	while (ocm_av_iter_next(&iter, &attr, &v) == 0) {
		av_list->list[count].name = (char *)attr;
		if (v->type == OCM_VALUE_STR) {
			_value = strdup(v->s.str);
		} else {
			_value = malloc(32);
			switch (v->type) {
			case OCM_VALUE_INT8:
				sprintf(_value, "%"PRIi8, v->i8);
				break;
			case OCM_VALUE_INT16:
				sprintf(_value, "%"PRIi16, v->i16);
				break;
			case OCM_VALUE_INT32:
				sprintf(_value, "%"PRIi32, v->i32);
				break;
			case OCM_VALUE_INT64:
				sprintf(_value, "%"PRIi64, v->i64);
				break;
			case OCM_VALUE_UINT8:
				sprintf(_value, "%"PRIu8, v->u8);
				break;
			case OCM_VALUE_UINT16:
				sprintf(_value, "%"PRIu16, v->u16);
				break;
			case OCM_VALUE_UINT32:
				sprintf(_value, "%"PRIu32, v->u32);
				break;
			case OCM_VALUE_UINT64:
				sprintf(_value, "%"PRIu64, v->u64);
				break;
			case OCM_VALUE_FLOAT:
				sprintf(_value, "%f", v->f);
				break;
			case OCM_VALUE_DOUBLE:
				sprintf(_value, "%lf", v->d);
				break;
			}
		}
		av_list->list[count].value = _value;
		count++;
	}
	av_list->count = count;
	return av_list;
}

void me_config_plugin(ocm_cfg_cmd_t cmd)
{
	const struct ocm_value *v;

	char *plugin_name;
	int rc;
	struct me_plugin *pi;

	v = ocm_av_get_value(cmd, "name");
	if (!v || v->type != OCM_VALUE_STR) {
		me_log("'name' is not specified in 'config'.\n");
		return;
	}
	plugin_name = (char*)v->s.str;

	pi = me_find_plugin(plugin_name);

	if (!pi) {
		pi = me_new_plugin(plugin_name);
		if (!pi) {
			me_log("Failed to load the plugin '%s'\n",
							plugin_name);
			return;
		}
	}

	struct attr_value_list *av_list = ocm_cmd_to_attr_value_list(cmd);

	pthread_mutex_lock(&pi->lock);
	rc = pi->config(NULL, av_list);
	if (rc)
		me_log("plugin '%s': configuration error.\n", plugin_name);

	pthread_mutex_unlock(&pi->lock);
	free_av_list_value(av_list);
	free(av_list);
	return;
}

void me_create_model_policy(ocm_cfg_cmd_t cmd)
{
	char *model_name;
	char *model_id_s;
	char *thresholds;
	char *param;
	char *attr;
	char *report_flags;
	char replybuf[128];

	const struct ocm_value *v;

	int rc;
	struct me_plugin *pi;

	attr = "name";
	v = ocm_av_get_value(cmd, attr);
	if (!v || v->type != OCM_VALUE_STR)
		goto einval;

	model_name = (char*)v->s.str;

	attr = "model_id";
	v = ocm_av_get_value(cmd, attr);
	if (!v || v->type != OCM_VALUE_STR)
		goto einval;
	model_id_s = (char*)v->s.str;

	attr = "thresholds";
	v = ocm_av_get_value(cmd, attr);
	if (!v || v->type != OCM_VALUE_STR)
		goto einval;
	thresholds = (char*)v->s.str;

	attr = "param";
	v = ocm_av_get_value(cmd, attr);
	if (!v || v->type != OCM_VALUE_STR)
		param = NULL;
	else
		param = (char*)v->s.str;

	attr = "report_flags";
	v = ocm_av_get_value(cmd, attr);
	if (!v || v->type != OCM_VALUE_STR)
		report_flags = NULL;
	else
		report_flags = (char*)v->s.str;

	rc = create_model_policy(model_name, model_id_s, thresholds,
					param, report_flags, replybuf);
	if (rc)
		me_log("%s\n", replybuf + 1);
#ifdef DEBUG
	else
		me_log("Created model id '%s'\n", model_id_s);
#endif
	return;

einval:
	me_log("'%s' is not specified in 'config'.\n", attr);
	return;
}

void me_ls_plugins(ocm_cfg_cmd_t cmd)
{
	char reply_buf[4096];
	ls_plugins(reply_buf);
}

void me_ls_models(ocm_cfg_cmd_t cmd)
{
	char *msg;
	model_policy_t mp;
	if (LIST_EMPTY(&mp_list)) {
		me_log("No model policy.\n");
		return;
	}

	msg += sprintf(msg, "%-20s %-10s %-20s %-15s\n",
			"Model_Name", "Model_ID",
			"Thresholds", "Parameters");
	char thresholds_s[512];
	char id_s[512];
	LIST_FOREACH(mp, &mp_list, link) {
		sprintf(id_s, "%" PRIu16, mp->model_id);
		sprintf(thresholds_s, "%.2f,%.2f,%.2f",
				mp->cfg->thresholds[ME_SEV_INFO],
				mp->cfg->thresholds[ME_SEV_WARNING],
				mp->cfg->thresholds[ME_SEV_CRITICAL]);
		msg += sprintf(msg, "%-20s %-10" PRIu16 "%-20s %-15s\n",
				mp->model_pi->base.name, mp->model_id,
				thresholds_s,
				mp->model_pi->print_param(mp->cfg->params));
	}
}

void me_load_plugin(ocm_cfg_cmd_t cmd)
{
	char *plugin_name;
	int rc = 0;

	const struct ocm_value *v;
	v = ocm_av_get_value(cmd, "name");
	if (!v || v->type != OCM_VALUE_STR) {
		me_log("'name' is not specified in 'load'\n");
		return;
	}

	plugin_name = (char*)v->s.str;

	struct me_plugin *pi = me_find_plugin(plugin_name);
	if (pi) {
		me_log("Plugin '%s' already loaded\n", plugin_name);
		return;
	}

	pi = me_new_plugin(plugin_name);
	if (!pi) {
		me_log("Failed to load the plugin '%s'\n", plugin_name);
		return;
	}
}

void me_model(ocm_cfg_cmd_t cmd)
{
	int rc;
	char *model_id_s, *metric_id;
	char reply_buf[1028];

	const struct ocm_value *v;
	v = ocm_av_get_value(cmd, "model_id");
	if (!v || v->type != OCM_VALUE_STR) {
		me_log("'model_id' is not specified in 'model'.\n");
		return;
	}
	model_id_s = (char*)v->s.str;

	v = ocm_av_get_value(cmd, "metric_id");
	if (!v || v->type != OCM_VALUE_STR) {
		me_log("'metric_id' is not specified in 'model'.\n");
		return;
	}
	metric_id = (char*)v->s.str;

	rc = create_model(model_id_s, metric_id, reply_buf);
	if (rc)
		me_log("%s\n", reply_buf + 1);
}

void me_start_consumer(ocm_cfg_cmd_t cmd)
{
	char *consumer_name;
	char reply_buf[1028];

	const struct ocm_value *v;
	v = ocm_av_get_value(cmd, "name");
	if (!v || v->type != OCM_VALUE_STR) {
		me_log("'name' is not specified in 'start_consumer'.\n");
		return;
	}
	consumer_name = (char*)v->s.str;

	int rc = start_consumer(consumer_name, reply_buf);
	if (rc)
		me_log("%s\n", reply_buf + 1);
	return;
}

void me_store(ocm_cfg_cmd_t cmd)
{
	char *store_name, *container;
	char reply_buf[1024];

	const struct ocm_value *v;
	v = ocm_av_get_value(cmd, "name");
	if (!v || v->type != OCM_VALUE_STR) {
		me_log("'name' is not specified in 'store'.\n");
		return;
	}
	store_name = (char*)v->s.str;

	v = ocm_av_get_value(cmd, "container");
	if (!v || v->type != OCM_VALUE_STR) {
		me_log("'container' is not specified in 'store'.\n");
		return;
	}
	container = (char*)v->s.str;

	int rc;
	rc = start_store(store_name, container, reply_buf);
	if (rc)
		me_log("%s\n", reply_buf + 1);
}

typedef void (*me_cfg_fn)(ocm_cfg_cmd_t cmd);
struct me_cfg_fn_keyword {
	const char *verb;
	me_cfg_fn action;
};

int me_cfg_fn_keyword_cmp(const void *_a, const void *_b)
{
	const struct me_cfg_fn_keyword *a = (struct me_cfg_fn_keyword *)_a;
	const struct me_cfg_fn_keyword *b = (struct me_cfg_fn_keyword *)_b;
	return strcmp(a->verb, b->verb);
}

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof(a[0]))
struct me_cfg_fn_keyword verb_table[] = {
	{ "config", me_config_plugin },
	{ "create", me_create_model_policy },
	{ "list", me_ls_plugins },
	{ "list_model", me_ls_models },
	{ "load", me_load_plugin },
	{ "model", me_model },
	{ "start_consumer", me_start_consumer },
	{ "store", me_store },
};

void me_cfg_recv_cmd(struct ocm_event *e)
{
	struct ocm_cfg_cmd_iter cmd_iter;
	ocm_cfg_cmd_iter_init(&cmd_iter, e->cfg);

	ocm_cfg_cmd_t cmd;
	struct me_cfg_fn_keyword keyword;
	struct me_cfg_fn_keyword *kw;

	while (ocm_cfg_cmd_iter_next(&cmd_iter, &cmd) == 0) {
		keyword.verb = ocm_cfg_cmd_verb(cmd);
		kw = bsearch(&keyword, verb_table, ARRAY_SIZE(verb_table),
				sizeof(*kw), me_cfg_fn_keyword_cmp);

		if (kw) {
			kw->action(cmd);
		} else {
			me_log("Invalid verb '%s'\n", keyword.verb);
			me_cleanup(4);
		}
	}
}

void me_cfg_recv_err(struct ocm_event *e)
{
	me_log("Error from ocmd. Key: '%s'. msg: '%s'.\n",
			ocm_err_key(e->err), ocm_err_msg(e->err));
	me_cleanup(4);
}

int me_cfg_cb(struct ocm_event *e)
{
	switch (e->type) {
	case OCM_EVENT_ERROR:
		me_cfg_recv_err(e);
		break;
	case OCM_EVENT_CFG_RECEIVED:
		me_cfg_recv_cmd(e);
		break;
	default:
		break;
	}
	return 0;
}

int setup_configuration(const char *xprt, uint16_t port, const char *hostname)
{
	ocm_t me_ocm = ocm_create(xprt, port, NULL, me_log);
	if (!me_ocm) {
		me_log("Failed to create the ocm\n");
		return -1;
	}

	int rc;

	char key[512];
	sprintf(key, "%s/me", hostname);
	rc = ocm_register(me_ocm, key, me_cfg_cb);
	if (rc)
		return rc;

	return ocm_enable(me_ocm);
}

/* --- END configure --- */

typedef enum {
	ME_HOSTNAME=0,
	ME_THREAD_COUNT,
	ME_TRANSPORT,
	ME_SOCKNAME,
	ME_LOGFILE,
	ME_VERBOSE,
	ME_FOREGROUND,
	ME_HASH_RBT_SZ,
	ME_OCM_PORT,
	ME_MAX_SEM_INPUT,
	ME_N_OPTIONS
} ME_OPTION;

int has_arg[ME_N_OPTIONS] = {[0] = 0};
#define ME_DEFAULT_PORT 60001
#define ME_DEFAULT_OCM_PORT 60002

int main(int argc, char **argv) {
	int ret;
	int op;
	char *model_name;

	struct sigaction cleanup_act, logrotate_act;
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	cleanup_act.sa_handler = me_cleanup;
	cleanup_act.sa_flags = 0;
	cleanup_act.sa_mask = sigset;

	sigaction(SIGHUP, &cleanup_act, NULL);
	sigaction(SIGINT, &cleanup_act, NULL);
	sigaction(SIGTERM, &cleanup_act, NULL);

	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	logrotate_act.sa_handler = me_logrotate_act;
	logrotate_act.sa_flags = 0;
	logrotate_act.sa_mask = sigset;
	sigaction(SIGUSR1, &logrotate_act, NULL);

	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'F':
			foreground = 1;
			has_arg[ME_FOREGROUND] = 1;
			break;
		case 'S':
			sockname = strdup(optarg);
			has_arg[ME_SOCKNAME] = 1;
			break;
		case 'h':
			strcpy(myhostname, optarg);
			has_arg[ME_HOSTNAME] = 1;
			break;
		case 'l':
			logfile = strdup(optarg);
			has_arg[ME_LOGFILE] = 1;
			break;
		case 'x':
			transport = strdup(optarg);
			has_arg[ME_TRANSPORT] = 1;
			break;
		case 'p':
			num_worker_thread = atoi(optarg);
			has_arg[ME_THREAD_COUNT] = 1;
			break;
		case 'H':
			hash_rbt_sz = atoi(optarg);
			has_arg[ME_HASH_RBT_SZ] = 1;
			break;
		case 'O':
			ocm_port = atoi(optarg);
			has_arg[ME_OCM_PORT] = 1;
			break;
		case 'i':
			max_sem_input = atoi(optarg);
			has_arg[ME_MAX_SEM_INPUT] = 1;
			break;
		default:
			me_usage(argv);
			exit(1);
			break;
		}
	}

	/* Taking care of the log file */
	if (logfile)
		log_fp = me_open_log();
	else
		log_fp = stdout;

	if (!foreground) {
		if (daemon(1, 1)) {
			me_log("Model Evaluator: daemon(1,1) failed: "
					"errno = %d.\n", errno);
			me_cleanup(8);
		}
	}

	if (model_manager_init(hash_rbt_sz, max_sem_input))
		me_cleanup(1);

	/* Taking care of myhostname */
	if (myhostname[0] == '\0') {
		ret = gethostname(myhostname, sizeof(myhostname));
		if (ret) {
			me_log("gethostname error '%d'.\n", ret);
			me_cleanup(4);
		}
	}

	/* Create the attribute and keyword lists */
	av_list = av_new(128);
	kw_list = av_new(128);

	if (setup_control(sockname))
		me_cleanup(4);

	char *xprt = strtok(transport, ":");
	char *port_s = strtok(NULL, ":");
	uint16_t port_no;
	if (!port_s)
		port_no = ME_DEFAULT_PORT;
	else
		port_no = atoi(port_s);

	if (ocm_port == 0)
		ocm_port = ME_DEFAULT_OCM_PORT;

	/*
	 * Create worker threads
	 */
	int i;
	pthread_t worker_threads[num_worker_thread];
	for (i = 0; i < num_worker_thread; i++) {
		pthread_create(&worker_threads[i], NULL, evaluate_update, NULL);
	}

	me_log("Started ME Daemon version " VERSION ".\n");

	/* Setting up the listener port */
	if (transport)
		me_listen_on_transport(xprt, port_no);

	if (setup_configuration("sock", ocm_port, myhostname))
		me_cleanup(4);

	for (i = 0; i < num_worker_thread; i++) {
		ret = pthread_join(worker_threads[i], NULL);
		if (ret) {
			me_log("Failed to join the worker thread. Error %d\n",
					ret);
			exit(1);
		}
	}

	me_cleanup(0);
	return 0;
}
