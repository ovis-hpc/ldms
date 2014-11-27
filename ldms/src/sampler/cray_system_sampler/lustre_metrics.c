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
#include "lustre_metrics.h"

/* LUSTRE SPECIFIC */
struct str_map *lustre_idx_map = NULL;
struct lustre_svc_stats_head lustre_svc_head = {0};

int get_metric_size_lustre(size_t *m_sz, size_t *d_sz,
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


int add_metrics_lustre(ldms_set_t set, int comp_id,
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

int sample_metrics_lustre(ldmsd_msg_log_f msglog)
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


