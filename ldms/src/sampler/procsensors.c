/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
 * \file procsensors.c
 * \brief reads from sysfs the data that populates lm sensors (e.g., in*_input, fan*_input, temp*_input)
 *
 * NOTE: data of interest defined in config file with format:
 * full_path_to_sysfs_var metricname multiplier offset
 *
 * All fields must be defined. No extraneous space. No other separators. Blank lines ok.
 * Hash in first char position are comments.
 * As of 8/12/14 revision, this is NOT backwards compatible.
 *
 * for example:
 * /home/xxx/temp tempa 1 0
 * /home/xxx/fan fanx 1 0
 * /home/yyy/temp tempb 2 0.2
 *
 * /home/yyy/fanx fannew 1 0
 * # this is a comment
 *
 * #
 * /home/yyy/temp0_input temp0 1 0
 *
 *
 * This results in metricset variables: tempa, fanx, tempb, fannew
 *
 * NOTE: metricname must be unique to the metric set
 * NOTE: Multiplier and offset are float but the evenual val will be u64
 *
 * NOTE: data files have to be opened and closed on each file in sys for the data values to change.
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
#include "ldms_slurmjobid.h"

#define MAXSENSORFILENAME 512

struct vinfo {
	char* vname;
	char* mname;
	float multiplier;
	float offset;
};

static ldms_set_t set;
static FILE *mf;
static ldms_metric_t *metric_table;
static struct vinfo** lm_srcs;
static int lm_nentries;
static ldmsd_msg_log_f msglog;
static uint64_t comp_id;

LDMS_JOBID_GLOBALS;

static int parse_conf_file(const char* ffile)
{
	char lbuf[MAXSENSORFILENAME];
	char* s;
	int rc = 0;

	FILE *fp = fopen(ffile,"r");
	if (!fp) {
		msglog(LDMS_LERROR,"Could not open the procsensors config file '%s'...returning\n",
		       ffile);
		return EINVAL;
	}

	//count the entries
	lm_nentries = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), fp);
		if (!s)
			break;

		if ((strlen(lbuf) > 0) && (lbuf[0] != '#') && (lbuf[0] != '\n')){
			lm_nentries++;
		}
	} while (s);

	if (lm_nentries == 0){
		msglog(LDMS_LERROR,"No entries in the procsensors config file '%s'...returning\n",
		       ffile);
		if (fp) fclose(fp);
		return EINVAL;
	}

	fseek(fp, 0, SEEK_SET);

	lm_srcs = (struct vinfo**)malloc(lm_nentries*sizeof(struct vinfo));
	if (!lm_srcs){
		fclose(fp);
		return ENOMEM;
	}

	int i = 0;
	do {
		char vname[MAXSENSORFILENAME];
		char mname[MAXSENSORFILENAME];
		double multiplier;
		double offset;

		s = fgets(lbuf, sizeof(lbuf), fp);
		if (!s)
			break;
		// each line format: path, varname, newname, multiplier, offset
		if ((strlen(lbuf) == 0) || (lbuf[0] == '#') || (lbuf[0] == '\n')){
			continue;
		}

		rc = sscanf(lbuf, "%s %s %lf %lf",
			    vname, mname, &multiplier, &offset);
		if (rc != 4){
			msglog(LDMS_LERROR,"Bad format line in the procsensors config file '%s' '%s'...returning\n",
			       lbuf, ffile);
			rc = EINVAL;
			break;
		}

		lm_srcs[i] = (struct vinfo*)malloc(sizeof(struct vinfo));
		if (!lm_srcs[i]){
			fclose(fp);
			return ENOMEM;
		}

		lm_srcs[i]->vname = strdup(vname);
		lm_srcs[i]->mname = strdup(mname);
		lm_srcs[i]->multiplier = multiplier;
		lm_srcs[i]->offset = offset;
		i++;
	} while (s);

	if (fp)
		fclose(fp);
	fp = NULL;

	return rc;

}

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i, metric_count = 0;

	tot_meta_sz = 0;
	tot_data_sz = 0;

	LDMS_SIZE_JOBID_METRIC(procsensors, meta_sz, tot_meta_sz,
		data_sz, tot_data_sz, metric_count, rc, msglog);

	for (i = 0 ; i < lm_nentries; i++){
		rc = ldms_get_metric_size(lm_srcs[i]->mname, LDMS_V_U64, &meta_sz, &data_sz);
		if (rc)
			return rc;

		tot_meta_sz += meta_sz;
		tot_data_sz += data_sz;
		metric_count++;
	}

	/* Create the metric set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;

	int metric_no = 0;

	LDMS_ADD_JOBID_METRIC(metric_table,metric_no,set,rc,err,comp_id);
	/*
	 * Process again to define all the metrics.
	 */
	for (i = 0; i < lm_nentries; i++){
		metric_table[metric_no] = ldms_add_metric(set, lm_srcs[i]->mname, LDMS_V_U64);
		if (!metric_table[metric_no]) {
			rc = ENOMEM;
			goto err;
		}
		ldms_set_user_data(metric_table[metric_no], comp_id);
		metric_no++;
	}

	return 0;

