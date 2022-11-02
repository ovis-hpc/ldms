/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014-2016 Sandia Corporation. All rights reserved.
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

#define ARIES_MAX_TILES 48
#define ARIES_NUM_TILES 40

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

/**
 * For the XC, a single gpcdr file becomes too large for sys. It
 * must be split up. We require that it be split into four separate files,
 * one for each type
 */
typedef enum{
	TRAFFIC,
	STALLED,
	SENDLINKSTATUS,
	RECVLINKSTATUS,
	ENDLINKS,
} aries_linksmetrics_type_t;

typedef struct{
	aries_linksmetrics_type_t enumtype;
	char* fname;
	FILE* lm_f;
	char* basename;
	char* baseunit;
	int doderived;
	char* derivedname;
	char* derivedunit;
} aries_linksmetrics_info_t;

/* Defined in cray_aries_r_sampler.c */
extern ovis_log_t cray_aries_log;

/** NOTE: the enum isnt x-refed. Just iterate thru while < ENDLINKS */
aries_linksmetrics_info_t linksinfo[] = {
	{TRAFFIC, "/sys/devices/virtual/gni/gpcdr0/metricsets/linktraffic/metrics", NULL, "traffic", "(B)", 1, "SAMPLE_traffic", "(B/s)"},
	{STALLED, "/sys/devices/virtual/gni/gpcdr0/metricsets/linkstalled/metrics", NULL, "stalled", "(ns)", 0, NULL, NULL},
	{SENDLINKSTATUS, "/sys/devices/virtual/gni/gpcdr0/metricsets/linksendstatus/metrics", NULL, "sendlinkstatus", "(1)", 0, NULL, NULL},
	{RECVLINKSTATUS, "/sys/devices/virtual/gni/gpcdr0/metricsets/linkrecvstatus/metrics", NULL, "recvlinkstatus", "(1)", 0, NULL, NULL}
};

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

#define NICMETRICS_FILE  "/sys/devices/virtual/gni/gpcdr0/metricsets/nic/metrics"


/* LINKSMETRICS Specific */
static uint64_t linksmetrics_prev_time[ENDLINKS];
static int linksmetrics_time_multiplier[ENDLINKS];
static int* linksmetrics_base_metric_table[ENDLINKS];
static int* linksmetrics_derived_metric_table[ENDLINKS];
static uint64_t** linksmetrics_base_values[ENDLINKS]; /**< holds curr & prev raw module
					   data for derived computation */
static uint64_t* linksmetrics_base_diff[ENDLINKS]; /**< holds diffs for the module values */
static int linksmetrics_values_idx[ENDLINKS];
static int linksmetrics_valid;

/* NICMETRICS Specific */
static FILE *nm_f;
static uint64_t nicmetrics_prev_time;
static int nicmetrics_time_multiplier;
static int* nicmetrics_base_metric_table;
static int* nicmetrics_derived_metric_table;
static uint64_t** nicmetrics_base_values; /**< holds curr & prev raw module data
					for derived computation */
static int nicmetrics_values_idx; /**< index of the curr values for the above */
static int nicmetrics_valid;


static int hsn_metrics_type = HSN_METRICS_DEFAULT;

/** internal calculations */
static uint64_t __linksmetrics_derived_metric_calc(
	int i, uint64_t* diff, uint64_t time_delta);


//diridx is now the tile
static int __links_metric_name(int infoidx, int isbase, int tile,
			       char newname[]){

#ifdef HAVE_SPACELESS_NAMES
	char* format = "%s_%03d_%s";
#else
	char* format = "%s_%03d %s";
#endif

	if (isbase == 1)
		sprintf(newname, format,
			linksinfo[infoidx].basename,
			tile,
			linksinfo[infoidx].baseunit);
	else
		sprintf(newname, format,
			linksinfo[infoidx].derivedname,
			tile,
			linksinfo[infoidx].derivedunit);

	return 0;
}


int hsn_metrics_config(int i){
	int j;

	/* Check for the existence of the files. NOTE: this is
	   currently only done for the ARIES */
	for (j = 0; j < ENDLINKS; j++){
		FILE* junk = fopen(linksinfo[j].fname, "r");
		if (!junk){
			ovis_log(cray_aries_log, OVIS_LCRITICAL, "cray_aries_r_sampler: missing gpcdr file <%s>"
			       "Check that 1)gpcdr is running: lsmod | grep gpcdr and "
			       "2) the gpcdr configuration file "
			       "(e.g., /etc/opt/cray/gni-gpcdr-utils/gpcdr-init.conf "
			       "or if specified via GPCDRINIT_CONF) correctly specifies "
			       "METRICSETS=\"linktraffic linstalled linksendstatus "
			       "linkrecvstatus nic\"\n",
			       linksinfo[j].fname);
			return ENOENT;
		}
		fclose(junk);
	}

	return 0;

}



