/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 * Copyright 2022 NTESS.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#include <limits.h>
#include <string.h>
#include <dirent.h>
#include <coll/rbt.h>
#include <sys/queue.h>
#include <unistd.h>
#include "ldms.h"
#include "ldmsd.h"
#include "config.h"
#include "lustre_mdc.h"
#include "lustre_mdc_general.h"
#include "jobid_helper.h"

#define _GNU_SOURCE

#define MDC_PATH "/proc/fs/lustre/mdc"
#define TIMING_SEARCH_PATH "/sys/kernel/debug/lustre/mdc/"
#define PERM_CHECK_PATH "/sys/kernel/debug"

ovis_log_t lustre_mdc_log;
static struct comp_id_data cid;

char producer_name[LDMS_PRODUCER_NAME_MAX];

/* red-black tree root for mdcs */
static struct rbt mdc_tree;

static struct base_auth auth;
static int mdc_timing = 1; /* 0 means do not, -1 means cannot */

struct mdc_data {
	char *fs_name;
	char *name;
	char *path;
	char *md_stats_path; /* operations: /proc/fs/lustre/mdc/ * /md_stats */
	char *timing__stats_path; /* timings: /sys/kernel/debug/lustre/mdc/ * /stats */
	ldms_set_t general_metric_set;
	struct rbn mdc_tree_node;
};

static int string_comparator(void *a, const void *b)
{
	return strcmp((char *)a, (char *)b);
}

static struct mdc_data *mdc_create(const char *mdc_name, const char *basedir)
{
	struct mdc_data *mdc;
	char path_tmp[PATH_MAX];
	char *state;

	ovis_log(lustre_mdc_log, OVIS_LDEBUG, "mdc_create() %s from %s\n",
	       mdc_name, basedir);
	mdc = calloc(1, sizeof(*mdc));
	if (mdc == NULL)
		goto out1;
	mdc->name = strdup(mdc_name);
	if (mdc->name == NULL)
		goto out2;
	snprintf(path_tmp, PATH_MAX, "%s/%s", basedir, mdc_name);
	mdc->path = strdup(path_tmp);
	if (mdc->path == NULL)
		goto out3;
	snprintf(path_tmp, PATH_MAX, "%s/md_stats", mdc->path);
	mdc->md_stats_path = strdup(path_tmp);
	if (mdc->md_stats_path == NULL)
		goto out4;
	snprintf(path_tmp, PATH_MAX, "%s/%s/stats", TIMING_SEARCH_PATH,
		mdc_name );
	mdc->timing__stats_path = strdup(path_tmp);
	if (mdc->timing__stats_path == NULL)
		goto out5;
	mdc->fs_name = strdup(mdc_name);
	if (mdc->fs_name == NULL)
		goto out6;
	if (strtok_r(mdc->fs_name, "-", &state) == NULL) {
		ovis_log(lustre_mdc_log, OVIS_LWARNING, SAMP
			": unable to parse filesystem name from \"%s\"\n",
		       mdc->fs_name);
		goto out7;
	}
	mdc->general_metric_set = mdc_general_create(producer_name,
							mdc->fs_name,
							mdc->name, &cid,
							&auth);
	if (mdc->general_metric_set == NULL)
		goto out7;
	rbn_init(&mdc->mdc_tree_node, mdc->name);

	return mdc;
out7:
	free(mdc->fs_name);
out6:
	free(mdc->timing__stats_path);
out5:
	free(mdc->md_stats_path);
out4:
	free(mdc->path);
out3:
	free(mdc->name);
out2:
	free(mdc);
out1:
	return NULL;
}

static void mdc_destroy(struct mdc_data *mdc)
{
	ovis_log(lustre_mdc_log, OVIS_LDEBUG, "mdc_destroy() %s\n", mdc->name);
	mdc_general_destroy(mdc->general_metric_set);
	free(mdc->timing__stats_path);
	free(mdc->fs_name);
	free(mdc->md_stats_path);
	free(mdc->path);
	free(mdc->name);
	free(mdc);
}

static void mdcs_destroy()
{
	struct rbn *rbn;
	struct mdc_data *mdc;

	while (!rbt_empty(&mdc_tree)) {
		rbn = rbt_min(&mdc_tree);
		mdc = container_of(rbn, struct mdc_data,
				   mdc_tree_node);
		rbt_del(&mdc_tree, rbn);
		mdc_destroy(mdc);
	}
}

static int dir_once_log;
/* List subdirectories in MDC_PATH to get list of
   MDC names.  Create mdc_data structures for any MDCS any that we
   have not seen, and delete any that we no longer see. */
