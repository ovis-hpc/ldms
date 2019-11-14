/* -*- c-basic-offset: 8 -*-
  * Copyright (c) 2018-2019 National Technology & Engineering Solutions
  * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
  * NTESS, the U.S. Government retains certain rights in this software.
  * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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
#include <time.h>

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#include "ldms_appinfo_shm.h" /* shared memory structs for appinfo sampler */

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

typedef struct appinfo_inst_s *appinfo_inst_t;
struct appinfo_inst_s {
	struct ldmsd_plugin_inst_s base;

	char *metrics_optstr;

	int shmem_fd; /** file descriptor for shmem **/
	struct stat shmem_stat;
	shmem_header_t *shmem_header; /** pointer to beginning of shmem **/
	void *shmem_data; /** pointer to this process' shmem data segment **/
	int shmem_pmid; /** index into process metadata array **/
	char *shmem_name; /** shared memory name **/
	char *sem_name; /** ldmsapp_mutex name **/
	sem_t *ldmsapp_mutex; /** mutex for accessing shmem **/
	int last_process_sampled; /** index of last process sampled **/
};

static int create_shmem_data(appinfo_inst_t inst, ldms_set_t set)
{
	int i, num_metrics;
	unsigned int data_block_size, tot_size;
	const char *mname;
	enum ldms_value_type mtype;
	app_metric_t *am;
	void *proc1_data, *tp;

	INST_LOG(inst, LDMSD_LDEBUG, "Begin shmem data creation.\n");
	sem_wait(inst->ldmsapp_mutex); /* shared mem control */

	/* make pointer to first process data block in shared mem */
	/* we will set the metric info in the proc1 data block as */
	/* a template, and then copy it to the others */
	proc1_data = ((void*) inst->shmem_header) + sizeof(shmem_header_t);

	num_metrics = ldms_set_card_get(set);
	if (num_metrics > MAX_METRICS) {
		INST_LOG(inst, LDMSD_LERROR,
			 "too many metrics (%d), truncating.\n", num_metrics);
		num_metrics = MAX_METRICS;
	}
	inst->shmem_header->max_processes = MAX_PROCESSES; /* shmem field set */
	inst->shmem_header->metric_count = num_metrics; /* shmem field set */
	data_block_size = 0;
	/* walk through metric set */
	for (i = 0; i < num_metrics; i++) {
		mname = ldms_metric_name_get(set,i);
		mtype = ldms_metric_type_get(set,i);
		if (!mname || mtype==LDMS_V_NONE) {
			INST_LOG(inst, LDMSD_LERROR,
				 "bad metric def at metric %d!\n", i);
			continue;
		}
		inst->shmem_header->metric_offset[i] = data_block_size;
		am = (app_metric_t *) (proc1_data + data_block_size);
		am->status = 0;
		strncpy(am->name, mname, sizeof(am->name));
		am->name[sizeof(am->name)-1] = '\0';
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
		inst->shmem_header->metric_offset[i] = 0;
	inst->shmem_header->data_block_size = data_block_size;
	/* Clear out process table */
	tot_size = sizeof(shmem_header_t);
	for (i=0; i < MAX_PROCESSES; i++) {
		inst->shmem_header->proc_metadata[i].status = AVAILABLE;
		inst->shmem_header->proc_metadata[i].app_id = 0;
		inst->shmem_header->proc_metadata[i].job_id = 0;
		inst->shmem_header->proc_metadata[i].process_id = 0;
		inst->shmem_header->proc_metadata[i].rank_id = 0;
		inst->shmem_header->proc_metadata[i].start_offset = tot_size;
		tot_size += data_block_size;
	}
	/* Copy initialized process data 1 to all other data blocks */
	tp = proc1_data + data_block_size;
	for (i=1; i < MAX_PROCESSES; i++) {
		memcpy(tp,proc1_data,data_block_size);
		tp += data_block_size;
	}

	sem_post(inst->ldmsapp_mutex); /* shared mem control */
	INST_LOG(inst, LDMSD_LDEBUG, "Done shmem data creation.\n");
	return tot_size;
}

static int sample_proc_metric(appinfo_inst_t inst, ldms_set_t set,
			      app_metric_t *metric, int id)
{
	/* TODO Should id param be absolute or relative to metric_offset? */
	/* (if relative we need this:) mid = mid + metric_offset; */
	switch (metric->vtype) {
	case LDMS_V_CHAR_ARRAY:
		ldms_metric_array_set_str(set, id, metric->value.a_char);
		break;
	case LDMS_V_CHAR:
	case LDMS_V_U8:
	case LDMS_V_S8:
	case LDMS_V_U16:
	case LDMS_V_S16:
	case LDMS_V_U32:
	case LDMS_V_S32:
	case LDMS_V_U64:
	case LDMS_V_S64:
		ldms_metric_set(set, id, &metric->value);
		break;
	default:
		INST_LOG(inst, LDMSD_LERROR,
			 "Unrecognized or unsupported metric type '%d'\n",
			 metric->vtype);
		return -1;
	}
	return 0;
}
/* ============== Sampler Plugin APIs ================= */

static
int appinfo_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	appinfo_inst_t inst = (void*)pi;
	int rc;

	if (!inst->metrics_optstr)
		return EINVAL;

	INST_LOG(inst, LDMSD_LDEBUG, "Creating schema.\n");

	/* Create process id metric */
	rc = ldms_schema_metric_add(schema, "proc_id", LDMS_V_U32, "");
	if (rc < 0)
		return -rc; /* rc = -errno */
	/* Create rank id metric */
	rc = ldms_schema_metric_add(schema, "rank_id", LDMS_V_U32, "");
	if (rc < 0)
		return -rc; /* rc = -errno */

	/*
	 * Create all of our specific metrics here
	 */
	rc = -7;

	INST_LOG(inst, LDMSD_LDEBUG, "Creating config-opt metrics.\n");

	char *mptr, *mstr, *iptr, *mname, *mtype;
	mstr = strtok_r(inst->metrics_optstr, ",", &mptr);
	while (mstr) {
		/* get metric from "name:type" internal string */
		mname = strtok_r(mstr, ":", &iptr);
		if (!mname)
			return EINVAL;
		mtype = strtok_r(NULL, ":", &iptr);
		if (!mtype)
			return EINVAL;
		if (ldms_metric_str_to_type(mtype) == LDMS_V_NONE)
			return EINVAL;
		if (ldms_type_is_array(ldms_metric_str_to_type(mtype))) {
			/* TODO Make a 256-byte char array as the data???? */
			if (ldms_metric_str_to_type(mtype) ==
					LDMS_V_CHAR_ARRAY) {
				rc = ldms_schema_metric_array_add(schema, mname,
						LDMS_V_CHAR_ARRAY, "", 256);
				if (rc < 0)
					return -rc;
			}
		} else {
			/* add a standard metric type */
			rc = ldms_schema_metric_add(schema, mname,
					ldms_metric_str_to_type(mtype), "");
			if (rc < 0)
				return -rc;
		}

		/* get next metric in opt string */
		mstr = strtok_r(NULL, ",", &mptr);
	} /* end loop creating appinfo metrics from config string */

	/* all good, so return success */
	INST_LOG(inst, LDMSD_LDEBUG, "Done creating schema.\n");
	return 0;
}

