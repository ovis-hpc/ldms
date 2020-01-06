/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
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

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#include "gpcd_pub.h"
#include "gpcd_lib.h"
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#ifndef __linux__ // modern linux provides either bitness in asm/unistd.h as needed
#if defined(__i386__)
#include "/usr/include/asm/unistd.h"
#endif

#if defined(__x86_64__)
#include "/usr/include/asm-x86_64/unistd.h"
#endif
#else // __linux__
#include <asm/unistd.h>
#endif // __linux__


#define MSR_MAXLEN 20LL
#define MMRCONF_CONFIGLINE_MAX 1024

typedef enum{CFG_PRE, CFG_DONE_INIT, CFG_IN_FINAL, CFG_FAILED_FINAL, CFG_DONE_FINAL} ctrcfg_state;

static pthread_mutex_t cfglock;

struct active_counter{
	char* name;
	int isHex;
	uint64_t value;
	int mid;
};

struct kw {
	char *token;
	int (*action)(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg);
};

static int kw_comparator(const void *a, const void *b)
{
	struct kw *_a = (struct kw *)a;
	struct kw *_b = (struct kw *)b;
	return strcmp(_a->token, _b->token);
};

// TODO: there is a lot of this that could be freed early since there isnt a dynamic counter list.
struct mstruct{
	char* file;
	int num_counter;
	struct active_counter* counter_list;
	gpcd_context_t* ctx;
};



static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static ldms_schema_t schema;
static uint64_t comp_id;
#define SAMP "aries_mmr_configurable"
static char *default_schema_name = SAMP;
static char *rtrid;
static base_data_t base;

static ctrcfg_state cfgstate = CFG_PRE;
static gpcd_mmr_list_t *listp = NULL;
static struct mstruct setvals;
static struct mstruct readvals;


static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=" SAMP " action=initialize [setfile=<cfile> rtrid=<rtrid>] readfile=<rfile> " BASE_CONFIG_USAGE
		"            - Initialization activities for the set. Does not create it. Sampler specific arguments:\n"
		"            setfile         - Optional configuration file with the counter value assignment options\n"
		"            readfile        - Configuration file with the counters to read\n"
		"            rtrid           - Optional unique rtr string identifier. Defaults to 0 length string\n"
		"\n"
		"    config name=aries_mmr_configurable action=finalize\n"
		"            - Creates the set (includes all the counter setting).\n"
		"\n"
		"    config name=aries_mmr_configurable action=ls\n"
		"            - List the configuration for set and the variables to read.\n"
		"\n"
		"    config name=aries_mmr_configurable action=reset\n"
		"             - Resets the countervals as described by the original setfile\n"
		"\n"
		;
}

