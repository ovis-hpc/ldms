/*
 * Copyright (c) 2013-2015,2019 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2015,2019 Sandia Corporation. All rights reserved.
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
 * \file gemini_metrics_gpcdr.h
 * \brief Utilities for cray_system_sampler for gemini metrics using gpcdr.

 */

#ifndef __GEMINI_METRICS_GPCDR_H_
#define __GEMINI_METRICS_GPCDR_H_

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

#include "gemini.h"
#include "cray_gemini_r_sampler.h"
#include "ldms.h"
#include "ldmsd.h"

/** order in the module. Counting on this being same as in gemini.h */
static char* linksmetrics_dir[] = {
	"X+","X-","Y+","Y-","Z+","Z-"
};

#define STR_WRAP(NAME) #NAME
#define PREFIX_ENUM_M(NAME) M_ ## NAME
#define PREFIX_ENUM_LB(NAME) LB_ ## NAME
#define PREFIX_ENUM_LD(NAME) LD_ ## NAME

/** default conf file reports these as "stalled" but the counter is the inq_stall" */
#define LINKSMETRICS_BASE_LIST(WRAP)\
	WRAP(traffic), \
	WRAP(packets), \
	WRAP(inq_stall),	      \
	WRAP(credit_stall),	      \
	WRAP(sendlinkstatus), \
	WRAP(recvlinkstatus)

#define LINKSMETRICS_DERIVED_LIST(WRAP) \
	WRAP(SAMPLE_GEMINI_LINK_BW),	\
	WRAP(SAMPLE_GEMINI_LINK_USED_BW),	\
	WRAP(SAMPLE_GEMINI_LINK_PACKETSIZE_AVE), \
		WRAP(SAMPLE_GEMINI_LINK_INQ_STALL), \
	WRAP(SAMPLE_GEMINI_LINK_CREDIT_STALL)


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

static char* linksmetrics_baseunit[] = {
	"(B)",
	"(1)",
	"(ns)",
	"(ns)",
	"(1)",
	"(1)"
	};

#ifdef HAVE_SPACELESS_NAMES
static char* linksmetrics_derivedunit[] = {
	"(B/s)",
	"(\%_x1e6)",
	"(B)",
	"(\%_x1e6)",
	"(\%_x1e6)"
	};
#else
static char* linksmetrics_derivedunit[] = {
	"(B/s)",
	"(\% x1e6)",
	"(B)",
	"(\% x1e6)",
	"(\% x1e6)"
	};
#endif

#define NUM_LINKSMETRICS_DIR (sizeof(linksmetrics_dir)/sizeof(linksmetrics_dir[0]))
#define NUM_LINKSMETRICS_BASENAME (sizeof(linksmetrics_basename)/sizeof(linksmetrics_basename[0]))
#define NUM_LINKSMETRICS_DERIVEDNAME (sizeof(linksmetrics_derivedname)/sizeof(linksmetrics_derivedname[0]))

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

#define LINKSMETRICS_FILE  "/sys/devices/virtual/gni/gpcdr0/metricsets/links/metrics"
#define NICMETRICS_FILE  "/sys/devices/virtual/gni/gpcdr0/metricsets/nic/metrics"

#define RCAHELPER_CMD "/opt/cray/rca/default/bin/rca-helper -O"


/* config */
int hsn_metrics_config(cray_gemini_inst_t inst, char* filename);


/* add metrics */
int add_metrics_linksmetrics(cray_gemini_inst_t inst, ldms_schema_t schema);
int add_metrics_nicmetrics(cray_gemini_inst_t inst, ldms_schema_t schema);

/** setup after add before sampling */
int linksmetrics_setup(cray_gemini_inst_t inst);
int nicmetrics_setup(cray_gemini_inst_t inst);

/* sampling */
int sample_metrics_linksmetrics(cray_gemini_inst_t inst, ldms_set_t set);
int sample_metrics_nicmetrics(cray_gemini_inst_t inst, ldms_set_t set);

#endif
