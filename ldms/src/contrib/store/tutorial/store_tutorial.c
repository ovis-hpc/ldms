/**
 * Copyright (c) 2019 National Technology & Engineering Solutions
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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#define PNAME "store_tutorial"
#define MAXSCHEMA 5

static ovis_log_t mylog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

#define LOGFILE "/var/log/store_tutorial.log"

struct tutorial_store_handle {
	char *path;	     /* full path will be path/container/schema */
	FILE *file;
	pthread_mutex_t lock;
};

/* TUT: keeping these because in a more complex scenario would use the
 * values for flush and searching for a store to close */
static struct tutorial_store_handle* tstorehandle[MAXSCHEMA];
static int numschema = 0;
static char* root_path;
static pthread_mutex_t cfg_lock;

/**
 * \brief Configuration
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char* s;
	int rc = 0;

	pthread_mutex_lock(&cfg_lock);
	s = av_value(avl, "path");
	if (!s){
	   ovis_log(mylog, OVIS_LDEBUG, PNAME ": missing path in config\n");
	   rc = EINVAL;
	} else {
	  root_path = strdup(s);
	  ovis_log(mylog, OVIS_LDEBUG, PNAME ": setting root_path to '%s'\n", root_path);
	}

	pthread_mutex_unlock(&cfg_lock);

	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "    config name=store_tutorial path=<path> \n"
		"         - Set the root path for the storage of csvs and some default parameters\n"
		"         - path      The path to the root of the csv directory\n"
		;
}

static ldmsd_store_handle_t
open_store(ldmsd_plug_handle_t s, const char *container, const char* schema,
		struct ldmsd_strgp_metric_list *list)
{
	struct tutorial_store_handle *s_handle = NULL;
	int rc = 0;
	char* path = NULL;
	char* dpath = NULL;

	pthread_mutex_lock(&cfg_lock);
	if (!root_path) {
	     ovis_log(mylog, OVIS_LERROR, PNAME ": config not called. cannot open.\n");
	     return NULL;
	}

	if (numschema == (MAXSCHEMA-1)){
	     ovis_log(mylog, OVIS_LERROR, PNAME ": Exceeded MAXSCHEMA. cannot open.\n");
	     return NULL;
	}


	size_t pathlen = strlen(root_path) + strlen(schema) + strlen(container) + 8;
	path = malloc(pathlen);
	if (!path)
	   goto out;
	dpath = malloc(pathlen);
	if (!dpath)
	   goto out;
	sprintf(path, "%s/%s/%s", root_path, container, schema);
	sprintf(dpath, "%s/%s", root_path, container);

	ovis_log(mylog, OVIS_LDEBUG, PNAME ": schema '%s' will have file path '%s'\n", schema, path);

	s_handle = calloc(1, sizeof *s_handle);
	if (!s_handle)
		goto err0;

	pthread_mutex_init(&s_handle->lock, NULL);
	pthread_mutex_lock(&s_handle->lock);
	s_handle->path = strdup(path);

	/* create path if not already there. */
	rc = mkdir(dpath, 0777);
	if ((rc != 0) && (errno != EEXIST)) {
		ovis_log(mylog, OVIS_LERROR, PNAME ": Failure %d creating directory '%s'\n",
			 errno, dpath);
		goto err1;
	}

	s_handle->file = fopen_perm(s_handle->path, "a+", LDMSD_DEFAULT_FILE_PERM);
	if (!s_handle->file){
		ovis_log(mylog, OVIS_LERROR, PNAME ": Error %d opening the file %s.\n",
		       errno, s_handle->path);
		goto err1;
	}
	pthread_mutex_unlock(&s_handle->lock);

	tstorehandle[numschema++] = s_handle;

	goto out;

err1:
	fclose(s_handle->file);
	s_handle->file = NULL;
	pthread_mutex_unlock(&s_handle->lock);
	pthread_mutex_destroy(&s_handle->lock);

err0:

	free(s_handle);
	s_handle = NULL;
out:

	free(path);
	free(dpath);
	pthread_mutex_unlock(&cfg_lock);
	return s_handle;
}

static int store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh,
		 ldms_set_t set, int *metric_array, size_t metric_count)
{
	const struct ldms_timestamp _ts = ldms_transaction_timestamp_get(set);
	const struct ldms_timestamp *ts = &_ts;
	const char* pname;
	struct tutorial_store_handle *s_handle;
	int i;
	int rc;

	s_handle = _sh;
	if (!s_handle)
		return EINVAL;

	pthread_mutex_lock(&s_handle->lock);
	if (!s_handle->file){
		ovis_log(mylog, OVIS_LERROR, PNAME ": Cannot insert values for <%s>: file is NULL\n",
		       s_handle->path);
		pthread_mutex_unlock(&s_handle->lock);
		return EPERM;
	}

	// TUT: would print header here if it were the first time

	fprintf(s_handle->file, "%"PRIu32".%06"PRIu32 ",%"PRIu32,
		ts->sec, ts->usec, ts->usec);
	fprintf(s_handle->file, ",");
	pname = ldms_set_producer_name_get(set);
	if (pname != NULL)
		fprintf(s_handle->file, "%s", pname);


	for (i = 0; i != metric_count; i++) {
		enum ldms_value_type metric_type = ldms_metric_type_get(set, metric_array[i]);
		//TUT: only supporting U64
		switch (metric_type){
		case LDMS_V_U64:
			rc = fprintf(s_handle->file, ",%"PRIu64,
					ldms_metric_get_u64(set, metric_array[i]));
			if (rc < 0)
				ovis_log(mylog, OVIS_LERROR, PNAME ": Error %d writing to '%s'\n",
						rc, s_handle->path);
			break;
		default:
		  ovis_log(mylog, OVIS_LERROR, PNAME ": cannot handle metric type for metric %d\n", i);
		}
	}
	fprintf(s_handle->file,"\n");

	pthread_mutex_unlock(&s_handle->lock);

	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_store ldmsd_plugin_interface = {
	.base = {
			.type = LDMSD_PLUGIN_STORE,
			.config = config,
			.usage = usage,
                        .constructor = constructor,
                        .destructor = destructor,
	},
	.open = open_store,
	.store = store,
};

static void __attribute__ ((constructor)) store_tutorial_init();
static void store_tutorial_init()
{
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_tutorial_fini(void);
static void store_tutorial_fini()
{
	pthread_mutex_destroy(&cfg_lock);
}
