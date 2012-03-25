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

//FIXME: how to get these values into the plugin?
#define DEFAULT_DB_SOCKET "/var/lib/mysql/mysql.sock"
#define DEFAULT_USER "ovis"
#define DEFAULT_PASS ""
#define DEFAULT_DB "junk2"
#define DEFAULT_DBHOST "localhost"

#define LOGFILE "/var/log/flat_file.log"

struct fmetric {
	ldms_metric_t md;
	uint64_t key;		/* component id or whatever else you like */
	LIST_ENTRY(fmetric) entry;
};

struct fset {
	ldms_set_t sd;
	char *file_path;
	FILE *file;
	LIST_HEAD(fmetric_list,  fmetric) metric_list;
	LIST_ENTRY(fset) entry;
};

LIST_HEAD(fset_list, fset) set_list;

static ldmsd_msg_log_f msglog;

ldms_metric_t compid_metric_handle;

char file_path[LDMS_MAX_CONFIG_STR_LEN];
char set_name[LDMS_MAX_CONFIG_STR_LEN];
char metric_name[LDMS_MAX_CONFIG_STR_LEN];

MYSQL *conn;

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

static int add_set(char *set_name, char *file_path)
{
	FILE *file;
	ldms_set_t sd;
	struct fset *set = get_fset(set_name);
	if (set)
		return EEXIST;

	sd = ldms_get_set(set_name);
	if (!sd)
		return ENOENT;

	file = fopen(file_path, "a");
	if (!file)
		return errno;

	set = calloc(1, sizeof *set);
	if (!set) {
		fclose(file);
		return ENOMEM;
	}
	set->sd = sd;
	set->file = file;
	set->file_path = strdup(file_path);
	LIST_INSERT_HEAD(&set_list, set, entry);
	return 0;
}

static int remove_set(char *set_name)
{
	struct fset *set = get_fset(set_name);
	if (!set)
		return ENOENT;
	LIST_REMOVE(set, entry);
	free(set->file_path);
	fclose(set->file);
	return 0;
}

static int add_metric(char *set_name, char *metric_name, uint64_t key)
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

	metric->md = md;
	metric->key = key;
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
	return 0;
}

