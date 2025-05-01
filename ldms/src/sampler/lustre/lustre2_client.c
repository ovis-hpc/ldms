/**
 * Copyright (c) 2013-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2018 Open Grid Computing, Inc. All rights reserved.
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
 *	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 *
 *	Redistributions in binary form must reproduce the above
 *	copyright notice, this list of conditions and the following
 *	disclaimer in the documentation and/or other materials provided
 *	with the distribution.
 *
 *	Neither the name of Sandia nor the names of any contributors may
 *	be used to endorse or promote products derived from this software
 *	without specific prior written permission.
 *
 *	Neither the name of Open Grid Computing nor the names of any
 *	contributors may be used to endorse or promote products derived
 *	from this software without specific prior written permission.
 *
 *	Modified source versions must be plainly marked as such, and
 *	must not be misrepresented as being the original software.
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
#include "ldmsd_plug_api.h"

#include "lustre_sampler.h"
#include "../sampler_base.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#define STR_MAP_SIZE 4093
#define SAMP "lustre2_client"

static struct lustre_metric_src_list lms_list = {0};

static ldms_set_t set;
static base_data_t base;

static ovis_log_t mylog;

static char tmp_path[PATH_MAX];

//OMAR
//"osc", "mdc", "llite"
/* OSC */
static char *osc_path = NULL;
static const char * default_osc_path[] = {
		"/sys/kernel/debug/lustre/osc",
		"/proc/fs/lustre/osc",
};
/* MDC */
static char *mdc_path = NULL;
static const char * default_mdc_path[] = {
		"/sys/kernel/debug/lustre/mdc",
		"/proc/fs/lustre/mdc",
};
/* LLITE */
static char *llite_path = NULL;
static const char * default_llite_path[] = {
		"/sys/kernel/debug/lustre/llite",
		"/proc/fs/lustre/llite",
};

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

#define LLITE_KEY_LEN ARRAY_SIZE(llite_key)

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
 * \param oscs The comma-separated list of OST clients. Being NULL means
 *	using all OSCs at the time.
 * \param mdcs Similar to \c oscs, but for MDS clients.
 * \param llites Similar to \c oscs, but for Lustre mount point.
 *
 * \returns 0 on success.
 * \returns \c errno on error.
 */
static int create_metric_set(const char *oscs, const char *mdcs,
			     const char *llites)
{
	int rc, i;

	/* Calculate size for Clients */
	struct str_list_head *lh_osc, *lh_mdc, *lh_llite;
	struct str_list_head *heads[] = {NULL, NULL, NULL};
	char **keys[] = {stats_key, stats_key, llite_key};
	int keylen[] = {STATS_KEY_LEN, STATS_KEY_LEN, LLITE_KEY_LEN};
	lh_osc = lh_mdc = lh_llite = 0;

	/* test if the osc default path exist */
	if (osc_path == NULL) {
		/* Try possible luster stat locations */
		for (i=0; i < ARRAY_SIZE(default_osc_path); i++) {
			osc_path = strdup(default_osc_path[i]);
			lh_osc = construct_client_list(oscs, osc_path);
			if (!lh_osc) {
				ovis_log(mylog, OVIS_LDEBUG, SAMP ": default osc path '%s'"
					" not found\n", osc_path);
				/* Set to NULL, if no path exist */
				osc_path = NULL;
				continue;
			}
			break;
		}
	} else {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": osc user path used\n");
		lh_osc = construct_client_list(oscs, osc_path);
	}


	if (!lh_osc) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": osc user and default paths failed\n");
		goto err0;
	}
	heads[0] = lh_osc;

	/* test if the mdc default path exist */
	if (mdc_path == NULL) {
		/* Try possible luster stat locations */
		for (i=0; i < ARRAY_SIZE( default_mdc_path); i++) {
			mdc_path = strdup(default_mdc_path[i]);
			lh_mdc = construct_client_list(mdcs, mdc_path);
			if (!lh_mdc) {
				ovis_log(mylog, OVIS_LDEBUG, SAMP ": default mdc path '%s'"
					" not found\n", mdc_path);
				/* Set to NULL, if no path exist */
				mdc_path = NULL;
				continue;
			}
			break;
		}
	} else {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": mdc user path used\n");
		lh_mdc = construct_client_list(mdcs, mdc_path);
	}

	if (!lh_mdc) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": mdc user and default paths failed\n");
		goto err0;
	}
	heads[1] = lh_mdc;

	/* test if the llite default path exist */
	if (llite_path == NULL) {
		/* Try possible luster stat locations */
		for (i=0; i < ARRAY_SIZE(default_llite_path); i++) {
			llite_path = strdup(default_llite_path[i]);
			lh_llite = construct_client_list(llites, llite_path);
			if (!lh_llite) {
				ovis_log(mylog, OVIS_LDEBUG, SAMP ": default llite path '%s'"
					" not found\n", llite_path);
				/* Set to NULL, if no path exist */
				llite_path = NULL;
				continue;
			}
			break;
		}
	} else {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": llite user path used\n");
		lh_llite = construct_client_list(llites, llite_path);
	}

	if (!lh_llite) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": llite user and default paths failed\n");
		goto err0;
	}
	heads[2] = lh_llite;

	ldms_schema_t schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": base_schema_new failed\n");
		goto err0;
	}

	char *namebase[] = {"osc", "mdc", "llite"};
	char *pathbase[] = {osc_path, mdc_path, llite_path};
	struct str_list *sl;

	char suffix[128];
	for (i = 0; i < ARRAY_SIZE(heads); i++) {
		LIST_FOREACH(sl, heads[i], link) {
			/* For general stats */
			sprintf(tmp_path, "%s/%s*/stats",
					pathbase[i], sl->str);
			sprintf(suffix, "#%s.%s", namebase[i], sl->str);
			rc = stats_construct_routine(schema, tmp_path,
					"client.", suffix, &lms_list, keys[i],
					keylen[i]);
			if (rc) {
				ovis_log(mylog, OVIS_LERROR, SAMP
					 ": stats_construct_routine err for %s\n", tmp_path);
				goto err1;
			}
		}
	}

	/* Done calculating, now it is time to construct set */
	set = base_set_new(base);
	if (!set) {
		rc = errno;
		ovis_log(mylog, OVIS_LERROR, SAMP ": base_set_new failed\n");
		goto err1;
	}
	return 0;
