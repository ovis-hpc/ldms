/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2016,2018,2022-2023 National Technology & Engineering
 * Solutions of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525
 * with NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2016,2018,2022-2023 Open Grid Computing, Inc. All rights
 * reserved.
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
 * \file slingshot_switch.c
 * \brief slingshot switch data provider
 *
 * This sampler uses the dump_counters command to get data.
 * It uses \c LDMS_V_LIST and \c LDMS_V_RECORD in the same way as
 * \c procnetdev2.
 *
 * CONFIG file format:
 * p = comma separated list (no range yet) OR n = number
 * list of variables one per line or a group that it will parse
 *
 * interspersed comment lines (including first line) indicated by '#'
 * as the first char in a line
 *
 * GOAL is to construct the command line for dump counters
 */
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "../sampler_base.h"

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof(a) / sizeof(*a))
#endif

#define DUMPCOUNTERSLINE_MAX 2048
#define SS_VARNAME_MAX 128
#define SS_LINE_MAX 1024
//NOTE: avoid MAX in future versions
#define SS_PORT_MAX 70
static char slist[DUMPCOUNTERSLINE_MAX];
static char commandline[DUMPCOUNTERSLINE_MAX];
static char commandlineports[DUMPCOUNTERSLINE_MAX];
static char commandlinevars[DUMPCOUNTERSLINE_MAX];
static int commandline_ok = 0;
static int numvars = 0; /* NOTE: not keeping track of which ones */
static int numports = 0; /* NOTE: not keeping track of which ones */

static int rec_metric_id_port;

static int rec_def_idx;
static int ssport_list_mid;
static size_t rec_heap_sz;

#define SAMP "slingshot_switch"
static ldmsd_msg_log_f msglog;
static base_data_t base;



/* strip leading and trailing whitespace -- TAKEN FROM IBMAD_RECORDS_SAMPLER*/
static void strip_whitespace(char **start)
{
	/* strip leading whitespace */
	while (isspace(**start)) {
		(*start)++;
	}

	/* strip trailing whitespace */
	char * last;
	last = *start + strlen(*start) - 1;
	while (last > *start) {
		if (isspace(last[0])) {
			last--;
		} else {
			break;
		}
	}
	last[1] = '\0';
}

static int buildCommandLinePorts(char* lbuf){
	char plist[SS_LINE_MAX];
	char *pcpy;
	char *pch = NULL;
	char *saveptr = NULL;
	int n;
	int i;
	int rc;


	rc = sscanf(lbuf, "p=%s\n", plist);
	if (rc != 1){
		/* then parse for n */
		rc = sscanf(lbuf, "n=%d\n", &n);
		if (rc != 1){
			msglog(LDMSD_LERROR,
			       SAMP ": Bad format in file. First line MUST be p=XXX or n=XXX\n");
			return EINVAL;
		}
		if ((n < 0) || (n > SS_PORT_MAX)) {
			msglog(LDMSD_LERROR,
			       SAMP ": ERROR: Bad port value '%d'\n", n);
			return EINVAL;
		}
		numports = n+1; /* will start at 0 */
		snprintf(commandlineports, sizeof(commandlineports),
			 " -n %d", n );
		return 0;
	}

	/* parse for p */
	snprintf(commandlineports, sizeof(commandlineports), "");
	pcpy = strdup(plist);
	if (!pcpy){
		msglog(LDMSD_LERROR, SAMP ": Out of memory\n");
		rc = ENOMEM;
		goto err;
	}

	pch = strtok_r(pcpy, ",", &saveptr);
	while (pch != NULL){
		/* NOTE: concatinates a range expression to the first val only */
		rc = sscanf(pch, "%d", &i);
		if (rc != 1){
			msglog(LDMSD_LERROR,
			       SAMP ": Bad format in file. p has '%s' within\n",
			       pch);
			rc = EINVAL;
			goto err;
		} else if ((i < 0) || (i > SS_PORT_MAX)){
			msglog(LDMSD_LERROR, SAMP ": Bad port value '%d'\n", n);
			rc = EINVAL;
			goto err;
		}
		snprintf(lbuf, sizeof(lbuf), " -p %d", i);
		if (strlen(lbuf) + strlen(commandlineports) >=
		    sizeof(commandlineports)){
			msglog(LDMSD_LERROR,
			       SAMP ": port lengths will exceed MAX command line\n");
			rc = EINVAL;
			goto err;
		}
		strcat(commandlineports, lbuf);
		numports++; /* NOTE: not checking for duplicates */
		pch = strtok_r(NULL, ",", &saveptr);
	}
	rc = 0;

 err:
	free(pcpy);
	pcpy = NULL;
	return rc;
}


