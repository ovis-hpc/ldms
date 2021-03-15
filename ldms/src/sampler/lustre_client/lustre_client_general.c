/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#include <stdio.h>
#include <stdint.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>

#include "ldms.h"
#include "ldmsd.h"
#include "lustre_client.h"
#include "lustre_client_general.h"
#include "jobid_helper.h"

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

int llite_general_schema_is_initialized()
{
        if (llite_general_schema != NULL)
                return 0;
        else
                return -1;
}

int llite_general_schema_init(const char *producer_name)
{
        ldms_schema_t sch;
        int rc;
        int i;

        log_fn(LDMSD_LDEBUG, SAMP" llite_general_schema_init()\n");
        sch = ldms_schema_new("lustre_client");
        if (sch == NULL)
                goto err1;
	rc = jobid_helper_schema_add(sch);
	if (rc <0)
		goto err2;
        rc = ldms_schema_meta_array_add(sch, "fs_name", LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0)
                goto err2;
        rc = ldms_schema_meta_array_add(sch, "llite", LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0)
                goto err2;
        /* add llite stats entries */
        for (i = 0; llite_stats_uint64_t_entries[i] != NULL; i++) {
                rc = ldms_schema_metric_add(sch, llite_stats_uint64_t_entries[i],
                                            LDMS_V_U64);
                if (rc < 0)
                        goto err2;
        }

        llite_general_schema = sch;

        return 0;
err2:
        ldms_schema_delete(sch);
err1:
        log_fn(LDMSD_LERROR, SAMP" lustre_llite_general schema creation failed\n");
        return -1;
}

void llite_general_schema_fini()
{
        log_fn(LDMSD_LDEBUG, SAMP" llite_general_schema_fini()\n");
        if (llite_general_schema != NULL) {
                ldms_schema_delete(llite_general_schema);
                llite_general_schema = NULL;
        }
}

void llite_general_destroy(ldms_set_t set)
{
        ldms_set_unpublish(set);
        ldms_set_delete(set);
}

/* must be schema created by llite_general_schema_create() */
ldms_set_t llite_general_create(const char *producer_name,
                                const char *fs_name,
                                const char *llite_name)
{
        ldms_set_t set;
        int index;
        char instance_name[256];

        log_fn(LDMSD_LDEBUG, SAMP" llite_general_create()\n");
        snprintf(instance_name, sizeof(instance_name), "%s/%s",
                 producer_name, llite_name);
        set = ldms_set_new(instance_name, llite_general_schema);
        ldms_set_producer_name_set(set, producer_name);
        index = ldms_metric_by_name(set, "fs_name");
        ldms_metric_array_set_str(set, index, fs_name);
        index = ldms_metric_by_name(set, "llite");
        ldms_metric_array_set_str(set, index, llite_name);
        ldms_set_publish(set);

        return set;
}

static void llite_stats_sample(const char *stats_path,
                                   ldms_set_t general_metric_set)
{
        FILE *sf;
        char buf[512];
        char str1[64+1];

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
	jobid_helper_metric_update(general_metric_set);
        while (fgets(buf, sizeof(buf), sf)) {
                uint64_t val1, val2;
                int rc;
                int index;

                rc = sscanf(buf, "%64s %lu samples [%*[^]]] %*u %*u %lu",
                            str1, &val1, &val2);
                if (rc == 2) {
                        index = ldms_metric_by_name(general_metric_set, str1);
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
                        index = ldms_metric_by_name(general_metric_set, str1);
                        if (index == -1) {
                                log_fn(LDMSD_LWARNING, SAMP" llite stats metric not found: %s\n",
                                       str1);
                        } else {
                                ldms_metric_set_u64(general_metric_set, index, val2);
                        }
                        continue;
                }
        }
        ldms_transaction_end(general_metric_set);
out1:
        fclose(sf);

        return;
}

void llite_general_sample(const char *llite_name, const char *stats_path,
                          ldms_set_t general_metric_set)
{
        log_fn(LDMSD_LDEBUG, SAMP" llite_general_sample() %s\n",
               llite_name);
        llite_stats_sample(stats_path, general_metric_set);
}
