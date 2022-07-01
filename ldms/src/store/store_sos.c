/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2018,2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2012-2018,2022 Open Grid Computing, Inc. All rights reserved.
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
#define _GNU_SOURCE
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
#include <coll/rbt.h>
#include "ldms.h"
#include "ldmsd.h"

#define LOG_(level, ...) do { \
	msglog(level, "store_sos: "__VA_ARGS__); \
} while(0);

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

struct sos_single_list_records {
	int list_mid;
};

struct sos_lists_of_records {
	int num_lists;
	int *list_mid;
	int *rec_len;
};

struct sos_list {
	int list_mid; /* Metric index of each list */
	enum ldms_value_type mtype; /* Value type of the list entries */
	size_t count; /* The length of records or arrays, 1 otherwise */
};

struct sos_list_ctxt {
	int num_lists;
	struct sos_list *list;
};

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
	int store_flags;
	enum store_sos_mode {
		STORE_SOS_M_BASIC = 0,
		STORE_SOS_M_LISTS,
		STORE_SOS_M_LAST
	} mode;
	union {
		struct sos_single_list_records slist;
		struct sos_lists_of_records lrec;
		struct sos_list_ctxt lists;
	} ctxt;
	pthread_mutex_t lock; /**< lock at metric store level */

	int job_id_idx;
	int comp_id_idx;
	sos_attr_t ts_attr;
	sos_attr_t comp_id;
	sos_attr_t job_id;
	sos_attr_t first_attr;

	LIST_ENTRY(sos_instance) entry;

	struct rbt schema_rbt;
};
static pthread_mutex_t cfg_lock;
LIST_HEAD(sos_inst_list, sos_instance) inst_list;

static char root_path[PATH_MAX]; /**< store root path */
static ldmsd_msg_log_f msglog __attribute__(( format(printf, 2, 3) ));
time_t timeout = 5;		/* Default is 5 seconds */

struct row_schema_key_s {
	const struct ldms_digest_s *digest;
	const char *name;
};

struct row_schema_rbn_s {
	struct rbn rbn;
	struct row_schema_key_s key;
	struct ldms_digest_s digest;
	char name[128];
	sos_schema_t sos_schema;
};

static int row_schema_rbn_cmp(void *tree_key, const void *key)
{
	const struct row_schema_key_s *tk = tree_key, *k = key;
	int rc;
	/* compare digest first */
	rc = memcmp(tk->digest, k->digest, sizeof(*k->digest));
	if (rc)
		return rc;
	/* then compare name */
	return strcmp(tk->name, k->name);
}

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

sos_type_t sos_type_map[] = {
	[LDMS_V_NONE] = SOS_TYPE_UINT32,
	[LDMS_V_CHAR] = SOS_TYPE_UINT32,
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
	[LDMS_V_TIMESTAMP] = SOS_TYPE_TIMESTAMP,
};

static inline sos_type_t
sos_type_from_ldms_type(enum ldms_value_type ldms_type)
{
	if ((LDMS_V_NONE < ldms_type && ldms_type <= LDMS_V_D64_ARRAY) ||
	    ldms_type == LDMS_V_TIMESTAMP)
		return sos_type_map[ldms_type];
	return -1;
}

const char *_sos_type_sym_tbl[] = {
	[SOS_TYPE_INT16]             = "SOS_TYPE_INT16",
	[SOS_TYPE_INT32]             = "SOS_TYPE_INT32",
	[SOS_TYPE_INT64]             = "SOS_TYPE_INT64",
	[SOS_TYPE_UINT16]            = "SOS_TYPE_UINT16",
	[SOS_TYPE_UINT32]            = "SOS_TYPE_UINT32",
	[SOS_TYPE_UINT64]            = "SOS_TYPE_UINT64",
	[SOS_TYPE_FLOAT]             = "SOS_TYPE_FLOAT",
	[SOS_TYPE_DOUBLE]            = "SOS_TYPE_DOUBLE",
	[SOS_TYPE_LONG_DOUBLE]       = "SOS_TYPE_LONG_DOUBLE",
	[SOS_TYPE_TIMESTAMP]         = "SOS_TYPE_TIMESTAMP",
	[SOS_TYPE_OBJ]               = "SOS_TYPE_OBJ",
	[SOS_TYPE_STRUCT]            = "SOS_TYPE_STRUCT",
	[SOS_TYPE_JOIN]              = "SOS_TYPE_JOIN",
	[SOS_TYPE_BYTE_ARRAY]        = "SOS_TYPE_BYTE_ARRAY",
	[SOS_TYPE_CHAR_ARRAY]        = "SOS_TYPE_CHAR_ARRAY",
	[SOS_TYPE_INT16_ARRAY]       = "SOS_TYPE_INT16_ARRAY",
	[SOS_TYPE_INT32_ARRAY]       = "SOS_TYPE_INT32_ARRAY",
	[SOS_TYPE_INT64_ARRAY]       = "SOS_TYPE_INT64_ARRAY",
	[SOS_TYPE_UINT16_ARRAY]      = "SOS_TYPE_UINT16_ARRAY",
	[SOS_TYPE_UINT32_ARRAY]      = "SOS_TYPE_UINT32_ARRAY",
	[SOS_TYPE_UINT64_ARRAY]      = "SOS_TYPE_UINT64_ARRAY",
	[SOS_TYPE_FLOAT_ARRAY]       = "SOS_TYPE_FLOAT_ARRAY",
	[SOS_TYPE_DOUBLE_ARRAY]      = "SOS_TYPE_DOUBLE_ARRAY",
	[SOS_TYPE_LONG_DOUBLE_ARRAY] = "SOS_TYPE_LONG_DOUBLE_ARRAY",
	[SOS_TYPE_OBJ_ARRAY]         = "SOS_TYPE_OBJ_ARRAY",
};

static inline const char *
sos_type_sym(sos_type_t sos_type)
{
	const char *sym;
	if (sos_type <= SOS_TYPE_LAST) {
		sym = _sos_type_sym_tbl[sos_type];
		if (sym)
			return sym;
	}
	return "UNKNOWN";
}

