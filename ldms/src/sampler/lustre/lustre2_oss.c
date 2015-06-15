/*
 * Copyright (c) 2013-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2015 Sandia Corporation. All rights reserved.
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
	/* metric source status (sampler induced) */
	"status",

	/* real statistics */
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
	"unregister_page_removal_cb",
	"register_lock_cancel_cb",
	"unregister_lock_cancel_cb",
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

/**
 * OST metrics that are in single-metric-file source.
 *
 * For example, please see "/proc/fs/lustre/obdfilter/XXXXX/kbytesavail.
 */
const char *ost_single_attr[] = {
	"filesfree",
	"kbytesavail",
};
#define OST_SINGLE_ATTR_LEN __ALEN(ost_single_attr)

struct lustre_metric_src_list lms_list = {0};

static ldms_set_t set;
static ldmsd_msg_log_f msglog;
static char *producer_name;

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
	int rc, i, j;
	uint64_t metric_value;
	char metric_name[128];

	struct str_list_head *lh = construct_ost_list(osts);
	if (!lh)
		goto err0;

	/* Done calculating, now it is time to construct set */
	ldms_schema_t schema = ldms_schema_new("Lustre_OSS");
	if (schema)
		goto err1;
	char suffix[128];
	for (i = 0; i < OSS_SERVICES_LEN; i++) {
		sprintf(tmp_path, "/proc/fs/lustre/ost/OSS/%s/stats",
				oss_services[i]);
		sprintf(suffix, "#oss.%s", oss_services[i]);
		rc = stats_construct_routine(schema, tmp_path, "oss.lstats.",
					     suffix, &lms_list, stats_key,
					     STATS_KEY_LEN, stats_key_id);
		if (rc)
			goto err2;
	}
	struct str_list *sl;
	LIST_FOREACH(sl, lh, link) {
		/* For general stats */
		sprintf(tmp_path, "/proc/fs/lustre/obdfilter/%s/stats", sl->str);
		sprintf(suffix, "#ost.%s", sl->str);
		rc = stats_construct_routine(schema, tmp_path, "oss.lstats.",
					     suffix, &lms_list, obdf_key,
					     OBDF_KEY_LEN, obdf_key_id);
		if (rc)
			goto err2;
		for (j = 0; j < OST_SINGLE_ATTR_LEN; j++) {
			sprintf(tmp_path, "/proc/fs/lustre/osd-ldiskfs/%s/%s",
						sl->str, ost_single_attr[j]);
			rc = single_construct_routine(schema, tmp_path,
					"oss.lustre.", suffix, &lms_list);
			if (rc)
				goto err2;
		}
	}
	set = ldms_set_new(path, schema);
	if (!set) {
		rc = errno;
		goto err2;
	}
	ldms_schema_delete(schema);
	return 0;
err2:
	msglog(LDMSD_LINFO, "lustre_oss.c:create_metric_set@err2\n");
	lustre_metric_src_list_free(&lms_list);
	ldms_schema_delete(schema);
	msglog(LDMSD_LINFO, "WARNING: lustre_oss set DESTROYED\n");
	set = 0;
err1:
	msglog(LDMSD_LINFO, "lustre_oss.c:create_metric_set@err1\n");
	free_str_list(lh);
err0:
	msglog(LDMSD_LDEBUG, "%s:%s@err0\n", __FILE__, __func__);
	msglog(LDMSD_LDEBUG, "%s:%s: osts: %s\n", __FILE__, __func__, osts);
	return errno;
}

static void term(void)
{
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

/**
 * \brief Configuration
 *
 * (ldmsctl usage note)
 * <code>
 * config name=lustre_oss producer=<prod_name> instance=<inst_name> osts=<OST1>,...
 *     prod_name       The producer id value.
 *     inst_name     The set name.
 *     osts              The comma-separated list of the OSTs to sample from.
 * </code>
 * If osts is not given, the plugin will create ldms_set according to the
 * available OSTs at the time.
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value, *osts;

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "lustre2_oss: missing 'producer'\n");
		return ENOENT;
	}

	value = av_value(avl, "set");
	if (!value) {
		msglog(LDMSD_LERROR, "lustre2_oss: missing 'instance'\n");
		return EINVAL;
	}
	osts = av_value(avl, "osts");

	if (set) {
		msglog(LDMSD_LERROR, "lustre2_oss: Set already created.\n");
		return EINVAL;
	}
	int rc = create_metric_set(value, osts);
	if (rc)
		return rc;
	ldms_set_producer_name_set(set, producer_name);
}

static const char *usage(void)
{
	return
"config name=lustre_oss producer=<prod_name> instance=<inst_name> osts=<ost_list>\n"
"	<prod_name>	The producer name\n"
"	<inst_name>	The instance name\n"
"	<osts>		A comma separated list of OSTs\n"
"For osts: if not specified, all of the\n"
"currently available OSTs will be added.\n";
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	if (!set)
		return EINVAL;
	ldms_transaction_begin(set);

	struct lustre_metric_src *lms;

	/* For all stats */
	LIST_FOREACH(lms, &lms_list, link) {
		lms_sample(set, lms);
	}

out:
	ldms_transaction_end(set);
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
	set = NULL;
	lustre_sampler_set_msglog(pf);
	stats_key_id = str_map_create(STR_MAP_SIZE);
	if (!stats_key_id) {
		msglog(LDMSD_LERROR, "stats_key_id map create error!\n");
		goto err_nomem;
	}
	str_map_id_init(stats_key_id, stats_key, STATS_KEY_LEN, 1);

	obdf_key_id = str_map_create(STR_MAP_SIZE);
	if (!obdf_key_id) {
		msglog(LDMSD_LERROR, "obdf_key_id map create error!\n");
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
