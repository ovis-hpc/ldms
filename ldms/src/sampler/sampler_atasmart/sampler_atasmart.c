/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2018 Open Grid Computing, Inc. All rights reserved.
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
#include "ldmsd_plug_api.h"
#include "sampler_base.h"

#define NFIELD 9
static char *fieldname[NFIELD] = {
	"Value", "Worst", "Threshold", "Pretty",
	"Prefailure", "Online", "Good_now", "Good_past",
	"Flag"
};

struct ldms_atasmart {
	ldms_schema_t schema;
	SkDisk **d;
	int metric_no;
	int curr_disk_no;
};

struct atatsmart_set_size {
	size_t tot_meta_sz;
	size_t tot_data_sz;
	int metric_count;
	int disk_no;
};

static ovis_log_t mylog;

static ldms_set_t set;
static char **disknames;
static int num_disks;
#define SAMP "sampler_atasmart"
static int metric_offset;
static base_data_t base;

struct ldms_atasmart *smarts;

#define MAX_METRIC_NAME_LEN 256

int get_metric_name(char *metric_name, char *fieldname,
					char *namebase, char *diskname)
{
	char *disk = strrchr(diskname, '/');
	if (!disk)
		disk = diskname;
	else
		disk = disk + 1;

	/* + 1 for the hash character '#' */
	size_t mname_len = strlen(fieldname) + strlen(namebase)
						+ strlen(disk) + 1;
	if (mname_len >= MAX_METRIC_NAME_LEN)
		return -1;

	sprintf(metric_name, "%s_%s#%s", fieldname, namebase, disk);
	return 0;
}

void atasmart_get_disk_info(SkDisk *d, const SkSmartAttributeParsedData *a,
				void *userdata)
{
	int rc;
	char name_base[128];
	char metric_name[MAX_METRIC_NAME_LEN];
	struct ldms_atasmart *smarts = (struct ldms_atasmart *) userdata;
	char *dname = disknames[smarts->curr_disk_no];
	sprintf(name_base, "%s", a->name);
	int i;
	for (i = 0; i < NFIELD; i++) {
		rc = get_metric_name(metric_name, fieldname[i], name_base, dname);
		if (rc) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": metric_name '%s_%s#%s' "
					"longer than the max length %d.\n",
					fieldname[i], name_base,
				dname, MAX_METRIC_NAME_LEN);
			errno = rc;
			break;
		}
		switch (i) {
		/* If the value is invalid, the metric value is -1 */
		case 0: /* current value */
		case 1: /* worst value */
		case 2: /* threshold */
			ldms_schema_metric_add(smarts->schema, metric_name, LDMS_V_S16);
			break;
		case 3: /* Pretty value */
			ldms_schema_metric_add(smarts->schema, metric_name, LDMS_V_U64);
			break;
		/* the value is 1, 0. */
		case 4: /* prefail/old-age */
		case 5: /* online/offline */
			ldms_schema_metric_add(smarts->schema, metric_name, LDMS_V_U8);
			break;
		/* the value is 1, 0, -1 for invalid */
		case 6: /* good now */
		case 7: /* good in the past */
			ldms_schema_metric_add(smarts->schema, metric_name, LDMS_V_S8);
			break;
		case 8: /* flag */
			ldms_schema_metric_add(smarts->schema, metric_name, LDMS_V_U16);
			break;
		default:
			assert(0);
		}
		smarts->metric_no++;
	}
}

