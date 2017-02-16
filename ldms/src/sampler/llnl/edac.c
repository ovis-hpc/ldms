/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017, Sandia Corporation.
 * Copyright (c) 2016, Lawrence Livermore National Security, LLC. 
 * Produced at the Lawrence Livermore National Laboratory. Written by
 * Kathleen Shoga <shoga1@llnl.gov> (Lawrence Livermore National Lab)
 * 
 * LLNL-CODE-685879 All rights reserved. 
 * This file is part of EDAC Plugin, Version 1.0
 *
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License (as published 
 * by the Free Software Foundation) version 2, dated June 1991.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the terms 
 * and conditions of the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Please see LLNL_LICENSE_EDAC for the full details.
 *
 * This work was performed under the auspices of the U.S. Department of 
 * Energy by Lawrence Livermore National Laboratory under 
 * Contract DE-AC52-07NA27344.
 *
 * This plugin uses the template of the other LDMS plugins, but it
 * gives access to different data. The following is the original header.
 *
 *
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
 * \file edac.c
 * \Grab edac data; based off of clock.c.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <dirent.h>
// extra stuff not needed.
#include <stdlib.h> // needed for strtoull processing of comp_id
//#include <string.h> // needed for memcpy in ldms.h unused feature
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_jobid.h"

// plugin specific (edac)
int edac_valid = 0; /* sample disabled until configured correctly */
int max_mc;
int max_csrow;
int totalCommands;
#define MAXSZ 70
char command[MAXSZ][MAXSZ];
char edac_name[MAXSZ][MAXSZ];

static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;
#define SAMP "edac"
static char *default_schema_name = SAMP;
static uint64_t comp_id;


static char *replace_slash(char *s)
{
	char *s1;

	s1 = s;
	while ( *s1 ) {
		if ( *s1 == '/' ) {
			*s1 = '_';
		}
		++ s1;
	}
	return s;
}
static int metric_offset = 1;
LJI_GLOBALS;

