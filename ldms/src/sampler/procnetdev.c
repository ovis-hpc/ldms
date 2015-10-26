/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010-2015 Sandia Corporation. All rights reserved.
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
#define MAXIFACE 5
static char iface[MAXIFACE][20];
static char mindex[MAXIFACE];

static ldms_set_t set;
static ldms_schema_t schema;
static char* default_schema_name = "procnetdev";
static FILE *mf = NULL;
static ldmsd_msg_log_f msglog;
static char *producer_name;


struct kw {
	char *token;
	int (*action)(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg);
};

static ldms_set_t get_set()
{
	return set;
}

static int create_metric_set(const char *instance_name, char* schema_name)
{
	union ldms_value v[NVARS];
	int rc;
	char *s;
	char lbuf[256];
	char metric_name[128];
	char curriface[20];
	int i, j;

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "Could not open /proc/net/dev file "
				"'%s'...exiting\n",
				procfile);
		return ENOENT;
	}

	/* Create a metric set of the required size */
	schema = ldms_schema_new(schema_name);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	/* Use all specified ifaces whether they exist or not. These will be
	   populated with 0 values for non existent ifaces. The metrics will appear
	   in the order of the ifaces as specified */

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

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err;
	}
	return 0;

err:

	if (mf)
		fclose(mf);
	mf = NULL;

	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;

	return rc;
}


/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set", "component_id"};
	int numdep = 2;

	for (i = 0; i < numdep; i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			msglog(LDMSD_LERROR, "procnetdev: config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(void)
{
	return
		"config name=procnetdev producer=<prod_name> instance=<inst_name> ifaces=<ifs> [schema=<sname>]\n"
		"    <prod_name>     The producer name\n"
		"    <inst_name>     The instance name\n"
		"    <ifs>           A comma-separated list of interface names (e.g. eth0,eth1)\n"
		"                    Order matters. All ifaces will be included\n"
		"                    whether they exist of not up to a total of MAXIFACE\n"
		"    <sname>         Optional schema name. Defaults to 'procnetdev'\n";
}

/**
 * \brief Configuration
 *
 *   config name=procnetdev producer=<prod_name> instance=<inst_name> ifaces=<ifs> [schema=<sname>]
 *     <prod_name>     The producer name
 *     <inst_name>     The instance name
 *     <ifs>           A comma-separated list of interface names (e.g. eth0,eth1)
 *     <sname>         Optional schema name. Defaults to 'procnetdev'
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *sname = NULL;
	char* ifacelist = NULL;
	char* pch = NULL;
	char *saveptr = NULL;
	char *ivalue = NULL;
	char *value = NULL;
	void *arg = NULL;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}

	if (set) {
		msglog(LDMSD_LERROR, "procnetdev: Set already created.\n");
		return EINVAL;
	}

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "procnetdev: missing 'producer'.\n");
		return ENOENT;
	}

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, "procnetdev: missing 'instance'.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0){
		msglog(LDMSD_LERROR, "meminfo: schema name invalid.\n");
		return EINVAL;
	}

	ivalue = av_value(avl, "ifaces");
	if (!ivalue) {
		msglog(LDMSD_LERROR,"procnetdev: config missing argument ifaces=namelist\n");
		goto err;
	}
	ifacelist = strdup(ivalue);
	pch = strtok_r(ifacelist, ",", &saveptr);
	while (pch != NULL){
		if (niface >= (MAXIFACE-1))
			goto err;
		snprintf(iface[niface], 20, "%s", pch);
		msglog(LDMSD_LDEBUG, "procnetdev: added iface <%s>\n", iface[niface]);
		niface++;
		pch = strtok_r(NULL, ",", &saveptr);
	}
	free(ifacelist);
	ifacelist = NULL;

	if (niface == 0)
		goto err;

	rc = create_metric_set(value, sname);
	if (rc) {
		msglog(LDMSD_LERROR, "procnetdev: failed to create a metric set.\n");
		goto err;
	}
	ldms_set_producer_name_set(set, producer_name);

	return 0;

 err:
	if (ifacelist)
		free(ifacelist);
	return rc;

}

static int sample(void)
{
	char *s;
	char lbuf[256];
	char curriface[20];
	union ldms_value v[NVARS];
	int i, j, metric_no;
	float dt;

	if (!set){
		msglog(LDMSD_LDEBUG, "procnetdev: plugin not initialized\n");
		return EINVAL;
	}

	if (!mf)
		mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "Could not open /proc/net/dev file "
				"'%s'...exiting\n", procfile);
		return ENOENT;
	}

	metric_no = 0;
	fseek(mf, 0, SEEK_SET); //seek should work if get to EOF
	int usedifaces = 0;
	s = fgets(lbuf, sizeof(lbuf), mf);
	s = fgets(lbuf, sizeof(lbuf), mf);
	/* data */

	ldms_transaction_begin(set);
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
			msglog(LDMSD_LINFO, "Procnetdev: wrong number of "
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
	ldms_transaction_end(set);

	return 0;
}


static void term(void)
{
	if (mf)
		fclose(mf);
	mf = 0;
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;

	if (set)
		ldms_set_delete(set);
	set = NULL;
}


static struct ldmsd_sampler procnetdev_plugin = {
	.base = {
		.name = "procnetdev",
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
	return &procnetdev_plugin.base;
}
