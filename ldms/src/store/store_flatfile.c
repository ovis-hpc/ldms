/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012-2016 Sandia Corporation. All rights reserved.
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
#include "ovis-ldms-config.h"
#include "coll/idx.h"
#include "ldms.h"
#include "ldmsd.h"
#include <ovis_util/util.h>

/*
 * NOTE:
 *   (flatfile::path) = (root_path)/(container)/(schema)/(metric)
 */

static idx_t store_idx;
static char tmp_path[PATH_MAX];
static char *root_path; /**< store root path */
static char *ignore_path; /* file of metrics to blacklist */
static char *include_path; /* file of metrics to whitelist */
static ldmsd_msg_log_f msglog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

/**
 * \brief Store for individual metric.
 */
struct flatfile_metric_store {
	FILE *file; /**< File handle */
	pthread_mutex_t lock; /**< lock at metric store level */
	int ignore; /* should we skip storing this metric. */
	char *path; /**< path of the flatfile store */
	LIST_ENTRY(flatfile_metric_store) entry; /**< Entry for free list. */
};

struct flatfile_store_instance {
	struct ldmsd_store *store;
	char *path; /**< (root_path)/(container)/schema */
	char *schema;
	void *ucontext;
	idx_t include_idx;
	idx_t ignore_idx;
	idx_t ms_idx;
	LIST_HEAD(ms_list, flatfile_metric_store) ms_list;
	int metric_count;
	struct flatfile_metric_store *ms[OVIS_FLEX];
};

static pthread_mutex_t cfg_lock;

#define IGNORE_MET (void *)16
#define MAX_METRIC_NAME_LEN 1024
/* return an idx of metric names to ignore */
static idx_t parse_metrics(const char *fname)
{
	idx_t metrics = idx_create();
	if (!metrics) {
		msglog(LDMSD_LERROR, "Out of memory for %s at %s:%d\n",
			fname, __FILE__, __LINE__);
		errno = ENOMEM;
		return NULL;
	}
	if (!fname) {
		return NULL;
	}
	FILE *fp = fopen(fname, "r");
	if (!fp) {
		msglog(LDMSD_LERROR, "Can't open file %s at %s:%d\n",
			fname, __FILE__, __LINE__);
		errno = EINVAL;
		return NULL;
	}
	char lbuf[MAX_METRIC_NAME_LEN];
	char sbuf[MAX_METRIC_NAME_LEN];
	lbuf[0] = '\0';
	char *s;
	int line = 0;
	do {
		line++;
		s = fgets(lbuf, sizeof(lbuf), fp);
		if (!s)
			break;
		int nw = sscanf(lbuf, "%s", sbuf);
		if (!nw)
			continue;
		if (s[0] == '#')
			continue;
		size_t keylen = strlen(sbuf);
		errno = 0;
		idx_add(metrics, (void *)sbuf, keylen, IGNORE_MET);
		if (errno) {
			msglog(LDMSD_LERROR, "Out of memory for %s at %s:%d\n",
				fname, __FILE__, __LINE__);
			goto out;
		}
	} while (s);
out:
	fclose(fp);
	return metrics;
}

/**
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	value = av_value(avl, "path");
	if (!value)
		goto err;
	char *ivalue = av_value(avl, "ignore");
	char *kvalue = av_value(avl, "include");

	pthread_mutex_lock(&cfg_lock);
	if (root_path) {
		free(root_path);
		root_path = NULL;
	}
	if (kvalue) {
		if(include_path)
			free(include_path);
		include_path = strdup(kvalue);
		if (!include_path)
			goto err1;
	}
	if (ivalue) {
		if(ignore_path)
			free(ignore_path);
		ignore_path = strdup(ivalue);
		if (!ignore_path)
			goto err1;
	}
	root_path = strdup(value);
err1:
	pthread_mutex_unlock(&cfg_lock);
	if (!root_path)
		return ENOMEM;
	return 0;
 err:
	return EINVAL;
}

static void term(struct ldmsd_plugin *self)
{
	/*
	 * TODO: Iterate through all unclosed stores and cleanup resources
	 * and close any open files.
	 */
}

