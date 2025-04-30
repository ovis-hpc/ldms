/* -*- c-basic-offset: 8 -*-
  * Copyright (c) 2018 National Technology & Engineering Solutions
  * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
  * NTESS, the U.S. Government retains certain rights in this software.
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
/*
 * \file appinfo.c
 * \brief  Profiler metric sampler
 *
 * This sampler collect metrics data from the user application.
 *
 * Note: the sampler doesn't support array data structure, except char array.
 *   (from some code left over from Omar's version, need to rethink this)
 *
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_jobid.h"
#include <time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "sampler_base.h"

#define SAMP "appinfo"
static ovis_log_t mylog;

static ldms_set_t set = NULL; /* metric set to fill in w/ data */
static int metric_offset; /* starting index for non-base metrics */
static base_data_t base;  /* inherited base sampler data */
static int jobid_index=1, appid_index=2; /* will lookup for better init */

/* Shared memory defs */
#include "ldms_appinfo_shm.h" /* shared memory structs for appinfo sampler */
static int shmem_fd; /** file descriptor for shmem **/
static struct stat shmem_stat;
static shmem_header_t *shmem_header; /** pointer to beginning of shmem **/
static void *shmem_data; /** pointer to this process' shmem data segment **/
static int shmem_pmid; /** index into process metadata array **/
static char *shmem_name = "/shm_ldmsapp"; /** shared memory name **/
static char *sem_name = "/sem_ldmsapp"; /** ldmsapp_mutex name **/
static sem_t *ldmsapp_mutex; /** mutex for accessing shmem **/
static int last_process_sampled = 0; /** index of last process sampled **/

/* Internal forward prototypes */
static int sample_proc_metric(app_metric_t *metric, int id);
static int create_shmem_data(ldms_set_t set);

/**
* Create the metric set (and schema?). This is called from config()
* and is responsible for creating the appinfo-specific metrics,
* which is done after the base sampler code created its metrics.
* @param base is the base sampler data structure
**/
static int create_metric_set(base_data_t base, char *metrics_optstr)
{
	ldms_schema_t schema;
	int rc, i;
	uint64_t metric_value;
	union ldms_value v;
	char *s;
	char lbuf[256];
	char metric_name[128];

	ovis_log(mylog, OVIS_LDEBUG, "Creating schema.\n");

	/* Invoke base sampler schema create part */
	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
			"%s: The schema '%s' could not be created, errno=%d.\n",
			__FILE__, base->schema_name, errno);
		goto err;
	}

	/* Get first metric id of non-base metrics */
	metric_offset = ldms_schema_metric_count_get(schema);

	/* Create process id metric */
	rc = ldms_schema_metric_add(base->schema, "proc_id", LDMS_V_U32);
	if (rc < 0) {
		errno = ENOMEM;
		goto err;
	}
	/* Create rank id metric */
	rc = ldms_schema_metric_add(base->schema, "rank_id", LDMS_V_U32);
	if (rc < 0) {
		errno = ENOMEM;
		goto err;
	}

	/*
	 * Create all of our specific metrics here
	 */
	rc = -7; /* placeholder for metrics error reporting
	if (!metrics_optstr)
		goto err; /* more error here??? */

	ovis_log(mylog, OVIS_LDEBUG, "Creating config-opt metrics.\n");

	char *mptr, *mstr, *iptr, *mname, *mtype;
	mstr = strtok_r(metrics_optstr, ",", &mptr);
	while (mstr) {
		/* get metric from "name:type" internal string */
		mname = strtok_r(mstr, ":", &iptr);
		if (!mname)
			goto err;
		mtype = strtok_r(NULL, ":", &iptr);
		if (!mtype)
			goto err;
		if (ldms_metric_str_to_type(mtype) == LDMS_V_NONE)
			goto err;
		if (ldms_type_is_array(ldms_metric_str_to_type(mtype))) {
			/* TODO Make a 256-byte char array as the data???? */
			if (ldms_metric_str_to_type(mtype) ==
					LDMS_V_CHAR_ARRAY) {
				rc = ldms_schema_metric_array_add(schema, mname,
					ldms_metric_str_to_type(mtype), 256);
				if (rc < 0)
					goto err;
			}
		} else {
			/* add a standard metric type */
			rc = ldms_schema_metric_add(schema, mname,
							ldms_metric_str_to_type(mtype));
			if (rc < 0)
				goto err;
		}

		/* get next metric in opt string */
		mstr = strtok_r(NULL, ",", &mptr);
	} /* end loop creating appinfo metrics from config string */


	/*
	 * From sampler_base.c this looks like it creates the whole
	 * metric set and also sets the base metrics to initial values
	 */
	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}
	/* find indices of job_id and app_id, and save */
	jobid_index = ldms_metric_by_name(base->set, "job_id");
	appid_index = ldms_metric_by_name(base->set, "app_id");

	/* TODO SHOULD initialize our own metrics to 0 here???? */
	/* all good, so return success */
	ovis_log(mylog, OVIS_LDEBUG, "Done creating schema.\n");
	return 0;

 err:
	/* cleanup any of our own stuff here (nothing for now) */
	/* TODO delete base schema???? */
	return rc;
}

