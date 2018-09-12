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
 * No bugs reported!
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
#include <sys/inotify.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static ldms_schema_t schema;
static char* default_schema_name = "hweventpapi";
#define SAMP "hweventpapi"
static long_long* papi_event_val;
static char *appname_str = "";
static char *jobid = "";
static char *username = "";
static int pids_count = 0;
/* 0-Sampler not attached yet 1-Sampler is attached to a process */
static int attach = 0;
static int multiplex = 0;
//static uint64_t compid;
char* compid;
static int* papi_event_sets;
static int* events_codes;
static int* apppid; /* Application PIDS array */
static uint64_t max_pids;
static uint8_t num_nodes;
static uint8_t ppn;
static uint8_t num_threads;
static int metric_offset = 0;
static int exist_before = 0;
int papi_event_set = PAPI_NULL;
static int inotif;
static char *oldline = "";
static int inot_length;
static int inot_fd;
static int inot_wd;
static char inot_buffer[16];
static char *metadata_filename;
char *producer_name;
static base_data_t base;
FILE *file = NULL;

struct attr_value_list *av_list_local;
struct attr_value_list *kw_list_local;

pthread_t meta_thread;

struct sampler_meta {
	char* conf_line;
	long interval;
};

static int create_metric_set(const char* instance_name, const char* schema_name,
	char* events)
{
	int rc, i, event_count, j;
	int metric_id, pec = 0;
	int event_code = PAPI_NULL;
	char* event_name;
	char* status;
	PAPI_event_info_t event_info;
	union ldms_value v;

	rc = PAPI_library_init(PAPI_VER_CURRENT);
	if (rc != PAPI_VER_CURRENT) {
		msglog(LDMSD_LERROR, SAMP ": library init error! %d: %s\n", rc,
			PAPI_strerror(rc));
		rc = ENOENT;
		goto err;
	}

	/*
	 * Added support to multiplex event set
	 * Enable and initialize multiplex support
	 * check if the configuration have multiplex option enabled
	 */
	if (multiplex) {
		msglog(LDMSD_LDEBUG, SAMP ": multiplex is %d\n", multiplex);
		rc = PAPI_multiplex_init();
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, SAMP ": failed to initialize "
				"multiplexing!\n");
			rc = ENOENT;
			goto err;
		}
	}

	rc = PAPI_create_eventset(&papi_event_set);
	if (rc != PAPI_OK) {
		msglog(LDMSD_LERROR, SAMP ": failed to create empty event "
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
			msglog(LDMSD_LERROR, SAMP ": failed to bind papi to cpu"
				" component!\n");
			rc = ENOENT;
			goto err;
		}

		/* Convert papi_event_set to a multiplexed event set */
		rc = PAPI_set_multiplex(papi_event_set);
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, SAMP ": failed to convert event "
				"set to multiplexed!\n");
			rc = ENOENT;
			goto err;
		}
	}

	schema = base_schema_new(base);
	if (!schema) {
		msglog(LDMSD_LERROR,
			"%s: schema '%s' could not be created, errno=%d.\n",
			__FILE__, base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/*
	 * Create six metrics: Application name, Jobid, Username, Pid,
	 * number of nodes, number of processes per node, and number of threads
	 */
	rc = ldms_schema_metric_array_add(schema, "Appname", LDMS_V_CHAR_ARRAY,
		256);
	if (rc < 0) {
		msglog(LDMSD_LERROR, SAMP ": failed to add application name to"
			" metric set.\n");
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_metric_add(schema, "Jobid", LDMS_V_U64);
	if (rc < 0) {
		msglog(LDMSD_LERROR, SAMP ": failed to add jobid to metric "
			"set.\n");
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_metric_array_add(schema, "Username", LDMS_V_CHAR_ARRAY,
		256);
	if (rc < 0) {
		msglog(LDMSD_LERROR, SAMP ": failed to add username to metric "
			"set.\n");
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_metric_add(schema, "NumNodes", LDMS_V_U8);
	if (rc < 0) {
		msglog(LDMSD_LERROR, SAMP ": failed to add NumNodes to"
			" metric set.\n");
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_metric_add(schema, "PPN", LDMS_V_U8);
	if (rc < 0) {
		msglog(LDMSD_LERROR, SAMP ": failed to add PPn to metric "
			"set.\n");
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_metric_add(schema, "NumThreads", LDMS_V_U8);
	if (rc < 0) {
		msglog(LDMSD_LERROR, SAMP ": failed to add NumThreads to metric"
			" set.\n");
		rc = ENOMEM;
		goto err;
	}

	/* Location of first metric from proc/meminfo file */
	metric_offset = ldms_schema_metric_count_get(schema);

	ldms_schema_metric_array_add(schema, "Pid", LDMS_V_U64_ARRAY, max_pids);
	if (rc < 0) {
		msglog(LDMSD_LERROR, SAMP ": failed to add PID to metric "
			"set.\n");
		rc = ENOMEM;
		goto err;
	}

	/* calculate papi events count (pec) from user input */
	char* events_tmp;
	events_tmp = events;
	for (pec = 0; events_tmp[pec]; events_tmp[pec] == ',' ? pec++ :
		*events_tmp++);

	msglog(LDMSD_LDEBUG, SAMP ": user input papi event counts are %d and "
		"events length are %d\n", pec, strlen(events));

	/* Allocate the memory space from papi events names and codes */
	events_codes = (int*) calloc(pec + 1, sizeof (int));

	int c = 0;
	event_name = strtok_r(events, ",", &status);
	while (event_name) {

		if (PAPI_event_name_to_code(event_name, &event_code) !=
			PAPI_OK) {
			msglog(LDMSD_LERROR, SAMP ": failed to get event code "
				"of %s\n", event_name);
			goto next_event;
		}
		if (PAPI_query_event(event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, SAMP ": failed to query event"
				" 0x%X\n", event_code);
			goto next_event;
		}
		if (PAPI_get_event_info(event_code, &event_info) != PAPI_OK) {
			msglog(LDMSD_LERROR, SAMP ": failed to get event info"
				" 0x%X\n", event_code);
			goto next_event;
		}
		if (PAPI_add_event(papi_event_set, event_code) != PAPI_OK) {
			msglog(LDMSD_LERROR, SAMP ": failed to add event 0x%X"
				" to event set\n", event_code);
			goto next_event;
		}

		events_codes[c] = event_code;
		c++;
		/*
		 * Add papi-event array metric to the schema metric set
		 */
		ldms_schema_metric_array_add(schema, event_name,
			LDMS_V_U64_ARRAY, max_pids);
		if (rc < 0) {
			msglog(LDMSD_LERROR, SAMP ": failed to add event %s to"
				" metric set.\n", event_name);
			rc = ENOMEM;
			goto err;
		}

		msglog(LDMSD_LINFO, SAMP ": event [name: %s, code: 0x%x] "
			"has been added.\n", event_name, event_code);

next_event:
		event_name = strtok_r(NULL, ",", &status);
	}

	event_count = PAPI_num_events(papi_event_set);
	if (event_count == 0) {
		msglog(LDMSD_LERROR, SAMP ": no event has been added.\n");
		rc = ENOENT;
		goto err;
	}

	papi_event_val = calloc(event_count, sizeof (uint64_t));
	if (papi_event_val == NULL) {
		msglog(LDMSD_LERROR, SAMP ": failed to allocate papi event read"
			" buffer.\n");
		rc = ENOMEM;
		goto err;
	}

	set = base_set_new(base);
	if (!set) {
		msglog(LDMSD_LERROR, SAMP ": failed to create metric set %s.\n",
			instance_name);
		rc = errno;
		goto err;
	}

	/*
	 * This number was created based on the number of the
	 * default metrics created by base_set_new()
	 * TODO: need to find this number dynamically
	 */
	metric_id = 3;

	/* papi_event_set is saved in location */
	ldms_metric_user_data_set(set, 0, papi_event_set);

	/* set the application name, jobid, and username from configuration*/
	ldms_metric_array_set(set, metric_id++, (ldms_mval_t) appname_str, 0,
		strlen(appname_str) + 1);
	v.v_u64 = (uint64_t) atoi(jobid);
	ldms_metric_set(set, metric_id++, &v);
	ldms_metric_array_set(set, metric_id++, (ldms_mval_t) username, 0,
		strlen(username) + 1);
	v.v_u8 = num_nodes;
	ldms_metric_set(set, metric_id++, &v);
	v.v_u8 = ppn;
	ldms_metric_set(set, metric_id++, &v);
	v.v_u8 = num_threads;
	ldms_metric_set(set, metric_id++, &v);

	return 0;

err:
	msglog(LDMSD_LDEBUG, SAMP ": error in create metric set function\n");

	if (base)
		base_del(base);
	base = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
	return rc;
}

static struct sampler_meta * read_sup_file()
{
	/* Read a file Ex. /tmp/sampler.txt to the get the sampler info */
	FILE *file;
	char *line = NULL;
	size_t len = 0;
	char *record;
	char* status;
	struct sampler_meta * meta;

	meta = calloc(1, sizeof *meta + 1024);
	if (!meta)
		return NULL;

	msglog(LDMSD_LDEBUG, SAMP ": start reading the file\n");

	msglog(LDMSD_LDEBUG, SAMP ": open the file: %s\n", metadata_filename);
	file = fopen(metadata_filename, "r");
	fseek(file, 0, SEEK_SET);

	if ((file != NULL)) { /* APPNAME is set */
		int token_number = 1;
		if (getline(&line, &len, file) == -1) {
			msglog(LDMSD_LDEBUG, SAMP ": no new data written into"
				" the file\n");
			goto nextread;
		}

		msglog(LDMSD_LDEBUG, SAMP ": existing line = %s\n", line);
		msglog(LDMSD_LDEBUG, SAMP ": oldline = %s\n", oldline);
		/* Check if the same job line read before */
		if (strcmp(line, oldline) == 0) {
			msglog(LDMSD_LDEBUG, SAMP ": same data exist\n");
			goto nextread;
		}

		oldline = strdup(line);

		record = strtok_r(line, ";", &status);
		while (record) {
			strtok(record, "\n");
			switch (token_number) {
				case 1:
					meta->conf_line = strdup(record);
					msglog(LDMSD_LDEBUG, SAMP ": sampler "
						"conf line = %s\n",
						meta->conf_line);
					break;
				case 2:
					if ((meta->interval = atol(record))
						== 0) {
						goto nextread;
					}
					msglog(LDMSD_LDEBUG, SAMP ": sampler "
						"record = %s, interval = %d\n",
						record, meta->interval);
					break;
			}
			record = strtok_r(NULL, ";", &status);
			token_number++;
		}

		msglog(LDMSD_LDEBUG, SAMP ": end reading new data exist\n");
		return meta;
nextread:
		msglog(LDMSD_LDEBUG, SAMP ": end reading no new data\n");
		free(meta);
		return NULL;
	} else {
		msglog(LDMSD_LERROR, SAMP ": error opening the "
			"file: %d \n", errno);
		free(meta);
		return NULL;
	}
}

static int string2attr_list_local(char *str, struct attr_value_list **__av_list,
	struct attr_value_list **__kw_list)
{
	char *cmd_s;
	struct attr_value_list *av_list;
	struct attr_value_list *kw_list;
	int tokens, rc;

	/*
	 * Count the number of spaces. That's the maximum number of
	 * tokens that could be present.
	 */
	for (tokens = 0, cmd_s = str; cmd_s[0] != '\0';) {
		tokens++;
		/* find whitespace */
		while (cmd_s[0] != '\0' && !isspace(cmd_s[0]))
			cmd_s++;
		/* Now skip whitespace to next token */
		while (cmd_s[0] != '\0' && isspace(cmd_s[0]))
			cmd_s++;
	}

	/* add the producer the compid to the tokens */
	char *new_str;
	char s1[] = " producer=";
	char s2[] = " component_id=";

	if ((new_str = malloc(strlen(str) + strlen(s1) + strlen(producer_name)
		+ strlen(s2) + strlen(compid) + 1)) != NULL) {
		/* ensures the memory is an empty string */
		new_str[0] = '\0';
		strcat(new_str, str);
		strcat(new_str, s1);
		strcat(new_str, producer_name);
		strcat(new_str, s2);
		strcat(new_str, compid);
	} else {
		msglog(LDMSD_LERROR, SAMP ": malloc failed!\n");
		goto err;
	}
	tokens += 2;

	msglog(LDMSD_LDEBUG, SAMP ": the configuration string to be tokenized "
		"is %s -- number of tokens are %d\n", new_str, tokens);

	rc = ENOMEM;
	av_list = av_new(tokens);
	kw_list = av_new(tokens);
	if (!av_list || !kw_list)
		goto err;

	rc = tokenize(new_str, kw_list, av_list);
	if (rc)
		goto err;
	*__av_list = av_list;
	*__kw_list = kw_list;
	return 0;
err:
	if (av_list)
		av_free(av_list);
	if (kw_list)
		av_free(kw_list);
	*__av_list = NULL;
	*__kw_list = NULL;
	return rc;
}

/*
 * Stop PAPI when the application is finished
 * or killed
 */
int deatach_pids()
{
	int event_count, rc, c;

	event_count = PAPI_num_events(ldms_metric_user_data_get(set, 0));
	long_long values[event_count];
	for (c = 0; c < pids_count && papi_event_sets; c++) {
		rc = PAPI_stop(papi_event_sets[c], values);
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, SAMP ": failed to stop process"
				" pid= %d. rc= %d\n", apppid[0], rc);
		}
		/*
		 * Detach when the application is finished or killed
		 */
		rc = PAPI_detach(papi_event_sets[c]);
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, SAMP ": failed to de-attach to "
				"process pid= %d. rc= %d\n", apppid[0], rc);
		}
		if (rc != PAPI_ENOEVST && apppid[0] != 0) {
			PAPI_cleanup_eventset(papi_event_sets[c]);
			PAPI_destroy_eventset(&papi_event_sets[c]);
		} else {
			msglog(LDMSD_LDEBUG, SAMP ": event set does not "
				"exist\n");
		}
	}

	if (apppid) {
		free(apppid);
		apppid = NULL;
	}

	if (papi_event_sets) {
		free(papi_event_sets);
		papi_event_sets = NULL;
	}

	attach = 0;
	pids_count = 0;
	strcpy(appname_str, "");

	msglog(LDMSD_LDEBUG, SAMP ": application is dead de-attach done!\n");

	return 0;
}

/**
 * \brief Configuration
 *
 * config name=spapi producer=<producer_name>
 * instance=<instance_name> metafile=<file>
 *     producer     The producer name.
 *     component_id The component id value.
 *     metafile	    The PAPI configuration file name and path
 *
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl,
	struct attr_value_list * avl)
{

	char *component_id;
	char* filename;
	char* producername;

	msglog(LDMSD_LDEBUG, SAMP ": config start \n");

	schema = NULL;

	producername = av_value(avl, "producer");
	if (!producername) {
		msglog(LDMSD_LERROR, SAMP ": missing producer\n");
		goto out;
	}

	producer_name = strdup(producername);

	component_id = av_value(avl, "component_id");
	if (component_id)
		compid = strdup(component_id);
	else
		compid = strdup("0");

	filename = av_value(avl, "metafile");
	if (!filename) {
		msglog(LDMSD_LERROR, SAMP ": sampler meta file is required\n");
		return ENOENT;
	}

	metadata_filename = strdup(filename);
	/* Check to see if we were successful */
	if (metadata_filename == NULL) {
		/* We were not so display a message */
		msglog(LDMSD_LERROR, SAMP ": could not allocate required "
			"memory for the string file name\n");
		goto out;
	}

	inot_fd = inotify_init1(IN_NONBLOCK);

	/* checking for error */
	if (inot_fd < 0) {
		msglog(LDMSD_LERROR, SAMP ": inotify initialization"
			" error = %d \n", inot_fd);
		return ENOENT;

	}

	return 0;
out:
	return EINVAL;
}

/*
 * Use the new configuration parameters to create a new schema and set
 */
int config_local(struct attr_value_list *kwl,
	struct attr_value_list * avl)
{
	int rc;
	char *instance_name = NULL;
	char *schema_name = NULL;
	char *events;
	char *maxpids;
	char *multiplx;
	char *numnodes;
	char *pp_n;
	char *numthreads;

	msglog(LDMSD_LDEBUG, SAMP ": re-configure the sampler\n");

	base = base_config(avl, SAMP, SAMP, msglog);
	if (!base) {
		rc = errno;
		goto out;
	}

	maxpids = av_value(avl, "max_pids");
	if (maxpids)
		max_pids = (uint64_t) (atoi(maxpids));
	else
		max_pids = 1;

	multiplx = av_value(avl, "multiplex");
	if (multiplx)
		multiplex = (uint64_t) (atoi(multiplx));
	else
		multiplex = 0;

	msglog(LDMSD_LDEBUG, SAMP ": maximum PIDs are %d.\n", max_pids);

	events = av_value(avl, "events");
	if (!events) {
		msglog(LDMSD_LERROR, SAMP ": missing papi events.\n");
		goto out;
	}

	/*
	 * The user have to supply the sampler with the application
	 * name during the configuration without supplying a file
	 *
	 * NOTE: the wait will be in the sampling not during the
	 * configuration
	 *
	 */
	msglog(LDMSD_LDEBUG, SAMP ": application name passed to "
		"the configuration \n");

	/*
	 * When the app name file is not used, the user have to supply
	 * the information using the configuration.
	 */
	appname_str = strdup(av_value(avl, "appname"));
	if (!appname_str) {
		msglog(LDMSD_LERROR, SAMP ": application name is "
			"required\n");
		goto out;
	}
	jobid = strdup(av_value(avl, "jobid"));
	if (!jobid) {
		msglog(LDMSD_LERROR, SAMP ": jobid is required\n");
		goto out;
	}
	username = strdup(av_value(avl, "username"));
	if (!username) {
		msglog(LDMSD_LERROR, SAMP ": username is required\n");
		goto out;
	}

	/* Number of nodes */
	numnodes = av_value(avl, "num_nodes");
	if (numnodes)
		num_nodes = (uint8_t) (atoi(numnodes));
	else
		num_nodes = 0;

	/* processes per nodes */
	pp_n = av_value(avl, "ppn");
	if (pp_n)
		ppn = (uint8_t) (atoi(pp_n));
	else
		ppn = 0;

	/* number of threads per process */
	numthreads = av_value(avl, "num_threads");
	if (numthreads)
		num_threads = (uint8_t) (atoi(numthreads));
	else
		num_threads = 0;

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": set already created.\n");
		goto out;
	}

	rc = create_metric_set(instance_name, schema_name, events);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		goto out;
	}

	ldms_set_producer_name_set(set, producer_name);
	return 0;
