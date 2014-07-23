/*
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
#include <mysql/my_global.h>
#include <mysql/mysql.h>
#include <coll/idx.h>
#include "ldms.h"
#include "ldmsd.h"


#define TV_SEC_COL	0
#define TV_USEC_COL	1
#define GROUP_COL	2
#define VALUE_COL	3

/**
 * NOTE: One database for all. There is a separate mysql connection per table.
 * Increment your max_connections in your /etc/my.cnf accordingly.
 */

static char* db_host = NULL;
static char* db_schema = NULL;
static char* db_user = NULL;
static char* db_passwd = NULL;
static int createovistables = 0;

static idx_t store_idx;
static ldmsd_msg_log_f msglog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

/* for each metric */
struct mysql_metric_store {
	MYSQL *conn;
	char* tablename;
	char* cleansedmetricname;
	pthread_mutex_t lock;
	LIST_ENTRY(mysql_metric_store) entry;
};

struct mysql_store_instance {
	struct ldmsd_store *store;
	char *container;
	char *comp_type;
	void *ucontext;
	idx_t ms_idx;
	LIST_HEAD(ms_list, mysql_metric_store) ms_list;
	int metric_count;
	struct mysql_metric_store *ms[0];
};

pthread_mutex_t cfg_lock;

void cleanse_string(char *s)
{
	char *c;
	for (c = s; *c; c++) {
		if (!isalnum(*c)) {
			/* replace mysql non-allowed chars with _ */
			*c = '_';
		}
	}
}

static int init_conn(MYSQL **conn)
{
	*conn = NULL;

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

	/* passwd can be null.... */
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
	msglog("Config mysql store\n");

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

	/* will init the conn when each table is created */

	msglog("Config mysql store: successful \n");

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
	return
"  config name=store_mysql dbschema=<db_schema> dbuser=<dbuser> dbhost=<dbhost>\n"
"    - Set the dbinfo for the mysql storage for data.\n"
"	dbhost		The host of the database (check format)\n"
"	dbschema	The name of the database \n"
"	dbuser		The username of the database \n"
"	dbpasswd	The passwd for the user of the database (optional)\n"
"	ovistables	Create and populate supporting tables for use with\n"
"			OVIS (0/1) (optional. 0 default)\n"
		;
}

static ldmsd_store_handle_t
get_store(const char *container)
{
	pthread_mutex_lock(&cfg_lock);

	ldmsd_store_handle_t sh = idx_find(store_idx, (void*)container,
					   strlen(container));
	pthread_mutex_unlock(&cfg_lock);
	return sh;
}

static void *get_ucontext(ldmsd_store_handle_t sh)
{
	struct mysql_store_instance *si = sh;
	return si->ucontext;
}

static int createTable(MYSQL* conn, char* tablename)
{
	/* will create a table (but not supporting OVIS tables)
NOTE: this is locked from the surrounding function */

	int mysqlerrx;
	char query1[4096];
	if (conn == NULL)
		return EPERM;

	/* FIXME: we have no way of knowing the data storage type.
	   for now store everything as uint64_t */
	char* mysqlstoragestring = "BIGINT UNSIGNED";


	/* create table if required */
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

	mysqlerrx = mysql_query(conn, query1);
	if (mysqlerrx != 0){
		msglog("Cannot query to create table '%s'. Error: %d\n",
				tablename, mysqlerrx);
		return mysqlerrx;
	}


	return 0;
}