/**
 * Check config data for validity. Check for invalid flags, with
 * particular emphasis on warning the user about deprecated usage.
 **/
static int config_check(struct attr_value_list *kwl,
			struct attr_value_list *avl, void *arg)
{
	/* JEC -- meminfo had no config params specific? This looks generic. */
	/* so what will appinfo need? */
	char *value;
	int i;

	char* deprecated[]={"set"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(mylog, OVIS_LERROR, "config argument %s has "
				"been deprecated.\n", deprecated[i]);
			return EINVAL;
		}
	}

	ovis_log(mylog, OVIS_LDEBUG, "Config check done.\n");
	return 0;
}

/**
 * Sampler usage message.
 * @param self is the LDMS plugin handle
 **/
static const char *usage(ldmsd_plug_handle_t handle)
{
	return "config name=" SAMP BASE_CONFIG_USAGE
		" producer=<prod_name> instance=<inst_name>\n"
		"	metrics=<metric name>:<metric type>,<metric name>:<metric type>,...\n"
		"	appname=<appname> jobid=<jobid> username=<username>\n"
		"	[component_id=<compid> schema=<sname>]\n"
		"	prod_name	The producer name\n"
		"	inst_name	The instance name\n"
		"	compid		Optional unique number identifier. Defaults to zero.\n"
		"	metrics	Metrics with types"
		"		appname	The application name\n"
		"		jobid		The jobid\n"
		"		username	The username\n"
		"	sname		Optional schema name. Defaults to '" SAMP "'\n";
}

