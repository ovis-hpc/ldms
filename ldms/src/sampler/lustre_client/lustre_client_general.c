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

#define MAXNAMESIZE 64

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


/* the following get added to the schema if extra215 config option is present. */
static const char *extra215_llite_stats_uint64_t_entries[] = {
        "read",
        "write",
        "opencount",
        "openclosetime",
        NULL
};

/* the following exist at least as far back as 2.15 lustre clients */
static const char *extratimes_llite_stats_uint64_t_entries[] = {
	"start_time", /* this truncates the data to seconds; it's ok. */
	"elapsed_time", /* this truncates the data to seconds; it's ok. */
        NULL
};

int llite_general_schema_is_initialized()
{
        if (llite_general_schema != NULL)
                return 0;
        else
                return -1;
}

int llite_general_schema_init(comp_id_t cid, int schema_extras)
{
        ldms_schema_t sch;
        int rc;
        int i;

        log_fn(LDMSD_LDEBUG, SAMP ": llite_general_schema_init()\n");
	char schema_name[LDMS_SET_NAME_MAX];
	if (schema_extras)
		sprintf(schema_name,"lustre_client_%d", schema_extras);
	else
		sprintf(schema_name,"lustre_client");
        sch = ldms_schema_new(schema_name);
        if (sch == NULL) {
		log_fn(LDMSD_LERROR, SAMP ": lustre_llite_general schema new failed"
			" (out of memory)\n");
                goto err1;
	}
	const char *field;
	field = "component_id";
	rc = comp_id_helper_schema_add(sch, cid);
	if (rc) {
		rc = -rc;
		goto err2;
	}
	field = "job data";
	rc = jobid_helper_schema_add(sch);
	if (rc <0) {
		goto err2;
	}
	field = "fs_name";
        rc = ldms_schema_meta_array_add(sch, field, LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0) {
                goto err2;
	}
	field = "llite";
        rc = ldms_schema_meta_array_add(sch, field, LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0) {
                goto err2;
	}
        /* add llite stats entries */
        for (i = 0; llite_stats_uint64_t_entries[i] != NULL; i++) {
		field = llite_stats_uint64_t_entries[i];
                rc = ldms_schema_metric_add(sch, field, LDMS_V_U64);
                if (rc < 0) {
                        goto err2;
		}
        }
	if (schema_extras & EXTRA215) {
		for (i = 0; extra215_llite_stats_uint64_t_entries[i] != NULL; i++) {
			field = extra215_llite_stats_uint64_t_entries[i];
			rc = ldms_schema_metric_add(sch, field, LDMS_V_U64);
			if (rc < 0) {
				goto err2;
			}
		}
	}
	if (schema_extras & EXTRATIMES) {
		for (i = 0; extratimes_llite_stats_uint64_t_entries[i] != NULL; i++) {
			field = extratimes_llite_stats_uint64_t_entries[i];
			rc = ldms_schema_metric_add(sch, field, LDMS_V_U64);
			if (rc < 0) {
				goto err2;
			}
		}
	}

        llite_general_schema = sch;

        return 0;
err2:
	log_fn(LDMSD_LERROR, SAMP ": lustre_llite_general schema creation failed to add %s. (%s)\n",
		field, STRERROR(-rc));
        ldms_schema_delete(sch);
err1:
        return -1;
}

void llite_general_schema_fini()
{
        log_fn(LDMSD_LDEBUG, SAMP ": llite_general_schema_fini()\n");
        if (llite_general_schema != NULL) {
                ldms_schema_delete(llite_general_schema);
                llite_general_schema = NULL;
        }
}

void llite_general_destroy(ldms_set_t set)
{
        ldmsd_set_deregister(ldms_set_instance_name_get(set), SAMP);
        ldms_set_unpublish(set);
        ldms_set_delete(set);
}

/* must be schema created by llite_general_schema_create() */
ldms_set_t llite_general_create(const char *producer_name,
                                const char *fs_name,
				const char *llite_name,
				const comp_id_t cid,
				const struct base_auth *auth)
{
        ldms_set_t set;
        int index;
        char instance_name[LDMS_PRODUCER_NAME_MAX+64];

        log_fn(LDMSD_LDEBUG, SAMP ": llite_general_create()\n");
        snprintf(instance_name, sizeof(instance_name), "%s/%s",
                 producer_name, llite_name);
        set = ldms_set_new(instance_name, llite_general_schema);
	if (!set) {
		errno = ENOMEM;
		return NULL;
	}
        ldms_set_producer_name_set(set, producer_name);
	base_auth_set(auth, set);
        index = ldms_metric_by_name(set, "fs_name");
        ldms_metric_array_set_str(set, index, fs_name);
        index = ldms_metric_by_name(set, "llite");
        ldms_metric_array_set_str(set, index, llite_name);
	comp_id_helper_metric_update(set, cid);
        ldms_set_publish(set);
        ldmsd_set_register(set, SAMP);
        return set;
}

static int llite_stats_sample(const char *stats_path,
                                   ldms_set_t general_metric_set)
{
        FILE *sf;
        char buf[512];
	char str1[MAXNAMESIZE+1];
	int ec = 0;

        sf = fopen(stats_path, "r");
        if (sf == NULL) {
                log_fn(LDMSD_LWARNING, SAMP ": file %s not found\n",
                       stats_path);
                return ENOENT;
        }

        /* The first line should always be "snapshot_time"
           we will ignore it because it always contains the time that we read
           from the file, not any information about when the stats last
           changed */
        if (fgets(buf, sizeof(buf), sf) == NULL) {
                log_fn(LDMSD_LWARNING, SAMP ": failed on read from %s\n",
                       stats_path);
		ec = ENOMSG;
                goto out1;
        }
        if (strncmp("snapshot_time", buf, sizeof("snapshot_time")-1) != 0) {
                log_fn(LDMSD_LWARNING, SAMP ": first line in %s is not \"snapshot_time\": %s\n",
                       stats_path, buf);
		ec = ENOMSG;
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
                        if (index != -1) {
                                ldms_metric_set_u64(general_metric_set, index, val1);
			} /*else {
				// this is a normal case as lustre evolves reporting.
                                log_fn(LDMSD_LWARNING, SAMP ": llite stats metric not found: %s\n",
                                       str1);
                        } */
                        continue;
                } else if (rc == 3) {
                        int base_name_len = strlen(str1);
                        sprintf(str1+base_name_len, ".sum"); /* append ".sum" */
                        index = ldms_metric_by_name(general_metric_set, str1);
                        if (index == -1) {
				/* non-sum metric, possibly */
				str1[base_name_len] = '\0';
				index = ldms_metric_by_name(general_metric_set, str1);
				if (index != -1) {
					ldms_metric_set_u64(general_metric_set, index, val1);
				}/* else {
				// this is a normal case as lustre evolves reporting
					log_fn(LDMSD_LWARNING,
					SAMP ": llite stats metric not found: %s\n", str1);
				} */
			} else {
				/* sum metric */
                                ldms_metric_set_u64(general_metric_set, index, val2);
                        }
                        continue;
                }
        }
        ldms_transaction_end(general_metric_set);
out1:
        fclose(sf);

        return ec;
}

void llite_general_sample(const char *llite_name, const char *stats_path,
                          ldms_set_t general_metric_set)
{
        log_fn(LDMSD_LDEBUG, SAMP ": llite_general_sample() %s\n",
               llite_name);
        llite_stats_sample(stats_path, general_metric_set);
}
