/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2017 Open Grid Computing, Inc. All rights reserved.
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

#ifndef __CRAY_ARIES_R_SAMPLER_H_
#define __CRAY_ARIES_R_SAMPLER_H_
#define _GNU_SOURCE
#include "ldmsd.h"
#include "ldmsd_sampler.h"
#include "cray_sampler_base.h"
#include "aries_metrics_gpcdr.h"

typedef struct cray_aries_inst_s {
	struct cray_sampler_inst_s base;

	int off_hsn;

	/* LINKSMETRICS Specific */
	FILE *lm_f[ENDLINKS];
	uint64_t linksmetrics_prev_time[ENDLINKS];
	int linksmetrics_time_multiplier[ENDLINKS];
	int* linksmetrics_base_metric_table[ENDLINKS];
	int* linksmetrics_derived_metric_table[ENDLINKS];
	uint64_t** linksmetrics_base_values[ENDLINKS]; /**< holds curr & prev raw module
							 data for derived computation */
	uint64_t* linksmetrics_base_diff[ENDLINKS]; /**< holds diffs for the module values */
	int linksmetrics_values_idx[ENDLINKS];
	int linksmetrics_valid;

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
} *cray_aries_inst_t;
#endif
