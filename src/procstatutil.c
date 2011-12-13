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
/*
 * This is the /proc/stat/util data provider
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

#define PROC_FILE "/proc/stat"

static char *procfile = PROC_FILE;

struct raw{
  unsigned long long user;
  unsigned long long sys;
  unsigned long long idle;
  unsigned long long uptime;
};

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
struct raw* prev;
int numcpu_plusone = 0; // including one for the node
int maxcpu_plusone = 5; // will increase
ldms_metric_t *compid_metric_handle;

static int config(char *str)
{
  if (!set || !compid_metric_handle ){
    msglog("meminfo: plugin not initialized\n");
    return EINVAL;
  }
  //expects "component_id value"                                                                                  
  if (0 == strncmp(str,"component_id",12)){
    char junk[128];
    int rc;
    union ldms_value v;

    rc = sscanf(str,"component_id %" PRIu64 "%s\n",&v.v_u64,junk);
    if (rc < 1){
      return EINVAL;
    }
    ldms_set_metric(compid_metric_handle, &v);
  }

  return 1;

}

static ldms_set_t get_set()
{
  return set;
}

static int init(const char *path)
{
  size_t meta_sz, tot_meta_sz;
  size_t data_sz, tot_data_sz;
  int rc, i, metric_count;
  char *s;
  char lbuf[256];
  char metric_name[128];

  mf = fopen(procfile, "r");
  if (!mf) {
    msglog("Could not open the procstatutil file '%s'...exiting\n", procfile);
    return ENOENT;
  }

  prev = (struct raw*) malloc (maxcpu_plusone * sizeof (struct raw));
  if (!prev) {
    return ENOMEM;
  };

  /* Process the file once first to determine the metric set size
   * and store the info since this does a diff calculation. (Decide If we want to keep the diff).
   */
  rc = ldms_get_metric_size("component_id", LDMS_V_U64, &tot_meta_sz, &tot_data_sz);
  metric_count = 0; //only for the data metrics

  fseek(mf, 0, SEEK_SET);
  do {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long sys;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long hardirq;
    unsigned long long softirq;
    unsigned long long steal;
    unsigned long long guest;
    int icpu;

    s = fgets(lbuf, sizeof(lbuf), mf);
    if (!s)
      break;

    if (!strncmp( lbuf, "cpu ", 4) ){
      /* FIXME: should test for 10 cols */
      sscanf(lbuf + 5, "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
	     &user, &nice, &sys, &idle, &iowait, &hardirq, &softirq, &steal, &guest);
      rc = ldms_get_metric_size("cpu_user", LDMS_V_U64, &meta_sz, &data_sz);
      tot_meta_sz +=meta_sz;
      tot_data_sz +=data_sz;
      metric_count++;

      rc = ldms_get_metric_size("cpu_sys",  LDMS_V_U64, &meta_sz, &data_sz);
      tot_meta_sz +=meta_sz;
      tot_data_sz +=data_sz;
      metric_count++;

      rc = ldms_get_metric_size("cpu_idle",  LDMS_V_U64, &meta_sz, &data_sz);
      tot_meta_sz +=meta_sz;
      tot_data_sz +=data_sz;
      metric_count++;

      rc = ldms_get_metric_size("cpu_nonidle",  LDMS_V_U64, &meta_sz, &data_sz);
      tot_meta_sz +=meta_sz;
      tot_data_sz +=data_sz;
      metric_count++;

      rc = ldms_get_metric_size("cpu_user_raw", LDMS_V_U64, &meta_sz, &data_sz);
      tot_meta_sz +=meta_sz;
      tot_data_sz +=data_sz;
      metric_count++;

      rc = ldms_get_metric_size("cpu_nice_raw", LDMS_V_U64, &meta_sz, &data_sz);
      tot_meta_sz +=meta_sz;
      tot_data_sz +=data_sz;
      metric_count++;

      rc = ldms_get_metric_size("cpu_sys_raw",  LDMS_V_U64, &meta_sz, &data_sz);
      tot_meta_sz +=meta_sz;
      tot_data_sz +=data_sz;
      metric_count++;

      rc = ldms_get_metric_size("cpu_idle_raw", LDMS_V_U64, &meta_sz, &data_sz);
      tot_meta_sz +=meta_sz;
      tot_data_sz +=data_sz;
      metric_count++;

      rc = ldms_get_metric_size("cpu_iowait_raw",  LDMS_V_U64, &meta_sz, &data_sz);
      tot_meta_sz +=meta_sz;
      tot_data_sz +=data_sz;
      metric_count++;

      rc = ldms_get_metric_size("cpu_irq_raw", LDMS_V_U64, &meta_sz, &data_sz);
      tot_meta_sz +=meta_sz;
      tot_data_sz +=data_sz;
      metric_count++;

      rc = ldms_get_metric_size("cpu_softirq_raw", LDMS_V_U64, &meta_sz, &data_sz);
      tot_meta_sz +=meta_sz;
      tot_data_sz +=data_sz;
      metric_count++;
      numcpu_plusone++;
    } else {
      if (!strncmp( lbuf, "cpu", 3) ){
	sscanf(lbuf + 3, "%d %llu %llu %llu %llu %llu %llu %llu %llu %llu",
	       &icpu, &user, &nice, &sys, &idle, &iowait, &hardirq, &softirq, &steal, &guest);
	snprintf(metric_name, 127,"cpu%d_user", icpu);
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz +=meta_sz;
	tot_data_sz +=data_sz;
	metric_count++;

	snprintf(metric_name, 127,"cpu%d_sys", icpu);
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz +=meta_sz;
	tot_data_sz +=data_sz;
	metric_count++;

	snprintf(metric_name, 127,"cpu%d_idle", icpu);
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz +=meta_sz;
	tot_data_sz +=data_sz;
	metric_count++;

	snprintf(metric_name, 127,"cpu%d_nonidle", icpu);
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz +=meta_sz;
	tot_data_sz +=data_sz;
	metric_count++;

	snprintf(metric_name, 127,"cpu%d_user_raw", icpu);
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz +=meta_sz;
	tot_data_sz +=data_sz;
	metric_count++;

	snprintf(metric_name, 127,"cpu%d_nice_raw", icpu);
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz +=meta_sz;
	tot_data_sz +=data_sz;
	metric_count++;

	snprintf(metric_name, 127,"cpu%d_sys_raw", icpu);
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz +=meta_sz;
	tot_data_sz +=data_sz;
	metric_count++;

	snprintf(metric_name, 127,"cpu%d_idle_raw", icpu);
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz +=meta_sz;
	tot_data_sz +=data_sz;
	metric_count++;

	snprintf(metric_name, 127,"cpu%d_iowait_raw", icpu);
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz +=meta_sz;
	tot_data_sz +=data_sz;
	metric_count++;

	snprintf(metric_name, 127,"cpu%d_irq_raw", icpu);
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz +=meta_sz;
	tot_data_sz +=data_sz;
	metric_count++;

	snprintf(metric_name, 127,"cpu%d_softirq_raw", icpu);
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz +=meta_sz;
	tot_data_sz +=data_sz;
	metric_count++;
	numcpu_plusone++;
      } else {
	continue;
      }
    }
		
    if (numcpu_plusone >= maxcpu_plusone){
      maxcpu_plusone*=2;
      prev = realloc(prev, maxcpu_plusone*sizeof(struct raw));
      if (!prev){
	return ENOMEM;
      }
    }

    prev[numcpu_plusone-1].user = user;
    prev[numcpu_plusone-1].sys = sys;
    prev[numcpu_plusone-1].idle = idle;
    //no guest
    prev[numcpu_plusone-1].uptime = user + nice + sys + idle + iowait + hardirq + steal + softirq; 
		
  } while (s);
  
  //shrink the array back
  prev = realloc(prev, numcpu_plusone*sizeof(struct raw));
  if (!prev){
    return ENOMEM;
  }

  /* Create a metric set of the required size */
  rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
  if (rc){
    free(prev);
    return rc;
  }

  metric_table = calloc(metric_count, sizeof(ldms_metric_t));
  if (!metric_table)
    goto err;


  /*
   * Define all the metrics given the numcpu
   */
  compid_metric_handle = ldms_add_metric(set, "component_id", LDMS_V_U64);
  if (!compid_metric_handle) {
    rc = ENOMEM;
    goto err;
  }
  //compid's value will be set in config

  int metric_no = 0;
  metric_table[metric_no] = ldms_add_metric(set, "cpu_user", LDMS_V_U64);
  if (!metric_table[metric_no]) {
    rc = ENOMEM;
    goto err;
  }
  metric_no++;

  metric_table[metric_no] = ldms_add_metric(set, "cpu_sys", LDMS_V_U64);
  if (!metric_table[metric_no]) {
    rc = ENOMEM;
    goto err;
  }
  metric_no++;

  metric_table[metric_no] = ldms_add_metric(set, "cpu_idle", LDMS_V_U64);
  if (!metric_table[metric_no]) {
    rc = ENOMEM;
    goto err;
  }
  metric_no++;

  metric_table[metric_no] = ldms_add_metric(set, "cpu_nonidle", LDMS_V_U64);
  if (!metric_table[metric_no]) {
    rc = ENOMEM;
    goto err;
  }
  metric_no++;

  metric_table[metric_no] = ldms_add_metric(set, "cpu_user_raw", LDMS_V_U64);
  if (!metric_table[metric_no]) {
    rc = ENOMEM;
    goto err;
  }
  metric_no++;

  metric_table[metric_no] = ldms_add_metric(set, "cpu_nice_raw", LDMS_V_U64);
  if (!metric_table[metric_no]) {
    rc = ENOMEM;
    goto err;
  }
  metric_no++;

  metric_table[metric_no] = ldms_add_metric(set, "cpu_sys_raw", LDMS_V_U64);
  if (!metric_table[metric_no]) {
    rc = ENOMEM;
    goto err;
  }
  metric_no++;

  metric_table[metric_no] = ldms_add_metric(set, "cpu_idle_raw", LDMS_V_U64);
  if (!metric_table[metric_no]) {
    rc = ENOMEM;
    goto err;
  }
  metric_no++;

  metric_table[metric_no] = ldms_add_metric(set, "cpu_iowait_raw", LDMS_V_U64);
  if (!metric_table[metric_no]) {
    rc = ENOMEM;
    goto err;
  }
  metric_no++;

  metric_table[metric_no] = ldms_add_metric(set, "cpu_irq_raw", LDMS_V_U64);
  if (!metric_table[metric_no]) {
    rc = ENOMEM;
    goto err;
  }
  metric_no++;

  metric_table[metric_no] = ldms_add_metric(set, "cpu_softirq_raw", LDMS_V_U64);
  if (!metric_table[metric_no]) {
    rc = ENOMEM;
    goto err;
  }
  metric_no++;

  for (i = 0; i < (numcpu_plusone-1); i++){
    snprintf(metric_name, 127,"cpu%d_%s",i,"user");
    metric_table[metric_no] = ldms_add_metric(set, metric_name,  LDMS_V_U64);
    if (!metric_table[metric_no]) {
      rc = ENOMEM;
      goto err;
    }

    snprintf(metric_name, 127,"cpu%d_%s",i,"sys");
    metric_table[metric_no] = ldms_add_metric(set, metric_name,  LDMS_V_U64);
    if (!metric_table[metric_no]) {
      rc = ENOMEM;
      goto err;
    }

    snprintf(metric_name, 127,"cpu%d_%s",i,"idle");
    metric_table[metric_no] = ldms_add_metric(set, metric_name,  LDMS_V_U64);
    if (!metric_table[metric_no]) {
      rc = ENOMEM;
      goto err;
    }

    snprintf(metric_name, 127,"cpu%d_%s",i,"nonidle");
    metric_table[metric_no] = ldms_add_metric(set, metric_name,  LDMS_V_U64);
    if (!metric_table[metric_no]) {
      rc = ENOMEM;
      goto err;
    }

    snprintf(metric_name, 127,"cpu%d_%s",i,"user_raw");
    metric_table[metric_no] = ldms_add_metric(set, metric_name,  LDMS_V_U64);
    if (!metric_table[metric_no]) {
      rc = ENOMEM;
      goto err;
    }

    snprintf(metric_name, 127,"cpu%d_%s",i,"nice_raw");
    metric_table[metric_no] = ldms_add_metric(set, metric_name,  LDMS_V_U64);
    if (!metric_table[metric_no]) {
      rc = ENOMEM;
      goto err;
    }

    snprintf(metric_name, 127,"cpu%d_%s",i,"sys_raw");
    metric_table[metric_no] = ldms_add_metric(set, metric_name,  LDMS_V_U64);
    if (!metric_table[metric_no]) {
      rc = ENOMEM;
      goto err;
    }

    snprintf(metric_name, 127,"cpu%d_%s",i,"idle_raw");
    metric_table[metric_no] = ldms_add_metric(set, metric_name,  LDMS_V_U64);
    if (!metric_table[metric_no]) {
      rc = ENOMEM;
      goto err;
    }

    snprintf(metric_name, 127,"cpu%d_%s",i,"iowait_raw");
    metric_table[metric_no] = ldms_add_metric(set, metric_name,  LDMS_V_U64);
    if (!metric_table[metric_no]) {
      rc = ENOMEM;
      goto err;
    }

    snprintf(metric_name, 127,"cpu%d_%s",i,"irq_raw");
    metric_table[metric_no] = ldms_add_metric(set, metric_name,  LDMS_V_U64);
    if (!metric_table[metric_no]) {
      rc = ENOMEM;
      goto err;
    }

    snprintf(metric_name, 127,"cpu%d_%s",i,"softirq_raw");
    metric_table[metric_no] = ldms_add_metric(set, metric_name,  LDMS_V_U64);
    if (!metric_table[metric_no]) {
      rc = ENOMEM;
      goto err;
    }
  } //for

  return 0;

 err:
  free(prev);
  ldms_set_release(set);

  return rc ;
}


