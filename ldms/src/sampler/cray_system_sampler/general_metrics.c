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
 * \file general_metrics.c
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <ctype.h>
#include <wordexp.h>
#include "rca_metrics.h"
#include "general_metrics.h"

//------------------------//
//make these all static....
#include "../lustre/lustre_sampler.h"

#define VMSTAT_FILE "/proc/vmstat"
#define LOADAVG_FILE "/proc/loadavg"
#define CURRENT_FREEMEM_FILE "/proc/current_freemem"
#define KGNILND_FILE  "/proc/kgnilnd/stats"
#define PROCNETDEV_FILE "/proc/net/dev"

/* CURRENT_FREEMEM Specific */
FILE *cf_f;
int cf_m;
static char* CURRENT_FREEMEM_METRICS[] = {"current_freemem"};
#define NUM_CURRENT_FREEMEM_METRICS (sizeof(CURRENT_FREEMEM_METRICS)/sizeof(CURRENT_FREEMEM_METRICS[0]))
ldms_metric_t* metric_table_current_freemem;
int (*sample_metrics_cf_ptr)(ldmsd_msg_log_f msglog);

/* VMSTAT Specific */
FILE *v_f;
static char* VMSTAT_METRICS[] = {"nr_dirty", "nr_writeback"};
#define NUM_VMSTAT_METRICS (sizeof(VMSTAT_METRICS)/sizeof(VMSTAT_METRICS[0]))
ldms_metric_t* metric_table_vmstat;
/* additional vmstat metrics if getting cf from vmstat. Order matters (see calc within) */
static char* VMCF_METRICS[] = {"nr_free_pages", "nr_file_pages", "nr_slab_reclaimable", "nr_shmem"};
#define NUM_VMCF_METRICS (sizeof(VMCF_METRICS)/sizeof(VMCF_METRICS[0]))
int (*sample_metrics_vmstat_ptr)(ldmsd_msg_log_f msglog);


/* LOADAVG Specific */
FILE *l_f;
static char* LOADAVG_METRICS[] = {"loadavg_latest(x100)",
				  "loadavg_5min(x100)",
				  "loadavg_running_processes",
				  "loadavg_total_processes"};
#define NUM_LOADAVG_METRICS (sizeof(LOADAVG_METRICS)/sizeof(LOADAVG_METRICS[0]))
ldms_metric_t *metric_table_loadavg;

/* PROCNETDEV Specific (Specific interface and indicies supported)*/
FILE *pnd_f;
static char* iface ="ipogif0";
int idx_iface;
static char* PROCNETDEV_METRICS[] = {"ipogif0_rx_bytes",
				     "ipogif0_tx_bytes"};
#define NUM_PROCNETDEV_METRICS (sizeof(PROCNETDEV_METRICS)/sizeof(PROCNETDEV_METRICS[0]))

ldms_metric_t *metric_table_procnetdev;
int procnetdev_valid;


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
#define NUM_KGNILND_METRICS (sizeof(KGNILND_METRICS)/sizeof(KGNILND_METRICS[0]))
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
#define CSS_LUSTRE_NAME_MAX 1024
#define CSS_LUSTRE_PATH_MAX 4096


/* Lustre specific vars */
/**
 * str<->idx in LUSTRE_METRICS.
 */
extern struct lustre_svc_stats_head lustre_svc_head;
extern struct str_map *lustre_idx_map;

//---------------------------------------//

/* LUSTRE SPECIFIC */
struct str_map *lustre_idx_map = NULL;
struct lustre_svc_stats_head lustre_svc_head = {0};

