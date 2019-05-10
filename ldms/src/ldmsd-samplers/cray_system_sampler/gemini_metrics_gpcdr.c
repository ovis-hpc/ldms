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


/** internal calculations */
static uint64_t __linksmetrics_derived_metric_calc(cray_gemini_inst_t inst,
	int i, int j, uint64_t** diff, uint64_t time_delta);
static int __links_metric_name(int, int, int, char[]);
static int rcahelper_tilesperdir(cray_gemini_inst_t inst);
static int bwhelper_maxbwperdir(cray_gemini_inst_t inst);


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


static int bwhelper_maxbwperdir(cray_gemini_inst_t inst)
{

	/** have to get these values from the interconnect file for now.
	 * dimension will then include the host dir
	 */

	gemini_state_t junk;
	int tiles_per_dir_junk[GEMINI_NUM_LOGICAL_LINKS];
	int rc;

	if (inst->rtrfile == NULL)
		return EINVAL;

	rc = gem_link_perf_parse_interconnect_file(inst,
					   inst->rtrfile,
					   junk.tile,
					   &inst->linksmetrics_max_link_bw,
					   &tiles_per_dir_junk);

	if (rc != 0){
		INST_LOG(inst, LDMSD_LERROR,
			 "linksmetrics: Error parsing interconnect file\n");
		return rc;
	}

	return 0;

}


