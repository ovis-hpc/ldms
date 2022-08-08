/**
 * Copyright (c) 2014-2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2014-2022 Open Grid Computing, Inc. All rights reserved.
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


static int buffer_type = 1; /* autobuffering */
static int buffer_sz = 0;
#define MAX_ROLLOVER_STORE_KEYS 20
#define STORE_DERIVED_NAME_MAX 256
#define STORE_DERIVED_LINE_MAX 4096
#define STORE_DERIVED_METRIC_MAX 500

static pthread_t rothread;
static idx_t store_idx;
static char* storekeys[MAX_ROLLOVER_STORE_KEYS]; //TODO: make this variable size
static int nstorekeys = 0;
static char *root_path;
static int altheader;
static char* derivedconf = NULL;  //Mutliple derived files
static int ageusec = -1;
static int rollover;
/** rollempty determines if timed rollovers are performed even
 * when no data has been written (producing empty files).
 */
static int rollempty = 1;
static int rolltype;
/** ROLLTYPES documents rolltype and is used in help output. Also used for buffering. */
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

#define LOGFILE "/var/log/store_function_csv.log"

/**
 * store_function_csv - specify some metrics to be stored can be RAW or combo of other functions.
 * this is intended to replace store_dervied_csv.
 *
 * This can be used in conjunction with store_csv to have 1 file of all RAW
 * go to one location and 1 file of a subset of RAW and other functions to go to
 * another.
 *
 * Notes:
 * - Format of the configuration file:
 *   (0)schema  (1)new_name  (2)func  (3)num_dep_vars  (4)list_dep_vars (5)scale/thresh  (6)writeout
 *   a) options are space separated; list_dep_vars is comma separated.
 *   b) variables must be defined before they are used in subsequent variables
 *   c) will always apply scale as part of a function.
 *   d) NOTE: the data types supported and the data types in the calculation is limited
 *      and subject to change.
 * - Merics now matched on both name and schema.
 *   a) In case of a name collision, the name matching the base metric
 *      is preferentially chosen to determine the metric the calculation is based upon
 * - Validity. Any invalid value results in a zero writeout. For every variable, there is a valid indicator in the output.
 *   a) Rollover (determined by a neg value) and neg values - both return 0
 *   b) neg quantity in a calculation
 * - New in v3: if time diff is not positive, always write out something and flag. Flag is only for time.
 *   This is the same flag for time > ageusec.
 *   if its RAW data, write the val. if its RATE data, write zero and flag.
 *   (in v2 was: two timestamps for the same component with dt = 0 wont writeout. Presumably
 *   this shouldnt happen.)
 * - New in v3: redo order of RATE calculation to keep precision.
 *
 *   FIXME: Review the following:
 * - STORE_DERIVED_METRIC_MAX - is fixed value.
 * - there is no function to iterate thru an idx. Right now, that memory is
 *   lost in destroy_store.
 * - if a host goes down and comes back up, then may have a long time range
 *   between points. currently, this is still calculated, but can be flagged
 *   with ageout. Currently this is global, not per collector type
 * - Scale casting is still in development. Overflow is not checked for. This was inconsistent prior to mid Nov 2016.
 *   In Nov 2016, this has been made consistent and chosen to enable fractional and less than 1 values for the scale,
 *   so rely on the uint64_t being cast to double as part of the multiplication with the double scale, and as a result,
 *   there may be overflow. Writeout is still uint64_t.
 *
 * TODO:
 * - For the implict cast for scale operations, should this be bypassed for scale == 1?
 * - Currently only handles uint64_t scalar and vector types
 * - Currently only printsout uint64_t values. (cast)
 * - Fix the frees
 * - not keeping or writing the user data
 * - decide if should have option to write these out to multiple files for the same schema.
 */

static char* var_sep = ",";
static char type_sep = ':';

#define BYMSRNAME "BYMSRNAME"
#define MSR_MAXLEN 20LL

/**** definitions *****/
typedef enum {
	UNIVARIATE,
	BIVARIATE,
	MULTIVARIATE,
	VARIATE_END
} variate_t;


#ifdef MAX
#error __FILE__ "uses MAX as enum value. Macro MAX incompatible."
#endif
#ifdef MIN
#error __FILE__ "uses MIN as enum value. Macro MIN incompatible."
#endif

//NOTE: not implementing EUC yet.
typedef enum {
	RATE,
	DELTA,
	RAW,
	RAWTERM,
	MAX_N,
	MIN_N,
	SUM_N,
	AVG_N,
	SUB_AB,
	MUL_AB,
	DIV_AB,
	THRESH_GE, //scale is thresh
	THRESH_LT, //scale is thresh
	MAX,
	MIN,
	SUM, //sum across the vectors' dimension
	AVG,
	SUM_VS,
	SUB_VS,
	SUB_SV,
	MUL_VS,
	DIV_VS,
	DIV_SV,
	FCT_END //invalid
} func_t;

struct func_info{
	char* name; //name of the fct
	variate_t variatetype;
	int createreturn; //create space for the return vals (should usually be the case)
	int createstore; //create space to store data in addtion to the return vals (to be used in the calculation)
};

//order matters
struct func_info func_def[(FCT_END+1)] = {
	{"RATE", UNIVARIATE, 1, 1},
	{"DELTA", UNIVARIATE, 1, 1},
	{"RAW", UNIVARIATE, 1, 0},
	{"RAWTERM", UNIVARIATE, 0, 0},
	{"MAX_N", MULTIVARIATE, 1, 0},
	{"MIN_N", MULTIVARIATE, 1, 0},
	{"SUM_N", MULTIVARIATE, 1, 0},
	{"AVG_N", MULTIVARIATE, 1, 0},
	{"SUB_AB", BIVARIATE, 1, 0},
	{"MUL_AB", BIVARIATE, 1, 0},
	{"DIV_AB", BIVARIATE, 1, 0},
	{"THRESH_GE", UNIVARIATE, 1, 0},
	{"THRESH_LT", UNIVARIATE, 1, 0},
	{"MAX", UNIVARIATE, 1, 0},
	{"MIN", UNIVARIATE, 1, 0},
	{"SUM", UNIVARIATE, 1, 0},
	{"AVG", UNIVARIATE, 1, 0},
	{"SUM_VS", BIVARIATE, 1, 0},
	{"SUB_VS", BIVARIATE, 1, 0},
	{"SUB_SV", BIVARIATE, 1, 0},
	{"MUL_VS", BIVARIATE, 1, 0},
	{"DIV_VS", BIVARIATE, 1, 0},
	{"DIV_SV", BIVARIATE, 1, 0},
	{"FCT_END", VARIATE_END, 0, 0},
};
/******/

/****** per schema per instance data stores (stored in sets_idx) ******/
struct dinfo{ //values of the derived metrics, updated as the calculations go along.
	int dim;
	uint64_t* storevals;
	uint64_t* returnvals;
	int storevalid; //flag for if this is valid for dependent calculations.
	int returnvalid; //flag for if this is valid for dependent calculations.
};

struct setdatapoint{ //one of these for each instance for each schema
	struct ldms_timestamp *ts;
	struct dinfo* datavals; /* derived vals from the last timestep (one for each derived metric)
				   indicies are those of the derived metrics (not the sources).
				   The der and datavals are in the same order. */
};
/******/

/***** per schema (instance-data independent) info *******/
typedef enum {
	BASE,
	DER
} met_t;

struct idx_type{
	int i;
	int dim; /* the dimensionality of the var at this index. keeping it here so dont have
		    to go to the data or the metric array to get it */
	met_t typei; //indicates if it came from the set metrics or a derived
	enum ldms_value_type metric_type;
};

struct derived_data{ //the generic information about the derived metric
	char* name; // new variable name
	int idx; // the new variable idx in this array
	func_t fct;
	int dim; //dimensionality of this metric (this is dependent upon the dimensionality of the underlying metrics)
	int nvars; // number of input vars for this func
	struct idx_type* varidx; // array of the indicies of the input vars for this func
	double scale; //number to scale by. what should this type be?
	int writeout;
};
/******/

/*** per schema (includes instance data within the sets_idx) *******/
struct function_store_handle { //these are per-schema
	struct ldmsd_store *store;
	char *path;
	FILE *file;
	FILE *headerfile;
	struct derived_data* der[STORE_DERIVED_METRIC_MAX]; /* these are about the derived metrics (independent of instance)
							      TODO: dynamic. */
	int numder; /* there are numder actual items in the der array */
	idx_t sets_idx; /* to keep track of sets/data involved to do the diff (contains setdatapoint)
			   key is the instance name of the set. There will be N entries in this index, where N = number
			   of instances matching this schema (typically 1 per host aggregating from).
			   Each setdatapoint will have X datavals where X = number of derived metrics (numder).
			   The der and the datavals are in the same order */
	int numsets;
	printheader_t printheader;
	int parseconfig;
	char *store_key; /* this is the container+schema */
	char *schema; /* in v3 need to keep this for the comparison with the conf file */
	pthread_mutex_t lock;
	void *ucontext;
	int buffer_type;
	int buffer_sz;
	int64_t lastflush;
	int64_t store_count;
	int64_t byte_count;
};

/******/

static pthread_mutex_t cfg_lock;

static void printStructs(struct function_store_handle *s_handle);

static func_t enumFct(const char* fct){
	int i;

	//FIXME: Do this better later....
	for (i = 0; i < FCT_END; i++){
		if (strcmp(func_def[i].name, fct) == 0)
			return i;
	}

	return FCT_END;
}

static int config_buffer(char *bs, char *bt, int *rbs, int *rbt);

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


