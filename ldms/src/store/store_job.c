/*
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
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
#include <stdbool.h>
#include <linux/limits.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <coll/idx.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ovis_util/spool.h"
#include "ldms_slurmjobid.h"
#include "uthash.h"


#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#define STOR "store_job"

typedef enum {CSV_CFGMAIN_PRE, CSV_CFGMAIN_IN, CSV_CFGMAIN_DONE, CSV_CFGMAIN_FAILED} csvcfg_state;

/** container for file data. */
struct job_data {
	uint64_t jid;
	pthread_mutex_t jlock;
	FILE *file;
	char *filename;
	FILE *headerfile;
	char *headerfilename;
	bool header_done;
	int ref_count;
	UT_hash_handle hh;
};

struct comp_data {
	uint64_t cid;
	time_t last_update; // when too long ago, clear job reference
	struct job_data *job;
	UT_hash_handle hh;
};


static struct column_step {
	int begin;
	int end;
	int step;
} cs = {0,0,0};


/* override for special keys */
struct storek{
	char* key;
	int altheader;
	int id_pos;
	struct column_step cs;
	char *openhook;
	char *spooler;
	char *spooldir;
};

static bool quitting = false;
static csvcfg_state cfgstate = CSV_CFGMAIN_PRE;
#define MAX_STORE_KEYS 20
static idx_t store_idx; // NOTE: this doesnt have an iterator. Hence storekeys.
static char* storekeys[MAX_STORE_KEYS]; //FIXME: make this variable size
static int nstorekeys = 0;
static struct storek specialkeys[MAX_STORE_KEYS]; //FIXME: make this variable size
static int nspecialkeys = 0;
static char *root_path;
static int altheader;
static int id_pos;
static int preen_frequency = 100000; // number of stores between clear of idle jobs
static int max_idle_seconds = 600; // number of seconds without update before a job is considered gone.
static bool ietfcsv = true; /* if true, follow ietf 4180 csv spec wrt headers */
static bool caldate = true; /* if true, append human date stamps to filename rather than utc sec. */
static char *jobid_metric = NULL;
static pthread_mutex_t term_lock;

/* help for column output orders */
#define ORDERTYPES \
"                     forward: metric columns ordered as added in sampler.\n" \
"                     reverse: columns reverse of order added in sampler.\n" \
"                     alnum: sorted per man page (not implemented)\n"

/** the full path of an executable to run to notify of new files.
	NULL indicates no notice requested.
 */
static char *openhook = NULL;
/** the full path of an executable to run to move files to spooldir.
	NULL indicates no spooling requested.
 */
static char *spooler = NULL;
/** the target directory for spooled data.
	undefined if spooler is NULL.
	May be overridden per container via config action=container.
 */
static char *spooldir = NULL;

static ldmsd_msg_log_f msglog;



#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

/*
 * store_job - csv compid, value pairs UNLESS id_pos is specified.
 * In that case, two options for which metric's comp id
 * will be used. 0 = last metric added to the sampler,
 * 1 = first metric added to the sampler (order because of the inverse mvec).
 * Interpretation has slightly changed becuase of the sequence options but
 * the values of 0/1  and which metric have not.
 */

