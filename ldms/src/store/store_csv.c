/*
 * Copyright (c) 2013-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2015 Sandia Corporation. All rights reserved.
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

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

typedef enum{CSV_CFGINIT_PRE, CSV_CFGINIT_IN, CSV_CFGINIT_DONE, CSV_CFGINIT_FAILED} csvcfg_state;

static struct column_step {
	int begin;
	int end;
	int step;
} cs = {0,0,0};


/* override for special keys */
struct storek{
	char* key;
	int altheader;
	int udata;
	struct column_step cs;
};

static csvcfg_state cfgstate = CSV_CFGINIT_PRE;
#define MAX_ROLLOVER_STORE_KEYS 20
static idx_t store_idx; //NOTE: this doesnt have an iterator. Hence storekeys.
static char* storekeys[MAX_ROLLOVER_STORE_KEYS]; //FIXME: make this variable size
static int nstorekeys = 0;
static struct storek specialkeys[MAX_ROLLOVER_STORE_KEYS]; //FIXME: make this variable size
static int nspecialkeys = 0;
static char *root_path;
static int altheader;
static int udata;
static int rollover;
/** rolltype determines how to interpret rollover values > 0. */
static int rolltype;
/** ROLLTYPES documents rolltype and is used in help output. */
#define ROLLTYPES \
"                     1: wake approximately every rollover seconds and roll.\n" \
"                     2: wake daily at rollover seconds after midnight (>=0) and roll.\n" \
"                     3: roll after approximately rollover records are written.\n" \
"                     4: roll after approximately rollover bytes are written.\n"

#define MAXROLLTYPE 4
#define MINROLLTYPE 1
/** default -- do not roll */
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
/** Interval to check for passing the record or byte count limits. */
#define ROLL_LIMIT_INTERVAL 60

/* help for column output orders */
#define ORDERTYPES \
"                     forward: metric columns ordered as added in sampler.\n" \
"                     reverse: columns reverse of order added in sampler.\n" \
"                     alnum: sorted per man page (not implemented)\n"

static ldmsd_msg_log_f msglog;
static pthread_t rothread;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

#define LOGFILE "/var/log/store_csv.log"

/*
 * New in v3: no more id_pos. Always producer name which is always written out before the metrics.
 */

struct csv_store_handle {
	struct ldmsd_store *store;
	char *path;
	FILE *file;
	FILE *headerfile;
	printheader_t printheader;
	int altheader;
	int udata;
	char *store_key; /* this is the container+schema */
	struct column_step cs;
	pthread_mutex_t lock;
	void *ucontext;
	int64_t store_count;
	int64_t byte_count;
};

static pthread_mutex_t cfg_lock;

struct kw {
	char *token;
	int (*action)(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg);
};

static int kw_comparator(const void *a, const void *b)
{
	struct kw *_a = (struct kw *)a;
	struct kw *_b = (struct kw *)b;
	return strcmp(_a->token, _b->token);
}

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
	struct csv_store_handle *s_handle;

	pthread_mutex_lock(&cfg_lock);

	time_t appx = time(NULL);

	for (i = 0; i < nstorekeys; i++){
		if (storekeys[i] != NULL){
			s_handle = idx_find(store_idx, (void *)(storekeys[i]), strlen(storekeys[i]));
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

				//re name: if got here, then rollover requested
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
				if (s_handle->altheader){
					//re name: if got here, then rollover requested
					snprintf(tmp_headerpath, PATH_MAX,
						 "%s.HEADER.%d",
						 s_handle->path, (int)appx);
					/* truncate a separate headerfile if it exists.
					 * FIXME: do we still want to do this? */
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
						msglog(LDMSD_LERROR, "%s: Error: cannot open file <%s>\n",
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
			} /* shandle */
		} /* storekeys[i] */
	} /* for */

	pthread_mutex_unlock(&cfg_lock);

	return 0;

}

