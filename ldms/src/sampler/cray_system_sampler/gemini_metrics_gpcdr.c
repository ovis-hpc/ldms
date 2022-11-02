/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2016 Sandia Corporation. All rights reserved.
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
 * \file gemini_metrics_gpcdr.c
 * \brief Functions used in the cray_system_sampler that are particular to
 * gemini using gpcdr interface.
 */

/* have to include this now to get the max bw */
#include "rtr_util.h"
#include "gemini.h"
#include "gemini_metrics_gpcdr.h"

/* Defined in the plugins */
extern ovis_log_t cray_gemini_log;

typedef enum {
	HSN_METRICS_COUNTER,
	HSN_METRICS_DERIVED,
	HSN_METRICS_BOTH,
	HSN_METRICS_END
} hsn_metrics_type_t;
#define HSN_METRICS_DEFAULT HSN_METRICS_COUNTER


/** order in the module. Counting on this being same as in gemini.h */
static char* linksmetrics_dir[] = {
	"X+","X-","Y+","Y-","Z+","Z-"
};

#define STR_WRAP(NAME) #NAME
#define PREFIX_ENUM_M(NAME) M_ ## NAME
#define PREFIX_ENUM_LB(NAME) LB_ ## NAME
#define PREFIX_ENUM_LD(NAME) LD_ ## NAME

/** default conf file reports these as "stalled" but the counter is the inq_stall" */
#define LINKSMETRICS_BASE_LIST(WRAP)\
	WRAP(traffic), \
	WRAP(packets), \
	WRAP(inq_stall),	      \
	WRAP(credit_stall),	      \
	WRAP(sendlinkstatus), \
	WRAP(recvlinkstatus)

#define LINKSMETRICS_DERIVED_LIST(WRAP) \
	WRAP(SAMPLE_GEMINI_LINK_BW),	\
	WRAP(SAMPLE_GEMINI_LINK_USED_BW),	\
	WRAP(SAMPLE_GEMINI_LINK_PACKETSIZE_AVE), \
		WRAP(SAMPLE_GEMINI_LINK_INQ_STALL), \
	WRAP(SAMPLE_GEMINI_LINK_CREDIT_STALL)


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
	"(1)",
	"(ns)",
	"(ns)",
	"(1)",
	"(1)"
	};

#ifdef HAVE_SPACELESS_NAMES
static char* linksmetrics_derivedunit[] = {
	"(B/s)",
	"(\%_x1e6)",
	"(B)",
	"(\%_x1e6)",
	"(\%_x1e6)"
	};
#else
static char* linksmetrics_derivedunit[] = {
	"(B/s)",
	"(\% x1e6)",
	"(B)",
	"(\% x1e6)",
	"(\% x1e6)"
	};
#endif

#define NUM_LINKSMETRICS_DIR (sizeof(linksmetrics_dir)/sizeof(linksmetrics_dir[0]))
#define NUM_LINKSMETRICS_BASENAME (sizeof(linksmetrics_basename)/sizeof(linksmetrics_basename[0]))
#define NUM_LINKSMETRICS_DERIVEDNAME (sizeof(linksmetrics_derivedname)/sizeof(linksmetrics_derivedname[0]))

#define NICMETRICS_BASE_LIST(WRAP) \
	WRAP(totaloutput_optA),     \
		WRAP(totalinput), \
		WRAP(fmaout), \
		WRAP(bteout_optA), \
		WRAP(bteout_optB), \
		WRAP(totaloutput_optB)

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

#define RCAHELPER_CMD "/opt/cray/rca/default/bin/rca-helper -O"

/* LINKSMETRICS Specific */
static FILE *lm_f;
static uint64_t linksmetrics_prev_time;
static int* linksmetrics_base_metric_table;
static int* linksmetrics_derived_metric_table;
static uint64_t*** linksmetrics_base_values; /**< holds curr & prev raw module
					   data for derived computation */
