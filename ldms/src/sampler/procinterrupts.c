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
 * \file procinterrupts.c
 * \brief /proc/interrupts data provider
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

#define PROC_FILE "/proc/interrupts"
static char *procfile = PROC_FILE;

static ldms_set_t set = NULL;
static FILE *mf = NULL;
static ldmsd_msg_log_f msglog;
static int nprocs;
static char *producer_name;
static ldms_schema_t schema;
static char *default_schema_name = "procinterrupts";

static ldms_set_t get_set()
{
	return set;
}


static int getNProcs(char buf[]){
	int nproc = 0;
	char* pch;
	pch = strtok(buf, " ");
	while (pch != NULL){
		if (pch[0] == '\n'){
			break;
		}
		if (pch[0] != ' '){
			nproc++;
		}
		pch = strtok(NULL," ");
	}

	return nproc;
}


static char lbuf[4096];		/* NB: Some machines have many cores */
static int create_metric_set(const char *instance_name, char* schema_name)
{
	int rc, i;
	char *s;
	char metric_name[128];

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "Could not open the interrupts file '%s'...exiting\n",
				procfile);
		return ENOENT;
	}

	char beg_name[128];

	/* Create a metric set of the required size */
	schema = ldms_schema_new(schema_name);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	/*
	 * Process the file to define all the metrics.
	 */
	fseek(mf, 0, SEEK_SET);
	/* first line is the cpu list */
	s = fgets(lbuf, sizeof(lbuf), mf);
	nprocs = getNProcs(lbuf);
	if (nprocs <= 0) {
		msglog(LDMSD_LINFO, "Bad number of CPU.\n");
		fclose(mf);
		return EINVAL;
	}

	while(s){
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		int currcol = 0;
		char* pch = strtok (lbuf," ");
		while (pch != NULL && currcol <= nprocs){
			if (pch[0] == '\n'){
				break;
			}
			if (currcol == 0){
				/* Strip the colon from metric name if present */
				i = strlen(pch);
				if (i && pch[i-1] == ':')
					pch[i-1] = '\0';
				strcpy(beg_name, pch);
			} else {
				snprintf(metric_name, 128, "irq.%s#%d",
						beg_name, (currcol-1));
				rc = ldms_schema_metric_add(schema, metric_name,
							    LDMS_V_U64);
				if (rc < 0) {
					rc = ENOMEM;
					goto err;
				}
			}
			currcol++;
			pch = strtok(NULL," ");
		}
	}
	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err;
	}

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

/**
 * \brief Configuration
 *
 * - config name=procinterrupts producer=<prod_name> instance=<inst_name> [schema=<sname>]
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc = 0;
	char *sname;
	char *value;

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "procinterrupts: missing 'producer'.\n");
		return ENOENT;
	}

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, "procinterrupts: missing 'instance'.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
        if (!sname)
                sname = default_schema_name;
        if (strlen(sname) == 0){
                msglog(LDMSD_LERROR, "%s: schema name invalid.\n",
		       __FILE__);
                return EINVAL;
        }

	if (set) {
		msglog(LDMSD_LERROR, "procinterrupts: Set already created.\n");
		return EINVAL;
	}

	rc = create_metric_set(value, sname);
	if (rc) {
		msglog(LDMSD_LERROR, "procinterrupts: failed to create the metric set.\n");
		return rc;
	}

	ldms_set_producer_name_set(set, producer_name);
	return 0;
}

static int sample(void)
{
	int rc;
	int metric_no;
	char *s;
	union ldms_value v;

	if (!set){
		msglog(LDMSD_LDEBUG, "procinterrupts: plugin not initialized\n");
		return EINVAL;
	}
	ldms_transaction_begin(set);

	metric_no = 0;
	fseek(mf, 0, SEEK_SET);
	/* first line is the cpu list */
	s = fgets(lbuf, sizeof(lbuf), mf);

	while(s){
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		int currcol = 0;
		char* pch = strtok(lbuf, " ");
		while (pch != NULL && currcol <= nprocs){
			if (pch[0] == '\n') {
				break;
			}
			if (pch[0] != ' ') {
				if (currcol != 0){
					char* endptr;
					unsigned long long int l1;
					l1 = strtoull(pch,&endptr,10);
					if (endptr != pch){
						v.v_u64 = l1;
						ldms_metric_set(set, metric_no, &v);
						metric_no++;
					} else {
						msglog(LDMSD_LERROR,
							"bad val <%s>\n",pch);
						rc = EINVAL;
						goto out;
					}
				}
				currcol++;
			}
			pch = strtok(NULL," ");
		} /* end while(strtok) */
	} while (s);
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

static const char *usage(void)
{
	return  "config name=procinterrupts producer=<prod_name> instance=<inst_name> [schema=<sname>]\n"
		"    <prod_name>     The producer name\n"
		"    <inst_name>     The instance name\n"
		"    <sname>      Optional schema name. Defaults to 'procinterrupts'\n";
}

static struct ldmsd_sampler procinterrupts_plugin = {
	.base = {
		.name = "procinterrupts",
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
	return &procinterrupts_plugin.base;
}
