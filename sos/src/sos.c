/*
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

/*
 * Author: Tom Tucker tom at ogc dot us
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>

#include "sos.h"
#include "sos_priv.h"
#include "ods.h"
#include "obj_idx.h"

#include "ovis_util/util.h"
#include "../config.h"

sos_class_t sos_class_new(sos_t sos, const char *name,
			  size_t attr_count, sos_attr_t attrs)
{
	size_t sz;
	sos_class_t sc;
	int i;

	sz = sizeof *sc + (attr_count * sizeof attrs[0]);
	sc = malloc(sz);
	if (!sc)
		goto out;
	strcpy(sc->name, name);
	sc->count = attr_count;
	for (i = 0; i < sc->count; i++)
		sc->attrs[i] = attrs[i];
 out:
	return sc;
}

sos_attr_t sos_attr_by_name(sos_t sos, const char *name)
{
	int attr_id;
	for (attr_id = 0; attr_id < sos->classp->count; attr_id++)
		if (strncmp(sos->classp->attrs[attr_id].name, name,
			    SOS_ATTR_NAME_LEN) == 0)
			return &sos->classp->attrs[attr_id];

	return NULL;
}

sos_attr_t sos_obj_attr_by_id(sos_t sos, int attr_id)
{
	if (attr_id >= 0 && attr_id < sos->classp->count)
		return &sos->classp->attrs[attr_id];

	return NULL;
}

int sos_obj_attr_index(sos_t sos, int attr_id)
{
	return sos_obj_attr_by_id(sos, attr_id)->has_idx;
}

int sos_attr_has_index(sos_attr_t attr)
{
	return attr->has_idx;
}

/**
 * \brief Sizes for default SOS data types.
 */
static uint32_t type_sizes[] = {
	[SOS_TYPE_INT32] = 4,
	[SOS_TYPE_UINT32] = 4,
	[SOS_TYPE_INT64] = 8,
	[SOS_TYPE_UINT64] = 8,
	[SOS_TYPE_DOUBLE] = 8,
	[SOS_TYPE_STRING] = 8,
	[SOS_TYPE_BLOB] = 8, /**< \note ODS reference to BLOB. */
};
static int type_is_builtin(enum sos_type_e e)
{
	if (e >= SOS_TYPE_INT32 && e < SOS_TYPE_USER)
		return (1==1);
	return (1==2);
}
static int type_is_valid(enum sos_type_e e)
{
	if (e >= SOS_TYPE_INT32 && e <= SOS_TYPE_USER)
		return (1==1);
	return (1==2);
}

char *sos_type_to_str(enum sos_type_e type)
{
	switch (type) {
	case SOS_TYPE_INT32:
		return "int32";
	case SOS_TYPE_INT64:
		return "int64";
	case SOS_TYPE_UINT32:
		return "uint32";
	case SOS_TYPE_UINT64:
		return "uint64";
	case SOS_TYPE_DOUBLE:
		return "double";
	case SOS_TYPE_STRING:
		return "string";
	case SOS_TYPE_BLOB:
		return "blob";
	case SOS_TYPE_USER:
		return "user";
	case SOS_TYPE_UNKNOWN:
		return "unknown";
	default:
		assert(0);
	}
}

size_t sos_obj_attr_size(sos_t sos, int attr_id, sos_obj_t obj)
{
	sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
	return attr->attr_size_fn(attr, obj);
}

const char *sos_attr_name(sos_t sos, sos_attr_t attr)
{
	return attr->name;
}

void sos_attr_key_set(sos_attr_t attr, void *value, obj_key_t key)
{
	attr->set_key_fn(attr, value, key);
}

void sos_attr_key_from_str(sos_attr_t attr, obj_key_t key, const char *str)
{
	obj_key_from_str(attr->oidx, key, str);
}

void sos_key_from_str(sos_iter_t i, obj_key_t key, const char *key_val)
{
	obj_key_from_str(i->attr->oidx, key, key_val);
}

void sos_obj_attr_key_set(sos_t sos, int attr_id, void *value, obj_key_t key)
{
	sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
	attr->set_key_fn(attr, value, key);
}

/* Create a new iterator */
sos_iter_t sos_iter_new(sos_t sos, int attr_id)
{
	sos_attr_t attr;
	sos_iter_t i;

	attr = sos_obj_attr_by_id(sos, attr_id);
	if (!attr)
		return NULL;

	if (!sos_attr_has_index(attr))
		return NULL;

	i = calloc(1, sizeof *i);
	if (!i)
		goto err;

	i->sos = sos;
	i->attr = attr;
	i->iter = obj_iter_new(i->attr->oidx);
	if (!i->iter)
		goto err;
	obj_iter_begin(i->iter);
	return i;
 err:
	if (i)
		free(i);
	return NULL;
}

void sos_iter_free(sos_iter_t iter)
{
	obj_iter_delete(iter->iter);
	free(iter);
}

static inline sos_meta_t sos_meta(sos_t sos)
{
	return ods_get_user_data(sos->ods, &sos->meta_sz);
}

static inline sos_dattr_t get_dattr(sos_t sos, int attr_id)
{
	return &sos_meta(sos)->attrs[attr_id];
}

const char *sos_iter_name(sos_iter_t i)
{
	return i->attr->name;
}

sos_obj_t sos_ref_to_obj(sos_t sos, obj_ref_t ref)
{
	return ods_obj_ref_to_ptr(sos->ods, ref);
}

obj_ref_t sos_obj_to_ref(sos_t sos, sos_obj_t obj)
{
	return ods_obj_ptr_to_ref(sos->ods, obj);
}

sos_obj_t sos_iter_obj(sos_iter_t i)
{
	obj_ref_t obj_o = obj_iter_ref(i->iter);
	if (!obj_o)
		return NULL;
	return ods_obj_ref_to_ptr(i->sos->ods, obj_o);
}

obj_ref_t sos_iter_ref(sos_iter_t i)
{
	obj_ref_t obj_o = obj_iter_ref(i->iter);
	return obj_o;
}

int sos_iter_next(sos_iter_t i)
{
	return obj_iter_next(i->iter);
}

int sos_iter_prev(sos_iter_t i)
{
	return obj_iter_prev(i->iter);
}

int sos_iter_begin(sos_iter_t i)
{
	return obj_iter_begin(i->iter);
}

int sos_iter_end(sos_iter_t i)
{
	return obj_iter_end(i->iter);
}

int sos_iter_seek_sup(sos_iter_t i, obj_key_t key)
{
	return obj_iter_find_lub(i->iter, key);
}

int sos_iter_seek_inf(sos_iter_t i, obj_key_t key)
{
	return obj_iter_find_glb(i->iter, key);
}

int sos_iter_key_cmp(sos_iter_t iter, obj_key_t other)
{
	return obj_key_cmp(iter->attr->oidx, obj_iter_key(iter->iter), other);
}

int sos_iter_seek(sos_iter_t i, obj_key_t key)
{
	return obj_iter_find(i->iter, key);
}

obj_key_t sos_iter_key(sos_iter_t i)
{
	return obj_iter_key(i->iter);
}

