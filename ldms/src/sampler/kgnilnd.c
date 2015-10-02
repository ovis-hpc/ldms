/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010,2014-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010,2014-2015 Sandia Corporation. All rights reserved.
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
 * \file kgnilnd.c
 * \brief /proc/kgnilnd data provider
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

static ldms_set_t set = NULL;
static FILE *mf = 0;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;
static char *default_schema_name = "kgnilnd";

static ldms_set_t get_set()
{
	return set;
}


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

static int create_metric_set(const char *instance_name, char* schema_name)
{
	int rc;
	uint64_t metric_value;
	char *s;
	char lbuf[256];
	char metric_name[128];

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "Could not open the kgnilnd file "
				"'%s'...exiting\n", procfile);
		return ENOENT;
	}

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	/* Process the file to define all the metrics.*/
	fseek(mf, 0, SEEK_SET);
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
			}
		} else {
			rc = ENOMEM;
			goto err;
		}
	} while (s);

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err;
	}
	return 0;

 err:
	if (schema)
		ldms_schema_delete(schema);
	if (mf)
		fclose(mf);
	mf = NULL;
	return rc;
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *sname;
	int rc = 0;

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "%s: missing producer\n",
		       __FILE__);
		return ENOENT;
	}

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, "%s: missing instance.\n",
		       __FILE__);
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname){
		sname = default_schema_name;
	}
	if (strlen(sname) == 0){
		msglog(LDMSD_LERROR, "%s: schema name invalid.\n",
		       __FILE__);
		return EINVAL;
	}

	if (set) {
		msglog(LDMSD_LERROR, "%s: Set already created.\n",
		       __FILE__);
		return EINVAL;
	}

	rc = create_metric_set(value, sname);
	if (rc) {
		msglog(LDMSD_LERROR, "%s: failed to create a metric set.\n",
		       __FILE__);
		return rc;
	}

	ldms_set_producer_name_set(set, producer_name);
	return 0;

}

static int sample(void)
{
	int metric_no, rc;
	char *s;
	char lbuf[256];
	union ldms_value v;

	if (!set || !mf){
	  msglog(LDMSD_LDEBUG, "kgnilnd: plugin not initialized\n");
	  return EINVAL;
	}
	ldms_transaction_begin(set);

	metric_no = 0;
	fseek(mf, 0, SEEK_SET);
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
	ldms_transaction_end(set);
	return 0;
}

static void term(void)
{
	if (mf)
		fclose(mf);
	mf = NULL;
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static const char *usage(void)
{
	return  "config name=kgnilnd producer=<prod_name> instance=<inst_name> [schema=<sname>]\n"
		"    prod_name     The producer name.\n"
		"    inst_name     The set name.\n",
		"    <sname>      Optional schema name. Defaults to 'kgnilnd'\n";
}


static struct ldmsd_sampler kgnilnd_plugin = {
	.base = {
		.name = "kgnilnd",
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
	return &kgnilnd_plugin.base;
}
