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
 * \file sysclassib.c
 * \brief reads from 
 * 1) all files in: /sys/class/infiniband/mlx4_{0/1}/ports/{1/2}/counters
 *    which have well-known names
 * 2) /sys/class/infiniband/mlx4_{0/1}/ports/{1,2}/rate
 *
 * in config, you can specify ib0 --> mlx4_0 and port1 
 *                            ib1 --> mlx4_0 and port2
 *                            ib2 --> mlx4_1 and port1 
 *                            ib3 --> mlx4_1 and port2 
 *
 * for older kernels, the filehandles have to be opened and closed each time;
 * for newer kernels (>= 2.6.35) they do not.
 *
 * FIXME: verify that if you unload & load sampler that filehandles
 * close and reopen properly
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include "ldms.h"
#include "ldmsd.h"

//FIXME: make this a parameter later..
static char* ibbasedir = "/sys/class/infiniband/mlx4_";
const static char* countervarnames[] = {"excessive_buffer_overrun_errors",
					"link_downed", 
					"link_error_recovery",
					"local_link_integrity_errors",
					"port_rcv_constraint_errors",
					"port_rcv_data",
					"port_rcv_errors",
					"port_rcv_packets",
					"port_rcv_remote_physical_errors",
					"port_rcv_switch_relay_errors",
					"port_xmit_constraint_errors",
					"port_xmit_data",
					"port_xmit_discards",
					"port_xmit_packets",
					"port_xmit_wait",
					"symbol_error",
					"VL15_dropped"};
const static int numcountervar = 17;
//NOTE: known that the other file is "rate" and the variable will be "rate"
//max number of interfaces we can include. FIXME: alloc as added
#define MAXIFACE 4
static int useiface[MAXIFACE];
static FILE* fd[4][18];
static char filename[4][18][256]; //filenames for the filehandles. now need to store these since have to close and open filehandles all the time...

static uint64_t counter;

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
ldms_metric_t compid_metric_handle;
ldms_metric_t counter_metric_handle;
union ldms_value comp_id;

#define V1 2
#define V2 6
#define V3 34

int newerkernel;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

struct kw {
  char *token;
  int (*action)(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg);
};

static int kw_comparator(const void *a, const void *b)
{
  struct kw *_a = (struct kw *)a;
  struct kw *_b = (struct kw *)b;
  return strcmp(_a->token, _b->token);
}

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i, j, metric_count;
	char metric_name[128];

	rc = ldms_get_metric_size("component_id", LDMS_V_U64,
				  &tot_meta_sz, &tot_data_sz);

	//and add the counter
	rc = ldms_get_metric_size("counter", LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz += meta_sz;
	tot_data_sz += data_sz;


	metric_count = 0;
	for (j = 0; j < MAXIFACE; j++){
	  if (useiface[j] != 1){
	    continue;
	  }
	  for (i = 0; i < numcountervar; i++){
	    //the metric name will be iface:name
	    snprintf(metric_name, 127, "ib%d_%s",j,countervarnames[i]);
	    rc = ldms_get_metric_size(metric_name,
				      LDMS_V_U64, &meta_sz, &data_sz);
	    if (rc)
	      return rc;
	  
	    tot_meta_sz += meta_sz;
	    tot_data_sz += data_sz;
	    metric_count++;
	  }
	  snprintf(metric_name, 127, "ib%d_rate",j);
	  rc = ldms_get_metric_size(metric_name,
				    LDMS_V_U64, &meta_sz, &data_sz);
	  if (rc)
	    return rc;
	  tot_meta_sz += meta_sz;
	  tot_data_sz += data_sz;
	  metric_count++;
	}

	/* Create the metric set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
	  return rc;
	  
	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
	  goto err;

	/*
	 * Process again to define all the metrics.
	 */
	compid_metric_handle = ldms_add_metric(set, "component_id", LDMS_V_U64);
	if (!compid_metric_handle) {
	  rc = ENOMEM;
	  goto err;
	} //compid set in sample

	  //and add the counter
	counter_metric_handle = ldms_add_metric(set, "counter", LDMS_V_U64);
	if (!counter_metric_handle) {
	  rc = ENOMEM;
	  goto err;
	} //counter updated in sample

	//add the metrics and open all the filehandles
	int metric_no = 0;
	for (j = 0; j < MAXIFACE; j++){
	  if (useiface[j] != 1){
	    continue;
	  }
	  for (i = 0; i < numcountervar; i++){
	    snprintf(metric_name, 127, "ib%d_%s",j,countervarnames[i]);
	    metric_table[metric_no] =
	      ldms_add_metric(set, metric_name, LDMS_V_U64);
	    if (!metric_table[metric_no]) {
	      rc = ENOMEM;
	      goto err;
	    }
	    snprintf(metric_name, 255, "%s%d/ports/%d/%s/%s", ibbasedir, ((j == 0 || j == 1)? 0:1), ((j == 0 || j == 2) ? 1:2), "counters",
		     countervarnames[metric_no]);
	    fd[j][i] = fopen(metric_name, "r");
	    if (!fd[j][i]){
	      msglog("Could not open the sysclassib file '%s' ...exiting\n",
		     metric_name);
	      return ENOENT;
	    }
	    snprintf(filename[j][i],255,"%s", metric_name); //now have to keep track of these...
	    metric_no++;
	  }

	  snprintf(metric_name, 127, "ib%d_rate",j);
	  metric_table[metric_no] =
	    ldms_add_metric(set, metric_name, LDMS_V_U64);
	  if (!metric_table[metric_no]) {
	    rc = ENOMEM;
	    goto err;
	  }
	  snprintf(metric_name, 255, "%s%d/ports/%d/rate", ibbasedir, ((j == 0 || j == 1)? 0:1), ((j == 0 || j == 2) ? 1:2));
	  //last one is the rate
	  fd[j][numcountervar] = fopen(metric_name, "r");
	  if (!fd[j][numcountervar]){
	    msglog("Could not open the sysclassib file '%s' ...exiting\n",
		   metric_name);
	    return ENOENT;
	  }
	  snprintf(filename[j][numcountervar],255,"%s", metric_name); //now have to keep track of these...
	  metric_no++;
	}
	return 0;

 err:
	for (j = 0; j < MAXIFACE; j++){
	  for (i = 0; i <= numcountervar; i++){ //get the rate as well
	    if (fd[j][i] != NULL){
	      fclose(fd[j][i]);
	    }
	    fd[j][i] = 0;
	  }
	}
	ldms_set_release(set);
	return rc;
}