static const char *sos_type_to_key_str(enum sos_type_e t)
{
	static const char *key_map[] = {
		"INT32",
		"INT64",
		"UINT32",
		"UINT64",
		"DOUBLE",
		"STRING",
		"BLOB",
	};
	return key_map[t];
}

static obj_idx_t init_idx(sos_t sos, int o_flag, int o_mode, sos_attr_t attr)
{
	obj_idx_t idx;
	char tmp_path[PATH_MAX];
	int rc;
	sprintf(tmp_path, "%s_%s", sos->path, attr->name);
	/* Check if the index exists */
	idx = obj_idx_open(tmp_path);
	if (!idx) {
		char *order_sz = getenv("BPTREE_ORDER");
		int order = 7;
		if (order_sz)
			order = atoi(order_sz);
		if (!order)
			order = 7;
		/* Create the index */
		rc = obj_idx_create_sz(tmp_path, o_mode,
				    "BPTREE", sos_type_to_key_str(attr->type),
				    SOS_INITIAL_SIZE, order);
		if (!rc)
			idx = obj_idx_open(tmp_path);
	}
	return idx;
}

void sos_commit(sos_t sos, int flags)
{
	int attr_id;

	ods_commit(sos->ods, flags);
	for (attr_id = 0; attr_id < sos->classp->count; attr_id++)
		if (sos->classp->attrs[attr_id].oidx)
			obj_idx_commit(sos->classp->attrs[attr_id].oidx,
				       flags);
}

void sos_close(sos_t sos, int flags)
{
	int attr_id;

	sos_commit(sos, flags);
	ods_close(sos->ods, flags);
	for (attr_id = 0; attr_id < sos->classp->count; attr_id++) {
		obj_idx_close(sos->classp->attrs[attr_id].oidx, flags);
	}
	if (sos->path)
		free(sos->path);
	free(sos);
}

/*
 * Initialize the on-disk meta-data from the input class definition
 */
static sos_meta_t make_meta(sos_t sos, sos_meta_t meta, sos_class_t classp)
{
	uint32_t cur_off;
	int attr_id;

	memset(meta, 0, sizeof *meta);
	meta->ods_extend_sz = SOS_ODS_EXTEND_SZ;

	/* Record the byte order. */
	meta->byte_order = SOS_OBJ_BYTE_ORDER;

	/* parse the input class spec and build the meta data */
	strncpy(meta->classname, classp->name, sizeof(meta->signature));
	meta->attr_cnt = classp->count;

	cur_off = 0;
	for (attr_id = 0; attr_id < classp->count; attr_id++) {
		strncpy(meta->attrs[attr_id].name, classp->attrs[attr_id].name,
			SOS_ATTR_NAME_LEN);
		if (!type_is_valid(classp->attrs[attr_id].type))
			goto err;
		meta->attrs[attr_id].type = classp->attrs[attr_id].type;
		meta->attrs[attr_id].data = cur_off;
		cur_off += type_sizes[classp->attrs[attr_id].type];
	}

	/* Now build the index and link info */
	for (attr_id = 0; attr_id < classp->count; attr_id++) {
		if (classp->attrs[attr_id].has_idx)
			meta->attrs[attr_id].has_idx = 1;
		else
			meta->attrs[attr_id].has_idx = 0;
	}

	meta->obj_sz = cur_off;

	/* Mark the meta data as valid */
	memcpy(meta->signature, SOS_SIGNATURE, sizeof(SOS_SIGNATURE));
	return meta;
 err:
	return NULL;
}

static sos_attr_size_fn_t attr_size_fns[] = {
	[SOS_TYPE_INT32] = SOS_TYPE_INT32__attr_size_fn,
	[SOS_TYPE_INT64] = SOS_TYPE_INT64__attr_size_fn,
	[SOS_TYPE_UINT32] = SOS_TYPE_UINT32__attr_size_fn,
	[SOS_TYPE_UINT64] = SOS_TYPE_UINT64__attr_size_fn,
	[SOS_TYPE_DOUBLE] = SOS_TYPE_DOUBLE__attr_size_fn,
	[SOS_TYPE_STRING] = SOS_TYPE_STRING__attr_size_fn,
	[SOS_TYPE_BLOB] = SOS_TYPE_BLOB__attr_size_fn
};

static sos_get_key_fn_t get_key_fns[] = {
	[SOS_TYPE_INT32] = SOS_TYPE_INT32__get_key_fn,
	[SOS_TYPE_INT64] = SOS_TYPE_INT64__get_key_fn,
	[SOS_TYPE_UINT32] = SOS_TYPE_UINT32__get_key_fn,
	[SOS_TYPE_UINT64] = SOS_TYPE_UINT64__get_key_fn,
	[SOS_TYPE_DOUBLE] = SOS_TYPE_DOUBLE__get_key_fn,
	[SOS_TYPE_STRING] = SOS_TYPE_STRING__get_key_fn,
	[SOS_TYPE_BLOB] = SOS_TYPE_BLOB__get_key_fn
};

static sos_set_key_fn_t set_key_fns[] = {
	[SOS_TYPE_INT32] = SOS_TYPE_INT32__set_key_fn,
	[SOS_TYPE_INT64] = SOS_TYPE_INT64__set_key_fn,
	[SOS_TYPE_UINT32] = SOS_TYPE_UINT32__set_key_fn,
	[SOS_TYPE_UINT64] = SOS_TYPE_UINT64__set_key_fn,
	[SOS_TYPE_DOUBLE] = SOS_TYPE_DOUBLE__set_key_fn,
	[SOS_TYPE_STRING] = SOS_TYPE_STRING__set_key_fn,
	[SOS_TYPE_BLOB] = SOS_TYPE_BLOB__set_key_fn
};

static sos_set_fn_t set_fns[] = {
	[SOS_TYPE_INT32] = SOS_TYPE_INT32__set_fn,
	[SOS_TYPE_INT64] = SOS_TYPE_INT64__set_fn,
	[SOS_TYPE_UINT32] = SOS_TYPE_UINT32__set_fn,
	[SOS_TYPE_UINT64] = SOS_TYPE_UINT64__set_fn,
	[SOS_TYPE_DOUBLE] = SOS_TYPE_DOUBLE__set_fn,
	[SOS_TYPE_STRING] = SOS_TYPE_STRING__set_fn,
	[SOS_TYPE_BLOB] = SOS_TYPE_BLOB__set_fn
};

static sos_get_fn_t get_fns[] = {
	[SOS_TYPE_INT32] = SOS_TYPE_INT32__get_fn,
	[SOS_TYPE_INT64] = SOS_TYPE_INT64__get_fn,
	[SOS_TYPE_UINT32] = SOS_TYPE_UINT32__get_fn,
	[SOS_TYPE_UINT64] = SOS_TYPE_UINT64__get_fn,
	[SOS_TYPE_DOUBLE] = SOS_TYPE_DOUBLE__get_fn,
	[SOS_TYPE_STRING] = SOS_TYPE_STRING__get_fn,
	[SOS_TYPE_BLOB] = SOS_TYPE_BLOB__get_fn
};

