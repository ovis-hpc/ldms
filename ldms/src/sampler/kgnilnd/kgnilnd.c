/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010,2014-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010,2014-2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file kgnilnd.c
 * \brief /proc/kgnilnd data provider
 *
 * Unlike other samplers, we reopen the kgnilnd file each time we sample. If the open
 * fails, we still return success instead of an error so that that sampler will not
 * be stopped.
 */
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "sampler_base.h"

#define PROC_FILE "/proc/kgnilnd/stats"

static char *procfile = PROC_FILE;

static ovis_log_t mylog;

static ldms_set_t set = NULL;
static FILE *mf = 0;
#define SAMP "kgnilnd"
static int metric_offset = 0;
static int nmetrics = 0;
static base_data_t base;


static char *replace_space(char *s)
{
	char *s1;

	s1 = s;
	while ( *s1 ) {
		if ( isspace( *s1 ) ) {
			*s1 = '_';
		}
		++ s1;
	}
	return s;
}

struct kgnilnd_metric {
	int idx;
	uint64_t udata;
};

static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	uint64_t metric_value;
	union ldms_value v;
	char *s;
	char lbuf[256];
	char metric_name[128];
	int rc;

	mf = fopen(procfile, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open the " SAMP " file "
				"'%s'...aborting\n", procfile);
		return ENOENT;
	}

	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/* Location of first metric from proc/meminfo file */
	metric_offset = ldms_schema_metric_count_get(schema);

	/* Process the file to define all the metrics.*/
	if (fseek(mf, 0, SEEK_SET) != 0){
		ovis_log(mylog, OVIS_LERROR, "Could not seek in the kgnilnd file "
		       "'%s'...aborting\n", procfile);
		rc = EINVAL;
		goto err;
	}

	nmetrics = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		char* end = strchr(s, ':');
		if (end && *end) {
			*end = '\0';
			replace_space(s);
			if (sscanf(end + 1, " %" PRIu64 "\n", &metric_value) == 1) {
				rc = ldms_schema_metric_add(schema, s, LDMS_V_U64);
				if (rc < 0) {
					rc = ENOMEM;
					goto err;
				}
				nmetrics++;
			}
		} else {
			rc = ENOMEM;
			goto err;
		}
	} while (s);

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

err:
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
	int numdep = 1;

	for (i = 0; i < numdep; i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(mylog, OVIS_LERROR, "config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}


static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	void *arg;
	int rc = 0;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return EINVAL;
	}


	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base){
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}
	return 0;

err:
	base_del(base);
	return rc;
}

static int __kgnilnd_reset(){
	union ldms_value v;
	int i;

	v.v_u64 = 0;
	base_sample_begin(base);
	for (i = 0; i < nmetrics; i++){
		ldms_metric_set(set, (i+metric_offset), &v);
	}
	base_sample_end(base);
}

static int sample(ldmsd_plug_handle_t handle)
{
	int metric_no, rc;
	char *s;
	char lbuf[256];
	union ldms_value v;

	if (!set){
	  ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
	  /* let this one fail */
	  //return 0;
	  return EINVAL;
	}

	if (!mf){
		mf = fopen(procfile, "r");
		if (!mf) {
			ovis_log(mylog, OVIS_LERROR, "Could not open the kgnilnd file "
			       "'%s'...\n", procfile);
			__kgnilnd_reset();
			/* return success so it wont exit. try again next time */
			// return ENOENT;
			return 0;
		}
	}

	if (fseek(mf, 0, SEEK_SET) != 0){
		/* perhaps the file handle has become invalid.
		 * close it so it will reopen it on the next round.
		 */
		ovis_log(mylog, OVIS_LERROR, "Could not seek in the " SAMP " file "
		       "'%s'...closing filehandle\n", procfile);
		fclose(mf);
		mf = 0;
		__kgnilnd_reset();
		/* return success so it wont exit. try again next time */
//		return EINVAL;
		return 0;
	}

	base_sample_begin(base);
	metric_no = metric_offset;
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		char* end = strchr(s, ':');
		if (end && *end) {
			*end = '\0';
			replace_space(s);
			if (sscanf(end + 1, " %" PRIu64 "\n", &v.v_u64) == 1) {
				ldms_metric_set(set, metric_no, &v);
				metric_no++;
			}
		} else {
			rc = EINVAL;
			goto out;
		}
	} while (s);
	rc = 0;
out:
	base_sample_end(base);
	return 0;
}

static void term(ldmsd_plug_handle_t handle)
{
	if (mf)
		fclose(mf);
	mf = NULL;
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP BASE_CONFIG_USAGE;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
