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
 * \file papi.c
 * \brief PAPI data provider
 */
#define _GNU_SOURCE
#include <sys/errno.h>
#include <stdlib.h>
#include <string.h>
#include "ldms.h"
#include "ldmsd.h"
#include "papi.h"

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static ldms_schema_t schema;
static char* default_schema_name = "papi";
static long_long* papi_event_val;

static int create_metric_set(const char* instance_name, const char* schema_name, char* events, uint64_t pid)
{
	int rc, i, event_count;
	int event_code = PAPI_NULL;
	int papi_event_set = PAPI_NULL;
	char *event_name;
	PAPI_event_info_t event_info;

	rc = PAPI_library_init(PAPI_VER_CURRENT);
	if(rc != PAPI_VER_CURRENT) {
		msglog(LDMSD_LERROR, "papi: library init error!\n");
		rc = ENOENT;
		goto err;
	}

	rc = PAPI_create_eventset(&papi_event_set);
	if(rc != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to creat empty event set!\n");
		rc = ENOENT;
		goto err;
	}

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		msglog(LDMSD_LERROR, "papi: failed to creat schema!\n");
		rc = ENOMEM;
		goto err;
	}

	event_name = strtok(events, ",");
	while (event_name) {
		if(PAPI_event_name_to_code(event_name, &event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to get event code of %s\n", event_name);
			goto next_event;
		}
		if(PAPI_query_event(event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to query event 0x%X\n", event_code);
			goto next_event;
		}
		if (PAPI_get_event_info(event_code, &event_info) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to get event info 0x%X\n", event_code);
			goto next_event;
		}
		if(PAPI_add_event(papi_event_set, event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to add event 0x%X to event set\n", event_code);
			goto next_event;
		}

		rc = ldms_schema_metric_add(schema, event_name, LDMS_V_U64);
		if (rc < 0) {
			msglog(LDMSD_LERROR, "papi: failed to add event %s to metric set.\n", event_name);
			rc = ENOMEM;
			goto err;
		}

		msglog(LDMSD_LINFO, "papi: event [name: %s, code: 0x%x] has been added.\n", event_name, event_code);

next_event:
		event_name = strtok(NULL, ",");
	}

	event_count = PAPI_num_events(papi_event_set);
	if(event_count == 0) {
		msglog(LDMSD_LERROR, "papi: no event has been added.\n");
		rc = ENOENT;
		goto err;
	}

	papi_event_val = calloc(event_count, sizeof(uint64_t));
	if(papi_event_val  == NULL) {
		msglog(LDMSD_LERROR, "papi: failed to allocate papi event read buffer.\n");
		rc = ENOMEM;
		goto err;
	}

	rc = PAPI_attach(papi_event_set, pid);
	if(rc != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to attach to process pid=%d.\n", pid);
		rc = ENOENT;
		goto err;
	}

	rc = PAPI_start(papi_event_set);
	if(rc != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to start papi event set\n");
		rc = ENOMEM;
		goto err;
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		msglog(LDMSD_LERROR, "papi: failed to create metric set %s.\n", instance_name);
		rc = errno;
		goto err;
	}

	ldms_metric_user_data_set(set, 0, papi_event_set);

	return 0;

err:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	return rc;
}

/**
 * \brief Configuration
 *
 * config name=spapi producer=<producer_name> instance=<instance_name> pid=<pid> events=<event1,event2,...>
 *     producer     The component id value.
 *     instance     The set name.
 *     pid          The process to attach to.
 *     events       The the name of the hardware counter events 
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;
	uint64_t pid = 0;
	char *pid_str;
	char *producer_name;
	char *instance_name;
	char *schema_name;
	char *events;

	producer_name = av_value(avl, "producer");
	if (!producer_name){
		msglog(LDMSD_LERROR, "papi: missing producer\n");
		return ENOENT;
	}

	instance_name = av_value(avl, "instance");
	if (!instance_name) {
		msglog(LDMSD_LERROR, "papi: missing instance.\n");
		return ENOENT;
	}

	events = av_value(avl, "events");
	if (!events) {
		msglog(LDMSD_LERROR, "papi: missing events.\n");
		return ENOENT;
	}

	pid_str = av_value(avl, "pid");
	if (!pid_str){
		msglog(LDMSD_LERROR, "papi: missing pid.\n");
		return ENOENT;
	}
	pid = strtoull(pid_str, NULL, 0);

	schema_name = av_value(avl, "schema");
	if (!schema_name)
		schema_name = default_schema_name;
	if (strlen(schema_name) == 0){
		msglog(LDMSD_LERROR, "papi: schema name invalid.\n");
		return EINVAL;
	}

	if (set) {
		msglog(LDMSD_LERROR, "papi: Set already created.\n");
		return EINVAL;
	}

	rc = create_metric_set(instance_name, schema_name, events, pid);
	if (rc) {
		msglog(LDMSD_LERROR, "papi: failed to create a metric set.\n");
		return rc;
	}

	ldms_set_producer_name_set(set, producer_name);
	return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	int i;
	int event_count;
	union ldms_value val;
	int papi_event_set;

	if (!set) {
		msglog(LDMSD_LERROR, "papi: plugin not initialized\n");
		return EINVAL;
	}

	/* Read user data from the first metric */
	papi_event_set = ldms_metric_user_data_get(set, 0);
	event_count = PAPI_num_events(papi_event_set);

	ldms_transaction_begin(set);

	if(PAPI_read(papi_event_set, papi_event_val) != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to read event set %d\n", papi_event_set);
	}

	for(i = 0; i < event_count; ++i)
	{
		val.v_u64 = papi_event_val[i];
		ldms_metric_set(set, i, &val);
	}

	ldms_transaction_end(set);

	return 0;
}

static void term(void)
{
	int papi_event_set; 

	papi_event_set = ldms_metric_user_data_get(set, 0);
	if (PAPI_stop(papi_event_set, papi_event_val) != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to stop event set!\n");
	}

	PAPI_destroy_eventset(&papi_event_set);
	PAPI_shutdown();

	if (set)
		ldms_set_delete(set);
	set = NULL;

	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
}

static const char *usage(void)
{
	return  "config name=spapi producer=<producer_name> instance=<instance_name> pid=<pid> events=<event1,event2,...>\n"
		"    producer     The producer name\n"
		"    instance     The set instance name.\n"
		"    pid          The process to attach to.\n"
		"    events       The name of papi events.\n";
}

static struct ldmsd_sampler papi_plugin = {
	.base = {
		.name = "spapi",
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
	return &papi_plugin.base;
}
