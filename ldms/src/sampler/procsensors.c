/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012-2016 Sandia Corporation. All rights reserved.
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
 * \file procsensors.c
 * \brief reads from proc the data that populates lm sensors (in*_input, fan*_input, temp*_input)
 *
 * NOTE: data files have to be opened and closed on each file in sys for the data vaules to change.
 * The actual functionality of the data gathering by the system takes time on systems.
 * Sample stores the data locally and then writes it out so that the collection will not occur during
 * partial set. There is therefore some slop in the actaul time for the data point.
 *
 * filename will be the variable name. mysql inserter will have to convert names and downselect which ones
 * to record.
 *
 * FIXME: decideif multipliers should go here....
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

//FIXME: make this a parameter later..
static char* procsensorsfiledir = "/sys/devices/pci0000:00/0000:00:01.1/i2c-1/1-002f/";
const static int vartypes = 3;
const static char* varnames[] = {"in", "fan", "temp"};
const static int varbounds[] = {0, 9, 1, 9, 1, 6};
static ldms_set_t set;
static int metric_count; /* now global */
static uint64_t* metric_values;
static ldmsd_msg_log_f msglog;
#define SAMP "procsensors"
static int metric_offset;
static base_data_t base;


static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	int rc, i, j;
	char metric_name[128];
	rc = ENOMEM;

	schema = base_schema_new(base);
	if (!schema) {
		msglog(LDMSD_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		goto err;
	}

	/* Location of first metric from proc/meminfo file */
	metric_offset = ldms_schema_metric_count_get(schema);


	/*
	 * Process file to define all the metrics.
	 */

	metric_count = 0;
	for (i = 0; i < vartypes; i++){
		for (j = varbounds[2 * i]; j <= varbounds[2 * i + 1]; j++){
			snprintf(metric_name, 127,
					"%s%d_input",varnames[i],j);
			rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
			if (rc < 0) {
				rc = ENOMEM;
				goto err;
			}
			metric_count++;
		}
	}

	metric_values = calloc(metric_count, sizeof(uint64_t));
	if (!metric_values)
		goto err;

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

err:
	return rc;
}


static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc;

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	base = base_config(avl, SAMP, SAMP, msglog);
	if (!base)
		goto err;

	rc = create_metric_set(base);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create the metric set.\n");
		goto err;
	}

	return 0;

err:
	base_del(base);
	return rc;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int sample(struct ldmsd_sampler *self)
{
	int rc;
	int metric_no;
	char *s;
	char procfile[256];
	char lbuf[20];
	union ldms_value v;
	struct timespec time1;
	int i, j;
	FILE *mf;

	base_sample_begin(base);
	metric_no = 0;
	for (i = 0; i < vartypes; i++){
		for (j = varbounds[2*i]; j <= varbounds[2*i+1]; j++){
			snprintf(procfile, 255, "%s/%s%d_input",
					procsensorsfiledir, varnames[i],j);

			//FIXME: do we really want to open and close each one?
			mf = fopen(procfile, "r");
			if (!mf) {
				msglog(LDMSD_LERROR, SAMP "Could not open "
						"the procsensors file "
						"'%s'...exiting\n", procfile);
				rc = ENOENT;
				goto out;
			}
			s = fgets(lbuf, sizeof(lbuf), mf);
			if (!s){
				fclose(mf);
				break;
			}
			rc = sscanf(lbuf, "%"PRIu64 "\n", &metric_values[metric_no]);
			if (rc != 1){
				fclose(mf);
				rc = EINVAL;
				goto out;
			}

			metric_no++;
			fclose(mf);
		}
	}


	/* now do the writeout */

	/* metrics */
	for (i = 0; i < metric_count; i++){
		v.v_u64 = metric_values[i];
		ldms_metric_set(set , (i+metric_offset), &v);
	}

	rc = 0;
out:
	base_sample_end(base);
	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name= " SAMP BASE_CONFIG_USAGE;

}

static struct ldmsd_sampler procsensors_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
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
	set = NULL;
	return &procsensors_plugin.base;
}
