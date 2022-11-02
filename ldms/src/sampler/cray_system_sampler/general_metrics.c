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
#include "general_metrics.h"

/* Defined in cray_sampler_base.c */
extern ovis_log_t __cray_sampler_log;

FILE* ene_f[NUM_ENERGY_METRICS];
int* metric_table_energy;
FILE *cf_f;
int cf_m;
int* metric_table_current_freemem;
int (*sample_metrics_cf_ptr)(ldms_set_t set);
FILE *v_f;
int* metric_table_vmstat;
int (*sample_metrics_vmstat_ptr)(ldms_set_t set);
FILE *l_f;
int *metric_table_loadavg;
FILE *pnd_f;
int idx_iface;
int *metric_table_procnetdev;
int procnetdev_valid;
int* metric_table_kgnilnd;

/* KGNILND Specific */
FILE *k_f;

int sample_metrics_vmstat(ldms_set_t set)
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
			ovis_log(__cray_sampler_log, OVIS_LERROR,"ERR: Issue reading the source file '%s'\n",
								VMSTAT_FILE);
			fclose(v_f);
			v_f = 0;
			rc = EINVAL;
			return rc;
		}
		for (j = 0; j < NUM_VMSTAT_METRICS; j++){
			if (!strcmp(metric_name, VMSTAT_METRICS[j])){
				ldms_metric_set(set, metric_table_vmstat[j], &v);
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


int sample_metrics_vmcf(ldms_set_t set)
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
			ovis_log(__cray_sampler_log, OVIS_LERROR,"ERR: Issue reading the source file '%s'\n",
								VMSTAT_FILE);
			fclose(v_f);
			v_f = 0;
			rc = EINVAL;
			return rc;
		}
		if (found_metrics < NUM_VMSTAT_METRICS){
			for (j = 0; j < NUM_VMSTAT_METRICS; j++){
				if (!strcmp(metric_name, VMSTAT_METRICS[j])){
					ldms_metric_set(set, metric_table_vmstat[j], &v);
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
		ldms_metric_set(set, metric_table_current_freemem[0], &v);
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



int sample_metrics_kgnilnd(ldms_set_t set)
{
	char lbuf[256];
	char metric_name[128];
	int found_metrics;
	char* s;
	union ldms_value v;
	int j, rc;

	if (!k_f){
		k_f = fopen(KGNILND_FILE, "r");
		if (!k_f) {
			v.v_u64 = 0;
			for (j = 0; j < NUM_KGNILND_METRICS; j++)
				ldms_metric_set(set, metric_table_kgnilnd[j], &v);
			return EINVAL;
		}
	}

	if (fseek(k_f, 0, SEEK_SET) != 0){
		/* perhaps the file handle has become invalid.
		 * close it so it will reopen on the next round
		 * TODO: zero out the values
		 */
		ovis_log(__cray_sampler_log, OVIS_LDEBUG, "css -kgnilnd seek failed\n");
		fclose(k_f);
		k_f = 0;
		v.v_u64 = 0;
		for (j = 0; j < NUM_KGNILND_METRICS; j++)
			ldms_metric_set(set, metric_table_kgnilnd[j], &v);
		return EINVAL;
	}

	found_metrics = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), k_f);
		if (!s)
			break;

		if (s[0] == '\n')
			continue;

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
			ovis_log(__cray_sampler_log, OVIS_LERROR,"ERR: Issue reading metric name from the source"
						" file '%s'\n", KGNILND_FILE);
			rc = EINVAL;
			return rc;
		}
		if (sscanf(end + 1, " %"PRIu64"\n", &v.v_u64) == 1 ) {
			for (j = 0; j < NUM_KGNILND_METRICS; j++){
				if (strcmp(metric_name, KGNILND_METRICS[j]))
					continue;
				ldms_metric_set(set, metric_table_kgnilnd[j], &v);
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

int sample_metrics_current_freemem(ldms_set_t set)
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
			ovis_log(__cray_sampler_log, OVIS_LERROR,"ERR: Issue reading the source file '%s'\n",
							CURRENT_FREEMEM_FILE);
			fclose(cf_f);
			cf_f = 0;
			rc = EINVAL;
			return rc;
		}
		ldms_metric_set(set, metric_table_current_freemem[0], &v);
		found_metrics++;
	}


	fclose(cf_f);
	cf_f = 0;

	if (found_metrics != NUM_CURRENT_FREEMEM_METRICS){
		return EINVAL;
	}

	return 0;

}


int sample_metrics_energy(ldms_set_t set)
{
	char lbuf[256];
	char metric_name[128];
	char* s;
	union ldms_value v;
	int i, j, rc, rcout;

	/** note - not counting how many found since these are all separate sources. */
	rcout = 0;
	for (i = 0; i < NUM_ENERGY_METRICS; i++){
		/** see if we have to open and close these each time. Keeping an array because I think we can */
		if (ene_f[i])
			fclose(ene_f[i]);
		ene_f[i] = 0;
		v.v_u64 = 0;
		ene_f[i] = fopen(ENERGY_FILES[i], "r");
		if (ene_f[i]){
			fseek(ene_f[i], 0, SEEK_SET);
			s = fgets(lbuf, sizeof(lbuf), ene_f[i]);
			if (s) {
				//Ignore the unit
				rc = sscanf(lbuf, "%"PRIu64"\n", &v.v_u64);
				if (rc != 1) {
					ovis_log(__cray_sampler_log, OVIS_LERROR,
					       "ERR: Issue reading the source file '%s'\n",
					       ENERGY_FILES[i]);
					rc = EINVAL;
					rcout = rc;
				}

			}
			fclose(ene_f[i]);
			ene_f[i] = 0;
		}
		ldms_metric_set(set, metric_table_energy[i], &v);
	}

	return rcout;

}

int procnetdev_setup()
{
	/** need tx rx bytes for ipogif0 interface only */
	procnetdev_valid = 0;

	if (!pnd_f) {
		ovis_log(__cray_sampler_log, OVIS_LERROR,"procnetdev: filehandle NULL\n");
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
		ovis_log(__cray_sampler_log, OVIS_LERROR,"procnetdev: cannot find iface <%s>\n", iface);
		return EINVAL;
	}

	procnetdev_valid = 1;
	return 0;
}

int sample_metrics_procnetdev(ldms_set_t set)
{

	if (procnetdev_valid == 0) {
		return 0;
	}

	if (!pnd_f) {
		ovis_log(__cray_sampler_log, OVIS_LERROR,"procnetdev: filehandle NULL\n");
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
				ldms_metric_set(set,
						metric_table_procnetdev[0],
						&v[0]);
				ldms_metric_set(set,
						metric_table_procnetdev[1],
						&v[1]);
				found++;
			}
		}
	} while(s);

	if (!found) {
		ovis_log(__cray_sampler_log, OVIS_LERROR,"cray_system_sampler: sample_metrics_procnetdev matched no ifaces\n");
		return EINVAL;
	}

	return 0;
}

int sample_metrics_loadavg(ldms_set_t set)
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
			ovis_log(__cray_sampler_log, OVIS_LERROR,"ERR: Issue reading the source file '%s'"
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
			ldms_metric_set(set, metric_table_loadavg[i], &v[i]);
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
