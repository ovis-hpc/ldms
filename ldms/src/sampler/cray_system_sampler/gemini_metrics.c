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
 * \file gemini_metrics.c
 * \brief Functions used in the cray_system_sampler that are particular to
 * gemini -- gpcd and gpcdr as well as related rca (mesh coord).
 */


/**
 * Sub sampler notes:
 *
 * gem_link_perf and linksmetrics are alternate interfaces to approximately
 * the same data. similarly true for nic_perf and nicmetrics.
 * Use depends on whether or not your system has the the gpcdr module.
 *
 * gem_link_perf:
 * Link aggregation methodlogy from gpcd counters based on Kevin Pedretti's
 * (Sandia Naional Laboratories) gemini performance counter interface and
 * link aggregation library. It has been augmented with pattern analysis
 * of the interconnect file.
 *
 * linksmetrics:
 * uses gpcdr interface
 *
 * nic_perf:
 * raw counter read, performing the same sum defined in the gpcdr design
 * document.
 *
 * nicmetrics:
 * uses gpcdr interface
 */

#include <rca_lib.h>
#include <rs_id.h>
#include <rs_meshcoord.h>
#include "gem_link_perf_util.h"
#include "gemini_metrics.h"

/** internal calculations */

static uint64_t __gem_link_aggregate_phits(
	int i, uint64_t sample_link_ctrs[][GEMINI_NUM_TILE_COUNTERS]);
static uint64_t __gem_link_aggregate_packets(
	int i, uint64_t sample_link_ctrs[][GEMINI_NUM_TILE_COUNTERS]);
static uint64_t __linksmetrics_derived_metric_calc(
	int i, int j, uint64_t** diff, uint64_t time_delta);
static uint64_t __gem_link_derived_metric_calc(
	int i, int j, uint64_t sample_link_ctrs[][GEMINI_NUM_TILE_COUNTERS],
	uint64_t time_delta);
static uint64_t __gem_link_base_metric_calc(
	int i, int j, uint64_t module_ctrs[][GEMINI_NUM_TILE_COUNTERS]);
static uint64_t __nic_perf_metric_calc(int i, uint64_t vals[]);
static int __links_metric_name(int, int, int, int, char[]);


static int __links_metric_name(int islinksmetrics, int isbase, int nameidx,
				   int diridx, char newname[]){

	if (islinksmetrics){
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
	} else {
		if (isbase == 1)
			sprintf(newname, "%s_%s %s",
				gemini_linkdir_name[diridx],
				ns_glp_basename[nameidx],
				ns_glp_baseunit[nameidx]);
		else
			sprintf(newname, "%s_%s %s",
				gemini_linkdir_name[diridx],
				ns_glp_derivedname[nameidx],
				ns_glp_derivedunit[nameidx]);
	}

	return 0;
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
				__links_metric_name(1, 1, i, j, newname);
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
				__links_metric_name(1, 0, i, j, newname);
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

int get_metric_size_nic_perf(size_t *m_sz, size_t *d_sz, ldmsd_msg_log_f msglog)
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
			sprintf(newname, "C_%s", nicmetrics_basename[i]);
			rc = ldms_get_metric_size(newname, LDMS_V_U64,
						  &meta_sz, &data_sz);
			if (rc)
				return rc;
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
		}

		ns_nic_base_metric_table =
			calloc(NUM_NICMETRICS, sizeof(ldms_metric_t));
		if (!ns_nic_base_metric_table)
			return ENOMEM;

		/* allocate accumulator memory here so metric_count aligns */
		ns_nic_base_acc =
			calloc(NUM_NICMETRICS, sizeof(uint64_t));
		if (!ns_nic_base_acc)
			return ENOMEM;
	}

	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		for (i = 0; i < NUM_NICMETRICS; i++) {
			sprintf(newname, "C_%s_%s %s",
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

		ns_nic_derived_metric_table =
			calloc(NUM_NICMETRICS, sizeof(ldms_metric_t));
		if (!ns_nic_derived_metric_table)
			return ENOMEM;

	}

	*m_sz = tot_meta_sz;
	*d_sz = tot_data_sz;

	return 0;

}

