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
#include <assert.h>
#include <sos/sos.h>
#include "ldms.h"
#include "ldmsd.h"

#define LDMSD_SOS_POSTROT "LDMSD_SOS_POSTROTATE"

/*
 * According to 'man useradd' and 'man groupadd'
 * the max length of the user/group name is 32
 */
#define MAX_USER_GROUP_LEN	32
#define MAX_OWNER	(MAX_USER_GROUP_LEN * 2 + 1)

/*
 * NOTE:
 *   <sos::path> = <root_path>/<container>
 */

struct sos_instance {
	struct ldmsd_store *store;
	char *container;
	char *schema_name;
	char *path; /**< <root_path>/<container> */
	void *ucontext;
	sos_t sos; /**< sos handle */
	sos_schema_t sos_schema;
	pthread_mutex_t lock; /**< lock at metric store level */
	uint32_t last_rotate; /**< Last rotation timestamp */
	LIST_ENTRY(sos_instance) entry;
};
static pthread_mutex_t cfg_lock;
LIST_HEAD(sos_inst_list, sos_instance) inst_list;

static char root_path[PATH_MAX]; /**< store root path */

static ldmsd_msg_log_f msglog;

static time_t time_limit = 0;
static int max_copy = 1;
static size_t init_size = 4 * 1024 * 1024; /* default size 4MB */
static char owner[MAX_OWNER];

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

sos_type_t sos_type_map[] = {
	[LDMS_V_NONE] = SOS_TYPE_UINT32,
	[LDMS_V_U8] = SOS_TYPE_UINT32,
	[LDMS_V_S8] = SOS_TYPE_INT32,
	[LDMS_V_U16] = SOS_TYPE_UINT32,
	[LDMS_V_S16] = SOS_TYPE_UINT32,
	[LDMS_V_U32] = SOS_TYPE_UINT32,
	[LDMS_V_S32] = SOS_TYPE_INT32,
	[LDMS_V_U64] = SOS_TYPE_UINT64,
	[LDMS_V_S64] = SOS_TYPE_INT64,
	[LDMS_V_F32] = SOS_TYPE_FLOAT,
	[LDMS_V_D64] = SOS_TYPE_DOUBLE,
};

static void set_none_fn(sos_value_t v, ldms_set_t set, int i) {
	assert(0 == "Invalid LDMS metric type");
}
static void set_u8_fn(sos_value_t v, ldms_set_t s, int i) {
	v->data->prim.uint32_ = ldms_metric_get_u8(s, i);
}
static void set_s8_fn(sos_value_t v, ldms_set_t s, int i) {
	v->data->prim.int32_ = ldms_metric_get_s8(s, i);
}
static void set_u16_fn(sos_value_t v, ldms_set_t s, int i) {
	v->data->prim.uint32_ = ldms_metric_get_u16(s, i);
}
static void set_s16_fn(sos_value_t v, ldms_set_t s, int i) {
	v->data->prim.int32_ = ldms_metric_get_s16(s, i);
}
static void set_u32_fn(sos_value_t v, ldms_set_t s, int i) {
	v->data->prim.uint32_ = ldms_metric_get_u32(s, i);
}
static void set_s32_fn(sos_value_t v, ldms_set_t s, int i) {
	v->data->prim.int32_ = ldms_metric_get_s32(s, i);
}
static void set_u64_fn(sos_value_t v, ldms_set_t s, int i) {
	v->data->prim.uint64_ = ldms_metric_get_u64(s, i);
}
static void set_s64_fn(sos_value_t v, ldms_set_t s, int i) {
	v->data->prim.int64_ = ldms_metric_get_s64(s, i);
}
static void set_float_fn(sos_value_t v, ldms_set_t s, int i) {
	v->data->prim.float_ = ldms_metric_get_float(s, i);
}
static void set_double_fn(sos_value_t v, ldms_set_t s, int i) {
	v->data->prim.double_ = ldms_metric_get_double(s, i);
}

