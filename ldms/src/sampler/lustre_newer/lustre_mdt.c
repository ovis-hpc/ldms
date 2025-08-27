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
#include "ldmsd_plug_api.h"
#include "config.h"
#include "lustre_mdt.h"
#include "lustre_mdt_general.h"
#include "lustre_mdt_job_stats.h"
#include "lustre_shared.h"

#define _GNU_SOURCE

#define MDT_PATH "/proc/fs/lustre/mdt"

static const char * const possible_osd_base_paths[] = {
	"/sys/fs/lustre", /* at least lustre >= 1.15 (probably >= 1.12) */
	"/proc/fs/lustre", /* older lustre */
	NULL
};

struct mdt_data {
        char *fs_name;
        char *name;
        char *path;
        char *md_stats_path; /* md_stats */
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

static struct mdt_data *mdt_create(lm_context_t ctxt, const char *mdt_name, const char *basedir)
{
        struct mdt_data *mdt;
        char path_tmp[PATH_MAX];
        char *state;

        ovis_log(ctxt->log, OVIS_LDEBUG, "mdt_create() %s from %s\n",
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
        mdt->md_stats_path = strdup(path_tmp);
        if (mdt->md_stats_path == NULL)
                goto out4;
        snprintf(path_tmp, PATH_MAX, "%s/job_stats", mdt->path);
        mdt->job_stats_path = strdup(path_tmp);
        if (mdt->job_stats_path == NULL)
                goto out5;
        mdt->fs_name = strdup(mdt_name);
        if (mdt->fs_name == NULL)
                goto out6;
        if (strtok_r(mdt->fs_name, "-", &state) == NULL) {
                ovis_log(ctxt->log, OVIS_LWARNING, "unable to parse filesystem name from \"%s\"\n",
                       mdt->fs_name);
                goto out7;
        }
        mdt->general_metric_set = mdt_general_create(ctxt, mdt->fs_name, mdt->name);
        if (mdt->general_metric_set == NULL)
                goto out7;
        mdt->osd_path = lustre_osd_dir_find(possible_osd_base_paths,
					    mdt->name,
					    ctxt->log);
        rbn_init(&mdt->mdt_tree_node, mdt->name);
        rbt_init(&mdt->job_stats, string_comparator);

        return mdt;
out7:
        free(mdt->fs_name);
out6:
        free(mdt->job_stats_path);
out5:
        free(mdt->md_stats_path);
out4:
        free(mdt->path);
out3:
        free(mdt->name);
out2:
        free(mdt);
out1:
        return NULL;
}

static void mdt_destroy(lm_context_t ctxt, struct mdt_data *mdt)
{
        ovis_log(ctxt->log, OVIS_LDEBUG, "mdt_destroy() %s\n", mdt->name);
        mdt_general_destroy(ctxt, mdt->general_metric_set);
        mdt_job_stats_destroy(ctxt, &mdt->job_stats);
        free(mdt->osd_path);
        free(mdt->fs_name);
        free(mdt->job_stats_path);
        free(mdt->md_stats_path);
        free(mdt->path);
        free(mdt->name);
        free(mdt);
}

static void mdts_destroy(lm_context_t ctxt)
{
        struct rbn *rbn;
        struct mdt_data *mdt;

        while (!rbt_empty(&ctxt->mdt_tree)) {
                rbn = rbt_min(&ctxt->mdt_tree);
                mdt = container_of(rbn, struct mdt_data,
                                   mdt_tree_node);
                rbt_del(&ctxt->mdt_tree, rbn);
                mdt_destroy(ctxt, mdt);
        }
}

/* List subdirectories in MDT_PATH to get list of
   MDT names.  Create mdt_data structures for any MDTS that we
   have not seen, and delete any that we no longer see. */
static void mdts_refresh(lm_context_t ctxt)
{
        struct dirent *dirent;
        DIR *dir;
        struct rbt new_mdt_tree;

        rbt_init(&new_mdt_tree, string_comparator);

        /* Make sure we have mdt_data objects in the new_mdt_tree for
           each currently existing directory.  We can find the objects
           cached in the context mdt_tree (in which case we move them
           from mdt_tree to new_mdt_tree), or they can be newly allocated
           here. */

        dir = opendir(MDT_PATH);
        if (dir == NULL) {
                ovis_log(ctxt->log, OVIS_LDEBUG, "unable to open obdfilter dir %s\n",
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
                rbn = rbt_find(&ctxt->mdt_tree, dirent->d_name);
                if (rbn) {
                        mdt = container_of(rbn, struct mdt_data,
                                           mdt_tree_node);
                        rbt_del(&ctxt->mdt_tree, &mdt->mdt_tree_node);
                } else {
                        mdt = mdt_create(ctxt, dirent->d_name, MDT_PATH);
                }
                if (mdt == NULL)
                        continue;
                rbt_ins(&new_mdt_tree, &mdt->mdt_tree_node);
        }
        closedir(dir);

        /* destroy any mdts remaining in the context mdt_tree since we
           did not see their associated directories this time around */
        mdts_destroy(ctxt);

        /* copy the new_mdt_tree into place over the global mdt_tree */
        memcpy(&ctxt->mdt_tree, &new_mdt_tree, sizeof(struct rbt));

        return;
}

static void mdts_sample(lm_context_t ctxt)
{
        struct rbn *rbn;

        /* walk tree of known MDTs */
        RBT_FOREACH(rbn, &ctxt->mdt_tree) {
                struct mdt_data *mdt;
                mdt = container_of(rbn, struct mdt_data, mdt_tree_node);
                mdt_general_sample(ctxt, mdt->name, mdt->md_stats_path, mdt->osd_path,
                                   mdt->general_metric_set);
                mdt_job_stats_sample(ctxt,
				     mdt->fs_name, mdt->name,
                                     mdt->job_stats_path, &mdt->job_stats);
        }
}

static int config(ldmsd_plug_handle_t handle,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	lm_context_t ctxt = ldmsd_plug_ctxt_get(handle);

        ovis_log(ctxt->log, OVIS_LDEBUG, "config() called\n");
	char *ival = av_value(avl, "producer");
	if (ival) {
		if (strlen(ival) < sizeof(ctxt->producer_name)) {
			strncpy(ctxt->producer_name, ival, sizeof(ctxt->producer_name));
		} else {
                        ovis_log(ctxt->log, OVIS_LERROR, "config: producer name too long.\n");
                        return EINVAL;
		}
	}
	comp_id_helper_config(avl, &ctxt->cid);
        return 0;
}

static int sample(ldmsd_plug_handle_t handle)
{
	lm_context_t ctxt = ldmsd_plug_ctxt_get(handle);

        ovis_log(ctxt->log, OVIS_LDEBUG, "sample() called\n");
        if (mdt_general_schema_is_initialized() < 0) {
                if (mdt_general_schema_init(ctxt) < 0) {
                        ovis_log(ctxt->log, OVIS_LERROR, "general schema create failed\n");
                        return ENOMEM;
                }
        }
        if (mdt_job_stats_schema_is_initialized() < 0) {
                if (mdt_job_stats_schema_init(ctxt) < 0) {
                        ovis_log(ctxt->log, OVIS_LERROR, "job stats schema create failed\n");
                        return ENOMEM;
                }
        }

        mdts_refresh(ctxt);
        mdts_sample(ctxt);

        return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	ovis_log(ldmsd_plug_log_get(handle), OVIS_LDEBUG, "usage() called\n");
	return  "config name=lutre_mdt\n";
}

static int constructor(ldmsd_plug_handle_t handle)
{
	lm_context_t ctxt;

	ctxt = calloc(1, sizeof(*ctxt));
	if (ctxt == NULL) {
		ovis_log(ldmsd_plug_log_get(handle), OVIS_LERROR,
			 "Failed to allocate context\n");
		return ENOMEM;
	}
	ctxt->log = ldmsd_plug_log_get(handle);
	ctxt->plug_name = strdup(ldmsd_plug_name_get(handle));
	ctxt->cfg_name = strdup(ldmsd_plug_cfg_name_get(handle));
        gethostname(ctxt->producer_name, sizeof(ctxt->producer_name));
        rbt_init(&ctxt->mdt_tree, string_comparator);
	ldmsd_plug_ctxt_set(handle, ctxt);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	lm_context_t ctxt = ldmsd_plug_ctxt_get(handle);

	ovis_log(ctxt->log, OVIS_LDEBUG, "destructor() called\n");
	mdts_destroy(ctxt);
	mdt_general_schema_fini(ctxt);
	mdt_job_stats_schema_fini(ctxt);

	free(ctxt->cfg_name);
	free(ctxt->plug_name);
	free(ctxt);
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
