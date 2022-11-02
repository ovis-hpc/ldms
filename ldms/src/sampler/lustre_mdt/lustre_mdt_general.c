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
#include "lustre_mdt.h"
#include "lustre_mdt_general.h"

static ldms_schema_t mdt_general_schema;

/* Defined in lustre_mdt.c */
extern ovis_log_t lustre_mdt_log;

static char *mdt_md_stats_uint64_t_entries[] = {
        "open",
        "close",
        "close.sum",
        "mknod",
        "mknod.sum",
        "link",
        "unlink",
        "mkdir",
        "rmdir",
        "rename",
        "getattr",
        "setattr",
        "getxattr",
        "setxattr",
        "statfs",
        "sync",
        "samedir_rename",
        "crossdir_rename",
        NULL
};

static char *osd_uint64_t_fields[] = {
        "filesfree",
        "filestotal",
        "kbytesavail",
        "kbytesfree",
        "kbytestotal",
        NULL
};

int mdt_general_schema_is_initialized()
{
        if (mdt_general_schema != NULL)
                return 0;
        else
                return -1;
}

int mdt_general_schema_init(comp_id_t cid)
{
        ldms_schema_t sch;
        int rc;
        int i;

        ovis_log(lustre_mdt_log, OVIS_LDEBUG, "mdt_general_schema_init()\n");
        sch = ldms_schema_new("lustre_mdt");
        if (sch == NULL)
                goto err1;
	const char *field;
	field = "component_id";
	rc = comp_id_helper_schema_add(sch, cid);
	if (rc) {
		rc = -rc;
		goto err2;
	}
        rc = ldms_schema_meta_array_add(sch, "fs_name", LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0)
                goto err2;
        rc = ldms_schema_meta_array_add(sch, "mdt", LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0)
                goto err2;
        /* add mdt md_stats entries */
        for (i = 0; mdt_md_stats_uint64_t_entries[i] != NULL; i++) {
                rc = ldms_schema_metric_add(sch, mdt_md_stats_uint64_t_entries[i],
                                            LDMS_V_U64);
                if (rc < 0)
                        goto err2;
        }
        /* add osd entries */
        for (i = 0; osd_uint64_t_fields[i] != NULL; i++) {
                rc = ldms_schema_metric_add(sch, osd_uint64_t_fields[i],
                                            LDMS_V_U64);
                if (rc < 0)
                        goto err2;
        }

        mdt_general_schema = sch;

        return 0;
err2:
	ovis_log(lustre_mdt_log, OVIS_LERROR, "lustre_mdt_general schema creation failed to add %s. (%s)\n",
		field, STRERROR(-rc));
        ldms_schema_delete(sch);
err1:
        ovis_log(lustre_mdt_log, OVIS_LERROR, "lustre_mdt_general schema creation failed\n");
        return -1;
}

void mdt_general_schema_fini()
{
        ovis_log(lustre_mdt_log, OVIS_LDEBUG, "mdt_general_schema_fini()\n");
        if (mdt_general_schema != NULL) {
                ldms_schema_delete(mdt_general_schema);
                mdt_general_schema = NULL;
        }
}

/* Returns strdup'ed string or NULL.  Caller must free. */
char *mdt_general_osd_path_find(const char *search_path, const char *mdt_name)
{
        struct dirent *dirent;
        DIR *dir;
        char *osd_path = NULL;

        dir = opendir(search_path);
        if (dir == NULL) {
                return NULL;
        }

        while ((dirent = readdir(dir)) != NULL) {
                if (dirent->d_type == DT_DIR &&
                    strncmp(dirent->d_name, "osd-", strlen("osd-")) == 0) {
                        char tmp_path[PATH_MAX];
                        snprintf(tmp_path, PATH_MAX, "%s/%s/%s",
                                 search_path, dirent->d_name, mdt_name);
                        if (access(tmp_path, F_OK) == 0) {
                                osd_path = strdup(tmp_path);
                                break;
                        }
                }
        }

        closedir(dir);

        if (osd_path != NULL) {
                ovis_log(lustre_mdt_log, OVIS_LDEBUG, "for mdt %s found osd path %s\n",
                       mdt_name, osd_path);
        } else {
                ovis_log(lustre_mdt_log, OVIS_LWARNING, "osd for mdt %s not found\n",
                       mdt_name);
        }

        return osd_path;
}

static uint64_t file_read_uint64_t(const char *dir, const char *file)
{
        uint64_t val;
        char filepath[PATH_MAX];
        char valbuf[64];
        FILE *fp;

        snprintf(filepath, PATH_MAX, "%s/%s", dir, file);
        fp = fopen(filepath, "r");
        if (fp == NULL) {
                ovis_log(lustre_mdt_log, OVIS_LWARNING, "unable to open %s\n", filepath);
                return 0;
        }
        if (fgets(valbuf, sizeof(valbuf), fp) == NULL) {
                ovis_log(lustre_mdt_log, OVIS_LWARNING, "unable to read %s\n", filepath);
                fclose(fp);
                return 0;
        }
        fclose(fp);

        /* turn string into int */
        sscanf(valbuf, "%lu", &val);

        return val;
}