struct job_store_handle {
	struct ldmsd_store *store;
	bool bad; // not usable
	int jobidindex;
	struct job_data *jobhash;
	struct comp_data *comphash;
	char *path;
	char *openhook;
	char *spooler;
	char *spooldir;
	int altheader;
	int id_pos;
	int ncalls;
	char *store_key;
	char *subdir;
	struct column_step cs;
	pthread_mutex_t lock;
	void *ucontext;
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

static int hooks_closed = 0;
static
void handle_spool(const char *name, const char *spooler, const char *spooldir)
{
	const char *msg;
	if (hooks_closed) {
		msglog(LDMS_LINFO, "Request after sheller closed: %s %s\n",
			name, spooler);
		return;
	}
	int rc = ovis_file_spool(spooler, name, spooldir, &msg);
	if (rc) {
		msglog(LDMS_LERROR,"Spooling error for '%s %s %s': %s: %s\n",
			spooler, name, spooldir,
			msg, strerror(rc));
		hooks_closed = 1;
	}
}

static void handle_open(const char *hook, const char *name)
{
	if (!hook || !name)
		return;
	if (hooks_closed) {
		msglog(LDMS_LINFO, "Request after sheller closed: %s %s\n",
			name, hook);
		return;
	}
	const char *argv[] = { hook, name, NULL};
	int rc = sheller_init();
	if (rc && rc != EALREADY) {
		msglog(LDMS_LERROR,"Openhook init error for '%s %s': %s\n",
			hook, name, strerror(rc));
		hooks_closed = 1;
		return;
	}
	rc = sheller_call((int)ARRAY_SIZE(argv) - 1,argv);
	if (rc) {
		msglog(LDMS_LERROR,"Openhook error for '%s %s': %s\n",
			hook, name, strerror(rc));
		hooks_closed = 1;
	}
}


/**
 * configurations for a container that can override the vals in config_main.
 */
static int config_container(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	char *altvalue;
	char *ivalue;
	char *rvalue;
	int ipos = -1;
	int idx;
	int i;

	pthread_mutex_lock(&cfg_lock);

	//have to do this after main is configured
	if (cfgstate != CSV_CFGMAIN_DONE) {
		msglog(LDMS_LERROR, "Error " STOR ": wrong state for config_container %d\n",
		       cfgstate);
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	//get overrides for a particular container.

	value = av_value(avl, "container");
	if (!value){
		msglog(LDMS_LERROR, "Error " STOR ": config missing container name\n");
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	//do we have this already. if so, then can update those values.
	idx = -1;
	for (i = 0; i < nspecialkeys; i++){
		if (strcmp(value, specialkeys[i].key) == 0){
			idx = i;
			break;
		}
	}
	if (idx < 0){
		if (nspecialkeys > (MAX_STORE_KEYS-1)){
			msglog(LDMS_LDEBUG, "Error " STOR ": Exceeded max store keys\n");
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}

		idx = nspecialkeys;
		specialkeys[idx].key = strdup(value);
	}
	//defaults to main
	specialkeys[idx].id_pos = id_pos;
	specialkeys[idx].altheader = altheader;
	specialkeys[idx].cs.begin = cs.begin;
	specialkeys[idx].cs.end = cs.end;
	specialkeys[idx].cs.step = cs.step;
	//increment nspecialkeys now. note that if the args are a problem, we will have incremented.
	if (idx == nspecialkeys)
		nspecialkeys++;

	ivalue = av_value(avl, "id_pos");
	if (ivalue){
		ipos = atoi(ivalue);
		if ((ipos < 0) || (ipos > 1)) {
			msglog(LDMS_LDEBUG, "Error " STOR ": Bad option for id_pos\n");
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}
		specialkeys[idx].id_pos = ipos;
	}

	altvalue = av_value(avl, "altheader");
	if (altvalue) {
		specialkeys[idx].altheader = atoi(altvalue);
	}

	rvalue = av_value(avl, "sequence");
	if (rvalue){
		switch (rvalue[0]) {
		case 'f':
			if (strcmp(rvalue,"forward")==0) {
				specialkeys[idx].cs.step = -1;
			}
			break;
		case 'r':
			if (strcmp(rvalue,"reverse")==0) {
				specialkeys[idx].cs.step = 1;
			}
			break;
		case 'a':
			if (strcmp(rvalue,"alnum")==0) {
				msglog(LDMS_LERROR, STOR " sequence alnum"
				       " unsupported. using default from main.\n");
			}
			break;
		default:
			msglog(LDMS_LERROR, STOR " using default from main"
			       "%s unknown\n",rvalue);
			break;
		}
	}
	int rc = 0;
	char *spoolerval =  av_value(avl, "spooler");
	char *spooldirval =  av_value(avl, "spooldir");
	if (spoolerval && spooldirval &&
		strlen(spoolerval) >= 2 && strlen(spooldirval) >= 2) {
		char *tmp1 = strdup(spoolerval);
		char *tmp2 = strdup(spooldirval);
		if (!tmp1 || !tmp2) {
			free(tmp1);
			free(tmp2);
			rc = ENOMEM;
		} else {
			specialkeys[idx].spooler = tmp1;
			specialkeys[idx].spooldir = tmp2;
		}
	} else {
		if (spooldirval || spoolerval) {
			msglog(LDMS_LERROR, STOR ": both spooler "
				"and spooldir must be specificed correctly. "
				"got instead %s and %s\n",
				(spoolerval?  spoolerval : "no spooler" ),
				(spooldirval?  spooldirval : "no spooldir" ));
			rc = EINVAL;
		}
	}
	char *openhookval =  av_value(avl, "openhook");
	if (openhookval) {
		if ( strlen(openhookval) >= 2 ) {
			char *tmp1 = strdup(openhookval);
			if (!tmp1) {
				rc = ENOMEM;
			} else {
				openhook = tmp1;
				specialkeys[idx].openhook = tmp1;
			}
		} else {
			msglog(LDMS_LERROR, STOR ": bad openhook:"
				" %s\n", openhookval);
			rc = EINVAL;
		}
	}

	pthread_mutex_unlock(&cfg_lock);
	return rc;
}

/**
 * configurations for the whole store. these will be defaults if not overridden.
 * some implementation details are for backwards compatibility
 */
static int config_main(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	char *altvalue;
	char *ivalue;
	char *rvalue;
	char *bvalue;
	char *spoolerval;
	char *spooldirval;
	int ipos = -1;

	pthread_mutex_lock(&cfg_lock);

	if ((cfgstate != CSV_CFGMAIN_PRE) &&
	    (cfgstate != CSV_CFGMAIN_DONE)){
		msglog(LDMS_LERROR, STOR ": wrong state for config_main %d\n",
		       cfgstate);
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	cfgstate = CSV_CFGMAIN_IN;

	value = av_value(avl, "path");
	if (!value) {
		cfgstate = CSV_CFGMAIN_FAILED;
		pthread_mutex_unlock(&cfg_lock);
		return EINVAL;
	}

	altvalue = av_value(avl, "altheader");

	ivalue = av_value(avl, "deadcheck");
	if (ivalue){
		preen_frequency = atoi(ivalue);
		if (preen_frequency < 0) {
			cfgstate = CSV_CFGMAIN_FAILED;
			msglog(LDMS_LERROR, STOR ": deadcheck %d < 0\n",
			       preen_frequency);
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}
	}

	ivalue = av_value(avl, "maxidle");
	if (ivalue){
		max_idle_seconds = atoi(ivalue);
		if (max_idle_seconds < 0) {
			cfgstate = CSV_CFGMAIN_FAILED;
			msglog(LDMS_LERROR, STOR ": maxidle %d < 0\n",
			       max_idle_seconds);
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}
	}

	ivalue = av_value(avl, "id_pos");
	if (ivalue){
		ipos = atoi(ivalue);
		if ((ipos < 0) || (ipos > 1)) {
			cfgstate = CSV_CFGMAIN_FAILED;
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}
	}

	bvalue = av_value(avl, "ietfcsv");
	if (bvalue){
		switch (bvalue[0]) {
		case '1':
		case 't':
		case 'T':
		case '\0':
			ietfcsv = true;
			break;
		case '0':
		case 'f':
		case 'F':
			ietfcsv = false;
			break;
		default:
			cfgstate = CSV_CFGMAIN_FAILED;
			msglog(LDMS_LERROR, STOR ": bad ietfcsv=%s\n",
				bvalue);
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}
	}
	bvalue = av_value(avl, "caldate");
	if (bvalue){
		switch (bvalue[0]) {
		case '1':
		case 't':
		case 'T':
		case '\0':
			caldate = true;
			break;
		case '0':
		case 'f':
		case 'F':
			caldate = false;
			break;
		default:
			cfgstate = CSV_CFGMAIN_FAILED;
			msglog(LDMS_LERROR, STOR ": bad caldate=%s\n",
				bvalue);
			pthread_mutex_unlock(&cfg_lock);
			return EINVAL;
		}
	}

	const char *metname = NULL;
	rvalue = av_value(avl, "jobmetric");
	if (rvalue) {
		metname = rvalue;
	}

	rvalue = av_value(avl, "sequence");
	cs.step = 1; //reverse is the default for historical reasons (mvec causes the metrics to be reversed)
	if (rvalue){
		switch (rvalue[0]) {
		case 'f':
			if (strcmp(rvalue,"forward")==0) {
				cs.step = -1;
			}
			break;
		case 'a':
			if (strcmp(rvalue,"alnum")==0) {
				msglog(LDMS_LERROR, STOR " sequence alnum"
					" unsupported. using reverse.\n");
			}
			/* fallthru */
		default:
			if (strcmp(rvalue,"reverse")!=0) {
				msglog(LDMS_LERROR, STOR " sequence=reverse"
					" assumed. %s unknown\n",rvalue);
			}
			break;
		}
	}

	if (root_path)
		free(root_path);
	if (jobid_metric)
		free(jobid_metric);

	root_path = strdup(value);
	if (metname)
		jobid_metric = strdup(metname);
	else
		jobid_metric = strdup( SLURM_JOBID_METRIC_NAME);

	id_pos = ipos;

	if (altvalue)
		altheader = atoi(altvalue);
	else
		altheader = 0;

	if (!root_path || !jobid_metric) {
		cfgstate = CSV_CFGMAIN_FAILED;
		free(root_path);
		free(jobid_metric);
		pthread_mutex_unlock(&cfg_lock);
		return ENOMEM;
	}

	int rc = 0;
	spoolerval =  av_value(avl, "spooler");
	spooldirval =  av_value(avl, "spooldir");
	if (spoolerval && spooldirval &&
		strlen(spoolerval) >= 2 && strlen(spooldirval) >= 2) {
		char *tmp1 = strdup(spoolerval);
		char *tmp2 = strdup(spooldirval);
		if (!tmp1 || !tmp2) {
			free(tmp1);
			free(tmp2);
			free(root_path);
			root_path = NULL;
			cfgstate = CSV_CFGMAIN_FAILED;
			rc = ENOMEM;
		} else {
			spooler = tmp1;
			spooldir = tmp2;
		}
	} else {
		if (spooldirval || spoolerval) {
			msglog(LDMS_LERROR, STOR ": both spooler "
				"and spooldir must be specificed correctly. "
				"got instead %s and %s\n",
				(spoolerval?  spoolerval : "no spooler" ),
				(spooldirval?  spooldirval : "no spooldir" ));
			rc = EINVAL;
		}
	}
	char *openhookval =  av_value(avl, "openhook");
	if (openhookval) {
		if ( strlen(openhookval) >= 2 ) {
			char *tmp1 = strdup(openhookval);
			if (!tmp1) {
				free(spooler);
				free(spooldir);
				free(root_path);
				spooler = spooldir = root_path = NULL;
				cfgstate = CSV_CFGMAIN_FAILED;
				rc = ENOMEM;
			} else {
				openhook = tmp1;
			}
		} else {
			msglog(LDMS_LERROR, STOR ": bad openhook:"
				" %s\n", openhookval);
			rc = EINVAL;
		}
	}

	if (cfgstate == CSV_CFGMAIN_IN) {
		cfgstate = CSV_CFGMAIN_DONE;
	}
	pthread_mutex_unlock(&cfg_lock);

	return rc;
}


struct kw kw_tbl[] = {
	{ "container", config_container},
	{ "main", config_main},
};


static int closeall();
/**
 * \brief Configuration
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	struct kw *kw;
	struct kw key;
	int bw = 0;
	int rc;

	char* haveclose = av_value(avl, "close");
	if (haveclose && strcmp(haveclose,"all")==0) {
		return closeall();
	}

	if ((cfgstate != CSV_CFGMAIN_PRE) &&
	    (cfgstate != CSV_CFGMAIN_DONE)) {
		msglog(LDMS_LERROR, "Store_csv: wrong state for config %d\n",
		       cfgstate);
		return EINVAL;
	}

	rc = 0;
	char* action = av_value(avl, "action");
	if (!action){
		/* treat it like it is main for backwards compatibility */
		action = strdup("main");
		if (!action) {
			msglog(LDMS_LERROR, STOR ": config action=%s OOM\n",
				action);
			return ENOMEM;
		}
		bw = 1;
	}

	key.token = action;
	kw = bsearch(&key, kw_tbl, ARRAY_SIZE(kw_tbl),
		     sizeof(*kw), kw_comparator);
	if (!kw) {
		msglog(LDMS_LERROR, STOR ": Invalid configuration keyword '%s'\n", action);
		if (bw){
			free(action);
		}
		return EINVAL;
	}

	rc = kw->action(kwl, avl, NULL);
	if (bw)
		free(action);

	if (rc) {
		msglog(LDMS_LERROR, STOR ": error '%s'\n", action);
		return rc;
	}

	return 0;
}


static void term(void)
{
	msglog(LDMS_LINFO, STOR ": term called\n");
}

static const char *usage(void)
{
	return  "    config name=" STOR " [action=main] path=<path>\n"
		"           [id_pos=<0/1> sequence=<order> altheader=<0/1> ietfcsv=<bool> caldate=<bool>]\n"
		"           [spooler=<prog> spooldir=<dir> openhook=<openexec>]\n"
		"           [jobmetric=<metname>] [deadcheck=<ns>] [maxidle=<isec>]\n"
		"         - Set the root path for the storage of csvs and some default parameters\n"
		"         - action    When action = main or not specified can set the following parameters:\n"
		"         - path      The path to the root of the job csv directory\n"
		"         - openexec  The path to the data/header file open agent.\n"
		"         - spooler   The path to the spool transfer agent.\n"
		"         - spooldir  The path to the spool directory for closed output files.\n"
		"         - ietfcsv   Use ietf formatting if true. Default true.\n"
		"         - caldate   Use calendar date file suffix if true, else utc seconds. Default true.\n"
		"         - ns        The number of store events between retired job checks. Default 100000.\n"
		"         - isec      The number of seconds after which a job is assumed dead. Default 600.\n"
		"         - metname   Job ID is metric metname, which must be present in sets."
	        " default " SLURM_JOBID_METRIC_NAME ".\n"
		"         - altheader Header in a separate file (optional, default 0)\n"
		"         - id_pos    Use only one comp_id either first metric added to the\n"
		"                     sampler (1) or last added (0)\n"
		"                     (Optional default use all compid)\n"
		"         - sequence  Determine the metric column ordering:\n"
		ORDERTYPES
                "    config name=" STOR " [action=container] container=<name> \n"
		"           [id_pos=<0/1> sequence=<order> altheader=<0/1>]\n"
		"         - Override the default parameters set by action=main for particular containers\n"
		"         - altheader Header in a separate file (optional, default to main)\n"
		"         - spooler/spooldir/openexec (optional, default to main)\n"
		"         - id_pos    Use only one comp_id either first metric added to the\n"
		"                     sampler (1) or last added (0)\n"
		"                     (Optional default to main)\n"
		"         - sequence  Determine the metric column ordering (default to main):\n"
		ORDERTYPES
		;
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
	struct job_store_handle *s_handle = _s_handle;
	return s_handle->ucontext;
}

static
void get_loop_limits(struct job_store_handle *s_handle,
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
		msglog(LDMS_LERROR, STOR " sequence bug in loop (%d)\n",
			cs.step);
		s_handle->cs.begin = 0;
		s_handle->cs.end = 0;
	}
}

/* Only called from Store which already has the lock */
static int print_header(struct job_store_handle *s_handle, struct job_data *jdp,
			ldms_mvec_t mvec)
{

	if (!jdp || jdp->header_done)
		return 0;
	FILE* fp = jdp->headerfile;
	if (!fp)
		fp = jdp->file;

	const char* name;
	int i;

	if (!fp){
		msglog(LDMS_LDEBUG, STOR ": Cannot print header;no files\n");
		return EINVAL;
	}

	char *wsopt = " "; /* non-ietf optional whitespace */
	char *wsqt = ""; /* ietf quotation wrapping strings */
	if (ietfcsv) {
		wsopt = "";
		wsqt = "\"";
	}
	/* This allows optional loading a float (Time) into an int field and
	   retaining usec as a separate field */
	fprintf(fp, "#%sTime%s,%s%sTime_usec%s",wsqt,wsqt,wsqt,wsopt,wsqt);

	int num_metrics = ldms_mvec_get_count(mvec);
	get_loop_limits(s_handle, num_metrics);

	if (s_handle->id_pos < 0) {
		for (i = s_handle->cs.begin; i != s_handle->cs.end;
			i += s_handle->cs.step) {
			name = ldms_get_metric_name(mvec->v[i]);
			fprintf(fp, ",%s%s%s.CompId%s,%s%s%s.value%s",
				wsqt,wsopt,name,wsqt, wsqt,wsopt,name,wsqt);
		}
	} else {
		fprintf(fp, ",%s%sCompId%s",wsqt,wsopt,wsqt);
		for (i = s_handle->cs.begin; i != s_handle->cs.end; i += s_handle->cs.step) {
			name = ldms_get_metric_name(mvec->v[i]);
			fprintf(fp, ",%s%s%s%s",wsqt,wsopt, name,wsqt);
		}
	}
	fprintf(fp, "\n");

	/* Flush for the header, whether or not it is the data file as well */
	fflush(fp);

	if (jdp->headerfile) {
		fclose(jdp->headerfile);
		jdp->headerfile = NULL;
	}
	jdp->header_done = true;

	return 0;
}

void freeJD(struct job_store_handle *s_handle, struct job_data *jdp) {
	pthread_mutex_destroy(&jdp->jlock);
	if (jdp->file) {
		// msglog(LDMS_LERROR, STOR ": freeJD: %s %s %s.\n", jdp->filename, s_handle->spooler, s_handle->spooldir);
		fclose(jdp->file);
		jdp->file = NULL;
		handle_spool(jdp->filename, s_handle->spooler,
			s_handle->spooldir);
	}
	if (jdp->headerfile) {
		fclose(jdp->headerfile);
		jdp->headerfile = NULL;
		handle_spool(jdp->headerfilename, s_handle->spooler,
			 s_handle->spooldir);
	} else {
		if (jdp->headerfilename) {
			handle_spool(jdp->headerfilename, s_handle->spooler,
				 s_handle->spooldir);
		}
	}
	free(jdp->headerfilename);
	jdp->headerfilename = NULL;
	free(jdp->filename);
	jdp->filename = NULL;
	free(jdp);
}

void deleteJD( struct job_store_handle *s_handle, struct job_data *jd)
{
	if (!jd)
	       return;
	// msglog(LDMS_LDEBUG, STOR ": DELrefjd %s %" PRIu64 " , %d.\n", s_handle->store_key, jd->jid, jd->ref_count);
	if (jd->ref_count > 0) {
		jd->ref_count--;
	}
	if (!jd->ref_count) {
		jd->ref_count--;
		HASH_DEL(s_handle->jobhash, jd);
		freeJD(s_handle, jd);
		return;
	}
	if (jd->ref_count < 0)
		msglog(LDMS_LERROR, STOR ": over-called deleteJD.\n");
}

void freeCD(struct job_store_handle *s_handle, struct comp_data *cdp)
{
	if (!cdp)
		return;
	if (s_handle && cdp->job) {
		deleteJD(s_handle, cdp->job);
	}
	cdp->job = NULL;
	cdp->cid = 0;
	free(cdp);
}

static void destroy_store(ldmsd_store_handle_t _s_handle)
{
	int i;

	struct job_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		return;
	}

