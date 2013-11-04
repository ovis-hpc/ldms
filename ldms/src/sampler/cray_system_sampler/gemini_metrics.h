/*
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
 * \file gemini_metrics.h
 * \brief Utilities for cray_system_sampler for gemini and mesh_coord metrics
 */

#ifndef __GEMINI_METRICS_H_
#define __GEMINI_METRICS_H_

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
#include <ctype.h>

#define PREFIX_ENUM_M(NAME) M_ ## NAME
#define PREFIX_ENUM_LB(NAME) LB_ ## NAME
#define PREFIX_ENUM_LD(NAME) LD_ ## NAME
#define PREFIX_ENUM_GM(NAME) GM_ ## NAME
#define PREFIX_ENUM_GB(NAME) GB_ ## NAME
#define PREFIX_ENUM_GD(NAME) GD_ ## NAME

#define COUNTER_48BIT_MAX 281474976710655

static char* nettopo_meshcoord_metricname[] = {
      "nettopo_mesh_coord_X",
      "nettopo_mesh_coord_Y",
      "nettopo_mesh_coord_Z"
};
#define NETTOPODIM (sizeof(nettopo_meshcoord_metricname)/sizeof(nettopo_meshcoord_metricname[0]))

typedef struct {
	int x, y, z;
} nettopo_coord_t;

/** order in the module */
static char* linksmetrics_dir[] = {
	"X+","X-","Y+","Y-","Z+","Z-"
};

#define LINKSMETRICS_BASE_LIST(WRAP)\
	WRAP(traffic), \
	WRAP(packets), \
	WRAP(stalled),	      \
	WRAP(sendlinkstatus), \
	WRAP(recvlinkstatus)


#define NS_GLP_BASE_LIST(WRAP)	\
	WRAP(traffic),	\
	WRAP(packets),	\
	WRAP(input_stalls),	\
	WRAP(output_stalls)

#define LINKSMETRICS_DERIVED_LIST(WRAP) \
	WRAP(SAMPLE_GEMINI_LINK_BW),	\
	WRAP(SAMPLE_GEMINI_LINK_PACKETSIZE_AVE), \
	WRAP(SAMPLE_GEMINI_LINK_STALLED)

#define NS_GLP_DERIVED_LIST(WRAP) \
	WRAP(SAMPLE_GEMINI_LINK_BW),	\
	WRAP(SAMPLE_GEMINI_LINK_USED_BW),	\
	WRAP(SAMPLE_GEMINI_LINK_PACKETSIZE_AVE), \
	WRAP(SAMPLE_GEMINI_LINK_INPUT_STALLS),	\
	WRAP(SAMPLE_GEMINI_LINK_OUTPUT_STALLS)

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

static char* ns_glp_basename[] = {
	NS_GLP_BASE_LIST(STR_WRAP)
};
static char* ns_glp_derivedname[] = {
	NS_GLP_DERIVED_LIST(STR_WRAP)
};
typedef enum {
	NS_GLP_BASE_LIST(PREFIX_ENUM_GB)
} ns_glp_base_metric_t;
typedef enum {
	NS_GLP_DERIVED_LIST(PREFIX_ENUM_GD)
} ns_glp_derived_metric_t;

static char* linksmetrics_baseunit[] = {
	"(B)",
	"(1)",
	"(ns)",
	"(1)",
	"(1)"
	};

static char* linksmetrics_derivedunit[] = {
	"(B/s)",
	"(B)",
	"(\% x1000 x tiles)"
	};

static char* ns_glp_baseunit[] = {
	"(B)",
	"(1)",
	"(ns)",
	"(ns)",
	};

static char* ns_glp_derivedunit[] = {
	"(B/s)",
	"(\% x1000)",
	"(B)",
	"(\% x1000)",
	"(\% x1000)"
	};


#define NUM_LINKSMETRICS_DIR (sizeof(linksmetrics_dir)/sizeof(linksmetrics_dir[0]))
#define NUM_LINKSMETRICS_BASENAME (sizeof(linksmetrics_basename)/sizeof(linksmetrics_basename[0]))
#define NUM_LINKSMETRICS_DERIVEDNAME (sizeof(linksmetrics_derivedname)/sizeof(linksmetrics_derivedname[0]))

#define NUM_NS_GLP_BASENAME (sizeof(ns_glp_basename)/sizeof(ns_glp_basename[0]))
#define NUM_NS_GLP_DERIVEDNAME (sizeof(ns_glp_derivedname)/sizeof(ns_glp_derivedname[0]))

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

typedef enum {
	GEMINI_METRICS_COUNTER,
	GEMINI_METRICS_DERIVED,
	GEMINI_METRICS_BOTH
} gemini_metrics_type_t;