/**
 * Sampler configuration. Here we do normal sampler configuration (create
 * metric set, etc) but also we create and initialize the shared memory
 * structs for communicating with the application.
 * @param self is the LDMS plugin handle
 * @param kwl is the configuration keyword list
 * @param avl is the configuration attribute-value list
 **/
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl,
		struct attr_value_list *avl)
{
	int rc;
	int actual_shmem_size;
	char *metrics_str;
	/* initialize as failed */
	shmem_header = MAP_FAILED;
	ldmsapp_mutex = SEM_FAILED;
	shmem_fd = -1;
	ovis_log(mylog, OVIS_LDEBUG, "Begin configuring.\n");

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, NULL);
	if (rc != 0){
		return rc;
	}

	/* Invoke base sampler config, let it do what it needs */
	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, ldmsd_plug_log_get(handle));
	if (!base) {
		rc = errno;
		goto err;
	}

	metrics_str = av_value(avl,"metrics");
	if (!metrics_str) {
		ovis_log(mylog, OVIS_LERROR, "not metrics option given.\n");
		goto err;
	}
	/* create appinfo metrics beyond base */
	rc = create_metric_set(base, metrics_str);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}

	/* Do shared mem stuff after metrics create so that we know the size */
	/* of the metric set TODO: somehow use this in creating shmem size */

	ovis_log(mylog, OVIS_LDEBUG, "Configuring shared memory.\n");

	/* create shared memory semaphore, fail if error */
	ldmsapp_mutex = sem_open(sem_name, O_CREAT, 0666, 1);
	if (ldmsapp_mutex == SEM_FAILED) {
		ovis_log(mylog, OVIS_LERROR, "Semaphore open error: %s\n",
			STRERROR(errno));
		goto err;
	}

	/* Reset the lock to 1 -- JEC: why? in case it existed... */
	while (sem_trywait(ldmsapp_mutex) >= 0)
		;
	sem_post(ldmsapp_mutex);

	/* open shared memory segment */
	shmem_fd = shm_open(shmem_name, O_CREAT|O_RDWR, 0666);
	if (shmem_fd == -1) {
		ovis_log(mylog, OVIS_LERROR, "Shmem open/create error: %s\n",
			STRERROR(errno));
		goto err;
	}
	/* Set size of shared memory segment */
	/* This is just the max possible size, for now; must fix */
	rc = ftruncate(shmem_fd, sizeof(shmem_header_t) +
			MAX_PROCESSES*MAX_METRICS*(sizeof(app_metric_t)+256));
	/* Get stats (size) of shared memory segment and check */
	if (fstat(shmem_fd, &shmem_stat) == -1) {
		ovis_log(mylog, OVIS_LERROR, "Shmem stat error: %s\n",
			STRERROR(errno));
		goto err;
	}
	if (shmem_stat.st_size < 256 /* TODO CHECK ACTUAL SIZE */) {
		ovis_log(mylog, OVIS_LERROR, "Shmem size too small: %u\n",
			shmem_stat.st_size);
		goto err;
	}

	/* map shared memory into our memory */
	shmem_header = (shmem_header_t *) mmap(0, shmem_stat.st_size,
				PROT_READ|PROT_WRITE, MAP_SHARED, shmem_fd, 0);
	if (shmem_header == MAP_FAILED) {
		ovis_log(mylog, OVIS_LERROR, "Shmem mmap error: %s\n",
			STRERROR(errno));
		goto err;
	}

	/* Now set up all the sampler-specified data in shared memory */
	actual_shmem_size = create_shmem_data(set);
	/* TODO do another ftruncate here to shrink shmem to actual size */

	/* BACK to regular config stuff here */

	ovis_log(mylog, OVIS_LDEBUG, "Done configuring.\n");
	return 0;
 err:
	/* cleanup any successful progress that might have happened */
	if (shmem_header != MAP_FAILED) munmap(shmem_header,shmem_stat.st_size);
	if (shmem_fd != -1) close(shmem_fd);
	if (ldmsapp_mutex != SEM_FAILED) sem_close(ldmsapp_mutex);
	/* normal sampler cleanup */
	base_del(base);
	/* TODO delete metric set if created? */
	return rc;
}

/**
 * Sample data. In the appinfo sampler, we need to find in the shared
 * memory the next process data structure that is ready for sampling,
 * copy it into the metric set, and mark it as sampled.
 **/