err1:
	ovis_log(mylog, OVIS_LINFO, SAMP ": create_metric_set@err1\n");
	lustre_metric_src_list_free(&lms_list);
	set = 0;
err0:
	for (i = 0; i < ARRAY_SIZE(heads); i++) {
		if (heads[i])
			free_str_list(heads[i]);
	}
	ovis_log(mylog, OVIS_LINFO, SAMP ": create_metric_set@err0\n");
	return errno;
}

static void term(ldmsd_plug_handle_t handle)
{
	if (set) {
		ldms_set_delete(set);
		set = NULL;
	}
	if (base) {
		base_del(base);
		base = NULL;
	}
}

/**
 * \brief Configuration
 *
 * (ldmsctl usage note)
 * <code>
 * config name=lustre2_client producer_name=<producer_name> instance_name=<instance_name> osts=<OST1>,...
 *     producer_name	   The producer id value.
 *     instance_name	 The set name.
 *     osts		 The comma-separated list of the OSTs to sample from.
 * </code>
 * If osts is not given, the plugin will create ldms_set according to the
 * available OSTs at the time.
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *oscs, *mdcs, *llites;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), "Lustre_Client", mylog);
	if (!base)
		return errno;

	oscs = av_value(avl, "osc");
	mdcs = av_value(avl, "mdc");
	llites = av_value(avl, "llite");

	char *pvalue = av_value(avl, "osc_path");
	if (pvalue) {
		osc_path = strdup(pvalue);
	}
	pvalue = av_value(avl, "mdc_path");
	if (pvalue) {
		mdc_path = strdup(pvalue);
	}
	pvalue = av_value(avl, "llite_path");
	if (pvalue) {
		llite_path = strdup(pvalue);
	}

	int rc = create_metric_set(oscs, mdcs, llites);
	if (rc) {
		base_del(base);
		base = NULL;
		return rc;
	}
	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return
"config name=" SAMP " " BASE_CONFIG_SYNOPSIS
"	[osc=<CSV>] [mdc=<CSV>] [llite=<CSV>] [osc_path=<oscpath>] [mdc_path=<mdcpath>] [llite_path=<llitepath>]\n"
"\n"
BASE_CONFIG_DESC
"    osc	  The comma-separated value list of OCSs.\n"
"    mdc	  The comma-separated value list of MDCs.\n"
"    llite	  The comma-separated value list of LLITEs.\n"
"    oscpath      User custom path to osc.\n"
"    mdcpath      User custom path to mdc.\n"
"    llitepath    User custom path to llite.\n"
"\n"
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

static int sample(ldmsd_plug_handle_t handle)
{
	if (!set)
		return EINVAL;


	base_sample_begin(base);

	struct lustre_metric_src *lms;

	/* For all stats */
	LIST_FOREACH(lms, &lms_list, link) {
		lms_sample(set, lms);
	}

	base_sample_end(base);
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	static int init_complete = 0;

	if (init_complete)
		return -1;

	mylog = ldmsd_plug_log_get(handle);
	set = NULL;
	lustre_sampler_set_pilog(mylog);
	init_complete = 1;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
