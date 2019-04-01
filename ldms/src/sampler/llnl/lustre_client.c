/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory. Written by
 * Christopher J. Morrone <morrone2@llnl.gov>
 * All rights reserved.
 *
 * This work was performed under the auspices of the U.S. Department of
 * Energy by Lawrence Livermore National Laboratory under
 * Contract DE-AC52-07NA27344.
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
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <dirent.h>
#include <coll/rbt.h>
#include <coll/idx.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_jobid.h"

#define _GNU_SOURCE

#define SAMP "lustre_client"
#define LLITE_PATH "/proc/fs/lustre/llite"
#define DEFAULT_SCHEMA_NAME "lustre_client"
/* from lustre code */
#define MAX_OBD_NAME 128
#define LUSTRE_MAXFSNAME 8

static ldmsd_msg_log_f log_fn;
static char producer_name[LDMS_PRODUCER_NAME_MAX+1];
static char schema_name[LDMS_SET_NAME_MAX+1];
static char host_name[HOST_NAME_MAX+1];
static uint64_t component_id;
static time_t last_refresh;
static time_t refresh_interval = 30;
static int job_id_index;
LJI_GLOBALS;

/* red-black tree root for llites */
static struct rbt llite_tree;

struct llite_data {
        char *fs_name;
        char *name;
        char *path;
        char *stats_path;
        ldms_set_t general_metric_set; /* a pointer */
        struct rbn llite_tree_node;
};

static ldms_schema_t llite_general_schema;

static char *llite_stats_uint64_t_entries[] = {
        "dirty_pages_hits",
        "dirty_pages_misses",
        "read_bytes.sum",
        "write_bytes.sum",
        "brw_read.sum",
        "brw_write.sum",
        "ioctl",
        "open",
        "close",
        "mmap",
        "page_fault",
        "page_mkwrite",
        "seek",
        "fsync",
        "readdir",
        "setattr",
        "truncate",
        "flock",
        "getattr",
        "create",
        "link",
        "unlink",
        "symlink",
        "mkdir",
        "rmdir",
        "mknod",
        "rename",
        "statfs",
        "alloc_inode",
        "setxattr",
        "getxattr",
        "getxattr_hits",
        "listxattr",
        "removexattr",
        "inode_permission",
        NULL
};

/*******************************************************************/
/* ldms_metrict_by_name() employs a linear lookup and is verboten for
 * use by plugins in the main sample() loop.  This section section
 * implemants a faster lookup using the API from idx.[ch], and offers
 * alt_ldms_metric_by_name() as a limited alternative to the original.
 * Someday, perhaps ldms_metric_by_name() will be improved and this
 * section can be removed.
 */
static idx_t metric_idx;

static int metric_idx_add(const char *name, int index)
{
        int *val;
        int rc;

        val = malloc(sizeof(val));
        if (val == NULL) {
                return ENOMEM;
        }

        *val = index;
        rc = idx_add(metric_idx, (idx_key_t)name, strlen(name), val);

        return rc;
}

static void metric_idx_create()
{
        metric_idx = idx_create();
}

static void metric_idx_obj_free(void *obj, void *cb_arg)
{
        free(obj);
}

static void metric_idx_destroy()
{
        idx_traverse(metric_idx, metric_idx_obj_free, NULL);
        idx_destroy(metric_idx);
}

/* "set" is unused in this function.  It is just there to allow this
   call to be a direct replacement for ldms_metric_by_name().  This plugin
   only uses one schema, so there is only one global lookup index. */ 
static int alt_ldms_metric_by_name(ldms_set_t set, const char *name)
{
        int *val;

        val = idx_find(metric_idx, (idx_key_t)name, strlen(name));

        return *val;
}

/*******************************************************************/

static int llite_general_schema_is_initialized()
{
        if (llite_general_schema != NULL)
                return 0;
        else
                return -1;
}

