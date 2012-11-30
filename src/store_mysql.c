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
#include <mysql/my_global.h>
#include <mysql/mysql.h>
#include <sos/idx.h>
#include "ldms.h"
#include "ldmsd.h"


#define TV_SEC_COL	0
#define TV_USEC_COL	1
#define GROUP_COL	2
#define VALUE_COL	3

static char* db_host;
static char* db_schema;
static char* db_user;
static char* db_passwd;
//FIXME - Will db connection be at this level or in the metric store? 
MYSQL *conn = NULL; 

static idx_t metric_idx;
static ldmsd_msg_log_f msglog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

struct mysql_metric_store {
	struct ldmsd_store *store;
        char* tablename;
        char* cleansedmetricname;
	char *metric_key;
        void *ucontext; 
	pthread_mutex_t lock;
};

pthread_mutex_t cfg_lock;


static int initConn(){
  if ((strlen(db_host) == 0) || strlen(db_schema) ||
      (strlen(db_user) == 0)){ 
    msglog("Invalid parameters for database");
    return EINVAL;
  }

  conn = mysql_init(NULL);
  if (conn == NULL){
    msglog("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
    return EPERM;
  }

  if (!mysql_real_connect(conn, db_host, db_user,
			  db_passwd, db_schema, 0, NULL, 0)){
    msglog("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
    return EPERM;
  }

  return 0;
}




/** 
 * \brief Configuration
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	value = av_value(avl, "dbhost");
	if (!value)
		goto err;

	pthread_mutex_lock(&cfg_lock);
	if (db_host)
	  free(db_host);
	db_host = strdup(value);
	pthread_mutex_unlock(&cfg_lock);
	if (!db_host)
		return ENOMEM;


	value = av_value(avl, "dbschema");
	if (!value)
		goto err;

	pthread_mutex_lock(&cfg_lock);
	if (db_schema)
	  free(db_schema);
	db_schema = strdup(value);
	pthread_mutex_unlock(&cfg_lock);
	if (!db_schema)
		return ENOMEM;

	value = av_value(avl, "dbuser");
	if (!value)
		goto err;

	pthread_mutex_lock(&cfg_lock);
	if (db_user)
	  free(db_user);
	db_user = strdup(value);
	pthread_mutex_unlock(&cfg_lock);
	if (!db_user)
		return ENOMEM;

	value = av_value(avl, "dbpasswd");
	if (!value)
		goto err;

	pthread_mutex_lock(&cfg_lock);
	if (db_passwd)
	  free(db_passwd);
	db_passwd = strdup(value);
	pthread_mutex_unlock(&cfg_lock);
	if (!db_passwd)
		return ENOMEM;

	//NOTE: do we want 1 global conn, or per metric?
	//NOTE: keep it open all the time or close? 
	int rc = initConn();
	if (rc != 0)
	  return rc;

	return 0;
 err:
	return EINVAL;
}

static void term(void)
{
  if (conn) mysql_close(conn);
}

static const char *usage(void)
{
	return  "    config name=store_mysql dbschema=<db_schema> dbuser=<dbuser> dbpasswd=<dbpasswd> dbhost=<dbhost>\n"
		"        - Set the dbinfo for the mysql storage for data.\n"
		"        dbhost    The host of the database (check format)"
		"        dbschema  The name of the database \n"
        	"        dbuser      The username of the database \n"
		"        dbpasswd    The passwd for the user of the database (find an alternate method later)\n";
}

static ldmsd_metric_store_t
get_store(struct ldmsd_store *s, const char *comp_name, const char *metric_name)
{
	char metric_key[128];
	ldmsd_metric_store_t ms;

	pthread_mutex_lock(&cfg_lock);
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
	struct mysql_metric_store *ms = _ms;
	return ms->ucontext;
}

static int createTable(char* tablename){
  //will create a table (but not supporting OVIS tables)

  if (conn == NULL)
    return EPERM;

  //FIXME: we have no way of knowing the data storage type -- for now store everything as int (?)
  char storagestring[20] = "INT";

  char query1[4096];
  //create table if required
  snprintf(query1, 4095,"%s%s%s%s%s%s%s%s%s",
	   "CREATE TABLE IF NOT EXISTS ",
	   tablename,
	   "(`TableKey`  INT NOT NULL AUTO_INCREMENT NOT NULL, `CompId`  INT(32) NOT NULL, `Value` ",
	   storagestring,
	   " NOT NULL, `Time`  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, `Level`  INT(32) NOT NULL DEFAULT 0, PRIMARY KEY  (`TableKey` ), KEY ",
	   tablename,
	   "_Time (`Time` ), KEY ",
	   tablename,
	   "_Level (`CompId` ,`Level` ,`Time` ))");
  if (mysql_query(conn, query1) != 0){
    msglog("Cannot query to create table '%s'\n", tablename);
    return -1;
  }

  return 0;

}

static ldmsd_metric_store_t
new_store(struct ldmsd_store *s, const char *comp_name, const char *metric_name,
	  void *ucontext)
{

  //FIXME: is there someway I can get the type of the metric from here???

	char metric_key[128];
	struct mysql_metric_store *ms;

	pthread_mutex_lock(&cfg_lock);
	/*
	 * Create a table for this comptype and metric name if one does not
	 * already exist
	 */
	sprintf(metric_key, "%s:%s", comp_name, metric_name);
	ms = idx_find(metric_idx, metric_key, strlen(metric_key));
	if (!ms) {
	  ms = calloc(1, sizeof *ms);
	  if (!ms)
	    goto out;
	  ms->ucontext = ucontext;
	  ms->store = s;
	  pthread_mutex_init(&ms->lock, NULL);

	  //TABLENAME - Assuming OVIS form for tablename, columns
	  char* cleansedmetricname = strdup(metric_name);
	  if (!cleansedmetricname)
	    goto err1;
	  int i;
	  for (i = 0; i < strlen(cleansedmetricname); i++){
	    if (!isalnum(cleansedmetricname[i]) || cleansedmetricname[i] != '_'){
	      cleansedmetricname[i] = '_'; // replace mysql non-allowed chars with _
	    }
	  }
	  ms->cleansedmetricname = cleansedmetricname;
	  char* tempmetricname = strdup(cleansedmetricname);
	  if (!tempmetricname){
	    goto err1A;
	  }
	  tempmetricname[0] = toupper(tempmetricname[0]);

	  char* cleansedcompname = strdup(comp_name);
	  if (!tempmetricname){
	    free(tempmetricname);
	    goto err1A;
	  }
	  for (i = 0; i < strlen(cleansedcompname); i++){
	    if (!isalnum(cleansedcompname[i]) || cleansedcompname[i] != '_'){
	      cleansedcompname[i] = '_'; // replace mysql non-allowed chars with _
	    }
	  }
	  cleansedcompname[0] = toupper(cleansedcompname[0]);

	  int sz = strlen(tempmetricname)+ strlen(cleansedcompname)+strlen("MetricValues");
	  char* tablename = (char*)malloc(sz*sizeof(char));
	  if (!tablename){
	    free(tempmetricname);
	    free(cleansedcompname);
	    goto err1A;
	  }
	  snprintf(tablename, sz,"Metric%s%sValues",tempmetricname,cleansedcompname);
	  ms->tablename = tablename;
	  free(tempmetricname);	 
	  free(cleansedcompname);

	  ms->metric_key = strdup(metric_key);
	  if (!ms->metric_key)
	    goto err2;

	  int tablecreated = createTable(ms->tablename);
	  if (tablecreated)
	    idx_add(metric_idx, metric_key, strlen(metric_key), ms);
	  else
	    goto err3;
	}
	goto out;
 err3:
	free(ms->metric_key);
 err2:
	free(ms->tablename);
 err1A:
	free(ms->cleansedmetricname);
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

  struct mysql_metric_store *ms;

  if (!_ms)
    return EINVAL;

  ms = _ms;

  if (conn == NULL)
    return EPERM;
	
  //FIXME -- need some checks here on the type (mysql table create is before the new_store)
  // ldms_value_type valtype = ldms_get_metric_type(m);
  uint64_t val = ldms_get_u64(m);

  //NOTE -- dropping the subsecond part of the time to be consistent with OVIS tables.
  //unlike prev inserters which used the time of the insert, here we will use the timeval.
  //FIXME: do we need this in a YYYY-MM-DD HH-mm-SS type-format?
  int sec = (int)(tv.tv_sec);

  long int level = lround( -log2( drand48())); //residual OVIS-ism

  char insertStatement[1024];
  snprintf(insertStatement,1023,
	   "INSERT INTO %s VALUES( %d, %d, %d, NULL, %ld )",
	   ms->tablename, sec, comp_id, (int)(val), level); 

  if (mysql_query(conn, insertStatement) != 0)
    return -1;

  return 0;
}

static int flush_store(ldmsd_metric_store_t _ms)
{
  //NOTE - later change this so that data is queued up in store and so that flush 
  //does the bulk insert (note that that is on a per metric (thus ms) basis).
  
  //  struct mysql_metric_store *ms = _ms;
  //  if (!_ms)
  //    return EINVAL;
  //	sos_flush(ms->sos);  old code... FIXME: will do bulk insert call here....

	return 0;
}

static void close_store(ldmsd_metric_store_t _ms)
{
	struct mysql_metric_store *ms = _ms;
	if (!_ms)
		return;
	// mysql has no equivalent for the table close (this is *not* the connection close,
	// if it is at the global and not ms level)

	//	if (ms->sos)
	//		sos_close(ms->sos);
	idx_delete(metric_idx, ms->metric_key, strlen(ms->metric_key));
	free(ms->tablename);
	free(ms->metric_key);
	free(ms);
}

static void destroy_store(ldmsd_metric_store_t _ms)
{
}

static struct ldmsd_store store_sos = {
	.base = {
		.name = "sos",
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
	return &store_sos.base;
}

static void __attribute__ ((constructor)) store_mysql_init();
static void store_mysql_init()
{
	metric_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_mysql_fini(void);
static void store_mysql_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(metric_idx);
}

