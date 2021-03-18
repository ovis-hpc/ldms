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
#include <sys/errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <pthread.h>
#include <dlfcn.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <netdb.h>
#include <regex.h>
#include <pwd.h>
#include <unistd.h>
#include <mmalloc/mmalloc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <inttypes.h>

#include "ovis_util/os_util.h"
#include "ldms.h"
#include "ldms_appinfo.h"
#include "ldms_appinfo_shm.h" /* shared memory structs for appinfo sampler */

/* GLOBALS */

static enum {
  LDMSAPP_STAT_OK=0, LDMSAPP_STAT_ERROR, LDMSAPP_STAT_DISABLED
  } ldmsapp_status; /** status of ldmsapp **/
static int silent=0; /** don't print error message if !=0 **/

/*
 * Shared memory global variables
 */
static int shmem_fd; /** file descriptor for shmem **/
static struct stat shmem_stat;
static shmem_header_t *shmem_header; /** pointer to beginning of shmem **/
static void *shmem_data; /** pointer to this process' shmem data segment **/
static int shmem_pmid; /** index into process metadata array **/
static char *shmem_name = "/shm_ldmsapp"; /** shared memory name **/
static char *sem_name = "/sem_ldmsapp"; /** ldmsapp_mutex name **/
static sem_t *ldmsapp_mutex; /** mutex for accessing shmem **/

/*
 * Macros for standard code in all of the reporting functions
 * - move these to top once we decide they are correct, and then
 *	simplify all reporting functions to use them
 * - these perhaps should all be in a function rather than
 *	in macros -- think about it
 */
#define REPORT_METRIC_SETUP(shmam,mid,type,msg) \
	if (ldmsapp_status != LDMSAPP_STAT_OK) \
		return -1; \
	if ((mid) < 0 || (mid) >= shmem_header->metric_count) { \
		if (!silent) fprintf(stderr,\
			"LDMS: No such metric number (%d): \n", (mid)); \
		return -1; \
	} \
	/* (just wait) if (sem_trywait(ldmsapp_mutex) < 0) return -1; */ \
	sem_wait(ldmsapp_mutex); \
	(shmam) = (app_metric_t *) (shmem_data + \
			shmem_header->metric_offset[(mid)]); \
	if (!(shmam)) { \
		/* some error? must ensure no access to null ptr */ \
		sem_post(ldmsapp_mutex); \
		return -1; \
	} \
	if ((shmam)->vtype != (type)) { \
		/* shouldn't really print here, either; only for debugging */ \
		if (!silent) fprintf(stderr,"LDMSAPP_LERROR: Metric is not of "\
					msg " type (%d,%d)\n", \
					(shmam)->vtype,(type)); \
		/* not sure if you want to return -1 here or not? */ \
		sem_post(ldmsapp_mutex); \
		return -1; \
	}

#define REPORT_METRIC_TEARDOWN() \
	sem_post(ldmsapp_mutex); \
	shmem_header->proc_metadata[shmem_pmid].status = NEEDSAMPLED;

/**
 * Initialize the ldmsapp interface. This initialization assumes that
 * LDMS and an LDMS appinfo sampler are running on the node this runs
 * on; if not, the opening of shared memory and a semaphore will fail
 * and the interface will be disabled.
 * @param ldmsapp_enable is LDMSAPP_ENABLE for normal initialization or
 *			    LDMSAPP_DISABLE for disabling init.
 * @param appid is a unique application id #
 * @param jobid is the job id # (0 means look at PBS_JOBID env var)
 * @param rank is the MPI (or other) process rank id
 * @param psilent is nonzero to suppress any stderr output messages
 **/
