/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2017 Sandia Corporation. All rights reserved.
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
 * \file job.c
 * \brief shared job data provider
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <time.h>
#include <pthread.h>
#include <strings.h>
#include "ldms.h"
#include "ldmsd.h"
#include "jobinfo.h"

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t job_schema;
#define SAMP "jobinfo"
static uint64_t compid;
pthread_t jobinfo_thread;
char * jobinfo_datafile = LDMS_JOBINFO_DATA_FILE;


char *job_metrics[] = {
	"job_id",
	"job_status",
	"app_id",
	"user_id",
	"comp_id",
	"job_start",
	"job_end",
	"job_exit_status",
	NULL
};

#define	JOBINFO_JOB_ID		0
#define	JOBINFO_JOB_STATUS	1
#define	JOBINFO_APP_ID		2
#define	JOBINFO_USER_ID		3
#define	JOBINFO_COMP_ID		4
#define	JOBINFO_JOB_START	5
#define	JOBINFO_JOB_END		6
#define	JOBINFO_EXIT_STATUS	7

#define	JOBINFO_JOB_USER	8
#define	JOBINFO_JOB_NAME	9

/*
 * read jobinfo metrics from the shared file.
 */
void
jobinfo_read_data(void)
{

	union ldms_value	v;
	struct jobinfo		jobinfo;
	int			fd;

	/*
 	 * Open job data file and read data. Ignore any failures and just
 	 * use the zeroed out data.
 	 */
	bzero((void *)&jobinfo, sizeof(jobinfo));
	fd = open(jobinfo_datafile, O_RDONLY);
	if (fd >= 0) {
		(void)read(fd, &jobinfo, sizeof(jobinfo));
		close(fd);
	}

	ldms_transaction_begin(set);
	v.v_u64 = jobinfo.app_id;
	ldms_metric_set(set, JOBINFO_APP_ID, &v);
	v.v_u64 = jobinfo.job_id;
	ldms_metric_set(set, JOBINFO_JOB_ID, &v);
	v.v_u64 = jobinfo.user_id;
	ldms_metric_set(set, JOBINFO_USER_ID, &v);
	v.v_u64 = jobinfo.job_status;
	ldms_metric_set(set, JOBINFO_JOB_STATUS, &v);
	v.v_u64 = compid;
	ldms_metric_set(set, JOBINFO_COMP_ID, &v);
	ldms_metric_array_set_str(set, JOBINFO_JOB_USER, jobinfo.job_user);
	ldms_metric_array_set_str(set, JOBINFO_JOB_NAME, jobinfo.job_name);
	v.v_u64 = jobinfo.job_start;
	ldms_metric_set(set, JOBINFO_JOB_START, &v);
	v.v_u64 = jobinfo.job_end;
	ldms_metric_set(set, JOBINFO_JOB_END, &v);
	v.v_u64 = jobinfo.job_exit_status;
	ldms_metric_set(set, JOBINFO_EXIT_STATUS, &v);
	ldms_transaction_end(set);

	return;
}

/*
 * Wait for changes to the jobinfo file and update the metrics.
 */
void *
jobinfo_thread_proc(void *arg)
{
	int			rc;
	int			nd;
	int			wd;
	struct inotify_event	ev;

	nd = inotify_init();

	wd = inotify_add_watch(nd, jobinfo_datafile, IN_CLOSE_WRITE);
	if (wd == -1)
		return NULL;

	while (1) {
		rc = read(nd, &ev, sizeof(ev));
		if (rc < sizeof(ev)) {
			continue;
		}
		if (set == NULL) {
			continue;
		}

		jobinfo_read_data();
	}

	inotify_rm_watch(nd, wd);
	close(nd);

	return NULL;
}

static int
create_metric_set(const char *instance_name, char* schema_name)
{
	int			rc;
	int			i;

	job_schema = ldms_schema_new(schema_name);
	if (job_schema == NULL) {
		rc = ENOMEM;
		goto err;
	}

	for (i = 0; job_metrics[i] != NULL; i++) {
		rc = ldms_schema_metric_add(job_schema, job_metrics[i],
					    LDMS_V_U64);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
	}

	rc = ldms_schema_metric_array_add(job_schema, "job_user",
				LDMS_V_CHAR_ARRAY, JOBINFO_MAX_USERNAME);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_metric_array_add(job_schema, "job_name",
				LDMS_V_CHAR_ARRAY, JOBINFO_MAX_JOBNAME);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}

	set = ldms_set_new(instance_name, job_schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	/* seed the metrics */
	jobinfo_read_data();

	return 0;

 err:
	if (job_schema)
		ldms_schema_delete(job_schema);
	job_schema = NULL;
	return rc;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP " producer=<prod_name> instance=<inst_name>]\n [component_id=<compid>}"
		"    <prod_name>  The producer name\n"
		"    <inst_name>  The instance name\n"
		"    <compid>     Optional unique number identifier. Defaults to zero.\n";
}

/**
 * \brief Configuration
 *
 * config name=shared producer=<name> instance=<instance_name>
 *     producer_name    The producer id value.
 *     instance_name    The set name.
 */
static int
config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char			*value;
	int			rc;

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

	/*
	 * If the data file is overridden here, it must also be shared
	 * with jobinfo provider.
	 */
	value = getenv("LDMS_JOBINFO_DATA_FILE");
	if (value) {
		jobinfo_datafile = strdup(value);
		if (jobinfo_datafile == NULL) {
			msglog(LDMSD_LERROR, SAMP ": no memory for file.\n");
			return ENOMEM;
		}
	} else {
		jobinfo_datafile = LDMS_JOBINFO_DATA_FILE;
	}

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, SAMP ": missing instance.\n");
		return ENOENT;
	}

	rc = pthread_create(&jobinfo_thread, NULL, jobinfo_thread_proc, 0);
	if (rc != 0)
		return ENOMEM;

	rc = create_metric_set(value, "jobinfo");
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		pthread_cancel(jobinfo_thread);
		return rc;
	}

	ldms_set_producer_name_set(set, producer_name);
	return 0;
}

static ldms_set_t
get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int
sample(struct ldmsd_sampler *self)
{
	return 0;
}

static void
term(struct ldmsd_plugin *self)
{
	if (job_schema)
		ldms_schema_delete(job_schema);
	job_schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler job_plugin = {
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
	return &job_plugin.base;
}
