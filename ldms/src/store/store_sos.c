/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-14 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012-14 Sandia Corporation. All rights reserved.
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
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <sys/syscall.h>
#include <sos/sos.h>
#include <coll/idx.h>
#include "ldms.h"
#include "ldmsd.h"
#include "store_sos.h"

#define TV_SEC_COL	0
#define TV_USEC_COL	1
#define GROUP_COL	2
#define VALUE_COL	3

#define LDMSD_SOS_POSTROT "LDMSD_SOS_POSTROTATE"

/*
 * According to 'man useradd' and 'man groupadd'
 * the max length of the user/group name is 32
 */
#define MAX_USER_GROUP_LEN	32
#define MAX_OWNER	(MAX_USER_GROUP_LEN * 2 + 1)

/*
 * NOTE:
 *   (sos::path) = (root_path)/(comp_type)/(metric)
 */

static idx_t store_idx;
static idx_t metric_idx;
static char root_path[PATH_MAX]; /**< store root path */
static ldmsd_msg_log_f msglog;
static pthread_mutex_t cfg_lock;
static time_t time_limit = 0;
static int max_copy = 1;
static size_t init_size = 4 * 1024 * 1024; /* default size 4MB */
static char owner[MAX_OWNER];

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

/**
 * \brief Store for individual metric.
 */
struct sos_metric_store {
	sos_t sos; /**< sos handle */
	pthread_mutex_t lock; /**< lock at metric store level */
	char *path; /**< path of the sos store */
	uint32_t last_rotate; /**< Last rotation timestamp */
	LIST_ENTRY(sos_metric_store) entry;
};

struct sos_store_instance {
	struct ldmsd_store *store;
	char *path; /**< (root_path)/(comp_type) */
	char *container;
	void *ucontext;
	LIST_HEAD(ms_list, sos_metric_store) ms_list;
	int metric_count;
	struct sos_metric_store **ms;
};

static int store_sos_change_owner(char *path)
{
	int rc = 0;
	if (owner[0] != '\0') {
		errno = 0;
		char cmd_s[1024];
		sprintf(cmd_s, "chown -R %s %s", owner, root_path);
		rc = system(cmd_s);
		if (rc) {
			msglog("store_sos: Error %d: Changing owner "
					"%s to %s\n", errno, owner);
			return -1;
		}
	}
	return rc;
}

static int store_sos_open_sos(struct sos_metric_store *ms, enum ldms_value_type type);

static struct sos_metric_store *create_metric_store(char *name,
				enum ldms_value_type type, char *path)
{
	struct sos_metric_store *ms;
	ms = calloc(1, sizeof(*ms));
	if (!ms) {
		msglog("store_sos: Out of memory\n");
		return NULL;
	}

	char tmp_path[PATH_MAX];
	snprintf(tmp_path, PATH_MAX, "%s/%s", path, name);
	ms->path = strdup(tmp_path);
	if (!ms->path) {
		msglog("store_sos: Out of memory\n");
		goto err;
	}
	pthread_mutex_init(&ms->lock, NULL);
	idx_add(metric_idx, name, strlen(name), ms);

	if (store_sos_open_sos(ms, type) < 0)
		goto err;

	return ms;
err:
	msglog("store_sos: Failed to create metric store of '%s'\n",
								name);
	free(ms);
	return NULL;
}

static int store_sos_init_store(char *metric_names)
{
	int rc = mkdir(root_path, 0777);
	if ((rc == -1) && (errno != EEXIST)) {
		msglog("store_sos: Error %d: Failed to make "
				"the directory '%s'\n",
				errno, root_path);
		return -1;
	}

	char *name, *ptr, *type_s;
	enum ldms_value_type type;
	struct sos_metric_store *ms;

	name = strtok_r(metric_names, "(", &ptr);
	while (name) {
		type_s = strtok_r(NULL, ")", &ptr);
		if (!type_s) {
			msglog("store_sos: Expect metric_name(type)\n");
			return -1;
		}

		type = ldms_str_to_type(type_s);
		if (type == LDMS_V_NONE) {
			msglog("store_sos: '%s' has an invalid type '%s'.",
					name, type_s);
			return -1;
		}
		ms = create_metric_store(name, type, root_path);
		name = strtok_r(NULL, ",", &ptr);
		name = strtok_r(NULL, "(", &ptr);
	}

	return store_sos_change_owner(root_path);
}