static int get_metric_size_lustre(size_t *m_sz, size_t *d_sz,
			   ldmsd_msg_log_f msglog)
{
	struct lustre_svc_stats *lss;
	size_t msize = 0;
	size_t dsize = 0;
	size_t m, d;
	char name[CSS_LUSTRE_NAME_MAX];
	int i;
	int rc;

	LIST_FOREACH(lss, &lustre_svc_head, link) {
		for (i=0; i<LUSTRE_METRICS_LEN; i++) {
			snprintf(name, CSS_LUSTRE_NAME_MAX, "%s#stats.%s", LUSTRE_METRICS[i]
					, lss->name);
			rc = ldms_get_metric_size(name, LDMS_V_U64, &m, &d);
			if (rc)
				return rc;
			msize += m;
			dsize += d;
		}
	}
	*m_sz = msize;
	*d_sz = dsize;
	return 0;
}


static int add_metrics_lustre(ldms_set_t set, int comp_id,
			      ldmsd_msg_log_f msglog)
{
	struct lustre_svc_stats *lss;
	int i;
	int count = 0;
	char name[CSS_LUSTRE_NAME_MAX];

	LIST_FOREACH(lss, &lustre_svc_head, link) {
		for (i=0; i<LUSTRE_METRICS_LEN; i++) {
			snprintf(name, CSS_LUSTRE_NAME_MAX, "%s#stats.%s", LUSTRE_METRICS[i]
							, lss->name);
			ldms_metric_t m = ldms_add_metric(set, name,
								LDMS_V_U64);
			if (!m)
				return ENOMEM;
			lss->metrics[i+1] = m;
			ldms_set_user_data(m, comp_id);
			count++;
		}
	}
	return 0;
}



int handle_llite(const char *llite)
{
	char *_llite = strdup(llite);
	if (!_llite)
		return ENOMEM;
	char *saveptr = NULL;
	char *tok = strtok_r(_llite, ",", &saveptr);
	struct lustre_svc_stats *lss;
	char path[CSS_LUSTRE_PATH_MAX];
	while (tok) {
		snprintf(path, CSS_LUSTRE_PATH_MAX,"/proc/fs/lustre/llite/%s-*/stats",tok);
		lss = lustre_svc_stats_alloc(path, LUSTRE_METRICS_LEN+1);
		lss->name = strdup(tok);
		if (!lss->name)
			goto err;
		lss->key_id_map = lustre_idx_map;
		LIST_INSERT_HEAD(&lustre_svc_head, lss, link);
		tok = strtok_r(NULL, ",", &saveptr);
	}
	free(_llite);
	return 0;
err:
	lustre_svc_stats_list_free(&lustre_svc_head);
	return ENOMEM;
}

static int sample_metrics_vmstat(ldmsd_msg_log_f msglog)
{
	char lbuf[256];
	char metric_name[128];
	int found_metrics;
	char* s;
	union ldms_value v;
	int j, rc;


	/* open and close each time */
	if (v_f)
		fclose(v_f);

	if (VMSTAT_FILE != NULL){
		v_f = fopen(VMSTAT_FILE, "r");
		if (!v_f)
			return 0;
	}

	found_metrics = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), v_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &v.v_u64);
		if (rc != 2) {
			msglog(LDMS_LDEBUG,"ERR: Issue reading the source file '%s'\n",
								VMSTAT_FILE);
			fclose(v_f);
			v_f = 0;
			rc = EINVAL;
			return rc;
		}
		for (j = 0; j < NUM_VMSTAT_METRICS; j++){
			if (!strcmp(metric_name, VMSTAT_METRICS[j])){
				ldms_set_metric(metric_table_vmstat[j], &v);
				found_metrics++;
				break;
			}
		}
	} while (s);

	fclose(v_f);
	v_f = 0;

	if (found_metrics != NUM_VMSTAT_METRICS){
		return EINVAL;
	}

	return 0;

}


