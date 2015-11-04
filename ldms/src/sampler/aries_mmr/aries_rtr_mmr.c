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
 * \file aries_rtr_mmr.c
 * \brief aries network metric provider (reads gpcd mmr)
 *
 * parses 4 config files:
 * 1) raw names that go in as is
 * 2) r/c metrics.
 * 3) ptile metrics.
 * 4) nic metrics. THIS IS NOT AN OPTION FOR THE RTR SAMPLER
 * VC's should be explictly put in separately.
 */

enum {RAW_T, RC_T, NIC_T, PTILE_T, END_T};

#define ARIES_MAX_ROW 6
#define ARIES_MAX_COL 8
#define MAX_LEN 256

struct mstruct{
	int num_metrics;
	char** metrics;
	int max; // number actually addedd
	gpcd_context_t* ctx;
};

static struct mstruct mvals[END_T];

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;
static char *default_schema_name = "aries_rtr_mmr";
static uint64_t compid;
static uint64_t jobid;
static int metric_offset = 0;

struct met{
	char* name;
	LIST_ENTRY(met) entry;
};

LIST_HEAD(raw_list, met) raw_list;
LIST_HEAD(rc_list, met) rc_list;
LIST_HEAD(nic_list, met) nic_list;
LIST_HEAD(ptile_list, met) ptile_list;

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
		msglog(LDMSD_LERROR, " aries_rtr_mmr: Cannot open file <%s>\n", fname);
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
			msglog(LDMSD_LDEBUG, "aries_rtr_mmr: skipping input <%s>\n", lbuf);
			continue;
		}
		countA++;
	} while(s);

	if (countA == 0){
		temp = NULL;
	} else {
		fseek(mf, 0, SEEK_SET);
		temp = calloc(countA, sizeof(*temp));
		countB = 0;
		//parse again to populate the metrics;
		do {
			s = fgets(lbuf, sizeof(lbuf), mf);
			if (!s)
				break;
			rc = sscanf(lbuf," %s", name);
			if ((rc != 1) || (strlen(name) == 0) || (name[0] == '#')){
				continue;
			}
			msglog(LDMSD_LDEBUG, "aries_rtr_mmr: config read <%s>\n", lbuf);
			temp[countB] = strdup(name);
			if (countB == countA)
				break;
			countB++;
		} while(s);
	}


	mvals[mtype].num_metrics = countA;
	mvals[mtype].metrics = temp;

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
		msglog(LDMSD_LERROR, "aries_rtr_mmr: Could not create context\n");
		return NULL;
	}

	//add them backwards
	for (i = num-1; i >= 0 ; i--){
		desc = (gpcd_mmr_desc_t *)
			gpcd_lookup_mmr_byname(met[i]);

		if (!desc) {
			msglog(LDMSD_LINFO, "aries_rtr_mmr: Could not lookup <%s>\n", met[i]);
			//leave it out and continue....
			continue;
		}

		status = gpcd_context_add_mmr(lctx, desc);
		if (status != 0) {
			msglog(LDMSD_LERROR, "aries_rtr_mmr: Could not add mmr for <%s>\n", met[i]);
			gpcd_remove_context(lctx);
			return NULL;
		}
		struct met* e = calloc(1, sizeof(*e));
		e->name = strdup(met[i]);
		msglog(LDMSD_LDEBUG, "aries_rtr_mmr: will be adding metric <%s>\n", met[i]);

		LIST_INSERT_HEAD(&raw_list, e, entry);
		count++;
	}

	*nmet = count;
	return lctx;
}


/**
 * Build linked list of tile performance counters we wish to get values for.
 * No aggregation.
 */
