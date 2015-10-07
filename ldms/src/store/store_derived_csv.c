/**
 * Copyright (c) 2014-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014-2015 Sandia Corporation. All rights reserved.
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
#include <unistd.h>
#include <coll/idx.h>
#include "ldms.h"
#include "ldmsd.h"
#include "store_common.h"

#define TV_SEC_COL    0
#define TV_USEC_COL    1
#define GROUP_COL    2
#define VALUE_COL    3


#define MAX_ROLLOVER_STORE_KEYS 20
#define STORE_DERIVED_NAME_MAX 256
#define STORE_DERIVED_LINE_MAX 256
#define STORE_DERIVED_METRIC_MAX 500

static pthread_t rothread;
static idx_t store_idx;
static char* storekeys[MAX_ROLLOVER_STORE_KEYS]; //TODO: make this variable size
static int nstorekeys = 0;
static char *root_path;
static int altheader;
static char* derivedconf = NULL;
static int ageusec = -1;
static int rollover;
static int rolltype;
/** ROLLTYPES documents rolltype and is used in help output. */
#define ROLLTYPES \
"                     1: wake approximately every rollover seconds and roll.\n" \
"                     2: wake daily at rollover seconds after midnight (>=0) and roll.\n" \
"                     3: roll after approximately rollover records are written.\n" \
"                     4: roll after approximately rollover bytes are written.\n"
#define MAXROLLTYPE 4
#define MINROLLTYPE 1
#define DEFAULT_ROLLTYPE -1
/** minimum rollover for type 1;
    rolltype==1 and rollover < MIN_ROLL_1 -> rollover = MIN_ROLL_1
    also used for minimum sleep time for type 2;
    rolltype==2 and rollover results in sleep < MIN_ROLL_SLEEPTIME -> skip this roll and do it the next day */
#define MIN_ROLL_1 10
/** minimum rollover for type 3;
    rolltype==3 and rollover < MIN_ROLL_RECORDS -> rollover = MIN_ROLL_RECORDS */
#define MIN_ROLL_RECORDS 3
/** minimum rollover for type 4;
    rolltype==4 and rollover < MIN_ROLL_BYTES -> rollover = MIN_ROLL_BYTES */
#define MIN_ROLL_BYTES 1024
/** Interval to check for passing the record or byte count limits */
#define ROLL_LIMIT_INTERVAL 60
static ldmsd_msg_log_f msglog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

#define LOGFILE "/var/log/store_derived_csv.log"

/**
 * store_derived - specify some metrics to be stored, either RAW or RATE.
 * This can be used in conjunction with store_csv to have 1 file of all RAW
 * go to one location and 1 file of a subset of RAW and some RATES go to
 * another.
 *
 * Notes:
 * - format of the configuration file is comma separated
 *   New in v3: metrics now matched on both name and schema. This is a change
 *   in the configuration file then as well.
 * - rollover (determined by a neg value) and neg values - both return 0
 * - New in v3: if time diff is not positive, always write out something and flag.
 *   if its RAW data, write the val. if its RATE data, write zero
 *   (in v2 was: two timestamps for the same component with dt = 0 wont writeout. Presumably
 *   this shouldnt happen.)
 * - New in v3: redo order of RATE calculation to keep precision.
 * - STORE_DERIVED_METRIC_MAX - is fixed value.
 * - there is no function to iterate thru an idx. Right now, that memory is
 *   lost in destroy_store.
 * - if a host goes down and comes back up, then may have a long time range
 *   between points. currently, this is still calculated, but can be flagged
 *   with ageout. Currently this is global, not per collector type
 * - New in v3: no more idpos. ProducerName instead.
 * - New in v3: agesec -> ageusec.
 *
 * TODO:
 * - Currently only handles uint64_t types
 * - Currently only printsout uint64_t values. (cast)
 */

typedef enum {
	RAW = 0,
	RATE
} der_t;

struct derived_data { // rawname/dername are good candidates for dstrings.
	char rawname[STORE_DERIVED_NAME_MAX]; //can lose this after the deridx is set....
	char dername[STORE_DERIVED_NAME_MAX]; //should we bother to keep this???
	der_t dertype;
	double multiplier;
	int deridx;
};

struct setdatapoint {
	struct ldms_timestamp *ts;
	uint64_t* datavals; //subset of the vals we need. NOTE: mvec has a set assoc that we dont want.
	//TODO: making these all uint64_t. this will have to support everything like the metric does.
	//NOTE/TODO: not keeping or writing the user data
};

/**
 * If this is going to have the last dp, then a store_handle can only be for a
 * particular sampler/schema (not multiple samplers, nor the same sampler, but multiple schema
 */
