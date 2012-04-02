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
/*
 * This is the mysqlinsert sampler. It saves a local metric set's data to
 * a mysql database in the ovis table format.
 */
#include <ctype.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <mysql/my_global.h>
#include <mysql/mysql.h>
#include "ldms.h"
#include "ldmsd.h"

//DB_SOCKET is currently not used
//#define DEFAULT_DB_SOCKET "/var/lib/mysql/mysql.sock" 
#define DEFAULT_USER "ovis"
#define DEFAULT_PASS ""
#define DEFAULT_DB "mysqlinserttest"
#define DEFAULT_DBHOST "localhost"

#define LOGFILE "/var/log/mysqlinsert.log"

struct fmetric {
  ldms_metric_t md;
  uint64_t key;		/* component id or whatever else you like. if you change this, change how to get compId below */   //FIXME: check if this has to be unique in the metric set.  
  int diffMetric;
  int lastValSet;
  uint64_t lastVal;
  char *tableName; //FIXME: should this be set by the "addmetric" or by this code?
  LIST_ENTRY(fmetric) entry;
};

struct fset {
  ldms_set_t sd;
  uint64_t lastDataGnInsert; //data generation number of the last insert
  LIST_HEAD(fmetric_list,  fmetric) metric_list;
  LIST_ENTRY(fset) entry;
};

LIST_HEAD(fset_list, fset) set_list;

static ldmsd_msg_log_f msglog;

ldms_metric_t compid_metric_handle;

//NOTE that some of these variables will stop this from being able to be multithreaded.
char set_name[LDMS_MAX_CONFIG_STR_LEN];
char metric_name[LDMS_MAX_CONFIG_STR_LEN];
char ovis_metric_name[LDMS_MAX_CONFIG_STR_LEN];
char comp_assoc[LDMS_MAX_CONFIG_STR_LEN];
char db_schema[LDMS_MAX_CONFIG_STR_LEN];
char db_host[LDMS_MAX_CONFIG_STR_LEN];
char username[LDMS_MAX_CONFIG_STR_LEN];
char password[LDMS_MAX_CONFIG_STR_LEN];


MYSQL *conn = NULL;

static struct fset *get_fset(char *set_name)
{
	struct fset *ss;
	LIST_FOREACH(ss, &set_list, entry) {
		if (0 == strcmp(set_name, ldms_get_set_name(ss->sd)))
			return ss;
	}
	return NULL;
}

static struct fmetric *get_metric(struct fset *set, char *metric_name)
{
	struct fmetric *met;
	LIST_FOREACH(met, &set->metric_list, entry) {
		if (0 == strcmp(metric_name, ldms_get_metric_name(met->md)))
			return met;
	}
	return NULL;
}

static int add_set(char *set_name)
{
	ldms_set_t sd;
	struct fset *set = get_fset(set_name);
	if (set)
		return EEXIST;

	sd = ldms_get_set(set_name);
	if (!sd)
		return ENOENT;

	set = calloc(1, sizeof *set);
	if (!set) {
		return ENOMEM;
	}
	set->sd = sd;
	set->lastDataGnInsert = -1;
	LIST_INSERT_HEAD(&set_list, set, entry);
	return 0;
}

static int remove_set(char *set_name)
{
	struct fset *set = get_fset(set_name);
	if (!set)
		return ENOENT;
	LIST_REMOVE(set, entry);
	set->lastDataGnInsert = -1;
	return 0;
}

static int add_metric(char *set_name, char *metric_name, uint64_t key, int diff_metric, char* table_name)
{
	ldms_metric_t *md;
	struct fmetric *metric;
	struct fset *set = get_fset(set_name);
	if (!set)
		return ENOENT;

	metric = get_metric(set, metric_name);
	if (metric)
		return EEXIST;

	md = ldms_get_metric(set->sd, metric_name);
	if (!md)
		return ENOENT;

	metric = calloc(1, sizeof *metric);
	if (!metric)
		return ENOMEM;

	if (diff_metric != 0 && diff_metric != 1){
	  return EINVAL;
	}

	metric->md = md;
	metric->key = key;
	metric->diffMetric = diff_metric;
	metric->lastValSet = 0;
	metric->lastVal = 0;
	metric->tableName = strdup(table_name);
	LIST_INSERT_HEAD(&set->metric_list, metric, entry);
	return 0;
}


