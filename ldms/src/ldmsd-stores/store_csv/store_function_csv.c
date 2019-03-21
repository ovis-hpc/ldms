/**
 * Copyright (c) 2014-2017,2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2014-2017,2019 Open Grid Computing, Inc. All rights reserved.
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


/**
 * \file store_function_csv.c
 * store_function_csv - specify some metrics to be stored can be RAW or combo of
 * other functions.
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
 *   d) NOTE: the data types supported and the data types in the calculation is
 *      limited and subject to change.
 * - Merics now matched on both name and schema.
 *   a) In case of a name collision, the name matching the base metric is
 *      preferentially chosen to determine the metric the calculation is based
 *      upon
 * - Validity. Any invalid value results in a zero writeout. For every variable,
 *   there is a valid indicator in the output.
 *   a) Rollover (determined by a neg value) and neg values - both return 0
 *   b) neg quantity in a calculation
 * - New in v3: if time diff is not positive, always write out something and
 *   flag. Flag is only for time.  This is the same flag for time > ageusec. if
 *   its RAW data, write the val. if its RATE data, write zero and flag. (in v2
 *   was: two timestamps for the same component with dt = 0 wont writeout.
 *   Presumably this shouldnt happen.)
 * - New in v3: redo order of RATE calculation to keep precision.
 *
 *   FIXME: Review the following:
 * - STORE_DERIVED_METRIC_MAX - is fixed value.
 * - there is no function to iterate thru an idx. Right now, that memory is
 *   lost in destroy_store.
 * - if a host goes down and comes back up, then may have a long time range
 *   between points. currently, this is still calculated, but can be flagged
 *   with ageout. Currently this is global, not per collector type
 * - Scale casting is still in development. Overflow is not checked for. This
 *   was inconsistent prior to mid Nov 2016.  In Nov 2016, this has been made
 *   consistent and chosen to enable fractional and less than 1 values for the
 *   scale, so rely on the uint64_t being cast to double as part of the
 *   multiplication with the double scale, and as a result, there may be
 *   overflow. Writeout is still uint64_t.
 *
 * TODO:
 * - For the implict cast for scale operations, should this be bypassed for
 *   scale == 1?
 * - Currently only handles uint64_t scalar and vector types
 * - Currently only printsout uint64_t values. (cast)
 * - Fix the frees
 * - not keeping or writing the user data
 * - decide if should have option to write these out to multiple files for the
 *   same schema.
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
#include "ldmsd_store.h"

#include "store_common.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

#define TV_SEC_COL    0
#define TV_USEC_COL    1
#define GROUP_COL    2
#define VALUE_COL    3

#define MAX_ROLLOVER_STORE_KEYS 20
#define STORE_DERIVED_NAME_MAX 256
#define STORE_DERIVED_LINE_MAX 4096
#define STORE_DERIVED_METRIC_MAX 500

/** ROLLTYPES documents rolltype and is used in help output. Also used for buffering. */
#define ROLLTYPES \
"                     1: wake approximately every rollover seconds and roll.\n" \
"                     2: wake daily at rollover seconds after midnight (>=0) and roll.\n" \
"                     3: roll after approximately rollover records are written.\n" \
"                     4: roll after approximately rollover bytes are written.\n"
#define MAXROLLTYPE 4
#define MINROLLTYPE 1
#define DEFAULT_ROLLTYPE -1
/* minimum rollover for type 1;
 * rolltype==1 and rollover < MIN_ROLL_1 -> rollover = MIN_ROLL_1
 *             also used for minimum sleep time for type 2;
 * rolltype==2 and rollover results in sleep < MIN_ROLL_SLEEPTIME -> skip this
 *             roll and do it the next day */
#define MIN_ROLL_1 10
/* minimum rollover for type 3;
 * rolltype==3 and rollover < MIN_ROLL_RECORDS -> rollover = MIN_ROLL_RECORDS */
#define MIN_ROLL_RECORDS 3
/* minimum rollover for type 4;
 * rolltype==4 and rollover < MIN_ROLL_BYTES -> rollover = MIN_ROLL_BYTES */
#define MIN_ROLL_BYTES 1024
/* Interval to check for passing the record or byte count limits */
#define ROLL_LIMIT_INTERVAL 60

static char* var_sep = ",";
static char type_sep = ':';

#define BYMSRNAME "BYMSRNAME"
#define MSR_MAXLEN 20LL

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
	THRESH_GE, /* scale is thresh */
	THRESH_LT, /*scale is thresh */
	MAX,
	MIN,
	SUM, /* sum across the vectors' dimension */
	AVG,
	SUM_VS,
	SUB_VS,
	SUB_SV,
	MUL_VS,
	DIV_VS,
	DIV_SV,
	FCT_END //invalid
} func_t;

struct func_info {
	char* name; /* name of the fct */
	variate_t variatetype;
	int createreturn; /* create space for the return vals
			   * (should usually be the case) */
	int createstore; /* create space to store data in addtion to the return
			  * vals (to be used in the calculation) */
};

/* ordered by enum func_t */
struct func_info func_def[(FCT_END+1)] = {
	 { "RATE"     , UNIVARIATE  , 1, 1 },
	 { "DELTA"    , UNIVARIATE  , 1, 1 },
	 { "RAW"      , UNIVARIATE  , 1, 0 },
	 { "RAWTERM"  , UNIVARIATE  , 0, 0 },
	 { "MAX_N"    , MULTIVARIATE, 1, 0 },
	 { "MIN_N"    , MULTIVARIATE, 1, 0 },
	 { "SUM_N"    , MULTIVARIATE, 1, 0 },
	 { "AVG_N"    , MULTIVARIATE, 1, 0 },
	 { "SUB_AB"   , BIVARIATE   , 1, 0 },
	 { "MUL_AB"   , BIVARIATE   , 1, 0 },
	 { "DIV_AB"   , BIVARIATE   , 1, 0 },
	 { "THRESH_GE", UNIVARIATE  , 1, 0 },
	 { "THRESH_LT", UNIVARIATE  , 1, 0 },
	 { "MAX"      , UNIVARIATE  , 1, 0 },
	 { "MIN"      , UNIVARIATE  , 1, 0 },
	 { "SUM"      , UNIVARIATE  , 1, 0 },
	 { "AVG"      , UNIVARIATE  , 1, 0 },
	 { "SUM_VS"   , BIVARIATE   , 1, 0 },
	 { "SUB_VS"   , BIVARIATE   , 1, 0 },
	 { "SUB_SV"   , BIVARIATE   , 1, 0 },
	 { "MUL_VS"   , BIVARIATE   , 1, 0 },
	 { "DIV_VS"   , BIVARIATE   , 1, 0 },
	 { "DIV_SV"   , BIVARIATE   , 1, 0 },
	 { "FCT_END"  , VARIATE_END , 0, 0 },
};

typedef struct func_name_map_s {
	const char *name;
	func_t f;
} *func_name_map_t;

/* ordered by name */
struct func_name_map_s func_name_map[] = {
	 { "AVG"       , AVG       },
	 { "AVG_N"     , AVG_N     },
	 { "DELTA"     , DELTA     },
	 { "DIV_AB"    , DIV_AB    },
	 { "DIV_SV"    , DIV_SV    },
	 { "DIV_VS"    , DIV_VS    },
	 { "FCT_END"   , FCT_END   },
	 { "MAX"       , MAX       },
	 { "MAX_N"     , MAX_N     },
	 { "MIN"       , MIN       },
	 { "MIN_N"     , MIN_N     },
	 { "MUL_AB"    , MUL_AB    },
	 { "MUL_VS"    , MUL_VS    },
	 { "RATE"      , RATE      },
	 { "RAW"       , RAW       },
	 { "RAWTERM"   , RAWTERM   },
	 { "SUB_AB"    , SUB_AB    },
	 { "SUB_SV"    , SUB_SV    },
	 { "SUB_VS"    , SUB_VS    },
	 { "SUM"       , SUM       },
	 { "SUM_N"     , SUM_N     },
	 { "SUM_VS"    , SUM_VS    },
	 { "THRESH_GE" , THRESH_GE },
	 { "THRESH_LT" , THRESH_LT },
};

/******/

/****** per schema per instance data stores (stored in sets_idx) ******/
struct dinfo { //values of the derived metrics, updated as the calculations go along.
	int dim;
	uint64_t* storevals;
	uint64_t* returnvals;
	int storevalid; // flag for if this is valid for dependent calculations.
	int returnvalid; //flag for if this is valid for dependent calculations.
};

struct setdatapoint { // one of these for each instance for each schema
	struct ldms_timestamp ts;
	struct dinfo datavals[]; /* derived vals from the last timestep (one for
				   each derived metric) indicies are those of
				   the derived metrics (not the sources).  The
				   der and datavals are in the same order. */
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

typedef struct store_function_csv_inst_s *store_function_csv_inst_t;
struct store_function_csv_inst_s {
	struct ldmsd_plugin_inst_s base;

	ldmsd_strgp_t strgp; /* associated strgp  */

	idx_t store_idx;
	char* storekeys[MAX_ROLLOVER_STORE_KEYS]; //TODO: make this variable size
	int nstorekeys;
	int altheader;
	char* derivedconf;  //Mutliple derived files
	int ageusec;

	/* rollover */
	int rollover;
	int rolltype;
	struct ovis_event_s roll_ev; /* rollover event */