static int llite_general_schema_init(const char *schema_name)
{
        ldms_schema_t sch;
        int rc;
        int i;

        log_fn(LDMSD_LDEBUG, SAMP" llite_general_schema_init()\n");
        sch = ldms_schema_new(schema_name);
        if (sch == NULL)
                goto err1;
        rc = ldms_schema_meta_array_add(sch, "hostname", LDMS_V_CHAR_ARRAY, HOST_NAME_MAX+1);
        if (rc < 0)
                goto err2;
        rc = ldms_schema_meta_array_add(sch, "fs_name", LDMS_V_CHAR_ARRAY, LUSTRE_MAXFSNAME+1);
        if (rc < 0)
                goto err2;
        rc = ldms_schema_meta_array_add(sch, "llite", LDMS_V_CHAR_ARRAY, MAX_OBD_NAME);
        if (rc < 0)
                goto err2;
	rc = ldms_schema_meta_add(sch, "component_id", LDMS_V_U64);
        if (rc < 0)
                goto err2;
	job_id_index = LJI_ADD_JOBID(sch);
	if (job_id_index < 0) {
		goto err2;
	}
        /* add llite stats entries */
        for (i = 0; llite_stats_uint64_t_entries[i] != NULL; i++) {
                rc = ldms_schema_metric_add(sch, llite_stats_uint64_t_entries[i],
                                            LDMS_V_U64);
                if (rc < 0)
                        goto err2;
                rc = metric_idx_add(llite_stats_uint64_t_entries[i], rc);
                if (rc != 0) {
                        goto err2;
                }
        }

        llite_general_schema = sch;

        return 0;
err2:
        ldms_schema_delete(sch);
err1:
        log_fn(LDMSD_LERROR, SAMP" lustre_client schema creation failed\n");
        return -1;
}

static void llite_general_schema_fini()
{
        log_fn(LDMSD_LDEBUG, SAMP" llite_general_schema_fini()\n");
        if (llite_general_schema != NULL) {
                ldms_schema_delete(llite_general_schema);
                llite_general_schema = NULL;
                metric_idx_destroy();
        }
}

static void llite_general_destroy(ldms_set_t set)
{
        /* ldms_set_unpublish(set); */
        ldms_set_delete(set);
}

/* must be schema created by llite_general_schema_create() */
static ldms_set_t llite_general_create(const char *host_name, const char *producer_name,
                                       const char *fs_name, const char *llite_name,
                                       uint64_t component_id)
{
        ldms_set_t set;
        int index;
        char instance_name[256];

        log_fn(LDMSD_LDEBUG, SAMP" llite_general_create()\n");
        snprintf(instance_name, sizeof(instance_name), "%s/%s",
                 producer_name, llite_name);
        set = ldms_set_new(instance_name, llite_general_schema);
        ldms_set_producer_name_set(set, producer_name);
        index = ldms_metric_by_name(set, "hostname");
        ldms_metric_array_set_str(set, index, host_name);
        index = ldms_metric_by_name(set, "fs_name");
        ldms_metric_array_set_str(set, index, fs_name);
        index = ldms_metric_by_name(set, "llite");
        ldms_metric_array_set_str(set, index, llite_name);
        index = ldms_metric_by_name(set, "component_id");
        ldms_metric_set_u64(set, index, component_id);
        /* ldms_set_publish(set); */

        return set;
}