static int rcahelper_tilesperdir(cray_gemini_inst_t inst)
{

	FILE* pipe;
	char buffer[128];
	char *saveptr = NULL;

	int i;
	int rc;

	pipe = popen(RCAHELPER_CMD, "r");
	if (!pipe) {
		INST_LOG(inst, LDMSD_LERROR,
			 "gemini_metrics: rca-helper fail\n");
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
						INST_LOG(inst, LDMSD_LERROR,
							 "rca-helper: err %d "
							 "<%s>\n", i, pch);
						rc = EINVAL;
						goto err;
					}
				}
				ntiles++;
				pch = strtok_r(NULL, " \n", &saveptr);
			}
		}
		if (ntiles >= 0) {
			inst->linksmetrics_tiles_per_dir[i++] = ntiles;
		}
	}
	pclose(pipe);
	pipe = 0;

	/** NOTE: does not include the hostfacing dir */
	if (i != NUM_LINKSMETRICS_DIR) {
		INST_LOG(inst, LDMSD_LERROR,
			 "rca-helper: err (i = %d NUM_LINKSMETRICS_DIR = %d)\n",
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


int hsn_metrics_config(cray_gemini_inst_t inst, const char* fname)
{
	if (inst->rtrfile) {
		free(inst->rtrfile);
		inst->rtrfile = NULL;
	}
	if (fname == NULL)
		inst->rtrfile = NULL;
	else
		inst->rtrfile = strdup(fname);

	//only need rtrfile for derived
	if (__DERIVED(inst)){
		if (inst->rtrfile == NULL) {
			INST_LOG(inst, LDMSD_LERROR,
				 "%s: rtrfile needed for hsn_metrics_type %d\n",
				 __FILE__, inst->hsn_metrics_type);
			return EINVAL;
		}
	}

	return 0;
}


int linksmetrics_setup(cray_gemini_inst_t inst)
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
	inst->num_linksmetrics_exists = 0;
	inst->linksmetrics_valid = 0;

	inst->lm_f = fopen(LINKSMETRICS_FILE, "r");
	if (!inst->lm_f) {
		INST_LOG(inst, LDMSD_LERROR,
			 "WARNING: Could not open the source file '%s'\n",
			 LINKSMETRICS_FILE);
		return EINVAL;
	}


	if (__DERIVED(inst)) {

		rc = bwhelper_maxbwperdir(inst);
		if (rc)
			return rc;

		rc = rcahelper_tilesperdir(inst);
		if (rc)
			return rc;

		/** storage for metrics for computations in terms of the ones
		 * gpcdr can possibly have */

		inst->linksmetrics_base_values = calloc(2, sizeof(uint64_t**));
		if (!inst->linksmetrics_base_values)
			return ENOMEM;

		inst->linksmetrics_base_diff = calloc(NUM_LINKSMETRICS_BASENAME,
							sizeof(uint64_t*));
		if (!inst->linksmetrics_base_diff)
			return ENOMEM;

		for (i = 0; i < 2; i++){
			inst->linksmetrics_base_values[i] =
					calloc(NUM_LINKSMETRICS_BASENAME,
					       sizeof(uint64_t*));
			if (!inst->linksmetrics_base_values[i])
				return ENOMEM;

			for (j = 0; j < NUM_LINKSMETRICS_BASENAME; j++){
				inst->linksmetrics_base_values[i][j] =
						calloc(NUM_LINKSMETRICS_DIR,
						       sizeof(uint64_t));
				if (!inst->linksmetrics_base_values[i][j])
					return ENOMEM;

				if (i == 0){
					inst->linksmetrics_base_diff[j] =
						calloc(NUM_LINKSMETRICS_DIR,
						       sizeof(uint64_t));
					if (!inst->linksmetrics_base_diff[j])
						return ENOMEM;
				}
			}
		}
		inst->linksmetrics_values_idx = 0;
	}

	/* timestamp */
	s = fgets(lbuf, sizeof(lbuf), inst->lm_f);
	if (!s)
		return EINVAL;
	rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name,
		    &inst->linksmetrics_prev_time, units);
	if (rc != 3) {
		INST_LOG(inst, LDMSD_LERROR,
			 "ERR: Issue reading the source file '%s'\n",
			 LINKSMETRICS_FILE);
		rc = EINVAL;
		return rc;
	}
	if (strcmp(units,"ms") != 0){
		INST_LOG(inst, LDMSD_LERROR,
			 "linksmetrics: wrong gpcdr interface\n");
		rc = EINVAL;
		return rc;
	}

	do {
		int dir = -1;
		s = fgets(lbuf, sizeof(lbuf), inst->lm_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &val,
			    units);
		if (rc != 3) {
			INST_LOG(inst, LDMSD_LERROR,
				 "ERR: Issue reading the source file '%s'\n",
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
			INST_LOG(inst, LDMSD_LERROR, "linksmetric bad metric\n");
			return EINVAL;
		}
		/* metric_no in terms of the ones gpcdr can possibly have */
		metric_no = lastbase*NUM_LINKSMETRICS_DIR+dir;
		inst->linksmetrics_indicies[inst->num_linksmetrics_exists++] =
								      metric_no;


		/* store the val for the first calculation */
		if (__DERIVED(inst)){
			inst->linksmetrics_base_values[
				inst->linksmetrics_values_idx][lastbase][dir] =
									    val;
		}
	} while (s);


	inst->linksmetrics_values_idx = 1;
	inst->linksmetrics_valid = 1;

	return 0;
}


int nicmetrics_setup(cray_gemini_inst_t inst)
{
	char lbuf[256];
	char metric_name[128];
	char units[32];
	char* s;
	uint64_t val;
	int metric_no = 0;
	int i, j, rc;

	rc = 0;
	inst->nicmetrics_valid = 0;

	inst->nm_f = fopen(NICMETRICS_FILE, "r");
	if (!inst->nm_f) {
		INST_LOG(inst, LDMSD_LERROR,
			 "WARNING: Could not open the source file '%s'\n",
			 NICMETRICS_FILE);
		return EINVAL;
	}


	/** storage for derived metrics */
	inst->nicmetrics_values_idx = 0;
	if (__DERIVED(inst)){
		inst->nicmetrics_base_values = calloc(2, sizeof(uint64_t*));
		if (!inst->nicmetrics_base_values)
			return ENOMEM;
		for (i = 0; i < 2; i++){
			inst->nicmetrics_base_values[i] =
				calloc(NUM_NICMETRICS, sizeof(uint64_t));
			if (!inst->nicmetrics_base_values[i])
				return ENOMEM;
		}
		/* Open the file and store the first set of values */
		fseek(inst->nm_f, 0, SEEK_SET);
		/* timestamp */
		s = fgets(lbuf, sizeof(lbuf), inst->nm_f);
		if (!s)
			return EINVAL;
		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n",
			    metric_name, &inst->nicmetrics_prev_time, units);
		if (rc != 3) {
			INST_LOG(inst, LDMSD_LERROR,
				 "ERR: Issue reading source file '%s'\n",
				 NICMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}
		if (strcmp(units, "ms") == 0){
			inst->nicmetrics_time_multiplier = 1000;
		} else {
			INST_LOG(inst, LDMSD_LERROR,
				 "nicmetrics: wrong gpcdr interface "
				 "(time units)\n");
			rc = EINVAL;
			return rc;
		}
		metric_no = 0;
		do {
			s = fgets(lbuf, sizeof(lbuf), inst->nm_f);
			if (!s)
				break;
			rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name,
				    &val, units);
			if (rc != 3) {
				INST_LOG(inst, LDMSD_LERROR,
					 "ERR: Issue reading source file "
					 "'%s'\n", NICMETRICS_FILE);
				rc = EINVAL;
				return rc;
			}
			inst->nicmetrics_base_values[
				inst->nicmetrics_values_idx][metric_no++] = val;
		} while (s);

		inst->nicmetrics_values_idx = 1;
	}

	inst->nicmetrics_valid = 1;

	return 0;
}


