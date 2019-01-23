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
#include "cray_aries_r_sampler.h"

#define ARIES_MAX_TILES 48
#define ARIES_NUM_TILES 40

#define STR_WRAP(NAME) #NAME
#define PREFIX_ENUM_M(NAME) M_ ## NAME
#define PREFIX_ENUM_LB(NAME) LB_ ## NAME
#define PREFIX_ENUM_LD(NAME) LD_ ## NAME

/** NOTE: the enum isnt x-refed. Just iterate thru while < ENDLINKS */
aries_linksmetrics_info_t linksinfo[] = {
	{ TRAFFIC,
	  "/sys/devices/virtual/gni/gpcdr0/metricsets/linktraffic/metrics",
	  "traffic", "(B)", 1, "SAMPLE_traffic", "(B/s)" },
	{ STALLED,
	  "/sys/devices/virtual/gni/gpcdr0/metricsets/linkstalled/metrics",
	  "stalled", "(ns)", 0, NULL, NULL },
	{ SENDLINKSTATUS,
	  "/sys/devices/virtual/gni/gpcdr0/metricsets/linksendstatus/metrics",
	  "sendlinkstatus", "(1)", 0, NULL, NULL },
	{ RECVLINKSTATUS,
	  "/sys/devices/virtual/gni/gpcdr0/metricsets/linkrecvstatus/metrics",
	  "recvlinkstatus", "(1)", 0, NULL, NULL },
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
#define NUM_NICMETRICS (sizeof(nicmetrics_basename) / \
			sizeof(nicmetrics_basename[0]))

#define NICMETRICS_FILE  \
	"/sys/devices/virtual/gni/gpcdr0/metricsets/nic/metrics"



/** internal calculations */
static uint64_t __linksmetrics_derived_metric_calc(cray_aries_inst_t inst,
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


int hsn_metrics_config(char *ebuf, int ebufsz)
{
	int j;

	/* Check for the existence of the files. NOTE: this is
	   currently only done for the ARIES */
	for (j = 0; j < ENDLINKS; j++){
		FILE* junk = fopen(linksinfo[j].fname, "r");
		if (junk) {
			fclose(junk);
			continue;
		}
		ldmsd_log(LDMSD_LCRITICAL,
			"cray_aries_r_sampler: missing gpcdr file <%s>"
			"Check that 1)gpcdr is running: lsmod | grep gpcdr and "
			"2) the gpcdr configuration file "
			"(e.g., /etc/opt/cray/gni-gpcdr-utils/gpcdr-init.conf "
			"or if specified via GPCDRINIT_CONF) correctly "
			"specifies METRICSETS=\"linktraffic linstalled "
			"linksendstatus linkrecvstatus nic\"\n",
			linksinfo[j].fname);
		snprintf(ebuf, ebufsz,
			"cray_aries_r_sampler: missing gpcdr file <%s>"
			"Check that 1)gpcdr is running: lsmod | grep gpcdr and "
			"2) the gpcdr configuration file "
			"(e.g., /etc/opt/cray/gni-gpcdr-utils/gpcdr-init.conf "
			"or if specified via GPCDRINIT_CONF) correctly "
			"specifies METRICSETS=\"linktraffic linstalled "
			"linksendstatus linkrecvstatus nic\"\n",
			linksinfo[j].fname);
		return ENOENT;
	}

	return 0;

}

int aries_linksmetrics_setup(cray_aries_inst_t inst)
{
	char lbuf[256];
	char metric_name[128];
	char units[32];
	char* s;
	uint64_t val;
	int count = 0;
	int i, k, rc;
	FILE *f;

	rc = 0;
	inst->linksmetrics_valid = 0;

	/** storage for derived computations if necessary */
	for (k = 0; k < ENDLINKS; k++){
		if (!__DERIVED(inst) || !linksinfo[k].doderived)
			continue;
		if (!linksinfo[k].doderived)
			continue;

		inst->linksmetrics_base_values[k] = calloc(2,sizeof(uint64_t*));
		if (!inst->linksmetrics_base_values[k])
			return ENOMEM;

		inst->linksmetrics_base_diff[k] = calloc(ARIES_MAX_TILES,
							 sizeof(uint64_t));
		if (!inst->linksmetrics_base_diff[k])
			return ENOMEM;

		for (i = 0; i < 2; i++){
			inst->linksmetrics_base_values[k][i] =
				calloc(ARIES_MAX_TILES, sizeof(uint64_t));
			if (!inst->linksmetrics_base_values[k][i])
				return ENOMEM;
		}
	}

	/**
	 * Make sure files are there and all have metrics.
	 * Also store the prev vals if need them
	 */
	for (k = 0; k < ENDLINKS; k++){
		inst->linksmetrics_values_idx[k] = 0;

		f = fopen(linksinfo[k].fname, "r");
		if (!f) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Could not open the source file '%s'\n",
				 linksinfo[k].fname);
			return EINVAL;
		}
		inst->lm_f[k] = f;

		/* timestamp */
		s = fgets(lbuf, sizeof(lbuf), f);
		if (!s)
			return EINVAL;

		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n", metric_name,
			    &inst->linksmetrics_prev_time[k], units);

		if (rc != 3) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Issue reading the source file '%s'\n",
				 linksinfo[k].fname);
			rc = EINVAL;
			return rc;
		}
		if (strcmp(units,"ms") == 0){
			inst->linksmetrics_time_multiplier[k] = 1000;
		} else if (strcmp(units,"seconds") == 0){
			inst->linksmetrics_time_multiplier[k] = 1;
		} else {
			INST_LOG(inst, LDMSD_LERROR,
				 "linksmetrics: wrong gpcdr interface "
				 "(time units)\n");
			rc = EINVAL;
			return rc;
		}

		count = 0;
		do {
			int dir = -1;
			s = fgets(lbuf, sizeof(lbuf), f);
			if (!s)
				break;
			rc = sscanf(lbuf, "%[^:]:%d %" PRIu64 " %s\n",
				    metric_name, &dir, &val, units);
			if (rc != 4) {
				INST_LOG(inst, LDMSD_LERROR,
					 "Issue reading the source file '%s'\n",
					 linksinfo[k].fname);
				rc = EINVAL;
				return rc;
			}

			/* store the val for the first calculation */
			if (__DERIVED(inst) && linksinfo[k].doderived) {
				const int j = inst->linksmetrics_values_idx[k];
				inst->linksmetrics_base_values[k][j][dir] = val;
			}
			count++;
		} while (s);

		if (count != ARIES_NUM_TILES) {
			INST_LOG(inst, LDMSD_LERROR,
				 "wrong number of metrics in file '%s'\n",
				 linksinfo[k].fname);
			rc = EINVAL;
			return rc;
		}

		inst->linksmetrics_values_idx[k] = 1;

		//NOTE: leaving the filehandles open
	} /* ENDLINKS */

	inst->linksmetrics_valid = 1;
	return 0;
}


