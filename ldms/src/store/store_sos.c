/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2012-2018 Open Grid Computing, Inc. All rights reserved.
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

/*
 * sos_handle_s structure, to share sos among multiple sos instances that refers
 * to the same container.
 */
typedef struct sos_handle_s {
	int ref_count;
	char path[PATH_MAX];
	sos_t sos;
	LIST_ENTRY(sos_handle_s) entry;
} *sos_handle_t;

static LIST_HEAD(sos_handle_list, sos_handle_s) sos_handle_list;

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
	sos_handle_t sos_handle; /**< sos handle */
	sos_schema_t sos_schema;
	pthread_mutex_t lock; /**< lock at metric store level */

	int job_id_idx;
	int comp_id_idx;
	sos_attr_t ts_attr;
	sos_attr_t comp_id;
	sos_attr_t comp_time_attr;
	sos_attr_t job_id;
	sos_attr_t job_comp_time_attr;
	sos_attr_t first_attr;

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
	[LDMS_V_U16] = SOS_TYPE_UINT16,
	[LDMS_V_S16] = SOS_TYPE_UINT16,
	[LDMS_V_U32] = SOS_TYPE_UINT32,
	[LDMS_V_S32] = SOS_TYPE_INT32,
	[LDMS_V_U64] = SOS_TYPE_UINT64,
	[LDMS_V_S64] = SOS_TYPE_INT64,
	[LDMS_V_F32] = SOS_TYPE_FLOAT,
	[LDMS_V_D64] = SOS_TYPE_DOUBLE,
	[LDMS_V_CHAR_ARRAY] = SOS_TYPE_CHAR_ARRAY,
	[LDMS_V_U8_ARRAY] = SOS_TYPE_BYTE_ARRAY,
	[LDMS_V_S8_ARRAY] = SOS_TYPE_BYTE_ARRAY,
	[LDMS_V_U16_ARRAY] = SOS_TYPE_UINT16_ARRAY,
	[LDMS_V_S16_ARRAY] = SOS_TYPE_INT16_ARRAY,
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
	v->data->prim.uint16_ = ldms_metric_get_u16(s, i);
}
static void set_s16_fn(sos_value_t v, ldms_set_t s, int i) {
	v->data->prim.int16_ = ldms_metric_get_s16(s, i);
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

sos_handle_t create_handle(const char *path, sos_t sos)
{
	sos_handle_t h = calloc(1, sizeof(*h));
	if (!h)
		return NULL;
	strncpy(h->path, path, sizeof(h->path));
	h->ref_count = 1;
	h->sos = sos;
	pthread_mutex_lock(&cfg_lock);
	LIST_INSERT_HEAD(&sos_handle_list, h, entry);
	pthread_mutex_unlock(&cfg_lock);
	return h;
}

sos_handle_t create_container(const char *path)
{
	int rc = 0;
	sos_t sos;
	time_t t;
	char part_name[16];	/* Unix timestamp as string */
	sos_part_t part;

	rc = sos_container_new(path, 0660);
	if (rc) {
		msglog(LDMSD_LERROR, "Error %d creating the container at '%s'\n",
		       rc, path);
		goto err_0;
	}
	sos = sos_container_open(path, SOS_PERM_RW);
	if (!sos) {
		msglog(LDMSD_LERROR, "Error %d opening the container at '%s'\n",
		       errno, path);
		goto err_0;
	}
	/*
	 * Create the first partition. All other partitions and
	 * rollover are handled with the SOS partition commands
	 */
	t = time(NULL);
	sprintf(part_name, "%d", (unsigned int)t);
	rc = sos_part_create(sos, part_name, path);
	if (rc) {
		msglog(LDMSD_LERROR, "Error %d creating the partition '%s' in '%s'\n",
		       rc, part_name, path);
		goto err_1;
	}
	part = sos_part_find(sos, part_name);
	if (!part) {
		msglog(LDMSD_LERROR, "Newly created partition was not found\n");
		goto err_1;
	}
	rc = sos_part_state_set(part, SOS_PART_STATE_PRIMARY);
	if (rc) {
		msglog(LDMSD_LERROR, "New partition could not be made primary\n");
		goto err_2;
	}
	sos_part_put(part);
	return create_handle(path, sos);
 err_2:
	sos_part_put(part);
 err_1:
	sos_container_close(sos, SOS_COMMIT_ASYNC);
 err_0:
	if (rc)
		errno = rc;
	return NULL;
}

static void close_container(sos_handle_t h)
{
	assert(h->ref_count == 0);
	sos_container_close(h->sos, SOS_COMMIT_ASYNC);
	free(h);
}

static void put_container_no_lock(sos_handle_t h)
{
	h->ref_count--;
	if (h->ref_count == 0) {
		/* remove from list, destroy the handle */
		LIST_REMOVE(h, entry);
		close_container(h);
	}
}

static void put_container(sos_handle_t h)
{
	pthread_mutex_lock(&cfg_lock);
	put_container_no_lock(h);
	pthread_mutex_unlock(&cfg_lock);
}

static sos_handle_t find_container(const char *path)
{
	sos_handle_t h;
	LIST_FOREACH(h, &sos_handle_list, entry){
		if (0 != strncmp(path, h->path, sizeof(h->path)))
			continue;

		/* found */
		/* take reference */
		pthread_mutex_lock(&cfg_lock);
		h->ref_count++;
		pthread_mutex_unlock(&cfg_lock);
		return h;
	}
	return NULL;
}

/**
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	struct sos_instance *si;
	int rc;
	char *value;
	value = av_value(avl, "path");
	if (!value)
		return EINVAL;

	pthread_mutex_lock(&cfg_lock);
	strncpy(root_path, value, PATH_MAX);

	/* Run through all open containers and close them. They will
	 * get re-opened when store() is next called
	 */
	rc = ENOMEM;
	LIST_FOREACH(si, &inst_list, entry) {
		pthread_mutex_lock(&si->lock);
		if (si->sos_handle) {
			put_container_no_lock(si->sos_handle);
			si->sos_handle = NULL;
		}
		size_t pathlen =
			strlen(root_path) + strlen(si->container) + 4;
		if (si->path)
			free(si->path);
		si->path = malloc(pathlen);
		if (!si->path)
			goto err_0;
		sprintf(si->path, "%s/%s", root_path, si->container);
		pthread_mutex_unlock(&si->lock);
	}
	pthread_mutex_unlock(&cfg_lock);
	return 0;

 err_0:
	pthread_mutex_unlock(&cfg_lock);
	return rc;
}