static void osd_sample(const char *osd_path, ldms_set_t general_metric_set)
{
        char *field;
        uint64_t val;
        int index;
        int i;

        for (i = 0; (field = osd_uint64_t_fields[i]) != NULL; i++) {
                val = file_read_uint64_t(osd_path, field);
                index = ldms_metric_by_name(general_metric_set, field);
                ldms_metric_set_u64(general_metric_set, index, val);
         }
}


void mdt_general_destroy(ldms_set_t set)
{
        ldmsd_set_deregister(ldms_set_instance_name_get(set), SAMP);
        ldms_set_unpublish(set);
        ldms_set_delete(set);
}


/* must be schema created by mdt_general_schema_create() */
ldms_set_t mdt_general_create(const char *producer_name,
			      const char *fs_name,
			      const char *mdt_name,
			      const comp_id_t cid)
{
        ldms_set_t set;
        int index;
        char instance_name[LDMS_PRODUCER_NAME_MAX+64];

        ovis_log(lustre_mdt_log, OVIS_LDEBUG, "mdt_general_create()\n");
        snprintf(instance_name, sizeof(instance_name), "%s/%s",
                 producer_name, mdt_name);
        set = ldms_set_new(instance_name, mdt_general_schema);
        ldms_set_producer_name_set(set, producer_name);
        index = ldms_metric_by_name(set, "fs_name");
        ldms_metric_array_set_str(set, index, fs_name);
        index = ldms_metric_by_name(set, "mdt");
        ldms_metric_array_set_str(set, index, mdt_name);
	comp_id_helper_metric_update(set, cid);
        ldms_set_publish(set);
	ldmsd_set_register(set, SAMP);
        return set;
}

static void mdt_md_stats_sample(const char *stats_path,
                                   ldms_set_t general_metric_set)
{
        FILE *sf;
        char buf[512];
        char str1[64+1];

        sf = fopen(stats_path, "r");
        if (sf == NULL) {
                ovis_log(lustre_mdt_log, OVIS_LWARNING, "file %s not found\n",
                       stats_path);
                return;
        }

        /* The first line should always be "snapshot_time"
           we will ignore it because it always contains the time that we read
           from the file, not any information about when the stats last
           changed */
        if (fgets(buf, sizeof(buf), sf) == NULL) {
                ovis_log(lustre_mdt_log, OVIS_LWARNING, "failed on read from %s\n",
                       stats_path);
                goto out1;
        }
        if (strncmp("snapshot_time", buf, sizeof("snapshot_time")-1) != 0) {
                ovis_log(lustre_mdt_log, OVIS_LWARNING, "first line in %s is not \"snapshot_time\": %s\n",
                       stats_path, buf);
                goto out1;
        }

        ldms_transaction_begin(general_metric_set);
        while (fgets(buf, sizeof(buf), sf)) {
                uint64_t val1, val2;
                int rc;
                int index;

                rc = sscanf(buf, "%64s %lu samples [%*[^]]] %*u %*u %lu",
                            str1, &val1, &val2);
                if (rc == 2) {
                        index = ldms_metric_by_name(general_metric_set, str1);
                        if (index == -1) {
                                ovis_log(lustre_mdt_log, OVIS_LWARNING, "mdt md_stats metric not found: %s\n",
                                       str1);
                        } else {
                                ldms_metric_set_u64(general_metric_set, index, val1);
                        }
                        continue;
                } else if (rc == 3) {
                        int base_name_len = strlen(str1);
                        index = ldms_metric_by_name(general_metric_set, str1);
                        if (index == -1) {
                                ovis_log(lustre_mdt_log, OVIS_LWARNING, "mdt md_stats metric not found: %s\n",
                                       str1);
                        } else {
                                ldms_metric_set_u64(general_metric_set, index, val1);
                        }
                        sprintf(str1+base_name_len, ".sum"); /* append ".sum" */
                        index = ldms_metric_by_name(general_metric_set, str1);
                        if (index == -1) {
                                ovis_log(lustre_mdt_log, OVIS_LWARNING, "mdt md_stats metric not found: %s\n",
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

void mdt_general_sample(const char *mdt_name, const char *stats_path,
                        const char *osd_path, ldms_set_t general_metric_set)
{
        ovis_log(lustre_mdt_log, OVIS_LDEBUG, "mdt_general_sample() %s\n",
               mdt_name);
        mdt_md_stats_sample(stats_path, general_metric_set);
        osd_sample(osd_path, general_metric_set);
}
