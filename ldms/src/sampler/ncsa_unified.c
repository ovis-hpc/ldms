/* -*- c-basic-offset: 8 -*-
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
 * \file ncsa_unified.c
 * \brief unified custom data provider for ncsa interested metrics. Combo of
 * metrics from other samplers.
 */

/**
 * Sub-sampler Notes:
 * GEM_LINK_PERF: Utilizes the aggregation methology of Kevin Pedretti, SNL.
 */

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
#include <time.h>
#include <wordexp.h>

#include "lustre/lustre_sampler.h"
#include "gem_link_perf_util.h"

/* Constants */
#define FALSE 0
#define TRUE 1

typedef enum { NS_VMSTAT,
	       NS_LOADAVG,
	       NS_CURRENT_FREEMEM,
	       NS_KGNILND,
	       NS_LUSTRE,
	       NS_GEM_LINK_PERF,
	       NS_NIC_PERF,
	       NS_NUM } nsca_sources_t;

#define VMSTAT_FILE "/proc/vmstat"
#define LOADAVG_FILE "/proc/loadavg"
#define CURRENT_FREEMEM_FILE "/proc/current_freemem"
#define KGNILND_FILE  "/proc/kgnilnd/stats"

/* CURRENT_FREEMEM Specific */
FILE *cf_f;
static char* CURRENT_FREEMEM_METRICS[] = {"current_freemem"};
int num_current_freemem_metrics = 1;
ldms_metric_t* metric_table_current_freemem;

/* VMSTAT Specific */
FILE *v_f;
static char* VMSTAT_METRICS[] = {"nr_dirty", "nr_writeback"};
int num_vmstat_metrics = 2;
ldms_metric_t* metric_table_vmstat;

/* LOADAVG Specific */
FILE *l_f;
static char* LOADAVG_METRICS[] = {"loadavg_latest(*100)",
				  "loadavg_5min(*100)",
				  "loadavg_running_processes",
				  "loadavg_total_processes"};
int num_loadavg_metrics = 4;
ldms_metric_t *metric_table_loadavg;

/* KGNILND Specific */
FILE *k_f;
static char* KGNILND_METRICS[] = {"SMSG_ntx",
				  "SMSG_tx_bytes",
				  "SMSG_nrx",
				  "SMSG_rx_bytes",
				  "RDMA_ntx",
				  "RDMA_tx_bytes",
				  "RDMA_nrx",
				  "RDMA_rx_bytes"
};
int num_kgnilnd_metrics = 8;
ldms_metric_t* metric_table_kgnilnd;

/* LUSTRE Specific */
/**
 * This is for single llite.
 * The real metrics will contain all llites.
 */
static char *LUSTRE_METRICS[] = {
	/* file operation */
	"dirty_pages_hits",
	"dirty_pages_misses",
	"writeback_from_writepage",
	"writeback_from_pressure",
	"writeback_ok_pages",
	"writeback_failed_pages",
	"read_bytes",
	"write_bytes",
	"brw_read",
	"brw_write",
	"ioctl",
	"open",
	"close",
	"mmap",
	"seek",
	"fsync",
	/* inode operation */
	"setattr",
	"truncate",
	"lockless_truncate",
	"flock",
	"getattr",
	/* special inode operation */
	"statfs",
	"alloc_inode",
	"setxattr",
	"getxattr",
	"listxattr",
	"removexattr",
	"inode_permission",
	"direct_read",
	"direct_write",
	"lockless_read_bytes",
	"lockless_write_bytes",
};
#define LUSTRE_METRICS_LEN (sizeof(LUSTRE_METRICS)/sizeof(LUSTRE_METRICS[0]))
#define LLITE_PREFIX "/proc/fs/lustre/llite"

/* Lustre specific vars */
/**
 * str<->idx in LUSTRE_METRICS.
 */
struct str_map *lustre_idx_map = NULL;

struct lustre_svc_stats_head lustre_svc_head = {0};

struct str_list_head llite_str_list = {0};

/* NIC_PERF Specific */
#define PREFIX_ENUM_M(NAME) M_ ## NAME

#define NIC_PERF_METRIC_LIST(WRAP) \
	WRAP(GM_ORB_PERF_STALLED), \
		WRAP(GM_NPT_PERF_NPT_BLOCKED_CNTR), \
		WRAP(GM_NPT_PERF_NPT_STALLED_CNTR), \
		WRAP(GM_RAT_PERF_HEADER_BYTES_VC0), \
		WRAP(GM_RAT_PERF_DATA_BYTES_VC0), \
		WRAP(GM_ORB_BW), \
		WRAP(GM_NPT_BW), \
		WRAP(GM_ORB_PACKETSIZE_AVE), \
		WRAP(GM_NPT_PACKETSIZE_AVE)

static char* nic_perf_metric_name[] = {
	NIC_PERF_METRIC_LIST(STR_WRAP)
};

typedef enum {
	NIC_PERF_METRIC_LIST(PREFIX_ENUM_M)
} nic_perf_metric_t;


#define NUM_NIC_PERF_METRICS 9

uint64_t ns_nic_diff[NUM_NIC_PERF_RAW];
gpcd_context_t *ns_nic_curr_ctx;
gpcd_context_t *ns_nic_prev_ctx;
gpcd_context_t *ns_nic_int_ctx;
gpcd_mmr_list_t *ns_nic_listp;
gpcd_mmr_list_t *ns_nic_plistp;
ldms_metric_t ns_nic_metric_table[NUM_NIC_PERF_METRICS];
struct timespec ns_nic_time1, ns_nic_time2;
struct timespec *ns_nic_curr_time, *ns_nic_prev_time, *ns_nic_int_time;
int ns_nic_valid = 0;


/* GEM_LINK_PERF_SPECIFIC */
ldms_metric_t ns_gemlink_meshcoord_metric_handle[NETTOPODIM];

gpcd_context_t *ns_gemlink_curr_ctx;
gpcd_context_t *ns_gemlink_prev_ctx;
gpcd_context_t *ns_gemlink_int_ctx;
gpcd_mmr_list_t *ns_gemlink_listp;
gpcd_mmr_list_t *ns_gemlink_plistp;
ldms_metric_t* ns_gemlink_metric_table;
int num_gem_link_perf_metrics = 0;
int ns_gemlink_valid = 0;

gemini_state_t *ns_gemlink_state;
gemini_coord_t ns_gemlink_my_coord;
char *ns_gemlink_rtrfile = NULL;
int ns_gemlink_rc_to_tid[GEMINI_NUM_TILE_ROWS][GEMINI_NUM_TILE_COLUMNS];
struct timespec ns_gemlink_time1, ns_gemlink_time2;
struct timespec *ns_gemlink_curr_time, *ns_gemlink_prev_time,
	*ns_gemlink_int_time;
uint64_t ns_gemlink_diff;

