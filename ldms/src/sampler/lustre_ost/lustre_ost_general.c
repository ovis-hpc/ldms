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
#include "lustre_ost.h"
#include "lustre_ost_general.h"

#define MAXNAMESIZE 64

static ldms_schema_t ost_general_schema;

static char *obdfilter_stats_uint64_t_entries[] = {
	"read_bytes.sum", /* sum field from read_bytes entry */
	"write_bytes.sum",/* sum field from write_bytes entry */
        "setattr",
        "punch",
        "sync",
        "destroy",
        "create",
        "statfs",
        "get_info",
        "set_info",
        "quotactl",
        "connect",
        "reconnect",
        "disconnect",
        "preprw",
        "commitrw",
        "ping",
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

int ost_general_schema_is_initialized()
{
        if (ost_general_schema != NULL)
                return 0;
        else
                return -1;
}

int ost_general_schema_init(comp_id_t cid)
{
        ldms_schema_t sch;
        int rc;
        int i;

        log_fn(LDMSD_LDEBUG, SAMP" ost_general_schema_init()\n");
        sch = ldms_schema_new("lustre_ost");
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
        rc = ldms_schema_meta_array_add(sch, "ost", LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0)
                goto err2;
        /* add obdfilter stats entries */
        for (i = 0; obdfilter_stats_uint64_t_entries[i] != NULL; i++) {
                rc = ldms_schema_metric_add(sch, obdfilter_stats_uint64_t_entries[i],
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

        ost_general_schema = sch;

        return 0;
err2:
	log_fn(LDMSD_LERROR, SAMP ": lustre_ost_general schema creation failed to add %s. (%s)\n",
		field, STRERROR(-rc));
        ldms_schema_delete(sch);
err1:
        log_fn(LDMSD_LERROR, SAMP" lustre_ost_general schema creation failed\n");
        return -1;
}

void ost_general_schema_fini()
{
        log_fn(LDMSD_LDEBUG, SAMP" ost_general_schema_fini()\n");
        if (ost_general_schema != NULL) {
                ldms_schema_delete(ost_general_schema);
                ost_general_schema = NULL;
        }
}

/*
 * Find the osd directory that should contain simple stats files such
 * as "kbytesfree".
 *
 * Returns strdup'ed string or NULL.  Caller must free.
 */
char *ost_general_osd_path_find(const char * const *paths, const char *component_name)
{
	char *path;
	char *osd_dir = NULL;
	int i;

	for (i = 0, path = (char *)paths[0]; path != NULL; i++, path = (char *)paths[i]) {
		struct dirent *dirent;
		DIR *dir;

		dir = opendir(path);
		if (dir == NULL) {
			log_fn(LDMSD_LDEBUG,
			       "osd for %s, base dir %s does not exist\n",
			       component_name, path);
			continue;
		}

		while ((dirent = readdir(dir)) != NULL) {
			if (dirent->d_type == DT_DIR &&
			    strncmp(dirent->d_name, "osd-", strlen("osd-")) == 0) {
				char tmp_path[PATH_MAX];
				snprintf(tmp_path, PATH_MAX, "%s/%s/%s",
					 path, dirent->d_name, component_name);
				if (access(tmp_path, F_OK) == 0) {
					osd_dir = strdup(tmp_path);
					break;
				}
			}
		}

		closedir(dir);

		if (osd_dir != NULL) {
			log_fn(LDMSD_LDEBUG,
			       "osd for %s found at path %s\n",
			       component_name, osd_dir);
			break;
		} else {
			log_fn(LDMSD_LDEBUG,
			       "osd for %s not found in base path %s\n",
			       component_name, path);
		}

	}

	return osd_dir;
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
                log_fn(LDMSD_LWARNING, SAMP" unable to open %s\n", filepath);
                return 0;
        }
        if (fgets(valbuf, sizeof(valbuf), fp) == NULL) {
                log_fn(LDMSD_LWARNING, SAMP" unable to read %s\n", filepath);
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


void ost_general_destroy(ldms_set_t set)
{
        ldmsd_set_deregister(ldms_set_instance_name_get(set), SAMP);
        ldms_set_unpublish(set);
        ldms_set_delete(set);
}


/* must be schema created by ost_general_schema_create() */
ldms_set_t ost_general_create(const char *producer_name,
			      const char *fs_name,
			      const char *ost_name,
			      const comp_id_t cid)
{
        ldms_set_t set;
        int index;
        char instance_name[LDMS_PRODUCER_NAME_MAX+64];

        log_fn(LDMSD_LDEBUG, SAMP" ost_general_create()\n");
        snprintf(instance_name, sizeof(instance_name), "%s/%s",
                 producer_name, ost_name);
        set = ldms_set_new(instance_name, ost_general_schema);
        ldms_set_producer_name_set(set, producer_name);
        index = ldms_metric_by_name(set, "fs_name");
        ldms_metric_array_set_str(set, index, fs_name);
        index = ldms_metric_by_name(set, "ost");
        ldms_metric_array_set_str(set, index, ost_name);
	comp_id_helper_metric_update(set, cid);
        ldms_set_publish(set);
	ldmsd_set_register(set, SAMP);
        return set;
}

static int obdfilter_stats_sample(const char *stats_path,
				  ldms_set_t metric_set)
{
	FILE *sf;
	char buf[512];
	int rc = 0;

	sf = fopen(stats_path, "r");
	if (sf == NULL) {
		log_fn(LDMSD_LWARNING, ": file %s not found\n",
			 stats_path);
		return ENOENT;
	}

	/* The first line should always be "snapshot_time"
           we will ignore it because it always contains the time that we read
           from the file, not any information about when the stats last
           changed */
	if (fgets(buf, sizeof(buf), sf) == NULL) {
		log_fn(LDMSD_LWARNING, ": failed on read from %s\n",
			 stats_path);
		rc = ENOMSG;
		goto out1;
	}
	if (strncmp("snapshot_time", buf, sizeof("snapshot_time")-1) != 0) {
		log_fn(LDMSD_LWARNING, ": first line in %s is not \"snapshot_time\": %s\n",
			 stats_path, buf);
		rc = ENOMSG;
		goto out1;
	}

	while (fgets(buf, sizeof(buf), sf)) {
		char field_name[MAXNAMESIZE+1];
		uint64_t samples, sum;
		int num_matches;
		int index;

		num_matches = sscanf(buf,
				     "%64s %"SCNu64" samples [%*[^]]] %*u %*u %"SCNu64" %*u",
				     field_name, &samples, &sum);
		if (num_matches >= 2) {
			/* we know at least "samples" is available */
			index = ldms_metric_by_name(metric_set, field_name);
			if (index != -1) {
				ldms_metric_set_u64(metric_set, index, samples);
			}
		}
		if (num_matches >= 3) {
			/* we know that "sum" is also avaible */
			int base_name_len = strlen(field_name);
			sprintf(field_name+base_name_len, ".sum"); /* append ".sum" */
			index = ldms_metric_by_name(metric_set, field_name);
			if (index != -1) {
				ldms_metric_set_u64(metric_set, index, sum);
			}
		}
	}
out1:
	fclose(sf);

	return rc;
}

void ost_general_sample(const char *ost_name, const char *stats_path,
                        const char *osd_path, ldms_set_t general_metric_set)
{
        log_fn(LDMSD_LDEBUG, SAMP" ost_general_sample() %s\n",
               ost_name);
	ldms_transaction_begin(general_metric_set);
        obdfilter_stats_sample(stats_path, general_metric_set);
        osd_sample(osd_path, general_metric_set);
	ldms_transaction_end(general_metric_set);
}
