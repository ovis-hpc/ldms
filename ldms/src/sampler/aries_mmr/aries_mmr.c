/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <pthread.h>
#include <linux/limits.h>
#include "gpcd_pub.h"
#include "gpcd_lib.h"
#include "ldms.h"
#include "ldmsd.h"

/**
 * \file aries_mmr.c
 * \brief aries network metric provider (reads gpcd mmr)
 *
 * parses 2 config files:
 * 1) raw names that go in as is
 * 2) r/c metrics.
 * VC's should be explictly put in separately.
 */

#define RC_T 0
#define RAW_T 1
#define ARIES_MAX_ROW 6
#define ARIES_MAX_COL 8
#define MAX_LEN 256

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;
static char *default_schema_name = "aries_mmr";

static int num_rcmetrics = 0;
static char** rcmetrics = NULL;
int rcmax = 0; //number actually added

static int num_rawmetrics = 0;
static char** rawmetrics = NULL;
int rawmax = 0; //number actually added


struct met{
	char* name;
	LIST_ENTRY(met) entry;
};

LIST_HEAD(raw_list, met) raw_list;
LIST_HEAD(rc_list, met) rc_list;

static gpcd_context_t *rc_ctx = NULL;
static gpcd_context_t *raw_ctx = NULL;
static gpcd_mmr_list_t *listp = NULL;


int parseConfig(char* fname, int mtype){
	FILE *mf;
	char *s;
	char** temp;
	char name[MAX_LEN];
	char lbuf[MAX_LEN];
	int countA = 0;
	int countB = 0;
	int rc;


	mf = fopen(fname, "r");
	if (!mf){
		msglog(LDMSD_LERROR, " aries_mmr: Cannot open file <%s>\n", fname);
		return EINVAL;
	}

	fseek(mf, 0, SEEK_SET);
	//parse once to get the number of metrics
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf," %s", name);
		if ((rc != 1) || (strlen(name) == 0) || (name[0] == '#')){
			msglog(LDMSD_LDEBUG, "aries_mmr: skipping input <%s>\n", lbuf);
			continue;
		}
		countA++;
	} while(s);

	if (countA == 0){
		temp = NULL;
	} else {
		fseek(mf, 0, SEEK_SET);
		countB = 0;
		temp = calloc(countA, sizeof(*temp));
		//parse again to populate the metrics;
		do {
			s = fgets(lbuf, sizeof(lbuf), mf);
			if (!s)
				break;
			rc = sscanf(lbuf," %s", name);
			if ((rc != 1) || (strlen(name) == 0) || (name[0] == '#')){
				continue;
			}
			msglog(LDMSD_LDEBUG, "aries_mmr: config read <%s>\n", lbuf);
			temp[countB] = strdup(name);
			if (countB == countA)
				break;
			countB++;
		} while(s);
	}

	if (mtype == RC_T){
		num_rcmetrics = countB;
		rcmetrics = temp;
	} else {
		num_rawmetrics = countB;
		rawmetrics = temp;
	}

	fclose(mf);

	return 0;

}

/**
 * Build linked list of tile performance counters we wish to get values for.
 * No aggregation.
 */
gpcd_context_t *create_context_list(char** met, int num, int* nmet)
{
	gpcd_context_t *lctx = NULL;
	gpcd_mmr_desc_t *desc;
	int count = 0;
	int i, status;

	lctx = gpcd_create_context();
	if (!lctx) {
		msglog(LDMSD_LERROR, "aries_mmr: Could not create context\n");
		return NULL;
	}

	for (i = 0; i < num; i++){
		desc = (gpcd_mmr_desc_t *)
			gpcd_lookup_mmr_byname(met[i]);

		if (!desc) {
			msglog(LDMSD_LINFO, "aries_mmr: Could not lookup <%s>\n", met[i]);
			//leave it out and continue....
			continue;
		}

		status = gpcd_context_add_mmr(lctx, desc);
		if (status != 0) {
			msglog(LDMSD_LERROR, "aries_mmr: Could not add mmr for <%s>\n", met[i]);
			gpcd_remove_context(lctx);
			return NULL;
		}
		struct met* e = calloc(1, sizeof(*e));
		e->name = strdup(met[i]);
		msglog(LDMSD_LDEBUG, "aries_mmr: will be adding metric <%s>\n", met[i]);

		LIST_INSERT_HEAD(&raw_list, e, entry);
		count++;
	}

	*nmet = count;
	return lctx;
}