#define LINKSMETRICS_FILE  "/sys/devices/virtual/gni/gpcdr0/metricsets/links/metrics"
#define NICMETRICS_FILE  "/sys/devices/virtual/gni/gpcdr0/metricsets/nic/metrics"

int gemini_metrics_type; /**< raw, derived, both */

/* NETTOPO Specific */
nettopo_coord_t nettopo_coord;
ldms_metric_t* nettopo_metric_table;

/* LINKSMETRICS Specific */
FILE *lm_f;
uint64_t linksmetrics_prev_time;
ldms_metric_t* linksmetrics_base_metric_table;
ldms_metric_t* linksmetrics_derived_metric_table;
uint64_t*** linksmetrics_base_values; /**< holds curr & prev raw module
					   data for derived computation */
uint64_t** linksmetrics_base_diff; /**< holds diffs for the module values */

int linksmetrics_values_idx; /**< index of the curr values for the above */
int num_linksmetrics_exists;
int* linksmetrics_indicies; /**< track metric table index in
			       which to store the raw data */
int linksmetrics_valid;

/* NICMETRICS Specific */
FILE *nm_f;
uint64_t nicmetrics_prev_time;
ldms_metric_t* nicmetrics_base_metric_table;
ldms_metric_t* nicmetrics_derived_metric_table;
uint64_t** nicmetrics_base_values; /**< holds curr & prev raw module data
					for derived computation */
int nicmetrics_values_idx; /**< index of the curr values for the above */
int nicmetrics_valid;

/* GEM_LINK_PERF_SPECIFIC */
gpcd_context_t *ns_glp_curr_ctx;
gpcd_context_t *ns_glp_prev_ctx;
gpcd_context_t *ns_glp_int_ctx;
gpcd_mmr_list_t *ns_glp_listp;
gpcd_mmr_list_t *ns_glp_plistp;
uint64_t* ns_glp_base_acc; /**< per base metric accumulator */
ldms_metric_t* ns_glp_base_metric_table;
ldms_metric_t* ns_glp_derived_metric_table;
int ns_glp_valid;

gemini_state_t *ns_glp_state;
char *ns_glp_rtrfile;
int ns_glp_rc_to_tid[GEMINI_NUM_TILE_ROWS][GEMINI_NUM_TILE_COLUMNS];
struct timespec ns_glp_time1, ns_glp_time2;
struct timespec *ns_glp_curr_time, *ns_glp_prev_time,
	*ns_glp_int_time;
uint64_t ns_glp_diff;

static double ns_glp_max_link_bw[GEMINI_NUM_LOGICAL_LINKS];
static int ns_glp_tiles_per_dir[GEMINI_NUM_LOGICAL_LINKS];

/* NIC_PERF Specific */
uint64_t ns_nic_diff[NUM_NIC_PERF_RAW];
uint64_t ns_nic_curr[NUM_NIC_PERF_RAW];
gpcd_context_t *ns_nic_curr_ctx;
gpcd_context_t *ns_nic_prev_ctx;
gpcd_context_t *ns_nic_int_ctx;
gpcd_mmr_list_t *ns_nic_listp;
gpcd_mmr_list_t *ns_nic_plistp;
uint64_t* ns_nic_base_acc; /**< prev per metric accumulator */
ldms_metric_t* ns_nic_base_metric_table;
ldms_metric_t* ns_nic_derived_metric_table;
struct timespec ns_nic_time1, ns_nic_time2;
struct timespec *ns_nic_curr_time, *ns_nic_prev_time, *ns_nic_int_time;
int ns_nic_valid;

/** get metric size */
int get_metric_size_gem_link_perf(size_t *m_sz, size_t *d_sz,
				  ldmsd_msg_log_f msglog);
int get_metric_size_linksmetrics(size_t *m_sz, size_t *d_sz,
				 ldmsd_msg_log_f msglog);
int get_metric_size_nicmetrics(size_t *m_sz, size_t *d_sz,
			       ldmsd_msg_log_f msglog);

/** add metrics */
int add_metrics_gem_link_perf(ldms_set_t set, int comp_id,
			      ldmsd_msg_log_f msglog);


/** setup after add before sampling */
int gem_link_perf_setup(ldmsd_msg_log_f msglog);
int linksmetrics_setup(ldmsd_msg_log_f msglog);
int nic_perf_setup(ldmsd_msg_log_f msglog);
int nettopo_setup(ldmsd_msg_log_f msglog);


/** sampling */
int sample_metrics_gem_link_perf(ldmsd_msg_log_f msglog);
int sample_metrics_linksmetrics(ldmsd_msg_log_f msglog);
int sample_metrics_nicmetrics(ldmsd_msg_log_f msglog);
int sample_metrics_nic_perf(ldmsd_msg_log_f msglog);
int sample_metrics_nettopo(ldmsd_msg_log_f msglog);

#endif