struct csv_derived_store_handle {
	struct ldmsd_store *store;
	char *path;
	FILE *file;
	FILE *headerfile;
	struct derived_data der[STORE_DERIVED_METRIC_MAX]; //TODO: dynamic.
	int numder;
	idx_t sets_idx; /* to keep track of sets involved to do the diff */
	int numsets;
	printheader_t printheader;
	int parseconfig;
	char *store_key; /* this is the container+schema */
	char *schema; /* in v3 need to keep this for the comparison with the conf file */
	pthread_mutex_t lock;
	void *ucontext;
	int64_t store_count;
	int64_t byte_count;
};

static pthread_mutex_t cfg_lock;

static char* allocStoreKey(const char* container, const char* schema){

  if ((container == NULL) || (schema == NULL) ||
      (strlen(container) == 0) ||
      (strlen(schema) == 0)){
    msglog(LDMSD_LERROR, "%s: container or schema null or empty. cannot create key\n",
	   __FILE__);
    return NULL;
  }

  size_t pathlen = strlen(schema) + strlen(container) + 8;
  char* path = malloc(pathlen);
  if (!path)
    return NULL;

  snprintf(path, pathlen, "%s/%s", container, schema);
  return path;
}

/* Time-based rolltypes will always roll the files when this
function is called.
Volume-based rolltypes must check and shortcircuit within this
function.
*/
static int handleRollover(){
	//get the config lock
	//for every handle we have, do the rollover

	int i;
	struct csv_derived_store_handle *s_handle;

	pthread_mutex_lock(&cfg_lock);

	time_t appx = time(NULL);

	for (i = 0; i < nstorekeys; i++){
		if (storekeys[i] != NULL){
			s_handle = idx_find(store_idx, (void *)storekeys[i], strlen(storekeys[i]));
			if (s_handle){
				FILE* nhfp = NULL;
				FILE* nfp = NULL;
				char tmp_path[PATH_MAX];
				char tmp_headerpath[PATH_MAX];

				//if we've got here then we've called new_store, but it might be closed
				pthread_mutex_lock(&s_handle->lock);
				switch (rolltype) {
				case 1:
					break;
				case 2:
					break;
				case 3:
					if (s_handle->store_count < rollover)  {
						pthread_mutex_unlock(&s_handle->lock);
						continue;
					} else {
						s_handle->store_count = 0;
					}
					break;
				case 4:
					if (s_handle->byte_count < rollover) {
						pthread_mutex_unlock(&s_handle->lock);
						continue;
					} else {
						s_handle->byte_count = 0;
					}
					break;
				default:
					msglog(LDMSD_LDEBUG, "%s: Error: unexpected rolltype in store(%d)\n",
					       __FILE__, rolltype);
					break;
				}

				if (s_handle->file)
					fflush(s_handle->file);
				if (s_handle->headerfile)
					fflush(s_handle->headerfile);

				//re name: if got here then rollover requested
				snprintf(tmp_path, PATH_MAX, "%s.%d",
					 s_handle->path, (int) appx);
				nfp = fopen(tmp_path, "a+");
				if (!nfp){
					//we cant open the new file, skip
					msglog(LDMSD_LERROR, "%s: Error: cannot open file <%s>\n",
					       __FILE__, tmp_path);
					pthread_mutex_unlock(&s_handle->lock);
					continue;
				}
				if (altheader){
					//re name: if got here then rollover requested
					snprintf(tmp_headerpath, PATH_MAX,
						 "%s.HEADER.%d",
						 s_handle->path, (int)appx);
					/* truncate a separate headerfile if it exists.
					* NOTE: do we still want to do this? */
					nhfp = fopen(tmp_headerpath, "w");
					if (!nhfp){
						fclose(nfp);
						msglog(LDMSD_LERROR, "%s: Error: cannot open file <%s>\n",
						       __FILE__, tmp_headerpath);
					}
				} else {
					nhfp = fopen(tmp_path, "a+");
					if (!nhfp){
						fclose(nfp);
						msglog(LDMSD_LDEBUG, "%s: Error: cannot open file <%s>\n",
						       __FILE__, tmp_path);
					}
				}
				if (!nhfp) {
					pthread_mutex_unlock(&s_handle->lock);
					continue;
				}

				//close and swap
				if (s_handle->file)
					fclose(s_handle->file);
				if (s_handle->headerfile)
					fclose(s_handle->headerfile);
				s_handle->file = nfp;
				s_handle->headerfile = nhfp;
				s_handle->printheader = DO_PRINT_HEADER;
				pthread_mutex_unlock(&s_handle->lock);
			}
		}
	}

	pthread_mutex_unlock(&cfg_lock);

	return 0;

}