int aries_linksmetrics_setup()
{
	char lbuf[256];
	char metric_name[128];
	char units[32];
	char* s;
	uint64_t val;
	int metric_no = 0;
	int count = 0;
	int i, j, k, rc;

	rc = 0;
	linksmetrics_valid = 0;

	/** storage for derived computations if necessary */
	for (k = 0; k < ENDLINKS; k++){
		if (((hsn_metrics_type == HSN_METRICS_DERIVED) ||
		     (hsn_metrics_type == HSN_METRICS_BOTH)) &&
		    linksinfo[k].doderived){

			linksmetrics_base_values[k] = calloc(2, sizeof(uint64_t*));
			if (!linksmetrics_base_values[k])
				return ENOMEM;

			linksmetrics_base_diff[k] = calloc(ARIES_MAX_TILES,
							   sizeof(uint64_t));
			if (!linksmetrics_base_diff[k])
				return ENOMEM;

			for (i = 0; i < 2; i++){
				linksmetrics_base_values[k][i] =
					calloc(ARIES_MAX_TILES, sizeof(uint64_t));
				if (!linksmetrics_base_values[k][i])
					return ENOMEM;
			}
		}
	}

	/**
	 * Make sure files are there and all have metrics.
	 * Also store the prev vals if need them
	 */
	for (k = 0; k < ENDLINKS; k++){
		linksmetrics_values_idx[k] = 0;

		linksinfo[k].lm_f = fopen(linksinfo[k].fname, "r");
		if (!linksinfo[k].lm_f) {
			ovis_log(cray_aries_log, OVIS_LWARN,"Could not open the source file '%s'\n",
			       linksinfo[k].fname);
			return EINVAL;
		}

		fseek(linksinfo[k].lm_f, 0, SEEK_SET);
		/* timestamp */
		s = fgets(lbuf, sizeof(lbuf), linksinfo[k].lm_f);
		if (!s)
			return EINVAL;

		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name,
			    &linksmetrics_prev_time[k], units);

		if (rc != 3) {
			ovis_log(cray_aries_log, OVIS_LERROR,"Issue reading the source file '%s'\n",
			       linksinfo[k].fname);
			rc = EINVAL;
			return rc;
		}
		if (strcmp(units,"ms") == 0){
			linksmetrics_time_multiplier[k] = 1000;
		} else if (strcmp(units,"seconds") == 0){
			linksmetrics_time_multiplier[k] = 1;
		} else {
			ovis_log(cray_aries_log, OVIS_LERROR,"linksmetrics: wrong gpcdr interface (time units)\n");
			rc = EINVAL;
			return rc;
		}

		count = 0;
		do {
			int dir = -1;
			s = fgets(lbuf, sizeof(lbuf), linksinfo[k].lm_f);
			if (!s)
				break;
			rc = sscanf(lbuf, "%[^:]:%d %" PRIu64 " %s\n", metric_name, &dir, &val,
				    units);
			if (rc != 4) {
				ovis_log(cray_aries_log, OVIS_LERROR,"Issue reading the source file '%s'\n",
				       linksinfo[k].fname);
				rc = EINVAL;
				return rc;
			}

			/* store the val for the first calculation */
			if (((hsn_metrics_type == HSN_METRICS_DERIVED) ||
			     (hsn_metrics_type == HSN_METRICS_BOTH)) &&
			    linksinfo[k].doderived){
				linksmetrics_base_values[k][linksmetrics_values_idx[k]][dir] = val;
			}
			count++;
		} while (s);

		if (count != ARIES_NUM_TILES){
			ovis_log(cray_aries_log, OVIS_LERROR, "wrong number of metrics in file '%s'\n",
			       linksinfo[k].fname);
			rc = EINVAL;
			return rc;
		}

		linksmetrics_values_idx[k] = 1;

		//NOTE: leaving the filehandles open
	} /* ENDLINKS */

	linksmetrics_valid = 1;
	return 0;

}


