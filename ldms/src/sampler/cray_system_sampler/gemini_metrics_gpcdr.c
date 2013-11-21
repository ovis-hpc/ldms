/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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

#include "gemini_metrics_gpcdr.h"
#include "gemini_metrics.h"
/* have to include this now to get the max bw */
#include "rtr_util.h"

/** internal calculations */


static uint64_t __linksmetrics_derived_metric_calc(
	int i, int j, uint64_t** diff, uint64_t time_delta);
static int __links_metric_name(int, int, int, char[]);
static int rcahelper_tilesperdir(ldmsd_msg_log_f msglog);
static int bwhelper_maxbwperdir(ldmsd_msg_log_f msglog);


static int __links_metric_name(int isbase, int nameidx,
				   int diridx, char newname[]){

	if (isbase == 1)
		sprintf(newname, "%s_%s %s",
			linksmetrics_dir[diridx],
			linksmetrics_basename[nameidx],
			linksmetrics_baseunit[nameidx]);
	else
		sprintf(newname, "%s_%s %s",
			linksmetrics_dir[diridx],
			linksmetrics_derivedname[nameidx],
			linksmetrics_derivedunit[nameidx]);

	return 0;
}


static int bwhelper_maxbwperdir(ldmsd_msg_log_f msglog)
{

	/** have to get these values from the interconnect file for now.
	 * dimension will then include the host dir
	 */

	gemini_state_t junk;
	int tiles_per_dir_junk[GEMINI_NUM_LOGICAL_LINKS];
	int rc;

	if (rtrfile == NULL)
		return EINVAL;

	rc = gem_link_perf_parse_interconnect_file(&msglog,
						   rtrfile,
						   junk.tile,
						   &linksmetrics_max_link_bw,
						   &tiles_per_dir_junk);

	if (rc != 0){
		msglog("linksmetrics: Error parsing interconnect file\n");
		return rc;
	}

	return 0;

}


