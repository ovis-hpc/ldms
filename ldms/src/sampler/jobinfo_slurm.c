/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2017 Sandia Corporation. All rights reserved.
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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <strings.h>
#include <string.h>
#include <pwd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include <slurm/spank.h>

#include "jobinfo.h"

/*
 * This is a SLURM SPANK plugin that writes to a jobinfo datafile. This
 * datafile is read by the LDMS jobinfo sampler that populates a jobinfo
 * metric set.
 */

SPANK_PLUGIN(jobinfo, 1)
struct jobinfo		jobinfo;

int
hook_write_jobinfo(struct jobinfo *jobinfo)
{
	int			rc = 0;
	FILE			*f;
	char			*datafile;

	datafile = getenv("LDMS_JOBINFO_DATA_FILE");
	if (datafile == NULL)
		datafile = LDMS_JOBINFO_DATA_FILE;

	f = fopen(datafile, "w");
	if (f == NULL) {
		return errno;
	}

	rc = fwrite(jobinfo, sizeof(*jobinfo), 1, f);
	if (rc != 1) {
		rc = EIO;
	}

	fclose(f);

	return rc;
}

/*
 * Called by SLURM just before job start.
 */
int
slurm_spank_init(spank_t sh, int argc, char *argv[])
{
	spank_context_t 	context;
	spank_err_t		err;
	char			buf[512];
	struct passwd		*pw;

	context = spank_context();
	if (context != S_CTX_REMOTE)
		return ESPANK_SUCCESS;

	bzero(&jobinfo, sizeof(jobinfo));

	err = spank_get_item(sh, S_JOB_UID, &jobinfo.user_id);
	if (err != ESPANK_SUCCESS) {
		return ESPANK_SUCCESS;
	}

	err = spank_get_item(sh, S_JOB_ID, &jobinfo.job_id);
	if (err != ESPANK_SUCCESS) {
		return ESPANK_SUCCESS;
	}

	err = spank_getenv(sh, "SLURM_JOB_NAME", buf, sizeof(buf));
	if (err == ESPANK_SUCCESS) {
		strncpy(jobinfo.job_name, buf, sizeof(jobinfo.job_name));
	}
	
	pw = getpwuid(jobinfo.user_id);
	if (pw != NULL) {
		strncpy(jobinfo.job_user, pw->pw_name, sizeof(jobinfo.job_user));
	}

	jobinfo.job_status = JOBINFO_JOB_STARTED;
	jobinfo.job_start = time(NULL);

	hook_write_jobinfo(&jobinfo);

	return ESPANK_SUCCESS;
}

/*
 * Called by SLURM just after job exit.
 */
int
slurm_spank_task_exit(spank_t sh, int argc, char *argv[])
{
	spank_context_t 	context;
	int			val;

	context = spank_context();
	if (context != S_CTX_REMOTE)
		return ESPANK_SUCCESS;

	spank_get_item(sh, S_TASK_EXIT_STATUS, &val);
	jobinfo.job_exit_status = WEXITSTATUS(val);

	spank_get_item(sh, S_TASK_ID, &val);
	jobinfo.app_id = val;

	jobinfo.job_status = JOBINFO_JOB_EXITED;
	jobinfo.job_end = time(NULL);
	hook_write_jobinfo(&jobinfo);

	return ESPANK_SUCCESS;
}
