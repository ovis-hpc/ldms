/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014 Sandia Corporation. All rights reserved.
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
 * \file aries_metrics_gpcdr.c
 * \brief Functions used in the cray_system_sampler that are particular to
 * aries using gpcdr interface.
 */

#include "aries_metrics_gpcdr.h"

#define ARIES_NUM_TILES 48

typedef enum {
	HSN_METRICS_COUNTER,
	HSN_METRICS_DERIVED,
	HSN_METRICS_BOTH,
	HSN_METRICS_END
} hsn_metrics_type_t;
#define HSN_METRICS_DEFAULT HSN_METRICS_COUNTER

#define STR_WRAP(NAME) #NAME
#define PREFIX_ENUM_M(NAME) M_ ## NAME
#define PREFIX_ENUM_LB(NAME) LB_ ## NAME
#define PREFIX_ENUM_LD(NAME) LD_ ## NAME

#define LINKSMETRICS_BASE_LIST(WRAP)\
	WRAP(traffic), \
	WRAP(stalled),	      \
	WRAP(sendlinkstatus), \
	WRAP(recvlinkstatus)

//THESE HAVE NOT BEEN FIXED YET
#define LINKSMETRICS_DERIVED_LIST(WRAP) \
	WRAP(SAMPLE_ARIES_TRAFFIC)

static char* linksmetrics_basename[] = {
	LINKSMETRICS_BASE_LIST(STR_WRAP)
};
static char* linksmetrics_derivedname[] = {
	LINKSMETRICS_DERIVED_LIST(STR_WRAP)
};
typedef enum {
	LINKSMETRICS_BASE_LIST(PREFIX_ENUM_LB)
} linksmetrics_base_metric_t;
typedef enum {
	LINKSMETRICS_DERIVED_LIST(PREFIX_ENUM_LD)
} linksmetrics_derived_metric_t;

static char* linksmetrics_baseunit[] = {
	"(B)",
	"(ns)",
	"(1)",
	"(1)"
	};

static char* linksmetrics_derivedunit[] = {
	"(B/s)",
	};
#define NUM_LINKSMETRICS_DIR (sizeof(linksmetrics_dir)/sizeof(linksmetrics_dir[0]))
#define NUM_LINKSMETRICS_BASENAME (sizeof(linksmetrics_basename)/sizeof(linksmetrics_basename[0]))
#define NUM_LINKSMETRICS_DERIVEDNAME (sizeof(linksmetrics_derivedname)/sizeof(linksmetrics_derivedname[0]))

#define NICMETRICS_BASE_LIST(WRAP) \
        WRAP(totaloutput), \
                WRAP(totalinput), \
                WRAP(fmaout), \
                WRAP(bteout)

static char* nicmetrics_derivedprefix = "SAMPLE";
static char* nicmetrics_derivedunit =  "(B/s)";

static char* nicmetrics_basename[] = {
        NICMETRICS_BASE_LIST(STR_WRAP)
};

typedef enum {
        NICMETRICS_BASE_LIST(PREFIX_ENUM_M)
} nicmetrics_metric_t;
#define NUM_NICMETRICS (sizeof(nicmetrics_basename)/sizeof(nicmetrics_basename[0]))

#define LINKSMETRICS_FILE  "/sys/devices/virtual/gni/gpcdr0/metricsets/links/metrics"
#define NICMETRICS_FILE  "/sys/devices/virtual/gni/gpcdr0/metricsets/nic/metrics"


/* LINKSMETRICS Specific */
static FILE *lm_f;
static uint64_t linksmetrics_prev_time;
static int linksmetrics_time_multiplier;
static ldms_metric_t* linksmetrics_base_metric_table;
static ldms_metric_t* linksmetrics_derived_metric_table;
static uint64_t*** linksmetrics_base_values; /**< holds curr & prev raw module
					   data for derived computation */
static uint64_t** linksmetrics_base_diff; /**< holds diffs for the module values */

static int linksmetrics_values_idx; /**< index of the curr values for the above */
static int num_linksmetrics_exists;
static int* linksmetrics_indicies; /**< track metric table index in
			       which to store the raw data */