static void set_none_fn(sos_value_t v, ldms_mval_t mval) {
	assert(0 == "Invalid LDMS metric type");
}
static void set_u8_fn(sos_value_t v, ldms_mval_t mval) {
	v->data->prim.uint32_ = mval->v_u8;
}
static void set_s8_fn(sos_value_t v, ldms_mval_t mval) {
	v->data->prim.int32_ = mval->v_s8;
}
static void set_u16_fn(sos_value_t v, ldms_mval_t mval) {
	v->data->prim.uint16_ = mval->v_u16;
}
static void set_s16_fn(sos_value_t v, ldms_mval_t mval) {
	v->data->prim.int16_ = mval->v_s16;
}
static void set_char_fn(sos_value_t v, ldms_mval_t mval) {
	v->data->prim.uint32_ = mval->v_char;
}
static void set_u32_fn(sos_value_t v, ldms_mval_t mval) {
	v->data->prim.uint32_ = mval->v_u32;
}
static void set_s32_fn(sos_value_t v, ldms_mval_t mval) {
	v->data->prim.int32_ = mval->v_s32;
}
static void set_u64_fn(sos_value_t v, ldms_mval_t mval) {
	v->data->prim.uint64_ = mval->v_u64;
}
static void set_s64_fn(sos_value_t v, ldms_mval_t mval) {
	v->data->prim.int64_ = mval->v_s64;
}
static void set_float_fn(sos_value_t v, ldms_mval_t mval) {
	v->data->prim.float_ = mval->v_f;
}
static void set_double_fn(sos_value_t v, ldms_mval_t mval) {
	v->data->prim.double_ = mval->v_d;
}