static const char *usage(struct ldmsd_plugin *self)
{
	return
"    config name=store_flatfile path=<path> [include=white] [ignore=black]\n"
"              - Set the root path for the storage of flatfiles.\n"
"              path      The path to the root of the flatfile directory\n"
"              white     The list of metrics to keep one per line in a file\n"
"                        All others are ignored.\n"
"              black     The list of metrics to ignore one per line in a file\n"
"                        All others are kept.\n"
"    Content black file is ignored when include= is present. Use only one.\n";
}

static void *get_ucontext(ldmsd_store_handle_t _sh)
{
	struct flatfile_store_instance *si = _sh;
	return si->ucontext;
}

static ldmsd_store_handle_t
open_store(struct ldmsd_store *s, const char *container, const char *schema,
	  struct ldmsd_strgp_metric_list *metric_list, void *ucontext)
{
	struct flatfile_store_instance *si;
	struct flatfile_metric_store *ms;
	int i;
	char *key = NULL;
	size_t len;

	len = strlen(container) + strlen(schema) + 2;
	key = malloc(len);
	if (!key) {
		return NULL;
	}
	snprintf(key, len, "%s:%s", container, schema);

	pthread_mutex_lock(&cfg_lock);
	/*
	 * Add a component type directory if one does not
	 * already exist
	 */
	si = idx_find(store_idx, (void *)key, strlen(key));
	if (!si) {
		/*
		 * First, count the metric.
		 */
		int metric_count = 0;
		ldmsd_strgp_metric_t x;
		TAILQ_FOREACH(x, metric_list, entry) {
			metric_count++;
		}
		sprintf(tmp_path, "%s/%s/%s", root_path, container, schema);
		f_mkdir_p(tmp_path, 0777);

		/*
		 * Open a new store for this component-type and
		 * metric combination
		 */
		si = calloc(1, sizeof(*si) +
				metric_count *
				sizeof(struct flatfile_metric_store *));
		if (!si)
			goto out;
		si->metric_count = metric_count;
		errno = 0;
		si->ignore_idx = parse_metrics(ignore_path);
		if (!si->ignore_idx && errno != 0)
			goto err1;
		errno = 0;
		si->include_idx = parse_metrics(include_path);
		if (!si->include_idx && errno != 0)
			goto err2;
		si->ms_idx = idx_create();
		if (!si->ms_idx)
			goto err2;
		si->ucontext = ucontext;
		si->store = s;
		si->path = strdup(tmp_path);
		if (!si->path)
			goto err2;
		si->schema = strdup(schema);
		if (!si->schema)
			goto err3;
		i = 0;
		char mname[128];
		char *name;
		void *ign = NULL;
		TAILQ_FOREACH(x, metric_list, entry) {
			name = strchr(x->name, '#');
			if (si->include_idx) {
				ign = idx_find(si->include_idx, x->name,
					strlen(x->name));
				if (ign)
					ign = 0;
				else
					ign = IGNORE_MET;
			} else {
				if (si->ignore_idx) {
					ign = idx_find(si->ignore_idx, x->name,
						   strlen(x->name));
				}
			}
			if (name) {
				int len = name - x->name;
				name = strncpy(mname, x->name, len);
				name[len] = 0;
			} else {
				name = x->name;
			}
			ms = idx_find(si->ms_idx, name, strlen(name));
			if (ms) {
				si->ms[i++] = ms;
				continue;
			}
			/* Create new metric store if not exist. */
			ms = calloc(1, sizeof(*ms));
			if (!ms) {
				msglog(LDMSD_LERROR, "Out of memory at %s:%d\n",
					__FILE__, __LINE__);
				goto err4;
			}
			sprintf(tmp_path, "%s/%s", si->path, name);
			ms->path = strdup(tmp_path);
			if (!ms->path) {
				msglog(LDMSD_LERROR, "Out of memory at %s:%d\n",
					__FILE__, __LINE__);
				goto err4;
			}
			if (ign) {
				ms->ignore = 1;
				msglog(LDMSD_LDEBUG, "%s: ignoring %s\n",
					__FILE__, x->name);
			} else {
				msglog(LDMSD_LDEBUG, "%s: storing %s\n",
					__FILE__, x->name);
				ms->ignore = 0;
				ms->file = fopen_perm(ms->path, "a+",
					LDMSD_DEFAULT_FILE_PERM);
				if (!ms->file) {
					int eno = errno;
					msglog(LDMSD_LERROR, "Error opening %s: %d: %s at %s:%d\n",
						ms->path, eno, strerror(eno),
						__FILE__, __LINE__);
					goto err4;
				}
			}
			pthread_mutex_init(&ms->lock, NULL);
			idx_add(si->ms_idx, name, strlen(name), ms);
			LIST_INSERT_HEAD(&si->ms_list, ms, entry);
			msglog(LDMSD_LDEBUG, "%s: ignore = %d, index = %d\n",
				ms->path, ms->ignore, i);
			si->ms[i++] = ms;
		}
		idx_add(store_idx, (void *)key, strlen(key), si);
	}
	goto out;
err4:
	if (ms) {
		if (ms->path)
			free(ms->path);
		if (ms->file)
			fclose(ms->file);
		free(ms);
	}
	while ((ms = LIST_FIRST(&si->ms_list))) {
		LIST_REMOVE(ms, entry);
		if (ms->path)
			free(ms->path);
		if (ms->file)
			fclose(ms->file);
		free(ms);
	}

