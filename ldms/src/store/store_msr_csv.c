/**
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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



#define MAX_ROLLOVER_STORE_KEYS 20
#define STORE_CTR_METRIC_MAX 21
#define STORE_CTR_NAME_MAX 20

/** Fields for the name translation. These must match msr */
#define MSR_MAXOPTIONS 8
#define MSR_HOST 0
#define MSR_CNT_MASK 0
#define MSR_INV 0
#define MSR_EDGE 0
#define MSR_ENABLE 1
#define MSR_INTT 0


struct MSRcounter_tr{
	char name[STORE_CTR_NAME_MAX];
	uint64_t w_reg;
	uint64_t event;
	uint64_t umask;
	uint64_t r_reg;
	uint64_t os_user;
	uint64_t wctl;
};

static struct MSRcounter_tr counter_assignments[] = {
	{"TOT_CYC", 0xc0010200, 0x076, 0x00, 0xc0010201, 0b11, 0},
	{"TOT_INS", 0xc0010200, 0x0C0, 0x00, 0xc0010201, 0b11, 0},
	{"L2_DCM",  0xc0010202, 0x043, 0x00, 0xc0010203, 0b11, 0},
	{"L1_DCM",  0xc0010204, 0x041, 0x01, 0xc0010205, 0b11, 0},
	{"DP_OPS",  0xc0010206, 0x003, 0xF0, 0xc0010207, 0b11, 0},
	{"VEC_INS", 0xc0010208, 0x0CB, 0x04, 0xc0010209, 0b11, 0},
	{"TLB_DM",  0xc001020A, 0x046, 0x07, 0xc001020B, 0b11, 0},
	{"L3_CACHE_MISSES", 0xc0010240, 0x4E1, 0xF7, 0xc0010241, 0b0, 0}
};


static pthread_t rothread;
static idx_t store_idx;
static char* storekeys[MAX_ROLLOVER_STORE_KEYS]; //FIXME: make this variable size
static int nstorekeys = 0;
static char *root_path;
static int altheader;
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

#define LOGFILE "/var/log/store_msr_csv.log"

/**
 * This store is particular to the msr sampler because the msr sample has a particular data format of
 * name and one or more value pairs. The previous value of a counter is known to be invalid if its name has
 * been invalidated.
 * output format: permetric:  name (num), name (string), compid, numvals, derived_value(s)
 *
 * Notes:
 * - will write out a compid per metric, it is the one stored with the variable name.
 * - this store only accepts the one set
 * - this does not have config file.
 * - currently, the derived value is just a diff (not a rate).
 * - rollover: negative values return a 0. there is no flag to indicate that.
 * - invalid scenarios:
 *    - if a time for a metric is invalid (determined by 0 metric name), that time's values are zero.
 *    - if the previous time for a metric is invalid, that time's values are zero.
 *    _ if the name of the previous entry and this dont match, that time's values are zero
 *      (could have deliberately changed the metric and both be valid)
 *    - negative dt. this is not checked for.
 *    - there is no flag to indicate per core rollover.
 * - there is no function to iterate thru an idx. Right now, that memory is
 *   lost in destroy_store.
 */

struct setdatapoint { //raw data from the last mvec
	struct ldms_timestamp *ts;
	uint64_t* datavals;
};

struct ctrinfo {
	int nameidx;
	int numvals;
};

//If this is going to have the last dp, then a store_handle can only be for a particular sampler (not multiple samplers)
struct store_msr_csv_handle {
	struct ldmsd_store *store;
	int createDS;
	int printheader;
	char *path;
	FILE *file;
	FILE *headerfile;
	idx_t sets_idx;
	int numsets;
	int nummvecvals;
	int numctr;
	struct ctrinfo ctr[STORE_CTR_METRIC_MAX]; //FIXME: dynamic
	int step;
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

	struct store_msr_csv_handle *s_handle;

	pthread_mutex_lock(&cfg_lock);

	time_t appx = time(NULL);