	pthread_mutex_lock(&s_handle->lock);
	msglog(LDMS_LDEBUG,"Destroying " STOR " <%s>\n", s_handle->store_key);
	struct comp_data *cdp = NULL;
	struct comp_data *ctmp = NULL;
	struct job_data *jdp = NULL;
	struct job_data *tmp = NULL;
	HASH_ITER(hh, s_handle->comphash, cdp, ctmp) {
		HASH_DEL(s_handle->comphash, cdp);
		freeCD(s_handle, cdp);
	}
	HASH_ITER(hh, s_handle->jobhash, jdp, tmp) {
		HASH_DEL(s_handle->jobhash, jdp);
		freeJD(s_handle, jdp);
	}
	s_handle->store = NULL;
	if (s_handle->path)
		free(s_handle->path);
	s_handle->path = NULL;
	if (s_handle->ucontext)
		free(s_handle->ucontext);
	s_handle->ucontext = NULL;
	s_handle->spooler = NULL;
	s_handle->spooldir = NULL;
	s_handle->openhook = NULL;

	idx_delete(store_idx, s_handle->store_key, strlen(s_handle->store_key));

	// FIXME: this destroys special keys for all handles, not just input.
	for (i = 0; i < nspecialkeys; i++){
		// if (specialkeys[i].key =
		free(specialkeys[i].key);
		specialkeys[i].key = NULL;
	}