static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	if (base)
		return base->set;
	return NULL;
}

static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	ldms_record_t rec_def;
	size_t heap_sz;
	FILE *mf;
	char cline[DUMPCOUNTERSLINE_MAX];
	char lbuf[SS_LINE_MAX];
	char varname[SS_VARNAME_MAX];
	int port;
	char* varptr;
	char* s;
	unsigned long v;
	int i;
	int rc;

	/* make a 1 port output to parse */
	snprintf(cline, sizeof(cline), "dump_counters -p 1");
	if (strlen(cline) + strlen(commandlinevars) > sizeof(cline)){
		msglog(LDMSD_LERROR,
		       SAMP ": Varname lengths will exceed MAX command line\n");
		return EINVAL;
	}
	strcat(cline, commandlinevars);

	/* Create a metric set of the required size */
	schema = base_schema_new(base);
	if (!schema) {
		msglog(LDMSD_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = EINVAL;
		goto err1;
	}

	rec_def = ldms_record_create("slingshot_port");
	if (!rec_def){
		rc = EINVAL;
		goto err1;
	}

	/* NOTE: need val 'x' (not 0 nor NULL) */
	msglog(LDMSD_LDEBUG,
	       SAMP ": Adding record %d '%s' type LDMS_V_CHAR_ARRAY \n",
	       0, "port");
	rc = ldms_record_metric_add(rec_def, "port", "x", LDMS_V_U64, 1);
	if (rc < 0){
		/* errno is already set */
		goto err3;
	}
	rec_metric_id_port = rc;
	msglog(LDMSD_LDEBUG,
	       SAMP ": Port metric id '%d'\n", rec_metric_id_port);
	msglog(LDMSD_LDEBUG,
	       SAMP ": will be executing '%s' to get the var names\n",
	       cline);
	numvars = 0;

	mf = popen(cline, "r");
	if (!mf) {
	msglog(LDMSD_LERROR, SAMP ": Could not execute '%s'\n", cline);
		rc = ENOENT;
		goto err3;
	}
	/* first line is just the header */
	s = fgets(lbuf, sizeof(lbuf), mf);
	if (!s){
		msglog(LDMSD_LERROR, SAMP ": Could not skip first line.\n");
		rc = EINVAL;
		goto err3;
	}
	msglog(LDMSD_LDEBUG, SAMP ": Read '%s'\n", lbuf);

	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		msglog(LDMSD_LDEBUG, SAMP ": Read '%s'\n", lbuf);

		rc = sscanf(lbuf,"%d,%[^,],%lu", &port, varname, &v);
		if (rc != 3) {
			/* NOTE: Treating like this is the last line. */
			rc = 0;
			break;
		}
		varptr = varname;
		strip_whitespace(&varptr);

		msglog(LDMSD_LDEBUG,
		       SAMP ": Adding record %d '%s' type U64 \n",
		       numvars++, varptr);
		/* NOTE: need val 'x' (not 0 nor NULL) */
		rc = ldms_record_metric_add(rec_def, varptr, "x",
					    LDMS_V_U64, 1);
		if (rc < 0) {
			/* errno is already set */
			goto err3;
		}
		msglog(LDMSD_LDEBUG, SAMP ": Added metric id '%d'\n", rc);
	} while(s);
	rc = 0;

	if (numvars == 0){
		msglog(LDMSD_LERROR, SAMP ": No vars!\n");
		rc = EINVAL;
		goto err3;
	} else {
		msglog(LDMSD_LDEBUG,
		       SAMP ": Added %d records + port\n", numvars);
	}

	rec_heap_sz = ldms_record_heap_size_get(rec_def);
	heap_sz = SS_PORT_MAX * ldms_record_heap_size_get(rec_def);

	/* Add record definition into the schema */
	rec_def_idx = ldms_schema_record_add(schema, rec_def);
	if (rec_def_idx < 0) {
		rc = -rec_def_idx;
		goto err3;
	}

	/* Add a list (of records) */
	/* NOTE: if this is being allocated, can unnecessary ones be avoided? */
	ssport_list_mid = ldms_schema_metric_list_add(schema, "slingshot_port_list",
						      NULL, heap_sz);
	if (ssport_list_mid < 0) {
		rc = -ssport_list_mid;
		goto err2;
	}

	base_set_new(base);
	if (!base->set) {
		rc = errno;
		goto err2;
	}

	if (mf)
		pclose(mf);
	mf = NULL;

	return 0;
