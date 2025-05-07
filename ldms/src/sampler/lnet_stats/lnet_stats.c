/**
 * Copyright (c) 2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016,2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file lnet.c
 * \brief /proc/?/lnet/stats data provider
 *
 * as of Lustre 2.5-2.8:
 * __proc_lnet_stats in
 * lustre-?/lnet/lnet/router_proc.c
 * generates the proc file as:
 *         len = snprintf(tmpstr, tmpsiz,
 *                     "%u %u %u %u %u %u %u "LPU64" "LPU64" "
 *                     LPU64" "LPU64,
 *                     ctrs->msgs_alloc, ctrs->msgs_max,
 *                     ctrs->errors,
 *                     ctrs->send_count, ctrs->recv_count,
 *                     ctrs->route_count, ctrs->drop_count,
 *                     ctrs->send_length, ctrs->recv_length,
 *                     ctrs->route_length, ctrs->drop_length);
 * where LPU64 is equivalent to uint64_t.
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
#include "ldmsd_plug_api.h"
#include "sampler_base.h"
#include <assert.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

static char *lnet_state_file = NULL;
static const char * default_lnet_state_files[] = {
		"/sys/kernel/debug/lnet/stats",
		"/proc/sys/lnet/stats",
};
static ldms_set_t set = NULL;
#define SAMP "lnet_stats"

static ovis_log_t mylog;

static int metric_offset;
static base_data_t base;

static const char * stat_name[] = {
	"msgs_alloc",
	"msgs_max",
	"errors",
	"send_count",
	"recv_count",
	"route_count",
	"drop_count",
	"send_length",
	"recv_length",
	"route_length",
	"drop_length",
};

#define NAME_CNT sizeof(stat_name)/sizeof(stat_name[0])

static uint64_t stats_val[NAME_CNT];

#define ROUTER_STAT_BUF_SIZE NAME_CNT*24

static char statsbuf[ROUTER_STAT_BUF_SIZE];

static int parse_err_cnt;

static int parse_stats()
{
	int i;
	for (i = 0; i < NAME_CNT; i++) {
		stats_val[i] = 0;
	}
	FILE *fp;
	fp = fopen(lnet_state_file, "r");
	if (!fp) {
		return ENOENT;
	}
	int rc;
	char *s = fgets(statsbuf, sizeof(statsbuf) - 1, fp);
	if (!s)
	{
		rc = EIO;
		goto err;
	}

	assert(NAME_CNT == 11); /* fix scanf call if this fails */
	int n = sscanf(statsbuf,
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " " ,
		&(stats_val[0]),
		&(stats_val[1]),
		&(stats_val[2]),
		&(stats_val[3]),
		&(stats_val[4]),
		&(stats_val[5]),
		&(stats_val[6]),
		&(stats_val[7]),
		&(stats_val[8]),
		&(stats_val[9]),
		&(stats_val[10]));

	if (n < 11)
	{
		rc = EIO;
		goto err;
	}

	rc = 0;

err:
	fclose(fp);
	return rc;

}

/* file can be missing if all mounts are currently unmounted, but it may appear later. */
static int get_data()
{
	int i;
	/* If no user input */
	if (lnet_state_file == NULL) {
		/* Try possible lustre stat locations */
		for (i=0; i < ARRAY_SIZE(default_lnet_state_files); i++) {
			lnet_state_file = strdup(default_lnet_state_files[i]);
			int parse_err = parse_stats();
			if (parse_err) {
				free(lnet_state_file);
				lnet_state_file = NULL;
				continue;
			}
			break;
		}
	}

	if (lnet_state_file == NULL || parse_stats()) {
		if (!parse_err_cnt) {
			ovis_log(mylog, OVIS_LINFO, SAMP ": Could not parse the " SAMP
				 " file '%s'\n", lnet_state_file ? lnet_state_file :
				"/sys/kernel/debug/lnet/stats or /proc/sys/lnet/stats"
				);
		}
		parse_err_cnt++;
		return ENOENT;
	}
	if (parse_err_cnt) {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": parsed file '%s'\n", lnet_state_file);
		parse_err_cnt = 0;
	}
	return 0;
}

static int create_metric_set(base_data_t base)
{
	int rc, i;
	ldms_schema_t schema;

	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/* Location of the first metric */
	metric_offset = ldms_schema_metric_count_get(schema);

	/*
	 * Process the file to define all the metrics.
	 */
	i = 0;
	for ( ; i < NAME_CNT; i++) {
		rc = ldms_schema_metric_add(schema, stat_name[i], LDMS_V_U64);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

 err:

	return rc;
}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(mylog, OVIS_LERROR, SAMP ": config argument %s is obsolete.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " [file=<stats_path>] " BASE_CONFIG_USAGE
		"    <stats_path>  The lnet stats file name if not using the default\n";
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	void * arg = NULL;
	int rc;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, arg);
	if (rc != 0) {
		return rc;
	}

	char *pvalue = av_value(avl, "file");
	if (pvalue) {
		lnet_state_file = strdup(pvalue);
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": User-defined lnet_state_file %s.\n", pvalue);
	}

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base)
		return EINVAL;

	rc  = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}

	return 0;

err:
	base_del(base);
	return 0;
}

static int sample(ldmsd_plug_handle_t handle)
{
	int i;
	int metric_no;
	union ldms_value v;

	if (!set) {
		return ENOMEM;
	}

	int parse_err = get_data();
	if (parse_err)
		return 0;

	base_sample_begin(base);
	metric_no = metric_offset;
	for (i = 0; i < NAME_CNT; i++) {
		v.v_u64 = stats_val[i];
		ldms_metric_set(set, metric_no, &v);
		metric_no++;
	}
	base_sample_end(base);
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
        set = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	base_del(base);
	base = NULL;
	free(lnet_state_file);
	lnet_state_file = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

struct ldmsd_sampler ldmsd_plugin_interface  = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