int aries_nicmetrics_setup(cray_aries_inst_t inst)
{
	char lbuf[256];
	char metric_name[128];
	char units[32];
	char* s;
	uint64_t val;
	int metric_no = 0;
	int i, rc;

	rc = 0;
	inst->nicmetrics_valid = 0;

	inst->nm_f = fopen(NICMETRICS_FILE, "r");
	if (!inst->nm_f) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Could not open the source file '%s'\n",
			 NICMETRICS_FILE);
		return EINVAL;
	}


	inst->nicmetrics_values_idx = 0;

	/** storage for derived metrics */
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

		/* store the first set of values */
		fseek(inst->nm_f, 0, SEEK_SET);
		/* timestamp */
		s = fgets(lbuf, sizeof(lbuf), inst->nm_f);
		if (!s)
			return EINVAL;
		rc = sscanf(lbuf, "%s %" PRIu64 " %s\n",
			    metric_name, &inst->nicmetrics_prev_time, units);
		if (rc != 3) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Issue reading source file '%s'\n",
				 NICMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}
		if (strcmp(units,"ms") == 0){
			inst->nicmetrics_time_multiplier = 1000;
		} else if (strcmp(units,"seconds") == 0){
			inst->nicmetrics_time_multiplier = 1;
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
					 "Issue reading source file '%s'\n",
					 NICMETRICS_FILE);
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


