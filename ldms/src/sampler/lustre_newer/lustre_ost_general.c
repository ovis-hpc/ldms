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
#include "lustre_shared.h"

/* Defined in lustre_ost.c */
extern ovis_log_t lustre_ost_log;

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

        ovis_log(lustre_ost_log, OVIS_LDEBUG, "ost_general_schema_init()\n");
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
	ovis_log(lustre_ost_log, OVIS_LERROR, "lustre_ost_general schema creation failed to add %s. (%s)\n",
		field, STRERROR(-rc));
        ldms_schema_delete(sch);
err1:
        ovis_log(lustre_ost_log, OVIS_LERROR, "lustre_ost_general schema creation failed\n");
        return -1;
}

void ost_general_schema_fini()
{
        ovis_log(lustre_ost_log, OVIS_LDEBUG, "ost_general_schema_fini()\n");
        if (ost_general_schema != NULL) {
                ldms_schema_delete(ost_general_schema);
                ost_general_schema = NULL;
        }
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
                ovis_log(lustre_ost_log, OVIS_LWARNING, "unable to open %s\n", filepath);
                return 0;
        }
        if (fgets(valbuf, sizeof(valbuf), fp) == NULL) {
                ovis_log(lustre_ost_log, OVIS_LWARNING, "unable to read %s\n", filepath);
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


void ost_general_destroy(lo_context_t ctxt, ldms_set_t set)
{
        ldmsd_set_deregister(ldms_set_instance_name_get(set), ctxt->cfg_name);
        ldms_set_unpublish(set);
        ldms_set_delete(set);
}


/* must be schema created by ost_general_schema_create() */
ldms_set_t ost_general_create(lo_context_t ctxt,
			      const char *producer_name,
			      const char *fs_name,
			      const char *ost_name,
			      const comp_id_t cid)
{
        ldms_set_t set;
        int index;
        char instance_name[LDMS_PRODUCER_NAME_MAX+64];

        ovis_log(lustre_ost_log, OVIS_LDEBUG, "ost_general_create()\n");
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
	ldmsd_set_register(set, ctxt->cfg_name);
        return set;
}


void ost_general_sample(const char *ost_name, const char *stats_path,
                        const char *osd_path, ldms_set_t general_metric_set)
{
        ovis_log(lustre_ost_log, OVIS_LDEBUG, "ost_general_sample() %s\n",
               ost_name);
        ldms_transaction_begin(general_metric_set);
        lustre_stats_file_sample(stats_path, general_metric_set, lustre_ost_log);
        osd_sample(osd_path, general_metric_set);
        ldms_transaction_end(general_metric_set);
}
