/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017-2018 Open Grid Computing, Inc. All rights reserved.
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
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <time.h>
#include <pthread.h>
#include <strings.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include "ldms.h"
#include "ldmsd.h"
#include "jobinfo.h"

static ovis_log_t mylog;
static ldms_set_t set = NULL;
static char *producer_name;
static ldms_schema_t job_schema;
#define SAMP "jobinfo"
static uint64_t compid;
pthread_t jobinfo_thread;
char * jobinfo_datafile = LDMS_JOBINFO_DATA_FILE;
uid_t uid;
gid_t gid;
uint32_t perm;
bool uid_is_set;
bool gid_is_set;
bool perm_is_set;

char *job_metrics[] = {
	"component_id",
	"job_id",
	"app_id",
	"job_status",
	"user_id",
	"job_start",
	"job_end",
	"job_exit_status",
	NULL
};

/* The first three metrics must match the sampler_base order */
#define	JOBINFO_COMP_ID		0
#define	JOBINFO_JOB_ID		1
#define	JOBINFO_APP_ID		2

#define	JOBINFO_JOB_STATUS	3
#define	JOBINFO_USER_ID		4
#define	JOBINFO_JOB_START	5
#define	JOBINFO_JOB_END		6
#define	JOBINFO_EXIT_STATUS	7

#define	JOBINFO_JOB_USER	8
#define	JOBINFO_JOB_NAME	9

struct attr_s {
	const char *av_name;
	int metric_id;
} attr_dict[] = {
	{ "JOB_APP_ID",	JOBINFO_APP_ID },
	{ "JOB_END",	JOBINFO_JOB_END },
	{ "JOB_EXIT",	JOBINFO_EXIT_STATUS },
	{ "JOB_ID",	JOBINFO_JOB_ID },
	{ "JOB_NAME",	JOBINFO_JOB_NAME },
	{ "JOB_START",	JOBINFO_JOB_START },
	{ "JOB_STATUS",	JOBINFO_JOB_STATUS },
	{ "JOB_USER",	JOBINFO_JOB_USER },
	{ "JOB_USER_ID",JOBINFO_USER_ID },
};
#define DICT_LEN (sizeof(attr_dict) / sizeof(attr_dict[0]))

int attr_cmp_fn(const void *a, const void *b)
{
	struct attr_s *av = (struct attr_s *)a;
	struct attr_s *bv = (struct attr_s *)b;
	return strcmp(av->av_name, bv->av_name);
}

/*
 * read jobinfo metrics from the shared file.
 */
static void jobinfo_read_data(void)
{
	char buf[80];
	char *s, *p;
	FILE* job_file = fopen(jobinfo_datafile, "r");
	if (!job_file)
		return;

	ldms_transaction_begin(set);
	ldms_metric_set_u64(set, JOBINFO_COMP_ID, compid);

	while (NULL != (s = fgets(buf, sizeof(buf), job_file))) {
		char *name;
		char *value;
		struct attr_s key;
		struct attr_s *av;

		/* Ignore garbage lines */
		name = strtok_r(s, "=", &p);
		if (!name)
			continue;
		value = strtok_r(NULL, "=", &p);
		if (!value)
			continue;

		key.av_name = name;
		av = (struct attr_s *)
			bsearch(&key, attr_dict,
				DICT_LEN, sizeof(*av),attr_cmp_fn);
		if (av) {
			/* skip leading \" */
			while (*value == '"')
				value++;
			/* Strip newlines */
			while ((s = strstr(value, "\n")))
				*s = '\0';
			/* Strip trailing \" */
			while ((s = strstr(value, "\"")))
				*s = '\0';
			switch (av->metric_id) {
			case JOBINFO_JOB_NAME:
				ldms_metric_array_set_str(set,
							  av->metric_id, value);
				break;
			case JOBINFO_JOB_USER:
				ldms_metric_array_set_str(set,
							  av->metric_id, value);
				break;
			default:
				ldms_metric_set_u64(set, av->metric_id,
						    strtol(value, NULL, 0));
				break;
			}
		}
	}
	ldms_transaction_end(set);
	fclose(job_file);
}