	/* from function_store_handle */
	char *path;
	FILE *file;

	/* these are about the derived metrics (independent of instance)
	 * TODO: dynamic. */
	struct derived_data* der[STORE_DERIVED_METRIC_MAX];
	int numder; /* there are numder actual items in the der array */
	idx_t sets_idx; /* to keep track of sets/data involved to do the diff
			   (contains setdatapoint) key is the instance name of
			   the set. There will be N entries in this index, where
			   N = number of instances matching this schema
			   (typically 1 per host aggregating from). Each
			   setdatapoint will have X datavals where X = number of
			   derived metrics (numder).  The der and the datavals
			   are in the same order */
	int numsets;
	printheader_t printheader;
	int parseconfig;
	char *store_key; /* this is the container+schema */
	pthread_mutex_t lock;
	void *ucontext;
	int buffer_type;
	int buffer_sz;
	int64_t lastflush;
	int64_t store_count;
	int64_t byte_count;
};



static
int __try_print_header(store_function_csv_inst_t inst, ldmsd_strgp_t strgp);


__attribute__((format(printf, 2, 3)))
int iprintf(store_function_csv_inst_t inst, const char *fmt, ...)
{
	int sz;
	va_list ap;
	va_start(ap, fmt);
	sz = vfprintf(inst->file, fmt, ap);
	va_end(ap);
	if (sz < 0) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Error %d writing to '%s'\n", sz, inst->path);
	} else {
		inst->byte_count += sz;
	}
	return sz;
}

/* ============== rollover routines ================= */
/* NOTE all instances share the same rollover scheduler */
ovis_scheduler_t roll_sched; /* roll-over scheduler */
pthread_t roll_thread; /* rollover thread */

/* rollover thread procedure */
static void* rolloverThreadInit(void* m)
{
	int rc;
	rc = ovis_scheduler_loop(roll_sched, 0);
	if (rc) {
		ldmsd_log(LDMSD_LERROR,
			  "store_csv: rollover scheduler exited, rc: %d\n", rc);
	}
	return NULL;
}

static void handleRollover(store_function_csv_inst_t inst)
{
	FILE* nfp = NULL;
	char tmp_path[PATH_MAX];
	time_t appx = time(NULL);

	pthread_mutex_lock(&inst->lock);

	switch (inst->rolltype) {
	case 1:
		break;
	case 2:
		break;
	case 3:
		if (inst->store_count < inst->rollover)  {
			/* do not roll yet */
			goto out;
		} else {
			/* reset counters, will flush + roll below */
			inst->store_count = 0;
			inst->lastflush = 0;
		}
		break;
	case 4:
		if (inst->byte_count < inst->rollover) {
			/* do not roll yet */
			goto out;
		} else {
			/* reset counters, will flush + roll below */
			inst->byte_count = 0;
			inst->lastflush = 0;
		}
		break;
	default:
		INST_LOG(inst, LDMSD_LDEBUG,
			 "%s: Error: unexpected rolltype in store(%d)\n",
			 __FILE__, inst->rolltype);
		break;
	}

	if (inst->file)
		fflush(inst->file);

	//re name: if got here then rollover requested
	snprintf(tmp_path, PATH_MAX, "%s.%d", inst->path, (int) appx);
	nfp = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!nfp) {
		//we cant open the new file, skip
		INST_LOG(inst, LDMSD_LERROR,
			 "%s: Error: cannot open file <%s>\n",
			 __FILE__, tmp_path);
		goto out;
	}

	//close and swap
	if (inst->file)
		fclose(inst->file);
	inst->file = nfp;

	inst->printheader = DO_PRINT_HEADER;

out:
	pthread_mutex_unlock(&inst->lock);
}

static int scheduleRollover(store_function_csv_inst_t inst);

static void rolloverTask(ovis_event_t ev)
{
	//if got here, then rollover requested
	store_function_csv_inst_t inst = ev->param.ctxt;
	ovis_scheduler_event_del(roll_sched, ev);
	handleRollover(inst);
	scheduleRollover(inst);
}

static int scheduleRollover(store_function_csv_inst_t inst)
{
	int tsleep;
	time_t rawtime;
	struct tm *info;
	int secSinceMidnight;
	int rc;

	switch (inst->rolltype) {
	case 1:
		tsleep = (inst->rollover < MIN_ROLL_1) ?
			 MIN_ROLL_1 : inst->rollover;
		break;
	case 2:
		time( &rawtime );
		info = localtime( &rawtime );
		secSinceMidnight = info->tm_hour*3600 +
				   info->tm_min*60 + info->tm_sec;
		tsleep = 86400 - secSinceMidnight + inst->rollover;
		if (tsleep < MIN_ROLL_1) {
			/* if we just did a roll then skip this one */
			tsleep +=86400;
		}
		break;
	case 3:
		if (inst->rollover < MIN_ROLL_RECORDS)
			inst->rollover = MIN_ROLL_RECORDS;
		tsleep = ROLL_LIMIT_INTERVAL;
		break;
	case 4:
		if (inst->rollover < MIN_ROLL_BYTES)
			inst->rollover = MIN_ROLL_BYTES;
		tsleep = ROLL_LIMIT_INTERVAL;
		break;
	default:
		tsleep = 60;
		break;
	}

	inst->roll_ev.param.cb_fn = rolloverTask;
	inst->roll_ev.param.type = OVIS_EVENT_TIMEOUT;
	inst->roll_ev.param.ctxt = inst;
	inst->roll_ev.param.timeout.tv_sec = tsleep;
	rc = ovis_scheduler_event_add(roll_sched, &inst->roll_ev);
	return rc;
}

/* ============== END - rollover routines ================= */


static void printStructs(store_function_csv_inst_t inst)
{
	int i, j;

	INST_LOG(inst, LDMSD_LDEBUG,
		 "=========================================\n");
	for (i = 0; i < inst->numder; i++){
		INST_LOG(inst, LDMSD_LDEBUG,
			 "Schema <%s> New metric <%s> idx %d writeout %d\n",
			 (inst->strgp)?(inst->strgp->schema):(NULL),
			 inst->der[i]->name,
			 inst->der[i]->idx,
			 inst->der[i]->writeout);
		INST_LOG(inst, LDMSD_LDEBUG, "\tfun: %s dim %d scale %g\n",
			 func_def[inst->der[i]->fct].name,
			 inst->der[i]->dim,
			 inst->der[i]->scale);
		INST_LOG(inst, LDMSD_LDEBUG, "\tDepends on vars (type:idx)\n");
		for (j = 0; j < inst->der[i]->nvars; j++){
			INST_LOG(inst, LDMSD_LDEBUG,
				 "\t\t%d:%d\n", inst->der[i]->varidx[j].typei,
				 inst->der[i]->varidx[j].i);
		}
	}
	INST_LOG(inst, LDMSD_LDEBUG,
		 "=========================================\n");
}

#define FUNC_INFO(x) ((struct func_info *)(x))
#define FUNC_NAME_MAP(x) ((func_name_map_t)(x))

static int func_cmp(const void *a, const void *b)
{
	return strcmp(FUNC_NAME_MAP(a)->name, FUNC_NAME_MAP(b)->name);
}

static func_t enumFct(const char* fct)
{
	struct func_name_map_s *ent, key = {.name = (void*)fct};

	ent = bsearch(&key, func_name_map, sizeof(func_name_map)/sizeof(key),
		      sizeof(key), func_cmp);
	if (ent)
		return ent->f;

	return FCT_END;
}

static int __checkValidLine(store_function_csv_inst_t inst,
			    const char* lbuf, const char* schema_name,
			    const char* metric_name, const char* function_name,
			    int nmet, char* metric_csv, double scale,
			    int output, int iter, int rcl)
{

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
		INST_LOG(inst, LDMSD_LWARNING,
			 "%s: (%d) Bad format in fct config file <%s> rc=%d. "
			 "Skipping\n", __FILE__, iter, lbuf, rcl);
		return -1;
	}

	if ((strlen(metric_name) == 0) || (strlen(function_name) == 0) ||
				(strlen(metric_csv) == 0)) {
		INST_LOG(inst, LDMSD_LWARNING,
			 "%s: (%d) Bad vals in fct config file <%s>. "
			 "Skipping\n", __FILE__, iter, lbuf);
		return -1;
	}

	func_t tf = enumFct(function_name);
	if (tf == FCT_END) {
		INST_LOG(inst, LDMSD_LWARNING,
			 "%s: (%d) Bad func in fct config file <%s> <%s>. "
			 "Skipping\n", __FILE__, iter, lbuf, function_name);
		return -1;
	}

	//now check the validity of the number of dependent vars for this func
	variate_t vart = func_def[tf].variatetype;
	int badvart = 0;
	switch(vart) {
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
	if (badvart) {
		INST_LOG(inst, LDMSD_LWARNING,
			 "%s: (%d) Wrong number of dependent metrics (%d) for "
			 "func <%s> in config file <%s>. Skipping\n",
			 __FILE__, iter, nmet, function_name, lbuf);
		return -1;
	}

	return 0;
};

