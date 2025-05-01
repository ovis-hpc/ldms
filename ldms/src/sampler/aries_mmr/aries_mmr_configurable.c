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
#include "ldmsd_plug_api.h"
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

static ovis_log_t mylog;

static ldms_set_t set = NULL;
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


static const char *usage(ldmsd_plug_handle_t handle)
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
		ovis_log(mylog, OVIS_LERROR,  " Cannot open config file <%s>\n",
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
			ovis_log(mylog, OVIS_LWARNING, " Comment in config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}
		if (i == 0){ // setfile
			rc = sscanf(lbuf, "%[^,],%c,%s\n",
				    name, &typeid, &rest);

			ovis_log(mylog, OVIS_LDEBUG,
			       " Setfile variable <%s>\n", name);

			if (rc != 3){
				ovis_log(mylog, OVIS_LWARNING,
				       " Bad format in config file <%s> <rc=%d>. Skipping\n",
				       lbuf, rc);
				continue;
			}
		} else { // readfile
			rc = sscanf(lbuf, "%[^,],%c",
				    name, &typeid);

			ovis_log(mylog, OVIS_LDEBUG,
			       " Readfile variable <%s>\n", name);

			if (rc != 2){
				ovis_log(mylog, OVIS_LWARNING,
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
			ovis_log(mylog, OVIS_LWARNING,
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
			ovis_log(mylog, OVIS_LWARNING,
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
			ovis_log(mylog, OVIS_LWARNING, " Comment in config file <%s>. Skipping\n",
			       lbuf);
			continue;
		}
		if (i == 0){ // setfile
			if (count == setvals.num_counter){
				ovis_log(mylog, OVIS_LERROR,
				       " Changed number of valid entries from first pass. aborting.\n");
				fclose(fp);
				return EINVAL;
			}
			rc = sscanf(lbuf, "%[^,],%c,%s\n",
				    name, &typeid, &rest);
			if (rc != 3){
				ovis_log(mylog, OVIS_LWARNING,
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
				ovis_log(mylog, OVIS_LDEBUG,
				       " Setfile read variable <%s> <0x%lx>\n",
				       setvals.counter_list[count].name,
				       setvals.counter_list[count].value);
			} else {
				ovis_log(mylog, OVIS_LDEBUG,
				       " Setfile read variable <%s> <%" PRIu64 ">\n",

				       setvals.counter_list[count].name,
				       setvals.counter_list[count].value);
			}
		} else { // readfile
			if (count == readvals.num_counter){
				ovis_log(mylog, OVIS_LERROR,
				       " Changed number of valid entries from first pass. aborting.\n");
				fclose(fp);
				return EINVAL;
			}
			rc = sscanf(lbuf, "%[^,],%c",
				    name, &typeid);
			if (rc != 2){
				ovis_log(mylog, OVIS_LWARNING,
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
			ovis_log(mylog, OVIS_LDEBUG,
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
		ovis_log(mylog, OVIS_LERROR,
		       "in wrong state for sampling <%d>\n", cfgstate);
		return -1;
	}

	//Keeping a context to be able to read them before and after
	//if debug, also write out the counters before and after
	if (ovis_log_get_level(mylog) == OVIS_LDEBUG) {
		rc = gpcd_context_read_mmr_vals(setvals.ctx);
		if (rc){
			ovis_log(mylog, OVIS_LERROR, "Cannot read set mmr vals\n");
			return EINVAL;
		}

		listp = setvals.ctx->list;
		if (!listp){
			ovis_log(mylog, OVIS_LERROR, "Context list is null\n");
			rc = EINVAL;
			goto out;
		}

		mid = setvals.num_counter - 1;
		while (listp != NULL){
			if (mid < 0){
				ovis_log(mylog, OVIS_LERROR, "bad index value\n");
				rc = EINVAL;
				goto out;
			}
			//context is read off in the reverse order
			if (setvals.counter_list[mid].isHex){
				ovis_log(mylog, OVIS_LERROR, "Before reset %s is 0x%lx\n",
				       setvals.counter_list[mid].name, listp->value);
			} else {
				ovis_log(mylog, OVIS_LERROR, "Before reset %s is %" PRIu64 "\n",
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
			ovis_log(mylog, OVIS_LERROR, "Could not lookup (1) <%s>\n", met);
			//TODO: what to do to clean up? This should not happen.
			return -1;
		}

//		ovis_log(mylog, OVIS_LDEBUG, "mmrd->addr %" PRIu64 "\n",
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
				ovis_log(mylog, OVIS_LERROR, "cannot gpcd_disable_perms. Must be root.\n");
				ovis_log(mylog, OVIS_LERROR, "cannot write mmr val because of gpcd permissions.\n");
				return rc;
			}
			rc = gpcd_write_mmr_val(desc, &(setvals.counter_list[i].value),
						0); //addr is 0, dont need nicaddr
			if (rc) {
				ovis_log(mylog, OVIS_LERROR, "cannot write_mmr_val '%s'", met);
				//TODO: Decide if should continue or return with error.
				return rc;
			}
		}
	}


	//if debug, also write out the counters before and after
	if (ovis_log_get_level(mylog) == OVIS_LDEBUG) {
		rc = gpcd_context_read_mmr_vals(setvals.ctx);
		if (rc){
			ovis_log(mylog, OVIS_LERROR, "Cannot read raw set mmr vals\n");
			return EINVAL;
		}

		listp = setvals.ctx->list;
		if (!listp){
			ovis_log(mylog, OVIS_LERROR, "Context list is null\n");
			rc = EINVAL;
			goto out;
		}

		mid = setvals.num_counter - 1;
		while (listp != NULL){
			if (mid < 0){
				ovis_log(mylog, OVIS_LERROR, "bad index value\n");
				rc = EINVAL;
				goto out;
			}
			//context is read off in the reverse order
			if (setvals.counter_list[mid].isHex){
				ovis_log(mylog, OVIS_LERROR, "After reset %s is 0x%lx\n",
				       setvals.counter_list[mid].name, listp->value);
			} else {
				ovis_log(mylog, OVIS_LERROR, "After reset %s is %" PRIu64 "\n",
				       setvals.counter_list[mid].name, listp->value);
			}
			listp=listp->next;
			mid--;
		}
	}


	return 0;

out:
	//will only get here in the debug
	ovis_log(mylog, OVIS_LDEBUG, "Problems debugging the state of the set counters\n");
	return EINVAL;

}




static int list(struct attr_value_list *kwl, struct attr_value_list *avl,
		void *arg)
{
	int i;

	ovis_log(mylog, OVIS_LINFO,"%-48s %-20s %5s\n", "Name", "default", "R/S");
	ovis_log(mylog, OVIS_LINFO,"%-48s %-20s %5s\n",
	       "------------------------------------------------",
	       "--------------------","-----");
	pthread_mutex_lock(&cfglock);
	for (i = 0; i < readvals.num_counter; i++){
		ovis_log(mylog, OVIS_LINFO,"%-48s %-20s %3s\n",
		       readvals.counter_list[i].name, "N/A", "R");
	}
	for (i = 0; i < setvals.num_counter; i++){
		if (setvals.counter_list[i].isHex){
			ovis_log(mylog, OVIS_LINFO,"%-48s 0x%lx %3s\n",
			       setvals.counter_list[i].name, setvals.counter_list[i].value, "S");
		} else {
			ovis_log(mylog, OVIS_LINFO,"%-48s %20d %3s\n",
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
		ovis_log(mylog, OVIS_LERROR, "cannot reinit\n");
		return -1;
	}

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	pthread_mutex_lock(&cfglock);

	cfile = av_value(avl, "setfile");
	if (!cfile){
		ovis_log(mylog, OVIS_LWARNING, "NOTE -- no setfile (optional)");
	} else {
		rc = parseConfig(cfile,0);
		if (rc != 0){
			ovis_log(mylog, OVIS_LERROR,
			       "error parsing setfile. Aborting\n");
			_free_cfg();
			pthread_mutex_unlock(&cfglock);
			return rc;
		}
	}

	cfile = av_value(avl, "readfile");
	if (!cfile){
		ovis_log(mylog, OVIS_LERROR, "no readfile");
		_free_cfg();
		rc = EINVAL;
		pthread_mutex_unlock(&cfglock);
		return rc;
	} else {
		rc = parseConfig(cfile,1);
		if (rc != 0){
			ovis_log(mylog, OVIS_LERROR,
			       "error parsing readfile. Aborting\n");
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

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
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
		ovis_log(mylog, OVIS_LERROR,
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
		ovis_log(mylog, OVIS_LERROR, "NULL context\n");
		return -1;
	}

	desc = (gpcd_mmr_desc_t *)
		gpcd_lookup_mmr_byname(met);
	if (!desc) {
		ovis_log(mylog, OVIS_LINFO, "Could not lookup (2) <%s>\n", met);
		return -1;
	}

	status = gpcd_context_add_mmr(lctx, desc);
	if (status != 0) {
		ovis_log(mylog, OVIS_LERROR, "Could not add mmr for <%s>\n", met);
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
		ovis_log(mylog, OVIS_LERROR, "Could not create context\n");
		return EINVAL;
	}

	readvals.ctx = gpcd_create_context();
	if (!readvals.ctx){
		ovis_log(mylog, OVIS_LERROR, "Could not create context\n");
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
	ovis_log(mylog, OVIS_LDEBUG, "finalizing\n");

	if (cfgstate != CFG_DONE_INIT){
		ovis_log(mylog, OVIS_LERROR,
		       "in wrong state to finalize <%d>\n", cfgstate);
		pthread_mutex_unlock(&cfglock);
		return -1;
	}

	cfgstate = CFG_IN_FINAL;

	rc = create_contexts();
	if (rc) {
		//if couldnt add any of the metrics, then this will have failed
		ovis_log(mylog, OVIS_LERROR, "failed to create contexts.\n");
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}

	rc = resetCounters(NULL,NULL,NULL);
	if (rc != 0){
		ovis_log(mylog, OVIS_LERROR,
		       "Cannot reset counters in finalize");
		goto err;
	}

	cfgstate = CFG_DONE_FINAL;
	pthread_mutex_unlock(&cfglock);
	return 0;

 err:
	ovis_log(mylog, OVIS_LERROR, "failed finalize\n");
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
	ovis_log(mylog, OVIS_LDEBUG, "resetting state to CFG_PRE\n");
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


static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl,
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
	ovis_log(mylog, OVIS_LDEBUG,usage(context));
	goto err2;
 err1:
	ovis_log(mylog, OVIS_LDEBUG, "Invalid configuration keyword '%s'\n", action);
 err2:
	return 0;
}



static int sample(ldmsd_plug_handle_t handle)
{
	union ldms_value v;
	char sval[MSR_MAXLEN];
	int mid;
	int rc;

	if (cfgstate != CFG_DONE_FINAL){
		ovis_log(mylog, OVIS_LERROR,
		       "in wrong state for sampling <%d>\n", cfgstate);
		return -1;
	}

	if (!set){
		ovis_log(mylog, OVIS_LERROR, "plugin not initialized\n");
		return EINVAL;
	}

	//sample reads only the readvals
	if (readvals.num_counter){
		rc = gpcd_context_read_mmr_vals(readvals.ctx);
		if (rc){
			ovis_log(mylog, OVIS_LERROR, "Cannot read raw mmr vals\n");
			return EINVAL;
		}

		// we already set aries_rtr_id and that will not change

		listp = readvals.ctx->list;
		if (!listp){
			ovis_log(mylog, OVIS_LERROR, "Context list is null\n");
			rc = EINVAL;
			goto out;
		}

		mid = readvals.num_counter - 1;
		while (listp != NULL){
			if (mid < 0){
				ovis_log(mylog, OVIS_LERROR, "bad index value\n");
				rc = EINVAL;
				goto out;
			}
			//context is read off in the reverse order
			if (readvals.counter_list[mid].isHex){
				rc = snprintf(sval, MSR_MAXLEN-1, "0x%lx", listp->value);
				if (rc < 0){
					ovis_log(mylog, OVIS_LERROR, "cannot format hex value for setting variable");
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

static void term(ldmsd_plug_handle_t handle)
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

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
        set = NULL;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
