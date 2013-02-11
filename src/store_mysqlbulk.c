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


#define TV_SEC_COL    0
#define TV_USEC_COL    1
#define GROUP_COL    2
#define VALUE_COL    3

#define NUM_BULK_INSERT 1000

//one database for all
//NOTE: there is a separate mysql connection per table. Increment your max_connections in your /etc/my.cnf accordingly
static char* db_host = NULL;
static char* db_schema = NULL;
static char* db_user = NULL;
static char* db_passwd = NULL;
static int createovistables = 0;

static idx_t metric_idx;
static ldmsd_msg_log_f msglog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

struct mysqlbulk_metric_store {
  struct ldmsd_store *store;
  char* tablename;
  char* cleansedmetricname;
  MYSQL *conn;
  char* insertvalues[NUM_BULK_INSERT];
  int insertcount;
  char *metric_key;
  void *ucontext; 
  pthread_mutex_t lock;
};

pthread_mutex_t cfg_lock;


static int initConn(MYSQL **conn){
  *conn = NULL; //default

  if ((strlen(db_host) == 0) || (strlen(db_schema) == 0) ||
      (strlen(db_user) == 0)){ 
    msglog("Invalid parameters for database");
    return EINVAL;
  }

  *conn = mysql_init(NULL);
  if (*conn == NULL){
    msglog("Error %u: %s\n", mysql_errno(*conn), mysql_error(*conn));
    return EPERM;
  }

  //passwd can be null....
  if (!mysql_real_connect(*conn, db_host, db_user,
			  db_passwd, db_schema, 0, NULL, 0)){
    msglog("Error %u: %s\n", mysql_errno(*conn), mysql_error(*conn));
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

  //optional passwd
  value = av_value(avl, "dbpasswd");
  if (value){
    pthread_mutex_lock(&cfg_lock);
    if (db_passwd)
      free(db_passwd);
    db_passwd = strdup(value);
    pthread_mutex_unlock(&cfg_lock);
    if (!db_passwd){
      return ENOMEM;
    }
  }

  //optional 
  value = av_value(avl, "createovistables");
  if (value){
    pthread_mutex_lock(&cfg_lock);
    createovistables = atoi(value);
    pthread_mutex_unlock(&cfg_lock);
  }

  //will init the conn when each table is created

  return 0;
 err:
  return EINVAL;
}

static void term(void)
{

  pthread_mutex_lock(&cfg_lock);

  if (db_host) free (db_host);
  db_host = NULL;
  if (db_schema) free (db_schema);
  db_schema = NULL;
  if (db_passwd) free (db_passwd);
  db_passwd = NULL;
  if (db_user) free (db_user);
  db_user = NULL;

  pthread_mutex_unlock(&cfg_lock);

}

static const char *usage(void)
{
    return  "    config name=store_mysqlbulk dbschema=<db_schema> dbuser=<dbuser> dbhost=<dbhost>\n"
      "        - Set the dbinfo for the mysql storage for data.\n"
      "        dbhost           The host of the database (check format)"
      "        dbschema         The name of the database \n"
      "        dbuser           The username of the database \n"
      "        dbpasswd         The passwd for the user of the database (optional)\n"
      "        ovistables       Create and populate supporting tables for use with OVIS (0/1) (optional. 0 default)"
      ;
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
  struct mysqlbulk_metric_store *ms = _ms;
  return ms->ucontext;
}

static int createTable(MYSQL* conn, char* tablename){
  //will create a table (but not supporting OVIS tables)
  //NOTE: this is locked from the surrounding function

  if (conn == NULL)
    return EPERM;

  //FIXME: we have no way of knowing the data storage type. for now store everything as uint64_t
  char* mysqlstoragestring = "BIGINT UNSIGNED";

  char query1[4096];
  //create table if required
  snprintf(query1, 4095,"%s%s%s%s%s%s%s%s%s",
	   "CREATE TABLE IF NOT EXISTS ",
	   tablename,
	   " (`TableKey`  INT NOT NULL AUTO_INCREMENT NOT NULL, `CompId`  INT(32) NOT NULL, `Value` ",
	   mysqlstoragestring,
	   " NOT NULL, `Time`  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, `Level`  INT(32) NOT NULL DEFAULT 0, PRIMARY KEY  (`TableKey` ), KEY ",
	   tablename,
	   "_Time (`Time` ), KEY ",
	   tablename,
	   "_Level (`CompId` ,`Level` ,`Time` ))");
  
  int mysqlerrx = mysql_query(conn, query1);
  if (mysqlerrx != 0){
    msglog("Cannot query to create table '%s'. Error: %d\n", tablename, mysqlerrx);
    return -1;
  }


  return 0;

}

static int createOVISSupportingTables(MYSQL* conn, char *tableName, char* compAssoc, char* ovisMetricName){
  //1) will create MetricValueTypes and MetricValueTableIndex if necessary
  //2) will populate those *fully* if ComponentTypes exists and is populated
  //or *incompletely* if ComponentTypes does not exist -- that is, will just put -1
  //for all component types. In this latter case, the user will have to
  //correct the CompType col upon putting these tables into an ovdb output.

  //NOTE: this is still locked from the surrounding function. Want this to occur directly after the above if
  //necessary. Not combining them for clarity/schema_change later.

  //FIXME: later should get these from the created table type
  //  char* mysqlstoragestring = "BIGINT UNSIGNED";
  char* ovisstoragestring = "uint64_t";

  MYSQL_RES *result;
  MYSQL_ROW row;

  //get the comptype              
  int ctype = -1;
  char query1[4096];
  snprintf(query1,4095,"SELECT CompType From ComponentTypes WHERE ShortName ='%s'", compAssoc); //FIXME: check for cap       
  if (mysql_query(conn, query1) == 0){
    result = mysql_store_result(conn);
    int num_fields = mysql_num_fields(result);
    if ((num_fields == 1) && (mysql_num_rows(result) == 1)){
      while((row = mysql_fetch_row(result))){
	ctype = atoi(row[0]);
	break;
      }
    }
    if (result) mysql_free_result(result);
  }
  //if we dont have the table or the component type, use default value (-1)

  //create the MetricValueType Table if necessary
  char* query2 =  "CREATE TABLE IF NOT EXISTS `MetricValueTypes` (`ValueType` int(11) NOT NULL AUTO_INCREMENT,  `Name` tinytext NOT NULL,  `Units` tinytext NOT NULL,  `Storage` tinytext NOT NULL,  `Constant` int(32) NOT NULL DEFAULT '0',  PRIMARY KEY (`ValueType`))";
  if (mysql_query(conn,query2) != 0){
    msglog("Err: Cannot check for the MetricValueTypeTable\n");
    return -1;
  }

  //create and retain the MetricValueType
  int metrictype = -1;
  snprintf(query1, 4095,
	   "SELECT ValueType from MetricValueTypes WHERE Name='%s' AND Units='1' AND Constant=0 AND Storage='%s'",
	   ovisMetricName, ovisstoragestring);
  if (mysql_query(conn, query1) != 0){
    msglog("Err: Cannot query for MetricValueType for '%s'\n", ovisMetricName);
    return -1;
  }
  result = mysql_store_result(conn);
  int num_fields = mysql_num_fields(result);
  if ((num_fields == 1) && (mysql_num_rows(result) == 1)){
    while((row = mysql_fetch_row(result))){
      metrictype = atoi(row[0]);
      //FIXME: check that if it is already in there that it is not in with the wrong type....
      mysql_free_result(result);
      break;
    }
  } else {
    mysql_free_result(result);
    snprintf(query1, 4095, "INSERT INTO MetricValueTypes(Name, Units, Storage, Constant) VALUES ('%s', '1', '%s', 0)",
	     ovisMetricName,ovisstoragestring);
    if (mysql_query(conn, query1) !=0){
      msglog("Cannot insert MetricValueType for '%s'\n", ovisMetricName);
      return -1;
    }

    snprintf(query1, 4095,
	     "SELECT ValueType from MetricValueTypes WHERE Name='%s' AND Units='1' AND Constant=0 AND Storage='%s'",
             ovisMetricName, ovisstoragestring );
    if (mysql_query(conn, query1) != 0){
      msglog("Cannot query for MetricValueType for '%s'\n", ovisMetricName);
      return -1;
    }
    result = mysql_store_result(conn);
    int num_fields = mysql_num_fields(result);
    if ((num_fields != 1) || (mysql_num_rows(result) != 1)){
      msglog("No MetricValueType for '%s'\n", ovisMetricName);
      mysql_free_result(result);
      return -1;
    }
    while((row = mysql_fetch_row(result))){
      metrictype = atoi(row[0]);
      mysql_free_result(result);
    }
  }

  //create the MetricValueTableIndex if necessary
  query2 =  "CREATE TABLE IF NOT EXISTS `MetricValueTableIndex` (`TableId` int(11) NOT NULL AUTO_INCREMENT,  `TableName` tinytext NOT NULL,  `CompType` int(32) NOT NULL,  `ValueType` int(32) NOT NULL,  `MinDeltaTime` float NOT NULL DEFAULT '1',  PRIMARY KEY (`TableId`))";
  if (mysql_query(conn,query2) != 0){
    msglog("Err: Cannot check for the MetricValueTableIndex\n");
    return -1;
  }

  //create the TableId if necessary
  snprintf(query1, 4095,
	   "SELECT TableId from MetricValueTableIndex WHERE TableName='%s' AND CompType=%d AND ValueType=%d",
	   tableName, ctype, metrictype);
  if (mysql_query(conn, query1) != 0){
    msglog("Cannot query for table id for '%s'\n", tableName);
    return -1;
  }
  result = mysql_store_result(conn);
  num_fields = mysql_num_fields(result);
  if ((num_fields != 1) || (mysql_num_rows(result) != 1)){ //FIXME: better check
    mysql_free_result(result);
    snprintf(query1, 4095,
	     "INSERT INTO MetricValueTableIndex ( TableName, CompType, ValueType, MinDeltaTime ) VALUES ('%s', %d, %d, 0)",
	     tableName, ctype, metrictype);
    if (mysql_query(conn, query1) != 0){
      msglog("Cannot insert into MetricValueTableIndex for tableName '%s'\n", tableName);
      return -1;
    }
  } else {
    mysql_free_result(result);
  }


  return 0;
}

static ldmsd_metric_store_t
new_store(struct ldmsd_store *s, const char *comp_name, const char *metric_name,
	  void *ucontext)
{

  pthread_mutex_lock(&cfg_lock);

  //FIXME: is there someway I can get the type of the metric from here???

  char metric_key[128];
  struct mysqlbulk_metric_store *ms;

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
    ms->insertcount = 0;
    pthread_mutex_init(&ms->lock, NULL);

    //TABLENAME - Assuming OVIS form for tablename, columns
    char* cleansedmetricname = strdup(metric_name);
    if (!cleansedmetricname)
      goto err1;
    int i;
    for (i = 0; i < strlen(cleansedmetricname); i++){
      //      if (!isalnum(cleansedmetricname[i]) && cleansedmetricname[i] != '_'){
      if (!isalnum(cleansedmetricname[i])){
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
      //if (!isalnum(cleansedcompname[i]) && cleansedcompname[i] != '_')
      if (!isalnum(cleansedcompname[i])){
	cleansedcompname[i] = '_'; // replace mysql non-allowed chars with _
      }
    }
    cleansedcompname[0] = toupper(cleansedcompname[0]); 

    int sz = strlen(tempmetricname)+ strlen(cleansedcompname)+strlen("MetricValues");
    char* tablename = (char*)malloc((sz+5)*sizeof(char));
    if (!tablename){
      free(tempmetricname);
      free(cleansedcompname);
      goto err1A;
    }
    snprintf(tablename, (sz+5),"Metric%s%sValues",cleansedcompname,tempmetricname);
    ms->tablename = tablename;
    //will need to retain the compname and metricname temporarily if need to build the OVIS supporting tables

    ms->metric_key = strdup(metric_key);
    if (!ms->metric_key){
      free(tempmetricname);     
      free(cleansedcompname);
      goto err2;
    }

    int rc = initConn(&(ms->conn));
    if (rc != 0){
      free(tempmetricname);     
      free(cleansedcompname);
      goto err3;
    }

    rc = createTable(ms->conn, ms->tablename);
    if (rc != 0){
      free(tempmetricname);     
      free(cleansedcompname);
      goto err3;
    }
    if (createovistables){
      rc = createOVISSupportingTables(ms->conn, ms->tablename, cleansedcompname, tempmetricname);
      if (rc != 0){
	free(tempmetricname);     
	free(cleansedcompname);
	goto err3;
      }
    }
    free(tempmetricname);     
    free(cleansedcompname);

    idx_add(metric_idx, metric_key, strlen(metric_key), ms);
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

static int flush_store(ldmsd_metric_store_t _ms)
{

  struct mysqlbulk_metric_store *ms = _ms;
  if (!_ms)
    return EINVAL;

  if (ms->conn == NULL){
    msglog("Cannot insert value for <%s>: Connection to mysql is closed\n", ms->tablename);
    return EPERM;
  }

  char* insertStatement;
  insertStatement = (char*) malloc(ms->insertcount*128*sizeof(char));
  if (!insertStatement){
    return ENOMEM;
  }
  sprintf(insertStatement,"INSERT INTO %s VALUES ", ms->tablename);

  int i;
  for (i = 0; i < ms->insertcount; i++){
    strcat(insertStatement,(ms->insertvalues[i]));
    strcat(insertStatement,",");
    free(ms->insertvalues[i]);
  }
  ms->insertcount = 0;
  insertStatement[strlen(insertStatement)-1] = '\0';

  int mysqlerrx = mysql_query(ms->conn, insertStatement);
  if (mysqlerrx != 0){
    msglog("Failed to perform query <%s>. Error: %d\n", mysqlerrx);
    return -1;
  }

  return 0;
}

static int
store(ldmsd_metric_store_t _ms, uint32_t comp_id,
      struct timeval tv, ldms_metric_t m)
{
  //NOTE: ldmsd_store invokes the lock on this ms, so we dont have to do it here
  struct mysqlbulk_metric_store *ms;

  if (!_ms){
    return EINVAL;
  }

  ms = _ms;

  if (ms->insertcount == NUM_BULK_INSERT){
    int rc = flush_store(ms);
    if (rc != 0){
      msglog("Can't flush store for %s. Not accepting new data\n",ms->tablename);
      return rc;
    }
  }

  //FIXME -- need some checks here on the type (mysql table create is before the new_store)
  // ldms_value_type valtype = ldms_get_metric_type(m);
  uint64_t val = ldms_get_u64(m);

  //NOTE -- dropping the subsecond part of the time to be consistent with OVIS tables.
  //unlike prev inserters which used the time of the insert, here we will use the timeval.
  //FIXME: do we need this in a YYYY-MM-DD HH-mm-SS type-format?
  int sec = (int)(tv.tv_sec);

  long int level = lround( -log2( drand48())); //residual OVIS-ism

  char data[128];
  snprintf(data,127,"(NULL, %d, %" PRIu64 ", FROM_UNIXTIME(%d), %ld )",
	   (int)comp_id, val, sec, level); 
  ms->insertvalues[ms->insertcount++] = strdup(data);

  return 0;
}

static void close_store(ldmsd_metric_store_t _ms)
{
  pthread_mutex_lock(&cfg_lock);

  struct mysqlbulk_metric_store *ms = _ms;
  if (!_ms)
    return;

  msglog("Closing store for %s which is a free of the idx and close conn\n", ms->tablename);
  idx_delete(metric_idx, ms->metric_key, strlen(ms->metric_key));
  if (ms->conn) mysql_close(ms->conn);
  ms->conn = NULL; 
  free(ms->tablename);
  free(ms->metric_key);
  free(ms);

  pthread_mutex_unlock(&cfg_lock);
}

static void destroy_store(ldmsd_metric_store_t _ms)
{
}

static struct ldmsd_store store_mysqlbulk = {
  .base = {
    .name = "mysqlbulk",
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
  return &store_mysqlbulk.base;
}

static void __attribute__ ((constructor)) store_mysqlbulk_init();
static void store_mysqlbulk_init()
{
  metric_idx = idx_create();
  pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_mysqlbulk_fini(void);
static void store_mysqlbulk_fini()
{
  pthread_mutex_destroy(&cfg_lock);
  idx_destroy(metric_idx);
}