static int __getCompcase(char* pch, char** temp_val)
{
	int compcase = 0; //default is by name
	char* temp_ii = strdup(pch);
	const char* ptr = strchr(temp_ii, type_sep);
	if (ptr) {
		int index = ptr - temp_ii;
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
		       int* metric_arry, size_t metric_count)
{
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
			enum ldms_value_type metric_type =
				ldms_metric_type_get(set, metric_arry[i]);
			if (metric_type != LDMS_V_CHAR_ARRAY)
				continue;
			const char* strval = ldms_metric_array_get_str(set,
								metric_arry[i]);
			//if this name matches the str we are looking for
			//get the ctrnum from the well known variable name CtrN_name
			int ctrnum = -1;
			if (strcmp(strval, strmatch) != 0)
				continue;
			rc = sscanf(name, "Ctr%d_name", &ctrnum);
			if (rc != 1) //should only have this value for this name
				return -1;

			//well known metric names are CtrN_c and CtrN_n
			//should be able to assume order, but not doing that....
			char corename[MSR_MAXLEN];
			char numaname[MSR_MAXLEN];
			snprintf(corename, MSR_MAXLEN-1, "Ctr%d_c", ctrnum);
			snprintf(numaname, MSR_MAXLEN-1, "Ctr%d_n", ctrnum);
			for (j = 0; j < metric_count; j++) {
				const char* strval2 = ldms_metric_name_get(set,
								metric_arry[j]);
				if ((strcmp(strval2, corename) == 0) ||
				    (strcmp(strval2, numaname) == 0)){
					return j;
				}
			}
		}
	}

	return -1;
}

static int calcDimValidate(store_function_csv_inst_t inst,
			   struct derived_data* dd)
{
	/* - We can generally calculate the dimensionality of a derived metric from
	   the known dimensionalities of its dependent metrics.
	   - Also check for correct dimensionalities wrt to the function and the
	   other dependent metrics
	   - Note that we have already checked that there are the right number of dependent vars
	*/

	func_t fct = dd->fct;
	int nvals = dd->nvars; //dependent vars
	struct idx_type* vals = dd->varidx; //dependent vars indicies

	switch (fct) {
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
		INST_LOG(inst, LDMSD_LERROR,
			 "%s:%d Error - No code to validate function %d\n",
			 __FILE__, __LINE__, fct);
		return EINVAL;
		break;
	}

	return EINVAL;
};