typedef void (*sos_value_set_fn)(sos_value_t v, ldms_mval_t mval);
sos_value_set_fn sos_value_set[] = {
	[LDMS_V_NONE] = set_none_fn,
	[LDMS_V_CHAR] = set_char_fn,
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

typedef void (*sos_mval_set_fn)(sos_value_t, ldms_mval_t);
static void sos_mval_set_char(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.uint32_ = mval->v_char;
}

static void sos_mval_set_u8(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.uint32_ = mval->v_u8;
}

static void sos_mval_set_s8(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.int32_ = mval->v_s8;
}

static void sos_mval_set_u16(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.uint16_ = mval->v_u16;
}

static void sos_mval_set_s16(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.int16_ = mval->v_s16;
}

static void sos_mval_set_u32(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.uint32_ = mval->v_u32;
}

static void sos_mval_set_s32(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.int32_ = mval->v_s32;
}

static void sos_mval_set_u64(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.uint64_ = mval->v_u64;
}

static void sos_mval_set_s64(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.int64_ = mval->v_s64;
}

static void sos_mval_set_f32(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.float_ = mval->v_f;
}

static void sos_mval_set_d64(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.double_ = mval->v_d;
}

static void sos_mval_set_ts(sos_value_t v, ldms_mval_t mval)
{
	v->data->prim.timestamp_.fine.secs = mval->v_ts.sec;
	v->data->prim.timestamp_.fine.usecs = mval->v_ts.usec;
}

sos_mval_set_fn sos_mval_set_tbl[] = {
	[LDMS_V_CHAR] = sos_mval_set_char,
	[LDMS_V_U8] = sos_mval_set_u8,
	[LDMS_V_S8] = sos_mval_set_s8,
	[LDMS_V_U16] = sos_mval_set_u16,
	[LDMS_V_S16] = sos_mval_set_s16,
	[LDMS_V_U32] = sos_mval_set_u32,
	[LDMS_V_S32] = sos_mval_set_s32,
	[LDMS_V_U64] = sos_mval_set_u64,
	[LDMS_V_S64] = sos_mval_set_s64,
	[LDMS_V_F32] = sos_mval_set_f32,
	[LDMS_V_D64] = sos_mval_set_d64,
	[LDMS_V_TIMESTAMP] = sos_mval_set_ts,
};

static inline void
sos_mval_set(sos_value_t v, ldms_mval_t mval, enum ldms_value_type type)
{
	sos_mval_set_fn fn;
	assert(type <= LDMS_V_LAST);
	fn = sos_mval_set_tbl[type];
	assert(fn);
	fn(v, mval);
}

/* caller must hold cfg_lock */
static sos_handle_t __create_handle(const char *path, sos_t sos)
{
	sos_handle_t h = calloc(1, sizeof(*h));
	if (!h)
		return NULL;
	int len = strlen(path);
	if (len >= sizeof(h->path)) {
		errno = ENAMETOOLONG;
		free(h);
		return NULL;
	}
	memcpy(h->path, path, len+1);
	h->ref_count = 1;
	h->sos = sos;
	LIST_INSERT_HEAD(&sos_handle_list, h, entry);
	return h;
}

/* caller must hold cfg_lock */
static sos_handle_t __create_container(const char *path)
{
	int rc = 0;
	sos_t sos;
	time_t t;
	char part_name[16];	/* Unix timestamp as string */
	sos_part_t part;
	sos_handle_t h;

	rc = sos_container_new(path, 0660);
	if (rc) {
		LOG_(LDMSD_LERROR, "Error %d creating the container at '%s'\n",
		       rc, path);
		goto err_0;
	}
	sos = sos_container_open(path, SOS_PERM_RW);
	if (!sos) {
		LOG_(LDMSD_LERROR, "Error %d opening the container at '%s'\n",
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
		LOG_(LDMSD_LERROR, "Error %d creating the partition '%s' in '%s'\n",
		       rc, part_name, path);
		goto err_1;
	}
	part = sos_part_find(sos, part_name);
	if (!part) {
		LOG_(LDMSD_LERROR, "Newly created partition was not found\n");
		goto err_1;
	}
	rc = sos_part_state_set(part, SOS_PART_STATE_PRIMARY);
	if (rc) {
		LOG_(LDMSD_LERROR, "New partition could not be made primary\n");
		goto err_2;
	}
	sos_part_put(part);
	h = __create_handle(path, sos);
	if (!h)
		goto err_1;
	return h;
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

/* caller must hold cfg_lock */
static sos_handle_t __find_container(const char *path)
{
	sos_handle_t h = NULL;
	LIST_FOREACH(h, &sos_handle_list, entry){
		if (0 != strncmp(path, h->path, sizeof(h->path)))
			continue;
		/* found */
		/* take reference */
		h->ref_count++;
		break;
	}
	return h;
}

static sos_handle_t get_container(const char *path)
{
	sos_handle_t h = NULL;
	sos_t sos;

	pthread_mutex_lock(&cfg_lock);
	h = __find_container(path);
	if (h)
		goto out;
	/* See if the container exists, but has not been opened yet. */
	sos = sos_container_open(path, SOS_PERM_RW);
	if (sos) {
		/* Create a new handle and add it for this SOS */
		h = __create_handle(path, sos);
		if (!h) {
			sos_container_close(sos, SOS_COMMIT_ASYNC);
			errno = ENOMEM;
		}
		goto out;
	}
	/* the container does not exist, must create it */
	h = __create_container(path);
 out:
	pthread_mutex_unlock(&cfg_lock);
	return h;
}

/**
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	struct sos_instance *si;
	int rc = 0;
	int len;
	char *value;

	value = av_value(avl, "timeout");
	if (value)
		timeout = strtol(value, NULL, 0);

	value = av_value(avl, "path");
	if (!value) {
		LOG_(LDMSD_LERROR,
		       "%s[%d]: The 'path' configuration option is required.\n",
		       __func__, __LINE__);
		return EINVAL;
	}
	pthread_mutex_lock(&cfg_lock);
	if (0 == strcmp(value, root_path))
		/* Ignore the call if the root_path is unchanged */
		goto out;
	len = strlen(value);
	if (len >= PATH_MAX) {
		LOG_(LDMSD_LERROR,
		       "%s[%d]: The 'path' is too long.\n",
		       __func__, __LINE__);
		rc = ENAMETOOLONG;
		goto out;
	}
	strcpy(root_path, value);

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
		if (!si->path) {
			rc = errno;
			pthread_mutex_unlock(&si->lock);
			goto out;
		}
		sprintf(si->path, "%s/%s", root_path, si->container);
		pthread_mutex_unlock(&si->lock);
	}
	rc = 0;
 out:
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
	rbt_init(&si->schema_rbt, row_schema_rbn_cmp);
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

static int
__schema_list(sos_schema_t schema, ldms_set_t set, int list_idx)
{
	int i, rc, rec_type = -1;
	ldms_mval_t lent, lh;
	enum ldms_value_type prev_mtype = LDMS_V_NONE, mtype = 0;
	size_t count, rlen, rcount;
	char *name, *lname, *mname;
	size_t nlen;

	lname = (char *)ldms_metric_name_get(set, list_idx);
	nlen = strlen(lname);
	lh = ldms_metric_get(set, list_idx);
	for (lent = ldms_list_first(set, lh, &mtype, &count);
	     lent; lent = ldms_list_next(set, lent, &mtype, &count)) {
		if (LDMS_V_RECORD_INST == mtype) {
			if (-1 == rec_type) {
				rec_type = ldms_record_type_get(lent);
			} else if (rec_type != ldms_record_type_get(lent)) {
				LOG_(LDMSD_LERROR, "set '%s' contains a list of records "
					"of multiple record types, which store_sos does "
					"not support records of multiple record types.\n",
					ldms_set_instance_name_get(set));
				return EINVAL;
			}
		}
		if ((LDMS_V_NONE != prev_mtype) && (prev_mtype != mtype)) {
			LOG_(LDMSD_LERROR, "List '%s' in set '%s' contains "
					"multiple value types. "
					"store_sos doesn't support this.\n",
					lname, ldms_set_instance_name_get(set));
			return EINVAL;
		}
		prev_mtype = mtype;
	}

	/* Add the list entry index column */
	mname = "entry_idx";
	name = malloc(nlen + strlen(mname) + 2);
	if (!name)
		return ENOMEM;
	snprintf(name, nlen + strlen(mname) + 2, "%s_%s", lname, mname);
	rc = sos_schema_attr_add(schema, name, SOS_TYPE_UINT64);

	if (LDMS_V_RECORD_INST == mtype) {
		lent = ldms_list_first(set, lh, &mtype, &count);
		rlen = ldms_record_card(lent);
		for (i = 0; i < rlen; i++) {
			mname = (char *)ldms_record_metric_name_get(lent, i);
			name = malloc(nlen + strlen(mname) + 2); /* +2 for : and \0 */
			if (!name)
				return ENOMEM;
			snprintf(name, nlen + strlen(mname) + 2, "%s_%s", lname, mname);
			mtype = ldms_record_metric_type_get(lent, i, &rcount);
			rc = sos_schema_attr_add(schema, name, sos_type_map[mtype]);
			free(name);
			if (rc)
				return rc;
		}
	} else {
		rc = sos_schema_attr_add(schema, lname, sos_type_map[mtype]);
	}
	return 0;
}

static sos_schema_t
create_schema(struct sos_instance *si, ldms_set_t set,
	      int *metric_arry, size_t metric_count)
{
	int rc, i;
	enum ldms_value_type mtype;

	sos_schema_t schema = sos_schema_new(si->schema_name);
	if (!schema)
		goto err_0;

	rc = sos_schema_attr_add(schema, "timestamp", SOS_TYPE_TIMESTAMP);
	if (rc)
		goto err_1;
	rc = sos_schema_attr_add(schema, "component_id", SOS_TYPE_UINT64);
	if (rc)
		goto err_1;
	rc = sos_schema_attr_add(schema, "job_id", SOS_TYPE_UINT64);
	if (rc)
		goto err_1;

	for (i = 0; i < metric_count; i++) {
		if (0 == strcmp("timestamp", ldms_metric_name_get(set, metric_arry[i])))
			continue;
		if (0 == strcmp("component_id", ldms_metric_name_get(set, metric_arry[i])))
			continue;
		if (0 == strcmp("job_id", ldms_metric_name_get(set, metric_arry[i])))
			continue;
		LOG_(LDMSD_LINFO, "Adding attribute %s to the schema\n",
		       ldms_metric_name_get(set, metric_arry[i]));

		mtype = ldms_metric_type_get(set, metric_arry[i]);
		if (LDMS_V_RECORD_TYPE == mtype) {
			continue;
		} else if (LDMS_V_LIST == mtype) {
			rc = __schema_list(schema, set, metric_arry[i]);
		} else {
			rc = sos_schema_attr_add(schema,
				ldms_metric_name_get(set, metric_arry[i]),
				sos_type_map[ldms_metric_type_get(set, metric_arry[i])]);
		}
		if (rc)
			goto err_1;
	}

	/*
	 * Time/Job_Id/Component_Id Index
	 */
	char *time_job_comp_attrs[] = { "timestamp", "job_id", "component_id" };
	rc = sos_schema_attr_add(schema, "time_job_comp", SOS_TYPE_JOIN, 3, time_job_comp_attrs);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "time_job_comp");
	if (rc)
		goto err_1;
	/*
	 * Time/Component/Job_Id Index
	 */
	char *time_comp_job_attrs[] = { "timestamp", "component_id", "job_id" };
	rc = sos_schema_attr_add(schema, "time_comp_job", SOS_TYPE_JOIN, 3, time_comp_job_attrs);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "time_comp_job");
	if (rc)
		goto err_1;

	/*
	 * Job_Id/Component_Id/Timestamp Index
	 */
	char *job_comp_time_attrs[] = { "job_id", "component_id", "timestamp" };
	rc = sos_schema_attr_add(schema, "job_comp_time", SOS_TYPE_JOIN, 3, job_comp_time_attrs);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "job_comp_time");
	if (rc)
		goto err_1;
	/*
	 * Job_Id/Timestamp/Component_Id Index
	 */
	char *job_time_comp_attrs[] = { "job_id", "timestamp", "component_id" };
	rc = sos_schema_attr_add(schema, "job_time_comp", SOS_TYPE_JOIN, 3, job_time_comp_attrs);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "job_time_comp");
	if (rc)
		goto err_1;

	/*
	 * Component_Id/Timestamp/Job_Id Index
	 */
	char *comp_time_job_attrs[] = { "component_id", "timestamp", "job_id" };
	rc = sos_schema_attr_add(schema, "comp_time_job", SOS_TYPE_JOIN, 3, comp_time_job_attrs);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "comp_time_job");
	if (rc)
		goto err_1;

	/*
	 * Component_Id/Job_Id/Timestamp Index
	 */
	char *comp_job_time_attrs[] = { "component_id", "job_id", "timestamp" };
	rc = sos_schema_attr_add(schema, "comp_job_time", SOS_TYPE_JOIN, 3, comp_job_time_attrs);
	if (rc)
		goto err_1;
	rc = sos_schema_index_add(schema, "comp_job_time");
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
__sos_mode_set(struct sos_instance *si, ldms_set_t set,
		      int *metric_arry, size_t metric_count)
{
	int i, j;
	enum ldms_value_type mtype;
	int num_lists = 0;

	/* The metrics from the set _must_ come before the index
	 * attributes or the store method will fail attempting to set
	 * an index attribute from a LDMS metric set value
	 */
	for (i = 0; i < metric_count; i++) {
		if (LDMS_V_LIST == ldms_metric_type_get(set, metric_arry[i])) {
			if (0 == ldms_list_len(set, ldms_metric_get(set, metric_arry[i]))) {
				/*
				 * The list is empty, so we cannot determine
				 * the value type of list entries.
				 */
				 return EINTR;
			}
			num_lists++;
		}
	}
	 if (num_lists > 0) {
		si->mode = STORE_SOS_M_LISTS;
		si->ctxt.lists.num_lists = num_lists;
		si->ctxt.lists.list = calloc(num_lists, sizeof(struct sos_list));
		if (!si->ctxt.lists.list) {
			LOG_(LDMSD_LCRITICAL, "store_sos: Out of memory\n");
			return ENOMEM;
		}
	} else {
		si->mode = STORE_SOS_M_BASIC;
	}
	j = 0;
	for (i = 0; i < metric_count; i++) {
		mtype = ldms_metric_type_get(set, metric_arry[i]);
		if (LDMS_V_LIST == mtype) {
			struct sos_list list;
			list.list_mid = metric_arry[i];
			/* Get the list entry value type and count */
			(void) ldms_list_first(set,
					ldms_metric_get(set, metric_arry[i]),
					&(list.mtype), &(list.count));
			si->ctxt.lists.list[j++] = list;
		}
	}
	return 0;
}

