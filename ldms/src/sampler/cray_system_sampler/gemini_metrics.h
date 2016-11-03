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
 * \file gemini_metrics.h
 * \brief Utilities for cray_system_sampler for gemini metrics
 *        Common to gpcd and gpcrd interfaces...
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
 * (Sandia National Laboratories) gemini performance counter interface and
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

#define STR_WRAP(NAME) #NAME
#define PREFIX_ENUM_M(NAME) M_ ## NAME
#define PREFIX_ENUM_LB(NAME) LB_ ## NAME
#define PREFIX_ENUM_LD(NAME) LD_ ## NAME
#define PREFIX_ENUM_GM(NAME) GM_ ## NAME
#define PREFIX_ENUM_GB(NAME) GB_ ## NAME
#define PREFIX_ENUM_GD(NAME) GD_ ## NAME

#define COUNTER_48BIT_MAX 281474976710655

/** currently the nic metric names are the same for both */

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

int gemini_metrics_type; /**< raw, derived, both */

char* rtrfile; /**< needed for gpcd, but also used to get maxbw for gpcdr */

#endif