static int sample(ldmsd_plug_handle_t handle)
{
	int rc, i;
	int metric_no;
	char *s;
	void *proc_data;
	app_metric_t *am;
	char lbuf[256];
	char metric_name[128];
	union ldms_value v;
	ovis_log(mylog, OVIS_LDEBUG, "Begin sample.\n");

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}
	sem_wait(ldmsapp_mutex); /* shared mem control */
	/* Find which process we should sample from */
	i = (last_process_sampled+1) % MAX_PROCESSES;
	do {
		if (shmem_header->proc_metadata[i].status == NEEDSAMPLED)
			break;
		i = (i+1) % MAX_PROCESSES;
	} while (i != last_process_sampled);
	if (shmem_header->proc_metadata[i].status != NEEDSAMPLED) {
		/* no process needs sampled, so skip! */
		ovis_log(mylog, OVIS_LDEBUG, "No new data to sample.\n");
		sem_post(ldmsapp_mutex); /* shared mem control */
		return 0;
	}
	/* we will sample this process, so reset its status */
	shmem_header->proc_metadata[i].status = NOTREADY;
	last_process_sampled = i;

	/* let base sampler fill in its data */
	base_sample_begin(base);
	/* fill in app_id and job_id directly (TODO use common code here?) */
	ldms_metric_set_u64(base->set, jobid_index,
				shmem_header->proc_metadata[i].job_id);
	ldms_metric_set_u64(base->set, appid_index,
				shmem_header->proc_metadata[i].app_id);
	/* start our metrics at offset */
	metric_no = metric_offset;
	/* fill in process and rank ids */
	ldms_metric_set_u32(set, metric_no++,
				shmem_header->proc_metadata[i].process_id);
	ldms_metric_set_u32(set, metric_no++,
				shmem_header->proc_metadata[i].rank_id);
	/* then here fill in all of our metrics */
	/* - copy process metrics over to metric set */
	/* - create pointer to process data block */
	proc_data = ((void *) shmem_header) +
				shmem_header->proc_metadata[i].start_offset;
	for (i = metric_no; i < shmem_header->metric_count; i++) {
		am = (app_metric_t *) (proc_data +
					shmem_header->metric_offset[i]);
		sample_proc_metric(am, i);
	}

out:
	base_sample_end(base);
	sem_post(ldmsapp_mutex); /* shared mem control */
	ovis_log(mylog, OVIS_LDEBUG, "End sample.\n");
	return 0;
}

/**
 * Sampler termination.
 **/
static void term(ldmsd_plug_handle_t handle)
{
	ovis_log(mylog, OVIS_LDEBUG, "Terminating sampler.\n");
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
	/* cleanup our own stuff here */
	if (shmem_header != MAP_FAILED) munmap(shmem_header,shmem_stat.st_size);
	if (shmem_fd != -1) close(shmem_fd);
	if (ldmsapp_mutex != SEM_FAILED) sem_close(ldmsapp_mutex);
	/* probably should unlink them */
	shm_unlink(shmem_name);
	sem_unlink(sem_name);
	ovis_log(mylog, OVIS_LDEBUG, "Done terminating.\n");
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
        set = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

/**
 * Sampler LDMS definition.
 **/
struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};

/**
 * Create shared memory structs and data from the newly defined
 * schema and metric set (actually, just use the metric set)
 * @return total shared memory size
 **/