gpcd_context_t *create_context_np(char** met, int num, int nptype, int* nmet)
{

	gpcd_context_t *lctx = NULL;
	gpcd_mmr_desc_t *desc;
	int rangemax = -1;
	char key;
	int i, k, status;

	lctx = gpcd_create_context();
	if (!lctx) {
		msglog(LDMSD_LERROR, "aries_rtr_mmr: could not create context\n");
		return NULL;
	}

	switch (nptype){
	case NIC_T:
		key = 'n';
		rangemax = 3;
		break;
	case PTILE_T:
		key = 'p';
		rangemax = 7;
		break;
	default:
		msglog(LDMSD_LERROR, "aries_rtr_mmr: Invalid type to create_context_np\n");
		return NULL;
		break;
	}

	int nvalid = 0;
	//add them backwards
	for (k = num-1; k >=0 ; k--){
		char *ptr = strchr(met[k], key);
		if (!ptr){
			msglog(LDMSD_LERROR, "aries_rtr_mmr: invalid metricname: key <%c> not found in <%s>\n",
			       key, met[k]);
			continue;
		}
		for (i = rangemax; i >=0; i--) {
			char* newname = strdup(met[k]);
			char* ptr = strchr(newname, key);
			if (!ptr) {
				msglog(LDMSD_LERROR, "aries_rtr_mmr: Bad ptr!\n");
				return NULL;
			}
			char ch[2];
			snprintf(ch, 2, "%d", i);
			ptr[0] = ch[0];

			desc = (gpcd_mmr_desc_t *)
				gpcd_lookup_mmr_byname(newname);
			if (!desc) {
				msglog(LDMSD_LERROR, "aries_rtr_mmr: Could not lookup <%s>\n", newname);
				free(newname);
				continue;
			}

			status = gpcd_context_add_mmr(lctx, desc);
			if (status != 0) {
				msglog(LDMSD_LERROR, "aries_rtr_mmr: Could not add mmr for <%s>\n", newname);
				gpcd_remove_context(lctx);
				return NULL;
			}

			struct met* e = calloc(1, sizeof(*e));
			e->name = newname;

			if (nptype == NIC_T)
				LIST_INSERT_HEAD(&nic_list, e, entry);
			else
				LIST_INSERT_HEAD(&ptile_list, e, entry);
			nvalid++;
		}
	}

	*nmet = nvalid;
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
		msglog(LDMSD_LERROR, "aries_rtr_mmr: Could not create context\n");
		return NULL;
	}

	int nvalid = 0;
	//add them backwards
	for (k = ibase-1; k >= 0; k--){
		for (i = rmax-1; i >= 0 ; i--) {
			for (j = cmax-1; j >= 0 ; j--) {
				snprintf(name, MAX_LEN, "AR_RTR_%d_%d_%s", i, j, basemetrics[k]);

				desc = (gpcd_mmr_desc_t *)
					gpcd_lookup_mmr_byname(name);

				if (!desc) {
					msglog(LDMSD_LINFO, "aries_rtr_mmr: Could not lookup <%s>\n", name);
					//leave it out and continue....
					continue;
				}

				status = gpcd_context_add_mmr(lctx, desc);
				if (status != 0) {
					msglog(LDMSD_LERROR, "aries_rtr_mmr: Could not add mmr for <%s>\n", name);
					gpcd_remove_context(lctx);
					return NULL;
				}

				struct met* e = calloc(1, sizeof(*e));
				e->name = strdup(name);
				msglog(LDMSD_LDEBUG, "aries_rtr_mmr: will be adding metric <%s>\n", name);
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
	union ldms_value v;
	int rc, i;
	//the list processing in the sample will correspond to the order
	//of the name lists

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

	rc = ldms_schema_metric_add(schema, "job_id", LDMS_V_U64);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}
	metric_offset = rc+1;

	for (i = 0; i < END_T; i++){
		struct met *np;
		if (mvals[i].max == 0)
			continue;
		switch (i) {
		case RAW_T:
			np = raw_list.lh_first;
			break;
		case RC_T:
			np = rc_list.lh_first;
			break;
		case NIC_T:
			np = nic_list.lh_first;
			break;
		case PTILE_T:
			np = ptile_list.lh_first;
			break;
		}

		if (np == NULL){
			msglog(LDMSD_LERROR, "aries_rtr_mmr: problem with list names\n");
			break;
		}

		do {
			rc = ldms_schema_metric_add(schema, np->name, LDMS_V_U64);
			if (rc < 0) {
				rc = ENOMEM;
				goto err;
			}
			msglog(LDMSD_LDEBUG, "aries_rtr_mmr: adding <%s> to schema\n",
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

	return rc;
}


static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *sname;
	char *rawf;
	char *rcf;
	char *nicf;
	char *ptilef;
	void * arg = NULL;
	int i;
	int rc;


	if (set) {
		msglog(LDMSD_LERROR, "aries_rtr_mmr: Set already created.\n");
		return EINVAL;
	}

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "aries_rtr_mmr: missing producer\n");
		return ENOENT;
	}

	value = av_value(avl, "component_id");
	if (value)
		compid = (uint64_t)(atoi(value)); //this is ok since it will really be a u32
	else
		compid = 0;

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, "aries_rtr_mmr: missing instance.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0){
		msglog(LDMSD_LERROR, "aries_rtr_mmr: schema name invalid.\n");
		return EINVAL;
	}

	rawf = av_value(avl, "rawfile");
	if (rawf){
		rc = parseConfig(rawf, RAW_T);
		if (rc){
			msglog(LDMSD_LERROR, "aries_rtr_mmr: error parsing <%s>\n", rawf);
			return EINVAL;
		}
	}

	rcf = av_value(avl, "rcfile");
	if (rcf){
		rc = parseConfig(rcf, RC_T);
		if (rc){
			msglog(LDMSD_LERROR, "aries_rtr_mmr: error parsing <%s>\n", rcf);
			return EINVAL;
		}
	}

/*
	nicf = av_value(avl, "nicfile");
	if (nicf){
		rc = parseConfig(nicf, NIC_T);
		if (rc){
			msglog(LDMSD_LERROR, "aries_rtr_mmr: error parsing <%s>\n", nicf);
			return EINVAL;
		}
	}
*/

	ptilef = av_value(avl, "ptilefile");
	if (ptilef){
		rc = parseConfig(ptilef, PTILE_T);
		if (rc){
			msglog(LDMSD_LERROR, "aries_rtr_mmr: error parsing <%s>\n", ptilef);
			return EINVAL;
		}
	}

	if (!rcf && !rawf && !nicf && !ptilef){
		msglog(LDMSD_LERROR, "aries_rtr_mmr: must specificy at least one input file\n");
		return EINVAL;
	}

	for (i = 0; i < END_T; i++){
		switch (i){
		case RAW_T:
			mvals[i].ctx = create_context_list(mvals[i].metrics, mvals[i].num_metrics, &mvals[i].max);
			break;
		case RC_T:
			mvals[i].ctx = create_context_rc(mvals[i].metrics, mvals[i].num_metrics, &mvals[i].max);
			break;
		case NIC_T:
			//fall thru
		case PTILE_T:
			mvals[i].ctx = create_context_np(mvals[i].metrics, mvals[i].num_metrics, i, &mvals[i].max);
			break;
		}

		if (mvals[i].max && !mvals[i].ctx){
			msglog(LDMSD_LERROR, "aries_rtr_mmr: Cannot create context for %d\n", i);
			return EINVAL;
		}
	}

	rc = create_metric_set(value, sname);
	if (rc) {
		msglog(LDMSD_LERROR, "aries_rtr_mmr: failed to create a metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);


	return 0;
}


static int sample(void){

	union ldms_value v;
	int metric_no;
	int i;
	int rc;

	if (!set) {
		msglog(LDMSD_LERROR, "aries_rtr_mmr: plugin not initialized\n");
		return EINVAL;
	}

	for (i = 0; i < END_T; i++){
		if (mvals[i].max){
			rc = gpcd_context_read_mmr_vals(mvals[i].ctx);
			if (rc){
				msglog(LDMSD_LERROR, "aries_rtr_mmr: Cannot read raw mmr vals\n");
				return EINVAL;
			}
		}
	}

	ldms_transaction_begin(set);

	metric_no = metric_offset;
	for (i = 0; i < END_T; i++){
		if (mvals[i].max == 0)
			continue;
		listp = mvals[i].ctx->list;

		if (!listp){
			msglog(LDMSD_LERROR, "aries_rtr_mmr: Context list is null\n");
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

	int i;

	for (i = 0; i < END_T; i++){
		if (mvals[i].ctx)
			gpcd_remove_context(mvals[i].ctx);
		mvals[i].ctx = NULL;
		mvals[i].num_metrics = 0;
		mvals[i].max = 0;
	}


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

	while (nic_list.lh_first != NULL){
		free(nic_list.lh_first->name);
		nic_list.lh_first->name = NULL;
		LIST_REMOVE(nic_list.lh_first, entry);
	}

	while (ptile_list.lh_first != NULL){
		free(ptile_list.lh_first->name);
		ptile_list.lh_first->name = NULL;
		LIST_REMOVE(ptile_list.lh_first, entry);
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
	return  "config name=aries_rtr_mmr producer=<prod_name> instance=<inst_name> component_id=<compid> [(raw|rc|ptile)file=<file> schema=<sname>]\n"
		"    <prod_name>       The producer name\n"
		"    <inst_name>       The instance name\n"
		"    <compid>    The instance name\n"
		"    <rawfile>         File with full names of metrics\n"
		"    <rcfile>          File with abbreviated names of metrics to be added in for all rows and columns\n"
		"    <ptilefile>       File with full name with 'p' to be replaced for all ptile options (0-7)\n"
		"    <sname>           Optional schema name. Defaults to 'aries_rtr_mmr'\n"
		"    NOTE: nicfile is NOT an option for the aries_rtr_mmr sampler\n";

}

static struct ldmsd_sampler aries_rtr_mmr_plugin = {
	.base = {
		.name = "aries_rtr_mmr",
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
	return &aries_rtr_mmr_plugin.base;
}