int add_metrics_linksmetrics(cray_gemini_inst_t inst, ldms_schema_t schema)
{
	char newname[96];
	int metric_no;
	int i, j;
	int rc = 0;

	if (__COUNTER(inst)){
		metric_no = NUM_LINKSMETRICS_DIR * NUM_LINKSMETRICS_BASENAME + 1;

		inst->linksmetrics_base_metric_table =
						calloc(metric_no, sizeof(int));
		if (!inst->linksmetrics_base_metric_table)
			return ENOMEM;

		/* keep track of the next possible index to use */
		inst->linksmetrics_indicies = calloc(metric_no, sizeof(int));
		if (!inst->linksmetrics_indicies)
			return ENOMEM;
		metric_no = 0;
		for (i = 0; i < NUM_LINKSMETRICS_BASENAME; i++){
			for (j = 0; j < NUM_LINKSMETRICS_DIR; j++){
				__links_metric_name(1, i, j, newname);
				rc = ldms_schema_metric_add(schema, newname,
							    LDMS_V_U64, "");
				if (rc < 0){
					INST_LOG(inst, LDMSD_LERROR,
						 "Failed to add metric <%s>\n",
						 newname);
					return ENOMEM;
				}
				inst->linksmetrics_base_metric_table[metric_no++] = rc;
			}
		}
	}

	if (__DERIVED(inst)){
		metric_no = NUM_LINKSMETRICS_DIR * NUM_LINKSMETRICS_DERIVEDNAME + 1;

		inst->linksmetrics_derived_metric_table =
						calloc(metric_no, sizeof(int));
		if (!inst->linksmetrics_derived_metric_table)
			return ENOMEM;
		metric_no = 0;
		for (i = 0; i < NUM_LINKSMETRICS_DERIVEDNAME; i++) {
			for (j = 0; j < NUM_LINKSMETRICS_DIR; j++) {
				__links_metric_name(0, i, j, newname);
				rc = ldms_schema_metric_add(schema, newname,
							    LDMS_V_U64, "");
				if (rc < 0){
					INST_LOG(inst, LDMSD_LERROR,
						 "Failed to add metric <%s>\n",
						 newname);
					return ENOMEM;
				}
				inst->linksmetrics_derived_metric_table[metric_no++] = rc;
			}
		}
	}

	return 0;
}


