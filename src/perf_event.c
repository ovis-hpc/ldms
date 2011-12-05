/*
 * Copyright (c) 2011 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2011 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
/*
 * This is the kernel perf_event provider
 */
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include "papi.h"
#include "ldms.h"
#include "ldmsd.h"

#define LOGFILE "/var/log/perf_event.log"

static ldms_set_t sampled_event_set;
static ldms_set_t avail_event_set;
static ldmsd_msg_log_f msglog;

static int papi_event_set = PAPI_NULL;
static long long papi_event_values[PAPI_MAX_PRESET_EVENTS];
static int papi_sampled_count;	/* number of events in the papi_event_set */
static ldms_metric_t metric_table[PAPI_MAX_PRESET_EVENTS];
static int event_table[PAPI_MAX_PRESET_EVENTS];

static char papi_msg_buf[32];
static char event_name[PAPI_MAX_STR_LEN];

static void update_metric_table()
{
	int i, rc;
	papi_sampled_count = PAPI_MAX_PRESET_EVENTS;
	rc = PAPI_list_events(papi_event_set, event_table, &papi_sampled_count);
	if (rc) {
		msglog("perf_event: PAPI error %s\n", papi_msg_buf);
		return;
	}
	for (i = 0; i < papi_sampled_count; i++) {
		rc = PAPI_event_code_to_name(event_table[i], event_name);
		if (!rc)
			metric_table[i] = ldms_get_metric(sampled_event_set,
							  event_name);
		else
			metric_table[i] = NULL;
	}
}

char config_arg[LDMS_MAX_CONFIG_STR_LEN];
static int config(char *config_str)
{
	enum {
		ADD_EVENT,
		REMOVE_EVENT,
		ATTACH_EVENTSET,
		DETACH_EVENTSET,
		START,
		STOP,
		RESET
	} action;
	int event_no, rc;
	ldms_metric_t m;
	int pid;

	if (!sampled_event_set) {
		msglog("perf_event: plugin not initilialized.\n");
		return EINVAL;
	}
	if (0 == strncmp(config_str, "add", 3))
		action = ADD_EVENT;
	else if (0 == strncmp(config_str, "remove", 6))
		action = REMOVE_EVENT;
	else if (0 == strncmp(config_str, "attach", 6))
		action = ATTACH_EVENTSET;
	else if (0 == strncmp(config_str, "detach", 6))
		action = DETACH_EVENTSET;
	else if (0 == strncmp(config_str, "start", 5))
		action = START;
	else if (0 == strncmp(config_str, "stop", 4))
		action = STOP;
	else if (0 == strncmp(config_str, "reset", 5))
		action = RESET;
	else {
		msglog("perf_event: Invalid configuration string '%s'\n",
		       config_str);
		return EINVAL;
	}
	switch (action) {
	case ADD_EVENT:
		sscanf(config_str, "add=%s", config_arg);
		rc = PAPI_event_name_to_code(config_arg, &event_no);
		if (rc)
			break;
		rc = PAPI_add_event(papi_event_set, event_no);
		if (rc)
			break;
		m = ldms_get_metric(avail_event_set, config_arg);
		if (m)
			ldms_set_u64(m, 1);
		else
			msglog("perf_event: Internal error uncrecognized event");
		update_metric_table();
		break;
	case REMOVE_EVENT:
		sscanf(config_str, "remove=%s", config_arg);
		rc = PAPI_event_name_to_code(config_arg, &event_no);
		if (rc)
			break;
		rc = PAPI_remove_event(papi_event_set, event_no);
		if (rc)
			break;
		m = ldms_get_metric(avail_event_set, config_arg);
		if (m)
			ldms_set_u64(m, 0);
		else
			msglog("perf_event: Internal error uncrecognized event");
		update_metric_table();
		break;
	case ATTACH_EVENTSET:
		sscanf(config_str, "attach=%d", &pid);
		rc = PAPI_attach(papi_event_set, pid);
		break;
	case DETACH_EVENTSET:
		rc = PAPI_detach(papi_event_set);
		break;
	case START:
		rc = PAPI_start(papi_event_set);
		break;
	case STOP:
		rc = PAPI_stop(papi_event_set, papi_event_values);
		break;
	case RESET:
		rc = PAPI_reset(papi_event_set);
		break;
	}
	if (rc) {
		PAPI_perror(rc, papi_msg_buf, sizeof papi_msg_buf);
		msglog("perf_event: PAPI error %s\n", papi_msg_buf);
	}

	return rc;
}

