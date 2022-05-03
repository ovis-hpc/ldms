/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2022 Open Grid Computing, Inc. All rights
 * reserved.
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
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <regex.h>
#include <libgen.h>
#include <time.h>
#include <ftw.h>
#include "../sampler_base.h"
#include "ldms.h"
#include "ldmsd.h"

struct sensor {
	struct sensor_key {
		int package_no;
		int sensor_no;
	} key;
	int meta_init;
	int package_no_midx;
	int sensor_no_midx;

	char *temp_label;	/* temp[#]_label */
	char *temp_label_path;
	FILE *temp_label_fp;
	int temp_label_midx;

	int temp_max;		/* temp[#]_max */
	char *temp_max_path;
	FILE *temp_max_fp;
	int temp_max_midx;

	int temp_crit;		/* temp[#]_crit */
	char *temp_crit_path;
	FILE *temp_crit_fp;
	int temp_crit_midx;

	int temp_crit_alarm;	/* temp[#]_crit_alarm */
	char *temp_crit_alarm_path;
	FILE *temp_crit_alarm_fp;
	int temp_crit_alarm_midx;

	int temp_input;		/* temp[#]_input */
	char *temp_input_path;
	FILE *temp_input_fp;
	int temp_input_midx;

	struct rbn rbn;
};

static int sensor_cmp(void *a_, const void *b_)
{
	struct sensor *a = a_;
	const struct sensor *b = b_;
	if (a->key.package_no < b->key.package_no)
		return -1;
	if (a->key.package_no > b->key.package_no)
		return 1;
	if (a->key.sensor_no < b->key.sensor_no)
		return -1;
	if (a->key.sensor_no > b->key.sensor_no)
		return 1;
	return 0;
}

static struct rbt sensor_tree = RBT_INITIALIZER(sensor_cmp);
static regex_t sensor_re;
static const char *sensor_str =
	"(\\/sys\\/devices\\/platform\\/coretemp.){1}([0-9]+){1}(\\/hwmon){1}"
	"(\\/hwmon){1}([0-9]+){1}(\\/temp){1}([0-9]+){1}(_label)";

int create_sensors_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
	int rc;
	regmatch_t match[16];
	char path[PATH_MAX];
	char *coretemp = strstr(fpath, "coretemp");
	if (!coretemp)
		return 0;
	switch (typeflag) {
	case FTW_F:  /* fpath is a regular file. */
		rc = regexec(&sensor_re, fpath, 16, match, 0);
		if (!rc) {
			struct sensor *sensor = calloc(1, sizeof *sensor);
			sensor->key.package_no = strtod(&fpath[match[2].rm_so], NULL);
			sensor->key.sensor_no =	strtod(&fpath[match[7].rm_so], NULL);
			char *dirn = strdup(fpath);
			dirn = dirname(dirn);
			sprintf(path, "%s/temp%d_label",  dirn, sensor->key.sensor_no);
			sensor->temp_label_path = strdup(path);
			sprintf(path, "%s/temp%d_max", dirn, sensor->key.sensor_no);
			sensor->temp_max_path = strdup(path);
			sprintf(path, "%s/temp%d_crit", dirn, sensor->key.sensor_no);
			sensor->temp_crit_path = strdup(path);
			sprintf(path, "%s/temp%d_crit_alarm", dirn, sensor->key.sensor_no);
			sensor->temp_crit_alarm_path = strdup(path);
			sprintf(path, "%s/temp%d_input", dirn, sensor->key.sensor_no);
			sensor->temp_input_path = strdup(path);
			rbn_init(&sensor->rbn, &sensor->key.package_no);
			rbt_ins(&sensor_tree, &sensor->rbn);
		}
		break;
	case FTW_D:  /* fpath is a directory. */
		break;
	case FTW_DNR: /* fpath is a directory which can't be read. */
	case FTW_NS: /* The stat(2) call failed on fpath, which is not a symbolic link. */
	case FTW_SL: /* Symbolic link.  */
	default:
		break;
	}
	return 0;
}

void collect_sensors(void)
{
	struct rbn *rbn;
	struct sensor *sens;
	FILE *fp;
	char fbuf[80];
	int rc;

	RBT_FOREACH(rbn, &sensor_tree) {
		sens = container_of(rbn, struct sensor, rbn);
		/* temp_label -- doens't change */
		if (!sens->temp_label) {
			fp = fopen(sens->temp_label_path, "r");
			rc = fread(fbuf, 1, sizeof(fbuf), fp);
			fclose(fp);
			char *nl = strstr(fbuf, "\n");
			if (nl)
				*nl = '\0';
			fbuf[rc] = '\0';
			sens->temp_label = strdup(fbuf);
		}
		/* temp_input */
		if (!sens->temp_input_fp)
			sens->temp_input_fp = fopen(sens->temp_input_path, "r");
		else
			fseek(sens->temp_input_fp, 0, SEEK_SET);
		rc = fread(fbuf, 1, sizeof(fbuf), sens->temp_input_fp);
		fbuf[rc] = '\0';
		sens->temp_input = (float)strtol(fbuf, NULL, 0) / 1000.0;
		/* temp_max */
		if (!sens->temp_max_fp)
			sens->temp_max_fp = fopen(sens->temp_max_path, "r");
		else
			fseek(sens->temp_max_fp, 0, SEEK_SET);
		rc = fread(fbuf, 1, sizeof(fbuf), sens->temp_max_fp);
		fbuf[rc] = '\0';
		sens->temp_max = (float)strtol(fbuf, NULL, 0) / 1000.0;
		/* temp_crit */
		if (!sens->temp_crit) {
			fp = fopen(sens->temp_crit_path, "r");
			rc = fread(fbuf, 1, sizeof(fbuf), fp);
			fclose(fp);
			fbuf[rc] = '\0';
			sens->temp_crit = (float)strtol(fbuf, NULL, 0) / 1000;
		}
		/* temp_crit_alarm */
		if (!sens->temp_crit_alarm_fp)
			sens->temp_crit_alarm_fp = fopen(sens->temp_crit_alarm_path, "r");
		else
			fseek(sens->temp_crit_alarm_fp, 0, SEEK_SET);
		rc = fread(fbuf, 1, sizeof(fbuf), sens->temp_crit_alarm_fp);
		fbuf[rc] = '\0';
		sens->temp_crit_alarm = strtol(fbuf, NULL, 0);
	}
}