//took the lock going into this
static int parseConfig(char* fname, int i){

	char name[MMRCONF_CONFIGLINE_MAX];
	char lbuf[MMRCONF_CONFIGLINE_MAX];
	char rest[MSR_MAXLEN];
	char* s;
	uint64_t temp;
	char typeid;
	int rc;
	int count;

	FILE *fp = fopen(fname, "r");
	if (!fp){
		msglog(LDMSD_LERROR, SAMP " Cannot open config file <%s>\n",
		       fname);
		return EINVAL;
	}

	count = 0;
	//parse once to count
	do  {
		s = fgets(lbuf, sizeof(lbuf), fp);
		if (!s)
			break;
		if ((strlen(lbuf) > 0)  && (lbuf[0] == '#')){
			msglog(LDMSD_LWARNING, SAMP " Comment in config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}
		if (i == 0){ // setfile
			rc = sscanf(lbuf, "%[^,],%c,%s\n",
				    name, &typeid, &rest);

			msglog(LDMSD_LDEBUG, SAMP
			       " Setfile variable <%s>\n", name);

			if (rc != 3){
				msglog(LDMSD_LWARNING, SAMP
				       " Bad format in config file <%s> <rc=%d>. Skipping\n",
				       lbuf, rc);
				continue;
			}
		} else { // readfile
			rc = sscanf(lbuf, "%[^,],%c",
				    name, &typeid);

			msglog(LDMSD_LDEBUG, SAMP
			       " Readfile variable <%s>\n", name);

			if (rc != 2){
				msglog(LDMSD_LWARNING, SAMP
				       " Bad format in config file <%s> <rc=%d>. Skipping\n",
				       lbuf, rc);
				continue;
			}
		}
		count++;
	} while (s);


	if (i == 0) {
		setvals.file = strdup(fname);
		setvals.num_counter = count;
		setvals.counter_list = NULL;
		// if there is a file, warn if there are no counters (might have commented them out for testing)
		if (count == 0){
			msglog(LDMSD_LWARNING, SAMP
			       " No valid lines for counters in config file <%s>. This is ok.\n",
			       fname);
			fclose(fp);
			return 0;
		} else {
			setvals.counter_list =
				(struct active_counter*)malloc(setvals.num_counter*sizeof(struct active_counter));
			if (!setvals.counter_list){
				fclose(fp);
				return ENOMEM;
			}
		}
	} else {
		readvals.file = strdup(fname);
		readvals.num_counter = count;
		readvals.counter_list = NULL;
		// if there is a file, warn if there are no counters
		if (count == 0){
			msglog(LDMSD_LWARNING, SAMP
			       " No valid lines for counters in config file <%s>. This is ok.\n",
			       fname);
			fclose(fp);
			return 0;
		} else {
			readvals.counter_list =
				(struct active_counter*)malloc(readvals.num_counter*sizeof(struct active_counter));
			if (!readvals.counter_list){
				fclose(fp);
				return ENOMEM;
			}
		}
	}


	//parse again to fill
	fseek(fp, 0, SEEK_SET);
	count = 0;
	do  {
		s = fgets(lbuf, sizeof(lbuf), fp);
		if (!s)
			break;

	if ((strlen(lbuf) > 0)  && (lbuf[0] == '#')){
			msglog(LDMSD_LWARNING, SAMP " Comment in config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}
		if (i == 0){ // setfile
			if (count == setvals.num_counter){
				msglog(LDMSD_LERROR, SAMP
				       " Changed number of valid entries from first pass. aborting.\n");
				fclose(fp);
				return EINVAL;
			}
			rc = sscanf(lbuf, "%[^,],%c,%s\n",
				    name, &typeid, &rest);
			if (rc != 3){
				msglog(LDMSD_LWARNING, SAMP
				       " Bad format in config file <%s> <rc=%d>. Skipping\n",
				       fname, rc);
				continue;
			}
			setvals.counter_list[count].name = strdup(name);
			if (typeid == 'H'){
				setvals.counter_list[count].isHex = 1;
				setvals.counter_list[count].value = strtoll(rest,NULL,0);
			} else {
				setvals.counter_list[count].isHex = 0;
				setvals.counter_list[count].value = strtoll(rest,NULL,10);
			}
			setvals.counter_list[count].mid = -1;
			if (setvals.counter_list[count].isHex){
				msglog(LDMSD_LDEBUG, SAMP
				       " Setfile read variable <%s> <0x%lx>\n",
				       setvals.counter_list[count].name,
				       setvals.counter_list[count].value);
			} else {
				msglog(LDMSD_LDEBUG, SAMP
				       " Setfile read variable <%s> <%" PRIu64 ">\n",

				       setvals.counter_list[count].name,
				       setvals.counter_list[count].value);
			}
		} else { // readfile
			if (count == readvals.num_counter){
				msglog(LDMSD_LERROR, SAMP
				       " Changed number of valid entries from first pass. aborting.\n");
				fclose(fp);
				return EINVAL;
			}
			rc = sscanf(lbuf, "%[^,],%c",
				    name, &typeid);
			if (rc != 2){
				msglog(LDMSD_LWARNING, SAMP
				       " Bad format in config file <%s> <rc=%d>. Skipping\n",
				       fname, rc);
				continue;
			}
			readvals.counter_list[count].name = strdup(name);
			readvals.counter_list[count].value = 0;
			if (typeid == 'H'){
				readvals.counter_list[count].isHex = 1;
			} else {
				readvals.counter_list[count].isHex = 0;
			}
			readvals.counter_list[count].mid = -1;
			msglog(LDMSD_LDEBUG, SAMP
			       " Readfile read variable <%s> <H=%d>\n",
			       readvals.counter_list[count].name,
			       readvals.counter_list[count].isHex);
		}
		count++;
	} while (s);

	fclose(fp);
	return 0;
}


// the calling function needs to get the cfg lock
static void _free_cfg(){

	int i;

	if (rtrid) free(rtrid);
	rtrid = NULL;

	if (setvals.file) free(setvals.file);
	setvals.file = NULL;

	if (readvals.file) free(readvals.file);
	readvals.file = NULL;

	if (setvals.ctx)
		gpcd_remove_context(setvals.ctx);
	setvals.ctx = NULL;
	if (setvals.counter_list) {
		for (i = 0; i < setvals.num_counter; i++){
			if (setvals.counter_list[i].name) free(setvals.counter_list[i].name);
			setvals.counter_list[i].name = NULL;
			setvals.counter_list[i].value = 0;
			setvals.counter_list[i].isHex = 0;
			setvals.counter_list[i].mid = -1;
		}
	}
	free(setvals.counter_list);
	setvals.counter_list = NULL;
	setvals.num_counter = 0;

	if (readvals.ctx)
		gpcd_remove_context(readvals.ctx);
	readvals.ctx = NULL;
	if (readvals.counter_list){
		for (i = 0; i < readvals.num_counter; i++){
			if (readvals.counter_list[i].name) free(readvals.counter_list[i].name);
			readvals.counter_list[i].name = NULL;
			readvals.counter_list[i].value = 0;
			readvals.counter_list[i].isHex = 0;
			readvals.counter_list[i].mid = -1;
		}
	}
	free(readvals.counter_list);
	readvals.counter_list = NULL;
	readvals.num_counter = 0;


}


static int resetCounters(struct attr_value_list *kwl, struct attr_value_list *avl,
		void *arg)
{
	int i;
	int mid;
	int rc;


	if (setvals.num_counter <= 0) return 0;

	if ((cfgstate != CFG_DONE_FINAL) && (cfgstate != CFG_IN_FINAL)){
		msglog(LDMSD_LERROR,
		       SAMP ": in wrong state for sampling <%d>\n", cfgstate);
		return -1;
	}

	//Keeping a context to be able to read them before and after
	//if debug, also write out the counters before and after
	if (ldmsd_loglevel_get() == LDMSD_LDEBUG){
		rc = gpcd_context_read_mmr_vals(setvals.ctx);
		if (rc){
			msglog(LDMSD_LERROR, SAMP ": Cannot read set mmr vals\n");
			return EINVAL;
		}

		listp = setvals.ctx->list;
		if (!listp){
			msglog(LDMSD_LERROR, SAMP ": Context list is null\n");
			rc = EINVAL;
			goto out;
		}

		mid = setvals.num_counter - 1;
		while (listp != NULL){
			if (mid < 0){
				msglog(LDMSD_LERROR, SAMP ": bad index value\n");
				rc = EINVAL;
				goto out;
			}
			//context is read off in the reverse order
			if (setvals.counter_list[mid].isHex){
				msglog(LDMSD_LERROR, SAMP ": Before reset %s is 0x%lx\n",
				       setvals.counter_list[mid].name, listp->value);
			} else {
				msglog(LDMSD_LERROR, SAMP ": Before reset %s is %" PRIu64 "\n",
				       setvals.counter_list[mid].name, listp->value);
			}
			listp=listp->next;
			mid--;
		}
	}


	// this is the actual reset
	for (i = 0; i < setvals.num_counter; i++){
		char* met = setvals.counter_list[i].name;
		gpcd_mmr_desc_t* desc = (gpcd_mmr_desc_t *)
			gpcd_lookup_mmr_byname(met);
		if (!desc) {
			msglog(LDMSD_LERROR, SAMP ": Could not lookup (1) <%s>\n", met);
			//TODO: what to do to clean up? This should not happen.
			return -1;
		}

//		msglog(LDMSD_LDEBUG, SAMP ": mmrd->addr %" PRIu64 "\n",
//		       (uint64_t*)&desc->addr;

		//if the perms aren't toggled, you will not be able to write.
		rc = gpcd_write_mmr_val(desc, &(setvals.counter_list[i].value),
					0); //addr is 0, dont need nicaddr
		if (rc){
			//will try one toggle, after which will give up writing for this metric
			//note that there is an unavoidable race condition if it is retoggled out of band
			//between the toggle and the write
			rc = gpcd_disable_perms();
			if (rc){
				msglog(LDMSD_LERROR, SAMP ": cannot gpcd_disable_perms. Must be root.\n");
				msglog(LDMSD_LERROR, SAMP ": cannot write mmr val because of gpcd permissions.\n");
				return rc;
			}
			rc = gpcd_write_mmr_val(desc, &(setvals.counter_list[i].value),
						0); //addr is 0, dont need nicaddr
			if (rc) {
				msglog(LDMSD_LERROR, SAMP ": cannot write_mmr_val '%s'", met);
				//TODO: Decide if should continue or return with error.
				return rc;
			}
		}
	}


	//if debug, also write out the counters before and after
	if (ldmsd_loglevel_get() == LDMSD_LDEBUG){
		rc = gpcd_context_read_mmr_vals(setvals.ctx);
		if (rc){
			msglog(LDMSD_LERROR, SAMP ": Cannot read raw set mmr vals\n");
			return EINVAL;
		}

		listp = setvals.ctx->list;
		if (!listp){
			msglog(LDMSD_LERROR, SAMP ": Context list is null\n");
			rc = EINVAL;
			goto out;
		}

		mid = setvals.num_counter - 1;
		while (listp != NULL){
			if (mid < 0){
				msglog(LDMSD_LERROR, SAMP ": bad index value\n");
				rc = EINVAL;
				goto out;
			}
			//context is read off in the reverse order
			if (setvals.counter_list[mid].isHex){
				msglog(LDMSD_LERROR, SAMP ": After reset %s is 0x%lx\n",
				       setvals.counter_list[mid].name, listp->value);
			} else {
				msglog(LDMSD_LERROR, SAMP ": After reset %s is %" PRIu64 "\n",
				       setvals.counter_list[mid].name, listp->value);
			}
			listp=listp->next;
			mid--;
		}
	}


	return 0;

out:
	//will only get here in the debug
	msglog(LDMSD_LDEBUG, SAMP ": Problems debugging the state of the set counters\n");
	return EINVAL;

}




static int list(struct attr_value_list *kwl, struct attr_value_list *avl,
		void *arg)
{
	int i;

	msglog(LDMSD_LINFO,"%-48s %-20s %5s\n", "Name", "default", "R/S");
	msglog(LDMSD_LINFO,"%-48s %-20s %5s\n",
	       "------------------------------------------------",
	       "--------------------","-----");
	pthread_mutex_lock(&cfglock);
	for (i = 0; i < readvals.num_counter; i++){
		msglog(LDMSD_LINFO,"%-48s %-20s %3s\n",
		       readvals.counter_list[i].name, "N/A", "R");
	}
	for (i = 0; i < setvals.num_counter; i++){
		if (setvals.counter_list[i].isHex){
			msglog(LDMSD_LINFO,"%-48s 0x%lx %3s\n",
			       setvals.counter_list[i].name, setvals.counter_list[i].value, "S");
		} else {
			msglog(LDMSD_LINFO,"%-48s %20d %3s\n",
			       setvals.counter_list[i].name, setvals.counter_list[i].value, "S");
		}
	}
	pthread_mutex_unlock(&cfglock);
	return 0;

}


static int init(struct attr_value_list *kwl, struct attr_value_list *avl,
		void *arg)
{
	char* value;
	char* cfile;
	int rc;
	int i;

	if (cfgstate != CFG_PRE){
		msglog(LDMSD_LERROR, SAMP ": cannot reinit\n");
		return -1;
	}

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	pthread_mutex_lock(&cfglock);

	cfile = av_value(avl, "setfile");
	if (!cfile){
		msglog(LDMSD_LWARNING, SAMP ": NOTE -- no setfile (optional)");
	} else {
		rc = parseConfig(cfile,0);
		if (rc != 0){
			msglog(LDMSD_LERROR,
			       SAMP ": error parsing setfile. Aborting\n");
			_free_cfg();
			pthread_mutex_unlock(&cfglock);
			return rc;
		}
	}

	cfile = av_value(avl, "readfile");
	if (!cfile){
		msglog(LDMSD_LERROR, SAMP ": no readfile");
		_free_cfg();
		rc = EINVAL;
		pthread_mutex_unlock(&cfglock);
		return rc;
	} else {
		rc = parseConfig(cfile,1);
		if (rc != 0){
			msglog(LDMSD_LERROR,
			       SAMP ": error parsing readfile. Aborting\n");
			_free_cfg();
			pthread_mutex_unlock(&cfglock);
			return rc;
		}
	}

	value = av_value(avl, "aries_rtr_id");
	if (value)
		rtrid = strdup(value);
	else
		rtrid = strdup("");


	base = base_config(avl, SAMP, SAMP, msglog);
	if (!base) {
		rc = errno;
		_free_cfg();
		pthread_mutex_unlock(&cfglock);
		return rc;
	}

	pthread_mutex_unlock(&cfglock);
	cfgstate = CFG_DONE_INIT;

	return 0;
}


static int create_metric_set(base_data_t base){
	int i;
	int rc;

	/* create the base */
	schema = base_schema_new(base);
	if (!schema) {
		msglog(LDMSD_LERROR, SAMP
		       ": The schema '%s' could not be created, errno=%d.\n",
			base->schema_name, errno);
		goto err;
	}

	rc = ldms_schema_meta_array_add(schema, "aries_rtr_id",
					LDMS_V_CHAR_ARRAY, strlen(rtrid)+1);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}


	/* Add the metrics. Only the ones to be read.
	   Adding them in the order of the file.
	   They will be read off the context in reverse order but
	   the metric id will correct for the order */
	for (i = 0; i < readvals.num_counter; i++){
		if (readvals.counter_list[i].isHex){
			rc = ldms_schema_metric_array_add(schema, readvals.counter_list[i].name,
						    LDMS_V_CHAR_ARRAY,MSR_MAXLEN);
		} else {
			rc = ldms_schema_metric_add(schema, readvals.counter_list[i].name,
						    LDMS_V_U64);
		}

		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
		readvals.counter_list[i].mid = rc;
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	ldms_metric_array_set_str(set, ldms_metric_by_name(set, "aries_rtr_id"), rtrid);
	return 0;

err:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;

	return rc;
}

/**
 * Build ist of performance counters we wish to get values for.
 * No aggregation.
 */
int addMetricToContext(gpcd_context_t* lctx, char* met)
{
	gpcd_mmr_desc_t *desc;
	int status;
	int i;

	if (lctx == NULL){
		msglog(LDMSD_LERROR, SAMP ": NULL context\n");
		return -1;
	}

	desc = (gpcd_mmr_desc_t *)
		gpcd_lookup_mmr_byname(met);
	if (!desc) {
		msglog(LDMSD_LINFO, SAMP ": Could not lookup (2) <%s>\n", met);
		return -1;
	}

	status = gpcd_context_add_mmr(lctx, desc);
	if (status != 0) {
		msglog(LDMSD_LERROR, SAMP ": Could not add mmr for <%s>\n", met);
		gpcd_remove_context(lctx); //some other option?
		return -1;
	}

	return 0;
}


// cfg lock is taken coming into this
static int create_contexts(){
	int i;
	int rc;

	//Adding metrics to the contexts in the order of the file. They will be read off the contexts in reverse order, however.

	setvals.ctx = gpcd_create_context();
	if (!setvals.ctx){
		msglog(LDMSD_LERROR, SAMP ": Could not create context\n");
		return EINVAL;
	}

	readvals.ctx = gpcd_create_context();
	if (!readvals.ctx){
		msglog(LDMSD_LERROR, SAMP ": Could not create context\n");
		return EINVAL;
	}

	for (i = 0; i < setvals.num_counter; i++){
		rc = addMetricToContext(setvals.ctx, setvals.counter_list[i].name);
		if (rc != 0){
			return rc;
		}
	}

	for (i = 0; i < readvals.num_counter; i++){
		rc = addMetricToContext(readvals.ctx, readvals.counter_list[i].name);
		if (rc != 0){
			return rc;
		}
	}

	return 0;
}



static int finalize(struct attr_value_list *kwl, struct attr_value_list *avl,
		    void *arg)
{

	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	struct active_counter* pe;
	char name[MSR_MAXLEN];
	union ldms_value v;
	int rc;
	int i, j, k;

	pthread_mutex_lock(&cfglock);
	msglog(LDMSD_LDEBUG, SAMP ": finalizing\n");

	if (cfgstate != CFG_DONE_INIT){
		msglog(LDMSD_LERROR,
		       SAMP ": in wrong state to finalize <%d>\n", cfgstate);
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	cfgstate = CFG_IN_FINAL;

	rc = create_contexts();
	if (rc) {
		//if couldnt add any of the metrics, then this will have failed
		msglog(LDMSD_LERROR, SAMP ": failed to create contexts.\n");
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}

	rc = resetCounters(NULL,NULL,NULL);
	if (rc != 0){
		msglog(LDMSD_LERROR,
		       SAMP ": Cannot reset counters in finalize");
		goto err;
	}

	cfgstate = CFG_DONE_FINAL;
	pthread_mutex_unlock(&cfglock);
	return 0;

 err:
	msglog(LDMSD_LERROR, SAMP ": failed finalize\n");
	cfgstate = CFG_FAILED_FINAL;
	if (schema) ldms_schema_delete(schema);
	schema = NULL;
	_free_cfg();
	if (base)
		base_del(base);
	base = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
	msglog(LDMSD_LDEBUG, SAMP ": resetting state to CFG_PRE\n");
	// let the user configure again
	cfgstate = CFG_PRE;
	pthread_mutex_unlock(&cfglock);

	return rc;

}


struct kw kw_tbl[] = {
	{ "finalize", finalize },
	{ "initialize", init },
	{ "ls", list },
	{ "reset", resetCounters },
};


static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl,
		  struct attr_value_list *avl)
{
	struct kw *kw;
	struct kw key;
	int rc;


	char *action = av_value(avl, "action");

	if (!action)
		goto err0;

	key.token = action;
	kw = bsearch(&key, kw_tbl, ARRAY_SIZE(kw_tbl),
		     sizeof(*kw), kw_comparator);
	if (!kw)
		goto err1;

	rc = kw->action(kwl, avl, NULL);
	if (rc)
		goto err2;
	return 0;
 err0:
	msglog(LDMSD_LDEBUG,usage(self));
	goto err2;
 err1:
	msglog(LDMSD_LDEBUG, SAMP ": Invalid configuration keyword '%s'\n", action);
 err2:
	return 0;
}



static int sample(struct ldmsd_sampler *self)
{
	union ldms_value v;
	char sval[MSR_MAXLEN];
	int mid;
	int rc;

	if (cfgstate != CFG_DONE_FINAL){
		msglog(LDMSD_LERROR,
		       SAMP ": in wrong state for sampling <%d>\n", cfgstate);
		return -1;
	}

	if (!set){
		msglog(LDMSD_LERROR, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	//sample reads only the readvals
	if (readvals.num_counter){
		rc = gpcd_context_read_mmr_vals(readvals.ctx);
		if (rc){
			msglog(LDMSD_LERROR, SAMP ": Cannot read raw mmr vals\n");
			return EINVAL;
		}

		// we already set aries_rtr_id and that will not change

		listp = readvals.ctx->list;
		if (!listp){
			msglog(LDMSD_LERROR, SAMP ": Context list is null\n");
			rc = EINVAL;
			goto out;
		}

		mid = readvals.num_counter - 1;
		while (listp != NULL){
			if (mid < 0){
				msglog(LDMSD_LERROR, SAMP ": bad index value\n");
				rc = EINVAL;
				goto out;
			}
			//context is read off in the reverse order
			if (readvals.counter_list[mid].isHex){
				rc = snprintf(sval, MSR_MAXLEN-1, "0x%lx", listp->value);
				if (rc < 0){
					msglog(LDMSD_LERROR, SAMP ": cannot format hex value for setting variable");
					continue;
				}
				ldms_metric_array_set_str(set, readvals.counter_list[mid].mid, sval);
			} else {
				v.v_u64 = listp->value;
				ldms_metric_set(set, readvals.counter_list[mid].mid, &v);
			}
			listp=listp->next;
			mid--;
		}
	}

	rc = 0;
out:
	base_sample_end(base);
	return 0;

}


static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static void term(struct ldmsd_plugin *self)
{
	int i;

	_free_cfg();

	if (base)
		base_del(base);
	base = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler aries_mmr_configurable_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	set = NULL;
	return &aries_mmr_configurable_plugin.base;
}