/* Time-based rolltypes will always roll the nonempty files when this
 * function is called.
 * Volume-based rolltypes must check and shortcircuit within this
 * function.
*/
static int handleRollover(){
	//get the config lock
	//for every handle we have, do the rollover

	int i;
	struct function_store_handle *s_handle;

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
				case 2:
					if (!s_handle->store_count && !rollempty)
						/* skip rollover of empty files */
						goto out;
					break;
				case 3:
					if (s_handle->store_count < rollover)  {
						pthread_mutex_unlock(&s_handle->lock);
						continue;
					} else {
						s_handle->store_count = 0;
						s_handle->lastflush = 0;
					}
					break;
				case 4:
					if (s_handle->byte_count < rollover) {
						pthread_mutex_unlock(&s_handle->lock);
						continue;
					} else {
						s_handle->byte_count = 0;
						s_handle->lastflush = 0;
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
				nfp = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
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
					nhfp = fopen_perm(tmp_headerpath, "w", LDMSD_DEFAULT_FILE_PERM);
					if (!nhfp){
						fclose(nfp);
						msglog(LDMSD_LERROR, "%s: Error: cannot open file <%s>\n",
						       __FILE__, tmp_headerpath);
					}
				} else {
					nhfp = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
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
				s_handle->store_count = 0;
out:
				pthread_mutex_unlock(&s_handle->lock);
			}
		}
	}

	pthread_mutex_unlock(&cfg_lock);

	return 0;

}