static int create_metric_set(base_data_t base)
{
	int rc, i;
	int num_skipped_disks = 0;
	smarts = calloc(1, sizeof(struct ldms_atasmart));
	if (!smarts) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Failed to create set.\n");
		return ENOMEM;
	}

	smarts->d = calloc(num_disks, sizeof(SkDisk *));
	if (!smarts->d) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Failed to create set.\n");
		goto err;
	}

	smarts->schema = base_schema_new(base);
	if (!smarts->schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err0;
	}

	/* Location of first metric */
	metric_offset = ldms_schema_metric_count_get(smarts->schema);


	for (i = 0; i < num_disks; i++) {
		smarts->curr_disk_no = i;
		rc = sk_disk_open(disknames[i], &(smarts->d[i]));
		if (rc) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": Create SkDisk '%s' failed. Error %d.\n",
					disknames[i], rc);
			free(disknames[i]);
			disknames[i] = NULL;
			num_skipped_disks++;
			continue;
		}

		rc = sk_disk_smart_read_data(smarts->d[i]);
		if (rc) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": Read data SkDisk '%s'. "
					"Error %d\n", disknames[i], rc);
			free(disknames[i]);
			disknames[i] = NULL;
			sk_disk_free(smarts->d[i]);
			smarts->d[i] = NULL;
			num_skipped_disks++;
			continue;
		}

		rc = sk_disk_smart_parse_attributes(smarts->d[i],
				atasmart_get_disk_info, (void *) smarts);
		if (rc) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": Get size of SkDisk '%s'. "
					"Error %d\n", disknames[i], rc);
			free(disknames[i]);
			disknames[i] = NULL;
			sk_disk_free(smarts->d[i]);
			smarts->d[i] = NULL;
			num_skipped_disks++;
			continue;
		}
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		ovis_log(mylog, OVIS_LERROR, SAMP ": Failed to create metric set.\n");
		goto err1;
	}

	return 0;

err1:
	for (i = 0; i < num_disks; i++) {
		if (smarts->d[i]) {
			sk_disk_free(smarts->d[i]);
			smarts->d[i] = NULL;
		}
	}
err0:
	free(smarts->d);
err:
	free(smarts);

	return ENOMEM;
}

static const char *usage(ldmsd_plug_handle_t handle)
static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " disks=<disknames> " BASE_CONFIG_USAGE
		"    <disks>        A comma-separated list of disk names,\n"
		"		       e.g., /dev/sda,/dev/sda1.\n";
}

/**
 * \brief Configuration
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *s;
	char *tmp;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	int i;
	int rc = 0;
	num_disks = 0;
	value = av_value(avl, "disks");
	if (value) {
		s = strdup(value);
		if (!s) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": enomem.\n");
			return ENOMEM;
		}
		tmp = strtok(s, ",");
		while (tmp) {
			num_disks++;
			tmp = strtok(NULL, ",");
		}
		disknames = calloc(num_disks , sizeof(char *));
		free(s);
		if (!disknames) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": enomem.\n");
			return ENOMEM;
		}
		tmp = strtok(value, ",");
		i = 0;
		while (tmp) {
			disknames[i] = strdup(tmp);
			if (!disknames[i]) {
				for (i = 0; disknames[i] != NULL; i++){
					free(disknames[i]);
				}
				free(disknames);
				disknames = NULL;
				return ENOMEM;
			}
			tmp = strtok(NULL, ",");
			i++;
		}
	} else {
		ovis_log(mylog, OVIS_LERROR, SAMP ": failed to parse the disk names\n");
		return -1;
	}

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base){
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc){
		ovis_log(mylog, OVIS_LERROR, SAMP ":failed to create a metric set.\n");
		goto err;
	}

	return 0;

err:
	for (i = 0; i < num_disks; i++){
		free(disknames[i]);
	}
	free(disknames);
	disknames = NULL;

	base_del(base);
	base = NULL;

	return rc;
}

int atasmart_set_metric(SkDisk *d, SkSmartAttributeParsedData *a,
				void *userdata)
{
	union ldms_value v;

	int *metric_no = (int *) userdata;
	int i;
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
		case 8: /* flag */
			v.v_u16 = a->flags;
			break;
		default:
			assert(0);
		}
		ldms_metric_set(set, *metric_no, &v);
		(*metric_no)++;
	}
	return 0;
}

static int sample(ldmsd_plug_handle_t handle)
{
	int ret;
	int metric_no;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}
	base_sample_begin(base);

	metric_no = metric_offset;
	int i;
	for (i = 0; i < num_disks; i++) {
		ret = sk_disk_smart_parse_attributes(smarts->d[i],
			(SkSmartAttributeParseCallback)atasmart_set_metric,
			(void *) &metric_no);
		if (ret) {
			ovis_log(mylog, OVIS_LDEBUG, "atasmart: Failed to get metric. "
					"SkDisk '%s'."
					" Error %d\n", disknames[i], ret);
			goto err;
		}
	}

	base_sample_end(base);
	return 0;
err:

	base_sample_end(base);
	return ret;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
	set = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	int i;
	for (i = 0; i < num_disks; i++) {
		sk_disk_free(smarts->d[i]);
		free(disknames[i]);
	}
	free(smarts);
	free(disknames);
	smarts = NULL;
	if (base)
		base_del(base);
	base = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