static void term(struct ldmsd_plugin *self)
{
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=store_sos path=<path>\n"
		"       path The path to primary storage\n";
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
	sprintf(si->path, "%s/%s", root_path, container);
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

	/*
	 * Time Index
	 */
	rc = sos_schema_attr_add(schema, "timestamp", SOS_TYPE_TIMESTAMP);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "timestamp");
	if (rc)
		goto err_1;
	rc = sos_schema_attr_add(schema, "component_id", SOS_TYPE_UINT64);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "component_id");
	if (rc)
		goto err_1;
	rc = sos_schema_attr_add(schema, "job_id", SOS_TYPE_UINT64);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "job_id");
	if (rc)
		goto err_1;

	/* The metrics from the set _must_ come before the index
	 * attributes or the store method will fail attempting to set
	 * an index attribute from a LDMS metric set value
	 */
	for (i = 0; i < metric_count; i++) {
		if (0 == strcmp("timestamp", ldms_metric_name_get(set, metric_arry[i])))
			continue;
		if (0 == strcmp("component_id", ldms_metric_name_get(set, metric_arry[i])))
			continue;
		if (0 == strcmp("job_id", ldms_metric_name_get(set, metric_arry[i])))
			continue;
		msglog(LDMSD_LINFO, "Adding attribute %s to the schema\n",
		       ldms_metric_name_get(set, metric_arry[i]));
		rc = sos_schema_attr_add(schema,
			ldms_metric_name_get(set, metric_arry[i]),
			sos_type_map[ldms_metric_type_get(set, metric_arry[i])]);
		if (rc)
			goto err_1;
	}

	/*
	 * Component/Time Index
	 */
	char *comp_time_attrs[] = { "component_id", "timestamp" };
	rc = sos_schema_attr_add(schema, "comp_time", SOS_TYPE_JOIN, 2, comp_time_attrs);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "comp_time");
	if (rc)
		goto err_1;
	/*
	 * Job/Component/Time Index
	 */
	char *job_comp_time_attrs[] = { "job_id", "component_id", "timestamp" };
	rc = sos_schema_attr_add(schema, "job_comp_time", SOS_TYPE_JOIN, 3, job_comp_time_attrs);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "job_comp_time");
	if (rc)
		goto err_1;

	/*
	 * Job/Time/Component Index
	 */
	char *job_time_comp_attrs[] = { "job_id", "timestamp", "component_id" };
	rc = sos_schema_attr_add(schema, "job_time_comp", SOS_TYPE_JOIN, 3, job_time_comp_attrs);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "job_time_comp");
	if (rc)
		goto err_1;

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

	/* Check if the container is already open */
	si->sos_handle = find_container(si->path);
	if (si->sos_handle) {
		/* See if the required schema is already present */
		schema = sos_schema_by_name(si->sos_handle->sos, si->schema_name);
		if (!schema)
			goto add_schema;
		si->sos_schema = schema;
		return 0;
	}
	/* See if it exists, but has not been opened yet. */
	sos_t sos = sos_container_open(si->path, SOS_PERM_RW);
	if (sos) {
		/* Create a new handle and add it for this SOS */
		si->sos_handle = create_handle(si->path, sos);
		if (!si->sos_handle) {
			sos_container_close(sos, SOS_COMMIT_ASYNC);
			return ENOMEM;
		}

		/* See if the schema exists */
		schema = sos_schema_by_name(sos, si->schema_name);
		if (!schema)
			goto add_schema;
		si->sos_schema = schema;
		return 0;
	}

	si->sos_handle = create_container(si->path);
	if (!si->sos_handle) {
		return errno;
	}

 add_schema:
	schema = create_schema(si, set, metric_arry, metric_count);
	if (!schema)
		goto err_0;
	rc = sos_schema_add(si->sos_handle->sos, schema);
	if (rc) {
		sos_schema_free(schema);
		if (rc == EEXIST) {
			/* Added by our failover peer? */
			schema = sos_schema_by_name(si->sos_handle->sos,
						    si->schema_name);
			if (schema)
				goto out;
		}
		msglog(LDMSD_LERROR, "Error %d adding the schema to the container\n", rc);
		goto err_1;
	}
 out:
	si->sos_schema = schema;
	return 0;
 err_1:
	sos_schema_free(schema);
 err_0:
	put_container(si->sos_handle);
	return EINVAL;
}

