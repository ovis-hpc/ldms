/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 * Copyright 2022 NTESS.
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
#include "lustre_mdc.h"
#include "lustre_mdc_general.h"
#include "jobid_helper.h"

static ldms_schema_t mdc_general_schema;

#define MAXNAMESIZE 64

static char *mdc_md_stats_uint64_t_entries[] = {
	"close",
	"create",
	"enqueue",
	"getattr",
	"intent_lock",
	"link",
	"rename",
	"setattr",
	"fsync",
	"read_page",
	"unlink",
	"setxattr",
	"getxattr",
	"intent_getattr_async",
	"revalidate_lock",
	NULL
};

static char *md_timing_fields[] = { /* count, min_usec, max_usec, tot_usec, tot(usec^2) */
	"req_waittime",
	"mds_getattr",
	"mds_getattr_lock",
	"mds_close",
	"mds_readpage",
	"mds_connect",
	"mds_get_root",
	"mds_statfs",
	"ldlm_cancel",
	"obd_ping",
	"seq_query",
	"fld_query",
	NULL
};


int mdc_general_schema_is_initialized()
{
	if (mdc_general_schema)
		return 0;
	else
		return -1;
}

int mdc_general_schema_init(comp_id_t cid, int mdc_timing)
{
	ldms_schema_t sch;
	int rc;
	int i;
	char str1[MAXNAMESIZE+1];

	log_fn(LDMSD_LDEBUG, SAMP" mdc_general_schema_init()\n");
	const char *sn;
	if (mdc_timing > 0)
		sn = "lustre_mdc_ops_timing";
	else
		sn = "lustre_mdc_ops";
	sch = ldms_schema_new(sn);
	if (sch == NULL)
		goto err1;
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
	rc = ldms_schema_meta_array_add(sch, field, LDMS_V_CHAR_ARRAY, MAXNAMESIZE);
	if (rc < 0)
		goto err2;
	field = "mdc";
	rc = ldms_schema_meta_array_add(sch, field, LDMS_V_CHAR_ARRAY, MAXNAMESIZE);
	if (rc < 0)
		goto err2;
	/* add mdc md_stats entries */
	for (i = 0; mdc_md_stats_uint64_t_entries[i] != NULL; i++) {
		field = mdc_md_stats_uint64_t_entries[i];
		rc = ldms_schema_metric_add(sch, field, LDMS_V_U64);
		if (rc < 0)
			goto err2;
	}
	/* add mdc stats timing entries. the order here is assumed and matched
	 * in sampling. */

	if (!mdc_timing)
		goto timing0;

	for (i = 0; md_timing_fields[i] != NULL; i++) {
		sprintf(str1, "%s__count", md_timing_fields[i]);
		rc = ldms_schema_metric_add(sch, str1, LDMS_V_U64);
		if (rc < 0)
			goto err3;
		sprintf(str1, "%s__min", md_timing_fields[i]);
		rc = ldms_schema_metric_add(sch, str1, LDMS_V_U64);
		if (rc < 0)
			goto err3;
		sprintf(str1, "%s__max", md_timing_fields[i]);
		rc = ldms_schema_metric_add(sch, str1, LDMS_V_U64);
		if (rc < 0)
			goto err3;
		sprintf(str1, "%s__sum", md_timing_fields[i]);
		rc = ldms_schema_metric_add(sch, str1, LDMS_V_U64);
		if (rc < 0)
			goto err3;
		sprintf(str1, "%s__sumsqs", md_timing_fields[i]);
		rc = ldms_schema_metric_add(sch, str1, LDMS_V_U64);
		if (rc < 0)
			goto err3;
	}

timing0:
	mdc_general_schema = sch;
	return 0;
err3:
	log_fn(LDMSD_LERROR, SAMP ": lustre_mdc_general schema creation failed to add %s. (%s)\n",
		str1, STRERROR(-rc));
	goto out;
err2:
	log_fn(LDMSD_LERROR, SAMP ": lustre_mdc_general schema creation failed to add %s. (%s)\n",
		field, STRERROR(-rc));
out:
	ldms_schema_delete(sch);
err1:
	log_fn(LDMSD_LERROR, SAMP" lustre_mdc_general schema creation failed\n");
	return -1;
}

void mdc_general_schema_fini()
{
	log_fn(LDMSD_LDEBUG, SAMP" mdc_general_schema_fini()\n");
	if (mdc_general_schema != NULL) {
		ldms_schema_delete(mdc_general_schema);
		mdc_general_schema = NULL;
	}
}

void mdc_general_destroy(ldms_set_t set)
{
	ldmsd_set_deregister(ldms_set_instance_name_get(set), SAMP);
	ldms_set_unpublish(set);
	ldms_set_delete(set);
}