err:

	ldms_destroy_set(set);
	for (i = 0; i < lm_nentries; i++){
		if (lm_srcs[i]){
			if (lm_srcs[i]->vname) free(lm_srcs[i]->vname);
			if (lm_srcs[i]->mname) free(lm_srcs[i]->mname);
			free(lm_srcs[i]);
		}
	}
	if (lm_srcs)
		free(lm_srcs);

	return rc;
}

/**
 * \brief Configuration
 *
 * Usage:
 * config name=procsensors component_id=<comp_id> set=<setname> conffile=<conf> with_jobid=<bool>
 *     comp_id     The component id value.
 *     setname     The set name.
 *     bool        include jobid in set or not.
 *     conf        The full pathname for the config file
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtol(value, NULL, 0);


	value = av_value(avl, "conffile");
	if (value)
		parse_conf_file(value);
	else
		return -1;

	LDMS_CONFIG_JOBID_METRIC(value,avl);

	value = av_value(avl, "set");
	if (value)
		create_metric_set(value);

	return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	int rc, retrc;
	char *s;
	char lbuf[20];
	uint64_t tempval;
	union ldms_value v;
	int i;

	rc = 0;
	retrc = 0;

	ldms_begin_transaction(set);
	int metric_no = 0;
	LDMS_JOBID_SAMPLE(v,metric_table,metric_no);

	for (i = 0; i < lm_nentries; i++){
		//FIXME: do we really want to open and close each one?
		mf = fopen(lm_srcs[i]->vname, "r");
		if (!mf) {
			msglog(LDMS_LDEBUG,"Could not open the procsensors file '%s'...continuing\n",
			       lm_srcs[i]->vname);
			retrc = ENOENT;
		} else {
			s = fgets(lbuf, sizeof(lbuf), mf);
			if (s){
				rc = sscanf(lbuf, "%"PRIu64 "\n", &tempval);
				if (rc != 1){
					/* do not go to out */
					retrc = EINVAL;
				} else {
					/* assume since sensors can cast w/o overflow */
					v.v_u64 = (uint64_t)((double)tempval*lm_srcs[i]->multiplier + lm_srcs[i]->offset);
					ldms_set_metric(metric_table[metric_no], &v);
				}
			} else {
				/* do not go to out */
				retrc = EINVAL;
			}
			if (mf) fclose(mf);
		}
		metric_no++;
	}

	ldms_end_transaction(set);
	return retrc;
}

static void term(void)
{
	int i;

	ldms_destroy_set(set);
	for (i = 0; i < lm_nentries; i++){
		if (lm_srcs[i]){
			if (lm_srcs[i]->vname) free(lm_srcs[i]->vname);
			if (lm_srcs[i]->mname) free(lm_srcs[i]->mname);
			free(lm_srcs[i]);
		}
	}
	if (lm_srcs)
		free(lm_srcs);
	LDMS_JOBID_TERM;
}

static const char *usage(void)
{
	return  "config name=procsensors component_id=<comp_id> set=<set> config\n"
		"    comp_id    The component id.\n"
		LDMS_JOBID_DESC
		"    set        The set name.\n";
}

static struct ldmsd_sampler procsensors_plugin = {
	.base = {
		.name = "procsensors",
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
	return &procsensors_plugin.base;
}