static sos_class_t init_classp(sos_t sos, sos_meta_t meta)
{
	int attr_id;
	sos_class_t classp =
		malloc(sizeof(*classp) + (meta->attr_cnt * sizeof(struct sos_attr_s)));
	if (!classp)
		goto out;
	classp->name = strdup(meta->classname);
	classp->count= meta->attr_cnt;
	for (attr_id = 0; attr_id < meta->attr_cnt; attr_id++) {
		enum sos_type_e at;
		classp->attrs[attr_id].name = strdup(meta->attrs[attr_id].name);
		classp->attrs[attr_id].type = meta->attrs[attr_id].type;
		classp->attrs[attr_id].has_idx = meta->attrs[attr_id].has_idx;
		at = classp->attrs[attr_id].type;
		if (type_is_builtin(at)) {
			classp->attrs[attr_id].attr_size_fn = attr_size_fns[at];
			classp->attrs[attr_id].get_key_fn = get_key_fns[at];
			classp->attrs[attr_id].set_key_fn = set_key_fns[at];
			classp->attrs[attr_id].set_fn = set_fns[at];
			classp->attrs[attr_id].get_fn = get_fns[at];
		}
	}

 out:
	return classp;
}

/*
 * Create/open the indexes as required from the meta data
 */
static sos_class_t init_sos(sos_t sos, int o_flag, int o_mode,
		    sos_meta_t meta, sos_class_t classp)
{
	int attr_id;
	if (!classp) {
		classp = init_classp(sos, meta);
		if (!classp)
			goto err;
	}
	for (attr_id = 0; attr_id < meta->attr_cnt; attr_id++) {
		obj_idx_t oidx;

		classp->attrs[attr_id].id = attr_id;
		classp->attrs[attr_id].sos = sos;
		classp->attrs[attr_id].data = meta->attrs[attr_id].data;

		if (!classp->attrs[attr_id].has_idx) {
			classp->attrs[attr_id].oidx = NULL;
			continue;
		}

		oidx = init_idx(sos, o_flag, o_mode, &classp->attrs[attr_id]);
		if (!oidx)
			goto err;

		classp->attrs[attr_id].oidx = oidx;
	}
	return classp;
 err:
	return NULL;
}

static void free_sos(sos_t sos)
{
	return;
}

sos_class_t dup_class(sos_class_t classp)
{
	size_t sz = sizeof(*classp) +
		(classp->count * sizeof(struct sos_attr_s));
	sos_class_t dup = malloc(sz);
	if (dup)
		memcpy(dup, classp, sz);
	return dup;
}

sos_t sos_open_sz(const char *path, int o_flag, ...)
{
	char tmp_path[PATH_MAX];
	va_list argp;
	int o_mode;
	size_t init_size = 0;
	sos_meta_t meta;
	sos_class_t classp;
	struct stat _stat = {0};
	struct sos_s *sos;

	sos = calloc(1, sizeof(*sos));
	if (!sos)
		goto out;

	sos->path = strdup(path);

	if (o_flag & O_CREAT) {
		va_start(argp, o_flag);
		o_mode = va_arg(argp, int);
		classp = va_arg(argp, sos_class_t);
		init_size = va_arg(argp, size_t);
	} else {
		o_mode = 0;
		classp = NULL;
	}

	if (classp) {
		/* Duplicate the class because we update it with state later */
		classp = dup_class(classp);
		if (!classp) {
			errno = ENOMEM;
			return NULL;
		}
	}

	sprintf(tmp_path, "%s_sos", sos->path);
	sos->ods = ods_open_sz(tmp_path, o_flag, o_mode, init_size);
	if (!sos->ods)
		goto err;

	meta = ods_get_user_data(sos->ods, &sos->meta_sz);
	if (memcmp(meta->signature, SOS_SIGNATURE, sizeof(SOS_SIGNATURE))) {
		/*
		 * You can't create a new repository without a class
		 * definition.
		 */
		if (!classp)
			goto err;

		meta = make_meta(sos, meta, classp);
		if (!meta)
			goto err;
	}
	sos->meta = meta;
	sprintf(tmp_path, "%s_sos.OBJ", sos->path);
	stat(tmp_path, &_stat);
	sos->classp = init_sos(sos, o_flag, _stat.st_mode | o_mode, meta, classp);
	if (!sos->classp)
		goto err;
 out:
	return sos;

 err:
	free_sos(sos);
	return NULL;
}

/**
 * Create a new tuple store
 */
sos_t sos_open(const char *path, int o_flag, ...)
{
	va_list argp;
	int o_mode;
	sos_class_t classp;

	if (o_flag & O_CREAT) {
		va_start(argp, o_flag);
		o_mode = va_arg(argp, int);
		classp = va_arg(argp, sos_class_t);
	} else {
		o_mode = 0;
		classp = NULL;
	}

	return sos_open_sz(path, o_flag, o_mode, classp, SOS_INITIAL_SIZE);
}

int sos_extend(sos_t sos, size_t sz)
{
	int rc = ods_extend(sos->ods, sos_meta(sos)->ods_extend_sz);
	if (rc) {
		perror("ods_extend");
		return rc;
	}
	sos->meta = ods_get_user_data(sos->ods, &sos->meta_sz);
	return 0;
}

void sos_obj_delete(sos_t sos, sos_obj_t obj)
{
	/* free the blobs/strings first, otherwise they will be
	 * dangling blobs/strings */
	int i;
	obj_ref_t ref;
	void *ptr;
	for (i = 0; i < sos->classp->count; i++) {
		sos_attr_t attr = sos_obj_attr_by_id(sos, i);
		switch (attr->type) {
		case SOS_TYPE_BLOB:
		case SOS_TYPE_STRING:
			ref = *(obj_ref_t *)&obj->data[attr->data];
			ptr = ods_obj_ref_to_ptr(attr->sos->ods, ref);
			ods_free(attr->sos->ods, ptr);
			break;
		default:
			/* do nothing */
			break;
		}
	}
	ods_free(sos->ods, obj);
}

sos_obj_t sos_obj_new(sos_t sos)
{
	sos_obj_t obj = ods_alloc(sos->ods, sos_meta(sos)->obj_sz);
	if (!obj) {
		if (sos_extend(sos, sos_meta(sos)->ods_extend_sz))
			goto err;
		obj = ods_alloc(sos->ods, sos_meta(sos)->obj_sz);
		if (!obj) {
			errno = ENOMEM;
			goto err;
		}
	}
	bzero(obj, sos_meta(sos)->obj_sz);
	return obj;
 err:
	return NULL;
}

size_t SOS_TYPE_INT32__attr_size_fn(sos_attr_t attr, sos_obj_t obj)
{
	return sizeof(int32_t);
}

size_t SOS_TYPE_UINT32__attr_size_fn(sos_attr_t attr, sos_obj_t obj)
{
	return sizeof(uint32_t);
}

size_t SOS_TYPE_INT64__attr_size_fn(sos_attr_t attr, sos_obj_t obj)
{
	return sizeof(int64_t);
}

size_t SOS_TYPE_UINT64__attr_size_fn(sos_attr_t attr, sos_obj_t obj)
{
	return sizeof(uint64_t);
}

size_t SOS_TYPE_DOUBLE__attr_size_fn(sos_attr_t attr, sos_obj_t obj)
{
	return sizeof(double);
}