static void* rolloverThreadInit(void* m){
	while(1){
		int tsleep;
		switch (rolltype) {
		case 1:
		  tsleep = (rollover < MIN_ROLL_1) ? MIN_ROLL_1 : rollover;
		  break;
		case 2: {
		  time_t rawtime;
		  struct tm *info;

		  time( &rawtime );
		  info = localtime( &rawtime );
		  int secSinceMidnight = info->tm_hour*3600+info->tm_min*60+info->tm_sec;
		  tsleep = 86400 - secSinceMidnight + rollover;
		  if (tsleep < MIN_ROLL_1){
		    /* if we just did a roll then skip this one */
		    tsleep+=86400;
		  }
		}
		  break;
		case 3:
		  if (rollover < MIN_ROLL_RECORDS)
		    rollover = MIN_ROLL_RECORDS;
		  tsleep = ROLL_LIMIT_INTERVAL;
		  break;
		case 4:
		  if (rollover < MIN_ROLL_BYTES)
		    rollover = MIN_ROLL_BYTES;
		  tsleep = ROLL_LIMIT_INTERVAL;
		  break;
		default:
		  break;
		}
		sleep(tsleep);
		handleRollover();
	}

	return NULL;
}


/**
 * \brief Config for derived vars
 */
static int derivedConfig(char* fname, struct csv_derived_store_handle *s_handle){
	//read the file and keep the metrics names until its time to print the headers

	char lbuf[STORE_DERIVED_LINE_MAX];
	char metric_name[STORE_DERIVED_NAME_MAX];
	char schema_name[STORE_DERIVED_NAME_MAX];
	int tval;
	double mval;
	char* s;
	int rc, rcl;

	//TODO: for now will read this in for every option (e.g., different base set for store)
	//Dont yet have a way to determine which of the handles a certain metric will be associated with

	FILE* fp = fopen(fname, "r");
	if (!fp) {
		msglog(LDMSD_LERROR,"%s: Cannot open config file <%s>\n",
		       __FILE__, fname);
		return EINVAL;
	}
	s_handle->parseconfig = 0;
	s_handle->numder = 0;

	rc = 0;
	rcl = 0;
	do {

		//TODO: TOO many metrics. dynamically alloc
		if (s_handle->numder == STORE_DERIVED_METRIC_MAX) {
			msglog(LDMSD_LERROR,"%s: Too many metrics <%s>\n",
			       __FILE__, fname);
			rc = EINVAL;
			break;
		}

		s = fgets(lbuf, sizeof(lbuf), fp);
		if (!s)
			break;
//		printf("Read <%s>\n", lbuf);
		rcl = sscanf(lbuf, "%[^,] , %[^,] , %d , %lf", metric_name, schema_name, &tval, &mval);
//		printf("Name <%s> val <%d> mult <%lf>\n", metric_name, tval, mval);
		if ((strlen(metric_name) > 0) && (metric_name[0] == '#')){
		// hashed lines are comments (means metric name cannot start with #)
			msglog(LDMSD_LDEBUG,"%s: Comment in derived config file <%s>. Skipping\n",
			       __FILE__, lbuf);
			continue;
		}
		if (rcl != 4) {
			msglog(LDMSD_LERROR,"%s: Bad format in derived config file <%s> rc=%d. Skipping\n",
			       __FILE__, lbuf, rcl);
			continue;
		}
		if (strlen(schema_name) == 0){
			msglog(LDMSD_LERROR,"%s: Bad schema in derived config file <%s> <%d>. Skipping\n",
			       __FILE__, lbuf, tval);
			continue;
		}
		if ((tval < 0) || (tval > 1)) {
			msglog(LDMSD_LERROR,"%s: Bad type in derived config file <%s> <%d>. Skipping\n",
			       __FILE__, lbuf, tval);
			continue;
		}

		//only keep this item if the schema matches
		if (strcmp(s_handle->schema, schema_name) != 0) {
			msglog(LDMSD_LDEBUG, "store derived <%s> rejecting schema <%s>\n",
			       s_handle->store_key, schema_name);
			continue;
		}

		msglog(LDMSD_LDEBUG, "store derived <%s> accepting metric <%s> schema <%s>\n",
		       s_handle->store_key, metric_name, schema_name);

		s_handle->der[s_handle->numder].multiplier = mval;
		s_handle->der[s_handle->numder].dertype = tval;
		s_handle->der[s_handle->numder].deridx = -1; //Don't have this yet
		snprintf(s_handle->der[s_handle->numder].rawname,
			 STORE_DERIVED_NAME_MAX, "%s",
			 metric_name);
		if (tval == RAW)
			snprintf(s_handle->der[s_handle->numder].dername,
				 STORE_DERIVED_NAME_MAX,
				 "%s (x %.2e)",
				 s_handle->der[s_handle->numder].rawname,
				 s_handle->der[s_handle->numder].multiplier);
		else
			snprintf(s_handle->der[s_handle->numder].dername,
				 STORE_DERIVED_NAME_MAX,
				 "Rate_%s (x %.2e)",
				 s_handle->der[s_handle->numder].rawname,
				 s_handle->der[s_handle->numder].multiplier);
		s_handle->numder++;
	} while (s);

	if (fp)
		fclose(fp);
	fp = NULL;

	return rc;
}

