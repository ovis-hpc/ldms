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
 * \file procstatutil.c
 * \brief /proc/stat/util data provider
 */
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

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;

int numcpu_plusone;
uint64_t comp_id = UINT64_MAX;

#undef CHECK_PROCSTATUTIL_TIMING
#ifdef CHECK_PROCSTATUTIL_TIMING
//Some temporary for testing
ldms_metric_t tv_sec_metric_handle2;
ldms_metric_t tv_nsec_metric_handle2;
ldms_metric_t tv_dnsec_metric_handle;
#endif
#define PROCSTATUTIL_LINE_MAX 256
#define PROCSTATUTIL_NAME_MAX 256

typedef enum {
        PROCSTATUTIL_METRICS_CPU,
        PROCSTATUTIL_METRICS_BOTH
} procstatutil_metrics_type_t;

/**
 * Which metrics - cpu or both. default cpu.
 */
procstatutil_metrics_type_t procstatutil_metrics_type;

static ldms_set_t get_set()
{
	return set;
}

/*
 * Depending on the kernel version, not all of the rows will
 * necessarily be used. Since new columns are added at the end of the
 * line, this should work across all kernels up to 3.5.
 */
static int ncoresuffix = 3; //used for getting sum names from per core names (below)
static char *metric_name_fmt[] = {
	"user#%d",
	"nice#%d",
	"sys#%d",
	"idle#%d",
	"iowait#%d",
	"irq#%d",
	"softirq#%d",
	"steal#%d",
	"guest#%d",
	"guest_nice#%d",
};
static int create_metric_set(const char *path)
{
	size_t meta_sz, total_meta_sz;
	size_t data_sz, total_data_sz;
	int rc;
	int metric_count;
	int column_count = 0;
	int cpu_count;
	char *s;
	char lbuf[PROCSTATUTIL_LINE_MAX];
	char metric_name[PROCSTATUTIL_NAME_MAX];

	mf = fopen("/proc/stat", "r");
	if (!mf) {
		msglog(LDMS_LDEBUG,"Could not open the /proc/stat file.\n");
		return ENOENT;
	}

	total_meta_sz = 0;
	total_data_sz = 0;

	fseek(mf, 0, SEEK_SET);

	metric_count = 0;
	cpu_count = -1;
	do {
		int column;
		char *token;
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		/* Throw away first column which is the CPU 'name' */
		token = strtok(lbuf, " \t\n");
		/* NOTE: dont have to check for corner case null since breaking after CPUs */
		if (0 != strncmp(token, "cpu", 3))
			break;

		if ((cpu_count == -1) && 
		    (procstatutil_metrics_type != PROCSTATUTIL_METRICS_BOTH)){
			cpu_count++;
			continue;
		}

		column = 0;
		for (token = strtok(NULL, " \t\n"); token;
		     token = strtok(NULL, " \t\n")) {

			if ((cpu_count == -1) && 
			    (procstatutil_metrics_type == PROCSTATUTIL_METRICS_BOTH)){
				int len = (strlen(metric_name_fmt[column]) - ncoresuffix + 1) < PROCSTATUTIL_NAME_MAX ?
					(strlen(metric_name_fmt[column]) - ncoresuffix + 1) : PROCSTATUTIL_NAME_MAX;
				snprintf(metric_name, len, metric_name_fmt[column++]);
			} else {
				snprintf(metric_name, PROCSTATUTIL_NAME_MAX,
					 metric_name_fmt[column++], cpu_count);
			}
			rc = ldms_get_metric_size(metric_name,
						  LDMS_V_U64, &meta_sz,
						  &data_sz);
			if (rc)
				return rc;

			total_meta_sz += meta_sz;
			total_data_sz += data_sz;
			metric_count++;
		}
		column_count = column;
		cpu_count++;
	} while (1);

#ifdef CHECK_PROCSTATUTIL_TIMING
	rc = ldms_get_metric_size("procstatutil_tv_sec2", LDMS_V_U64, &meta_sz, &data_sz);
        total_meta_sz += meta_sz;
        total_data_sz += data_sz;

        rc = ldms_get_metric_size("procstatutil_tv_nsec2", LDMS_V_U64, &meta_sz, &data_sz);
        total_meta_sz += meta_sz;
        total_data_sz += data_sz;

        rc = ldms_get_metric_size("procstatutil_tv_dnsec", LDMS_V_U64, &meta_sz, &data_sz);
        total_meta_sz += meta_sz;
        total_data_sz += data_sz;
#endif

	/* Create a metric set of the required size */
	rc = ldms_create_set(path, total_meta_sz, total_data_sz, &set);
	if (rc)
		return rc;

	rc = ENOMEM;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;

	int cpu_no;
	ldms_metric_t m;
	int metric_no = 0;
	uint64_t cpu_comp_id = comp_id;
	
	if (procstatutil_metrics_type == PROCSTATUTIL_METRICS_BOTH){
		int column;
		for (column = 0; column < column_count; column++) {
			int len = (strlen(metric_name_fmt[column]) - ncoresuffix + 1) < PROCSTATUTIL_NAME_MAX ?
				(strlen(metric_name_fmt[column]) - ncoresuffix + 1) : PROCSTATUTIL_NAME_MAX;
			snprintf(metric_name, len, metric_name_fmt[column]);
			m = ldms_add_metric(set, metric_name, LDMS_V_U64);
			if (!m)
				goto err;
			ldms_set_user_data(m, cpu_comp_id);
			metric_table[metric_no++] = m;
		}
	}

	for (cpu_no = 0; cpu_no < cpu_count; cpu_no++) {
		int column;
		for (column = 0; column < column_count; column++) {
			snprintf(metric_name, PROCSTATUTIL_NAME_MAX,
				 metric_name_fmt[column], cpu_no);
			m = ldms_add_metric(set, metric_name, LDMS_V_U64);
			if (!m)
				goto err;
			ldms_set_user_data(m, cpu_comp_id);
			metric_table[metric_no++] = m;
		}
		cpu_comp_id++;
	}

#ifdef CHECK_PROCSTATUTIL_TIMING
	tv_sec_metric_handle2 = ldms_add_metric(set, "procstatutil_tv_sec2", LDMS_V_U64);
        if (!tv_sec_metric_handle2)
	  goto err;

        tv_nsec_metric_handle2 = ldms_add_metric(set, "procstatutil_tv_nsec2", LDMS_V_U64);
        if (!tv_nsec_metric_handle2)
	  goto err;

        tv_dnsec_metric_handle = ldms_add_metric(set, "procstatutil_tv_dnsec", LDMS_V_U64);
        if (!tv_nsec_metric_handle2)
	  goto err;
#endif

	return 0;
 err:
	ldms_destroy_set(set);
	return rc ;
}

