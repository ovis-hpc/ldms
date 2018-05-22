/*
 * Copyright (c) 2013-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2016 Sandia Corporation. All rights reserved.
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
#include "ldms_jobid.h"

#include "lustre_sampler.h"

#define STR_MAP_SIZE 4093

static struct lustre_metric_src_list lms_list = {0};

static ldms_set_t set;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static uint64_t compid;
static char tmp_path[PATH_MAX];

static int metric_offset = 1;
LJI_GLOBALS;

char *llite_key[] = {
	/* metric source status (sampler induced) */
	"status",

	/* real statistics */
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

	/* Additional derived metrics */
	"read_bytes.rate",
	"write_bytes.rate",
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
	int rc, i;

	/* Calculate size for Clients */
	struct str_list_head *lh_osc, *lh_mdc, *lh_llite;
	struct str_list_head *heads[] = {NULL, NULL, NULL};
	char **keys[] = {stats_key, stats_key, llite_key};
	int keylen[] = {STATS_KEY_LEN, STATS_KEY_LEN, LLITE_KEY_LEN};
	lh_osc = lh_mdc = lh_llite = 0;

	lh_osc = construct_client_list(oscs, "/proc/fs/lustre/osc");
	if (!lh_osc)
		goto err0;
	heads[0] = lh_osc;

	lh_mdc = construct_client_list(mdcs, "/proc/fs/lustre/mdc");
	if (!lh_mdc)
		goto err0;
	heads[1] = lh_mdc;

	lh_llite = construct_client_list(llites, "/proc/fs/lustre/llite");
	if (!lh_llite)
		goto err0;
	heads[2] = lh_llite;

	ldms_schema_t schema = ldms_schema_new("Lustre_Client");
	if (!schema)
		goto err0;

	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		rc = ENOMEM;
		goto err1;
	}
	metric_offset++;
	rc = LJI_ADD_JOBID(schema);
	if (rc < 0) {
		goto err1;

	}
	char *namebase[] = {"osc", "mdc", "llite"};
	struct str_list *sl;

	char suffix[128];
	for (i = 0; i < sizeof(heads) / sizeof(*heads); i++) {
		LIST_FOREACH(sl, heads[i], link) {
			/* For general stats */
			sprintf(tmp_path, "/proc/fs/lustre/%s/%s*/stats",
					namebase[i], sl->str);
			sprintf(suffix, "#%s.%s", namebase[i], sl->str);
			rc = stats_construct_routine(schema, tmp_path,
					"client.lstats.", suffix, &lms_list, keys[i],
					keylen[i]);
			if (rc)
				goto err1;
		}
	}

	/* Done calculating, now it is time to construct set */
	set = ldms_set_new(path, schema);
	if (!set) {
		rc = errno;
		goto err1;
	}
	ldms_schema_delete(schema);
	return 0;
err1:
	msglog(LDMSD_LINFO, "lustre_oss.c:create_metric_set@err1\n");
	lustre_metric_src_list_free(&lms_list);
	ldms_schema_delete(schema);
	msglog(LDMSD_LINFO, "WARNING: lustre_oss set DESTROYED\n");
	set = 0;
err0:
	for (i = 0; i < sizeof(heads) / sizeof(*heads); i++) {
		if (heads[i])
			free_str_list(heads[i]);
	}
	msglog(LDMSD_LINFO, "lustre_oss.c:create_metric_set@err0\n");
	return errno;
}

static void term(struct ldmsd_plugin *self)
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
 * config name=lustre_client producer_name=<producer_name> instance_name=<instance_name> osts=<OST1>,...
 *     producer_name       The producer id value.
 *     instance_name     The set name.
 *     osts              The comma-separated list of the OSTs to sample from.
 * </code>
 * If osts is not given, the plugin will create ldms_set according to the
 * available OSTs at the time.
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value, *oscs, *mdcs, *llites;

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "lustre2_client: missing producer\n");
		return ENOENT;
	}
	value = av_value(avl, "component_id");
	if (value)
		compid = (uint64_t)(atoi(value));
	else
		compid = 0;

	LJI_CONFIG(value,avl);

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, "lustre2_client: missing instance\n");
		return EINVAL;
	}
	oscs = av_value(avl, "osc");
	mdcs = av_value(avl, "mdc");
	llites = av_value(avl, "llite");

	if (set) {
		msglog(LDMSD_LERROR, "lustre2_client: Set already created.\n");
		return EINVAL;
	}
	int rc = create_metric_set(value, oscs, mdcs, llites);
	if (rc)
		return rc;
	//add specialized metrics
	ldms_metric_set_u64(set, 0, compid);
	ldms_set_producer_name_set(set, producer_name);
	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return
"config name=lustre2_client producer=<prod_name> instance=<inst_name> [OPTIONS]\n"
"    	prod_name             The producer name\n"
"       inst_name             The instance name\n"
"    OPTIONS:\n"
"	osc=STR,STR,...	      The list of OCSs.\n"
"	mdc=STR,STR,...	      The list of MDCs.\n"
"	llite=STR,STR,...     The list of llites.\n"
"	with_jobid=<jid>      Collect jobid data or not.\n"
"       component_id=<id>     Numeric identifier for host.\n"
LJI_DESC
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

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int sample(struct ldmsd_sampler *self)
{
	if (!set)
		return EINVAL;
	ldms_transaction_begin(set);

	struct lustre_metric_src *lms;
	LJI_SAMPLE(set, 1);
	/* For all stats */
	LIST_FOREACH(lms, &lms_list, link) {
		lms_sample(set, lms);
	}

	ldms_transaction_end(set);
	return 0;
}

static struct ldmsd_sampler lustre_client_plugin = {
	.base = {
		.name = "lustre_client",
		.type = LDMSD_PLUGIN_SAMPLER,
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
	set = NULL;
	lustre_sampler_set_msglog(pf);

	init_complete = 1;
out:
	return &lustre_client_plugin.base;
err_nomem:
	errno = ENOMEM;
	return NULL;
}