static struct derived_data*
createDerivedData(store_function_csv_inst_t inst, const char* metric_name,
		  func_t tf, int nmet, const char* metric_csv, double scale,
		  int output, ldms_set_t set, int* metric_arry,
		  size_t metric_count, int numder,
		  struct derived_data** existder)
{

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
	while (pch != NULL) {
		if (count == tmpder->nvars){
			INST_LOG(inst, LDMSD_LERROR,
				 "%s: Too many input vars for input %s. "
				 "expected %d on var %d\n",
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
					ldms_metric_type_get(set,
							metric_arry[matchidx]);
			switch (metric_type) {
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
			if (tmpder->fct != RAWTERM) {
				if ((metric_type != LDMS_V_U64) &&
				    (metric_type != LDMS_V_U64_ARRAY)) {
					const char* name = ldms_metric_name_get(set, metric_arry[matchidx]);
					INST_LOG(inst, LDMSD_LERROR,
						 "%s: unsupported type %d for base metric %s\n",
						 __FILE__, (int)metric_type, name);
					goto err;
				}
			}
		}

		//if not, does it depend on a derived metric?
		if (tmpder->varidx[count].i == -1) {
			//check through all the derived metrics we already have
			for (j = 0; j < numder; j++) {
				if (strcmp(pch, existder[j]->name) != 0)
					continue;
				tmpder->varidx[count].i = j;
				tmpder->varidx[count].typei = DER;
				tmpder->varidx[count].dim = existder[j]->dim;
				tmpder->varidx[count].metric_type = LDMS_V_NONE;
				break;
			}
		}

		if (tmpder->varidx[count].i == -1) {
			INST_LOG(inst, LDMSD_LERROR,
				 "%s: Cannot find matching index for <%s>\n",
				 __FILE__, pch);
			goto err;
		}
		count++;
		pch = strtok_r(NULL, var_sep, &saveptr_i);
	}

	free(x_i);
	x_i = NULL;
	if (count != tmpder->nvars) {
		INST_LOG(inst, LDMSD_LERROR,
			 "%s: inconsistent specification for nvars for "
			 "metric %s. expecting %d got %d\n",
			 __FILE__, metric_name, tmpder->nvars, count);
		goto err;
	}

	//get the dimensionality of this metric. also check for valid dimensionality of its dependent metrics
	if (calcDimValidate(inst, tmpder) != 0) {
		INST_LOG(inst, LDMSD_LERROR,
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
static int derivedConfig(store_function_csv_inst_t inst, char* fname_s,
			 ldms_set_t set, int* metric_arry, size_t metric_count)
{
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

	//TODO: for now will read this in for every option (e.g., different base
	//set for store)
	//Dont yet have a way to determine which of the handles a certain metric
	//will be associated with

	INST_LOG(inst, LDMSD_LDEBUG, "%s: Function config file(s) is: <%s>\n",
		 __FILE__, fname_s);

	char* saveptr_o = NULL;
	char* temp_o = strdup(fname_s);
	char* x_o = temp_o;
	char* fname = strtok_r(temp_o, ",", &saveptr_o);

	inst->parseconfig = 0;
	inst->numder = 0;

	while (fname != NULL) {
		INST_LOG(inst, LDMSD_LDEBUG,
			 "%s: Parsing Function config file: <%s>\n",
			 __FILE__, fname);

		fp = fopen(fname, "r");
		if (!fp) {
			INST_LOG(inst, LDMSD_LERROR,
				 "%s: Cannot open config file <%s>\n",
				 __FILE__, fname);
			return EINVAL;
		}

		rc = 0;
		rcl = 0;
		iter = 0;
		do {
			//TODO: TOO many metrics. dynamically alloc
			if (inst->numder == STORE_DERIVED_METRIC_MAX) {
				INST_LOG(inst, LDMSD_LERROR,
					 "%s: Too many metrics <%s>\n",
					 __FILE__, fname);
				rc = EINVAL;
				break;
			}

			lbuf[0] = '\0';
			s = fgets(lbuf, sizeof(lbuf), fp);
			if (!s)
				break;
			rcl = sscanf(lbuf, "%s %s %s %d %s %lf %d",
				     schema_name, metric_name, function_name,
				     &nmet, metric_csv, &scale, &output);
			iter++;
			if (__checkValidLine(inst, lbuf, schema_name,
					     metric_name, function_name, nmet,
					     metric_csv, scale, output, iter,
					     rcl) != 0 ) {
				continue;
			}

			//only keep this item if the schema matches
			if (strcmp(inst->strgp->schema, schema_name) != 0) {
//				msglog(LDMSD_LDEBUG, "%s: (%d) <%s> rejecting schema <%s>\n",
//				       __FILE__, iter, s_handle->store_key, schema_name);
				continue;
			}

			func_t tf = enumFct(function_name);
			struct derived_data* tmpder =
				createDerivedData(inst, metric_name, tf, nmet,
						  metric_csv, scale, output,
						  set, metric_arry,
						  metric_count, inst->numder,
						  inst->der);
			if (tmpder != NULL) {
//				msglog(LDMSD_LDEBUG, "store fct <%s> accepting metric <%s> schema <%s> (%d)\n",
//				       s_handle->store_key, metric_name, schema_name, iter);
				tmpder->idx = inst->numder;
				inst->der[inst->numder] = tmpder;
				inst->numder++;
			} else {
				INST_LOG(inst, LDMSD_LDEBUG,
					 "store fct <%s> invalid spec for "
					 "metric <%s> schema <%s> (%d): "
					 "rejecting \n",
					 inst->store_key, metric_name,
					 schema_name, iter);
			}
		} while (s);

		if (fp)
			fclose(fp);
		fp = NULL;

		fname = strtok_r(NULL, ",", &saveptr_o);
	}
	free(x_o);
	x_o = NULL;

	printStructs(inst);

	return rc;
}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(store_function_csv_inst_t inst,
			struct attr_value_list *kwl,
			struct attr_value_list *avl,
			char *ebuf, int ebufsz)
{
	char *value;
	int i;

	char* deprecated[] = {"comp_type", "id_pos", "idpos"};
	int numdep = sizeof(deprecated)/sizeof(deprecated[0]);


	for (i = 0; i < numdep; i++) {
		value = av_value(avl, deprecated[i]);
		if (value){
			snprintf(ebuf, ebufsz, "config argument %s has been "
				 "deprecated.\n", deprecated[i]);
			return EINVAL;
		}
	}

	value = av_value(avl, "agesec");
	if (value) {
		snprintf(ebuf, ebufsz, "config argument agesec has been "
			 "deprecated in favor of ageusec\n");
		return EINVAL;
	}

	return 0;
}

/**
 * configuration check for the buffer args
 */
static int config_buffer(store_function_csv_inst_t inst,
			 char *bs, char *bt, int *rbs, int *rbt)
{
	int tempbs;
	int tempbt;


	if (!bs && !bt){
		*rbs = 1;
		*rbt = 0;
		return 0;
	}

	if (!bs && bt){
		INST_LOG(inst, LDMSD_LERROR,
			 "%s: Cannot have buffer type without buffer\n",
			 __FILE__);
		return EINVAL;
	}

	tempbs = atoi(bs);
	if (tempbs < 0) {
		INST_LOG(inst, LDMSD_LERROR,
			 "%s: Bad val for buffer %d\n", __FILE__, tempbs);
		return EINVAL;
	}
	if ((tempbs == 0) || (tempbs == 1)) {
		if (bt){
			INST_LOG(inst, LDMSD_LERROR,
				 "%s: Cannot have no/autobuffer with buffer "
				 "type\n", __FILE__);
			return EINVAL;
		} else {
			*rbs = tempbs;
			*rbt = 0;
			return 0;
		}
	}

	if (!bt) {
		INST_LOG(inst, LDMSD_LERROR,
			 "%s: Cannot have buffer size with no buffer type\n",
			 __FILE__);
		return EINVAL;
	}

	tempbt = atoi(bt);
	if ((tempbt != 3) && (tempbt != 4)) {
		INST_LOG(inst, LDMSD_LERROR,
			 "%s: Invalid buffer type %d\n",
			 __FILE__, tempbt);
		return EINVAL;
	}

	if (tempbt == 4) {
		//adjust bs for kb
		tempbs *= 1024;
	}

	*rbs = tempbs;
	*rbt = tempbt;

	return 0;
}


/* ============== Store Plugin APIs ================= */

static
int __try_print_header(store_function_csv_inst_t inst, ldmsd_strgp_t strgp)
{
	/* inst->lock must be held */
	FILE *fp = NULL;
	time_t appx = time(NULL);
	char tmp_headerpath[PATH_MAX];
	int i, j;

	switch (inst->printheader) {
	case FIRST_PRINT_HEADER:
	case DO_PRINT_HEADER:
		/* continue to print the header */
		break;
	case DONT_PRINT_HEADER:
		/* don't print, just return a success */
		return 0;
	case BAD_HEADER:
		/* fall through */
	default:
		/* bad value */
		return EINVAL;
	}

	if (inst->parseconfig) {
		/* config parse still needed */
		return EBUSY;
	}

	if (inst->altheader) {
		if (inst->rolltype >= MINROLLTYPE){
			snprintf(tmp_headerpath, PATH_MAX, "%s.HEADER.%d",
				 inst->path, (int)appx);
		} else {
			snprintf(tmp_headerpath, PATH_MAX, "%s.HEADER",
				 inst->path);
		}

		/* truncate a separate headerfile if its the first time */
		if (inst->printheader == FIRST_PRINT_HEADER) {
			fp = fopen_perm(tmp_headerpath, "w", LDMSD_DEFAULT_FILE_PERM);
		} else if (inst->printheader == DO_PRINT_HEADER){
			fp = fopen_perm(tmp_headerpath, "a+", LDMSD_DEFAULT_FILE_PERM);
		}
		if (!fp) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Error: Cannot open headerfile\n");
			return errno;
		}
	} else {
		fp = inst->file;
	}

	/* This allows optional loading a float (Time) into an int field and retaining usec as
	   a separate field */
	fprintf(fp, "#Time,Time_usec,DT,DT_usec");
	fprintf(fp, ",ProducerName");
	fprintf(fp, ",component_id,job_id");

	//Print the header using the metrics associated with this set
	for (i = 0; i < inst->numder; i++) {
		if (inst->der[i]->writeout) {
			if (inst->der[i]->dim == 1) {
				fprintf(fp, ",%s,%s.Flag",
					inst->der[i]->name, inst->der[i]->name);
			} else {
				for (j = 0; j < inst->der[i]->dim; j++) {
					fprintf(fp, ",%s%s%d",
						inst->der[i]->name,
						HDR_ARRAY_SEP, j);
				}
				fprintf(fp, ",%s.Flag", inst->der[i]->name);
			}
		}
	}
	fprintf(fp, ",TimeFlag\n");

	/* Flush for the header, whether or not it is the data file as well */
	fflush(fp);
	fsync(fileno(fp));
	if (fp != inst->file)
		fclose(fp);

	inst->printheader = DONT_PRINT_HEADER;

	return 0;
}

int store_function_csv_open(ldmsd_plugin_inst_t pi, ldmsd_strgp_t strgp)
{
	/* Perform `open` operation */
	store_function_csv_inst_t inst = (void*)pi;
	char tmp_path[PATH_MAX];
	time_t appx = time(NULL);
	int rc;

	pthread_mutex_lock(&inst->lock);

	if (inst->file) {
		rc = EALREADY; /* already open */
		goto out;
	}
	inst->strgp = strgp;
	if (inst->rolltype >= MINROLLTYPE) {
		snprintf(tmp_path, PATH_MAX, "%s.%d", inst->path, (int)appx);
	} else {
		snprintf(tmp_path, PATH_MAX, "%s", inst->path);
	}
	inst->file = fopen_perm(tmp_path, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!inst->file) {
		INST_LOG(inst, LDMSD_LERROR,
			 "%s: Error %d opening the file %s.\n",
			 __FILE__, errno, tmp_path);

		rc = errno;
		goto out;
	}

	inst->printheader = FIRST_PRINT_HEADER;
	rc = scheduleRollover(inst);

out:
	pthread_mutex_unlock(&inst->lock);
	return rc;
}

int store_function_csv_close(ldmsd_plugin_inst_t pi)
{
	/* Perform `close` operation */
	store_function_csv_inst_t inst = (void*)pi;

	pthread_mutex_lock(&inst->lock);
	if (inst->file)
		fclose(inst->file);
	inst->file = NULL;
	inst->strgp = NULL;
	ovis_scheduler_event_del(roll_sched, &inst->roll_ev);
	pthread_mutex_unlock(&inst->lock);
	return 0;
}

int store_function_csv_flush(ldmsd_plugin_inst_t pi)
{
	/* Perform `flush` operation */
	store_function_csv_inst_t inst = (void*)pi;
	if (inst->file)
		fflush(inst->file);
	return 0;
}

static int get_datapoint(store_function_csv_inst_t inst,
			 const char* instance_name,
			 struct setdatapoint** rdp,
			 int* firsttime)
{
	/* inst->lock must be held */
	struct setdatapoint* dp = NULL;
	int i, rc;
	int name_len = strlen(instance_name);
	struct dinfo *dv;

	if (rdp == NULL) {
		INST_LOG(inst, LDMSD_LERROR,
			 "%s:%d arg to getDatapoint is NULL!\n",
			 __FILE__, __LINE__);
		return EINVAL;
	}

	dp = idx_find(inst->sets_idx, (void*)instance_name, name_len);

	if (dp) {
		*firsttime = 0;
		goto out;
	}

	*firsttime = 1;
	//create a container to hold it
	dp = calloc(1, sizeof(*dp) + inst->numder * sizeof(dp->datavals[0]));
	if (!dp) {
		INST_LOG(inst, LDMSD_LCRITICAL,
			 "%s:%d ENOMEM\n", __FILE__, __LINE__);
		return ENOMEM;
	}

	dp->ts.sec = 0;
	dp->ts.usec = 0;
	inst->numsets++;

	//create the space for the return vals and store vals if needed
	for (i = 0; i < inst->numder; i++) {
		dv = &dp->datavals[i];
		dv->dim = inst->der[i]->dim;

		if (func_def[inst->der[i]->fct].createreturn) {
			dv->returnvals = calloc(dv->dim, sizeof(uint64_t));
			if (!dp->datavals[i].returnvals) {
				rc = ENOMEM;
				goto err;
			}
			dv->returnvalid = 0;
		}

		if (func_def[inst->der[i]->fct].createstore){
			dv->storevals = calloc(dv->dim, sizeof(uint64_t));
			if (!dv->storevals) {
				rc = ENOMEM;
				goto err;
			}
			dv->storevalid = 0;
		}
	}

	*firsttime = 1;

	rc = idx_add(inst->sets_idx, (void*)instance_name, name_len, dp);
	if (rc)
		goto err;

out:
	*rdp = dp;
	return 0;

err:
	if (dp) {
		for (i = 0; i < inst->numder; i++) {
			dv = &dp->datavals[i];
			if (dv->returnvals)
				free(dv->returnvals);
			if (dv->storevals)
				free(dv->storevals);
		}
		free(dp);
	}
	return rc;
}

static int doRAWTERMFunc(store_function_csv_inst_t inst,
			 ldms_set_t set, int* metric_array,
			 struct derived_data* dd)
{
	//using these to ease updating the data for this metric
	struct idx_type* vals = dd->varidx; //dependent vars indicies. only 1
	int i = vals[0].i;
	int idx = metric_array[i];
	enum ldms_value_type rtype = vals[0].metric_type;
	double scale = dd->scale;
	double val;
	int dim = dd->dim;
	int j;

	//it can only be a base.
	//it must be valid, becuase the raw data is valid - will writeout flag 0 at end
	switch (rtype) {
	case LDMS_V_CHAR:
		//scale is unused
		iprintf(inst, ",%c", ldms_metric_get_char(set, idx));
		break;
	case LDMS_V_U8:
		iprintf(inst, ",%hhu",
			(uint8_t)(ldms_metric_get_u8(set, idx) * scale));
		break;
	case LDMS_V_S8:
		iprintf(inst, ",%hhd",
			(int8_t)(ldms_metric_get_s8(set, idx) * scale));
		break;
	case LDMS_V_U16:
		iprintf(inst, ",%hu",
			(uint16_t)(ldms_metric_get_u16(set, idx) * scale));
		break;
	case LDMS_V_S16:
		iprintf(inst, ",%hd",
			(int16_t)(ldms_metric_get_s16(set, idx) * scale));
		break;
	case LDMS_V_U32:
		iprintf(inst, ",%" PRIu32,
			(uint32_t)(ldms_metric_get_u32(set, idx) * scale));
		break;
	case LDMS_V_S32:
		iprintf(inst, ",%" PRId32,
			(int32_t)(ldms_metric_get_s32(set, idx) * scale));
		break;
	case LDMS_V_U64:
		iprintf(inst, ",%"PRIu64,
			(uint64_t)(ldms_metric_get_u64(set, idx) * scale));
		break;
	case LDMS_V_S64:
		iprintf(inst, ",%" PRId64,
			(int64_t)(ldms_metric_get_s64(set, idx) * scale));
		break;
	case LDMS_V_F32:
		iprintf(inst, ",%f",
			(float)(ldms_metric_get_float(set, idx) * scale));
		break;
	case LDMS_V_D64:
		iprintf(inst, ",%lf", ldms_metric_get_double(set, idx) * scale);
		break;
	case LDMS_V_CHAR_ARRAY:
		//scale unused
		iprintf(inst, ",%s", ldms_metric_array_get_str(set, idx));
		break;
	case LDMS_V_U8_ARRAY:
		for (j = 0; j < dim; j++) {
			val = ldms_metric_array_get_u8(set, idx, j) * scale;
			iprintf(inst, ",%hhu", (uint8_t)val);
		}
		break;
	case LDMS_V_S8_ARRAY:
		for (j = 0; j < dim; j++) {
			val = ldms_metric_array_get_s8(set, idx, j) * scale;
			iprintf(inst, ",%hhd", (int8_t)val);
		}
		break;
	case LDMS_V_U16_ARRAY:
		for (j = 0; j < dim; j++) {
			val = ldms_metric_array_get_u16(set, idx, j) * scale;
			iprintf(inst, ",%hu", (uint16_t)val);
		}
		break;
	case LDMS_V_S16_ARRAY:
		for (j = 0; j < dim; j++) {
			val = ldms_metric_array_get_s16(set, idx, j) * scale;
			iprintf(inst, ",%hd", (int16_t)val);
		}
		break;
	case LDMS_V_U32_ARRAY:
		for (j = 0; j < dim; j++) {
			val = ldms_metric_array_get_u32(set, idx, j) * scale;
			iprintf(inst, ",%"PRIu32, (uint32_t)val);
		}
		break;
	case LDMS_V_S32_ARRAY:
		for (j = 0; j < dim; j++) {
			val = ldms_metric_array_get_s32(set, idx, j) * scale;
			iprintf(inst, ",%"PRId32, (int32_t)val);
		}
		break;
	case LDMS_V_U64_ARRAY:
		for (j = 0; j < dim; j++) {
			val = ldms_metric_array_get_u64(set, idx, j) * scale;
			iprintf(inst, ",%"PRIu64, (uint64_t)val);
		}
		break;
	case LDMS_V_S64_ARRAY:
		for (j = 0; j < dim; j++) {
			val = ldms_metric_array_get_s64(set, idx, j) * scale;
			iprintf(inst, ",%"PRId64, (int64_t)val);
		}
		break;
	case LDMS_V_F32_ARRAY:
		for (j = 0; j < dim; j++) {
			val = ldms_metric_array_get_float(set, idx, j) * scale;
			iprintf(inst, ",%f", (float)val);
		}
		break;
	case LDMS_V_D64_ARRAY:
		for (j = 0; j < dim; j++) {
			val = ldms_metric_array_get_double(set, idx, j) * scale;
			iprintf(inst, ",%lf", val);
		}
		break;
	default:
		//print no value
		iprintf(inst, ",");
		break;
	}

	//print the flag -- which is always 0
	iprintf(inst, ",0");

	return 1; //always valid
}

typedef int (*do_func_fn)(store_function_csv_inst_t inst, ldms_set_t set,
			  int* metric_arry, struct setdatapoint* dp,
			  struct derived_data* dd,
			  struct timeval diff, int flagtime);

static
int __func_pointwise(store_function_csv_inst_t inst, ldms_set_t set,
	       int* metric_arry, struct setdatapoint* dp,
	       struct derived_data* dd, uint64_t op(uint64_t, double))
{
	int j;
	struct idx_type* vals = dd->varidx; //dependent vars indicies
	double scale = dd->scale;
	int dim = dd->dim;
	int idx = dd->idx;
	uint64_t* retvals = dp->datavals[idx].returnvals;
	int* retvalid = &(dp->datavals[idx].returnvalid);

	if (vals[0].typei == BASE) {
		*retvalid = 1;
		if (vals[0].metric_type == LDMS_V_U64) {
			uint64_t temp = ldms_metric_get_u64(set,
							metric_arry[vals[0].i]);
			retvals[0] = op(temp, scale);
		} else { //it must be an array
			for (j = 0; j < dim; j++){
				uint64_t temp = ldms_metric_array_get_u64(set,
						     metric_arry[vals[0].i], j);
				retvals[j] = op(temp, scale);
			}
		}
	} else {
		*retvalid = dp->datavals[vals[0].i].returnvalid;
		if (*retvalid) {
			for (j = 0; j < dim; j++) {
				uint64_t temp =
					dp->datavals[vals[0].i].returnvals[j];
				retvals[j] = op(temp, scale);
			}
		} else {
			for (j = 0; j < dim; j++)
				retvals[j] = 0;
		}
	}
	return *retvalid;
}

uint64_t __mult(uint64_t m, double scale)
{
	return m * scale;
}

static
int __func_raw(store_function_csv_inst_t inst, ldms_set_t set,
	       int* metric_arry, struct setdatapoint* dp,
	       struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_pointwise(inst, set, metric_arry, dp, dd,  __mult);
}

static uint64_t __ge(uint64_t m, double scale)
{
	return m >= scale;
}

static
int __func_thresh_ge(store_function_csv_inst_t inst, ldms_set_t set,
		     int* metric_arry, struct setdatapoint* dp,
		     struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_pointwise(inst, set, metric_arry, dp, dd,  __ge);
}

static uint64_t __lt(uint64_t m, double scale)
{
	return m < scale;
}

static
int __func_thresh_lt(store_function_csv_inst_t inst, ldms_set_t set,
		     int* metric_arry, struct setdatapoint* dp,
		     struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_pointwise(inst, set, metric_arry, dp, dd,  __lt);
}

static uint64_t __op_max(uint64_t a, uint64_t b)
{
	return a>b?a:b;
}

static uint64_t __op_min(uint64_t a, uint64_t b)
{
	return a<b?a:b;
}

static uint64_t __op_sum(uint64_t a, uint64_t b)
{
	return a+b;
}

uint64_t __op_sub(uint64_t a, uint64_t b)
{
	return a - b;
}

uint64_t __op_mul(uint64_t a, uint64_t b)
{
	return a * b;
}

uint64_t __op_div(uint64_t a, uint64_t b)
{
	return a / b;
}

static
int __func_reduce(store_function_csv_inst_t inst, ldms_set_t set,
		  int* metric_arry, struct setdatapoint* dp,
		  struct derived_data* dd,
		  uint64_t (*op)(uint64_t, uint64_t))
{
	int j;
	struct idx_type* vals = dd->varidx; //dependent vars indicies
	double scale = dd->scale;
	int idx = dd->idx;
	uint64_t* retvals = dp->datavals[idx].returnvals;
	int* retvalid = &(dp->datavals[idx].returnvalid);
	if (vals[0].typei == BASE) {
		*retvalid = 1;
		if (vals[0].metric_type == LDMS_V_U64){
			retvals[0] = ldms_metric_get_u64(set, metric_arry[vals[0].i]) * scale;
		} else { //it must be an array
			retvals[0] = ldms_metric_array_get_u64(set, metric_arry[vals[0].i], 0);
			for (j = 1; j < vals[0].dim; j++) {
				uint64_t curr = ldms_metric_array_get_u64(set, metric_arry[vals[0].i], j);
				retvals[0] = op(retvals[0], curr);
			}
			retvals[0] *= scale;
		}
	} else {
		*retvalid = dp->datavals[vals[0].i].returnvalid;
		if (*retvalid){
			retvals[0] = (dp->datavals[vals[0].i].returnvals[0]);
			for (j = 1; j < vals[0].dim; j++){
				uint64_t curr = dp->datavals[vals[0].i].returnvals[j];
				retvals[0] = op(retvals[0], curr);
			}
			retvals[0] *= scale;
		} else {
			retvals[0] = 0;
		}
	}
	return *retvalid;
}

static
int __func_max(store_function_csv_inst_t inst, ldms_set_t set,
		     int* metric_arry, struct setdatapoint* dp,
		     struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_reduce(inst, set, metric_arry, dp, dd,  __op_max);
}

static
int __func_min(store_function_csv_inst_t inst, ldms_set_t set,
		     int* metric_arry, struct setdatapoint* dp,
		     struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_reduce(inst, set, metric_arry, dp, dd,  __op_min);
}

static
int __func_sum(store_function_csv_inst_t inst, ldms_set_t set,
		     int* metric_arry, struct setdatapoint* dp,
		     struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_reduce(inst, set, metric_arry, dp, dd,  __op_sum);
}

static
int __func_avg(store_function_csv_inst_t inst, ldms_set_t set,
		     int* metric_arry, struct setdatapoint* dp,
		     struct derived_data* dd, struct timeval diff, int flagtime)
{
	int rc;
	rc = __func_reduce(inst, set, metric_arry, dp, dd,  __op_sum);
	dp->datavals[dd->idx].returnvals[0] /= dd->varidx[0].dim;
	return rc;
}

static
int __func_delta(store_function_csv_inst_t inst, ldms_set_t set,
		     int* metric_arry, struct setdatapoint* dp,
		     struct derived_data* dd, struct timeval diff, int flagtime)
{
	struct idx_type* vals = dd->varidx; //dependent vars indicies
	double scale = dd->scale;
	int dim = dd->dim;
	int idx = dd->idx;
	uint64_t* retvals = dp->datavals[idx].returnvals;
	int* retvalid = &(dp->datavals[idx].returnvalid);
	uint64_t* storevals = dp->datavals[idx].storevals;
	int* storevalid = &(dp->datavals[idx].storevalid);
	uint64_t temp;

	int j;

	*retvalid = 1;
	if (vals[0].typei == DER) {
		//...this is about the validity of current value of the dependent variable
		*retvalid = dp->datavals[vals[0].i].returnvalid;
		if (!*retvalid)
			goto out;
		for (j = 0; j < dim; j++) {
			temp = dp->datavals[vals[0].i].returnvals[j];
			if (temp < storevals[j])
				*retvalid = 0; //return also invalid if negative
			retvals[j] = (temp - storevals[j]) * scale;
			storevals[j] = temp;
		}
	} else if (vals[0].metric_type == LDMS_V_U64) {
		/* BASE single metric */
		temp = ldms_metric_get_u64(set, metric_arry[vals[0].i]);
		retvals[0] = (temp - storevals[0]) * scale;
		storevals[0] = temp; /* update stored value */
	} else {
		/* BASE array metric */
		for (j = 0; j < dim; j++) {
			temp = ldms_metric_array_get_u64(set,
					metric_arry[vals[0].i], j);
			if (temp < storevals[j])
				*retvalid = 0; //return also invalid if negative
			retvals[j] = (temp - storevals[j]) * scale;
			storevals[j] = temp;
		}
	}

	if (!*storevalid || flagtime) //return also invalid if back in time or this store invalid
		*retvalid = 0;
	*storevalid = 1; /* store values is valid for the next evaluation */

out:
	if (!*retvalid) {
		/* reset return values if invalid */
		for (j = 0; j < dim; j++)
			retvals[j] = 0;
	}
	return *retvalid;
}

static
int __func_rate(store_function_csv_inst_t inst, ldms_set_t set,
		     int* metric_arry, struct setdatapoint* dp,
		     struct derived_data* dd, struct timeval diff, int flagtime)
{
	/* rate = delta / sec */
	int rc, i;
	double sec;
	uint64_t* retvals = dp->datavals[dd->idx].returnvals;
	rc = __func_delta(inst, set, metric_arry, dp, dd, diff, flagtime);
	if (rc) {
		sec = diff.tv_sec + diff.tv_usec * 1e-6;
		for (i = 0; i < dd->dim; i++) {
			retvals[i] /= sec;
		}
	}
	return rc;
}

static
int __func_n_reduce(store_function_csv_inst_t inst, ldms_set_t set,
		    int* metric_arry, struct setdatapoint* dp,
		    struct derived_data* dd,
		    uint64_t (*op)(uint64_t, uint64_t))
{
	/* multi variate reduce */

	int nvals = dd->nvars; //dependent vars
	struct idx_type* vals = dd->varidx; //dependent vars indicies
	double scale = dd->scale;
	int dim = dd->dim;
	int idx = dd->idx;
	uint64_t* retvals = dp->datavals[idx].returnvals;
	int* retvalid = &(dp->datavals[idx].returnvalid);

	int i, j;

	//this is multivariate...and the same dimensionality as the var
	/* retval is not valid if any of the inputs is invalid */
	//storeval does not matter

	*retvalid = 1;

	/* initialize retvals with first value */
	if (vals[0].typei == DER) {
		*retvalid = dp->datavals[vals[0].i].returnvalid;
		if (*retvalid == 0) //abort
			goto out;
		for (j = 0; j < dim; j++) {
			retvals[j] = dp->datavals[vals[0].i].returnvals[j];
		}
	}
	else if (vals[0].metric_type == LDMS_V_U64) {
		retvals[0] = ldms_metric_get_u64(set, metric_arry[vals[0].i]);
	} else {
		for (j = 0; j < dim; j++) {
			retvals[j] = ldms_metric_array_get_u64(set,
						metric_arry[vals[0].i], j);
		}
	}

	for (i = 1; i < nvals; i++){
		if (vals[i].typei == DER) {
			*retvalid = dp->datavals[vals[i].i].returnvalid;
			if (*retvalid == 0) //abort
				goto out;
			for (j = 0; j < dim; j++) {
				retvals[j] = op(retvals[j],
					dp->datavals[vals[i].i].returnvals[j]);
			}
		}
		else if (vals[i].metric_type == LDMS_V_U64) {
			retvals[0] = op(retvals[0],
					ldms_metric_get_u64(set,
						metric_arry[vals[i].i]));
		} else {
			for (j = 0; j < dim; j++) {
				retvals[j] = op(retvals[j],
						ldms_metric_array_get_u64(set,
						    metric_arry[vals[i].i], j));
			}
		}
	}
out:
	if (*retvalid) {
		for (j = 0; j < dim; j++)
			retvals[j] *= scale;
	} else {
		for (j = 0; j < dim; j++)
			retvals[j] = 0;
	}
	return *retvalid;
}

static
int __func_max_n(store_function_csv_inst_t inst, ldms_set_t set,
		 int* metric_arry, struct setdatapoint* dp,
		 struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_n_reduce(inst, set, metric_arry, dp, dd, __op_max);
}

static
int __func_min_n(store_function_csv_inst_t inst, ldms_set_t set,
		 int* metric_arry, struct setdatapoint* dp,
		 struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_n_reduce(inst, set, metric_arry, dp, dd, __op_min);
}

static
int __func_sum_n(store_function_csv_inst_t inst, ldms_set_t set,
		 int* metric_arry, struct setdatapoint* dp,
		 struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_n_reduce(inst, set, metric_arry, dp, dd, __op_sum);
}

static
int __func_avg_n(store_function_csv_inst_t inst, ldms_set_t set,
		 int* metric_arry, struct setdatapoint* dp,
		 struct derived_data* dd, struct timeval diff, int flagtime)
{
	/* avg is sum / nvars */
	int i, rc;
	uint64_t* retvals = dp->datavals[dd->idx].returnvals;
	rc = __func_n_reduce(inst, set, metric_arry, dp, dd, __op_sum);
	if (rc) {
		for (i = 0; i < dd->dim; i++) {
			/* scale already applied by __func_n_reduce() */
			retvals[i] /= (double)dd->nvars;
		}
	}
	return rc;
}

static
int __func_ab(store_function_csv_inst_t inst, ldms_set_t set,
		 int* metric_arry, struct setdatapoint* dp,
		 struct derived_data* dd,
		 uint64_t (op)(uint64_t, uint64_t))
{
	//this is bivariate and the same dimensionality as the var
	//SUB will flag upon neg. set value to zero.
	//
	struct idx_type* vals = dd->varidx; //dependent vars indicies
	double scale = dd->scale;
	int dim = dd->dim;
	int idx = dd->idx;
	uint64_t* retvals = dp->datavals[idx].returnvals;
	int* retvalid = &(dp->datavals[idx].returnvalid);

	int j;
	uint64_t x0, x1;

	*retvalid = 1;

	if (vals[0].typei == DER) {
		/* DERIVED */
		*retvalid *= dp->datavals[vals[0].i].returnvalid;
		*retvalid *= dp->datavals[vals[1].i].returnvalid;
		if (!retvalid)
			goto out;
		for (j = 0; j < dim; j++) {
			x0 = dp->datavals[vals[0].i].returnvals[j];
			x1 = dp->datavals[vals[1].i].returnvals[j];
			if (op == __op_sub && x0 < x1) {
				*retvalid = 0;
				goto out;
			}
			retvals[j] = op(x0, x1) * scale;
		}
	} else if (vals[0].metric_type == LDMS_V_U64) {
		/* BASE scalar */
		x0 = ldms_metric_get_u64(set, metric_arry[vals[0].i]);
		x1 = ldms_metric_get_u64(set, metric_arry[vals[1].i]);
		if (op == __op_sub && x0 < x1) {
			*retvalid = 0;
			goto out;
		}
		retvals[0] = op(x0, x1) * scale;
	} else {
		/* BASE array */
		for (j = 0; j < dim; j++) {
			x0 = ldms_metric_array_get_u64(set,
						metric_arry[vals[0].i], j);
			x1 = ldms_metric_array_get_u64(set,
						metric_arry[vals[1].i], j);
			if (op == __op_sub && x0 < x1) {
				*retvalid = 0;
				goto out;
			}
			retvals[j] = op(x0, x1) * scale;
		}
	}
out:
	if (!*retvalid) {
		for (j = 0; j < dim; j++) {
			retvals[j] = 0;
		}
	}
	return *retvalid;
}

static
int __func_sub_ab(store_function_csv_inst_t inst, ldms_set_t set,
		  int* metric_arry, struct setdatapoint* dp,
		  struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_ab(inst, set, metric_arry, dp, dd, __op_sub);
}

static
int __func_mul_ab(store_function_csv_inst_t inst, ldms_set_t set,
		  int* metric_arry, struct setdatapoint* dp,
		  struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_ab(inst, set, metric_arry, dp, dd, __op_mul);
}

static
int __func_div_ab(store_function_csv_inst_t inst, ldms_set_t set,
		  int* metric_arry, struct setdatapoint* dp,
		  struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_ab(inst, set, metric_arry, dp, dd, __op_div);
}

static
int __func_vs(store_function_csv_inst_t inst, ldms_set_t set,
		 int* metric_arry, struct setdatapoint* dp,
		 struct derived_data* dd,
		 int v_idx,
		 uint64_t (op)(uint64_t, uint64_t))
{
	int s_idx = !v_idx;
	double scale = dd->scale;
	int dim = dd->dim;
	int idx = dd->idx;
	uint64_t* retvals = dp->datavals[idx].returnvals;
	int* retvalid = &(dp->datavals[idx].returnvalid);

	int j;
	uint64_t s, v;
	met_t mt;

	struct idx_type *vs = &dd->varidx[s_idx];
	struct idx_type *vv = &dd->varidx[v_idx];

	*retvalid = 1;
	if (vs->typei == DER) {
		*retvalid = dp->datavals[vs->i].returnvalid;
		s = dp->datavals[vs->i].returnvals[0];
	} else if (vs->metric_type == LDMS_V_U64) {
		s = ldms_metric_get_u64(set, metric_arry[vs->i]);
	} else {
		s = ldms_metric_array_get_u64(set, metric_arry[vs->i], 0); //dim 1
	}
	if (!*retvalid)
		goto out;
	if (vv->metric_type == LDMS_V_U64) {
		v = ldms_metric_get_u64(set, metric_arry[vv->i]);
		*retvalid = v_idx?(s>=v):(v>=s);
		if (!*retvalid)
			goto out;
		retvals[0] = (v_idx?op(s, v):op(v, s)) * scale;
	} else {
		mt = vv->typei;
		*retvalid = (mt == DER)?(dp->datavals[vv->i].returnvalid):(1);
		if (!retvalid)
			goto out;
		for (j = 0; j < dim; j++) {
			if (mt == DER) {
				v = dp->datavals[vv->i].returnvals[j];
			} else {
				v = ldms_metric_array_get_u64(set,
							metric_arry[vv->i], j);
			}
			if (op == __op_sub) {
				/* guard negative result */
				*retvalid = v_idx?(s>=v):(v>=s);
			} else if (op == __op_div) {
				/* guard divided by zero */
				*retvalid = v_idx?(v>0):(s>0);
			}
			if (!*retvalid)
				goto out;
			retvals[j] = (v_idx?op(s, v):op(v, s)) * scale;
		}
	}

out:
	if (!*retvalid) {
		for (j = 0; j < dim; j++) {
			retvals[j] = 0;
		}
	}
	return *retvalid;
}

static
int __func_sum_vs(store_function_csv_inst_t inst, ldms_set_t set,
		  int* metric_arry, struct setdatapoint* dp,
		  struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_vs(inst, set, metric_arry, dp, dd, 0, __op_sum);
}

static
int __func_sub_vs(store_function_csv_inst_t inst, ldms_set_t set,
		  int* metric_arry, struct setdatapoint* dp,
		  struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_vs(inst, set, metric_arry, dp, dd, 0, __op_sub);
}

static
int __func_sub_sv(store_function_csv_inst_t inst, ldms_set_t set,
		  int* metric_arry, struct setdatapoint* dp,
		  struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_vs(inst, set, metric_arry, dp, dd, 1, __op_sub);
}

static
int __func_mul_vs(store_function_csv_inst_t inst, ldms_set_t set,
		  int* metric_arry, struct setdatapoint* dp,
		  struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_vs(inst, set, metric_arry, dp, dd, 0, __op_mul);
}

static
int __func_div_vs(store_function_csv_inst_t inst, ldms_set_t set,
		  int* metric_arry, struct setdatapoint* dp,
		  struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_vs(inst, set, metric_arry, dp, dd, 0, __op_div);
}

static
int __func_div_sv(store_function_csv_inst_t inst, ldms_set_t set,
		  int* metric_arry, struct setdatapoint* dp,
		  struct derived_data* dd, struct timeval diff, int flagtime)
{
	return __func_vs(inst, set, metric_arry, dp, dd, 1, __op_div);
}

static do_func_fn do_func[] = {
	[RATE]      = __func_rate,
	[DELTA]     = __func_delta,
	[RAW]       = __func_raw,
	[RAWTERM]   = NULL,
	[MAX_N]     = __func_max_n,
	[MIN_N]     = __func_min_n,
	[SUM_N]     = __func_sum_n,
	[AVG_N]     = __func_avg_n,
	[SUB_AB]    = __func_sub_ab,
	[MUL_AB]    = __func_mul_ab,
	[DIV_AB]    = __func_div_ab,
	[THRESH_GE] = __func_thresh_ge,
	[THRESH_LT] = __func_thresh_lt,
	[MAX]       = __func_max,
	[MIN]       = __func_min,
	[SUM]       = __func_sum,
	[AVG]       = __func_avg,
	[SUM_VS]    = __func_sum_vs,
	[SUB_VS]    = __func_sub_vs,
	[SUB_SV]    = __func_sub_sv,
	[MUL_VS]    = __func_mul_vs,
	[DIV_VS]    = __func_div_vs,
	[DIV_SV]    = __func_div_sv,
};

static int doFunc(store_function_csv_inst_t inst, ldms_set_t set,
		  int* metric_arry, struct setdatapoint* dp,
		  struct derived_data* dd,
		  struct timeval diff, int flagtime)
{
	// CAN PASS IN  struct timeval curr, struct timeval prev, for debugging...

	/*
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

	int rc;
	if (dd->fct > FCT_END || dd->fct == RAWTERM) {
		/* invalid function */
		return EINVAL;
	}

	rc = do_func[dd->fct](inst, set, metric_arry, dp, dd, diff, flagtime);
	return rc;
}

int store_function_csv_store(ldmsd_plugin_inst_t pi, ldms_set_t set,
			     ldmsd_strgp_t strgp)
{
	/* `store` data from `set` into the store */
	store_function_csv_inst_t inst = (void*)pi;
	const struct ldms_timestamp _ts = ldms_transaction_timestamp_get(set);
	const struct ldms_timestamp *ts = &_ts;
	struct setdatapoint* dp = NULL;
	const char* pname;
	uint64_t compid;
	uint64_t jobid;
	struct timeval prev, curr, diff;
	int skip = 0;
	int setflagtime = 0;
	int tempidx;
	int doflush = 0;
	int rc;
	int i, j;

	pthread_mutex_lock(&inst->lock);

	if (!inst->file) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Cannot insert values for <%s>: file is closed\n",
			 inst->path);
		rc = EINVAL;
		goto out;
	}

	if (inst->parseconfig) {
		/* config parsing needed */
		rc = derivedConfig(inst, inst->derivedconf, set,
				   strgp->metric_arry, strgp->metric_count);
		if (rc != 0) {
			INST_LOG(inst, LDMSD_LERROR,
				 "derivedConfig failed, rc: %d (%s:%d)\n",
				 rc, __FILE__, __LINE__);
			goto out;
		}
		inst->parseconfig = 0;
	}

	rc = __try_print_header(inst, strgp);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Header printing failed, rc: %d\n", rc);
		goto out;
	}

