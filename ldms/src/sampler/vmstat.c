/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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

/**
 * \file vmstat.c
 * \brief /proc/vmstat data provider
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_slurmjobid.h"

#define PROC_FILE "/proc/vmstat"

static char *procfile = PROC_FILE;

static ldms_set_t set;
static FILE *mf;
static ldms_metric_t *metric_table;
static ldmsd_msg_log_f msglog;
static uint64_t comp_id;

static ldms_set_t get_set()
{
	return set;
}

LDMS_JOBID_GLOBALS;

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, metric_count;
	uint64_t metric_value;
	char *s;
	char lbuf[256];
	char metric_name[128];

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMS_LERROR,"Could not open the vmstat file '%s'\n", procfile);
		return ENOENT;
	}

	tot_data_sz = 0;
	tot_meta_sz = 0;

	LDMS_SIZE_JOBID_METRIC(vmstat,meta_sz,tot_meta_sz,
		data_sz,tot_data_sz,metric_count,rc,msglog);
	/*
	 * Process the file once first to determine the metric set size.
	 */
	metric_count = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &metric_value);
		if (rc < 2)
			break;

		rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
		if (rc)
			return rc;

		tot_meta_sz += meta_sz;
		tot_data_sz += data_sz;
		metric_count++;
	} while (s);


	/* Create the metric set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;

	/*
	 * Process the file again to define all the metrics.
	 */
	rc = ENOMEM;

	int metric_no = 0;

	LDMS_ADD_JOBID_METRIC(metric_table,metric_no,set,rc,err,comp_id);

	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &metric_value);
		if (rc < 2)
			break;

		rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
		if (rc)
			return rc;

		metric_table[metric_no] = ldms_add_metric(set, metric_name, LDMS_V_U64);

		if (!metric_table[metric_no]){
			rc = ENOMEM;
			goto err;
		}
		ldms_set_user_data(metric_table[metric_no], comp_id);
		metric_no++;
	} while (s);

	return 0;

 err:
	ldms_destroy_set(set);
	return rc;
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtol(value, NULL, 0);

	LDMS_CONFIG_JOBID_METRIC(value,avl);

	value = av_value(avl, "set");
	if (value)
		create_metric_set(value);

	return 0;
}

static int sample(void)
{
	int rc;
	int metric_no;
	char *s;
	char lbuf[256];
	char metric_name[128];
	union ldms_value v;

	if (!set) {
		msglog(LDMS_LDEBUG,"vmstat: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);

	metric_no = 0;

	LDMS_JOBID_SAMPLE(v,metric_table,metric_no);

	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &v.v_u64);
		if (rc != 2) {
			rc = EINVAL;
			goto out;
		}
		ldms_set_metric(metric_table[metric_no], &v);
		metric_no++;
	} while (s);
	rc = 0;
 out:
	ldms_end_transaction(set);
	return rc;
}

static void term(void)
{
	if (set)
		ldms_destroy_set(set);
	set = NULL;

	LDMS_JOBID_TERM;

}

static const char *usage(void)
{
	return "config name=vmstat component_id=<comp_id> set=<setname> "
			"    comp_id     The component id value.\n"
			"    setname     The set name.\n"
			LDMS_JOBID_DESC
			;
}

static struct ldmsd_sampler vmstat_plugin = {
	.base = {
		.name = "vmstat",
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &vmstat_plugin.base;
}