	free(si->schema);
err3:
	free(si->path);
err2:
	if (si->ms_idx)
		idx_destroy(si->ms_idx);
	if (si->ignore_idx)
		idx_destroy(si->ignore_idx);
	if (si->include_idx)
		idx_destroy(si->include_idx);
err1:
	free(si);
	si = NULL;
out:
	pthread_mutex_unlock(&cfg_lock);
	if (key)
		free(key);
	return si;
}

static int
store(ldmsd_store_handle_t _sh, ldms_set_t set, int *metric_arry, size_t metric_count)
{
	struct flatfile_store_instance *si;
	int i;
	int rc = 0, rc2 = 0;
	int last_rc = 0;
	int last_errno = 0;

	if (!_sh)
		return EINVAL;

	si = _sh;
	const struct ldms_timestamp _ts = ldms_transaction_timestamp_get(set);
	const struct ldms_timestamp *ts = &_ts;
	uint64_t comp_id;

	const char *prod;
	prod = ldms_set_producer_name_get(set);
	for (i=0; i<metric_count; i++) {
		if (si->ms[i]->ignore) {
			msglog(LDMSD_LDEBUG, "%s: ignoring %d because %d\n", __FILE__, i, &si->ms[i]->ignore);
			continue;
		}
		pthread_mutex_lock(&si->ms[i]->lock);
		comp_id = ldms_metric_user_data_get(set, metric_arry[i]);
		/* time, host, compid, value */
#define STAMP \
	rc2 = fprintf(si->ms[i]->file, "%"PRIu32".%06"PRIu32" %s %"PRIu64, \
		ts->sec, ts->usec, prod, comp_id)
		enum ldms_value_type metric_type =
			ldms_metric_type_get(set, metric_arry[i]);
		switch (metric_type) {
		case LDMS_V_CHAR_ARRAY:
			STAMP;
			rc = fprintf(si->ms[i]->file, " %s\n",
			     ldms_metric_array_get_str(set, metric_arry[i]));
			break;
		case LDMS_V_U8:
			STAMP;
			rc = fprintf(si->ms[i]->file, " %u\n",
			     (unsigned)ldms_metric_get_u8(set, metric_arry[i]));
			break;
		case LDMS_V_S8:
			STAMP;
			rc = fprintf(si->ms[i]->file, " %d\n",
			     (int)ldms_metric_get_s8(set, metric_arry[i]));
			break;
		case LDMS_V_U16:
			STAMP;
			rc = fprintf(si->ms[i]->file, " %u\n",
			     (unsigned)ldms_metric_get_u16(set, metric_arry[i]));
			break;
		case LDMS_V_S16:
			STAMP;
			rc = fprintf(si->ms[i]->file, " %d\n",
			     (int)ldms_metric_get_s16(set, metric_arry[i]));
			break;
		case LDMS_V_U32:
			STAMP;
			rc = fprintf(si->ms[i]->file, " %u\n",
			     (unsigned)ldms_metric_get_u32(set, metric_arry[i]));
			break;
		case LDMS_V_S32:
			STAMP;
			rc = fprintf(si->ms[i]->file, " %d\n",
			     ldms_metric_get_s32(set, metric_arry[i]));
			break;
		case LDMS_V_U64:
			STAMP;
			rc = fprintf(si->ms[i]->file, " %"PRIu64"\n",
			     ldms_metric_get_u64(set, metric_arry[i]));
			break;
		case LDMS_V_S64:
			STAMP;
			rc = fprintf(si->ms[i]->file, " %"PRId64"\n",
			     ldms_metric_get_s64(set, metric_arry[i]));
			break;
		case LDMS_V_F32:
			STAMP;
			rc = fprintf(si->ms[i]->file, " %.9g\n",
			     ldms_metric_get_float(set, metric_arry[i]));
			break;
		case LDMS_V_D64:
			STAMP;
			rc = fprintf(si->ms[i]->file, " %.17g\n",
			     ldms_metric_get_double(set, metric_arry[i]));
			break;
		default:
			/* array types not supported yet. want row and split files options */
			break;
		}
#undef STAMP
		if (rc < 0 || rc2 < 0) {
			last_errno = errno;
			last_rc = (rc != 0 ? rc : rc2);
			msglog(LDMSD_LERROR, "Error %d: %s at %s:%d\n", last_errno,
					strerror(last_errno), __FILE__,
					__LINE__);
		}
		pthread_mutex_unlock(&si->ms[i]->lock);
	}

	if (last_errno)
		errno = last_errno;
	return last_rc;
}