/**
 * Build linked list of tile performance counters we wish to get values for.
 * No aggregation.  Will add all 48 for all variables.
 */
gpcd_context_t *create_context_rc(char** basemetrics, int ibase, int* nmet)
{
	int i, j, k, status;
	char name[MAX_LEN];
	gpcd_context_t *lctx;
	gpcd_mmr_desc_t *desc;
	int rmax = ARIES_MAX_ROW;
	int cmax = ARIES_MAX_COL;

	lctx = gpcd_create_context();
	if (!lctx) {
		msglog(LDMSD_LERROR, "aries_mmr: Could not create context\n");
		return NULL;
	}

	int nvalid = 0;
	for (k = 0; k < ibase; k++){
		for (i = 0; i < rmax; i++) {
			for (j = 0; j < cmax; j++) {
				snprintf(name, MAX_LEN, "AR_RTR_%d_%d_%s", i, j, basemetrics[k]);

				desc = (gpcd_mmr_desc_t *)
					gpcd_lookup_mmr_byname(name);

				if (!desc) {
					msglog(LDMSD_LINFO, "aries_mmr: Could not lookup <%s>\n", name);
					//leave it out and continue....
					continue;
				}

				status = gpcd_context_add_mmr(lctx, desc);
				if (status != 0) {
					msglog(LDMSD_LERROR, "aries_mmr: Could not add mmr for <%s>\n", name);
					gpcd_remove_context(lctx);
					return NULL;
				}

				struct met* e = calloc(1, sizeof(*e));
				e->name = strdup(name);
				msglog(LDMSD_LDEBUG, "aries_mmr: will be adding metric <%s>\n", name);
				LIST_INSERT_HEAD(&rc_list, e, entry);
				nvalid++;
			}
		}
	}

	*nmet = nvalid;
	return lctx;
}