int get_metric_size_gem_link_perf(size_t *m_sz, size_t *d_sz,
				  ldmsd_msg_log_f msglog)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	char newname[96];
	int metric_count;
	int useme;
	int i, j;
	int rc;

	tot_meta_sz = 0;
	tot_data_sz = 0;

	if ((gemini_metrics_type == GEMINI_METRICS_COUNTER) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		metric_count = 0;
		for (i = 0; i < GEMINI_NUM_LOGICAL_LINKS; i++) {
			for (j = 0; j < NUM_NS_GLP_BASENAME; j++){
				__links_metric_name(0, 1, j, i, newname);
				rc = ldms_get_metric_size(newname,
							  LDMS_V_U64,
							  &meta_sz,
							  &data_sz);
				if (rc)
					return rc;
				tot_meta_sz += meta_sz;
				tot_data_sz += data_sz;
				metric_count++;
			}
		}

		ns_glp_base_metric_table =
			calloc(metric_count, sizeof(ldms_metric_t));
		if (!ns_glp_base_metric_table)
			return ENOMEM;

		/* allocate accumulator memory here so metric_count aligns */
		ns_glp_base_acc =
			calloc(metric_count, sizeof(uint64_t));
		if (!ns_glp_base_acc)
			return ENOMEM;
	}


	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		metric_count = 0;
		for (i = 0; i < GEMINI_NUM_LOGICAL_LINKS; i++) {
			for (j = 0; j < NUM_NS_GLP_DERIVEDNAME; j++) {
				//well known case
				if ((i == GEMINI_LINK_DIR_HOST) &&
				    (j == GD_SAMPLE_GEMINI_LINK_USED_BW))
					continue;
				__links_metric_name(0, 0, j, i, newname);
				rc = ldms_get_metric_size(newname,
							  LDMS_V_U64,
							  &meta_sz,
							  &data_sz);
				tot_meta_sz += meta_sz;
				tot_data_sz += data_sz;
				metric_count++;
			}
		}

		ns_glp_derived_metric_table =
			calloc(metric_count, sizeof(ldms_metric_t));
		if (!ns_glp_derived_metric_table)
			return ENOMEM;
	}

	*m_sz = tot_meta_sz;
	*d_sz = tot_data_sz;

	return 0;
}


