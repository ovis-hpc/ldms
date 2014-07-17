/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
static uint64_t counter;
uint64_t comp_id;

static ldms_set_t get_set()
{
	return set;
}

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, metric_count;
	int i, j;
	char metric_name[128];

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog("Could not open /proc/net/rpc/nfs file '%s'...exiting\n",
				procfile);
		return ENOENT;
	}

	/*
	 * Determine the metric set size.
	 */
	tot_meta_sz = 0;
	tot_data_sz = 0;

	/* Don't need to look at the file since we have all the name info
	 * NOTE: make sure these are added in the order they will appear in the file
	 */
	metric_count = 0;
	for (i = 0; i < MAXOPTS; i++) {
		for (j = 0; j < numvars[i]; j++) {
			snprintf(metric_name,127,"%s",varnames[i][j]);
			rc = ldms_get_metric_size(metric_name, LDMS_V_U64,
					&meta_sz, &data_sz);
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
			metric_count++;
		}
	}

	/* Create a metric set of the required size */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;

	/* Make sure these are added in the order they will appear in the file */
	metric_count = 0;
	for (i = 0; i < MAXOPTS; i++) {
		for (j = 0; j < numvars[i]; j++) {
			snprintf(metric_name,127,"%s", varnames[i][j]);
			metric_table[metric_count] =
				ldms_add_metric(set, metric_name, LDMS_V_U64);
			if (!metric_table[metric_count]) {
				rc = ENOMEM;
				goto err;
			}
			ldms_set_user_data(metric_table[metric_count], comp_id);
			metric_count++;
		}
	}

	return 0;

err:
	ldms_destroy_set(set);
	return rc;
}

static const char *usage(void)
{
	return  "config name=procnfs component_id=<comp_id> set=<setname>\n"
		"    comp_id     The component id value.\n"
		"    setname     The set name.\n";
}


/**
 * \brief Configuration
 *
 * - config procnfs  component_id=<value> set=<setname>
 *
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{

	char *value;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtol(value, NULL, 0);

	value = av_value(avl, "set");
	if (value)
		create_metric_set(value);

	return 0;

}
#define LINE_FMT "%s %s %s %" PRIu64 " %" PRIu64 " %" PRIu64 " %" \
	PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" \
PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" \
PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" \
	PRIu64 " %" PRIu64 " %" PRIu64 " %s\n"
static int sample(void)
{
	int metric_no;
	int rc, i;
	char *s;
	char lbuf[256];
	union ldms_value v[23];

	if (!set) {
		msglog("procnfs: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);

	metric_no = 0;

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
				ldms_set_metric(metric_table[0], &v[0]);
				ldms_set_metric(metric_table[1], &v[1]);
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
					ldms_set_metric(metric_table[i], &v[i]);
				break;
			default:
				break;
		}
		currlinenum++;
	} while (s); /* must get to EOF for the switch to work */
	rc = 0;
out:
	ldms_end_transaction(set);
	return rc;
}


static void term(void)
{
	if (mf)
		fclose(mf);
	mf = 0;

	if (set)
		ldms_destroy_set(set);
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
	return &procnfs_plugin.base;
}