/**
 * \brief Configuration
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	value = av_value(avl, "path");
	if (!value)
		goto einval;

	pthread_mutex_lock(&cfg_lock);
	snprintf(root_path, PATH_MAX, "%s", value);

	value = av_value(avl, "time_limit");
	if (value)
		time_limit = atoi(value);

	value = av_value(avl, "max_copy");
	if (value)
		max_copy = atoi(value);

	value = av_value(avl, "init_size");
	if (value)
		init_size = atoi(value);

	value = av_value(avl, "owner");
	if (value) {
		if (strlen(value) > MAX_OWNER) {
			msglog("store_sos: 'owner' (%s) exceeds %d "
					"characters.\n", value, MAX_OWNER);
			goto einval;
		}
		snprintf(owner, MAX_OWNER, "%s", value);
	} else {
		owner[0] = '\0';
	}

	value = av_value(avl, "metric_names");
	if (value)
		if (store_sos_init_store(value))
			goto err;

	pthread_mutex_unlock(&cfg_lock);
	return 0;
einval:
	pthread_mutex_unlock(&cfg_lock);
	return EINVAL;
err:
	pthread_mutex_unlock(&cfg_lock);
	return errno;
}

static void term(void)
{
}

static const char *usage(void)
{
	return  "    config name=store_sos path=<path> owner=<user:group> metric_names=<metrics>\n"
		"        - Set the root path for the storage of SOS files.\n"
		"        path      The path to the root of the SOS directory\n"
		"	 user:group     Optional. Store_sos will 'chown -R user:group <path>'\n"
		"	 metric_names   Optional. The format is <metric_name(type)>,<metric_name(type)>,...\n"
		"			If this is given the store sos of the metrics will be created\n"
		"			when store_sos is configured.\n";
}

static ldmsd_store_handle_t
get_store(const char *container)
{
	ldmsd_store_handle_t sh;

	pthread_mutex_lock(&cfg_lock);
	/*
	 * Add a component type directory if one does not
	 * already exist
	 */
	sh = idx_find(store_idx, (void *)container, strlen(container));
	pthread_mutex_unlock(&cfg_lock);
	return sh;
}

static void *get_ucontext(ldmsd_store_handle_t _sh)
{
	struct sos_store_instance *si = _sh;
	return si->ucontext;
}

static int store_sos_open_sos(struct sos_metric_store *ms, enum ldms_value_type type)
{
	struct sos_class_s *class = NULL;

	switch (type) {
	case LDMS_V_S8:
	case LDMS_V_S16:
	case LDMS_V_S32:
		class = &ovis_metric_class_int32;
		break;
	case LDMS_V_S64:
		class = &ovis_metric_class_int64;
		break;
	case LDMS_V_U8:
	case LDMS_V_U16:
	case LDMS_V_U32:
		class = &ovis_metric_class_uint32;
		break;
	case LDMS_V_U64:
		class = &ovis_metric_class_uint64;
		break;
	case LDMS_V_F:
	case LDMS_V_D:
		class = &ovis_metric_class_double;
		break;
	default:
		msglog("store_sos: not support ldms_value_type '%s'\n",
						ldms_type_to_str(type));
		return ENOTSUP;
	}
	ms->sos = sos_open_sz(ms->path, O_RDWR|O_CREAT, 0660, class, init_size);
	if (!ms->sos) {
		msglog("store_sos: Failed to open %s, class: %s\n", ms->path, class->name);
		return ENOMEM;
	}

	return 0;
}

static ldmsd_store_handle_t
new_store(struct ldmsd_store *s, const char *comp_type, const char *container,
	  struct ldmsd_store_metric_index_list *metric_list, void *ucontext)
{
	struct sos_store_instance *si;
	struct sos_metric_store *ms;
	int i, metric_count;