static int sample_metrics_vmcf(ldmsd_msg_log_f msglog)
{
	char lbuf[256];
	char metric_name[128];
	uint64_t vmcf[NUM_VMCF_METRICS];
	int found_metrics;
	int found_submetrics;
	int done = 0;
	char* s;
	union ldms_value v;
	int j, rc;

	/* open and close each time */
	if (v_f)
		fclose(v_f);

	if (VMSTAT_FILE != NULL){
		v_f = fopen(VMSTAT_FILE, "r");
		if (!v_f)
			return 0;
	}

	found_metrics = 0;
	found_submetrics = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), v_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &v.v_u64);
		if (rc != 2) {
			msglog(LDMS_LDEBUG,"ERR: Issue reading the source file '%s'\n",
								VMSTAT_FILE);
			fclose(v_f);
			v_f = 0;
			rc = EINVAL;
			return rc;
		}
		if (found_metrics < NUM_VMSTAT_METRICS){
			for (j = 0; j < NUM_VMSTAT_METRICS; j++){
				if (!strcmp(metric_name, VMSTAT_METRICS[j])){
					ldms_set_metric(metric_table_vmstat[j], &v);
					found_metrics++;
					if ((found_metrics == NUM_VMSTAT_METRICS) &&
					    (found_submetrics == NUM_VMCF_METRICS)){
						done = 1;
						break;
					}
					break;
				}
			}
		}
		if (found_submetrics < NUM_VMCF_METRICS){
			for (j = 0; j < NUM_VMCF_METRICS; j++){
				if (!strcmp(metric_name, VMCF_METRICS[j])){
					vmcf[j] = v.v_u64;
					found_submetrics++;
					if ((found_metrics == NUM_VMSTAT_METRICS) &&
					    (found_submetrics == NUM_VMCF_METRICS)){
						done = 1;
						break;
					}
					break;
				}
			}
		}
	} while (s && !done);

	fclose(v_f);
	v_f = 0;

	if (found_submetrics == NUM_VMCF_METRICS) {
		//treating the order like its well known
		//	(nr_free_pages + nr_file_pages + nr_slab_reclaimable - nr_shmem) * 4
		v.v_u64 = (vmcf[0] + vmcf[1] + vmcf[2] - vmcf[3]) * 4;
		ldms_set_metric(metric_table_current_freemem[0], &v);
	} else {
		return EINVAL;
	}

	if (found_metrics != NUM_VMSTAT_METRICS)
		return EINVAL;

	return 0;

}

static char *replace_space(char *s)
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



static int sample_metrics_kgnilnd(ldmsd_msg_log_f msglog)
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
			msglog(LDMS_LDEBUG,"ERR: Issue reading metric name from the source"
						" file '%s'\n", KGNILND_FILE);
			rc = EINVAL;
			return rc;
		}
		if (sscanf(end + 1, " %"PRIu64"\n", &v.v_u64) == 1 ) {
			for (j = 0; j < NUM_KGNILND_METRICS; j++){
				if (strcmp(metric_name, KGNILND_METRICS[j]))
					continue;
				ldms_set_metric(metric_table_kgnilnd[j], &v);
				found_metrics++;
				break;
			}
		}
	} while (s);

	if (found_metrics != NUM_KGNILND_METRICS){
		return EINVAL;
	}

	return 0;

}

static int sample_metrics_current_freemem(ldmsd_msg_log_f msglog)
{
	/* only has 1 val, no label */
	char lbuf[256];
	char metric_name[128];
	int found_metrics;
	char* s;
	union ldms_value v;
	int j, rc;


	/* Close and open each time */
//	if (!cf_f)
//		return 0;


	if (cf_f)
		fclose(cf_f);

	if (CURRENT_FREEMEM_FILE != NULL){
		cf_f = fopen(CURRENT_FREEMEM_FILE, "r");
		if (!cf_f)
			return 0;
	}


	found_metrics = 0;
//	fseek(cf_f, 0, SEEK_SET);
	s = fgets(lbuf, sizeof(lbuf), cf_f);
	if (s) {
		rc = sscanf(lbuf, "%"PRIu64"\n", &v.v_u64);
		if (rc != 1) {
			msglog(LDMS_LDEBUG,"ERR: Issue reading the source file '%s'\n",
							CURRENT_FREEMEM_FILE);
			fclose(cf_f);
			cf_f = 0;
			rc = EINVAL;
			return rc;
		}
		ldms_set_metric(metric_table_current_freemem[0], &v);
		found_metrics++;
	}


	fclose(cf_f);
	cf_f = 0;

	if (found_metrics != NUM_CURRENT_FREEMEM_METRICS){
		return EINVAL;
	}

	return 0;

}


