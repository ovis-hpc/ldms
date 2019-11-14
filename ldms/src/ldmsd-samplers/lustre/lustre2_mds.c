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
 * \file lustre_mds.c
 * \brief Lustre MDS data sampler.
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

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#include "lustre_sampler.h"

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

typedef struct lustre2_mds_inst_s *lustre2_mds_inst_t;
struct lustre2_mds_inst_s {
	struct ldmsd_plugin_inst_s base;

	struct str_list_head lh_mdt;
	struct lustre_metric_src_list lms_list;
};

int construct_mdt_list(struct str_list_head *h, const char *mdts)
{
	if (mdts) {
		if (strcmp(mdts, "*") == 0)
			return construct_dir_list(h, "/proc/fs/lustre/mdt");
		return construct_str_list(h, mdts);
	}
	return 0;
}
/* ============== Sampler Plugin APIs ================= */

static
int lustre2_mds_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	lustre2_mds_inst_t inst = (void*)pi;
	int i, rc = 0;
	char suffix[256];
	char *tmp_path;

	tmp_path = malloc(PATH_MAX);
	if (!tmp_path)
		return ENOMEM;

	for (i = 0; i < MDS_SERVICES_LEN; i++) {
		sprintf(tmp_path, "/proc/fs/lustre/mds/MDS/%s/stats",
				mds_services[i]);
		sprintf(suffix, "#mds.%s", mds_services[i]);
		rc = stats_construct_routine(schema, tmp_path, "mds.lstats.",
					     suffix, &inst->lms_list, stats_key,
					     STATS_KEY_LEN);
		if (rc)
			goto out;
	}
	struct str_list *sl;
	LIST_FOREACH(sl, &inst->lh_mdt, link) {
		/* For general stats */
		sprintf(tmp_path, "/proc/fs/lustre/mdt/%s/stats", sl->str);
		sprintf(suffix, "#mdt.%s", sl->str);
		rc = stats_construct_routine(schema, tmp_path, "mds.lstats.",
					     suffix, &inst->lms_list, stats_key,
					     STATS_KEY_LEN);
		if (rc)
			goto out;
		/* For md_stats */
		sprintf(tmp_path, "/proc/fs/lustre/mdt/%s/md_stats", sl->str);
		sprintf(suffix, "#mdt.%s", sl->str);
		rc = stats_construct_routine(schema, tmp_path, "md_stats.",
					     suffix, &inst->lms_list,
					     md_stats_key, MD_STATS_KEY_LEN);
		if (rc)
			goto out;
	}
	rc = 0;

out:
	if (tmp_path)
		free(tmp_path);
	return rc;
}

static
int lustre2_mds_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	lustre2_mds_inst_t inst = (void*)pi;
	struct lustre_metric_src *lms;

	LIST_FOREACH(lms, &inst->lms_list, link) {
		lms_sample(set, lms);
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *lustre2_mds_desc(ldmsd_plugin_inst_t pi)
{
	return "lustre2_mds - lustre2_mds sampler plugin";
}

static
char *_help = "\
lustre2_mds configuration synopsis:\n\
    config name=INST [COMMON_OPTIONS] mdt=<CSV>\n\
\n\
Option descriptions:\n\
    mdt    A comma-separated list of MDTs. It can be `*` for all MDTs.\n\
           This parameter is REQUIRED.\n\
";

static
const char *lustre2_mds_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void lustre2_mds_del(ldmsd_plugin_inst_t pi)
{
	lustre2_mds_inst_t inst = (void*)pi;

	lustre_metric_src_list_free(&inst->lms_list);
	free_str_list(&inst->lh_mdt);
}

static
int lustre2_mds_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	lustre2_mds_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;
	json_entity_t mdts;
	char *attr_name;

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */
	mdts = json_value_find(json, "mdts");
	mdts = mdts?mdts:json_value_find(json, "mdt");
	attr_name = mdts?"mdts":"mdt";
	if (!mdts) {
		snprintf(ebuf, ebufsz, "%s: `mdt` not specified.\n",
			 pi->inst_name);
		return EINVAL;
	}
	if (mdts->type != JSON_STRING_VALUE) {
		snprintf(ebuf, ebufsz, "%s: The given '%s' value is not "
				"a string.\n", pi->inst_name, attr_name);
		return EINVAL;
	}
	rc = construct_mdt_list(&inst->lh_mdt, (char *)json_value_str(mdts)->str);
	if (rc < 0) {
		snprintf(ebuf, ebufsz, "%s: mdt list construct error: %d\n",
			 pi->inst_name, -rc);
		return -rc;
	}
	if (rc == 0) {
		snprintf(ebuf, ebufsz, "%s: No mdt found.\n", pi->inst_name);
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
int lustre2_mds_init(ldmsd_plugin_inst_t pi)
{
	lustre2_mds_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = lustre2_mds_update_schema;
	samp->update_set = lustre2_mds_update_set;

	LIST_INIT(&inst->lh_mdt);
	LIST_INIT(&inst->lms_list);
	return 0;
}

static
struct lustre2_mds_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "lustre2_mds",

                /* Common Plugin APIs */
		.desc   = lustre2_mds_desc,
		.help   = lustre2_mds_help,
		.init   = lustre2_mds_init,
		.del    = lustre2_mds_del,
		.config = lustre2_mds_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	lustre2_mds_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
