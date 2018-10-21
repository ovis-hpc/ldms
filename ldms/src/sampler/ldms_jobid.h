/**
 * Copyright (c) 2016-2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
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
 * \file ldms_jobid.c
 * \brief Provide Job Information sampling and service for
 * other samplers from any resource manager that can write
 * a text file at job start.
 * Does not support job-start and job-end timestamp collection,
 * but provides dummy time values == 1.
 */
#ifndef ldms_jobid_h_seen
#define ldms_jobid_h_seen

#include <stdbool.h>
#include <stdint.h>
#include "ldmsd.h"

#define LJI_USER_NAME_MAX 33
struct ldms_job_info {
	uint64_t jobid;
	uint64_t uid;
	uint64_t appid;
	char user[LJI_USER_NAME_MAX];
};

#define LJI_jobid 0x1
#define LJI_uid 0x2
#define LJI_user 0x4
#define LJI_appid 0x8
#define LJI_all (LJI_jobid|LJI_uid|LJI_user|LJI_appid)

/* Default names all samplers will use for job info metrics. */
/** uint64_t holding integer identifier from resource manager. */
#define LJI_JOBID_METRIC_NAME LDMSD_JOBID
/** uint64_t holding unix uid as accounted by resource manager. */
#define LJI_UID_METRIC_NAME "uid"
/** login name as accounted by resource manager. */
#define LJI_USER_METRIC_NAME "username"
/** application id as accounted by resource manager. */
#define LJI_APPID_METRIC_NAME "app_id"

/** NULL terminated array of job-related metric names. */
extern const char *lji_metric_names[];

/* Create jobid metric with standard name. */
#define LJI_ADD_JOBID(schema) \
	ldms_schema_metric_add(schema, LJI_JOBID_METRIC_NAME, LDMS_V_U64)

/* Create uid metric with standard name. */
#define LJI_ADD_UID(schema) \
	ldms_schema_metric_add(schema, LJI_UID_METRIC_NAME, LDMS_V_U64)

/* Create app_id metric with standard name. */
#define LJI_ADD_APPID(schema) \
	ldms_schema_metric_add(schema, LJI_APPID_METRIC_NAME, LDMS_V_U64)

#ifdef ENABLE_JOBID
/* \brief Get the most recently sampled job information.
\param ji job info to fill according to flags.
\param flags combination of bit flags for fields wanted.
\return 0 on success or an errno value, in which case content
	of fields requested by flags becomes undefined.
*/
int ldms_job_info_get(struct ldms_job_info *ji, unsigned flags);

/*
 * A set of macros for inserting jobid column in most samplers.
 * Currently default is to have jobids.
 */

#define LJI_GLOBALS \
	static bool with_jobid = true

/* Compute the with_jobid config flag. Default is true if jobid enabled. */
#define LJI_CONFIG(value,avl) \
        value = av_value(avl, "with_jobid"); \
        if (value && strcmp(value,"0")==0) \
                with_jobid = false

/* get the most recently updated value from the other plugin. */
#define LJI_SAMPLE(set, metric_number) \
        if (with_jobid) { \
		struct ldms_job_info ji; \
		union ldms_value lv; \
		if (ldms_job_info_get(&ji,LJI_jobid) ) \
			lv.v_u64 = 0; \
		else \
			lv.v_u64 = ji.jobid; \
                ldms_metric_set(set, metric_number, &lv); \
        } else { \
                ldms_metric_set_u64(set, metric_number, 0); \
	}

/* String to include in usage strings. */
#define LJI_DESC \
	"    <jid>         1/0: lookup job info, or report 0. default 1 (yes).\n"

#else
#include <errno.h>
#define ldms_job_info_get(x,y) EINVAL
#define LJI_GLOBALS \
	static bool with_jobid = false
#define LJI_CONFIG(x,y)
#define LJI_SAMPLE(x,y)
#define LJI_DESC
#endif /* disabled: without ENABLE_JOBID. disabling not recommended for use with sos */

#endif /* ldms_jobid_h_seen */