int ldmsapp_initialize(int ldmsapp_enable, int appid, int jobid,
			int rank, int psilent)
{
	int i;
	/* initialize as failed */
	shmem_header = MAP_FAILED;
	ldmsapp_mutex = SEM_FAILED;
	shmem_fd = -1;

	if (ldmsapp_enable == LDMSAPP_DISABLE) {
		ldmsapp_status = LDMSAPP_STAT_DISABLED;
		return 0; /* done for disabled case */
	} else {
		ldmsapp_status = LDMSAPP_STAT_OK;
	}
	silent = psilent;

	/* Set up job id if needed */
	if (jobid == 0) {
		char *js = getenv("PBS_JOBID");
		if (js) jobid = (int) strtol(js, 0, 10);
		else {
			js = getenv("SLURM_JOB_ID");
			if (js) jobid = (int) strtol(js, 0, 10);
		}
	}

	/* open existing shared memory semaphore, fail if doesn't exist */
	ldmsapp_mutex = sem_open(sem_name, 0);
	if (ldmsapp_mutex == SEM_FAILED) {
		if (!silent) fprintf(stderr,"LDMSAPP_LERROR: "
				"Semaphore open error: %s\n",
				STRERROR(errno));
		goto err;
	}

	/* Application should not reset semaphore here, the sampler does */

	/* open shared memory segment */
	shmem_fd = shm_open(shmem_name, O_RDWR, 0);
	if (shmem_fd == -1) {
		if (!silent) fprintf(stderr,"LDMSAPP_LERROR: "
				"Shmem open error: %s\n",
				STRERROR(errno));
		goto err;
	}
	/* Get stats (size) of shared memory segment */
	if (fstat(shmem_fd, &shmem_stat) == -1) {
		if (!silent) fprintf(stderr,"LDMSAPP_LERROR: "
				"Shmem stat error: %s\n",
				STRERROR(errno));
		goto err;
	}
	if (shmem_stat.st_size < sizeof(shmem_header_t)) {
		if (!silent) fprintf(stderr,"LDMSAPP_LERROR: "
			"Shmem size too small: %ld\n",shmem_stat.st_size);
		goto err;
	}
	/* map shared memory into our memory */
	shmem_header = (shmem_header_t *) mmap(0, shmem_stat.st_size,
				PROT_READ|PROT_WRITE, MAP_SHARED, shmem_fd, 0);
	if (shmem_header == MAP_FAILED) {
		if (!silent) fprintf(stderr,"LDMSAPP_LERROR: "
				"Shmem mmap error: %s\n",
				STRERROR(errno));
		goto err;
	}

	/* JEC: allocate ourselves one of the process data struct blocks */
	sem_wait(ldmsapp_mutex);
	if (shmem_header->num_registered >= shmem_header->max_processes) {
		sem_post(ldmsapp_mutex);
		if (!silent) fprintf(stderr,"LDMSAPP_LERROR: "
			"no procs left: %d\n", shmem_header->num_registered);
		goto err;
	}
	/* find open process data block */
	for (i=0; i < shmem_header->max_processes; i++)
		if (shmem_header->proc_metadata[i].status == AVAILABLE)
			break;
	if (i >= shmem_header->max_processes) {
		sem_post(ldmsapp_mutex);
		if (!silent) fprintf(stderr,"LDMSAPP_LERROR: "
					"no procs left (2): %d\n", i);
		goto err;
	}
	shmem_pmid = i;
	if (shmem_header->proc_metadata[shmem_pmid].status != AVAILABLE) {
		sem_post(ldmsapp_mutex);
		if (!silent) fprintf(stderr,"LDMSAPP_LERROR: "
				"proc md alloc!: %d\n",	shmem_pmid);
		goto err;
	}
	shmem_header->proc_metadata[shmem_pmid].status = ALLOCATED;
	shmem_header->proc_metadata[shmem_pmid].app_id = appid;
	shmem_header->proc_metadata[shmem_pmid].job_id = jobid;
	shmem_header->proc_metadata[shmem_pmid].process_id = getpid();
	shmem_header->proc_metadata[shmem_pmid].rank_id = rank;
	shmem_header->proc_metadata[shmem_pmid].start_offset =
		sizeof(shmem_header_t) + (shmem_header->num_registered *
					shmem_header->data_block_size);
	/* calc position of next avail data block */
	shmem_data = (((void*) shmem_header) + sizeof(shmem_header_t) +
		(shmem_header->num_registered * shmem_header->data_block_size));
	shmem_header->num_registered++;
	sem_post(ldmsapp_mutex);
	if (shmem_data+shmem_header->data_block_size >
		((void*) shmem_header) + shmem_stat.st_size) {
		if (!silent) fprintf(stderr,"LDMSAPP_LERROR: "
			"Shmem too small! %d\n", shmem_header->num_registered);
		goto err;
	}

	/*
	* return -2 if the parsed failed for some user metrics
	* otherwise return 0
	* JEC: parse error on sampler side should result in no shmem!
	*/
	return 0;

err:
	/* cleanup any successful progress that might have happened */
	if (shmem_header != MAP_FAILED) munmap(shmem_header,shmem_stat.st_size);
	if (shmem_fd != -1) close(shmem_fd);
	if (ldmsapp_mutex != SEM_FAILED) sem_close(ldmsapp_mutex);
	ldmsapp_status = LDMSAPP_STAT_ERROR;
	return -1;
}

/**
 * Cleanup and shutdown LDMS appinfo interface. Mainly just cleans up
 * the shared memory stuff.
 **/