static int procnetdev_setup(ldmsd_msg_log_f msglog)
{
	/** need tx rx bytes for ipogif0 interface only */
	procnetdev_valid = 0;

	if (!pnd_f) {
		msglog(LDMS_LDEBUG,"procnetdev: filehandle NULL\n");
		return EINVAL;
	}

	char lbuf[256];
	char* s;
	int count = -1;

	/* assume on product system ifaces and order do not change w/o reboot */
	idx_iface = -1;
	do {
		s = fgets(lbuf, sizeof(lbuf), pnd_f);
		if (!s)
			break;
		count++;
		if (strstr(lbuf,iface))
			idx_iface = count; /* continue past eof */
	} while(s);

	if (idx_iface == -1){
		msglog(LDMS_LDEBUG,"procnetdev: cannot find iface <%s>\n", iface);
		return EINVAL;
	}

	procnetdev_valid = 1;
	return 0;
}

static int sample_metrics_procnetdev(ldmsd_msg_log_f msglog)
{

	if (procnetdev_valid == 0) {
		return 0;
	}

	if (!pnd_f) {
		msglog(LDMS_LDEBUG,"procnetdev: filehandle NULL\n");
		return EINVAL;
	}

	char lbuf[256];
	char curriface[10];
	union ldms_value v[2];
	char* s;
	int rc;
	int i;
	int found = 0;

	i = -1;
	fseek(pnd_f, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), pnd_f);
		if (!s)
			break;
		i++;
		if (i == idx_iface){
			int rc = sscanf(lbuf, "%[^:]%*c %" PRIu64 " %*"
					PRIu64 " %*" PRIu64 " %*" PRIu64 " %*"
					PRIu64 " %*" PRIu64 " %*" PRIu64 " %*"
					PRIu64 " %" PRIu64 "",
					curriface, &v[0].v_u64, &v[1].v_u64);
			if (strstr(curriface,iface) && (rc == 3)){
				ldms_set_metric(
					metric_table_procnetdev[0],&v[0]);
				ldms_set_metric(
					metric_table_procnetdev[1], &v[1]);
				found++;
			}
		}
	} while(s);

	if (!found)
		return EINVAL;

	return 0;
}

static int sample_metrics_loadavg(ldmsd_msg_log_f msglog)
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

	/* open and close each time */
	if (l_f)
		fclose(l_f);

	if (LOADAVG_FILE != NULL){
		l_f = fopen(LOADAVG_FILE, "r");
		if (!l_f)
			return 0;
	}

//	if (!l_f)
//		return 0;

	found_metrics = 0;
