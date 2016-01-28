/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2105 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010-2105 Sandia Corporation. All rights reserved.
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
 * \file procnfs.c
 * \brief /proc/net/rpc/nfs data provider
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
#include "ldms.h"
#include "ldmsd.h"

/**
 * File: /proc/net/rpc/nfs
 *
 * Gets the following selected data items:
 *
 * Second line:
 * rpc 2 numeric fields
 * field1: Total number of RPC calls to NFS,
 * field2: Number of times a call had to be retransmitted due to a timeout while waiting for a reply from server
 *
 *  Fourth line:
 * proc3 23 numeric fields:
 * field3: getattr
 * field4: setattr
 * field5: lookup
 * field6: access
 * field7: readlink
 * field8: read
 * field9: write
 * field10: create
 * field11: mkdir
 * field12: symlink
 * field13: mknod
 * field14: remove
 * field15: rmdir
 * field16: rename
 * field17: link
 * field18: readdir
 * field19: readdirplus
 * field20: fsstat
 * field21: fsinfo
 * field22: pathconf
 * field23: commit
 */

#define PROC_FILE "/proc/net/rpc/nfs"
static char *procfile = PROC_FILE;

#define MAXOPTS 2

static char* varnames[MAXOPTS][21] = {
	{ "numcalls", "retransmitts"},
	{ "getattr", "setattr", "lookup", "access",
		"readlink", "read", "write", "create",
		"mkdir", "symlink", "mknod", "remove",
		"rmdir", "rename", "link", "readdir",
		"readdirplus", "fsstat", "fsinfo", "pathconf",
		"commit" }
};

static int numvars[MAXOPTS] = { 2, 21 };

static ldms_set_t set;
static ldms_schema_t schema;
static char *default_schema_name = "procnfs";
static FILE *mf;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static uint64_t compid;
static uint64_t jobid;
static int metric_offset = 2;

static ldms_set_t get_set()
{
	return set;
}

static int create_metric_set(const char *path, char* schema_name)
{
	int rc;
	int i, j;
	union ldms_value v;
	char metric_name[128];

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "Could not open /proc/net/rpc/nfs file "
				"'%s'...exiting\n",
				procfile);
		return ENOENT;
	}

	/* Create a metric set of the required size */
	schema = ldms_schema_new(schema_name);
	if (!schema) {
		fclose(mf);
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_metric_add(schema, "job_id", LDMS_V_U64);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}

	/* Make sure these are added in the order they will appear in the file */
	for (i = 0; i < MAXOPTS; i++) {
		for (j = 0; j < numvars[i]; j++) {
			snprintf(metric_name, 127, "%s", varnames[i][j]);
			rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
			if (rc < 0) {
				rc = ENOMEM;
				goto err;
			}
		}
	}
	set = ldms_set_new(path, schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	//add specialized metrics
	v.v_u64 = compid;
	ldms_metric_set(set, 0, &v);
	v.v_u64 = 0;
	ldms_metric_set(set, 1, &v);
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

static const char *usage(void)
{
	return  "config name=procnfs producer=<prod_name> instance=<inst_name> [component_id=<compid> schema=<sname>]\n"
		"    <prod_name>     The producer name\n"
		"    <inst_name>     The instance name\n"
		"    <compid>        Optional unique number identifier. Defaults to zero.\n"
		"    <sname>         Optional schema name. Defaults to 'procnfs'\n";
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
			msglog(LDMSD_LERROR, "procnfs: config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}


/**
 * \brief Configuration
 *
 * config name=procnfs producer_name=<comp_id> instance_name=<instance_name> [component_id=<compid> schema=<sname>]
 *     producer_name      The producer id value.
 *     instance_name    The set name.
 *     component_id     The component id. Defaults to zero
 *     sname            Optional schema name. Defaults to meminfo
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *sname;
	char *value;
	void *arg = NULL;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}

	if (set) {
		msglog(LDMSD_LERROR, "procnfs: Set already created.\n");
		return EINVAL;
	}

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "procnfs: missing 'producer'\n");
		return ENOENT;
	}

	value = av_value(avl, "component_id");
	if (value)
		compid = (uint64_t)(atoi(value));
	else
		compid = 0;

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, "procnfs: missing 'instance'\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0){
		msglog(LDMSD_LERROR, "meminfo: schema name invalid.\n");
		return EINVAL;
	}


	rc = create_metric_set(value, sname);
	if (rc) {
		msglog(LDMSD_LERROR, "procnfs: failed to create the metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);
	return 0;

}
#define LINE_FMT "%s %s %s %" PRIu64 " %" PRIu64 " %" PRIu64 " %" \
	PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" \
PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" \
PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" \
	PRIu64 " %" PRIu64 " %" PRIu64 " %s\n"

static int sample(void)
{
	int rc, i;
	char *s;
	char lbuf[256];
	union ldms_value v[23];

	if (!set) {
		msglog(LDMSD_LDEBUG, "procnfs: plugin not initialized\n");
		return EINVAL;
	}
	ldms_transaction_begin(set);

	fseek(mf, 0, SEEK_SET);
	/*
	 * Format of the file is well known --
	 * We want lines 1 and 3 (starting with 0)
	 */
	int currlinenum = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		char junk[5][100];
		switch (currlinenum) {
			case 1:
				rc = sscanf(lbuf, "%s %" PRIu64 " %" PRIu64 "%s\n",
						junk[0], &v[0].v_u64, &v[1].v_u64, junk[1]);
				if (rc != 4) {
					rc = EINVAL;
					goto out;
				}
				ldms_metric_set(set, (0 + metric_offset), &v[0]);
				ldms_metric_set(set, (1 + metric_offset), &v[1]);
				break;
			case 3:
				rc = sscanf(lbuf, LINE_FMT,
						junk[0], junk[1], junk[2], &v[2].v_u64,
						&v[3].v_u64, &v[4].v_u64, &v[5].v_u64,
						&v[6].v_u64, &v[7].v_u64, &v[8].v_u64,
						&v[9].v_u64, &v[10].v_u64, &v[11].v_u64,
						&v[12].v_u64, &v[13].v_u64,	&v[14].v_u64,
						&v[15].v_u64, &v[16].v_u64, &v[17].v_u64,
						&v[18].v_u64, &v[19].v_u64, &v[20].v_u64,
						&v[21].v_u64, &v[22].v_u64, junk[3]);
				if (rc < 24) {
					rc = EINVAL;
					goto out;
				}
				for (i = 2; i < 23; i++)
					ldms_metric_set(set, (i+metric_offset), &v[i]);
				break;
			default:
				break;
		}
		currlinenum++;
	} while (s); /* must get to EOF for the switch to work */
	rc = 0;
out:
	ldms_transaction_end(set);
	return rc;
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


static struct ldmsd_sampler procnfs_plugin = {
	.base = {
		.name = "procnfs",
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
	return &procnfs_plugin.base;
}