out:

	if (papi_event_set) {
		PAPI_destroy_eventset(&papi_event_set);
		PAPI_shutdown();
	}

	if (papi_event_val)
		free(papi_event_val);

	if (base)
		base_del(base);
	base = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
	free(events_codes);
	return EINVAL;
}

static ldms_set_t get_set(struct ldmsd_sampler * self)
{
	return set;
}

/*
 * This function called by the create_event_sets to create and add papi events
 * Input: event set
 */
static int papi_events(int c)
{
	int event_count;
	int rc;
	/* Create an event set for each pid */
	papi_event_sets[c] = PAPI_NULL;
	msglog(LDMSD_LDEBUG, SAMP ": application PID[%d] = %d\n", c,
		apppid[c]);
	rc = PAPI_create_eventset(&papi_event_sets[c]);
	if (rc != PAPI_OK) {
		msglog(LDMSD_LERROR, SAMP ": failed to create empty "
			"event set number %d error %d!\n", c, rc);
		return -1;
	}

	if (multiplex) {
		msglog(LDMSD_LDEBUG, SAMP ": multiplex in function is %d\n",
			multiplex);
		/* Explicitly bind event set to cpu component.
		 * PAPI documentation states that this must be done after
		 * PAPI_create_eventset, but before calling PAPI_set_multiplex.
		 * The argument 0 binds to cpu component.
		 */
		rc = PAPI_assign_eventset_component(papi_event_sets[c], 0);
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, SAMP ": failed to bind papi to cpu"
				" component!\n");
			rc = ENOENT;
			return -1;
		}

		/* Convert papi_event_set to a multiplexed event set */
		rc = PAPI_set_multiplex(papi_event_sets[c]);
		if (rc != PAPI_OK) {
			msglog(LDMSD_LERROR, SAMP ": failed to convert event "
				"set to multiplexed!\n");
			rc = ENOENT;
			return -1;
		}
	}

	event_count = PAPI_num_events(ldms_metric_user_data_get(set, 0));

	rc = PAPI_add_events(papi_event_sets[c],
		events_codes, event_count);
	if (rc != PAPI_OK) {
		msglog(LDMSD_LERROR, SAMP ": failed to add "
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
	/* The shell command to grep the application PID */
	char* command = calloc(strlen(appname_str) + 14, sizeof (char));
	/*
	 * Save the number of pids again because sometimes
	 * the number in the first read is incorrect
	 */
	msglog(LDMSD_LDEBUG, SAMP ": pgrep %s | wc -l\n", appname_str);
	sprintf(command, "pgrep %s | wc -l", appname_str);
	FILE *pipe_fp1;
	if ((pipe_fp1 = popen(command, "r")) == NULL) {
		msglog(LDMSD_LERROR, SAMP ": failed "
			"pgrep %s | wc -l\n", appname_str);
		pids_count = 0;
	} else {
		/* Get the application PID counts */
		fscanf(pipe_fp1, "%d", &pids_count);

		pclose(pipe_fp1);

		msglog(LDMSD_LDEBUG, SAMP ": create_event_sets pids_count"
			" = %d\n", pids_count);
		apppid = (int*) calloc(pids_count, sizeof (int));

		papi_event_sets = (int*) calloc(pids_count, sizeof (int));

		sprintf(command, "pgrep %s", appname_str);

		c = 0;
		/* Get the application pid */
		if ((pipe_fp1 = popen(command, "r")) == NULL) {
			msglog(LDMSD_LERROR, SAMP ": failed "
				"pgrep %s\n", appname_str);
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
	if (command)
                free(command);
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
	i = metric_offset;
	/* Attach to all PIDs */
	event_count = PAPI_num_events(ldms_metric_user_data_get(set, 0));
	while (c < pids_count && c < max_pids) {

		/*
		 * Start sampling
		 */
		if (PAPI_read(papi_event_sets[c],
			papi_event_val) != PAPI_OK) {
			msglog(LDMSD_LERROR, SAMP ": failed to read event "
				"set %d\n", papi_event_sets[c]);
			return -1;
		}

		/*
		 * Save the PID number in the pid metric
		 */
		val.v_u64 = apppid[c];
		ldms_metric_array_set_u64(set, metric_offset, c, val.v_u64);
		for (j = 0; j < event_count; j++) {
			/*
			 * j + i + 1 because
			 * component_id, appname, jobid, username, and pid are
			 * the first metrics
			 */
			val.v_u64 = papi_event_val[j];
			ldms_metric_array_set_u64(set, metric_offset + j + 1,
				c, val.v_u64);
		}
		c++;
	}
	return 0;
}

static int sample(struct ldmsd_sampler * self)
{
	int rc, c;
	int pid0_exist;
	int err;
	struct stat file_stat;
	struct sampler_meta *meta = NULL;

	/* Check if the file exist */
	err = stat(metadata_filename, &file_stat);
	msglog(LDMSD_LDEBUG, SAMP ": start the sample function, check if the "
		"file exist (0-yes)? %d\n", err);
	if (err != 0) {
		msglog(LDMSD_LDEBUG, SAMP ": file does not exist, create "
			"one\n");
		/* if not exist create one */
		file = fopen(metadata_filename, "w+");
		exist_before = 0;
		return 0;
	} else {
		/*
		 * If the file exist, set the inotify once
		 * Add the watch for close with write only
		 * exist_before used to add the watch one time
		 */
		if (exist_before == 0) {
			msglog(LDMSD_LDEBUG, SAMP ": add inotify watch to "
				"the file\n");
			file = fopen(metadata_filename, "w+");
			inot_wd = inotify_add_watch(inot_fd,
				metadata_filename,
				IN_CLOSE_WRITE);
			/* Read the support file */
			meta = read_sup_file();
			exist_before = 1;
		} else {
			msglog(LDMSD_LDEBUG, SAMP ": inotify, check if there is"
				" new data written to the file\n");
			inot_length = read(inot_fd,
				inot_buffer, 16);
			if (inot_length > 0) {
				/* File changed */
				/* Read the support file */
				msglog(LDMSD_LDEBUG, SAMP ": inotify, file was"
					" changed!\n");
				meta = read_sup_file();

				msglog(LDMSD_LDEBUG, SAMP ": remove the old "
					"base, schema, set, and papi stuff\n");
				/* remove the old configuration */
				msglog(LDMSD_LDEBUG, SAMP ": free base\n");
				if (base)
					base_del(base);
				base = NULL;
				msglog(LDMSD_LDEBUG, SAMP ": free set\n");
				if (set)
					ldms_set_delete(set);
				set = NULL;

				msglog(LDMSD_LDEBUG, SAMP ": free "
					"papi_event_set\n");
				if (papi_event_set) {
					PAPI_destroy_eventset(&papi_event_set);
					PAPI_shutdown();
				}
				papi_event_set = PAPI_NULL;

				msglog(LDMSD_LDEBUG, SAMP ": free "
					"papi_event_val\n");
				if (papi_event_val) {
					free(papi_event_val);
					papi_event_val = NULL;
				}
				msglog(LDMSD_LDEBUG, SAMP ": free appid\n");
				if (apppid) {
					free(apppid);
					apppid = NULL;
				}

				msglog(LDMSD_LDEBUG, SAMP ": free "
					"papi_event_sets\n");
				if (papi_event_sets) {
					free(papi_event_sets);
					papi_event_sets = NULL;
				}

				msglog(LDMSD_LDEBUG, SAMP ": free "
					"papi_event_codes\n");
				if (events_codes) {
					free(events_codes);
					events_codes = NULL;
				}

				/*
				 * If a new data read from the file then create a schema and
				 * set
				 */
				if (meta != NULL) {
					msglog(LDMSD_LDEBUG, SAMP ": new data "
						"exist, form the av_list from "
						"the file content\n");

					/* create the avl list */
					string2attr_list_local(meta->conf_line,
						&av_list_local,
						&kw_list_local);
					msglog(LDMSD_LDEBUG, SAMP ": run "
						"config_local to configure the "
						"sampler\n");

					/* Configure the sampler */
					config_local(kw_list_local,
						av_list_local);
				}
			}
		}
	}

	if (!set || schema == NULL) {
		msglog(LDMSD_LDEBUG, SAMP ": wait for new data in the file to "
			"configure the sampler\n");
		return 0;
	}

	/* The shell command to grep the application PID */
	char* command = calloc(strlen(appname_str) + 14, sizeof (char));

	msglog(LDMSD_LDEBUG, SAMP ": sampler configured successfully, watch "
		"for application PIDs (count = %d) \n", pids_count);

	if (pids_count == 0) {
		msglog(LDMSD_LDEBUG, SAMP ": watch for application, no PIDs,"
			" application not running\n");
		if (strlen(appname_str) > 1) {
			msglog(LDMSD_LDEBUG, SAMP ": pgrep %s | wc -l\n", appname_str);
			sprintf(command, "pgrep %s | wc -l", appname_str);
			FILE *fp = popen(command, "r");
			/* Get the application PID counts */
			fscanf(fp, "%d", &pids_count);
			msglog(LDMSD_LDEBUG, SAMP ": pids count = %d\n", pids_count);
			pclose(fp);

			if (pids_count >= ppn) {
				msglog(LDMSD_LDEBUG, SAMP ": create eventsets"
					" for %d PIDs\n", pids_count);
				if (create_event_sets() < 0) {
					goto err1;
				}
			} else {
				msglog(LDMSD_LDEBUG, SAMP ": Waiting for "
					"application to start\n");
				pids_count = 0;
			}
		} else msglog(LDMSD_LDEBUG, SAMP ": Waiting for the file "
			"to be changed\n");
	} else { /* When PID exist */
		/* check if the attach happened before, no need to attach */
		if (attach == 0) {
			msglog(LDMSD_LDEBUG, SAMP ": application started, papi"
				" start attaching now\n");
			c = 0;
			/* Attach to all PIDs */
			while (c < pids_count) {
				rc = PAPI_attach(papi_event_sets[c], apppid[c]);
				if (rc != PAPI_OK) {
					msglog(LDMSD_LERROR, SAMP ": failed to"
						" attach to process pid = %d"
						" rc= %d.\n", apppid[c], rc);
					goto err1;
				}
				rc = PAPI_start(papi_event_sets[c]);
				if (rc != PAPI_OK) {
					msglog(LDMSD_LERROR, SAMP ": failed to"
						" start papi event set "
						"rc= %d\n", rc);
					goto err1;
				}
				attach = 1;
				c++;
			}
			msglog(LDMSD_LDEBUG, SAMP ": application started, papi"
				" attach done!\n");
		} else {

			/* Check if the PAPI is attached and the the application
			 *  is a life by searching for the process number
			 * if note exist then De-attach PAPI
			 */
			msglog(LDMSD_LDEBUG, SAMP ": papi already attached, "
				"check if the application is alive \n");
			if (apppid)
				pid0_exist = kill(apppid[0], 0);
			else
				pid0_exist = ENOENT;

			if (pid0_exist == 0) {
				msglog(LDMSD_LDEBUG, SAMP ": application is "
					"alive, read papi events \n");
				base_sample_begin(base);
				if (save_events_data() < 0) {
					base_sample_end(base);
					goto err1;
				}
				base_sample_end(base);
			} else {
				msglog(LDMSD_LDEBUG, SAMP ": application is "
					"dead, papi de-attach now \n");
				deatach_pids();
			}
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
	if (file)
		fclose(file);

	if (papi_event_set) {
		PAPI_destroy_eventset(&papi_event_set);
		PAPI_shutdown();
	}

	if (papi_event_val)
		free(papi_event_val);
	if (base)
		base_del(base);
	base = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static const char *usage(struct ldmsd_plugin * self)
{
	return "config name=spapi producer=<producer_name> metafile=<file>\n"
	"    producer	  The producer name.\n"
	"    component_id The component id value.\n"
	"    metafile	  The PAPI configuration file name and path.\n";

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