int aries_nicmetrics_setup()
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
		ovis_log(cray_aries_log, OVIS_LWARN,"Could not open the source file '%s'\n",
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
			ovis_log(cray_aries_log, OVIS_LERROR,"Issue reading source file '%s'\n",
			       NICMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}
		if (strcmp(units,"ms") == 0){
			nicmetrics_time_multiplier = 1000;
		} else if (strcmp(units,"seconds") == 0){
			nicmetrics_time_multiplier = 1;
		} else {
			ovis_log(cray_aries_log, OVIS_LERROR,"nicmetrics: wrong gpcdr interface (time units)\n");
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
				ovis_log(cray_aries_log, OVIS_LERROR,"Issue reading source file '%s'\n",
				       NICMETRICS_FILE);
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


int add_metrics_aries_linksmetrics(ldms_schema_t schema)
{

	char newname[96];
	int i, j;
	int rc;


	/** NOTE: add all 48, even if they arent used */
	for (i = 0; i < ENDLINKS; i++){
		if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)){

			linksmetrics_base_metric_table[i] =
				calloc(ARIES_MAX_TILES, sizeof(int));
			if (!linksmetrics_base_metric_table[i])
				return ENOMEM;
		}


		if (((hsn_metrics_type == HSN_METRICS_DERIVED) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)) &&
		    linksinfo[i].doderived){
			linksmetrics_derived_metric_table[i] =
				calloc(ARIES_MAX_TILES, sizeof(int));
			if (!linksmetrics_derived_metric_table[i])
				return ENOMEM;
		} else {
			linksmetrics_derived_metric_table[i] = NULL;
		}
	}


	for (i = 0; i < ENDLINKS; i++){
		if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)){
			for (j = 0; j < ARIES_MAX_TILES; j++){
				__links_metric_name(i, 1, j, newname);
				rc = ldms_schema_metric_add(schema, newname, LDMS_V_U64);
				if (rc < 0){
					ovis_log(cray_aries_log, OVIS_LERROR, "Failed to add metric <%s>\n",
					       newname);
					return ENOMEM;
				}
				linksmetrics_base_metric_table[i][j] = rc;
			}
		}


		if (((hsn_metrics_type == HSN_METRICS_DERIVED) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)) &&
		    linksinfo[i].doderived){
			for (j = 0; j < ARIES_MAX_TILES; j++) {
				__links_metric_name(i, 0, j, newname);
				rc = ldms_schema_metric_add(schema, newname,
							    LDMS_V_U64);
				if (rc < 0){
					ovis_log(cray_aries_log, OVIS_LERROR, "Failed to add metric <%s>\n",
					       newname);
					return ENOMEM;
				}
				linksmetrics_derived_metric_table[i][j] = rc;
			}
		}
	}
	return 0;
}


int add_metrics_aries_nicmetrics(ldms_schema_t schema)
{
	char newname[96];
	int i, j;
	int rc;

	if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		nicmetrics_base_metric_table =
			calloc(NUM_NICMETRICS, sizeof(int));
		if (!nicmetrics_base_metric_table)
			return ENOMEM;
	}

	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		nicmetrics_derived_metric_table =
			calloc(NUM_NICMETRICS, sizeof(int));
		if (!nicmetrics_derived_metric_table)
			return ENOMEM;

	}

	if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		for (i = 0; i < NUM_NICMETRICS; i++){
			rc = ldms_schema_metric_add(schema, nicmetrics_basename[i],
						    LDMS_V_U64);
			if (rc < 0){
				ovis_log(cray_aries_log, OVIS_LERROR, "Failed to add metric <%s>\n",
				       nicmetrics_basename[i]);
				return ENOMEM;
			}
			nicmetrics_base_metric_table[i] = rc;
		}
	}

	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		for (i = 0; i < NUM_NICMETRICS; i++) {
			sprintf(newname, "%s_%s %s",
				nicmetrics_derivedprefix,
				nicmetrics_basename[i],
				nicmetrics_derivedunit);
			rc = ldms_schema_metric_add(schema, newname, LDMS_V_U64);
			if (rc < 0){
				ovis_log(cray_aries_log, OVIS_LERROR, "Failed to add metric <%s>\n",
				       newname);
				return ENOMEM;
			}
			nicmetrics_derived_metric_table[i] = rc;
		}
	}

	return 0;

 err:
	return rc;
}

