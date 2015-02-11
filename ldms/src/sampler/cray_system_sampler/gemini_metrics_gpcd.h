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
#include "gpcd_util.h"


/* config */
int hsn_metrics_config(int i, char* filename);

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