static ldms_set_t set;
#define SAMP "coretemp"
static ldmsd_msg_log_f msglog;
static base_data_t base;

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int create_metric_set(base_data_t base)
{
	int rc;
	ldms_schema_t schema;
	struct rbn *rbn;
	struct sensor *sens;
	char metric_name[80];

	/* Create a metric set of the required size */
	schema = base_schema_new(base);
	if (!schema) {
		msglog(LDMSD_LERROR,
		       SAMP ": The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = EINVAL;
		goto err_0;
	}

	/*
	 * Walk the /sys/devices/platform/coretemp.[package#]/hpmon/hwmon[package#]
	 * path to count the number of sensors
	 */
	rc = regcomp(&sensor_re, sensor_str, REG_EXTENDED);
	rc = nftw("/sys/devices/platform", create_sensors_cb, 16, FTW_PHYS);
	if (rc) {
		msglog(LDMSD_LERROR,
		       SAMP ": Error %d creating the sensor tree.\n", rc);
		goto err_0;

	}
	collect_sensors();
	RBT_FOREACH(rbn, &sensor_tree) {
		sens = container_of(rbn, struct sensor, rbn);
		sprintf(metric_name, "temp_%d_%d_label", sens->key.package_no, sens->key.sensor_no);
		rc = ldms_schema_meta_array_add(schema, metric_name, LDMS_V_CHAR_ARRAY, 24);
		if (rc < 0)
			goto err_1;
		sens->temp_label_midx = rc;

		sprintf(metric_name, "temp_%d_%d_package_no", sens->key.package_no, sens->key.sensor_no);
		rc = ldms_schema_meta_add(schema, metric_name, LDMS_V_U32);
		if (rc < 0)
			goto err_1;
		sens->package_no_midx = rc;

		sprintf(metric_name, "temp_%d_%d_sensor_no", sens->key.package_no, sens->key.sensor_no);
		rc = ldms_schema_meta_add(schema, metric_name, LDMS_V_U32);
		if (rc < 0)
			goto err_1;
		sens->sensor_no_midx = rc;

		sprintf(metric_name, "temp_%d_%d_input", sens->key.package_no, sens->key.sensor_no);
		rc = ldms_schema_metric_add_with_unit(schema, metric_name, "C", LDMS_V_F32);
		if (rc < 0)
			goto err_1;
		sens->temp_input_midx = rc;

		sprintf(metric_name, "temp_%d_%d_max", sens->key.package_no, sens->key.sensor_no);
		rc = ldms_schema_metric_add_with_unit(schema, metric_name, "C", LDMS_V_F32);
		if (rc < 0)
			goto err_1;
		sens->temp_max_midx = rc;

		sprintf(metric_name, "temp_%d_%d_crit", sens->key.package_no, sens->key.sensor_no);
		rc = ldms_schema_metric_add_with_unit(schema, metric_name, "C", LDMS_V_F32);
		if (rc < 0)
			goto err_1;
		sens->temp_crit_midx = rc;

		sprintf(metric_name, "temp_%d_%d_crit_alarm", sens->key.package_no, sens->key.sensor_no);
		rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U32);
		if (rc < 0)
			goto err_1;
		sens->temp_crit_alarm_midx = rc;
	}

	set = base_set_new(base);
	if (!set) {
		msglog(LDMSD_LERROR, SAMP ": Error %d creating the metric set.\n", errno);
		rc = errno;
		goto err_0;
	}

	return 0;

err_1:
	msglog(LDMSD_LERROR, SAMP ": Error %d adding the metric '%s' to the schema.\n",
		rc, metric_name);

err_0:
	return rc;
}


static const char *usage(struct ldmsd_plugin *self)
{
	return "config name=" SAMP BASE_CONFIG_USAGE;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	base = base_config(avl, SAMP, SAMP, msglog);
	if (!base){
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}

	return 0;

 err:
	base_del(base);
	return rc;

}

static int sample(struct ldmsd_sampler *self)
{
	struct sensor *sens;
	struct rbn *rbn;

	if (!set){
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	RBT_FOREACH(rbn, &sensor_tree) {
		sens = container_of(rbn, struct sensor, rbn);
		if (!sens->meta_init) {
			sens->meta_init = 1;
			ldms_metric_array_set_str(set, sens->temp_label_midx, sens->temp_label);
			ldms_metric_set_u32(set, sens->package_no_midx, sens->key.package_no);
			ldms_metric_set_u32(set, sens->sensor_no_midx, sens->key.sensor_no);
		}
		ldms_metric_set_float(set, sens->temp_input_midx, sens->temp_input);
		ldms_metric_set_float(set, sens->temp_max_midx, sens->temp_max);
		ldms_metric_set_float(set, sens->temp_crit_midx, sens->temp_crit);
		ldms_metric_set_u32(set, sens->temp_crit_alarm_midx, sens->temp_crit_alarm);
	}
	base_sample_end(base);
	return 0;
}


static void term(struct ldmsd_plugin *self)
{
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler coretemp_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
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
	return &coretemp_plugin.base;
}
