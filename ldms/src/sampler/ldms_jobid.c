/**
 * Copyright (c) 2015-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file ldms_jobid.c
 * \brief /var/run/ldms.jobid shared data provider.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_jobid.h"
#include "sampler_base.h"


/* This sampler relies on the resource manager to
place notes in JOBID_FILE about the current job, or 0 if the
node is not reserved. If this file is absent, the sampler
will run, reporting 0 jobid.

The file checked is by default:
	/var/run/ldms.jobinfo
exists if the next lines are added to slurm-prolog or equivalent
	echo JOBID=$SLURM_JOBID > /var/run/ldms.slurm.jobinfo
	echo UID=$SLURM_UID >> /var/run/ldms.jobinfo
	echo USER=$SLURM_USER >> /var/run/ldms.jobinfo
	echo APPID=0 >> /var/run/ldms.jobinfo
and the next line is added to slurm-epilog or equivalent
	echo "JOBID=0" > /var/run/ldms.jobinfo
On some slurm 2.3.x systems, these are in /etc/nodestate/bin/.
On systems with non-slurm reservations or shared reservations,
this sampler may need to be cloned and modified.

The functionality is generic to files containing the
key=value pairs listed above. APPID often exists on Cray systems;
if it is not present, 0 will be recorded.
*/
#define DEFAULT_FILE "/var/run/ldms.jobinfo"
#define SAMP "jobid"
static char *default_schema_name = SAMP;
static const char *JOBID_FILE = DEFAULT_FILE;
static const char *JOBID_COLNAME = LJI_JOBID_METRIC_NAME;

static ovis_log_t mylog;

const char *lji_metric_names[] = {
LJI_JOBID_METRIC_NAME,
LJI_UID_METRIC_NAME,
LJI_USER_METRIC_NAME,
LJI_APPID_METRIC_NAME,
NULL
};

static char *procfile = NULL;

static char *metric_name = NULL;
static ldms_set_t set = NULL;
static char *producer_name;
static ldms_schema_t schema;
static uint64_t compid = 0;
static struct base_auth auth;

static struct ldms_job_info ji;

static int32_t compid_pos = 0;
static int32_t jobid_pos = 0;
static int32_t uid_pos = 0;
static int32_t user_pos = 0;
static int32_t appid_pos = 0;
static int32_t jstart_pos = 0;
static int32_t jend_pos = 0;


#define JI_SIZE 32768
static char ji_buf[JI_SIZE];
static int set_meta_new = 0;
static int parse_err_logged = 0;