	rc = get_datapoint(inst, ldms_set_instance_name_get(set), &dp, &skip);
	if (rc)
		goto out;

	/*
	 * New in v3: if time diff is not positive, always write out something and flag.
	 * if its RAW data, write the val. if its RATE data, write zero
	 */

	setflagtime = 0;
	prev.tv_sec = dp->ts.sec;
	prev.tv_usec = dp->ts.usec;
	curr.tv_sec = ts->sec;
	curr.tv_usec = ts->usec;

	if (prev.tv_sec*1e6+prev.tv_usec >= curr.tv_sec*1e6+curr.tv_usec) {
		INST_LOG(inst, LDMSD_LDEBUG,
			 " %s:%d Time diff is <= 0 for set %s. Flagging\n",
			 __FILE__, __LINE__, ldms_set_instance_name_get(set));
		setflagtime = 1;
	}
	//always do this and write it out
	timersub(&curr, &prev, &diff);

	pname = ldms_set_producer_name_get(set);

	tempidx = ldms_metric_by_name(set, "component_id");
	if (tempidx != -1)
		compid = ldms_metric_get_u64(set, tempidx);
	else
		compid = 0;

	tempidx = ldms_metric_by_name(set, "job_id");
	if (tempidx != -1)
		jobid = ldms_metric_get_u64(set, tempidx);
	else
		jobid = 0;

