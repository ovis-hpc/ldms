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
#include <linux/inotify.h>
#include <sys/stat.h>

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static ldms_schema_t schema;
static char* default_schema_name = "spapi";
static long_long* papi_event_val;
static char *appname_str = "";
static char *appname_filename;
static int pids_count = 0;
/* 0-Sampler not attached yet 1-Sampler is attached to a process */
static int attach = 0;
static int multiplex = 0;
static uint64_t compid;
static int* papi_event_sets;
static int event_codes[500];
static int* apppid; /* Application PIDS array */
static uint64_t max_pids;
static int inot_length;;
static int inot_fd;
static int inot_wd;
static char inot_buffer[16];
static int exist_before = 0;
static int inotif;
static char *oldline = "";

static int create_metric_set(const char* instance_name, const char* schema_name,
	char* events, char* filename)
{
	int rc, i, event_count, j;
	int event_code = PAPI_NULL;
	int papi_event_set = PAPI_NULL;
	char *event_name;
	char* status;
	PAPI_event_info_t event_info;
	union ldms_value v;
	char* events_names[500];
	char buf[255];

	appname_filename = strdup(filename);
	/* Check to see if we were successful */
	if (appname_filename == NULL) {
		/* We were not so display a message */
		msglog(LDMSD_LERROR, "papi: Could not allocate required "
			"memory for the string file name\n");
		rc = ENOENT;
		goto err;
	}

	rc = PAPI_library_init(PAPI_VER_CURRENT);
	if (rc != PAPI_VER_CURRENT) {
		msglog(LDMSD_LERROR, "papi: library init error! %d: %s\n", rc,
			PAPI_strerror(rc));
		rc = ENOENT;
		goto err;
	}

	/* 
	 * Intialize the Inotify instance to check if the appnamefile was written
	 */

	if (inotif > 0 ) { 
		
		inot_fd = inotify_init1(IN_NONBLOCK);

		/* checking for error */
		if ( inot_fd < 0 ) {
			msglog(LDMSD_LERROR, "papi: inotify intialization" 
				"error = %d \n", inot_fd);
			rc = ENOENT;
			goto err;
			
		}
	}
	/* 
	 * Added support to multiplex event set 
	 * Enable and initialize multiplex support 
	 * check if the configuration have multiplex option enabled 
	 */
	if (multiplex) {
		rc = PAPI_multiplex_init();
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to initialize "
				"multiplexing!\n");
			rc = ENOENT;
			goto err;
		}
	}

	rc = PAPI_create_eventset(&papi_event_set);
	if (rc != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to create empty event "
			"set!\n");
		rc = ENOENT;
		goto err;
	}
	if (multiplex) {
		/* Explicitly bind event set to cpu component.	
		 * PAPI documentation states that this must be done after 
		 * PAPI_create_eventset, but before calling PAPI_set_multiplex.
		 * The argument 0 binds to cpu component.
		 */
		rc = PAPI_assign_eventset_component(papi_event_set, 0);
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to bind papi to cpu"
				" component!\n");
			rc = ENOENT;
			goto err;
		}

		/* Convert papi_event_set to a multiplexed event set */
		rc = PAPI_set_multiplex(papi_event_set);
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to convert event set"
				" to multiplexed!\n");
			rc = ENOENT;
			goto err;
		}
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
	 * Create two metrics: Application name, Jobid, Username, and Pid
	 */
	rc = ldms_schema_metric_array_add(schema, "Appname", LDMS_V_CHAR_ARRAY,
		256);
	if (rc < 0) {
		msglog(LDMSD_LERROR, "papi: failed to add application name to"
			" metric set.\n");
		rc = ENOMEM;
		goto err;
	}
	rc = ldms_schema_metric_array_add(schema, "Jobid", LDMS_V_CHAR_ARRAY,
		256);
	if (rc < 0) {
		msglog(LDMSD_LERROR, "papi: failed to add jobid to metric "
			"set.\n");
		rc = ENOMEM;
		goto err;
	}
	rc = ldms_schema_metric_array_add(schema, "Username", LDMS_V_CHAR_ARRAY,
		256);
	if (rc < 0) {
		msglog(LDMSD_LERROR, "papi: failed to add username to metric "
			"set.\n");
		rc = ENOMEM;
		goto err;
	}
	rc = ldms_schema_metric_add(schema, "Pid", LDMS_V_U64);
	if (rc < 0) {
		msglog(LDMSD_LERROR, "papi: failed to add PID to metric "
			"set.\n");
		rc = ENOMEM;
		goto err;
	}

	int c = 0;
	event_name = strtok_r(events, ",", &status);
	while (event_name) {

		if (PAPI_event_name_to_code(event_name, &event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to get event code of"
				" %s\n", event_name);
			goto next_event;
		}
		if (PAPI_query_event(event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to query event"
				" 0x%X\n", event_code);
			goto next_event;
		}
		if (PAPI_get_event_info(event_code, &event_info) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to get event info"
				" 0x%X\n", event_code);
			goto next_event;
		}
		if (PAPI_add_event(papi_event_set, event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to add event 0x%X"
				" to event set\n", event_code);
			goto next_event;
		}

		events_names[c] = event_name;
		event_codes[c] = event_code;
		c++;
		/*
		 * Add the papi-event metric to the schema metric set
		 */
		rc = ldms_schema_metric_add(schema, event_name, LDMS_V_U64);
		if (rc < 0) {
			msglog(LDMSD_LERROR, "papi: failed to add event %s to"
				" metric set.\n", event_name);
			rc = ENOMEM;
			goto err;
		}

		msglog(LDMSD_LINFO, "papi: event [name: %s, code: 0x%x] has"
			" been added.\n", event_name, event_code);