static int remove_metric(char *set_name, char *metric_name)
{
	struct fset *set;
	struct fmetric *met;
	set = get_fset(set_name);
	if (!set)
		return ENOENT;
	met = get_metric(set, metric_name);
	if (!met)
		return ENOENT;
	LIST_REMOVE(met, entry);
	free(met->tableName);
	return 0;
}


static int set_dbconfigs()
{
  if (strlen(db_schema) == 0)
    snprintf(db_schema,LDMS_MAX_CONFIG_STR_LEN-1,"%s",DEFAULT_DB);
  if (strlen(db_host) == 0)
    snprintf(db_host,LDMS_MAX_CONFIG_STR_LEN-1,"%s",DEFAULT_DBHOST);
  if (strlen(username) == 0)
    snprintf(username,LDMS_MAX_CONFIG_STR_LEN-1,"%s",DEFAULT_USER);
  if (strlen(password) == 0)
    snprintf(password,LDMS_MAX_CONFIG_STR_LEN-1,"%s",DEFAULT_PASS);
  return 0;
}


static int createTable(char* ovisMetricName, char* compAssoc, char *tableName){
  //will create a table and the supporting tables, if necessary
  MYSQL_RES *result;
  MYSQL_ROW row;

  if (conn == NULL)
    return EPERM;

  //get the comptype
  int ctype = -1;
  char query1[4096];
  snprintf(query1,4095,"SELECT CompType From ComponentTypes WHERE ShortName ='%s'", compAssoc); //FIXME: check for cap
  if (mysql_query(conn, query1)){
    msglog("Cannot query for Component type '%s'\n", compAssoc);
    return -1;
  }
  result = mysql_store_result(conn);
  int num_fields = mysql_num_fields(result);
  if ((num_fields == 1) && (mysql_num_rows(result) == 1)){
    while((row = mysql_fetch_row(result))){
      ctype = atoi(row[0]); 
      mysql_free_result(result);
      break;
    }
  } else {
    //dont have this comp assoc
    msglog("Component type '%s' unknown\n", compAssoc);
    return -1;
  }


  //create table if required
  snprintf(query1, 4095,"%s%s%s%s%s%s%s",
	   "CREATE TABLE IF NOT EXISTS ",
	   tableName,
	   "(`TableKey`  INT NOT NULL AUTO_INCREMENT NOT NULL, `CompId`  INT(32) NOT NULL, `Value`  INT(32) NOT NULL, `Time`  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, `Level`  INT(32) NOT NULL DEFAULT 0, PRIMARY KEY  (`TableKey` ), KEY ",
	   tableName,
	   "_Time (`Time` ), KEY ",
	   tableName,
	   "_Level (`CompId` ,`Level` ,`Time` ))");
  
  if (mysql_query(conn, query1) != 0){
    msglog("Cannot query to create table '%s'\n", compAssoc);
    return -1;
  }

  //create the MetricValueType
  int metrictype = -1;
  snprintf(query1, 4095, "%s%s%s",
	   "SELECT ValueType from MetricValueTypes WHERE Name='",
	   ovisMetricName,
	   "' AND Units='1' AND Constant=0 AND Storage='int'");
  //NOTE that storage will be int and not int(32)
  if (mysql_query(conn, query1) != 0){
    msglog("Cannot query for MetricValueType for '%s'\n", ovisMetricName);
    return -1;
  }
  result = mysql_store_result(conn);
  num_fields = mysql_num_fields(result);
  if ((num_fields == 1) && (mysql_num_rows(result) == 1)){
    //it could be in there already (more than 1 sampler can provide the same data)
    while((row = mysql_fetch_row(result))){
      metrictype = atoi(row[0]); 
      mysql_free_result(result);
      break;
    }
  } else {
    char query2[4096];
    mysql_free_result(result);

    //FIXME: currently we make all ldms metrics long int. can we support long int in the db and processing?
    // can we have other types for ldms and have them easily discovered?
    snprintf(query2, 4095, "%s%s%s",
	     "INSERT INTO MetricValueTypes(Name, Units, Storage, Constant) VALUES ('",
	     ovisMetricName,
	     "', '1', 'int', 0)");
    if (mysql_query(conn, query2) !=0){
      msglog("Cannot insert MetricValueType for '%s'\n", ovisMetricName);
      return -1;
    }
    snprintf(query1, 4095, "%s%s%s",
	     "SELECT ValueType from MetricValueTypes WHERE Name='",
	     ovisMetricName,
	     "' AND Units='1' AND Constant=0 AND Storage='int'");
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
      break;
    }
  }


  //create the TableId if necessary
  snprintf(query1, 4095, "%s%s%s%d%s%d",
	   "SELECT TableId from MetricValueTableIndex WHERE TableName='",
	   tableName,
	   "' AND CompType=",
	   ctype,
	   " AND ValueType=",
	   metrictype);
  if (mysql_query(conn, query1) != 0){
    msglog("Cannot query for table id for '%s'\n", tableName);
    return -1;
  }
  result = mysql_store_result(conn);
  num_fields = mysql_num_fields(result);
  if ((num_fields != 1) || (mysql_num_rows(result) != 1)){
    mysql_free_result(result);
    snprintf(query1, 4095, "%s%s%s%d%s%d%s",
	     "INSERT INTO MetricValueTableIndex ( TableName, CompType, ValueType ) VALUES ('",
	     tableName,
	     "', ",
	     ctype,
	     ", ",
	     metrictype,
	     ")");
    if (mysql_query(conn, query1) != 0){
      msglog("Cannot insert into MetricValueTableIndex for tableName '%s'\n", tableName);
      return -1;
    }
  } else {
    mysql_free_result(result);
  }

  return 0;
}


