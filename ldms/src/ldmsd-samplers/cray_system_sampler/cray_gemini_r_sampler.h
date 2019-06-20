/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2017,2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2017,2019 Open Grid Computing, Inc. All rights reserved.
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
#ifndef __CRAY_GEMINI_R_SAMPLER_H__
#define __CRAY_GEMINI_R_SAMPLER_H__
#define _GNU_SOURCE
#include "gemini.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"
#include "cray_sampler_base.h"

typedef struct cray_gemini_inst_s {
	struct cray_sampler_inst_s base;

	/* LINKSMETRICS Specific */
	FILE *lm_f;
	uint64_t linksmetrics_prev_time;
	int* linksmetrics_base_metric_table;
	int* linksmetrics_derived_metric_table;
	uint64_t*** linksmetrics_base_values; /**< holds curr & prev raw module
						data for derived computation */
	uint64_t** linksmetrics_base_diff; /**< holds diffs for the module values */

	int linksmetrics_values_idx; /**< index of the curr values for the above */
	int num_linksmetrics_exists;
	int* linksmetrics_indicies; /**< track metric table index in
				      which to store the raw data */

	double linksmetrics_max_link_bw[GEMINI_NUM_LOGICAL_LINKS];
	int linksmetrics_tiles_per_dir[GEMINI_NUM_LOGICAL_LINKS];
	int linksmetrics_valid;

	char* rtrfile; /**< needed for gpcd, but also used to get maxbw for gpcdr */

	/* NICMETRICS Specific */
	FILE *nm_f;
	uint64_t nicmetrics_prev_time;
	int nicmetrics_time_multiplier;
	int* nicmetrics_base_metric_table;
	int* nicmetrics_derived_metric_table;
	uint64_t** nicmetrics_base_values; /**< holds curr & prev raw module data
					     for derived computation */
	int nicmetrics_values_idx; /**< index of the curr values for the above */
	int nicmetrics_valid;


	int hsn_metrics_type;

	int off_hsn;
} *cray_gemini_inst_t;

#define __DERIVED(inst) ((inst)->hsn_metrics_type & HSN_METRICS_DERIVED)
#define __COUNTER(inst) ((inst)->hsn_metrics_type & HSN_METRICS_COUNTER)

#endif