static int flush_store(ldmsd_store_handle_t _sh)
{
	struct flatfile_store_instance *si = _sh;
	if (!_sh)
		return EINVAL;
	int lrc, rc = 0;
	int eno = 0;
	struct flatfile_metric_store *ms;
	LIST_FOREACH(ms, &si->ms_list, entry) {
		pthread_mutex_lock(&ms->lock);
		lrc = fflush(ms->file);
		if (lrc) {
			rc = lrc;
			eno = errno;
			msglog(LDMSD_LERROR, "Errro %d: %s at %s:%d\n", eno, strerror(eno),
					__FILE__, __LINE__);
		}
		pthread_mutex_unlock(&ms->lock);
	}
	if (eno)
		errno = eno;
	return rc;
}

static void close_store(ldmsd_store_handle_t _sh)
{
	pthread_mutex_lock(&cfg_lock);
	/*
	 * NOTE: This close function looks like destroy to me.
	 */
	struct flatfile_store_instance *si = _sh;
	if (!_sh)
		return;
	struct flatfile_metric_store *ms;
	while ((ms = LIST_FIRST(&si->ms_list))) {
		LIST_REMOVE(ms, entry);
		if (ms->file)
			fclose(ms->file);
		if (ms->path)
			free(ms->path);
		free(ms);
	}
	idx_delete(store_idx, (void *)(si->schema), strlen(si->schema));
	free(si->path);
	free(si->schema);
	if (si->include_idx)
		idx_destroy(si->include_idx);
	if (si->ignore_idx)
		idx_destroy(si->ignore_idx);
	idx_destroy(si->ms_idx);
	free(si);
	pthread_mutex_unlock(&cfg_lock);
}

static struct ldmsd_store store_flatfile = {
	.base = {
		.name = "flatfile",
		.type = LDMSD_PLUGIN_STORE,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.open = open_store,
	.close = close_store,
	.store = store,
	.get_context = get_ucontext,
	.flush = flush_store,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &store_flatfile.base;
}

static void __attribute__ ((constructor)) store_flatfile_init();
static void store_flatfile_init()
{
	store_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_flatfile_fini(void);
static void store_flatfile_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(store_idx);
}