static int config(char *config_str)
{
	enum {
		ADD_SET,
		ADD_METRIC,
		REMOVE_SET,
		REMOVE_METRIC,
		DATABASE_INFO,
	} action;
	int rc;
	uint64_t key;
	int diff_metric;

	if (0 == strncmp(config_str, "add_metric", 10))
		action = ADD_METRIC;
	else if (0 == strncmp(config_str, "remove_metric", 13))
		action = REMOVE_METRIC;
	else if (0 == strncmp(config_str, "add", 3))
		action = ADD_SET;
	else if (0 == strncmp(config_str, "remove", 6))
		action = REMOVE_SET;
	else if (0 == strncmp(config_str, "database_info", 13))
                action = DATABASE_INFO;
	else {
		msglog("mysqlinsert: Invalid configuration string '%s'\n",
		       config_str);
		return EINVAL;
	}
	switch (action) {
	case ADD_SET:
                sscanf(config_str, "add=%s", set_name);
		rc = add_set(set_name);
		break;
	case REMOVE_SET:
		sscanf(config_str, "remove=%s", set_name);
		rc = remove_set(set_name);
		break;
	case ADD_METRIC:
	  {

	    //NOTE: not making this generic since the insert also has a format
	    rc = sscanf(config_str, "add_metric=%[^&]&%[^&]&%"PRIu64"&%d&%[^&]&%s",
			set_name, metric_name, &key, &diff_metric, comp_assoc, ovis_metric_name); 	
	    if (rc != 6){
	      msglog("Problems parsing add_metric\n");
	      return EINVAL;
	    }

	    //table naming convention:  
	    char table_name[LDMS_MAX_CONFIG_STR_LEN];
	    char cap_comp_assoc[LDMS_MAX_CONFIG_STR_LEN];
	    char cap_ovis_metric_name[LDMS_MAX_CONFIG_STR_LEN];
	    snprintf(cap_comp_assoc, (LDMS_MAX_CONFIG_STR_LEN-1), "%s", comp_assoc);
	    snprintf(cap_ovis_metric_name, (LDMS_MAX_CONFIG_STR_LEN-1), "%s", ovis_metric_name);
	    cap_comp_assoc[0] = toupper(comp_assoc[0]);
	    cap_ovis_metric_name[0] = toupper(cap_ovis_metric_name[0]);

	    snprintf(table_name,LDMS_MAX_CONFIG_STR_LEN-1, "Metric%s%sValues", cap_comp_assoc, cap_ovis_metric_name);

	    rc = add_metric(set_name, metric_name, key, diff_metric, table_name);
	    rc = createTable(ovis_metric_name, comp_assoc, table_name); 
	    if (rc != 0){
	      msglog("Cannot create table '%s'.\n",table_name);
	      return rc;
	    }
	  }
		break;
	case REMOVE_METRIC:
		sscanf(config_str, "remove_metric=%[^&]&%s", set_name, metric_name);
		rc = remove_metric(set_name, metric_name);
		break;
	case DATABASE_INFO:
	        rc = sscanf(config_str, "database_info=%[^&]&%[^&]&%[^&]&%s",
			    db_schema, db_host, username, password);
		if (rc != 3 && rc != 4){
		  msglog("Problems parsing database_info (%d)\n", rc);
		  return EINVAL;
		}
                rc = set_dbconfigs();
		break;
	default:
		msglog("Invalid config statement '%s'.\n", config_str);
		return EINVAL;
	}

	return rc;
}