next_event:
		event_name = strtok_r(NULL, ",", &status);
	}

	event_count = PAPI_num_events(papi_event_set);
	if (event_count == 0) {
		msglog(LDMSD_LERROR, "papi: no event has been added.\n");
		rc = ENOENT;
		goto err;
	}

	/* Save data for max_pids, we have one set created earlier */
	for (i = 1; i < max_pids; i++) {
		sprintf(buf, "Pid_%d", i);
		rc = ldms_schema_metric_add(schema, buf, LDMS_V_U64);
		if (rc < 0) {
			msglog(LDMSD_LERROR, "papi: failed to add PID to metric"
				" set.\n");
			rc = ENOMEM;
			goto err;
		}
		for (j = 0; j < event_count; j++) {
			/*
			 * Add the papi-event metric to the schema metric set
			 */
			sprintf(buf, "%s_%d", events_names[j], i);
			rc = ldms_schema_metric_add(schema, buf, LDMS_V_U64);
			if (rc < 0) {
				msglog(LDMSD_LERROR, "papi: failed to add event"
					" %s to metric set.\n", event_name);
				rc = ENOMEM;
				goto err;
			}
		}
	}

	papi_event_val = calloc(event_count, sizeof (uint64_t));
	if (papi_event_val == NULL) {
		msglog(LDMSD_LERROR, "papi: failed to allocate papi event read"
			" buffer.\n");
		rc = ENOMEM;
		goto err;
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		msglog(LDMSD_LERROR, "papi: failed to create metric set %s.\n",
			instance_name);
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
 * config name=spapi producer=<producer_name> instance=<instance_name> [pid=<pid>] [max_pids=<number>] [component_id=<compid>] [multiplex=<1|0>] [appnamefile=<appname>] events=<event1,event2,...>
 *     producer     The producer name.
 *     component_id The component id value.
 *     instance     The set name.
 *     pid	    The process to attach to.
 *     max_pids     The maximum pids to collect
 *     multiplex    Enable papi multiplex (set to 1)
 *     appnamefile  The application name is saved in a file ex. /tmp/appname.txt
 *     events	    The the name of the hardware counter events 
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl,
	struct attr_value_list * avl)
{
	int rc;
	uint64_t pid = 0;
	char *pid_str;
	char *producer_name;
	char *instance_name;
	char *schema_name;
	char *events;
	char *filename;
	char *component_id;
	char *maxpids;
	char *multiplx;
	char *val;

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

	maxpids = av_value(avl, "max_pids");
	if (maxpids)
		max_pids = (uint64_t) (atoi(maxpids));
	else
		max_pids = 1;

	/* Set if inotify is uesd or not 1 yes, 0 no  */
	val = av_value(avl, "inotify");
        if (val)
                inotif = (uint64_t) (atoi(val));
        else
                inotif = 0;

	multiplx = av_value(avl, "multiplex");
	if (multiplx)
		multiplex = (uint64_t) (atoi(multiplx));
	else
		multiplex = 0;

	msglog(LDMSD_LDEBUG, "papi: Maximum PIDs are %d.\n", max_pids);

	events = av_value(avl, "events");
	if (!events) {
		msglog(LDMSD_LERROR, "papi: missing events.\n");
		return ENOENT;
	}

	/*
	 * Supply it with an application name
	 * 
	 * If the daemon use an application name then the
	 * daemon will wait while sampling for an application to run where
	 * the user supplies the daemon with the file path and name
	 * during configuration
	 * NOTE: the wait will be in the sampling not during the 
	 * configuration
	 * 
	 */

