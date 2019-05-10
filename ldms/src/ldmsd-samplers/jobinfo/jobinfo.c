/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2017-2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file jobinfo.c
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
#include <ctype.h>
#include <pwd.h>
#include <grp.h>

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#include "jobinfo.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

char *job_metrics[] = {
	/* These 3 metrics are pre-added by samp_create_schema() */
	#if 0
	"component_id",
	"job_id",
	"app_id",
	#endif
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

typedef struct jobinfo_inst_s *jobinfo_inst_t;
struct jobinfo_inst_s {
	struct ldmsd_plugin_inst_s base;

	enum {
		JOBINFO_INST_STOPPED,
		JOBINFO_INST_STARTED,
	} state;

	pthread_t jobinfo_thread;
	char *jobinfo_datafile;
};


int attr_cmp_fn(const void *a, const void *b)
{
	struct attr_s *av = (struct attr_s *)a;
	struct attr_s *bv = (struct attr_s *)b;
	return strcmp(av->av_name, bv->av_name);
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
	int			mask = IN_CLOSE_WRITE | IN_CREATE |
				       IN_DELETE | IN_DELETE_SELF;
	jobinfo_inst_t          inst = arg;
	ldmsd_sampler_type_t    samp = (void*)inst->base.base;

	nd = inotify_init();
	while (1) {
		/*
		 * The file is created by slurm. It may not exist at
		 * the time this sampler is loaded
		 */
		if (wd < 0) {
			wd = inotify_add_watch(nd, inst->jobinfo_datafile, mask);
			if (wd < 0) {
				INST_LOG(inst, LDMSD_LINFO,
					 "the job file %s does not exist, "
					 "retrying in 60s\n",
					 inst->jobinfo_datafile);
				sleep(60);
				continue;
			}
		}

		rc = read(nd, &ev, sizeof(ev));
		if (rc < sizeof(ev) || (ev.mask & (IN_CREATE |
						   IN_DELETE |
						   IN_DELETE_SELF))) {
			INST_LOG(inst, LDMSD_LINFO,
				 "Error %d reading from the inotify "
				 "descriptor.\n", errno);
			/* Watch descriptor no longer valid */
			inotify_rm_watch(nd, wd);
			wd = -1;
			continue;
		}
		samp->sample(&inst->base);
	}

	inotify_rm_watch(nd, wd);
	close(nd);

	return NULL;
}

/* ============== Sampler Plugin APIs ================= */

static
int jobinfo_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	int i, rc;

	for (i = 0; job_metrics[i] != NULL; i++) {
		rc = ldms_schema_metric_add(schema, job_metrics[i],
					    LDMS_V_U64, "");
		if (rc < 0)
			return -rc; /* rc = -errno */
	}

	rc = ldms_schema_metric_array_add(schema, "job_user",
				LDMS_V_CHAR_ARRAY, "", JOBINFO_MAX_USERNAME);
	if (rc < 0)
		return -rc; /* rc = -errno */

	rc = ldms_schema_metric_array_add(schema, "job_name",
				LDMS_V_CHAR_ARRAY, "", JOBINFO_MAX_JOBNAME);
	if (rc < 0)
		return -rc; /* rc = -errno */
	return 0;
}

static
int jobinfo_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	jobinfo_inst_t inst = (void*)pi;

	char buf[80];
	char *s, *p;
	FILE* job_file;

	job_file = fopen(inst->jobinfo_datafile, "r");
	if (!job_file)
		return 0; /* it is OK, will try again next time */

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
	fclose(job_file);
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *jobinfo_desc(ldmsd_plugin_inst_t pi)
{
	return "jobinfo - jobinfo sampler plugin";
}

static
char *_help = "\
jobinfo takes no extra options.\n\
";

static
const char *jobinfo_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int jobinfo_start(ldmsd_plugin_inst_t pi)
{
	int rc;
	jobinfo_inst_t inst = (void*)pi;
	if (inst->state == JOBINFO_INST_STARTED)
		return 0; /* already start, just return 0 */
	rc = pthread_create(&inst->jobinfo_thread, NULL, jobinfo_thread_proc,
			    inst);
	if (0 == rc)
		inst->state = JOBINFO_INST_STARTED;
	return rc;
}

static
int jobinfo_stop(ldmsd_plugin_inst_t pi)
{
	int rc;
	jobinfo_inst_t inst = (void*)pi;
	if (inst->state == JOBINFO_INST_STOPPED)
		return 0; /* already stopped, just return 0 */
	rc = pthread_cancel(inst->jobinfo_thread);
	if (rc)
		return rc;
	rc = pthread_join(inst->jobinfo_thread, NULL);
	if (0 == rc)
		inst->state = JOBINFO_INST_STOPPED;
	return rc;
}

static
int jobinfo_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	jobinfo_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	ldms_schema_t schema;
	ldms_set_t set;
	char *value;
	int rc;

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	/*
	 * If the data file is overridden here, it must also be shared
	 * with jobinfo provider.
	 */
	value = getenv("LDMS_JOBINFO_DATA_FILE");
	inst->jobinfo_datafile = strdup(value?value:LDMS_JOBINFO_DATA_FILE);
	if (inst->jobinfo_datafile == NULL) {
		INST_LOG(inst, LDMSD_LERROR, "no memory for file.\n");
		return ENOMEM;
	}

	/* create schema + set */
	schema = samp->create_schema(pi);
	if (!schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, schema, NULL);
	ldms_schema_delete(schema);
	if (!set)
		return errno;
	jobinfo_start(pi);
	return 0;
}

static
void jobinfo_del(ldmsd_plugin_inst_t pi)
{
	jobinfo_inst_t inst = (void*)pi;

	jobinfo_stop(pi);

	if (inst->jobinfo_datafile)
		free(inst->jobinfo_datafile);
}

static
int jobinfo_init(ldmsd_plugin_inst_t pi)
{
	jobinfo_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;

	/* override update_schema() and update_set() */
	samp->update_schema = jobinfo_update_schema;
	samp->update_set = jobinfo_update_set;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct jobinfo_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "jobinfo",

                /* Common Plugin APIs */
		.desc   = jobinfo_desc,
		.help   = jobinfo_help,
		.init   = jobinfo_init,
		.del    = jobinfo_del,
		.config = jobinfo_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	jobinfo_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