/*
 * Wait for changes to the jobinfo file and update the metrics.
 */
void *
jobinfo_thread_proc(void *arg)
{
	int			rc;
	int			nd;
	int			wd = -1;
	struct inotify_event	ev;
	int			mask = IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_DELETE_SELF;

	nd = inotify_init();
	while (1) {
		/*
		 * The file is created by slurm. It may not exist at
		 * the time this sampler is loaded
		 */
		if (wd < 0) {
			wd = inotify_add_watch(nd, jobinfo_datafile, mask);
			if (wd < 0) {
				ovis_log(mylog, OVIS_LINFO,
				       "the job file %s does not exist, retrying in 60s\n",
				       jobinfo_datafile);
				sleep(60);
				continue;
			}
		}

		rc = read(nd, &ev, sizeof(ev));
		if (rc < sizeof(ev) || (ev.mask & (IN_CREATE | IN_DELETE | IN_DELETE_SELF))) {
			ovis_log(mylog, OVIS_LINFO,
			       "Error %d reading from the inotify descriptor.\n",
			       errno);
			/* Watch descriptor no longer valid */
			inotify_rm_watch(nd, wd);
			wd = -1;
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
        if (uid_is_set)
                ldms_set_uid_set(set, uid);
        if (gid_is_set)
                ldms_set_gid_set(set, gid);
        if (perm_is_set)
                ldms_set_perm_set(set, perm);

	/* seed the metrics */
	jobinfo_read_data();

	return 0;

 err:
	if (job_schema)
		ldms_schema_delete(job_schema);
	job_schema = NULL;
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
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
config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char			*value;
	int			rc;

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		ovis_log(mylog, OVIS_LERROR, "missing producer.\n");
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
			ovis_log(mylog, OVIS_LERROR, "no memory for file.\n");
			return ENOMEM;
		}
	} else {
		jobinfo_datafile = LDMS_JOBINFO_DATA_FILE;
	}

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	/* uid, gid, permission */
	uid_is_set = false;
	gid_is_set = false;
	perm_is_set = false;
	value = av_value(avl, "uid");
	if (value) {
		if (isalpha(value[0])) {
			/* Try to lookup the user name */
			struct passwd *pwd = getpwnam(value);
			if (!pwd) {
				ovis_log(mylog, OVIS_LERROR,
				       "The specified user '%s' does not exist\n",
				       value);
				return EINVAL;
			}
			uid = pwd->pw_uid;
		} else {
			uid = strtol(value, NULL, 0);
		}
		uid_is_set = true;
	}
	value = av_value(avl, "gid");
	if (value) {
		if (isalpha(value[0])) {
			/* Try to lookup the group name */
			struct group *grp = getgrnam(value);
			if (!grp) {
				ovis_log(mylog, OVIS_LERROR,
				       "The specified group '%s' does not exist\n",
				       value);
				return EINVAL;
			}
			gid = grp->gr_gid;
		} else {
			gid = strtol(value, NULL, 0);
		}
		gid_is_set = true;
	}
	value = av_value(avl, "perm");
	if (value) {
		if (value[0] != '0') {
			ovis_log(mylog, OVIS_LINFO,
			       "Warning, the permission bits '%s' are not specified "
			       "as an Octal number.\n",
			       value);
		}
		perm = strtol(value, NULL, 0);
		perm_is_set = true;
	}

	value = av_value(avl, "instance");
	if (!value) {
		ovis_log(mylog, OVIS_LERROR, "missing instance.\n");
		return ENOENT;
	}

	rc = create_metric_set(value, "jobinfo");
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);
	ldms_set_publish(set);

	rc = pthread_create(&jobinfo_thread, NULL, jobinfo_thread_proc, 0);
	if (rc != 0) {
		ldms_set_delete(set);
		set = NULL;
		return ENOMEM;
	}
	return 0;
}

static int
sample(ldmsd_plug_handle_t handle)
{
	return 0;
}

static void
term(ldmsd_plug_handle_t handle)
{
	if (job_schema)
		ldms_schema_delete(job_schema);
	job_schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
        set = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