//	fseek(l_f, 0, SEEK_SET);
	s = fgets(lbuf, sizeof(lbuf), l_f);
	if (s) {
		rc = sscanf(lbuf, "%f %f %f %d/%d %d\n",
			    &vf[0], &vf[1], &vf[2], &vi[0], &vi[1], &vi[2]);
		if (rc != 6) {
			msglog(LDMS_LDEBUG,"ERR: Issue reading the source file '%s'"
					" (rc=%d)\n", LOADAVG_FILE, rc);
			fclose(l_f);
			l_f = NULL;
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

	fclose(l_f);
	l_f = NULL;

	if (found_metrics != NUM_LOADAVG_METRICS){
		return EINVAL;
	}

	return 0;
}


static int sample_metrics_lustre(ldmsd_msg_log_f msglog)
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


static int get_metric_size_simple(char** metric_names, int num_metrics,
				  size_t *m_sz, size_t *d_sz,
				  ldmsd_msg_log_f msglog)
{

	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int i, rc;

	tot_data_sz = 0;
	tot_meta_sz = 0;



	for (i = 0; i < num_metrics; i++){
		rc = ldms_get_metric_size(metric_names[i], LDMS_V_U64,
							&meta_sz, &data_sz);
		if (rc)
			return rc;
		tot_meta_sz+= meta_sz;
		tot_data_sz+= data_sz;
	}

	*m_sz = tot_meta_sz;
	*d_sz = tot_data_sz;

	return 0;

}

int get_metric_size_generic(size_t *m_sz, size_t *d_sz,
			    cray_system_sampler_sources_t source_id,
			    ldmsd_msg_log_f msglog)
{

	int i, rc;

	switch (source_id){
	case NS_NETTOPO:
		return get_metric_size_simple(nettopo_meshcoord_metricname,
					      NETTOPODIM,
					      m_sz, d_sz, msglog);
		break;
	case NS_VMSTAT:
		sample_metrics_vmstat_ptr = NULL;
		return get_metric_size_simple(VMSTAT_METRICS,
					      NUM_VMSTAT_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_LOADAVG:
		return get_metric_size_simple(LOADAVG_METRICS,
					      NUM_LOADAVG_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_CURRENT_FREEMEM:
		sample_metrics_cf_ptr = NULL;
		return get_metric_size_simple(CURRENT_FREEMEM_METRICS,
					      NUM_CURRENT_FREEMEM_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_PROCNETDEV:
		return get_metric_size_simple(PROCNETDEV_METRICS,
					      NUM_PROCNETDEV_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_KGNILND:
		return get_metric_size_simple(KGNILND_METRICS,
					      NUM_KGNILND_METRICS,
					      m_sz, d_sz, msglog);
		break;
	case NS_LUSTRE:
		return get_metric_size_lustre(m_sz, d_sz, msglog);
		break;
	default:
		break;
	}

	return 0;
}


static int add_metrics_simple(ldms_set_t set, char** metric_names,
			      int num_metrics, ldms_metric_t** metric_table,
			      char (*fname)[], FILE** g_f,
			      int comp_id, ldmsd_msg_log_f msglog)
{
	int i, rc;

	if (num_metrics == 0){
		return 0;
	}

	*metric_table = calloc(num_metrics, sizeof(ldms_metric_t));
	if (! (*metric_table)){
		msglog(LDMS_LDEBUG,"cray_system_sampler: cannot calloc metric_table\n");
		return ENOMEM;
	}

	if (fname != NULL){
		*g_f = fopen(*fname, "r");
		if (!(*g_f)) {
			/* this is not an error */
			msglog(LDMS_LDEBUG,"WARNING: Could not open the source file '%s'\n",
			       *fname);
		}
	} else {
		if (g_f && *g_f)
			*g_f = NULL;
	}


	for (i = 0; i < num_metrics; i++){
		(*metric_table)[i] = ldms_add_metric(set, metric_names[i],
						     LDMS_V_U64);

		if (!(*metric_table)[i]){
			msglog(LDMS_LDEBUG,"cray_system_sampler: cannot add metric %d\n",
			       i);
			rc = ENOMEM;
			return rc;
		}
		ldms_set_user_data((*metric_table)[i], comp_id);
	}

	return 0;
}


int add_metrics_generic(ldms_set_t set, int comp_id,
			       cray_system_sampler_sources_t source_id,
			       ldmsd_msg_log_f msglog)
{
	int i, rc;

	switch (source_id){
	case NS_NETTOPO:
		rc = add_metrics_simple(set,
					nettopo_meshcoord_metricname,
					NETTOPODIM,
					&nettopo_metric_table,
					NULL, NULL,
					comp_id, msglog);
		if (rc != 0)
			return rc;
		nettopo_setup(msglog);
		return 0;
	case NS_VMSTAT:
		rc = add_metrics_simple(set, VMSTAT_METRICS,
					NUM_VMSTAT_METRICS,
					&metric_table_vmstat,
					&VMSTAT_FILE, &v_f,
					comp_id, msglog);
		if (rc != 0) {
			sample_metrics_vmstat_ptr == NULL;
			return rc;
		}
		if (v_f != NULL){
			fclose(v_f);
			v_f = NULL;
		}
		if (sample_metrics_vmstat_ptr == NULL) {
			//could be set from current_freemem
			sample_metrics_vmstat_ptr == sample_metrics_vmstat;
		}
		return rc;

		break;
	case NS_LOADAVG:
		return add_metrics_simple(set, LOADAVG_METRICS,
					  NUM_LOADAVG_METRICS,
					  &metric_table_loadavg,
					  &LOADAVG_FILE, &l_f,
					  comp_id, msglog);
		if (rc != 0)
			return rc;
		if (l_f != NULL){
			fclose(l_f);
			l_f = NULL;
		}

		return rc;

		break;
	case NS_CURRENT_FREEMEM:
		cf_m = 0;
		rc = add_metrics_simple(set, CURRENT_FREEMEM_METRICS,
					NUM_CURRENT_FREEMEM_METRICS,
					&metric_table_current_freemem,
					&CURRENT_FREEMEM_FILE, &cf_f,
					comp_id, msglog);
		if (rc != 0)
			return rc; //This will NOT happen if the file DNE
		if (cf_f != NULL) {
			fclose(cf_f);
			cf_f = NULL;
			sample_metrics_cf_ptr = &sample_metrics_current_freemem;
		} else {
			/* if there is no current_freemem, get it out of vmstat */
			sample_metrics_cf_ptr = NULL;
			sample_metrics_vmstat_ptr = sample_metrics_vmcf;
		}
		return rc;

		break;
	case NS_PROCNETDEV:
		rc = add_metrics_simple(set, PROCNETDEV_METRICS,
					  NUM_PROCNETDEV_METRICS,
					  &metric_table_procnetdev,
					  &PROCNETDEV_FILE, &pnd_f,
					  comp_id, msglog);
		if (rc != 0)
			return rc;
		rc = procnetdev_setup(msglog);
		if (rc != 0) /* Warn but OK to continue */
			msglog(LDMS_LDEBUG,"cray_system_sampler: procnetdev invalid\n");
		break;
	case NS_KGNILND:
		return add_metrics_simple(set, KGNILND_METRICS,
					  NUM_KGNILND_METRICS,
					  &metric_table_kgnilnd,
					  &KGNILND_FILE, &k_f,
					  comp_id, msglog);
		break;
	case NS_LUSTRE:
		return add_metrics_lustre(set, comp_id, msglog);
		break;
	default:
		break;
	}

	return 0;
}

int sample_metrics_generic(cray_system_sampler_sources_t source_id,
			   ldmsd_msg_log_f msglog)
{
	int rc;

	switch (source_id){
	case NS_NETTOPO:
		rc = sample_metrics_nettopo(msglog);
		break;
	case NS_VMSTAT:
		if (sample_metrics_vmstat_ptr != NULL)
			rc = sample_metrics_vmstat_ptr(msglog);
		else 
			rc = 0;
		break;
	case NS_CURRENT_FREEMEM:
		if (sample_metrics_cf_ptr != NULL)
			rc = sample_metrics_cf_ptr(msglog);
		else
			rc = 0;
		break;
	case NS_LOADAVG:
		rc = sample_metrics_loadavg(msglog);
		break;
	case NS_KGNILND:
		rc = sample_metrics_kgnilnd(msglog);
		break;
	case NS_PROCNETDEV:
		rc = sample_metrics_procnetdev(msglog);
		break;
	case NS_LUSTRE:
		rc = sample_metrics_lustre(msglog);
		break;
	default:
		//do nothing
		break;
	}

	return rc;
}

