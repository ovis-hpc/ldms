/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014 Sandia Corporation. All rights reserved.
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
 * \file nvidia_metrics.h nvidia gpu metrics using nvml
 */

#ifndef __NVIDIA_METRICS_H_
#define __NVIDIA_METRICS_H_

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
#include "ldms.h"
//What to do about having nvml.h ? Should we have zero'ed metrics even w/o nvml.h?
#include "nvml.h"

#define NVIDIA_MAX_DEVICES 4
#define NVIDIA_MAX_METRIC_NAME_SIZE (NVML_DEVICE_NAME_BUFFER_SIZE+40)
unsigned int nvidia_device_count;
char nvidia_device_names[NVIDIA_MAX_DEVICES][NVML_DEVICE_NAME_BUFFER_SIZE];
nvmlPciInfo_t nvidia_pci[NVIDIA_MAX_DEVICES];
nvmlDevice_t nvidia_device[NVIDIA_MAX_DEVICES];

static char* NVIDIA_METRICS[] = {"gpu_power_usage",
				 "gpu_power_limit",
				 "gpu_pstate",
				 "gpu_temp",
				 "gpu_memory_used",
				 "gpu_agg_dbl_ecc_register_file",
				 "gpu_agg_dbl_ecc_l1_cache",
				 "gpu_agg_dbl_ecc_l2_cache",
				 "gpu_agg_dbl_ecc_total_errors",
				 "gpu_util_rate"};

#define NUM_NVIDIA_METRICS (sizeof(NVIDIA_METRICS)/sizeof(NVIDIA_METRICS[0]))
ldms_metric_t* metric_table_nvidia;
int nvidia_valid;


int get_metric_size_nvidia(size_t *m_sz, size_t *d_sz,
			   ldmsd_msg_log_f msglog);
int add_metrics_nvidia(ldms_set_t set, int comp_id,
			      ldmsd_msg_log_f msglog);
int nvidia_setup(ldmsd_msg_log_f msglog);
int nvidia_shutdown(ldmsd_msg_log_f msglog);
int sample_metrics_nvidia(ldmsd_msg_log_f msglog);


#endif