	filename = av_value(avl, "appnamefile");
	if (!filename) {
		msglog(LDMSD_LERROR, "papi: missing pid or application name\n");
		return ENOENT;
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

	rc = create_metric_set(instance_name, schema_name, events, filename);
	if (rc) {
		msglog(LDMSD_LERROR, "papi: failed to create a metric set.\n");
		return rc;
	}

	ldms_set_producer_name_set(set, producer_name);
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler * self)
{
	return set;
}

/*
 * Read the support application file function 
 * Read three lines from the file
 *	application name,jobid, user name
 */
void read_sup_file()
{

	/* Read a file ex. /tmp/myapp.txt to the get the application name */
	FILE *file;
	char *line = NULL;
	size_t len = 0;
	char *record;
	char* status;

	msglog(LDMSD_LDEBUG, "papi: Start Reading the File\n");

	msglog(LDMSD_LDEBUG, "papi: Open the "
		"File: %s\n", appname_filename);
	file = fopen(appname_filename, "r");
	if ((file != NULL)) { /* APPNAME is set */
		int token_number = 1;
		if (getline(&line, &len, file) == -1) goto nextread;
		
		msglog(LDMSD_LDEBUG, "papi: line = %s\n", line);
		msglog(LDMSD_LDEBUG, "papi: oldline = %s\n", oldline);
		/* Check if the same job line read before */
		if (strcmp(line, oldline) == 0) goto nextread;
		
		oldline = strdup(line);
		
		ldms_transaction_begin(set);
		record = strtok_r(line, ",", &status);
		while (record) {
			strtok(record, "\n");
			switch (token_number) {
				case 1:
					/* Save the application name */
					ldms_metric_array_set(set, token_number,
						(ldms_mval_t) record, 0,
						strlen(record) + 1);
					appname_str = (char *)
						ldms_metric_array_get(set, 1);
					msglog(LDMSD_LDEBUG, "papi: "
						"APPNAME from file= %s \n",
						ldms_metric_array_get(set, 1));
					break;
				case 2:
					/* Save the jobid */
					ldms_metric_array_set(set, token_number,
						(ldms_mval_t) record, 0,
						strlen(record) + 1);
					msglog(LDMSD_LDEBUG, "papi: "
						"jobid = %s \n",
						record);
					break;
				case 3:
					/* Save the user name */
					ldms_metric_array_set(set, token_number,
						(ldms_mval_t) record, 0,
						strlen(record) + 1);
					msglog(LDMSD_LDEBUG, "papi: "
						"user name = %s \n"
						, record);
					break;
			}
			record = strtok_r(NULL, ",", &status);
			token_number++;
		}

		ldms_transaction_end(set);
nextread:
		msglog(LDMSD_LDEBUG, "papi: End reading the File\n");
		fclose(file);
	} else {
		msglog(LDMSD_LDEBUG, "papi: Error in open the "
			"appname file: %d \n", errno);
	}
}

/*
 * This function called by the create_event_sets to create and add papi events
 * Input: event set  
 */
static int papi_events(int c)
{
	int event_count;
	int rc, num;
	/* Create an event set for each pid */
	papi_event_sets[c] = PAPI_NULL;
	msglog(LDMSD_LDEBUG, "papi: Application PID[%d] = %d\n", c,
		apppid[c]);
	rc = PAPI_create_eventset(&papi_event_sets[c]);
	if (rc != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to create empty "
			"event set number %d error %d!\n", c, rc);
		return -1;
	}
	
	if (multiplex) {
		/* Explicitly bind event set to cpu component.	
		 * PAPI documentation states that this must be done after 
		 * PAPI_create_eventset, but before calling PAPI_set_multiplex.
		 * The argument 0 binds to cpu component.
		 */
		rc = PAPI_assign_eventset_component(papi_event_sets[c], 0);
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to bind papi to cpu"
				" component!\n");
			rc = ENOENT;
			return -1;
		}

		/* Convert papi_event_set to a multiplexed event set */
		rc = PAPI_set_multiplex(papi_event_sets[c]);
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to convert event set"
				" to multiplexed!\n");
			rc = ENOENT;
			return -1;
		}
	}

	event_count = PAPI_num_events(ldms_metric_user_data_get(set, 0));

	rc = PAPI_add_events(papi_event_sets[c],
			event_codes, event_count);
	if (rc != PAPI_OK) {
		msglog(LDMSD_LERROR, "papi: failed to add "
				"event to event set error %d\n",
				rc);
		return -1;
	}
	return 0;
}

/*
 * Create event sets for each PID to collect information
 */
