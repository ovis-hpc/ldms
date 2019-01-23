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
#include "cray_sampler_base.h"


int sample_metrics_vmstat(cray_sampler_inst_t inst, ldms_set_t set)
{
	char lbuf[256];
	char metric_name[128];
	int found_metrics;
	char* s;
	union ldms_value v;
	int j, rc = 0;
	FILE *v_f;

	/* open and close each time */
	v_f = fopen(VMSTAT_FILE, "r");
	if (!v_f)
		return 0;

	found_metrics = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), v_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &v.v_u64);
		if (rc != 2) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Issue reading the source file '%s'\n",
				 VMSTAT_FILE);
			rc = EINVAL;
			goto out;
		}
		for (j = 0; j < NUM_VMSTAT_METRICS; j++){
			if (!strcmp(metric_name, VMSTAT_METRICS[j])){
				ldms_metric_set(set,
					inst->metric_table_vmstat[j], &v);
				found_metrics++;
				break;
			}
		}
	} while (s);

	if (found_metrics != NUM_VMSTAT_METRICS) {
		rc = EINVAL;
	}
out:
	if (v_f)
		fclose(v_f);

	return rc;

}


int sample_metrics_vmcf(cray_sampler_inst_t inst, ldms_set_t set)
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
	FILE *v_f;

	/* open and close each time */
	v_f = fopen(VMSTAT_FILE, "r");
	if (!v_f)
		return 0;

	found_metrics = 0;
	found_submetrics = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), v_f);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &v.v_u64);
		if (rc != 2) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Issue reading the source file '%s'\n",
				 VMSTAT_FILE);
			rc = EINVAL;
			goto out;
		}
		if (found_metrics < NUM_VMSTAT_METRICS){
			for (j = 0; j < NUM_VMSTAT_METRICS; j++){
				if (strcmp(metric_name, VMSTAT_METRICS[j]))
					continue;
				ldms_metric_set(set,
						inst->metric_table_vmstat[j],
						&v);
				found_metrics++;
				if ((found_metrics == NUM_VMSTAT_METRICS) &&
				    (found_submetrics == NUM_VMCF_METRICS)){
					done = 1;
					break;
				}
				break;
			}
		}
		if (found_submetrics < NUM_VMCF_METRICS){
			for (j = 0; j < NUM_VMCF_METRICS; j++){
				if (strcmp(metric_name, VMCF_METRICS[j]))
					continue;
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
	} while (s && !done);

	if (found_submetrics == NUM_VMCF_METRICS) {
		/* treating the order like its well known
		 * (nr_free_pages + nr_file_pages
		 *     + nr_slab_reclaimable - nr_shmem) * 4
		 */
		v.v_u64 = (vmcf[0] + vmcf[1] + vmcf[2] - vmcf[3]) * 4;
		ldms_metric_set(set, inst->metric_table_current_freemem[0], &v);
	} else {
		rc = EINVAL;
		goto out;
	}

	if (found_metrics != NUM_VMSTAT_METRICS) {
		rc = EINVAL;
		goto out;
	}

	rc = 0;

out:
	if (v_f)
		fclose(v_f);
	return rc;

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



int sample_metrics_kgnilnd(cray_sampler_inst_t inst, ldms_set_t set)
{
	char lbuf[256];
	char metric_name[128];
	int found_metrics;
	char* s;
	union ldms_value v;
	int j, rc;
	FILE *k_f;

	k_f = fopen(KGNILND_FILE, "r");
	if (!k_f) {
		rc = 0;
		goto out;
	}

	if (fseek(k_f, 0, SEEK_SET) != 0){
		/* perhaps the file handle has become invalid.
		 * close it so it will reopen on the next round
		 * TODO: zero out the values
		 */
		INST_LOG(inst, LDMSD_LDEBUG, "css -kgnilnd seek failed\n");
		rc = EINVAL;
		goto out;
	}

	found_metrics = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), k_f);
		if (!s)
			break;

		if (s[0] == '\n')
			continue;

		rc = sscanf(s, "%[^:]: %"PRIu64, metric_name, &v.v_u64);
		if (rc != 2) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Bad source format, file: '%s'. "
				 "Expecting `name: value` format, but got: "
				 "%s\n" ,
				 KGNILND_FILE, s);
			rc = EINVAL;
			goto out;
		}

		replace_space(metric_name);

		for (j = 0; j < NUM_KGNILND_METRICS; j++){
			if (strcmp(metric_name, KGNILND_METRICS[j]))
				continue;
			ldms_metric_set(set, inst->metric_table_kgnilnd[j], &v);
			found_metrics++;
			break;
		}
	} while (s);

	if (found_metrics != NUM_KGNILND_METRICS){
		rc = EINVAL;
		goto out;
	}

	rc = 0;

out:
	if (k_f)
		fclose(k_f);
	if (rc) {
		/* reset metrics in case of an error */
		v.v_u64 = 0;
		for (j = 0; j < NUM_KGNILND_METRICS; j++)
			ldms_metric_set(set, inst->metric_table_kgnilnd[j], &v);
	}
	return rc;

}

