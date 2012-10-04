/*
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
/*
 * This is the flat_file sampler. It saves a local metric set's data to
 * a flat file.
 */
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/un.h>
#include "ldms.h"
#include "ldmsd.h"

static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;

#define LOGFILE "/var/log/flat_file.log"
struct fmetric {
	ldms_metric_t md;
	uint64_t key;		/* component id or whatever else you like */
	LIST_ENTRY(fmetric) entry;
};

struct fset {
	ldms_set_t sd;
	char *file_path;
	FILE *file;
	LIST_HEAD(fmetric_list,  fmetric) metric_list;
	LIST_ENTRY(fset) entry;
};

LIST_HEAD(fset_list, fset) set_list;

static ldmsd_msg_log_f msglog;

ldms_metric_t compid_metric_handle;

char file_path[LDMSD_MAX_CONFIG_STR_LEN];
char set_name[LDMSD_MAX_CONFIG_STR_LEN];
char metric_name[LDMSD_MAX_CONFIG_STR_LEN];

static struct fset *get_fset(char *set_name)
{
	struct fset *ss;
	LIST_FOREACH(ss, &set_list, entry) {
		if (0 == strcmp(set_name, ldms_get_set_name(ss->sd)))
			return ss;
	}
	return NULL;
}

static struct fmetric *get_metric(struct fset *set, char *metric_name)
{
	struct fmetric *met;
	LIST_FOREACH(met, &set->metric_list, entry) {
		if (0 == strcmp(metric_name, ldms_get_metric_name(met->md)))
			return met;
	}
	return NULL;
}

static int add_set(char *set_name, char *file_path)
{
	FILE *file;
	ldms_set_t sd;
	struct fset *set = get_fset(set_name);
	if (set)
		return EEXIST;

	sd = ldms_get_set(set_name);
	if (!sd)
		return ENOENT;

	file = fopen(file_path, "a");
	if (!file)
		return errno;

	set = calloc(1, sizeof *set);
	if (!set) {
		fclose(file);
		return ENOMEM;
	}
	set->sd = sd;
	set->file = file;
	set->file_path = strdup(file_path);
	LIST_INSERT_HEAD(&set_list, set, entry);
	return 0;
}

static int remove_set(char *set_name)
{
	struct fset *set = get_fset(set_name);
	if (!set)
		return ENOENT;
	LIST_REMOVE(set, entry);
	free(set->file_path);
	fclose(set->file);
	return 0;
}

static int add_metric(char *set_name, char *metric_name, uint64_t key)
{
	ldms_metric_t *md;
	struct fmetric *metric;
	struct fset *set = get_fset(set_name);
	if (!set)
		return ENOENT;

	metric = get_metric(set, metric_name);
	if (metric)
		return EEXIST;

	md = ldms_get_metric(set->sd, metric_name);
	if (!md)
		return ENOENT;

	metric = calloc(1, sizeof *metric);
	if (!metric)
		return ENOMEM;

	metric->md = md;
	metric->key = key;
	LIST_INSERT_HEAD(&set->metric_list, metric, entry);
	return 0;
}