int nettopo_setup(ldmsd_msg_log_f msglog)
{
	rs_node_t node;
	mesh_coord_t loc;
	uint16_t nid;

	rca_get_nodeid(&node);
	nid = (uint16_t)node.rs_node_s._node_id;
	rca_get_meshcoord(nid, &loc);
	nettopo_coord.x = loc.mesh_x;
	nettopo_coord.y = loc.mesh_y;
	nettopo_coord.z = loc.mesh_z;

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

		while(!strstr(metric_name, linksmetrics_basename[lastbase]) &&
		      (lastbase < NUM_LINKSMETRICS_BASENAME)){
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


int nic_perf_setup(ldmsd_msg_log_f msglog)
{

	int error;

	ns_nic_curr_ctx = nic_perf_create_context(&msglog);
	if (!ns_nic_curr_ctx) {
		msglog("ns_nic: gpcd_create_context failed");
		ns_nic_valid = 0;
		return EINVAL;
	}


	ns_nic_prev_ctx = nic_perf_create_context(&msglog);
	if (!ns_nic_prev_ctx) {
		msglog("ns_nic: gpcd_create_context failed");
		ns_nic_valid = 0;
		return EINVAL;
	}


	ns_nic_prev_time = &ns_nic_time1;
	ns_nic_curr_time = &ns_nic_time2;

	clock_gettime(CLOCK_REALTIME, ns_nic_prev_time);
	error = gpcd_context_read_mmr_vals(ns_nic_prev_ctx);

	if (error) {
		msglog("nic_perf: Error in gpcd_context_read_mmr_vals\n");
		ns_nic_valid = 0;
		return EINVAL;
	}

	ns_nic_plistp = ns_nic_prev_ctx->list;
	ns_nic_valid = 1;

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
				__links_metric_name(1, 1, i, j, newname);
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
				__links_metric_name(1, 0, i, j, newname);
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


int add_metrics_nic_perf(ldms_set_t set, int comp_id, ldmsd_msg_log_f msglog)
{
	char newname[96];
	int i, j;
	int rc = 0;


	if ((gemini_metrics_type == GEMINI_METRICS_COUNTER) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		for (i = 0; i < NUM_NICMETRICS; i++){
			sprintf(newname, "C_%s", nicmetrics_basename[i]);
			ns_nic_base_metric_table[i] =
				ldms_add_metric(set, newname, LDMS_V_U64);
			if (!ns_nic_base_metric_table[i]) {
				msglog("Bad add_metric %d\n",i);
				rc = ENOMEM;
				return rc;
			}
			/* XXX comp_id */
			ldms_set_user_data(ns_nic_base_metric_table[i],
					   comp_id);
		}
	}

	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		for (i = 0; i < NUM_NICMETRICS; i++) {
			sprintf(newname, "C_%s_%s %s",
				nicmetrics_derivedprefix,
				nicmetrics_basename[i],
				nicmetrics_derivedunit);
			ns_nic_derived_metric_table[i] =
				ldms_add_metric(set, newname, LDMS_V_U64);
			if (!ns_nic_derived_metric_table[i]) {
				msglog("Bad add_metric %d\n", i);
				rc = ENOMEM;
				return rc;
			}
			/* XXX comp_id */
			ldms_set_user_data(ns_nic_derived_metric_table[i],
					   comp_id);
		}
	}

 err:
	return rc;
}


int add_metrics_gem_link_perf(ldms_set_t set, int comp_id,
			      ldmsd_msg_log_f msglog)
{

	char newname[96];
	int metric_no;
	int i, j;
	int rc = 0;

	if ((gemini_metrics_type == GEMINI_METRICS_COUNTER) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		metric_no = 0;
		for (i = 0; i < GEMINI_NUM_LOGICAL_LINKS; i++) {
			for (j = 0; j < NUM_NS_GLP_BASENAME; j++){
				__links_metric_name(0, 1, j, i, newname);
				ns_glp_base_metric_table[metric_no] =
					ldms_add_metric(set, newname,
							LDMS_V_U64);
				if (!ns_glp_base_metric_table[metric_no])
					return ENOMEM;

				/* XXX comp_id */
				ldms_set_user_data(
					ns_glp_base_metric_table[metric_no++],
					comp_id);
			}
		}
	}

	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		metric_no = 0;
		for (i = 0; i < GEMINI_NUM_LOGICAL_LINKS; i++) {
			for (j = 0; j < NUM_NS_GLP_DERIVEDNAME; j++) {
				/* well known case */
				if ((i == GEMINI_LINK_DIR_HOST) &&
				    (j == GD_SAMPLE_GEMINI_LINK_USED_BW))
					continue;
				__links_metric_name(0, 0, j, i, newname);
				ns_glp_derived_metric_table[metric_no] =
					ldms_add_metric(set, newname,
							LDMS_V_U64);
				if (!ns_glp_derived_metric_table[metric_no])
					return ENOMEM;

				/* XXX comp_id */
				ldms_set_user_data(
					ns_glp_derived_metric_table[
						metric_no++],
					comp_id);
			}
		}
	}

 err:
	return rc;
}

int gem_link_perf_setup(ldmsd_msg_log_f msglog)
{

	int error;
	int rc;
	int metric_count;
	int i, j;

	if (ns_glp_rtrfile == NULL){
		ns_glp_valid = 0;
		return EINVAL;
	}

	ns_glp_curr_ctx = gem_link_perf_create_context(&msglog);
	if (!ns_glp_curr_ctx) {
		msglog("ns_glp: gpcd_create_context failed");
		ns_glp_valid = 0;
		return EINVAL;
	}

	ns_glp_prev_ctx = gem_link_perf_create_context(&msglog);
	if (!ns_glp_prev_ctx) {
		msglog("ns_nic: gpcd_create_context failed");
		ns_glp_valid = 0;
		return EINVAL;
	}

	/*  Allocate memory for struct state */
	ns_glp_state = malloc(sizeof(gemini_state_t));
	if (!ns_glp_state) {
		msglog("ns_glp: Error allocating memory for state\n");
		ns_glp_valid = 0;
		return ENOMEM;
	}


	/*  Figure out our tile info */
	rc = gem_link_perf_parse_interconnect_file(&msglog,
						   ns_glp_rtrfile,
						   ns_glp_state->tile,
						   &ns_glp_max_link_bw,
						   &ns_glp_tiles_per_dir);
	if (rc != 0){
		msglog("ns_glp: Error parsing interconnect file\n");
		ns_glp_valid = 0;
		return rc;
	}

	/*  Fill in rc_to_tid array */
	for (i=0; i<GEMINI_NUM_TILE_ROWS; i++) {
		for (j=0; j<GEMINI_NUM_TILE_COLUMNS; j++) {
			error = tcoord_to_tid(i, j,
					      &(ns_glp_rc_to_tid[i][j]));
			if (error) {
				msglog("ns_glp: Error converting r,c to"
				       " tid\n");
				ns_glp_valid = 0;
				return error;
			}
		}
	}

	ns_glp_prev_time = &ns_glp_time1;
	ns_glp_curr_time = &ns_glp_time2;

	clock_gettime(CLOCK_REALTIME, ns_glp_prev_time);
	error = gpcd_context_read_mmr_vals(ns_glp_prev_ctx);

	if (error) {
		msglog("ns_glp: Error in gpcd_context_read_mmr_vals\n");
		ns_glp_valid = 0;
		return EINVAL;
	}

	ns_glp_plistp = ns_glp_prev_ctx->list;
	ns_glp_valid = 1;

	return 0;
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

	time_delta = curr_time - nicmetrics_prev_time;	/* time in sec */
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
				v.v_u64 = (uint64_t)(diff/time_delta);
			else
				v.v_u64 = 0;

			ldms_set_metric(nicmetrics_derived_metric_table[i], &v);
		}
	}

	nicmetrics_values_idx = (nicmetrics_values_idx == 0? 1: 0);
	nicmetrics_prev_time = curr_time;
	return 0;

}


