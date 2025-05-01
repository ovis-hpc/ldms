/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file ipmisensors.c
 * \brief ipmi data provider
 */
#define _GNU_SOURCE
#include <ctype.h>
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

#define MAXIPMICMDLEN 1024
#define IPMIERRVALUE -9999
#define IPMISENSORSCMDWOR "ipmi-sensors -h%s -u%s -p%s --comma-separated-output --no-header-output --session-timeout=500 --retransmission-timeout=250 --quiet-cache --no-sensor-type 2>/dev/null"
static char cmd[MAXIPMICMDLEN];
static ldms_set_t set = NULL;
static ovis_log_t mylog;
#define SAMP "ipmisensors"
static char* defaultusername = "admin";
static char* defaultpassword = "password";
static int metric_offset;
static base_data_t base;

#if 0
static char* trim_whitespace(char *str)
{
	char *end;

	//leading
	while (isspace((unsigned char)*str)) str++;

	if (*str == 0)
		return str;

	end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) end--;

	// Write new null terminator character
	end[1] = '\0';

	return str;
}
#endif

static int create_command(char* hostname, char* username, char* password)
{
	FILE *mf;
	char lbuf[256];
	int i;

	i = snprintf(cmd, MAXIPMICMDLEN, IPMISENSORSCMDWOR, hostname,
		     username, password);
	if ((i <= 0) || (i >= MAXIPMICMDLEN)){
		ovis_log(mylog, OVIS_LERROR, SAMP " arguments too long for command length",
		       "...exiting sampler\n");
		return EINVAL;
	}

	//check command works
	mf = popen(cmd, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not call the " SAMP " cmd "
				"'%s'...exiting sampler\n", cmd);
		return ENOENT;
	}
	// if it fails the first line will have the wrong format
	char *s = fgets(lbuf, sizeof(lbuf), mf);
	if (!s || strchr(lbuf, ',') == NULL){
		ovis_log(mylog, OVIS_LERROR, SAMP " bad arguments for command",
		       "...exiting sampler\n");
		return EINVAL;
	}
	pclose(mf);

	return 0;
}


static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	FILE* mf;
	char lbuf[256];
	char *name, *ptr;
	char *current_pos;
	char *s;
	int rc;


	mf = popen(cmd, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not call the " SAMP " cmd "
				"'%s'...exiting sampler\n", cmd);
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

	/* Location of first metric from output */
	metric_offset = ldms_schema_metric_count_get(schema);

	/*
	 * Process the file to define all the metrics.
	 * FIXME: Not checking the format of the line.
	 * 2,Inlet ambient,Temperature,26.00,C,'OK'
	 */
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		//first one is a number
		name = strtok_r(lbuf, ",", &ptr);
		if (!name) {
			ovis_log(mylog, OVIS_LERROR, "Data line format problem <%s>.\n",
			       lbuf);
			rc = EINVAL;
			goto err;
		}

		//second one is the name
		name = strtok_r(NULL, ",", &ptr);
		if (!name) {
			ovis_log(mylog, OVIS_LERROR, "Data line format problem <%s>.\n",
			       lbuf);
			rc = EINVAL;
			goto err;
		}

		// replace space with underscores
		for ( ; (current_pos = strchr(name,' '))!=NULL;
		     *current_pos = '_');

		rc = ldms_schema_metric_add(schema, name, LDMS_V_F32);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
	} while (s);
	pclose(mf);

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

 err:
	if (mf)
		pclose(mf);
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
			ovis_log(mylog, OVIS_LERROR, "config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " address=<address> username=<username> password=<password> " BASE_CONFIG_USAGE
		"    address       address of the host to contact. h flag in the ipmi-sensors command (e.g., cn1-ipmi).\n"
		"    username      username for the ipmi query. u flag in the ipmi-sensors command (default 'admin').\n"
		"    password      password for the ipmi query. p flag in the ipmi-sensors command (default 'password').\n";
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *hostname, *username, *password;
	int rc;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, NULL);
	if (rc != 0){
		return rc;
	}

	hostname = av_value(avl, "address");
	if (!hostname){
		ovis_log(mylog, OVIS_LERROR, "config missing hostname.\n");
		rc = EINVAL;
		goto err;
	}

	username = av_value(avl, "username");
	if (username == NULL){
		username = defaultusername;
	}

	password = av_value(avl, "password");
	if (password == NULL){
		password = defaultpassword;
	}

	rc = create_command(hostname, username, password);
	if (rc != 0)
		goto err;


	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base) {
		rc = errno;
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

static int sample(ldmsd_plug_handle_t handle)
{
	int metric_no;
	char *s;
	char lbuf[256];
	FILE *mf;
	char *junk, *value, *ptr, *next;
	float fvalue;
	union ldms_value v;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	metric_no = metric_offset;

	// assume order remains the same
	mf = popen(cmd, "r");
	if (mf == NULL){
		ovis_log(mylog, OVIS_LERROR, "Could not call the " SAMP " cmd "
		       "'%s'...not sampling\n", cmd);
		goto out;
	}

	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		junk = strtok_r(lbuf, ",", &ptr);
                if (!junk) {
                        ovis_log(mylog, OVIS_LERROR, "Data line format problem <%s>.\n",
                               lbuf);
                        goto out;
		}

		junk = strtok_r(NULL, ",", &ptr);
		if (!junk) {
			ovis_log(mylog, OVIS_LERROR, "Data line format problem <%s>.\n",
			       lbuf);
			goto out;
		}

		value = strtok_r(NULL, ",", &ptr);
		if (!value) {
			ovis_log(mylog, OVIS_LERROR, "Data line format problem <%s>.\n",
			       lbuf);
			goto out;
		}
		fvalue = IPMIERRVALUE;
		if (isdigit(value[0])){
			fvalue = strtof(value, &next); // FIXME --need to check for hex
		}

		v.v_f = fvalue;
		ldms_metric_set(set, metric_no, &v);
		metric_no++;
	} while (s);

 out:
	if (mf)
		pclose(mf);

	base_sample_end(base);
	return 0;
}

static void term(ldmsd_plug_handle_t handle)
{

	cmd[0] = '\0';
	if (base)
		base_del(base);
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