/**
 * \brief Configuration
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value = NULL;
	char *dervalue = NULL;
	char *altvalue = NULL;
	char *ivalue = NULL;
	char *rvalue = NULL;
	int roll = -1;
	int rollmethod = DEFAULT_ROLLTYPE;

	pthread_mutex_lock(&cfg_lock);

	value = av_value(avl, "path");
	if (!value){
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	altvalue = av_value(avl, "altheader");

	rvalue = av_value(avl, "rollover");
	if (rvalue){
		roll = atoi(rvalue);
		if (roll < 0){
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}
	}

	rvalue = av_value(avl, "rolltype");
	if (rvalue){
		if (roll < 0) /* rolltype not valid without rollover also */
			return EINVAL;
		rollmethod = atoi(rvalue);
		if (rollmethod < MINROLLTYPE)
			return EINVAL;
		if (rollmethod > MAXROLLTYPE)
			return EINVAL;
	}

	//TODO: make this variable per schema or similar.
	ivalue = av_value(avl, "ageusec");
	if (ivalue){
		if (atoi(ivalue) < 0) {
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		} else {
			ageusec = atoi(ivalue);
		}
	}

	dervalue = av_value(avl, "derivedconf");
	if (!dervalue) {
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	if (root_path)
		free(root_path);

	root_path = strdup(value);

	rollover = roll;
	if (rollmethod >= MINROLLTYPE) {
		rolltype = rollmethod;
		pthread_create(&rothread, NULL, rolloverThreadInit, NULL);
	}

	if (altvalue)
		altheader = atoi(altvalue);
	else
		altheader = 0;

	if (derivedconf)
		free(derivedconf);

	if (dervalue)
		derivedconf = strdup(dervalue);

	pthread_mutex_unlock(&cfg_lock);
	if (!root_path)
		return ENOMEM;
	return 0;
}

static void term(void)
{
	if (root_path)
		free(root_path);
	if (derivedconf)
		free(derivedconf);
}

static const char *usage(void)
{
	return  "    config name=store_derived_csv path=<path> altheader=<0/1> derivedconf=<fullpath> ageusec=<sec>\n"
		"         - Set the root path for the storage of csvs.\n"
		"           path      The path to the root of the csv directory\n"
		"         - altheader Header in a separate file (optional, default 0)\n"
		"         - rollover  Greater than zero; enables file rollover and sets interval\n"
		"         - rolltype  [1-n] Defines the policy used to schedule rollover events.\n"
		ROLLTYPES
		"         - derivedconf (optional) Full path to derived config file\n"
		"         - ageusec     Set flag field if dt > this val in usec.\n"
		"                     (Optional default no value used.)\n";
}


static void *get_ucontext(ldmsd_store_handle_t _s_handle)
{
	struct csv_derived_store_handle *s_handle = _s_handle;
	return s_handle->ucontext;
}


/*
static void printDataStructure(struct csv_derived_store_handle *s_handle){
	int i;
	for (i = 0; i < s_handle->numder; i++){
		printf("%d: dername=<%s> type=%d idx=%d\n",
		       i, s_handle->der[i].dername, (int)(s_handle->der[i].dertype), s_handle->der[i].deridx);
	}
}
*/


