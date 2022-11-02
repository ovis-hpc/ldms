/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2016 Sandia Corporation. All rights reserved.
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
 * \file general_metrics.h non-HSN metrics
 */

#ifndef __CRAY_SAMPLER_BASE_H_
#define __CRAY_SAMPLER_BASE_H_

#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <ctype.h>
#include <wordexp.h>
#include "config.h"
//have it for the logfile and set
#include "ldmsd.h"
#include "ldms.h"


/**
 * The intent is that the various cray network sampler files handle whatever has
 * to be handled for the particular transports (gemini vs aries) AND
 * you can build different versions of the network sampler at the same time
 * (e.g., both gemini_r and gemini_d).
 *
 * Everything else is handled here in the "generic" functions that have a
 * big switch statement. All those cases are handled within the switch statement
 * either by a simple interface or a particular interface. The former are calls
 * called "simple" which are static to cray_sampler_base.c. The latter are
 * particular calls which are defined in separate files (e.g, lustre).
 *
 * Right now, several calls which are generally simple, but might have some
 * particul-osity (e.g., sample function might be particular) are grouped
 * together in "general_metrics" but could be split out into their own files.
 *
 * Anything can be defined here with a default and bypassed in the cray network
 * sampler files.
 *
 * Specifically then:
 * 1) the transport related cases, linksmetrics and nicmetrics, have no defaults
 * (other than do nothing) and will have to be handled in the network base
 * sampler files.
 * 2) other data sources which are handled or not in different transport
 * cases, in particular, energy and nettopo, have defaults and may be
 * bypassed in the sampler.
 * 3) other non-network related data sources, in particular existence of
 * cray_nvidia as specified by the build, is handled in this related c file.
 */

#define CSS_STRWRAP(NAME) #NAME
#define CSS_NSWRAP(NAME) NS_ ## NAME
#define CSS_NS(WRAP) \
       WRAP (NETTOPO),      \
       WRAP (LINKSMETRICS), \
       WRAP (NICMETRICS), \
       WRAP (ENERGY), \
       WRAP (LUSTRE), \
       WRAP (VMSTAT), \
       WRAP (LOADAVG), \
       WRAP (CURRENT_FREEMEM), \
       WRAP (KGNILND),    \
       WRAP (PROCNETDEV), \
       WRAP (NVIDIA), \
       WRAP (NUM)

typedef enum cray_system_sampler_sources {
       CSS_NS(CSS_NSWRAP)
} cray_system_sampler_sources_t;

extern const char *ns_names[];

void set_cray_sampler_log(ovis_log_t pi_log);
int set_offns_generic(cray_system_sampler_sources_t i);
int get_offns_generic(cray_system_sampler_sources_t i);
int config_generic(struct attr_value_list *kwl,
		   struct attr_value_list *avl);
int add_metrics_generic(ldms_schema_t schema,
		       cray_system_sampler_sources_t source_id);
int sample_metrics_generic(ldms_set_t set,
			   cray_system_sampler_sources_t source_id);

#endif
