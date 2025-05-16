/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020-2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2021, Advanced Micro Devices, Inc. All rights reserved.
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

#ifndef RDC_DATA_H
#define RDC_DATA_H

#include "ldms.h"
#include "ldmsd.h"
#include "ovis_json/ovis_json.h"
#include "sampler_base.h"
/* rdc/rdc.h in at least ROCm 6.2.1 forget to include <assert.h>, so we
 * include it here for them before including <rdc/rdc.h> */
#include <assert.h>
#include <rdc/rdc.h>

#define SAMP "rdc_sampler"
#define RDCINFO_INST(dummy) (singleton)
#define INST_LOG(inst, lvl, fmt, ...) do { \
	ovis_log(inst->mylog, (lvl), fmt, ##__VA_ARGS__); \
} while (0)
#define SCHEMA_HAVE_UNITS 0
#define MAX_SCHEMA_BASE 32

/* TYPES and constants */
typedef struct rdcinfo_inst_s *rdcinfo_inst_t;

struct rdcinfo_inst_s {
	ovis_log_t mylog;
	pthread_mutex_t lock;
	/* everything below here is 'private' and should only be used in rdcinfo.c */
	base_data_t base;
	char schema_name_base[MAX_SCHEMA_BASE];
	char *schema_name;
	int first_index; /* beginning of plugin data */
	int metric_offset; /* beginning of non-meta data */

	/* Extend plugin-specific data here */
	struct timespec rdc_start;
	uint32_t meta_done;
	rdc_field_t field_ids[RDC_MAX_FIELD_IDS_PER_FIELD_GROUP];
	ldms_set_t devset[RDC_MAX_NUM_DEVICES];
	uint32_t num_sets;
	uint32_t num_fields;
	uint32_t warmup;
	uint32_t update_freq;
	uint32_t max_keep_age;
	uint32_t max_keep_samples;
	rdc_handle_t rdc_handle;
	rdc_gpu_group_t group_id;
	rdc_field_grp_t field_group_id;
	rdc_group_info_t group_info;
	rdc_field_group_info_t field_info;
};

/* get the help string. caller must free it. */
char * rdcinfo_usage();

// create unconfigured instance
rdcinfo_inst_t rdcinfo_new(ovis_log_t mylog);

// clear all configuration except log and lock. caller must hold inst->lock if multithreaded.
void rdcinfo_reset(rdcinfo_inst_t);

// free inst data. inst must be unlocked and reset.
void rdcinfo_delete(rdcinfo_inst_t inst);

// set configuration. If already configured,
// call reset first. caller must be holding inst->lock if multithreaded
int rdcinfo_config(rdcinfo_inst_t inst, struct attr_value_list *avl);

// caller must be holding inst->lock
int rdcinfo_sample(rdcinfo_inst_t inst);

#endif /* RDC_DATA_H */