static size_t __element_byte_len_[] = {
	[LDMS_V_NONE] = 0,
	[LDMS_V_CHAR] = 1,
	[LDMS_V_U8] = 1,
	[LDMS_V_S8] = 1,
	[LDMS_V_U16] = 2,
	[LDMS_V_S16] = 2,
	[LDMS_V_U32] = 4,
	[LDMS_V_S32] = 4,
	[LDMS_V_U64] = 8,
	[LDMS_V_S64] = 8,
	[LDMS_V_F32] = 4,
	[LDMS_V_D64] = 8,
	[LDMS_V_CHAR_ARRAY] = 1,
	[LDMS_V_U8_ARRAY] = 1,
	[LDMS_V_S8_ARRAY] = 1,
	[LDMS_V_U16_ARRAY] = 2,
	[LDMS_V_S16_ARRAY] = 2,
	[LDMS_V_U32_ARRAY] = 4,
	[LDMS_V_S32_ARRAY] = 4,
	[LDMS_V_F32_ARRAY] = 4,
	[LDMS_V_U64_ARRAY] = 8,
	[LDMS_V_S64_ARRAY] = 8,
	[LDMS_V_D64_ARRAY] = 8,
};

static inline size_t __element_byte_len(enum ldms_value_type t)
{
	if (t < LDMS_V_FIRST || t > LDMS_V_LAST)
		assert(0 == "Invalid type specified");
	return __element_byte_len_[t];
}