static int parse_jobinfo(const char* file, struct ldms_job_info *ji, ldms_set_t js) {
	int rc = 0;
	ji->appid = ji->jobid = ji->uid = 0;
	memset(&(ji->user[0]), '\0', LJI_USER_NAME_MAX);
	strcpy(&(ji->user[0]), "root");
	FILE *mf = fopen(file, "r");
	struct attr_value_list *kvl = NULL;
	struct attr_value_list *avl = NULL;
	int recovery = 0;
	if (parse_err_logged) {
		recovery = 1;
	}

	if (!mf) {
		if (!parse_err_logged) {
			ovis_log(mylog, OVIS_LINFO,"Could not open the jobid file '%s'\n", procfile);
			parse_err_logged = 1;
		}
		rc = EINVAL;
		goto err;
	}
	rc = fseek(mf, 0, SEEK_END);
	if (rc) {
		if (!parse_err_logged) {
			ovis_log(mylog, OVIS_LINFO,"Could not seek '%s'\n", file);
			parse_err_logged = 1;
		}
		goto err;
	}
	long flen = ftell(mf);
	if (flen >= JI_SIZE) {
		rc = E2BIG;
		if (!parse_err_logged) {
			ovis_log(mylog, OVIS_LINFO,"File %s too big (>%d).\n", file, JI_SIZE);
			parse_err_logged = 1;
		}
		goto err;
	}
	rewind(mf);

	size_t nread = fread(ji_buf, sizeof(char), flen, mf);
	ji_buf[flen] = '\0';
	if (nread < flen) {
		ji_buf[nread] = '\0';
	}
	fclose(mf);
	mf = NULL;

	int maxlist = 1;
	char *t = ji_buf;
	while (t[0] != '\0') {
		if (isspace(t[0])) maxlist++;
		t++;
	}
	kvl = av_new(maxlist);
	avl = av_new(maxlist);
	rc = tokenize(ji_buf, kvl, avl);
	av_free(kvl);
	kvl = NULL;
	if (rc) {
		if (!parse_err_logged) {
			ovis_log(mylog, OVIS_LERROR,"Fail tokenizing '%s'\n", file);
			parse_err_logged = 1;
		}
		goto err;
	}

	char *endp;
	char *tmp = av_value(avl, "JOBID");
	uint64_t j;
	if (tmp) {
		endp = NULL;
		errno = 0;
		j = strtoull(tmp, &endp, 0);
		if (endp == tmp || errno) {
			if (!parse_err_logged) {
				ovis_log(mylog, OVIS_LERROR,"Fail parsing JOBID '%s'\n", tmp);
				parse_err_logged = 1;
			}
			rc = EINVAL;
			goto err;
		}
		ji->jobid = j;
	} else {
		if (!parse_err_logged) {
			ovis_log(mylog, OVIS_LERROR,"JOBID= missing from '%s'\n", file);
			parse_err_logged = 1;
		}
		rc = EINVAL;
		ji->jobid = 0;
		goto err;
	}

	tmp = av_value(avl, "UID");
	if (tmp) {
		endp = NULL;
		errno = 0;
		j = strtoull(tmp, &endp, 0);
		if (endp == tmp || errno) {
			if (!parse_err_logged) {
				ovis_log(mylog, OVIS_LINFO,"Fail parsing UID '%s'\n",
					tmp);
				parse_err_logged = 1;
			}
			rc = EINVAL;
			goto err;
		}
		ji->uid = j;
	} else {
		ji->uid = 0;
	}

	tmp = av_value(avl, "APPID");
	if (tmp) {
		endp = NULL;
		errno = 0;
		j = strtoull(tmp, &endp, 0);
		if (endp == tmp || errno) {
			ovis_log(mylog, OVIS_LINFO,"Fail parsing APPID '%s'\n", tmp);
			if (!parse_err_logged) {
				ovis_log(mylog, OVIS_LINFO,"Fail parsing APPID '%s'\n",
					tmp);
				parse_err_logged = 1;
			}
			rc = EINVAL;
			goto err;
		}
		ji->appid = j;
	} else {
		ji->appid = 0;
	}

	tmp = av_value(avl, "USER");
	if (tmp) {
		if (strlen(tmp) >= LJI_USER_NAME_MAX) {
			if (!parse_err_logged) {
				ovis_log(mylog, OVIS_LINFO,"Username too long '%s'\n",
					tmp);
				parse_err_logged = 1;
			}
			rc = EINVAL;
		} else {
			strcpy(ji->user, tmp);
		}
	} else {
		strcpy(ji->user, "root");
	}

	if (!rc && recovery) {
		ovis_log(mylog, OVIS_LDEBUG, "jobid parse recovered for '%s'\n", file);
		parse_err_logged = 0;
	}
	goto out;
err:
	strcpy(ji->user, "root");
	ji->uid = 0;
	ji->appid = 0;
	ji->jobid = 0;
	if (mf) {
		fclose(mf);
	}
out:
	ldms_transaction_begin(js);

	union ldms_value v;
	if (set_meta_new) {
		set_meta_new = 0;
		v.v_u64 = compid;
		ldms_metric_set(js, compid_pos, &v);
		v.v_u32 = 0;
		ldms_metric_set(js, jstart_pos, &v);
		ldms_metric_set(js, jend_pos, &v);
	}
	v.v_u64 = ji->jobid;
	ldms_metric_set(js, jobid_pos, &v);

	v.v_u64 = ji->uid;
	ldms_metric_set(js, uid_pos, &v);

	v.v_u64 = ji->appid;
	ldms_metric_set(js, appid_pos, &v);

	ldms_mval_t mv = (ldms_mval_t) &(ji->user);
	ldms_metric_array_set(js, user_pos, mv, 0, LJI_USER_NAME_MAX);

	ldms_transaction_end(js);

	if (avl)
		av_free(avl);
	return rc;
}

static int create_metric_set(const char *instance_name, char* schema_name,
			     int set_array_card)
{
	int rc = 0;

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		rc = errno;
		ovis_log(mylog, OVIS_LERROR, SAMP ": ldms_schema_new fail.\n");
		goto err;
	}

	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": ldms_schema_meta_add component_id fail.\n");
		goto err;
	}
	compid_pos = rc;

	rc = ldms_schema_metric_add(schema, metric_name,
		LDMS_V_U64);
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": ldms_schema_metric_add "
			"%s fail.\n", metric_name);
		goto err;
	}
	jobid_pos = rc;

	rc = ldms_schema_metric_add(schema, LJI_UID_METRIC_NAME,
		LDMS_V_U64);
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": ldms_schema_metric_add "
			LJI_UID_METRIC_NAME " fail.\n");
		goto err;
	}
	uid_pos = rc;

	rc = ldms_schema_metric_array_add(schema, LJI_USER_METRIC_NAME,
		LDMS_V_CHAR_ARRAY, LJI_USER_NAME_MAX);
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": ldms_schema_metric_array_add "
			LJI_USER_METRIC_NAME " fail.\n");
		goto err;
	}
	user_pos = rc;
	rc = 0;

	rc = ldms_schema_metric_add(schema, "app_id", LDMS_V_U64);
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": ldms_schema_metric_add app_id fail.\n");
		goto err;
	}
	appid_pos = rc;

	rc = ldms_schema_meta_add(schema, "job_start", LDMS_V_U32);
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": ldms_schema_meta_add job_start fail.\n");
		goto err;
	}
	jstart_pos = rc;

	rc = ldms_schema_meta_add(schema, "job_end", LDMS_V_U32);
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": ldms_schema_meta_add job_end fail.\n");
		goto err;
	}
	jend_pos = rc;
	rc = ldms_schema_array_card_set(schema, set_array_card);
	if (rc < 0) {
		errno = rc;
		goto err;
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		ovis_log(mylog, OVIS_LERROR, SAMP ": ldms_set_new fail.\n");
		goto err;
	}
	base_auth_set(&auth, set);
	set_meta_new = 1;

	rc = ldms_set_publish(set);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": ldms_set_publish fail.\n");
		errno = rc;
		goto err;
	} else {
		ldmsd_set_register(set, SAMP);
	}


	/*
	 * Process the file to init jobid.
	 */
	(void)parse_jobinfo(procfile, &ji, set);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": first parse_jobinfo fail ignored;"
			"expect 0/root data values. Queue system silent?\n");
	}
	return 0;

 err:
	if (set) {
		ldms_set_delete(set);
		set = NULL;
	}
	if (schema) {
		ldms_schema_delete(schema);
		schema = NULL;
	}
	ovis_log(mylog, OVIS_LDEBUG, SAMP ": rc=%d: %s.\n", rc, STRERROR(-rc));
	return -rc;
}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set","colname"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(mylog, OVIS_LERROR, SAMP "config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}