static uint64_t** linksmetrics_base_diff; /**< holds diffs for the module values */

static int linksmetrics_values_idx; /**< index of the curr values for the above */
static int num_linksmetrics_exists;
static int* linksmetrics_indicies; /**< track metric table index in
			       which to store the raw data */

static double linksmetrics_max_link_bw[GEMINI_NUM_LOGICAL_LINKS];
static int linksmetrics_tiles_per_dir[GEMINI_NUM_LOGICAL_LINKS];
static int linksmetrics_valid;

static char* rtrfile = NULL; /**< needed for gpcd, but also used to get maxbw for gpcdr */

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
	int i, int j, uint64_t** diff, uint64_t time_delta);
static int __links_metric_name(int, int, int, char[]);
static int rcahelper_tilesperdir();
static int bwhelper_maxbwperdir();


static int __links_metric_name(int isbase, int nameidx,
				   int diridx, char newname[]){

#ifdef HAVE_SPACELESS_NAMES
	char* format = "%s_%s_%s";
#else
	char* format = "%s_%s %s";
#endif

	if (isbase == 1)
		sprintf(newname, format,
			linksmetrics_dir[diridx],
			linksmetrics_basename[nameidx],
			linksmetrics_baseunit[nameidx]);
	else
		sprintf(newname, format,
			linksmetrics_dir[diridx],
			linksmetrics_derivedname[nameidx],
			linksmetrics_derivedunit[nameidx]);

	return 0;
}


static int bwhelper_maxbwperdir()
{

	/** have to get these values from the interconnect file for now.
	 * dimension will then include the host dir
	 */

	gemini_state_t junk;
	int tiles_per_dir_junk[GEMINI_NUM_LOGICAL_LINKS];
	int rc;

	if (rtrfile == NULL)
		return EINVAL;

	rc = gem_link_perf_parse_interconnect_file(rtrfile,
						   junk.tile,
						   &linksmetrics_max_link_bw,
						   &tiles_per_dir_junk);

	if (rc != 0){
		ovis_log(cray_gemini_log, OVIS_LERROR,"linksmetrics: Error parsing interconnect file\n");
		return rc;
	}

	return 0;

}


int rcahelper_tilesperdir()
{

	FILE* pipe;
	char buffer[128];
	char *saveptr = NULL;

	int i;
	int rc;

	pipe = popen(RCAHELPER_CMD, "r");
	if (!pipe) {
		ovis_log(cray_gemini_log, OVIS_LERROR,"gemini_metrics: rca-helper fail\n");
		rc = EINVAL;
		goto err;
	}

	/**
	 * output format:
	 * rca-helper -O
	 * X+      002 003 004 005 012 013 014 015 030 031 036 037 040 041 046 047
	 * X-
	 * Y+      042 045 050 051 052 055 056 057
	 * Y-
	 * Z+      000 001 010 011 025 026 027 035
	 * Z-      006 007 016 017 020 021 022 032
	 *
	 * NOTE: Can later see about using the new ioctl
	 * NOTE: later may want to retain the tile info.
	 */

	i = 0;
	while(!feof(pipe)) {
		char* pch;
		int ntiles = -1;

		if(fgets(buffer, 128, pipe) != NULL){
			pch = strtok_r(buffer, " \n", &saveptr);
			while (pch != NULL){
				if (ntiles == -1) {
					if (strcmp(linksmetrics_dir[i],
						   pch) != 0){
						ovis_log(cray_gemini_log, OVIS_LERROR,"rca-helper: err %d <%s>\n",
						       i, pch);
						rc = EINVAL;
						goto err;
					}
				}
				ntiles++;
				pch = strtok_r(NULL, " \n", &saveptr);
			}
		}
		if (ntiles >= 0) {
			linksmetrics_tiles_per_dir[i++] = ntiles;
		}
	}
	pclose(pipe);
	pipe = 0;

	/** NOTE: does not include the hostfacing dir */
	if (i != NUM_LINKSMETRICS_DIR) {
		ovis_log(cray_gemini_log, OVIS_LERROR,"rca-helper: err (i = %d NUM_LINKSMETRICS_DIR = %d)\n",
		       i, NUM_LINKSMETRICS_DIR);
		rc = EINVAL;
		goto err;
	}

	return 0;

err:

	if (pipe)
		pclose(pipe);

	return rc;

}