static int create_metric_set(const char *instance_name, char* schema_name)
{
	int rc, i;
	//the list processing in the sample will correspond to the order
	//of the name lists

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	for (i = 0; i < 2; i++){
		struct met *np;
		if (i == RAW_T){
			if (rawmax == 0)
				continue;
			np = raw_list.lh_first;
		} else {
			if (rcmax == 0)
				continue;
			np = rc_list.lh_first;
		}

		if (np == NULL){
			msglog(LDMSD_LERROR, "aries_mmr: problem with list names\n");
			break;
		}

		do {
			rc = ldms_schema_metric_add(schema, np->name, LDMS_V_U64);
			if (rc < 0) {
				rc = ENOMEM;
				goto err;
			}
			msglog(LDMSD_LDEBUG, "aries_mmr: adding <%s> to schema\n",
			       np->name);
			np = np->entry.le_next;
		} while (np != NULL);

	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	//NOTE: we can get rid of the names here if we want

	return 0;

err:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;

	return rc;
}


static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *sname;
	char *rawf;
	char *rcf;
	void * arg = NULL;
	int rc;


	if (set) {
		msglog(LDMSD_LERROR, "aries_mmr: Set already created.\n");
		return EINVAL;
	}

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "aries_mmr: missing producer\n");
		return ENOENT;
	}

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, "aries_mmr: missing instance.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0){
		msglog(LDMSD_LERROR, "aries_mmr: schema name invalid.\n");
		return EINVAL;
	}

	rawf = av_value(avl, "rawfile");
	if (rawf){
		rc = parseConfig(rawf, RAW_T);
		if (rc){
			msglog(LDMSD_LERROR, "aries_mmr: error parsing <%s>\n", rawf);
			return EINVAL;
		}
	}

	rcf = av_value(avl, "rcfile");
	if (rcf){
		rc = parseConfig(rcf, RC_T);
		if (rc){
			msglog(LDMSD_LERROR, "aries_mmr: error parsing <%s>\n", rcf);
			return EINVAL;
		}
	}

	if (!rcf && !rawf){
		msglog(LDMSD_LERROR, "aries_mmr: must specificy at least one of raw/rc file\n");
		return EINVAL;
	}

	raw_ctx = create_context_list(rawmetrics, num_rawmetrics, &rawmax);
	if (rawmax && !raw_ctx){
		msglog(LDMSD_LERROR, "aries_mmr: cannot create context for raw metrics\n");
		return EINVAL;
	}

	rc_ctx = create_context_rc(rcmetrics, num_rcmetrics, &rcmax);
	if (rcmax && !rc_ctx){
		msglog(LDMSD_LERROR, "aries_mmr: cannot create context for rc metrics\n");
		return EINVAL;
	}

	rc = create_metric_set(value, sname);
	if (rc) {
		msglog(LDMSD_LERROR, "aries_mmr: failed to create a metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);
	return 0;
}


static int sample(void){

	union ldms_value v;
	int metric_no;
	int num;
	int rc;

	if (!set) {
		msglog(LDMSD_LERROR, "aries_mmr: plugin not initialized\n");
		return EINVAL;
	}

	//only read if we have metrics
	if (rawmax){
		rc = gpcd_context_read_mmr_vals(raw_ctx);
		if (rc){
			msglog(LDMSD_LERROR, "aries_mmr: Cannot read raw mmr vals\n");
			return EINVAL;
		}
	}

	if (rcmax){
		rc  = gpcd_context_read_mmr_vals(rc_ctx);
		if (rc){
			msglog(LDMSD_LERROR, "aries_mmr: Cannot read rc mmr vals\n");
			return EINVAL;
		}
	}

	ldms_transaction_begin(set);

	metric_no = 0;
	for (num = 0; num < 2; num++){
		if (num == RAW_T){
			if (!rawmax)
				continue;
			listp = raw_ctx->list;
		} else {
			if (!rcmax)
				continue;
			listp = rc_ctx->list;
		}

		if (!listp){
			msglog(LDMSD_LERROR, "aries_mmr: Context list is null\n");
			rc = EINVAL;
			goto err;
		}

		while (listp != NULL){
			v.v_u64 = listp->value;
			ldms_metric_set(set, metric_no, &v);
			metric_no++;

			if (listp->next != NULL)
				listp=listp->next;
			else
				break;
		}
	}

out:
	ldms_transaction_end(set);
	return 0;

err:
	ldms_transaction_end(set);
	return rc;
}



static ldms_set_t get_set()
{
	return set;
}

static void term(void)
{
	if (rc_ctx)
		gpcd_remove_context(rc_ctx);
	rc_ctx = NULL;

	if (raw_ctx)
		gpcd_remove_context(raw_ctx);
	raw_ctx = NULL;

	while (rc_list.lh_first != NULL){
		free(rc_list.lh_first->name);
		rc_list.lh_first->name = NULL;
		LIST_REMOVE(rc_list.lh_first, entry);
	}

	while (raw_list.lh_first != NULL){
		free(raw_list.lh_first->name);
		raw_list.lh_first->name = NULL;
		LIST_REMOVE(raw_list.lh_first, entry);
	}

	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static const char *usage(void)
{
	return  "config name=aries_mmr producer=<prod_name> instance=<inst_name> [rawfile=<rawf> rcfile=<rcf> schema=<sname>]\n"
		"    <prod_name>  The producer name\n"
		"    <inst_name>  The instance name\n"
		"    <rawf>       File with full names of metrics\n";
		"    <rc>         File with abbreviated names of metrics to be added in for all rows and columns\n";
		"    <sname>      Optional schema name. Defaults to 'aries_mmr'\n";
}

static struct ldmsd_sampler aries_mmr_plugin = {
	.base = {
		.name = "aries_mmr",
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
	return &aries_mmr_plugin.base;
}
