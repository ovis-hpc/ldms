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
#include "lustre_metrics.h"

/* LUSTRE SPECIFIC */
struct str_list_head *llite_str_list = NULL;
struct lustre_metric_src_list lms_list = {0};

/* Defined in cray_sampler_base.c */
extern ovis_log_t __cray_sampler_log;

int add_metrics_lustre(ldms_schema_t schema)
{
	struct str_list *sl;
	int i;
	int rc;
	int count = 0;
	char name[CSS_LUSTRE_NAME_MAX];
	static char path_tmp[4096];
	static char suffix[128];

	LIST_FOREACH(sl, llite_str_list, link) {
		ovis_log(__cray_sampler_log, OVIS_LDEBUG, "should be adding metrics for <%s>\n",
		      sl->str);
		snprintf(path_tmp, sizeof(path_tmp), "/proc/fs/lustre/llite/%s-*/stats", sl->str);
		snprintf(suffix, sizeof(suffix), "#llite.%s", sl->str);
		rc = stats_construct_routine(schema, path_tmp,
				"client.lstats.", suffix, &lms_list, LUSTRE_METRICS,
				LUSTRE_METRICS_LEN);
		if (rc) {
			ovis_log(__cray_sampler_log, OVIS_LDEBUG, "%s: returning error: %d from stats_construct_routine for %s\n",
			       __FILE__, rc, path_tmp);
			return rc;
		}
	}

	return 0;
}

int handle_llite(const char *llite)
{
	struct str_list_head *head = construct_str_list(llite);
	llite_str_list = construct_str_list(llite);
	if (!llite_str_list)
		return errno;
	return 0;
}

int sample_metrics_lustre(ldms_set_t set)
{
	struct lustre_metric_src *lms;
	int retrc = 0;
	int rc;

	LIST_FOREACH(lms, &lms_list, link) {
		rc = lms_sample(set, lms);
		if (rc) //go ahead and sample the other ones
			retrc = rc;
	}

	return retrc;
}
