/* -*- c-basic-offset: 8 -*-
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
/**
 * \file slurmjobid.c
 * \brief /var/run/ldms.slurm.jobid shared data provider.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_slurmjobid.h"


/* This sampler relies on the resource manager to
leave a note in JOBID_FILE about the current job, or 0 if the
node is not reserved. If this file is absent, the sampler
will run, reporting 0 jobid and logging complaints.

The file checked is by default:
	/var/run/ldms.slurm.jobid
exists if the next line is added to slurm-prolog
	echo $SLURM_JOBID > /var/run/ldms.slurm.jobid
and the next line is added to slurm-epilog
	echo "0" > /var/run/ldms.slurm.jobid
On some slurm 2.3.x systems, these are in /etc/nodestate/bin/.
On systems with non-slurm reservations or shared reservations,
this sampler may need to be cloned and modified.

The functionality is generic to files containing a single
integer in ascii encoding.
*/
static const char *JOBID_FILE="/var/run/ldms.slurm.jobid";
static const char *JOBID_COLNAME = SLURM_JOBID_METRIC_NAME;
#define JOBID_LINE_MAX 64  //max # of chars in lbuf

static char *procfile = NULL;
static char *metric_name = NULL;
static ldms_set_t set;
static FILE *mf;
static ldms_metric_t *metric_table;
static ldmsd_msg_log_f msglog;
static uint64_t comp_id = UINT64_MAX;
static char *qc_dir = NULL;
// data for resource_manager users
// update at every sample.
static resource_info_manager rim = NULL;
static uint64_t last_jobid = 0;
static uint64_t last_generation = 0;

#ifdef HAVE_QC_SAMPLER
static int qc_file = -1;
static int get_qc_file(const char *qc_dir, int *qc_file);
#endif

static
int slurm_rim_update(struct resource_info *self, enum rim_task t, void * tinfo);

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, metric_count;

	metric_count = 0;
	tot_meta_sz = 0;
	tot_data_sz = 0;

	/* Set size calculation. */
	rc = ldms_get_metric_size(metric_name, LDMS_V_U64,
				  &meta_sz, &data_sz);
	if (rc)
		return rc;
	tot_meta_sz += meta_sz;
	tot_data_sz += data_sz;
	metric_count++;

	/* Create the metric set */
	rc = ENOMEM;
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;
	/*
	 * Process the file to init jobid.
	 */
	mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMS_LINFO,"Could not open the jobid file '%s'\n", procfile);
	} else {
		fclose(mf);
		mf = NULL;
	}

	int metric_no = 0;
	metric_table[metric_no] = ldms_add_metric(set, metric_name, LDMS_V_U64);
	if (!metric_table[metric_no]) {
		rc = ENOMEM;
		goto err;
	}
	ldms_set_user_data(metric_table[metric_no], comp_id);
	metric_no++;

	return 0;

 err:
	ldms_destroy_set(set);
	return rc;
}

/**
 * \brief Configuration
 *
 * config name=slurmjobid component_id=<comp_id> set=<setname>
 *        qc_log_dir=<qc_log_directory>
 *     comp_id     The component id value.
 *     file  The file to find slurm job id in on 1st line in ascii.
 *     colname  The metric name to use in output.
 *     setname     The set name.
 *     qc_log_dir  The QC data file directory.
 *                 This option is only relevant if --enable-qc-sampler
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;

	value = av_value(avl, "file");
	if (value) {
		procfile = strdup(value);
		if (!procfile) {
			msglog(LDMS_LERROR,"slurmjobid no memory\n");
			return ENOMEM;
		}
	}

	value = av_value(avl, "colname");
	if (value) {
		metric_name = strdup(value);
		if (!metric_name) {
			msglog(LDMS_LERROR,"slurmjobid no memory\n");
			return ENOMEM;
		}
	}
	metric_name = (char *)JOBID_COLNAME;
	msglog(LDMS_LDEBUG,"Slurm jobid file is '%s'\n", procfile);

	value = av_value(avl, "component_id");
	if (value) {
		char *endp = NULL;
		errno = 0;
		comp_id = strtoull(value, &endp, 0);
		if (endp == value || errno) {
			msglog(LDMS_LERROR,"Fail parsing component_id '%s'\n", 
				value);
			return EINVAL;
		}
	}

	int err;
	err = register_resource_info(rim, JOBID_COLNAME, "node", NULL,
		slurm_rim_update, NULL);
	if (err) {
		msglog(LDMS_LERROR,"Exporting '%s' failed\n", 
			JOBID_COLNAME);
		return err;
	}

	/* open a qc data file to store this sample.          */
	/* if user does not specify qc_log_dir, then it's ok. */