static double ns_gemlink_max_link_bw[GEMINI_NUM_LOGICAL_LINKS];
static int ns_gemlink_tiles_per_dir[GEMINI_NUM_LOGICAL_LINKS];

/* General vars */
ldms_set_t set;
ldmsd_msg_log_f msglog;
uint64_t comp_id;

char *replace_space(char *s)
{
	char *s1;

	s1 = s;
	while ( *s1 ) {
		if ( isspace( *s1 ) ) {
			*s1 = '_';
		}
		++s1;
	}
	return s;
}

static ldms_set_t get_set()
{
	return set;
}


int get_metric_size_nic_perf(size_t *m_sz, size_t *d_sz, int *mc)
{
	size_t msize = 0;
	size_t dsize = 0;
	size_t m, d;
	char newname[64];
	int count = 0;
	int i;
	int rc;

	for (i = 0; i < NUM_NIC_PERF_METRICS; i++){
		strcpy(newname, nic_perf_metric_name[i]);
		switch(i){
		case M_GM_ORB_PERF_STALLED:
		case M_GM_NPT_PERF_NPT_BLOCKED_CNTR:
		case M_GM_NPT_PERF_NPT_STALLED_CNTR:
		case M_GM_RAT_PERF_HEADER_BYTES_VC0:
		case M_GM_RAT_PERF_DATA_BYTES_VC0:
			rc = ldms_get_metric_size(newname, LDMS_V_U64, &m, &d);
			break;
		case M_GM_ORB_BW:
		case M_GM_NPT_BW:
			strcat(newname, " (B/s)");
			rc = ldms_get_metric_size(newname, LDMS_V_U64, &m, &d);
			break;
		case M_GM_ORB_PACKETSIZE_AVE:
		case M_GM_NPT_PACKETSIZE_AVE:
			strcat(newname, " (B)");
			rc = ldms_get_metric_size(newname, LDMS_V_U64, &m, &d);
			break;
		default:
			msglog("nic_perf: error in get_metric_size\n");
			return EINVAL;
		}

		if (rc)
			return rc;

		msize +=m;
		dsize +=d;
		count++;
	}

	*m_sz = msize;
	*d_sz = dsize;
	*mc = count;
	return 0;
}


int get_metric_size_gem_link_perf(size_t *m_sz, size_t *d_sz, int *mc)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int metric_count;
	char newname[64];
	int i, j;
	int rc;

	tot_meta_sz = 0;
	tot_data_sz = 0;
	metric_count = 0;

	for (i = 0; i < NETTOPODIM; i++){
		/* will want this to be LDMS_V_U8 */
		rc = ldms_get_metric_size(nettopo_meshcoord_metricname[i],
					  LDMS_V_U64, &meta_sz,
					  &data_sz);
		tot_meta_sz += meta_sz;
		tot_data_sz += data_sz;
		metric_count++;
	}

	/*  Process derived metrics */
	for (i = 0; i < GEMINI_NUM_LOGICAL_LINKS; i++) {
		for (j = 0; j < GEMINI_NUM_LINK_STATS; j++) {
			strncpy(newname, ns_gemlink_gemctrdir[i], 2);
			newname[2] = '_';
			newname[3] = '\0';
			strcat(newname, ns_gemlink_gemstatsname[j]);
			switch (j) {
			case SAMPLE_GEMINI_TCTR_LINK_BW:
				strcat(newname, " (B/s)");
				/* will want this to be LDMS_V_LD */
				rc = ldms_get_metric_size(newname,
							  LDMS_V_U64, &meta_sz,
							  &data_sz);
				break;
			case SAMPLE_GEMINI_TCTR_LINK_USED_BW:
				strcat(newname, " (% * 1000))");
				/* will want this to be LDMS_V_LD */
				rc = ldms_get_metric_size(newname,
							  LDMS_V_U64, &meta_sz,
							  &data_sz);
				break;
			case SAMPLE_GEMINI_TCTR_LINK_PACKETSIZE_AVE:
				strcat(newname, " (B)");
				/* will want this to be LDMS_V_LD */
				rc = ldms_get_metric_size(newname,
							  LDMS_V_U64, &meta_sz,
							  &data_sz);
				break;
			case SAMPLE_GEMINI_TCTR_LINK_INPUT_STALLS:
			case SAMPLE_GEMINI_TCTR_LINK_OUTPUT_STALLS:
				/* will want this to be LDMS_V_LD */
				strcat(newname, " (% * 1000)");
				rc = ldms_get_metric_size(newname,
							  LDMS_V_U64, &meta_sz,
							  &data_sz);
				break;
			default:
				msglog("gem_link_perf: error in"
				       " get_metric_size\n");
				return EINVAL;
			}

			if (rc) {
				msglog("gem_link_perf: error in"
				       " get_metric_size\n");
				return rc;
			}
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
			metric_count++;
		}
	}

	num_gem_link_perf_metrics = metric_count;
	ns_gemlink_metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!ns_gemlink_metric_table)
		return ENOMEM;

	*m_sz = tot_meta_sz;
	*d_sz = tot_data_sz;
	*mc = metric_count;
	return 0;
}

int get_metric_size_lustre(size_t *m_sz, size_t *d_sz, int *mc)
{
	struct lustre_svc_stats *lss;
	size_t msize = 0;
	size_t dsize = 0;
	size_t m, d;
	int count = 0;
	char name[1024];
	int i;
	int rc;
	LIST_FOREACH(lss, &lustre_svc_head, link) {
		for (i=0; i<LUSTRE_METRICS_LEN; i++) {
			sprintf(name, "%s.stats.%s", lss->name,
							LUSTRE_METRICS[i]);
			rc = ldms_get_metric_size(name, LDMS_V_U64, &m, &d);
			if (rc)
				return rc;
			msize += m;
			dsize += d;
			count++;
		}
	}
	*m_sz = msize;
	*d_sz = dsize;
	*mc = count;
	return 0;
}


static int get_metric_size_generic(size_t *m_sz, size_t *d_sz, int* mc,
						nsca_sources_t source_id)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	char** metric_names;
	int num_metrics;
	int i, rc;
	int metric_count;

	tot_data_sz = 0;
	tot_meta_sz = 0;
	metric_count = 0;


	switch (source_id){
	case NS_VMSTAT:
		metric_names = VMSTAT_METRICS;
		num_metrics = num_vmstat_metrics;
		break;
	case NS_LOADAVG:
		metric_names = LOADAVG_METRICS;
		num_metrics = num_loadavg_metrics;
		break;
	case NS_CURRENT_FREEMEM:
		metric_names = CURRENT_FREEMEM_METRICS;
		num_metrics = num_current_freemem_metrics;
		break;
	case NS_KGNILND:
		metric_names = KGNILND_METRICS;
		num_metrics = num_kgnilnd_metrics;
		break;
	case NS_LUSTRE:
		return get_metric_size_lustre(m_sz, d_sz, mc);
		break;
	case NS_NIC_PERF:
		return get_metric_size_nic_perf(m_sz, d_sz, mc);
		break;
	case NS_GEM_LINK_PERF:
		return get_metric_size_gem_link_perf(m_sz, d_sz, mc);
		break;
	default:
		metric_names = NULL;
		num_metrics = 0;
		break;
	}

	for (i = 0; i < num_metrics; i++){
		rc = ldms_get_metric_size(metric_names[i], LDMS_V_U64,
							&meta_sz, &data_sz);
		if (rc)
			return rc;
		tot_meta_sz+= meta_sz;
		tot_data_sz+= data_sz;
		metric_count++;
	}

	*m_sz = tot_meta_sz;
	*d_sz = tot_data_sz;
	*mc = metric_count;

	return 0;
}

