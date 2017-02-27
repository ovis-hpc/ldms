/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011, 2016 Open Grid Computing, Inc. All rights reserved.
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
 *	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 *
 *	Redistributions in binary form must reproduce the above
 *	copyright notice, this list of conditions and the following
 *	disclaimer in the documentation and/or other materials provided
 *	with the distribution.
 *
 *	Neither the name of Sandia nor the names of any contributors may
 *	be used to endorse or promote products derived from this software
 *	without specific prior written permission.
 *
 *	Neither the name of Open Grid Computing nor the names of any
 *	contributors may be used to endorse or promote products derived
 *	from this software without specific prior written permission.
 *
 *	Modified source versions must be plainly marked as such, and
 *	must not be misrepresented as being the original software.
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
/*
 * 1- I need to add a better way to communicate instead of file
 * 2- I need to add the app and node id to be stored 
 */

/**
 * \file papi.c
 * \brief PAPI data provider
 */
#define _GNU_SOURCE
#include <sys/errno.h>
#include <stdlib.h>
#include <string.h>
#include <papi.h>
#include "ldms.h"
#include "ldmsd.h"

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static ldms_schema_t schema;
static char* default_schema_name = "papi";
static long_long* papi_event_val;
static char *appname_str;
static char *appname_filename;
static uint64_t pid = 0;
static int attach = 0; /* 0-Sampler not attached yet 1-Sampler is attached to a process */
static int multiplex = 0;
static uint64_t compid;