int sample_metrics_current_freemem(cray_sampler_inst_t inst, ldms_set_t set)
{
	/* only has 1 val, no label */
	char lbuf[256];
	int found_metrics;
	char* s;
	union ldms_value v;
	int rc;
	FILE *cf_f;


	/* Close and open each time */
	cf_f = fopen(CURRENT_FREEMEM_FILE, "r");
	if (!cf_f)
		return 0;

	found_metrics = 0;
	s = fgets(lbuf, sizeof(lbuf), cf_f);
	if (s) {
		rc = sscanf(lbuf, "%"PRIu64"\n", &v.v_u64);
		if (rc != 1) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Issue reading the source file '%s'\n",
				 CURRENT_FREEMEM_FILE);
			rc = EINVAL;
			goto out;
		}
		ldms_metric_set(set, inst->metric_table_current_freemem[0], &v);
		found_metrics++;
	}

	if (found_metrics != NUM_CURRENT_FREEMEM_METRICS){
		return EINVAL;
		goto out;
	}

	rc = 0;

out:
	if (cf_f)
		fclose(cf_f);
	return rc;

}


int sample_metrics_energy(cray_sampler_inst_t inst, ldms_set_t set)
{
	char lbuf[256];
	char* s;
	union ldms_value v;
	int i, rc, rcout;
	FILE *f;

	/* note - not counting how many found since these are all
	 * separate sources. */
	rcout = 0;
	for (i = 0; i < NUM_ENERGY_METRICS; i++){
		f = fopen(ENERGY_FILES[i], "r");
		if (!f)
			continue;
		v.v_u64 = 0;
		s = fgets(lbuf, sizeof(lbuf), f);
		if (s) {
			//Ignore the unit
			rc = sscanf(lbuf, "%"PRIu64"\n", &v.v_u64);
			if (rc != 1) {
				INST_LOG(inst, LDMSD_LERROR,
					 "Issue reading the source file '%s'\n",
					 ENERGY_FILES[i]);
				rcout = EINVAL;
			}

		}
		fclose(f);
		f = NULL;
		ldms_metric_set(set, inst->metric_table_energy[i], &v);
	}

	return rcout;

}

int procnetdev_setup(cray_sampler_inst_t inst)
{
	/** need tx rx bytes for ipogif0 interface only */
	inst->procnetdev_valid = 0;

	if (!inst->pnd_f) {
		INST_LOG(inst, LDMSD_LERROR,"procnetdev: filehandle NULL\n");
		return EINVAL;
	}

	char lbuf[256];
	char* s;
	int count = -1;

	/* assume on product system ifaces and order do not change w/o reboot */
	inst->idx_iface = -1;
	do {
		s = fgets(lbuf, sizeof(lbuf), inst->pnd_f);
		if (!s)
			break;
		count++;
		if (strstr(lbuf,iface))
			inst->idx_iface = count; /* continue past eof */
	} while(s);

	if (inst->idx_iface == -1){
		INST_LOG(inst, LDMSD_LERROR,
			 "procnetdev: cannot find iface <%s>\n", iface);
		return EINVAL;
	}

	inst->procnetdev_valid = 1;
	return 0;
}

int sample_metrics_procnetdev(cray_sampler_inst_t inst, ldms_set_t set)
{

	if (!inst->procnetdev_valid) {
		return 0;
	}

	if (!inst->pnd_f) {
		INST_LOG(inst, LDMSD_LERROR,"procnetdev: filehandle NULL\n");
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
	fseek(inst->pnd_f, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), inst->pnd_f);
		if (!s)
			break;
		i++;
		if (i == inst->idx_iface){
			rc = sscanf(lbuf, "%[^:]%*c %" PRIu64 " %*"
				    PRIu64 " %*" PRIu64 " %*" PRIu64 " %*"
				    PRIu64 " %*" PRIu64 " %*" PRIu64 " %*"
				    PRIu64 " %" PRIu64 "",
				    curriface, &v[0].v_u64, &v[1].v_u64);
			if (!strstr(curriface,iface) || (rc != 3))
				continue;
			ldms_metric_set(set, inst->metric_table_procnetdev[0],
					&v[0]);
			ldms_metric_set(set, inst->metric_table_procnetdev[1],
					&v[1]);
			found++;
		}
	} while(s);

	if (!found) {
		INST_LOG(inst, LDMSD_LERROR,
			 "sample_metrics_procnetdev matched no ifaces\n");
		return EINVAL;
	}

	return 0;
}

int sample_metrics_loadavg(cray_sampler_inst_t inst, ldms_set_t set)
{
	/* 0.12 0.98 0.86 1/345 24593. well known: want fields 1, 2, and both of
	 * 4 in that order.*/

	char lbuf[256];
	int found_metrics;
	char* s;
	union ldms_value v[4];
	float vf[3];
	int vi[3];
	int i, rc;
	FILE *l_f;

	/* open and close each time */
	l_f = fopen(LOADAVG_FILE, "r");
	if (!l_f)
		return 0;

	found_metrics = 0;
	s = fgets(lbuf, sizeof(lbuf), l_f);
	if (s) {
		rc = sscanf(lbuf, "%f %f %f %d/%d %d\n",
			    &vf[0], &vf[1], &vf[2], &vi[0], &vi[1], &vi[2]);
		if (rc != 6) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Issue reading the source file '%s' "
				 "(rc=%d)\n", LOADAVG_FILE, rc);
			rc = EINVAL;
			goto out;
		}
		v[0].v_u64 = vf[0]*100;
		v[1].v_u64 = vf[1]*100;
		v[2].v_u64 = vi[0];
		v[3].v_u64 = vi[1];
		for (i = 0; i < 4; i++){
			ldms_metric_set(set, inst->metric_table_loadavg[i],
					&v[i]);
		}
		found_metrics=4;
	}

	if (found_metrics != NUM_LOADAVG_METRICS){
		rc = EINVAL;
		goto out;
	}

	rc = 0;

out:
	if (l_f)
		fclose(l_f);

	return rc;
}