static int add_metrics_lustre(int curr_metric_no, int comp_id, int *mc)
{
	struct lustre_svc_stats *lss;
	int i;
	int count = 0;
	char name[1024];
	LIST_FOREACH(lss, &lustre_svc_head, link) {
		for (i=0; i<LUSTRE_METRICS_LEN; i++) {
			sprintf(name, "%s.stats.%s", lss->name,
					LUSTRE_METRICS[i]);
			ldms_metric_t m = ldms_add_metric(set, name,
								LDMS_V_U64);
			if (!m)
				return ENOMEM;
			lss->metrics[i+1] = m;
			ldms_set_user_data(m, comp_id);
			count++;
		}
	}
	*mc = count;
	return 0;
}

static int add_metrics_nic_perf(int curr_metric_no, int comp_id, int *mc)
{
	int i;
	char newname[64];
	int rc;

	for (i = 0; i < NUM_NIC_PERF_METRICS; i++){
		strcpy(newname, nic_perf_metric_name[i]);
		switch(i){
		case M_GM_ORB_PERF_STALLED:
		case M_GM_NPT_PERF_NPT_BLOCKED_CNTR:
		case M_GM_NPT_PERF_NPT_STALLED_CNTR:
		case M_GM_RAT_PERF_HEADER_BYTES_VC0:
		case M_GM_RAT_PERF_DATA_BYTES_VC0:
			ns_nic_metric_table[i] =
				ldms_add_metric(set, newname, LDMS_V_U64);
			break;
		case M_GM_ORB_BW:
		case M_GM_NPT_BW:
			strcat(newname, " (B/s)");
			ns_nic_metric_table[i] =
				ldms_add_metric(set, newname, LDMS_V_U64);
			break;
		case M_GM_ORB_PACKETSIZE_AVE:
		case M_GM_NPT_PACKETSIZE_AVE:
			strcat(newname, " (B)");
			ns_nic_metric_table[i] =
				ldms_add_metric(set, newname, LDMS_V_U64);
			break;
		default:
			msglog("nic_perf: error in add_metrics\n");
			return EINVAL;
		}

		if (!ns_nic_metric_table[i])
			return ENOMEM;
		ldms_set_user_data(ns_nic_metric_table[i], comp_id);
	}

	return 0;
}

static int nic_perf_setup(){

	int error;

	ns_nic_curr_ctx = nic_perf_create_context(&msglog);
	if (!ns_nic_curr_ctx) {
		msglog("ns_nic: gpcd_create_context failed");
		ns_nic_valid = 0;
		return EINVAL;
	}


	ns_nic_prev_ctx = nic_perf_create_context(&msglog);
	if (!ns_nic_prev_ctx) {
		msglog("ns_nic: gpcd_create_context failed");
		ns_nic_valid = 0;
		return EINVAL;
	}


	ns_nic_prev_time = &ns_nic_time1;
	ns_nic_curr_time = &ns_nic_time2;

	clock_gettime(CLOCK_REALTIME, ns_nic_prev_time);
	error = gpcd_context_read_mmr_vals(ns_nic_prev_ctx);

	if (error) {
		msglog("nic_perf: Error in gpcd_context_read_mmr_vals\n");
		ns_nic_valid = 0;
		return EINVAL;
	}

	ns_nic_plistp = ns_nic_prev_ctx->list;
	ns_nic_valid = 1;

	return 0;
}


static int add_metrics_gem_link_perf(int curr_metric_no, int comp_id, int *mc)
{

	char newname[64];
	int metric_no;
	int i, j;
	int rc;

	/* will want this to be LDMS_V_U8 */
	for (i = 0; i < NETTOPODIM; i++) {
		ns_gemlink_meshcoord_metric_handle[i] =
			ldms_add_metric(set, nettopo_meshcoord_metricname[i],
					LDMS_V_U64);
		if (!ns_gemlink_meshcoord_metric_handle[i]) {
			msglog("gem_link_perf: Cannot create"
			       " meshcoord metric handle\n");
			goto err;
		}
		ldms_set_user_data(ns_gemlink_meshcoord_metric_handle[i],
				   comp_id);
	}

	/*  Process derived metrics */
	metric_no = 0;
	for (i=0; i<GEMINI_NUM_LOGICAL_LINKS; i++) {
		for (j=0; j<GEMINI_NUM_LINK_STATS; j++) {
			strncpy(newname, ns_gemlink_gemctrdir[i], 2);
			newname[2] = '_';
			newname[3] = '\0';
			strcat(newname, ns_gemlink_gemstatsname[j]);

			switch (j){
			case SAMPLE_GEMINI_TCTR_LINK_BW:
				strcat(newname, " (B/s)");
				ns_gemlink_metric_table[metric_no] =
					ldms_add_metric(set, newname,
							LDMS_V_U64);
				/* will want this to be LDMS_V_LD */
				break;
			case SAMPLE_GEMINI_TCTR_LINK_USED_BW:
				strcat(newname, " (% * 1000))");
				ns_gemlink_metric_table[metric_no] =
					ldms_add_metric(set, newname,
							LDMS_V_U64);
				/* will want this to be LDMS_V_LD */
				break;
			case SAMPLE_GEMINI_TCTR_LINK_PACKETSIZE_AVE:
				strcat(newname, " (B)");
				ns_gemlink_metric_table[metric_no] =
					ldms_add_metric(set, newname,
							LDMS_V_U64);
				/* will want this to be LDMS_V_LD */
				break;
			case SAMPLE_GEMINI_TCTR_LINK_INPUT_STALLS:
			case SAMPLE_GEMINI_TCTR_LINK_OUTPUT_STALLS:
				strcat(newname, " (% * 1000)");
				ns_gemlink_metric_table[metric_no] =
					ldms_add_metric(set, newname,
							LDMS_V_U64);
				break;
			default:
				msglog("gem_link_perf: error in"
				       " add_metrics\n");
				return EINVAL;
			}

			if (!ns_gemlink_metric_table[metric_no]) {
				msglog("gem_link_perf: metricname = "
						"'%s' metric_no = %d error in"
				       " add_metric\n", newname,
				       metric_no);
				rc = ENOMEM;
				return rc;
			}
			/* XXX comp_id */
			ldms_set_user_data(ns_gemlink_metric_table[metric_no],
					   comp_id);
			metric_no++;
		}
	}

 err:
	return rc;
}

