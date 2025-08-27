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
#include "lustre_shared.h"

static ldms_schema_t mdt_general_schema;

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

int mdt_general_schema_init(lm_context_t ctxt)
{
        ldms_schema_t sch;
        int rc;
        int i;

        ovis_log(ctxt->log, OVIS_LDEBUG, "mdt_general_schema_init()\n");
        sch = ldms_schema_new("lustre_mdt");
        if (sch == NULL)
                goto err1;
	const char *field;
	field = "component_id";
	rc = comp_id_helper_schema_add(sch, &ctxt->cid);
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
	ovis_log(ctxt->log, OVIS_LERROR, "lustre_mdt_general schema creation failed to add %s. (%s)\n",
		field, STRERROR(-rc));
        ldms_schema_delete(sch);
err1:
        ovis_log(ctxt->log, OVIS_LERROR, "lustre_mdt_general schema creation failed\n");
        return -1;
}

void mdt_general_schema_fini(lm_context_t ctxt)
{
        ovis_log(ctxt->log, OVIS_LDEBUG, "mdt_general_schema_fini()\n");
        if (mdt_general_schema != NULL) {
                ldms_schema_delete(mdt_general_schema);
                mdt_general_schema = NULL;
        }
}

static void osd_sample(const char *osd_path, ldms_set_t general_metric_set, ovis_log_t log)
{
        char *field;
        uint64_t val;
        int index;
        int i;

        for (i = 0; (field = osd_uint64_t_fields[i]) != NULL; i++) {
                val = lustre_file_read_uint64_t(osd_path, field, log);
                index = ldms_metric_by_name(general_metric_set, field);
                ldms_metric_set_u64(general_metric_set, index, val);
         }
}


void mdt_general_destroy(lm_context_t ctxt, ldms_set_t set)
{
        ldmsd_set_deregister(ldms_set_instance_name_get(set), ctxt->cfg_name);
        ldms_set_unpublish(set);
        ldms_set_delete(set);
}


/* must be schema created by mdt_general_schema_create() */
ldms_set_t mdt_general_create(lm_context_t ctxt,
			      const char *fs_name,
			      const char *mdt_name)
{
        ldms_set_t set;
        int index;
        char instance_name[LDMS_PRODUCER_NAME_MAX+64];

        ovis_log(ctxt->log, OVIS_LDEBUG, "mdt_general_create()\n");
        snprintf(instance_name, sizeof(instance_name), "%s/%s",
                 ctxt->producer_name, mdt_name);
        set = ldms_set_new(instance_name, mdt_general_schema);
        ldms_set_producer_name_set(set, ctxt->producer_name);
        index = ldms_metric_by_name(set, "fs_name");
        ldms_metric_array_set_str(set, index, fs_name);
        index = ldms_metric_by_name(set, "mdt");
        ldms_metric_array_set_str(set, index, mdt_name);
	comp_id_helper_metric_update(set, &ctxt->cid);
        ldms_set_publish(set);
	ldmsd_set_register(set, ctxt->cfg_name);
        return set;
}

void mdt_general_sample(lm_context_t ctxt,
			const char *mdt_name, const char *stats_path,
                        const char *osd_path, ldms_set_t general_metric_set)
{
        ovis_log(ctxt->log, OVIS_LDEBUG, "mdt_general_sample() %s\n",
               mdt_name);
        ldms_transaction_begin(general_metric_set);
        lustre_stats_file_sample(stats_path, general_metric_set, ctxt->log);
        osd_sample(osd_path, general_metric_set, ctxt->log);
        ldms_transaction_end(general_metric_set);
}
