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
#include "lustre_ost.h"
#include "lustre_ost_general.h"
#include "lustre_ost_job_stats.h"
#include "lustre_shared.h"

#define _GNU_SOURCE

#define OBDFILTER_PATH "/proc/fs/lustre/obdfilter"

static const char * const possible_osd_base_paths[] = {
	"/sys/fs/lustre", /* at least lustre >= 1.15 (probably >= 1.12) */
	"/proc/fs/lustre", /* older lustre */
	NULL
};

struct ost_data {
        char *fs_name;
        char *name;
        char *path;
        char *stats_path;
        char *job_stats_path;
        char *osd_path;
        ldms_set_t general_metric_set; /* a pointer */
        struct rbn ost_tree_node;
        struct rbt job_stats; /* key is jobid */
};

static int string_comparator(void *a, const void *b)
{
        return strcmp((char *)a, (char *)b);
}

static struct ost_data *ost_create(lo_context_t ctxt, const char *ost_name, const char *basedir)
{
        struct ost_data *ost;
        char path_tmp[PATH_MAX]; /* TODO: move large stack allocation to heap */
        char *state;

        ovis_log(ctxt->log, OVIS_LDEBUG, "ost_create() %s from %s\n",
               ost_name, basedir);
        ost = calloc(1, sizeof(*ost));
        if (ost == NULL)
                goto out1;
        ost->name = strdup(ost_name);
        if (ost->name == NULL)
                goto out2;
        snprintf(path_tmp, PATH_MAX, "%s/%s", basedir, ost_name);
        ost->path = strdup(path_tmp);
        if (ost->path == NULL)
                goto out3;
        snprintf(path_tmp, PATH_MAX, "%s/stats", ost->path);
        ost->stats_path = strdup(path_tmp);
        if (ost->stats_path == NULL)
                goto out4;
        snprintf(path_tmp, PATH_MAX, "%s/job_stats", ost->path);
        ost->job_stats_path = strdup(path_tmp);
        if (ost->job_stats_path == NULL)
                goto out5;
        ost->fs_name = strdup(ost_name);
        if (ost->fs_name == NULL)
                goto out6;
        if (strtok_r(ost->fs_name, "-", &state) == NULL) {
                ovis_log(ctxt->log, OVIS_LWARNING, "unable to parse filesystem name from \"%s\"\n",
                       ost->fs_name);
                goto out7;
        }
        ost->general_metric_set = ost_general_create(ctxt, ost->fs_name, ost->name);
        if (ost->general_metric_set == NULL)
                goto out7;
        ost->osd_path = lustre_osd_dir_find(possible_osd_base_paths,
					    ost->name,
					    ctxt->log);
        rbn_init(&ost->ost_tree_node, ost->name);
        rbt_init(&ost->job_stats, string_comparator);

        return ost;
out7:
        free(ost->fs_name);
out6:
        free(ost->job_stats_path);
out5:
        free(ost->stats_path);
out4:
        free(ost->path);
out3:
        free(ost->name);
out2:
        free(ost);
out1:
        return NULL;
}

static void ost_destroy(lo_context_t ctxt, struct ost_data *ost)
{
        ovis_log(ctxt->log, OVIS_LDEBUG, "ost_destroy() %s\n", ost->name);
        ost_general_destroy(ctxt, ost->general_metric_set);
        ost_job_stats_destroy(ctxt, &ost->job_stats);
        free(ost->osd_path);
        free(ost->fs_name);
        free(ost->job_stats_path);
        free(ost->stats_path);
        free(ost->path);
        free(ost->name);
        free(ost);
}

static void osts_destroy(lo_context_t ctxt)
{
        struct rbn *rbn;
        struct ost_data *ost;

        while (!rbt_empty(&ctxt->ost_tree)) {
                rbn = rbt_min(&ctxt->ost_tree);
                ost = container_of(rbn, struct ost_data,
                                   ost_tree_node);
                rbt_del(&ctxt->ost_tree, rbn);
                ost_destroy(ctxt, ost);
        }
}

