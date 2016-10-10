/*
 * Copyright (c) 2013-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2015 Sandia Corporation. All rights reserved.
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
 * \file gpcd_util.c
 * \brief Utilities reading and aggregating the gemini_performance counters.
 */

/**
 * Link aggregation methodlogy from gpcd counters based on Kevin Pedretti's
 * (Sandia National Laboratories) gemini performance counter interface and
 * link aggregation library. It has been augmented with pattern analysis
 * of the interconnect file.
 *
 * NOTE: Link aggregation methodology has been deprecated in v3.
 */


#define _GNU_SOURCE

#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>
#include "gpcd_util.h"
#include "ldmsd.h"
#include "ldms.h"

/**
 * Build linked list of tile performance counters we wish to get values for
 */
gpcd_context_t *gem_link_perf_create_context(ldmsd_msg_log_f* msglog_outer)
{
	int i, j, k, status;
	char name[128];
	gpcd_context_t *lctx;
	gpcd_mmr_desc_t *desc;

	lctx = gpcd_create_context();
	if (!lctx)
		return NULL;

	/*  loop over all 48 tiles, for each tile add its 6 static counters to
	 *  the context */
	for (i = 0; i < GEMINI_NUM_TILE_ROWS; i++) {
		for (j = 0; j < GEMINI_NUM_TILE_COLUMNS; j++) {
			for (k = 0; k < GEMINI_NUM_TILE_COUNTERS; k++) {
				sprintf(name,
					"GM_%d_%d_TILE_PERFORMANCE_COUNTERS_%d",
						i, j, k);
				desc = (gpcd_mmr_desc_t *)
						gpcd_lookup_mmr_byname(name);
				if (!desc) {
					gpcd_remove_context(lctx);
					return NULL;
				}
				status = gpcd_context_add_mmr(lctx, desc);
				if (status != 0) {
					gpcd_remove_context(lctx);
					return NULL;
				}
			}
		}
	}
	return lctx;
}


/**
 * Build linked list of performance counters we wish to get values for
 */
gpcd_context_t *nic_perf_create_context(ldmsd_msg_log_f* msglog)
{
	int i, status;
	gpcd_context_t *lctx;
	gpcd_mmr_desc_t *desc;

	lctx = gpcd_create_context();
	if (!lctx)
		return NULL;

	for (i = 0; i < NUM_NIC_PERF_RAW; i++) {
		desc = (gpcd_mmr_desc_t *)
			gpcd_lookup_mmr_byname(nic_perf_raw_name[i]);
		if (!desc) {
			gpcd_remove_context(lctx);
			return NULL;
		}

		status = gpcd_context_add_mmr(lctx, desc);
		if (status != 0) {
			gpcd_remove_context(lctx);
			return NULL;
		}
	}

	return lctx;
}
