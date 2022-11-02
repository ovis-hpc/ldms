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

#ifndef __GENERAL_METRICS_H_
#define __GENERAL_METRICS_H_

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
//have it for the logfile and set
#include "ldmsd.h"
#include "ldms.h"

//TODO: move cases into individual files. move vars into the c files.
#define VMSTAT_FILE "/proc/vmstat"
#define LOADAVG_FILE "/proc/loadavg"
#define CURRENT_FREEMEM_FILE "/proc/current_freemem"
#define KGNILND_FILE  "/proc/kgnilnd/stats"
#define PROCNETDEV_FILE "/proc/net/dev"

/* ENERGY Specific */
static char* ENERGY_METRICS[] = {"energy(J)",
				 "freshness",
				 "power(W)",
				 "power_cap(W)"};
static char* ENERGY_FILES[] = {"/sys/cray/pm_counters/energy",
			       "/sys/cray/pm_counters/freshness",
			       "/sys/cray/pm_counters/power",
			       "/sys/cray/pm_counters/power_cap"};
#define NUM_ENERGY_METRICS (sizeof(ENERGY_METRICS)/sizeof(ENERGY_METRICS[0]))
extern FILE* ene_f[NUM_ENERGY_METRICS];
extern int* metric_table_energy;

/* CURRENT_FREEMEM Specific */
extern FILE *cf_f;
extern int cf_m;
static char* CURRENT_FREEMEM_METRICS[] = {"current_freemem"};
#define NUM_CURRENT_FREEMEM_METRICS (sizeof(CURRENT_FREEMEM_METRICS)/sizeof(CURRENT_FREEMEM_METRICS[0]))
extern int* metric_table_current_freemem;
extern int (*sample_metrics_cf_ptr)(ldms_set_t set);

/* VMSTAT Specific */
extern FILE *v_f;
static char* VMSTAT_METRICS[] = {"nr_dirty", "nr_writeback"};
#define NUM_VMSTAT_METRICS (sizeof(VMSTAT_METRICS)/sizeof(VMSTAT_METRICS[0]))
extern int* metric_table_vmstat;
/* additional vmstat metrics if getting cf from vmstat. Order matters (see calc within) */
static char* VMCF_METRICS[] = {"nr_free_pages", "nr_file_pages", "nr_slab_reclaimable", "nr_shmem"};
#define NUM_VMCF_METRICS (sizeof(VMCF_METRICS)/sizeof(VMCF_METRICS[0]))
extern int (*sample_metrics_vmstat_ptr)(ldms_set_t set);


/* LOADAVG Specific */
extern FILE *l_f;
static char* LOADAVG_METRICS[] = {"loadavg_latest(x100)",
				  "loadavg_5min(x100)",
				  "loadavg_running_processes",
				  "loadavg_total_processes"};
#define NUM_LOADAVG_METRICS (sizeof(LOADAVG_METRICS)/sizeof(LOADAVG_METRICS[0]))
extern int *metric_table_loadavg;

/* PROCNETDEV Specific (Specific interface and indicies supported)*/
extern FILE *pnd_f;
static char* iface ="ipogif0";
extern int idx_iface;
static char* PROCNETDEV_METRICS[] = {"ipogif0_rx_bytes",
				     "ipogif0_tx_bytes"};
#define NUM_PROCNETDEV_METRICS (sizeof(PROCNETDEV_METRICS)/sizeof(PROCNETDEV_METRICS[0]))

extern int *metric_table_procnetdev;
extern int procnetdev_valid;


/* KGNILND Specific */
extern FILE *k_f;
static char* KGNILND_METRICS[] = {"SMSG_ntx",
				  "SMSG_tx_bytes",
				  "SMSG_nrx",
				  "SMSG_rx_bytes",
				  "RDMA_ntx",
				  "RDMA_tx_bytes",
				  "RDMA_nrx",
				  "RDMA_rx_bytes"
};
#define NUM_KGNILND_METRICS (sizeof(KGNILND_METRICS)/sizeof(KGNILND_METRICS[0]))
extern int* metric_table_kgnilnd;


/** helpers */
extern int procnetdev_setup();

/** sample */
int sample_metrics_vmstat(ldms_set_t set);
int sample_metrics_vmcf(ldms_set_t set);
int sample_metrics_kgnilnd(ldms_set_t set);
int sample_metrics_current_freemem(ldms_set_t set);
int sample_metrics_energy(ldms_set_t set);
int sample_metrics_loadavg(ldms_set_t set);
int sample_metrics_procnetdev(ldms_set_t set);

#endif