static uint64_t __nic_perf_metric_calc(int metric, uint64_t vals[])
{
	/* vals already account for individual rollover. */

	switch (metric){
	case M_totaloutput_optA:
		return (vals[R_GM_ORB_PERF_VC0_FLITS] +
			vals[R_GM_NPT_PERF_ACP_FLIT_CNTR] +
			vals[R_GM_NPT_PERF_NRP_FLIT_CNTR] +
			vals[R_GM_NPT_PERF_NPT_FLIT_CNTR] -
			vals[R_GM_ORB_PERF_VC0_PKTS] -
			vals[R_GM_NPT_PERF_NL_RSP_PKT_CNTR]) * 16;
		break;
	case M_totalinput:
		return (vals[R_GM_RAT_PERF_DATA_FLITS_VC0] +
			vals[R_GM_ORB_PERF_VC1_FLITS] -
			vals[R_GM_ORB_PERF_VC1_PKTS]) * 16;
		break;
	case M_fmaout:
		return (vals[R_GM_TARB_PERF_FMA_FLITS] -
			vals[R_GM_TARB_PERF_FMA_PKTS]) * 16;
		break;
	case M_bteout_optA:
		return (vals[R_GM_TARB_PERF_BTE_FLITS] -
			vals[R_GM_TARB_PERF_BTE_PKTS]) * 16;
	case M_bteout_optB:
		return (uint64_t)(
			(64.0/3.0) *
			(vals[R_GM_TARB_PERF_BTE_FLITS] -
			 2 * vals[R_GM_TARB_PERF_BTE_PKTS]));
		break;
	case M_totaloutput_optB: {
		uint64_t fmaout = (vals[R_GM_TARB_PERF_FMA_FLITS] -
				   vals[R_GM_TARB_PERF_FMA_PKTS]) * 16;
		uint64_t bteout_optB = (uint64_t)(
			(64.0/3.0) *
			(vals[R_GM_TARB_PERF_BTE_FLITS] -
			 2 * vals[R_GM_TARB_PERF_BTE_PKTS]));
		uint64_t outputresp =
			(vals[R_GM_NPT_PERF_ACP_FLIT_CNTR] +
			 vals[R_GM_NPT_PERF_NRP_FLIT_CNTR] +
			 vals[R_GM_NPT_PERF_NPT_FLIT_CNTR] -
			 vals[R_GM_NPT_PERF_NL_RSP_PKT_CNTR]) * 16;

		return (fmaout + bteout_optB + outputresp);
	}
	default:
		return 0;
	}
}

