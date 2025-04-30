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
 * \file ipmireader.c
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
#define IPMIFAILVALUE -8888
#define IPMIRETRYDEFAULT 600
#define IPMICACHECMDR "ipmitool -H%s -U%s -P%s sdr dump -N 1 -R 1 %s 2>/dev/null"
#define IPMICMDWR "ipmitool -H%s -U%s -P%s sdr -S %s -N 1 -R 1 2>/dev/null"
#define IPMICMDWOR "ipmitool -H%s -U%s -P%s sdr -N 1 -R 1 2>/dev/null"
//IPMICMD "ipmitool -Hcn1-ipmi -UXXX -PYYY sdr"
static char cmd[MAXIPMICMDLEN];
static ldms_set_t set = NULL;
#define SAMP "ipmireader"
static char* defaultusername = "admin";
static char* defaultpassword = "password";
static int retry;
static int metric_offset;
static int metric_total;
static base_data_t base;

static ovis_log_t mylog;

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

static int create_commands(char* hostname, char* username, char* password, char* sdrcache)
{
	FILE *mf;
	char lbuf[256];
	char cmdbuf[MAXIPMICMDLEN];
	char *s;
	int rc = 0;
	int i;

	if (sdrcache != NULL){
		//redirect stderr so it goes somewhere
		i = snprintf(cmdbuf, MAXIPMICMDLEN, IPMICACHECMDR, hostname,
			     username, password, sdrcache);
		if ((i <= 0) || (i >= MAXIPMICMDLEN)){
			ovis_log(mylog, OVIS_LERROR, "arguments too long for cache command length",
			       "...exiting sampler\n");
			return EINVAL;
		}

		mf = popen(cmdbuf, "r");
		if (!mf) {
			ovis_log(mylog, OVIS_LERROR,
			       "Could not call the " SAMP " cmd '%s'\n", cmd);
			rc = ENOENT;
			goto err;
		}

		/* if the cache command does not succeed, this will still work, but without the cache
		 * FIXME: we would like to know that it did not succeed, and continue. Does not
		 * look like there is something to catch with perror, but maybe there is something we
		 * can get with stderr output
		 */

		/*
		 * this seems to be necessary to give the cache file time to get written out.
		 * if it isnt written in time, the command will still work, and hopefully it will be
		 * written by the time of the sample call
		 */

		sleep(2);

		//check if the cache file exists and give a warning if it does not
		if (access( sdrcache, F_OK ) == -1 ) {
			ovis_log(mylog, OVIS_LERROR, SAMP "Could not create cachefile '%s'. continuing without \n",
			       sdrcache);

		}

		//this will work, but print out error messages if the cache command does not succeed
		i = snprintf(cmd, MAXIPMICMDLEN, IPMICMDWR, hostname,
			     username, password, sdrcache);
		if ((i <= 0) || (i >= MAXIPMICMDLEN)){
			ovis_log(mylog, OVIS_LERROR, "arguments too long for command length",
			       "...exiting sampler\n");
			return EINVAL;
		}
	} else {
		i = snprintf(cmd, MAXIPMICMDLEN, IPMICMDWOR, hostname,
			     username, password);
		if ((i <= 0) || (i >= MAXIPMICMDLEN)){
			ovis_log(mylog, OVIS_LERROR, "arguments too long for command length",
			       "...exiting sampler\n");
			return EINVAL;
		}
	}

	//check command works
	mf = popen(cmd, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR,
		       "Could not call the " "cmd '%s'\n", cmd);
		rc = ENOENT;
		goto err;
	}
	// if it fails the first line will have the wrong format
	s = fgets(lbuf, sizeof(lbuf), mf);
	if (!s){
		ovis_log(mylog, OVIS_LERROR, SAMP "no return from call\n");
		rc = ENOENT;
		goto err;
	}
	if (strchr(lbuf, '|') == NULL){
		ovis_log(mylog, OVIS_LERROR, "bad return from command\n");
		rc = ENOENT;
		goto err;
	}

        rc = 0;

 err:

	pclose(mf);
	return rc;
}


