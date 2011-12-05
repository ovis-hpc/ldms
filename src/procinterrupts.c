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
 * This is the /proc/interrupts data provider. 
 * NOTE: Assumes 16 processors (FIXME)
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

#define PROC_FILE "/proc/interrupts"
static char *procfile = PROC_FILE;

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
int num_numlines;

static int config(char *str)
{
	return EINVAL;
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
		msglog("Could not open the interrupts file '%s'...exiting\n", procfile);
		return ENOENT;
	}

	char beg_name[128];
	char end_name[128];
	num_numlines = 0; //first num_numlines (after the header) are all cpu0 only metrics

	/*
	 * Process the file once first to determine the metric set size.
	 */
	metric_count = 0;
	rc = ldms_get_metric_size("component_id", LDMS_V_U64, &tot_meta_sz, &tot_data_sz);
	metric_count++;
	fseek(mf, 0, SEEK_SET);
	//first line is the cpu list
	s = fgets(lbuf, sizeof(lbuf), mf);
	while(s) {
	  s = fgets(lbuf, sizeof(lbuf), mf);
	  if (!s)
	    break;
	  char* pch;
	  int firsttime = 1;
	  int foundmetric = 0;
	  long int l1 = 0;
	  pch = strtok (lbuf," ");
	  while (pch != NULL){
	    //	    printf("<%s>\n",pch);
	    if (firsttime){
	      /* Strip the colon from metric name if present */
	      i = strlen(pch);
	      if (i && pch[i-1] == ':')
		pch[i-1] = '\0';
	      strcpy(beg_name, pch);
		    
	      char *endptr;
	      l1 = strtol (pch, &endptr,10);
	      if (endptr == pch){
		//if the metric name is not a number, the metric name will be CpuX_name (there will be 16)
		for (i = 0; i < 16; i++){
		  snprintf(metric_name,128,"Cpu%d_%s",i,beg_name);
		  rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
		  tot_meta_sz += meta_sz;
		  tot_data_sz += data_sz;
		  metric_count++;
		}
		foundmetric = 1;
		break;
	      }
	      firsttime = 0;
	    } else {
	      //if the metric name is a number, the metric name will be Cpu0_lastcol_num (there will be only Cpu0)
	      strcpy(end_name, pch);
	    }
	    pch = strtok(NULL, " ");
	  } // while (strtok)
	  if (!foundmetric){
	    //chomp
	    i = strlen(end_name);
	    if (i && end_name[i-1] == '\n')
		end_name[i-1] = '\0';
	    snprintf(metric_name,128,"Cpu0_%s_%ld",end_name,l1);
	    rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
	    tot_meta_sz += meta_sz;
	    tot_data_sz += data_sz;
	    metric_count++;
	    num_numlines++;
	  }
	} //while

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
	int metric_no = 0;
	//FIXME:  how is the component_id going to get set???
	metric_table[metric_no] = ldms_add_metric(set, "component_id", LDMS_V_U64);
	if (!metric_table[metric_no]) {
	  rc = ENOMEM;
	  goto err;
	}
	metric_no++;

	fseek(mf, 0, SEEK_SET);
	//first line is the cpu list
	s = fgets(lbuf, sizeof(lbuf), mf);
	while(s){
	  s = fgets(lbuf, sizeof(lbuf), mf);
	  if (!s)
	    break;
	  char* pch;
	  int firsttime = 1;
	  int foundmetric = 0;
	  long int l1 = 0;
	  pch = strtok (lbuf," ");
	  while (pch != NULL){
	    if (firsttime){
	      /* Strip the colon from metric name if present */
	      i = strlen(pch);
	      if (i && pch[i-1] == ':')
		pch[i-1] = '\0';
	      strcpy(beg_name, pch);
		    
	      char *endptr;
	      l1 = strtol (pch, &endptr,10);
	      if (endptr == pch){
		//if the metric name is not a number, the metric name will be CpuX_name (there will be 16)
		for (i = 0; i < 16; i++){
		  snprintf(metric_name,128,"Cpu%d_%s",i,beg_name);
		  metric_table[metric_no] = ldms_add_metric(set, metric_name, LDMS_V_U64);
		  if (!metric_table[metric_no]){
		    rc = ENOMEM;
		    goto err;
		  }
		  metric_no++;
		}
		foundmetric = 1;
		break;
	      }
	      firsttime = 0;
	    } else {
	      //if the metric name is a number, the metric name will be Cpu0_lastcol_num (there will be only Cpu0)
	      strcpy(end_name, pch);
	    }
	    pch = strtok(NULL, " ");
	  } //while (strtok)
	  if (!foundmetric){
	    //chomp
	    i = strlen(end_name);
	    if (i && end_name[i-1] == '\n')
		end_name[i-1] = '\0';
	    snprintf(metric_name,128,"Cpu0_%s_%ld",end_name,l1);
	    metric_table[metric_no] = ldms_add_metric(set, metric_name, LDMS_V_U64);
	    if (!metric_table[metric_no]){
	      rc = ENOMEM;
	      goto err;
	    }
	    metric_no++;
	  }
	} //while
	return 0;

	/* FIXME: how will we set the comp id???
	{//fill in the comp id
	  uint64_t gn;
	  metric_no = 0;
	  gn = un_set_u64(set_no, metric_no, (unsigned long long) ovis_compid);
	  if ((int64_t)gn == -1L)
	    no_server();
	}
	*/

 err:
	ldms_set_release(set);
	return rc;
}

static int sample(void)
{
  int rc;
  int metric_no;
  char *s;
  char lbuf[256];
  char metric_name[128];
  char junk[128];
  union ldms_value v;
  int count = 0;

  metric_no = 1; // 0 is the component_id FIXME: is this still true?
  fseek(mf, 0, SEEK_SET);
  //first line is the cpu list
  s = fgets(lbuf, sizeof(lbuf), mf);
  if (!s)
    return 0;
  while(s){
    s = fgets(lbuf, sizeof(lbuf), mf);
    if (!s)
      break;
    
    if (count < num_numlines){
      //then we only need the first col
      rc = sscanf(lbuf, "%s %"PRIu64 " %s\n", metric_name, &v.v_u64, junk);
      if (rc != 3)
	return EINVAL;
      
      ldms_set_metric(metric_table[metric_no], &v);
      metric_no++;
      count++;
    } else {
      //we need 16 vals
      union ldms_value mv[16];
      int i;

      rc = sscanf(lbuf, "%s %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %"PRIu64 " %s\n", metric_name, &mv[0].v_u64, &mv[1].v_u64, &mv[2].v_u64, &mv[3].v_u64, &mv[4].v_u64, &mv[5].v_u64, &mv[6].v_u64, &mv[7].v_u64, &mv[8].v_u64, &mv[9].v_u64, &mv[10].v_u64, &mv[11].v_u64, &mv[12].v_u64, &mv[13].v_u64, &mv[14].v_u64, &mv[15].v_u64, junk);
      for (i = 0; i < 16; i++){
	ldms_set_metric(metric_table[metric_no], &v);
	metric_no++;
      }
      count++;
    }
  } while (s);
  return 0;
}


static void term(void)
{
	ldms_set_release(set);
}


static struct ldms_plugin procinterrupts_plugin = {
	.name = "procinterrupts",
	.init = init,
	.term = term,
	.config = config,
	.get_set = get_set,
	.sample = sample,
};

struct ldms_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &procinterrupts_plugin;
}
