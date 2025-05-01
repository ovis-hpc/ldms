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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ldms.h"
#include "ldmsd.h"
#include "config.h"
#include "lustre_client.h"
#include "lustre_client_general.h"
#include "jobid_helper.h"

#define _GNU_SOURCE

ovis_log_t lustre_client_log;

/* locations where llite stats might be found */
static const char * const llite_paths[] = {
        "/proc/fs/lustre/llite",          /* lustre pre-2.12 */
        "/sys/kernel/debug/lustre/llite"  /* lustre 2.12 and later */
};
static const int llite_paths_len = sizeof(llite_paths) / sizeof(llite_paths[0]);

static struct comp_id_data cid;

char producer_name[LDMS_PRODUCER_NAME_MAX];

/* red-black tree root for llites */
static struct rbt llite_tree;

static struct base_auth auth;

struct llite_data {
        char *fs_name;
        char *name;
        char *path;
        char *stats_path;
        ldms_set_t general_metric_set; /* a pointer */
        struct rbn llite_tree_node;
};

static int string_comparator(void *a, const void *b)
{
        return strcmp((char *)a, (char *)b);
}

static struct llite_data *llite_create(const char *llite_name, const char *basedir)
{
        struct llite_data *llite;
        char path_tmp[PATH_MAX];
        char *state;

        ovis_log(lustre_client_log, OVIS_LDEBUG, " llite_create() %s from %s\n",
               llite_name, basedir);
        llite = calloc(1, sizeof(*llite));
        if (llite == NULL)
                goto out1;
        llite->name = strdup(llite_name);
        if (llite->name == NULL)
                goto out2;
        snprintf(path_tmp, PATH_MAX, "%s/%s", basedir, llite_name);
        llite->path = strdup(path_tmp);
        if (llite->path == NULL)
                goto out3;
        snprintf(path_tmp, PATH_MAX, "%s/stats", llite->path);
        llite->stats_path = strdup(path_tmp);
        if (llite->stats_path == NULL)
                goto out4;
        llite->fs_name = strdup(llite_name);
        if (llite->fs_name == NULL)
                goto out5;
        if (strtok_r(llite->fs_name, "-", &state) == NULL) {
                ovis_log(lustre_client_log, OVIS_LWARNING, "unable to parse filesystem name from \"%s\"\n",
                       llite->fs_name);
                goto out6;
        }
        llite->general_metric_set = llite_general_create(producer_name,
                                                         llite->fs_name,
                                                         llite->name,
                                                         &cid, &auth);
        if (llite->general_metric_set == NULL)
                goto out6;
        rbn_init(&llite->llite_tree_node, llite->name);

        return llite;
out6:
        free(llite->fs_name);
out5:
        free(llite->stats_path);
out4:
        free(llite->path);
out3:
        free(llite->name);
out2:
        free(llite);
out1:
        return NULL;
}

static void llite_destroy(struct llite_data *llite)
{
        ovis_log(lustre_client_log, OVIS_LDEBUG, "llite_destroy() %s\n", llite->name);
        llite_general_destroy(llite->general_metric_set);
        free(llite->fs_name);
        free(llite->stats_path);
        free(llite->path);
        free(llite->name);
        free(llite);
}

static void llites_destroy()
{
        struct rbn *rbn;
        struct llite_data *llite;

        while (!rbt_empty(&llite_tree)) {
                rbn = rbt_min(&llite_tree);
                llite = container_of(rbn, struct llite_data,
                                   llite_tree_node);
                rbt_del(&llite_tree, rbn);
                llite_destroy(llite);
        }
}

/* Different versions of Lustre put the llite client stats in different place.
   Returns a pointer to a path, or NULL if no llite directory found anywhere.
 */
static const char *const find_llite_path()
{
        static const char *previously_found_path = NULL;
        struct stat sb;
        int i;

        for (i = 0; i < llite_paths_len; i++) {
                if (stat(llite_paths[i], &sb) == -1 || !S_ISDIR(sb.st_mode))
                        continue;
                if (previously_found_path != llite_paths[i]) {
                        /* just for logging purposes */
                        previously_found_path = llite_paths[i];
                        ovis_log(lustre_client_log, OVIS_LDEBUG, "find_llite_path() found %s\n",
                               llite_paths[i]);
                }
                return llite_paths[i];
        }

        ovis_log(lustre_client_log, OVIS_LWARNING, "no llite directories found\n");
        return NULL;
}

static int dir_once_log;
/* List subdirectories in llite_path to get list of
   LLITE names.  Create llite_data structures for any LLITEs that we
   have not seen, and delete any that we no longer see. */
