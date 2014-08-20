/*
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
/**
 * \file lustre_oss.c
 * \brief Lustre OSS data sampler.
 * \author Narate Taerat <narate@ogc.us>
 *
 * This plugin samples data from multiple OSTs in the OSS it is running on.
 * The plugin will sample stats in OSTs according to the targets given at
 * configuration time. If no target is given, it will listen to OSTs that are
 * available at the time. Please see configure() for more information about this
 * plugin configuration.
 *
 * This plugin gets its content from:
 * <code>
 * /proc/fs/lustre/ost/OSS/<service>/stats
 * /proc/fs/lustre/obdfilter/lustre-OSTXXXX/stats
 * </code>
 *
 */


#define _GNU_SOURCE
#include <dirent.h>
#include <limits.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include "ldms.h"
#include "ldmsd.h"

#include "str_map.h"
#include "lustre_sampler.h"

#define STR_MAP_SIZE 4093

/**
 * These are the services under /proc/fs/lustre/oss/OSS/
 */
char *oss_services[] = {
	"ost",
	"ost_create",
	"ost_io",
	"ost_seq"
};
#define OSS_SERVICES_LEN (__ALEN(oss_services))

/**
 * This will holds IDs for stats_key.
 */
struct str_map *stats_key_id;

/**
 * IDs for obdfilter keys.
 */
struct str_map *obdf_key_id;

char *obdf_key[] = {
	"iocontrol",
	"get_info",
	"set_info_async",
	"attach",
	"detach",
	"setup",
	"precleanup",
	"cleanup",
	"process_config",
	"postrecov",
	"add_conn",
	"del_conn",
	"connect",
	"reconnect",
	"disconnect",
	"fid_init",
	"fid_fini",
	"statfs",
	"statfs_async",
	"packmd",
	"unpackmd",
	"checkmd",
	"preallocate",
	"precreate",
	"create",
	"create_async",
	"destroy",
	"setattr",
	"setattr_async",
	"getattr",
	"getattr_async",
	"brw",
	"brw_async",
	"prep_async_page",
	"get_lock",
	"queue_async_io",
	"queue_group_io",
	"trigger_group_io",
	"set_async_flags",
	"teardown_async_page",
	"merge_lvb",
	"update_lvb",
	"adjust_kms",
	"punch",
	"sync",
	"migrate",
	"copy",
	"iterate",
	"preprw",
	"commitrw",
	"enqueue",
	"match",
	"change_cbdata",
	"find_cbdata",
	"cancel",
	"cancel_unused",
	"join_lru",
	"init_export",
	"destroy_export",
	"extent_calc",
	"llog_init",
	"llog_connect",
	"llog_finish",
	"pin",
	"unpin",
	"import_event",
	"notify",
	"health_check",
	"quotacheck",
	"quotactl",
	"quota_adjust_qunit",
	"ping",
	"register_page_removal_cb",
	"LPROCFS_OBD_OP_INIT(num_private_stats,stats,unregister_page_removal_cb",
	"register_lock_cancel_cb",
	"stats,unregister_lock_cancel_cb",
	"pool_new",
	"pool_rem",
	"pool_add",
	"pool_del",
	"getref",
	"putref",

	/* private to obdfilter */
	"read_bytes",
	"write_bytes",
};

#define OBDF_KEY_LEN __ALEN(obdf_key)

struct lustre_svc_stats_head svc_stats = {0};

static ldms_set_t set;
FILE *mf;
ldmsd_msg_log_f msglog;
uint64_t comp_id;

char tmp_path[PATH_MAX];


/**
 * \brief Construct string list out of a given comma-separated list of OSTs.
 *
 * \param osts The comma-separated list of OSTs.
 * \returns NULL on error.
 * \returns ::str_list_head pointer on success.
 */
struct str_list_head* construct_ost_list(const char *osts)
{
	if (osts)
		/* OSTs is given */
		return construct_str_list(osts);
	else
		/* OSTs is not given, get current ones from proc fs */
		return construct_dir_list("/proc/fs/lustre/obdfilter");
}

/**
 * \brief Create metric set.
 *
 * Currently, lustre_oss samples all possible metrics in the given OSTs.
 * In the future, it will support metric filtering too.
 *
 * \param path The set name, e.g. vic1/lustre_oss (it does look like a path).
 * \param osts The comma-separated list of OSTs. NULL means all OSTs available
 * 	at the time.
 *
 * \returns 0 on success.
 * \returns \c errno on error.
 */