int gem_link_perf_setup(){

	int error;
	int rc;
	int i, j;

	if (ns_gemlink_rtrfile == NULL){
		ns_gemlink_valid = 0;
		return EINVAL;
	}

	ns_gemlink_curr_ctx = gem_link_perf_create_context(&msglog);
	if (!ns_gemlink_curr_ctx) {
		msglog("ns_gemlink: gpcd_create_context failed");
		ns_gemlink_valid = 0;
		return EINVAL;
	}

	ns_gemlink_prev_ctx = gem_link_perf_create_context(&msglog);
	if (!ns_gemlink_prev_ctx) {
		msglog("ns_nic: gpcd_create_context failed");
		ns_gemlink_valid = 0;
		return EINVAL;
	}

	/*  Allocate memory for struct state */
	ns_gemlink_state = malloc(sizeof(gemini_state_t));
	if (!ns_gemlink_state) {
		msglog("gem_link_perf: Error allocating memory for state\n");
		ns_gemlink_valid = 0;
		return ENOMEM;
	}

	/*  Figure out our tile info and connectivity to neighboring geminis */
	rc = gem_link_perf_parse_interconnect_file(&msglog,
						   ns_gemlink_rtrfile,
						   ns_gemlink_state->neighbor,
						   ns_gemlink_state->tile,
						   &ns_gemlink_my_coord,
						   &ns_gemlink_max_link_bw,
						   &ns_gemlink_tiles_per_dir);
	if (rc != 0){
		msglog("gem_link_perf: Error parsing interconnect file\n");
		ns_gemlink_valid = 0;
		return rc;
	}

	/*  Fill in rc_to_tid array */
	for (i=0; i<GEMINI_NUM_TILE_ROWS; i++) {
		for (j=0; j<GEMINI_NUM_TILE_COLUMNS; j++) {
			error = tcoord_to_tid(i, j,
					      &(ns_gemlink_rc_to_tid[i][j]));
			if (error) {
				msglog("gem_link_perf: Error converting r,c to"
				       " tid\n");
				ns_gemlink_valid = 0;
				return error;
			}
		}
	}

	ns_gemlink_prev_time = &ns_gemlink_time1;
	ns_gemlink_curr_time = &ns_gemlink_time2;

	clock_gettime(CLOCK_REALTIME, ns_gemlink_prev_time);
	error = gpcd_context_read_mmr_vals(ns_gemlink_prev_ctx);

	if (error) {
		msglog("gemlink_perf: Error in gpcd_context_read_mmr_vals\n");
		ns_gemlink_valid = 0;
		return EINVAL;
	}

	ns_gemlink_plistp = ns_gemlink_prev_ctx->list;
	ns_gemlink_valid = 1;

	return 0;
}

static int add_metrics_generic(int curr_metric_no, int comp_id, int* mc,
		nsca_sources_t source_id)
{
	char **metric_names;
	int num_metrics;
	ldms_metric_t** metric_table;
	char (*fname)[];
	FILE** g_f;
	int i, rc;
	int metric_no;

	switch (source_id){
	case NS_VMSTAT:
		metric_names = VMSTAT_METRICS;
		num_metrics = num_vmstat_metrics;
		fname = &VMSTAT_FILE;
		metric_table = &metric_table_vmstat;
		g_f = &v_f;
		break;
	case NS_LOADAVG:
		metric_names = LOADAVG_METRICS;
		num_metrics = num_loadavg_metrics;
		metric_table = &metric_table_loadavg;
		fname = &LOADAVG_FILE;
		g_f = &l_f;
		break;
	case NS_CURRENT_FREEMEM:
		metric_names = CURRENT_FREEMEM_METRICS;
		num_metrics = num_current_freemem_metrics;
		metric_table = &metric_table_current_freemem;
		fname = &CURRENT_FREEMEM_FILE;
		g_f = &cf_f;
		break;
	case NS_KGNILND:
		metric_names = KGNILND_METRICS;
		num_metrics = num_kgnilnd_metrics;
		metric_table = &metric_table_kgnilnd;
		fname = &KGNILND_FILE;
		g_f = &k_f;
		break;
	case NS_LUSTRE:
		return add_metrics_lustre(curr_metric_no, comp_id, mc);
		break;
	case NS_NIC_PERF:
		rc =  add_metrics_nic_perf(curr_metric_no, comp_id, mc);
		if (rc != 0)
			return rc;
		rc = nic_perf_setup();
		if (rc != 0) {
			msglog("ncsa_unified: setting nic_perf invalid\n");
			ns_nic_valid = 0; //OK to continue
		}
		return 0;
		break;
	case NS_GEM_LINK_PERF:
		rc = add_metrics_gem_link_perf(curr_metric_no, comp_id, mc);
		if (rc != 0)
			return rc;
		rc = gem_link_perf_setup();
		if (rc != 0) {
			msglog("ncsa_unified: setting gem_link_perf invalid\n");
			ns_gemlink_valid = 0; //OK to continue
		}
		return 0;
		break;
	default:
		metric_names = NULL;
		num_metrics = 0;
		metric_table = NULL;
		fname = NULL;
		g_f = NULL;
		break;
	}

	if (num_metrics == 0){
		*mc = 0;
		return 0;
	}

	*metric_table = calloc(num_metrics, sizeof(ldms_metric_t));
	if (! (*metric_table)){
		msglog("ncsa_unified: cannot calloc metric_table\n");
		return ENOMEM;
	}

	if (fname != NULL){
		*g_f = fopen(*fname, "r");
		if (!(*g_f)) {
			/* this is not an error */
			msglog("WARNING: Could not open the source file '%s'\n",
									*fname);
		}
	} else {
		*g_f = NULL;
	}

	for (i = 0; i < num_metrics; i++){

		(*metric_table)[i] = ldms_add_metric(set,
						metric_names[i], LDMS_V_U64);

		if (!(*metric_table)[i]){
			msglog("ncsa_unified: cannot add metric\n");
			rc = ENOMEM;
			return rc;
		}
		ldms_set_user_data((*metric_table)[i], comp_id);
	}

	*mc = num_metrics;

	return 0;
}

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, metric_count, tot_metric_count;
	uint64_t metric_value;
	char *s;
	char lbuf[256];
	char metric_name[128];
	int i;

	/*
	 * Determine the metric set size.
	 * Will create each metric in the set, even if the source does not exist
	 */

	tot_metric_count = 0;
	metric_count = 0;

	tot_data_sz = 0;
	tot_meta_sz = 0;

	for (i = 0; i < NS_NUM; i++){
		rc =  get_metric_size_generic(&meta_sz, &data_sz,
					      &metric_count, i);
		if (rc)
			return rc;
		tot_meta_sz += meta_sz;
		tot_data_sz += data_sz;
		tot_metric_count += metric_count;
	}

	/* Create the metric set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	/*
	 * Define all the metrics.
	 */
	rc = ENOMEM;
	int metric_no = 0;

	for (i = 0; i < NS_NUM; i++) {
		rc = add_metrics_generic(metric_no, comp_id, &metric_count, i);
		if (rc)
			goto err;
		metric_no += metric_count;
	}

	return 0;

 err:
	ldms_set_release(set);
	return rc;
}

