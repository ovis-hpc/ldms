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
 * \file rapl.c
 * \brief sandibridge energy sampling using RAPL (via PAPI interface)
 * \This sampler require msr module to work. To load msr module: modprobe msr
 */

#define _GNU_SOURCE
#include <sys/errno.h>
#include <stdlib.h>
#include <string.h>
#include "ldms.h"
#include "ldmsd.h"
#include "papi.h"

#define MAX_RAPL_EVENTS 64

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static ldms_schema_t schema;
static char* default_schema_name = "rapl";
static long_long* papi_event_val;

static int find_rapl_component()
{
	const PAPI_component_info_t *cmpinfo = NULL;
	int numcmp = PAPI_num_components();
	int cid;
	int rapl_cid = -1;

	for(cid = 0; cid < numcmp; cid++) {
		if ( (cmpinfo = PAPI_get_component_info(cid)) == NULL ) {
			msglog(LDMSD_LERROR, "rapl: failed to get component info from PAPI.\n");
			break;
		}

		if (strstr(cmpinfo->name, "rapl")) {
			if (cmpinfo->disabled)
				msglog(LDMSD_LERROR, "rapl: RAPL component is disabled in PAPI.\n");
			else
				rapl_cid=cid;
			break;
		}
	}

	return rapl_cid;
}

static int create_metric_set(const char* instance_name, const char* schema_name)
{
	int rc, i, rapl_event_count, rapl_cid;
	int event_code = PAPI_NULL;
	int papi_event_set = PAPI_NULL;
	char event_names[MAX_RAPL_EVENTS][PAPI_MAX_STR_LEN];
	char units[MAX_RAPL_EVENTS][PAPI_MIN_STR_LEN];
	int data_type[MAX_RAPL_EVENTS];
	PAPI_event_info_t event_info;
	const PAPI_component_info_t *cmpinfo;

	rc = PAPI_library_init(PAPI_VER_CURRENT);
	if(rc != PAPI_VER_CURRENT) {
		msglog(LDMSD_LERROR, "papi: library init error!\n");
		rc = ENOENT;
		goto err;
	}

	rapl_cid = find_rapl_component();
	if (rapl_cid == -1) {
		msglog(LDMSD_LERROR, "rapl: failed to load rapl component in PAPI.\n");
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

	/*enum all rapl events*/
	event_code = PAPI_NATIVE_MASK;
	rc = PAPI_enum_cmp_event(&event_code, PAPI_ENUM_FIRST, rapl_cid);

	while (rc == PAPI_OK) {
		if (PAPI_event_code_to_name(event_code, event_names[rapl_event_count]) != PAPI_OK ) {
			msglog(LDMSD_LERROR, "rapl: error translating event code 0x%x\n", event_code);
			goto next_event;
		}
		if (PAPI_get_event_info(event_code, &event_info) != PAPI_OK) {
			msglog(LDMSD_LERROR, "rapl: failed to get event info for 0x%x\n", event_code);
			goto next_event;
		}
		if (PAPI_add_event(papi_event_set, event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, "rapl: failed to add event 0x%X to event set\n", event_code);
			goto next_event;
		}

		strncpy(units[rapl_event_count], event_info.units, sizeof(units[0])-1);
		/* buffer must be null terminated to safely use strstr operation on it below */
		units[rapl_event_count][sizeof(units[0])-1] = '\0';
		data_type[rapl_event_count] = event_info.data_type;

		if (ldms_schema_metric_add(schema, event_names[rapl_event_count], LDMS_V_U64) < 0) {
			msglog(LDMSD_LERROR, "rapl: failed to add event %s to metric set.\n", event_names[rapl_event_count]);
			rc = ENOMEM;
			goto err;
		}

		msglog(LDMSD_LINFO, "rapl: event [name: %s, code: 0x%x] has been added.\n", event_names[rapl_event_count], event_code);

		rapl_event_count++;

next_event:
		rc = PAPI_enum_cmp_event(&event_code, PAPI_ENUM_EVENTS, rapl_cid);
	}

	if(rapl_event_count == 0) {
		msglog(LDMSD_LERROR, "rapl: no event has been added.\n");
		rc = ENOENT;
		goto err;
	}

	papi_event_val = calloc(rapl_event_count, sizeof(uint64_t));
	if(papi_event_val  == NULL) {
		msglog(LDMSD_LERROR, "rapl: failed to allocate papi event read buffer.\n");
		rc = ENOMEM;
		goto err;
	}

	rc = PAPI_start(papi_event_set);
	if(rc != PAPI_OK) {
		msglog(LDMSD_LERROR, "rapl: failed to start papi event set\n");
		rc = ENOMEM;
		goto err;
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		msglog(LDMSD_LERROR, "rapl: failed to create metric set %s.\n", instance_name);
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
 * config name=rapl producer=<producer_name> instance=<instance_name> <schema=<schema_name>>
 *     producer     The component id value.
 *     instance     The set name.
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;
	char *instance_name;
	char *schema_name;
	char *producer_name;

	producer_name = av_value(avl, "producer");
	if (!producer_name){
		msglog(LDMSD_LERROR, "rapl: missing producer\n");
		return ENOENT;
	}

	instance_name = av_value(avl, "instance");
	if (!instance_name) {
		msglog(LDMSD_LERROR, "rapl: missing instance.\n");
		return ENOENT;
	}

	schema_name = av_value(avl, "schema");
	if (!schema_name)
		schema_name = default_schema_name;
	if (strlen(schema_name) == 0){
		msglog(LDMSD_LERROR, "rapl: schema name invalid.\n");
		return EINVAL;
	}

	if (set) {
		msglog(LDMSD_LERROR, "rapl: Set already created.\n");
		return EINVAL;
	}

	rc = create_metric_set(instance_name, schema_name);
	if (rc) {
		msglog(LDMSD_LERROR, "rapl: failed to create a metric set.\n");
		return rc;
	}

	ldms_set_producer_name_set(set, producer_name);

	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int sample(struct ldmsd_sampler *self)
{
	int i;
	int event_count;
	union ldms_value val;
	int papi_event_set;

	if (!set) {
		msglog(LDMSD_LERROR, "rapl: plugin not initialized\n");
		return EINVAL;
	}

	/* Read user data from the first metric */
	papi_event_set = ldms_metric_user_data_get(set, 0);
	event_count = PAPI_num_events(papi_event_set);

	ldms_transaction_end(set);

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

static void term(struct ldmsd_plugin *self)
{
	int papi_event_set; 

	papi_event_set = ldms_metric_user_data_get(set, 0);
	if (PAPI_stop(papi_event_set, papi_event_val) != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to stop event set!\n");
	}

	free(papi_event_val);

	PAPI_destroy_eventset(&papi_event_set);
	PAPI_shutdown();

	if (set)
		ldms_set_delete(set);
	set = NULL;

	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=spapi producer=<producer_name> instance=<instance_name>\n"
		"    producer     The producer name\n"
		"    instance     The set instance name.\n";
}

static struct ldmsd_sampler rapl_plugin = {
	.base = {
		.name = "rapl",
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
	return &rapl_plugin.base;
}
