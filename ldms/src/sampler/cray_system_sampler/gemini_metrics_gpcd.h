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
 * \file gemini_metrics_gpcd.h
 * \brief Utilities for cray_system_sampler for gemini metrics using gpcd
 */

#ifndef __GEMINI_METRICS_GPCD_H_
#define __GEMINI_METRICS_GPCD_H_

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
#include "ldms.h"
#include "ldmsd.h"
#include "gemini.h"
#include "gemini_metrics.h"
#include "gpcd_util.h"

#define NS_GLP_BASE_LIST(WRAP)	\
	WRAP(traffic),	\
	WRAP(packets),	\
	WRAP(inq_stall),	\
	WRAP(credit_stall)

#define NS_GLP_DERIVED_LIST(WRAP) \
	WRAP(SAMPLE_GEMINI_LINK_BW),	\
	WRAP(SAMPLE_GEMINI_LINK_USED_BW),	\
	WRAP(SAMPLE_GEMINI_LINK_PACKETSIZE_AVE), \
	WRAP(SAMPLE_GEMINI_LINK_INQ_STALL),	\
	WRAP(SAMPLE_GEMINI_LINK_CREDIT_STALL)


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


static char* ns_glp_baseunit[] = {
	"(B)",
	"(1)",
	"(ns)",
	"(ns)",
	};

static char* ns_glp_derivedunit[] = {
	"(B/s)",
	"(\% x1e6)",
	"(B)",
	"(\% x1e6)",
	"(\% x1e6)"
	};


#define NUM_NS_GLP_BASENAME (sizeof(ns_glp_basename)/sizeof(ns_glp_basename[0]))
#define NUM_NS_GLP_DERIVEDNAME (sizeof(ns_glp_derivedname)/sizeof(ns_glp_derivedname[0]))

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

double ns_glp_max_link_bw[GEMINI_NUM_LOGICAL_LINKS];
int ns_glp_tiles_per_dir[GEMINI_NUM_LOGICAL_LINKS];

gemini_state_t *ns_glp_state;
int ns_glp_rc_to_tid[GEMINI_NUM_TILE_ROWS][GEMINI_NUM_TILE_COLUMNS];
struct timespec ns_glp_time1, ns_glp_time2;
struct timespec *ns_glp_curr_time, *ns_glp_prev_time,
	*ns_glp_int_time;
uint64_t ns_glp_diff;

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
int get_metric_size_nic_perf(size_t *m_sz, size_t *d_sz,
				  ldmsd_msg_log_f msglog);

/** add metrics */
int add_metrics_gem_link_perf(ldms_set_t set, int comp_id,
			      ldmsd_msg_log_f msglog);
int add_metrics_nic_perf(ldms_set_t set, int comp_id,
			      ldmsd_msg_log_f msglog);

/** setup after add before sampling */
int gem_link_perf_setup(ldmsd_msg_log_f msglog);
int nic_perf_setup(ldmsd_msg_log_f msglog);

/** sampling */
int sample_metrics_gem_link_perf(ldmsd_msg_log_f msglog);
int sample_metrics_nic_perf(ldmsd_msg_log_f msglog);

#endif
