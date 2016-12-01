/* 
 * Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
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
#include "ldms_jobid.h"
#include <assert.h>
#define PROC_FILE_DEFAULT "/proc/sys/lnet/stats"

static char *procfile = NULL;
static ldms_set_t set = NULL;
static FILE *mf = 0;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;
#define SAMP "lnet_stats"
static char *default_schema_name = SAMP;
static uint64_t compid;

static int metric_offset = 1;
LJI_GLOBALS;

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

static int parse_stats(FILE *fp)
{
	int rc;
	if ((rc = fseek(fp, 0, SEEK_SET))) {
		return rc;
	}
	char *s = fgets(statsbuf, sizeof(statsbuf) - 1, fp);
	if (!s)
	{
		return EIO;
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
		return EIO;
	}

	if ((rc = fseek(fp, 0, SEEK_SET))) {
		return rc;
	}
	return 0;

}

static int create_metric_set(const char *instance_name, char* schema_name)
{
	int rc, i;
	union ldms_value v;

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "Could not open the " SAMP " file "
				"'%s'...exiting sampler\n", procfile);
		return ENOENT;
	}

	int parse_err = parse_stats(mf);
	if (parse_err) {
		msglog(LDMSD_LERROR, "Could not parse the " SAMP " file "
				"'%s'\n", procfile);
		rc = parse_err;
		goto err;
	}

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}

	metric_offset++;
	rc = LJI_ADD_JOBID(schema);
	if (rc < 0) {
		goto err;
	}

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

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	//add specialized metrics
	v.v_u64 = compid;
	ldms_metric_set(set, 0, &v);

	LJI_SAMPLE(set,1);
	return 0;

 err:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (mf)
		fclose(mf);
	mf = NULL;
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
			msglog(LDMSD_LERROR, SAMP ": config argument %s is obsolete.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP " producer=<prod_name> instance=<inst_name> [component_id=<compid> schema=<sname> with_jobid=<jid> file=<proc_name>] \n"
		"    <prod_name>  The producer name\n"
		"    <inst_name>  The instance name\n"
		"    <compid>     Optional unique number identifier. Defaults to zero.\n"
		LJI_DESC
		"    <sname>      Optional schema name. Defaults to '" SAMP "'\n"
		"    <proc_name>  The lnet proc file name if not "  PROC_FILE_DEFAULT "\n";
}

/**
 * \brief Configuration
 *
 * config name=lnet_stats producer=<name> instance=<instance_name> [component_id=<compid> schema=<sname>] [with_jobid=<jid>]
 *     producer_name    The producer id value.
 *     instance_name    The set name.
 *     component_id     The component id. Defaults to zero
 *     sname            Optional schema name. Defaults to lnet_stats
 *     proc_name        The lnet proc file name if not PROC_FILE_DEFAULT
 *     jid              lookup jobid or report 0.
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *sname;
	void * arg = NULL;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0) {
		return rc;
	}

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, SAMP ": missing producer.\n");
		return ENOENT;
	}

	value = av_value(avl, "component_id");
	if (value)
		compid = (uint64_t)(atoi(value));
	else
		compid = 0;

	LJI_CONFIG(value,avl);

	char *pvalue = av_value(avl, "file");
	if (pvalue) {
		procfile = strdup(pvalue);
	} else {
		procfile = strdup(PROC_FILE_DEFAULT);
	}
	if (!procfile) {
		msglog(LDMSD_LERROR, SAMP ": config out of memory.\n");
		return ENOMEM;
	}

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, SAMP ": missing instance.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0) {
		msglog(LDMSD_LERROR, SAMP ": schema name invalid.\n");
		return EINVAL;
	}

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	rc = create_metric_set(value, sname);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int sample(struct ldmsd_sampler *self)
{
	int rc = 0, i;
	int metric_no;
	union ldms_value v;

	if (!set) {
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}
	ldms_transaction_begin(set);

	LJI_SAMPLE(set, 1);

	metric_no = metric_offset;
	int parse_err = parse_stats(mf);
	if (parse_err) {
		if (parse_err_cnt < 2) {
			msglog(LDMSD_LERROR, "Could not parse the " SAMP
				 " file '%s'\n", procfile);
		}
		parse_err_cnt++;
		rc = parse_err;
		goto out;
	}
	for (i = 0; i < NAME_CNT; i++) {
		v.v_u64 = stats_val[i];
		ldms_metric_set(set, metric_no, &v);
		metric_no++;
	}
 out:
	ldms_transaction_end(set);
	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	if (mf)
		fclose(mf);
	mf = NULL;
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (procfile)
		free(procfile);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler lnet_stats_plugin = {
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
	return &lnet_stats_plugin.base;
}