static void llite_stats_sample(const char *stats_path,
                                   ldms_set_t general_metric_set)
{
        FILE *sf;
        char buf[512];
        int index;

        sf = fopen(stats_path, "r");
        if (sf == NULL) {
                log_fn(LDMSD_LWARNING, SAMP" file %s not found\n",
                       stats_path);
                return;
        }

        /* The first line should always be "snapshot_time"
           we will ignore it because it always contains the time that we read
           from the file, not any information about when the stats last
           changed */
        if (fgets(buf, sizeof(buf), sf) == NULL) {
                log_fn(LDMSD_LWARNING, SAMP" failed on read from %s\n",
                       stats_path);
                goto out1;
        }
        if (strncmp("snapshot_time", buf, sizeof("snapshot_time")-1) != 0) {
                log_fn(LDMSD_LWARNING, SAMP" first line in %s is not \"snapshot_time\": %s\n",
                       stats_path, buf);
                goto out1;
        }

        ldms_transaction_begin(general_metric_set);
        while (fgets(buf, sizeof(buf), sf)) {
                uint64_t val1, val2;
                int rc;
                char str1[64+1];

                rc = sscanf(buf, "%64s %lu samples [%*[^]]] %*u %*u %lu",
                            str1, &val1, &val2);
                if (rc == 2) {
                        index = alt_ldms_metric_by_name(general_metric_set, str1);
                        if (index == -1) {
                                log_fn(LDMSD_LWARNING, SAMP" llite stats metric not found: %s\n",
                                       str1);
                        } else {
                                ldms_metric_set_u64(general_metric_set, index, val1);
                        }
                        continue;
                } else if (rc == 3) {
                        int base_name_len = strlen(str1);
                        sprintf(str1+base_name_len, ".sum"); /* append ".sum" */
                        index = alt_ldms_metric_by_name(general_metric_set, str1);
                        if (index == -1) {
                                log_fn(LDMSD_LWARNING, SAMP" llite stats metric not found: %s\n",
                                       str1);
                        } else {
                                ldms_metric_set_u64(general_metric_set, index, val2);
                        }
                        continue;
                }
        }

	LJI_SAMPLE(general_metric_set, job_id_index);

        ldms_transaction_end(general_metric_set);
out1:
        fclose(sf);

        return;
}

static void llite_general_sample(const char *llite_name, const char *stats_path,
                                 ldms_set_t general_metric_set)
{
        log_fn(LDMSD_LDEBUG, SAMP" llite_general_sample() %s\n",
               llite_name);
        llite_stats_sample(stats_path, general_metric_set);
}

static int string_comparator(void *a, const void *b)
{
        return strcmp((char *)a, (char *)b);
}