static ldms_set_t get_set()
{
	return NULL;
}

static int init(const char *path)
{
  //FIXME: will we want the conn live all the time?

  if ((strlen(db_host) == 0) || (strlen(db_schema) == 0) 
      || (strlen(username) == 0)){
    msglog("Invalid parameters for database");
    return EINVAL;
  }

  conn = mysql_init(NULL);
  if (conn == NULL){
    msglog("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
    return EPERM;
  }

  if (!mysql_real_connect(conn, db_host, username,
			  password, db_schema, 0, NULL, 0)){
    msglog("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
    return EPERM;
  }

  return 0;
}

static int sample(void)
{
  struct fset *set;
  struct fmetric *met;

  if (conn == NULL){
    msglog("Error: no mysql connection\n");
    return EPERM;
  }

  LIST_FOREACH(set, &set_list, entry) {
    //two generation numbers -- meta data: for when a metric gets added or removed, data: for when *any* value gets updated.
    //only insert vals again if the data generation number has been incremented, however this does not guarentee that all the
    //data values are new.
    uint64_t datagn = ldms_get_data_gn(&set);
    if (datagn == set->lastDataGnInsert){ //does rollover exist? not doing less than just in case...
      continue;
    }
    set->lastDataGnInsert = datagn; //updating before the insert, in case we want to know what gn caused the problem. could move till after...

    LIST_FOREACH(met, &set->metric_list, entry) {
      int compId = met->key; // FIXME: i think right now the key is the component id.
      if (met->tableName == NULL){
	msglog("Error: no table for metric '%s'", ldms_get_metric_name(met->md));		  
	return EINVAL;
      }

      uint64_t val = ldms_get_u64(met->md);
      if (met->diffMetric){
	if (met->lastValSet){
	  uint64_t temp = val;
	  val -= met->lastVal; //FIXME: what about rollover?
	  met->lastVal = temp;
	} else {
	  //dont record it
	  met->lastVal = val;
	  met->lastValSet = TRUE;
	  continue; 
	}
      }

      char insertStatement[1024];
      long int level = lround( -log2( drand48()));
      snprintf(insertStatement,1023,"INSERT INTO %s VALUES( NULL, %d, %d, NULL, %ld )",
	       met->tableName, compId, (int)val, level); //FIXME: cast insert type?
      if (mysql_query(conn, insertStatement) != 0){
	printf("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
	exit(1);
      }
      
    }
  }
  return 0;
}

static void term(void)
{
  // FIXME: is this correct to be in term?
  mysql_close(conn);
}

static const char *usage(void)
{
	return  "    config mysqlinsert add=<set_name>\n"
		"        - Adds a metric set\n"
		"        set_name    The name of the metric set.\n"
		"    config mysqlinsert remove=<set_name>\n"
		"        - Removes a metric set. \n"
		"        set_name    The name of the metric set\n"
		"    config mysqlinsert add_metric=<set_name>&<metric_name>&key&diff_metric&<comp_assoc>&<ovis_metric_name>\n"
		"        - Add the specified metric to the set of values stored from the set\n"
		"        set_name    The name of the metric set.\n"
		"        metric_name The name of the metric.\n"
		"        key         An unique Id for the Metric. Typically the component_id.\n"
		"        diff_metric 1 for diff; 0 for not.\n"
		"        comp_assoc  ovis comp type short name (case matters).\n"
		"        ovis_metric_name  The ovis name for this metric. Table will be created\n"
                "                    using the ovis naming convention.\n"
                "                    The table will be created if it does not already exist.\n"
		"    config mysqlinsert remove_metric=<set_name>&<metric_name>\n"
		"        - Stop storing values for the specified metric\n"
		"        set_name    The name of the metric set.\n"
		"        metric_name The name of the metric.\n"
	        "    config mysqlinsert database_info=<db_schema>&<db_host>&<username>&<password>\n"
                "        - Database information\n"
                "        db_schema   The database name (default: mysqlinserttest).\n"
                "        db_host     The database hostname (default: localhost).\n"
                "        username   The database username (default: ovis).\n"
	        "        password   The database user's password (default: <none>).\n";
}

static struct ldms_plugin mysqlinsert_plugin = {
	.name = "mysqlinsert",
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
	msglog("mysqlinsert: plugin loaded\n");
	return &mysqlinsert_plugin;
}
