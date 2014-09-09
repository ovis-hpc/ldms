/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
 * \file sampler_atasmart.c
 * \brief Collect S.M.A.R.T. attribute values.
 *
 * The sampler uses \c libatasmart to collect the S.M.A.R.T metrics.
 *
 * For each attribute, all values are, except the raw value, collected by
 * the sampler. The metric name format is ID_name,
 * where 'ID' is the attribute ID and 'name' is the attribute name.
 * The invalid values are collected as -1.
 */
#define _GNU_SOURCE
#include <assert.h>
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
#include <malloc.h>
#include <atasmart.h>
#include "ldms.h"
#include "ldmsd.h"

#define NFIELD 8
static char *fieldname[NFIELD] = {
	"Value", "Worst", "Threshold", "Pretty",
	"Type", "Updates", "Good", "Good_Past"
};

struct ldms_atasmart {
	SkDisk *d;
};

struct atatsmart_set_size {
	size_t tot_meta_sz;
	size_t tot_data_sz;
	int metric_count;
};

static ldms_set_t set;
static ldms_metric_t *metric_table;
static ldmsd_msg_log_f msglog;
static uint64_t comp_id;
static char **disknames;
static int num_disks;

struct ldms_atasmart *smarts;

int atasmart_get_disk_info(SkDisk *d, const SkSmartAttributeParsedData *a,
				void *userdata)
{
	int rc;
	char name_base[128];
	char metric_name[128];
	struct atatsmart_set_size *sz = (struct atatsmart_set_size *) userdata;
	size_t meta_sz, data_sz;
	sprintf(name_base, "%s", a->name);
	int i;
	for (i = 0; i < NFIELD; i++) {
		sprintf(metric_name, "%s#%s", fieldname[i], name_base);
		switch (i) {
		/* If the value is invalid, the metric value is -1 */
		case 0: /* current value */
		case 1: /* worst value */
		case 2: /* threshold */
			rc = ldms_get_metric_size(metric_name, LDMS_V_S16,
					&meta_sz, &data_sz);
			break;
		case 3: /* Pretty value */
			rc = ldms_get_metric_size(metric_name, LDMS_V_U64,
					&meta_sz, &data_sz);
			break;
		/* the value is 1, 0. */
		case 4: /* prefail/old-age */
		case 5: /* online/offline */
			rc = ldms_get_metric_size(metric_name, LDMS_V_U8,
					&meta_sz, &data_sz);
			break;
		/* the value is 1, 0, -1 for invalid */
		case 6: /* good now */
		case 7: /* good in the past */
			rc = ldms_get_metric_size(metric_name, LDMS_V_S8,
					&meta_sz, &data_sz);
			break;
		default:
			assert(0);
		}

		if (rc)
			return rc;

		sz->tot_meta_sz += meta_sz;
		sz->tot_data_sz += data_sz;
		sz->metric_count++;
	}
	return 0;
}

int atasmart_add_metric(SkDisk *d, const SkSmartAttributeParsedData *a,
				void *userdata)
{
	int rc;
	char name_base[128];
	char metric_name[128];
	int *metric_no = (int *) userdata;
	sprintf(name_base, "%s", a->name);
	int comp_id_increment = (*metric_no / NFIELD);

	ldms_metric_t m;
	int i;
	for (i= 0; i < NFIELD; i++) {
		sprintf(metric_name, "%s#%s", fieldname[i], name_base);
		switch (i) {
		/* If the value is invalid, the metric value is -1 */
		case 0: /* Current value */
		case 1: /* Worst value */
		case 2: /* Threshold */
			m = ldms_add_metric(set, metric_name, LDMS_V_S16);
			break;
		case 3: /* Pretty */
			m = ldms_add_metric(set, metric_name, LDMS_V_U64);
			break;
		/* The value is either 0 or 1 */
		case 4: /* prefail/old-age */
		case 5: /* online/offline */
			m = ldms_add_metric(set, metric_name, LDMS_V_U8);
			break;
		/* The value is 0, 1 or -1 for invalid. */
		case 6: /* good now: yes/ (n/a) */
		case 7: /* good in the past: yes/(n/a) */
			m = ldms_add_metric(set, metric_name, LDMS_V_S8);
			break;
		default:
			assert(0);
		}
		if (!m)
			return ENOMEM;
		ldms_set_user_data(m, comp_id + comp_id_increment);
		metric_table[*metric_no] = m;
		(*metric_no)++;
	}
	return 0;
}