/* List subdirectories in OBDFILTER_PATH to get list of
   OST names.  Create ost_data structures for any OSTS any that we
   have not seen, and delete any that we no longer see. */
static void osts_refresh(lo_context_t ctxt)
{
        struct dirent *dirent;
        DIR *dir;
        struct rbt new_ost_tree;

        rbt_init(&new_ost_tree, string_comparator);

        /* Make sure we have ost_data objects in the new_ost_tree for
           each currently existing directory.  We can find the objects
           cached in the context ost_tree (in which case we move them
           from ost_tree to new_ost_tree), or they can be newly allocated
           here. */

        dir = opendir(OBDFILTER_PATH);
        if (dir == NULL) {
                ovis_log(ctxt->log, OVIS_LDEBUG, "unable to open obdfilter dir %s\n",
                       OBDFILTER_PATH);
                return;
        }
        while ((dirent = readdir(dir)) != NULL) {
                struct rbn *rbn;
                struct ost_data *ost;

                if (dirent->d_type != DT_DIR ||
                    strcmp(dirent->d_name, ".") == 0 ||
                    strcmp(dirent->d_name, "..") == 0)
                        continue;
                rbn = rbt_find(&ctxt->ost_tree, dirent->d_name);
                if (rbn) {
                        ost = container_of(rbn, struct ost_data,
                                           ost_tree_node);
                        rbt_del(&ctxt->ost_tree, &ost->ost_tree_node);
                } else {
                        ost = ost_create(ctxt, dirent->d_name, OBDFILTER_PATH);
                }
                if (ost == NULL)
                        continue;
                rbt_ins(&new_ost_tree, &ost->ost_tree_node);
        }
        closedir(dir);

        /* destroy any osts remaining in the context ost_tree since we
           did not see their associated directories this time around */
        osts_destroy(ctxt);

        /* copy the new_ost_tree into place over the global ost_tree */
        memcpy(&ctxt->ost_tree, &new_ost_tree, sizeof(struct rbt));

        return;
}

static void osts_sample(lo_context_t ctxt)
{
        struct rbn *rbn;

        /* walk tree of known OSTs */
        RBT_FOREACH(rbn, &ctxt->ost_tree) {
                struct ost_data *ost;
                ost = container_of(rbn, struct ost_data, ost_tree_node);
                ost_general_sample(ctxt, ost->name, ost->stats_path, ost->osd_path,
                                   ost->general_metric_set);
                ost_job_stats_sample(ctxt, ost->fs_name, ost->name,
                                     ost->job_stats_path, &ost->job_stats);
        }
}

static int config(ldmsd_plug_handle_t handle,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	lo_context_t ctxt = ldmsd_plug_ctxt_get(handle);

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
	lo_context_t ctxt = ldmsd_plug_ctxt_get(handle);

	ovis_log(ctxt->log, OVIS_LDEBUG, "sample() called\n");
        if (ost_general_schema_is_initialized() < 0) {
                if (ost_general_schema_init(ctxt) < 0) {
                        ovis_log(ctxt->log, OVIS_LERROR, "general schema create failed\n");
                        return ENOMEM;
                }
        }
        if (ost_job_stats_schema_is_initialized() < 0) {
                if (ost_job_stats_schema_init(ctxt) < 0) {
                        ovis_log(ctxt->log, OVIS_LERROR, "job stats schema create failed\n");
                        return ENOMEM;
                }
        }

        osts_refresh(ctxt);
        osts_sample(ctxt);

        return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
        ovis_log(ldmsd_plug_log_get(handle), OVIS_LDEBUG, "usage() called\n");
	return  "config name=lustre_ost\n";
}

static int constructor(ldmsd_plug_handle_t handle)
{
	lo_context_t ctxt;

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
	rbt_init(&ctxt->ost_tree, string_comparator);
	ldmsd_plug_ctxt_set(handle, ctxt);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	lo_context_t ctxt = ldmsd_plug_ctxt_get(handle);

	ovis_log(ctxt->log, OVIS_LDEBUG, "term() called\n");
	osts_destroy(ctxt);
	ost_general_schema_fini(ctxt);
	ost_job_stats_schema_fini(ctxt);

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
