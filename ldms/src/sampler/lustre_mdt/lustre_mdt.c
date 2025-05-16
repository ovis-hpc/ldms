/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
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
#include "lustre_mdt.h"
#include "lustre_mdt_general.h"
#include "lustre_mdt_job_stats.h"

#define _GNU_SOURCE

#define MDT_PATH "/proc/fs/lustre/mdt"
#define OSD_SEARCH_PATH "/proc/fs/lustre"

ovis_log_t luster_mdt_log;

static struct comp_id_data cid;

char producer_name[LDMS_PRODUCER_NAME_MAX];

/* red-black tree root for mdts */
static struct rbt mdt_tree;

struct mdt_data {
        char *fs_name;
        char *name;
        char *path;
        char *stats_path; /* md_stats */
        char *job_stats_path;
        char *osd_path;
        ldms_set_t general_metric_set; /* a pointer */
        struct rbn mdt_tree_node;
        struct rbt job_stats; /* key is jobid */
};

static int string_comparator(void *a, const void *b)
{
        return strcmp((char *)a, (char *)b);
}

static struct mdt_data *mdt_create(const char *mdt_name, const char *basedir)
{
        struct mdt_data *mdt;
        char path_tmp[PATH_MAX]; /* TODO: move large stack allocation to heap */
        char *state;

        ovis_log(luster_mdt_log, OVIS_LDEBUG, SAMP" mdt_create() %s from %s\n",
               mdt_name, basedir);
        mdt = calloc(1, sizeof(*mdt));
        if (mdt == NULL)
                goto out1;
        mdt->name = strdup(mdt_name);
        if (mdt->name == NULL)
                goto out2;
        snprintf(path_tmp, PATH_MAX, "%s/%s", basedir, mdt_name);
        mdt->path = strdup(path_tmp);
        if (mdt->path == NULL)
                goto out3;
        snprintf(path_tmp, PATH_MAX, "%s/md_stats", mdt->path);
        mdt->stats_path = strdup(path_tmp);
        if (mdt->stats_path == NULL)
                goto out4;
        snprintf(path_tmp, PATH_MAX, "%s/job_stats", mdt->path);
        mdt->job_stats_path = strdup(path_tmp);
        if (mdt->job_stats_path == NULL)
                goto out5;
        mdt->fs_name = strdup(mdt_name);
        if (mdt->fs_name == NULL)
                goto out6;
        if (strtok_r(mdt->fs_name, "-", &state) == NULL) {
                ovis_log(luster_mdt_log, OVIS_LWARNING, SAMP" unable to parse filesystem name from \"%s\"\n",
                       mdt->fs_name);
                goto out7;
        }
        mdt->general_metric_set = mdt_general_create(producer_name, mdt->fs_name, mdt->name, &cid);
        if (mdt->general_metric_set == NULL)
                goto out7;
        mdt->osd_path = mdt_general_osd_path_find(OSD_SEARCH_PATH, mdt->name);
        rbn_init(&mdt->mdt_tree_node, mdt->name);
        rbt_init(&mdt->job_stats, string_comparator);

        return mdt;
out7:
        free(mdt->fs_name);
out6:
        free(mdt->job_stats_path);
out5:
        free(mdt->stats_path);
out4:
        free(mdt->path);
out3:
        free(mdt->name);
out2:
        free(mdt);
out1:
        return NULL;
}

static void mdt_destroy(struct mdt_data *mdt)
{
        ovis_log(luster_mdt_log, OVIS_LDEBUG, SAMP" mdt_destroy() %s\n", mdt->name);
        mdt_general_destroy(mdt->general_metric_set);
        mdt_job_stats_destroy(&mdt->job_stats);
        free(mdt->osd_path);
        free(mdt->fs_name);
        free(mdt->job_stats_path);
        free(mdt->stats_path);
        free(mdt->path);
        free(mdt->name);
        free(mdt);
}

static void mdts_destroy()
{
        struct rbn *rbn;
        struct mdt_data *mdt;

        while (!rbt_empty(&mdt_tree)) {
                rbn = rbt_min(&mdt_tree);
                mdt = container_of(rbn, struct mdt_data,
                                   mdt_tree_node);
                rbt_del(&mdt_tree, rbn);
                mdt_destroy(mdt);
        }
}

