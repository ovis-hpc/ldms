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
 * \file lustre_client.c
 * \brief Lustre client data sampler.
 * \author Narate Taerat <narate@ogc.us>
 *
 * Stats files are from:
 * <code>
 * /proc/fs/lustre/osc/XXXXX/stats (regarding OSTs)
 * /proc/fs/lustre/mdc/XXXXX/stats (regarding MDTs)
 * /proc/fs/lustre/llite/XXXXX/stats (regarding mount points)
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
 * This will holds IDs for stats_key.
 */
struct str_map *stats_key_id = NULL;

/**
 * ID map for llite metrics.
 */
struct str_map *llite_key_id = NULL;

struct lustre_svc_stats_head svc_stats = {0};

static ldms_set_t set;
FILE *mf;
ldmsd_msg_log_f msglog;
uint64_t comp_id;

char tmp_path[PATH_MAX];

char *llite_key[] = {
	"dirty_pages_hits",
	"dirty_pages_misses",
	"read_bytes",
	"write_bytes",
	"brw_read",
	"brw_write",
	"osc_read",
	"osc_write",
	"ioctl",
	"open",
	"close",
	"mmap",
	"seek",
	"fsync",
	"readdir",
		/* inode operation */
	"setattr",
	"truncate",
	"flock",
	"getattr",
		/* dir inode operation */
	"create",
	"link",
	"unlink",
	"symlink",
	"mkdir",
	"rmdir",
	"mknod",
	"rename",
		/* special inode operation */
	"statfs",
	"alloc_inode",
	"setxattr",
	"getxattr",
	"listxattr",
	"removexattr",
	"inode_permission",
};

#define LLITE_KEY_LEN sizeof(llite_key)/sizeof(llite_key[0])

/**
 * \brief Construct client string list.
 * Construct a list of strings according to \c clients.
 * If \c clients is NULL, the directories in \c alt_dir_patht will be used
 * instead.
 * \param clients The comma-separated string.
 * \param alt_dir_path The alternative directory for the string list.
 * \returns NULL on error.
 * \returns The head of the list on success.
 */
struct str_list_head* construct_client_list(const char *clients,
					    const char *alt_dir_path)
{
	if (clients) {
		if (strcmp(clients, "*") == 0)
			/* OSTs is not given, get current ones from proc fs */
			return construct_dir_list(alt_dir_path);
		else
			/* OSTs is given */
			return construct_str_list(clients);
	}

	static struct str_list_head empty = {0};
	return &empty;
}

/**
 * \brief Create metric set.
 *
 * \param path The set name, e.g. vic1/lustre_oss (it does look like a path).
 * \param oscs The comma-separated list of OST clients. Being NULL means
 * 	using all OSCs at the time.
 * \param mdcs Similar to \c oscs, but for MDS clients.
 * \param llites Similar to \c oscs, but for Lustre mount point.
 *
 * \returns 0 on success.
 * \returns \c errno on error.
 */
static int create_metric_set(const char *path, const char *oscs,
			     const char *mdcs, const char *llites)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i, j, metric_count;
	char metric_name[LUSTRE_NAME_MAX];

	/* First calculate the set size */
	metric_count = 0;
	tot_meta_sz = tot_data_sz = 0;

	/* Calculate size for Clients */
#define LH_OSC 0
#define LH_MDC 1
#define LH_LLITE 2
#define HEADCOUNT 3
	struct str_list_head *heads[ HEADCOUNT] = {0};
	char **keys[] = {stats_key, stats_key, llite_key};
	int keylen[] = {STATS_KEY_LEN, STATS_KEY_LEN, LLITE_KEY_LEN};
	struct str_map *maps[] = {stats_key_id, stats_key_id, llite_key_id};

	heads[LH_OSC] = construct_client_list(oscs, "/proc/fs/lustre/osc");
	if (!heads[LH_OSC])
		goto err0;

	heads[LH_MDC] = construct_client_list(mdcs, "/proc/fs/lustre/mdc");
	if (!heads[LH_MDC])
		goto err0;

	heads[LH_LLITE] = construct_client_list(llites, "/proc/fs/lustre/llite");
	if (!heads[LH_LLITE])
		goto err0;

	char *namebase[] = {"osc", "mdc", "llite"};
	struct str_list *sl;
	for (i = 0; i < sizeof(heads) / sizeof(*heads); i++) {
		LIST_FOREACH(sl, heads[i], link) {
			/* For general stats */
			for (j = 0; j < keylen[i]; j++) {
				snprintf(metric_name, LUSTRE_NAME_MAX,
					 "lstats.%s#%s.%s",
						keys[i][j], namebase[i],
						sl->str);
				ldms_get_metric_size(metric_name, LDMS_V_U64,
						&meta_sz, &data_sz);
				tot_meta_sz += meta_sz;
				tot_data_sz += data_sz;
				metric_count++;
			}
		}
	}

	/* Done calculating, now it is time to construct set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		goto err0;
	char suffix[LUSTRE_NAME_MAX];
	for (i = 0; i < HEADCOUNT; i++) {
		LIST_FOREACH(sl, heads[i], link) {
			/* For general stats */
			snprintf(tmp_path, PATH_MAX,
				 "/proc/fs/lustre/%s/%s*/stats",
					namebase[i], sl->str);
			snprintf(suffix, LUSTRE_NAME_MAX, "#%s.%s",
				 namebase[i], sl->str);
			rc = stats_construct_routine(set, comp_id, tmp_path,
					"lstats.", suffix, &svc_stats, keys[i],
					keylen[i], maps[i]);
			if (rc)
				goto err1;
		}
	}

	rc = 0;
	goto out;