static int linksmetrics_valid;

/* NICMETRICS Specific */
static FILE *nm_f;
static uint64_t nicmetrics_prev_time;
static int nicmetrics_time_multiplier;
static ldms_metric_t* nicmetrics_base_metric_table;
static ldms_metric_t* nicmetrics_derived_metric_table;
static uint64_t** nicmetrics_base_values; /**< holds curr & prev raw module data
					for derived computation */
static int nicmetrics_values_idx; /**< index of the curr values for the above */
static int nicmetrics_valid;


static int hsn_metrics_type = HSN_METRICS_DEFAULT;


/** internal calculations */
static uint64_t __linksmetrics_derived_metric_calc(
	int i, int j, uint64_t** diff, uint64_t time_delta);
static int __links_metric_name(int, int, int, char[]);


//diridx is now the tile
static int __links_metric_name(int isbase, int nameidx,
				   int diridx, char newname[]){

	if (isbase == 1)
		sprintf(newname, "%s_%03d %s",
			linksmetrics_basename[nameidx],
			diridx,
			linksmetrics_baseunit[nameidx]);
	else
		sprintf(newname, "%s_%03d %s",
			linksmetrics_derivedname[nameidx],
			diridx,
			linksmetrics_derivedunit[nameidx]);

	return 0;
}


int hsn_metrics_config(int i){
	if ((i < 0) || (i >= HSN_METRICS_END))
		return EINVAL;

	hsn_metrics_type = i;

	return 0;

}


int get_metric_size_aries_linksmetrics(size_t *m_sz, size_t *d_sz,
				 ldmsd_msg_log_f msglog)
{
	size_t tot_meta_sz = 0;
	size_t tot_data_sz = 0;
	size_t meta_sz = 0;
	size_t data_sz = 0;
	char newname[96];
	int count;
	int num_possible_base_names = 0;
	int i, j;
	int rc;

	count = 0;
	if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		for (i = 0; i < NUM_LINKSMETRICS_BASENAME; i++){
			for (j = 0; j < ARIES_NUM_TILES; j++){
				__links_metric_name(1, i, j, newname);
				rc = ldms_get_metric_size(newname,
							  LDMS_V_U64,
							  &meta_sz,
							  &data_sz);
				if (rc)
					return rc;
				tot_meta_sz += meta_sz;
				tot_data_sz += data_sz;
				count++;
			}
		}

		linksmetrics_base_metric_table =
			calloc(count, sizeof(ldms_metric_t));
		if (!linksmetrics_base_metric_table)
			return ENOMEM;

		/* keep track of the next possible index to use */
		linksmetrics_indicies = calloc(count, sizeof(int));
		if (!linksmetrics_indicies)
			return ENOMEM;
	}

	count = 0;
	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		for (i = 0; i < NUM_LINKSMETRICS_DERIVEDNAME; i++) {
			for (j = 0; j < ARIES_NUM_TILES; j++) {
				__links_metric_name(0, i, j, newname);
				rc = ldms_get_metric_size(newname,
							  LDMS_V_U64,
							  &meta_sz,
							  &data_sz);
				if (rc)
					return rc;
				tot_meta_sz += meta_sz;
				tot_data_sz += data_sz;
				count++;
			}
		}

		linksmetrics_derived_metric_table =
			calloc(count, sizeof(ldms_metric_t));
		if (!linksmetrics_derived_metric_table)
			return ENOMEM;

	}

	*m_sz = tot_meta_sz;
	*d_sz = tot_data_sz;

	return 0;

}