static int create_metric_set(char *setname)
{
	int ret;
	struct atatsmart_set_size size;
	size.metric_count = size.tot_data_sz = size.tot_meta_sz = 0;
	/* Create the array of SkDisks */
	smarts = calloc(num_disks, sizeof(struct ldms_atasmart));
	if (!smarts)
		return ENOMEM;

	/* Get the total meta and data sizes */
	int i = 0;
	for (i = 0; i < num_disks; i++) {
		if (ret = sk_disk_open(disknames[i], &smarts[i].d)) {
			msglog(LDMS_LDEBUG,"atasmart: Failed to create SkDisk '%s'."
					"Error %d.\n", disknames[i], ret);
			goto err0;
		}

		if (ret = sk_disk_smart_read_data(smarts[i].d)) {
			msglog(LDMS_LDEBUG,"atasmart: Failed to read data SkDisk '%s'."
					"Error %d.\n", disknames[i], ret);
			goto err0;
		}

		ret = sk_disk_smart_parse_attributes(smarts[i].d,
				atasmart_get_disk_info, (void *) &size);
		if (ret) {
			msglog(LDMS_LDEBUG,"atasmart: Failed to get size of SkDisk '%s'."
					"Error %d.\n", disknames[i], ret);
			goto err0;
		}
	}

	/* Allocate memory for the set */
	ret = ldms_create_set(setname, size.tot_meta_sz,
			size.tot_data_sz, &set);
	if (ret)
		goto err0;

	metric_table = calloc(size.metric_count, sizeof(ldms_metric_t));
	if (!metric_table) {
		ret = ENOMEM;
		goto err1;
	}

	/* Add metrics to the set */
	int metric_no = 0;
	for (i = 0; i < num_disks; i++) {
		ret = sk_disk_smart_parse_attributes(smarts[i].d,
			(SkSmartAttributeParseCallback)atasmart_add_metric,
			(void *) &metric_no);
		if (ret) {
			msglog(LDMS_LDEBUG,"atasmart: Failed to add metric. SkDisk '%s'."
					"Error %d.\n", disknames[i], ret);
			goto err2;
		}
	}
	return 0;
err2:
	free(metric_table);
err1:
	ldms_destroy_set(set);
err0:
	i = 0;
	while (i < num_disks && smarts[i].d) {
		sk_disk_free(smarts[i].d);
		i++;
	}
	free(smarts);
	return ret;
}


static const char *usage(void)
{
	return  "config name=sampler_atasmart component_id=<comp_id> \n"
		"	set=<setname> disks=<disknames>\n"
		"    comp_id     The component id value.\n"
		"    setname     The set name.\n"
		"    disknames   The comma-separated list of disk names,\n"
		"		 e.g., /dev/sda,/dev/sda1.\n";
}

/**
 * \brief Configuration
 *
 * config name=sampler_atasmart component_id=<comp_id>
 * 	  set=<setname> disks=<disknames>
 *     comp_id     The component id value.
 *     setname     The set name.
 *     disknames   The comma-separated list of disk names,
 *     		   e.g., /dev/sda,/dev/sda1.
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *s;
	char *tmp;
	num_disks = 0;

	value = av_value(avl, "disks");
	if (value) {
		s = strdup(value);
		tmp = strtok(s, ",");
		while (tmp) {
			num_disks++;
			tmp = strtok(NULL, ",");
		}
		disknames = malloc(num_disks * sizeof(char *));
		free(s);
		tmp = strtok(value, ",");
		int i = 0;
		while (tmp) {
			disknames[i] = strdup(tmp);
			tmp = strtok(NULL, ",");
			i++;
		}
	} else {
		return -1;
	}

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtoull(value, NULL, 0);
	else
		return -1;

	value = av_value(avl, "set");
	if (value)
		return create_metric_set(value);
	else
		return -1;
}

static ldms_set_t get_set()
{
	return set;
}

int atasmart_set_metric(SkDisk *d, SkSmartAttributeParsedData *a,
				void *userdata)
{
	union ldms_value v;
	char name_base[128];

	int *metric_no = (int *) userdata;
	int i;
	sprintf(name_base, "%s", a->name);
	for (i = 0; i < NFIELD; i++) {
		switch (i) {
		/* If the value is invalid, the metric value is -1 */
		case 0: /* Current value */
			v.v_s16 = a->current_value_valid ?
					a->current_value : -1;
			break;
		case 1: /* Worst value */
			v.v_s16 = a->worst_value_valid ? a->worst_value : -1;
			break;
		case 2: /* Threshold */
			v.v_s16 = a->threshold_valid ? a->threshold : -1;
			break;
		case 3: /* Pretty */
			v.v_u64 = a->pretty_value;
			break;
		/* The value is either 0 or 1 */
		case 4: /* prefail/old-age */
			v.v_u8 = a->prefailure;
			break;
		case 5: /* online/offline */
			v.v_u8 = a->online;
			break;
		/* The value is 0, 1 or -1. */
		case 6: /* good now: yes/ (n/a) */
			v.v_s8 = a->good_now_valid ? a->good_now : -1;
			break;
		case 7: /* good in the past: yes/(n/a) */
			v.v_s8 = a->good_in_the_past_valid ?
					a->good_in_the_past : -1;
			break;
		default:
			assert(0);
		}
		ldms_set_metric(metric_table[*metric_no], &v);
		(*metric_no)++;
	}
	return 0;
}

static int sample(void)
{
	int ret;
	int metric_no;

	if (!set) {
		msglog(LDMS_LDEBUG,"atasmart: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);

	metric_no = 0;
	int i;
	for (i = 0; i < num_disks; i++) {
		ret = sk_disk_smart_parse_attributes(smarts[i].d,
			(SkSmartAttributeParseCallback)atasmart_set_metric,
			&metric_no);
		if (ret) {
			msglog(LDMS_LDEBUG,"atasmart: Failed to get metric. SkDisk '%s'."
					" Error %d\n", disknames[i], ret);
			goto err;
		}
	}

err:
	ldms_end_transaction(set);
	return ret;
out:
	ldms_end_transaction(set);
	return 0;
}

static void term(void)
{
	int i;
	for (i = 0; i < num_disks; i++) {
		sk_disk_free(smarts[i].d);
		free(disknames[i]);
	}
	free(smarts);
	free(metric_table);
	free(disknames);

	if (set)
		ldms_destroy_set(set);
	set = NULL;
}


static struct ldmsd_sampler sampler_atasmart_plugin = {
	.base = {
		.name = "sampler_atasmart",
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
	return &sampler_atasmart_plugin.base;
}
