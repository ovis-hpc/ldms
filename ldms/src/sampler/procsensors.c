/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012-2015 Sandia Corporation. All rights reserved.
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
 * \file procsensors.c
 * \brief reads from proc the data that populates lm sensors (in*_input, fan*_input, temp*_input)
 *
 * NOTE: data files have to be opened and closed on each file in sys for the data vaules to change.
 * The actual functionality of the data gathering by the system takes time on systems.
 * Sample stores the data locally and then writes it out so that the collection will not occur during
 * partial set. There is therefore some slop in the actaul time for the data point.
 *
 * filename will be the variable name. mysql inserter will have to convert names and downselect which ones
 * to record.
 *
 * FIXME: decideif multipliers should go here....
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
#include <pthread.h>
#include "ldms.h"
#include "ldmsd.h"

//FIXME: make this a parameter later..
static char* procsensorsfiledir = "/sys/devices/pci0000:00/0000:00:01.1/i2c-1/1-002f/";
const static int vartypes = 3;
const static char* varnames[] = {"in", "fan", "temp"};
const static int varbounds[] = {0, 9, 1, 9, 1, 6};
static uint64_t counter;
static ldms_set_t set;
static ldms_schema_t schema;
static int metric_count; /* now global */
static uint64_t* metric_values;
static uint64_t* metric_times;
static int num_metric_times;
static ldmsd_msg_log_f msglog;
static char *producer_name;

#undef CHECK_SENSORS_TIMING
#ifdef CHECK_SENSORS_TIMING
/* Some temporary for testing x ref with metric_times */
int tv_sec_metric_handle2;
int tv_nsec_metric_handle2;
int tv_dnsec_metric_handle;
int tv_sec_metric_handle3;
int tv_nsec_metric_handle3;
int tv_dnwrite_metric_handle;
#endif

static int create_metric_set(const char *instance_name)
{
	int rc, i, j;
	char metric_name[128];

	/* Create the metric set */
	schema = ldms_schema_new("procsensors");
	if (!schema)
		return ENOMEM;

	/*
	 * Process file to define all the metrics.
	 */

	int metric_no = 0;
	for (i = 0; i < vartypes; i++){
		for (j = varbounds[2 * i]; j <= varbounds[2 * i + 1]; j++){
			snprintf(metric_name, 127,
					"%s%d_input",varnames[i],j);
			rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
			if (rc < 0) {
				rc = ENOMEM;
				goto err;
			}
		}
	}

#ifdef CHECK_SENSORS_TIMING
	num_metric_times = 4;
	tv_sec_metric_handle2 = ldms_schema_metric_add(schema, "procsensors_tv_sec2", LDMS_V_U64);
	if (tv_sec_metric_handle2 < 0) {
		rc = ENOMEM;
		goto err;
	}

	tv_nsec_metric_handle2 = ldms_schema_metric_add(schema, "procsensors_tv_nsec2", LDMS_V_U64);
	if (tv_nsec_metric_handle2 < 0) {
		rc = ENOMEM;
		goto err;
	}

	tv_dnsec_metric_handle = ldms_schema_metric_add(schema, "procsensors_tv_dnsec", LDMS_V_U64);
	if (tv_dnsec_metric_handle < 0) {
		rc = ENOMEM;
		goto err;
	}

	tv_sec_metric_handle3 = ldms_schema_metric_add(schema, "procsensors_tv_sec3", LDMS_V_U64);
	if (tv_sec_metric_handle3 < 0) {
		rc = ENOMEM;
		goto err;
	}

	tv_nsec_metric_handle3 = ldms_schema_metric_add(schema, "procsensors_tv_nsec3", LDMS_V_U64);
	if (tv_nsec_metric_handle3 < 0) {
		rc = ENOMEM;
		goto err;
	}

	tv_dnwrite_metric_handle = ldms_schema_metric_add(schema, "procsensors_tv_dnwrite", LDMS_V_U64);
	if (tv_dnwrite_metric_handle < 0) {
		rc = ENOMEM;
		goto err;
	}
#endif

	metric_values = calloc(metric_count, sizeof(uint64_t));
	if (!metric_values)
		goto err;

	metric_times = calloc(num_metric_times, sizeof(uint64_t));
	if (!metric_times)
		goto err;

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

err:
	ldms_schema_delete(schema);
	schema = NULL;
	return rc;
}

