/*
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
 */

/**
 * \file procnetdev.c
 * \brief /proc/net/dev data provider
 */
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include "ldms.h"
#include "ldmsd.h"

#define PROC_FILE "/proc/net/dev"
static char *procfile = PROC_FILE;
int ninterfaces;
#define VARSPERINTERFACE 16;
static char varname[][30] = 
  {"rx_bytes", "rx_packets", "rx_errs", "rx_drop", "rx_fifo", "rx_frame",
   "rx_compressed", "rx_multicast", "tx_bytes", "tx_packets", "tx_errs",
   "tx_drop", "tx_fifo", "tx_colls", "tx_carrier", "tx_compressed"};

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
ldms_metric_t compid_metric_handle;
union ldms_value comp_id;


static ldms_set_t get_set()
{
	return set;
}

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i, metric_count;
	char *s;
	char lbuf[256];
	char metric_name[128];

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog("Could not open /proc/net/dev file '%s'...exiting\n", procfile);
		return ENOENT;
	}

	char ifname[128];

	/*
	 * Process the file once first to determine the metric set size.
	 */

	rc = ldms_get_metric_size("component_id", LDMS_V_U64, &tot_meta_sz, &tot_data_sz);
	metric_count = 0;
	fseek(mf, 0, SEEK_SET);

	//first line is header
	s = fgets(lbuf, sizeof(lbuf), mf);
	//second line is header
	//we are currently assuming we know the header names....
	s = fgets(lbuf, sizeof(lbuf), mf);
	//rest is data
	while(s) {
	  s = fgets(lbuf, sizeof(lbuf), mf);
	  if (!s)
	    break;
	  int currcol = 0;
	  char* pch = strtok (lbuf," \t|");
	  while (pch != NULL){
	    if (pch[0] == '\n'){
	      break;
	    }
	    if (currcol == 0){
	      /* Strip the colon from interface name if present */
	      i = strlen(pch);
	      if (i && pch[i-1] == ':')
		pch[i-1] = '\0';
	      strcpy(ifname, pch);
	    } else {
	      //the metric name will be ifname:name  
	      snprintf(metric_name,128,"%s:%s",ifname,varname[currcol-1]);
	      rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	      tot_meta_sz += meta_sz;
	      tot_data_sz += data_sz;
	      metric_count++;
	    }
	    currcol++;
	    pch = strtok(NULL," ");
	  } // while (strtok)
	} //while(s)

	/* Create a metric set of the required size */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
	  return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
	  goto err;

	/*
	 * Process the file again to define all the metrics.
	 */
	compid_metric_handle = ldms_add_metric(set, "component_id", LDMS_V_U64);
	if (!compid_metric_handle) {
	  rc = ENOMEM;
	  goto err;
	} //compid set in sample

	int metric_no = 0;
	fseek(mf, 0, SEEK_SET);

	//first line is header
	s = fgets(lbuf, sizeof(lbuf), mf);
	//second line is header
	//we are currently assuming we know the header names....
	s = fgets(lbuf, sizeof(lbuf), mf);
	//rest is data
	while(s) {
	  s = fgets(lbuf, sizeof(lbuf), mf);
	  if (!s)
	    break;
	  int currcol = 0;
	  char* pch = strtok (lbuf," \t|");
	  while (pch != NULL){
	    if (pch[0] == '\n'){
	      break;
	    }
	    if (currcol == 0){
	      /* Strip the colon from interface name if present */
	      i = strlen(pch);
	      if (i && pch[i-1] == ':')
		pch[i-1] = '\0';
	      strcpy(ifname, pch);
	    } else {
	      //the metric name will be ifname:name  
	      snprintf(metric_name,128,"%s:%s",ifname,varname[currcol-1]);
	      metric_table[metric_no] = ldms_add_metric(set, metric_name, LDMS_V_U64);
	      if (!metric_table[metric_no]){
		rc = ENOMEM;
		goto err;
	      }
	      metric_no++;
	    }
	    currcol++;
	    pch = strtok(NULL," ");
	  } // while (strtok)
	} //while(s)
	return 0;

 err:
	ldms_set_release(set);
	return rc;
}

/**
 * \brief Configuration
 *
 * - config procinterrupts component_id <value>
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{

  char *value;
  
  value = av_value(avl, "component_id");
  if (value)
    comp_id.v_u64 = strtol(value, NULL, 0);
  
  value = av_value(avl, "set");
  if (value)
    create_metric_set(value);
  
  return 0;
}

static int sample(void)
{
	int metric_no;
	char *s;
	char lbuf[256];
	union ldms_value v;

	if (!set){
	  msglog("procnetdev: plugin not initialized\n");
	  return EINVAL;
	}
	ldms_set_metric(compid_metric_handle, &comp_id);

	metric_no = 0;
	fseek(mf, 0, SEEK_SET);


	//first line is header
	s = fgets(lbuf, sizeof(lbuf), mf);
	//second line is header
	//we are currently assuming we know the header names....
	s = fgets(lbuf, sizeof(lbuf), mf);
	//rest is data
	while(s) {
	  s = fgets(lbuf, sizeof(lbuf), mf);
	  if (!s)
	    break;
	  int currcol = 0;
	  char* pch = strtok (lbuf," \t|");
	  while (pch != NULL){
	    if (pch[0] == '\n'){
	      break;
	    }
	    if (currcol == 0){
	      //skip
	    } else {
	      char* endptr;
	      unsigned long long int l1;
	      l1 = strtoull(pch,&endptr,10);
	      if (endptr != pch){
		v.v_u64 = l1;
		ldms_set_metric(metric_table[metric_no], &v);
		metric_no++;
	      } else {
		msglog("bad val <%s>\n",pch);
		return EINVAL;
	      }
	    }
	    currcol++;
	    pch = strtok(NULL," ");
	  } // while (strtok)
	} //while(s)

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
	return  "config name=procnetdev component_id=<comp_id> set=<setname>\n"
		"    comp_id     The component id value.\n"
		"    setname     The set name.\n";
}



static struct ldmsd_sampler procnetdev_plugin = {
	.base = {
		.name = "procnetdev",
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
	return &procnetdev_plugin.base;
}