typedef void (*sos_value_set_fn)(sos_value_t v, ldms_set_t set, int i);
sos_value_set_fn sos_value_set[] = {
	[LDMS_V_NONE] = set_none_fn,
	[LDMS_V_U8] = set_u8_fn,
	[LDMS_V_S8] = set_s8_fn,
	[LDMS_V_U16] = set_u16_fn,
	[LDMS_V_S16] = set_s16_fn,
	[LDMS_V_U32] = set_u32_fn,
	[LDMS_V_S32] = set_s32_fn,
	[LDMS_V_U64] = set_u64_fn,
	[LDMS_V_S64] = set_s64_fn,
	[LDMS_V_F32] = set_float_fn,
	[LDMS_V_D64] = set_double_fn,
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
			msglog(LDMSD_LERROR, "store_sos: Error %d: Changing owner "
					"%s to %s\n", errno, owner);
			return -1;
		}
	}
	return rc;
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
			msglog(LDMSD_LERROR, "store_sos: 'owner' (%s) exceeds %d "
					"characters.\n", value, MAX_OWNER);
			goto einval;
		}
		snprintf(owner, MAX_OWNER, "%s", value);
	} else {
		owner[0] = '\0';
	}
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
		"        path           The path to the root of the SOS containers directory\n"
		"	 user:group     Optional. Store_sos will 'chown -R user:group <path>'\n"
		"	 metric_names   Optional. The format is <metric_name(type)>,<metric_name(type)>,...\n"
		"			If this is given the store sos of the metrics will be created\n"
		"			when store_sos is configured, otherwise it will be created when the\n"
		"                       first update completes.\n";
}

static void *get_ucontext(ldmsd_store_handle_t _sh)
{
	struct sos_instance *si = _sh;
	return si->ucontext;
}

static ldmsd_store_handle_t
open_store(struct ldmsd_store *s, const char *container, const char *schema,
	   struct ldmsd_store_metric_list *metric_list, void *ucontext)
{
	struct sos_instance *si = NULL;

	si = calloc(1, sizeof(*si));
	if (!si)
		goto out;
	si->ucontext = ucontext;
	si->container = strdup(container);
	if (!si->container)
		goto err1;
	si->schema_name = strdup(schema);
	if (!si->schema_name)
		goto err2;
	size_t pathlen =
		strlen(root_path) + strlen(si->container) + 4;
	si->path = malloc(pathlen);
	if (!si->path)
		goto err3;
	sprintf(si->path, "/%s/%s", root_path, container);
	pthread_mutex_init(&si->lock, NULL);
	pthread_mutex_lock(&cfg_lock);
	LIST_INSERT_HEAD(&inst_list, si, entry);
	pthread_mutex_unlock(&cfg_lock);
	return si;
 err4:
	free(si->path);
 err3:
	free(si->schema_name);
 err2:
	free(si->container);
 err1:
	free(si);
 out:
	return NULL;
}

void store_sos_cleanup(sos_t sos, uint32_t sec)
{
#if 0
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
#endif
}

static sos_schema_t
create_schema(struct sos_instance *si, ldms_set_t set,
	      int *metric_arry, size_t metric_count)
{
	int rc, i;
	sos_schema_t schema = sos_schema_new(si->schema_name);
	if (!schema)
		goto err_0;

	rc = sos_schema_attr_add(schema, "Timestamp", SOS_TYPE_TIMESTAMP);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "Timestamp");
	if (rc)
		goto err_1;
	rc = sos_schema_attr_add(schema, "CompId", SOS_TYPE_UINT64);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "CompId");
	if (rc)
		goto err_1;
	for (i = 0; i < metric_count; i++) {
		rc = sos_schema_attr_add(schema,
					 ldms_metric_name_get(set, i),
					 sos_type_map[ldms_metric_type_get(set, i)]);
		if (rc)
			goto err_1;
	}
	return schema;
 err_1:
	sos_schema_put(schema);
	schema = NULL;
 err_0:
	return schema;
}

static int
_open_store(struct sos_instance *si, ldms_set_t set,
	    int *metric_arry, size_t metric_count)
{
	int rc, i;
	struct sos_metric_store *ms;
	sos_schema_t schema;

	si->sos = sos_container_open(si->path, SOS_PERM_RW);
	if (si->sos) {
		schema = sos_schema_by_name(si->sos, si->schema_name);
		if (!schema)
			goto add_schema;
		si->sos_schema = schema;
		return 0;
	}

	/* Create the SOS container */
	rc = sos_container_new(si->path, 0660);
	if (rc)
		return rc;
	store_sos_change_owner(si->path);
	si->sos = sos_container_open(si->path, SOS_PERM_RW);
	if (!si->sos)
		return errno;
 add_schema:
	schema = create_schema(si, set, metric_arry, metric_count);
	if (!schema)
		goto err_0;
	rc = sos_schema_add(si->sos, schema);
	if (rc)
		goto err_1;
	si->sos_schema = schema;
	return 0;
 err_1:
	sos_schema_put(schema);
 err_0:
	sos_container_close(si->sos, SOS_COMMIT_ASYNC);
	si->sos = NULL;
	return EINVAL;
}