	if (!skip) {
		/* format: #Time, Time_usec, DT, DT_usec */
		iprintf(inst, "%"PRIu32".%06"PRIu32 ",%"PRIu32,
			ts->sec, ts->usec, ts->usec);
		iprintf(inst, ",%lu.%06lu,%lu",
			diff.tv_sec, diff.tv_usec, diff.tv_usec);

		if (pname != NULL){
			iprintf(inst, ",%s", pname);
		} else {
			iprintf(inst, ",");
		}

		iprintf(inst, ",%"PRIu64",%"PRIu64, compid, jobid);
	}

	//always get the vals because may need the stored value, even if skip this time

	for (i = 0; i < inst->numder; i++) {
		//go thru all the vals....only write the writeout vals

		int rvalid;

		if (inst->der[i]->fct == RAWTERM) {
			//this will also do its writeout
			if (!skip)
				rvalid = doRAWTERMFunc(inst, set, strgp->metric_arry, inst->der[i]);
		} else {
			rvalid = doFunc(inst, set, strgp->metric_arry,
					dp, inst->der[i],
					diff, setflagtime);
			//write it out, if its writeout and not skip
			//FIXME: Should the writeout be moved in so its like doRAWTERMFunc ?
			if (!skip && inst->der[i]->writeout) {
				struct dinfo* di = &(dp->datavals[i]);
				for (j = 0; j < di->dim; j++) {
					iprintf(inst, ",%" PRIu64,
						di->returnvals[j]);
				}
				iprintf(inst, ",%d", (!di->returnvalid));
			}
		}
	}