static void* rolloverThreadInit(void* m){
	//if got here, then rollover requested

	while(1){
		int tsleep;
		switch (rolltype) {
		case 1:
			tsleep = (rollover < MIN_ROLL_1) ?
				 MIN_ROLL_1 : rollover;
			break;
		case 2: {
				time_t rawtime;
				struct tm *info;

				time( &rawtime );
				info = localtime( &rawtime );
				int secSinceMidnight = info->tm_hour*3600 +
					info->tm_min*60 + info->tm_sec;
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
			tsleep = 60;
			break;
		}
		sleep(tsleep);
		handleRollover();
	}

	return NULL;
}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 *
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"idpos", "id_pos"};
	int numdep = 2;


	for (i = 0; i < numdep; i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			msglog(LDMSD_LERROR, "store_csv: config argument %s has been deprecated.\n",
				deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

/**
 * configurations for a container+schema that can override the vals in config_init
 */
static int config_custom(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *cvalue;
	char *svalue;
	char *skey = NULL;
	char *altvalue;
	char *rvalue;
	int idx;
	int i;

	pthread_mutex_lock(&cfg_lock);

	//have to do this after config_init
	if (cfgstate != CSV_CFGINIT_DONE){
		msglog(LDMSD_LERROR, "%s: Error: wrong state for config_container %d\n",
		       __FILE__, cfgstate);
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	cvalue = av_value(avl, "container");
	if (!cvalue){
	  msglog(LDMSD_LERROR, "%s: Error: config missing container name\n", __FILE__);
	  pthread_mutex_unlock(&cfg_lock);
	  return EINVAL;
	}

	svalue = av_value(avl, "schema");
	if (!svalue){
	  msglog(LDMSD_LERROR, "%s: Error: config missing schema name\n", __FILE__);
	  pthread_mutex_unlock(&cfg_lock);
	  return EINVAL;
	}

	skey = allocStoreKey(cvalue, svalue);
	if (skey == NULL){
	  msglog(LDMSD_LERROR, "%s: Cannot create storekey for custom_config\n",
		 __FILE__);
	  pthread_mutex_unlock(&cfg_lock);
	  return EINVAL;
	}

	//do we have this already. if so, then can update those values.
	idx = -1;
	for (i = 0; i < nspecialkeys; i++){
		if (strcmp(skey, specialkeys[i].key) == 0){
			idx = i;
			break;
		}
	}
	if (idx < 0){
		if (nspecialkeys > (MAX_ROLLOVER_STORE_KEYS-1)){
			msglog(LDMSD_LERROR, "%s: Error store_csv: Exceeded max store keys\n",
				__FILE__);
			if (skey)
				  free(skey);
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}

		idx = nspecialkeys;
		specialkeys[idx].key = strdup(skey);
	}
	free(skey);
	skey = NULL;

	//defaults to init
	specialkeys[idx].altheader = altheader;
	specialkeys[idx].udata = udata;
	specialkeys[idx].cs.begin = cs.begin;
	specialkeys[idx].cs.end = cs.end;
	specialkeys[idx].cs.step = cs.step;
	//increment nspecialkeys now. note that if the args are a problem, we will have incremented.
	if (idx == nspecialkeys)
		nspecialkeys++;

	altvalue = av_value(avl, "altheader");
	if (altvalue) {
		specialkeys[idx].altheader = atoi(altvalue);
	}

	altvalue = av_value(avl, "userdata");
	if (altvalue) {
		specialkeys[idx].udata = atoi(altvalue);
	}

	rvalue = av_value(avl, "sequence");
	if (rvalue){
		switch (rvalue[0]) {
		case 'f':
			if (strcmp(rvalue,"forward")==0) {
				specialkeys[idx].cs.step = 1;
			}
			break;
		case 'r':
			if (strcmp(rvalue,"reverse")==0) {
				specialkeys[idx].cs.step = -1;
			}
			break;
		case 'a':
			if (strcmp(rvalue,"alnum")==0) {
				msglog(LDMSD_LERROR,"%s: sequence alnum"
				       " unsupported. using default from init.\n", __FILE__);
			}
			break;
		default:
			msglog(LDMSD_LERROR,"%s using default from init"
			       "%s unknown\n",__FILE__, rvalue);
			break;
		}
	}

	pthread_mutex_unlock(&cfg_lock);
	return 0;
}

/**
 * configurations for the whole store. these will be defaults if not overridden.
 * some implementation details are for backwards compatibility
 */
static int config_init(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	char *altvalue;
	char *uvalue;
	char *rvalue;
	int roll = -1;
	int rollmethod = DEFAULT_ROLLTYPE;

	pthread_mutex_lock(&cfg_lock);

	if (cfgstate != CSV_CFGINIT_PRE){ //Cannot redo since might have already created the roll thread.
	  msglog(LDMSD_LERROR, "%s: wrong state for config_init %d\n",
		       __FILE__, cfgstate);
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	cfgstate = CSV_CFGINIT_IN;

	value = av_value(avl, "path");
	if (!value) {
		cfgstate = CSV_CFGINIT_FAILED;
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	altvalue = av_value(avl, "altheader");

	uvalue = av_value(avl, "userdata");

	rvalue = av_value(avl, "rollover");
	if (rvalue){
		roll = atoi(rvalue);
		if (roll < 0) {
			cfgstate = CSV_CFGINIT_FAILED;
			msglog(LDMSD_LERROR, "%s: Error: bad rollover value %d\n",
			       __FILE__, roll);
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}
	}

	rvalue = av_value(avl, "rolltype");
	if (rvalue){
		if (roll < 0){
			/* rolltype not valid without rollover also */
			cfgstate = CSV_CFGINIT_FAILED;
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}
		rollmethod = atoi(rvalue);
		if (rollmethod < MINROLLTYPE ){
			cfgstate = CSV_CFGINIT_FAILED;
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}
		if (rollmethod > MAXROLLTYPE){
			cfgstate = CSV_CFGINIT_FAILED;
			return EINVAL;
		}
	}

	rvalue = av_value(avl, "sequence");
	cs.step = 1; //forward is the default
	if (rvalue){
		switch (rvalue[0]) {
		case 'r':
			if (strcmp(rvalue,"reverse")==0) {
				cs.step = -1;
			}
			break;
		case 'a':
			if (strcmp(rvalue,"alnum")==0) {
				msglog(LDMSD_LERROR,"%s sequence alnum"
				       " unsupported. using reverse.\n", __FILE__);
			}
			/* fallthru */
		default:
			if (strcmp(rvalue,"forward")!=0) {
				msglog(LDMSD_LERROR,"%s sequence=forward"
				       " assumed. %s unknown\n",__FILE__, rvalue);
			}
			break;
		}
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

	if (uvalue)
		udata = atoi(uvalue);
	else
		udata = 0;

	if (!root_path) {
		cfgstate = CSV_CFGINIT_FAILED;
		msglog(LDMSD_LERROR, "%s: Error: missing root_path\n",
		       __FILE__, roll);
		pthread_mutex_unlock(&cfg_lock);
		return ENOMEM;
	}

	cfgstate = CSV_CFGINIT_DONE;
	pthread_mutex_unlock(&cfg_lock);

	return 0;
}


struct kw kw_tbl[] = {
	{ "custom", config_custom},
	{ "init", config_init},
};


/**
 * \brief Configuration
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	struct kw *kw;
	struct kw key;
	void* arg;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0)
		return rc;

	rc = 0;
	char* action = av_value(avl, "action");
	if (!action){
		msglog(LDMSD_LERROR, "%s: Error: missing required keyword 'action'\n",
		       __FILE__);
		return EINVAL;
	}

	key.token = action;
	kw = bsearch(&key, kw_tbl, ARRAY_SIZE(kw_tbl),
		     sizeof(*kw), kw_comparator);
	if (!kw) {
		msglog(LDMSD_LERROR, "%s: Invalid configuration keyword action '%s'\n",
		       __FILE__, action);
		return EINVAL;
	}
	rc = kw->action(kwl, avl, NULL);
	if (rc) {
	  msglog(LDMSD_LERROR, "%s: error in '%s' %d\n",
		 __FILE__, action, rc);
	  return rc;
	}

	return 0;
}

static void term(void)
{
	int i;

	for (i = 0; i < nspecialkeys; i++){
		free(specialkeys[i].key);
		specialkeys[i].key = NULL;
	}

}

static const char *usage(void)
{
	return  "    config name=store_csv action=init path=<path> rollover=<num> rolltype=<num>\n"
		"           [sequence=<order> altheader=<0/!0> userdata=<0/!0>]\n"
		"         - Set the root path for the storage of csvs and some default parameters\n"
		"         - path      The path to the root of the csv directory\n"
		"         - altheader Header in a separate file (optional, default 0)\n"
		"         - userdata     UserData in printout (optional, default 0)\n"
		"         - rollover  Greater than or equal to zero; enables file rollover and sets interval\n"
		"         - rolltype  [1-n] Defines the policy used to schedule rollover events.\n"
		ROLLTYPES
		"         - sequence  Determine the metric column ordering:\n"
		ORDERTYPES
		"\n"
		"    config name=store_csv action=custom container=<c_name> schema=<s_name>\n"
		"           [sequence=<order> altheader=<0/1> userdata=<0/1>]\n"
		"         - Override the default parameters set by action=init for particular containers\n"
		"         - altheader Header in a separate file (optional, default to init)\n"
		"         - userdata     UserData in printout (optional, default to init)\n"
		"         - sequence  Determine the metric column ordering (default to init):\n"
		ORDERTYPES
		;
}

static void *get_ucontext(ldmsd_store_handle_t _s_handle)
{
	struct csv_store_handle *s_handle = _s_handle;
	return s_handle->ucontext;
}

static
void get_loop_limits(struct csv_store_handle *s_handle,
		     int num_metrics) {
	switch (s_handle->cs.step) {
	case 1:
		s_handle->cs.begin = 0;
		s_handle->cs.end = num_metrics;
		break;
	case -1:
		s_handle->cs.begin = num_metrics - 1;
		s_handle->cs.end = -1;
		break;
	default:
		msglog(LDMSD_LERROR, "store_csv sequence bug in loop (%d)\n",
			cs.step);
		s_handle->cs.begin = 0;
		s_handle->cs.end = 0;
	}
}

/*
 * note: this should be residual from v2 where we may not have had the header info until a store was called
 * which then meant we had the mvec. ideally the print_header will always happen from the store_open.
 * Currently we still have to keep this to invert the metric order
 */
static int print_header_from_store(struct csv_store_handle *s_handle, ldms_set_t set,
				   int *metric_array, size_t metric_count)
{
	/* Only called from Store which already has the lock */
	FILE* fp;
	uint32_t len;
	int i, j;

	if (s_handle == NULL){
		msglog(LDMSD_LERROR, "%s: Null store handle. Cannot print header\n",
			__FILE__);
		return EINVAL;
	}
	s_handle->printheader = DONT_PRINT_HEADER;

	fp = s_handle->headerfile;
	if (!fp){
		msglog(LDMSD_LERROR, "%s: Cannot print header for store_csv. No headerfile\n",
			__FILE__);
		return EINVAL;
	}

	/* This allows optional loading a float (Time) into an int field and
	   retaining usec as a separate field */
	fprintf(fp, "#Time, Time_usec, ProducerName");

	get_loop_limits(s_handle, metric_count);
	for (i = s_handle->cs.begin; i != s_handle->cs.end; i += s_handle->cs.step){
		const char* name = ldms_metric_name_get(set, metric_array[i]);
		enum ldms_value_type metric_type = ldms_metric_type_get(set, metric_array[i]);

		/* use same formats as ldms_ls */
		switch (metric_type){
		case LDMS_V_U8_ARRAY:
		case LDMS_V_S8_ARRAY:
		case LDMS_V_U16_ARRAY:
		case LDMS_V_S16_ARRAY:
		case LDMS_V_U32_ARRAY:
		case LDMS_V_S32_ARRAY:
		case LDMS_V_U64_ARRAY:
		case LDMS_V_S64_ARRAY:
		case LDMS_V_F32_ARRAY:
		case LDMS_V_D64_ARRAY:
			len = ldms_array_metric_get_len(set, metric_array[i]);
			if (s_handle->udata){
				for (j = 0; j < len; j++){ //there is only 1 name for all of them.
					fprintf(fp, ", %s%d.userdata, %s%d.value",
						name, j, name, j);
				}
			} else {
				for (j = 0; j < len; j++){ //there is only 1 name for all of them.
					fprintf(fp, ", %s%d", name, j);
				}
			}
			break;
		default:
			if (s_handle->udata){
				fprintf(fp, ", %s.userdata, %s.value", name, name);
			} else {
				fprintf(fp, ", %s", name);
			}
			break;
		}
	}

	fprintf(fp, "\n");

	/* Flush for the header, whether or not it is the data file as well */
	fflush(fp);

	fclose(s_handle->headerfile);
	s_handle->headerfile = 0;

	return 0;
}

/*
 *  THIS FUNCTION IS INCOMPLETE. We want this but we want a nice way to invert the metric order
 */
#if 0
static int print_header_from_open(struct csv_store_handle *s_handle,
				  struct ldmsd_store_metric_list *metric_list,
				  void *ucontext){
}
#endif

static ldmsd_store_handle_t
open_store(struct ldmsd_store *s, const char *container, const char* schema,
		struct ldmsd_strgp_metric_list *list, void *ucontext)
{
	struct csv_store_handle *s_handle = NULL;
	int add_handle = 0;
	int rc = 0;
	char* skey = NULL;
	char* path;
	int idx;
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
		size_t pathlen = strlen(root_path) +
			strlen(schema) +
			strlen(container) + 8;
		path = malloc(pathlen);
		if (!path)
			goto out;

		sprintf(path, "%s/%s", root_path, container);
		rc = mkdir(path, 0777);
		if ((rc != 0) && (errno != EEXIST)){
			msglog(LDMSD_LDEBUG,"%s: Error: %d creating directory '%s'\n",
				 __FILE__, errno, path);
			goto err0;
		}
		/* New in v3: this is a name change */
		sprintf(path, "%s/%s/%s", root_path, container, schema);
		s_handle = calloc(1, sizeof *s_handle);
		if (!s_handle)
			goto err0;

		s_handle->ucontext = ucontext;
		s_handle->store = s;
		add_handle = 1;

		pthread_mutex_init(&s_handle->lock, NULL);
		s_handle->path = path;
		s_handle->store_key = strdup(skey);
		if (!s_handle->store_key) {
			/* Take the lock becauase we will unlock in the err path */
			pthread_mutex_lock(&s_handle->lock);
			goto err1;
		}

		s_handle->store_count = 0;
		s_handle->byte_count = 0;

		idx = -1;
		for (i = 0; i < nspecialkeys; i++){
			if (strcmp(s_handle->store_key, specialkeys[i].key) == 0){
				idx = i;
				break;
			}
		}
		if (idx >= 0){
			s_handle->altheader = specialkeys[idx].altheader;
			s_handle->udata = specialkeys[idx].udata;
			s_handle->cs.begin = specialkeys[idx].cs.begin;
			s_handle->cs.end = specialkeys[idx].cs.end;
			s_handle->cs.step = specialkeys[idx].cs.step;
		} else {
			s_handle->altheader = altheader;
			s_handle->udata = udata;
			s_handle->cs.begin = cs.begin;
			s_handle->cs.end = cs.end;
			s_handle->cs.step = cs.step;
		}

		s_handle->printheader = FIRST_PRINT_HEADER;
	} else {
		s_handle->printheader = DO_PRINT_HEADER;
	}

	/* Take the lock in case its a store that has been closed */
	pthread_mutex_lock(&s_handle->lock);


	/* For both actual new store and reopened store, open the data file */
	char tmp_path[PATH_MAX];
	time_t appx = time(NULL);
	if (rolltype >= MINROLLTYPE){
		//append the files with epoch. assume wont collide to the sec.
		snprintf(tmp_path, PATH_MAX, "%s.%d",
			 s_handle->path, (int)appx);
	} else {
		snprintf(tmp_path, PATH_MAX, "%s",
			 s_handle->path);
	}

	if (!s_handle->file) { /* theoretically, we should never already have this file */
		s_handle->file = fopen(tmp_path, "a+");
	}
	if (!s_handle->file){
		msglog(LDMSD_LERROR, "%s: Error %d opening the file %s.\n",
		       __FILE__, errno, s_handle->path);
		goto err2;
	}

	/*
	 * Always reprint the header because this is a store that may have been
	 * closed and then reopened because a new metric has been added.
	 * New in v3: since it may be a new set of metrics, possibly append to the header.
	 */

	if (!s_handle->headerfile){ /* theoretically, we should never already have this file */
		if (s_handle->altheader) {
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

		if (!s_handle->headerfile) {
			msglog(LDMSD_LERROR, "%s: Error: Cannot open headerfile\n",
			       __FILE__);
			goto err3;
		}
	}

	/* ideally here should always be the printing of the header, and we could drop keeping
	 * track of printheader.
	 * FIXME: cant do this yet, until we have an easy way to invert the metric order
	 * print_header_from_open(s_handle, struct ldmsd_store_metric_index_list *list, void *ucontext);
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

err4:
err3:
	fclose(s_handle->file);
	s_handle->file = NULL;
err2:
	free(s_handle->store_key);
err1:
	pthread_mutex_unlock(&s_handle->lock);
	pthread_mutex_destroy(&s_handle->lock);
	free(s_handle);
	s_handle = NULL;
err0:
	free(path);
out:
	if (skey)
		  free(skey);
	pthread_mutex_unlock(&cfg_lock);
	return s_handle;
}

static int store(ldmsd_store_handle_t _s_handle, ldms_set_t set, int *metric_array, size_t metric_count)
{
	const struct ldms_timestamp _ts = ldms_transaction_timestamp_get(set);
	const struct ldms_timestamp *ts = &_ts;
	const char* pname;
	uint64_t udata;
	struct csv_store_handle *s_handle;
	uint32_t len;
	int i, j;
	int rc, rcu;

	s_handle = _s_handle;
	if (!s_handle)
		return EINVAL;

	pthread_mutex_lock(&s_handle->lock);
	if (!s_handle->file){
		msglog(LDMSD_LERROR,"%s: Cannot insert values for <%s>: file is NULL\n",
		       __FILE__, s_handle->path);
		pthread_mutex_unlock(&s_handle->lock);
		/* FIXME: will returning an error stop the store? */
		return EPERM;
	}

	// Temporarily still have to print header until can invert order of metrics from open
	/* FIXME: New in v3: should not ever have to print the header from here */
	switch (s_handle->printheader){
	case DO_PRINT_HEADER:
		/* fall thru */
	case FIRST_PRINT_HEADER:
		rc = print_header_from_store(s_handle, set, metric_array, metric_count);
		if (rc){
			msglog(LDMSD_LERROR, "%s: %s cannot print header: %d. Not storing\n",
			       __FILE__, s_handle->store_key, rc);
			s_handle->printheader = BAD_HEADER;
			pthread_mutex_unlock(&s_handle->lock);
			/* FIXME: will returning an error stop the store? */
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

	fprintf(s_handle->file, "%"PRIu32".%06"PRIu32 ", %"PRIu32,
		ts->sec, ts->usec, ts->usec);
	pname = ldms_set_producer_name_get(set);
	if (pname != NULL){
		fprintf(s_handle->file, ", %s", pname);
		s_handle->byte_count += strlen(pname);
	} else {
		fprintf(s_handle->file, ", ");
	}

	/* FIXME: will we want to throw an error if we cannot write? */
	get_loop_limits(s_handle, metric_count);
	for (i = s_handle->cs.begin; i != s_handle->cs.end; i += s_handle->cs.step) {
		udata = ldms_metric_user_data_get(set, metric_array[i]);
		enum ldms_value_type metric_type = ldms_metric_type_get(set, metric_array[i]);
		//use same formats as ldms_ls
		switch (metric_type){
		case LDMS_V_U8_ARRAY:
			len = ldms_array_metric_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				rc = 0;
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ", %hhu",
					     ldms_array_metric_get_u8(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_U8:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ", %hhu",
				     ldms_metric_get_u8(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_S8_ARRAY:
			len = ldms_array_metric_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ", %hhd",
					     ldms_array_metric_get_s8(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_S8:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ", %hhd",
					     ldms_metric_get_s8(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
							rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_U16_ARRAY:
			len = ldms_array_metric_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ", %hu",
						ldms_array_metric_get_u16(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_U16:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ", %hu",
					ldms_metric_get_u16(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_S16_ARRAY:
			len = ldms_array_metric_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ", %hd",
						ldms_array_metric_get_s16(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_S16:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ", %hd",
					ldms_metric_get_s16(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;

			break;
		case LDMS_V_U32_ARRAY:
			len = ldms_array_metric_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ", %" PRIu32,
						ldms_array_metric_get_u32(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_U32:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ", %" PRIu32,
					ldms_metric_get_u32(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_S32_ARRAY:
			len = ldms_array_metric_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ", %" PRId32,
						ldms_array_metric_get_s32(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_S32:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ", %" PRId32,
					ldms_metric_get_s32(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_U64_ARRAY:
			len = ldms_array_metric_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ", %" PRIu64,
						ldms_array_metric_get_u64(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_U64:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ", %"PRIu64,
					ldms_metric_get_u64(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						rc, s_handle->path);
			break;
		case LDMS_V_S64_ARRAY:
			len = ldms_array_metric_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ", %" PRId64,
						ldms_array_metric_get_s64(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_S64:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ", %" PRId64,
					ldms_metric_get_s64(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_F32_ARRAY:
			len = ldms_array_metric_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ", %f",
						ldms_array_metric_get_float(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_F32:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ", %f",
					ldms_metric_get_float(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		case LDMS_V_D64_ARRAY:
			len = ldms_array_metric_get_len(set, metric_array[i]);
			for (j = 0; j < len; j++){
				if (s_handle->udata) {
					rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
					if (rcu < 0)
						msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						       rcu, s_handle->path);
					else
						s_handle->byte_count += rcu;
				}
				rc = fprintf(s_handle->file, ", %lf",
						ldms_array_metric_get_double(set, metric_array[i], j));
				if (rc < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
							rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
			break;
		case LDMS_V_D64:
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ", %"PRIu64, udata);
				if (rcu < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ", %lf",
					ldms_metric_get_double(set, metric_array[i]));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		default:
			// print no value
			if (s_handle->udata) {
				rcu = fprintf(s_handle->file, ", ");
				if (rcu < 0)
					msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
					       rcu, s_handle->path);
				else
					s_handle->byte_count += rcu;
			}
			rc = fprintf(s_handle->file, ", ");
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
						rc, s_handle->path);
			else
				s_handle->byte_count += rc;
			break;
		}
	}
	fprintf(s_handle->file,"\n");

	s_handle->store_count++;
	pthread_mutex_unlock(&s_handle->lock);

	return 0;
}

static int flush_store(ldmsd_store_handle_t _s_handle)
{
	struct csv_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		msglog(LDMSD_LERROR,"%s: flush error.\n", __FILE__);
		return -1;
	}
	pthread_mutex_lock(&s_handle->lock);
	fflush(s_handle->file);
	pthread_mutex_unlock(&s_handle->lock);
	return 0;
}

static void close_store(ldmsd_store_handle_t _s_handle)
{
	/* note: closing a store removes from the idx list.
	 * note: do not remove the specialkeys
	 */

	int i;

	pthread_mutex_lock(&cfg_lock);
	struct csv_store_handle *s_handle = _s_handle;
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

	idx_delete(store_idx, s_handle->store_key, strlen(s_handle->store_key));

	for (i = 0; i < nstorekeys; i++){
		if (strcmp(storekeys[i], s_handle->store_key) == 0){
			free(storekeys[i]);
			storekeys[i] = 0;
			//note the space is still in the array
			break;
		}
	}

	if (s_handle->store_key)
		free(s_handle->store_key);
	pthread_mutex_unlock(&s_handle->lock);
	pthread_mutex_destroy(&s_handle->lock);
	pthread_mutex_unlock(&cfg_lock);

	free(s_handle); //FIXME: should this happen?
}

static struct ldmsd_store store_csv = {
	.base = {
			.name = "csv",
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
	return &store_csv.base;
}

static void __attribute__ ((constructor)) store_csv_init();
static void store_csv_init()
{
	store_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_csv_fini(void);
static void store_csv_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(store_idx);
}