	pthread_mutex_lock(&cfg_lock);
	/*
	 * Add a component type directory if one does not
	 * already exist
	 */
	si = idx_find(store_idx, (void *)container, strlen(container));
	if (!si) {
		/*
		 * First, count the metric.
		 */
		metric_count = 0;
		struct ldmsd_store_metric_index *x;
		LIST_FOREACH(x, metric_list, entry) {
			metric_count++;
		}
		char tmp_path[PATH_MAX];
		sprintf(tmp_path, "%s/%s", root_path, comp_type);
		mkdir(tmp_path, 0777);

		/*
		 * Open a new store for this component-type and
		 * metric combination
		 */
		si = calloc(1, sizeof(*si));
		if (!si)
			goto out;
		si->metric_count = metric_count;
		si->ucontext = ucontext;
		si->store = s;
		si->path = strdup(tmp_path);
		if (!si->path)
			goto err2;
		si->container = strdup(container);
		if (!si->container)
			goto err3;

		if (metric_count == 0) {
			idx_add(store_idx, (void *)container,
					strlen(container), si);
			goto out;
		}

		si->ms = calloc(metric_count,
				sizeof(struct sos_metric_store *));
		if (!si->ms)
			goto err4;

		i = 0;
		char buff[128];
		char *name;
		LIST_FOREACH(x, metric_list, entry) {
			name = strchr(x->name, '#');
			if (name) {
				int len = name - x->name;
				name = strncpy(buff, x->name, len);
				name[len] = 0;
			} else {
				name = x->name;
			}
			ms = idx_find(metric_idx, name, strlen(name));
			if (ms) {
				si->ms[i++] = ms;
				continue;
			}
			/* Create ms if not exist */
			ms = calloc(1, sizeof(*ms));
			if (!ms)
				goto err5;
			char tmp_path[PATH_MAX];
			snprintf(tmp_path, PATH_MAX, "%s/%s", si->path, name);
			ms->path = strdup(tmp_path);
			if (!ms->path) {
				msglog("store_sos: Out of memory\n");
				free(ms);
				goto err5;
			}

			/*
			 * NOTE: sos will be opened the first time
			 * the metric to be stored.
			 */

			pthread_mutex_init(&ms->lock, NULL);
			idx_add(metric_idx, name, strlen(name), ms);
			LIST_INSERT_HEAD(&si->ms_list, ms, entry);
			si->ms[i++] = ms;
		}
		idx_add(store_idx, (void *)container, strlen(container), si);
	}
	goto out;
err5:
	while (ms = LIST_FIRST(&si->ms_list)) {
		LIST_REMOVE(ms, entry);
		if (ms->path)
			free(ms->path);
		free(ms);
	}
	free(si->ms);
err4:
	free(si->container);
err3:
	free(si->path);
err2:
	free(si);
	si = NULL;
out:
	pthread_mutex_unlock(&cfg_lock);
	return si;
}

static int store_sos_create_ms_list(struct sos_store_instance *si,
						ldms_mvec_t mvec)
{
	int i;
	si->metric_count = mvec->count;
	si->ms = calloc(mvec->count, sizeof(struct sos_metric_store *));
	if (!si->ms)
		return ENOMEM;

	char buff[128];
	char *name;
	const char *metric_name;
	struct sos_metric_store *ms;
	for (i = 0; i < mvec->count; i++) {
		metric_name = ldms_get_metric_name(mvec->v[i]);
		name = strchr(metric_name, '#');
		if (name) {
			int len = name - metric_name;
			name = strncpy(buff, metric_name, len);
			name[len] = 0;
		} else {
			name = strdup(metric_name);
		}
		ms = idx_find(metric_idx, name, strlen(name));
		if (ms) {
			si->ms[i] = ms;
			continue;
		}
		/* Create ms if not exist */
		ms = calloc(1, sizeof(*ms));
		if (!ms)
			goto err;

		char tmp_path[PATH_MAX];
		snprintf(tmp_path, PATH_MAX, "%s/%s", si->path, name);
		ms->path = strdup(tmp_path);
		if (!ms->path) {
			msglog("store_sos: Out of memory\n");
			free(ms);
			goto err;
		}

		/*
		 * NOTE: sos will be opened the first time
		 * the metric to be stored.
		 */

		pthread_mutex_init(&ms->lock, NULL);
		idx_add(metric_idx, name, strlen(name), ms);
		LIST_INSERT_HEAD(&si->ms_list, ms, entry);
		si->ms[i] = ms;
	}
	return 0;
err:
	while (ms = LIST_FIRST(&si->ms_list)) {
		LIST_REMOVE(ms, entry);
		if (ms->path)
			free(ms->path);
		free(ms);
	}
	free(si->ms);
	free(si->container);
	free(si->path);
	return -1;
}