	for (i = 0; i < nstorekeys; i++){
		if (storekeys[i] != NULL){
			s_handle = idx_find(store_idx, (void*)storekeys[i], strlen(storekeys[i]));
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
				s_handle->printheader = 1;
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
 * \brief get the index of the matching counter_assigment (based on wctl). Use this to get the string name
 */
static int getNameIdx(uint64_t wctlin){
	int i;

	if (wctlin == 0)
		return -1;

	//FIXME: get a better struct for this
	for (i = 0; i < MSR_MAXOPTIONS; i++){
		if (wctlin == counter_assignments[i].wctl){
			return i;
		}
	}

	return -1;
}

/**
 * \brief Calc the names for the translation
 */
static int calcTranslations(){
	int i;

	for (i = 0; i < MSR_MAXOPTIONS; i++){
		uint64_t w_reg = counter_assignments[i].w_reg;
		uint64_t event_hi = counter_assignments[i].event >> 8;
		uint64_t event_low = counter_assignments[i].event & 0xFF;
		uint64_t umask = counter_assignments[i].umask;
		uint64_t os_user = counter_assignments[i].os_user;

		counter_assignments[i].wctl = MSR_HOST << 40 | event_hi << 32 | MSR_CNT_MASK << 24 | MSR_INV << 23 | MSR_ENABLE << 22 | MSR_INTT << 20 | MSR_EDGE << 18 | os_user << 16 | umask << 8 | event_low;

	}
}



/**
 * \brief Create DS
 */
static int createDS(struct store_msr_csv_handle *s_handle,
		    ldms_mvec_t mvec){

	int num_metrics;
	char buf[STORE_CTR_NAME_MAX];

	int temp;
	int i;
	int rc;

	//well known format CtrX followed by CtrN_cYY, but probably in reverse order
	s_handle->createDS = 0;
	s_handle->numctr = 0;
	num_metrics = ldms_mvec_get_count(mvec);
	s_handle->nummvecvals = num_metrics;

	//one end or the other is Ctr0.
	int currctr = 0;
	snprintf(buf, STORE_CTR_NAME_MAX-1, "Ctr%d", currctr);
	const char* name = ldms_get_metric_name(mvec->v[0]);
	if (strcmp(name, buf) == 0){
		s_handle->step = 1;
	} else {
		name = ldms_get_metric_name(mvec->v[num_metrics-1]);
		if (strcmp(name, buf) == 0){
			s_handle->step = -1;
		} else {
			msglog(LDMS_LERROR, "store_msr_csv: unexpected Ctr0 location. Aborting\n");
			return -1;
		}
	}


	int beg = 0;
	int end = num_metrics;
	if (s_handle->step == -1){
		beg = num_metrics-1;
		end = -1;
	}
	currctr = -1;
	for (i = beg; i != end; i+= s_handle->step){
		name = ldms_get_metric_name(mvec->v[i]);
		if (strcmp(name, buf) == 0){
			currctr++;
			if (currctr == STORE_CTR_METRIC_MAX){
				msglog(LDMS_LERROR, "store_msr_csv: Too many counters <%d>. Aborting\n", currctr);
				return -1;
			}
			s_handle->ctr[currctr].nameidx = i;
			s_handle->ctr[currctr].numvals = 0;
			snprintf(buf, STORE_CTR_NAME_MAX-1, "Ctr%d", currctr+1);
		} else {
			s_handle->ctr[currctr].numvals++;
		}
	}

	s_handle->numctr = currctr+1;
	s_handle->nummvecvals = num_metrics;
	return 0;
}


/**
 * \brief Configuration
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value = NULL;
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

	calcTranslations();

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
	return  "    config name=store_msr_csv path=<path> altheader=<0/1>\n"
		"         - Set the root path for the storage of csvs.\n"
		"           path      The path to the root of the csv directory\n"
		"         - altheader Header in a separate file (optional, default 0)\n"
		"         - rollover  Greater than zero; enables file rollover and sets interval\n"
		"         - rolltype  [1-n] Defines the policy used to schedule rollover events.\n"
		ROLLTYPES
		"                     \n";
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
	struct store_msr_csv_handle *s_handle = _s_handle;
	return s_handle->ucontext;
}


static int print_header(struct store_msr_csv_handle *s_handle,
			ldms_mvec_t mvec)
{

	const char* name;
	int rc = 0;
	int i, j;


	/* Only called from Store which already has the lock */
	FILE* fp = s_handle->headerfile;
	if (!fp){
		msglog(LDMS_LDEBUG,"Cannot print header for store_msr_csv. No headerfile\n");
		return EINVAL;
	}
	s_handle->printheader = 0;

	if (s_handle->createDS){
		rc = createDS(s_handle, mvec);
		if (rc != 0){
			msglog(LDMS_LERROR, "store_msr: ERROR parsing mvec for createDS. Aborting\n");
			return -1;
		}
	}

	//Now write the header from the datastruct

	/* This allows optional loading a float (Time) into an int field and retaining usec as
	   a separate field */
	fprintf(fp, "#Time, Time_usec, DT, DT_usec");
	/* output format: permetric:  name (num), name (string), compid, numvals, derived_value(s) */
	for (i = 0; i < s_handle->numctr; i++){
		fprintf(fp, ", Ctr%d, Ctr%d_string, Ctr%d_CompId, Ctr%d_numvals",
			i, i, i, i);
		for (j = 0; j < s_handle->ctr[i].numvals; j++){
			fprintf(fp, ", Ctr%d_c%02d_der", i, j);
		}
	}
	fprintf(fp, "\n");

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
	struct store_msr_csv_handle *s_handle;
	int add_handle = 0;
	int rc = 0;

	pthread_mutex_lock(&cfg_lock);

	msglog(LDMS_LDEBUG, "store_msr_csv: entered new_store\n");
	s_handle = idx_find(store_idx, (void *)container, strlen(container));
	if (!s_handle) {

		msglog(LDMS_LDEBUG, "store_msr_csv: no handle for <%s>. creating\n", container);
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
		s_handle->createDS = 1;
		s_handle->printheader = 1;
		s_handle->numsets = 0;
		s_handle->nummvecvals = 0;
		s_handle->numctr = 0;
		s_handle->store_count = 0;
		s_handle->byte_count = 0;
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
	if (s_handle->printheader && !s_handle->headerfile){
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
			msglog(LDMS_LDEBUG,"store_msr_csv: Cannot open headerfile");
			goto err4;
		}
	}

	if (add_handle) {
		msglog(LDMS_LDEBUG, "store_msr_csv: adding handle for <%s>. \n", container);
		if (nstorekeys == (MAX_ROLLOVER_STORE_KEYS-1)){
			msglog(LDMS_LDEBUG, "Error: Exceeded max store keys\n");
			goto err5;
		} else {
			idx_add(store_idx, (void *)container,
				strlen(container), s_handle);
			storekeys[nstorekeys++] = strdup(container);
		}
	}

	pthread_mutex_unlock(&s_handle->lock);
	msglog(LDMS_LDEBUG, "store_msr_csv: new_store for <%s> successful\n", container);
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


	msglog(LDMS_LDEBUG, "store_msr_csv: in error path for new_store for <%s>. \n", container);

 out: //NO shandle OR successful
	pthread_mutex_unlock(&cfg_lock);
	return s_handle;
}

static int
store(ldmsd_store_handle_t _s_handle, ldms_set_t set, ldms_mvec_t mvec)
{

	/* NOTE: ldmsd_store invokes the lock on the whole s_handle */

	uint64_t comp_id;
	struct store_msr_csv_handle *s_handle;
	const struct ldms_timestamp *ts = ldms_get_timestamp(set);
	int rc;
	int i, j;

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

	if (s_handle->printheader) {
		rc = print_header(s_handle, mvec);
		if (rc != 0){
			msglog(LDMS_LDEBUG,"store_msr_csv: Error in print_header: %d\n", rc);
			pthread_mutex_unlock(&s_handle->lock);
			return rc;
		}
	}

	int num_metrics = ldms_mvec_get_count(mvec);
	if (num_metrics != s_handle->nummvecvals){
		msglog(LDMS_LERROR, "store_msr_csv: wrong number of vals in mvec <%d> expecting <%d>. Not storing\n",
		       num_metrics, s_handle->nummvecvals);
		return -1;
	}

	//now we get the vector, have to parse and compare to previous values to decide what to do

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
		dp->ts->sec = ts->sec;
		dp->ts->usec = ts->usec;

		dp->datavals = (uint64_t*) malloc((num_metrics)*sizeof(uint64_t));
		if (dp->datavals == NULL){
			pthread_mutex_unlock(&s_handle->lock);
			return ENOMEM;
		}
		ldms_metric_t* v = ldms_mvec_get_metrics(mvec); //new vals
		if (v == NULL) {
			pthread_mutex_unlock(&s_handle->lock);
			return EINVAL; //shouldnt happen
		}
		for (i = 0; i < s_handle->nummvecvals; i++){
			dp->datavals[i] = ldms_get_u64(v[i]);
		}
		goto out;
	}

	struct timeval prev, curr, diff;
	prev.tv_sec = dp->ts->sec;
	prev.tv_usec = dp->ts->usec;
	curr.tv_sec = ts->sec;
	curr.tv_usec = ts->usec;

	timersub(&curr, &prev, &diff);
	//08-23-15. Time diff of zero temporarily ok since we are doing diff and not rate
//	if ((diff.tv_sec == 0) && (diff.tv_usec == 0)){
//		msglog(LDMS_LDEBUG,"store_msr_csv: Time diff is zero for set %s. Skipping\n", ldms_get_set_name(set));
//		goto out;
//	}

	/* format: #Time, Time_usec, DT, DT_usec */
	fprintf(s_handle->file, "%"PRIu32".%06"PRIu32 ", %"PRIu32,
		ts->sec, ts->usec, ts->usec);
	fprintf(s_handle->file, ", %lu.%06lu, %lu",
		diff.tv_sec, diff.tv_usec, diff.tv_usec);


	ldms_metric_t* v = ldms_mvec_get_metrics(mvec); //new vals
	if (v == NULL) {
		pthread_mutex_unlock(&s_handle->lock);
		return EINVAL; //shouldnt happen
	}

	for (i = 0; i < s_handle->numctr; i++){
		int cidx = s_handle->ctr[i].nameidx;
		uint64_t cname = ldms_get_u64(mvec->v[cidx]);
		uint64_t lastname = dp->datavals[cidx];
		int comp_id = ldms_get_user_data(mvec->v[cidx]);
		int numvals = s_handle->ctr[i].numvals;
		int valid = 1;
		if ((cname == 0) || (lastname == 0) || (cname != lastname)){
			valid = 0;
		}

		//output format: permetric:  name (num), name (string), compid, numvals, derived_value(s)
		if (!valid){
			rc = fprintf(s_handle->file, ", %" PRIu64 ", %" PRIu64 ", %" PRIu32 ", %d",
				0, 0, comp_id, s_handle->ctr[i].numvals);
			if (rc < 0)
				msglog(LDMS_LERROR, "store_csv_msr: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;

			for (j = 0; j < s_handle->ctr[i].numvals; j++){
				rc = fprintf(s_handle->file, ", 0");
				if (rc < 0)
					msglog(LDMS_LERROR, "store_csv_msr: Error %d writing to '%s'\n",
					       rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
		} else {
			int nameidx = getNameIdx(cname);
			rc = fprintf(s_handle->file, ", %" PRIu64 ", %s, %" PRIu32 ", %d",
				     cname,  ((nameidx >= 0)? counter_assignments[nameidx].name: "N/A"),
				     comp_id, s_handle->ctr[i].numvals);
			if (rc < 0)
				msglog(LDMS_LERROR, "store_csv_msr: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;

			// we know dt > 0. 08-23-15 Temporarily dt == 0 has been restored, but that is ok since not doing divide
			for (j = 0; j < s_handle->ctr[i].numvals; j++){
				cidx+=s_handle->step;
				uint64_t val;
				uint64_t currval = ldms_get_u64(mvec->v[cidx]);
				if (currval == dp->datavals[cidx]){
					val = 0;
				} else if (currval > dp->datavals[cidx]){
					/* 08-23-15 Temporarily disabling rate.
					double temp = (double)(currval - dp->datavals[cidx]);
					//ROLLOVER - Should we assume ULONG_MAX is the rollover? Just use 0 for now....
					temp /= (double)(diff.tv_sec*1000000.0 + diff.tv_usec);
					temp *= 1000000.0;
					val = (uint64_t) temp;
					*/
					val = currval - dp->datavals[cidx];
				} else {
					//NOTE: no flag to tell this case
					val = 0;
				}
				rc = fprintf(s_handle->file, ", %" PRIu64, val);
				if (rc < 0)
					msglog(LDMS_LERROR, "store_csv_msr: Error %d writing to '%s'\n",
					       rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
		}
		if (rc < 0)
			msglog(LDMS_LERROR, "store_csv_msr: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
	}
	rc = fprintf(s_handle->file, "\n");

	//now copy over all the vec and the time
	dp->ts->sec = ts->sec;
	dp->ts->usec = ts->usec;
	for (i = 0; i < s_handle->nummvecvals; i++){
		dp->datavals[i] = ldms_get_u64(mvec->v[i]);
	}

out:
	s_handle->store_count++;
	pthread_mutex_unlock(&s_handle->lock);

	return 0;
}

static int flush_store(ldmsd_store_handle_t _s_handle)
{
	struct store_msr_csv_handle *s_handle = _s_handle;
	if (!s_handle) {
		msglog(LDMS_LDEBUG,"store_msr_csv: flush error.\n");
		return -1;
	}
	pthread_mutex_lock(&s_handle->lock);
	fflush(s_handle->file);
	pthread_mutex_unlock(&s_handle->lock);

	return 0;
}

static void close_store(ldmsd_store_handle_t _s_handle)
{
	struct store_msr_csv_handle *s_handle = _s_handle;
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
	struct store_msr_csv_handle *s_handle = _s_handle;
	if (!s_handle) {
		pthread_mutex_unlock(&cfg_lock);
		return;
	}

	pthread_mutex_lock(&s_handle->lock);
	msglog(LDMS_LDEBUG,"Destroying store_msr_csv with path <%s>\n", s_handle->path);

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

	s_handle->numctr = 0;

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

static struct ldmsd_store store_msr_csv = {
	.base = {
			.name = "msr_csv",
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
	return &store_msr_csv.base;
}

static void __attribute__ ((constructor)) store_msr_csv_init();
static void store_msr_csv_init()
{
	store_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_msr_csv_fini(void);
static void store_msr_csv_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(store_idx);
}