#ifdef HAVE_QC_SAMPLER
	value = av_value(avl, "qc_log_dir");
	if (value) {
		int ec;  //error code

		qc_dir = strdup(value);
		/* try to open the qc data file */
		ec = get_qc_file(qc_dir, &qc_file);
		if (ec!=0)
			return(ec);
	}
#endif

	value = av_value(avl, "set");
	if (value)
		create_metric_set(value);

	return 0;
}

static ldms_set_t get_set()
{
	return set;
}


/* as a policy matter, the missing file has the value 0, not an error. */
static int sample(void)
{
	int metric_no;
	char *s;
	uint64_t metric_value = 0;
	char lbuf[JOBID_LINE_MAX];
	union ldms_value v;
#ifdef HAVE_QC_SAMPLER
	/* gettimeOfday emits 2 long int                      */
	/* on a word width of 64 bits, a long int is 21 chars */
	const int qc_len_date_and_time = 32+strlen(",")+32+1;
	const int qc_len_buffer =
		  5 //strlen("#time")
		+ 1 //strlen(",")
		+ qc_len_date_and_time
		+ 1 //strlen(",")
		+ qc_len_date_and_time
		+ 1;  //string terminator
	char qc_date_and_time[qc_len_date_and_time];
	char qc_buffer[qc_len_buffer];
#endif

	if (!set) {
		msglog(LDMS_LDEBUG,"slurmjobid: plugin not initialized\n");
		return EINVAL;
	}
	ldms_begin_transaction(set);

	metric_no = 0;

#ifdef HAVE_QC_SAMPLER
	/* get current date & time, write to qc file */
	if (qc_file != -1) {
		struct timeval qc_time;

		gettimeofday(&qc_time, NULL);
		snprintf(qc_date_and_time,
			 qc_len_date_and_time,
			 "%ld.%06ld",
			qc_time.tv_sec, qc_time.tv_usec);
		qc_date_and_time[qc_len_date_and_time-1]='\0';
		snprintf(qc_buffer,qc_len_buffer,
			"%s,%s,%s\n",
			"#time",
			qc_date_and_time,
			qc_date_and_time);
		qc_buffer[qc_len_buffer-1] = '\0';
		write(qc_file,	qc_buffer, strlen(qc_buffer));
	}
#endif

	/* unlike device drivers, the file for jobid gets replaced
	   and must be reread every time. In production clusters, it's
	   almost certain in a local ram file system, so this is fast
	   enough compared to contacting a slurm daemon.
	*/
	mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMS_LINFO,"Could not open the jobid file '%s'\n", procfile);
		metric_value = 0;
	} else {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s) {
			msglog(LDMS_LINFO,"Fail reading jobid file '%s'\n", procfile);
			metric_value = 0;
		} else {
			char *endp = NULL;
			errno = 0;
			metric_value = strtoull(lbuf, &endp, 0);
			if (endp == lbuf || errno) {
				metric_value = 0;
				msglog(LDMS_LERROR,
					"Fail parsing '%s' from %s\n",
					lbuf, procfile);
			}
		}
		fclose(mf);
		mf = NULL;
	}

	v.v_u64 = metric_value;
	ldms_set_metric(metric_table[metric_no], &v);
	last_jobid = metric_value;
	last_generation++;

#ifdef HAVE_QC_SAMPLER
	/* write a metric to the qc data file */
	if (qc_file != -1) {
		snprintf(qc_buffer,qc_len_buffer,
			"%s,%s,%" PRIu64 "\n",
			ldms_get_metric_name(metric_table[metric_no]),
			qc_date_and_time,
			v.v_u64);
		qc_buffer[qc_len_buffer-1] = '\0';
		write(qc_file, qc_buffer, strlen(qc_buffer));
	}
#endif

	metric_no++;

#ifdef HAVE_QC_SAMPLER
	/* flush qc file */
	fsync(qc_file);