void store_sos_cleanup(sos_t sos, uint32_t sec)
{
	int rc;
	sos_iter_t itr = sos_iter_new(sos, 0);
	sos_obj_t obj;
	rc = sos_iter_begin(itr);
	if (rc)
		return;
	obj = sos_iter_obj(itr);
	while (obj) {
		if (sos_obj_attr_get_uint32(sos, 0, obj) >= sec)
			break;
		sos_iter_obj_remove(itr);
		sos_obj_delete(sos, obj);
		obj = sos_iter_obj(itr);
	}
	sos_iter_free(itr);
}

static int
store(ldmsd_store_handle_t _sh, ldms_set_t set, ldms_mvec_t mvec, int flags)
{
	struct sos_store_instance *si;
	sos_obj_t obj;
	int i;
	int rc = 0;
	int last_rc = 0;
	int last_errno = 0;
	enum ldms_value_type mtype;

	/* Do not store if data update is not complete */
	if (!(flags & LDMSD_STORE_UPDATE_COMPLETE))
		return 0;

	if (!_sh)
		return EINVAL;


	si = _sh;

	/* When call new_store the lookup_cb hasn't finished yet. */
	if (si->metric_count == 0) {
		rc = store_sos_create_ms_list(si, mvec);
		if (rc) {
			msglog("store_sos: Failed to create store "
				"for %s.\n", si->container);
			return -1;
		}
	}

	int32_t v32;
	uint32_t vu32;
	int64_t v64;
	uint64_t vu64;
	double vd;
	const struct ldms_timestamp *ts = ldms_get_timestamp(set);

	for (i = 0; i < mvec->count; i++) {
		pthread_mutex_lock(&si->ms[i]->lock);

		if (!si->ms[i]->sos) {
			if (store_sos_open_sos(si->ms[i],
					ldms_get_metric_type(mvec->v[i]))) {
				pthread_mutex_unlock(&si->ms[i]->lock);
				return ENOMEM;
			}
			store_sos_change_owner(root_path);
		}

		if (!si->ms[i]->last_rotate)
			si->ms[i]->last_rotate = ts->sec;

		if (time_limit && (ts->sec / time_limit) > (si->ms[i]->last_rotate / time_limit)) {
			sos_t new_sos = sos_rotate(si->ms[i]->sos, max_copy);
			if (new_sos) {
				si->ms[i]->sos = new_sos;
				si->ms[i]->last_rotate = ts->sec;
				sos_post_rotation(new_sos, LDMSD_SOS_POSTROT);
			} else {
				msglog("WARN: sos_rotate failed: %s\n",
					si->ms[i]->path);
			}
		}

		obj = sos_obj_new(si->ms[i]->sos);
		if (!obj) {
			msglog("Error %d: %s at %s:%d\n", errno,
					strerror(errno), __FILE__, __LINE__);
			errno = ENOMEM;
			pthread_mutex_unlock(&si->ms[i]->lock);
			return -1;
		}
		uint64_t metric_id = ldms_get_user_data(mvec->v[i]);
		sos_obj_attr_set(si->ms[i]->sos, 0, obj, (void*)&ts->sec);
		sos_obj_attr_set(si->ms[i]->sos, 1, obj, (void*)&ts->usec);
		sos_obj_attr_set(si->ms[i]->sos, 2, obj, &metric_id);

		mtype = ldms_get_metric_type(mvec->v[i]);
		switch (mtype) {
		case LDMS_V_S8:
			v32 = ldms_get_s8(mvec->v[i]);
			sos_obj_attr_set(si->ms[i]->sos, 3, obj, &v32);
			break;
		case LDMS_V_U8:
			vu32 = ldms_get_u8(mvec->v[i]);
			sos_obj_attr_set(si->ms[i]->sos, 3, obj, &vu32);
			break;
		case LDMS_V_S16:
			v32 = ldms_get_s16(mvec->v[i]);
			sos_obj_attr_set(si->ms[i]->sos, 3, obj, &v32);
			break;
		case LDMS_V_U16:
			vu32 = ldms_get_u16(mvec->v[i]);
			sos_obj_attr_set(si->ms[i]->sos, 3, obj, &vu32);
			break;
		case LDMS_V_S32:
			v32 = ldms_get_s32(mvec->v[i]);
			sos_obj_attr_set(si->ms[i]->sos, 3, obj, &v32);
			break;
		case LDMS_V_U32:
			vu32 = ldms_get_u32(mvec->v[i]);
			sos_obj_attr_set(si->ms[i]->sos, 3, obj, &vu32);
			break;
		case LDMS_V_S64:
			v64 = ldms_get_s64(mvec->v[i]);
			sos_obj_attr_set(si->ms[i]->sos, 3, obj, &v64);
			break;
		case LDMS_V_U64:
			vu64 = ldms_get_u64(mvec->v[i]);
			sos_obj_attr_set(si->ms[i]->sos, 3, obj, &vu64);
			break;
		case LDMS_V_F:
			vd = ldms_get_float(mvec->v[i]);
			sos_obj_attr_set(si->ms[i]->sos, 3, obj, &vd);
			break;
		case LDMS_V_D:
			vd = ldms_get_double(mvec->v[i]);
			sos_obj_attr_set(si->ms[i]->sos, 3, obj, &vd);
			break;
		case LDMS_V_LD:
		default:
			msglog("store_sos: Does not support type '%s'\n",
						ldms_type_to_str(mtype));
			break;
		}

		rc = sos_obj_add(si->ms[i]->sos, obj);
		pthread_mutex_unlock(&si->ms[i]->lock);
		if (rc) {
			last_errno = errno;
			last_rc = rc;
			msglog("Error %d: %s at %s:%d\n", errno,
					strerror(errno), __FILE__, __LINE__);
		}
	}

	if (last_errno)
		errno = last_errno;
	return last_rc;
}