int sample_metrics_nic_perf(ldmsd_msg_log_f msglog)
{

	int metric_no;
	int done = 0;
	union ldms_value v;
	union ldms_value vd;
	uint64_t prevval;
	uint64_t time_delta;
	int error;
	int rc;
	int i;

	if (ns_nic_valid == 0)
		return 0;

	clock_gettime(CLOCK_REALTIME, ns_nic_curr_time);

	error = gpcd_context_read_mmr_vals(ns_nic_curr_ctx);
	if (error) {
		msglog("nic_perf: Error reading mmr_vals\n");
		rc = EINVAL;
		return rc;
	} else {
		ns_nic_listp = ns_nic_curr_ctx->list;
		ns_nic_plistp = ns_nic_prev_ctx->list;
	}

	metric_no = NUM_NIC_PERF_RAW-1;
	do {
		if ( ns_nic_listp->value < ns_nic_plistp->value )
			/* counters are really 48 bit put in a 64 */
			ns_nic_diff[metric_no] =
				(COUNTER_48BIT_MAX - ns_nic_plistp->value) +
				ns_nic_listp->value;
		else
			ns_nic_diff[metric_no] =
				(uint64_t)(ns_nic_listp->value -
					   ns_nic_plistp->value);

		ns_nic_curr[metric_no] =
			(uint64_t)(ns_nic_listp->value);

		if ((ns_nic_listp->next != NULL) &&
				(ns_nic_plistp->next != NULL)) {
			ns_nic_listp = ns_nic_listp->next;
			ns_nic_plistp = ns_nic_plistp->next;
		} else {
			ns_nic_int_ctx = ns_nic_prev_ctx;
			ns_nic_prev_ctx = ns_nic_curr_ctx;
			ns_nic_curr_ctx = ns_nic_int_ctx;
			done = 1;
		}
		metric_no--;
	} while (!done);
	done = 0;


	time_delta = ( 1000000000 *
		       (ns_nic_curr_time->tv_sec - ns_nic_prev_time->tv_sec) +
		       (ns_nic_curr_time->tv_nsec - ns_nic_prev_time->tv_nsec));

	for (i = 0; i < NUM_NICMETRICS; i++){
		uint64_t temp = __nic_perf_metric_calc(i, ns_nic_diff);
		if ((gemini_metrics_type == GEMINI_METRICS_COUNTER) ||
		    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
			/* end user has to deal with "rollover" */
			ns_nic_base_acc[i] += temp;
			v.v_u64 = ns_nic_base_acc[i];
			ldms_set_metric(ns_nic_base_metric_table[i], &v);
		}
		if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
		    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
			v.v_u64 = (double)(temp)/
				((double)time_delta/1000000000);
			ldms_set_metric(ns_nic_derived_metric_table[i], &v);
		}
	}

	ns_nic_int_time = ns_nic_prev_time;
	ns_nic_prev_time = ns_nic_curr_time;
	ns_nic_curr_time = ns_nic_int_time;

	return 0;
}


static uint64_t __linksmetrics_derived_metric_calc(int i, int j,
						   uint64_t** diff,
						   uint64_t timedelta){
	int rc = 0;

	switch (i) {
	case LD_SAMPLE_GEMINI_LINK_BW:
		if (timedelta > 0)
			return diff[LB_traffic][j]/timedelta;
		else
			return 0;
		break;
	case LD_SAMPLE_GEMINI_LINK_PACKETSIZE_AVE:
		if (diff[LB_packets][j] > 0)
			return (diff[LB_traffic][j]/diff[LB_packets][j]);
		else
			return 0;
		break;
	case LD_SAMPLE_GEMINI_LINK_STALLED:
		/* see __gem_link_derived_metric_calc for a description of this
		 * calculation. Here we dont know the num tiles per dir so
		 * its not included in the calc, but rather in the unit */
		if (timedelta > 0){
			uint64_t temp = (uint64_t)(
				(1.25 * diff[LB_stalled][j])/
				(double)(timedelta * 100.0 * 1000.0));
			if (temp > 100 * 1000)
				msglog("ERR: Time %lld.%09ld stalled[%d]:"
				       " %llu diff[LB_stalled][%d] = %g"
				       " tiles_per_dir = UNK time_delta = %g\n",
				       (long long) ns_glp_curr_time->tv_sec,
				       ns_glp_curr_time->tv_nsec,
				       j, temp, j, (double)diff[LB_stalled][j],
				       (double) timedelta);
			return temp;
		} else {
			return 0;
		}
		break;

	default:
		return 0;
	}
}

static uint64_t __gem_link_aggregate_phits(
	int i, uint64_t ctrs[][GEMINI_NUM_TILE_COUNTERS]){

	return ctrs[i][GEMINI_TCTR_VC0_INPUT_PHITS] +
		ctrs[i][GEMINI_TCTR_VC1_INPUT_PHITS];

}