int add_metrics_nicmetrics(cray_gemini_inst_t inst, ldms_schema_t schema)
{
	char newname[96];
	int i, j;
	int rc;

	if (__COUNTER(inst)){
		inst->nicmetrics_base_metric_table =
					calloc(NUM_NICMETRICS, sizeof(int));
		if (!inst->nicmetrics_base_metric_table)
			return ENOMEM;
		for (i = 0; i < NUM_NICMETRICS; i++){
			rc = ldms_schema_metric_add(schema,
						    nicmetrics_basename[i],
						    LDMS_V_U64, "");
			if (rc < 0){
				INST_LOG(inst, LDMSD_LERROR,
					 "Failed to add metric <%s>\n",
					 nicmetrics_basename[i]);
				return ENOMEM;
			}
			inst->nicmetrics_base_metric_table[i] = rc;
		}
	}

#ifdef HAVE_SPACELESS_NAMES
	char* format = "%s_%s_%s";
#else
	char* format = "%s_%s %s";
#endif
	if (__DERIVED(inst)){
		inst->nicmetrics_derived_metric_table =
					calloc(NUM_NICMETRICS, sizeof(int));
		if (!inst->nicmetrics_derived_metric_table)
			return ENOMEM;
		for (i = 0; i < NUM_NICMETRICS; i++) {
			sprintf(newname, format,
				nicmetrics_derivedprefix,
				nicmetrics_basename[i],
				nicmetrics_derivedunit);
			rc = ldms_schema_metric_add(schema, newname,
						    LDMS_V_U64, "");
			if (rc < 0){
				INST_LOG(inst, LDMSD_LERROR,
					 "Failed to add metric <%s>\n",
					 newname);
				return ENOMEM;
			}
			inst->nicmetrics_derived_metric_table[i] = rc;
		}
	}

	return 0;
}

int sample_metrics_linksmetrics(cray_gemini_inst_t inst, ldms_set_t set)
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

	if (!inst->lm_f || !inst->linksmetrics_valid)
		return 0;


	fseek(inst->lm_f, 0, SEEK_SET);

	/* read the timestamp */
	s = fgets(lbuf, sizeof(lbuf), inst->lm_f);
	if (!s) {
		INST_LOG(inst, LDMSD_LERROR,
			 "ERR: Issue reading the source file '%s'\n",
			 LINKSMETRICS_FILE);
		return EINVAL;
	}
	rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &curr_time,
		    units);
	if (rc != 3) {
		INST_LOG(inst, LDMSD_LERROR,
			 "ERR: Issue reading the source file '%s'\n",
			 LINKSMETRICS_FILE);
		rc = EINVAL;
		return rc;
	}

	count = 0;
	idx = inst->linksmetrics_values_idx;
	time_delta = curr_time - inst->linksmetrics_prev_time;
	do {
		s = fgets(lbuf, sizeof(lbuf), inst->lm_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &v.v_u64,
			    units);
		if (rc != 3) {
			INST_LOG(inst, LDMSD_LERROR,
				 "ERR: Issue reading the source file '%s'\n",
				 LINKSMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}

		if (__COUNTER(inst)){
			ldms_metric_set(set,
				inst->linksmetrics_base_metric_table[
					inst->linksmetrics_indicies[count]],
				&v);
		}

		if (__DERIVED(inst)){
			int ibase, idir;
			ibase = (int)(inst->linksmetrics_indicies[count]/
				      NUM_LINKSMETRICS_DIR);
			idir = inst->linksmetrics_indicies[count] %
				NUM_LINKSMETRICS_DIR;

			inst->linksmetrics_base_values[idx][ibase][idir] = v.v_u64;

			if (inst->linksmetrics_base_values[idx][ibase][idir] <
			    inst->linksmetrics_base_values[!idx][ibase][idir]) {
				/* the gpcdr values are 64 bit */
				inst->linksmetrics_base_diff[ibase][idir] =
					(ULONG_MAX - inst->linksmetrics_base_values[!idx][ibase][idir]) +
					inst->linksmetrics_base_values[idx][ibase][idir];
			} else {
				inst->linksmetrics_base_diff[ibase][idir] =
					inst->linksmetrics_base_values[idx][ibase][idir] -
					inst->linksmetrics_base_values[!idx][ibase][idir];
			}
		}
		count++;

	} while (s);

	if (__DERIVED(inst)){
		metric_no = 0;
		for (i = 0; i < NUM_LINKSMETRICS_DERIVEDNAME; i++) {
			for (j = 0; j < NUM_LINKSMETRICS_DIR; j++) {
				v.v_u64 = __linksmetrics_derived_metric_calc(
					inst, i, j,
					inst->linksmetrics_base_diff,
					time_delta);
				ldms_metric_set(set,
						inst->linksmetrics_derived_metric_table[metric_no++],
						&v);
			}
		}
	}

	if (count != inst->num_linksmetrics_exists) {
		inst->linksmetrics_valid = 0;
		return EINVAL;
	}

	inst->linksmetrics_values_idx = !inst->linksmetrics_values_idx;
	inst->linksmetrics_prev_time = curr_time;

	return 0;
}


