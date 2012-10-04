/*
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
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
#include <sos/sos.h>
#include <sos/idx.h>
#include "ldms.h"
#include "ldmsd.h"

SOS_OBJ_BEGIN(ovis_metric_class, "OvisMetric")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("comp_id", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("value", SOS_TYPE_UINT64)
SOS_OBJ_END(4);

#define TV_SEC_COL	0
#define TV_USEC_COL	1
#define GROUP_COL	2
#define VALUE_COL	3

static idx_t metric_idx;
static char tmp_path[PATH_MAX];
static char *root_path;
static ldmsd_msg_log_f msglog;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

struct sos_metric_store {
	struct ldmsd_store *store;
	sos_t sos;
	char *path;
	char *metric_key;
	void *ucontext;
	pthread_mutex_t lock;
};

pthread_mutex_t cfg_lock;

/** 
 * \brief Configuration
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	value = av_value(avl, "path");
	if (!value)
		goto err;

	pthread_mutex_lock(&cfg_lock);
	if (root_path)
		free(root_path);
	root_path = strdup(value);
	pthread_mutex_unlock(&cfg_lock);
	if (!root_path)
		return ENOMEM;
	return 0;
 err:
	return EINVAL;
}

static void term(void)
{
}

static const char *usage(void)
{
	return  "    config name=sos path=<root>\n"
		"        - Set the root path for the storage of SOS files.\n"
		"        path      The path to the root of the SOS directory\n";
}

static ldmsd_metric_store_t
get_store(struct ldmsd_store *s, const char *comp_name, const char *metric_name)
{
	char metric_key[128];
	ldmsd_metric_store_t ms;

	pthread_mutex_lock(&cfg_lock);
	/*
	 * Add a component type directory if one does not
	 * already exist
	 */
	sprintf(metric_key, "%s:%s", comp_name, metric_name);
	ms = idx_find(metric_idx, metric_key, strlen(metric_key));
	pthread_mutex_unlock(&cfg_lock);
	return ms;
}

static void *get_ucontext(ldmsd_metric_store_t _ms)
{
	struct sos_metric_store *ms = _ms;
	return ms->ucontext;
}

static ldmsd_metric_store_t
new_store(struct ldmsd_store *s, const char *comp_name, const char *metric_name,
	  void *ucontext)
{
	char metric_key[128];
	struct sos_metric_store *ms;

	pthread_mutex_lock(&cfg_lock);
	/*
	 * Add a component type directory if one does not
	 * already exist
	 */
	sprintf(metric_key, "%s:%s", comp_name, metric_name);
	ms = idx_find(metric_idx, metric_key, strlen(metric_key));
	if (!ms) {
		sprintf(tmp_path, "%s/%s", root_path, comp_name);
		mkdir(tmp_path, 0777);

		/*
		 * Open a new store for this component-type and
		 * metric combination
		 */
		ms = calloc(1, sizeof *ms);
		if (!ms)
			goto out;
		ms->ucontext = ucontext;
		ms->store = s;
		pthread_mutex_init(&ms->lock, NULL);
		sprintf(tmp_path, "%s/%s/%s", root_path, comp_name, metric_name);
		ms->path = strdup(tmp_path);
		if (!ms->path)
			goto err1;
		ms->metric_key = strdup(metric_key);
		if (!ms->metric_key)
			goto err2;
		ms->sos = sos_open(ms->path, O_CREAT | O_RDWR, 0660,
				  &ovis_metric_class);
		if (ms->sos)
			idx_add(metric_idx, metric_key, strlen(metric_key), ms);
		else
			goto err3;
	}
	goto out;
 err3:
	free(ms->metric_key);
 err2:
	free(ms->path);
 err1:
	free(ms);
 out:
	pthread_mutex_unlock(&cfg_lock);
	return ms;
}

static int
store(ldmsd_metric_store_t _ms, uint32_t comp_id,
      struct timeval tv, ldms_metric_t m)
{
	struct sos_metric_store *ms;
	sos_obj_t obj;

	if (!_ms)
		return EINVAL;

	ms = _ms;
	obj = sos_obj_new(ms->sos);
	if (!obj) {
		errno = ENOMEM;
		return -1;
	}
	sos_obj_attr_set(ms->sos, 0, obj, &tv.tv_sec);
	sos_obj_attr_set(ms->sos, 1, obj, &tv.tv_usec);
	sos_obj_attr_set(ms->sos, 2, obj, &comp_id);
	sos_obj_attr_set(ms->sos, 3, obj, ldms_get_value_ptr(m));
	if (sos_obj_add(ms->sos, obj))
		return -1;
	return 0;
}

static int flush_store(ldmsd_metric_store_t _ms)
{
	struct sos_metric_store *ms = _ms;
	if (!_ms)
		return EINVAL;
	sos_flush(ms->sos);
	return 0;
}

static void close_store(ldmsd_metric_store_t _ms)
{
	struct sos_metric_store *ms = _ms;
	if (!_ms)
		return;
	if (ms->sos)
		sos_close(ms->sos);
	idx_delete(metric_idx, ms->metric_key, strlen(ms->metric_key));
	free(ms->path);
	free(ms->metric_key);
	free(ms);
}

static void destroy_store(ldmsd_metric_store_t _ms)
{
}

static struct ldmsd_store store_sos = {
	.base = {
		.name = "sos",
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
	return &store_sos.base;
}

static void __attribute__ ((constructor)) store_sos_init();
static void store_sos_init()
{
	metric_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_sos_fini(void);
static void store_sos_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(metric_idx);
}