/* must be schema created by mdc_general_schema_create() */
ldms_set_t mdc_general_create(const char *producer_name,
				const char *fs_name,
				const char *mdc_name,
				const comp_id_t cid,
				const struct base_auth *auth)
{
	ldms_set_t set;
	int index;
	char instance_name[LDMS_PRODUCER_NAME_MAX + MAXNAMESIZE];

	log_fn(LDMSD_LDEBUG, SAMP" mdc_general_create()\n");
	snprintf(instance_name, sizeof(instance_name), "%s/%s",
		 producer_name, mdc_name);
	set = ldms_set_new(instance_name, mdc_general_schema);
	if (!set) {
		errno = ENOMEM;
		return NULL;
	}
	ldms_set_producer_name_set(set, producer_name);
	base_auth_set(auth, set);
	index = ldms_metric_by_name(set, "fs_name");
	ldms_metric_array_set_str(set, index, fs_name);
	index = ldms_metric_by_name(set, "mdc");
	ldms_metric_array_set_str(set, index, mdc_name);
	comp_id_helper_metric_update(set, cid);
	ldms_set_publish(set);
	ldmsd_set_register(set, SAMP);
	return set;
}

static int mdc_ops_sample(const char *path,
			   ldms_set_t general_metric_set)
{
	FILE *sf;
	char buf[512];
	char str1[MAXNAMESIZE+1];
	int ec = 0;

	sf = fopen(path, "r");
	if (sf == NULL) {
		return ENOENT;
	}

	/* The first line should always be "snapshot_time"
	   we will ignore it because it always contains the time that we read
	   from the file, not any information about when the stats last
	   changed */
	if (fgets(buf, sizeof(buf), sf) == NULL) {
		ec = ENOMSG;
		goto out1;
	}
	if (strncmp("snapshot_time", buf, sizeof("snapshot_time")-1) != 0) {
		ec = ENOMSG;
		goto out1;
	}

	while (fgets(buf, sizeof(buf), sf)) {
		uint64_t val1 = 0, val2 = 0, val3 = 0, val4 = 0, val5 = 0;
		int rc;
		int index;

		rc = sscanf(buf, "%64s %lu samples [%*[^]]] %lu %lu %lu %lu",
			    str1, &val1, &val2, &val3, &val4, &val5);
		if (rc == 2) {
			index = ldms_metric_by_name(general_metric_set, str1);
			if (index > 1) {
				ldms_metric_set_u64(general_metric_set, index, val1);
			}
			continue;
		}
	}
out1:
	fclose(sf);
	return ec;
}

static int mdc_timing_sample(const char *path,
				ldms_set_t general_metric_set)
{
	FILE *sf;
	char buf[512];
	char str1[MAXNAMESIZE+1];
	char str2[2*MAXNAMESIZE+1];
	int ec = 0;
	sf = fopen(path, "r");
	if (sf == NULL) {
		return ENOENT;
	}

	/* The first line should always be "snapshot_time"
	   we will ignore it because it always contains the time that we read
	   from the file, not any information about when the stats last
	   changed */
	if (fgets(buf, sizeof(buf), sf) == NULL) {
		ec = ENOMSG;
		goto out1;
	}
	if (strncmp("snapshot_time", buf, sizeof("snapshot_time")-1) != 0) {
		ec = ENOMSG;
		goto out1;
	}
	while (fgets(buf, sizeof(buf), sf)) {
		uint64_t valmin = 0, valmax = 0, valsum = 0, valsumsqs = 0,
			valcount = 0;
		int rc;
		int index;

		rc = sscanf(buf, "%64s %lu samples [%*[^]]] %lu %lu %lu %lu",
				str1, &valcount, &valmin, &valmax, &valsum,
				&valsumsqs);
		if (rc == 6) {
			sprintf(str2, "%s__count", str1);
			index = ldms_metric_by_name(general_metric_set, str2);
			if (index < 1) {
				continue;
			}
			ldms_metric_set_u64(general_metric_set, index, valcount);
			ldms_metric_set_u64(general_metric_set, index+1, valmin);
			ldms_metric_set_u64(general_metric_set, index+2, valmax);
			ldms_metric_set_u64(general_metric_set, index+3, valsum);
			ldms_metric_set_u64(general_metric_set, index+4, valsumsqs);
		}
	}
out1:
	fclose(sf);
	return ec;
}

void mdc_general_sample(const char *mdc_name, const char *md_stats_path,
			const char *stats_path, ldms_set_t general_metric_set,
			const int mdc_timing)
{
	ldms_transaction_begin(general_metric_set);
	jobid_helper_metric_update(general_metric_set);
	int ec1, ec2 = 0;
	ec1 = mdc_ops_sample(md_stats_path, general_metric_set);
	if (!ec1) {
		if (mdc_timing)
			ec2 = mdc_timing_sample(stats_path, general_metric_set);
	} else {
		log_fn(LDMSD_LDEBUG, SAMP": mdc_md_stats_sample %s fail (%s)\n",
			md_stats_path, STRERROR(ec1));
	}
	if (ec2)
		log_fn(LDMSD_LDEBUG, SAMP": mdc_timing_sample %s fail (%s)\n",
			stats_path, STRERROR(ec2));
	ldms_transaction_end(general_metric_set);
}