	for (i = 0; i < nstorekeys; i++){
		if (storekeys[i] == s_handle->store_key) {
			storekeys[i] = NULL;
			break;
		}
	}
	if (s_handle->store_key)
		free(s_handle->store_key);
	if (s_handle->subdir)
		free(s_handle->subdir);
	pthread_mutex_unlock(&s_handle->lock);
	pthread_mutex_destroy(&s_handle->lock);
	free(s_handle);
}

static ldmsd_store_handle_t
new_store(struct ldmsd_store *s, const char *comp_type, const char* container,
		struct ldmsd_store_metric_index_list *list, void *ucontext)
{
	struct job_store_handle *s_handle;
	int idx;
	int i;

	if (!container || strchr(container,'/') != NULL) {
		msglog(LDMS_LERROR,"Invalid container name '%s'\n", container);
		return NULL;
	}

	pthread_mutex_lock(&cfg_lock);
	s_handle = idx_find(store_idx, (void *)container, strlen(container));
	if (!s_handle) {
		s_handle = calloc(1, sizeof *s_handle);
		if (!s_handle)
			goto out;
		s_handle->ucontext = ucontext;
		s_handle->store = s;

		pthread_mutex_init(&s_handle->lock, NULL);

		s_handle->store_key = strdup(container);
		if (!s_handle->store_key) {
			msglog(LDMS_LERROR,"Container dup failed '%s'\n", container);
			goto err1;
		}

		s_handle->subdir = strdup(comp_type);
		if (!s_handle->subdir) {
			msglog(LDMS_LERROR,"comp_type dup failed '%s'\n", comp_type);
			goto err1;
		}

		idx = -1;
		for (i = 0; i < nspecialkeys; i++) {
			if (strcmp(s_handle->store_key, specialkeys[i].key) == 0){
				idx = i;
				break;
			}
		}
		if (idx >= 0) {
			s_handle->altheader = specialkeys[idx].altheader;
			s_handle->id_pos = specialkeys[idx].id_pos;
			s_handle->cs.begin = specialkeys[idx].cs.begin;
			s_handle->cs.end = specialkeys[idx].cs.end;
			s_handle->cs.step = specialkeys[idx].cs.step;
			if (specialkeys[idx].openhook) {
				s_handle->openhook = specialkeys[idx].openhook;
			} else {
				s_handle->openhook = openhook;
			}
			if (specialkeys[idx].spooler) {
				s_handle->spooler = specialkeys[idx].spooler;
				s_handle->spooldir = specialkeys[idx].spooldir;
			} else {
				s_handle->spooler = spooler;
				s_handle->spooldir = spooldir;
			}
		} else {
			s_handle->altheader = altheader;
			s_handle->id_pos = id_pos;
			s_handle->cs.begin = cs.begin;
			s_handle->cs.end = cs.end;
			s_handle->cs.step = cs.step;
			s_handle->openhook = openhook;
			s_handle->spooler = spooler;
			s_handle->spooldir = spooldir;
		}
		if (nstorekeys == (MAX_STORE_KEYS-1)){
			msglog(LDMS_LDEBUG, "Error: Exceeded max store keys\n");
			goto err1;
		} else {
			idx_add(store_idx, (void *)container, strlen(container), s_handle);
			storekeys[nstorekeys++] = s_handle->store_key;
		}
	}