static int create_event_sets()
{
	int c;
	char command[250]; /* The shell command to grep the application PID */
	/*
	 * Save the number of pids again because sometimes
	 * the number in the first read is incorrect
	 */
	msglog(LDMSD_LDEBUG, "papi: pgrep %s | wc -l\n", appname_str);
	sprintf(command, "pgrep %s | wc -l", appname_str);
	FILE *pipe_fp1;
	if ((pipe_fp1 = popen(command, "r")) == NULL) {
		msglog(LDMSD_LERROR, "papi: pipe - pid counts "
			"failed pgrep %s | wc -l\n", appname_str);
		pids_count = 0;
	} else {
		/* Get the application PID counts */
		fscanf(pipe_fp1, "%d", &pids_count);

		pclose(pipe_fp1);

		apppid = (int*) calloc(pids_count, sizeof (int));

		papi_event_sets = (int*) calloc(pids_count*20,
			sizeof (int));

		sprintf(command, "pgrep %s", appname_str);

		c = 0;
		/* Get the application pid */
		if ((pipe_fp1 = popen(command, "r")) == NULL) {
			msglog(LDMSD_LERROR, "papi: pipe - pid counts"
				" failed pgrep %s\n", appname_str);
			pids_count = 0;
		} else {
			while (fscanf(pipe_fp1, "%d", &apppid[c]) != -1
				&& c < pids_count) {
				if (papi_events(c) < 0) {
                                    pclose(pipe_fp1);
                                    return -1;
                                }
				c++;
			}
			pclose(pipe_fp1);
		}
	}
	return 0;
}

static int save_events_data()
{
	int c, i, j, event_count;
	union ldms_value val;
	/* PAPI attached to a process start sampling
	 * Read user data from the first metric
	 */
	c = 0;
	i = 4;
	/* Attach to all PIDs */
	event_count = PAPI_num_events(ldms_metric_user_data_get(set, 0));
	while (c < pids_count && c < max_pids) {

		/*
		 * Start sampling
		 */
		if (PAPI_read(papi_event_sets[c],
			papi_event_val) != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to read event "
				"set %d\n", papi_event_sets[c]);
			return -1;
		}

		/*
		 * *************************************
		 * I stopped here, I need to find away 
		 * to save the PIDs result into multiple 
		 * events :(
		 * 
		 * 
		 * Propebly I need to use the PUSH to 
		 * push each metric values a side
		 * 
		 * I need to find how I can push 
		 * multiple metric values in the same 
		 * sample cycle
		 * 
		 * The solution?? After the loop I 
		 * should push the values to the store
		 */

		/* 
		 * Save the PID number in the pid metric
		 */
		val.v_u64 = apppid[c];
		ldms_metric_set(set, i, &val);
		for (j = 0; j < event_count; j++) {
			/* 
			 * j + i + 1 because 
			 * component_id, appname, jobid, username, and pid are 
			 * the first metrics 
			 */
			val.v_u64 = papi_event_val[j];
			ldms_metric_set(set, j + i + 1,
				&val);
		}
		i += event_count + 1;
		c++;
	}
	return 0;
}

int deatach_pids()
{
	int event_count, j, rc, c;
	union ldms_value val;
	/* 
	 * Stop PAPI when the application is finished
	 * or killed 
	 */
	event_count = PAPI_num_events(ldms_metric_user_data_get(set, 0));
	long_long values[event_count];
	for (c = 0; c < pids_count; c++) {
		rc = PAPI_stop(papi_event_sets[c], values);
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to stop process"
				" pid= %d. rc= %d\n", apppid[0], rc);
		}
		/* 
		 * Detach when the application is finished or killed 
		 */
		rc = PAPI_detach(papi_event_sets[c]);
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, "papi: failed to de-attach to "
				"process pid= %d. rc= %d\n", apppid[0], rc);
		}
		if (rc != PAPI_ENOEVST && apppid[0] != 0) {
			PAPI_cleanup_eventset(papi_event_sets[c]);
			PAPI_destroy_eventset(&papi_event_sets[c]);
		} else {
			msglog(LDMSD_LDEBUG, "papi: Event set does not "
				"exist\n");
		}
	}
	free(apppid);
	free(papi_event_sets);
	attach = 0;
	pids_count = 0;
	appname_str = "";

	/* Clear PID metric */
	/* Save the PID number in the pid metric */
	for (j = 0; j < (event_count + 1) * max_pids; j++) {
		/* 
		 * j + 4 because component_id, appname, jobid, username are 
		 * the first metrics 
		 */
		val.v_u64 = 0;
		ldms_metric_set(set, j + 4, &val);
	}
	msglog(LDMSD_LDEBUG, "The application is dead Detach\n");

	return 0;
}