int get_metric_size_nicmetrics(size_t *m_sz, size_t *d_sz,
			       ldmsd_msg_log_f msglog)
{
	size_t tot_meta_sz = 0;
	size_t tot_data_sz = 0;
	size_t meta_sz = 0;
	size_t data_sz = 0;
	char newname[96];
	int i, j;
	int rc;

	if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		for (i = 0; i < NUM_NICMETRICS; i++){
			rc = ldms_get_metric_size(nicmetrics_basename[i],
						  LDMS_V_U64,
						  &meta_sz, &data_sz);
			if (rc)
				return rc;
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
		}

		nicmetrics_base_metric_table =
			calloc(NUM_NICMETRICS, sizeof(ldms_metric_t));
		if (!nicmetrics_base_metric_table)
			return ENOMEM;
	}

	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		for (i = 0; i < NUM_NICMETRICS; i++) {
			sprintf(newname, "%s_%s %s",
				nicmetrics_derivedprefix,
				nicmetrics_basename[i],
				nicmetrics_derivedunit);
			rc = ldms_get_metric_size(newname, LDMS_V_U64,
						  &meta_sz, &data_sz);
			if (rc)
				return rc;
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
		}

		nicmetrics_derived_metric_table =
			calloc(NUM_NICMETRICS, sizeof(ldms_metric_t));
		if (!nicmetrics_derived_metric_table)
			return ENOMEM;

	}

	*m_sz = tot_meta_sz;
	*d_sz = tot_data_sz;

	return 0;

}


int aries_linksmetrics_setup(ldmsd_msg_log_f msglog)
{
	char lbuf[256];
	char metric_name[128];
	char units[32];
	char* s;
	uint64_t val;
	int metric_no = 0;
	int count = 0;
	int i, j, rc;
	int lastbase = 0;

	rc = 0;
	num_linksmetrics_exists = 0;
	linksmetrics_valid = 0;

	lm_f = fopen(LINKSMETRICS_FILE, "r");
	if (!lm_f) {
		msglog(LDMS_LDEBUG,"WARNING: Could not open the source file '%s'\n",
		       LINKSMETRICS_FILE);
		return EINVAL;

	}


	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){

		/** storage for metrics for computations in terms of the ones
		 * gpcdr can possibly have */

		linksmetrics_base_values = calloc(2, sizeof(uint64_t**));
		if (!linksmetrics_base_values)
			return ENOMEM;

		linksmetrics_base_diff = calloc(NUM_LINKSMETRICS_BASENAME,
						sizeof(uint64_t*));
		if (!linksmetrics_base_diff)
			return ENOMEM;

		for (i = 0; i < 2; i++){
			linksmetrics_base_values[i] =
				calloc(NUM_LINKSMETRICS_BASENAME,
				       sizeof(uint64_t*));
			if (!linksmetrics_base_values[i])
				return ENOMEM;

			for (j = 0; j < NUM_LINKSMETRICS_BASENAME; j++){
				linksmetrics_base_values[i][j] =
					calloc(ARIES_NUM_TILES,
					       sizeof(uint64_t));
				if (!linksmetrics_base_values[i][j])
					return ENOMEM;

				if (i == 0){
					linksmetrics_base_diff[j] =
						calloc(ARIES_NUM_TILES,
						       sizeof(uint64_t));
					if (!linksmetrics_base_diff[j])
						return ENOMEM;
				}
			}
		}

		linksmetrics_values_idx = 0;
	}

	/* Open the file now and determine which metrics are there */

	fseek(lm_f, 0, SEEK_SET);
	/* timestamp */
	s = fgets(lbuf, sizeof(lbuf), lm_f);
	if (!s)
		return EINVAL;
	rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name,
		    &linksmetrics_prev_time, units);
	if (rc != 3) {
		msglog(LDMS_LDEBUG,"ERR: Issue reading the source file '%s'\n",
		       LINKSMETRICS_FILE);
		rc = EINVAL;
		return rc;
	}
	if (strcmp(units,"ms") == 0){
		linksmetrics_time_multiplier = 1000;
	} else if (strcmp(units,"seconds") == 0){
		linksmetrics_time_multiplier = 1;
	} else {
		msglog(LDMS_LDEBUG,"linksmetrics: wrong gpcdr interface (time units)\n");
		rc = EINVAL;
		return rc;
	}

	do {
		int dir = -1;
		s = fgets(lbuf, sizeof(lbuf), lm_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%[^:]:%d %" PRIu64 " %s\n", metric_name, &dir, &val,
			    units);
		if (rc != 4) {
			msglog(LDMS_LDEBUG,"ERR: Issue reading the source file '%s'\n",
			       LINKSMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}

		while((lastbase < NUM_LINKSMETRICS_BASENAME) &&
		      !strstr(metric_name, linksmetrics_basename[lastbase])){
			lastbase++;
		}

		if ( (dir < 0) || (lastbase == NUM_LINKSMETRICS_BASENAME)){
			msglog(LDMS_LDEBUG,"cray_system_sampler: linksmetric bad metric\n");
			return EINVAL;
		}

		/* metric_no in terms of the ones gpcdr can possibly have */
		metric_no = lastbase*ARIES_NUM_TILES+dir;
		linksmetrics_indicies[num_linksmetrics_exists++] =  metric_no;

		/* store the val for the first calculation */
		if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)){
			linksmetrics_base_values[
				linksmetrics_values_idx][lastbase][dir] = val;
		}
	} while (s);


	linksmetrics_values_idx = 1;
	linksmetrics_valid = 1;

	return 0;

}