size_t SOS_TYPE_STRING__attr_size_fn(sos_attr_t attr, sos_obj_t obj)
{
	obj_ref_t ref = *(obj_ref_t *)&obj->data[attr->data];
	char *str = ods_obj_ref_to_ptr(attr->sos->ods, ref);
	return strlen(str) + 1;
}

size_t SOS_TYPE_BLOB__attr_size_fn(sos_attr_t attr, sos_obj_t obj)
{
	obj_ref_t ref = *(obj_ref_t *)&obj->data[attr->data];
	sos_blob_obj_t blob = ods_obj_ref_to_ptr(attr->sos->ods, ref);
	return sizeof(*blob) + blob->len;
}

void SOS_TYPE_INT32__get_key_fn(sos_attr_t attr, sos_obj_t obj, obj_key_t key)
{
	obj_key_set(key, &obj->data[attr->data], sizeof(int32_t));
}

void SOS_TYPE_UINT32__get_key_fn(sos_attr_t attr, sos_obj_t obj, obj_key_t key)
{
	obj_key_set(key, &obj->data[attr->data], sizeof(uint32_t));
}

void SOS_TYPE_INT64__get_key_fn(sos_attr_t attr, sos_obj_t obj, obj_key_t key)
{
	obj_key_set(key, &obj->data[attr->data], sizeof(int64_t));
}

void SOS_TYPE_UINT64__get_key_fn(sos_attr_t attr, sos_obj_t obj, obj_key_t key)
{
	obj_key_set(key, &obj->data[attr->data], sizeof(uint64_t));
}

void SOS_TYPE_DOUBLE__get_key_fn(sos_attr_t attr, sos_obj_t obj, obj_key_t key)
{
	obj_key_set(key, &obj->data[attr->data], sizeof(double));
}

void SOS_TYPE_STRING__get_key_fn(sos_attr_t attr, sos_obj_t obj, obj_key_t key)
{
	obj_ref_t ref = *(obj_ref_t *)&obj->data[attr->data];
	char *str = ods_obj_ref_to_ptr(attr->sos->ods, ref);
	obj_key_set(key, str, strlen(str)+1);
}

void SOS_TYPE_BLOB__get_key_fn(sos_attr_t attr, sos_obj_t obj, obj_key_t key)
{
	obj_ref_t ref = *(obj_ref_t *)&obj->data[attr->data];
	sos_blob_obj_t blob = ods_obj_ref_to_ptr(attr->sos->ods, ref);
	obj_key_set(key, blob, sizeof(*blob) + blob->len);
}

void SOS_TYPE_INT32__set_key_fn(sos_attr_t attr, void *value, obj_key_t key)
{
	obj_key_set(key, value, sizeof(int32_t));
}

void SOS_TYPE_UINT32__set_key_fn(sos_attr_t attr, void *value, obj_key_t key)
{
	obj_key_set(key, value, sizeof(uint32_t));
}

void SOS_TYPE_INT64__set_key_fn(sos_attr_t attr, void *value, obj_key_t key)
{
	obj_key_set(key, value, sizeof(int64_t));
}

void SOS_TYPE_UINT64__set_key_fn(sos_attr_t attr, void *value, obj_key_t key)
{
	obj_key_set(key, value, sizeof(uint64_t));
}

void SOS_TYPE_DOUBLE__set_key_fn(sos_attr_t attr, void *value, obj_key_t key)
{
	obj_key_set(key, value, sizeof(double));
}

void SOS_TYPE_STRING__set_key_fn(sos_attr_t attr, void *value, obj_key_t key)
{
	obj_key_set(key, value, strlen(value)+1);
}

void SOS_TYPE_BLOB__set_key_fn(sos_attr_t attr, void *value, obj_key_t key)
{
	sos_blob_obj_t blob = value;
	obj_key_set(key, blob, sizeof(*blob) +  blob->len);
}

void SOS_TYPE_INT32__set_fn(sos_attr_t attr, sos_obj_t obj, void *value)
{
	*(int32_t *)(&obj->data[attr->data]) = *(int32_t *)value;
}

void SOS_TYPE_UINT32__set_fn(sos_attr_t attr, sos_obj_t obj, void *value)
{
	*(uint32_t *)(&obj->data[attr->data]) = *(uint32_t *)value;
}

void SOS_TYPE_INT64__set_fn(sos_attr_t attr, sos_obj_t obj, void *value)
{
	*(int64_t *)(&obj->data[attr->data]) = *(int64_t *)value;
}

void SOS_TYPE_UINT64__set_fn(sos_attr_t attr, sos_obj_t obj, void *value)
{
	*(uint64_t *)(&obj->data[attr->data]) = *(uint64_t *)value;
}

void SOS_TYPE_DOUBLE__set_fn(sos_attr_t attr, sos_obj_t obj, void *value)
{
	*(double *)(&obj->data[attr->data]) = *(double *)value;
}

void SOS_TYPE_STRING__set_fn(sos_attr_t attr, sos_obj_t obj, void *value)
{
	obj_ref_t ref = *(obj_ref_t *)&obj->data[attr->data];
	obj_ref_t objref = ods_obj_ptr_to_ref(attr->sos->ods, obj);
	char *dst = ods_obj_ref_to_ptr(attr->sos->ods, ref);
	char *src = (char *)value;
	size_t src_len = strlen(src) + 1;
	if (dst) {
		/* If the memory containing the current value is big enough, use it */
		if (ods_obj_size(attr->sos->ods, dst) >= strlen(value) + 1) {
			strcpy(dst, src);
			return;
		} else
			ods_free(attr->sos->ods, dst);
	}
	dst = ods_alloc(attr->sos->ods, src_len);
	if (!dst) {
		if (ods_extend(attr->sos->ods, (src_len | (SOS_ODS_EXTEND_SZ - 1))+1))
			assert(NULL == "ods extend failure.");
		/* ods extended, obj and meta are now stale: update it */
		attr->sos->meta = ods_get_user_data(attr->sos->ods, &attr->sos->meta_sz);
		obj = ods_obj_ref_to_ptr(attr->sos->ods, objref);
		dst = ods_alloc(attr->sos->ods, src_len);
		if (!dst)
			assert(NULL == "memory allocation failure");
	}
	strcpy(dst, src);
	ref = ods_obj_ptr_to_ref(attr->sos->ods, dst);
	*(obj_ref_t *)&obj->data[attr->data] = ref;
}

void SOS_TYPE_BLOB__set_fn(sos_attr_t attr, sos_obj_t obj, void *value)
{
	ods_t ods = attr->sos->ods;
	obj_ref_t objref = ods_obj_ptr_to_ref(ods, obj);
	obj_ref_t bref = *(obj_ref_t *)&obj->data[attr->data];
	sos_blob_obj_t blob = ods_obj_ref_to_ptr(ods, bref);
	sos_blob_obj_t arg = (typeof(arg))value;
	size_t alloc_len = sizeof(*blob) + arg->len;

	if (blob && ods_obj_size(ods, blob) < alloc_len) {
		/* Cannot reuse space, free it and reset blob */
		ods_free(ods, blob);
		blob = NULL;
		*(obj_ref_t *)&obj->data[attr->data] = 0;
	}

	if (!blob) {
		/* blob not allocated --> allocate it */
		blob = ods_alloc(ods, alloc_len);
		if (!blob) {
			if (ods_extend(ods, (alloc_len | (SOS_ODS_EXTEND_SZ - 1))+1))
				goto err1;
			/* ods extended, obj is now stale: update it */
			attr->sos->meta = ods_get_user_data(attr->sos->ods, &attr->sos->meta_sz);
			obj = ods_obj_ref_to_ptr(attr->sos->ods, objref);
			blob = ods_alloc(ods, alloc_len);
			if (!blob)
				goto err1;
		}
		bref = ods_obj_ptr_to_ref(ods, blob);
	}

	memcpy(blob, arg, alloc_len);
	*(obj_ref_t *)&obj->data[attr->data] = bref;

	return;
err1:
	blob->len = 0;
	/* XXX Have some error report here */
}

