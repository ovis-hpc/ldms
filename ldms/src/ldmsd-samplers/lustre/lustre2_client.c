/**
 * Copyright (c) 2013-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file lustre2_client.c
 * \brief Lustre client data sampler.
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
#include "ldmsd_sampler.h"

#include "lustre_sampler.h"

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

typedef struct lustre2_client_inst_s *lustre2_client_inst_t;
struct lustre2_client_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */

	struct str_list_head lh_llite;
	struct str_list_head lh_osc;
	struct str_list_head lh_mdc;

	struct lustre_metric_src_list lms_list;

};

/* ============== Sampler Plugin APIs ================= */

static
int lustre2_client_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	lustre2_client_inst_t inst = (void*)pi;
	struct str_list *sl;
	int i, rc = 0;
	char suffix[256];
	char *namebase[] = {"osc", "mdc", "llite"};
	char *tmp_path;
	struct str_list_head *heads[] = {&inst->lh_osc, &inst->lh_mdc,
					 &inst->lh_llite};
	char **keys[] = {stats_key, stats_key, llite_key};
	int keylen[] = {STATS_KEY_LEN, STATS_KEY_LEN, LLITE_KEY_LEN};

	tmp_path = malloc(PATH_MAX);
	if (!tmp_path) {
		rc = ENOMEM;
		goto out;
	}

	for (i = 0; i < sizeof(heads) / sizeof(*heads); i++) {
		LIST_FOREACH(sl, heads[i], link) {
			/* For general stats */
			snprintf(tmp_path, PATH_MAX,
				 "/proc/fs/lustre/%s/%s*/stats",
				 namebase[i], sl->str);
			snprintf(suffix, sizeof(suffix),
				 "#%s.%s", namebase[i], sl->str);
			rc = stats_construct_routine(schema, tmp_path,
					"client.lstats.", suffix,
					&inst->lms_list, keys[i], keylen[i]);
			if (rc)
				goto out;
		}
	}

out:
	if (tmp_path)
		free(tmp_path);
	return rc;
}

static
int lustre2_client_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	lustre2_client_inst_t inst = (void*)pi;
	struct lustre_metric_src *lms;
	LIST_FOREACH(lms, &inst->lms_list, link) {
		lms_sample(set, lms);
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *lustre2_client_desc(ldmsd_plugin_inst_t pi)
{
	return "lustre2_client - lustre2_client sampler plugin";
}

static
char *_help ="\
lustre2_client config synopsis: \
    config name=INST [COMMON_OPTIONS] llite=<CSV> mdc=<CSV> osc=<CSV>\n\
\n\
Option descriptions:\n\
    llite   A comma-separated list of LLITEs\n\
    osc     A comma-separated list of OSCs\n\
    mdc     A comma-separated list of MDCs\n\
\n\
For oscs,mdcs and llites: if not specified, NONE of the oscs/mdcs/llites will\n\
be added. If {oscs,mdcs,llites} is set to *, all of the available\n\
{oscs,mdcs,llites} at the time will be added.\n\
\n\
NOTE: The names that make up the list of oscs, mdcs and llites do not have\n\
to include the uid part. For example, 'lustre-ffff8803245d4000' is the\n\
actual file in /proc/fs/lustre/llite/, but you can just say llites=lustre to\n\
include this component into the set.\n\
"
;

static
const char *lustre2_client_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void lustre2_client_del(ldmsd_plugin_inst_t pi)
{
	lustre2_client_inst_t inst = (void*)pi;

	lustre_metric_src_list_free(&inst->lms_list);

	free_str_list(&inst->lh_llite);
	free_str_list(&inst->lh_mdc);
	free_str_list(&inst->lh_osc);
}

int construct_client_list(struct str_list_head *h, const char *clients,
			  const char *alt_dir_path)
{
	if (clients) {
		if (strcmp(clients, "*") == 0)
			/* OSTs is not given, get current ones from proc fs */
			return construct_dir_list(h, alt_dir_path);
		else
			/* OSTs is given */
			return construct_str_list(h, clients);
	}
	return 0;
}

static
int lustre2_client_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	lustre2_client_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;
	const char *val;

	rc = samp->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */

	struct {
		const char *name;
		const char *alt_name;
		const char *path;
		struct str_list_head *h;
	} *ent, ents[] = {
		{  "llite",  "llites",  "/proc/fs/lustre/llite",  &inst->lh_llite  },
		{  "osc",    "oscs",    "/proc/fs/lustre/osc",    &inst->lh_osc    },
		{  "mdc",    "mdcs",    "/proc/fs/lustre/mdc",    &inst->lh_mdc    },
		{   NULL,     NULL,      NULL,                     NULL            },
	};

	/* processing `llite`, `osc`, and `mdc` attributes */
	for (ent = ents; ent->name; ent++) {
		val = av_value(avl, ent->name);
		val = (val)?(val):av_value(avl, ent->alt_name);
		if (!val)
			continue;
		rc = construct_client_list(ent->h, val, ent->path);
		if (rc < 0)
			return -rc; /* rc == -errno */
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
int lustre2_client_init(ldmsd_plugin_inst_t pi)
{
	lustre2_client_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = lustre2_client_update_schema;
	samp->update_set = lustre2_client_update_set;

	LIST_INIT(&inst->lh_llite);
	LIST_INIT(&inst->lh_mdc);
	LIST_INIT(&inst->lh_osc);
	return 0;
}

static
struct lustre2_client_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "lustre2_client",

                /* Common Plugin APIs */
		.desc   = lustre2_client_desc,
		.help   = lustre2_client_help,
		.init   = lustre2_client_init,
		.del    = lustre2_client_del,
		.config = lustre2_client_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	lustre2_client_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
