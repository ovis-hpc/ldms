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
 * \file lustre_mds.c
 * \brief Lustre MDS data sampler.
 * \author Narate Taerat <narate@ogc.us>
 *
 * This plugin samples data from multiple MDTs in the MDS it is running on.
 * The plugin will sample stats in MDTs according to the targets given at
 * configuration time. If no target is given, it will listen to MDTs that are
 * available at the time. Please see configure() for more information about this
 * plugin configuration.
 *
 * This plugin gets its content from:
 * <code>
 * /proc/fs/lustre/mdt/xxxx-MDT####/md_stats
 * /proc/fs/lustre/mdt/xxxx-MDT####/stats
 * /proc/fs/lustre/mds/MDS/mdt/stats
 * </code>
 *
 */


#define _GNU_SOURCE
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
#include <limits.h>
#include "ldms.h"
#include "ldmsd.h"

#include "str_map.h"
#include "lustre_sampler.h"

#define STR_MAP_SIZE 4093

#define MD_STATS_KEY_LEN (__ALEN(md_stats_key))
/**
 * These are the keys specific to /proc/fs/lustre/mdt/XXXXX/stats
 */
char *md_stats_key[] = {
	"open",
	"close",
	"mknod",
	"link",
	"unlink",
	"mkdir",
	"rmdir",
	"rename",
	"getattr",
	"setattr",
	"getxattr",
	"setxattr",
	"statfs",
	"sync",
	"samedir_rename",
	"crossdir_rename",
	"status" /* status of md_stats file */
};

/**
 * md_stats_key to IDs.
 */
struct str_map *md_stats_key_id;

#define MDS_SERVICES_LEN (__ALEN(mds_services))
/**
 * These are the services under /proc/fs/lustre/mds/MDS/
 */
char *mds_services[] = {
	"mdt",
	"mdt_fld",
	"mdt_out",
	"mdt_readpage",
	"mdt_seqm",
	"mdt_seqs",
	"mdt_setattr",
};

/**
 * This will holds IDs for stats_key.
 */
struct str_map *stats_key_id;

struct lustre_metric_src_list lms_list = {0};

static uint64_t counter;
static ldms_set_t set;
FILE *mf;
ldmsd_msg_log_f msglog;
uint64_t comp_id;

char tmp_path[PATH_MAX];

/**
 * \brief Construct string list out of a given comma-separated list of MDTs.
 *
 * \param mdts The comma-separated list of MDTs.
 * \returns NULL on error.
 * \returns ::str_list_head pointer on success.
 */
struct str_list_head* construct_mdt_list(const char *mdts)
{
	if (mdts)
		/* MDTs is given */
		return construct_str_list(mdts);
	else
		/* MDTs is not given, get current ones from proc fs */
		return construct_dir_list("/proc/fs/lustre/mdt");
}

/**
 * \brief Create metric set.
 *
 * Currently, lustre_mds samples all possible metrics in the given MDTs.
 * In the future, it will support metric filtering too.
 *
 * \param path The set name, e.g. vic1/lustre_mds (it does look like a path).
 * \param mdts The comma-separated list of MDTs. NULL means all MDTs available
 * 	at the time.
 *
 * \returns 0 on success.
 * \returns \c errno on error.
 */