/**
 * \brief Configuration
 *
 * Usage:
 * - config name=procstatutil component_id=<value> set=<value> metrics_type=<0/1>
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc = EINVAL;

	value = av_value(avl, "component_id");
	if (!value)
		goto out;
	comp_id = strtol(value, NULL, 0);

	value = av_value(avl,"metrics_type");
        if (value) {
                procstatutil_metrics_type = atoi(value);
                if ((procstatutil_metrics_type < PROCSTATUTIL_METRICS_CPU) ||
                    (procstatutil_metrics_type > PROCSTATUTIL_METRICS_BOTH)){
                        return EINVAL;
                }
        } else {
                procstatutil_metrics_type = PROCSTATUTIL_METRICS_CPU;
        }


	value = av_value(avl, "set");
	if (!value)
		goto out;
	rc = create_metric_set(value);
 out:
	return rc;
}

static int sample(void)
{
	int metric_no;
	char *s;
	char lbuf[PROCSTATUTIL_LINE_MAX];
	struct timespec time1;

#ifdef CHECK_PROCSTATUTIL_TIMING
	uint64_t beg_nsec; //testing
	union ldms_value vv;
#endif

	//	if (!set || !compid_metric_handle ){
	if (!set ){
		msglog(LDMS_LDEBUG,"procstatutil: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);

	clock_gettime(CLOCK_REALTIME, &time1);
#ifdef CHECK_PROCSTATUTIL_TIMING
        beg_nsec = time1.tv_nsec;
#endif


	fseek(mf, 0, SEEK_SET);


	/* Discard the first line that is a sum of the other cpu's values */
	if (procstatutil_metrics_type != PROCSTATUTIL_METRICS_BOTH){
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			return 0;
	}

	metric_no = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);

		char *token;
		if (!s)
			break;

		token = strtok(lbuf, " \t\n");
		/* First time have to check for corner case NULL (lbuf = "\n") */
		if (token == NULL)
			continue;

		if (0 != strncmp(token, "cpu", 3))
			continue; //get to EOF for seek to work

		for (token = strtok(NULL, " \t\n"); token;
				token = strtok(NULL, " \t\n")) {
			uint64_t v = strtoul(token, NULL, 0);
			ldms_set_u64(metric_table[metric_no], v);
			metric_no++;
		}
	} while (1);

#ifdef CHECK_PROCSTATUTIL_TIMING
	clock_gettime(CLOCK_REALTIME, &time1);
        vv.v_u64 = time1.tv_sec;
	ldms_set_metric(tv_sec_metric_handle2, &vv);
        vv.v_u64 = time1.tv_nsec;
	ldms_set_metric(tv_nsec_metric_handle2, &vv);
        vv.v_u64 = time1.tv_nsec - beg_nsec;
	ldms_set_metric(tv_dnsec_metric_handle, &vv);
#endif
	ldms_end_transaction(set);
	return 0;
}

static void term(void)
{
	if (set)
		ldms_destroy_set(set);
	set = NULL;
}


static const char *usage(void)
{
	return  "config name=procstatutil component_id=<comp_id> set=<setname>\n"
		"    comp_id     The component id value\n"
		"    setname     The set name\n";
}

static struct ldmsd_sampler procstatutil_plugin = {
	.base = {
		.name = "procstatutil",
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
	return &procstatutil_plugin.base;
}