static ldms_set_t get_set()
{
	return sampled_event_set;
}

/*
 * Create a metric set that lists the metric names and their masks
 */
ldms_set_t create_event_set(const char *set_name)
{
	int i;
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int metric_count;
	PAPI_event_info_t info;
	int rc;
	ldms_set_t new_set;
	
	rc = PAPI_library_init( PAPI_VER_CURRENT );
	if ( rc != PAPI_VER_CURRENT ) {
		msglog("perf_event: PAPI version error.\n");
		return NULL;
	}
	metric_count = 0;
	tot_meta_sz = tot_data_sz = 0;
	for ( i = 0; i < PAPI_MAX_PRESET_EVENTS; i++ ) {
		if ( PAPI_get_event_info( PAPI_PRESET_MASK | i, &info ) != PAPI_OK )
			continue;
		if ( !( info.count ) )
			continue;
		rc = ldms_get_metric_size(info.symbol, LDMS_V_U64, &meta_sz, &data_sz);
		if (rc) {
			msglog("perf_event: Error computing metric size for %s.\n", info.symbol);
			continue;
		}
		tot_meta_sz += meta_sz;
		tot_data_sz += data_sz;
		metric_count++;
	}
	rc = ldms_create_set(set_name, tot_meta_sz, tot_data_sz, &new_set);
	if (rc) {
		msglog("perf_event: Could not create the perf_event directory.\n");
		return NULL;
	}
	/* Add a metric for each available counter */
	for ( i = 0; i < PAPI_MAX_PRESET_EVENTS; i++ ) {
		ldms_metric_t *m;
		if ( PAPI_get_event_info( PAPI_PRESET_MASK | i, &info ) != PAPI_OK )
			continue;
		if ( !( info.count ) )
			continue;
		m = ldms_add_metric(new_set, info.symbol, LDMS_V_U64);
		if (!m)
			msglog("perf_event: Could not create metric for event %s.\n", info.symbol);
		ldms_set_u64(m, 0);
	}
	return new_set;
}

static int init(const char *path)
{
	/* Destroy any previously created set */
	if (sampled_event_set)
		ldms_set_release(sampled_event_set);
	/* Create the metric set */
	sampled_event_set = create_event_set(path);
	if (sampled_event_set)
		return 0;
	return -1;
}

static int sample(void)
{
	int rc, i;
	if (!sampled_event_set)
		return -1;
	rc = PAPI_read(papi_event_set, papi_event_values);
	if (rc) {
		PAPI_perror(rc, papi_msg_buf, sizeof papi_msg_buf);
		msglog("perf_event: PAPI error %s\n", papi_msg_buf);
	}
	for (i = 0; i < papi_sampled_count; i++)
		if (metric_table[i])
			ldms_set_u64(metric_table[i], papi_event_values[i]);
	return 0;
}

static void term(void)
{
	ldms_set_release(avail_event_set);
	ldms_set_release(sampled_event_set);
}


static struct ldms_plugin perf_event_plugin = {
	.name = "perf_event",
	.init = init,
	.term = term,
	.config = config,
	.get_set = get_set,
	.sample = sample,
};

struct ldms_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	if (!avail_event_set)
		avail_event_set = create_event_set("@active_events");
	if (papi_event_set == PAPI_NULL) {
		int rc = PAPI_create_eventset( &papi_event_set );
		if ( rc != PAPI_OK ) {
			msglog("perf_event: PAPI create event failed with error %d\n", rc);
			return NULL;
		}
	}
	msglog("perf_event: plugin loaded\n");
	return &perf_event_plugin;
}