static uint64_t __gem_link_aggregate_packets(
	int i, uint64_t ctrs[][GEMINI_NUM_TILE_COUNTERS]){

	return ctrs[i][GEMINI_TCTR_VC0_INPUT_PACKETS] +
		ctrs[i][GEMINI_TCTR_VC1_INPUT_PACKETS];
}

static uint64_t __gem_link_base_metric_calc(
	int i, int j, uint64_t module_ctrs[][GEMINI_NUM_TILE_COUNTERS])
{

	switch (j){
	case GB_traffic:
		return 3 * ( __gem_link_aggregate_phits(i, module_ctrs));
		break;
	case GB_packets:
		return  __gem_link_aggregate_packets(i, module_ctrs);
		break;
	case GB_input_stalls:
		return  module_ctrs[i][GEMINI_TCTR_INPUT_STALLS];
	case GB_output_stalls:
		return  module_ctrs[i][GEMINI_TCTR_OUTPUT_STALLS];
		break;
	default:
		return 0;
	}
}

static uint64_t __gem_link_derived_metric_calc(
	int i, int j, uint64_t sample_link_ctrs[][GEMINI_NUM_TILE_COUNTERS],
	uint64_t time_delta)
{
	int rc = 0;

	switch (j) {
	case GD_SAMPLE_GEMINI_LINK_BW:
		/*  sample_link_ctrs[i][0] and
		 *  sample_link_ctrs[i][1] are the number of
		 *  phits on each of virtual channel 0 and 1
		 *  respectively
		 *  each phit is 3 bytes so conversion to
		 *  bytes uses a multiplier of 3
		 *  bandwidth = bytes/time(in sec)
		 */
		if (time_delta > 0) {
			uint64_t phits =
				__gem_link_aggregate_phits(i, sample_link_ctrs);
			return 3 * (uint64_t)(
				(double)phits/((double)time_delta/1000000000));
		} else {
			return 0;
		}
		break;
	case GD_SAMPLE_GEMINI_LINK_USED_BW:
		if ((ns_glp_max_link_bw[i] > 0) && (time_delta > 0)) {
			/*  sample_link_ctrs[i][0] and
			 *  sample_link_ctrs[i][1] are the
			 *  number of phits on each of virtual
			 *  channel 0 and 1 respectively
			 *  each phit is 3 bytes so conversion
			 *  to bytes uses a multiplier of 3
			 *  time is in nsec so multiply by
			 *  10e9 to convert to sec
			 *  ns_glp_max_link_bw[i] comes from gemini.h
			 *  and is the bandwidth for that type
			 *  of link
			 *  multiplier of 100 converts fraction
			 *  to %. Multiply by 1000 to keep
			 *  precision.
			 */

			uint64_t phits =
				__gem_link_aggregate_phits(i, sample_link_ctrs);

			return (uint64_t)(
				(((double)phits/(double)time_delta) /
				(double) ns_glp_max_link_bw[i]) *
				100.0 * 3.0 * 1000.0);
		} else {
			return 0;
		}
		break;
	case GD_SAMPLE_GEMINI_LINK_PACKETSIZE_AVE: {
		uint64_t packets =
			__gem_link_aggregate_packets(i,	sample_link_ctrs);
		if ( packets > 0 ){
			/*  sample_link_ctrs[i][2] and
			 *  sample_link_ctrs[i][3] are the
			 *  number of packets on each of virtual
			 *  channel 0 and 1 respectively
			 *  average is being calculated as
			 *  (number of bytes total)/(number of
			 *  packets total)
			 */

			uint64_t phits =
				__gem_link_aggregate_phits(i, sample_link_ctrs);

			return 3 * (uint64_t)((double)phits/(double)packets);
		} else {
			return 0;
		}
	}
	break;
	case GD_SAMPLE_GEMINI_LINK_INPUT_STALLS:
		if ( (ns_glp_tiles_per_dir[i] > 0) && (time_delta > 0)){
			/*  sample_link_ctrs[i][4] is the number of
			 *  input stalls in direction i. Values for the
			 *  stall counter registers are in units of the
			 *  Gemini clock cycles. Gemini clock runs at
			 *  800Mhz, so multiply one tile's stall cycles
			 *  by 1.25 to get ns. Include all tiles in this
			 *  dir. Multiply by 100 to get percent.
			 *  Multiply by 1000 to keep precision.
			 */
			uint64_t temp = (uint64_t)(
				((1.25 * (double)sample_link_ctrs[i][
					  GEMINI_TCTR_INPUT_STALLS] /
				  (double)ns_glp_tiles_per_dir[i]) /
				 (double)time_delta) * 100.0 * 1000.0);
			if (temp > 100 * 1000)
				msglog("ERR: Time %lld.%09ld INPUT STALLS[%d]:"
				       " %llu sample_link_ctrs[%d][4] = %g"
				       " tiles_per_dir = %g time_delta = %g\n",
				       (long long) ns_glp_curr_time->tv_sec,
				       ns_glp_curr_time->tv_nsec,
				       i, temp, i,
				       (double)sample_link_ctrs[i][
					       GEMINI_TCTR_INPUT_STALLS],
				       (double)ns_glp_tiles_per_dir[i],
				       (double) time_delta);
			return temp;
		} else {
			return 0;
		}
		break;
	case GD_SAMPLE_GEMINI_LINK_OUTPUT_STALLS:
		if ( (ns_glp_tiles_per_dir[i] > 0) && (time_delta > 0)){
			/*  sample_link_ctrs[i][5] is the number of
			 *  input stalls in direction i. Values for the
			 *  stall counter registers are in units of the
			 *  Gemini clock cycles. Gemini clock runs at
			 *  800Mhz, so multiply one tile's stall cycles
			 *  by 1.25 to get ns. Include all tiles in this
			 *  dir. Multiply by 100 to get percent.
			 *  Multiply by 1000 to keep precision.
			 */
			uint64_t temp = (uint64_t)(
				((1.25 * (double)sample_link_ctrs[i][
					  GEMINI_TCTR_OUTPUT_STALLS] /
				  (double)ns_glp_tiles_per_dir[i]) /
				 (double)time_delta) * 100.0 * 1000.0);
			if (temp > 100 * 1000)
				msglog("ERR: Time %lld.%09ld OUTPUT STALLS[%d]:"
				       " %llu sample_link_ctrs[%d][5] = %g "
				       "tiles_per_dir = %g time_delta = %g\n",
				       (long long)ns_glp_curr_time->tv_sec,
				       ns_glp_curr_time->tv_nsec,
				       i, temp, i,
				       (double)sample_link_ctrs[i][
					       GEMINI_TCTR_OUTPUT_STALLS],
				       (double)ns_glp_tiles_per_dir[i],
				       (double) time_delta);
			return temp;
		} else {
			return 0;
		}
		break;
	default:
		/* Won't happen */
		return 0;
	}
}


