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
#include "sampler_base.h"

#define LINKSTATUS_FILE "/sys/devices/virtual/gni/gpcdr0/metricsets/links/metrics"
#define NUMROW_TILE 5
#define NUMCOL_TILE 8

static char *lsfile;
static char *lrfile;
static ldms_set_t set = NULL;
#define SAMP "aries_linkstatus"

static ovis_log_t mylog;

static base_data_t base;
static int metric_offset;

static int create_metric_set(base_data_t base)
{
	int rc, i,j;
	uint64_t metric_value;
	union ldms_value v;
	char *s;
	char lbuf[256];
	char metric_name[128];
	ldms_schema_t schema;

	FILE *mf = fopen(lsfile, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open the " SAMP " file "
				"'%s'...exiting sampler\n", lsfile);
		return ENOENT;
	}
	fclose(mf);


	mf = fopen(lrfile, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open the " SAMP " file "
		       "'%s'...exiting sampler\n", lrfile);
		return ENOENT;
	}
	fclose(mf);

	schema = base_schema_new(base);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}
	metric_offset = ldms_schema_metric_count_get(schema);

	/* Well known aries tiles dimension. send before receive */
	for (i = 0; i < NUMROW_TILE; i++) {
		snprintf(lbuf,255, "sendlinkstatus_r%d", i);
		rc = ldms_schema_metric_array_add(schema, lbuf, LDMS_V_U8_ARRAY, NUMCOL_TILE);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
	}
	/* Well known aries tiles dimension */
	for (i = 0; i < NUMROW_TILE; i++) {
		snprintf(lbuf,255, "recvlinkstatus_r%d", i);
		rc = ldms_schema_metric_array_add(schema, lbuf, LDMS_V_U8_ARRAY, NUMCOL_TILE);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
	}

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

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP BASE_CONFIG_USAGE
		" [file_send=<send_file_name> file_recv=<recv_file_name>\n"
		"    <send_file_name>  Optional location of the gpcdr file to read for send link status\n"
		"    <recv_file_name>  Optional location of the gpcdr file to read for recv link status\n";
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *fname;
	void * arg = NULL;
	int rc;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	base = base_config(avl, self->inst_name, SAMP, mylog);
	if (!base)
		return EINVAL;

	fname = av_value(avl, "file_send");
	if (!fname)
		lsfile = strdup(LINKSTATUS_FILE);
	else
		lsfile = strdup(fname);
	if (strlen(lsfile) == 0){
		ovis_log(mylog, OVIS_LERROR, "file name invalid.\n");
		goto err;
	}

	fname = av_value(avl, "file_recv");
	if (!fname)
		lrfile = strdup(LINKSTATUS_FILE);
	else
		lrfile = strdup(fname);
	if (strlen(lrfile) == 0){
		ovis_log(mylog, OVIS_LERROR, "file name invalid.\n");
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
	base = NULL;
	return EINVAL;
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
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);

	/* doing this infrequently, so open and close each time */
	FILE *mf = fopen(lsfile, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open the " SAMP " file "
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
				ovis_log(mylog, OVIS_LDEBUG,
				       "bad row col '%s'\n", lbuf);
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
		ovis_log(mylog, OVIS_LERROR, "Could not open the " SAMP " file "
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
				ovis_log(mylog, OVIS_LDEBUG,
				       "bad row col '%s'\n", lbuf);
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

	base_sample_end(base);
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
	if (set)
		ldms_set_delete(set);
	set = NULL;
	base_del(base);
	base = NULL;
	if (mylog)
		ovis_log_destroy(mylog);
}

static struct ldmsd_sampler aries_linkstatus_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.sample = sample,
};

struct ldmsd_plugin *get_plugin()
{
	int rc;
	mylog = ovis_log_register("sampler."SAMP, "Message for the " SAMP " plugin");
	if (!mylog) {
		rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the log subsystem "
					"of '" SAMP "' plugin. Error %d\n", rc);
	}
	set = NULL;
	return &aries_linkstatus_plugin.base;
}
