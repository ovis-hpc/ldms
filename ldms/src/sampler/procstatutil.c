/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010-2015 Sandia Corporation. All rights reserved.
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

static ldms_set_t set;
static ldms_schema_t schema;
static uint64_t *prev_value;
static ldmsd_msg_log_f msglog;
static int column_count;
static int cpu_count;
static char *producer_name;

static long USER_HZ; /* initialized in get_plugin() */
static struct timeval _tv[2] = { {0}, {0} };
static struct timeval *curr_tv = &_tv[0];
static struct timeval *prev_tv = &_tv[1];



#undef CHECK_PROCSTATUTIL_TIMING
#ifdef CHECK_PROCSTATUTIL_TIMING
//Some temporary for testing
ldms_metric_t tv_sec_metric_handle2;
ldms_metric_t tv_nsec_metric_handle2;
ldms_metric_t tv_dnsec_metric_handle;
#endif

static ldms_set_t get_set(struct ldmsd_sampler *self)
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

static int create_metric_set(const char *instance_name)
{
	int rc;
	char *s;
	char lbuf[256];
	char metric_name[128];
	FILE *mf;

	schema = ldms_schema_new("procstatutil");
	if (!schema)
		return ENOMEM;

	column_count = 0;
	cpu_count = 0;

	mf = fopen(fname, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "procstatutil: Could not open "
				"the %s file.\n", fname);
		goto err;
	}

	rc = ENOMEM;
	char *token, *ptr;
	fseek(mf, 0, SEEK_SET);

	/* Skip first line that is sum of remaining CPUs numbers */
	s = fgets(lbuf, sizeof(lbuf), mf);

	s = fgets(lbuf, sizeof(lbuf), mf);
	while (s && (0 == strncmp(s, "cpu", 3))) {
		column_count = 0;
		/* Throw away the first column which is the CPU 'name' */
		token = strtok_r(lbuf, " \t\n", &ptr);
		if (!token) {
			msglog(LDMSD_LERROR, "procstatutil: Failed to start. "
					"Unexpected format.\n");
			rc = EPERM;
			goto err0;
		}

		for (token = strtok_r(NULL, " \t\n", &ptr); token;
				token = strtok_r(NULL, " \t\n", &ptr)) {
			/* raw counter */
			sprintf(metric_name, metric_name_fmt[column_count],
								cpu_count);
			rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
			if (rc < 0)
				goto err0;

			/* rate */
			sprintf(metric_name, rate_metric_name_fmt[column_count],
								cpu_count);
			rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_F32);
			if (rc < 0)
				goto err0;
			column_count++;
		}
		cpu_count++;
		s = fgets(lbuf, sizeof(lbuf), mf);
	}

#ifdef CHECK_PROCSTATUTIL_TIMING
	tv_sec_metric_handle2 = ldms_schema_metric_add(set, "procstatutil_tv_sec2", LDMS_V_U64);
        if (!tv_sec_metric_handle2)
	  goto err0;

        tv_nsec_metric_handle2 = ldms_schema_metric_add(set, "procstatutil_tv_nsec2", LDMS_V_U64);
        if (!tv_nsec_metric_handle2)
	  goto err0;

        tv_dnsec_metric_handle = ldms_schema_metric_add(set, "procstatutil_tv_dnsec", LDMS_V_U64);
        if (!tv_nsec_metric_handle2)
	  goto err0;
#endif

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err0;
	}

	prev_value = calloc(column_count * cpu_count, sizeof(*prev_value));
	if (!prev_value)
		goto err1;

        fclose(mf);
	return 0;
err1:
	ldms_set_delete(set);
	set = NULL;
err0:
	fclose(mf);
err:
	ldms_schema_delete(schema);
	schema = NULL;
	return rc ;
}