static int
store(ldmsd_store_handle_t _sh, ldms_set_t set,
      int *metric_arry, size_t metric_count)
{
	struct sos_instance *si = _sh;
	struct ldms_timestamp timestamp;
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
	if (!si->sos_handle) {
		rc = _open_store(si, set, metric_arry, metric_count);
		if (rc) {
			pthread_mutex_unlock(&si->lock);
			msglog(LDMSD_LERROR, "store_sos: Failed to create store "
			       "for %s.\n", si->container);
			errno = rc;
			return -1;
		}
		si->job_id_idx = ldms_metric_by_name(set, "job_id");
		si->comp_id_idx = ldms_metric_by_name(set, "component_id");
		si->ts_attr = sos_schema_attr_by_name(si->sos_schema, "timestamp");
		si->job_comp_time_attr = sos_schema_attr_by_name(si->sos_schema, "job_comp_time");
		si->comp_time_attr = sos_schema_attr_by_name(si->sos_schema, "comp_time");
		si->first_attr = sos_schema_attr_by_name(si->sos_schema,
				ldms_metric_name_get(set, metric_arry[0]));
		if (si->comp_id_idx < 0)
			msglog(LDMSD_LINFO,
			       "The component_id is missing from the metric set/schema.\n");
		if (si->job_id_idx < 0)
			msglog(LDMSD_LERROR,
			       "The job_id is missing from the metric set/schema.\n");
		assert(si->comp_time_attr);
		assert(si->job_comp_time_attr);
		assert(si->ts_attr);
	}
	obj = sos_obj_new(si->sos_schema);
	if (!obj) {
		pthread_mutex_unlock(&si->lock);
		msglog(LDMSD_LERROR, "Error %d: %s at %s:%d\n", errno,
		       strerror(errno), __FILE__, __LINE__);
		errno = ENOMEM;
		return -1;
	}
	timestamp = ldms_transaction_timestamp_get(set);

	/* timestamp */
	value = sos_value_init(value, obj, si->ts_attr);
	value->data->prim.timestamp_.fine.secs = timestamp.sec;
	value->data->prim.timestamp_.fine.usecs = timestamp.usec;
	sos_value_put(value);

	enum ldms_value_type metric_type;
	int array_len;
	int esz;

	/*
	 * The assumption is that the metrics in the SOS schema have the same
	 * order as in metric_arry.
	 */
	for (i = 0, attr = si->first_attr; i < metric_count;
	     i++, attr = sos_schema_attr_next(attr)) {
		size_t count;
		if (!attr) {
			errno = E2BIG;
			msglog(LDMSD_LERROR,
			       "The set '%s' with schema '%s' has fewer "
			       "attributes than the SOS schema '%s' to which "
			       "it is being stored.\n",
			       ldms_set_instance_name_get(set),
			       ldms_set_schema_name_get(set),
			       sos_schema_name(si->sos_schema),
			       __FILE__, __LINE__);
			goto err;
		}
		metric_type = ldms_metric_type_get(set, metric_arry[i]);
		if (metric_type < LDMS_V_CHAR_ARRAY) {
			value = sos_value_init(value, obj, attr);
			sos_value_set[metric_type](value, set, metric_arry[i]);
			sos_value_put(value);
		} else {
			esz = __element_byte_len(metric_type);
			if (metric_type == LDMS_V_CHAR_ARRAY) {
				ldms_mval_t mval = ldms_metric_array_get(set, metric_arry[i]);
				array_len = strlen(mval->a_char);
				if (array_len > ldms_metric_array_get_len(set, metric_arry[i])) {
					array_len = ldms_metric_array_get_len(set, metric_arry[i]);
				}
			} else {
				array_len = ldms_metric_array_get_len(set, metric_arry[i]);
			}
			array_value = sos_array_new(array_value, attr,
							obj, array_len);
			if (!array_value) {
				goto err;
			}
			array_len *= esz;
			count = sos_value_memcpy(array_value,
					ldms_metric_array_get(set,
						metric_arry[i]),
						array_len);
			assert(count == array_len);
			sos_value_put(array_value);
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
	pthread_mutex_unlock(&si->lock);
	return errno;
}

static int flush_store(ldmsd_store_handle_t _sh)
{
	struct sos_instance *si = _sh;
	if (!_sh)
		return EINVAL;
	pthread_mutex_lock(&si->lock);
	/* It is possible that a sos was unsuccessfully created. */
	if (si->sos_handle)
		sos_container_commit(si->sos_handle->sos, SOS_COMMIT_ASYNC);
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

	if (si->sos_handle)
		put_container(si->sos_handle);
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
		.type = LDMSD_PLUGIN_STORE,
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
	LIST_INIT(&sos_handle_list);
}

static void __attribute__ ((destructor)) store_sos_fini(void);
static void store_sos_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	/* TODO: clean up container and metric trees */
}
