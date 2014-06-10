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
#include <sys/time.h>
#include <time.h>
#include "ldms.h"
#include "ldmsd.h"

#define fname "/proc/stat"

ldms_set_t set;
ldms_metric_t *metric_table;
ldms_metric_t *rate_metric_table;
uint64_t *prev_value;
ldmsd_msg_log_f msglog;
int column_count;
int cpu_count;
int is_reset_zero; /* 1 if the sampler couldn't read the file and set all values to 0 */

long USER_HZ; /* initialized in get_plugin() */
struct timeval _tv[2] = {0};
struct timeval *curr_tv = &_tv[0];
struct timeval *prev_tv = &_tv[1];

int numcpu_plusone;
uint64_t comp_id;

#undef CHECK_PROCSTATUTIL_TIMING
#ifdef CHECK_PROCSTATUTIL_TIMING
//Some temporary for testing
ldms_metric_t tv_sec_metric_handle2;
ldms_metric_t tv_nsec_metric_handle2;
ldms_metric_t tv_dnsec_metric_handle;
#endif

static ldms_set_t get_set()
{
	return set;
}

/*
 * Depending on the kernel version, not all of the rows will
 * necessarily be used. Since new columns are added at the end of the
 * line, this should work across all kernels up to 3.5.
 */
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

#define NUM_METRICS_PER_CORE 10

static char *rate_metric_name_fmt[] = {
	/* rate of above */
	"user.rate#%d",
	"nice.rate#%d",
	"sys.rate#%d",
	"idle.rate#%d",
	"iowait.rate#%d",
	"irq.rate#%d",
	"softirq.rate#%d",
	"steal.rate#%d",
	"guest.rate#%d",
	"guest_nice.rate#%d",
};