static int print_header_from_store(struct csv_derived_store_handle *s_handle,
				   ldms_set_t set, int* metric_arry, size_t metric_count)
{
	const char* name;
	int rc = 0;
	int i, j;

	/* Only called from Store which already has the lock */
	if (s_handle == NULL){
		msglog(LDMSD_LERROR, "%s: Null store handle. Cannot print header\n",
			__FILE__);
		return EINVAL;
	}
	s_handle->printheader = DONT_PRINT_HEADER;

	FILE* fp = s_handle->headerfile;
	if (!fp){
		msglog(LDMSD_LERROR, "%s: Cannot print header for store_derived_csv. No headerfile\n",
			__FILE__);
		return EINVAL;
	}

	if (s_handle->parseconfig){
		rc = derivedConfig(derivedconf,s_handle);
		if (rc != 0) {
			msglog(LDMSD_LERROR,"%s: derivedConfig failed for store_derived_csv. \n",
			       __FILE__);
			return rc;
		}
	}


	//Dont print the header yet, wait until find the metrics which are associated with this set

	/*  Determine here which if the derived metric names will be which metrics.
	 *  Note: any missing metrics will have deridx still = -1
	 */
	for (i = 0; i < metric_count; i++) {
		name = ldms_metric_name_get(set, metric_arry[i]);
		for (j = 0; j < s_handle->numder; j++){
			//NOTE: this means only 1 match (cant be both raw and dt)
			if (strcmp(name, s_handle->der[j].rawname) == 0){
				//printf("Found a match for derived: %s:%d\n", name, i);
				s_handle->der[j].deridx = i;
			}
		}
	}


	//NOW print the header using only the metrics for this set....

	/* This allows optional loading a float (Time) into an int field and retaining usec as
	   a separate field */
	fprintf(fp, "#Time, Time_usec, DT, DT_usec");

	// Write all the metrics we know we should have */
	fprintf(fp, ", ProducerName");
	for (i = 0; i < s_handle->numder; i++){
		if (s_handle->der[i].deridx != -1){
			fprintf(fp, ", %s", s_handle->der[i].dername);
		}
	}
	fprintf(fp, ", Flag\n");

	/* Flush for the header, whether or not it is the data file as well */
	fflush(fp);

	fclose(s_handle->headerfile);
	s_handle->headerfile = 0;

	return 0;
}


static ldmsd_store_handle_t
open_store(struct ldmsd_store *s, const char *container, const char* schema,
		struct ldmsd_strgp_metric_list *list, void *ucontext)
{
	struct csv_derived_store_handle *s_handle = NULL;
	int add_handle = 0;
	char* skey = NULL;
	int rc = 0;
	int i;

	pthread_mutex_lock(&cfg_lock);
	skey = allocStoreKey(container, schema);
	if (skey == NULL){
	  msglog(LDMSD_LERROR, "%s: Cannot open store\n",
		 __FILE__);
	  goto out;
	}

	s_handle = idx_find(store_idx, (void *)skey, strlen(skey));
	/* ideally, this should always be null, because we would have closed
	 * and removed an existing container before reopening it. defensively
	 * keeping this like v2 (note the lack of removal of some things in
	 * the close). Otherwise then could always do all these steps.
	 */
	if (!s_handle) {
		char tmp_path[PATH_MAX];

		//append or create
		snprintf(tmp_path, PATH_MAX, "%s/%s", root_path, container);
		rc = mkdir(tmp_path, 0777);
		if ((rc != 0) && (errno != EEXIST)){
			msglog(LDMSD_LERROR, "%s: Error: cannot create dir '%s'\n",
			       __FILE__, tmp_path);
			goto out;
		}
		/* New in v3: this is a name change */
		snprintf(tmp_path, PATH_MAX, "%s/%s/%s", root_path, container, schema);
		s_handle = calloc(1, sizeof *s_handle);
		if (!s_handle)
			goto out;
		s_handle->ucontext = ucontext;
		s_handle->store = s;

		s_handle->sets_idx = idx_create();
		if (!(s_handle->sets_idx))
			goto err1;

		add_handle = 1;

		pthread_mutex_init(&s_handle->lock, NULL);

		s_handle->path = NULL;
		s_handle->path = strdup(tmp_path);
		if (!s_handle->path) {
			/* Take the lock because we unlock in the err path */
			pthread_mutex_lock(&s_handle->lock);
			goto err2;
		}

		s_handle->store_key = strdup(skey);
		if (!s_handle->store_key) {
			/* Take the lock because we unlock in the err path */
			pthread_mutex_lock(&s_handle->lock);
			goto err2;
		}
		s_handle->schema = strdup(schema);
		if (!s_handle->schema) {
			/* Take the lock because we unlock in the err path */
			pthread_mutex_lock(&s_handle->lock);
			goto err2a;
		}

		s_handle->numder = 0;
		s_handle->parseconfig = 1;
		s_handle->printheader = FIRST_PRINT_HEADER;
	} else {
		//always redo the sets since we dont know if the metrics
		//have changed and thus the old values may be inconsistent
		s_handle->sets_idx = idx_create();
		if (!(s_handle->sets_idx))
			goto err1;

		//always reparse the config to reinitialize all indicies
		s_handle->numder = 0;
		s_handle->parseconfig = 1;
		s_handle->printheader = DO_PRINT_HEADER;
	}