int sample_metrics_aries_linksmetrics(ldms_set_t set)
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

	if (!linksmetrics_valid)
		return 0;

	for (i = 0; i < ENDLINKS; i++){
		FILE* lm_f = linksinfo[i].lm_f;
		idx = linksmetrics_values_idx[i];

		if (lm_f == NULL){
			continue;
		}

		fseek(lm_f, 0, SEEK_SET);

		/* read the timestamp */
		s = fgets(lbuf, sizeof(lbuf), lm_f);
		if (!s) {
			ovis_log(cray_aries_log, OVIS_LERROR,"Issue reading the source file '%s'\n",
			       linksinfo[i].fname);
			return EINVAL;
		}
		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &curr_time,
			    units);
		if (rc != 3) {
			ovis_log(cray_aries_log, OVIS_LERROR,"Issue reading the source file '%s'\n",
			       linksinfo[i].fname);
			rc = EINVAL;
			return rc;
		}

		count = 0;
		time_delta = curr_time - linksmetrics_prev_time[i];
		do {
			s = fgets(lbuf, sizeof(lbuf), lm_f);
			if (!s)
				break;
			rc = sscanf(lbuf, "%[^:]:%d %" PRIu64 " %s\n", metric_name, &dir, &v.v_u64,
				    units);
			if (rc != 4) {
				ovis_log(cray_aries_log, OVIS_LERROR,"Issue reading the source file '%s'\n",
				       linksinfo[i].fname);
				rc = EINVAL;
				return rc;
			}

			if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
			    (hsn_metrics_type == HSN_METRICS_BOTH)){
				ldms_metric_set(set,
						linksmetrics_base_metric_table[i][dir],
						&v);
			}

			if (((hsn_metrics_type == HSN_METRICS_DERIVED) ||
			    (hsn_metrics_type == HSN_METRICS_BOTH)) &&
			    linksinfo[i].doderived){
				linksmetrics_base_values[i][idx][dir] = v.v_u64;

				if ( linksmetrics_base_values[i][idx][dir] <
				     linksmetrics_base_values[i][!idx][dir]) {
					/* the gpcdr values are 64 bit */
					linksmetrics_base_diff[i][dir] =
						(ULONG_MAX - linksmetrics_base_values[i][!idx][dir]) +
						linksmetrics_base_values[i][idx][dir];
				} else {
					linksmetrics_base_diff[i][dir] =
						linksmetrics_base_values[i][idx][dir] -
						linksmetrics_base_values[i][!idx][dir];
				}
			}
			count++;

		} while (s); /** read whole file */

		if (((hsn_metrics_type == HSN_METRICS_DERIVED) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)) &&
		    linksinfo[i].doderived){
			for (j = 0; j < ARIES_MAX_TILES; j++) {
				/** there are 8 that wont need to be done, but those will ret 0 (base_diff = 0) */
				v.v_u64 = __linksmetrics_derived_metric_calc(
				     i, &(linksmetrics_base_diff[i][j]),
				     time_delta);
				ldms_metric_set(set,
						linksmetrics_derived_metric_table[i][j],
						&v);
			}
		}

		if (count != ARIES_NUM_TILES){
			ovis_log(cray_aries_log, OVIS_LERROR, "linksmetrics: in sample wrong num values for '%s'\n",
			       linksinfo[i].fname);
			linksmetrics_valid = 0;
			return EINVAL;
		}

		linksmetrics_values_idx[i] = (linksmetrics_values_idx[i] == 0? 1 : 0);
		linksmetrics_prev_time[i] = curr_time;
	}

	return 0;

}


int sample_metrics_aries_nicmetrics(ldms_set_t set)
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
		ovis_log(cray_aries_log, OVIS_LERROR,"Issue reading source file '%s'\n",
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
			ovis_log(cray_aries_log, OVIS_LERROR,"Issue reading source file '%s'\n",
			       NICMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}

		if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)){
			ldms_metric_set(set, nicmetrics_base_metric_table[i], &v);
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
				v.v_u64 = (uint64_t)((diff*nicmetrics_time_multiplier)/time_delta);
			else
				v.v_u64 = 0;

			ldms_metric_set(set, nicmetrics_derived_metric_table[i], &v);
		}
	}

	nicmetrics_values_idx = (nicmetrics_values_idx == 0? 1: 0);
	nicmetrics_prev_time = curr_time;
	return 0;

}

static uint64_t __linksmetrics_derived_metric_calc(int i, uint64_t* diff,
						   uint64_t timedelta){
	int rc = 0;


	switch (i) {
	case TRAFFIC:
		if ((timedelta > 0) && (diff != 0)){
			return (uint64_t)
				((double)(linksmetrics_time_multiplier[i] * (*diff))/(double) timedelta);
		} else {
			return 0;
		}
		break;
	default:
		return 0;
	}
}