static int create_metric_set(const char *path, const char *mdts)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i, j, metric_count;
	uint64_t metric_value;
	char metric_name[128];

	/* First calculate the set size */
	metric_count = 0;
	tot_meta_sz = tot_data_sz = 0;
	/* Calculate size for MDS */
	for (i = 0; i < MDS_SERVICES_LEN; i++) {
		for (j = 0; j < STATS_KEY_LEN; j++) {
			sprintf(metric_name, "mds.lstats.%s#mds.%s", stats_key[j],
					mds_services[i]);
			ldms_get_metric_size(metric_name, LDMS_V_U64,
						  &meta_sz, &data_sz);
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
			metric_count++;
		}
	}

	/* Calculate size for MDTs */
	struct str_list_head *lh = construct_mdt_list(mdts);
	if (!lh)
		goto err0;
	struct str_list *sl;
	LIST_FOREACH(sl, lh, link) {
		/* For general stats */
		for (j = 0; j < STATS_KEY_LEN; j++) {
			sprintf(metric_name, "mds.lstats.%s#mdt.%s", stats_key[j],
					sl->str);
			ldms_get_metric_size(metric_name, LDMS_V_U64,
					     &meta_sz, &data_sz);
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
			metric_count++;
		}
		/* For md_stats */
		for (j = 0; j < MD_STATS_KEY_LEN; j++) {
			sprintf(metric_name, "md_stats.%s#mdt.%s", md_stats_key[j],
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
	char suffix[128];
	for (i = 0; i < MDS_SERVICES_LEN; i++) {
		sprintf(tmp_path, "/proc/fs/lustre/mds/MDS/%s/stats",
				mds_services[i]);
		sprintf(suffix, "#mds.%s", mds_services[i]);
		rc = stats_construct_routine(set, comp_id, tmp_path,
					     "mds.lstats.", suffix,
					     &lms_list, stats_key,
					     STATS_KEY_LEN, stats_key_id);
		if (rc)
			goto err2;
	}
	LIST_FOREACH(sl, lh, link) {
		/* For general stats */
		sprintf(tmp_path, "/proc/fs/lustre/mdt/%s/stats", sl->str);
		sprintf(suffix, "#mdt.%s", sl->str);
		rc = stats_construct_routine(set, comp_id, tmp_path, "mds.lstats.",
					     suffix, &lms_list, stats_key,
					     STATS_KEY_LEN, stats_key_id);
		if (rc)
			goto err2;
		/* For md_stats */
		sprintf(tmp_path, "/proc/fs/lustre/mdt/%s/md_stats", sl->str);
		sprintf(suffix, "#mdt.%s", sl->str);
		rc = stats_construct_routine(set, comp_id, tmp_path,
					     "md_stats.", suffix, &lms_list,
					     md_stats_key, MD_STATS_KEY_LEN,
					     md_stats_key_id);
		if (rc)
			goto err2;
	}

	return 0;
err2:
	msglog("lustre_mds.c:create_metric_set@err2\n");
	lustre_metric_src_list_free(&lms_list);
	ldms_destroy_set(set);
	msglog("WARNING: lustre_mds set DESTROYED\n");
	set = 0;
err1:
	msglog("lustre_mds.c:create_metric_set@err1\n");
	free_str_list(lh);
err0:
	msglog("lustre_mds.c:create_metric_set@err0\n");
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
 * config name=lustre_mds component_id=<comp_id> set=<setname> mdts=<MDT1>,...
 *     component_id   The component id value.
 *     set            The set name.
 *     mdts           The comma-separated list of the MDTs to sample from.
 * </code>
 * If mdts is not given, the plugin will create ldms_set according to the
 * available MDTs at the time.
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value, *mdts;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtol(value, NULL, 0);

	value = av_value(avl, "set");
	mdts = av_value(avl, "mdts");
	if (!value)
		return EINVAL;

	return create_metric_set(value, mdts);
}

static const char *usage(void)
{
	return
"config name=lustre_mds component_id=<comp_id> set=<setname> mdts=MDT1,...\n"
"	component_id	The component id value.\n"
"	set		The set name.\n"
"	mdts		The list of MDTs.\n"
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

	struct lustre_metric_src *lms;

	/* For all stats */
	LIST_FOREACH(lms, &lms_list, link) {
		lms_sample(lms);
	}

 out:
	ldms_end_transaction(set);
	return 0;
}

static struct ldmsd_sampler lustre_mds_plugin = {
	.base = {
		.name = "lustre_mds",
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
	lustre_sampler_set_msglog(pf);
	stats_key_id = str_map_create(STR_MAP_SIZE);
	if (!stats_key_id) {
		msglog("stats_key_id map create error!\n");
		goto err_nomem;
	}
	str_map_id_init(stats_key_id, stats_key, STATS_KEY_LEN, 1);
	md_stats_key_id = str_map_create(STR_MAP_SIZE);
	if (!md_stats_key_id) {
		msglog("md_stats_key_id map create error!\n");
		goto err_nomem;
	}
	str_map_id_init(md_stats_key_id, md_stats_key, MD_STATS_KEY_LEN, 1);

	return &lustre_mds_plugin.base;
err_nomem:
	errno = ENOMEM;
	return NULL;
}