static int sample(struct ldmsd_sampler * self)
{
	int rc, c;
	int pids_count_left;
	int err;
	struct stat file_stat;

	if (!set) {
		msglog(LDMSD_LERROR, "papi: plugin not initialized\n");
		return EINVAL;
	}

	/*
	 * Wait for an application to run, get the PID
	 * and supply it to the LDMS daemon sampler
	 */

	char command[250]; /* The shell command to grep the application PID */

	msglog(LDMSD_LDEBUG, "PID counts = %d \n", pids_count);

	if (pids_count == 0) {

		if (inotif > 0 ) {
			/* Check if the file exist */
			err = stat("/tmp/appname", &file_stat);
			msglog(LDMSD_LDEBUG, "file exist? %d\n", err);
			if (err != 0) {
				/* if not exist do nothing */
				exist_before = 0;
				return 0;
			} else {
				/* 
				 * If the file exist, set the inotify once
				 * Add the watch for close with write only
				 * exist_before used to add the watch one time only
				 */
				if ( exist_before == 0) {
					inot_wd = inotify_add_watch(inot_fd, 
						appname_filename, IN_CLOSE_WRITE);
					/* Read the support file */
					read_sup_file();
					exist_before = 1;
				} else {
					inot_length = read( inot_fd, inot_buffer, 16 );
					if (inot_length > 0) {
						/* File changed */
						/* Read the support file */
						read_sup_file();
					} 
				}
			}
		} else {
			/* Read the support file */
			read_sup_file();
		}
		
		if (strlen(appname_str) > 1) {
			msglog(LDMSD_LDEBUG, "pgrep %s | wc -l\n", appname_str);
			sprintf(command, "pgrep %s | wc -l", appname_str);
			FILE *fp = popen(command, "r");
			/* Get the application PID counts */
			fscanf(fp, "%d", &pids_count);
			msglog(LDMSD_LDEBUG, "Pids count = %d\n", pids_count);
			pclose(fp);

			if (pids_count >= 1) {
				if (create_event_sets() < 0) {
					goto err1;
				}
			} else msglog(LDMSD_LDEBUG, "Waiting for application to"
				" start\n");
		} else msglog(LDMSD_LDEBUG, "Waiting for the appname file to be"
			" created or changed\n");
	} else { /* When PID exist */
		/* check if the attach happened before, no need to attach */
		if (attach == 0) {

			c = 0;
			/* Attach to all PIDs */
			while (c < pids_count) {
				rc = PAPI_attach(papi_event_sets[c], apppid[c]);
				if (rc != PAPI_OK) {
					msglog(LDMSD_LERROR, "papi: failed to"
						" attach to process pid = %d"
						" rc= %d.\n", apppid[c], rc);
					goto err1;
				}
				rc = PAPI_start(papi_event_sets[c]);
				if (rc != PAPI_OK) {
					msglog(LDMSD_LERROR, "papi: failed to"
						" start papi event set "
						"rc= %d\n", rc);
					goto err1;
				}
				attach = 1;
				c++;
			}
		} else {

			ldms_transaction_begin(set);

			/* Check if the PAPI is attached and the the application
			 *  is a life by searching for the process number 
			 * if note exist then De-attach PAPI
			 */
			pids_count_left = 0;
			sprintf(command, "pgrep %s | wc -l", appname_str);
			FILE *fp = popen(command, "r");
			/* Get the application PID counts */
			fscanf(fp, "%d", &pids_count_left);
			pclose(fp);

			if (pids_count_left >= 1) {
				if (save_events_data() < 0) {
					ldms_transaction_end(set);
					goto err1;
				}
			} else {
				deatach_pids();
			}
			ldms_transaction_end(set);
		}
	}

	return 0;

err1:
	/*
	 * Where error occurs a restart to the collection process
	 * will be applied and try to attach again
	 */
	deatach_pids();
	return 0;
}

static void term(struct ldmsd_plugin * self)
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

static const char *usage(struct ldmsd_plugin * self)
{
	return "config name=spapi producer=<producer_name> instance=<instance_name> [pid=<pid>] [max_pids=<number>] [component_id=<compid>] [multiplex=<1|0>] [appnamefile=<appnamefile>] "
	"events=<event1,event2,...>\n"
	"    producer	  The producer name.\n"
	"    component_id The component id value.\n"
	"    instance	  The set instance name.\n"
	"    pid	  The process to attach to.\n"
	"    max_pids	  The maximum pids to collect, default = 1.\n"
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

struct ldmsd_plugin * get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &papi_plugin.base;
}