int handle_llite(const char *llite)
{
	char *_llite = strdup(llite);
	if (!_llite)
		return ENOMEM;
	char *tok = strtok(_llite, ",");
	struct lustre_svc_stats *lss;
	char path[4096];
	while (tok) {
		sprintf(path, "/proc/fs/lustre/llite/%s-*/stats",tok);
		lss = lustre_svc_stats_alloc(path, LUSTRE_METRICS_LEN+1);
		lss->name = strdup(tok);
		if (!lss->name)
			goto err;
		lss->key_id_map = lustre_idx_map;
		LIST_INSERT_HEAD(&lustre_svc_head, lss, link);
		tok = strtok(NULL, ",");
	}
	return 0;
err:
	lustre_svc_stats_list_free(&lustre_svc_head);
	return ENOMEM;
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc = 0;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtol(value, NULL, 0);

	value = av_value(avl, "llite");
	if (value) {
		rc = handle_llite(value);
		if (rc)
			goto out;
	} else {
		rc = EINVAL;
		goto out;
	}

	value = av_value(avl, "rtrfile");
	if (value) {
		ns_gemlink_rtrfile = strdup(value);
	}

	value = av_value(avl, "set");
	if (value)
		rc = create_metric_set(value);

out:
	return rc;
}


static int sample_metrics_vmstat(int curr_metric_no, int* mc)
{
	char lbuf[256];
	char metric_name[128];
	int found_metrics;
	char* s;
	union ldms_value v;
	int j, rc;

	if (!v_f)
		return 0;

	found_metrics = 0;

	fseek(v_f, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), v_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &v.v_u64);
		if (rc != 2) {
			msglog("ERR: Issue reading the source file '%s'\n",
								VMSTAT_FILE);
			rc = EINVAL;
			return rc;
		}
		for (j = 0; j < num_vmstat_metrics; j++){
			if (!strcmp(metric_name, VMSTAT_METRICS[j])){
				ldms_set_metric(metric_table_vmstat[j], &v);
				found_metrics++;
				break;
			}
		}
	} while (s);

	if (found_metrics != num_vmstat_metrics){
		return EINVAL;
	}

	*mc = num_vmstat_metrics;
	return 0;

}

static int sample_metrics_kgnilnd(int curr_metric_no, int* mc)
{
	char lbuf[256];
	char metric_name[128];
	int found_metrics;
	char* s;
	union ldms_value v;
	int j, rc;

	if (!k_f){
		/* No file, just skip the sampling. */
		return 0;
	}

	found_metrics = 0;

	fseek(k_f, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), k_f);
		if (!s)
			break;

		char* end = strchr(s, ':');
		if (!end) {
			rc = EINVAL;
			return rc;
		}
		if (!*end)
			continue;
		*end = '\0';
		replace_space(s);

		if (sscanf(s, "%s", metric_name) != 1){
			msglog("ERR: Issue reading metric name from the source"
						" file '%s'\n", VMSTAT_FILE);
			rc = EINVAL;
			return rc;
		}
		if (sscanf(end + 1, " %"PRIu64"\n", &v.v_u64) == 1 ) {
			for (j = 0; j < num_kgnilnd_metrics; j++){
				if (strcmp(metric_name, KGNILND_METRICS[j]))
					continue;
				ldms_set_metric(metric_table_kgnilnd[j], &v);
				found_metrics++;
				break;
			}
		}
	} while (s);

	if (found_metrics != num_kgnilnd_metrics){
		return EINVAL;
	}

	*mc = num_kgnilnd_metrics;
	return 0;

}

static int sample_metrics_current_freemem(int curr_metric_no, int* mc)
{
	/* only has 1 val, no label */
	char lbuf[256];
	char metric_name[128];
	int found_metrics;
	char* s;
	union ldms_value v;
	int j, rc;


	if (!cf_f)
		return 0;

	found_metrics = 0;
	fseek(cf_f, 0, SEEK_SET);
	s = fgets(lbuf, sizeof(lbuf), cf_f);
	if (s) {
		rc = sscanf(lbuf, "%"PRIu64"\n", &v.v_u64);
		if (rc != 1) {
			msglog("ERR: Issue reading the source file '%s'\n",
							CURRENT_FREEMEM_FILE);
			rc = EINVAL;
			return rc;
		}
		ldms_set_metric(metric_table_current_freemem[0], &v);
		found_metrics++;
	}

	if (found_metrics != num_current_freemem_metrics){
		return EINVAL;
	}

	*mc = num_current_freemem_metrics;
	return 0;

}

static int sample_metrics_loadavg(int curr_metric_no, int* mc)
{
	/* 0.12 0.98 0.86 1/345 24593. well known: want fields 1, 2, and both of
	 * 4 in that order.*/

	char lbuf[256];
	char metric_name[128];
	int found_metrics;
	char* s, junk;
	union ldms_value v[4];
	float vf[3];
	int vi[3];
	int i, j, rc;

	if (!l_f)
		return 0;

	found_metrics = 0;
	fseek(l_f, 0, SEEK_SET);
	s = fgets(lbuf, sizeof(lbuf), l_f);
	if (s) {
		rc = sscanf(lbuf, "%f %f %f %d/%d %d\n",
			    &vf[0], &vf[1], &vf[2], &vi[0], &vi[1], &vi[2]);
		if (rc != 6) {
			msglog("ERR: Issue reading the source file '%s'"
					" (rc=%d)\n", LOADAVG_FILE, rc);
			rc = EINVAL;
			return rc;
		}
		v[0].v_u64 = vf[0]*100;
		v[1].v_u64 = vf[1]*100;
		v[2].v_u64 = vi[0];
		v[3].v_u64 = vi[1];
		for (i = 0; i < 4; i++){
			ldms_set_metric(metric_table_loadavg[i], &v[i]);
		}
		found_metrics=4;
	}

	if (found_metrics != num_loadavg_metrics){
		return EINVAL;
	}

	*mc = num_loadavg_metrics;
	return 0;
}

