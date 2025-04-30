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

/* locations where llite stats might be found */
static const char * const llite_paths[] = {
        "/proc/fs/lustre/llite",          /* lustre pre-2.12 */
        "/sys/kernel/debug/lustre/llite"  /* lustre 2.12 and later */
};
static const int llite_paths_len = sizeof(llite_paths) / sizeof(llite_paths[0]);
static char *test_path = NULL;
static int schema_extras = 0;

static struct comp_id_data cid;

ldmsd_msg_log_f log_fn;
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

        log_fn(LDMSD_LDEBUG, SAMP" llite_create() %s from %s\n",
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
                log_fn(LDMSD_LWARNING, SAMP" unable to parse filesystem name from \"%s\"\n",
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
        log_fn(LDMSD_LDEBUG, SAMP" llite_destroy() %s\n", llite->name);
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
	if (test_path) {
		if  (stat(test_path, &sb) == -1 || !S_ISDIR(sb.st_mode)) {
                        log_fn(LDMSD_LERROR, SAMP
				" find_llite_path() test_path given "
				"is not a directory: %s\n", test_path);
			return NULL;
		}
		return test_path;
	}

        for (i = 0; i < llite_paths_len; i++) {
                if (stat(llite_paths[i], &sb) == -1 || !S_ISDIR(sb.st_mode))
                        continue;
                if (previously_found_path != llite_paths[i]) {
                        /* just for logging purposes */
                        previously_found_path = llite_paths[i];
                        log_fn(LDMSD_LDEBUG, SAMP" find_llite_path() found %s\n",
                               llite_paths[i]);
                }
                return llite_paths[i];
        }

        log_fn(LDMSD_LWARNING, SAMP" no llite directories found\n");
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
	                log_fn(LDMSD_LDEBUG, SAMP" unable to open llite dir %s\n",
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

static int config(struct ldmsd_plugin *self,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        log_fn(LDMSD_LDEBUG, SAMP" config() called\n");
	test_path = av_value(avl, "test_path");
	if (test_path) {
		test_path = strdup(test_path);
		log_fn(LDMSD_LDEBUG, SAMP": test_path=%s\n",test_path);
	}

	schema_extras = 0;
	char *extra215 = av_value(avl, "extra215");
	if (extra215) {
		schema_extras |= EXTRA215;
		log_fn(LDMSD_LDEBUG, SAMP": schema with extra 2.15 enabled\n");
	}
	char *extratime = av_value(avl, "extratimes");
	if (extratime) {
		schema_extras |= EXTRATIMES;
		log_fn(LDMSD_LDEBUG, SAMP": schema with start_time enabled\n");
	}

	char *ival = av_value(avl, "producer");
	if (ival) {
		if (strlen(ival) < sizeof(producer_name)) {
			strncpy(producer_name, ival, sizeof(producer_name));
		} else {
                        log_fn(LDMSD_LERROR, SAMP": config: producer name too long.\n");
                        return EINVAL;
		}
	}
	(void)base_auth_parse(avl, &auth, log_fn);
	int jc = jobid_helper_config(avl);
        if (jc) {
		log_fn(LDMSD_LERROR, SAMP": set name for job_set="
			" is too long.\n");
		return jc;
	}
	int cc = comp_id_helper_config(avl, &cid);
        if (cc) {
		log_fn(LDMSD_LERROR, SAMP": value of component_id="
			" is invalid.\n");
		return cc;
	}
        return 0;
}

static int sample(struct ldmsd_sampler *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" sample() called\n");
        if (llite_general_schema_is_initialized() < 0) {
                if (llite_general_schema_init(&cid, schema_extras) < 0) {
                        log_fn(LDMSD_LERROR, SAMP" general schema create failed\n");
                        return ENOMEM;
                }
        }

        int err = llites_refresh();  /* running out of set memory is an error */
        llites_sample();

        return err;
}

static void term(struct ldmsd_plugin *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" term() called\n");
        llites_destroy();
        llite_general_schema_fini();
	free(test_path);
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

static struct ldmsd_sampler llite_plugin = {
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
        rbt_init(&llite_tree, string_comparator);
        gethostname(producer_name, sizeof(producer_name));

        return &llite_plugin.base;
}