static int create_metric_set(const char* instance_name, const char* schema_name, char* events)
{
	int rc, i, event_count;
	int event_code = PAPI_NULL;
	int papi_event_set = PAPI_NULL;
	char *event_name;
	char* status;
	PAPI_event_info_t event_info;
	union ldms_value v;

	rc = PAPI_library_init(PAPI_VER_CURRENT);
	if (rc != PAPI_VER_CURRENT) {
		msglog(LDMSD_LERROR, "papi: library init error! %d: %s\n", rc, PAPI_strerror(rc));
		rc = ENOENT;
		goto err;
	}

	/* Added support to multiplex event set */
	/* Enable and initialize multiplex support */
	if (multiplex) { /* check if the configuration have multiplex option enabled */
		rc = PAPI_multiplex_init();
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to initialize multiplexing!\n");
			rc = ENOENT;
			goto err;
		}
	}

	rc = PAPI_create_eventset(&papi_event_set);
	if (rc != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to create empty event set!\n");
		rc = ENOENT;
		goto err;
	}

	/* Explicitly bind event set to cpu component.	PAPI documentation states that 
	 * this must be done after PAPI_create_eventset, but before calling PAPI_set_multiplex.
	 * The argument 0 binds to cpu component.
	 */
	rc = PAPI_assign_eventset_component(papi_event_set, 0);
	if (rc != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to bind papi to cpu component!\n");
		rc = ENOENT;
		goto err;
	}

	/* Convert papi_event_set to a multiplexed event set */
	rc = PAPI_set_multiplex(papi_event_set);
	if (rc != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to convert event set to multiplexed!\n");
		rc = ENOENT;
		goto err;
	}

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		msglog(LDMSD_LERROR, "papi: failed to create schema!\n");
		rc = ENOMEM;
		goto err;
	}

	/* Add component id */
	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}

	/*
	 * Create two metrics: application name, jobid, and username
	 */
	rc = ldms_schema_metric_array_add(schema, "appname", LDMS_V_CHAR_ARRAY, 256);
	if (rc < 0) {
		msglog(LDMSD_LERROR, "papi: failed to add application name to metric set.\n");
		rc = ENOMEM;
		goto err;
	}
	rc = ldms_schema_metric_array_add(schema, "jobid", LDMS_V_CHAR_ARRAY, 256);
	if (rc < 0) {
		msglog(LDMSD_LERROR, "papi: failed to add jobid to metric set.\n");
		rc = ENOMEM;
		goto err;
	}
	rc = ldms_schema_metric_array_add(schema, "username", LDMS_V_CHAR_ARRAY, 256);
	if (rc < 0) {
		msglog(LDMSD_LERROR, "papi: failed to add username to metric set.\n");
		rc = ENOMEM;
		goto err;
	}

	event_name = strtok_r(events, ",", &status);
	while (event_name) {
		if (PAPI_event_name_to_code(event_name, &event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to get event code of %s\n", event_name);
			goto next_event;
		}
		if (PAPI_query_event(event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to query event 0x%X\n", event_code);
			goto next_event;
		}
		if (PAPI_get_event_info(event_code, &event_info) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to get event info 0x%X\n", event_code);
			goto next_event;
		}
		if (PAPI_add_event(papi_event_set, event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to add event 0x%X to event set\n", event_code);
			goto next_event;
		}

		/*
		 * Add the papi-event metric to the schema metric set
		 */
		rc = ldms_schema_metric_add(schema, event_name, LDMS_V_U64);
		if (rc < 0) {
			msglog(LDMSD_LERROR, "papi: failed to add event %s to metric set.\n", event_name);
			rc = ENOMEM;
			goto err;
		}

		msglog(LDMSD_LINFO, "papi: event [name: %s, code: 0x%x] has been added.\n", event_name, event_code);

next_event:
		event_name = strtok_r(NULL, ",", &status);
	}

	event_count = PAPI_num_events(papi_event_set);
	if (event_count == 0) {
		msglog(LDMSD_LERROR, "papi: no event has been added.\n");
		rc = ENOENT;
		goto err;
	}

	papi_event_val = calloc(event_count, sizeof (uint64_t));
	if (papi_event_val == NULL) {
		msglog(LDMSD_LERROR, "papi: failed to allocate papi event read buffer.\n");
		rc = ENOMEM;
		goto err;
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		msglog(LDMSD_LERROR, "papi: failed to create metric set %s.\n", instance_name);
		rc = errno;
		goto err;
	}

	/* Add component id metric 0 */
	v.v_u64 = compid;
	ldms_metric_set(set, 0, &v);

	/* papi_event_set is saved in location */
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
 * config name=spapi producer=<producer_name> instance=<instance_name> [pid=<pid>] [component_id=<compid>] [multiplex=<1|0>] [appnamefile=<appname>] events=<event1,event2,...>
 *     producer     The producer name.
 *     component_id The component id value.
 *     instance     The set name.
 *     pid	    The process to attach to.
 *     multiplex    Enable papi multiplex (set to 1)
 *     appnamefile  The application name is saved in a file ex. /tmp/appname.txt
 *     events	    The the name of the hardware counter events 
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;
	uint64_t pid = 0;
	char *pid_str;
	char *producer_name;
	char *instance_name;
	char *schema_name;
	char *events;
	char *component_id;

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "papi: missing producer\n");
		return ENOENT;
	}

	instance_name = av_value(avl, "instance");
	if (!instance_name) {
		msglog(LDMSD_LERROR, "papi: missing instance.\n");
		return ENOENT;
	}

	component_id = av_value(avl, "component_id");
	if (component_id)
		compid = (uint64_t) (atoi(component_id));
	else
		compid = 0;

	events = av_value(avl, "events");
	if (!events) {
		msglog(LDMSD_LERROR, "papi: missing events.\n");
		return ENOENT;
	}

	/*
	 * We have 2 choices:
	 * (1) Supply the daemon with a PID
	 * (2) Supply it with an application name
	 * 
	 * If the daemon use an application name then the
	 * daemon will wait while sampling for an application to run where
	 * the user supplies the daemon with the file path and name
	 * during configuration
	 * NOTE: the wait will be in the sampling not during the 
	 * configuration
	 * 
	 * If the daemon used with a PID then the daemon will run 
	 * without wait 
	 * 
	 */
	pid_str = av_value(avl, "pid");
	if (!pid_str) {
		appname_filename = av_value(avl, "appnamefile");
		if (!appname_filename) {
			msglog(LDMSD_LERROR, "papi: missing pid or application name.\n");
			return ENOENT;
		}
	} else {
		pid = strtoull(pid_str, NULL, 0);
	}

	schema_name = av_value(avl, "schema");
	if (!schema_name)
		schema_name = default_schema_name;
	if (strlen(schema_name) == 0) {
		msglog(LDMSD_LERROR, "papi: schema name invalid.\n");
		return EINVAL;
	}

	if (set) {
		msglog(LDMSD_LERROR, "papi: Set already created.\n");
		return EINVAL;
	}

	rc = create_metric_set(instance_name, schema_name, events);
	if (rc) {
		msglog(LDMSD_LERROR, "papi: failed to create a metric set.\n");
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
	int i, rc;
	int event_count;
	union ldms_value val;
	int papi_event_set;
	int status = 0; /* PAPI status */

	if (!set) {
		msglog(LDMSD_LERROR, "papi: plugin not initialized\n");
		return EINVAL;
	}

	/*
	 * Wait for an application to run, get the PID
	 * and supply it to the LDMS daemon sampler
	 */
	int apppid = 0; /* The application PID number */
	char command[250]; /* The shell command to grep the application PID */

	msglog(LDMSD_LDEBUG, "PID = %d \n", pid);

	if (pid == 0) {
		ldms_transaction_begin(set);
		appname_str = "";
		/* Read a file ex. /tmp/myapp.txt to the get the application name */
		FILE *file;
		char *line = NULL;
		size_t len = 0;

		file = fopen(appname_filename, "r");
		if ((file != NULL)) { /* APPNAME is set */
			int token_number = 1;
			/*
			 * Read three lines from the file
			 * application name
			 * jobid
			 * user name
			 */
			while (getline(&line, &len, file) != -1) {
				strtok(line, "\n");
				switch (token_number) {
					case 1:
						/* Save the application name */
						appname_str = (char *) ldms_metric_array_get(set, 1);
						ldms_metric_array_set(set, token_number, (ldms_mval_t)line, 0, strlen(line) + 1);
						msglog(LDMSD_LDEBUG, "APPNAME from file= %s \n",  ldms_metric_array_get(set, 1));
						break;
					case 2:
						/* Save the jobid */
						ldms_metric_array_set(set, token_number, (ldms_mval_t)line, 0, strlen(line) + 1);
						msglog(LDMSD_LDEBUG, "jobid = %s \n", line);
						break;
					case 3:
						/* Save the user name */
						ldms_metric_array_set(set, token_number, (ldms_mval_t)line, 0, strlen(line) + 1);
						msglog(LDMSD_LDEBUG, "user name = %s \n", line);
						break;
				}
				token_number++;
			}
			fclose(file);
		}
		if (strlen(appname_str) > 1) {
			msglog(LDMSD_LDEBUG, "pgrep %s | head | awk '{print $1;}'\n", appname_str);
			sprintf(command, "pgrep %s | head | awk '{print $1;}'",  appname_str);
			/* Get the application pid */
			FILE *fp = popen(command, "r");
			fscanf(fp, "%d", &apppid);
			pclose(fp);
			msglog(LDMSD_LDEBUG, "6 \n");
			if (apppid > 1) {
				msglog(LDMSD_LDEBUG, "Application PID = %d \n", apppid);
				pid = apppid; /* set the pid to application process id */
			} else msglog(LDMSD_LDEBUG, "Waiting for an application to start\n");
		} else msglog(LDMSD_LDEBUG, "Waiting for an application to start\n");
		ldms_transaction_end(set);
	} else { /* When PID exist */
		ldms_transaction_begin(set);
		msglog(LDMSD_LDEBUG, "attach = %d \n", attach);
		if (attach == 0) { /* check if the attach happened before, no need to attach */
			msglog(LDMSD_LDEBUG, "PAPI attach to process %" PRIu64 "\n", pid);
			papi_event_set = ldms_metric_user_data_get(set, 0);
			rc = PAPI_attach(papi_event_set, pid);
			if (rc != PAPI_OK) {
				msglog(LDMSD_LERROR, "papi: failed to attach to process pid= %d rc= %d.\n", pid, rc);
			}

			rc = PAPI_start(papi_event_set);
			if (rc != PAPI_OK) {
				msglog(LDMSD_LERROR, "papi: failed to start papi event set rc= %d\n", rc);
			}
			attach = 1;
			ldms_transaction_end(set);
		} else {
			/* PAPI attached to a process start sampling
			 *	      msglog(LDMSD_LDEBUG, "Start sampling \n");
			 * Read user data from the first metric
			 */
			papi_event_set = ldms_metric_user_data_get(set, 0);
			event_count = PAPI_num_events(papi_event_set);

			/*
			 * Start sampling
			 */
			ldms_transaction_begin(set);

			if (PAPI_read(papi_event_set, papi_event_val) != PAPI_OK) {
				msglog(LDMSD_LERROR, "papi: failed to read event set %d\n", papi_event_set);
			}

			/* Check if the PAPI is attached and the the application is a life 
			 * by searching for the process number if exist
			 * if note exist then De-attach PAPI
			 */
			sprintf(command, "ps -f --no-headers %" PRIu64 "| head | awk '{print $2;}'", pid);
			/* Get the application pid */
			FILE *fp = popen(command, "r");

			fscanf(fp, "%d", &apppid);
			pclose(fp);

			if (apppid > 1) {
				msglog(LDMSD_LDEBUG, "The application is running \n");
				msglog(LDMSD_LDEBUG, "event count = %d \n", event_count);
				for (i = 0; i < event_count; ++i) {
					msglog(LDMSD_LDEBUG, "event count = %d \n", event_count);
					/* i + 4 because component_id, appname, jobid, username are the first metrics */
					val.v_u64 = papi_event_val[i];
					ldms_metric_set(set, i + 4, &val);
				}
			} else {
				/* Stop PAPI when the application is finished or killed */
				long_long values[event_count];
				rc = PAPI_stop(papi_event_set, values);
				if (rc != PAPI_OK) {
					msglog(LDMSD_LERROR, "papi: failed to stop process pid= %d. rc= %d\n", pid, rc);
				}
				/* Detach when the application is finished or killed */
				rc = PAPI_detach(papi_event_set);
				if (rc != PAPI_OK) {
					msglog(LDMSD_LERROR, "papi: failed to de-attach to process pid= %d. rc= %d\n", pid, rc);
				}
				pid = 0;
				attach = 0;
				for (i = 0; i < event_count; ++i) {
					val.v_u64 = 0;
					/* i + 4 because component_id, appname, jobid, username are the first metrics */
					ldms_metric_set(set, i + 4, &val);
				}
				msglog(LDMSD_LDEBUG, "The application is dead Detach\n");
			}

			ldms_transaction_end(set);

		}
	}

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
	return "config name=spapi producer=<producer_name> instance=<instance_name> [pid=<pid>] [component_id=<compid>] [multiplex=<1|0>] [appnamefile=<appnamefile>] "
	"events=<event1,event2,...>\n"
	"    producer	  The producer name.\n"
	"    component_id The component id value.\n"
	"    instance	  The set instance name.\n"
	"    pid	  The process to attach to.\n"
	"    multiplex	  Enable papi multiplex (set to 1)\n"
	"    appnamefile  The application name is saved in a file ex. /tmp/appname.txt \n"
	"    events	  The name of papi events.\n";
}

static struct ldmsd_sampler papi_plugin = {
	.base =
	{
		.name = "spapi",
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
	return &papi_plugin.base;
}