int add_metrics_aries_linksmetrics(cray_aries_inst_t inst, ldms_schema_t schema)
{

	char newname[96];
	int i, j;
	int rc;
	int **base_tbl, **drv_tbl;

	base_tbl = inst->linksmetrics_base_metric_table;
	drv_tbl = inst->linksmetrics_derived_metric_table;

	/** NOTE: add all 48, even if they arent used */
	for (i = 0; i < ENDLINKS; i++){
		if (__COUNTER(inst)){

			base_tbl[i] = calloc(ARIES_MAX_TILES, sizeof(int));
			if (!base_tbl[i])
				return ENOMEM;
		}


		if (__DERIVED(inst) && linksinfo[i].doderived) {
			drv_tbl[i] = calloc(ARIES_MAX_TILES, sizeof(int));
			if (!drv_tbl[i])
				return ENOMEM;
		} else {
			drv_tbl[i] = NULL;
		}
	}


	for (i = 0; i < ENDLINKS; i++){
		if (__COUNTER(inst)){
			for (j = 0; j < ARIES_MAX_TILES; j++){
				__links_metric_name(i, 1, j, newname);
				rc = ldms_schema_metric_add(schema, newname,
							    LDMS_V_U64, "");
				if (rc < 0){
					INST_LOG(inst, LDMSD_LERROR,
						 "Failed to add metric <%s>\n",
						 newname);
					return -rc;
				}
				base_tbl[i][j] = rc;
			}
		}


		if (__DERIVED(inst) && linksinfo[i].doderived) {
			for (j = 0; j < ARIES_MAX_TILES; j++) {
				__links_metric_name(i, 0, j, newname);
				rc = ldms_schema_metric_add(schema, newname,
							    LDMS_V_U64, "");
				if (rc < 0){
					INST_LOG(inst, LDMSD_LERROR,
						 "Failed to add metric <%s>\n",
						 newname);
					return -rc;
				}
				drv_tbl[i][j] = rc;
			}
		}
	}
	return 0;
}


int add_metrics_aries_nicmetrics(cray_aries_inst_t inst, ldms_schema_t schema)
{
	char newname[96];
	int i;
	int rc;

	if (__COUNTER(inst)) {
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
				return -rc;
			}
			inst->nicmetrics_base_metric_table[i] = rc;
		}
	}

	if (__DERIVED(inst)) {
		inst->nicmetrics_derived_metric_table =
			calloc(NUM_NICMETRICS, sizeof(int));
		if (!inst->nicmetrics_derived_metric_table)
			return ENOMEM;

		for (i = 0; i < NUM_NICMETRICS; i++) {
			sprintf(newname, "%s_%s %s",
				nicmetrics_derivedprefix,
				nicmetrics_basename[i],
				nicmetrics_derivedunit);
			rc = ldms_schema_metric_add(schema, newname,
						    LDMS_V_U64, "");
			if (rc < 0){
				INST_LOG(inst, LDMSD_LERROR,
					 "Failed to add metric <%s>\n",
					 newname);
				return -rc;
			}
			inst->nicmetrics_derived_metric_table[i] = rc;
		}
	}

	return 0;
}

static
int __sample_link(cray_aries_inst_t inst, ldms_set_t set, int i)
{
	char lbuf[256];
	char metric_name[64];
	int dir;
	char units[32];
	char* s;
	union ldms_value v;
	uint64_t curr_time;
	uint64_t time_delta;
	int count = 0;
	int idx = 0;
	int j, rc;
	uint64_t ***base_val, **base_diff; /* short hand */
	int **base_tbl, **drv_tbl; /* short hand */

	FILE* lm_f = inst->lm_f[i];
	idx = inst->linksmetrics_values_idx[i];

	if (lm_f == NULL) {
		return 0;
	}

	base_val = inst->linksmetrics_base_values;
	base_diff = inst->linksmetrics_base_diff;
	base_tbl = inst->linksmetrics_base_metric_table;
	drv_tbl = inst->linksmetrics_derived_metric_table;

	fseek(lm_f, 0, SEEK_SET);

	/* read the timestamp */
	s = fgets(lbuf, sizeof(lbuf), lm_f);
	if (!s) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Issue reading the source file '%s'\n",
			 linksinfo[i].fname);
		return EINVAL;
	}
	rc = sscanf(lbuf, "%s %" PRIu64 " %s\n",
		    metric_name, &curr_time, units);
	if (rc != 3) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Issue reading the source file '%s'\n",
			 linksinfo[i].fname);
		rc = EINVAL;
		return rc;
	}

	count = 0;
	time_delta = curr_time - inst->linksmetrics_prev_time[i];
	do {
		s = fgets(lbuf, sizeof(lbuf), lm_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%[^:]:%d %" PRIu64 " %s\n",
			    metric_name, &dir, &v.v_u64, units);
		if (rc != 4) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Issue reading the source file '%s'\n",
				 linksinfo[i].fname);
			rc = EINVAL;
			return rc;
		}

		if (__COUNTER(inst)){
			ldms_metric_set(set, base_tbl[i][dir], &v);
		}

		if (__DERIVED(inst) && linksinfo[i].doderived) {
			base_val[i][idx][dir] = v.v_u64;

			if (base_val[i][idx][dir] < base_val[i][!idx][dir]) {
				/* the gpcdr values are 64 bit */
				base_diff[i][dir] =
					(ULONG_MAX - base_val[i][!idx][dir]) +
					base_val[i][idx][dir];
			} else {
				base_diff[i][dir] = base_val[i][idx][dir] -
						    base_val[i][!idx][dir];
			}
		}
		count++;

	} while (s); /* read whole file */

	if (__DERIVED(inst) && linksinfo[i].doderived) {
		for (j = 0; j < ARIES_MAX_TILES; j++) {
			/* there are 8 that wont need to be done,
			 * but those will ret 0 (base_diff = 0) */
			v.v_u64 = __linksmetrics_derived_metric_calc(
					inst, i,
					&(inst->linksmetrics_base_diff[i][j]),
					time_delta);
			ldms_metric_set(set, drv_tbl[i][j], &v);
		}
	}

	if (count != ARIES_NUM_TILES){
		INST_LOG(inst, LDMSD_LERROR,
			 "linksmetrics: in sample wrong num values for '%s'\n",
			 linksinfo[i].fname);
		inst->linksmetrics_valid = 0;
		return EINVAL;
	}

	inst->linksmetrics_values_idx[i] =
			(inst->linksmetrics_values_idx[i] == 0? 1 : 0);
	inst->linksmetrics_prev_time[i] = curr_time;
	return 0;
}