int sample_metrics_gem_link_perf(ldmsd_msg_log_f msglog)
{
	int metric_no;
	int done = 0;
	int error;
	union ldms_value v;
	union ldms_value vd;
	uint64_t prevval;
	uint64_t time_delta;
	uint64_t sample_link_ctrs[GEMINI_NUM_LOGICAL_LINKS]
	[GEMINI_NUM_TILE_COUNTERS];
	uint64_t sample_link_ctrs_module[GEMINI_NUM_LOGICAL_LINKS]
	[GEMINI_NUM_TILE_COUNTERS];
	int rc;
	int i,j,k;

	if (ns_glp_valid == 0)
		return 0;

	/*  Zero sample array */
	memset (sample_link_ctrs, 0, sizeof(sample_link_ctrs));
	memset (sample_link_ctrs_module, 0, sizeof(sample_link_ctrs_module));
	metric_no = (GEMINI_NUM_TILE_ROWS * GEMINI_NUM_TILE_COLUMNS *
		     GEMINI_NUM_TILE_COUNTERS) - 1;

	clock_gettime(CLOCK_REALTIME, ns_glp_curr_time);

	error = gpcd_context_read_mmr_vals(ns_glp_curr_ctx);
	if (error) {
		msglog("nic_perf: Error reading mmr_vals\n");
		rc = EINVAL;
		return rc;
	} else {
		ns_glp_listp = ns_glp_curr_ctx->list;
		ns_glp_plistp = ns_glp_prev_ctx->list;
	}

	do {
		i = metric_no/(GEMINI_NUM_TILE_COLUMNS *
			       GEMINI_NUM_TILE_COUNTERS);
		j = ((int)metric_no/GEMINI_NUM_TILE_COUNTERS) %
			GEMINI_NUM_TILE_COLUMNS;
		k = metric_no%GEMINI_NUM_TILE_COUNTERS;


		if (ns_glp_state->tile[ns_glp_rc_to_tid[i][j]].dir !=
		    GEMINI_LINK_DIR_INVALID) {

			if ( ns_glp_listp->value <
			     ns_glp_plistp->value ) {
				msglog("INFO: ns_glp rollover:"
				       " time= %lld.%09%ld i %d j %d k %d"
				       " listp->value %llu plistp->value = "
				       "%llu\n",
				       (long long) ns_glp_curr_time->tv_sec,
				       ns_glp_curr_time->tv_nsec,
				       i, j, k, ns_glp_listp->value,
				       ns_glp_plistp->value);
				/* counters are really 48 bit put in a 64 */
				ns_glp_diff =
					(COUNTER_48BIT_MAX -
					 ns_glp_plistp->value) +
					ns_glp_listp->value;
				msglog("INFO: ns_glp rollover cont'd: i "
				       "%d j %d k %d ns_glp_diff = %llu\n",
				       i, j, k, ns_glp_diff);
			} else {
				ns_glp_diff = (uint64_t)(ns_glp_listp->value -
							 ns_glp_plistp->value);
			}

			sample_link_ctrs[ns_glp_state->tile[
					ns_glp_rc_to_tid[i][j]].dir][k] +=
				ns_glp_diff;


		}

		if ((ns_glp_listp->next != NULL) &&
		    (ns_glp_plistp->next != NULL)) {
			ns_glp_listp = ns_glp_listp->next;
			ns_glp_plistp = ns_glp_plistp->next;
			metric_no--;
		} else {
			ns_glp_int_ctx = ns_glp_prev_ctx;
			ns_glp_prev_ctx = ns_glp_curr_ctx;
			ns_glp_curr_ctx = ns_glp_int_ctx;
			done = 1;
		}

	} while (!done);
	done = 0;

	if ((gemini_metrics_type == GEMINI_METRICS_COUNTER) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		metric_no = 0;
		for (i = 0; i < GEMINI_NUM_LOGICAL_LINKS; i++) {
			for (j = 0; j < NUM_NS_GLP_BASENAME; j++) {
				/* enduser has to detect "rollover" */
				ns_glp_base_acc[metric_no] +=
					__gem_link_base_metric_calc(
						i, j, sample_link_ctrs);
				v.v_u64 = ns_glp_base_acc[metric_no];
				ldms_set_metric(
					ns_glp_base_metric_table[metric_no++],
					&v);
			}
		}
	}

	if ((gemini_metrics_type == GEMINI_METRICS_DERIVED) ||
	    (gemini_metrics_type == GEMINI_METRICS_BOTH)){
		time_delta =
			((ns_glp_curr_time->tv_sec - ns_glp_prev_time->tv_sec) *
			 1000000000) +
			(ns_glp_curr_time->tv_nsec - ns_glp_prev_time->tv_nsec);

		metric_no = 0;
		for (i = 0; i < GEMINI_NUM_LOGICAL_LINKS; i++) {
			for (j = 0; j < NUM_NS_GLP_DERIVEDNAME; j++) {
				/* well known case */
				if ((i == GEMINI_LINK_DIR_HOST) &&
				    (j == GD_SAMPLE_GEMINI_LINK_USED_BW))
					continue;
				v.v_u64 = __gem_link_derived_metric_calc(
					i, j, sample_link_ctrs, time_delta);
				ldms_set_metric(
					ns_glp_derived_metric_table[metric_no],
					&v);
				metric_no++;
			}
		}
	}

	ns_glp_int_time = ns_glp_prev_time;
	ns_glp_prev_time = ns_glp_curr_time;
	ns_glp_curr_time = ns_glp_int_time;

	return 0;
}


int sample_metrics_nettopo(ldmsd_msg_log_f msglog)
{

	union ldms_value v;

	/*  Fill in mesh coords (this is static and should be moved) */
	/* will want these 3 to be LDMS_V_U8 */
	v.v_u64 = (uint64_t) nettopo_coord.x;
	ldms_set_metric(nettopo_metric_table[0], &v);
	v.v_u64 = (uint64_t) nettopo_coord.y;
	ldms_set_metric(nettopo_metric_table[1], &v);
	v.v_u64 = (uint64_t) nettopo_coord.z;
	ldms_set_metric(nettopo_metric_table[2], &v);

	return 0;
}