static int sample(void)
{
  int metric_no;
  char *s;
  char lbuf[256];

  metric_no = 0;
  fseek(mf, 0, SEEK_SET);
  do {
    s = fgets(lbuf, sizeof(lbuf), mf);
    if (!s)
      break;

    unsigned long long user;
    unsigned long long nice;
    unsigned long long sys;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long hardirq;
    unsigned long long softirq;
    unsigned long long steal;
    unsigned long long guest;
    unsigned long long uptime;
    uint64_t metric_value;
    union ldms_value v;
    int icpu;

    if (!strncmp( lbuf, "cpu ", 4) ){
      sscanf(lbuf + 5, "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
	     &user, &nice, &sys, &idle, &iowait, &hardirq, &softirq, &steal, &guest);
      icpu = 0;
    } else {
      if (!strncmp( lbuf, "cpu", 3) ){
	sscanf(lbuf + 3, "%d %llu %llu %llu %llu %llu %llu %llu %llu %llu",
	       &icpu, &user, &nice, &sys, &idle, &iowait, &hardirq, &softirq, &steal, &guest);
	icpu++;
      } else {
	continue;
      }
    }
		
    if ( icpu > maxcpu_plusone ){
      printf("Exceeded max cpu\n");
      return EINVAL;
    }
    
    uptime = user + nice + sys + idle + iowait + hardirq + steal + softirq; 
    unsigned long long duptime = uptime - prev[icpu].uptime;
    if (duptime > 0){
      v.v_u64 = 100*(user - prev[icpu].user)/duptime;
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;

      v.v_u64 = 100*(sys - prev[icpu].sys)/duptime;
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;

      v.v_u64 = 100*(idle - prev[icpu].idle)/duptime;
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;

      metric_value = v.v_u64;
      v.v_u64 = 100-metric_value;
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;

      v.v_u64 = user;
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;

      v.v_u64 = nice;
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;

      v.v_u64 = sys;
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;

      v.v_u64 = idle;
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;

      v.v_u64 = iowait;
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;

      v.v_u64 = hardirq;
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;

      v.v_u64 = softirq;
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;
    }

    prev[icpu].user = user;
    prev[icpu].sys = sys;
    prev[icpu].idle = idle;
    prev[icpu].uptime = uptime;
			
  } while (s);
  return 0;
}

static void term(void)
{
  ldms_set_release(set);
}


static struct ldms_plugin procstatutil_plugin = {
  .name = "procstatutil",
  .init = init,
  .term = term,
  .config = config,
  .get_set = get_set,
  .sample = sample,
};

struct ldms_plugin *get_plugin(ldmsd_msg_log_f pf)
{
  msglog = pf;
  return &procstatutil_plugin;
}