	/* Take the lock in case its a store that has been closed */
	pthread_mutex_lock(&s_handle->lock);

	/* For both actual new store and reopened store, open the data file */
	char tmp_path[PATH_MAX];
	time_t appx = time(NULL);
	if (rolltype >= MINROLLTYPE){
		snprintf(tmp_path, PATH_MAX, "%s.%d",
			 s_handle->path, (int)appx);
	} else {
		snprintf(tmp_path, PATH_MAX, "%s",
			 s_handle->path);
	}

	if (!s_handle->file)  { /* theoretically, we should never already have this file */
		s_handle->file = fopen(tmp_path, "a+");
	}
	if (!s_handle->file) {
		msglog(LDMSD_LERROR, "%s: Error %d opening the file %s.\n",
		       __FILE__, errno, tmp_path);
		goto err3;
	}

	/*
	 * Always reprint the header because this is a store that may have been
	 * closed and then reopened because a new metric has been added.
	 * New in v3: since it may be a new set of metrics, possibly append to the header.
	 */

	if (!s_handle->headerfile){ /* theoretically, we should never already have this file */
		if (altheader) {
			char tmp_headerpath[PATH_MAX];
			if (rolltype >= MINROLLTYPE){
				snprintf(tmp_headerpath, PATH_MAX,
					 "%s.HEADER.%d", s_handle->path, (int)appx);
			} else {
				snprintf(tmp_headerpath, PATH_MAX,
					 "%s.HEADER", s_handle->path);
			}

			/* truncate a separate headerfile if its the first time */
			if (s_handle->printheader == FIRST_PRINT_HEADER){
				s_handle->headerfile = fopen(tmp_headerpath, "w");
			} else if (s_handle->printheader == DO_PRINT_HEADER){
				s_handle->headerfile = fopen(tmp_headerpath, "a+");
			}
		} else {
			s_handle->headerfile = fopen(tmp_path, "a+");
		}

		if (!s_handle->headerfile){
			msglog(LDMSD_LERROR, "%s: Error: Cannot open headerfile\n",
			       __FILE__);
			goto err4;
		}
	}

	/* ideally here should always be the printing of the header, and we could drop keeping
	 * track of printheader. keeping this like v2 for consistency for now
	 */

	if (add_handle) {
		// do we have an empty space?
		int found = 0;
		for (i = 0; i < nstorekeys; i++){
			if (storekeys[i] == NULL){
				storekeys[i] = strdup(skey);
				found = 1;
				break;
			}
		}
		if (!found){
			if (nstorekeys == (MAX_ROLLOVER_STORE_KEYS-1)){
				msglog(LDMSD_LDEBUG, "%s: Error: Exceeded max store keys\n",
				       __FILE__);
				goto err4;
			} else {
				storekeys[nstorekeys++] = strdup(skey);
			}
		}
		idx_add(store_idx, (void *)skey, strlen(skey), s_handle);
	}

	pthread_mutex_unlock(&s_handle->lock);
	goto out;

 err5:
 err4: //NO Headerfile
	if (s_handle->headerfile)
		fclose(s_handle->headerfile);
	s_handle->headerfile = NULL;

 err3: //NO file
	if (s_handle->file)
		fclose(s_handle->file);
	s_handle->file = NULL;

 err2a:
	if (s_handle->schema)
		free(s_handle->schema);
	s_handle->schema = NULL;

 err2: //NO store key OR NO path
	if (s_handle->store_key)
		free(s_handle->store_key);
	s_handle->store_key = NULL;

	if (s_handle->path)
		free(s_handle->path);
	s_handle->path = NULL;

	if (s_handle->sets_idx)
		idx_destroy(s_handle->sets_idx);

	pthread_mutex_unlock(&s_handle->lock);
	pthread_mutex_destroy(&s_handle->lock);

 err1: //NO sets_idx
	free(s_handle);
	s_handle = NULL;


 out: //NO shandle OR successful
	if (skey)
	  free(skey);
	pthread_mutex_unlock(&cfg_lock);
	return s_handle;
}

