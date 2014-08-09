/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
 * \brief reads from sysfs the data that populates lm sensors (e.g., in*_input, fan*_input, temp*_input)
 *
 * NOTE: see srcinfo struct,maxvarspersrc vars and srcs var for defining the base files for your setup.
 * These vars will have to be changed and the code recompiled for your set up.
 * NOTE: there is an optional number (rangenum) in the filenames:
 * expecting files with name of path/vnamerangenumval_input OR path/vname_input if rangemin=rangemax
 * writes to variable name mnamerangenumval or mname if rangemin=rangemax
 *
 * For example:
 * define MAXVARS_PER_SRC 3
 *
 * struct srcinfo {
 * char* path;
 * char* vname[MAXVARS_PER_SRC];
 * char* mname[MAXVARS_PER_SRC];
 * int inputflag[MAXVARS_PER_SRC];
 * int ranges[MAXVARS_PER_SRC*2];
 * };
 *
 *
 * struct srcinfo srcs[] = {
 * {"/home/xxx", {"temp", "fan"}, {"tempa","fan"}, {1,0}, {0,1,0,4}},
 * {"/home/yyy", {"temp", "fanx", "cpu"}, {"tempb", "fanx", "CPU"}, {1,0,1}, {0,2,0,0,0,4}}
 * };
 * char** basesensorfilenames
 *
 * Results in:
 * /home/xxx/temp0_input -> tempa0
 * /home/xxx/fan0 -> fan0
 * /home/xxx/fan1 -> fan1
 * /home/xxx/fan2 -> fan2
 * /home/xxx/fan3 -> fan3
 * /home/yyy/temp0_input -> tempb0
 * /home/yyy/temp1_input -> tempb1
 * /home/yyy/fanx -> fanx
 * /home/yyy/cpu0_input -> CPU0
 * /home/yyy/cpu1_input -> CPU1
 * /home/yyy/cpu2_input -> CPU2
 * /home/yyy/cpu3_input -> CPU3
 *
 *
 * NOTE: data files have to be opened and closed on each file in sys for the data vaules to change.
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

#define MAXVARS_PER_SRC 5
#define MAXSENSORFILENAME 256

struct srcinfo {
	char* path;
	char* vname[MAXVARS_PER_SRC];
	char* mname[MAXVARS_PER_SRC];
	int inputflag[MAXVARS_PER_SRC];
	int ranges[MAXVARS_PER_SRC*2];
};