	//finally update the time for this whole set.
	dp->ts.sec = curr.tv_sec;
	dp->ts.usec = curr.tv_usec;

	if (!setflagtime)
		if ((inst->ageusec > 0) && ((diff.tv_sec*1e6+diff.tv_usec) > inst->ageusec))
			setflagtime = 1;

	if (!skip) {
		iprintf(inst, ",%d\n", setflagtime); //NOTE: currently only setting flag based on time
		inst->store_count++;

		if ((inst->buffer_type == 3) &&
		    ((inst->store_count - inst->lastflush) >=
		     inst->buffer_sz)) {
			inst->lastflush = inst->store_count;
			doflush = 1;
		} else if ((inst->buffer_type == 4) &&
				((inst->byte_count - inst->lastflush) >=
				 inst->buffer_sz)){
			inst->lastflush = inst->byte_count;
			doflush = 1;
		}
		if ((inst->buffer_sz == 0) || doflush){
			fflush(inst->file);
			fsync(fileno(inst->file));
		}
	}
	rc = 0;
out:
	pthread_mutex_unlock(&inst->lock);
	return rc;
}

/* ============== Common Plugin APIs ================= */

static
const char *store_function_csv_desc(ldmsd_plugin_inst_t pi)
{
	return "store_function_csv - Function CSV Storage plugin";
}

