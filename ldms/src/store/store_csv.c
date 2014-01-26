/*
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
#include <linux/limits.h>
#include <pthread.h>
#include <errno.h>
#include <coll/idx.h>
#include "ldms.h"
#include "ldmsd.h"

#define TV_SEC_COL    0
#define TV_USEC_COL    1
#define GROUP_COL    2
#define VALUE_COL    3

static idx_t store_idx;
static char *root_path;
static int altheader;
static int id_pos;
static ldmsd_msg_log_f msglog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

#define LOGFILE "/var/log/store_csv.log"

/*
 * store_csv - csv compid, value pairs UNLESS id_pos is specified.
 * In that case, either only the first/last (0/1) metric's comp id
 * will be used. First/last is determined by order in the store,
 * not the order in ldms_ls (probably reversed).
 */

struct csv_store_handle {
	struct ldmsd_store *store;
	char *path;
	FILE *file;
	FILE *headerfile;
	int printheader;
	char *store_key;
	pthread_mutex_t lock;
	void *ucontext;
};

pthread_mutex_t cfg_lock;

/**
 * \brief Configuration
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *altvalue;
	char *ivalue;
	int ipos = -1;

	value = av_value(avl, "path");
	if (!value)
		return EINVAL;

	altvalue = av_value(avl, "altheader");

	ivalue = av_value(avl, "id_pos");
	if (ivalue){
		ipos = atoi(ivalue);
		if ((ipos < 0) || (ipos > 1))
			return EINVAL;
	}

	pthread_mutex_lock(&cfg_lock);
	if (root_path)
		free(root_path);

	root_path = strdup(value);

	id_pos = ipos;

	if (altvalue)
		altheader = atoi(altvalue);
	else
		altheader = 0;

	pthread_mutex_unlock(&cfg_lock);
	if (!root_path)
		return ENOMEM;
	return 0;
}

static void term(void)
{
}

static const char *usage(void)
{
	return  "    config name=store_csv path=<path> altheader=<0/1>\n"
		"         - Set the root path for the storage of csvs.\n"
		"           path      The path to the root of the csv directory\n"
		"         - altheader Header in a separate file (optional, default 0)\n"
		"         - id_pos    Use only one comp_id either first or last (0/1)\n"
                "                     (Optional default use all compid)\n";
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
	struct csv_store_handle *s_handle = _s_handle;
	return s_handle->ucontext;
}

static int print_header(struct csv_store_handle *s_handle,
			ldms_mvec_t mvec)
{

	int compid;

	/* Only called from Store which already has the lock */
	FILE* fp = s_handle->headerfile;

	s_handle->printheader = 0;
	if (!fp){
		msglog("Cannot print header for store_csv. No headerfile\n");
		return EINVAL;
	}

	fprintf(fp, "#Time");
	int num_metrics = ldms_mvec_get_count(mvec);
	
	if (id_pos < 0) {
		int i, rc;

		for (i = 0; i < num_metrics; i++) {
			char* name = ldms_get_metric_name(mvec->v[i]);
			fprintf(fp, ", %s.CompId, %s.value",
				name, name);
		}
		fprintf(fp, "\n");
	} else {
		int i, rc;

		fprintf(fp, ", CompId");
		for (i = 0; i < num_metrics; i++) {
			char* name = ldms_get_metric_name(mvec->v[i]);
			fprintf(fp, ", %s", name);
		}
		fprintf(fp, "\n");
	}

	/* Flush for the header, whether or not it is the data file as well */
	fflush(fp);

	fclose(s_handle->headerfile);
	s_handle->headerfile = 0;

	return 0;
}

static ldmsd_store_handle_t
new_store(struct ldmsd_store *s, const char *comp_type, const char* container,
		struct ldmsd_store_metric_index_list *list, void *ucontext)
{
	struct csv_store_handle *s_handle;
	int add_handle = 0;

	pthread_mutex_lock(&cfg_lock);
	s_handle = idx_find(store_idx, (void *)container, strlen(container));
	if (!s_handle) {
		char tmp_path[PATH_MAX];

		//append or create
		sprintf(tmp_path, "%s/%s", root_path, comp_type);
		mkdir(tmp_path, 0777);
		sprintf(tmp_path, "%s/%s/%s", root_path, comp_type,
				container);

		s_handle = calloc(1, sizeof *s_handle);
		if (!s_handle)
			goto out;
		s_handle->ucontext = ucontext;
		s_handle->store = s;
		add_handle = 1;

		pthread_mutex_init(&s_handle->lock, NULL);

		s_handle->path = strdup(tmp_path);
		if (!s_handle->path)
			goto err1;

		s_handle->store_key = strdup(container);
		if (!s_handle->store_key)
			goto err2;

		s_handle->printheader = 1;
	}

	/* Take the lock in case its a store that has been closed */
	pthread_mutex_lock(&s_handle->lock);

	/* For both actual new store and reopened store, open the data file */
	if (!s_handle->file)
		s_handle->file = fopen(s_handle->path, "a+");
	if (!s_handle->file)
		goto err3;