int sample_metrics_nicmetrics(cray_gemini_inst_t inst, ldms_set_t set)
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

	if (!inst->nm_f || !inst->nicmetrics_valid)
		return 0;

	fseek(inst->nm_f, 0, SEEK_SET);
	/* timestamp */
	s = fgets(lbuf, sizeof(lbuf), inst->nm_f);
	if (!s) {
		rc = EINVAL;
		return rc;
	}

	rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &curr_time,
		    units);
	if (rc != 3) {
		INST_LOG(inst, LDMSD_LERROR,
			 "ERR: Issue reading source file '%s'\n",
			 NICMETRICS_FILE);
		rc = EINVAL;
		return rc;
	}

	time_delta = curr_time - inst->nicmetrics_prev_time; /* units see below */
	idx = inst->nicmetrics_values_idx;
	i = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), inst->nm_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &v.v_u64,
			    units);
		if (rc != 3) {
			INST_LOG(inst, LDMSD_LERROR,
				 "ERR: Issue reading source file '%s'\n",
				 NICMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}

		if (__COUNTER(inst)){
			ldms_metric_set(set, inst->nicmetrics_base_metric_table[i], &v);
		}
		if (__DERIVED(inst)){
			inst->nicmetrics_base_values[idx][i] = v.v_u64;
		}
		i++;
	} while (s);

	if (__DERIVED(inst)){
		/* time in ms (this is set in the gpcdr config) */
		for (i = 0; i < NUM_NICMETRICS; i++){
			uint64_t diff;

			/* the gpcdr values are 64 bit */
			if (inst->nicmetrics_base_values[idx][i] <
			    inst->nicmetrics_base_values[!idx][i] ) {
				diff = (ULONG_MAX -
					inst->nicmetrics_base_values[!idx][i]) +
					inst->nicmetrics_base_values[idx][i];
			} else {
				diff =  inst->nicmetrics_base_values[idx][i] -
					inst->nicmetrics_base_values[!idx][i];
			}

			if (time_delta > 0)
				v.v_u64 = (uint64_t)((diff*inst->nicmetrics_time_multiplier)/time_delta);
			else
				v.v_u64 = 0;

			ldms_metric_set(set, inst->nicmetrics_derived_metric_table[i], &v);
		}
	}

	inst->nicmetrics_values_idx = !inst->nicmetrics_values_idx;
	inst->nicmetrics_prev_time = curr_time;
	return 0;
}

static uint64_t __linksmetrics_derived_metric_calc(cray_gemini_inst_t inst,
						   int i, int j,
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
		if ((inst->linksmetrics_max_link_bw[j] > 0 ) && (timedelta > 0))
			return (uint64_t) (
				((double)(100.0 * 1000000.0 * (double)(diff[LB_traffic][j]))/
				 ((double)timedelta * 1000000)) /
				inst->linksmetrics_max_link_bw[j]);
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
		if ((inst->linksmetrics_tiles_per_dir[j] > 0) && (timedelta > 0)){
			uint64_t temp = (uint64_t)(
				(((double)diff[LB_inq_stall][j]/
				 (double)inst->linksmetrics_tiles_per_dir[j])/
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
		if ((inst->linksmetrics_tiles_per_dir[j] > 0) && (timedelta > 0)){
			uint64_t temp = (uint64_t)(
				(((double)diff[LB_credit_stall][j]/
				 (double)inst->linksmetrics_tiles_per_dir[j])/
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
