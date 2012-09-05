/*
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
 * \file lmsensors.c
 * \brief lmsensors data provider
 *
 * Since LDMS does not support double yet, read all vals as doubles but 
 * records them as uint64_t. Will need to cast back before insertion
 * into the database.
 */
#include <inttypes.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include "ldms.h"
#include "ldmsd.h"

static char command[10] = "sensors";
static uint64_t counter;

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
ldms_metric_t compid_metric_handle;
ldms_metric_t cast_metric_handle;
ldms_metric_t counter_metric_handle;

static pthread_mutex_t cfg_lock;

/** 
 * \brief Configuration
 * 
 * Usage: 
 * - config lmsensors component_id <value>
 */
static int config(char *str)
{
  pthread_mutex_lock(&cfg_lock);

  if (!set || !compid_metric_handle || !cast_metric_handle || !counter_metric_handle) {
    msglog("lmsensors: plugin not initialized\n");
    pthread_mutex_unlock(&cfg_lock);
    return EINVAL;
  }

  //expects "component_id value"
  if (0 == strncmp(str, "component_id", 12)) {
    char junk[128];
    int rc;
    union ldms_value v;
    
    rc = sscanf(str, "component_id %" PRIu64 "%s\n", &v.v_u64, junk);
    if (rc < 1){
      pthread_mutex_unlock(&cfg_lock);
      return EINVAL;
    }
    ldms_set_metric(compid_metric_handle, &v);
  }

  //add the val that indicates that it has been cast
  union ldms_value ucastval;
  //  double dcastval = 1.0;
  //  uint64_t* upcastval = (uint64_t*) &dcastval;
  //  ucastval.v_u64 = *upcastval;
  ucastval.v_u64 = 1;
  ldms_set_metric(cast_metric_handle, &ucastval);

  //add the counter
  counter = 0;
  union ldms_value v;
  v.v_u64 = counter;
  ldms_set_metric(counter_metric_handle, &v);

  pthread_mutex_unlock(&cfg_lock);
  return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int init(const char *path)
{
  size_t meta_sz, tot_meta_sz;
  size_t data_sz, tot_data_sz;
  int rc, metric_count;
  char buf[1024];
  char metric_name[128];

  FILE* fpipe;

  pthread_mutex_lock(&cfg_lock);

  rc = ldms_get_metric_size("component_id", LDMS_V_U64, &tot_meta_sz, &tot_data_sz);
  rc = ldms_get_metric_size("cast_from_native", LDMS_V_U64, &meta_sz, &data_sz);
  tot_meta_sz += meta_sz;
  tot_data_sz += data_sz;
  rc = ldms_get_metric_size("lmsensors_counter", LDMS_V_U64, &meta_sz, &data_sz);
  tot_meta_sz += meta_sz;
  tot_data_sz += data_sz;

  metric_count = 0;

  if (!(fpipe = (FILE*)popen(command,"r"))){
    perror("Problems with pipe");
    pthread_mutex_unlock(&cfg_lock);
    return -1;
  }

  /*
   * Process the file once first to determine the metric set size.
   */

  while(fgets(buf, sizeof buf, fpipe)){
    //examples: 
    //FAN7/CPU4:   0 RPM  (min =  712 RPM)                   ALARM
    //FAN3/CPU2:12500 RPM  (min =  712 RPM) 
    //CPU1 Temp: +26.8°C  (high = +72.0°C, hyst = +67.0°C) 

    char* pch=strchr(buf,':');
    if (pch != NULL){
      if ((pch-buf+1) != strlen(buf)){
	char *endptr;
	double val;
	snprintf(metric_name,(pch-buf+1),"%s",buf);
	errno = 0;
	val = strtod((pch+1),&endptr);
	if ((errno == ERANGE) || endptr == (pch+1)){
	  //skip this one
	} else {
	  rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	  if (rc){
	    if (fpipe) pclose(fpipe);
	    pthread_mutex_unlock(&cfg_lock);
	    return rc;
	  }

	  tot_meta_sz += meta_sz;
	  tot_data_sz += data_sz;
	  metric_count++;
	}
      }
    }
  }
  if (fpipe) pclose(fpipe);

  /* Create the metric set */
  rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
  if (rc){
    pthread_mutex_unlock(&cfg_lock);
    return rc;
  }

  metric_table = calloc(metric_count, sizeof(ldms_metric_t));
  if (!metric_table){
    goto err;
  }

  /*
   * Process the file again to define all the metrics.
   */
  compid_metric_handle = ldms_add_metric(set, "component_id", LDMS_V_U64);
  if (!compid_metric_handle) {
    rc = ENOMEM;
    goto err;
  } //compid set in config
  cast_metric_handle = ldms_add_metric(set, "cast_from_native", LDMS_V_U64);
  if (!cast_metric_handle) {
    rc = ENOMEM;
    goto err;
  } //cast_from_native set in config

  counter_metric_handle = ldms_add_metric(set, "lmsensors_counter", LDMS_V_U64);
  if (!counter_metric_handle) {
    rc = ENOMEM;
    goto err;
  } //counter set in config

  int metric_no = 0;
  if (!(fpipe = (FILE*)popen(command,"r"))){
    perror("Problems with pipe");
    goto err;
  }

  while(fgets(buf, sizeof buf, fpipe)){
    //examples: 
    //FAN7/CPU4:   0 RPM  (min =  712 RPM)                   ALARM
    //FAN3/CPU2:12500 RPM  (min =  712 RPM) 
    //CPU1 Temp: +26.8°C  (high = +72.0°C, hyst = +67.0°C) 

    char* pch=strchr(buf,':');
    if (pch != NULL){
      if ((pch-buf+1) != strlen(buf)){
	char *endptr;
	double val;
	snprintf(metric_name,(pch-buf+1),"%s",buf);
	errno = 0;
	val = strtod((pch+1),&endptr);
	if ((errno == ERANGE) || endptr == (pch+1)){
	  //skip this one
	} else {
	  metric_table[metric_no] = ldms_add_metric(set, metric_name, LDMS_V_U64);
	  if (!metric_table[metric_no]) {
	    rc = ENOMEM;
	    goto err;
	  }

	  metric_no++;
	}
      }
    }
  }
  if (fpipe) pclose(fpipe);
  pthread_mutex_unlock(&cfg_lock);

  return 0;

 err:
  ldms_set_release(set);
  if (fpipe) pclose(fpipe);
  pthread_mutex_unlock(&cfg_lock);
  return rc;

}

static int sample(void)
{
  int metric_no;
  char buf[1024];

  FILE* fpipe;

  pthread_mutex_lock(&cfg_lock);

  if (!(fpipe = (FILE*)popen(command,"r"))){
    perror("Problems with pipe");
    pthread_mutex_unlock(&cfg_lock);
    return -1;
  }

  //09-04-2012 change counter to update with each setmetric
  //  union ldms_value v;
  //  v.v_u64 = ++counter;
  //  ldms_set_metric(counter_metric_handle, &v);

  metric_no = 0;
  if (1){
  while(fgets(buf, sizeof buf, fpipe)){
    //examples: 
    //FAN7/CPU4:   0 RPM  (min =  712 RPM)                   ALARM
    //FAN3/CPU2:12500 RPM  (min =  712 RPM) 
    //CPU1 Temp: +26.8°C  (high = +72.0°C, hyst = +67.0°C) 

    char* pch=strchr(buf,':');
    if (pch != NULL){
      if ((pch-buf+1) != strlen(buf)){
	char *endptr;
	double val;
	errno = 0;
	val = strtod((pch+1),&endptr);
	if ((errno == ERANGE) || endptr == (pch+1)){
	  //skip this one
	} else {
	  uint64_t *temp = (uint64_t*) &val;
	  union ldms_value v;
	  v.v_u64 = *temp;

//          uint64_t temp = (uint64_t)val;
//	  union ldms_value v;
//	  v.v_u64 = temp;

	  ldms_set_metric(metric_table[metric_no], &v);

	  //09-04-2012 moved to increment with each setmetric
	  union ldms_value vc;
	  vc.v_u64 = ++counter;
	  ldms_set_metric(counter_metric_handle, &vc);

	  metric_no++;
	}
      }
    }
  }
  }


  if (fpipe) pclose(fpipe);
  pthread_mutex_unlock(&cfg_lock);

  return 0;
}


static void term(void)
{
	ldms_destroy_set(set);
}

static const char *usage(void)
{
	return  "    config lmsensors component_id <comp_id>\n"
		"        - Set the component_id value in the metric set.\n"
		"        comp_id     The component id value\n";
}

static struct ldms_plugin lmsensors_plugin = {
	.name = "lmsensors",
	.init = init,
	.term = term,
	.config = config,
	.get_set = get_set,
	.sample = sample,
	.usage = usage,
};

struct ldms_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &lmsensors_plugin;
}