static int create_metric_set(const char *instance_name, char* schema_name)
{
	int rc;
	union ldms_value v;
	schema = ldms_schema_new(schema_name);
	if (!schema) {
		msglog(LDMSD_LERROR, SAMP ": schema_new(%s) failed.\n",
			schema_name);
		rc = ENOMEM;
		goto err;
	}

	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		msglog(LDMSD_LERROR, SAMP ": add comp id failed.\n");
		rc = ENOMEM;
		goto err;
	}

	metric_offset++;
	rc = LJI_ADD_JOBID(schema);
	if (rc < 0) {
		msglog(LDMSD_LERROR, SAMP ": add jobid failed.\n");
		goto err;
	}

	// My variables
	FILE * myFile;
	int i;
	int counter=0;
	int mc_counter=0;
	int csrow_counter=0;

	// Set global totalCommands
	totalCommands = (max_mc*5) + (max_mc*max_csrow*3);
	// Check to make sure it's not more than array size
	if (totalCommands > MAXSZ)
	{
		msglog(LDMSD_LERROR, SAMP ": Plugin total registers larger than allowed. Check max_mc*5+max_mc*max_csrow*3 < %d\n", MAXSZ);
		msglog(LDMSD_LERROR, SAMP ": max_mc %d max_csrow %d\n", max_mc, max_csrow);
		return EINVAL;
	}
	if (! max_mc) {
		msglog(LDMSD_LERROR, SAMP ": No registers requested. Check max_mc, max_csrow\n");
		return EINVAL;
	}

	// Filling command and edac_name arrays
	for (mc_counter=0; mc_counter < max_mc; mc_counter++)
	{
		sprintf(command[counter], "/sys/devices/system/edac/mc/mc%d/ce_count",mc_counter);	
		counter++;
		sprintf(command[counter], "/sys/devices/system/edac/mc/mc%d/ce_noinfo_count",mc_counter);	
		counter++;
		sprintf(command[counter], "/sys/devices/system/edac/mc/mc%d/ue_count",mc_counter);	
		counter++;
		sprintf(command[counter], "/sys/devices/system/edac/mc/mc%d/ue_noinfo_count",mc_counter);	
		counter++;
		for (csrow_counter=0; csrow_counter < max_csrow; csrow_counter++)
		{
			sprintf(command[counter],"/sys/devices/system/edac/mc/mc%d/csrow%d/ce_count",mc_counter,csrow_counter);	
			counter++;
			sprintf(command[counter],"/sys/devices/system/edac/mc/mc%d/csrow%d/ue_count",mc_counter,csrow_counter);	
			counter++;
			sprintf(command[counter],"/sys/devices/system/edac/mc/mc%d/csrow%d/ch0_ce_count",mc_counter,csrow_counter);	
			counter++;
		}
		sprintf(command[counter], "/sys/devices/system/edac/mc/mc%d/seconds_since_reset",mc_counter);	
		counter++;
	}

	// Running through commands to check if they are all available. If not, then skip rest of setup and send error
	for ( i=0; i<totalCommands; i+=1 )
	{
		myFile = fopen(command[i], "r");
		if (myFile == NULL)
		{
			msglog(LDMSD_LERROR, SAMP ": failed to open file during config.\n");
			rc = EINVAL;
			edac_valid=0;
			return rc;
		}
		fclose(myFile);
	}

	// Setting the names
	for (i=0; i<totalCommands; i++)
	{
		strcpy(edac_name[i],command[i]+28);
		replace_slash(edac_name[i]);
	}
	
	for(i = 0; i < totalCommands; i++ )
	{
		rc = ldms_schema_metric_add(schema, edac_name[i], LDMS_V_U64);
		if (rc < 0) {
			msglog(LDMSD_LERROR, SAMP ": metric_add failed %s\n",
				edac_name[i]);
			rc = ENOMEM;
			goto err;
		}
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": failed to create set during config.\n");
		goto err;
	}

	// update specialized metrics
	v.v_u64 = comp_id;
	ldms_metric_set(set, 0, &v);

	LJI_SAMPLE(set,1);
	return 0;

 err:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	return rc;
}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set"};
	char* misplaced[]={"policy"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			msglog(LDMSD_LERROR, SAMP ": config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}
	for (i = 0; i < (sizeof(misplaced)/sizeof(misplaced[0])); i++){
		value = av_value(avl, misplaced[i]);
		if (value){
			msglog(LDMSD_LERROR, SAMP ": config argument %s is misplaced.\n",
			       misplaced[i]);
			return EINVAL;
		}
	}
#define EDAC_ROOT "/sys/devices/system/edac/mc"
	DIR* dir = opendir(EDAC_ROOT);
	if (dir) {
		closedir(dir);
	} else {
		if (ENOENT == errno)
			msglog(LDMSD_LERROR, SAMP ": edac not enabled? (no %s) found.\n",
				EDAC_ROOT);
		else
			msglog(LDMSD_LERROR, SAMP ": edac not reable? (at %s).\n",
				EDAC_ROOT);
		return EINVAL;
	}

	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP "  producer=<prod_name> instance=<inst_name> [component_id=<comp_id> schema=<sname> max_mc=<max_mc> max_csrow=<max_csrow>\n"
		"    <prod_name>  The producer name\n"
		"    <inst_name>  The instance name\n"
		"    <comp_id>    Optional unique number identifier. Defaults to zero.\n"
		LJI_DESC
		"    <sname>      Optional schema name. Defaults to '" SAMP "'\n"
		"    <max_mc>      The max number of mc.\n"
		"    <max_csrow>   The max number of csrows per mc.\n";
}

/**
 * \brief Configuration
 *
 * config name=edac component_id=<comp_id>  max_mc=<max_mc> max_csrow=<max_csrow> with_jobid=<bool>
 *     comp_id     The component id value.
 *     bool        lookup jobid or report 0.
 *     max_mc      The max number of mc.
 *     max_csrow   The max number of csrows per mc.
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *sname;
	void * arg = NULL;
	long tmp;
	int rc=0;

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, SAMP ": missing producer.\n");
		return ENOENT;
	}

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtoull(value, NULL, 0);
	else
		comp_id = 0;

	LJI_CONFIG(value,avl);

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, SAMP ": missing instance.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0) {
		msglog(LDMSD_LERROR, SAMP ": schema name invalid.\n");
		return EINVAL;
	}
	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	char *mvalue = av_value(avl, "max_mc");
	if (mvalue)
	{
		tmp=strtol(mvalue, NULL,10);
		if (tmp > 0)
			max_mc = (int)tmp;
		else
		{
			msglog(LDMSD_LERROR, SAMP ": Plugin input %s for max_mc not valid.\n",
				mvalue);
			return EINVAL;
		}

	}
	mvalue = av_value(avl, "max_csrow");
	if (mvalue)
	{
		tmp=strtol(mvalue, NULL,10);
		if (tmp > 0)
			max_csrow = (int)tmp;
		else
		{
			msglog(LDMSD_LERROR, SAMP ": Plugin input %s for max_csrow not valid.\n",
				mvalue);
			return EINVAL;
		}
	}

	rc = create_metric_set(value, sname);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);
	edac_valid=1;

	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int sample(struct ldmsd_sampler *self)
{
	// If there was an error before, don't send multiple errors, just return
	if (edac_valid != 1)
	{
		return 0;
	}
	int rc;
	int metric_no;
	union ldms_value v;

	// My variables
	FILE * myFile;
	char * s;
	char lineBuffer[256];
	int i;
	rc=0;

	if (!set) {
		msglog(LDMSD_LERROR, SAMP ": plugin not initialized\n");
		return EINVAL;
	}
	ldms_transaction_begin(set);

	LJI_SAMPLE(set, 1);
	metric_no = metric_offset;

	// Begin getting numbers
	for ( i=0; i<totalCommands; i+=1 )
	{
		myFile = fopen(command[i], "r");
		if (myFile == NULL)
		{
			msglog(LDMSD_LERROR, SAMP ": failed to open file\n");
			rc = EINVAL;
			edac_valid=0;
			goto out;
		}
		s = fgets(lineBuffer, sizeof(lineBuffer), myFile);
		if (!s) {
			rc = EINVAL;
			edac_valid=0;
			goto out;
		}
		rc = sscanf(lineBuffer, "%" PRIu64, &v.v_u64);
		if (rc != 1) {
			rc = EINVAL;
			fclose(myFile);
			msglog(LDMSD_LERROR, SAMP ": read as uint64_t failed.\n");
			edac_valid=0;
			goto out;
		}
		ldms_metric_set(set, metric_no, &v);
		metric_no++;
		fclose(myFile);
	}

out:
	rc = ldms_transaction_end(set);
	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler edac_plugin = {
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
	return &edac_plugin.base;
}