static int flush_store(ldmsd_store_handle_t _sh)
{
	struct sos_store_instance *si = _sh;
	struct sos_metric_store *ms;
	if (!_sh)
		return EINVAL;
	int i;
	LIST_FOREACH(ms, &si->ms_list, entry) {
		pthread_mutex_lock(&ms->lock);
		/* It is possible that a sos was unsuccessfully created. */
		if (ms->sos)
			sos_commit(ms->sos, ODS_COMMIT_ASYNC);
		pthread_mutex_unlock(&ms->lock);
	}
	return 0;
}

static void close_store(ldmsd_store_handle_t _sh)
{
	/*
	 * NOTE: This close function looks like destroy to me.
	 */
	struct sos_store_instance *si = _sh;
	struct sos_metric_store *ms;
	if (!_sh)
		return;
	int i;
	while (ms = LIST_FIRST(&si->ms_list)) {
		LIST_REMOVE(ms, entry);
		if (ms->sos)
			sos_close(ms->sos, ODS_COMMIT_ASYNC);
		if (ms->path)
			free(ms->path);
		free(ms);
	}
	idx_delete(store_idx, (void *)(si->container), strlen(si->container));
	free(si->path);
	free(si->container);
	free(si);
}

static void destroy_store(ldmsd_store_handle_t _sh)
{
	close_store(_sh);
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
	store_idx = idx_create();
	metric_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_sos_fini(void);
static void store_sos_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(store_idx);
	idx_destroy(metric_idx);
}