static int llites_refresh()
{
        const char *llite_path;
        struct dirent *dirent;
        DIR *dir;
        struct rbt new_llite_tree;
	int err = 0;

        llite_path = find_llite_path();
        if (llite_path == NULL) {
                return 0;
        }

        rbt_init(&new_llite_tree, string_comparator);

        /* Make sure we have llite_data objects in the new_llite_tree for
           each currently existing directory.  We can find the objects
           cached in the global llite_tree (in which case we move them
           from llite_tree to new_llite_tree), or they can be newly allocated
           here. */
        dir = opendir(llite_path);
        if (dir == NULL) {
		if (!dir_once_log) {
	                ovis_log(lustre_client_log, OVIS_LDEBUG, "unable to open llite dir %s\n",
	                       llite_path);
			dir_once_log = 1;
		}
                return 0;
        }
	dir_once_log = 0;
        while ((dirent = readdir(dir)) != NULL) {
                struct rbn *rbn;
                struct llite_data *llite;

                if (dirent->d_type != DT_DIR ||
                    strcmp(dirent->d_name, ".") == 0 ||
                    strcmp(dirent->d_name, "..") == 0)
                        continue;
                rbn = rbt_find(&llite_tree, dirent->d_name);
		errno = 0;
                if (rbn) {
                        llite = container_of(rbn, struct llite_data,
                                           llite_tree_node);
                        rbt_del(&llite_tree, &llite->llite_tree_node);
                } else {
                        llite = llite_create(dirent->d_name, llite_path);
                }
                if (llite == NULL) {
			err = errno;
                        continue;
		}
                rbt_ins(&new_llite_tree, &llite->llite_tree_node);
        }
        closedir(dir);

        /* destroy any llites remaining in the global llite_tree since we
           did not see their associated directories this time around */
        llites_destroy();

        /* copy the new_llite_tree into place over the global llite_tree */
        memcpy(&llite_tree, &new_llite_tree, sizeof(struct rbt));

        return err;
}

static void llites_sample()
{
        struct rbn *rbn;

        /* walk tree of known LLITEs */
        RBT_FOREACH(rbn, &llite_tree) {
                struct llite_data *llite;
                llite = container_of(rbn, struct llite_data, llite_tree_node);
                llite_general_sample(llite->name, llite->stats_path,
                                   llite->general_metric_set);
        }
}

static int config(ldmsd_plug_handle_t handle,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        ovis_log(lustre_client_log, OVIS_LDEBUG, "config() called\n");
	char *ival = av_value(avl, "producer");
	if (ival) {
		if (strlen(ival) < sizeof(producer_name)) {
			strncpy(producer_name, ival, sizeof(producer_name));
		} else {
                        ovis_log(lustre_client_log, OVIS_LERROR, SAMP": config: producer name too long.\n");
                        return EINVAL;
		}
	}
	(void)base_auth_parse(avl, &auth, lustre_client_log);
	int jc = jobid_helper_config(avl);
        if (jc) {
		ovis_log(lustre_client_log, OVIS_LERROR, SAMP": set name for job_set="
			" is too long.\n");
		return jc;
	}
	int cc = comp_id_helper_config(avl, &cid);
        if (cc) {
		ovis_log(lustre_client_log, OVIS_LERROR, SAMP": value of component_id="
			" is invalid.\n");
		return cc;
	}
        return 0;
}

static int sample(ldmsd_plug_handle_t handle)
{
        ovis_log(lustre_client_log, OVIS_LDEBUG, "sample() called\n");
        if (llite_general_schema_is_initialized() < 0) {
                if (llite_general_schema_init(&cid) < 0) {
                        ovis_log(lustre_client_log, OVIS_LERROR, "general schema create failed\n");
                        return ENOMEM;
                }
        }

        int err = llites_refresh();  /* running out of set memory is an error */
        llites_sample();

        return err;
}

static void term(ldmsd_plug_handle_t handle)
{
	ovis_log(lustre_client_log, OVIS_LDEBUG, "term() called\n");
	llites_destroy();
	llite_general_schema_fini();
}

static const char *usage(ldmsd_plug_handle_t handle)
{
        ovis_log(lustre_client_log, OVIS_LDEBUG, "usage() called\n");
	return  "config name=" SAMP;
}

static int constructor(ldmsd_plug_handle_t handle)
{
        lustre_client_log = ldmsd_plug_log_get(handle);
	rbt_init(&llite_tree, string_comparator);
	gethostname(producer_name, sizeof(producer_name));

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