static int create_metric_set(const char *path, const char *osts)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i, j, metric_count;
	char metric_name[LUSTRE_NAME_MAX];

	/* First calculate the set size */
	metric_count = 0;
	tot_meta_sz = tot_data_sz = 0;
	/* Calculate size for OSS */
	for (i = 0; i < OSS_SERVICES_LEN; i++) {
		for (j = 0; j < STATS_KEY_LEN; j++) {
			snprintf(metric_name, LUSTRE_NAME_MAX,
				 "lstats.%s#oss.%s", stats_key[j],
					oss_services[i]);
			ldms_get_metric_size(metric_name, LDMS_V_U64,
						  &meta_sz, &data_sz);
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
			metric_count++;
		}
	}

	/* Calculate size for OSTs */
	struct str_list_head *lh = construct_ost_list(osts);
	if (!lh)
		goto err0;
	struct str_list *sl;
	LIST_FOREACH(sl, lh, link) {
		/* For general stats */
		for (j = 0; j < OBDF_KEY_LEN; j++) {
			snprintf(metric_name, LUSTRE_NAME_MAX,
				"lstats.%s#ost.%s", obdf_key[j],
					sl->str);
			ldms_get_metric_size(metric_name, LDMS_V_U64,
					     &meta_sz, &data_sz);
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
			metric_count++;
		}
	}

	/* Done calculating, now it is time to construct set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		goto err1;
	char suffix[LUSTRE_NAME_MAX];
	for (i = 0; i < OSS_SERVICES_LEN; i++) {
		snprintf(tmp_path, PATH_MAX,
			"/proc/fs/lustre/ost/OSS/%s/stats",
				oss_services[i]);
		snprintf(suffix, LUSTRE_NAME_MAX, "#oss.%s", oss_services[i]);
		rc = stats_construct_routine(set, comp_id, tmp_path, "lstats.",
					     suffix, &svc_stats, stats_key,
					     STATS_KEY_LEN, stats_key_id);
		if (rc)
			goto err2;
	}
	LIST_FOREACH(sl, lh, link) {
		/* For general stats */
		snprintf(tmp_path, PATH_MAX,
			 "/proc/fs/lustre/obdfilter/%s/stats", sl->str);
		snprintf(suffix, LUSTRE_NAME_MAX, "#ost.%s", sl->str);
		rc = stats_construct_routine(set, comp_id, tmp_path, "lstats.",
					     suffix, &svc_stats, obdf_key,
					     OBDF_KEY_LEN, obdf_key_id);
		if (rc)
			goto err2;
	}

	free_str_list(lh);
	return 0;
err2:
	msglog("lustre_oss.c:create_metric_set@err2\n");
	lustre_svc_stats_list_free(&svc_stats);
	ldms_destroy_set(set);
	msglog("WARNING: lustre_oss set DESTROYED\n");
	set = 0;
err1:
	msglog("lustre_oss.c:create_metric_set@err1\n");
	free_str_list(lh);
err0:
	msglog("lustre_oss.c:create_metric_set@err0\n");
	return errno;
}

static void term(void)
{
	if (set)
		ldms_destroy_set(set);
	set = NULL;
}

/**
 * \brief Configuration
 *
 * (ldmsctl usage note)
 * <code>
 * config name=lustre_oss component_id=<comp_id> set=<setname> osts=<OST1>,...
 *     comp_id     The component id value.
 *     setname     The set name.
 *     osts        The comma-separated list of the OSTs to sample from.
 * </code>
 * If osts is not given, the plugin will create ldms_set according to the
 * available OSTs at the time.
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value, *osts;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtol(value, NULL, 0);

	value = av_value(avl, "set");
	osts = av_value(avl, "osts");
	if (value)
		create_metric_set(value, osts);

	return 0;
}

static const char *usage(void)
{
	return "config name=lustre_oss component_id=<comp_id> set=<setname>\n"
		"	component_id	The component id value.\n"
		"	set		The set name.\n"
		"	osts		The list of OSTs.\n"
		"For mdts: if not specified, all of the\n"
		"currently available MDTs will be added.\n";
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	if (!set)
		return EINVAL;
	ldms_begin_transaction(set);

	struct lustre_svc_stats *lss;

	/* For all stats */
	LIST_FOREACH(lss, &svc_stats, link) {
		lss_sample(lss);
	}

	ldms_end_transaction(set);
	return 0;
}

static struct ldmsd_sampler lustre_oss_plugin = {
	.base = {
		.name = "lustre_oss",
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	int init_complete = 0;
	if (init_complete)
		goto out;
	msglog = pf;
	lustre_sampler_set_msglog(pf);
	stats_key_id = str_map_create(STR_MAP_SIZE);
	if (!stats_key_id) {
		msglog("stats_key_id map create error!\n");
		goto err_nomem;
	}
	str_map_id_init(stats_key_id, stats_key, STATS_KEY_LEN, 1);

	obdf_key_id = str_map_create(STR_MAP_SIZE);
	if (!obdf_key_id) {
		msglog("obdf_key_id map create error!\n");
		goto err_nomem;
	}
	str_map_id_init(obdf_key_id, obdf_key, OBDF_KEY_LEN, 1);

	init_complete = 1;
out:
	return &lustre_oss_plugin.base;

err_nomem:
	errno = ENOMEM;
	return NULL;
}