err3:
	/* Only manually delete rec_def when it has not yet been added
	   to the schema */
	ldms_record_delete(rec_def);
err2:
	base_schema_delete(base);
	base = NULL;
err1:
	if (mf)
		pclose(mf);
	mf = NULL;

	return rc;
}


/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl,
			struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set"};

	for (i = 0; i < ARRAY_LEN(deprecated); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			msglog(LDMSD_LERROR,
			       SAMP ": config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return "config name=" SAMP " conffile=<conffile>\n" \
		BASE_CONFIG_USAGE \
		"    <conffile>     full path for the configuration file\n";
}

static int config(struct ldmsd_plugin *self,
		  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	FILE* mf;
	char* conffile;
	char lbuf[SS_LINE_MAX];
	char varname[SS_VARNAME_MAX];
	int len = 0;
	int pnline = 0;
	char *s;
	char *ivalue = NULL;
	void *arg = NULL;
	int i;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}

	if (base) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	/* process conffile */
	ivalue = av_value(avl, "conffile");
	if (!ivalue) {
		msglog(LDMSD_LERROR,
		       SAMP ": A configuration file is required.\n");
		goto err1;
	}

	conffile = strdup(ivalue);
	msglog(LDMSD_LDEBUG,
	       SAMP ": Should be trying to process conffile '%s'\n", conffile);
	mf = fopen(conffile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, SAMP ": Could not open '%s'\n", conffile);
		rc = ENOENT;
		goto err1;
	 }

	fseek(mf, 0, SEEK_SET); /* seek should work if get to EOF */
	/* first non-comment line MUST be "p=XXX" or "n=XXX" */
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s){
			msglog(LDMSD_LERROR,
			       SAMP ": Bad format in file. First line noncomment line MUST be p=XXX or n=XXX\n");
			rc = EINVAL;
			goto err1;
		}
		if ((strlen(lbuf) > 1) && (lbuf[0] == '#')){
			msglog(LDMSD_LDEBUG,
			       SAMP ": skipping input <%s>\n", lbuf);
			continue;
		}

		break; /* Got a line to parse */
	} while (s);

	/* note command line will be incomplete after this */
	rc = buildCommandLinePorts(lbuf);
	if (rc)
		goto err1;

	/* variables: store them in the slist, then execute
	   a command on a single port to get all the values.  */
	snprintf(commandlinevars, sizeof(commandlinevars), "");
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf," %s", varname); /* NOTE: check for extra whitespace */
		if ((rc != 1) || (strlen(varname) == 0) || (varname[0] == '#')){
			 msglog(LDMSD_LDEBUG,
				SAMP ": skipping input <%s>\n", lbuf);
			 continue;
		}
		 snprintf(lbuf, sizeof(lbuf), " -s %s", varname);
		 if (strlen(lbuf) + strlen(commandlinevars) >=
		     sizeof(commandlinevars)){
			 msglog(LDMSD_LERROR,
				SAMP ": varname lengths will exceed MAX command line\n");
			 rc = EINVAL;
			 goto err1;
		 }
		 strcat(commandlinevars, lbuf);
	} while(s);

	if (mf)
		fclose(mf);
	mf = NULL;

cfg:
	base = base_config(avl, SAMP, SAMP, msglog);
	if (!base){
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}

	snprintf(commandline, sizeof(commandline), "dump_counters");
	if (strlen(commandline) + strlen(commandlineports) + strlen(commandlinevars) > sizeof(commandline)){
		msglog(LDMSD_LERROR,
		       SAMP ": Ports and vars lengths will exceed MAX command line\n");
		rc = EINVAL;
		goto err;
	}

	strcat(commandline, commandlineports);
	strcat(commandline, commandlinevars);
	commandline_ok = 1;

	free(conffile);
	conffile = NULL;
	return 0;

 err:
	base_del(base);

 err1:
	if (mf)
		pclose(mf);
	mf = NULL;
	free(conffile);
	conffile = NULL;
	numvars = 0;
	numports = 0;
	commandline[0] = '\0';
	commandline_ok = 0;
	return rc;
}