static struct llite_data *llite_create(const char *llite_name, const char *basedir)
{
        struct llite_data *llite;
        char path_tmp[PATH_MAX]; /* TODO: move large stack allocation to heap */
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
        llite->general_metric_set = llite_general_create(host_name, producer_name,
                                                         llite->fs_name, llite->name,
                                                         component_id);
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

/* List subdirectories in LLITE_PATH to get list of
   LLITE names.  Create llite_data structures for any LLITES that we
   have not seen, and delete any that we no longer see. */
static void llites_refresh()
{
        static bool already_missing = false;
        struct dirent *dirent;
        DIR *dir;
        struct rbt new_llite_tree;

        log_fn(LDMSD_LDEBUG, SAMP" rescanning llite directory\n");
        rbt_init(&new_llite_tree, string_comparator);

        /* Make sure we have llite_data objects in the new_llite_tree for
           each currently existing directory.  We can find the objects
           cached in the global llite_tree (in which case we move them
           from llite_tree to new_llite_tree), or they can be newly allocated
           here. */

        dir = opendir(LLITE_PATH);
        if (dir == NULL) {
                if (!already_missing) {
                        log_fn(LDMSD_LWARNING, SAMP" unable to open llite dir %s\n",
                               LLITE_PATH);
                        already_missing = true;
                }
                return;
        } else {
                if (already_missing) {
                        log_fn(LDMSD_LWARNING, SAMP" llite dir %s is now accesible\n",
                               LLITE_PATH);
                        already_missing = false;
                }
        }

        while ((dirent = readdir(dir)) != NULL) {
                struct rbn *rbn;
                struct llite_data *llite;

                if (dirent->d_type != DT_DIR ||
                    strcmp(dirent->d_name, ".") == 0 ||
                    strcmp(dirent->d_name, "..") == 0)
                        continue;
                rbn = rbt_find(&llite_tree, dirent->d_name);
                if (rbn) {
                        llite = container_of(rbn, struct llite_data,
                                           llite_tree_node);
                        rbt_del(&llite_tree, &llite->llite_tree_node);
                } else {
                        llite = llite_create(dirent->d_name, LLITE_PATH);
                }
                if (llite == NULL)
                        continue;
                rbt_ins(&new_llite_tree, &llite->llite_tree_node);
        }
        closedir(dir);

        /* destroy any llites remaining in the global llite_tree since we
           did not see their associated directories this time around */
        /* llites_destroy();*/
        /* NOTE: ldms_set_delete() doesn't really work until some time post v4.2.
           So instead of llites_destroy(), we'll just keep old data around forever
           in this version of the code.  When ldms_set delete() works we can
           delete the following while() loop and uncomment the llites_destroy()
           call above. */
        while (!rbt_empty(&llite_tree)) {
                struct rbn *rbn;
                rbn = rbt_min(&llite_tree);
                rbt_del(&llite_tree, rbn);
                rbt_ins(&new_llite_tree, rbn);
        }

        /* copy the new_llite_tree into place over the global llite_tree */
        memcpy(&llite_tree, &new_llite_tree, sizeof(struct rbt));

        return;
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

static int sample(struct ldmsd_sampler *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" sample() called\n");
        if (llite_general_schema_is_initialized() < 0) {
                if (llite_general_schema_init(schema_name) < 0) {
                        log_fn(LDMSD_LERROR, SAMP" general schema create failed\n");
                        return ENOMEM;
                }
        }

        if ((time(NULL)-last_refresh) >= refresh_interval) {
                last_refresh = time(NULL);
                llites_refresh();
        }

        llites_sample();

        return 0;
}

static void term(struct ldmsd_plugin *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" term() called\n");
        llites_destroy();
        llite_general_schema_fini();
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static const char *usage(struct ldmsd_plugin *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" usage() called\n");
	return  "config name=" SAMP " [producer=<prod_name>] [instance=<inst_name>] [component_id=<compid>] [schema=<sname>] [rescan_sec=<rsec>] [with_jobid=<jid>]\n"
		"    <prod_name>  The producer name (default: the hostname)\n"
		"    <inst_name>  The instance name (ignored, dynamically generated)\n"
		"    <compid>     Optional unique number identifier (default: zero)\n"
		"    <sname>      Optional schema name\n"
                "    <rsec>       Interval in seconds between rescanning for lustre client mounts (default: 30s)\n"
		LJI_DESC;
	return  "config name=" SAMP;
}

static int config(struct ldmsd_plugin *self,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        char *value;

        log_fn(LDMSD_LDEBUG, SAMP" config() called\n");

        value = av_value(avl, "producer");
        if (value != NULL) {
                snprintf(producer_name, sizeof(producer_name),
                         "%s", value);
        }

	value = av_value(avl, "component_id");
	if (value != NULL) {
		component_id = (uint64_t)atoi(value);
        }

	value = av_value(avl, "instance");
	if (value != NULL) {
		log_fn(LDMSD_LWARNING, SAMP ": ignoring option \"instance=%s\"\n",
                       value);
	}

	value = av_value(avl, "schema");
	if (value != NULL) {
                snprintf(schema_name, sizeof(schema_name), "%s", value);
	}

	value = av_value(avl, "rescan_sec");
	if (value != NULL) {
		refresh_interval = (time_t)atoi(value);
        }

        LJI_CONFIG(value, avl);

        return 0;
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
        log_fn(LDMSD_LDEBUG, SAMP" get_plugin() called\n");
        rbt_init(&llite_tree, string_comparator);
        metric_idx_create();
        gethostname(host_name, sizeof(host_name));
        /* set the default producer name */
        strcpy(producer_name, host_name);
        /* set the default schema name */
        snprintf(schema_name, sizeof(schema_name), "%s", DEFAULT_SCHEMA_NAME);


        return &llite_plugin.base;
}
