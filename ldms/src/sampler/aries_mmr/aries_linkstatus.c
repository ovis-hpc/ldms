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
 * \file aries_linkstatus
 * \brief link metrics status from gpcdr
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
#include "../ldms_jobid.h"

#define LINKSTATUS_FILE "/sys/devices/virtual/gni/gpcdr0/metricsets/links/metrics"
#define NUMROW_TILE 5
#define NUMCOL_TILE 8

static char *lsfile;
static char *lrfile;
static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;
#define SAMP "aries_linkstatus"
static char *default_schema_name = SAMP;
static uint64_t compid;

static int metric_offset = 1;
LJI_GLOBALS;

static int create_metric_set(const char *instance_name, char* schema_name)
{
	int rc, i,j;
	uint64_t metric_value;
	union ldms_value v;
	char *s;
	char lbuf[256];
	char metric_name[128];

	FILE *mf = fopen(lsfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "Could not open the " SAMP " file "
				"'%s'...exiting sampler\n", lsfile);
		return ENOENT;
	}
	fclose(mf);


	mf = fopen(lrfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "Could not open the " SAMP " file "
		       "'%s'...exiting sampler\n", lrfile);
		return ENOENT;
	}
	fclose(mf);

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
	//well known aries tiles dimension. send before receive
	for (i = 0; i < NUMROW_TILE; i++){
		snprintf(lbuf,255, "sendlinkstatus_r%d", i);
		rc = ldms_schema_metric_array_add(schema, lbuf, LDMS_V_U8_ARRAY, NUMCOL_TILE);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
	}
	//well known aries tiles dimension
	for (i = 0; i < NUMROW_TILE; i++){
		snprintf(lbuf,255, "recvlinkstatus_r%d", i);
		rc = ldms_schema_metric_array_add(schema, lbuf, LDMS_V_U8_ARRAY, NUMCOL_TILE);
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

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP " producer=<prod_name> instance=<inst_name> [file_send=<send_file_name> file_recv=<recv_file_name> component_id=<compid> schema=<sname> with_jobid=<jid>]\n"
		"    <prod_name>  The producer name\n"
		"    <inst_name>  The instance name\n"
                "    <send_file_name>  Optional location of the gpcdr file to read for send link status\n",
                "    <recv_file_name>  Optional location of the gpcdr file to read for recv link status\n",
		"    <compid>     Optional unique number identifier. Defaults to zero.\n"
		LJI_DESC
		"    <sname>      Optional schema name. Defaults to '" SAMP "'\n";
}

/**
 * \brief Configuration
 *
 * config name=aries_linkstatus producer=<name> instance=<instance_name> [component_id=<compid> file_send=<send_file_name> file_recv=<recv_file_name> schema=<sname>] [with_jobid=<jid>]
 *     producer_name    The producer id value.
 *     instance_name    The set name.
 *     component_id     The component id. Defaults to zero
 *     send_file_name   Optional send_file name. Defaults to well known location.
 *     recv_file_name   Optional recv_file name. Defaults to well known location.
 *     sname            Optional schema name. Defaults to aries_linkstatus
 *     jid              lookup jobid or report 0.
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *sname;
	char *fname;
	void * arg = NULL;
	int rc;

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

	fname = av_value(avl, "file_send");
	if (!fname)
		lsfile = strdup(LINKSTATUS_FILE);
	else
		lsfile = strdup(fname);
	if (strlen(lsfile) == 0){
		msglog(LDMSD_LERROR, SAMP ": file name invalid.\n");
		return EINVAL;
	}

	fname = av_value(avl, "file_recv");
	if (!fname)
		lrfile = strdup(LINKSTATUS_FILE);
	else
		lrfile = strdup(fname);
	if (strlen(lrfile) == 0){
		msglog(LDMSD_LERROR, SAMP ": file name invalid.\n");
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
	int rc, row, col;
	int metric_no;
	char *s;
	char lbuf[256];
	char metric_name[128];

	union ldms_value v;


	if (!set) {
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	ldms_transaction_begin(set);

	LJI_SAMPLE(set, 1);

	/* doing this infrequently, so open and close each time */
	FILE *mf = fopen(lsfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "Could not open the " SAMP " file "
				"'%s'\n", lsfile);
		goto err;
	}

	/* NOTE: not ensuring that we have any of these.
	   This is a double pass if they are the same file */
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		//send before receive
		rc = sscanf(lbuf, "sendlinkstatus:0%1d%1d %"PRIu8,
			    &row, &col, &v.v_u8);
		if (rc == 3) {
			if ((row >= NUMROW_TILE) || (col >= NUMCOL_TILE)){
				msglog(LDMSD_LDEBUG,
				       SAMP ": bad row col '%s'\n", lbuf);
			} else {
				metric_no = metric_offset+row;
				ldms_metric_array_set_val(set, metric_no,
							  col, &v);
			}
		}
	} while (s);
	fclose(mf);
	mf = NULL;


	mf = fopen(lrfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "Could not open the " SAMP " file "
				"'%s'\n", lrfile);
		goto err;
	}

	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "recvlinkstatus:0%1d%1d %"PRIu8,
			    &row, &col, &v.v_u8);
		if (rc == 3) {
			if ((row >= NUMROW_TILE) || (col >= NUMCOL_TILE)){
				msglog(LDMSD_LDEBUG,
				       SAMP ": bad row col '%s'\n", lbuf);
			} else {
                                //well known aries layout
				metric_no = metric_offset+NUMROW_TILE+row;
				ldms_metric_array_set_val(set, metric_no,
							  col, &v);
			}
		}
	} while (s);
	fclose(mf);
	mf = NULL;

	ldms_transaction_end(set);
	if (mf) fclose(mf);
	return 0;

err:
	ldms_transaction_end(set);
	if (mf) fclose(mf);
	return EINVAL;
}

static void term(struct ldmsd_plugin *self)
{
	//don't close the file handle since we open and close it each time
	if (lsfile)
		free(lsfile);
	lsfile = NULL;
	if (lrfile)
		free(lrfile);
	lrfile = NULL;
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler aries_linkstatus_plugin = {
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
	return &aries_linkstatus_plugin.base;
}