static int remove_metric(char *set_name, char *metric_name)
{
	struct fset *set;
	struct fmetric *met;
	set = get_fset(set_name);
	if (!set)
		return ENOENT;
	met = get_metric(set, metric_name);
	if (!met)
		return ENOENT;
	LIST_REMOVE(met, entry);
	return 0;
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	enum {
		ADD_SET,
		ADD_METRIC,
		REMOVE_SET,
		REMOVE_METRIC,
                COMPONENT_ID,
	} action;
	char *cmd, *set, *path, *metric, *key_s;
	int rc = EINVAL;
	uint64_t key;

	cmd = av_name(kwl, 1);
	if (0 == strcmp(cmd, "add_metric"))
		action = ADD_METRIC;
	else if (0 == strcmp(cmd, "remove_metric"))
		action = REMOVE_METRIC;
	else if (0 == strcmp(cmd, "add"))
		action = ADD_SET;
	else if (0 == strcmp(cmd, "remove"))
		action = REMOVE_SET;
	else {
		msglog("flatfile: Invalid configuration string '%s'\n",
		       cmd);
		rc = EINVAL;
		goto out;
	}
	pthread_mutex_lock(&cfg_lock);
	switch (action) {
	case ADD_SET:
		set = av_value(avl, "set");
		path = av_value(avl, "path");
		if (set && path)
			rc = add_set(set, path);
		break;
	case REMOVE_SET:
		set = av_value(avl, "set");
		if (set)
			rc = remove_set(set);
		break;
	case ADD_METRIC:
		set = av_value(avl, "set");
		metric = av_value(avl, "metric");
		key_s = av_value(avl, "key");
		if (set && metric && key_s) {
			key = strtoul(key_s, NULL, 0);
			rc = add_metric(set, metric, key);
		}
		break;
	case REMOVE_METRIC:
		set = av_value(avl, "set");
		metric = av_value(avl, "metric");
		if (set && metric && key_s)
			rc = remove_metric(set, metric);
		break;
	default:
		msglog("Invalid config statement.\n");
		rc = EINVAL;
	}
 out:
	pthread_mutex_unlock(&cfg_lock);
	return rc;
}

static ldms_set_t get_set()
{
	return NULL;
}

static char data_str[2048];
static int sample(void)
{
	struct fset *set;
	struct fmetric *met;
	int rc;
	pthread_mutex_lock(&cfg_lock);
	LIST_FOREACH(set, &set_list, entry) {
	  //check here for the generation number and see if it changes if you want. there is a function to get it.
	  //ldms_get_data_gn. metadata number changes when a new metric gets added or removed. data number
	  //with any change
		LIST_FOREACH(met, &set->metric_list, entry) {
			/* timestamp metric_name metric_value */
			sprintf(data_str, 
				"%"PRIu64" %30s %"PRIu64" %"PRIu64"\n",
				(uint64_t)time(NULL), ldms_get_metric_name(met->md), 
				met->key, ldms_get_u64(met->md));
			// msglog(data_str); //this logs the sample to the log file 
			rc = fprintf(set->file, data_str);
			if (rc <= 0)
				msglog("Error %d writing '%s' to '%s'\n", rc, data_str, set->file_path);
			//FIXME: possibly put in something to force timely regular flushes
			fflush(set->file); //tail the flat file if you want to see it
		}
	}
	pthread_mutex_unlock(&cfg_lock);
	return 0;
}

static void term(void)
{
}

static const char *usage(void)
{
	return  "    config flatfile add=<set_name>&<path>\n"
		"        - Adds a metric set and associated file to contain sampled values.\n"
		"        set_name    The name of the metric set\n"
		"        path        Path to a file that will contain the sampled metrics.\n"
		"                    The file will be created if it does not already exist\n"
		"                    and appeneded to if it does.\n"
		"    config flatfile remove=<set_name>\n"
		"        - Removes a metric set. The file is closed, but not destroyed\n"
		"        set_name    The name of the metric set\n"
		"    config flatfile add_metric=<set_name>&<metric_name>&key\n"
		"        - Add the specified metric to the set of values stored from the set\n"
		"        set_name    The name of the metric set.\n"
		"        metric_name The name of the metric.\n"
		"        key         An unique Id for the Metric. Typically the component_id.\n"
		"    config flatfile remove_metric=<set_name>&<metric_name>\n"
		"        - Stop storing values for the specified metric\n"
		"        set_name    The name of the metric set.\n"
		"        metric_name The name of the metric.\n";
}

static struct ldmsd_sampler flat_file_plugin = {
	.base = {
		.name = "flat_file",
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	msglog("flat_file: plugin loaded\n");
	return &flat_file_plugin.base;
}