static int create_shmem_data(ldms_set_t set)
{
	int i, num_metrics;
	unsigned int data_block_size, tot_size;
	const char *mname;
	enum ldms_value_type mtype;
	app_metric_t *am;
	void *proc1_data, *tp;

	ovis_log(mylog, OVIS_LDEBUG, "Begin shmem data creation.\n");
	sem_wait(ldmsapp_mutex); /* shared mem control */

	/* make pointer to first process data block in shared mem */
	/* we will set the metric info in the proc1 data block as */
	/* a template, and then copy it to the others */
	proc1_data = ((void*) shmem_header) + sizeof(shmem_header_t);

	num_metrics = ldms_set_card_get(set);
	if (num_metrics > MAX_METRICS) {
		ovis_log(mylog, OVIS_LERROR, SAMP
			": too many metrics (%d), truncating.\n", num_metrics);
		num_metrics = MAX_METRICS;
	}
	shmem_header->max_processes = MAX_PROCESSES; /* shmem field set */
	shmem_header->metric_count = num_metrics; /* shmem field set */
	data_block_size = 0;
	/* walk through metric set */
	for (i = 0; i < num_metrics; i++) {
		mname = ldms_metric_name_get(set,i);
		mtype = ldms_metric_type_get(set,i);
		if (!mname || mtype==LDMS_V_NONE) {
			ovis_log(mylog, OVIS_LERROR,
				"bad metric def at metric %d!\n", i);
			continue;
		}
		shmem_header->metric_offset[i] = data_block_size;
		am = (app_metric_t *) (proc1_data + data_block_size);
		am->status = 0;
		strncpy(am->name, mname, sizeof(am->name));
		am->name[sizeof(am->name)] = '\0';
		am->vtype = mtype;
		am->value.v_u64 = 0; /* just zero out value union field */
		/* now add current record's size */
		if (!ldms_type_is_array(mtype)) {
			/* regular data type */
			am->size = sizeof(app_metric_t);
		} else {
			/* only a 256-byte array type is supported */
			/* must subtract the base data field */
			am->size = sizeof(app_metric_t) -
				sizeof(union ldms_value) + 256;
		}
		data_block_size += am->size;
	}
	/* Clear out any remaining metric offsets */
	for (; i < MAX_METRICS; i++)
		shmem_header->metric_offset[i] = 0;
	shmem_header->data_block_size = data_block_size;
	/* Clear out process table */
	tot_size = sizeof(shmem_header_t);
	for (i=0; i < MAX_PROCESSES; i++) {
		shmem_header->proc_metadata[i].status = AVAILABLE;
		shmem_header->proc_metadata[i].app_id = 0;
		shmem_header->proc_metadata[i].job_id = 0;
		shmem_header->proc_metadata[i].process_id = 0;
		shmem_header->proc_metadata[i].rank_id = 0;
		shmem_header->proc_metadata[i].start_offset = tot_size;
		tot_size += data_block_size;
	}
	/* Copy initialized process data 1 to all other data blocks */
	tp = proc1_data + data_block_size;
	for (i=1; i < MAX_PROCESSES; i++) {
		memcpy(tp,proc1_data,data_block_size);
		tp += data_block_size;
	}

	sem_post(ldmsapp_mutex); /* shared mem control */
	ovis_log(mylog, OVIS_LDEBUG, "Done shmem data creation.\n");
	return tot_size;
}

/**
 * Sample a process metric.
 * @param metric is the pointer to the process metric struct
 * @param id is the metric index (for now, absolute)
 * @return 0 on success, nonzero on failure
 **/
static int sample_proc_metric(app_metric_t *metric, int id)
{
	/* TODO Should id param be absolute or relative to metric_offset? */
	/* (if relative we need this:) mid = mid + metric_offset; */
	switch (metric->vtype) {
		case LDMS_V_CHAR:
			ldms_metric_set_char(set, id, metric->value.v_char);
			break;
		case LDMS_V_CHAR_ARRAY:
			ldms_metric_array_set_str(set, id,
						metric->value.a_char);
			break;
		case LDMS_V_U8:
		case LDMS_V_U8_ARRAY:
			ldms_metric_set_u8(set, id, metric->value.v_u8);
			break;
		case LDMS_V_U16:
		case LDMS_V_U16_ARRAY:
			ldms_metric_set_u16(set, id, metric->value.v_u16);
			break;
		case LDMS_V_U32:
		case LDMS_V_U32_ARRAY:
			ldms_metric_set_u32(set, id, metric->value.v_u32);
			break;
		case LDMS_V_U64:
		case LDMS_V_U64_ARRAY:
			ldms_metric_set_u64(set, id, metric->value.v_u64);
			break;
		case LDMS_V_S8:
			ldms_metric_set_s8(set, id, metric->value.v_s8);
			break;
		case LDMS_V_S16:
			ldms_metric_set_s16(set, id, metric->value.v_s16);
			break;
		case LDMS_V_S32:
			ldms_metric_set_s32(set, id, metric->value.v_s32);
			break;
		case LDMS_V_S64:
			ldms_metric_set_s64(set, id, metric->value.v_s64);
			break;
		default:
			ovis_log(mylog, OVIS_LERROR,
				"Unrecognized or unsupported"
				" metric type '%d'\n", metric->vtype);
			return -1;
	}
	return 0;
}