static int
store(ldmsd_store_handle_t _sh, ldms_set_t set,
      int *metric_arry, size_t metric_count)
{
	struct sos_instance *si = _sh;
	struct ldms_timestamp _timestamp;
	const struct ldms_timestamp *timestamp;
	sos_attr_t attr;
	struct sos_value_s value_;
	sos_value_t value;
	sos_obj_t obj;
	int i;
	int rc = 0;
	int last_rc = 0;
	int last_errno = 0;

	if (!si)
		return EINVAL;

	pthread_mutex_lock(&si->lock);
	if (!si->sos) {
		rc = _open_store(si, set, metric_arry, metric_count);
		if (rc) {
			pthread_mutex_unlock(&si->lock);
			msglog(LDMSD_LERROR, "store_sos: Failed to create store "
			       "for %s.\n", si->container);
			errno = rc;
			return -1;
		}
	}
	obj = sos_obj_new(si->sos_schema);
	if (!obj) {
		pthread_mutex_unlock(&si->lock);
		msglog(LDMSD_LERROR, "Error %d: %s at %s:%d\n", errno,
		       strerror(errno), __FILE__, __LINE__);
		errno = ENOMEM;
		return -1;
	}
	_timestamp = ldms_transaction_timestamp_get(set);
	timestamp = &_timestamp;
	if (!si->last_rotate)
		si->last_rotate = timestamp->sec;
#if 0
	if (time_limit && (ts->sec / time_limit) > (si->last_rotate / time_limit)) {
		sos_t new_sos = sos_rotate(si->sos, max_copy);
		if (new_sos) {
			si->sos = new_sos;
			si->last_rotate = ts->sec;
			sos_post_rotation(new_sos, LDMSD_SOS_POSTROT);
		} else {
			msglog(LDMSD_LINFO, "WARN: sos_rotate failed: %s\n",
					si->path);
		}
	}
#endif
	/* The first attribute is the timestamp */
	attr = sos_schema_attr_first(si->sos_schema);
	value = sos_value_init(&value_, obj, attr);
	value->data->prim.timestamp_.fine.secs = timestamp->sec;
	value->data->prim.timestamp_.fine.usecs = timestamp->usec;
	sos_value_put(value);

	/* The second attribute is the component id, that we extract
	 * from the udata for the first LDMS metric */
	uint64_t udata = ldms_metric_user_data_get(set, 0);
	attr = sos_schema_attr_next(attr);
	value = sos_value_init(value, obj, attr);
	value->data->prim.uint64_ = udata;
	sos_value_put(value);

	for (i = 0; i < metric_count; i++) {
		attr = sos_schema_attr_next(attr); assert(attr);
		value = sos_value_init(value, obj, attr);
		sos_value_set[ldms_metric_type_get(set, i)](value, set, i);
		sos_value_put(value);
	}
	rc = sos_obj_index(obj);
	sos_obj_put(obj);
	if (rc) {
		last_errno = errno;
		last_rc = rc;
		msglog(LDMSD_LERROR, "Error %d: %s at %s:%d\n", errno,
		       strerror(errno), __FILE__, __LINE__);
	}
	if (last_errno)
		errno = last_errno;
	pthread_mutex_unlock(&si->lock);
	return last_rc;
}

static int flush_store(ldmsd_store_handle_t _sh)
{
	int i;
	struct sos_instance *si = _sh;
	if (!_sh)
		return EINVAL;
	pthread_mutex_lock(&si->lock);
	/* It is possible that a sos was unsuccessfully created. */
	if (si->sos)
		sos_container_commit(si->sos, SOS_COMMIT_ASYNC);
	pthread_mutex_unlock(&si->lock);
	return 0;
}

static void close_store(ldmsd_store_handle_t _sh)
{
	int i;
	struct sos_instance *si = _sh;

	if (!si)
		return;

	pthread_mutex_lock(&cfg_lock);
	LIST_REMOVE(si, entry);
	pthread_mutex_unlock(&cfg_lock);

	if (si->sos)
		sos_container_close(si->sos, SOS_COMMIT_ASYNC);
	if (si->path)
		free(si->path);
	if (si->sos_schema)
		sos_schema_put(si->sos_schema);
	free(si->container);
	free(si->schema_name);
	free(si);
}

static struct ldmsd_store store_sos = {
	.base = {
		.name = "sos",
		.term = term,
		.config = config,
		.usage = usage,
	},
	.open = open_store,
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
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_sos_fini(void);
static void store_sos_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	/* TODO: clean up container and metric trees */
}
