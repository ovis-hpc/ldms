/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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

#include <ctype.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <pthread.h>
#include <errno.h>
#include <sos/idx.h>
#include "ldms.h"
#include "ldmsd.h"


#define TV_SEC_COL    0
#define TV_USEC_COL    1
#define GROUP_COL    2
#define VALUE_COL    3

static idx_t metric_idx;
static char tmp_path[PATH_MAX];
static char *root_path;
static ldmsd_msg_log_f msglog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

#define LOGFILE "/var/log/store_flatfile.log"

struct flatfile_metric_store {
  struct ldmsd_store *store;
  FILE *file;
  char* path;
  char *metric_key;
  void *ucontext; 
  pthread_mutex_t lock;
};

pthread_mutex_t cfg_lock;

/** 
 * \brief Configuration
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{

  char *value;
  value = av_value(avl, "path");
  if (!value)
    goto err;

  pthread_mutex_lock(&cfg_lock);
  if (root_path)
    free(root_path);
  root_path = strdup(value);
  pthread_mutex_unlock(&cfg_lock);
  if (!root_path)
    return ENOMEM;
  return 0;

 err:
  return EINVAL;

}

static void term(void)
{
  //should this close the filehandles as well?
}

static const char *usage(void)
{
  return  "    config name=store_flatfile path=<path>\n"
    "              - Set the root path for the storage of flatfiles.\n"
    "              path      The path to the root of the flatfile directory\n";
}


static ldmsd_metric_store_t
get_store(struct ldmsd_store *s, const char *comp_name, const char *metric_name)
{
  pthread_mutex_lock(&cfg_lock);

  char metric_key[128];
  ldmsd_metric_store_t ms;

  /* comment in sos says:  Add a component type directory if one does not
   * already exist
   * but it does not look like this function actually does an add...
   */
  sprintf(metric_key, "%s:%s", comp_name, metric_name);
  ms = idx_find(metric_idx, metric_key, strlen(metric_key));
  pthread_mutex_unlock(&cfg_lock);
  return ms;
}

static void *get_ucontext(ldmsd_metric_store_t _ms)
{
  struct flatfile_metric_store *ms = _ms;
  return ms->ucontext;
}

static ldmsd_metric_store_t
new_store(struct ldmsd_store *s, const char *comp_name, const char *metric_name,
	  void *ucontext)
{

  pthread_mutex_lock(&cfg_lock);

  char metric_key[128];
  struct flatfile_metric_store *ms;

  sprintf(metric_key, "%s:%s", comp_name, metric_name);
  ms = idx_find(metric_idx, metric_key, strlen(metric_key));
  if (!ms) {
    //append or create
    sprintf(tmp_path, "%s/%s", root_path, comp_name);
    mkdir(tmp_path, 0777);

    ms = calloc(1, sizeof *ms);
    if (!ms)
      goto out;
    ms->ucontext = ucontext;
    ms->store = s;
    pthread_mutex_init(&ms->lock, NULL);

    sprintf(tmp_path, "%s/%s/%s", root_path, comp_name, metric_name);
    ms->path = strdup(tmp_path);
    if (!ms->path)
      goto err1;
    ms->metric_key = strdup(metric_key);
    if (!ms->metric_key)
      goto err2;

    ms->file = fopen(ms->path, "a+");
    if (ms->file)
      idx_add(metric_idx, metric_key, strlen(metric_key), ms);
    else
      goto err3;
  }
  goto out;

 err3:
	free(ms->metric_key);
 err2:
	free(ms->path);
 err1:
	free(ms);
 out:
  pthread_mutex_unlock(&cfg_lock);
  return ms;
}

static int
store(ldmsd_metric_store_t _ms, uint32_t comp_id,
      struct timeval tv, ldms_metric_t m)
{
  //NOTE: later change this so data is queued up here and later bulk insert in the flush
  //NOTE: ldmsd_store invokes the lock on this ms, so we dont have to do it here

  struct flatfile_metric_store *ms;

  if (!_ms){
    return EINVAL;
  }

  ms = _ms;
  if (ms->file == NULL){
    msglog("Cannot insert value for <%s>: file is closed\n", ms->path);
    return EPERM;
  }

  //  char data_str[64];
  //  sprintf(data_str, 
  //	  "%"PRIu64".%"PRIu64" %"PRIu32" %"PRIu64"\n",
  //	  (uint64_t)(tv.tv_sec), (uint64_t)(tv.tv_usec),
  //	  comp_id, ldms_get_u64(m));
  // msglog(data_str); //this logs the sample to the log file 
  //  int rc = fprintf(ms->file, data_str);
  int rc = fprintf(ms->file, "%"PRIu64".%"PRIu64" %"PRIu32" %"PRIu64"\n",
	  (uint64_t)(tv.tv_sec), (uint64_t)(tv.tv_usec),
	  comp_id, ldms_get_u64(m));
  if (rc <= 0)
    msglog("Error %d writing to '%s'\n", rc, ms->path);
  //FIXME: possibly put in something to force timely regular flushes
  //  fflush(ms->file); //tail the flat file if you want to see it

  return 0;
}

static int flush_store(ldmsd_metric_store_t _ms)
{
  //NOTE - later change this so that data is queued up in store and so that flush 
  //does a bulk insert.
  
  return 0;
}

static void close_store(ldmsd_metric_store_t _ms)
{
  pthread_mutex_lock(&cfg_lock);

  struct flatfile_metric_store *ms = _ms;
  if (!_ms)
    return;

  idx_delete(metric_idx, ms->metric_key, strlen(ms->metric_key));
  if (ms->file) fclose(ms->file);
  ms->file = 0;
  free(ms->path);
  free(ms->metric_key);
  free(ms);

  pthread_mutex_unlock(&cfg_lock);
}

static void destroy_store(ldmsd_metric_store_t _ms)
{
}

static struct ldmsd_store store_flatfile = {
  .base = {
    .name = "flatfile",
    .term = term,
    .config = config,
    .usage = usage,
  },
  .get = get_store,
  .new = new_store,
  .destroy = destroy_store,
  .get_context = get_ucontext,
  .store = store,
  .flush = flush_store,
  .close = close_store,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
  msglog = pf;
  return &store_flatfile.base;
}

static void __attribute__ ((constructor)) store_flatfile_init();
static void store_flatfile_init()
{
  metric_idx = idx_create();
  pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_flatfile_fini(void);
static void store_flatfile_fini()
{
  pthread_mutex_destroy(&cfg_lock);
  idx_destroy(metric_idx);
}