static
int appinfo_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	appinfo_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int i;
	int metric_no;
	void *proc_data;
	app_metric_t *am;

	/* Populate set metrics */
	INST_LOG(inst, LDMSD_LDEBUG, "Begin sample.\n");

	sem_wait(inst->ldmsapp_mutex); /* shared mem control */
	/* Find which process we should sample from */
	i = (inst->last_process_sampled+1) % MAX_PROCESSES;
	do {
		if (inst->shmem_header->proc_metadata[i].status == NEEDSAMPLED)
			break;
		i = (i+1) % MAX_PROCESSES;
	} while (i != inst->last_process_sampled);
	if (inst->shmem_header->proc_metadata[i].status != NEEDSAMPLED) {
		/* no process needs sampled, so skip! */
		INST_LOG(inst, LDMSD_LDEBUG, "No new data to sample.\n");
		goto out;
	}
	/* we will sample this process, so reset its status */
	inst->shmem_header->proc_metadata[i].status = NOTREADY;
	inst->last_process_sampled = i;

	/* fill in app_id and job_id directly (TODO use common code here?) */
	ldms_metric_set_u64(set, samp->job_id_idx,
			    inst->shmem_header->proc_metadata[i].job_id);
	ldms_metric_set_u64(set, samp->app_id_idx,
			    inst->shmem_header->proc_metadata[i].app_id);
	/* start our metrics at offset */
	metric_no = samp->first_idx;
	/* fill in process and rank ids */
	ldms_metric_set_u32(set, metric_no++,
			    inst->shmem_header->proc_metadata[i].process_id);
	ldms_metric_set_u32(set, metric_no++,
			    inst->shmem_header->proc_metadata[i].rank_id);
	/* then here fill in all of our metrics */
	/* - copy process metrics over to metric set */
	/* - create pointer to process data block */
	proc_data = ((void *) inst->shmem_header) +
			inst->shmem_header->proc_metadata[i].start_offset;
	for (i = metric_no; i < inst->shmem_header->metric_count; i++) {
		am = (app_metric_t *) (proc_data +
					inst->shmem_header->metric_offset[i]);
		sample_proc_metric(inst, set, am, i);
	}