struct srcinfo srcs[] = {
	{"/sys/devices/pci0000:00/0000:00:01.1/i2c-1/1-002f/", {"in", "fan", "temp"}, {"in","fan", "temp"}, {1,1,1}, {0,9,1,9,1,6}}
};

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
char** sensorrawname;
int metric_count; //now global
ldmsd_msg_log_f msglog;
uint64_t comp_id;

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i, j, k;
	char metric_name[MAXSENSORFILENAME];
	int metric_no, metric_rawno;
	int nsrcs = sizeof(srcs)/sizeof(srcs[0]);

	tot_meta_sz = 0;
	tot_data_sz = 0;

	metric_count = 0;
	/* determine metric set size */
	for (i = 0; i < nsrcs; i++){
		for (j = 0; j < MAXVARS_PER_SRC; j++){
			if (srcs[i].vname[j] != NULL){
				int rangemin = srcs[i].ranges[j*2];
				int rangemax = srcs[i].ranges[j*2+1];
				if (rangemin == rangemax){ /* NO integer */
					snprintf(metric_name, MAXSENSORFILENAME, "%s", srcs[i].mname[j]);
					rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
					if (rc)
						return rc;

					tot_meta_sz += meta_sz;
					tot_data_sz += data_sz;
					metric_count++;
				} else {
					for (k = rangemin; k < rangemax; k++){ /* WITH integer */
						snprintf(metric_name, MAXSENSORFILENAME, "%s%d", srcs[i].mname[j],k);
						rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
						if (rc)
							return rc;

						tot_meta_sz += meta_sz;
						tot_data_sz += data_sz;
						metric_count++;
					}
				}
			}
		}
	}

	sensorrawname = (char**) malloc(metric_count*sizeof(char*));
	if (!sensorrawname){
		return ENOMEM;
	}

	/* Create the metric set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;

	/*
	 * Process again to define all the metrics.
	 */
	metric_no = 0;
	metric_rawno = -1;
	for (i = 0; i < nsrcs; i++){
		for (j = 0; j < MAXVARS_PER_SRC; j++){
			if (srcs[i].vname[j] != NULL){
				int rangemin = srcs[i].ranges[j*2];
				int rangemax = srcs[i].ranges[j*2+1];
				if (rangemin == rangemax){ /* NO integer */
					if (srcs[i].inputflag[j] == 1){
						snprintf(metric_name, MAXSENSORFILENAME,
							 "%s/%s_input", srcs[i].path, srcs[i].vname[j]);
					} else {
						snprintf(metric_name, MAXSENSORFILENAME, "%s/%s",
							 srcs[i].path, srcs[i].vname[j]);
					}
					sensorrawname[metric_no] = strdup(metric_name);
					if (!sensorrawname[metric_no]){
						rc = ENOMEM;
						goto err;
					}
					metric_rawno++;

					snprintf(metric_name, MAXSENSORFILENAME, "%s", srcs[i].mname[j]);
					metric_table[metric_no] =
						ldms_add_metric(set, metric_name, LDMS_V_U64);
					if (!metric_table[metric_no]) {
						rc = ENOMEM;
						goto err;
					}
					ldms_set_user_data(metric_table[metric_no], comp_id);
					metric_no++;
				} else {
					for (k = rangemin; k < rangemax; k++){ /* WITH integer */
						if (srcs[i].inputflag[j] == 1){
							snprintf(metric_name, MAXSENSORFILENAME,
								 "%s/%s%d_input", srcs[i].path, srcs[i].vname[j], k);
						} else {
							snprintf(metric_name, MAXSENSORFILENAME,
								 "%s/%s%d", srcs[i].path, srcs[i].vname[j], k);
						}
						sensorrawname[metric_no] = strdup(metric_name);
						if (!sensorrawname[metric_no]){
							rc = ENOMEM;
							goto err;
						}
						metric_rawno++;

						snprintf(metric_name, MAXSENSORFILENAME, "%s%d", srcs[i].mname[j],k);
						metric_table[metric_no] =
							ldms_add_metric(set, metric_name, LDMS_V_U64);
						if (!metric_table[metric_no]) {
							rc = ENOMEM;
							goto err;
						}
						ldms_set_user_data(metric_table[metric_no], comp_id);
						metric_no++;
					}
				}
			}
		}
	}

	return 0;

err:
	ldms_destroy_set(set);
	for (i = 0; i < metric_rawno; i++){
		free(sensorrawname[i]);
	}

	return rc;
}

/**
 * \brief Configuration
 *
 * Usage:
 * config name=procsensors component_id=<comp_id> set=<setname>
 *     comp_id     The component id value.
 *     setname     The set name.
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtol(value, NULL, 0);

	value = av_value(avl, "set");
	if (value)
		create_metric_set(value);

	return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	int rc, retrc;
	char *s;
	char lbuf[20];
	union ldms_value v;
	int i;

	rc = 0;
	retrc = 0;

	ldms_begin_transaction(set);
	for (i = 0; i < metric_count; i++){
		//FIXME: do we really want to open and close each one?
		mf = fopen(sensorrawname[i], "r");
		if (!mf) {
			msglog("Could not open the procsensors file '%s'...continuing\n",
			       sensorrawname[i]);
			retrc = ENOENT;
		} else {
			s = fgets(lbuf, sizeof(lbuf), mf);
			if (!s){
				if (mf) fclose(mf);
				break;
			}
			rc = sscanf(lbuf, "%"PRIu64 "\n", &v.v_u64);
			if (rc != 1){
				retrc = EINVAL;
				/* do not goto out - keep going */
			} else {
				ldms_set_metric(metric_table[i], &v);
			}
			if (mf) fclose(mf);
		}
	}

out:
	ldms_end_transaction(set);
	return retrc;
}

static void term(void)
{
	int i;

	ldms_destroy_set(set);
	for (i = 0; i < metric_count; i++){
		free(sensorrawname[i]);
	}
}

static const char *usage(void)
{
	return  "config name=procsensors component_id=<comp_id> set=<set>\n"
		"    comp_id    The component id.\n"
		"    set        The set name.\n";
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
