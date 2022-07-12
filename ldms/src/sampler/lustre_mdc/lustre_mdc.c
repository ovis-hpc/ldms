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

static struct comp_id_data cid;

ldmsd_msg_log_f log_fn;
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

	log_fn(LDMSD_LDEBUG, SAMP" mdc_create() %s from %s\n",
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
		log_fn(LDMSD_LWARNING, SAMP
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
	log_fn(LDMSD_LDEBUG, SAMP" mdc_destroy() %s\n", mdc->name);
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
			log_fn(LDMSD_LDEBUG, SAMP" unable to open mdc dir %s\n",
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

static int config(struct ldmsd_plugin *self,
		  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	log_fn(LDMSD_LDEBUG, SAMP" config() called\n");
	char *ival = av_value(avl, "producer");
	if (ival) {
		if (strlen(ival) < sizeof(producer_name)) {
			strncpy(producer_name, ival, sizeof(producer_name));
		} else {
			log_fn(LDMSD_LERROR, SAMP": config: producer name too long.\n");
			return EINVAL;
		}
	}
	ival = av_value(avl, "mdc_timing");
	if (ival && ival[0] == '0')
		mdc_timing = 0;
	(void)base_auth_parse(avl, &auth, log_fn);
	int je = jobid_helper_config(avl);
	if (je) {
		log_fn(LDMSD_LERROR, SAMP": job_set name too long\n");
		return ENAMETOOLONG;
	}
	comp_id_helper_config(avl, &cid);
	if (mdc_timing) {
		DIR *dchk = opendir(PERM_CHECK_PATH);
		if (!dchk) {
			log_fn(LDMSD_LERROR, SAMP": cannot open %s (%s)\n",
				PERM_CHECK_PATH, STRERROR(errno));
			log_fn(LDMSD_LERROR, SAMP": use mdc_timing=0 or "
				"run ldmsd with more privileges\n");
			mdc_timing = -1;
			return EPERM;
		} else {
			closedir(dchk);
		}
	}
	return 0;
}

static int sample(struct ldmsd_sampler *self)
{
	if (mdc_timing < 0)
		return EPERM;
	if (mdc_general_schema_is_initialized() < 0) {
		if (mdc_general_schema_init(&cid, mdc_timing) < 0) {
			log_fn(LDMSD_LERROR, SAMP" general schema create failed\n");
			return ENOMEM;
		}
	}

	int err = mdcs_refresh(); /* running out of set memory is an error */
	mdcs_sample();

	return err;
}

static void term(struct ldmsd_plugin *self)
{
	log_fn(LDMSD_LDEBUG, SAMP" term() called\n");
	mdcs_destroy();
	mdc_general_schema_fini();
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static const char *usage(struct ldmsd_plugin *self)
{
	log_fn(LDMSD_LDEBUG, SAMP" usage() called\n");
	return  "config name=" SAMP;
}

static struct ldmsd_sampler mdc_plugin = {
	.base = {
		.name = SAMP,
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
	log_fn = pf;
	log_fn(LDMSD_LDEBUG, SAMP" get_plugin() called ("PACKAGE_STRING")\n");
	rbt_init(&mdc_tree, string_comparator);
	gethostname(producer_name, sizeof(producer_name));

	return &mdc_plugin.base;
}
