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
	LIST_ENTRY(sos_instance) entry;
};
static pthread_mutex_t cfg_lock;
LIST_HEAD(sos_inst_list, sos_instance) inst_list;

static char root_path[PATH_MAX]; /**< store root path */

static ldmsd_msg_log_f msglog;

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
	[LDMS_V_U8_ARRAY] = SOS_TYPE_BYTE_ARRAY,
	[LDMS_V_S8_ARRAY] = SOS_TYPE_BYTE_ARRAY,
	[LDMS_V_U16_ARRAY] = SOS_TYPE_BYTE_ARRAY,
	[LDMS_V_S16_ARRAY] = SOS_TYPE_BYTE_ARRAY,
	[LDMS_V_U32_ARRAY] = SOS_TYPE_UINT32_ARRAY,
	[LDMS_V_S32_ARRAY] = SOS_TYPE_INT32_ARRAY,
	[LDMS_V_U64_ARRAY] = SOS_TYPE_UINT64_ARRAY,
	[LDMS_V_S64_ARRAY] = SOS_TYPE_INT64_ARRAY,
	[LDMS_V_F32_ARRAY] = SOS_TYPE_FLOAT_ARRAY,
	[LDMS_V_D64_ARRAY] = SOS_TYPE_DOUBLE_ARRAY,
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
	pthread_mutex_unlock(&cfg_lock);
	return 0;
einval:
	pthread_mutex_unlock(&cfg_lock);
	return EINVAL;
}

static void term(void)
{
}

static const char *usage(void)
{
	return  "    config name=store_sos path=<path>\n"
		"        - Set the root path for the storage of SOS files.\n"
		"        path           The path to the root of the SOS containers directory\n";
}

static void *get_ucontext(ldmsd_store_handle_t _sh)
{
	struct sos_instance *si = _sh;
	return si->ucontext;
}

static ldmsd_store_handle_t
open_store(struct ldmsd_store *s, const char *container, const char *schema,
	   struct ldmsd_strgp_metric_list *metric_list, void *ucontext)
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
 err3:
	free(si->schema_name);
 err2:
	free(si->container);
 err1:
	free(si);
 out:
	return NULL;
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
	sos_schema_free(schema);
	schema = NULL;
 err_0:
	return schema;
}

static int
_open_store(struct sos_instance *si, ldms_set_t set,
	    int *metric_arry, size_t metric_count)
{
	int rc;
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
	sos_schema_free(schema);
 err_0:
	sos_container_close(si->sos, SOS_COMMIT_ASYNC);
	si->sos = NULL;
	return EINVAL;
}

static inline size_t
__base_byte_len(enum ldms_value_type type)
{
	switch (type) {
	case LDMS_V_S8:
	case LDMS_V_U8:
	case LDMS_V_S8_ARRAY:
	case LDMS_V_U8_ARRAY:
		return 1;
	case LDMS_V_S16:
	case LDMS_V_U16:
	case LDMS_V_S16_ARRAY:
	case LDMS_V_U16_ARRAY:
		return 2;
	case LDMS_V_S32:
	case LDMS_V_U32:
	case LDMS_V_S32_ARRAY:
	case LDMS_V_U32_ARRAY:
	case LDMS_V_F32:
	case LDMS_V_F32_ARRAY:
		return 4;
	case LDMS_V_S64:
	case LDMS_V_U64:
	case LDMS_V_S64_ARRAY:
	case LDMS_V_U64_ARRAY:
	case LDMS_V_D64:
	case LDMS_V_D64_ARRAY:
		return 8;
	default:
		return 0;
	}
}

static inline void
__ldms_sos_array_copy(ldms_set_t set, int i, sos_value_t sos_array, size_t size)
{
	void *sos_dst = sos_array_ptr(sos_array);
	void *ldms_src = ldms_metric_array_get(set, i);
	memcpy(sos_dst, ldms_src, size);
}

static int
store(ldmsd_store_handle_t _sh, ldms_set_t set,
      int *metric_arry, size_t metric_count)
{
	struct sos_instance *si = _sh;
	struct ldms_timestamp _timestamp;
	const struct ldms_timestamp *timestamp;
	sos_attr_t attr;
	SOS_VALUE(value);
	SOS_VALUE(array_value);
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

	/* The first attribute is the timestamp */
	attr = sos_schema_attr_first(si->sos_schema);
	value = sos_value_init(value, obj, attr);
	value->data->prim.timestamp_.fine.secs = timestamp->sec;
	value->data->prim.timestamp_.fine.usecs = timestamp->usec;
	sos_value_put(value);

	/* The second attribute is the component id, that we extract
	 * from the udata for the first LDMS metric */
	uint64_t udata = ldms_metric_user_data_get(set, 0);
	enum ldms_value_type metric_type;
	int array_len;
	int esz;

	attr = sos_schema_attr_next(attr);
	value = sos_value_init(value, obj, attr);
	value->data->prim.uint64_ = udata;
	sos_value_put(value);

	for (i = 0; i < metric_count; i++) {
		attr = sos_schema_attr_next(attr); assert(attr);
		metric_type = ldms_metric_type_get(set, i);
		switch (metric_type) {
		case LDMS_V_S8:
		case LDMS_V_U8:
		case LDMS_V_CHAR:
		case LDMS_V_S16:
		case LDMS_V_U16:
		case LDMS_V_S32:
		case LDMS_V_U32:
		case LDMS_V_S64:
		case LDMS_V_U64:
		case LDMS_V_F32:
		case LDMS_V_D64:
			value = sos_value_init(value, obj, attr);
			sos_value_set[metric_type](value, set, i);
			sos_value_put(value);
			break;
		case LDMS_V_S16_ARRAY:
		case LDMS_V_U16_ARRAY:
			/* there is no s16/u16 array in sos */
			esz = __base_byte_len(metric_type);
			array_len = ldms_metric_array_get_len(set, i);
			array_value = sos_array_new(array_value, attr, obj, array_len*2);
			if (!array_value) {
				goto err;
			}
			__ldms_sos_array_copy(set, i, array_value, esz*array_len);
			sos_value_put(array_value);
			break;
		case LDMS_V_S8_ARRAY:
		case LDMS_V_U8_ARRAY:
		case LDMS_V_S32_ARRAY:
		case LDMS_V_U32_ARRAY:
		case LDMS_V_S64_ARRAY:
		case LDMS_V_U64_ARRAY:
		case LDMS_V_F32_ARRAY:
		case LDMS_V_D64_ARRAY:
			esz = __base_byte_len(metric_type);
			array_len = ldms_metric_array_get_len(set, i);
			array_value = sos_array_new(array_value, attr, obj, array_len);
			if (!array_value) {
				goto err;
			}
			__ldms_sos_array_copy(set, i, array_value, esz*array_len);
			sos_value_put(array_value);
			break;
		default:
			assert(0 == "Unexpected type");
			break;
		}
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
err:
	return errno;
}

static int flush_store(ldmsd_store_handle_t _sh)
{
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