	goto out;
err1:
	destroy_store(s_handle);
	s_handle = NULL;
out:
	pthread_mutex_unlock(&cfg_lock);
	if (!s_handle)
		msglog(LDMS_LDEBUG, STOR ": null return from new_store.\n");
	return s_handle;
}

/*
 * root/comp_type/job.%J/container."%F-%H:%M:%S%z"
 * root/comp_type/job.%J/container.HEADER."%F-%H:%M:%S%z" optional
 * \return 0 if ok.
 */
static int open_job_files(struct job_store_handle *s_handle, struct job_data *jdp, const char *container)
{
	dstring_t ds;
	dstring_t dsh;
	struct tm *tmp;
	time_t t;
	const char fmt[] = "%F-%H_%M_%S_%z";
	const char fmtutc[] = "%s";
	char tmp_path[PATH_MAX];
	snprintf(tmp_path, sizeof(tmp_path), "/job.%" PRIu64, jdp->jid);

	dstr_init(&ds);
	dstr_init(&dsh);
	dstrcat(&ds, root_path, DSTRING_ALL);
	dstrcat(&ds, "/", 1);
	dstrcat(&ds, s_handle->subdir, DSTRING_ALL);

	int rc = mkdir(dstrval(&ds), 0755);
	if ((rc != 0) && (errno != EEXIST)){
		msglog(LDMS_LDEBUG,"Error: cannot create dir '%s'\n", dstrval(&ds));
		goto err1;
	}
	dstrcat(&ds, tmp_path, DSTRING_ALL);

	rc = mkdir(dstrval(&ds), 0755);
	if ((rc != 0) && (errno != EEXIST)){
		msglog(LDMS_LDEBUG,"Error: cannot create dir '%s'\n", dstrval(&ds));
		goto err1;
	}

	dstrcat(&ds, "/", 1);
	dstrcat(&ds, container, DSTRING_ALL);
	dstrcat(&ds,".",1);
	t = time(NULL);
	tmp = localtime(&t);
	if (caldate)
		strftime(tmp_path,sizeof(tmp_path), fmt, tmp);
	else
		strftime(tmp_path,sizeof(tmp_path), fmtutc, tmp);
	if (s_handle->altheader) {
		dstrcat(&dsh, dstrval(&ds), DSTRING_ALL);
		dstrcat(&dsh, "HEADER.", 7);
		dstrcat(&dsh, tmp_path, DSTRING_ALL);
		jdp->headerfilename = dstr_extract(&dsh);
		if (!jdp->headerfilename) {
			msglog(LDMS_LERROR, STOR ": oom allocating headerfilename\n");
			goto err1;
		}
	}
	dstrcat(&ds, tmp_path, DSTRING_ALL);
	jdp->filename = dstr_extract(&ds);
	if (!jdp->filename) {
		msglog(LDMS_LERROR, STOR ": oom allocating filename\n");
		goto err1;
	}

	jdp->file = fopen(jdp->filename, "a");
	if (!jdp->file) {
		msglog(LDMS_LERROR, STOR ": cannot open output %s. err %d\n",
				jdp->filename, errno);
		goto err1;
	}

	if (jdp->headerfilename) {
		jdp->headerfile = fopen(jdp->headerfilename, "w");
		if (!jdp->headerfile) {
			msglog(LDMS_LERROR, STOR ": cannot open output %s. err %d\n",
					jdp->headerfilename, errno);
			goto err1;
		}
		handle_open(s_handle->openhook, jdp->headerfilename);
	}
	handle_open(s_handle->openhook, jdp->filename);

	pthread_mutex_unlock(&s_handle->lock);
	goto out;

err1:
	dstr_free(&ds);
	dstr_free(&dsh);
	return -1;
out:
	return 0;

}