out:
	sem_post(inst->ldmsapp_mutex); /* shared mem control */
	INST_LOG(inst, LDMSD_LDEBUG, "End sample.\n");
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *appinfo_desc(ldmsd_plugin_inst_t pi)
{
	return "appinfo - appinfo sampler plugin";
}

static
char *_help = "\
config name=<INST> [COMMON_OPTIONS]\n\
	metrics=<metric name>:<metric type>,<metric name>:<metric type>,...\n\
\n\
	metrics	        Metrics with types\n\
";

static
const char *appinfo_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int appinfo_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	appinfo_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	ldms_set_t set;
	json_entity_t value;
	int rc;
	int actual_shmem_size;

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */
	value = json_value_find(json, "metrics");
	if (!value) {
		ldmsd_log(LDMSD_LERROR, "%s: The 'metrics' attribute is "
						"missing.\n",pi->inst_name);
		return EINVAL;
	}
	if (value->type != JSON_STRING_VALUE) {
		ldmsd_log(LDMSD_LERROR, "%s: The given 'metrics' value is "
						"not a string.\n",pi->inst_name);
		return EINVAL;
	}
	inst->metrics_optstr = strdup(json_value_str(value)->str);
	if (!inst->metrics_optstr) {
		snprintf(ebuf, ebufsz, "no metrics option given.\n");
		return EINVAL;
	}
	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;

	/* Do shared mem stuff after metrics create so that we know the size */
	/* of the metric set TODO: somehow use this in creating shmem size */

	INST_LOG(inst, LDMSD_LDEBUG, "Configuring shared memory.\n");

	/* create shared memory semaphore, fail if error */
	inst->ldmsapp_mutex = sem_open(inst->sem_name, O_CREAT, 0666, 1);
	if (inst->ldmsapp_mutex == SEM_FAILED) {
		snprintf(ebuf, ebufsz, "Semaphore open error: %s\n",
			strerror(errno));
		return errno;
	}

	/* Reset the lock to 1 -- JEC: why? in case it existed... */
	while (sem_trywait(inst->ldmsapp_mutex) >= 0)
		;
	sem_post(inst->ldmsapp_mutex);

	/* open shared memory segment */
	inst->shmem_fd = shm_open(inst->shmem_name, O_CREAT|O_RDWR, 0666);
	if (inst->shmem_fd == -1) {
		snprintf(ebuf, ebufsz, "Shmem open/create error: %s\n",
			 strerror(errno));
		return errno;
	}
	/* Set size of shared memory segment */
	/* This is just the max possible size, for now; must fix */
	rc = ftruncate(inst->shmem_fd, sizeof(shmem_header_t) +
		       MAX_PROCESSES*MAX_METRICS*(sizeof(app_metric_t)+256));
	/* Get stats (size) of shared memory segment and check */
	if (fstat(inst->shmem_fd, &inst->shmem_stat) == -1) {
		snprintf(ebuf, ebufsz, "Shmem stat error: %s\n",
			 strerror(errno));
		return errno;
	}
	if (inst->shmem_stat.st_size < 256 /* TODO CHECK ACTUAL SIZE */) {
		snprintf(ebuf, ebufsz, "Shmem size too small: %zd\n",
			 inst->shmem_stat.st_size);
		return EINVAL;
	}

	/* map shared memory into our memory */
	inst->shmem_header = mmap(0, inst->shmem_stat.st_size,
				  PROT_READ|PROT_WRITE, MAP_SHARED,
				  inst->shmem_fd, 0);
	if (inst->shmem_header == MAP_FAILED) {
		snprintf(ebuf, ebufsz, "Shmem mmap error: %s\n",
			 strerror(errno));
		return errno;
	}

	/* Now set up all the sampler-specified data in shared memory */
	actual_shmem_size = create_shmem_data(inst, set);
	/* TODO do another ftruncate here to shrink shmem to actual size */

	/* BACK to regular config stuff here */

	INST_LOG(inst, LDMSD_LDEBUG, "Done configuring.\n");
	return 0;
}

