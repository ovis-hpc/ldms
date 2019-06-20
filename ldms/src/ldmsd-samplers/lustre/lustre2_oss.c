/**
 * Copyright (c) 2013-2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2016,2018 Open Grid Computing, Inc. All rights reserved.
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
#include "ldmsd_sampler.h"

#include "lustre_sampler.h"

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

typedef struct lustre2_oss_inst_s *lustre2_oss_inst_t;
struct lustre2_oss_inst_s {
	struct ldmsd_plugin_inst_s base;

	struct str_list_head lh_ost;
	struct lustre_metric_src_list lms_list;
};

int construct_ost_list(struct str_list_head *h, const char *osts)
{
	if (osts) {
		if (strcmp(osts, "*") == 0)
			return construct_dir_list(h, "/proc/fs/lustre/obdfilter");
		return construct_str_list(h, osts);
	}
	return 0;
}

/* ============== Sampler Plugin APIs ================= */

static
int lustre2_oss_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	lustre2_oss_inst_t inst = (void*)pi;
	int rc, i;
	char suffix[128];
	char *tmp_path;
	struct str_list *sl;

	tmp_path = malloc(PATH_MAX);
	if (!tmp_path)
		return ENOMEM;

	for (i = 0; i < OSS_SERVICES_LEN; i++) {
		snprintf(tmp_path, PATH_MAX, "/proc/fs/lustre/ost/OSS/%s/stats",
			 oss_services[i]);
		sprintf(suffix, "#oss.%s", oss_services[i]);
		rc = stats_construct_routine(schema, tmp_path, "oss.lstats.",
					     suffix, &inst->lms_list, stats_key,
					     STATS_KEY_LEN);
		if (rc)
			goto out;
	}
	LIST_FOREACH(sl, &inst->lh_ost, link) {
		/* For general stats */
		sprintf(tmp_path, "/proc/fs/lustre/obdfilter/%s/stats", sl->str);
		sprintf(suffix, "#ost.%s", sl->str);
		rc = stats_construct_routine(schema, tmp_path, "oss.lstats.",
					     suffix, &inst->lms_list, obdf_key,
					     OBDF_KEY_LEN);
		if (rc)
			goto out;
		for (i = 0; i < OST_SINGLE_ATTR_LEN; i++) {
			sprintf(tmp_path, "/proc/fs/lustre/osd-ldiskfs/%s/%s",
				sl->str, ost_single_attr[i]);
			rc = single_construct_routine(schema, tmp_path,
					"oss.lustre.", suffix, &inst->lms_list);
			if (rc)
				goto out;
		}
	}
out:
	free(tmp_path);
	return rc;
}

static
int lustre2_oss_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	lustre2_oss_inst_t inst = (void*)pi;
	struct lustre_metric_src *lms;

	LIST_FOREACH(lms, &inst->lms_list, link) {
		lms_sample(set, lms);
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *lustre2_oss_desc(ldmsd_plugin_inst_t pi)
{
	return "lustre2_oss - lustre2_oss sampler plugin";
}

static
char *_help = "\
lustre2_oss configuration synopsis:\n\
    config name=INST [COMMON_OPTIONS] ost=<CSV>\n\
\n\
Option descriptions:\n\
    ost    A comma-separated list of OSTs. `*` for all available OSTs.\n\
           This parameter is REQUIRED.\n\
";

static
const char *lustre2_oss_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void lustre2_oss_del(ldmsd_plugin_inst_t pi)
{
	lustre2_oss_inst_t inst = (void*)pi;

	/* The undo of lustre2_oss_init and instance cleanup */
	free_str_list(&inst->lh_ost);
	lustre_metric_src_list_free(&inst->lms_list);
}

static
int lustre2_oss_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	lustre2_oss_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;
	const char *val;

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */
	val = json_attr_find_str(json, "osts");
	val = val?val:json_attr_find_str(json, "ost");
	if (!val) {
		snprintf(ebuf, ebufsz, "%s: missing `ost` attribute.\n",
			 pi->inst_name);
		return EINVAL;
	}
	rc = construct_ost_list(&inst->lh_ost, val);
	if (rc < 0) {
		snprintf(ebuf, ebufsz, "%s: error constructing ost list, "
			 "errno: %d.\n", pi->inst_name, -rc);
		return -rc;
	}
	if (rc == 0) {
		snprintf(ebuf, ebufsz, "%s: no OST found.\n", pi->inst_name);
		return ENOENT;
	}

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;
	return 0;
}

static
int lustre2_oss_init(ldmsd_plugin_inst_t pi)
{
	lustre2_oss_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = lustre2_oss_update_schema;
	samp->update_set = lustre2_oss_update_set;

	/* NOTE More initialization code here if needed */
	LIST_INIT(&inst->lh_ost);
	LIST_INIT(&inst->lms_list);
	return 0;
}

static
struct lustre2_oss_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "lustre2_oss",

                /* Common Plugin APIs */
		.desc   = lustre2_oss_desc,
		.help   = lustre2_oss_help,
		.init   = lustre2_oss_init,
		.del    = lustre2_oss_del,
		.config = lustre2_oss_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	lustre2_oss_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