/**
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc = EINVAL;

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "procstatutil: missing 'producer'\n");
		return ENOENT;
	}

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, "procstatutil: missing 'instance'\n");
		return ENOENT;
	}

	if (set) {
		msglog(LDMSD_LERROR, "procstatutil: Set already created.\n");
		return EINVAL;
	}
	rc = create_metric_set(value);
	if (rc) {
		msglog(LDMSD_LERROR, "procstatutil: failed to create the metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);
	return rc;
}

static int sample(struct ldmsd_sampler *self)
{
	int raw_metric_no = 0;
	int metric_idx = 0;
	int column = 0;
	char *s;
	char lbuf[256];
	struct timeval diff_tv;
	struct timeval *tmp_tv;
	float dt;
	FILE *mf = 0;

#ifdef CHECK_PROCSTATUTIL_TIMING
	struct timespec time1;
	uint64_t beg_nsec; //testing
#endif

	if (!set ){
		msglog(LDMSD_LDEBUG, "procstatutil: plugin not initialized\n");
		return EINVAL;
	}

	ldms_transaction_begin(set);
	gettimeofday(curr_tv, NULL);
	timersub(curr_tv, prev_tv, &diff_tv);
	dt = diff_tv.tv_sec + diff_tv.tv_usec / 1e06;

#ifdef CHECK_PROCSTATUTIL_TIMING
	clock_gettime(CLOCK_REALTIME, &time1);
        beg_nsec = time1.tv_nsec;
#endif

	mf = fopen(fname, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "procstatutil: Error %d: Couldn't "
				"open the file %s\n", errno, fname);
		goto reset_to_0;
	}

	fseek(mf, 0, SEEK_SET);

	/* Discard the first line that is a sum of the other cpu's values */
	s = fgets(lbuf, sizeof(lbuf), mf);
	if (!s) {
		fclose(mf);
		return 0;
	}

	raw_metric_no = 0;
	s = fgets(lbuf, sizeof(lbuf), mf);
	while (s && (0 == strncmp(s, "cpu", 3))) {
		char *token;
		if (!s)
			break;

		column = 0;
		token = strtok(lbuf, " \t\n");
		for (token = strtok(NULL, " \t\n"); token;
				token = strtok(NULL, " \t\n")) {
			uint64_t v = strtoul(token, NULL, 0);
			/* raw data */
			ldms_metric_set_u64(set, metric_idx++, v);

			/* rate data */
			uint64_t dv;
			if (prev_value[raw_metric_no] == 0 ||
					prev_value[raw_metric_no] > v)
				/* for some reasons, the counters can decrease */
				dv = 0;
			else
				dv = v - prev_value[raw_metric_no];
			float rate = (dv * 1.0 / USER_HZ) / dt * 100.0;
			ldms_metric_set_float(set, metric_idx++, rate);
			prev_value[raw_metric_no] = v;
			raw_metric_no++;
			column++;
		}
		s = fgets(lbuf, sizeof(lbuf), mf);
	}
	fclose(mf);
#ifdef CHECK_PROCSTATUTIL_TIMING
	clock_gettime(CLOCK_REALTIME, &time1);
        vv.v_u64 = time1.tv_sec;
	ldms_metric_set(set, tv_sec_metric_handle2, &vv);
        vv.v_u64 = time1.tv_nsec;
	ldms_metric_set(set, tv_nsec_metric_handle2, &vv);
        vv.v_u64 = time1.tv_nsec - beg_nsec;
	ldms_metric_set(set, tv_dnsec_metric_handle, &vv);
#endif
	tmp_tv = curr_tv;
	curr_tv = prev_tv;
	prev_tv = tmp_tv;
	ldms_transaction_end(set);
	return 0;

reset_to_0:
	/* The set state is inconsistent. */
	if (mf)
		fclose(mf);
	metric_idx = 0;
	for (raw_metric_no = 0; raw_metric_no < cpu_count * column_count; raw_metric_no++) {
		ldms_metric_set_u64(set, metric_idx++, 0);
		ldms_metric_set_float(set, metric_idx++, 0);
		prev_value[raw_metric_no] = 0;
	}
	tmp_tv = curr_tv;
	curr_tv = prev_tv;
	prev_tv = tmp_tv;
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}


static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=procstatutil producer=<prod_name> instance=<inst_name>\n"
		"    <prod_name>     The producer name\n"
		"    <inst_name>     The instance name\n";
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
	set = NULL;
	USER_HZ = sysconf(_SC_CLK_TCK);
	return &procstatutil_plugin.base;
}