int nicmetrics_setup(ldmsd_msg_log_f msglog)
{
	char lbuf[256];
	char metric_name[128];
	char units[32];
	char* s;
	uint64_t val;
	int metric_no = 0;
	int i, j, rc;

	rc = 0;
	nicmetrics_valid = 0;

	nm_f = fopen(NICMETRICS_FILE, "r");
	if (!nm_f) {
		msglog(LDMS_LDEBUG,"WARNING: Could not open the source file '%s'\n",
		       NICMETRICS_FILE);
		return EINVAL;
	}


	/** storage for derived metrics */
	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		nicmetrics_base_values = calloc(2, sizeof(uint64_t*));
		if (!nicmetrics_base_values)
			return ENOMEM;
		for (i = 0; i < 2; i++){
			nicmetrics_base_values[i] =
				calloc(NUM_NICMETRICS, sizeof(uint64_t));
			if (!nicmetrics_base_values[i])
				return ENOMEM;
		}
	}

	nicmetrics_values_idx = 0;

	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		/* Open the file and store the first set of values */
		fseek(nm_f, 0, SEEK_SET);
		/* timestamp */
		s = fgets(lbuf, sizeof(lbuf), nm_f);
		if (!s)
			return EINVAL;
		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n",
			    metric_name, &nicmetrics_prev_time, units);
		if (rc != 3) {
			msglog(LDMS_LDEBUG,"ERR: Issue reading source file '%s'\n",
			       NICMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}
		if (strcmp(units,"ms") == 0){
			nicmetrics_time_multiplier = 1000;
		} else if (strcmp(units,"seconds") == 0){
			nicmetrics_time_multiplier = 1;
		} else {
			msglog(LDMS_LDEBUG,"nicmetrics: wrong gpcdr interface (time units)\n");
			rc = EINVAL;
			return rc;
		}
		metric_no = 0;
		do {
			s = fgets(lbuf, sizeof(lbuf), nm_f);
			if (!s)
				break;
			rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name,
				    &val, units);
			if (rc != 3) {
				msglog(LDMS_LDEBUG,"ERR: Issue reading source file '%s'\n",
				       LINKSMETRICS_FILE);
				rc = EINVAL;
				return rc;
			}
			nicmetrics_base_values[
				nicmetrics_values_idx][metric_no++] = val;
		} while (s);

		nicmetrics_values_idx = 1;
	}

	nicmetrics_valid = 1;

	return 0;
}