static int
_open_store(struct sos_instance *si, ldms_set_t set,
	    int *metric_arry, size_t metric_count)
{
	int rc;
	sos_schema_t schema;

	rc = __sos_mode_set(si, set, metric_arry, metric_count);
	if (rc)
		return rc;

	/* Check if the container is already open */
	si->sos_handle = get_container(si->path);
	if (!si->sos_handle)
		return errno;
	/* See if the required schema is already present */
	schema = sos_schema_by_name(si->sos_handle->sos, si->schema_name);
	if (!schema)
		goto add_schema;
	si->sos_schema = schema;
	return 0;

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
		LOG_(LDMSD_LERROR, "Error %d adding the schema to the container\n", rc);
		goto err_1;
	}
 out:
	si->sos_schema = schema;
	return 0;
 err_1:
	sos_schema_free(schema);
 err_0:
	put_container(si->sos_handle);
	si->sos_handle = NULL;
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
__store_timestamp(struct sos_instance *si, sos_obj_t obj, ldms_set_t set)
{
	SOS_VALUE(value);
	struct ldms_timestamp timestamp = ldms_transaction_timestamp_get(set);

	/* timestamp */
	if (NULL == sos_value_init(value, obj, si->ts_attr)) {
		LOG_(LDMSD_LERROR, "Error initializing timestamp attribute\n");
		return ENOMEM;
	}
	value->data->prim.timestamp_.fine.secs = timestamp.sec;
	value->data->prim.timestamp_.fine.usecs = timestamp.usec;
	sos_value_put(value);

	return 0;
}