void *SOS_TYPE_INT32__get_fn(sos_attr_t attr, sos_obj_t obj)
{
	return &obj->data[attr->data];
}

void *SOS_TYPE_UINT32__get_fn(sos_attr_t attr, sos_obj_t obj)
{
	return &obj->data[attr->data];
}

void *SOS_TYPE_INT64__get_fn(sos_attr_t attr, sos_obj_t obj)
{
	return &obj->data[attr->data];
}

void *SOS_TYPE_UINT64__get_fn(sos_attr_t attr, sos_obj_t obj)
{
	return &obj->data[attr->data];
}

void *SOS_TYPE_DOUBLE__get_fn(sos_attr_t attr, sos_obj_t obj)
{
	return &obj->data[attr->data];
}

void *SOS_TYPE_STRING__get_fn(sos_attr_t attr, sos_obj_t obj)
{
	obj_ref_t ref = *(obj_ref_t*)&obj->data[attr->data];
	return ods_obj_ref_to_ptr(attr->sos->ods, ref);
}

void *SOS_TYPE_BLOB__get_fn(sos_attr_t attr, sos_obj_t obj)
{
	obj_ref_t ref = *(obj_ref_t*)&obj->data[attr->data];
	return ods_obj_ref_to_ptr(attr->sos->ods, ref);
}

void sos_obj_attr_key(sos_t sos, int attr_id, sos_obj_t obj, obj_key_t key)
{
	sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
	if (attr)
		sos_attr_key(attr, obj, key);
}

void sos_attr_key(sos_attr_t attr, sos_obj_t obj, obj_key_t key)
{
	attr->get_key_fn(attr, obj, key);
}

int sos_attr_key_cmp(sos_attr_t attr, obj_key_t a, obj_key_t b)
{
	return obj_key_cmp(attr->oidx, a, b);
}

static int __remove_key(sos_t sos, sos_obj_t obj, sos_iter_t iter)
{
	int attr_id;
	size_t attr_sz;
	size_t key_sz = 1024;
	if (!obj)
		obj = sos_iter_obj(iter);
	obj_ref_t obj_ref = ods_obj_ptr_to_ref(sos->ods, obj);
	obj_iter_t oiter;
	obj_ref_t oref;
	int rc;

	/* Delete key at iterator position */
	if (iter) {
		obj_iter_key_del(iter->iter);
	}

	/* Delete the other keys related to the object */
	obj_key_t key = obj_key_new(key_sz);
	for (attr_id = 0; attr_id < sos_meta(sos)->attr_cnt; attr_id++) {
		if (iter && attr_id == iter->attr->id)
			continue;
		sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
		if (!sos_attr_has_index(attr))
			continue;

		attr_sz = sos_obj_attr_size(sos, attr_id, obj);
		if (attr_sz > key_sz) {
			obj_key_delete(key);
			key_sz = attr_sz;
			key = obj_key_new(key_sz);
		}
		sos_attr_key(attr, obj, key);
		oiter = obj_iter_new(attr->oidx);
		assert(oiter);
		rc = obj_iter_find_glb(oiter, key);
		assert(rc == 0);
		oref = obj_iter_ref(oiter);
		while (oref != obj_ref) {
			rc = obj_iter_next(oiter);
			assert(rc == 0);
			oref = obj_iter_ref(oiter);
		}
		obj_iter_key_del(oiter);
		obj_iter_delete(oiter);
	}
	obj_key_delete(key);

	return 0;
}

int sos_obj_remove(sos_t sos, sos_obj_t obj)
{
	return __remove_key(sos, obj, NULL);
}

int sos_iter_obj_remove(sos_iter_t iter)
{
	return __remove_key(iter->sos, NULL, iter);
}

int sos_obj_add(sos_t sos, sos_obj_t obj)
{
	int attr_id;
	obj_ref_t obj_ref = ods_obj_ptr_to_ref(sos->ods, obj);
	size_t attr_sz;
	size_t key_sz = 1024;
	obj_key_t key = obj_key_new(key_sz);

	for (attr_id = 0; attr_id < sos_meta(sos)->attr_cnt; attr_id++) {
		sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
		if (!sos_attr_has_index(attr))
			continue;
		attr_sz = sos_obj_attr_size(sos, attr_id, obj);
		if (attr_sz > key_sz) {
			obj_key_delete(key);
			key_sz = attr_sz;
			key = obj_key_new(key_sz);
		}
		sos_attr_key(attr, obj, key);
		if (obj_idx_insert(attr->oidx, key, obj_ref))
			goto err;
	}
	obj_key_delete(key);
	return 0;
 err:
	obj_key_delete(key);
	return -1;
}

void sos_obj_attr_set(sos_t sos, int attr_id, sos_obj_t obj, void *value)
{
	sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
	if (!attr)
		/* SegV on a bad attribute id */
		kill(getpid(), SIGSEGV);

	sos_attr_set(attr, obj, value);
}

void sos_attr_set(sos_attr_t attr, sos_obj_t obj, void *value)
{
	attr->set_fn(attr, obj, value);
}

void *sos_obj_attr_get(sos_t sos, int attr_id, sos_obj_t obj)
{
	sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
	if (!attr)
		/* SegV on a bad attribute id */
		kill(getpid(), SIGSEGV);
	return attr->get_fn(attr, obj);
}

void *sos_attr_get(sos_attr_t attr, sos_obj_t obj)
{
	return &obj->data[attr->data];
}


inline
sos_attr_t sos_obj_attr_by_name(sos_t sos, const char *name)
{
	sos_attr_t attr;
	int n = sos->classp->count;
	int i;
	for (i = 0; i < n; i++) {
		attr = sos->classp->attrs + i;
		if (strcmp(attr->name, name)==0)
			return attr;
	}
	return NULL;
}

LIST_HEAD(__str_lst_head, __str_lst);
struct __str_lst {
	char *str;
	LIST_ENTRY(__str_lst) link;
};

void __str_lst_add(struct __str_lst_head *head, const char *str)
{
	struct __str_lst *sl = malloc(sizeof(*sl));
	sl->str = strdup(str);
	if (!sl->str)
		goto err0;
	LIST_INSERT_HEAD(head, sl, link);
	return;
err0:
	free(sl);
}