int add_metrics_aries_linksmetrics(ldms_set_t set, int comp_id,
			     ldmsd_msg_log_f msglog)
{
	char newname[96];
	int metric_no;
	int i, j;
	int rc = 0;


	if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		metric_no = 0;
		for (i = 0; i < NUM_LINKSMETRICS_BASENAME; i++){
			for (j = 0; j < ARIES_NUM_TILES; j++){
				__links_metric_name(1, i, j, newname);
				linksmetrics_base_metric_table[metric_no] =
					ldms_add_metric(set, newname,
							LDMS_V_U64);
				if (!linksmetrics_base_metric_table[metric_no])
					return ENOMEM;
				/* XXX comp_id */
				ldms_set_user_data(
					linksmetrics_base_metric_table[
						metric_no++],
					comp_id);
			}
		}
	}

	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		metric_no = 0;
		for (i = 0; i < NUM_LINKSMETRICS_DERIVEDNAME; i++) {
			for (j = 0; j < ARIES_NUM_TILES; j++) {
				__links_metric_name(0, i, j, newname);
				linksmetrics_derived_metric_table[metric_no] =
					ldms_add_metric(set, newname,
							LDMS_V_U64);
				if (!linksmetrics_derived_metric_table[
					    metric_no])
					return ENOMEM;
				/* XXX comp_id */
				ldms_set_user_data(
					linksmetrics_derived_metric_table[
						metric_no++],
					comp_id);
			}
		}
	}


 err:
	return rc;
}


int add_metrics_nicmetrics(ldms_set_t set, int comp_id, ldmsd_msg_log_f msglog)
{
	char newname[96];
	int i;
	int rc = 0;


	if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		for (i = 0; i < NUM_NICMETRICS; i++){
			nicmetrics_base_metric_table[i] =
				ldms_add_metric(set, nicmetrics_basename[i],
						LDMS_V_U64);
			if (!nicmetrics_base_metric_table[i])
				return ENOMEM;
			/* XXX comp_id */
			ldms_set_user_data(nicmetrics_base_metric_table[i],
					   comp_id);
		}
	}

	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		for (i = 0; i < NUM_NICMETRICS; i++) {
			sprintf(newname, "%s_%s %s",
				nicmetrics_derivedprefix,
				nicmetrics_basename[i],
				nicmetrics_derivedunit);
			nicmetrics_derived_metric_table[i] =
				ldms_add_metric(set, newname, LDMS_V_U64);
			if (!nicmetrics_derived_metric_table[i])
				return ENOMEM;
			/* XXX comp_id */
			ldms_set_user_data(nicmetrics_derived_metric_table[i],
					   comp_id);
		}
	}

 err:
	return rc;
}

int sample_metrics_aries_linksmetrics(ldmsd_msg_log_f msglog)
{
	char lbuf[256];
	char metric_name[64];
	int dir;
	char units[32];
	char* s;
	union ldms_value v;
	uint64_t curr_time;
	uint64_t time_delta;
	int metric_no = 0;
	int count = 0;
	int idx = 0;
	int i, j, rc;

	if (!lm_f || !linksmetrics_valid)
		return 0;


	fseek(lm_f, 0, SEEK_SET);

	/* read the timestamp */
	s = fgets(lbuf, sizeof(lbuf), lm_f);
	if (!s) {
		msglog(LDMS_LDEBUG,"ERR: Issue reading the source file '%s'\n",
		       LINKSMETRICS_FILE);
		return EINVAL;
	}
	rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &curr_time,
		    units);
	if (rc != 3) {
		msglog(LDMS_LDEBUG,"ERR: Issue reading the source file '%s'\n",
		       LINKSMETRICS_FILE);
		rc = EINVAL;
		return rc;
	}

	count = 0;
	idx = linksmetrics_values_idx;
	time_delta = curr_time - linksmetrics_prev_time;
	do {
		s = fgets(lbuf, sizeof(lbuf), lm_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%[^:]:%d %" PRIu64 " %s\n", metric_name, &dir, &v.v_u64,
			    units);
		if (rc != 4) {
			msglog(LDMS_LDEBUG,"ERR: Issue reading the source file '%s'\n",
			       LINKSMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}

		if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)){

			ldms_set_metric(
				linksmetrics_base_metric_table[
					linksmetrics_indicies[count]], &v);
		}

		if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)){
			int ibase, idir;
			ibase = (int)(linksmetrics_indicies[count]/
				      ARIES_NUM_TILES);
			idir = dir;

			linksmetrics_base_values[idx][ibase][idir] = v.v_u64;

			if ( linksmetrics_base_values[idx][ibase][idir] <
			     linksmetrics_base_values[!idx][ibase][idir]) {
				/* the gpcdr values are 64 bit */
				linksmetrics_base_diff[ibase][idir] =
					(ULONG_MAX -
					 linksmetrics_base_values[!idx][ibase][
						 idir]) +
					linksmetrics_base_values[idx][ibase][
						idir];
			} else {
				linksmetrics_base_diff[ibase][idir] =
					linksmetrics_base_values[idx][ibase][
						idir] -
					linksmetrics_base_values[!idx][ibase][
						idir];
			}
		}
		count++;

	} while (s);

	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		metric_no = 0;
		for (i = 0; i < NUM_LINKSMETRICS_DERIVEDNAME; i++) {
			for (j = 0; j < ARIES_NUM_TILES; j++) {
				v.v_u64 = __linksmetrics_derived_metric_calc(
					i, j, linksmetrics_base_diff,
					time_delta);
				ldms_set_metric(
					linksmetrics_derived_metric_table[
						metric_no++], &v);
			}
		}
	}

	if (count != num_linksmetrics_exists) {
		linksmetrics_valid = 0;
		return EINVAL;
	}

	linksmetrics_values_idx = (linksmetrics_values_idx == 0? 1 : 0);
	linksmetrics_prev_time = curr_time;

	return 0;

}