	/* Only bother to open the headerfile if we have to print the header */
	if (s_handle->printheader && !s_handle->headerfile){
		char tmp_headerpath[PATH_MAX];

		if (altheader) {
			sprintf(tmp_headerpath, "%s.HEADER", s_handle->path);
			/* truncate a separate headerfile if exists */
			s_handle->headerfile = fopen(tmp_headerpath, "w");
		} else {
			s_handle->headerfile = fopen(s_handle->path, "a+");
		}

		if (!s_handle->headerfile)
			goto err4;
	}

	if (add_handle)
		idx_add(store_idx, (void *)container,
			strlen(container), s_handle);

	pthread_mutex_unlock(&s_handle->lock);
	goto out;

err4:
	fclose(s_handle->file);
	s_handle->file = NULL;
err3:
	free(s_handle->store_key);
err2:
	free(s_handle->path);
err1:
	pthread_mutex_unlock(&s_handle->lock);
	pthread_mutex_destroy(&s_handle->lock);
	free(s_handle);
	s_handle = NULL;
out:
	pthread_mutex_unlock(&cfg_lock);
	return s_handle;
}

static int
store(ldmsd_store_handle_t _s_handle, ldms_set_t set, ldms_mvec_t mvec)
{
	const struct ldms_timestamp *ts = ldms_get_timestamp(set);
	uint64_t comp_id;
	struct csv_store_handle *s_handle;
	s_handle = _s_handle;
	if (!s_handle)
		return EINVAL;

	if (!s_handle->file){
		msglog("Cannot insert values for <%s>: file is closed\n",
				s_handle->path);
		return EPERM;
	}

	pthread_mutex_lock(&s_handle->lock);

	if (s_handle->printheader)
		print_header(s_handle, mvec);

	fprintf(s_handle->file, "%"PRIu32".%06"PRIu32, ts->sec, ts->usec);

	int num_metrics = ldms_mvec_get_count(mvec);
	if (id_pos < 0){
		int i, rc;
		for (i = 0; i < num_metrics; i++) {
			comp_id = ldms_get_user_data(mvec->v[i]);
			rc = fprintf(s_handle->file, ", %" PRIu64 ", %" PRIu64,
				     comp_id, ldms_get_u64(mvec->v[i]));
			if (rc < 0)
				msglog("store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
		}
		fprintf(s_handle->file,"\n");
	} else {
		int i, rc;

		if (num_metrics > 0){
			i = (id_pos == 0)? 0: (num_metrics-1);
			rc = fprintf(s_handle->file, ", %" PRIu64,
				ldms_get_user_data(mvec->v[i]));
			if (rc < 0)
				msglog("store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
		}
		for (i = 0; i < num_metrics; i++) {
			rc = fprintf(s_handle->file, ", %" PRIu64, ldms_get_u64(mvec->v[i]));
			if (rc < 0)
				msglog("store_csv: Error %d writing to '%s'\n",
				       rc, s_handle->path);
		}
		fprintf(s_handle->file,"\n");
	}

	pthread_mutex_unlock(&s_handle->lock);

	return 0;
}

static int flush_store(ldmsd_store_handle_t _s_handle)
{
	struct csv_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		msglog("store_csv: flush error.\n");
		return -1;
	}
	pthread_mutex_lock(&s_handle->lock);
	fflush(s_handle->file);
	pthread_mutex_unlock(&s_handle->lock);
	return 0;
}

static void close_store(ldmsd_store_handle_t _s_handle)
{
	struct csv_store_handle *s_handle = _s_handle;
	if (!s_handle)
		return;

	pthread_mutex_lock(&s_handle->lock);
	if (s_handle->file)
		fclose(s_handle->file);
	s_handle->file = NULL;
	if (s_handle->headerfile)
		fclose(s_handle->headerfile);
	s_handle->headerfile = NULL;
	pthread_mutex_unlock(&s_handle->lock);
}

static void destroy_store(ldmsd_store_handle_t _s_handle)
{

	pthread_mutex_lock(&cfg_lock);
	struct csv_store_handle *s_handle = _s_handle;
	if (!s_handle) {
		pthread_mutex_unlock(&cfg_lock);
		return;
	}

	pthread_mutex_lock(&s_handle->lock);
	msglog("Destroying store_csv with path <%s>\n", s_handle->path);
	fflush(s_handle->file);
	s_handle->store = NULL;
	if (s_handle->path)
		free(s_handle->path);
	s_handle->path = NULL;
	if (s_handle->ucontext)
		free(s_handle->ucontext);
	s_handle->ucontext = NULL;
	if (s_handle->file)
		fclose(s_handle->file);
	s_handle->file = NULL;
	if (s_handle->headerfile)
		fclose(s_handle->headerfile);
	s_handle->headerfile = NULL;

	idx_delete(store_idx, s_handle->store_key, strlen(s_handle->store_key));
	if (s_handle->store_key)
		free(s_handle->store_key);
	pthread_mutex_unlock(&s_handle->lock);
	pthread_mutex_destroy(&s_handle->lock);
	pthread_mutex_unlock(&cfg_lock);
	free(s_handle);
}

static struct ldmsd_store store_csv = {
	.base = {
			.name = "csv",
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
	return &store_csv.base;
}

static void __attribute__ ((constructor)) store_csv_init();
static void store_csv_init()
{
	store_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_csv_fini(void);
static void store_csv_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(store_idx);
}