static int create_metric_set(const char *path)
{
	size_t meta_sz, total_meta_sz;
	size_t data_sz, total_data_sz;
	int rc;
	int metric_count;
	char *s;
	char lbuf[256];
	char metric_name[128];
	FILE *mf;

	column_count = 0;
	cpu_count = 0;

	mf = fopen(fname, "r");
	if (!mf) {
		msglog("procstatutil: Could not open the %s file.\n", fname);
		return ENOENT;
	}

	total_meta_sz = 0;
	total_data_sz = 0;

	fseek(mf, 0, SEEK_SET);

	/* Skip first line that is sum of remaining CPUs numbers */
	s = fgets(lbuf, sizeof(lbuf), mf);

	metric_count = 0;
	do {
		int column;
		char *token;
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		/* Throw away first column which is the CPU 'name' */
		token = strtok(lbuf, " \t\n");
		if (!token) {
			msglog("procstatutil: Failed to start. Unexpected format.\n");
			rc = EPERM;
			goto err;
		}

		if (0 != strncmp(token, "cpu", 3))
			break;

		column = 0;
		for (token = strtok(NULL, " \t\n"); token;
		     token = strtok(NULL, " \t\n")) {
			/* raw */
			sprintf(metric_name, metric_name_fmt[column], cpu_count);
			rc = ldms_get_metric_size(metric_name,
						  LDMS_V_U64, &meta_sz,
						  &data_sz);
			if (rc)
				return rc;

			total_meta_sz += meta_sz;
			total_data_sz += data_sz;

			/* rate */
			sprintf(metric_name, rate_metric_name_fmt[column], cpu_count);
			rc = ldms_get_metric_size(metric_name,
						  LDMS_V_F, &meta_sz,
						  &data_sz);
			if (rc)
				return rc;

			total_meta_sz += meta_sz;
			total_data_sz += data_sz;

			column++;
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
		goto err0;

	prev_value = calloc(metric_count, sizeof(*prev_value));
	if (!prev_value)
		goto err1;

	rate_metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!rate_metric_table)
		goto err2;

	int cpu_no;
	ldms_metric_t m;
	int metric_no = 0;
	uint64_t cpu_comp_id = comp_id;
	for (cpu_no = 0; cpu_no < cpu_count; cpu_no++) {
		int column;
		for (column = 0; column < column_count; column++) {
			/* raw counter */
			sprintf(metric_name, metric_name_fmt[column], cpu_no);
			m = ldms_add_metric(set, metric_name, LDMS_V_U64);
			if (!m)
				goto err3;
			ldms_set_user_data(m, cpu_comp_id);
			metric_table[metric_no] = m;
			/* rate */
			sprintf(metric_name, rate_metric_name_fmt[column], cpu_no);
			m = ldms_add_metric(set, metric_name, LDMS_V_F);
			if (!m)
				goto err3;
			ldms_set_user_data(m, cpu_comp_id);
			rate_metric_table[metric_no] = m;
			metric_no++;
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

        fclose(mf);
	return 0;
err3:
	free(rate_metric_table);
err2:
	free(prev_value);
err1:
	free(metric_table);
err0:
	ldms_set_release(set);
err:
	fclose(mf);
	return rc ;
}

/**
 * \brief Configuration
 *
 * Usage:
 * - config name=procstatutil component_id=<value> set=<value>
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc = EINVAL;

	is_reset_zero = 0;

	value = av_value(avl, "component_id");
	if (!value)
		goto out;
	comp_id = strtol(value, NULL, 0);

	value = av_value(avl, "set");
	if (!value)
		goto out;
	rc = create_metric_set(value);
 out:
	return rc;
}

static int sample(void)
{
	int metric_no = 0;
	int column = 0;
	char *s;
	char lbuf[256];
	union ldms_value vv;
	struct timeval diff_tv;
	struct timeval *tmp_tv;
	float dt;

	FILE *mf = 0;

#ifdef CHECK_PROCSTATUTIL_TIMING
	struct timespec time1;
	uint64_t beg_nsec; //testing
#endif

	if (!set ){
		msglog("procstatutil: plugin not initialized\n");
		return EINVAL;
	}

	ldms_begin_transaction(set);
	gettimeofday(curr_tv, NULL);
	timersub(curr_tv, prev_tv, &diff_tv);
	dt = diff_tv.tv_sec + diff_tv.tv_usec / 1e06;

#ifdef CHECK_PROCSTATUTIL_TIMING
	clock_gettime(CLOCK_REALTIME, &time1);
        beg_nsec = time1.tv_nsec;
#endif

	mf = fopen(fname, "r");
	if (!mf) {
		msglog("procstatutil: Couldn't open the file %s\n", fname);
		goto reset_to_0;
	}

	fseek(mf, 0, SEEK_SET);

	/* Discard the first line that is a sum of the other cpu's values */
	s = fgets(lbuf, sizeof(lbuf), mf);
	if (!s)
		return 0;

	metric_no = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);

		char *token;
		if (!s)
			break;

		column = 0;
		token = strtok(lbuf, " \t\n");
		if (!token) {
			if (metric_no < cpu_count * column_count) {
				/*
				 * The /proc/stat isn't in the expected format.
				 * Stop the reading and set all values to 0
				 * because the metric source is untrustworthy.
				 */
				if (is_reset_zero == 0) {
					msglog("procstatutil: Wrong format.\n");
					is_reset_zero = 1;
				}

				goto reset_to_0;
			}
			break;
		}

		if (0 != strncmp(token, "cpu", 3))
			continue; //get to EOF for seek to work

		for (token = strtok(NULL, " \t\n"); token;
				token = strtok(NULL, " \t\n")) {
			uint64_t v = strtoul(token, NULL, 0);
			uint64_t dv;
			if (prev_value[metric_no] == 0 ||
					prev_value[metric_no] > v)
				/* for some reasons, the counters can decrease */
				dv = 0;
			else
				dv = v - prev_value[metric_no];
			float rate = (dv * 1.0 / USER_HZ) / dt * 100.0;
			ldms_set_u64(metric_table[metric_no], v);
			ldms_set_float(rate_metric_table[metric_no], rate);
			prev_value[metric_no] = v;
			metric_no++;
			column++;
		}

		if (column < column_count) {
			if (is_reset_zero == 0) {
				msglog("procstatutil: Wrong format.\n");
				is_reset_zero = 1;
			}
			goto reset_to_0;
		}

	} while (1);
	fclose(mf);
#ifdef CHECK_PROCSTATUTIL_TIMING
	clock_gettime(CLOCK_REALTIME, &time1);
        vv.v_u64 = time1.tv_sec;
	ldms_set_metric(tv_sec_metric_handle2, &vv);
        vv.v_u64 = time1.tv_nsec;
	ldms_set_metric(tv_nsec_metric_handle2, &vv);
        vv.v_u64 = time1.tv_nsec - beg_nsec;
	ldms_set_metric(tv_dnsec_metric_handle, &vv);
#endif
	tmp_tv = curr_tv;
	curr_tv = prev_tv;
	prev_tv = tmp_tv;
	ldms_end_transaction(set);
	if (is_reset_zero == 1) {
		msglog("procstatutil: values obtained.\n");
		is_reset_zero = 0;
	}
	return 0;

reset_to_0:
	/* The set state is inconsistent. */
	if (mf)
		fclose(mf);
	for (metric_no = 0; metric_no < cpu_count * column_count; metric_no++) {
		ldms_set_u64(metric_table[metric_no], 0);
		ldms_set_float(rate_metric_table[metric_no], 0);
		prev_value[metric_no] = 0;
	}
	tmp_tv = curr_tv;
	curr_tv = prev_tv;
	prev_tv = tmp_tv;
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
	USER_HZ = sysconf(_SC_CLK_TCK);
	return &procstatutil_plugin.base;
}