int sample_metrics_nicmetrics(ldmsd_msg_log_f msglog)
{
	char lbuf[256];
	char metric_name[64];
	char units[32];
	uint64_t curr_time;
	uint64_t time_delta;
	char* s;
	union ldms_value v;
	int idx;
	int i, rc;

	if (!nm_f || !nicmetrics_valid)
		return 0;

	fseek(nm_f, 0, SEEK_SET);
	/* timestamp */
	s = fgets(lbuf, sizeof(lbuf), nm_f);
	if (!s) {
		rc = EINVAL;
		return rc;
	}

	rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &curr_time,
		    units);
	if (rc != 3) {
		msglog(LDMS_LDEBUG,"ERR: Issue reading source file '%s'\n",
		       NICMETRICS_FILE);
		rc = EINVAL;
		return rc;
	}

	time_delta = curr_time - nicmetrics_prev_time;	/* units see below */
	idx = nicmetrics_values_idx;
	i = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), nm_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &v.v_u64,
			    units);
		if (rc != 3) {
			msglog(LDMS_LDEBUG,"ERR: Issue reading source file '%s'\n",
			       NICMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}

		if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)){
			ldms_set_metric(nicmetrics_base_metric_table[i], &v);
		}
		if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)){
			nicmetrics_base_values[idx][i] = v.v_u64;
		}
		i++;
	} while (s);

	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		for (i = 0; i < NUM_NICMETRICS; i++){
			uint64_t diff;

			/* the gpcdr values are 64 bit */
			if ( nicmetrics_base_values[idx][i] <
			     nicmetrics_base_values[!idx][i] ) {
				diff = (ULONG_MAX -
					nicmetrics_base_values[!idx][i]) +
					nicmetrics_base_values[idx][i];
			} else {
				diff = nicmetrics_base_values[idx][i] -
					nicmetrics_base_values[!idx][i];
			}

			if (time_delta > 0)
				//				v.v_u64 = (uint64_t)((diff*1000)/time_delta);
				v.v_u64 = (uint64_t)((diff*nicmetrics_time_multiplier)/time_delta);
			else
				v.v_u64 = 0;

			ldms_set_metric(nicmetrics_derived_metric_table[i], &v);
		}
	}

	nicmetrics_values_idx = (nicmetrics_values_idx == 0? 1: 0);
	nicmetrics_prev_time = curr_time;
	return 0;

}

static uint64_t __linksmetrics_derived_metric_calc(int i, int j,
						   uint64_t** diff,
						   uint64_t timedelta){
	int rc = 0;

	switch (i) {
	case LD_SAMPLE_ARIES_TRAFFIC:
		if (timedelta > 0)
			return (uint64_t)
				((double)(linksmetrics_time_multiplier * diff[LB_traffic][j])/(double) timedelta);
		else
			return 0;
		break;
	default:
		return 0;
	}
}
