/**
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014 Sandia Corporation. All rights reserved.
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
static char* storekeys[MAX_ROLLOVER_STORE_KEYS]; //FIXME: make this variable size
static int nstorekeys = 0;
static char *root_path;
static int altheader;
static char* derivedconf = NULL;
static int id_pos;
static int agedt_sec = -1;
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
 * BASED on:
 * store_csv - csv compid, value pairs UNLESS id_pos is specified.
 * In that case, either only the first/last (0/1) metric's comp id
 * will be used. First/last is determined by order in the store,
 * not the order in ldms_ls (probably reversed).
 *
 * THIS is:
 * store_derived - specify some metrics to be stored, either RAW or RATE.
 * This can be used in conjunction with store_csv to have 1 file of all RAW
 * go to one location and 1 file of a subset of RAW and some RATES go to
 * another.
 *
 * Notes:
 * - format of the configuration file is comma separated
 * - rollover (determined by a neg value) and neg values - both return 0
 * - STORE_DERIVED_METRIC_MAX - is fixed value.
 * - there is no function to iterate thru an idx. Right now, that memory is
 *   lost in destroy_store.
 * - two timestamps for the same component with dt = 0 wont writeout. Presumably
 *   this shouldnt happen.
 * - if a host goes down and comes back up, then may have a long time range
 *   between points. currently, this is still calculated, but can be flagged
 *   with ageout. Currently this is global, not per collector type
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
	//FIXME: making these all uint64_t. this will have to support everything like the metric does.
	//NOTE: not keeping the user data. have this in the mvec
};

typedef enum {
	DONT_PRINT_HEADER,
	DO_PRINT_HEADER,
	BAD_HEADER
} printheader_t;

//If this is going to have the last dp, then a store_handle can only be for a particular sampler (not multiple samplers)
struct csv_derived_store_handle {
	struct ldmsd_store *store;
	char *path;
	FILE *file;
	FILE *headerfile;
	struct derived_data der[STORE_DERIVED_METRIC_MAX]; //FIXME: dynamic.
	int numder;
	idx_t sets_idx;
	int numsets;
	printheader_t printheader;
	int parseconfig;
	char *store_key;
	pthread_mutex_t lock;
	void *ucontext;
	int64_t store_count;
	int64_t byte_count;
};

static pthread_mutex_t cfg_lock;

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
					msglog(LDMS_LDEBUG, "Error: unexpected rolltype in store(%d)\n",
						rolltype);
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
					msglog(LDMS_LDEBUG, "Error: cannot open file <%s>\n",
					       tmp_path);
					pthread_mutex_unlock(&s_handle->lock);
					continue;
				}
				if (altheader){
					//re name: if got here then rollover requested
					snprintf(tmp_headerpath, PATH_MAX,
						 "%s.HEADER.%d",
						 s_handle->path, (int)appx);
					/* truncate a separate headerfile if it exists */
					nhfp = fopen(tmp_headerpath, "w");
					if (!nhfp){
						fclose(nfp);
						msglog(LDMS_LDEBUG, "Error: cannot open file <%s>\n",
						       tmp_headerpath);
					}
				} else {
					nhfp = fopen(tmp_path, "a+");
					if (!nhfp){
						fclose(nfp);
						msglog(LDMS_LDEBUG, "Error: cannot open file <%s>\n",
						       tmp_path);
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
	int tval;
	double mval;
	char* s;
	int rc, rcl;

	//FIXME: for now will read this in for every option (e.g., different base set for store)
	//Dont yet have a way to determine which of the handles a certain metric will be associated with


	FILE* fp = fopen(fname, "r");
	if (!fp) {
		msglog(LDMS_LDEBUG,"Cannot open config file <%s>\n", fname);
		return EINVAL;
	}
	s_handle->parseconfig = 0;
	s_handle->numder = 0;

	rc = 0;
	rcl = 0;
	do {

		//FIXME: TOO many metrics
		if (s_handle->numder == STORE_DERIVED_METRIC_MAX) {
			msglog(LDMS_LDEBUG,"Too many metrics <%s>\n", fname);
			rc = EINVAL;
			break;
		}

		s = fgets(lbuf, sizeof(lbuf), fp);
		if (!s)
			break;
//		printf("Read <%s>\n", lbuf);
		rcl = sscanf(lbuf, "%[^,] , %d , %lf", metric_name, &tval, &mval);
//		printf("Name <%s> val <%d> mult <%lf>\n", metric_name, tval, mval);
		if ((strlen(metric_name) > 0) && (metric_name[0] == '#')){
		// hashed lines are comments (means metric name cannot start with #)
			msglog(LDMS_LDEBUG,"Comment in derived config file <%s>. Skipping\n",lbuf);
			continue;
		}
		if (rcl != 3) {
			msglog(LDMS_LDEBUG,"Bad format in derived config file <%s> rc=%d. Skipping\n",lbuf, rcl);
			continue;
		}
		if ((tval < 0) || (tval > 1)) {
			msglog(LDMS_LDEBUG,"Bad type in derived config file <%s> <%d>. Skipping\n", lbuf, tval);
			continue;
		}

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
	int ipos = -1;
	int agev = -1;

	pthread_mutex_lock(&cfg_lock);

	value = av_value(avl, "path");
	if (!value){
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	altvalue = av_value(avl, "altheader");

	ivalue = av_value(avl, "id_pos");
	if (ivalue){
		ipos = atoi(ivalue);
		if ((ipos < 0) || (ipos > 1)) {
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}
	}

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

	ivalue = av_value(avl, "agesec");
	if (ivalue){
		agev = atoi(ivalue);
		if (agev < 0){
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
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

	id_pos = ipos;
	rollover = roll;
	if (rollmethod >= MINROLLTYPE) {
		rolltype = rollmethod;
		pthread_create(&rothread, NULL, rolloverThreadInit, NULL);
	}
	agedt_sec = agev;

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
}

static const char *usage(void)
{
	return  "    config name=store_derived_csv path=<path> altheader=<0/1> id_pos=<0/1> derivedconf=<fullpath> agesec=<sec>\n"
		"         - Set the root path for the storage of csvs.\n"
		"           path      The path to the root of the csv directory\n"
		"         - altheader Header in a separate file (optional, default 0)\n"
		"         - rollover  Greater than zero; enables file rollover and sets interval\n"
		"         - rolltype  [1-n] Defines the policy used to schedule rollover events.\n"
		ROLLTYPES
		"         - derivedconf (optional) Full path to derived config file\n"
		"         - agesec     Set flag field if dt > this val in sec.\n"
		"                     (Optional default no value used.)\n"
		"         - id_pos    Use only one comp_id either first or last (0/1)\n"
		"                     (Optional default use all compid)\n";
}

/*
 * currently store based on set name
   (all metrics for a set in the same store)
 */
static ldmsd_store_handle_t get_store(const char *container)
{
	ldmsd_store_handle_t s_handle;
	pthread_mutex_lock(&cfg_lock);
	s_handle = idx_find(store_idx, (void *)container, strlen(container));
	pthread_mutex_unlock(&cfg_lock);
	return s_handle;
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


static int print_header(struct csv_derived_store_handle *s_handle,
			ldms_mvec_t mvec)
{
	int num_metrics;
	const char* name;

	int rc = 0;
	int i, j;


	/* Only called from Store which already has the lock */
	FILE* fp = s_handle->headerfile;
	if (!fp){
		msglog(LDMS_LDEBUG,"Cannot print header for store_derived_csv. No headerfile\n");
		return EINVAL;
	}
	s_handle->printheader = DONT_PRINT_HEADER;

	if (s_handle->parseconfig){
	  rc = derivedConfig(derivedconf,s_handle);
	  if (rc != 0) {
	    msglog(LDMS_LDEBUG,"derivedConfig failed for store_derived_csv. \n");
	    return rc;
	  }
	}


	//Dont print the header yet, wait until find the metrics which are associated with this set

	/* Determine here which if the derived metric names will be which metrics.
	 *  Note: any missing metrics will have deridx still = -1
	 */
	num_metrics = ldms_mvec_get_count(mvec);
	for (i = 0; i < num_metrics; i++) {
		//printf("Checking metric <%s>\n",name);
		name = ldms_get_metric_name(mvec->v[i]);
		for (j = 0; j < s_handle->numder; j++){
			//FIXME: note this means only 1 match (cant be both raw and dt)
			if (strcmp(name, s_handle->der[j].rawname) == 0){
				//printf("Found a match for derived: %s:%d\n", name, i);
				s_handle->der[j].deridx = i;
			}
		}
	}


	//NOW print the header using only the metrics for this set....

	/* This allows optional loading a float (Time) into an int field and retaining usec as
	   a separate field */
	//FIXME: should we change this format so it looks like the raw file (e.g., add a compid to the DT)?
	fprintf(fp, "#Time, Time_usec, DT, DT_usec");

	// Write all the metrics we know we should have */
	if (id_pos < 0){
		for (i = 0; i < s_handle->numder; i++){
			if (s_handle->der[i].deridx != -1){
				fprintf(fp, ", %s.CompId, %s.value",
					s_handle->der[i].dername, s_handle->der[i].dername);
			}
		}
		fprintf(fp, ", Flag\n");
	} else {
		fprintf(fp, ", CompId");
		for (i = 0; i < s_handle->numder; i++){
			if (s_handle->der[i].deridx != -1){
				fprintf(fp, ", %s", s_handle->der[i].dername);
			}
		}
		fprintf(fp, ", Flag\n");
	}

	/* Flush for the header, whether or not it is the data file as well */
	fflush(fp);

	fclose(s_handle->headerfile);
	s_handle->headerfile = 0;

	return 0;
}


static ldmsd_store_handle_t
new_store(struct ldmsd_store *s, const char *comp_type, const char* container,
		struct ldmsd_store_metric_index_list *list, void *ucontext)
{
	struct csv_derived_store_handle *s_handle;
	int add_handle = 0;
	int rc = 0;

	pthread_mutex_lock(&cfg_lock);
	s_handle = idx_find(store_idx, (void *)container, strlen(container));
	if (!s_handle) {
		char tmp_path[PATH_MAX];

		//append or create
		snprintf(tmp_path, PATH_MAX, "%s/%s", root_path, comp_type);
		rc = mkdir(tmp_path, 0777);
		if ((rc != 0) && (errno != EEXIST)){
			msglog(LDMS_LDEBUG,"Error: cannot create dir '%s'\n", tmp_path);
			goto out;
		}
		snprintf(tmp_path, PATH_MAX, "%s/%s/%s", root_path, comp_type,
				container);

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

		s_handle->store_key = strdup(container);
		if (!s_handle->store_key) {
			/* Take the lock because we unlock in the err path */
			pthread_mutex_lock(&s_handle->lock);
			goto err2;
		}

		s_handle->printheader = DO_PRINT_HEADER;
		s_handle->parseconfig = 1;
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

	if (!s_handle->file)  {
		s_handle->file = fopen(tmp_path, "a+");
	}
	if (!s_handle->file)
		goto err3;

	/* Only bother to open the headerfile if we have to print the header(s) */
	if ((s_handle->printheader == DO_PRINT_HEADER) && !s_handle->headerfile){
		if (altheader) {
			char tmp_headerpath[PATH_MAX];
			if (rolltype >= MINROLLTYPE){
				snprintf(tmp_headerpath, PATH_MAX,
					 "%s.HEADER.%d", s_handle->path, (int)appx);
			} else {
				snprintf(tmp_headerpath, PATH_MAX,
					 "%s.HEADER", s_handle->path);
			}
			/* truncate a separate headerfile if exists */
			s_handle->headerfile = fopen(tmp_headerpath, "w");
		} else {
			s_handle->headerfile = fopen(tmp_path, "a+");
		}


		if (!s_handle->headerfile){
			msglog(LDMS_LDEBUG,"store_derived_csv: Cannot open headerfile");
			goto err4;
		}
	}

	if (add_handle) {
		if (nstorekeys == (MAX_ROLLOVER_STORE_KEYS-1)){
			msglog(LDMS_LDEBUG, "Error: Exceeded max store keys");
			goto err5;
		} else {
			idx_add(store_idx, (void *)container,
				strlen(container), s_handle);
			storekeys[nstorekeys++] = strdup(container);
		}
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
	pthread_mutex_unlock(&cfg_lock);
	return s_handle;
}

static int
store(ldmsd_store_handle_t _s_handle, ldms_set_t set, ldms_mvec_t mvec)
{

	/* NOTE: ldmsd_store invokes the lock on the whole s_handle */

	uint64_t comp_id;
	struct csv_derived_store_handle *s_handle;
	const struct ldms_timestamp *ts = ldms_get_timestamp(set);
	int setflagtime;
	int setflag = 0;
	int rc;
	int i;

	s_handle = _s_handle;
	if (!s_handle)
		return EINVAL;

	pthread_mutex_lock(&s_handle->lock);
	if (!s_handle->file){
		msglog(LDMS_LDEBUG,"Cannot insert values for <%s>: file is closed\n",
				s_handle->path);
		pthread_mutex_unlock(&s_handle->lock);
		return EPERM;
	}

	switch (s_handle->printheader){
	case DO_PRINT_HEADER:
		rc = print_header(s_handle, mvec);
		if (rc != 0){
			msglog(LDMS_LDEBUG,"store_derived_csv: Error in print_header: %d\n", rc);
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
					   (void*)(ldms_get_set_name(set)),
					   strlen(ldms_get_set_name(set)));
	if (dp == NULL){
		dp = (struct setdatapoint*)malloc(sizeof(struct setdatapoint));
		if (!dp) {
			pthread_mutex_unlock(&s_handle->lock);
			return ENOMEM;
		}

		dp->ts = NULL;
		dp->datavals = NULL;
		s_handle->numsets++;

		idx_add(s_handle->sets_idx, (void*)(ldms_get_set_name(set)),
			strlen(ldms_get_set_name(set)), dp);
	}


	if (dp->ts == NULL){ //first time - ONLY save
		dp->ts = (struct ldms_timestamp*)malloc(sizeof (struct ldms_timestamp));
		if (dp->ts == NULL) {
			pthread_mutex_unlock(&s_handle->lock);
			return ENOMEM;
		}

		dp->datavals = (uint64_t*) malloc((s_handle->numder)*sizeof(uint64_t));
		if (dp->datavals == NULL){
			pthread_mutex_unlock(&s_handle->lock);
			return ENOMEM;
		}
		goto skip;
	}


	/* Back port from v3: if time diff is not positive, write out something and flag.
	 * if tis RAW data, write the val. if its RATE data, write zero
	 */

	setflag = 0;
	setflagtime = 0;
	struct timeval prev, curr, diff;
	prev.tv_sec = dp->ts->sec;
	prev.tv_usec = dp->ts->usec;
	curr.tv_sec = ts->sec;
	curr.tv_usec = ts->usec;
	if ((double)prev.tv_sec*1000000+prev.tv_usec >= (double)curr.tv_sec*1000000+curr.tv_usec){
		msglog(LDMS_LDEBUG," %s: Time diff is <= 0 for set %s. Flagging\n",
		       __FILE__, ldms_set_instance_name_get(set));
		setflagtime = 1;
	}
	//always do this and write it out
	timersub(&curr, &prev, &diff);

	/* format: #Time, Time_usec, DT, DT_usec */
	fprintf(s_handle->file, "%"PRIu32".%06"PRIu32 ", %"PRIu32,
		ts->sec, ts->usec, ts->usec);
	fprintf(s_handle->file, ", %lu.%06lu, %lu",
		diff.tv_sec, diff.tv_usec, diff.tv_usec);

	int num_metrics = ldms_mvec_get_count(mvec);

	/* write the compid if necessary */
	if (id_pos >= 0){
		if (num_metrics > 0){
			i = (id_pos == 0)? 0: (num_metrics-1);
			rc = fprintf(s_handle->file, ", %" PRIu64,
				ldms_get_user_data(mvec->v[i]));
			if (rc < 0)
				msglog(LDMS_LDEBUG,"store_derived_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}
	}

	/* for all metrics in the conf, write the vals */
	for (i = 0; i < s_handle->numder; i++){
		int midx = s_handle->der[i].deridx;
		//NOTE: once intended to enable deltas to be specified for metrics which did not exist,
		//but currently cannot distinguish which metrics are missing metrics for which
		//sets
		if (midx != -1){
			uint64_t val;
			if (s_handle->der[i].dertype == RAW) {
				val = ldms_get_u64(mvec->v[midx]);
				val = (uint64_t) (val * s_handle->der[i].multiplier);
			} else {
				if (!setflagtime){ //then dt > 0
					uint64_t currval = ldms_get_u64(mvec->v[midx]);
					if (currval == dp->datavals[i]){
						val = 0;
					} else if (currval > dp->datavals[i]){
						double temp = (double)(currval - dp->datavals[i]);
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
			if (id_pos < 0) {
				comp_id = ldms_get_user_data(mvec->v[midx]);
				rc = fprintf(s_handle->file, ", %" PRIu64 ", %" PRIu64,
					     comp_id, val);
				if (rc < 0)
					msglog(LDMS_LDEBUG,"store_derived_csv: Error %d writing to '%s'\n",
					       rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			} else {
				rc = fprintf(s_handle->file, ", %" PRIu64, val);
				if (rc < 0)
					msglog(LDMS_LDEBUG,"store_derived_csv: Error %d writing to '%s'\n",
					       rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
		}

	} // i
	if (setflagtime || ((double)diff.tv_sec*1000000+diff.tv_usec > agedt_sec))
		setflag = 1;

	fprintf(s_handle->file, ", %d\n", setflag);
	s_handle->byte_count += 1;
	s_handle->store_count++;

skip:
	dp->ts->sec = ts->sec;
	dp->ts->usec = ts->usec;
	ldms_metric_t* v = ldms_mvec_get_metrics(mvec); //new vals
	for (i = 0; i < s_handle->numder; i++){
		int midx = s_handle->der[i].deridx;
		if (midx >= 0){
			dp->datavals[i] = ldms_get_u64(v[midx]);
		}
	}

out:

	pthread_mutex_unlock(&s_handle->lock);

	return 0;
}

static int flush_store(ldmsd_store_handle_t _s_handle)
{
	struct csv_derived_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		msglog(LDMS_LDEBUG,"store_derived_csv: flush error.\n");
		return -1;
	}
	pthread_mutex_lock(&s_handle->lock);
	fflush(s_handle->file);
	pthread_mutex_unlock(&s_handle->lock);

	return 0;
}

static void close_store(ldmsd_store_handle_t _s_handle)
{
	struct csv_derived_store_handle *s_handle = _s_handle;
	if (!s_handle)
		return;

	pthread_mutex_lock(&s_handle->lock);

	if (s_handle->file)
		fclose(s_handle->file);
	s_handle->file = NULL;
	if (s_handle->headerfile)
		fclose(s_handle->headerfile);
	s_handle->headerfile = NULL;

	pthread_mutex_unlock(&s_handle->lock);
}

static void destroy_store(ldmsd_store_handle_t _s_handle)
{

	int i;

	pthread_mutex_lock(&cfg_lock);
	struct csv_derived_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		pthread_mutex_unlock(&cfg_lock);
		return;
	}

	pthread_mutex_lock(&s_handle->lock);
	msglog(LDMS_LDEBUG,"Destroying store_derived_csv with path <%s>\n", s_handle->path);

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
	if (root_path)
		free(root_path);
	if (derivedconf)
		free(derivedconf);

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
			//FIXME: note the space is still there
			break;
		}
	}
	if (s_handle->store_key)
		free(s_handle->store_key);
	pthread_mutex_unlock(&s_handle->lock);
	pthread_mutex_destroy(&s_handle->lock);
	pthread_mutex_unlock(&cfg_lock);
	free(s_handle);
}

static struct ldmsd_store store_derived_csv = {
	.base = {
			.name = "derived_csv",
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