int rcahelper_tilesperdir(ldmsd_msg_log_f msglog)
{

	FILE* pipe;
	char buffer[128];

	int i;
	int rc;

	pipe = popen(RCAHELPER_CMD, "r");
	if (!pipe) {
		msglog("gemini_metrics: rca-helper fail\n");
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
			pch = strtok (buffer, " \n");
			while (pch != NULL){
				if (ntiles == -1) {
					if (strcmp(linksmetrics_dir[i],
						   pch) != 0){
						msglog("rca-helper: err %d <%s>\n",
						       i, pch);
						rc = EINVAL;
						goto err;
					}
				}
				ntiles++;
				pch = strtok(NULL, " \n");
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
		msglog("rca-helper: err (i = %d NUM_LINKSMETRICS_DIR = %d)\n",
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


int get_metric_size_linksmetrics(size_t *m_sz, size_t *d_sz,
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
	if ((gemini_metrics_type == GEMINI_METRICS_COUNTER) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		for (i = 0; i < NUM_LINKSMETRICS_BASENAME; i++){
			for (j = 0; j < NUM_LINKSMETRICS_DIR; j++){
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
	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		for (i = 0; i < NUM_LINKSMETRICS_DERIVEDNAME; i++) {
			for (j = 0; j < NUM_LINKSMETRICS_DIR; j++) {
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

	if ((gemini_metrics_type == GEMINI_METRICS_COUNTER) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
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

	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
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


int linksmetrics_setup(ldmsd_msg_log_f msglog)
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
		msglog("WARNING: Could not open the source file '%s'\n",
		       LINKSMETRICS_FILE);
		return EINVAL;

	}

	rc = bwhelper_maxbwperdir(msglog);
	if (rc)
		return rc;

	rc = rcahelper_tilesperdir(msglog);
	if (rc)
		return rc;

	/** storage for metrics for computations in terms of the ones
	 * gpcdr can possibly have */
	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){

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
		msglog("ERR: Issue reading the source file '%s'\n",
		       LINKSMETRICS_FILE);
		rc = EINVAL;
		return rc;
	}
	if (strcmp(units,"ms") != 0){
		msglog("linksmetrics: wrong gpcdr interface\n");
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
			msglog("ERR: Issue reading the source file '%s'\n",
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
			msglog("cray_system_sampler: linksmetric bad metric\n");
			return EINVAL;
		}
		/* metric_no in terms of the ones gpcdr can possibly have */
		metric_no = lastbase*NUM_LINKSMETRICS_DIR+dir;
		linksmetrics_indicies[num_linksmetrics_exists++] =  metric_no;


		/* store the val for the first calculation */
		if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
		    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
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
		msglog("WARNING: Could not open the source file '%s'\n",
		       NICMETRICS_FILE);
		return EINVAL;
	}


	/** storage for derived metrics */
	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
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

	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		/* Open the file and store the first set of values */
		fseek(nm_f, 0, SEEK_SET);
		/* timestamp */
		s = fgets(lbuf, sizeof(lbuf), nm_f);
		if (!s)
			return EINVAL;
		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n",
			    metric_name, &nicmetrics_prev_time, units);
		if (rc != 3) {
			msglog("ERR: Issue reading source file '%s'\n",
			       NICMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}
		if (strcmp(units,"ms") != 0){
			msglog("nicmetrics: wrong gpcdr interface\n");
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
				msglog("ERR: Issue reading source file '%s'\n",
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


int add_metrics_linksmetrics(ldms_set_t set, int comp_id,
			     ldmsd_msg_log_f msglog)
{
	char newname[96];
	int metric_no;
	int i, j;
	int rc = 0;


	if ((gemini_metrics_type == GEMINI_METRICS_COUNTER) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		metric_no = 0;
		for (i = 0; i < NUM_LINKSMETRICS_BASENAME; i++){
			for (j = 0; j < NUM_LINKSMETRICS_DIR; j++){
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

	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		metric_no = 0;
		for (i = 0; i < NUM_LINKSMETRICS_DERIVEDNAME; i++) {
			for (j = 0; j < NUM_LINKSMETRICS_DIR; j++) {
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


	if ((gemini_metrics_type == GEMINI_METRICS_COUNTER) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
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

	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
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

int sample_metrics_linksmetrics(ldmsd_msg_log_f msglog)
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
		msglog("ERR: Issue reading the source file '%s'\n",
		       LINKSMETRICS_FILE);
		return EINVAL;
	}
	rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name, &curr_time,
		    units);
	if (rc != 3) {
		msglog("ERR: Issue reading the source file '%s'\n",
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
			msglog("ERR: Issue reading the source file '%s'\n",
			       LINKSMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}

		if ((gemini_metrics_type == GEMINI_METRICS_COUNTER) ||
		    (gemini_metrics_type == GEMINI_METRICS_BOTH)){

			ldms_set_metric(
				linksmetrics_base_metric_table[
					linksmetrics_indicies[count]], &v);
		}

		if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
		    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
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

	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		metric_no = 0;
		for (i = 0; i < NUM_LINKSMETRICS_DERIVEDNAME; i++) {
			for (j = 0; j < NUM_LINKSMETRICS_DIR; j++) {
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
		msglog("ERR: Issue reading source file '%s'\n",
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
			msglog("ERR: Issue reading source file '%s'\n",
			       NICMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}

		if ((gemini_metrics_type == GEMINI_METRICS_COUNTER) ||
		    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
			ldms_set_metric(nicmetrics_base_metric_table[i], &v);
		}
		if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
		    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
			nicmetrics_base_values[idx][i] = v.v_u64;
		}
		i++;
	} while (s);

	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
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
				v.v_u64 = (uint64_t)((diff*1000)/time_delta);
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
	/* time in the config file is ms */
	int rc = 0;

	switch (i) {
	case LD_SAMPLE_GEMINI_LINK_BW:
		if (timedelta > 0)
			return (1000 * diff[LB_traffic][j])/
				timedelta;
		else
			return 0;
		break;
	case LD_SAMPLE_GEMINI_LINK_USED_BW:
		if ((linksmetrics_max_link_bw[j] > 0 ) && (timedelta > 0))
			return (uint64_t) (
				((double)(100 * 1000000 * diff[LB_traffic][j])/
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