static int sample_metrics_null(int curr_metric_no, int* mc,
			       nsca_sources_t source_id)
{
	/* This is for metrics you want in the enumeration,
	 * but dont want to sample */

	ldms_metric_t** metric_table = NULL;
	int num_metrics = 0;
	union ldms_value v;
	int i;

	switch (source_id){
	default:
		num_metrics = 0;
		break;
	}

	for (i = 0; i < num_metrics; i++){
		v.v_u64 = 0;
		ldms_set_metric(metric_table[i], &v);
	}

	*mc = num_metrics;

	return 0;
}

int  sample_metrics_lustre(int curr_metric_no, int* mc)
{
	struct lustre_svc_stats *lss;
	int rc;
	int count = 0;
	LIST_FOREACH(lss, &lustre_svc_head, link) {
		rc = lss_sample(lss);
		if (rc && rc != ENOENT)
			return rc;
		count += LUSTRE_METRICS_LEN;
	}
	return 0;
}

static inline
uint64_t get_m_gm_orb_bw(uint64_t time_delta){
	return (uint64_t) (
		3000000000 * ((double)(ns_nic_diff[R_GM_ORB_PERF_VC0_FLITS] +
				       ns_nic_diff[R_GM_ORB_PERF_VC1_FLITS]) /
			      (double)time_delta)
		);
}

static inline
uint64_t get_m_gm_orb_ave_bytes_per_pkt()
{
	return (uint64_t)(
		  3.0 * ((double)(ns_nic_diff[R_GM_ORB_PERF_VC0_FLITS] +
					ns_nic_diff[R_GM_ORB_PERF_VC1_FLITS]) /
			 (double)(ns_nic_diff[R_GM_ORB_PERF_VC0_PKTS] +
				  ns_nic_diff[R_GM_ORB_PERF_VC1_PKTS]))
		  );
}

static inline
uint64_t get_m_gm_npt_packetsize_ave()
{
	return  (uint64_t)(
		3.0 * (double)ns_nic_diff[R_GM_NPT_PERF_NPT_FLIT_CNTR] /
		(double)ns_nic_diff[R_GM_NPT_PERF_NPT_PKT_CNTR]
		);
}

static inline
uint64_t get_m_gm_npt_bw(uint64_t time_delta)
{
	return (uint64_t)(
		3000000000 * ((double)ns_nic_diff[R_GM_NPT_PERF_NPT_FLIT_CNTR] /
			      (double)time_delta)
		);
}

uint64_t __nic_perf_metric_calc(int metric, uint64_t time_delta)
{
	switch (metric){
	case M_GM_ORB_PERF_STALLED:
		return ns_nic_diff[R_GM_ORB_PERF_VC1_STALLED] +
			ns_nic_diff[R_GM_ORB_PERF_VC0_STALLED];
		break;
	case M_GM_ORB_PACKETSIZE_AVE:
		if ((ns_nic_diff[R_GM_ORB_PERF_VC0_PKTS] +
		     ns_nic_diff[R_GM_ORB_PERF_VC1_PKTS]) > 0)
			return get_m_gm_orb_ave_bytes_per_pkt();
		else
			return 0;
		break;
	case M_GM_ORB_BW:
		if (time_delta > 0)
			return get_m_gm_orb_bw(time_delta);
		else
			return 0;
		break;
	case M_GM_NPT_PACKETSIZE_AVE:
		if (ns_nic_diff[R_GM_NPT_PERF_NPT_PKT_CNTR] > 0)
			get_m_gm_npt_packetsize_ave();
		else
			return 0;
		break;
	case M_GM_NPT_BW:
		if (time_delta > 0)
			get_m_gm_npt_bw(time_delta);
		else
			return 0;
		break;
	case M_GM_NPT_PERF_NPT_BLOCKED_CNTR:
		return ns_nic_diff[R_GM_NPT_PERF_NPT_BLOCKED_CNTR];
		break;
	case M_GM_NPT_PERF_NPT_STALLED_CNTR:
		return ns_nic_diff[R_GM_NPT_PERF_NPT_STALLED_CNTR];
		break;
	case M_GM_RAT_PERF_HEADER_BYTES_VC0:
		return  3 * ns_nic_diff[R_GM_RAT_PERF_HEADER_FLITS_VC0];
		break;
	case M_GM_RAT_PERF_DATA_BYTES_VC0:
		return 3 * ns_nic_diff[R_GM_RAT_PERF_DATA_FLITS_VC0];
		break;
	}
	/* return 0 by default */
	return 0;
}

int sample_metrics_nic_perf(int curr_metric_no, int* mc)
{

	int metric_no;
	int done = FALSE;
	union ldms_value v;
	union ldms_value vd;
	uint64_t prevval;
	uint64_t time_delta;
	int error;
	int rc;
	int i;

	if (ns_nic_valid == 0)
		return 0;

	clock_gettime(CLOCK_REALTIME, ns_nic_curr_time);

	error = gpcd_context_read_mmr_vals(ns_nic_curr_ctx);
	if (error) {
		msglog("nic_perf: Error reading mmr_vals\n");
		rc = EINVAL;
		return rc;
	} else {
		ns_nic_listp = ns_nic_curr_ctx->list;
		ns_nic_plistp = ns_nic_prev_ctx->list;
	}

	metric_no = NUM_NIC_PERF_RAW-1;
	do {
		if ( ns_nic_listp->value < ns_nic_plistp->value )
			ns_nic_diff[metric_no] =
				(ULONG_MAX - ns_nic_plistp->value) +
				ns_nic_listp->value;
		else
			ns_nic_diff[metric_no] =
				(uint64_t)(ns_nic_listp->value -
						ns_nic_plistp->value);

		if ((ns_nic_listp->next != NULL) &&
				(ns_nic_plistp->next != NULL)) {
			ns_nic_listp = ns_nic_listp->next;
			ns_nic_plistp = ns_nic_plistp->next;
		} else {
			ns_nic_int_ctx = ns_nic_prev_ctx;
			ns_nic_prev_ctx = ns_nic_curr_ctx;
			ns_nic_curr_ctx = ns_nic_int_ctx;
			done = TRUE;
		}
		metric_no--;
	} while (!done);
	done = FALSE;

	time_delta = ((ns_nic_curr_time->tv_sec
				- ns_nic_prev_time->tv_sec) * 1000000000)
		     + (ns_nic_curr_time->tv_nsec -
			ns_nic_prev_time->tv_nsec);
	for (i = 0; i < NUM_NIC_PERF_METRICS; i++){
		v.v_u64 = __nic_perf_metric_calc(i, time_delta);
		ldms_set_metric(ns_nic_metric_table[i], &v);
	}

	ns_nic_int_time = ns_nic_prev_time;
	ns_nic_prev_time = ns_nic_curr_time;
	ns_nic_curr_time = ns_nic_int_time;

	rc = 0;

	return rc;
}