/* We assume that the order of \c mval passed by the caller and sos attributes are the same. */
static sos_attr_t
__store_metric(sos_obj_t obj, sos_attr_t attr, ldms_set_t set,
				enum ldms_value_type metric_type,
				ldms_mval_t mval, size_t count)
{
	SOS_VALUE(value);
	SOS_VALUE(array_value);
	int array_len;
	int esz;
	enum ldms_value_type mtype;
	ldms_mval_t rent;
	size_t cnt;
	int i;

	errno = 0;
	if ((LDMS_V_LIST == metric_type) ||
			(LDMS_V_RECORD_ARRAY == metric_type) ||
			(LDMS_V_RECORD_TYPE == metric_type)) {
		return attr;
	}

	if (LDMS_V_RECORD_INST == metric_type) {
		for (i = 0; i < ldms_record_card(mval); i++) {
			mtype = ldms_record_metric_type_get(mval, i, &cnt);
			rent = ldms_record_metric_get(mval, i);
			LOG_(LDMSD_LDEBUG, "store_sos: attr[%s, %d]: metric[%s, %s, %d]\n",
					sos_attr_name(attr), sos_attr_type(attr),
					ldms_record_metric_name_get(mval, i),
					ldms_metric_type_to_str(mtype),
					sos_type_map[mtype]);
			attr = __store_metric(obj, attr, set, mtype, rent, cnt);
			if (!attr && errno) {
				return NULL;
			}
		}
	} else if (metric_type < LDMS_V_CHAR_ARRAY) {
		LOG_(LDMSD_LDEBUG, "store_sos: attr[%s, %d]: metric[null, %s, %d]\n",
				sos_attr_name(attr), sos_attr_type(attr),
				ldms_metric_type_to_str(metric_type),
				sos_type_map[metric_type]);
		if (sos_attr_type(attr) != sos_type_map[metric_type]) {
			assert(0);
		}
		if (NULL == sos_value_init(value, obj, attr)) {
			LOG_(LDMSD_LERROR, "Error initializing '%s' attribute\n",
			       sos_attr_name(attr));
			errno = ENOMEM;
			return NULL;
		}
		sos_value_set[metric_type](value, mval);
		sos_value_put(value);
		attr = sos_schema_attr_next(attr);
	} else {
		LOG_(LDMSD_LINFO, "store_sos: attr[%s, %d]: metric[null, %s, %d]\n",
				sos_attr_name(attr), sos_attr_type(attr),
				ldms_metric_type_to_str(metric_type),
				sos_type_map[metric_type]);
		if (sos_attr_type(attr) != sos_type_map[metric_type]) {
			assert(0);
		}
		esz = __element_byte_len(metric_type);
		if (metric_type == LDMS_V_CHAR_ARRAY) {
			array_len = strlen(mval->a_char);
			if (array_len > count) {
				array_len = count;
			}
		} else {
			array_len = count;
		}
		array_value = sos_array_new(array_value, attr, obj, array_len);
		if (!array_value) {
			LOG_(LDMSD_LERROR, "Error %d allocating '%s' array of size %d\n",
			     errno,
			     sos_attr_name(attr),
			     array_len);
			errno = ENOMEM;
			return NULL;
		}
		array_len *= esz;
		count = sos_value_memcpy(array_value, mval, array_len);
		assert(count == array_len);
		sos_value_put(array_value);
		attr = sos_schema_attr_next(attr);
	}
	return attr;
}

struct sos_list_ent {
	size_t list_len; /* Number of list entries */
	ldms_mval_t mval; /* A list entry handle */
	int idx; /* A list entry index */
	enum ldms_value_type mtype; /* list entries' value type */
	size_t count; /* array length if list entries are arrays */
	int all_stored; /* All list entries have been stored */
};

static int
__store_list_row(struct sos_instance *si, ldms_set_t s,
		int *metric_arry, int metric_count,
		struct sos_list_ent *lent)
{
	int i, rc = 0;
	sos_obj_t obj;
	sos_attr_t attr;
	ldms_mval_t mval;
	enum ldms_value_type mtype;
	size_t count;
	int list_no = 0;

	obj = sos_obj_new(si->sos_schema);
	if (!obj) {
		LOG_(LDMSD_LERROR, "Error %d: %s at %s:%d\n", errno,
		       STRERROR(errno), __FILE__, __LINE__);
		rc = ENOMEM;
		goto err;
	}

	rc = __store_timestamp(si, obj, s);
	if (rc)
		goto err;

	attr = si->first_attr;
	for (i = 0; i < metric_count; i++) {
		if (!attr) {
			LOG_(LDMSD_LERROR,
			       "The set '%s' with schema '%s' has more "
			       "attributes than the SOS schema '%s' to which "
			       "it is being stored.\n",
			       ldms_set_instance_name_get(s),
			       ldms_set_schema_name_get(s),
			       sos_schema_name(si->sos_schema));
			rc = E2BIG;
			goto err;
		}
		mval = ldms_metric_get(s, metric_arry[i]);
		mtype = ldms_metric_type_get(s, metric_arry[i]);

		if (ldms_metric_is_array(s, metric_arry[i]))
			count = ldms_metric_array_get_len(s, metric_arry[i]);
		else
			count = 0;
		if (LDMS_V_LIST == mtype) {
			if (LDMS_V_LIST == lent[list_no].mtype) {
				LOG_(LDMSD_LERROR, "List '%s' in set '%s' "
					"contains a list, which store_sos "
					"does not support.\n",
					ldms_metric_name_get(s, metric_arry[i]),
					ldms_set_instance_name_get(s));
				rc = ENOTSUP;
				goto err;
			}
			if (si->ctxt.lists.list[list_no].mtype != lent[list_no].mtype) {
				LOG_(LDMSD_LERROR, "List '%s' in set '%s' "
					"contains a metric of '%s' instead of '%s'.\n",
					ldms_metric_name_get(s, metric_arry[i]),
					ldms_set_instance_name_get(s),
					ldms_metric_type_to_str(si->ctxt.lists.list[list_no].mtype),
					ldms_metric_type_to_str(lent[list_no].mtype));
				rc = EINVAL;
				goto err;
			}
			union ldms_value idxv;
			idxv.v_u64 = lent[list_no].idx;
			/* Store the list entry index */
			attr = __store_metric(obj, attr, s, LDMS_V_U64, &idxv, 1);
			if (!attr && errno) {
				rc = errno;
				goto err;
			}
			/* Store the record content */
			attr = __store_metric(obj, attr, s,
						lent[list_no].mtype,
						lent[list_no].mval,
						lent[list_no].count);
			if (!attr && errno) {
				rc = errno;
				goto err;
			}
			if (lent[list_no].idx + 1 == lent[list_no].list_len) {
				/* All records in the list have been stored. */
				lent[list_no].all_stored = 1;
			} else {
				lent[list_no].mval = ldms_list_next(s,
							  lent[list_no].mval,
							  &(lent[list_no].mtype),
							  &(lent[list_no].count));
				lent[list_no].idx++;
			}
			list_no++;
		} else {
			attr = __store_metric(obj, attr, s, mtype, mval, count);
			if (!attr && errno) {
				rc = errno;
				goto err;
			}
		}
	}
	rc = sos_obj_index(obj);
	sos_obj_put(obj);
	return rc;
err:
	sos_obj_delete(obj);
	return rc;
}