struct comp_data *newCD(uint64_t comp_id)
{
	struct comp_data *cdp;
	// msglog(LDMS_LDEBUG, STOR ": newCD %" PRIu64 ".\n", comp_id);
	cdp = calloc(1, sizeof(*cdp));
	if (!cdp) {
		msglog(LDMS_LERROR, STOR ": out of memory in newCD.\n");
	}
	cdp->cid = comp_id;
	return cdp;

}

void addRefJD(struct job_data *jdp, const char *who)
{
	if (jdp && jdp->ref_count >= 0) {
		// msglog(LDMS_LDEBUG, STOR ": %s addrefJD %" PRIu64 ".\n", who, jdp->jid);
		jdp->ref_count++;
	}
}

struct job_data *newJD(struct job_store_handle *s_handle, uint64_t jobid, ldms_mvec_t mvec)
{
	struct job_data *jdp;
	// msglog(LDMS_LDEBUG, STOR ": %s newJD %" PRIu64 ".\n", s_handle->store_key, jobid);
	jdp = calloc(1, sizeof(*jdp));
	if (!jdp) {
		msglog(LDMS_LERROR, STOR ": out of memory in newJD.\n");
		return NULL;
	}
	pthread_mutex_init(&jdp->jlock, NULL);
	jdp->jid = jobid;
	jdp->ref_count = 0;
	int ferr = open_job_files(s_handle, jdp, s_handle->store_key);
	if (ferr) {
		freeJD(s_handle, jdp);
		return NULL;
	}
	HASH_ADD_U64(s_handle->jobhash, jid, jdp);
	return jdp;
}