static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	FILE* mf;
	char lbuf[256];
	char *name, *value, *status, *ptr;
	char *newname;
	char *current_pos;
	char *s;
	int rc;


	mf = popen(cmd, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR,
		       "Could not call the " "cmd '%s'\n", cmd);
		return ENOENT;
	}

	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		//rc = errno;
		rc = EINVAL; //want this to be well-defined, different than other error.
		goto err;
	}

	/* Location of first metric from output */
	metric_offset = ldms_schema_metric_count_get(schema);
	metric_total = metric_offset;

	/*
	 * Process the file to define all the metrics.
	 * Any badly formatted line, will abort the entire parsing.
	 */
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		name = strchr(lbuf, '|');
		if (!name){
			ovis_log(mylog, OVIS_LERROR, SAMP ": Data line format problem <%s>.\n",
			       lbuf);
			rc = ENOENT;
			goto err;
		}

		name = strtok_r(lbuf, "|", &ptr);
		if (!name) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": Data line format problem <%s>.\n",
			       lbuf);
			rc = ENOENT;
			goto err;
		}
		newname = trim_whitespace(name);
		// replace space with underscores
		for ( ; (current_pos = strchr(newname,' '))!=NULL;
		     *current_pos = '_');

		value = strtok_r(NULL, "|", &ptr);
		if (!value) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": Data line format problem <%s>.\n",
			       lbuf);
			rc = ENOENT;
			goto err;
		}
		//we only need the name at this point, not the value

		// this will be unused
		status = strtok_r(NULL, "|", &ptr);
		if (!status) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": Data line format problem <%s>.\n",
			       lbuf);
			rc = ENOENT;
			goto err;
		}

		rc = ldms_schema_metric_add(schema, newname, LDMS_V_F32);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
		metric_total++;
	} while (s);
	pclose(mf);

	set = base_set_new(base);
	if (!set) {
		//want this to be well-defined, different than other error.
		//		rc = errno;
		rc = EINVAL;
		goto err;
	}

	return 0;

err:
	/* don't think there is anything to delete here.*/
	metric_total = 0;
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
			ovis_log(mylog, OVIS_LERROR, SAMP ": config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=" SAMP " address=<address> username=<username> password=<password> sdrcache=<cachefile> retry=<sec> " BASE_CONFIG_USAGE
		"    address       address of the host to contact. H flag in the ipmitool command (e.g., cn1-ipmi).\n"
		"    username      username for the ipmi query. U flag in the ipmitool command (default 'admin').\n"
		"    password      password for the ipmi query. P flag in the ipmitool command (default 'password').\n"
		"    sdrcache      output file for sdr cache file, to improve performance. Optional.\n"
		"    retry         interval to retry creating set if fails (e.g., host down). (default 600 sec).\n";

	//FIXME: make an optional command to refresh the cache file.
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *hostname, *username, *password, *sdrcache, *retrycmd;
	int rc;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, NULL);
	if (rc != 0){
		return rc;
	}

	hostname = av_value(avl, "address");
	if (!hostname){
		ovis_log(mylog, OVIS_LERROR, SAMP ": config missing hostname.\n");
		rc = EINVAL;
		goto err;
	}

	// default
	username = av_value(avl, "username");
	if (username == NULL)
		username = defaultusername;

	// default
	password = av_value(avl, "password");
	if (password == NULL)
		password = defaultpassword;

	//optional
	sdrcache = av_value(avl, "sdrcache");

	//optional
	retrycmd = av_value(avl, "retry");
	if (retrycmd == NULL)
		retry = IPMIRETRYDEFAULT;
	else
		retry = atoi(retrycmd);


	rc = 0;
	do {
		// if cannot create commands, retry indefinitely
		if (rc != 0)
			sleep(retry);

		rc = create_commands(hostname, username, password, sdrcache);
		ovis_log(mylog, OVIS_LERROR, SAMP "cannot create command. sleeping and retrying\n");
		if (rc == EINVAL)
			goto err;
	} while (rc != 0);


	rc = 0;
	do {
		//if cannot create metric set, retry indefinitely
		if (rc != 0)
			sleep(retry);

		base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
		if (!base) {
			rc = errno;
			goto err;
		}

		rc = create_metric_set(base);
		if (rc != 0) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": failed to create a metric set.\n");
			if (base)
				base_del(base);
			if (rc != ENOENT)
				goto err;
		}
		//otherwise it will retry
	} while (rc != 0);

	return rc;
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
	char *name, *value, *status, *ptr, *next;
	char *newvalue;
	float fvalue;
	union ldms_value v;
	int i;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	metric_no = metric_offset;

	// assume order remains the same
	mf = popen(cmd, "r");
	if (mf == NULL){
		ovis_log(mylog, OVIS_LERROR, "Could not call the " SAMP " cmd "
		       "'%s'...not sampling\n", cmd);
		goto err;
	}

	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		name = strtok_r(lbuf, "|", &ptr);
		if (!name) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": Data line format problem <%s>.\n",
			       lbuf);
			goto err;
		}

		value = strtok_r(NULL, "|", &ptr);
		if (!value) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": Data line format problem <%s>.\n",
			       lbuf);
			goto err;
		}

		newvalue = trim_whitespace(value);
		fvalue = IPMIERRVALUE;
		if (isdigit(newvalue[0])){
			fvalue = strtof(newvalue, &next); // FIXME --need to check for hex
		}

		// this will be unused
		status = strtok_r(NULL, "|", &ptr);
		if (!status) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": Data line format problem <%s>.\n",
			       lbuf);
			goto err;
		}
		v.v_f = fvalue;
		ldms_metric_set(set, metric_no, &v);
		metric_no++;
	} while (s);

	if (mf)
		pclose(mf);

	base_sample_end(base);

	return 0;
 err:
	// in case of err or cannot open the file, set all the metrics to the FAIL value
	v.v_f = IPMIFAILVALUE;
	for (i = metric_offset; i < metric_total; i++){
		ldms_metric_set(set, i, &v);
	}

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