static int
__store_lists(struct sos_instance *si, ldms_set_t set,
		int *metric_arry, int metric_count)
{
	int rc = 0;
	int i, mid, num_lists;
	ldms_mval_t mval;
	struct sos_list_ent *lent;

	num_lists = si->ctxt.lrec.num_lists;
	lent = calloc(num_lists, sizeof(*lent));
	if (!lent) {
		LOG_(LDMSD_LCRITICAL, "store_sos: Out of memory\n");
		rc = ENOMEM;
		goto err;
	}
	/*
	 * store_sos doesn't store the data if one of the list is empty.
	 */
	for (i = 0; i < si->ctxt.lists.num_lists; i++) {
		mid = si->ctxt.lists.list[i].list_mid;
		mval = ldms_metric_get(set, mid);
		lent[i].list_len = ldms_list_len(set, mval);
		if (0 == lent[i].list_len) {
			LOG_(LDMSD_LINFO, "store_sos: List '%s' in set '%s' is "
					"empty. store_sos won't store the data.\n",
					ldms_metric_name_get(set, mid),
					ldms_set_instance_name_get(set));
			goto err;
		}
		lent[i].mval = ldms_list_first(set, mval, &(lent[i].mtype), &(lent[i].count));
		lent[i].idx = 0;
	}

	int do_store;
	do {
		rc = __store_list_row(si, set, metric_arry, metric_count, lent);
		if (rc)
			goto err;
		do_store = 0;
		for (i = 0; i < num_lists; i ++) {
			if (!lent[i].all_stored) {
				do_store = 1;
				break;
			}
		}
	} while (do_store);
	return 0;
err:
	free(lent);
	return rc;
}

static int
__store_basic(struct sos_instance *si, ldms_set_t s,
		int *metric_arry, int metric_count)
{
	int rc, i;
	sos_obj_t obj;
	sos_attr_t attr;
	enum ldms_value_type metric_type;
	ldms_mval_t mval;

	obj = sos_obj_new(si->sos_schema);
	if (!obj) {
		LOG_(LDMSD_LERROR, "Error %d: %s at %s:%d\n", errno,
		       STRERROR(errno), __FILE__, __LINE__);
		rc = ENOMEM;
		goto err;
	}

	rc = __store_timestamp(si, obj, s);
	if (rc)
		goto err;

	metric_count = ldms_set_card_get(s);
	/*
	 * The assumption is that the metrics in the SOS schema have the same
	 * order as in metric_arry.
	 */
	for (i = 0, attr = si->first_attr; i < metric_count; i++) {
		size_t count = 0;
		if (!attr) {
			errno = E2BIG;
			LOG_(LDMSD_LERROR,
			       "The set '%s' with schema '%s' has fewer "
			       "attributes than the SOS schema '%s' to which "
			       "it is being stored.\n",
			       ldms_set_instance_name_get(s),
			       ldms_set_schema_name_get(s),
			       sos_schema_name(si->sos_schema));
			goto err;
		}
		metric_type = ldms_metric_type_get(s, metric_arry[i]);
		mval = ldms_metric_get(s, metric_arry[i]);
		if (ldms_metric_is_array(s, metric_arry[i]))
			count = ldms_metric_array_get_len(s, metric_arry[i]);
		attr = __store_metric(obj, attr, s, metric_type, mval, count);
		if (!attr && errno)
			goto err;
	}
	rc = sos_obj_index(obj);
	sos_obj_put(obj);
	return 0;
err:
	sos_obj_delete(obj);
	return rc;
}