/* we make a special case out of jobid 0, ignoring it. */
static struct job_data *get_job_files(struct job_store_handle *s_handle, ldms_mvec_t mvec)
{
	if (!s_handle) {
		return NULL;
	}

	int num_metrics = ldms_mvec_get_count(mvec);
	if (num_metrics < 1)
		return NULL;
	int i = (s_handle->id_pos == 0) ? 0: (num_metrics-1);
	uint64_t comp_id = ldms_get_user_data(mvec->v[i]);

	if (!comp_id)
		return NULL;

	struct comp_data *cdp;
	HASH_FIND_U64(s_handle->comphash, &comp_id, cdp);
	if (!cdp) {
		cdp = newCD(comp_id);
		if (!cdp) {
			return NULL;
		}
		HASH_ADD_U64(s_handle->comphash, cid, cdp);
	}
	
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	cdp->last_update = ts.tv_sec;

	uint64_t jobid = ldms_get_u64(mvec->v[s_handle->jobidindex]);
	struct job_data *jdp = NULL;
	HASH_FIND_U64(s_handle->jobhash, &jobid, jdp);
	if (!jdp && jobid ) {
		jdp = newJD(s_handle, jobid, mvec);
	}
	if (!jdp && jobid) {
		return NULL;
	}
	if (jobid) {
		print_header(s_handle, jdp, mvec);
	}
	if (cdp->job == NULL || cdp->job->jid != jobid) {
		deleteJD(s_handle, cdp->job);
		cdp->job = jdp;
		addRefJD(jdp, s_handle->store_key);
	}
	return jdp;
}

static int set_jobidindex(struct job_store_handle *s_handle, ldms_mvec_t mvec)
{
	const int num_metrics = ldms_mvec_get_count(mvec);
	int i;
	get_loop_limits(s_handle, num_metrics);
	for (i = s_handle->cs.begin; i != s_handle->cs.end; i += s_handle->cs.step) {
		const char *name = ldms_get_metric_name(mvec->v[i]);
		if (strcmp(name, jobid_metric)==0) {
			s_handle->jobidindex = i;
			return 0;
		}
	}
	s_handle->bad = true;
	return 1;
}
			
/* runs in handle lock under store().
 * any node not reporting in after some time has it's job reference
 * dropped. The job reference may get picked up or recreated later.
 */
static void clear_idle_jobs(struct job_store_handle *s_handle)
{
	struct comp_data *cdp = NULL;
	struct comp_data *ctmp = NULL;
	struct timespec ts;
	// msglog(LDMS_LERROR, STOR ": clear_idle_jobs called on %s\n", s_handle->store_key);
	clock_gettime(CLOCK_MONOTONIC, &ts);
	HASH_ITER(hh, s_handle->comphash, cdp, ctmp) {
		if (cdp->last_update + max_idle_seconds < ts.tv_sec) {
		//	msglog(LDMS_LERROR, STOR ": clear_idle_jobs deleteJD %s %" PRIu64 "\n", s_handle->store_key, cdp->cid);
			deleteJD(s_handle, cdp->job);
			cdp->job = NULL;
		} else {
		//	msglog(LDMS_LERROR, STOR ": clear_idle_jobs keep %s %" PRIu64 "\n", s_handle->store_key, cdp->cid);
		}
	}
	return;
}
		