int hsn_metrics_config(int i, char* fname) {
	if (i >= HSN_METRICS_END){
		return EINVAL;
	} else if (i < 0){
		hsn_metrics_type = HSN_METRICS_DEFAULT;
	} else {
		hsn_metrics_type = i;
	}

	if (rtrfile)
		free(rtrfile);
	if (fname == NULL)
		rtrfile = NULL;
	else
		rtrfile = strdup(fname);

	//only need rtrfile for derived
	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		if (rtrfile == NULL) {
			ovis_log(cray_gemini_log, OVIS_LERROR,"%s: rtrfile needed for hsn_metrics_type %d\n",
			       __FILE__, hsn_metrics_type);
			return EINVAL;
		}
	}

	return 0;

}


int linksmetrics_setup()
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
		ovis_log(cray_gemini_log, OVIS_LERROR,"WARNING: Could not open the source file '%s'\n",
		       LINKSMETRICS_FILE);
		return EINVAL;

	}


	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){

		rc = bwhelper_maxbwperdir();
		if (rc)
			return rc;

		rc = rcahelper_tilesperdir();
		if (rc)
			return rc;

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
					calloc(NUM_LINKSMETRICS_DIR,
					       sizeof(uint64_t));
				if (!linksmetrics_base_values[i][j])
					return ENOMEM;

				if (i == 0){
					linksmetrics_base_diff[j] =
					calloc(NUM_LINKSMETRICS_DIR,
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
		ovis_log(cray_gemini_log, OVIS_LERROR,"ERR: Issue reading the source file '%s'\n",
		       LINKSMETRICS_FILE);
		rc = EINVAL;
		return rc;
	}
	if (strcmp(units,"ms") != 0){
		ovis_log(cray_gemini_log, OVIS_LERROR,"linksmetrics: wrong gpcdr interface\n");
		rc = EINVAL;
		return rc;
	}

	do {
		int dir = -1;
		s = fgets(lbuf, sizeof(lbuf), lm_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &val,
			    units);
		if (rc != 3) {
			ovis_log(cray_gemini_log, OVIS_LERROR,"ERR: Issue reading the source file '%s'\n",
			       LINKSMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}

		while((lastbase < NUM_LINKSMETRICS_BASENAME) &&
		      !strstr(metric_name, linksmetrics_basename[lastbase])){
			lastbase++;
		}
		for (i = 0; i < NUM_LINKSMETRICS_DIR; i++){
			if (strstr(metric_name, linksmetrics_dir[i])){
				dir = i;
				break;
			}
		}
		if ( (dir < 0) || (lastbase == NUM_LINKSMETRICS_BASENAME)){
			ovis_log(cray_gemini_log, OVIS_LERROR,"cray_system_sampler: linksmetric bad metric\n");
			return EINVAL;
		}
		/* metric_no in terms of the ones gpcdr can possibly have */
		metric_no = lastbase*NUM_LINKSMETRICS_DIR+dir;
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


int nicmetrics_setup()
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
		ovis_log(cray_gemini_log, OVIS_LERROR,"WARNING: Could not open the source file '%s'\n",
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
			ovis_log(cray_gemini_log, OVIS_LERROR,"ERR: Issue reading source file '%s'\n",
			       NICMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}
		if (strcmp(units,"ms") == 0){
			nicmetrics_time_multiplier = 1000;
		} else {
			ovis_log(cray_gemini_log, OVIS_LERROR,"nicmetrics: wrong gpcdr interface (time units)\n");
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
				ovis_log(cray_gemini_log, OVIS_LERROR,"ERR: Issue reading source file '%s'\n",
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


int add_metrics_linksmetrics(ldms_schema_t schema) {
	char newname[96];
	int metric_no;
	int i, j;
	int rc = 0;

	if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		metric_no = NUM_LINKSMETRICS_DIR * NUM_LINKSMETRICS_BASENAME + 1;

		linksmetrics_base_metric_table =
			calloc(metric_no, sizeof(int));
		if (!linksmetrics_base_metric_table)
			return ENOMEM;

		/* keep track of the next possible index to use */
		linksmetrics_indicies = calloc(metric_no, sizeof(int));
		if (!linksmetrics_indicies)
			return ENOMEM;
	}

	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		metric_no = NUM_LINKSMETRICS_DIR * NUM_LINKSMETRICS_DERIVEDNAME + 1;

		linksmetrics_derived_metric_table =
			calloc(metric_no, sizeof(int));
		if (!linksmetrics_derived_metric_table)
			return ENOMEM;
	}


	if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		metric_no = 0;
		for (i = 0; i < NUM_LINKSMETRICS_BASENAME; i++){
			for (j = 0; j < NUM_LINKSMETRICS_DIR; j++){
				__links_metric_name(1, i, j, newname);
				rc = ldms_schema_metric_add(schema, newname,
							    LDMS_V_U64);
				if (rc < 0){
					ovis_log(cray_gemini_log, OVIS_LERROR, "Failed to add metric <%s>\n",
					       newname);
					return ENOMEM;
				}
				linksmetrics_base_metric_table[metric_no++] = rc;
			}
		}
	}

	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		metric_no = 0;
		for (i = 0; i < NUM_LINKSMETRICS_DERIVEDNAME; i++) {
			for (j = 0; j < NUM_LINKSMETRICS_DIR; j++) {
				__links_metric_name(0, i, j, newname);
				rc = ldms_schema_metric_add(schema, newname,
							    LDMS_V_U64);
				if (rc < 0){
					ovis_log(cray_gemini_log, OVIS_LERROR, "Failed to add metric <%s>\n",
					       newname);
					return ENOMEM;
				}
				linksmetrics_derived_metric_table[metric_no++] = rc;
			}
		}
	}

	return 0;

 err:
	return rc;
}


int add_metrics_nicmetrics(ldms_schema_t schema)
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
				ovis_log(cray_gemini_log, OVIS_LERROR, "Failed to add metric <%s>\n",
				       nicmetrics_basename[i]);
				return ENOMEM;
			}
			nicmetrics_base_metric_table[i] = rc;
		}
	}