int sample_metrics_aries_linksmetrics(cray_aries_inst_t inst, ldms_set_t set)
{
	int i, rc;

	if (!inst->linksmetrics_valid)
		return 0;

	for (i = 0; i < ENDLINKS; i++) {
		rc = __sample_link(inst, set, i);
		if (rc)
			return rc;
	}

	return 0;
}


int sample_metrics_aries_nicmetrics(cray_aries_inst_t inst, ldms_set_t set)
{
	char lbuf[256];
	char metric_name[64];
	char units[32];
	uint64_t curr_time;
	uint64_t time_delta, mult;
	char* s;
	union ldms_value v;
	int idx;
	int i, rc;
	int *base_tbl; /* short hand */
	uint64_t **base_val; /* short hand */

	if (!inst->nm_f || !inst->nicmetrics_valid)
		return 0;

	base_tbl = inst->nicmetrics_base_metric_table;
	base_val = inst->nicmetrics_base_values;

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
		INST_LOG(inst, LDMSD_LERROR,"Issue reading source file '%s'\n",
			 NICMETRICS_FILE);
		rc = EINVAL;
		return rc;
	}

	time_delta = curr_time - inst->nicmetrics_prev_time;/*units see below*/
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
				 "Issue reading source file '%s'\n",
				 NICMETRICS_FILE);
			rc = EINVAL;
			return rc;
		}

		if (__COUNTER(inst)){
			ldms_metric_set(set, base_tbl[i], &v);
		}
		if (__DERIVED(inst)){
			base_val[idx][i] = v.v_u64;
		}
		i++;
	} while (s);

	mult = inst->nicmetrics_time_multiplier; /* short hand */

	if (__DERIVED(inst)) {
		for (i = 0; i < NUM_NICMETRICS; i++){
			uint64_t diff;

			/* the gpcdr values are 64 bit */
			if ( base_val[idx][i] < base_val[!idx][i] ) {
				diff = (ULONG_MAX - base_val[!idx][i]) +
					base_val[idx][i];
			} else {
				diff = base_val[idx][i] - base_val[!idx][i];
			}

			if (time_delta > 0)
				v.v_u64 = ((diff*mult)/time_delta);
			else
				v.v_u64 = 0;

			ldms_metric_set(set,
				inst->nicmetrics_derived_metric_table[i], &v);
		}
	}

	inst->nicmetrics_values_idx = (inst->nicmetrics_values_idx == 0? 1: 0);
	inst->nicmetrics_prev_time = curr_time;
	return 0;

}

static uint64_t __linksmetrics_derived_metric_calc(cray_aries_inst_t inst,
						   int i, uint64_t* diff,
						   uint64_t timedelta){
	switch (i) {
	case TRAFFIC:
		if ((timedelta > 0) && (diff != 0)){
			return (uint64_t)
				((double)
				 (inst->linksmetrics_time_multiplier[i] *
				  (*diff))
				 /(double) timedelta);
		} else {
			return 0;
		}
		break;
	default:
		return 0;
	}
}