static int store(ldmsd_store_handle_t _s_handle, ldms_set_t set, ldms_mvec_t mvec)
{
	const struct ldms_timestamp *ts = ldms_get_timestamp(set);
	uint64_t comp_id;
	struct job_store_handle *s_handle;
	s_handle = _s_handle;
	if (!s_handle || s_handle->bad)
		return EINVAL;

	pthread_mutex_lock(&s_handle->lock);
	if (!s_handle->jobidindex) {
		set_jobidindex(s_handle, mvec);
	}
	if (s_handle->bad) {
		msglog(LDMS_LERROR, STOR ": set without %s metric seen in %s\n",
			jobid_metric, s_handle->store_key);
		pthread_mutex_unlock(&s_handle->lock);
		return EINVAL;
	}
	struct job_data *jdp = get_job_files(s_handle, mvec);
	if (!jdp) {
		pthread_mutex_unlock(&s_handle->lock);
		return 0;
	}
	if (!jdp->file){
		comp_id = ldms_get_user_data(mvec->v[0]);
		msglog(LDMS_LDEBUG,"Cannot insert values for <%s> on node %"
				PRIu64 "\n", s_handle->store_key, comp_id);
		pthread_mutex_unlock(&s_handle->lock);
		return EPERM;
	}

	s_handle->ncalls++;
	fprintf(jdp->file, "%"PRIu32".%06"PRIu32 ", %"PRIu32,
		ts->sec, ts->usec, ts->usec);

	/* mvec comes to the store as the inverse of how they added to the sampler which is also the inverse of ldms_ls -l display */
	int num_metrics = ldms_mvec_get_count(mvec);
	get_loop_limits(s_handle, num_metrics);
	if (s_handle->id_pos < 0){
		int i, rc;
		for (i = s_handle->cs.begin; i != s_handle->cs.end; i += s_handle->cs.step) {
			comp_id = ldms_get_user_data(mvec->v[i]);
			rc = fprintf(jdp->file, ", %" PRIu64 ", %" PRIu64,
				     comp_id, ldms_get_u64(mvec->v[i]));
			if (rc < 0)
				msglog(LDMS_LDEBUG, STOR ": Error %d writing to '%s'\n",
				       rc, jdp->filename);
		}
		fprintf(jdp->file,"\n");
	} else {
		int i, rc;

		if (num_metrics > 0){
			i = (s_handle->id_pos == 0)? 0: (num_metrics-1);
			rc = fprintf(jdp->file, ", %" PRIu64,
				ldms_get_user_data(mvec->v[i]));
			if (rc < 0)
				msglog(LDMS_LDEBUG, STOR ": Error %d writing to '%s'\n",
				       rc, jdp->filename);
		}
		for (i = s_handle->cs.begin; i != s_handle->cs.end; i += s_handle->cs.step) {
			rc = fprintf(jdp->file, ", %" PRIu64, ldms_get_u64(mvec->v[i]));
			if (rc < 0)
				msglog(LDMS_LDEBUG, STOR ": Error %d writing to '%s'\n",
				       rc, jdp->filename);
		}
		fprintf(jdp->file,"\n");
	}

	if (s_handle->ncalls > preen_frequency && preen_frequency) {
		clear_idle_jobs(s_handle);
		s_handle->ncalls = 0;
	}

	pthread_mutex_unlock(&s_handle->lock); // without lock somehow??


	return 0;
}

static int flush_store(ldmsd_store_handle_t _s_handle)
{
	struct job_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		msglog(LDMS_LDEBUG, STOR ": flush error.\n");
		return -1;
	}
	pthread_mutex_lock(&s_handle->lock);
	struct job_data *jdp = NULL;
	struct job_data *tmp = NULL;
	HASH_ITER(hh, s_handle->jobhash, jdp, tmp) {
		fflush(jdp->file);
	}
	pthread_mutex_unlock(&s_handle->lock);
	return 0;
}

static void close_store(ldmsd_store_handle_t _s_handle)
{
	msglog(LDMS_LINFO, "store_timeseries_var: close_store called\n");
	pthread_t tid = pthread_self();
	msglog(LDMS_LINFO, "store_timeseries_var: close_store ptid %lu\n",tid);
	
	pthread_mutex_lock(&cfg_lock);
	destroy_store(_s_handle);
	pthread_mutex_unlock(&cfg_lock);
}

static struct ldmsd_store store_job = {
	.base = {
			.name = "job",
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
	return &store_job.base;
}

static int closeall()
{
	pthread_mutex_lock(&term_lock);
	quitting = true;
	msglog(LDMS_LDEBUG, STOR ": close=all\n");
	term();

	pthread_mutex_lock(&cfg_lock);
	struct job_store_handle *si;
	int nk = nstorekeys;
	int i;
	for (i = 0; i < nk; i++) {
		if (storekeys[i] != NULL) {
			// msglog(LDMS_LDEBUG, "storekey %s\n",storekeys[i]);
			si = idx_find(store_idx, (void *)storekeys[i], strlen(storekeys[i]));
			if (!si) {
				msglog(LDMS_LERROR, "Cannot find store instance %s\n",
				storekeys[i]);
			} else {
				destroy_store(si);
				storekeys[i] = NULL;
			}
		}
	}

#define FREE(x) if (x) { free(x); x = NULL; }
	FREE(root_path);
	FREE(spooler);
	FREE(spooldir);
	FREE(openhook);
	FREE(jobid_metric);

	pthread_mutex_unlock(&cfg_lock);
	pthread_mutex_unlock(&term_lock);
	return 1;

}

static void __attribute__ ((constructor)) store_job_init();
static void store_job_init()
{
	store_idx = idx_create();
	pthread_mutex_init(&term_lock, NULL);
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_job_fini(void);
static void store_job_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	pthread_mutex_destroy(&term_lock);
	if (!quitting)
		closeall();
	idx_destroy(store_idx);
}