static void* rolloverThreadInit(void* m){
	while(1){
		int tsleep = 86400;
		switch (rolltype) {
		case 1:
		  tsleep = (rollover < MIN_ROLL_1) ? MIN_ROLL_1 : rollover;
		  break;
		case 2: {
		  time_t rawtime;
		  struct tm info;

		  time( &rawtime );
		  localtime_r( &rawtime, &info );
		  int secSinceMidnight = info.tm_hour * 3600 + info.tm_min * 60 + info.tm_sec;
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

static int __checkValidLine(const char* lbuf, const char* schema_name,
			const char* metric_name, const char* function_name,
			int nmet, char* metric_csv, double scale, int output,
			int iter, int rcl){

//NOTE: checking the existence of the dependent vars is done later

//	msglog(LDMSD_LDEBUG, "read:%d (%d): <%s> <%s> <%s> <%d> <%s> <%lf> <%d>\n",
//	       iter++, rcl,
//	       schema_name, metric_name, function_name, nmet, metric_csv,
//	       scale, output);

	if ((strlen(schema_name) > 0) && (schema_name[0] == '#')){
		// hashed lines are comments (means metric name cannot start with #)
		return -1;
	}

	if (rcl != 7) {
		msglog(LDMSD_LWARNING,"%s: (%d) Bad format in fct config file <%s> rc=%d. Skipping\n",
		       __FILE__, iter, lbuf, rcl);
		return -1;
	}

	if ((strlen(metric_name) == 0) || (strlen(function_name) == 0) ||
	    (strlen(metric_csv) == 0)){
		msglog(LDMSD_LWARNING,"%s: (%d) Bad vals in fct config file <%s>. Skipping\n",
		       __FILE__, iter, lbuf);
		return -1;
	}

	func_t tf = enumFct(function_name);
	if (tf == FCT_END) {
		msglog(LDMSD_LWARNING,"%s: (%d) Bad func in fct config file <%s> <%s>. Skipping\n",
		       __FILE__, iter, lbuf, function_name);
		return -1;
	}

	//now check the validity of the number of dependent vars for this func
	variate_t vart = func_def[tf].variatetype;
	int badvart = 0;
	switch(vart){
	case UNIVARIATE:
		if (nmet != 1)
			badvart = 1;
		break;
	case BIVARIATE:
		if (nmet != 2)
			badvart = 1;
		break;
	default:
		if (nmet <= 0)
			badvart = 1;
		break;
	}
	if (badvart){
		msglog(LDMSD_LWARNING,
		       "%s: (%d) Wrong number of dependent metrics (%d) for func <%s> in config file <%s>. Skipping\n",
		       __FILE__, iter, nmet, function_name, lbuf);
		return -1;
	}

	return 0;
};


static int __getCompcase(char* pch, char** temp_val){
	int compcase = 0; //default is by name
	char* temp_ii = strdup(pch);
	const char* ptr = strchr(temp_ii, type_sep);
	if (ptr){
		size_t index = ptr - temp_ii;
		if (index != (strlen(temp_ii) - 1)) {
			//otherwise colon at end, so don't do this
			ptr++;
			temp_ii[index] = '\0';
			if (strcmp(BYMSRNAME, ptr) == 0)
				compcase = 1;
			//if it doesnt match, then treat it as byname
			//where it compares against the whole name (including :)
		}
	}

	*temp_val = temp_ii;
	return compcase;
};


static int __matchBase(int compcase, char* strmatch, ldms_set_t set,
		       int* metric_arry, size_t metric_count){
	//this exists because of the special case to match an msr_interlagos variable

	int i, j;
	int rc;

	for (i = 0; i < metric_count; i++){
		const char* name = ldms_metric_name_get(set, metric_arry[i]);
		if (compcase == 0) {
			if (strcmp(name, strmatch) == 0)
				return i;
		} else {
			//special case of msr_interlagos
			//for this variable, check if its value matches the name
			enum ldms_value_type metric_type = ldms_metric_type_get(set, metric_arry[i]);
			if (metric_type == LDMS_V_CHAR_ARRAY) {
				const char* strval = ldms_metric_array_get_str(set, metric_arry[i]);
				//if this name matches the str we are looking for
				//get the ctrnum from the well known variable name CtrN_name
				int ctrnum = -1;
				if (strcmp(strval, strmatch) == 0){
					rc = sscanf(name, "Ctr%d_name", &ctrnum);
					if (rc != 1) //should only have this value for this name
						return -1;

					//well known metric names are CtrN_c and CtrN_n
					//should be able to assume order, but not doing that....
					char corename[MSR_MAXLEN];
					char numaname[MSR_MAXLEN];
					snprintf(corename, MSR_MAXLEN-1, "Ctr%d_c", ctrnum);
					snprintf(numaname, MSR_MAXLEN-1, "Ctr%d_n", ctrnum);
					for (j = 0; j < metric_count; j++){
						const char* strval2 = ldms_metric_name_get(set, metric_arry[j]);
						if ((strcmp(strval2, corename) == 0) ||
						    (strcmp(strval2, numaname) == 0)){
							return j;
						}
					}
				}
			}
		}
	}

	return -1;
}


static int calcDimValidate(struct derived_data* dd);

static struct derived_data* createDerivedData(const char* metric_name,
					      func_t tf,
					      int nmet, const char* metric_csv,
					      double scale, int output,
					      ldms_set_t set,
					      int* metric_arry, size_t metric_count,
					      int numder, struct derived_data** existder){

	//NOTE: we are NOT returning an error code

	char* temp_i = NULL;
	char* x_i = NULL;
	char* saveptr_i = NULL;
	char* pch;
	int count;
	int j;

	struct derived_data* tmpder = calloc(1, sizeof(struct derived_data));
	if (!tmpder)
		return tmpder;

	tmpder->name = strdup(metric_name);
	tmpder->fct = tf;
	tmpder->idx = -1; //gets set outside
	tmpder->dim = 0;
	tmpder->scale = scale;
	tmpder->writeout = output;

	//get the variable dependencies

	tmpder->nvars = nmet;
	tmpder->varidx = calloc((tmpder->nvars), sizeof(struct idx_type));
	if (!tmpder->varidx)
		goto err;

	for (j = 0; j < tmpder->nvars; j++)
		tmpder->varidx[j].i = -1; //dim is zero from calloc


	//split this csv list into the component names
	//lookup the component names in either the metric names or the derived names up until this point.
	temp_i = strdup(metric_csv);
	x_i = temp_i;
	saveptr_i = NULL;

	count = 0;
	pch = strtok_r(temp_i, var_sep, &saveptr_i);
	while (pch != NULL){
		if (count == tmpder->nvars){
			msglog(LDMSD_LERROR,
			       "%s: Too many input vars for input %s. expected %d on var %d\n",
			       __FILE__, metric_name, tmpder->nvars, count);
			goto err;
		}

		char* temp_ii = NULL;
		int compcase = __getCompcase(pch, &temp_ii);
		char* strmatch = NULL;
		int matchidx = -1;

		//does it depend on a base metric?
		strmatch = (compcase == 0 ? pch: temp_ii);
		matchidx = __matchBase(compcase, strmatch,
				     set, metric_arry, metric_count);
		free(temp_ii);

		if (matchidx >= 0) {
			tmpder->varidx[count].i = matchidx;
			tmpder->varidx[count].typei = BASE;
			enum ldms_value_type metric_type =
				ldms_metric_type_get(set, metric_arry[matchidx]);
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
				tmpder->varidx[count].dim = ldms_metric_array_get_len(set, metric_arry[matchidx]);
				break;
			default:
				//this includes CHAR_ARRAY (will write out only 1 header, will read with one call)
				tmpder->varidx[count].dim = 1;
				break;
			}
			tmpder->varidx[count].metric_type = metric_type;
			if (tmpder->fct != RAWTERM){
				if ((metric_type != LDMS_V_U64) && (metric_type != LDMS_V_U64_ARRAY)){
					const char* name = ldms_metric_name_get(set, metric_arry[matchidx]);
					msglog(LDMSD_LERROR,
					       "%s: unsupported type %d for base metric %s\n",
					       __FILE__, (int)metric_type, name);
					goto err;
				}
			}
		}

		//if not, does it depend on a derived metric?
		if (tmpder->varidx[count].i == -1){
			//check through all the derived metrics we already have
			for (j = 0; j < numder; j++){
				if (strcmp(pch, existder[j]->name) == 0){
					tmpder->varidx[count].i = j;
					tmpder->varidx[count].typei = DER;
					tmpder->varidx[count].dim = existder[j]->dim;
					tmpder->varidx[count].metric_type = LDMS_V_NONE;
					break;
				}
			}
		}

		if (tmpder->varidx[count].i == -1){
			msglog(LDMSD_LERROR,
			       "%s: Cannot find matching index for <%s>\n",
			       __FILE__, pch);
			goto err;
		}
		count++;
		pch = strtok_r(NULL, var_sep, &saveptr_i);
	}

	free(x_i);
	x_i = NULL;
	if (count != tmpder->nvars){
		msglog(LDMSD_LERROR,
		       "%s: inconsistent specification for nvars for metric %s. expecting %d got %d\n",
		       __FILE__, metric_name, tmpder->nvars, count);
		goto err;
	}

	//get the dimensionality of this metric. also check for valid dimensionality of its dependent metrics
	if (calcDimValidate(tmpder) != 0){
		msglog(LDMSD_LERROR,
		       "%s: Invalid dimensionality support for metric %s\n",
		       __FILE__, metric_name);
		goto err;
	}

	return tmpder;

err:
	if (x_i)
		free(x_i);
	x_i = NULL;

	if (tmpder){
		if (tmpder->name)
			free(tmpder->name);
		tmpder->name = NULL;
		if (tmpder->varidx)
			free (tmpder->varidx);
		tmpder->varidx = NULL;
		free(tmpder);
	}
	tmpder = NULL;

	return NULL;
}


/**
 * \brief Config for derived vars
 */
static int derivedConfig(char* fname_s, struct function_store_handle *s_handle, ldms_set_t set,
			 int* metric_arry, size_t metric_count){
	//read the file and keep the metrics names until its time to print the headers

	char lbuf[STORE_DERIVED_LINE_MAX];
	char metric_name[STORE_DERIVED_NAME_MAX];
	char schema_name[STORE_DERIVED_NAME_MAX];
	char function_name[STORE_DERIVED_NAME_MAX];
	char metric_csv[STORE_DERIVED_LINE_MAX];
	int nmet;
	double scale;
	int output;

	FILE *fp = NULL;


	char* s;
	int rc = EINVAL, rcl;
	int iter;

	//TODO: for now will read this in for every option (e.g., different base set for store)
	//Dont yet have a way to determine which of the handles a certain metric will be associated with

	msglog(LDMSD_LDEBUG, "%s: Function config file(s) is: <%s>\n",
	       __FILE__, fname_s);

	char* saveptr_o = NULL;
	char* temp_o = strdup(fname_s);
	char* x_o = temp_o;
	char* fname = strtok_r(temp_o, ",", &saveptr_o);

	s_handle->parseconfig = 0;
	s_handle->numder = 0;

	while(fname != NULL){
		msglog(LDMSD_LDEBUG, "%s: Parsing Function config file: <%s>\n",
		       __FILE__, fname);

		fp = fopen(fname, "r");
		if (!fp) {
			msglog(LDMSD_LERROR,"%s: Cannot open config file <%s>\n",
			       __FILE__, fname);
			free(temp_o);
			return EINVAL;
		}

		rc = 0;
		rcl = 0;
		iter = 0;
		do {
			//TODO: TOO many metrics. dynamically alloc
			if (s_handle->numder == STORE_DERIVED_METRIC_MAX) {
				msglog(LDMSD_LERROR,"%s: Too many metrics <%s>\n",
				       __FILE__, fname);
				rc = EINVAL;
				break;
			}

			lbuf[0] = '\0';
			s = fgets(lbuf, sizeof(lbuf), fp);
			if (!s)
				break;
			rcl = sscanf(lbuf, "%s %s %s %d %s %lf %d",
				     schema_name, metric_name, function_name, &nmet,
				     metric_csv, &scale, &output);
			iter++;
			if (__checkValidLine(lbuf, schema_name, metric_name, function_name, nmet,
					    metric_csv, scale, output, iter, rcl) != 0 ){
				continue;
			}

			//only keep this item if the schema matches
			if (strcmp(s_handle->schema, schema_name) != 0) {
//				msglog(LDMSD_LDEBUG, "%s: (%d) <%s> rejecting schema <%s>\n",
//				       __FILE__, iter, s_handle->store_key, schema_name);
				continue;
			}

			func_t tf = enumFct(function_name);
			struct derived_data* tmpder = createDerivedData(metric_name, tf,
									nmet, metric_csv,
									scale, output,
									set,
									metric_arry, metric_count,
									s_handle->numder,
									s_handle->der);
			if (tmpder != NULL){
//				msglog(LDMSD_LDEBUG, "store fct <%s> accepting metric <%s> schema <%s> (%d)\n",
//				       s_handle->store_key, metric_name, schema_name, iter);
				tmpder->idx = s_handle->numder;
				s_handle->der[s_handle->numder] = tmpder;
				s_handle->numder++;
			} else {
				msglog(LDMSD_LDEBUG, "store fct <%s> invalid spec for metric <%s> schema <%s> (%d): rejecting \n",
				       s_handle->store_key, metric_name, schema_name, iter);
			}
		} while (s);

		if (fp)
			fclose(fp);
		fp = NULL;

		fname = strtok_r(NULL, ",", &saveptr_o);
	}
	free(x_o);
	x_o = NULL;

	printStructs(s_handle);

	return rc;
}


/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"comp_type", "id_pos", "idpos"};
	int numdep = 3;


	for (i = 0; i < numdep; i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			msglog(LDMSD_LERROR, "store_csv: config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	value = av_value(avl, "agesec");
	if (value){
		msglog(LDMSD_LERROR, "store_csv: config argument agesec has been deprecated in favor of ageusec\n");
		return EINVAL;
	}

	return 0;
}

/**
 * configuration check for the buffer args
 */
static int config_buffer(char *bs, char *bt, int *rbs, int *rbt)
{
	int tempbs;
	int tempbt;


	if (!bs && !bt){
		*rbs = 1;
		*rbt = 0;
		return 0;
	}

	if (!bs && bt){
		msglog(LDMSD_LERROR,
		       "%s: Cannot have buffer type without buffer\n",
		       __FILE__);
		return EINVAL;
	}

	tempbs = atoi(bs);
	if (tempbs < 0){
		msglog(LDMSD_LERROR,
		       "%s: Bad val for buffer %d\n",
		       __FILE__, tempbs);
		return EINVAL;
	}
	if ((tempbs == 0) || (tempbs == 1)){
		if (bt){
			msglog(LDMSD_LERROR,
			       "%s: Cannot have no/autobuffer with buffer type\n",
			       __FILE__);
			return EINVAL;
		} else {
			*rbs = tempbs;
			*rbt = 0;
			return 0;
		}
	}

	if (!bt){
		msglog(LDMSD_LERROR,
		       "%s: Cannot have buffer size with no buffer type\n",
		       __FILE__);
		return EINVAL;
	}

	tempbt = atoi(bt);
	if ((tempbt != 3) && (tempbt != 4)){
		msglog(LDMSD_LERROR,
		       "%s: Invalid buffer type %d\n",
		       __FILE__, tempbt);
		return EINVAL;
	}

	if (tempbt == 4){
		//adjust bs for kb
		tempbs *= 1024;
	}

	*rbs = tempbs;
	*rbt = tempbt;

	return 0;
}


/**
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value = NULL;
	char *bvalue = NULL;
	char *dervalue = NULL;
	char *altvalue = NULL;
	char *ivalue = NULL;
	char *rvalue = NULL;
	void* arg = NULL;
	int buf = -1;
	int buft = -1;
	int roll = -1;
	int rollmethod = DEFAULT_ROLLTYPE;
	int rc;

	pthread_mutex_lock(&cfg_lock);

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		msglog(LDMSD_LERROR, "store_function failed config_check\n");
		pthread_mutex_unlock(&cfg_lock);
		return rc;
	}

	value = av_value(avl, "buffer");
	bvalue = av_value(avl, "buffertype");
	rc = config_buffer(value, bvalue, &buf, &buft);
	if (rc){
		pthread_mutex_unlock(&cfg_lock);
		return rc;
	}
	buffer_sz = buf;
	buffer_type = buft;

	value = av_value(avl, "path");
	if (!value){
		msglog(LDMSD_LERROR, "store_function missing path\n");
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	altvalue = av_value(avl, "altheader");

	rvalue = av_value(avl, "rollover");
	if (rvalue){
		roll = atoi(rvalue);
		if (roll < 0){
			msglog(LDMSD_LERROR, "store_function invalid rollover\n");
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

	rvalue = av_value(avl, "rollempty");
	if (rvalue){
		rollempty = atoi(rvalue);
		if (rollempty < 0)
			return EINVAL;
	}

	//TODO: make this variable per schema or similar.
	ivalue = av_value(avl, "ageusec");
	if (ivalue){
		if (atoi(ivalue) < 0) {
			msglog(LDMSD_LERROR, "store_function invalid ageusec\n");
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		} else {
			ageusec = atoi(ivalue);
		}
	}

	dervalue = av_value(avl, "derivedconf");
	if (!dervalue) {
		msglog(LDMSD_LERROR, "store_function missing derived conf\n");
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	if (root_path)
		free(root_path);

	root_path = strdup(value);
	if (!root_path) {
		msglog(LDMSD_LCRITICAL, "%s: ENOMEM\n", __FILE__);
		pthread_mutex_unlock(&cfg_lock);
		return ENOMEM;
	}

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

	return 0;
}

static void printStructs(struct function_store_handle *s_handle){
	int i, j;

	msglog(LDMSD_LDEBUG, "=========================================\n");
	for (i = 0; i < s_handle->numder; i++){
		msglog(LDMSD_LDEBUG, "Schema <%s> New metric <%s> idx %d writeout %d\n",
		       s_handle->schema,
		       s_handle->der[i]->name,
		       s_handle->der[i]->idx,
		       s_handle->der[i]->writeout);
		msglog(LDMSD_LDEBUG, "\tfun: %s dim %d scale %g\n",
		       func_def[s_handle->der[i]->fct].name,
		       s_handle->der[i]->dim,
		       s_handle->der[i]->scale);
		msglog(LDMSD_LDEBUG, "\tDepends on vars (type:idx)\n");
		for (j = 0; j < s_handle->der[i]->nvars; j++){
			msglog(LDMSD_LDEBUG, "\t\t%d:%d\n", s_handle->der[i]->varidx[j].typei,
			       s_handle->der[i]->varidx[j].i);
		}
	}
	msglog(LDMSD_LDEBUG, "=========================================\n");
}

static void term(struct ldmsd_plugin *self)
{

	//FIXME: update this for the free's.
	//Keep in mind restart and what vals have to be kept (if any).

	if (root_path)
		free(root_path);
	if (derivedconf)
		free(derivedconf);
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=store_function_csv [path=<path> altheader=<0|1>]\n"
		"                rollover=<num> rolltype=<num>\n"
		"                [buffer=<0/1/N> buffertype=<3/4>]\n"
                "                 derivedconf=<fullpath> [ageusec=<sec>]\n"
		"         - Set the root path for the storage of csvs and some parameters.\n"
		"           path       The path to the root of the csv directory\n"
		"         - altheader  Header in a separate file (optional, default 0)\n"
		"         - buffer    0 to disable buffering, 1 to enable it with autosize (default)\n"
		"                     N > 1 to flush after that many kb (> 4) or that many lines (>=1)\n"
		"         - buffertype [3,4] Defines the policy used to schedule buffer flush.\n"
		"                      Only applies for N > 1. Same as rolltypes.\n"
		"         - rollover   Greater than zero; enables file rollover and sets interval\n"
		"         - rollempty  0/1; 0 suppresses rollover of empty files, 1 allows it (default)\n"
		"         - rolltype   [1-n] Defines the policy used to schedule rollover events.\n"
		ROLLTYPES
		"         - derivedconf Full path to derived config files. csv for multiple files.\n"
		"         - ageusec     Set flag field if dt > this val in usec.\n"
		"                       (Optional default no value used. (although neg time will still flag))\n";
}


static void *get_ucontext(ldmsd_store_handle_t _s_handle)
{
	struct function_store_handle *s_handle = _s_handle;
	return s_handle->ucontext;
}

static int print_header_from_store(struct function_store_handle *s_handle,
				   ldms_set_t set, int* metric_arry, size_t metric_count)
{

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
		msglog(LDMSD_LERROR, "%s: Cannot print header for store_function_csv. No headerfile\n",
			__FILE__);
		return EINVAL;
	}

	if (s_handle->parseconfig){
		rc = derivedConfig(derivedconf, s_handle, set, metric_arry, metric_count);
		if (rc != 0) {
			msglog(LDMSD_LERROR,"%s: derivedConfig failed for store_function_csv. \n",
			       __FILE__);
			return rc;
		}
	}


	/* This allows optional loading a float (Time) into an int field and retaining usec as
	   a separate field */
	fprintf(fp, "#Time,Time_usec,DT,DT_usec");
	fprintf(fp, ",ProducerName");
	fprintf(fp, ",component_id,job_id");

	//Print the header using the metrics associated with this set
	for (i = 0; i < s_handle->numder; i++){
		if (s_handle->der[i]->writeout) {
			if (s_handle->der[i]->dim == 1) {
				fprintf(fp, ",%s,%s.Flag", s_handle->der[i]->name, s_handle->der[i]->name);
			} else {
				for (j = 0; j < s_handle->der[i]->dim; j++)
					fprintf(fp, ",%s.%d", s_handle->der[i]->name, j);
				fprintf(fp, ",%s.Flag", s_handle->der[i]->name);
			}
		}
	}
	fprintf(fp, ",TimeFlag\n");

	/* Flush for the header, whether or not it is the data file as well */
	fflush(fp);
	fsync(fileno(fp));

	fclose(s_handle->headerfile);
	s_handle->headerfile = 0;

	return 0;
}


static ldmsd_store_handle_t
open_store(struct ldmsd_store *s, const char *container, const char* schema,
		struct ldmsd_strgp_metric_list *list, void *ucontext)
{
	struct function_store_handle *s_handle = NULL;
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
		s_handle->buffer_sz = buffer_sz;
		s_handle->buffer_type = buffer_type;
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
		s_handle->file = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
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
				s_handle->headerfile = fopen_perm(tmp_headerpath, "w", LDMSD_DEFAULT_FILE_PERM);
			} else if (s_handle->printheader == DO_PRINT_HEADER){
				s_handle->headerfile = fopen_perm(tmp_headerpath, "a+", LDMSD_DEFAULT_FILE_PERM);
			}
		} else {
			s_handle->headerfile = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
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
				msglog(LDMSD_LERROR, "%s: Error: Exceeded max store keys\n",
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

static int calcDimValidate(struct derived_data* dd){
	/* - We can generally calculate the dimensionality of a derived metric from
	   the known dimensionalities of its dependent metrics.
	   - Also check for correct dimensionalities wrt to the function and the
	   other dependent metrics
	   - Note that we have already checked that there are the right number of dependent vars
	*/

	func_t fct = dd->fct;
	int nvals = dd->nvars; //dependent vars
	struct idx_type* vals = dd->varidx; //dependent vars indicies

	switch(fct){
	case RATE:
	case DELTA:
	case RAW:
	case RAWTERM:
	case THRESH_GE:
	case THRESH_LT:
		//these are all univariate and are of the same dimensionality as the dependent
		dd->dim = vals[0].dim;
		return 0;
		break;
	case MAX_N:
	case MIN_N:
	case SUM_N:
	case AVG_N:
	{
		int tmpdim = vals[0].dim;
		int i;

		for (i = 1; i < nvals-1; i++){
			if (vals[i].dim != tmpdim)
				return EINVAL;
		}
		dd->dim = tmpdim;
		return 0;
	}
		break;
	case SUB_AB:
	case MUL_AB:
	case DIV_AB:
		//these are all bivariate and are of the same dimensionality as the dependent
		if (vals[0].dim != vals[1].dim)
			return EINVAL;
		dd->dim = vals[0].dim;
		return 0;
		break;
	case MAX:
	case MIN:
	case AVG:
	case SUM:
		//these are all univariate and are dim 1
		dd->dim = 1;
		return 0;
		break;
	case SUB_SV:
	case DIV_SV:
		//check for scalar and vector
		//these are all bivariate and are of the same dimensionality vector
		if (vals[0].dim != 1)
			return EINVAL;
		dd->dim = vals[1].dim;
		return 0;
	case SUM_VS:
	case SUB_VS:
	case MUL_VS:
	case DIV_VS:
		//check for vector and scalar
		//these are all bivariate and are of the same dimensionality vector
		if (vals[1].dim != 1)
			return EINVAL;
		dd->dim = vals[0].dim;
		return 0;
		break;
	default:
		msglog(LDMSD_LERROR, "%s: Error - No code to validate function %s\n"
		       __FILE__, fct);
		return EINVAL;
		break;
	}

	return EINVAL;
};

static int doRAWTERMFunc(ldms_set_t set, struct function_store_handle *s_handle,
			 int* metric_array, struct derived_data* dd){

	//using these to ease updating the data for this metric
	struct idx_type* vals = dd->varidx; //dependent vars indicies. only 1
	int i = vals[0].i;
	enum ldms_value_type rtype = vals[0].metric_type;
	double scale = dd->scale;
	int dim = dd->dim;
	int rc;
	int j;

	//it can only be a base.
	//it must be valid, becuase the raw data is valid - will writeout flag 0 at end
	switch(rtype){
	case LDMS_V_CHAR:
		//scale is unused
		rc = fprintf(s_handle->file, ",%c",
			     ldms_metric_get_char(set, metric_array[i]));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_U8:
		rc = fprintf(s_handle->file, ",%hhu",
			     (uint8_t)((double)(ldms_metric_get_u8(set, metric_array[i])) * scale));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_S8:
		rc = fprintf(s_handle->file, ",%hhd",
			     (int8_t)((double)(ldms_metric_get_s8(set, metric_array[i])) * scale));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_U16:
		rc = fprintf(s_handle->file, ",%hu",
			     (uint16_t)((double)(ldms_metric_get_u16(set, metric_array[i])) * scale));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_S16:
		rc = fprintf(s_handle->file, ",%hd",
			     (int16_t)((double)(ldms_metric_get_s16(set, metric_array[i])) * scale));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_U32:
		rc = fprintf(s_handle->file, ",%" PRIu32,
			     (uint32_t)((double)(ldms_metric_get_u32(set, metric_array[i])) * scale));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_S32:
		rc = fprintf(s_handle->file, ",%" PRId32,
			     (int32_t)((double)(ldms_metric_get_s32(set, metric_array[i])) * scale));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_U64:
		rc = fprintf(s_handle->file, ",%"PRIu64,
			     (uint64_t)((double)(ldms_metric_get_u64(set, metric_array[i])) * scale));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_S64:
		rc = fprintf(s_handle->file, ",%" PRId64,
			     (int64_t)((double)(ldms_metric_get_s64(set, metric_array[i])) * scale));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_F32:
		rc = fprintf(s_handle->file, ",%f",
			     (float)(ldms_metric_get_float(set, metric_array[i]) * scale));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_D64:
		rc = fprintf(s_handle->file, ",%lf",
			     (ldms_metric_get_double(set, metric_array[i]) * scale));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_CHAR_ARRAY:
		//scale unused
		rc = fprintf(s_handle->file, ",%s",
			     ldms_metric_array_get_str(set, metric_array[i]));
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	case LDMS_V_U8_ARRAY:
		for (j = 0; j < dim; j++){
			rc = fprintf(s_handle->file, ",%hhu",
				     (uint8_t)((double)(ldms_metric_array_get_u8(set, metric_array[i], j)) * scale));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}
		break;
	case LDMS_V_S8_ARRAY:
		for (j = 0; j < dim; j++){
			rc = fprintf(s_handle->file, ",%hhd",
				     (int8_t)((double)(ldms_metric_array_get_s8(set, metric_array[i], j))* scale));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}
		break;
	case LDMS_V_U16_ARRAY:
		for (j = 0; j < dim; j++){
			rc = fprintf(s_handle->file, ",%hu",
				     (uint16_t)((double)(ldms_metric_array_get_u16(set, metric_array[i], j)) * scale));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}
		break;
	case LDMS_V_S16_ARRAY:
		for (j = 0; j < dim; j++){
			rc = fprintf(s_handle->file, ",%hd",
				     (int16_t)((double)(ldms_metric_array_get_s16(set, metric_array[i], j)) * scale));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}
		break;
	case LDMS_V_U32_ARRAY:
		for (j = 0; j < dim; j++){
			rc = fprintf(s_handle->file, ",%" PRIu32,
				     (uint32_t)((double)(ldms_metric_array_get_u32(set, metric_array[i], j)) * scale));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}
		break;
	case LDMS_V_S32_ARRAY:
		for (j = 0; j < dim; j++){
			rc = fprintf(s_handle->file, ",%" PRId32,
				     (int32_t)((double)(ldms_metric_array_get_s32(set, metric_array[i], j)) * scale));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}
		break;
	case LDMS_V_U64_ARRAY:
		for (j = 0; j < dim; j++){
			rc = fprintf(s_handle->file, ",%" PRIu64,
				     (uint64_t)((double)(ldms_metric_array_get_u64(set, metric_array[i], j)) * scale));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}
		break;
	case LDMS_V_S64_ARRAY:
		for (j = 0; j < dim; j++){
			rc = fprintf(s_handle->file, ",%" PRId64,
				     (int64_t)((double)(ldms_metric_array_get_s64(set, metric_array[i], j)) * scale));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}
		break;
	case LDMS_V_F32_ARRAY:
		for (j = 0; j < dim; j++){
			rc = fprintf(s_handle->file, ",%f",
				     (float)(ldms_metric_array_get_float(set, metric_array[i], j) * scale));
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}
		break;
	case LDMS_V_D64_ARRAY:
		for (j = 0; j < dim; j++){
			rc = fprintf(s_handle->file, ",%lf",
				     ldms_metric_array_get_double(set, metric_array[i], j) * scale);
			if (rc < 0)
				msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
			else
				s_handle->byte_count += rc;
		}
		break;
	default:
		//print no value
		rc = fprintf(s_handle->file, ",");
		if (rc < 0)
			msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
			       rc, s_handle->path);
		else
			s_handle->byte_count += rc;
		break;
	}

	//print the flag -- which is always 0
	rc = fprintf(s_handle->file, ",0");
	if (rc < 0)
		msglog(LDMSD_LERROR, "store_csv: Error %d writing to '%s'\n",
		       rc, s_handle->path);
	else
		s_handle->byte_count += rc;

	return 1; //always valid

};

/**
 * Call this function when expression interpreter logic breaks down.
 * I.e. at unreachable default branches in switch statements which
 * may become reachable if the func_t expands without correct matching expansion
 * in every logic branch.
 */
static void token_error(func_t fct, const char *expected, int line) {
	msglog(LDMSD_LERROR, "%s: unexpected func_t value %d, expected one of: at line %d. Did the function syntax expand?\n",__FILE__, fct, expected, line);
	exit(1);
}

#define TOKEN_ERR(f, ex) token_error(f, ex, __LINE__)

static int doFunc(ldms_set_t set, int* metric_arry,
		  struct setdatapoint* dp, struct derived_data* dd,
		  struct timeval diff, int flagtime){
//		CAN PASS IN  struct timeval curr, struct timeval prev, for debugging...

	/**
	 * NOTE: have to make tradeoffs in the chances of overflowing with casts
	 * and having the scale enable resolutions of diffs. Overflow is not checked for. See additional notes at start of file.
	 * This has been chosen to enable fractional and less than 1 values for the scale,
	 *   so rely on the uint64_t being cast to double as part of the multiplication with the double scale, and as a result,
	 *   there may be overflow. Writeout is still uint64_t.
	 *
	 * Made the following choices for the order of operations:
	 * RAW -     Apply scale after the value. Value Scale is cast to uint64_t. Then assign to uint64_t.
	 * RATE -    Subtract. Multiply by the scale, with explicit cast to double. Divide by time. Finally assign to u64.
	 *               This should allow you to shift the values enough to resolve differences that would
	 *               have been washed out in the division by time.
	 * DELTA -   Apply scale after the diff. Same cast and assignment as in RAW.
	 * SUM_XY (includes vector combinations)
	 *       -  Apply scale after the final SUM. Same cast and assignment as in RAW.
	 * SUB_XY (includes vector combinations)
	 *       -  Apply scale after the SUB. Same cast and assignment as in RAW.
	 * MUL_XY (includes vector combinations)
	 *       -  Apply scale after the MUL. Same cast and assignment as in RAW.
	 * DIV_XY (includes vector combinations)
	 *       -  Cast individual values to double before the DIV. Then apply scale as a double. Then assign to uint64_t.
	 * MIN/MAX/SUM - Apply scale after the function. Same case and assignment as in RAW.
	 * AVG - Sum. Multiply by the scale, with explict cast to double. Divide by N. Finally assign to u64.
	 * NOTE: THRESH functions have no scale (scale is the thresh)
	 *
	 * - Test mode for scale values with no integer part (ie. -1 < scale < 1), the casts are done differently, since all
	 *   of the value would be lost in the roundoff. This method does not check for overflow when it does this handling.
	 *   This is controlled by a flag SUB_INT_SCALE, whose behavior may change. Even when this flag is used, however,
	 *   the writeout is still uint64. FOR RAWTERM ONLY Currently.
	 *
	 * RETURNS:
	 * - Invalid computations due to overflow in the cast are not checked for nor marked.
	 * - The following invalid computations result in a 0 result value:
	 * -- Any computation involving an invalid value (derived values only are flagged this way)
	 * -- Negative values from a subtraction: RATE, DELTA, SUB_XY
	 * -- Negative dt: RATE
	 * Returns the valid flag (not an error code) which indicates the validity of the result.
	 */

	//TODO: return value could be used to flag for overflow in the future
	//the variables are such that any dependencies on other vars have already been updated by the time they get here.

	//FIXME....will want to add in checks that the dimension of the inputs are what we epxect incase
	//the user has messed up the schema

	//dp is the while setdatapoint (has the whole array. dd->idx is the index of the one im on).
	//dd is a pointer to the dd im on.

	//using these to ease updating the data for this metric
	func_t fct = dd->fct;
	int nvals = dd->nvars; //dependent vars
	struct idx_type* vals = dd->varidx; //dependent vars indicies
	double scale = dd->scale;
	int dim = dd->dim;
	int idx = dd->idx;
	uint64_t* retvals = dp->datavals[idx].returnvals;
	int* retvalid = &(dp->datavals[idx].returnvalid);
	uint64_t* storevals = dp->datavals[idx].storevals;
	int* storevalid = &(dp->datavals[idx].storevalid);

	int i, j;

//	msglog(LDMSD_LDEBUG, "before computation: %s oldtime=@%lu dim %d retvalid=%d storevalid=%d flagtime=%d\n",
//	       dd->name, prev.tv_sec, dim, *retvalid, *storevalid, flagtime );
//	for (j = 0; j < dim; j++){
//		msglog(LDMSD_LDEBUG, "%d: %llu\n", j, retvals[j]);
//	};

	switch (fct){
	case RAW:
	case THRESH_GE:
	case THRESH_LT:
		//this is univariate...and the same dimensionality as the var
		if (vals[0].typei == BASE) {
			*retvalid = 1;
			if (vals[0].metric_type == LDMS_V_U64){
				uint64_t temp = ldms_metric_get_u64(set, metric_arry[vals[0].i]);
//				msglog(LDMSD_LDEBUG, "getting value 0 %llu for var %d\n", temp, vals[0].i);
				switch(fct){
				case THRESH_GE:
					retvals[0] = (temp >= scale ? 1:0);
					break;
				case THRESH_LT:
					retvals[0] = (temp < scale ? 1:0);
					break;
				case RAW:
					retvals[0] = temp * scale;
					break;
				default:
					/* NOTREACHED */
					TOKEN_ERR(fct, "THRESH_GE, THRESH_LT,"
						" RAW");
					break;
				}
			} else { //it must be an array
				for (j = 0; j < dim; j++){
					uint64_t temp = ldms_metric_array_get_u64(set, metric_arry[vals[0].i], j);
//					msglog(LDMSD_LDEBUG, "getting value 0(%d) %llu for var %d\n", j, temp, vals[0].i);
					switch(fct){
					case THRESH_GE:
						retvals[j] = (temp >= scale ? 1:0);
						break;
					case THRESH_LT:
						retvals[j] = (temp < scale ? 1:0);
						break;
					case RAW:
						retvals[j] = temp * scale;
						break;
					default:
						/* NOTREACHED */
						TOKEN_ERR(fct, "THRESH_GE,"
							" THRESH_LT, RAW");
						break;
					}
				}
			}
		} else {
			*retvalid = dp->datavals[vals[0].i].returnvalid;
			if (*retvalid){
				for (j = 0; j < dim; j++) {
					uint64_t temp = dp->datavals[vals[0].i].returnvals[j];
//					msglog(LDMSD_LDEBUG, "getting value 0(%d) %llu for var %d\n", j, temp, vals[0].i);
					switch(fct){
					case THRESH_GE:
						retvals[j] = (temp >= scale ? 1:0);
						break;
					case THRESH_LT:
						retvals[j] = (temp < scale ? 1:0);
						break;
					case RAW:
						retvals[j] = temp * scale;
						break;
					default:
						/* NOTREACHED */
						TOKEN_ERR(fct, "THRESH_GE,"
							" THRESH_LT, RAW");
						break;
					}
				}
			} else {
				for (j = 0; j < dim; j++)
					retvals[j] = 0;
			}
		}
		break;
	case MAX:
	case MIN:
	case SUM:
	case AVG:
		//this is univariate...but dimensionality 1 result
		if (vals[0].typei == BASE) {
			*retvalid = 1;
			if (vals[0].metric_type == LDMS_V_U64){
				retvals[0] = ((double)(ldms_metric_get_u64(set, metric_arry[vals[0].i])) * scale);
			} else { //it must be an array
				retvals[0] = ldms_metric_array_get_u64(set, metric_arry[vals[0].i], 0);
				for (j = 1; j < vals[0].dim; j++) {
					uint64_t curr = ldms_metric_array_get_u64(set, metric_arry[vals[0].i], j);
					switch(fct){
					case MAX:
						if (retvals[0] < curr)
							retvals[0] = curr;
						break;
					case MIN:
						if (retvals[0] > curr)
							retvals[0] = curr;
						break;
					case SUM:
					case AVG:
						retvals[0] += curr;
						break;
					default:
						/* NOTREACHED */
						TOKEN_ERR(fct, "MIN,MAX,SUM,AVG");
						break;
					}
				}
				if (fct == AVG) {
					retvals[0] = (uint64_t)(((double)(retvals[0]) * scale)/(double)(vals[0].dim));
				} else {
					retvals[0] *= scale;
				}
			}
		} else {
			*retvalid = dp->datavals[vals[0].i].returnvalid;
			if (*retvalid){
				retvals[0] = (dp->datavals[vals[0].i].returnvals[0]);
				for (j = 1; j < vals[0].dim; j++){
					uint64_t curr = dp->datavals[vals[0].i].returnvals[j];
					switch(fct){
					case MAX:
						if (retvals[0] < curr)
							retvals[0] = curr;
						break;
					case MIN:
						if (retvals[0] > curr)
							retvals[0] = curr;
						break;
					case SUM:
					case AVG:
						retvals[0] += curr;
						break;
					default:
						/* NOTREACHED */
						TOKEN_ERR(fct, "MIN,MAX,SUM,AVG");
						break;
					}
				}
				if (fct == AVG) {
					retvals[0] = (uint64_t)(((double)(retvals[0]) * scale)/(double)vals[0].dim);
				} else {
					retvals[0] *= scale;
				}
			} else {
				retvals[0] = 0;
			}
			//store doesnt matter
		}
		break;
	case RATE:
	case DELTA:
	{
		//this is univariate...and the same dimensionality as the var
		/* - retval is not valid if a) storeval is invalid, b) newval is invalid,
		 * c) back in time, d) any negative values
		 * - storeval will be made invalid if newval is invalid (this will only
		 *  happen if the new dependent variable is invalid)
		 *  - note that storeval is not retval. it is the new vals (from which to do the diff)
		 */

		uint64_t* temp = calloc(dim, sizeof(uint64_t));
		int tempvalid = 1;
		*retvalid = 1;
		if (!temp) {
			*retvalid = 0;
			tempvalid = 0;
			goto oom1;
		}

		if (vals[0].typei == BASE) {
			if (vals[0].metric_type == LDMS_V_U64){
				temp[0] = ldms_metric_get_u64(set, metric_arry[vals[0].i]);
//				msglog(LDMSD_LDEBUG, "getting value 0 %llu for var %d\n", temp[0], vals[0].i);
			} else { //it must be an array
				for (j = 0; j < dim; j++) {
					temp[j] = ldms_metric_array_get_u64(set, metric_arry[vals[0].i], j);
//					msglog(LDMSD_LDEBUG, "getting value %d %llu for var %d\n", j, temp[j], vals[0].i);
					if (temp[j] < storevals[j])
						*retvalid = 0;
				}
			}
		} else {
			//...this is about the validity of current value of the dependent variable
			*retvalid = dp->datavals[vals[0].i].returnvalid;
			tempvalid = *retvalid;
			for (j = 0; j < dim; j++) {
				temp[j] = dp->datavals[vals[0].i].returnvals[j];
//				msglog(LDMSD_LDEBUG, "getting value %d %llu for var %d\n", j, temp[j], vals[0].i);
				if (temp[j] < storevals[j])
					*retvalid = 0; //return also invalid if negative
			}
		}

		if (!*storevalid || flagtime){ //return also invalid if back in time or this store invalid
//			msglog(LDMSD_LDEBUG, "store is not valid or flag time, so set return invalid\n");
			*retvalid = 0;
		} else {
//			msglog(LDMSD_LDEBUG, "store is valid and not flag time, so set return valid\n");
		}

		if (*retvalid){
			if (fct == DELTA){
//				msglog(LDMSD_LDEBUG, "ret valid, fctn delta, dim %d, scale %g\n", dim, scale);
				for (j = 0; j < dim; j++){
//					msglog(LDMSD_LDEBUG, "subtracting %llu - %llu\n", temp[j], storevals[j]);
					retvals[j] = (uint64_t)((double)(temp[j] - storevals[j])*scale);
//					msglog(LDMSD_LDEBUG, "setting ret[%d]=%llu\n", j, retvals[j]);
				}
			} else { //RATE
				double dt_usec = (double)(diff.tv_sec*1000000+diff.tv_usec);
				for (j = 0; j < dim; j++)
					retvals[j] = (uint64_t)((((double)(temp[j] - storevals[j])*1000000.0)*scale)/dt_usec);
			}
		} else {
//			msglog(LDMSD_LDEBUG, "ret not valid. setting all values to zero\n");
			for (j = 0; j < dim; j++)
				retvals[j] = 0;
		}
oom1:
		if (tempvalid){
//			msglog(LDMSD_LDEBUG, "new value valid. setting storevals to new values\n");
			for (j = 0; j < dim; j++) //dont store the scale, since it will be reapplied next time
				storevals[j] = temp[j];
		} else {
//			msglog(LDMSD_LDEBUG, "new value not valid. setting storevals to 0\n");
			for (j = 0; j < dim; j++)
				storevals[0] = 0;
		}
		free(temp);
		*storevalid = tempvalid;
	}
	break;
	case MAX_N:
	case MIN_N:
		//this is multivariate...and the same dimensionality as the var
		/* retval is not valid if any of the inputs is invalid */
		//storeval does not matter

		*retvalid = 1;
		if (vals[0].typei == BASE) {
			if (vals[0].metric_type == LDMS_V_U64){
				retvals[0] = ldms_metric_get_u64(set, metric_arry[vals[0].i]);
			} else {
				for (j = 0; j < dim; j++)
					retvals[j] = ldms_metric_array_get_u64(set, metric_arry[vals[0].i], j);
			}
		} else { //it must be an array
			*retvalid *= dp->datavals[vals[0].i].returnvalid;
			if (*retvalid == 0){ //abort
				break;
			} else {
				for (j = 0; j < dim; j++)
					retvals[j] = dp->datavals[vals[0].i].returnvals[j];
			}
		}

		for (i = 1; i < nvals; i++){
			if (vals[i].typei == BASE) {
				if (vals[i].metric_type == LDMS_V_U64){
					uint64_t temp =  ldms_metric_get_u64(set, metric_arry[vals[i].i]);
					if (fct == MAX_N){
						if (retvals[0] < temp)
							retvals[0] = temp;
					} else {
						if (retvals[0] > temp)
							retvals[0] = temp;
					}
				} else {
					if (fct == MAX_N){
						for (j = 0; j < dim; j++){
							uint64_t temp = ldms_metric_array_get_u64(set, metric_arry[vals[i].i], j);
							if (retvals[j] < temp)
								retvals[j] = temp;
						}
					} else {
						for (j = 0; j < dim; j++){
							uint64_t temp = ldms_metric_array_get_u64(set, metric_arry[vals[i].i], j);
							if (retvals[j] > temp)
								retvals[j] = temp;
						}
					}
				}
			} else { //it must be an array
				*retvalid *= dp->datavals[vals[i].i].returnvalid;
				if (*retvalid == 0){ //abort
					break;
				} else {
					if (fct == MAX_N){
						for (j = 0; j < dim; j++) {
							uint64_t temp = dp->datavals[vals[i].i].returnvals[j];
							if (retvals[j] < temp)
								retvals[j] = temp;
						}
					} else {
						for (j = 0; j < dim; j++) {
							uint64_t temp = dp->datavals[vals[i].i].returnvals[j];
							if (retvals[j] > temp)
								retvals[j] = temp;
						}
					}
				}
			}
		}

		if (*retvalid){
			for (j = 0; j < dim; j++)
				retvals[j] *= scale;
		} else {
			for (j = 0; j < dim; j++)
				retvals[j] = 0;
		}
	break;
	case SUM_N:
	case AVG_N:
		//this is multivariate...an the same dimensionality as the var
		/* retval is not valid if any of the inputs is invalid */
		//storeval does not matter

		*retvalid = 1;
		for (j = 0; j < dim; j++)
			retvals[j] = 0;

		for (i = 0; i < nvals; i++){
			if (vals[i].typei == BASE) {
				if (vals[i].metric_type == LDMS_V_U64){
					retvals[0] += ldms_metric_get_u64(set, metric_arry[vals[i].i]);
				} else {
					for (j = 0; j < dim; j++)
						retvals[j] += ldms_metric_array_get_u64(set, metric_arry[vals[i].i], j);
				}
			} else { //it must be an array
				*retvalid *= dp->datavals[vals[i].i].returnvalid;
				if (*retvalid == 0){ //abort
					break;
				} else {
					for (j = 0; j < dim; j++)
						retvals[j] += dp->datavals[vals[i].i].returnvals[j];
				}
			}
		}
		if (*retvalid){
			if (fct == SUM_N) {
				for (j = 0; j < dim; j++)
					retvals[j] *= scale;
			} else {
				for (j = 0; j < dim; j++)
					retvals[j] = (uint64_t)(((double)(retvals[j]) * scale)/(double)nvals);
			}
		} else {
			for (j = 0; j < dim; j++)
				retvals[j] = 0;
		}
	break;
	case SUB_AB:
	case DIV_AB:
	case MUL_AB:
		//this is bivariate and the same dimensionality as the var
		//SUB will flag upon neg. set value to zero.

		*retvalid = 1;

		for (i = 0; i < 2; i++){
			if (vals[i].typei == BASE) {
				if (vals[i].metric_type == LDMS_V_U64){
					uint64_t temp = ldms_metric_get_u64(set, metric_arry[vals[i].i]);
//					msglog(LDMSD_LDEBUG, "getting value %d %llu for var %d\n", i, temp, vals[i].i);
					if (i == 0){
						retvals[0] = temp;
					} else {
						switch (fct){
						case SUB_AB:
							if (temp > retvals[0]){
								*retvalid = 0;
								break;
							} else {
								retvals[0] -= temp;
								retvals[0] *= scale;
							}
							break;
						case MUL_AB:
							retvals[0] *= (temp * scale);
							break;
						case DIV_AB:
							if (temp == 0){
								*retvalid = 0;
								break;
							} else {
								retvals[0] = (uint64_t)(((double)retvals[0]/(double)temp)*scale);
							}
							break;
						default:
							/* NOTREACHED */
							TOKEN_ERR(fct, "SUB_AB,"
								"MUL_AB,DIV_AB");
							break;
						}
					}
				} else { //it must be an array
					for (j = 0; j < dim; j++) {
						uint64_t temp = ldms_metric_array_get_u64(set, metric_arry[vals[i].i], j);
//						msglog(LDMSD_LDEBUG, "getting value %d(%d) %llu for var %d\n", i, j, temp, vals[i].i);
						if (i == 0){
							retvals[j] = temp;
						} else {
							switch (fct){
							case SUB_AB:
								if (temp > retvals[j]){
									*retvalid = 0;
									break;
								} else {
									retvals[j] -= temp;
									retvals[j] *= scale;
								}
								break;
							case MUL_AB:
								retvals[j] *= (temp * scale);
								break;
							case DIV_AB:
								if (temp == 0)
									*retvalid = 0;
								else
									retvals[j] = (uint64_t)(((double)retvals[j]/(double)temp)*scale);
								break;
							default:
								/* NOTREACHED */
								TOKEN_ERR(fct, "SUB_AB,MUL_AB,DIV_AB");
								break;
							}
						}
					}
				}
			} else { //!BASE
				*retvalid *= dp->datavals[vals[i].i].returnvalid;
				if (!retvalid)
					break;
				for (j = 0; j < dim; j++){
					uint64_t temp = dp->datavals[vals[i].i].returnvals[j];
//					msglog(LDMSD_LDEBUG, "getting value %d(%d) %llu for var %d\n", i, j, temp, vals[i].i);
					if (i == 0){
						retvals[j] = temp;
					} else {
						switch (fct){
						case SUB_AB:
							if (temp > retvals[j]){
								*retvalid = 0;
								break;
							} else {
								retvals[j] -= temp;
								retvals[j] *= scale;
							}
							break;
						case MUL_AB:
							retvals[j] *= (temp * scale);
							break;
						case DIV_AB:
							if (temp == 0)
								*retvalid = 0;
							else
								retvals[j] = (uint64_t)(((double)retvals[j]/(double)temp)*scale);
							break;
						default:
							/* NOTREACHED */
							TOKEN_ERR(fct, "SUB_AB,MUL_AB,DIV_AB");
							break;
						}
					}
				}
			} //endif
		} //for i
		if (*retvalid) {
//			msglog(LDMSD_LDEBUG, "ret valid, fct SUB/MUL/DIV dim %d, scale %g\n", dim, scale);
			//already did scale....
		} else {
//			msglog(LDMSD_LDEBUG, "ret not valid. setting all values to zero\n");
			for (j = 0; j < dim; j++)
				retvals[j] = 0;
		}
		break;
	case SUM_VS:
	case SUB_VS:
	case SUB_SV:
	case MUL_VS:
	case DIV_VS:
	case DIV_SV:
	{
		//this is bivariate. One arg is a vector of the same dimensionality as the var.
		//Other arg is a scalar. (could be a vector of dim 1 if base)

		//SUB will flag upon neg. set value to zero.
		*retvalid = 1;
		uint64_t temp_scalar;
		int s_idx, v_idx;

		switch(fct){
		case SUM_VS:
		case SUB_VS:
		case MUL_VS:
		case DIV_VS:
			v_idx = 0;
			s_idx = 1;
			break;
		case SUB_SV:
		case DIV_SV:
			v_idx = 1;
			s_idx = 0;
			break;
		default:
			/* NOTREACHED */
			TOKEN_ERR(fct, "*_VS,*_SV");
			break;
		}

		if (vals[s_idx].typei == BASE){
			if (vals[s_idx].metric_type == LDMS_V_U64)
				temp_scalar = ldms_metric_get_u64(set, metric_arry[vals[s_idx].i]);
			else
				temp_scalar = ldms_metric_array_get_u64(set, metric_arry[vals[s_idx].i], 0); //dim 1
		} else {
			temp_scalar = dp->datavals[vals[s_idx].i].returnvals[0];
			*retvalid = dp->datavals[vals[s_idx].i].returnvalid;
		}
//		msglog(LDMSD_LDEBUG, "getting value %d %llu for var %d\n", s_idx, temp_scalar, vals[s_idx].i);

		if (*retvalid){
			if (vals[v_idx].typei == BASE) {
				if (vals[v_idx].metric_type == LDMS_V_U64) {
					uint64_t temp = ldms_metric_get_u64(set, metric_arry[vals[v_idx].i]);
//					msglog(LDMSD_LDEBUG, "getting value %d %llu for var %d\n", v_idx, temp, vals[v_idx].i);
					//do function for retvals[0] here...
					switch (fct){
					case SUM_VS:
						retvals[0] = (temp + temp_scalar) * scale;
						break;
					case SUB_VS:
						if (temp_scalar > temp)
							*retvalid = 0;
						else
							retvals[0] = (temp - temp_scalar) * scale;
						break;
					case SUB_SV:
						if (temp > temp_scalar)
							*retvalid = 0;
						else
							retvals[0] = (temp_scalar - temp) * scale;
						break;
					case MUL_VS:
						retvals[0] = temp * temp_scalar * scale;
						break;
					case DIV_VS:
						if (temp_scalar == 0)
							*retvalid = 0;
						else
							retvals[0] = (uint64_t)(((double)temp/(double)temp_scalar)*scale);
						break;
					case DIV_SV:
						if (temp == 0)
							*retvalid = 0;
						else
							retvals[0] = (uint64_t)(((double)temp_scalar/(double)temp)*scale);
						break;
					default:
						/* NOTREACHED */
						TOKEN_ERR(fct, "*_VS,*_SV");
						break;
					}
				} else { // it must be an array
					for (j = 0; j < dim; j++) {
						uint64_t temp = ldms_metric_array_get_u64(set, metric_arry[vals[v_idx].i], j);
//						msglog(LDMSD_LDEBUG, "getting value %d(%d) %llu for var %d\n",
//						       v_idx, j, temp, vals[v_idx].i);
						//do function for retvals[j] here...
						switch (fct){
						case SUM_VS:
							retvals[j] = (temp + temp_scalar) * scale;
							break;
						case SUB_VS:
							if (temp_scalar > temp){
								*retvalid = 0;
								break;
							} else {
								retvals[j] = (temp - temp_scalar) * scale;
							}
							break;
						case SUB_SV:
							if (temp > temp_scalar){
								*retvalid = 0;
								break;
							} else {
								retvals[j] = (temp_scalar - temp) * scale;
							}
							break;
						case MUL_VS:
							retvals[j]  = temp * temp_scalar * scale;
							break;
						case DIV_VS:
							if (temp_scalar == 0) {
								*retvalid = 0;
								break;
							} else {
								retvals[j] = (uint64_t)(((double)temp/(double)temp_scalar)*scale);
							}
							break;
						case DIV_SV:
							if (temp == 0) {
								*retvalid = 0;
								break;
							} else {
								retvals[j] = (uint64_t)(((double)temp_scalar/(double)temp)*scale);
							}
							break;
						default:
							/* NOTREACHED */
							TOKEN_ERR(fct, "*_VS,*_SV");
							break;
						}
					}
				}
			} else {
				*retvalid = dp->datavals[vals[v_idx].i].returnvalid;
				if (*retvalid){
					for (j = 0; j < dim; j++){
						uint64_t temp = dp->datavals[vals[v_idx].i].returnvals[j];
//						msglog(LDMSD_LDEBUG, "getting value %d(%d) %llu for var %d\n",
//						       v_idx, j, temp, vals[v_idx].i);
						//do function for retvals[j] here...
						switch (fct){
						case SUM_VS:
							retvals[j] = (temp + temp_scalar) * scale;
							break;
						case SUB_VS:
							if (temp_scalar > temp){
								*retvalid = 0;
								break;
							} else {
								retvals[j] = (temp - temp_scalar) * scale;
							}
							break;
						case SUB_SV:
							if (temp > temp_scalar){
								*retvalid = 0;
								break;
							} else {
								retvals[j] = (temp_scalar-temp) * scale;
							}
							break;
						case MUL_VS:
							retvals[j]  = temp * temp_scalar * scale;
							break;
						case DIV_VS:
							if (temp_scalar == 0) {
								*retvalid = 0;
								break;
							} else {
								retvals[j] = (uint64_t)(((double)temp/(double)temp_scalar)*scale);
							}
							break;
						case DIV_SV:
							if (temp == 0) {
								*retvalid = 0;
								break;
							} else {
								retvals[j] = (uint64_t)(((double)temp_scalar/(double)temp)*scale);
							}
							break;
						default:
							/* NOTREACHED */
							TOKEN_ERR(fct, "*_VS,*_SV");
							break;
						}
					}
				}
			}
		}

		if (!*retvalid){
//			msglog(LDMSD_LDEBUG, "ret not valid. setting all values to zero\n");
			for (j = 0; j < dim; j++){
				retvals[j] = 0;
			}
		} else {
			//already did scale....
//			msglog(LDMSD_LDEBUG, "ret valid, fct SUB/MUL/DIV dim %d, scale %g\n", dim, scale);
		}

	}
		break;
	default:
		//shouldnt happen
		msglog(LDMSD_LERROR, "%s: bad function in calculation <%d>\n",
		       __FILE__, fct);

		*storevalid = 0;
		*retvalid = 0;
	}

//	msglog(LDMSD_LDEBUG, "after computation: %s currtime=@%lu dim %d retvalid=%d storevalid=%d\n\n",
//	       dd->name, curr.tv_sec, dim, *retvalid, *storevalid);
//	if (storevals)
//		for (j = 0; j < dim; j++)
//			msglog(LDMSD_LDEBUG, "%d: ret=%llu store=%llu\n",
//			       j, retvals[j], storevals[j]);
//	else
//		for (j = 0; j < dim; j++)
//			msglog(LDMSD_LDEBUG, "%d: ret=%llu\n",
//			       j, retvals[j]);

//	msglog(LDMSD_LDEBUG,"Returning %d\n", *retvalid);

	return *retvalid;

};

static int get_datapoint(idx_t* sets_idx, const char* instance_name,
			 int numder, struct derived_data** der,
			 int* numsets, struct setdatapoint** rdp, int* firsttime){

	struct setdatapoint* dp = NULL;
	int i, j;

	if (rdp == NULL){
		msglog(LDMSD_LERROR, "%s: arg to getDatapoint is NULL!\n",
		       __FILE__);
		return EINVAL;
	}

	*rdp = NULL;
	*firsttime = 0;

	dp = idx_find(*sets_idx, (void*) instance_name,
		      strlen(instance_name));

	if (dp == NULL){
		//create a container to hold it
		dp = (struct setdatapoint*)malloc(sizeof(struct setdatapoint));
		if (!dp) {
			msglog(LDMSD_LCRITICAL, "%s: ENOMEM\n", __FILE__);
			return ENOMEM;
		}
		dp->ts = NULL;
		dp->datavals = NULL;
		(*numsets)++;

		idx_add(*sets_idx, (void*)instance_name,
			strlen(instance_name), dp);
	}

	if (dp->ts == NULL){ //first time....
		dp->ts = (struct ldms_timestamp*)malloc(sizeof (struct ldms_timestamp));
		if (dp->ts == NULL) {
			msglog(LDMSD_LCRITICAL, "%s: ENOMEM\n", __FILE__);
			free(dp);
			dp = NULL;
			return ENOMEM;
		}
		dp->ts->sec = 0;
		dp->ts->usec = 0;

		dp->datavals = calloc(numder, sizeof(struct dinfo));
		if (dp->datavals == NULL) {
			msglog(LDMSD_LCRITICAL, "%s: ENOMEM\n", __FILE__);
			free(dp->ts);
			free(dp);
			dp = NULL;
			return ENOMEM;
		}

		//create the space for the return vals and store vals if needed
		for (i = 0; i < numder; i++){
			dp->datavals[i].dim = der[i]->dim;

			if (func_def[der[i]->fct].createreturn){
				dp->datavals[i].returnvals = calloc(dp->datavals[i].dim, sizeof(uint64_t));
				if (dp->datavals[i].returnvals == NULL)
					goto err;
				dp->datavals[i].returnvalid = 0;
			} else {
				dp->datavals[i].returnvals = NULL;
			}

			if (func_def[der[i]->fct].createstore){
				dp->datavals[i].storevals = calloc(dp->datavals[i].dim, sizeof(uint64_t));
				if (dp->datavals[i].storevals == NULL)
					goto err;
				dp->datavals[i].storevalid = 0;
			} else {
				dp->datavals[i].storevals = NULL;
			}
		}

		*firsttime = 1;
	}

	*rdp = dp;
	return 0;


err:
	msglog(LDMSD_LCRITICAL, "%s: ENOMEM\n", __FILE__);
	for (j = 0; j <= i; j++){
		if (dp->datavals[j].storevals)
			free(dp->datavals[j].storevals);
		if (dp->datavals[j].returnvals)
			free(dp->datavals[j].returnvals);
	}
	free(dp->datavals);
	free(dp->ts);
	free(dp);
	dp = NULL;

	return ENOMEM;

};


static int
store(ldmsd_store_handle_t _s_handle, ldms_set_t set, int *metric_arry, size_t metric_count)
{

	const struct ldms_timestamp _ts = ldms_transaction_timestamp_get(set);
	const struct ldms_timestamp *ts = &_ts;
	struct setdatapoint* dp = NULL;
	const char* pname;
	uint64_t compid;
	uint64_t jobid;
	struct function_store_handle *s_handle;
	struct timeval prev, curr, diff;
	int skip = 0;
	int setflagtime = 0;
	int tempidx;
	int doflush = 0;
	int rc;
	int i, j;

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
		pthread_mutex_unlock(&s_handle->lock);
		return EINVAL;
		break;
	default:
		//ok to continue
		break;
	}

	rc = get_datapoint(&(s_handle->sets_idx), ldms_set_instance_name_get(set),
			   s_handle->numder, s_handle->der,
			   &(s_handle->numsets), &dp, &skip);
	if (rc != 0){
		pthread_mutex_unlock(&s_handle->lock);
		return rc;
	}

	/*
	 * New in v3: if time diff is not positive, always write out something and flag.
	 * if its RAW data, write the val. if its RATE data, write zero
	 */

	setflagtime = 0;
	prev.tv_sec = dp->ts->sec;
	prev.tv_usec = dp->ts->usec;
	curr.tv_sec = ts->sec;
	curr.tv_usec = ts->usec;

//	if (skip){
//		//even if skip, still need the vals to do the raw/rate calcs.
//		msglog(LDMSD_LDEBUG, "Note: firsttime (%lu) -- should be skipping writeout for set <%s>\n",
//		       curr.tv_sec, ldms_set_instance_name_get(set));
//	} else {
//		msglog(LDMSD_LDEBUG, "Note: After firsttime -- not skipping writeout for set <%s>\n",
//		       ldms_set_instance_name_get(set));
//	}

	if ((double)prev.tv_sec*1000000+prev.tv_usec >=
	    (double)curr.tv_sec*1000000+curr.tv_usec){
		msglog(LDMSD_LDEBUG," %s: Time diff is <= 0 for set %s. Flagging\n",
		       __FILE__, ldms_set_instance_name_get(set));
		setflagtime = 1;
	}
	//always do this and write it out
	timersub(&curr, &prev, &diff);

	pname = ldms_set_producer_name_get(set);

	tempidx = ldms_metric_by_name(set, "component_id");
	if (tempidx != -1)
		compid = ldms_metric_get_u64(set, metric_arry[tempidx]);
	else
		compid = 0;

	tempidx = ldms_metric_by_name(set, "job_id");
	if (tempidx != -1)
		jobid = ldms_metric_get_u64(set, metric_arry[tempidx]);
	else
		jobid = 0;

	if (!skip){
		/* format: #Time, Time_usec, DT, DT_usec */
		fprintf(s_handle->file, "%"PRIu32".%06"PRIu32 ",%"PRIu32,
			ts->sec, ts->usec, ts->usec);
		fprintf(s_handle->file, ",%lu.%06lu,%lu",
			diff.tv_sec, diff.tv_usec, diff.tv_usec);

		if (pname != NULL){
			fprintf(s_handle->file, ",%s", pname);
			s_handle->byte_count += strlen(pname);
		} else {
			fprintf(s_handle->file, ",");
		}

		fprintf(s_handle->file, ",%"PRIu64",%"PRIu64,
			compid, jobid);
	}
	//always get the vals because may need the stored value, even if skip this time

	for (i = 0; i < s_handle->numder; i++){ //go thru all the vals....only write the writeout vals

//		msglog(LDMSD_LDEBUG, "%s: Schema %s Updating variable %d of %d: %s\n",
//		       pname, s_handle->schema, i, s_handle->numder, s_handle->der[i]->name);

		if (s_handle->der[i]->fct == RAWTERM) {
			//this will also do its writeout
			if (!skip)
				(void)doRAWTERMFunc(set, s_handle, metric_arry, s_handle->der[i]);
		} else {
			(void)doFunc(set, metric_arry,
				     dp, s_handle->der[i],
				     diff, setflagtime);
			//write it out, if its writeout and not skip
			//FIXME: Should the writeout be moved in so its like doRAWTERMFunc ?
			if (!skip && s_handle->der[i]->writeout){
				struct dinfo* di = &(dp->datavals[i]);
				for (j = 0; j < di->dim; j++) {
					rc = fprintf(s_handle->file, ",%" PRIu64, di->returnvals[j]);
					if (rc < 0) {
						msglog(LDMSD_LERROR,"%s: Error %d writing to '%s'\n",
						       __FILE__, rc, s_handle->path);
						//FIXME: should this exit entirely from this store?
						break;
					} else {
						s_handle->byte_count += rc;
					}
				}
				rc = fprintf(s_handle->file, ",%d", (!di->returnvalid));
				if (rc < 0)
					msglog(LDMSD_LERROR,"%s: Error %d writing to '%s'\n",
					       __FILE__, rc, s_handle->path);
				else
					s_handle->byte_count += rc;
			}
		}
	}

	//finally update the time for this whole set.
	dp->ts->sec = curr.tv_sec;
	dp->ts->usec = curr.tv_usec;

	if (!setflagtime)
		if ((ageusec > 0) && ((diff.tv_sec*1000000+diff.tv_usec) > ageusec))
			setflagtime = 1;

	if (!skip){
		fprintf(s_handle->file, ",%d\n", setflagtime); //NOTE: currently only setting flag based on time
		s_handle->byte_count += 1;
		s_handle->store_count++;

		if ((s_handle->buffer_type == 3) &&
		    ((s_handle->store_count - s_handle->lastflush) >=
		     s_handle->buffer_sz)){
			s_handle->lastflush = s_handle->store_count;
			doflush = 1;
		} else if ((s_handle->buffer_type == 4) &&
			 ((s_handle->byte_count - s_handle->lastflush) >=
			  s_handle->buffer_sz)){
			s_handle->lastflush = s_handle->byte_count;
			doflush = 1;
		}
		if ((s_handle->buffer_sz == 0) || doflush){
			fflush(s_handle->file);
			fsync(fileno(s_handle->file));
		}
	}

	pthread_mutex_unlock(&s_handle->lock);

	return 0;
}

static int flush_store(ldmsd_store_handle_t _s_handle)
{
	struct function_store_handle *s_handle = _s_handle;
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
	struct function_store_handle *s_handle = _s_handle;
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
	s_handle->ucontext = NULL;
	if (s_handle->file)
		fclose(s_handle->file);
	s_handle->file = NULL;
	if (s_handle->headerfile)
		fclose(s_handle->headerfile);
	s_handle->headerfile = NULL;

	for (i = 0; i < s_handle->numder; i++){
		free(s_handle->der[i]->name);
		s_handle->der[i]->name = NULL;
		free(s_handle->der[i]->varidx);
		s_handle->der[i]->varidx = NULL;
		free(s_handle->der[i]);
		s_handle->der[i] = NULL;
	}

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

static struct ldmsd_store store_function_csv = {
	.base = {
			.name = "function_csv",
			.type = LDMSD_PLUGIN_STORE,
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
	return &store_function_csv.base;
}

static void __attribute__ ((constructor)) store_function_csv_init();
static void store_function_csv_init()
{
	store_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_function_csv_fini(void);
static void store_function_csv_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(store_idx);
}