static
char *_help = "\n\
store_function_csv config synopsis:\n\
    config name=<INST> [COMMON_OPTIONS] path=<PATH> derivedconf=<PATH> \n\
		[altheader=(0|1)] [rollover=<NUM> rolltype=<NUM>]\n\
		[buffer=(0|1|N) buffertype=(3|4)]\n\
		[ageusec=<USEC>]\n\
\n\
Option descriptions:\n\
	- path       The path to the CSV file.\n\
	- altheader  Print header in a separate file (optional, default 0).\n\
	- buffer     0 to disable buffering, 1 to enable it with autosize, \n\
		     N>1 to flush after that many kB (N>4, buffertype=4) \n\
		     or that many lines (N>1, buffertype=3). \n\
		     (default: 1).\n\
	- buffertype 3 for flushing after N lines, 4 for flusing after N kB,\n\
		     where N is the value of `buffer` attribute.\n\
	- rollover   Greater than zero; enables file rollover and sets interval\n\
	- rolltype   [1-n] Defines the policy used to schedule rollover events.\n"
ROLLTYPES "\
	- derivedconf Full path to derived config files. csv for multiple files.\n\
	- ageusec     Set flag field if dt > this val in usec.\n\
		      (Optional default no value used. (although neg time \n\
		      will still flag))\n"
;

static
const char *store_function_csv_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int store_function_csv_config(ldmsd_plugin_inst_t pi,
			      struct attr_value_list *avl,
			      struct attr_value_list *kwl,
			      char *ebuf, int ebufsz)
{
	store_function_csv_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;
	int rc;
	char *value = NULL;
	char *bvalue = NULL;
	int roll = -1;
	int rollmethod = DEFAULT_ROLLTYPE;
	int tmp;

	rc = config_check(inst, kwl, avl, ebuf, ebufsz);
	if (rc != 0){
		/* config_check also populate ebuf */
		INST_LOG(inst, LDMSD_LERROR,
			 "store_function failed config_check\n");
		goto out;
	}

	rc = store->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		goto out;

	value = av_value(avl, "buffer");
	bvalue = av_value(avl, "buffertype");
	rc = config_buffer(inst, value, bvalue, &inst->buffer_sz,
			   &inst->buffer_type);
	if (rc) {
		snprintf(ebuf, ebufsz, "config_buffer() error: %d\n", rc);
		goto out;
	}

	value = av_value(avl, "path");
	if (!value){
		snprintf(ebuf, ebufsz, "store_function missing path\n");
		rc = EINVAL;
		goto out;
	}
	inst->path = strdup(value);
	if (!inst->path) {
		snprintf(ebuf, ebufsz, "Not enough memory\n");
		rc = ENOMEM;
		goto out;
	}

	value = av_value(avl, "altheader");
	if (value)
		inst->altheader = atoi(value);

	/* rollover + rolltype */
	value = av_value(avl, "rollover");
	if (value) {
		roll = atoi(value);
		if (roll < 0) {
			snprintf(ebuf, ebufsz,
				 "store_function invalid rollover\n");
			rc = EINVAL;
			goto out;
		}
	}
	value = av_value(avl, "rolltype");
	if (value) {
		if (roll < 0) { /* rolltype not valid without rollover also */
			snprintf(ebuf, ebufsz,
				 "`rollover` attribute is required\n");
			rc = EINVAL;
			goto out;
		}
		rollmethod = atoi(value);
		if (rollmethod < MINROLLTYPE || rollmethod > MAXROLLTYPE) {
			snprintf(ebuf, ebufsz,
				 "Invalid roll type (%d)\n", rollmethod);
			rc = EINVAL;
			goto out;
		}
	}
	inst->rollover = roll;
	inst->rolltype = rollmethod;

	value = av_value(avl, "ageusec");
	if (value){
		tmp = atoi(value);
		if (tmp < 0) {
			snprintf(ebuf, ebufsz,
				 "invalid `ageusec` value\n");

			rc = EINVAL;
			goto out;
		}
		inst->ageusec = tmp;
	}

	value = av_value(avl, "derivedconf");
	if (!value) {
		snprintf(ebuf, ebufsz, "missing `derivedconf` attribute\n");
		rc = EINVAL;
		goto out;
	}
	inst->derivedconf = strdup(value);
	if (!inst->derivedconf) {
		snprintf(ebuf, ebufsz, "Out of memory\n");
		rc = ENOMEM;
		goto out;
	}

	/* rollover is scheduled at open */

	rc = 0;
out:
	return rc;
}

void idx_del_cb(void *obj, void *cb_arg)
{
	store_function_csv_inst_t inst = cb_arg;
	struct setdatapoint* dp = obj;
	int i;
	struct dinfo *dv;
	for (i = 0; i < inst->numder; i++) {
		dv = &dp->datavals[i];
		if (dv->storevals)
			free(dv->storevals);
		if (dv->returnvals)
			free(dv->returnvals);
	}
	free(dp);
}

static
void store_function_csv_del(ldmsd_plugin_inst_t pi)
{
	store_function_csv_inst_t inst = (void*)pi;

	/* no need to lock .. reaching here means everything about this
	 * instance has stopped (rollver + store) */

	idx_traverse(inst->sets_idx, idx_del_cb, inst);
	idx_destroy(inst->sets_idx);

	if (inst->file)
		fclose(inst->file);

	/* CONTINUE HERE */
}

static
int store_function_csv_init(ldmsd_plugin_inst_t pi)
{
	store_function_csv_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;

	/* override store operations */
	store->open = store_function_csv_open;
	store->close = store_function_csv_close;
	store->flush = store_function_csv_flush;
	store->store = store_function_csv_store;

	inst->sets_idx = idx_create();
	if (!inst->sets_idx)
		return ENOMEM;
	return 0;
}

static
struct store_function_csv_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "store",
		.plugin_name = "store_function_csv",

                /* Common Plugin APIs */
		.desc   = store_function_csv_desc,
		.help   = store_function_csv_help,
		.init   = store_function_csv_init,
		.del    = store_function_csv_del,
		.config = store_function_csv_config,

	},

	.rollover = -1,
	.rolltype = DEFAULT_ROLLTYPE,
	.roll_ev = OVIS_EVENT_INITIALIZER,
	.parseconfig = 1, /* non-zero means parsing needed */
};

ldmsd_plugin_inst_t new()
{
	store_function_csv_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}

__attribute__((constructor))
static void __init_once()
{
	int rc;
	roll_sched = ovis_scheduler_new();
	if (!roll_sched) {
		ldmsd_log(LDMSD_LERROR,
			  "store_csv: rollover scheduler creation failed, "
			  "errno: %d\n", errno);
		return;
	}
	rc = pthread_create(&roll_thread, NULL, rolloverThreadInit, NULL);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "store_csv: rollover thread creation "
			  "failed, rc: %d\n", rc);
		return;
	}
}