#endif

	ldms_end_transaction(set);
	return 0;
}

/*&
 Value change is driven by daemon, not by shared data requests.
 No configuration or private data is needed for this case.
*/
static
int slurm_rim_update(struct resource_info *self, enum rim_task t,
	void * tinfo)
{
	(void)tinfo;
        switch (t) {
        case rim_init:
                self->v.u64 = last_jobid;
                self->generation = last_generation;
                return 0;
        case rim_update: {
                self->v.u64 = last_jobid;
                self->generation = last_generation;
                return 0;
        }
        case rim_final:
                break;
        default:
                return EINVAL;
        }
        return 0;
}

static void term(void)
{
	if (set)
		ldms_destroy_set(set);
	set = NULL;

	if (qc_dir != NULL)
		free(qc_dir);
	qc_dir = NULL;

	if (metric_name && metric_name != JOBID_COLNAME ) {
		free(metric_name);
	}
	if ( procfile && procfile != JOBID_FILE ) {
		free(procfile);
	}
}

static const char *usage(void)
{
	return "config name=slurmjobid component_id=<comp_id> set=<setname> [file=<jobidfilename>]"
			"qc_log_dir=<qc_log_directory>\n"
			"    comp_id     The component id value.\n"
			"    setname     The set name.\n"
			"    jobidfilename     Optional file to read.\n"
			"    qc_log_dir  The QC data file directory.\n";
}

static struct ldmsd_sampler slurmjobid_plugin = {
	.base = {
		.name = "slurmjobid",
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
	rim = ldms_get_rim();
	
	return &slurmjobid_plugin.base;
}

#ifdef HAVE_QC_SAMPLER
static void close_qc_file()
{
	close(qc_file);
	qc_file = -1;
	free(qc_dir);
	qc_dir = NULL;
}

/**
 * Open a QC output file.
 * The name of the file is QC_[hostname]_[comp_id]_[random chars].txt.
 * @param qc_dir The directory that will contain the file.
 * @param qc_file sends the file descriptor to the caller
 * If qc_dir is NULL, then the errno is returned.
 * @return the errno
 */
static int get_qc_file(const char *qc_dir, int *qc_file)
{
	struct stat s;                     //need for stat()
	char hostname[HOST_NAME_MAX+1];    //hostname
	char *qc_filename;                 //full path name of qc file
	int err;                           //error codes
	char *ptr;                         //used for string parsing
	int len;

	/* used to send file descriptor back to the caller */
	*qc_file = -1;

	/* does path exist?       */
	/* is path a directory?   */
	if (qc_dir==NULL) {
		errno = ENOENT;
		return(errno);
	}
	err = stat(qc_dir, &s);
	if (-1 == err) {
		errno = ENOENT;
		return (errno);
	} else {
		if (!S_ISDIR(s.st_mode)) {
			errno = ENOTDIR;
			return (errno);
		}
	}

	/* get hostname */
	if (gethostname(hostname, HOST_NAME_MAX) != 0)
		strcpy(hostname,"unknown");
	hostname[HOST_NAME_MAX] = '\0';

	/* truncate hostname to first dot */
	ptr = strchr(hostname,'.');
        if (ptr!=NULL)
		*ptr = '\0';

	/* filename is QC_[hostname]_[comp_id]_[random chars].txt */
        len =     strlen(qc_dir)
        	+ 4  //strlen("/QC_")
        	+ strlen(hostname)
        	+1   //strlen("_")
        	+32  //comp_id is PRIu64
        	+12  //strlen("_slurmjobid_")
        	+6  //strlen("XXXXXX")
        	+4  //strlen(".txt")
        	+1; //string terminator
        qc_filename = (char *)malloc(len);
	snprintf(qc_filename, len, "%s/QC_%s_%" PRIu64 "_slurmjobid_%s.txt",
		 qc_dir,                        //user specified path
		 hostname,                      //which node in cluster????
		 comp_id,  //ldms comp_id
		 "XXXXXX");                     //random chars
        qc_filename[len-1] = '\0';

        /* open the file, save the file handle                    */
        /* NOTE:  using random chars to get a unique filename     */
	*qc_file = mkstemps(qc_filename, 4);
	free(qc_filename);
	if (*qc_file == -1) {
		errno = EIO;
		return (errno);
	}

	return (errno);
}
#endif

