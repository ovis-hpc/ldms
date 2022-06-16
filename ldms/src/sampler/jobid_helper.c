/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

#include "jobid_helper.h"

static char job_set_name[MAX_JOB_SET_NAME];
static ldms_set_t job_set;
static int job_id_idx = -1;
static int app_id_idx;
static int job_slot_list_idx;
static int job_slot_list_tail_idx;
static int job_start_idx;
static int job_end_idx;

int jobid_helper_schema_add(ldms_schema_t schema)
{
	int rc;

	rc = ldms_schema_metric_add(schema, "job_id", LDMS_V_U64);
	if (rc < 0) {
		return rc;
	}
	rc = ldms_schema_metric_add(schema, "app_id", LDMS_V_U64);
	if (rc < 0) {
		return rc;
	}

	return 0;
}

static void jobid_helper_init()
{
	job_set = ldms_set_by_name(job_set_name);
	if (!job_set) {
		goto err;
	}
	job_id_idx = ldms_metric_by_name(job_set, "job_id");
	if (job_id_idx < 0) {
		goto err;
	}
	/* app_id is optional */
	app_id_idx = ldms_metric_by_name(job_set, "app_id");

	/* If job_slot_list is present, we know it's the mt-slurm sampler */
	job_slot_list_idx = ldms_metric_by_name(job_set, "job_slot_list");
	job_slot_list_tail_idx = ldms_metric_by_name(job_set, "job_slot_list_tail");

	job_start_idx = ldms_metric_by_name(job_set, "job_start");
	if (job_start_idx < 0) {
		goto err;
	}
	job_end_idx = ldms_metric_by_name(job_set, "job_end");
	if (job_end_idx < 0) {
		goto err;
	}

	return;
err:
	job_set = NULL;
	job_id_idx = -1;
}

void jobid_helper_metric_update(ldms_set_t set)
{
	int index;
	uint64_t job_id = 0;
	uint64_t app_id = 0;
	uint32_t start, end;
	struct ldms_timestamp ts;

	if (!set)
		return;

	/* Check if job data is available */
	if (!job_set)
		jobid_helper_init();
	if (!job_set)
		return;

	if (job_id_idx < 0)
		return;

	ts = ldms_transaction_timestamp_get(set);

	if (job_slot_list_idx < 0) {
		start = ldms_metric_get_u64(job_set, job_start_idx);
		end = ldms_metric_get_u64(job_set, job_end_idx);
		if ((ts.sec >= start) && ((end == 0) || (ts.sec <= end))) {
			job_id = ldms_metric_get_u64(job_set, job_id_idx);
			app_id = ldms_metric_get_u64(job_set, app_id_idx);
		}
	} else {
		int slot_idx = ldms_metric_get_s32(job_set, job_slot_list_tail_idx);
		int slot = ldms_metric_array_get_s32(job_set, job_slot_list_idx, slot_idx);
		if (slot < 0) {
			job_id = app_id = 0;
		} else {
			start = ldms_metric_array_get_u32(job_set, job_start_idx, slot);
			end = ldms_metric_array_get_u32(job_set, job_end_idx, slot);
			if ((ts.sec >= start) && ((end == 0) || (ts.sec <= end))) {
				job_id = ldms_metric_array_get_u64(job_set, job_id_idx, slot);
				app_id = ldms_metric_array_get_u64(job_set, app_id_idx, slot);
			}
		}
	}

	index = ldms_metric_by_name(set, "job_id");
	ldms_metric_set_u64(set, index, job_id);
	index = ldms_metric_by_name(set, "app_id");
	ldms_metric_set_u64(set, index, app_id);

}

int jobid_helper_config(struct attr_value_list *avl)
{
	char *value;

	value = av_value(avl, "job_set");
	if (!value) {
		strcpy(job_set_name, "job_info");
	} else {
		if (strlen(value) > MAX_JOB_SET_NAME -1)
			return ENAMETOOLONG;
		strcpy(job_set_name, value);
	}
	return 0;
}