static int add_iface(struct attr_value_list *kwl, struct attr_value_list *avl,
		     void* arg){

  char *value;
  
  value = av_value(avl, "iface");
  if (value){
    if (strcmp(value, "ib0") == 0){
      useiface[0] = 1;
    } else if (strcmp(value, "ib1") == 0){
      useiface[1] = 1;
    } else if (strcmp(value, "ib2") == 0){
      useiface[2] = 1;
    } else if (strcmp(value, "ib3") == 0){
      useiface[3] = 1;
    } else {
      msglog("Invalid interface %s\n", value);
      return EINVAL;
    }
  } else {
    msglog("Invalid interface %s\n", value);
    return EINVAL;
  }

  return 0;
  
}


static int init(struct attr_value_list *kwl, struct attr_value_list *avl, void* arg){
  /* Set the compid and create the metric set. check kernel version */

  char *value;
  
  value = av_value(avl, "component_id");
  if (value)
    comp_id.v_u64 = strtol(value, NULL, 0);
  
  value = av_value(avl, "set");
  if (!value)
    return EINVAL;

  FILE* fp;
  char line[256];
  newerkernel = 0;
  if ((fp = (FILE*)popen("uname -r","r"))){
    if (fgets(line, sizeof line, fp)){
      int version[3];
      char junk[128];
      sscanf(line,"%d.%d.%d-%s",
	     &version[0], &version[1], &version[2],
	     junk);
      if ((version[0] >= V1) && (version[1] >= V2) && (version[2] > V3)){
	newerkernel = 1;
      }
    }
  }
  if (fp) pclose(fp);

  return create_metric_set(value);
}

struct kw kw_tbl[] = {
  { "add", add_iface },
  { "init", init },
};

static const char *usage(void)
{
  return
    "config name=procnetdev action=add iface=<iface>\n"
    "    iface       Interface name (e.g., ib0)\n"
    "config name=procnetdev action=init component_id=<comp_id> set=<setname>\n"
    "    comp_id     The component id value.\n"
    "    setname     The set name.\n";
}

/**
 * \brief Configuration
 *
 * - config procnetdev action=add iface=eth0
 *  (repeat this for each iface)
 * - config procnetdev action=init component_id=<value> set=<setname>
 *  (init must be after all the ifaces are added since it adds the metric set)
 *  
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{

  struct kw *kw;
  struct kw key;
  int rc;
  char *action = av_value(avl, "action");
  
  if (!action)
    goto err0;

  key.token = action;
  kw = bsearch(&key, kw_tbl, ARRAY_SIZE(kw_tbl),
	       sizeof(*kw), kw_comparator);
  if (!kw)
    goto err1;


  rc = kw->action(kwl, avl, NULL);
  if (rc)
    goto err2;
  return 0;
 err0:
  msglog(usage());
  goto err2;
 err1:
  msglog("Invalid configuration keyword '%s'\n", action);
 err2:
  return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
  int rc;
  char *s;
  char lbuf[20];
  union ldms_value v;
  int i,j;

  if (!set){
    msglog("sysclassib: plugin not initialized\n");
    return EINVAL;
  }

  //set the counter
  v.v_u64 = ++counter;
  ldms_set_metric(counter_metric_handle, &v);
  
  //set the compid
  ldms_set_metric(compid_metric_handle, &comp_id);
  
  int metricno = 0;
  for (j = 0; j < MAXIFACE; j++){
    if (useiface[j] != 1){
      continue;
    }
    for (i = 0; i <= numcountervar; i++){ //get the rate as well
      if (newerkernel){
	fseek(fd[j][i],0,SEEK_SET);
      } else {
	if (fd[j][i]) fclose(fd[j][i]);
	fd[j][i] = fopen(filename[j][i], "r");
	if (!fd[j][i]){
	  msglog("Could not open the sysclassib file '%s' ...exiting\n",
		 filename[j][i]);
	  return ENOENT;
	}
      }
      
      s = fgets(lbuf, sizeof(lbuf), fd[j][i]);
      if (!s){
	break;
      }
      rc = sscanf(lbuf, "%"PRIu64 "\n", &v.v_u64);
      if (rc != 1){
	return EINVAL;
      }
      ldms_set_metric(metric_table[metricno++], &v);

      if (!newerkernel){
	if (fd[j][i]) fclose(fd[j][i]);
	fd[j][i] = 0;
      }
    }
  }

  return 0;

}

static void term(void){
  
  int i, j;

  for (j = 0; j < MAXIFACE; j++){
    for (i = 0; i <= numcountervar; i++){ //get the rate as well
      if (fd[j][i] != NULL){
	fclose(fd[j][i]);
      }
      fd[j][i] = 0;
    }
  }

  if (set)
    ldms_destroy_set(set);
  set = NULL;
}

static struct ldmsd_sampler sysclassib_plugin = {
	.base = {
		.name = "sysclassib",
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
	return &sysclassib_plugin.base;
}