sos_t sos_destroy(sos_t sos)
{
	char *str = malloc(PATH_MAX+16);
	struct __str_lst_head head = {0};
	if (!str)
		return NULL; /* errno = ENOMEM should be set already */
	int i;
	/* object files */
	sprintf(str, "%s_sos.OBJ", sos->path);
	__str_lst_add(&head, str);
	sprintf(str, "%s_sos.PG", sos->path);
	__str_lst_add(&head, str);
	/* index files */
	for (i = 0; i < sos_meta(sos)->attr_cnt; i++) {
		sos_attr_t attr = sos->classp->attrs + i;
		if (attr->has_idx) {
			sprintf(str, "%s_%s.OBJ", sos->path, attr->name);
			__str_lst_add(&head, str);
			sprintf(str, "%s_%s.PG", sos->path, attr->name);
			__str_lst_add(&head, str);
		}
	}
	free(str);
	sos_close(sos, ODS_COMMIT_ASYNC);
	struct __str_lst *sl;
	int rc;
	while ((sl = LIST_FIRST(&head)) != NULL) {
		LIST_REMOVE(sl, link);
		rc = unlink(sl->str);
		if (rc)
			perror("unlink");
		free(sl->str);
		free(sl);
	}
	return sos;
}

void print_obj(sos_t sos, sos_obj_t obj, int attr_id)
{
	uint32_t t;
	uint32_t ut;
	uint32_t comp_id;
	uint64_t value;
	obj_key_t key = obj_key_new(1024);
	char tbuf[32];
	char kbuf[32];
	sos_blob_obj_t blob;
	const char *str;

	sos_obj_attr_key(sos, attr_id, obj, key);

	t = *(uint32_t *)sos_obj_attr_get(sos, 0, obj);
	ut = *(uint32_t *)sos_obj_attr_get(sos, 1, obj);
	sprintf(tbuf, "%d:%d", t, ut);
	sprintf(kbuf, "%02hhx:%02hhx:%02hhx:%02hhx",
		key->value[0],
		key->value[1],
		key->value[2],
		key->value[3]);
	comp_id = *(uint32_t *)sos_obj_attr_get(sos, 2, obj);
	value = *(uint64_t *)sos_obj_attr_get(sos, 3, obj);
	str = sos_obj_attr_get(sos, 4, obj);
	blob = sos_obj_attr_get(sos, 5, obj);
	printf("%11s %16s %8d %12lu %12s %*s\n",
	       kbuf, tbuf, comp_id, value, str, (int)blob->len, blob->data);
	obj_key_delete(key);
}

void sos_print_obj(sos_t sos, sos_obj_t obj, int attr_id)
{
	print_obj(sos, obj, attr_id);
}

/*** helper functions for swig ***/
int sos_get_attr_count(sos_t sos)
{
	return sos->classp->count;
}

enum sos_type_e sos_get_attr_type(sos_t sos, int attr_id)
{
	sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
	if (!attr)
		return SOS_TYPE_UNKNOWN;
	return attr->type;
}

const char *sos_get_attr_name(sos_t sos, int attr_id)
{
	sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
	if (!attr)
		return "N/A";
	return attr->name;
}

void sos_key_set_int32(sos_t sos, int attr_id, int32_t value, obj_key_t key)
{
	sos_obj_attr_key_set(sos, attr_id, &value, key);
}

void sos_key_set_int64(sos_t sos, int attr_id, int64_t value, obj_key_t key)
{
	sos_obj_attr_key_set(sos, attr_id, &value, key);
}

void sos_key_set_uint32(sos_t sos, int attr_id, uint32_t value, obj_key_t key)
{
	sos_obj_attr_key_set(sos, attr_id, &value, key);
}

void sos_key_set_uint64(sos_t sos, int attr_id, uint64_t value, obj_key_t key)
{
	sos_obj_attr_key_set(sos, attr_id, &value, key);
}

void sos_key_set_double(sos_t sos, int attr_id, double value, obj_key_t key)
{
	sos_obj_attr_key_set(sos, attr_id, &value, key);
}

int sos_verify_index(sos_t sos, int attr_id)
{
	sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
	if (!sos_attr_has_index(attr))
		return 0;
	return obj_idx_verify(attr->oidx);
}

struct __idx_ods_arg {
	int rc;
	sos_attr_t attr;
};

void __idx_ods_rebuild_fn(ods_t ods, void *ptr, size_t sz, void *_arg)
{
	struct __idx_ods_arg *arg = _arg;
	if (arg->rc)
		return;
	sos_t sos = arg->attr->sos;
	sos_attr_t attr = arg->attr;
	size_t attr_sz = sos_obj_attr_size(sos, attr->id, ptr);
	obj_ref_t obj_ref = ods_obj_ptr_to_ref(sos->ods, ptr);
	obj_key_t key = obj_key_new(attr_sz);
	if (!key) {
		arg->rc = ENOMEM;
		return;
	}
	sos_attr_key(attr, ptr, key);
	arg->rc = obj_idx_insert(attr->oidx, key, obj_ref);
	obj_key_delete(key);
}

int sos_rebuild_index(sos_t sos, int attr_id)
{
	int rc = 0;
	char *buff = malloc(PATH_MAX);
	if (!buff) {
		rc = ENOMEM;
		goto out;
	}
	sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
	obj_idx_close(attr->oidx, ODS_COMMIT_ASYNC);
	attr->oidx = NULL;
	snprintf(buff, PATH_MAX, "%s_%s.OBJ", sos->path, attr->name);
	rc = unlink(buff);
	if (rc)
		goto out;
	snprintf(buff, PATH_MAX, "%s_%s.PG", sos->path, attr->name);
	rc = unlink(buff);
	if (rc)
		goto out;
	attr->oidx = init_idx(sos, O_CREAT|O_RDWR, 0660, attr);
	if (!attr->oidx) {
		fprintf(stderr, "sos: ERROR: Cannot initialize index: %s_%s\n",
			sos->path, attr->name);
		rc = -1;
		goto out;
	}

	struct __idx_ods_arg arg = {0, attr};

	ods_iter(sos->ods, __idx_ods_rebuild_fn, &arg);
	rc = arg.rc;

out:
	free(buff);
	return rc;
}

#define SOS_BAK_FMT ".%d_sos"

static int __get_latest_backup(const char *sos_path)
{
	char *buff;
	int i, rc;
	struct stat st;

	buff = malloc(PATH_MAX);
	if (!buff)
		return -1;
	i = 1;
	while (1) {
		/* Check for sos_path.i.OBJ */
		snprintf(buff, PATH_MAX, "%s" SOS_BAK_FMT ".OBJ", sos_path, i);
		rc = stat(buff, &st);
		if (rc)
			goto out;

		/* Check for sos_path.i.PG */
		snprintf(buff, PATH_MAX, "%s" SOS_BAK_FMT ".PG", sos_path, i);
		rc = stat(buff, &st);
		if (rc)
			goto out;
		i++;
	}
out:
	free(buff);
	return i - 1;
}