/* List subdirectories in MDT_PATH to get list of
   MDT names.  Create mdt_data structures for any MDTS any that we
   have not seen, and delete any that we no longer see. */
static void mdts_refresh()
{
        struct dirent *dirent;
        DIR *dir;
        struct rbt new_mdt_tree;

        rbt_init(&new_mdt_tree, string_comparator);

        /* Make sure we have mdt_data objects in the new_mdt_tree for
           each currently existing directory.  We can find the objects
           cached in the global mdt_tree (in which case we move them
           from mdt_tree to new_mdt_tree), or they can be newly allocated
           here. */

        dir = opendir(MDT_PATH);
        if (dir == NULL) {
                ovis_log(luster_mdt_log, OVIS_LDEBUG, SAMP" unable to open obdfilter dir %s\n",
                       MDT_PATH);
                return;
        }
        while ((dirent = readdir(dir)) != NULL) {
                struct rbn *rbn;
                struct mdt_data *mdt;

                if (dirent->d_type != DT_DIR ||
                    strcmp(dirent->d_name, ".") == 0 ||
                    strcmp(dirent->d_name, "..") == 0)
                        continue;
                rbn = rbt_find(&mdt_tree, dirent->d_name);
                if (rbn) {
                        mdt = container_of(rbn, struct mdt_data,
                                           mdt_tree_node);
                        rbt_del(&mdt_tree, &mdt->mdt_tree_node);
                } else {
                        mdt = mdt_create(dirent->d_name, MDT_PATH);
                }
                if (mdt == NULL)
                        continue;
                rbt_ins(&new_mdt_tree, &mdt->mdt_tree_node);
        }
        closedir(dir);

        /* destroy any mdts remaining in the global mdt_tree since we
           did not see their associated directories this time around */
        mdts_destroy();

        /* copy the new_mdt_tree into place over the global mdt_tree */
        memcpy(&mdt_tree, &new_mdt_tree, sizeof(struct rbt));

        return;
}

static void mdts_sample()
{
        struct rbn *rbn;

        /* walk tree of known MDTs */
        RBT_FOREACH(rbn, &mdt_tree) {
                struct mdt_data *mdt;
                mdt = container_of(rbn, struct mdt_data, mdt_tree_node);
                mdt_general_sample(mdt->name, mdt->stats_path, mdt->osd_path,
                                   mdt->general_metric_set);
                mdt_job_stats_sample(producer_name, mdt->fs_name, mdt->name,
                                     mdt->job_stats_path, &mdt->job_stats);
        }
}

static int config(ldmsd_plug_handle_t handle,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        ovis_log(luster_mdt_log, OVIS_LDEBUG, SAMP" config() called\n");
	char *ival = av_value(avl, "producer");
	if (ival) {
		if (strlen(ival) < sizeof(producer_name)) {
			strncpy(producer_name, ival, sizeof(producer_name));
		} else {
                        ovis_log(luster_mdt_log, OVIS_LERROR, SAMP": config: producer name too long.\n");
                        return EINVAL;
		}
	}
	comp_id_helper_config(avl, &cid);
        return 0;
}

static int sample(ldmsd_plug_handle_t handle)
{
        ovis_log(luster_mdt_log, OVIS_LDEBUG, SAMP" sample() called\n");
        if (mdt_general_schema_is_initialized() < 0) {
                if (mdt_general_schema_init(&cid) < 0) {
                        ovis_log(luster_mdt_log, OVIS_LERROR, SAMP" general schema create failed\n");
                        return ENOMEM;
                }
        }
        if (mdt_job_stats_schema_is_initialized() < 0) {
                if (mdt_job_stats_schema_init() < 0) {
                        ovis_log(luster_mdt_log, OVIS_LERROR, SAMP" job stats schema create failed\n");
                        return ENOMEM;
                }
        }

        mdts_refresh();
        mdts_sample();

        return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	ovis_log(luster_mdt_log, OVIS_LDEBUG, SAMP" usage() called\n");
	return  "config name=" SAMP;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	luster_mdt_log = ldmsd_plug_log_get(handle);
        rbt_init(&mdt_tree, string_comparator);
        gethostname(producer_name, sizeof(producer_name));

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	ovis_log(luster_mdt_log, OVIS_LDEBUG, SAMP" destructor() called\n");
	mdts_destroy();
	mdt_general_schema_fini();
	mdt_job_stats_schema_fini();
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