static int create_OVIS_supporting_tables(MYSQL* conn, char *tableName,
		char* compAssoc, char* ovisMetricName)
{
	/**
	 * 1) will create MetricValueTypes and MetricValueTableIndex if necessary
	 * 2) will populate those *fully* if ComponentTypes exists and is populated
	 * or *incompletely* if ComponentTypes does not exist -- that is, will just put -1
	 * for all component types. In this latter case, the user will have to
	 * correct the CompType col upon putting these tables into an ovdb output.
	 **/

	/**
	 * NOTE: this is still locked from the surrounding function. Want this to
	 * occur directly after the above if /necessary. Not combining them for
	 * clarity/schema_change later.
	 **/

	/* FIXME: later should get these from the created table type */
	char* ovisstoragestring = "uint64_t";
	MYSQL_RES *result;
	MYSQL_ROW row;
	int ctype = -1;
	int metrictype = -1;
	int num_fields;
	char query1[4096];

	snprintf(query1, 4095,
			"SELECT CompType From ComponentTypes WHERE ShortName ='%s'",
			compAssoc); /* FIXME: check for cap */
	if (mysql_query(conn, query1) == 0){
		result = mysql_store_result(conn);
		num_fields = mysql_num_fields(result);
		if ((num_fields == 1) && (mysql_num_rows(result) == 1)){
			while((row = mysql_fetch_row(result))){
				ctype = atoi(row[0]);
				break;
			}
		}
		if (result) mysql_free_result(result);
	}

	/* if we dont have the table or the component type, use default value (-1) */

	/* create the MetricValueType Table if necessary */
	char* query2 =
		"CREATE TABLE IF NOT EXISTS `MetricValueTypes` (`ValueType` int(11) NOT NULL AUTO_INCREMENT,  `Name` tinytext NOT NULL,  `Units` tinytext NOT NULL,  `Storage` tinytext NOT NULL,  `Constant` int(32) NOT NULL DEFAULT '0',  PRIMARY KEY (`ValueType`))";
	if (mysql_query(conn, query2) != 0){
		msglog("Err: Cannot check for the MetricValueTypeTable\n");
		return -1;
	}

	/* create and retain the MetricValueType */
	snprintf(query1, 4095,
			"SELECT ValueType from MetricValueTypes WHERE Name='%s' AND Units='1' AND Constant=0 AND Storage='%s'",
			ovisMetricName, ovisstoragestring);
	if (mysql_query(conn, query1) != 0){
		msglog("Err: Cannot query for MetricValueType for '%s'\n",
				ovisMetricName);
		return -1;
	}
	result = mysql_store_result(conn);

	num_fields = mysql_num_fields(result);
	if ((num_fields == 1) && (mysql_num_rows(result) == 1)){
		while((row = mysql_fetch_row(result))){
			metrictype = atoi(row[0]);
			/* FIXME: check that if it is already in there
			   that it is not in with the wrong type.... */
			mysql_free_result(result);
			break;
		}
	} else {
		mysql_free_result(result);
		snprintf(query1, 4095,
				"INSERT INTO MetricValueTypes(Name, Units, Storage, Constant) VALUES ('%s', '1', '%s', 0)",
				ovisMetricName, ovisstoragestring);
		if (mysql_query(conn, query1) !=0){
			msglog("Cannot insert MetricValueType for '%s'\n",
					ovisMetricName);
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
		num_fields = mysql_num_fields(result);
		if ((num_fields != 1) || (mysql_num_rows(result) != 1)){
			msglog("No MetricValueType for '%s'\n",
					ovisMetricName);
			mysql_free_result(result);
			return -1;
		}
		while((row = mysql_fetch_row(result))){
			metrictype = atoi(row[0]);
			mysql_free_result(result);
		}
	}

	/* create the MetricValueTableIndex if necessary */
	query2 =  "CREATE TABLE IF NOT EXISTS `MetricValueTableIndex` (`TableId` int(11) NOT NULL AUTO_INCREMENT,  `TableName` tinytext NOT NULL,  `CompType` int(32) NOT NULL,  `ValueType` int(32) NOT NULL,  `MinDeltaTime` float NOT NULL DEFAULT '1',  PRIMARY KEY (`TableId`))";
	if (mysql_query(conn,query2) != 0){
		msglog("Err: Cannot check for the MetricValueTableIndex\n");
		return -1;
	}

	/* create the TableId if necessary */
	snprintf(query1, 4095,
			"SELECT TableId from MetricValueTableIndex WHERE TableName='%s' AND CompType=%d AND ValueType=%d",
			tableName, ctype, metrictype);
	if (mysql_query(conn, query1) != 0){
		msglog("Cannot query for table id for '%s'\n", tableName);
		return -1;
	}
	result = mysql_store_result(conn);
	num_fields = mysql_num_fields(result);
	if ((num_fields != 1) || (mysql_num_rows(result) != 1)){
		mysql_free_result(result);
		snprintf(query1, 4095,
				"INSERT INTO MetricValueTableIndex ( TableName, CompType, ValueType, MinDeltaTime ) VALUES ('%s', %d, %d, 0)",
				tableName, ctype, metrictype);
		if (mysql_query(conn, query1) != 0){
			msglog("Cannot insert into MetricValueTableIndex for tableName '%s'\n",
					tableName);
			return -1;
		}
	} else {
		mysql_free_result(result);
	}

	return 0;
}

int
new_metric_store(struct mysql_metric_store *msm, const char *comp_type,
		 const char *metric_name)
{
	char* cleansedmetricname;
	char* tempmetricname;
	char* cleansedcompname;
	int rc;

	/* Create a table for this comptype and metric name
	   if one does not already exist */
	int sz;
	int i;

	pthread_mutex_init(&msm->lock, NULL);

	/* TABLENAME - Assuming OVIS form for tablename, columns */
	cleansedmetricname = strdup(metric_name);
	if (!cleansedmetricname)
		goto err1;

	cleanse_string(cleansedmetricname);
	msm->cleansedmetricname = cleansedmetricname;

	tempmetricname = strdup(cleansedmetricname);
	if (!tempmetricname){
		goto err2;
	}

	tempmetricname[0] = toupper(tempmetricname[0]);
	cleansedcompname = strdup(comp_type);
	if (!cleansedcompname){
		goto err3;
	}

	cleanse_string(cleansedcompname);
	cleansedcompname[0] = toupper(cleansedcompname[0]);

	sz = strlen(tempmetricname) +
		strlen(cleansedcompname) +
		strlen("MetricValues");

	msm->tablename = (char*)malloc((sz+5)*sizeof(char));
	if (!msm->tablename){
		goto err4;
	}
	snprintf(msm->tablename, (sz+5),"Metric%s%sValues",
			cleansedcompname,
			tempmetricname);
	/* will need to retain the compname and metricname temporarily
	   if need to build the OVIS supporting tables */

	rc = init_conn(&(msm->conn));
	if (rc != 0){
		goto err5;
	}

	rc = createTable(msm->conn, msm->tablename);
	if (rc != 0){
		goto err6;
	}

	if (createovistables){
		rc = create_OVIS_supporting_tables(msm->conn,
				msm->tablename,
				cleansedcompname,
				tempmetricname);
		if (rc != 0){
			goto err6;
		}
	}

	if (tempmetricname)
		free(tempmetricname);

	if (cleansedcompname)
		free(cleansedcompname);

	return 0;
err6:
	mysql_close(msm->conn);
err5:
	free(msm->tablename);
err4:
	free(cleansedcompname);
err3:
	free(tempmetricname);
err2:
	free(msm->cleansedmetricname);
err1:
	return -1;
}

static ldmsd_store_handle_t
new_store(struct ldmsd_store *s, const char *comp_type, const char *container,
	  struct ldmsd_store_metric_index_list *metric_list, void *ucontext)
{

	msglog("New store mysql\n");
	pthread_mutex_lock(&cfg_lock);

	struct mysql_store_instance *si;
	struct mysql_metric_store *ms;
	struct ldmsd_store_metric_index *x;
	int metric_count;
	int rc = 0;

	si = idx_find(store_idx, (void*)container, strlen(container));
	if (si)
		goto out;

	metric_count = 0;
	LIST_FOREACH(x, metric_list, entry) {
		metric_count++;
	}

	si = calloc(1, sizeof(*si) +
		    metric_count*sizeof(struct mysql_metric_store));
	if (!si)
		goto out;
	si->ucontext = ucontext;
	si->store = s;
	si->metric_count = metric_count;

	si->ms_idx = idx_create();
	if (!si->ms_idx)
		goto err1;

	si->container = strdup(container);
	if (!si->container)
		goto err2;
	si->comp_type = strdup(comp_type);
	if (!si->comp_type)
		goto err3;

	/* for each metric, do msm initialization */
	int i = 0;
	char buff[128];
	char *name;
	LIST_FOREACH(x, metric_list, entry) {
		name = strchr(x->name, '#');
		if (name) {
			int len = name - x->name;
			name = strncpy(buff, x->name, len);
			name[len] = 0;
		} else {
			name = x->name;
		}
		ms = idx_find(si->ms_idx, name, strlen(name));
		if (ms) {
			si->ms[i++] = ms;
			continue;
		}
		/* Create ms if not exist */
		ms = calloc(1, sizeof(*ms));
		if (!ms)
			goto err4;
		rc = new_metric_store(ms, si->comp_type, name);
		if (rc)
			goto err4;
		idx_add(si->ms_idx, name, strlen(name), ms);
		LIST_INSERT_HEAD(&si->ms_list, ms, entry);
		si->ms[i++] = ms;
	}

	idx_add(store_idx, (void*)container, strlen(container), si);
	msglog("New store mysql successful\n");

	goto out;

err4:
	while(ms = LIST_FIRST(&si->ms_list)) {
		LIST_REMOVE(ms, entry);
		if (ms->conn) {
			mysql_close(ms->conn);
			free(ms->tablename);
			free(ms->cleansedmetricname);
		}
		free(ms);
	}
err3:
	free(si->container);
err2:
	idx_destroy(si->ms_idx);
err1:
	free(si);
	si = NULL;
out:
	pthread_mutex_unlock(&cfg_lock);
	return si;
}


static int
store(ldmsd_store_handle_t sh, ldms_set_t set, ldms_mvec_t mvec, int flags)
{
	/* NOTE: ldmsd_store invokes the lock on the whole si */

	struct mysql_store_instance *si;

	si = sh;
	if (!sh)
		return EINVAL;

	int i;
	const struct ldms_timestamp *ts = ldms_get_timestamp(set);

	for (i=0; i<mvec->count; i++) {
		struct mysql_metric_store *msm = si->ms[i];
		uint64_t comp_id = ldms_get_user_data(mvec->v[i]);
		uint64_t val = ldms_get_u64(mvec->v[i]);
		long int level = lround( -log2( drand48())); //residual OVIS-ism
		char insertStatement[1024];
		int mysqlerrx;

		if (msm->conn == NULL){
			msglog("Cannot insert value for <%s>: Connection"
					" to mysql is closed\n",
					msm->tablename);
			return EPERM;
		}

		snprintf(insertStatement,1023,
			 "INSERT INTO %s VALUES( NULL, %"PRIu64", %"PRIu64
			 ", FROM_UNIXTIME(%d), %ld )",
			 msm->tablename, comp_id,
			 val, ts->sec, level);

		pthread_mutex_lock(&msm->lock);

		mysqlerrx = mysql_query(msm->conn, insertStatement);
		if (mysqlerrx != 0) {
			msglog("Failed to perform query <%s>. Error: %d\n",
					mysqlerrx);
			return mysqlerrx;
		}

		pthread_mutex_unlock(&msm->lock);
	}

	return 0;
}


static int flush_store(ldmsd_store_handle_t sh)
{
	/* NOTE - later change this so that data is queued up in store */
	return 0;
}


/**
 * If we have closed the store, which closes the conns, reopen them.
 */
static int refresh_store(ldmsd_store_handle_t sh)
{

	struct mysql_store_instance *si;
	struct mysql_metric_store *msm;
	int i;

	si = sh;
	if (!si)
		return EINVAL;

	LIST_FOREACH(msm, &si->ms_list, entry) {
		int rc;
		pthread_mutex_lock(&msm->lock);
		if (msm->conn){
			mysql_close(msm->conn);
			msm->conn = NULL;
		}

		rc = init_conn(&(msm->conn));
		pthread_mutex_unlock(&msm->lock);
		if (rc != 0){
			return EPERM;
		}
	}

	return 0;
}


static void destroy_store(ldmsd_store_handle_t sh)
{
	pthread_mutex_lock(&cfg_lock);
	struct mysql_metric_store *msm;

	int i;
	struct mysql_store_instance *si = sh;
	if (!si)
		return;

	msglog("Destroying store_mysql for <%s>\n", si->container);

	while (msm = LIST_FIRST(&si->ms_list)) {
		pthread_mutex_lock(&msm->lock);
		LIST_REMOVE(msm, entry);
		if (msm->conn)
			mysql_close(msm->conn);
		if (msm->tablename)
			free(msm->tablename);
		if (msm->cleansedmetricname)
			free(msm->cleansedmetricname);
		pthread_mutex_unlock(&msm->lock);
		pthread_mutex_destroy(&msm->lock);
		free(msm);
	}

	if (si->comp_type)
		free(si->comp_type);
	idx_delete(store_idx, (void*)si->container, strlen(si->container));
	if (si->container)
		free(si->container);
	idx_destroy(si->ms_idx);
	free(si);

	pthread_mutex_unlock(&cfg_lock);
}

/**
 * Closing the store, closes the mysql conns, but does not destroy the store.
 * This is because the filehandles are kept in the params of the metrics, so they
 * could not be reestablished if destroyed without calling add_metric again.
 */
static void close_store(ldmsd_store_handle_t sh)
{
	/* Currently close is close and destroy */
	destroy_store(sh);
}

static struct ldmsd_store store_mysql = {
	.base = {
		.name = "mysql",
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
	return &store_mysql.base;
}

static void __attribute__ ((constructor)) store_mysql_init();
static void store_mysql_init()
{
	store_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_mysql_fini(void);
static void store_mysql_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(store_idx);
}