err1:
	msglog(LDMS_LDEBUG,"lustre_oss.c:create_metric_set@err1\n");
	lustre_svc_stats_list_free(&svc_stats);
	ldms_destroy_set(set);
	msglog(LDMS_LDEBUG,"WARNING: lustre_oss set DESTROYED\n");
	set = 0;
err0:
	for (i = 0; i < HEADCOUNT; i++) {
		if (heads[i])
			free_str_list(heads[i]);
	}
	msglog(LDMS_LDEBUG,"lustre_oss.c:create_metric_set@err0\n");
	rc = errno;
out:
	for (i = 0; i < HEADCOUNT; i++) {
		// 1) could the heads be passed to the list fillers instead of built by the list fillers?
		// 2) free_str_list(heads[i]); FTFY: OGC we need to destroy heads unless they are the empty head.
	}
#undef LH_OSC
#undef LH_MDC
#undef LH_LLITE
#undef HEADCOUNT
	return rc;
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
 * config name=lustre_client component_id=<comp_id> set=<setname> osts=<OST1>,...
 *     comp_id     The component id value.
 *     setname     The set name.
 *     osts        The comma-separated list of the OSTs to sample from.
 * </code>
 * If osts is not given, the plugin will create ldms_set according to the
 * available OSTs at the time.
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value, *oscs, *mdcs, *llites;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtol(value, NULL, 0);

	value = av_value(avl, "set");
	oscs = av_value(avl, "osc");
	mdcs = av_value(avl, "mdc");
	llites = av_value(avl, "llite");
	if (value)
		create_metric_set(value, oscs, mdcs, llites);

	return 0;
}

static const char *usage(void)
{
	return
"config name=lustre2_client [OPTIONS]\n"
"    OPTIONS:\n"
"	component_id=NUMBER	The component id value.\n"
"	set=STRING		The set name.\n"
"	osc=STR,STR,...	The list of OCSs.\n"
"	mdc=STR,STR,...	The list of MDCs.\n"
"	llite=STR,STR,...	The list of llites.\n"
"For oscs,mdcs and llites: if not specified, NONE of the\n"
"oscs/mdcs/llites will be added. If {oscs,mdcs,llites} is set to *, all\n"
"of the available {oscs,mdcs,llites} at the time will be added.\n"
"\n"
"NOTE: The names that make up the list of oscs, mdcs and llites do not have\n"
"to include the uid part. For example, 'lustre-ffff8803245d4000' is the\n"
"actual file in /proc/fs/lustre/llite/, but you can just say llites=lustre to\n"
"include this component into the set.\n"
;
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

static struct ldmsd_sampler lustre_client_plugin = {
	.base = {
		.name = "lustre_client",
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	static int init_complete = 0;
	if (init_complete)
		goto out;
	msglog = pf;
	lustre_sampler_set_msglog(pf);
	stats_key_id = str_map_create(STR_MAP_SIZE);
	if (!stats_key_id) {
		msglog(LDMS_LDEBUG,"stats_key_id map create error!\n");
		goto err_nomem;
	}
	str_map_id_init(stats_key_id, stats_key, STATS_KEY_LEN, 1);

	llite_key_id = str_map_create(STR_MAP_SIZE);
	if (!llite_key_id) {
		msglog(LDMS_LDEBUG,"llite_key_id map create error!\n");
		goto err_nomem;
	}
	str_map_id_init(llite_key_id, llite_key, LLITE_KEY_LEN, 1);

	init_complete = 1;
out:
	return &lustre_client_plugin.base;
err_nomem:
	errno = ENOMEM;
	return NULL;
}
