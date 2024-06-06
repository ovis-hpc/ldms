/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2024 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2024 Open Grid Computing, Inc. All rights reserved.
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
#define _GNU_SOURCE

#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>

/* Must include these three headers */
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"
/* -------------------------------- */


static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static base_data_t base;

#define SAMP "example"
#define MAX_LIST_LEN 10
#define ARRAY_LEN    5

static struct ldms_metric_template_s my_metric_template[] = {
	{            "u64",       0, LDMS_V_U64,        "unit",    1 }, /* 1 because this is a scalar */
	{      "array_u64",       0, LDMS_V_U64_ARRAY,  "unit",    ARRAY_LEN }, /* 5 is the array length */
	{     "list_o_u64",       0, LDMS_V_LIST,           "",    /* set heap_sz later */ },
	{     "record_def",       0, LDMS_V_RECORD_TYPE,    "",    /* set rec_def later */ },
	{ "list_o_records",       0, LDMS_V_LIST,           "",    /* set heap_sz later */ },
	{0},
};
static int my_metric_ids[5]; /* Array of the metric IDs (in the set) of the metrics in my_metric_template.  */

static struct ldms_metric_template_s my_record_template[] = {
	{       "rec_ent_u64",    0, LDMS_V_U64,       "unit", 1},
	{ "rec_ent_array_u64",    0, LDMS_V_U64_ARRAY, "unit", ARRAY_LEN},
	{0}
	/*
	 * Only scalars and arrays can be record entries.
	 */
};
static int record_ent_ids[2]; /* The record entry IDs of 'rec_ent_u64' and 'rec_ent_array_u64' in the record definition. */

static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	int rc;

	schema = base_schema_new(base); /* Create schema and populate common metrics */
	if (!schema) {
		rc = errno;
		msglog(LDMSD_LERROR,
		       SAMP ": The schema '%s' could not be created, errno=%d.\n",
		       base->schema_name, rc);
		goto err;
	}

	/* ------- Fill the missing fields in my_metric_template ---------------*/

	/* ----- list_o_u64 ------*/
	/*
	 * Calculate the list_o_u64 heap size
	 *
	 * 'list_o_u64' is my_metric_template[2].
	 */
	my_metric_template[2].len = ldms_list_heap_size_get(LDMS_V_U64, MAX_LIST_LEN, 1);
	/* -----------------------*/


	/* ----- list_o_records ------- */
	/* Create a record definition */
	/*
	 * Create a record definition
	 *
	 * 'record_def' is my_metric_template[3].
	 */
	my_metric_template[3].rec_def = ldms_record_from_template(my_metric_template[3].name,
								my_record_template,
								record_ent_ids);
	if (!my_metric_template[3].rec_def) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": Error %d: Failed to create record def '%s'\n",
					     rc, my_metric_template[3].name);
		goto err;
	}

	/* Calculate list_o_records heap size */
	/*
	 * 'list_o_records' is my_metric_template[4].
	 */
	my_metric_template[4].len = MAX_LIST_LEN * ldms_record_heap_size_get(my_metric_template[3].rec_def);
	/* ------------------------------*/

	/* The template is completely filled and ready. */
	/*---------------------------------------------------------------------*/

	rc = ldms_schema_metric_add_template(schema, my_metric_template, my_metric_ids);
	if (rc) {
		msglog(LDMSD_LERROR,
		       SAMP ": Error %d: Failed to add scalars and arrays.\n", rc);
		goto err;
	}

	/* At this point, all metrics have been added to `schema` */

	set = base_set_new(base); /* Create an LDMS set */
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

 err:
	return rc;
}

static const char *usage(struct ldmsd_plugin *self)
{
	/*
	 * TODO: Append the string to explain the plugin-specific attributes
	 *
	 * See procnetdev2 as an example
	 */
	return  "config name=" SAMP " " BASE_CONFIG_USAGE;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	base = base_config(avl, SAMP, SAMP, msglog); /* sampler_base object created */
	if (!base) {
		rc = errno;
		goto err;
	}

	/*
	 * TODO: process plugin-specific attributes if any
	 *
	 * For example, char *attr_value = av_value(avl, "attr_name")
	 */

	rc = create_metric_set(base);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}
	return 0;
 err:
	base_del(base);
	return rc;
}

static int sample(struct ldmsd_sampler *self)
{
	static uint64_t mvalue = 1;
	int i, j;

	base_sample_begin(base); /* Map current job information */

	/*
	 * TODO: Get data from source and assign to metrics in the set
	 */

	/* ------------ metric 'u64' --------------- */
	ldms_metric_set_u64(base->set, my_metric_ids[0], mvalue);

	/* metric 'array_u64' */
	for (i = 0; i < ARRAY_LEN; i++) {
		ldms_metric_array_set_u64(base->set, my_metric_ids[1], i, mvalue);
	}

	/* ----------- metric 'list_o_u64' --------------- */
	ldms_mval_t l_o_u64_handle;
	ldms_mval_t l_o_u64_ele_handle;

	l_o_u64_handle = ldms_metric_get(base->set, my_metric_ids[2]); /* Get 'list_o_u64' handle */
	ldms_list_purge(base->set, l_o_u64_handle); /* Purge the list */
	for (i = 0; i <= i % MAX_LIST_LEN; i++) {
		l_o_u64_ele_handle = ldms_list_append_item(base->set, l_o_u64_handle, LDMS_V_U16, 1); /* 1 because it is a scalar. */
		l_o_u64_ele_handle->v_u64 = mvalue;
	}

	/* ----------- metric 'list_o_records' ------------ */
	ldms_mval_t l_o_rec_handle;
	ldms_mval_t rec_handle;

	l_o_rec_handle = ldms_metric_get(base->set, my_metric_ids[4]);
	ldms_list_purge(base->set, l_o_rec_handle);

	for (i = 0; i <= i % MAX_LIST_LEN; i++) {
		rec_handle = ldms_record_alloc(base->set, my_metric_ids[3]); /* my_metric_ids[3] is 'rec_def' */
		ldms_list_append_record(base->set, l_o_rec_handle, rec_handle);

		/* Set record element values */
		ldms_record_set_u64(rec_handle, record_ent_ids[0], mvalue);
		for (j = 0; j < ARRAY_LEN; j++) {
			ldms_record_array_set_u64(rec_handle, record_ent_ids[1], j, mvalue);
		}
	}

	base_sample_end(base);  /* stamp a timestamp */

	mvalue++;
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	/*
	 * The plugin will be unloaded after this call.
	 *
	 * TODO: Clean up any resources the plugin has allocated.
	 *  - Free the memory
	 *  - Close the file descriptors
	 *  - etc
	 */
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler ldmscon2024_sampler = {
	.base = {
		.name = SAMP, /* Sampler plugin name */
		.type = LDMSD_PLUGIN_SAMPLER, /* Always LDMSD_PLUGIN_SAMPLER */
		.usage = usage, /* Return a brief description and attributes to be set */
		.config = config, /* Receive configuration attributes, define the set schema, and create a set */
		.term = term, /* Called before unload the plugin: close any opened files and free memory */
	},
	.get_set = NULL, /* Obsolete -- there is no caller. */
	.sample = sample, /* Sample the metric values from the source */
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f log_fn)
{
	msglog = log_fn;
	set = NULL;
	return &ldmscon2024_sampler.base;
}