#ifdef NDEBUG
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
static int
store(ldmsd_store_handle_t _sh, ldms_set_t set,
      int *metric_arry, size_t metric_count)
{
	struct sos_instance *si = _sh;
	struct ldms_timestamp timestamp;
	struct timespec now;
	SOS_VALUE(value);
	sos_obj_t obj;
	int rc = 0;

	if (!si)
		return EINVAL;

	pthread_mutex_lock(&si->lock);
	if (!si->sos_handle) {
		rc = _open_store(si, set, metric_arry, metric_count);
		if (rc) {
			pthread_mutex_unlock(&si->lock);
			LOG_(LDMSD_LERROR, "Failed to create store "
			       "for %s.\n", si->container);
			errno = rc;
			return -1;
		}
		si->job_id_idx = ldms_metric_by_name(set, "job_id");
		si->comp_id_idx = ldms_metric_by_name(set, "component_id");
		si->ts_attr = sos_schema_attr_by_name(si->sos_schema, "timestamp");
		si->first_attr = sos_schema_attr_by_name(si->sos_schema,
				ldms_metric_name_get(set, metric_arry[0]));
		if (si->comp_id_idx < 0)
			LOG_(LDMSD_LINFO,
			       "The component_id is missing from the metric set/schema.\n");
		if (si->job_id_idx < 0)
			LOG_(LDMSD_LERROR,
			       "The job_id is missing from the metric set/schema.\n");
		assert(si->ts_attr);
	}
	if (timeout > 0) {
		clock_gettime(CLOCK_REALTIME, &now);
		now.tv_sec += timeout;
		if (sos_begin_x_wait(si->sos_handle->sos, &now)) {
			LOG_(LDMSD_LERROR,
			     "Timeout attempting to open a transaction on the container '%s'.\n",
			     si->path);
			errno = ETIMEDOUT;
			pthread_mutex_unlock(&si->lock);
			return -1;
		}
	} else {
		sos_begin_x(si->sos_handle->sos);
	}
	obj = sos_obj_new(si->sos_schema);
	if (!obj) {
		LOG_(LDMSD_LERROR, "Error %d: %s at %s:%d\n", errno,
		       STRERROR(errno), __FILE__, __LINE__);
		errno = ENOMEM;
		goto err;
	}
	timestamp = ldms_transaction_timestamp_get(set);

	/* timestamp */
	if (NULL == sos_value_init(value, obj, si->ts_attr)) {
		LOG_(LDMSD_LERROR, "Error initializing timestamp attribute\n");
		errno = ENOMEM;
		goto err;
	}
	value->data->prim.timestamp_.fine.secs = timestamp.sec;
	value->data->prim.timestamp_.fine.usecs = timestamp.usec;
	sos_value_put(value);

	switch (si->mode) {
		case STORE_SOS_M_BASIC:
			rc = __store_basic(si, set, metric_arry, metric_count);
			break;
		case STORE_SOS_M_LISTS:
			rc = __store_lists(si, set, metric_arry, metric_count);
			break;
		default:
			LOG_(LDMSD_LERROR, "Unrecognized store_sos mode '%d' "
							"at %s:%d\n", si->mode,
							   __FILE__, __LINE__);
			errno = EINVAL;
			goto err;
	}
	sos_end_x(si->sos_handle->sos);
	if (rc) {
		LOG_(LDMSD_LERROR, "Error %d: %s at %s:%d\n", errno,
		       STRERROR(errno), __FILE__, __LINE__);
	}
	pthread_mutex_unlock(&si->lock);
	return rc;
err:
	sos_end_x(si->sos_handle->sos);
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

/* protected by strgp->lock */
static void close_store(ldmsd_store_handle_t _sh)
{
	struct sos_instance *si = _sh;
	struct row_schema_rbn_s *rrbn;

	if (!si)
		return;

	pthread_mutex_lock(&cfg_lock);
	LIST_REMOVE(si, entry);
	pthread_mutex_unlock(&cfg_lock);

	while ((rrbn = (struct row_schema_rbn_s *)rbt_min(&si->schema_rbt))) {
		rbt_del(&si->schema_rbt, &rrbn->rbn);
		/* rrbn->sos_schema will be freed when sos container closed */
		free(rrbn);
	}
	if (si->sos_handle)
		put_container(si->sos_handle);
	if (si->path)
		free(si->path);
	free(si->container);
	free(si->schema_name);
	free(si);
}

static int init_store_instance(ldmsd_strgp_t strgp)
{
	struct sos_instance *si;
	int len, rc;

	si = calloc(1, sizeof(*si));
	if (!si) {
		rc = errno;
		goto err_0;
	}
	rbt_init(&si->schema_rbt, row_schema_rbn_cmp);
	len = asprintf(&si->path, "%s/%s", root_path, strgp->container);
	if (len < 0) {
		rc = errno;
		goto err_1;
	}
	si->sos_handle = get_container(si->path);
	if (!si->sos_handle) {
		rc = errno;
		goto err_2;
	}
	strgp->store_handle = si;
	pthread_mutex_init(&si->lock, NULL);
	pthread_mutex_lock(&cfg_lock);
	LIST_INSERT_HEAD(&inst_list, si, entry);
	pthread_mutex_unlock(&cfg_lock);
	return 0;

 err_2:
	free(si->path);
 err_1:
	free(si);
 err_0:
	return rc;
}

static sos_schema_t
create_row_schema(ldmsd_strgp_t strgp, ldmsd_row_t row)
{
	struct sos_instance *si = (void*)strgp->store_handle;
	sos_t sos = si->sos_handle->sos;
	sos_schema_t sos_schema;
	int i, j, rc;
	const char *idx_attrs[256]; /* should be reasonably sufficient */

	sos_type_t sos_type;

	sos_schema = sos_schema_new(row->schema_name);
	if (!sos_schema) {
		LOG_(LDMSD_LERROR, "sos_schema_new() failed, errno: %d, "
		     "container: %s, schema: %s\n",
		     errno, si->path, row->schema_name);
		goto err_0;
	}

	/* attributes */
	for (i = 0; i < row->col_count; i++) {
		sos_type = sos_type_from_ldms_type(row->cols[i].type);
		if (sos_type == -1) {
			LOG_(LDMSD_LERROR, "Unsupported type %s, "
			     "errno: %d, container: %s, schema: %s\n",
			     ldms_metric_type_to_str(row->cols[i].type),
			     errno, si->path, row->schema_name);
			goto err_1;
		}
		rc = sos_schema_attr_add(sos_schema, row->cols[i].name, sos_type);
		if (rc)
			goto err_1;
	}

	/* indices */
	for (i = 0; i < row->idx_count; i++) {
		const char *idx_name;
		assert( row->indices[i]->col_count < 256 );
		if (row->indices[i]->col_count >= 256) {
			errno = E2BIG;
			goto err_1;
		}
		if (row->indices[i]->col_count == 1) {
			idx_name = row->indices[i]->cols[0]->name;
			goto add_idx;
		}

		/* join attribute */
		for (j = 0; j < row->indices[i]->col_count; j++) {
			idx_attrs[j] = row->indices[i]->cols[j]->name;
		}
		rc = sos_schema_attr_add(sos_schema, row->indices[i]->name,
				SOS_TYPE_JOIN, row->indices[i]->col_count,
				idx_attrs);
		if (rc)
			goto err_1;
		idx_name = row->indices[i]->name;
	add_idx:
		rc = sos_schema_index_add(sos_schema, idx_name);
		if (rc)
			goto err_1;
	}

	/* add to sos container */
	rc = sos_schema_add(sos, sos_schema);
	if (rc)
		goto err_1;

	return sos_schema;

 err_1:
	sos_schema_free(sos_schema);
 err_0:
	return NULL;
}

/* protected by strgp lock */
static struct row_schema_rbn_s *
get_row_schema(ldmsd_strgp_t strgp, ldmsd_row_t row)
{
	struct row_schema_rbn_s *rrbn;
	struct sos_instance *si = (void*)strgp->store_handle;
	struct row_schema_key_s key = {row->schema_digest, row->schema_name};

	rrbn = (void*)rbt_find(&si->schema_rbt, &key);
	if (rrbn)
		return rrbn;

	rrbn = calloc(1, sizeof(*rrbn));
	if (!rrbn) {
		LOG_(LDMSD_LERROR, "Not enough memory, errno: %d, "
		     "container: %s, schema: %s\n",
		     errno, si->path, row->schema_name);
		goto err_0;
	}
	snprintf(rrbn->name, sizeof(rrbn->name), "%s", row->schema_name);
	memcpy(&rrbn->digest, row->schema_digest, sizeof(rrbn->digest));
	rrbn->key.name = rrbn->name;
	rrbn->key.digest = &rrbn->digest;
	rbn_init(&rrbn->rbn, &rrbn->key);

	/* sos_schema */
	rrbn->sos_schema = sos_schema_by_name(si->sos_handle->sos, key.name);
	if (!rrbn->sos_schema) {
		rrbn->sos_schema = create_row_schema(strgp, row);
		if (!rrbn->sos_schema)
			goto err_1;
	}

	rbt_ins(&si->schema_rbt, &rrbn->rbn);
	return rrbn;

 err_1:
	free(rrbn);
 err_0:
	return NULL;
}

static int
commit_rows(ldmsd_strgp_t strgp, ldms_set_t set, ldmsd_row_list_t row_list, int row_count)
{
	int rc = 0;
	struct sos_instance *si;
	ldmsd_row_t row;
	ldmsd_col_t col;
	struct row_schema_rbn_s *rrbn;
	sos_obj_t sos_obj;
	sos_type_t sos_type;
	SOS_VALUE(value);
	SOS_VALUE(array_value);
	sos_attr_t sos_attr;
	int i, esz, array_len, count;

	if (!strgp->store_handle) {
		rc = init_store_instance(strgp);
		if (rc)
			goto out;
	}
	si = strgp->store_handle;
	if (!si->sos_handle) {
		/* rare; only in the case of store_sos reconfig */
		si->sos_handle = get_container(si->path);
		if (!si->sos_handle) {
			rc = errno;
			goto out;
		}
	}
	if (timeout > 0) {
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		now.tv_sec += timeout;
		if (sos_begin_x_wait(si->sos_handle->sos, &now)) {
			LOG_(LDMSD_LERROR,
			     "Timeout attempting to open a transaction on the container '%s'.\n",
			     si->path);
			errno = ETIMEDOUT;
			pthread_mutex_unlock(&si->lock);
			return -1;
		}
	} else {
		sos_begin_x(si->sos_handle->sos);
	}
	TAILQ_FOREACH(row, row_list, entry) {
		rrbn = get_row_schema(strgp, row);
		if (!rrbn) {
			/* get_row_schema() already logged the error */
			goto row_next;
		}
		sos_obj = sos_obj_new(rrbn->sos_schema);
		if (!sos_obj) {
			LOG_(LDMSD_LERROR, "cannot create SOS object, "
			     "errno: %d, container: %s, schema: %s\n",
			     errno, si->path, rrbn->key.name);
			goto row_next;
		}
		sos_attr = sos_schema_attr_first(rrbn->sos_schema);
		for (i = 0; i < row->col_count; i++) {
			col = &row->cols[i];
			if (!sos_attr) {
				LOG_(LDMSD_LERROR,
				     "sos attribute - ldms metric mismatch: "
				     "expecting more sos attributes\n");
				goto row_err;
			}
			sos_type = sos_type_from_ldms_type(col->type);
			if (sos_attr_type(sos_attr) != sos_type) {
				LOG_(LDMSD_LERROR,
				     "sos attribute - ldms metric type mismatch: "
				     "expecting %s, but got %s\n",
				     sos_type_sym(sos_type),
				     sos_type_sym(sos_attr_type(sos_attr)));
				goto row_err;
			}
			if (0 == ldms_type_is_array(col->type)) {
				sos_value_init(value, sos_obj, sos_attr);
				sos_mval_set(value, col->mval, col->type);
				sos_value_put(value);
			} else {
				esz = __element_byte_len(col->type);
				if (col->type == LDMS_V_CHAR_ARRAY) {
					array_len = strlen(col->mval->a_char);
					if (array_len > col->array_len) {
						array_len = col->array_len;
					}
				} else {
					array_len = col->array_len;
				}
				array_value = sos_array_new(array_value,
						sos_attr, sos_obj, array_len);
				if (!array_value) {
					LOG_(LDMSD_LERROR, "Error %d allocating '%s' array of size %d\n",
					     errno,
					     sos_attr_name(sos_attr),
					     array_len);
					errno = ENOMEM;
					goto row_err;
				}
				count = sos_value_memcpy(array_value,
						col->mval, array_len * esz);
				assert(count == array_len * esz);
				sos_value_put(array_value);
			}
			sos_attr = sos_schema_attr_next(sos_attr);
		}
		sos_obj_index(sos_obj);
		sos_obj_put(sos_obj);
		sos_obj = NULL;
		goto row_next;

	row_err:
		sos_obj_delete(sos_obj);
	row_next:
		continue;
	}
	sos_end_x(si->sos_handle->sos);
 out:
	return rc;
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
	.commit = commit_rows,
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