static int sample(struct ldmsd_sampler *self)
{
	FILE* mf;
	char lbuf[SS_LINE_MAX];
	int port;
	union ldms_value vport;
	char varname[SS_VARNAME_MAX];
	union ldms_value* v;
	char *s;
	int i,j ;
	int iss;
	ldms_mval_t lh, rec_inst;
	size_t heap_sz;
	int rc;


	msglog(LDMSD_LDEBUG, SAMP ": in sample\n");

	if (!base){
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	mf = popen(commandline, "r");
	if (!mf) {
		msglog(LDMSD_LERROR,
		       SAMP ": Could not execute '%s'\n", commandline);
		return ENOENT;
	}

	v = calloc(numvars, sizeof(union ldms_value));
	if (!v){
		msglog(LDMSD_LERROR, SAMP ": Out of memory.\n");
		if (mf)
			pclose(mf);
		mf = NULL;
		return ENOMEM;
	}

begin:
	base_sample_begin(base);

	lh = ldms_metric_get(base->set, ssport_list_mid);

	/* reset device data */
	ldms_list_purge(base->set, lh);

	fseek(mf, 0, SEEK_SET); //seek should work if get to EOF

	/* first line is just the header */
	s = fgets(lbuf, sizeof(lbuf), mf);
	msglog(LDMSD_LDEBUG, SAMP ": Read '%s'\n", lbuf);
	if (!s){
		msglog(LDMSD_LERROR, SAMP ": Bad format in command output.\n");
		rc = EINVAL;
		goto ERR;
	}

	/* for each port for each set of variables..... */
	i = 0; /* ports */
	j = 0; /* vars. NOTE: 'port' is not one of them */
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		msglog(LDMSD_LDEBUG, SAMP ": Read '%s'\n", lbuf);

		rc = sscanf(lbuf,"%d,%[^,],%lu", &port, varname, &v[j].v_u64);
		if (rc != 3) {
			msglog(LDMSD_LERROR,
			       SAMP ": Bad output to command on port %d variable %d (%d): '%s'\n",
			       i, j, rc, lbuf);
			rc = EINVAL;
			goto ERR;
		}
	rec:
		if (j == (numvars-1)){
			rec_inst = ldms_record_alloc(base->set, rec_def_idx);
			if (!rec_inst)
				goto resize;
			vport.v_u64 = port;
			msglog(LDMSD_LDEBUG,
			       SAMP ": Should be setting port to %lu\n",
			       vport.v_u64);
			ldms_record_set_u64(rec_inst, rec_metric_id_port,
					    vport.v_u64);
			for (j = 0; j < numvars; j++){
				msglog(LDMSD_LDEBUG,
				       SAMP ": Should be setting var %d metric %d to %lu\n",
				       j, rec_metric_id_port+j+1, v[j].v_u64);
				ldms_record_set_u64(rec_inst,
						    rec_metric_id_port+j+1,
						    v[j].v_u64);
			}
			ldms_list_append_record(base->set, lh, rec_inst);
			j = 0;
			i++;
		} else {
			j++;
		}
		if (i == numports){
			msglog(LDMSD_LDEBUG,
			       SAMP ": read everything expected. breaking\n");
			break;
			/* NOTE: not checking if there are extra lines.
			   There will be one extra in the output */
		}
	} while(s);

	base_sample_end(base);
	if (mf)
		pclose(mf);
	mf = NULL;

	return 0;

resize:

	free(v);

	/*
	 * We intend to leave the set in the inconsistent state so that
	 * the aggregators are aware that some metrics have not been newly
	 * sampled.
	 */
	heap_sz = ldms_set_heap_size_get(base->set) + 2*rec_heap_sz;
	base_set_delete(base);
	base_set_new_heap(base, heap_sz);
	if (!base->set) {
		rc = errno;
		ldmsd_log(LDMSD_LCRITICAL, SAMP " : Failed to create a set with "
						"a bigger heap. Error %d\n", rc);
		return rc;
	}
	goto begin;

 ERR:
	free(v);
	base_sample_end(base);
	if (mf)
		pclose(mf);
	mf = NULL;

	return rc;

}


static void term(struct ldmsd_plugin *self)
{
	numvars = 0;
	numports = 0;
	commandline[0] = '\0';
	commandline_ok = 0;
	base_set_delete(base);
	base_del(base);
	base = NULL;
}


static struct ldmsd_sampler slingshot_switch_plugin = {
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
	base = NULL;
	return &slingshot_switch_plugin.base;
}