static int __rename_obj_pg(const char *from, const char *to)
{
	size_t flen, tlen;
	char *_from, *_to;
	int rc = 0;

	flen = strlen(from);
	tlen = strlen(to);

	_from = malloc(flen + 5 + tlen + 5);
	if (!_from)
		return ENOMEM;
	_to = _from + flen + 5;
	strcpy(_from, from);
	strcpy(_to, to);

	strcpy(_from + flen, ".OBJ");
	strcpy(_to + tlen, ".OBJ");

	rc = rename(_from, _to);
	if (rc)
		goto out;

	strcpy(_from + flen, ".PG");
	strcpy(_to + tlen, ".PG");
	rc = rename(_from, _to);
	if (rc)
		goto revert;

	goto out;

revert:
	strcpy(_from + flen, ".OBJ");
	strcpy(_to + tlen, ".OBJ");
	/* trying to revert ... it is not guarantee though that this
	 * rename will be a success */
	rename(_to, _from);
out:
	free(_from);
	return rc;
}

/*
 * path is mutable, and expect to be able to append ".OBJ" or ".PG" at the end.
 */
static void __unlink_obj_pg(char *path)
{
	size_t len = strlen(path);
	strcpy(path + len, ".OBJ");
	unlink(path);
	strcpy(path + len, ".PG");
	unlink(path);
	path[len] = 0;
}

int sos_chown(sos_t sos, uid_t owner, gid_t group)
{
	int rc;
	sos_attr_t attr;
	int attr_id;
	int attr_count = sos_get_attr_count(sos);
	for (attr_id = 0; attr_id < attr_count; attr_id++) {
		attr = sos_obj_attr_by_id(sos, attr_id);
		if (!attr->has_idx)
			continue;
		rc = obj_idx_chown(attr->oidx, owner, group);
		if (rc)
			return rc;
	}
	return ods_chown(sos->ods, owner, group);
}

sos_t sos_rotate(sos_t sos, int N)
{
	sos_t new_sos = NULL;
	int M = __get_latest_backup(sos->path);
	char *buff;
	char *_a, *_b, *_tmp;
	size_t _len = strlen(sos->path);
	int i, attr_id, attr_count;
	struct stat st;
	int rc;

	rc = ods_stat(sos->ods, &st);
	if (rc)
		return NULL;

	buff = malloc(PATH_MAX * 2);
	if (!buff)
		return NULL;
	_a = buff;
	_b = _a + PATH_MAX;
	strcpy(_a, sos->path);
	strcpy(_b, sos->path);

	/* rename i --> i+1 */
	sprintf(_b + _len, SOS_BAK_FMT, M+1);
	for (i = M; i > -1; i--) {
		if (i)
			sprintf(_a + _len, SOS_BAK_FMT, i);
		else
			sprintf(_a + _len, "_sos");
		rc = __rename_obj_pg(_a, _b);

		if (rc)
			goto roll_back1;

		_tmp = _a;
		_a = _b;
		_b = _tmp;
	}

	/* rename && unlink indices
	 *   Rename before unlink because other processes might also have
	 *   index files opened, making the files lingering around until such
	 *   processes close the files. Non SOS owner processes are not expected
	 *   to open and use SOS for a long period of time.
	 */
	attr_count = sos_get_attr_count(sos);
	for (attr_id = 0; attr_id < attr_count; attr_id++) {
		sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
		if (!attr->has_idx)
			continue;
		sprintf(_a + _len, "_%s", attr->name);
		sprintf(_b + _len, ".1_%s", attr->name);
		rc = __rename_obj_pg(_a, _b);
		if (rc)
			goto roll_back2;
	}

	/* create new sos */
	new_sos = sos_open(sos->path, O_CREAT | O_RDWR, st.st_mode, sos->classp);
	if (!new_sos)
		goto roll_back2;
	sos_chown(new_sos, st.st_uid, st.st_gid);

	/* close old sos and strip the indices, there's no going from here */
	sos_close(sos, ODS_COMMIT_ASYNC);
	for (attr_id = 0; attr_id < attr_count; attr_id++) {
		sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
		if (!attr->has_idx)
			continue;
		sprintf(_a + _len, ".1_%s", attr->name);
		__unlink_obj_pg(_a);
	}

	/* remove too-old backups */
	if (!N)
		goto out;
	for (i = N+1; i <= M + 1; i++) {
		sprintf(_a + _len, SOS_BAK_FMT, i);
		__unlink_obj_pg(_a);
	}
	goto out;

roll_back2:
	/* index rename rollback */
	for (attr_id = 0; attr_id < attr_count; attr_id++) {
		sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
		if (!attr->has_idx)
			continue;
		sprintf(_a + _len, "_%s", attr->name);
		sprintf(_b + _len, ".1_%s", attr->name);
		__rename_obj_pg(_b, _a);
	}
roll_back1:
	/* _sos rename rollback */
	i++;
	if (i)
		sprintf(_a + _len, SOS_BAK_FMT, i);
	else
		sprintf(_a + _len, "_sos");
	while (i <= M) {
		/* rename i+1 -> i */
		sprintf(_b + _len, SOS_BAK_FMT, i+1);
		__rename_obj_pg(_b, _a);

		i++;
		_tmp = _a;
		_a = _b;
		_b = _tmp;
	}
out:
	free(buff);
	return new_sos;
}

int sos_post_rotation(sos_t sos, const char *env_var)
{
	const char *cmd = getenv(env_var);
	char *buff;
	int rc = 0;
	if (!cmd)
		return ENOENT;
	buff = malloc(65536);
	if (!buff)
		return ENOMEM;
	snprintf(buff, 65536, "SOS_PATH=\"%s\" %s", sos->path, cmd);
	if (-1 == ovis_execute(buff))
		rc = ENOMEM;

	free(buff);
	return rc;
}

sos_t sos_reinit(sos_t sos, uint64_t sz)
{
	char *buff;
	int attr_id, attr_count;
	sos_attr_t attr;
	sos_t new_sos = NULL;
	sos_class_t class = NULL;
	mode_t mode = 0660;
	int rc = 0;
	struct stat _stat;

	if (!sz)
		sz = SOS_INITIAL_SIZE;

	buff = malloc(PATH_MAX);
	if (!buff)
		goto out;

	class = dup_class(sos->classp);
	if (!class)
		goto out;

	attr_count = sos->classp->count;
	for (attr_id = 0; attr_id < attr_count; attr_id++) {
		attr = sos_obj_attr_by_id(sos, attr_id);
		if (!attr->has_idx)
			continue;
		sprintf(buff, "%s_%s", sos->path, attr->name);
		__unlink_obj_pg(buff);
	}
	rc = ods_stat(sos->ods, &_stat);
	if (!rc)
		mode = _stat.st_mode;
	sprintf(buff, "%s_sos", sos->path);
	__unlink_obj_pg(buff);
	sos_close(sos, ODS_COMMIT_ASYNC);
	buff[strlen(buff) - 4] = 0;
	new_sos = sos_open_sz(buff, O_CREAT|O_RDWR, mode, class, sz);
out:
	free(buff);
	free(class);
	return new_sos;
}

/*** end helper functions ***/

#ifdef SOS_MAIN
#include <stddef.h>
#include <coll/idx.h>

static void get_value_key(sos_attr_t attr, sos_obj_t obj, obj_key_t key)
{
	uint32_t kv;
	uint32_t limit = 100;
	uint64_t v = *(uint64_t *)sos_attr_get(attr, obj);

	kv = 0;
	key->len = 4;
	do {
		if (v < limit)
			break;
		limit += 100;
		kv++;
	} while (1);
	kv = htobe32(kv);
	memcpy(key->value, (unsigned char *)&kv, 4);
}