static int
store(ldmsd_store_handle_t _s_handle, ldms_set_t set, int *metric_arry, size_t metric_count)
{

	const struct ldms_timestamp _ts = ldms_transaction_timestamp_get(set);
	const struct ldms_timestamp *ts = &_ts;
	const char* pname;
	struct csv_derived_store_handle *s_handle;
	int setflagtime;
	int setflag;
	int rc;
	int i;

	s_handle = _s_handle;
	if (!s_handle)
		return EINVAL;

	pthread_mutex_lock(&s_handle->lock);
	if (!s_handle->file){
		msglog(LDMSD_LERROR, "%s: Cannot insert values for <%s>: file is closed\n",
		       __FILE__, s_handle->path);
		pthread_mutex_unlock(&s_handle->lock);
		return EPERM;
	}

	/* Keeping print header here for compatibility with the other.*/
	switch (s_handle->printheader){
	case DO_PRINT_HEADER:
		/* fall thru */
	case FIRST_PRINT_HEADER:
		rc = print_header_from_store(s_handle, set, metric_arry, metric_count);
		if (rc != 0){
			msglog(LDMSD_LERROR, "%s: Error in print_header: %d\n", __FILE__, rc);
			s_handle->printheader = BAD_HEADER;
			pthread_mutex_unlock(&s_handle->lock);
			return rc;
		}
		break;
	case BAD_HEADER:
		return EINVAL;
		break;
	default:
		//ok to continue
		break;
	}


	//keep this point.....
	struct setdatapoint* dp = idx_find(s_handle->sets_idx,
					   (void*)(ldms_set_instance_name_get(set)),
					   strlen(ldms_set_instance_name_get(set)));
	if (dp == NULL){
		dp = (struct setdatapoint*)malloc(sizeof(struct setdatapoint));
		if (!dp) {
			pthread_mutex_unlock(&s_handle->lock);
			return ENOMEM;
		}

		dp->ts = NULL;
		dp->datavals = NULL;
		s_handle->numsets++;

		idx_add(s_handle->sets_idx, (void*)(ldms_set_instance_name_get(set)),
			strlen(ldms_set_instance_name_get(set)), dp);
	}


	if (dp->ts == NULL){ //first time - ONLY save
		dp->ts = (struct ldms_timestamp*)malloc(sizeof (struct ldms_timestamp));
		if (dp->ts == NULL) {
			pthread_mutex_unlock(&s_handle->lock);
			return ENOMEM;
		}
		dp->ts->sec = ts->sec;
		dp->ts->usec = ts->usec;

		dp->datavals = (uint64_t*) malloc((s_handle->numder)*sizeof(uint64_t));
		if (dp->datavals == NULL){
			pthread_mutex_unlock(&s_handle->lock);
			return ENOMEM;
		}
		for (i = 0; i < s_handle->numder; i++){
			int midx = s_handle->der[i].deridx;
			if (midx >= 0)
				dp->datavals[i] = ldms_metric_get_u64(set, metric_arry[midx]);
		}
		goto out;
	}

	/* New in v3: if time diff is not positive, always write out something and flag.
	 * if its RAW data, write the val. if its RATE data, write zero
	 */

	setflag = 0;
	setflagtime = 0;
	struct timeval prev, curr, diff;
	prev.tv_sec = dp->ts->sec;
	prev.tv_usec = dp->ts->usec;
	curr.tv_sec = ts->sec;
	curr.tv_usec = ts->usec;
	if ((double)prev.tv_sec*1000000+prev.tv_usec >= (double)curr.tv_sec*1000000+curr.tv_usec){
		msglog(LDMSD_LDEBUG," %s: Time diff is <= 0 for set %s. Flagging\n",
		       __FILE__, ldms_set_instance_name_get(set));
		goto out;
		setflagtime = 1;
	}
	//always do this and write it out
	timersub(&curr, &prev, &diff);

	/* format: #Time, Time_usec, DT, DT_usec */
	fprintf(s_handle->file, "%"PRIu32".%06"PRIu32 ", %"PRIu32,
		ts->sec, ts->usec, ts->usec);
	fprintf(s_handle->file, ", %lu.%06lu, %lu",
		diff.tv_sec, diff.tv_usec, diff.tv_usec);

	pname = ldms_set_producer_name_get(set);
	if (pname != NULL){
		fprintf(s_handle->file, ", %s", pname);
		s_handle->byte_count += strlen(pname);
	} else {
		fprintf(s_handle->file, ", ");
	}

	/* for all metrics in the conf, write the vals. if setflag then only write vals for RAW and write 0 for RATE */
	for (i = 0; i < s_handle->numder; i++){
		int midx = s_handle->der[i].deridx;
		//NOTE: once intended to enable deltas to be specified for metrics which did not exist,
		//but currently cannot distinguish which metrics are missing metrics for which
		//sets
		if (midx != -1){
			uint64_t val;
			if (s_handle->der[i].dertype == RAW) {
				val = ldms_metric_get_u64(set, metric_arry[midx]);
				val = (uint64_t) (val * s_handle->der[i].multiplier);
			} else {
				if (!setflagtime){ //then dt > 0
					uint64_t currval = ldms_metric_get_u64(set, metric_arry[midx]);
					if (currval == dp->datavals[i]){
						val = 0;
					} else if (currval > dp->datavals[i]){
						double temp = (currval - dp->datavals[i]);
						temp *= s_handle->der[i].multiplier;
						temp *= 1000000.0;
						temp /= (double)(diff.tv_sec*1000000.0 + diff.tv_usec);
						val = (uint64_t) temp;
					} else {
						//ROLLOVER - Should we assume ULONG_MAX is the rollover? Just use 0 for now....
						setflag = 1;
						val = 0;
					}
				} else {
					setflag = 1;
					val = 0;
				}
			}

			rc = fprintf(s_handle->file, ", %" PRIu64, val);
			if (rc < 0)
				msglog(LDMSD_LERROR,"%s: Error %d writing to '%s'\n",
				       __FILE__, rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}

	} // i
	if (setflagtime || ((double)diff.tv_sec*1000000+diff.tv_usec > ageusec))
		setflag = 1;

	fprintf(s_handle->file, ", %d\n", setflag);
	s_handle->byte_count += 1;

	dp->ts->sec = ts->sec;
	dp->ts->usec = ts->usec;
	for (i = 0; i < s_handle->numder; i++){
		int midx = s_handle->der[i].deridx;
		if (midx >= 0){
			dp->datavals[i] = ldms_metric_get_u64(set, metric_arry[midx]);
		}
	}

out:
	s_handle->store_count++;
	pthread_mutex_unlock(&s_handle->lock);

	return 0;
}

static int flush_store(ldmsd_store_handle_t _s_handle)
{
	struct csv_derived_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		msglog(LDMSD_LERROR,"%s: flush error.\n, __FILE__");
		return -1;
	}
	pthread_mutex_lock(&s_handle->lock);
	fflush(s_handle->file);
	pthread_mutex_unlock(&s_handle->lock);

	return 0;
}