uint64_t __gem_link_metric_calc(
	int i, int j, uint64_t sample_link_ctrs[][GEMINI_NUM_TILE_COUNTERS],
	uint64_t time_delta)
{
	int rc = 0;

	switch (j) {
	case SAMPLE_GEMINI_TCTR_LINK_BW:
		/*  sample_link_ctrs[i][0] and
		 *  sample_link_ctrs[i][1] are the number of
		 *  phits on each of virtual channel 0 and 1
		 *  respectively
		 *  each phit is 3 bytes so conversion to
		 *  bytes uses a multiplier of 3
		 *  bandwidth = bytes/time(in sec)
		 */
		if (time_delta > 0)
			return 3 * (uint64_t)((double)(sample_link_ctrs[i][0] +
						       sample_link_ctrs[i][1]) /
					      ((double)time_delta/1000000000));
		else
			return 0;
		break;
	case SAMPLE_GEMINI_TCTR_LINK_USED_BW:
		if ((ns_gemlink_max_link_bw[i] > 0) && (time_delta > 0)) {
			/*  sample_link_ctrs[i][0] and
			 *  sample_link_ctrs[i][1] are the
			 *  number of phits on each of virtual
			 *  channel 0 and 1 respectively
			 *  each phit is 3 bytes so conversion
			 *  to bytes uses a multiplier of 3
			 *  time is in nsec so multiply by
			 *  10e9 to convert to sec
			 *  ns_gemlink_max_link_bw[i] comes from gemini.h
			 *  and is the bandwidth for that type
			 *  of link
			 *  multiplier of 100 converts fraction
			 *  to %. Multiply by 1000 to keep
			 *  precision.
			 */
			return (uint64_t)(
				(((double)(sample_link_ctrs[i][0] +
					   sample_link_ctrs[i][1]) /
				  (double)time_delta) /
				 (double) ns_gemlink_max_link_bw[i]) *
				100.0 * 3.0 * 1000.0);
		} else {
			return 0;
		}
		break;
	case SAMPLE_GEMINI_TCTR_LINK_PACKETSIZE_AVE:
		if ( (sample_link_ctrs[i][2] + sample_link_ctrs[i][3]) > 0 ) {
			/*  sample_link_ctrs[i][2] and
			 *  sample_link_ctrs[i][3] are the
			 *  number of packets on each of virtual
			 *  channel 0 and 1 respectively
			 *  average is being calculated as
			 *  (number of bytes total)/(number of
			 *  packets total)
			 */
			return 3 * (uint64_t)(
				(double)(sample_link_ctrs[i][0] +
					 sample_link_ctrs[i][1]) /
				(double)(sample_link_ctrs[i][2] +
					 sample_link_ctrs[i][3]));
		} else {
			return 0;
		}
		break;
	case SAMPLE_GEMINI_TCTR_LINK_INPUT_STALLS:
		if ( (ns_gemlink_tiles_per_dir[i] > 0) && (time_delta > 0)){
			/*  sample_link_ctrs[i][4] is the number of
			 *  input stalls in direction i. Values for the
			 *  stall counter registers are in units of the
			 *  Gemini clock cycles. Gemini clock runs at
			 *  800Mhz, so multiply one tile's stall cycles
			 *  by 1.25 to get ns. Include all tiles in this
			 *  dir. Multiply by 100 to get percent.
			 *  Multiply by 1000 to keep precision.
			 */
			uint64_t temp = (uint64_t)(
				((1.25 * (double)sample_link_ctrs[i][4] /
				  (double)ns_gemlink_tiles_per_dir[i]) /
				 (double)time_delta) * 100.0 * 1000.0);
			if (temp > 100 * 1000)
				msglog("ERR: Time %lld,%09ld INPUT STALLS[%d]:"
				       " %llu sample_link_ctrs[%d][4] = %g"
				       " tiles_per_dir = %g time_delta = %g\n",
				       (long long) ns_gemlink_curr_time->tv_sec,
				       ns_gemlink_curr_time->tv_nsec,
				       i, temp, i,
				       (double)sample_link_ctrs[i][4],
				       (double)ns_gemlink_tiles_per_dir[i],
				       (double) time_delta);
			return temp;
		} else {
			return 0;
		}
		break;
	case SAMPLE_GEMINI_TCTR_LINK_OUTPUT_STALLS:
		if ( (ns_gemlink_tiles_per_dir[i] > 0) && (time_delta > 0)){
			/*  sample_link_ctrs[i][5] is the number of
			 *  input stalls in direction i. Values for the
			 *  stall counter registers are in units of the
			 *  Gemini clock cycles. Gemini clock runs at
			 *  800Mhz, so multiply one tile's stall cycles
			 *  by 1.25 to get ns. Include all tiles in this
			 *  dir. Multiply by 100 to get percent.
			 *  Multiply by 1000 to keep precision.
			 */
			uint64_t temp = (uint64_t)(
				((1.25 * (double)sample_link_ctrs[i][5] /
				  (double)ns_gemlink_tiles_per_dir[i]) /
				 (double)time_delta) * 100.0 * 1000.0);
			if (temp > 100 * 1000)
				msglog("ERR: Time %lld.%09ld OUTPUT STALLS[%d]:"
				       " %llu sample_link_ctrs[%d][5] = %g "
				       "tiles_per_dir = %g time_delta = %g\n",
				       (long long)ns_gemlink_curr_time->tv_sec,
				       ns_gemlink_curr_time->tv_nsec,
				       i, temp, i,
				       (double)sample_link_ctrs[i][5],
				       (double)ns_gemlink_tiles_per_dir[i],
				       (double) time_delta);
			return temp;
		} else {
			return 0;
		}
		break;
	default:
		/* Won't happen */
		return 0;
	}
}