static int mdcs_refresh()
{
	struct dirent *dirent;
	DIR *dir;
	struct rbt new_mdc_tree;
	int err = 0;

	rbt_init(&new_mdc_tree, string_comparator);

	/* Make sure we have mdc_data objects in the new_mdc_tree for
	   each currently existing directory.  We can find the objects
	   cached in the global mdc_tree (in which case we move them
	   from mdc_tree to new_mdc_tree), or they can be newly allocated
	   here. */

	dir = opendir(MDC_PATH);
	if (dir == NULL) {
		if (!dir_once_log) {
			ovis_log(lustre_mdc_log, OVIS_LDEBUG, "unable to open mdc dir %s\n",
			       MDC_PATH); /* expected if lustre all unmounted */
			dir_once_log = 1;
		}
		return 0;
	}
	dir_once_log = 0;
	while ((dirent = readdir(dir)) != NULL) {
		struct rbn *rbn;
		struct mdc_data *mdc;

		if (dirent->d_type != DT_DIR ||
		    strcmp(dirent->d_name, ".") == 0 ||
		    strcmp(dirent->d_name, "..") == 0)
			continue;
		rbn = rbt_find(&mdc_tree, dirent->d_name);
		errno = 0;
		if (rbn) {
			mdc = container_of(rbn, struct mdc_data,
					   mdc_tree_node);
			rbt_del(&mdc_tree, &mdc->mdc_tree_node);
		} else {
			mdc = mdc_create(dirent->d_name, MDC_PATH);
		}
		if (mdc == NULL) {
			err = errno;
			continue;
		}
		rbt_ins(&new_mdc_tree, &mdc->mdc_tree_node);
	}
	closedir(dir);

	/* destroy any mdcs remaining in the global mdc_tree since we
	   did not see their associated directories this time around */
	mdcs_destroy();

	/* copy the new_mdc_tree into place over the global mdc_tree */
	memcpy(&mdc_tree, &new_mdc_tree, sizeof(struct rbt));

	return err;
}

static void mdcs_sample()
{
	struct rbn *rbn;

	/* walk tree of known MDCs */
	RBT_FOREACH(rbn, &mdc_tree) {
		struct mdc_data *mdc;
		mdc = container_of(rbn, struct mdc_data, mdc_tree_node);
		mdc_general_sample(mdc->name, mdc->md_stats_path,
				mdc->timing__stats_path,
				mdc->general_metric_set, mdc_timing);
	}
}

static int config(ldmsd_plug_handle_t handle,
		  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	ovis_log(lustre_mdc_log, OVIS_LDEBUG, "config() called\n");
	char *ival = av_value(avl, "producer");
	if (ival) {
		if (strlen(ival) < sizeof(producer_name)) {
			strncpy(producer_name, ival, sizeof(producer_name));
		} else {
			ovis_log(lustre_mdc_log, OVIS_LERROR, "config: producer name too long.\n");
			return EINVAL;
		}
	}
	ival = av_value(avl, "auto_reset");
	mdc_auto_reset = 1;
	if (ival && ival[0] == '0')
		mdc_auto_reset = 0;
	ival = av_value(avl, "mdc_timing");
	if (ival && ival[0] == '0')
		mdc_timing = 0;
	(void)base_auth_parse(avl, &auth, lustre_mdc_log);
	int je = jobid_helper_config(avl);
	if (je) {
		ovis_log(lustre_mdc_log, OVIS_LERROR, "job_set name too long\n");
		return ENAMETOOLONG;
	}
	comp_id_helper_config(avl, &cid);
	if (mdc_timing) {
		DIR *dchk = opendir(PERM_CHECK_PATH);
		if (!dchk) {
			ovis_log(lustre_mdc_log, OVIS_LERROR, "cannot open %s (%s)\n",
				PERM_CHECK_PATH, STRERROR(errno));
			ovis_log(lustre_mdc_log, OVIS_LERROR, "use mdc_timing=0 or "
				"run ldmsd with more privileges\n");
			mdc_timing = -1;
			return EPERM;
		} else {
			closedir(dchk);
		}
	}
	return 0;
}

static int sample(ldmsd_plug_handle_t handle)
{
	if (mdc_timing < 0)
		return EPERM;
	if (mdc_general_schema_is_initialized() < 0) {
		if (mdc_general_schema_init(&cid, mdc_timing) < 0) {
			ovis_log(lustre_mdc_log, OVIS_LERROR, "general schema create failed\n");
			return ENOMEM;
		}
	}

	int err = mdcs_refresh(); /* running out of set memory is an error */
	mdcs_sample();

	return err;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	ovis_log(lustre_mdc_log, OVIS_LDEBUG, "usage() called\n");
	return  "config name=" SAMP;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	lustre_mdc_log = ldmsd_plug_log_get(handle);
	rbt_init(&mdc_tree, string_comparator);
	gethostname(producer_name, sizeof(producer_name));

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	ovis_log(lustre_mdc_log, OVIS_LDEBUG, "term() called\n");
	mdcs_destroy();
	mdc_general_schema_fini();
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