static void close_store(ldmsd_store_handle_t _s_handle)
{
	int i;

	/* note closing a store removes it from the idx list. */

	pthread_mutex_lock(&cfg_lock);
	struct csv_derived_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		pthread_mutex_unlock(&cfg_lock);
		return;
	}

	pthread_mutex_lock(&s_handle->lock);
	msglog(LDMSD_LDEBUG,"%s: Closing store_csv with path <%s>\n",
	       __FILE__, s_handle->path);
	fflush(s_handle->file);
	s_handle->store = NULL;
	if (s_handle->path)
		free(s_handle->path);
	s_handle->path = NULL;
	if (s_handle->ucontext)
		free(s_handle->ucontext);
	s_handle->ucontext = NULL;
	if (s_handle->file)
		fclose(s_handle->file);
	s_handle->file = NULL;
	if (s_handle->headerfile)
		fclose(s_handle->headerfile);
	s_handle->headerfile = NULL;

	s_handle->numder = 0;

	if (s_handle->sets_idx) {
		//FIXME: need someway to iterate thru this to get the ptrs to free them
		idx_destroy(s_handle->sets_idx);
	}

	idx_delete(store_idx, s_handle->store_key, strlen(s_handle->store_key));

	for (i = 0; i < nstorekeys; i++){
		if (strcmp(storekeys[i], s_handle->store_key) == 0){
			free(storekeys[i]);
			storekeys[i] = 0;
			//note the space is still in the array
			break;
		}
	}
	if (s_handle->schema)
		free(s_handle->schema);
	if (s_handle->store_key)
		free(s_handle->store_key);
	pthread_mutex_unlock(&s_handle->lock);
	pthread_mutex_destroy(&s_handle->lock);
	pthread_mutex_unlock(&cfg_lock);

	free(s_handle); //FIXME: should this happen?
}

static struct ldmsd_store store_derived_csv = {
	.base = {
			.name = "derived_csv",
			.term = term,
			.config = config,
			.usage = usage,
	},
	.open = open_store,
	.get_context = get_ucontext,
	.store = store,
	.flush = flush_store,
	.close = close_store,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &store_derived_csv.base;
}

static void __attribute__ ((constructor)) store_derived_csv_init();
static void store_derived_csv_init()
{
	store_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_derived_csv_fini(void);
static void store_derived_csv_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(store_idx);
}
