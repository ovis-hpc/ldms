/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2016,2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file procnetdev.c
 * \brief /proc/net/dev data provider
 */
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#define PROC_FILE "/proc/net/dev"
static char *procfile = PROC_FILE;
#define NVARS 16
static char varname[][30] =
{"rx_bytes", "rx_packets", "rx_errs", "rx_drop", "rx_fifo", "rx_frame",
	"rx_compressed", "rx_multicast", "tx_bytes", "tx_packets", "tx_errs",
	"tx_drop", "tx_fifo", "tx_colls", "tx_carrier", "tx_compressed"};

int niface = 0;
//max number of interfaces we can include. TODO: alloc as added
#define MAXIFACE 21
static char iface[MAXIFACE][20];
static int mindex[MAXIFACE];

static ldms_set_t set;
#define SAMP "procnetdev"
static FILE *mf = NULL;
static int metric_offset;
static base_data_t base;

static ovis_log_t mylog;

struct kw {
	char *token;
	int (*action)(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg);
};

static int create_metric_set(base_data_t base)
{
	int rc;
	ldms_schema_t schema;
	char metric_name[128];
	int i, j;

	mf = fopen(procfile, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open " SAMP " file "
				"'%s'...exiting\n",
				procfile);
		return ENOENT;
	}

	/* Create a metric set of the required size */
	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = EINVAL;
		goto err;
	}

	/* Location of first metric from proc file */
	metric_offset = ldms_schema_metric_count_get(schema);

	/* Use all specified ifaces whether they exist or not. These will be
	   populated with 0 values for non existent ifaces. The metrics will appear
	   in the order of the ifaces as specified */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wformat-truncation"
	for (i = 0; i < niface; i++){
		for (j = 0; j < NVARS; j++){
			snprintf(metric_name, 128, "%s#%s", varname[j], iface[i]);
			rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
			if (rc < 0) {
				rc = ENOMEM;
				goto err;
			}
			if (j == 0)
				mindex[i] = rc;
		}
	}
#pragma GCC diagnostic pop

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

	for (i = 0; i < ARRAY_SIZE(deprecated); i++){
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
	return "config name=" SAMP " ifaces=<ifs>\n" \
		BASE_CONFIG_USAGE \
		"    <ifs>           A comma-separated list of interface names (e.g. eth0,eth1)\n"
		"                    Order matters. All ifaces will be included\n"
		"                    whether they exist of not up to a total of MAXIFACE\n";
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char* ifacelist = NULL;
	char* pch = NULL;
	char *saveptr = NULL;
	char *ivalue = NULL;
	void *arg = NULL;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}

	if (set) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	ivalue = av_value(avl, "ifaces");
	if (!ivalue) {
		ovis_log(mylog, OVIS_LERROR,SAMP ": config missing argument ifaces=namelist\n");
		goto err;
	}
	ifacelist = strdup(ivalue);
	if (!ifacelist) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": out of memory\n");
		goto err;
	}
	pch = strtok_r(ifacelist, ",", &saveptr);
	while (pch != NULL){
		if (niface >= (MAXIFACE-1)) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": too many ifaces: <%s>\n",
				pch);
			goto err;
		}
		snprintf(iface[niface], 20, "%s", pch);
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": added iface <%s>\n", iface[niface]);
		niface++;
		pch = strtok_r(NULL, ",", &saveptr);
	}
	free(ifacelist);
	ifacelist = NULL;

	if (niface == 0)
		goto err;


	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base){
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}

	return 0;

 err:
	if (ifacelist)
		free(ifacelist);
	base_del(base);
	return rc;

}

static int sample(ldmsd_plug_handle_t handle)
{
	char *s;
	char lbuf[256];
	char curriface[20];
	union ldms_value v[NVARS];
	int i, j, metric_no;

	if (!set){
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	if (!mf)
		mf = fopen(procfile, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Could not open /proc/net/dev file "
				"'%s'...exiting\n", procfile);
		return ENOENT;
	}

	base_sample_begin(base);
	metric_no = metric_offset;
	fseek(mf, 0, SEEK_SET); //seek should work if get to EOF
	int usedifaces = 0;
	s = fgets(lbuf, sizeof(lbuf), mf);
	s = fgets(lbuf, sizeof(lbuf), mf);
	/* data */

	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		if (usedifaces == niface)
			continue; /* must get to EOF for seek to work */

		char *pch = strchr(lbuf, ':');
		if (pch != NULL){
			*pch = ' ';
		}

		int rc = sscanf(lbuf, "%s %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 "\n", curriface, &v[0].v_u64,
				&v[1].v_u64, &v[2].v_u64, &v[3].v_u64,
				&v[4].v_u64, &v[5].v_u64, &v[6].v_u64,
				&v[7].v_u64, &v[8].v_u64, &v[9].v_u64,
				&v[10].v_u64, &v[11].v_u64, &v[12].v_u64,
				&v[13].v_u64, &v[14].v_u64, &v[15].v_u64);
		if (rc != 17){
			ovis_log(mylog, OVIS_LINFO, SAMP ": wrong number of "
					"fields in sscanf\n");
			continue;
		}

		/*
		 * note: ifaces will be in the same order each time
		 * so we can just include/skip w/o have to keep track of
		 * which on we are on
		 */
		for (j = 0; j < niface; j++){
			if (strcmp(curriface, iface[j]) == 0){ /* NOTE: small number so no conflicts (eg., eth1 and eth10) */
				metric_no = mindex[j];
				for (i = 0; i < NVARS; i++){
					ldms_metric_set(set, metric_no++, &v[i]);
				}
				usedifaces++;
				break;
			} /* end if */
		} /* end for*/
	} while (s);

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
	if (mf)
		fclose(mf);
	mf = NULL;
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