/**
 * \brief Configuration
 *
 * config name=jobid component_id=<comp_id> set=<setname>
 *     comp_id     The component id value.
 *     file  The file to find job id in on 1st line in ascii.
 *     setname     The set name.
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *sname;
	char *iname;
	void * arg = NULL;
	int rc;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}

	rc = base_auth_parse(avl, &auth, mylog);
	if (rc != 0){
		return rc;
	}

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": missing producer.\n");
		return ENOENT;
	}

	iname = av_value(avl, "instance");
	if (!iname) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": missing instance.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": schema name invalid.\n");
		return EINVAL;
	}

	value = av_value(avl, "component_id");
	if (value) {
		char *endp = NULL;
		errno = 0;
		compid = strtoull(value, &endp, 0);
		if (endp == value || errno) {
			ovis_log(mylog, OVIS_LERROR,"Fail parsing component_id '%s'\n",
				value);
			return EINVAL;
		}
	}

	value = av_value(avl, "file");
	if (value) {
		procfile = strdup(value);
		if (!procfile) {
			ovis_log(mylog, OVIS_LERROR,SAMP " no memory\n");
			return ENOMEM;
		}
	} else {
		procfile = (char *)JOBID_FILE;
	}

	metric_name = (char *)JOBID_COLNAME;
	ovis_log(mylog, OVIS_LDEBUG,"Jobid file is '%s'\n", procfile);

	value = av_value(avl, "set_array_card");
	int set_array_card = (value)?(strtol(value, NULL, 0)):(1);

	rc = create_metric_set(iname, sname, set_array_card);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": failed to create a metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);
	ovis_log(mylog, OVIS_LDEBUG, SAMP ": config() ok.\n");
	return 0;
}

static int sample(ldmsd_plug_handle_t handle)
{
	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG,SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	/* unlike device drivers, the file for jobid gets replaced
	   and must be reread every time. In production clusters, it's
	   almost certain in a local ram file system, so this is fast
	   enough compared to contacting a resource manager daemon.
	*/
	(void)parse_jobinfo(procfile, &ji, set);

	/* as a policy matter, missing file has the value 0, not an error. */
	return 0;
}

int ldms_job_info_get(struct ldms_job_info *ji, unsigned flags)
{
	if (!set) {
		return EAGAIN;
	}
	if (flags & LJI_jobid)
		ji->jobid = ldms_metric_get_u64(set, jobid_pos);
	if (flags & LJI_appid)
		ji->appid = ldms_metric_get_u64(set, appid_pos);
	if (flags & LJI_uid)
		ji->uid = ldms_metric_get_u64(set, uid_pos);
	if (flags & LJI_user) {
		const char * a = (char *)ldms_metric_array_get(set, user_pos);
		strncpy(ji->user, a, LJI_USER_NAME_MAX );
	}

	return 0;
}

static void term(ldmsd_plug_handle_t handle)
{
	if (set) {
		ldmsd_set_deregister(ldms_set_instance_name_get(set), SAMP);
		ldms_set_delete(set);
	}
	set = NULL;
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;

	if (metric_name && metric_name != JOBID_COLNAME ) {
		free(metric_name);
	}
	if ( procfile && procfile != JOBID_FILE ) {
		free(procfile);
	}
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return "config name=jobid producer=<prod_name> instance=<inst_name> "
		"[component_id=<compid> schema=<sname>] [file=<jobinfo>]"
		"    comp_id     The component id value.\n"
		"    <prod_name> The producer name\n"
		"    <inst_name> The instance name\n"
		"    <sname>     Optional schema name. Defaults to 'jobid'\n"
		"    <jobinfo>   Optional file to read.\n"
		"The default jobinfo is " DEFAULT_FILE ".\n"
		;
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