static int config(char *config_str)
{
	enum {
		ADD_SET,
		ADD_METRIC,
		REMOVE_SET,
		REMOVE_METRIC,
                COMPONENT_ID,
	} action;
	int rc;
	uint64_t key;

	if (0 == strncmp(config_str, "add_metric", 10))
		action = ADD_METRIC;
	else if (0 == strncmp(config_str, "remove_metric", 32))
		action = REMOVE_METRIC;
	else if (0 == strncmp(config_str, "add", 3))
		action = ADD_SET;
	else if (0 == strncmp(config_str, "remove", 6))
		action = REMOVE_SET;
	else {
		msglog("flatfile: Invalid configuration string '%s'\n",
		       config_str);
		return EINVAL;
	}
	switch (action) {
	case ADD_SET:
		sscanf(config_str, "add=%[^&]&%s", set_name, file_path);
		rc = add_set(set_name, file_path);
		break;
	case REMOVE_SET:
		sscanf(config_str, "remove=%s", set_name);
		rc = remove_set(set_name);
		break;
	case ADD_METRIC:
		sscanf(config_str, "add_metric=%[^&]&%[^&]&%"PRIu64, set_name, metric_name, &key);
		rc = add_metric(set_name, metric_name, key);
		break;
	case REMOVE_METRIC:
		sscanf(config_str, "remove_metric=%[^&]&%s", set_name, metric_name);
		rc = remove_metric(set_name, metric_name);
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
  //FIXME: how to get the values in?
  //FIXME: will we want the conn live all the time?
  conn = mysql_init(NULL);
  if (conn == NULL){
    printf ("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
    exit(1);
  }

  if (!mysql_real_connect(conn, DEFAULT_DBHOST, DEFAULT_USER,
			  DEFAULT_PASS, DEFAULT_DB, 0, NULL, 0)){
    printf("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
    exit(1);
  }
	return 0;
}

static char data_str[2048];
static int sample(void)
{
	struct fset *set;
	struct fmetric *met;
	//	int rc;
	LIST_FOREACH(set, &set_list, entry) {
	  //check here for the generation number and see if it changes if you want. there is a function to get it.
	  //ldms_get_data_gn. metadata number changes when a new metric gets added or removed. data number
	  //with any change
		LIST_FOREACH(met, &set->metric_list, entry) {
			/* timestamp metric_name metric_value */
			sprintf(data_str, 
				"%"PRIu64" %"PRIu64" %"PRIu64"\n",
				(uint64_t)time(NULL), met->key,
			ldms_get_u64(met->md));
			// msglog(data_str); //this logs the sample to the log file 
			//			rc = fprintf(set->file, data_str);
			//			if (rc <= 0)
			//				msglog("Error %d writing '%s' to '%s'\n", rc, data_str, set->file_path);
			//			fflush(set->file); tail the flat file if you want to see it
			//FIXME: will need to do remote assoc
			//FIXME: will need to put in supporting tables (e.g., MetricValueTableIndex)
			//FIXME: will want to support diffs
			
			//FIXME: will need to get these values
			char tableName[100];
			int compId = 1;
			char compType[10] = "Node";
			char metricName[40] = "WHAT_IS_THE_NAME"; //is the key the id?
			snprintf(tableName,100,"Metric%s%sValues",compType,metricName);

			//FIXME: avoid doing this each time
			char createDefinition[4096];
			snprintf(createDefinition, 4095,"%s%s%s","CREATE TABLE IF NOT EXISTS ", tableName, "(`TableKey`  INT NOT NULL AUTO_INCREMENT NOT NULL, `CompId`  INT(32) NOT NULL, `Value`  INT(32) NOT NULL, `Time`  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, `Level`  INT(32) NOT NULL DEFAULT 0, PRIMARY KEY  (`TableKey` ), INDEX MetricCoreCpu_RSSValues_Time (`Time` ), INDEX MetricNodeJunkValues_Level (`CompId` ,`Level` ,`Time` ))");

			if (!mysql_query(conn, createDefinition )){
			  //FIXME: why do these print when the mysql statement works?
			  //    printf("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
			  //    exit(1);
			}

			int val = ldms_get_u64(met->md);
			int level = 1; //FIXME: make this rand
			char insertStatement[1024];
			snprintf(insertStatement,1023,"INSERT INTO %s VALUES(NULL, %d, %d, NULL, %d)", tableName, compId, val, level);
			if (!mysql_query(conn, insertStatement )){
			  //    printf("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
			  //    exit(1);
			}

		}
	}
	return 0;
}

static void term(void)
{
  // FIXME: is this correct?
  mysql_close(conn);
}

static const char *usage(void)
{
	return  "    config flatfile add=<set_name>&<path>\n"
		"        - Adds a metric set and associated file to contain sampled values.\n"
		"        set_name    The name of the metric set\n"
		"        path        Path to a file that will contain the sampled metrics.\n"
		"                    The file will be created if it does not already exist\n"
		"                    and appeneded to if it does.\n"
		"    config flatfile remove=<set_name>\n"
		"        - Removes a metric set. The file is closed, but not destroyed\n"
		"        set_name    The name of the metric set\n"
		"    config flatfile add_metric=<set_name>&<metric_name>&key\n"
		"        - Add the specified metric to the set of values stored from the set\n"
		"        set_name    The name of the metric set.\n"
		"        metric_name The name of the metric.\n"
		"        key         An unique Id for the Metric. Typically the component_id.\n"
		"    config flatfile remove_metric=<set_name>&<metric_name>\n"
		"        - Stop storing values for the specified metric\n"
		"        set_name    The name of the metric set.\n"
		"        metric_name The name of the metric.\n";
}

static struct ldms_plugin flat_file_plugin = {
	.name = "flat_file",
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
	msglog("flat_file: plugin loaded\n");
	return &flat_file_plugin;
}
