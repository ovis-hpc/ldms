/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2011 Sandia Corporation. All rights reserved.
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
 * \file clock.c
 * \brief simplest example of a data provider. 
 * Also handy for overhead measurement.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
// extra stuff not needed.
#include <stdlib.h> // needed for strtoull processing of comp_id
//#include <stdio.h>
//#include <stdarg.h>
//#include <sys/types.h>
//#include <time.h>
//#include <pthread.h>
#include <string.h> // needed for memcpy in ldms.h unused feature
#include "ldms.h"
#include "ldmsd.h"

static uint64_t counter = 0;
ldms_set_t set = NULL;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
uint64_t comp_id;
const char *metric_name = "null_tick";

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, metric_count;

	metric_count = 0;
	tot_meta_sz = 0;
	tot_data_sz = 0;

	/* set size calculation. */
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	if (rc)
		return rc;

	tot_meta_sz += meta_sz;
	tot_data_sz += data_sz;
	metric_count++;

	/* Create the metric set */
	rc = ENOMEM;
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;
	/*
	 * Process the file again to define all the metrics.
	 */

	int metric_no = 0;
	metric_table[metric_no] =
		ldms_add_metric(set, metric_name, LDMS_V_U64);
	if (!metric_table[metric_no]) {
		rc = ENOMEM;
		goto err;
	}
	ldms_set_user_data(metric_table[metric_no], comp_id);
	return 0;

 err:
	ldms_set_release(set);
	return rc;
}

/**
 * \brief Configuration
 *
 * config name=clock component_id=<comp_id> set=<setname>
 *     comp_id     The component id value.
 *     setname     The set name.
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtoull(value, NULL, 0);

	value = av_value(avl, "set");
	if (value)
		create_metric_set(value);

	return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	int rc;
	int metric_no;
	union ldms_value v;

	if (!set) {
		msglog("clock: plugin not initialized\n");
		return EINVAL;
	}

	metric_no = 0;
	v.v_u64 = counter;
	counter++;
	rc = ldms_begin_transaction(set);
	if (rc)
		return rc;
	ldms_set_metric(metric_table[metric_no], &v);
	rc = ldms_end_transaction(set);
	if (rc)
		return rc;
	return 0;
}

static void term(void)
{
	if (set)
		ldms_destroy_set(set);
	set = NULL;
}

static const char *usage(void)
{
	return  "config name=clock component_id=<comp_id> set=<setname>\n"
		"    comp_id     The component id value.\n"
		"    setname     The set name.\n";
}

static struct ldmsd_sampler clock_plugin = {
	.base = {
		.name = "clock",
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
	return &clock_plugin.base;
}
