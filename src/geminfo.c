/*
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
/**
 * \file geminfo.c
 * \brief /proc/geminfo data provider
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

#define PROC_FILE "/proc/kgnilnd/stats"

static char *procfile = PROC_FILE;

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
ldms_metric_t compid_metric_handle;
union ldms_value comp_id;

static ldms_set_t get_set()
{
	return set;
}


char *replace_space(char *s)
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

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, metric_count;
	uint64_t metric_value;
	char *s;
	char lbuf[256];
	char metric_name[128];

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog("Could not open the geminfo file '%s'...exiting\n", procfile);
		return ENOENT;
	}


	/* Process the file once first to determine the metric set size.
	 */
	rc = ldms_get_metric_size("component_id", LDMS_V_U64, &tot_meta_sz, &tot_data_sz);
	metric_count = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		char* end = strchr(s, ':');
		if ( end ) {
			if ( *end ) {
				*end = '\0';
				replace_space(s);
				if ( sscanf( end + 1, " %" PRIu64 "\n", &metric_value ) == 1 ) {
					rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
					if (rc)
						return rc;

					tot_meta_sz += meta_sz;
					tot_data_sz += data_sz;
					metric_count++;
				}
			}
		} else {
			fprintf( stderr, "Error: string \"%s\" had no colon\n", s );
		}

	} while (s);


	/* Create the metric set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;


	/* Process the file again to define all the metrics.
	 */
	compid_metric_handle = ldms_add_metric(set, "component_id", LDMS_V_U64);
	if (!compid_metric_handle) {
		rc = ENOMEM;
		goto err;
	} //compid set in config

	int metric_no = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		char* end = strchr(s, ':');
		if ( end ) {
			if ( *end ) {
				*end = '\0';
				replace_space(s);
				if ( sscanf( end + 1, " %" PRIu64 "\n", &metric_value ) == 1 ) {
					metric_table[metric_no] = ldms_add_metric(set, s, LDMS_V_U64);
					if (!metric_table[metric_no]) {
						rc = ENOMEM;
						goto err;
					}
					metric_no++;
				}
			}
		} else {
			//	    fprintf( stderr, "Error: string \"%s\" had no colon\n", s );
			rc = ENOMEM;
			goto err;
		}
	} while (s);
	return 0;

 err:
	ldms_set_release(set);
	return rc;
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;

	value = av_value(avl, "component_id");
	if (value)
		comp_id.v_u64 = strtol(value, NULL, 0);
	
	value = av_value(avl, "set");
	if (value)
		create_metric_set(value);

	return 0;
}

static int sample(void)
{
	int metric_no;
	char *s;
	char lbuf[256];
	union ldms_value v;

	if (!set){
	  msglog("geminfo: plugin not initialized\n");
	  return EINVAL;
	}
	ldms_set_metric(compid_metric_handle, &comp_id);

	metric_no = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		char* end = strchr(s, ':');
		if ( end ) {
			if ( *end ) {
				*end = '\0';
				replace_space(s);
				if ( sscanf( end + 1, " %" PRIu64 "\n", &v.v_u64 ) == 1 ) {
					ldms_set_metric(metric_table[metric_no], &v);
					metric_no++;
				}
			}
		} else {
			//fprintf( stderr, "Error: string \"%s\" had no colon\n", s );
			return EINVAL;
		}
	} while (s);
	return 0;
}

static void term(void)
{
  if (set)
	ldms_set_release(set);
  set = NULL;
}

static const char *usage(void)
{
	return  "config name=geminfo component_id=<comp_id> set=<setname>\n"
		"    comp_id     The component id value.\n"
		"    setname     The set name.\n";
}


static struct ldmsd_sampler geminfo_plugin = {
	.base = {
		.name = "geminfo",
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
	return &geminfo_plugin.base;
}