int sample_metrics_gem_link_perf(int curr_metric_no, int* mc)
{
	int metric_no;
	int done = FALSE;
	int error;
	union ldms_value v;
	union ldms_value vd;
	uint64_t prevval;
	uint64_t time_delta;
	uint64_t sample_link_ctrs[GEMINI_NUM_LOGICAL_LINKS]
	[GEMINI_NUM_TILE_COUNTERS];
	int rc;
	int i,j,k;


	if (ns_gemlink_valid == 0)
		return 0;

	/*  Zero sample array */
	memset (sample_link_ctrs, 0, sizeof(sample_link_ctrs));
	metric_no = (GEMINI_NUM_TILE_ROWS * GEMINI_NUM_TILE_COLUMNS *
		     GEMINI_NUM_TILE_COUNTERS) - 1;

	/*  Fill in mesh coords (this is static and should be moved) */
	/* will want these 3 to be LDMS_V_U8 */
	v.v_u64 = (uint64_t) ns_gemlink_my_coord.x;
	ldms_set_metric(ns_gemlink_meshcoord_metric_handle[0], &v);
	v.v_u64 = (uint64_t) ns_gemlink_my_coord.y;
	ldms_set_metric(ns_gemlink_meshcoord_metric_handle[1], &v);
	v.v_u64 = (uint64_t) ns_gemlink_my_coord.z;
	ldms_set_metric(ns_gemlink_meshcoord_metric_handle[2], &v);

	clock_gettime(CLOCK_REALTIME, ns_gemlink_curr_time);

	error = gpcd_context_read_mmr_vals(ns_gemlink_curr_ctx);
	if (error) {
		msglog("nic_perf: Error reading mmr_vals\n");
		rc = EINVAL;
		return rc;
	} else {
		ns_gemlink_listp = ns_gemlink_curr_ctx->list;
		ns_gemlink_plistp = ns_gemlink_prev_ctx->list;
	}

	do {
		i = metric_no/(GEMINI_NUM_TILE_COLUMNS *
			       GEMINI_NUM_TILE_COUNTERS);
		j = ((int)metric_no/GEMINI_NUM_TILE_COUNTERS) %
			GEMINI_NUM_TILE_COLUMNS;
		k = metric_no%GEMINI_NUM_TILE_COUNTERS;

		if (ns_gemlink_state->tile[ns_gemlink_rc_to_tid[i][j]].dir !=
		    GEMINI_LINK_DIR_INVALID) {
			if ( ns_gemlink_listp->value <
			     ns_gemlink_plistp->value ) {
				msglog("gem_link_perf rollover:"
				       " time= %lld.%09%ld i %d j %d k %d"
				       " listp->value %llu plistp->value = "
				       "%llu\n",
				       (long long) ns_gemlink_curr_time->tv_sec,
				       ns_gemlink_curr_time->tv_nsec,
				       i, j, k, ns_gemlink_listp->value,
				       ns_gemlink_plistp->value);
				ns_gemlink_diff =
					(ULONG_MAX - ns_gemlink_plistp->value) +
					ns_gemlink_listp->value;
				msglog("INFO: gem_link_perf rollover cont'd: i "
				       "%d j %d k %d ns_gemlink_diff = %llu\n",
				       i, j, k, ns_gemlink_diff);
			} else {
				ns_gemlink_diff =
					(uint64_t)(ns_gemlink_listp->value -
						   ns_gemlink_plistp->value);
			}

			sample_link_ctrs[ns_gemlink_state->tile[
					ns_gemlink_rc_to_tid[i][j]].dir][k] +=
				ns_gemlink_diff;
		}

		if ((ns_gemlink_listp->next != NULL) &&
		    (ns_gemlink_plistp->next != NULL)) {
			ns_gemlink_listp = ns_gemlink_listp->next;
			ns_gemlink_plistp = ns_gemlink_plistp->next;
			metric_no--;
		} else {
			ns_gemlink_int_ctx = ns_gemlink_prev_ctx;
			ns_gemlink_prev_ctx = ns_gemlink_curr_ctx;
			ns_gemlink_curr_ctx = ns_gemlink_int_ctx;
			done = TRUE;
		}
	} while (!done);
	done = FALSE;

	metric_no = 0;
	time_delta =
		((ns_gemlink_curr_time->tv_sec - ns_gemlink_prev_time->tv_sec) *
		 1000000000) +
		(ns_gemlink_curr_time->tv_nsec - ns_gemlink_prev_time->tv_nsec);
	for (i = 0; i < GEMINI_NUM_LOGICAL_LINKS; i++) {
		for (j = 0; j < GEMINI_NUM_LINK_STATS; j++) {
			v.v_u64 = __gem_link_metric_calc(
				i, j, sample_link_ctrs, time_delta);
			ldms_set_metric(ns_gemlink_metric_table[metric_no], &v);
			metric_no++;
		}
	}

	ns_gemlink_int_time = ns_gemlink_prev_time;
	ns_gemlink_prev_time = ns_gemlink_curr_time;
	ns_gemlink_curr_time = ns_gemlink_int_time;

	rc = 0;

	return rc;
}

static int sample(void)
{
	int rc;
	int metric_no;
	int metric_count;
	char *s;
	char lbuf[256];
	char metric_name[128];
	union ldms_value v;
	int i;

	if (!set) {
		msglog("ncsa_unified: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);

	metric_no = 0;
	for (i = 0; i < NS_NUM; i++){
		switch (i){
		case NS_VMSTAT:
			rc = sample_metrics_vmstat(metric_no, &metric_count);
			metric_no += num_vmstat_metrics;
			break;
		case NS_CURRENT_FREEMEM:
			rc = sample_metrics_current_freemem(metric_no,
								&metric_count);
			metric_no += num_current_freemem_metrics;
			break;
		case NS_LOADAVG:
			rc = sample_metrics_loadavg(metric_no, &metric_count);
			metric_no += num_loadavg_metrics;
			break;
		case NS_KGNILND:
			rc = sample_metrics_kgnilnd(metric_no, &metric_count);
			metric_no += num_kgnilnd_metrics;
			break;
		case NS_LUSTRE:
			rc = sample_metrics_lustre(metric_no, &metric_count);
			metric_no += metric_count;
			break;
		case NS_NIC_PERF:
			rc = sample_metrics_nic_perf(metric_no, &metric_count);
			metric_no += NUM_NIC_PERF_METRICS;
			break;
		case NS_GEM_LINK_PERF:
			rc = sample_metrics_gem_link_perf(metric_no,
							  &metric_count);
			metric_no += num_gem_link_perf_metrics;
			break;
		default:
			rc =  sample_metrics_null(metric_no, &metric_count, i);
			metric_no += metric_count;
			break;
		}
		/* goto out on error */
		if (rc)
			goto out;
	}

	rc = 0;
 out:
	ldms_end_transaction(set);
	return rc;
}

static void term(void)
{
	if (set)
		ldms_destroy_set(set);
	set = NULL;
}

static const char *usage(void)
{
	return  "config name=ncsa_unified component_id=<comp_id> set=<setname>"
		" rtrfile=<parsedrtr.txt> llite=<ostlist>\n"
		"    comp_id     The component id value.\n"
		"    setname     The set name.\n",
		"    parsedrtr   The parsed interconnect file.\n",
		"    ostlist     Lustre OSTs\n";
}


static struct ldmsd_sampler ncsa_unified_plugin = {
	.base = {
		.name = "ncsa_unified",
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	static int init_complete = 0;
	if (init_complete)
		goto out;

	lustre_idx_map = str_map_create(1021);
	if (!lustre_idx_map)
		goto err;

	if (str_map_id_init(lustre_idx_map, LUSTRE_METRICS,
				LUSTRE_METRICS_LEN, 1))
		goto err;

	init_complete = 1;

out:
	return &ncsa_unified_plugin.base;

err:
	if (lustre_idx_map) {
		str_map_free(lustre_idx_map);
		lustre_idx_map = NULL;
	}
	return NULL;
}
