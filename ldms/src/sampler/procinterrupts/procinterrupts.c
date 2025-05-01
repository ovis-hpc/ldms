/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2016,2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file procinterrupts.c
 * \brief /proc/interrupts data provider
 */
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "sampler_base.h"

#define PROC_FILE "/proc/interrupts"
static char *procfile = PROC_FILE;
static ldms_set_t set = NULL;
static FILE *mf = NULL;
static int nprocs;
#define SAMP "procinterrupts"
static int metric_offset;
static base_data_t base;

static ovis_log_t mylog;

static int getNProcs(char buf[])
{
	int nproc = 0;
	char* pch;
	char *sp = NULL;
	pch = strtok_r(buf, " ", &sp);
	while (pch != NULL) {
		if (pch[0] == '\n') {
			break;
		}
		if (pch[0] != ' ') {
			nproc++;
		}
		pch = strtok_r(NULL, " ", &sp);
	}

	return nproc;
}


static char lbuf[4096];		/* NB: Some machines have many cores */
static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	int rc, i;
	char *s;
	char metric_name[128];
	char beg_name[64];

	mf = fopen(procfile, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open the " SAMP " file '%s'...exiting\n",
				procfile);
		return ENOENT;
	}


	schema = base_schema_new(base);
	if (!schema) {
		rc = errno;
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, rc);
		goto err;
	}

	/* Location of first metric from proc file */
	metric_offset = ldms_schema_metric_count_get(schema);

	/*
	 * Process the file to define all the metrics.
	 */
	fseek(mf, 0, SEEK_SET);
	/* first line is the cpu list */
	s = fgets(lbuf, sizeof(lbuf), mf);
	nprocs = getNProcs(lbuf);
	if (nprocs <= 0) {
		ovis_log(mylog, OVIS_LINFO, "Bad number of CPU.\n");
		fclose(mf);
		return EINVAL;
	}

	while(s) {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		int currcol = 0;
		char *sp = NULL;
		char* pch = strtok_r(lbuf, " ", &sp);
		while (pch != NULL && currcol <= nprocs) {
			if (pch[0] == '\n') {
				break;
			}
			if (currcol == 0) {
				/* Strip the colon from metric name if present */
				i = strlen(pch);
				if (i && pch[i-1] == ':')
					pch[i-1] = '\0';
				strcpy(beg_name, pch);
			} else {
				snprintf(metric_name, 128, "irq.%s#%d",
						beg_name, (currcol-1));
				rc = ldms_schema_metric_add(schema, metric_name,
							    LDMS_V_U64);
				if (rc < 0) {
					rc = ENOMEM;
					goto err;
				}
			}
			currcol++;
			pch = strtok_r(NULL, " ", &sp);
		}
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

err:
	if (mf)
		fclose(mf);
	mf = NULL;
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return "config name=" SAMP " " BASE_CONFIG_USAGE;
}

/**
 * \brief Configuration
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc = 0;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}


	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base)
		goto err;

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create the metric set.\n");
		goto err;
	}
	return 0;

err:
	base_del(base);
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
	int rc;
	int metric_no;
	char *s;
	union ldms_value v;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	metric_no = metric_offset;
	fseek(mf, 0, SEEK_SET);
	/* first line is the cpu list */
	s = fgets(lbuf, sizeof(lbuf), mf);

	while(s) {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		int currcol = 0;
		char * sp = NULL;
		char* pch = strtok_r(lbuf, " ", &sp);
		while (pch != NULL && currcol <= nprocs) {
			if (pch[0] == '\n') {
				break;
			}
			if (pch[0] != ' ') {
				if (currcol != 0) {
					char* endptr;
					unsigned long long int l1;
					l1 = strtoull(pch,&endptr,10);
					if (endptr != pch) {
						v.v_u64 = l1;
						ldms_metric_set(set, metric_no, &v);
						metric_no++;
					} else {
						ovis_log(mylog, OVIS_LERROR,
							"bad val <%s>\n",pch);
						rc = EINVAL;
						goto out;
					}
				}
				currcol++;
			}
			pch = strtok_r(NULL, " ", &sp);
		} /* end while(strtok) */
	} while (s);
	rc = 0;
out:
	base_sample_end(base);
	return rc;
}


static void term(ldmsd_plug_handle_t handle)
{
	if (mf)
		fclose(mf);
	mf = 0;
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
        set = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