/**
 * \brief Configuration
 *
 * Usage:
 * config name=procsensors producer=<prod_name> instance=<inst_name>
 *     <prod_name>       The producer name
 *     <inst_name>       The instance name
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc;
	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog("procsensors: missing 'producer'\n");
		return ENOENT;
	}

	value = av_value(avl, "instance");
	if (!value) {
		msglog("procsensors: missing 'instance'\n");
		return ENOENT;
	}
	rc = create_metric_set(value);
	if (rc) {
		msglog("procsensors: failed to create the metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);

	return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	int rc;
	int metric_no;
	char *s;
	char procfile[256];
	char lbuf[20];
	union ldms_value v;
	struct timespec time1;
	int i, j;
	FILE *mf;

	//set the counter
	uint64_t counterval = ++counter;

	int metric_time_no = 0;
	clock_gettime(CLOCK_REALTIME, &time1);
	metric_times[metric_time_no++] = time1.tv_sec;
	metric_times[metric_time_no++] = time1.tv_nsec;
	ldms_transaction_begin(set);
	metric_no = 0;
	for (i = 0; i < vartypes; i++){
		for (j = varbounds[2*i]; j <= varbounds[2*i+1]; j++){
			snprintf(procfile, 255, "%s/%s%d_input",
					procsensorsfiledir, varnames[i],j);

			//FIXME: do we really want to open and close each one?
			mf = fopen(procfile, "r");
			if (!mf) {
				msglog("Could not open the procsensors file "
						"'%s'...exiting\n", procfile);
				rc = ENOENT;
				goto out;
			}
			s = fgets(lbuf, sizeof(lbuf), mf);
			if (!s){
				fclose(mf);
				break;
			}
			rc = sscanf(lbuf, "%"PRIu64 "\n", &metric_values[metric_no]);
			if (rc != 1){
				fclose(mf);
				rc = EINVAL;
				goto out;
			}

			metric_no++;
			fclose(mf);
		}
	}

#ifdef CHECK_SENSORS_TIMING
	clock_gettime(CLOCK_REALTIME, &time1);
	metric_times[metric_time_no++] = time1.tv_sec;
	metric_times[metric_time_no++] = time1.tv_nsec;
#endif

	/* now do the writeout */

	/* metrics */
	metric_no = 0;
	for (i = 0; i < metric_count; i++){
		v.v_u64 = metric_values[i];
		ldms_metric_set(set , i, &v);
	}

#ifdef CHECK_SENSORS_TIMING
	//second set of times
	v.v_u64 = metric_times[2];
	ldms_metric_set(set, tv_sec_metric_handle2, &v);
	v.v_u64 = metric_times[3];
	ldms_metric_set(set, tv_nsec_metric_handle2, &v);
	v.v_u64 = metric_times[3]-metric_times[1];  //sub start of writeout nsec
	ldms_metric_set(set, tv_dnsec_metric_handle, &v);

	//and get the last write times. array storage for these is unused
	clock_gettime(CLOCK_REALTIME, &time1);
	v.v_u64 = time1.tv_sec;
	ldms_metric_set(set, tv_sec_metric_handle3, &v);
	v.v_u64 = time1.tv_nsec;
	ldms_metric_set(set, tv_nsec_metric_handle3, &v);
	v.v_u64 = time1.tv_nsec-metric_times[3];  //sub start of writeout nsec
	ldms_metric_set(set, tv_dnwrite_metric_handle, &v);
#endif
	rc = 0;
out:
	ldms_transaction_end(set);
	return rc;
}

static void term(void)
{
	if (schema)
		ldms_schema_delete(schema);
	if (set)
		ldms_set_delete(set);
}

static const char *usage(void)
{
	return  "config name=procsensors producer=<prod_name> instance=<inst_name>\n"
		"    <prod_name>    The producer name\n"
		"    <inst_name>    The instance name\n";
}

static struct ldmsd_sampler procsensors_plugin = {
	.base = {
		.name = "procsensors",
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
	return &procsensors_plugin.base;
}