static
void appinfo_del(ldmsd_plugin_inst_t pi)
{
	appinfo_inst_t inst = (void*)pi;

	/* set is deleted by sampler_del() */
	if (inst->metrics_optstr)
		free(inst->metrics_optstr);
	if (inst->shmem_header != MAP_FAILED)
		munmap(inst->shmem_header, inst->shmem_stat.st_size);
	if (inst->shmem_fd != -1) {
		close(inst->shmem_fd);
		unlink(inst->shmem_name);
	}
	if (inst->ldmsapp_mutex != SEM_FAILED) {
		sem_close(inst->ldmsapp_mutex);
		unlink(inst->sem_name);
	}
	/* shmem_name and sem_name are static, hence must not be freed */
}

static
json_entity_t appinfo_query(ldmsd_plugin_inst_t pi, const char *q)
{
	int i;
	json_entity_t attr, envs, str;
	json_entity_t result = ldmsd_sampler_query(pi, q);
	if (!result)
		return NULL;

	/* Override only the 'env' query */
	if (0 != strcmp(q, "env"))
		return result;

	const char *env_names[] = {
			"PBS_JOBID",
			"SLURM_JOB_ID",
			NULL
	};
	envs = json_entity_new(JSON_LIST_VALUE);
	if (!envs)
		goto enomem;
	str = json_entity_new(JSON_STRING_VALUE, "env");
	if (!str) {
		json_entity_free(envs);
		goto enomem;
	}
	attr = json_entity_new(JSON_ATTR_VALUE, str, envs);
	if (!attr) {
		json_entity_free(envs);
		json_entity_free(str);
		goto enomem;
	}

	for (i = 0; env_names[i]; i++) {
		str = json_entity_new(JSON_STRING_VALUE, env_names[i]);
		if (!str) {
			json_entity_free(attr);
			goto enomem;
		}
		json_item_add(envs, str);
	}
	json_attr_add(result, attr);
	return result;

enomem:
	ldmsd_plugin_qjson_err_set(result, ENOMEM, "Out of memory");
	return result;
}

static
int appinfo_init(ldmsd_plugin_inst_t pi)
{
	appinfo_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	/* override update_schema() and update_set() */
	samp->update_schema = appinfo_update_schema;
	samp->update_set = appinfo_update_set;
	samp->base.query = appinfo_query;

	return 0;
}

static
struct appinfo_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "appinfo",

		/* Common Plugin APIs */
		.desc   = appinfo_desc,
		.help   = appinfo_help,
		.init   = appinfo_init,
		.del    = appinfo_del,
		.config = appinfo_config,

	},
	.shmem_fd   = -1,
	.shmem_name = "/shm_ldmsapp",
	.sem_name   = "/sem_ldmsapp",
};

ldmsd_plugin_inst_t new()
{
	appinfo_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