static void set_key_value(sos_attr_t attr, void *value, obj_key_t key)
{
	uint32_t kv = *(uint32_t *)value;
	kv = htobe32(kv);
	memcpy(key->value, (unsigned char *)&kv, 4);
	key->len = 4;
}

SOS_OBJ_BEGIN(ovis_metric_class, "OvisMetric")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("comp_id", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("value", SOS_TYPE_UINT64),
	SOS_OBJ_ATTR_WITH_KEY("string", SOS_TYPE_STRING),
	SOS_OBJ_ATTR_WITH_KEY("blob", SOS_TYPE_BLOB),
SOS_OBJ_END(6);

idx_t ct_idx;
idx_t c_idx;
struct metric_store_s {
	sos_t sos;
	char *key;
	LIST_ENTRY(metric_store_s) entry;
};
LIST_HEAD(ms_q, metric_store_s) ms_head;

void print_header(struct metric_store_s *m, const char *attr_name)
{
	printf("\n%s by %s\n", m->key, attr_name);
	printf("%11s %16s %8s %12s %12s %12s\n", "Key", "Time", "CompID", "Value", "String", "Blob");
	printf("----------- ---------------- -------- ------------ ------------ ------------\n");
}

void dump_metric_store_fwd(struct metric_store_s *m,
			   int attr_id)
{
	int rc;
	sos_obj_t obj;
	sos_iter_t iter = sos_iter_new(m->sos, attr_id);
	print_header(m, sos_iter_name(iter));
	for (rc = sos_iter_begin(iter); !rc; rc = sos_iter_next(iter)) {
		obj = sos_iter_obj(iter);
		print_obj(m->sos, obj, attr_id);
	}
	sos_iter_free(iter);
}

void dump_metric_store_bkwd(struct metric_store_s *m,
			    int attr_id)
{
	int rc;
	sos_obj_t obj;
	sos_iter_t iter = sos_iter_new(m->sos, attr_id);
	print_header(m, sos_iter_name(iter));
	for (rc = sos_iter_end(iter); !rc; rc = sos_iter_prev(iter)) {
		obj = sos_iter_obj(iter);
		print_obj(m->sos, obj, attr_id);
	}
	sos_iter_free(iter);
}

char tmp_path[PATH_MAX];
int main(int argc, char *argv[])
{
	char *s;
	static char pfx[32];
	static char buf[128];
	static char buf2[128] = {0};
	sos_blob_obj_t blob = (void*)buf2;
	char *str;
	static char c_key[32];
	static char comp_type[32];
	static char metric_name[32];
	struct metric_store_s *m;

	if (argc < 2) {
		printf("usage: ./sos <dir>\n");
		exit(1);
	}
	strcpy(pfx, argv[1]);
	ct_idx = idx_create();
	c_idx = idx_create();

	while ((s = fgets(buf, sizeof(buf), stdin)) != NULL) {
		uint32_t comp_id;
		uint32_t tv_sec;
		uint32_t tv_usec;
		uint64_t value;
		int n;

		sscanf(buf, "%[^,],%[^,],%d,%ld,%d,%d,%s",
		       comp_type, metric_name,
		       &comp_id,
		       &value,
		       &tv_usec,
		       &tv_sec,
		       blob->data);
		blob->len = strlen(blob->data) + 1;

		/*
		 * Add a component type directory if one does not
		 * already exist
		 */
		if (!idx_find(ct_idx, &comp_type, 2)) {
			sprintf(tmp_path, "%s/%s", pfx, comp_type);
			mkdir(tmp_path, 0777);
			idx_add(ct_idx, &comp_type, 2, (void *)1UL);
		}
		sprintf(c_key, "%s:%s", comp_type, metric_name);
		m = idx_find(c_idx, c_key, strlen(c_key));
		if (!m) {
			/*
			 * Open a new SOS for this component-type and
			 * metric combination
			 */
			m = malloc(sizeof *m);
			sprintf(tmp_path, "%s/%s/%s", pfx, comp_type, metric_name);
			m->key = strdup(c_key);
			m->sos = sos_open_sz(tmp_path, O_CREAT | O_RDWR, 0660,
					  &ovis_metric_class, SOS_INITIAL_SIZE);
			if (m->sos) {
				idx_add(c_idx, c_key, strlen(c_key), m);
				LIST_INSERT_HEAD(&ms_head, m, entry);
			} else {
				free(m);
				printf("Could not create SOS database '%s'\n",
				       tmp_path);
				exit(1);
			}
		}
		/* Allocate a new object */
		sos_obj_t obj = sos_obj_new(m->sos);
		if (!obj)
			goto err;

		obj_ref_t objref = ods_obj_ptr_to_ref(m->sos->ods, obj);

		sos_obj_attr_set(m->sos, 0, obj, &tv_sec);
		sos_obj_attr_set(m->sos, 1, obj, &tv_usec);
		sos_obj_attr_set(m->sos, 2, obj, &comp_id);
		sos_obj_attr_set(m->sos, 3, obj, &value);
		sos_obj_attr_set(m->sos, 4, obj, blob->data); /* string */
		/* obj may stale due to possible ods_extend */
		obj = ods_obj_ref_to_ptr(m->sos->ods, objref);
		sos_obj_attr_set(m->sos, 5, obj, blob); /* blob */
		/* obj may stale due to possible ods_extend */
		obj = ods_obj_ref_to_ptr(m->sos->ods, objref);

		/* Add it to the indexes */
		if (sos_obj_add(m->sos, obj))
			goto err;
	}
	/*
	 * Iterate forwards through all the objects we've added and
	 * print them out
	 */
	LIST_FOREACH(m, &ms_head, entry) {
		dump_metric_store_fwd(m, 3);
	}
	LIST_FOREACH(m, &ms_head, entry) {
		dump_metric_store_fwd(m, 0);
	}
	LIST_FOREACH(m, &ms_head, entry) {
		dump_metric_store_fwd(m, 2);
	}
	LIST_FOREACH(m, &ms_head, entry) {
		dump_metric_store_fwd(m, 4);
	}
	LIST_FOREACH(m, &ms_head, entry) {
		dump_metric_store_fwd(m, 5);
	}
	/*
	 * Iterate backwards through all the objects we've added and
	 * print them out
	 */
	LIST_FOREACH(m, &ms_head, entry) {
		dump_metric_store_bkwd(m, 3);
	}
	LIST_FOREACH(m, &ms_head, entry) {
		dump_metric_store_bkwd(m, 0);
	}
	LIST_FOREACH(m, &ms_head, entry) {
		dump_metric_store_bkwd(m, 2);
	}
	LIST_FOREACH(m, &ms_head, entry) {
		dump_metric_store_bkwd(m, 4);
	}
	LIST_FOREACH(m, &ms_head, entry) {
		dump_metric_store_bkwd(m, 5);
	}

	LIST_FOREACH(m, &ms_head, entry) {
		sos_close(m->sos, ODS_COMMIT_SYNC);
	}
	return 0;
 err:
	return 1;
}
#endif