#ifdef HAVE_SPACELESS_NAMES
	char* format = "%s_%s_%s";
#else
	char* format = "%s_%s %s";
#endif

	if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
	    (hsn_metrics_type == HSN_METRICS_BOTH)){
		for (i = 0; i < NUM_NICMETRICS; i++) {
			sprintf(newname, format,
				nicmetrics_derivedprefix,
				nicmetrics_basename[i],
				nicmetrics_derivedunit);
			rc = ldms_schema_metric_add(schema, newname, LDMS_V_U64);
			if (rc < 0){
				ovis_log(cray_gemini_log, OVIS_LERROR, "Failed to add metric <%s>\n",
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

int sample_metrics_linksmetrics(ldms_set_t set)
{
	char lbuf[256];
	char metric_name[64];
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
		ovis_log(cray_gemini_log, OVIS_LERROR,"ERR: Issue reading the source file '%s'\n",
		       LINKSMETRICS_FILE);
		return EINVAL;
	}
	rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &curr_time,
		    units);
	if (rc != 3) {
		ovis_log(cray_gemini_log, OVIS_LERROR,"ERR: Issue reading the source file '%s'\n",
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
		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &v.v_u64,
			    units);
		if (rc != 3) {
			ovis_log(cray_gemini_log, OVIS_LERROR,"ERR: Issue reading the source file '%s'\n",
			       LINKSMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}

		if ((hsn_metrics_type == HSN_METRICS_COUNTER) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)){

			ldms_metric_set(set,
					linksmetrics_base_metric_table[linksmetrics_indicies[count]],
					&v);
		}

		if ((hsn_metrics_type == HSN_METRICS_DERIVED) ||
		    (hsn_metrics_type == HSN_METRICS_BOTH)){
			int ibase, idir;
			ibase = (int)(linksmetrics_indicies[count]/
				      NUM_LINKSMETRICS_DIR);
			idir = linksmetrics_indicies[count] %
				NUM_LINKSMETRICS_DIR;

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
			for (j = 0; j < NUM_LINKSMETRICS_DIR; j++) {
				v.v_u64 = __linksmetrics_derived_metric_calc(
					i, j, linksmetrics_base_diff,
					time_delta);
				ldms_metric_set(set,
						linksmetrics_derived_metric_table[metric_no++],
						&v);
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

int sample_metrics_nicmetrics(ldms_set_t set)
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
		ovis_log(cray_gemini_log, OVIS_LERROR,"ERR: Issue reading source file '%s'\n",
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
			ovis_log(cray_gemini_log, OVIS_LERROR,"ERR: Issue reading source file '%s'\n",
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
		/* time in ms (this is set in the gpcdr config) */
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

static uint64_t __linksmetrics_derived_metric_calc(int i, int j,
						   uint64_t** diff,
						   uint64_t timedelta){
	/* time in the config file is ms */
	int rc = 0;

	switch (i) {
	case LD_SAMPLE_GEMINI_LINK_BW:
		if (timedelta > 0)
			return (uint64_t)((double)(1000.0 * (double)(diff[LB_traffic][j]))/
				(double)(timedelta));
		else
			return 0;
		break;
	case LD_SAMPLE_GEMINI_LINK_USED_BW:
		if ((linksmetrics_max_link_bw[j] > 0 ) && (timedelta > 0))
			return (uint64_t) (
				((double)(100.0 * 1000000.0 * (double)(diff[LB_traffic][j]))/
				 ((double)timedelta * 1000000)) /
				linksmetrics_max_link_bw[j]);
		else
			return 0;
		break;
	case LD_SAMPLE_GEMINI_LINK_PACKETSIZE_AVE:
		if (diff[LB_packets][j] > 0)
			return (diff[LB_traffic][j]/diff[LB_packets][j]);
		else
			return 0;
		break;
	case LD_SAMPLE_GEMINI_LINK_INQ_STALL:
		/* see __gem_link_derived_metric_calc for a description of this
		 * calculation. 1.25 is already accounted for in the counter.
		 */
		if ((linksmetrics_tiles_per_dir[j] > 0) && (timedelta > 0)){
			uint64_t temp = (uint64_t)(
				(((double)diff[LB_inq_stall][j]/
				 (double)linksmetrics_tiles_per_dir[j])/
				 (double)(timedelta*1000000)) *
				100.0 * 1000000.0);
			return temp;
		} else {
			return 0;
		}
		break;
	case LD_SAMPLE_GEMINI_LINK_CREDIT_STALL:
		/* see __gem_link_derived_metric_calc for a description of this
		 * calculation. 1.25 is already accounted for in the counter.
		 */
		if ((linksmetrics_tiles_per_dir[j] > 0) && (timedelta > 0)){
			uint64_t temp = (uint64_t)(
				(((double)diff[LB_credit_stall][j]/
				 (double)linksmetrics_tiles_per_dir[j])/
				(double)(timedelta*1000000)) *
				100.0 * 1000000.0);
			return temp;
		} else {
			return 0;
		}
		break;

	default:
		return 0;
	}
}