int ldmsapp_finalize()
{
	int i;
	/* if never initialized, don't clean up */
	if (ldmsapp_status != LDMSAPP_STAT_OK)
		return -1;
	/*
	 * Wait for last data to be sampled, if there is some
	 * - but provide a hard-counter (10 seconds) failsafe override
	 */
	i = 0;
	while (i<80 && shmem_header->proc_metadata[shmem_pmid].status == NEEDSAMPLED) {
		usleep(125000);  /* 1/8 of a second */
		i++;
	}
	/* clean up our process record */
	sem_wait(ldmsapp_mutex);
	shmem_header->proc_metadata[shmem_pmid].app_id = 0;
	shmem_header->proc_metadata[shmem_pmid].job_id = 0;
	shmem_header->proc_metadata[shmem_pmid].process_id = 0;
	shmem_header->proc_metadata[shmem_pmid].rank_id = 0;
	shmem_header->proc_metadata[shmem_pmid].start_offset = 0;
	shmem_header->proc_metadata[shmem_pmid].status = AVAILABLE;
	shmem_header->num_registered--;
	sem_post(ldmsapp_mutex);
	/* release resources */
	munmap(shmem_header,shmem_stat.st_size);
	close(shmem_fd);
	sem_close(ldmsapp_mutex);
	/* JEC: don't unlink on app side, this
	 * would delete it so that it cannot be reused!
	 */
	/* shm_unlink(shm_name); */
	/* shm_unlink(shrm_name_meta); */
	return 0;
}

/*
 * Metric reporting functions below are for each supported
 * metric datatype. They all use the two macros at the top
 * to keep their source code short (maybe the macros should
 * be changed into functions).
 */

int ldmsapp_report_metric_u8(int metric_id, uint8_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_U8, "uint8")
	shm_metric->value.v_u8 = value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_report_metric_u16(int metric_id, uint16_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_U16, "uint16")
	shm_metric->value.v_u16 = value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_report_metric_u32(int metric_id, uint32_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_U32, "uint32")
	shm_metric->value.v_u32 = value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_report_metric_u64(int metric_id, uint64_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_U64, "uint64")
	shm_metric->value.v_u64 = value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_report_metric_s8(int metric_id, int8_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_S8, "int8")
	shm_metric->value.v_s8 = value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_report_metric_s16(int metric_id, int16_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_S16, "int16")
	shm_metric->value.v_s16 = value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_report_metric_s32(int metric_id, int32_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_S32, "int32")
	shm_metric->value.v_s32 = value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_report_metric_s64(int metric_id, int64_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_S64, "int64")
	shm_metric->value.v_s64 = value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_report_metric_float(int metric_id, float value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_F32, "real32")
	shm_metric->value.v_f = value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_report_metric_double(int metric_id, double value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_D64, "real64")
	shm_metric->value.v_d = value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_inc_metric_u8(int metric_id, uint8_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_U8, "uint8")
	shm_metric->value.v_u8 += value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_inc_metric_u16(int metric_id, uint16_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_U16, "uint16")
	shm_metric->value.v_u16 += value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_inc_metric_u32(int metric_id, uint32_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_U32, "uint32")
	shm_metric->value.v_u32 += value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_inc_metric_u64(int metric_id, uint64_t value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_U64, "uint64")
	shm_metric->value.v_u64 += value;
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

int ldmsapp_report_metric_str(int metric_id, char *value)
{
	app_metric_t *shm_metric;
	REPORT_METRIC_SETUP(shm_metric, metric_id, LDMS_V_CHAR_ARRAY, "char_array")
	strncpy(shm_metric->value.a_char, value, strlen(value)+1);
					 /* JEC IMPORTANT! ---^^^ */
	shm_metric->status = 1;
	REPORT_METRIC_TEARDOWN();
	return 0;
}

/**
 * Get all metric names. Do NOT free the returned data pointer!
 * NOT thread-safe, although names should always be the same.
 **/
char** ldmsapp_get_metric_names()
{
	static char metrics[MAX_METRICS+1][64]; /* not thread safe! */
	int i;
	app_metric_t *am;
	am = shmem_data;
	memset(metrics, 0, sizeof(metrics)); /* zero out for safety */
	if (ldmsapp_status >= 1) return NULL;
	sem_wait(ldmsapp_mutex);
	for (i = 0; i < shmem_header->metric_count; i++) {
		am = (app_metric_t *)
			(shmem_data + shmem_header->metric_offset[i]);
		strncpy(metrics[i], am->name, strlen(am->name)+1);
	}
	sem_post(ldmsapp_mutex);
	metrics[i][0] = '\0'; /* indicate end with empty string (redundant) */
	return (char**) metrics;
}

/**
 * Get metric name given its ID.
 * @param id is the absolute metric id (include base_set ids)
 **/
char* ldmsapp_get_metric_name_by_id(int id)
{
	static char name[64];
	int i;
	app_metric_t *am;
	am = shmem_data;
	if (ldmsapp_status >= 1) return NULL;
	sem_wait(ldmsapp_mutex);
	am = (app_metric_t *) (shmem_data + shmem_header->metric_offset[id]);
	strncpy(name, am->name, strlen(am->name)+1);
	sem_post(ldmsapp_mutex);
	return name;
}

/**
 * Get current count of metrics.
 **/
int ldmsapp_get_metrics_count()
{
	if (ldmsapp_status >= 1) return -1;
	return shmem_header->metric_count;
}
