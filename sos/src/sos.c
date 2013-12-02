/*
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
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
#include "../config.h"

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
	[SOS_TYPE_BLOB] = 16, /**< \note length + offset = 8 + 8 = 16 bytes */
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

size_t sos_attr_size(sos_t sos, sos_attr_t attr)
{
	return attr->size_fn(attr);
}

const char *sos_attr_name(sos_t sos, sos_attr_t attr)
{
	return attr->name;
}

int sos_attr_id(sos_t sos, sos_attr_t attr)
{
}

void sos_attr_key_set(sos_attr_t attr, void *value, sos_key_t key)
{
	attr->set_key_fn(attr, value, key);
}

void sos_obj_attr_key_set(sos_t sos, int attr_id, void *value, sos_key_t key)
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

	i = calloc(1, sizeof *i);
	if (!i)
		goto err;

	i->sos = sos;
	i->attr = attr;
	i->iter = oidx_iter_new(i->attr->oidx);
	if (!i->iter)
		goto err;
	struct sos_key_s k;
	uint64_t v = 0;
	sos_attr_key_set(attr, &v, &k);
	sos_iter_seek_sup(i, &k);
	return i;
 err:
	if (i)
		free(i);
	return NULL;
}

void sos_iter_free(sos_iter_t iter)
{
	oidx_iter_free(iter->iter);
	free(iter);
}

static inline sos_dattr_t get_dattr(sos_t sos, int attr_id)
{
	return &sos->meta->attrs[attr_id];
}

static sos_link_t get_link_from_dattr(sos_t sos, sos_dattr_t dattr, sos_obj_t obj)
{
	return (sos_link_t)((unsigned char *)obj + dattr->link);
}

static inline sos_link_t get_link(sos_t sos, sos_dattr_t dattr, uint64_t off)
{
	return get_link_from_dattr(sos, dattr,
				   ods_obj_offset_to_ptr(sos->ods, off));
}

static sos_link_t get_attr_link(sos_t sos, sos_attr_t attr, sos_obj_t obj)
{
	return get_link_from_dattr(sos, &sos->meta->attrs[attr->id], obj);
}

const char *sos_iter_name(sos_iter_t i)
{
	return i->attr->name;
}

sos_obj_t sos_iter_next(sos_iter_t i)
{
	uint64_t obj_o = oidx_iter_next_obj(i->iter);
	if (!obj_o)
		return NULL;
	return ods_obj_offset_to_ptr(i->sos->ods, obj_o);
}

sos_obj_t sos_iter_prev(sos_iter_t i)
{
	uint64_t obj_o = oidx_iter_prev_obj(i->iter);
	if (!obj_o)
		return NULL;
	return ods_obj_offset_to_ptr(i->sos->ods, obj_o);
}

void sos_iter_seek_start(sos_iter_t i)
{
	oidx_iter_seek_start(i->iter);
}

/*
 * XXX Doesn't really work ... maybe this is an incomplete implementation ...
 * take a look into this later.
 */
void sos_iter_seek_end(sos_iter_t i)
{
	oidx_iter_seek_end(i->iter);
}

uint64_t sos_iter_seek_sup(sos_iter_t i, sos_key_t key)
{
	uint64_t obj = oidx_iter_seek_sup(i->iter, key->key, key->keylen);
	return obj;
}

uint64_t sos_iter_seek_inf(sos_iter_t i, sos_key_t key)
{
	uint64_t obj = oidx_iter_seek_inf(i->iter, key->key, key->keylen);
	return obj;
}

int sos_iter_seek(sos_iter_t i, sos_key_t key)
{
	if (oidx_iter_seek(i->iter, key->key, key->keylen))
		return 1;
	return 0;
}

static oidx_t init_idx(sos_t sos, int o_flag, int o_mode, sos_attr_t attr)
{
	char tmp_path[PATH_MAX];
	sprintf(tmp_path, "%s_%s", sos->path, attr->name);
	return oidx_open(tmp_path, o_flag, o_mode);
}

/**
 * \brief Blob ODS initialization for the attribute \a attr.
 * \param sos The store handle.
 * \param o_flag The open flag (e.g. O_RDWR)
 * \param o_mode The mode (in the case of creation).
 * \param attr The blob attribute.
 */
static
ods_t init_blob_ods(sos_t sos, int o_flag, int o_mode, sos_attr_t attr)
{
	char tmp_path[PATH_MAX];
	sprintf(tmp_path, "%s_%s_blob", sos->path, attr->name);
	return ods_open(tmp_path, o_flag, o_mode);
}

void sos_flush(sos_t sos)
{
	int attr_id;

	ods_flush(sos->ods);
	for (attr_id = 0; attr_id < sos->classp->count; attr_id++)
		if (sos->classp->attrs[attr_id].oidx)
			oidx_flush(sos->classp->attrs[attr_id].oidx);
}

void sos_close(sos_t sos)
{
	int attr_id;
	oidx_t oidx;
	ods_t ods;

	sos_flush(sos);
	ods_close(sos->ods);
	for (attr_id = 0; attr_id < sos->classp->count; attr_id++) {
		if (oidx = sos->classp->attrs[attr_id].oidx)
			oidx_close(oidx);
		if (ods = sos->classp->attrs[attr_id].blob_ods)
			ods_close(ods);
	}
	sos->ods = NULL;
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
		if (classp->attrs[attr_id].has_idx) {
			/* obsoleted by new object management */
			/*
			meta->attrs[attr_id].link = cur_off;
			cur_off += sizeof(struct sos_link_s);
			*/
			meta->attrs[attr_id].has_idx = 1;
		} else {
			meta->attrs[attr_id].has_idx = 0;
			/* obsoleted by new object management */
			/* meta->attrs[attr_id].link = 0; */
		}
	}

	meta->obj_sz = cur_off;

	/* Mark the meta data as valid */
	memcpy(meta->signature, SOS_SIGNATURE, sizeof(SOS_SIGNATURE));
	return meta;
 err:
	return NULL;
}

static sos_size_fn_t size_fns[] = {
	[SOS_TYPE_INT32] = SOS_TYPE_INT32__size_fn,
	[SOS_TYPE_INT64] = SOS_TYPE_INT64__size_fn,
	[SOS_TYPE_UINT32] = SOS_TYPE_UINT32__size_fn,
	[SOS_TYPE_UINT64] = SOS_TYPE_UINT64__size_fn,
	[SOS_TYPE_DOUBLE] = SOS_TYPE_DOUBLE__size_fn,
	[SOS_TYPE_BLOB] = SOS_TYPE_BLOB__size_fn
};

static sos_get_key_fn_t get_key_fns[] = {
	[SOS_TYPE_INT32] = SOS_TYPE_INT32__get_key_fn,
	[SOS_TYPE_INT64] = SOS_TYPE_INT64__get_key_fn,
	[SOS_TYPE_UINT32] = SOS_TYPE_UINT32__get_key_fn,
	[SOS_TYPE_UINT64] = SOS_TYPE_UINT64__get_key_fn,
	[SOS_TYPE_DOUBLE] = SOS_TYPE_DOUBLE__get_key_fn,
	[SOS_TYPE_BLOB] = SOS_TYPE_BLOB__get_key_fn
};

static sos_set_key_fn_t set_key_fns[] = {
	[SOS_TYPE_INT32] = SOS_TYPE_INT32__set_key_fn,
	[SOS_TYPE_INT64] = SOS_TYPE_INT64__set_key_fn,
	[SOS_TYPE_UINT32] = SOS_TYPE_UINT32__set_key_fn,
	[SOS_TYPE_UINT64] = SOS_TYPE_UINT64__set_key_fn,
	[SOS_TYPE_DOUBLE] = SOS_TYPE_DOUBLE__set_key_fn,
	[SOS_TYPE_BLOB] = SOS_TYPE_BLOB__set_key_fn
};

static sos_cmp_key_fn_t cmp_key_fns[] = {
	[SOS_TYPE_INT32] = SOS_TYPE_INT32__cmp_key_fn,
	[SOS_TYPE_INT64] = SOS_TYPE_INT64__cmp_key_fn,
	[SOS_TYPE_UINT32] = SOS_TYPE_UINT32__cmp_key_fn,
	[SOS_TYPE_UINT64] = SOS_TYPE_UINT64__cmp_key_fn,
	[SOS_TYPE_DOUBLE] = SOS_TYPE_DOUBLE__cmp_key_fn,
	[SOS_TYPE_BLOB] = SOS_TYPE_BLOB__cmp_key_fn
};

static sos_set_fn_t set_fns[] = {
	[SOS_TYPE_INT32] = SOS_TYPE_INT32__set_fn,
	[SOS_TYPE_INT64] = SOS_TYPE_INT64__set_fn,
	[SOS_TYPE_UINT32] = SOS_TYPE_UINT32__set_fn,
	[SOS_TYPE_UINT64] = SOS_TYPE_UINT64__set_fn,
	[SOS_TYPE_DOUBLE] = SOS_TYPE_DOUBLE__set_fn,
	[SOS_TYPE_BLOB] = SOS_TYPE_BLOB__set_fn
};

static sos_get_fn_t get_fns[] = {
	[SOS_TYPE_INT32] = SOS_TYPE_INT32__get_fn,
	[SOS_TYPE_INT64] = SOS_TYPE_INT64__get_fn,
	[SOS_TYPE_UINT32] = SOS_TYPE_UINT32__get_fn,
	[SOS_TYPE_UINT64] = SOS_TYPE_UINT64__get_fn,
	[SOS_TYPE_DOUBLE] = SOS_TYPE_DOUBLE__get_fn,
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
			classp->attrs[attr_id].size_fn = size_fns[at];
			classp->attrs[attr_id].get_key_fn = get_key_fns[at];
			classp->attrs[attr_id].set_key_fn = set_key_fns[at];
			classp->attrs[attr_id].cmp_key_fn = cmp_key_fns[at];
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
		oidx_t oidx;

		classp->attrs[attr_id].id = attr_id;
		classp->attrs[attr_id].sos = sos;
		classp->attrs[attr_id].data = meta->attrs[attr_id].data;

		if (classp->attrs[attr_id].type == SOS_TYPE_BLOB) {
			/* It's a blob, so it needs blob initialization. */
			ods_t ods = init_blob_ods(sos, o_flag, o_mode,
					&classp->attrs[attr_id]);
			if (!ods)
				goto err;
			classp->attrs[attr_id].blob_ods = ods;
		} else {
			classp->attrs[attr_id].blob_ods = NULL;
		}

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

/**
 * Create a new tuple store
 */
sos_t sos_open(const char *path, int o_flag, ...)
{
	char tmp_path[PATH_MAX];
	va_list argp;
	int o_mode;
	sos_meta_t meta;
	sos_class_t classp;
	struct sos_s *sos;

	sos = calloc(1, sizeof(*sos));
	if (!sos)
		goto out;

	sos->path = strdup(path);

	if (o_flag & O_CREAT) {
		va_start(argp, o_flag);
		o_mode = va_arg(argp, int);
		classp = va_arg(argp, sos_class_t);
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
	sos->ods = ods_open(tmp_path, o_flag, o_mode);
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
	sos->classp = init_sos(sos, o_flag, o_mode, meta, classp);
	if (!sos->classp)
		goto err;
 out:
	return sos;

 err:
	free_sos(sos);
	return NULL;
}

int sos_extend(sos_t sos, size_t sz)
{
	int rc = ods_extend(sos->ods, sos->meta->ods_extend_sz);
	if (rc) {
		perror("ods_extend");
		return rc;
	}
	sos->meta = ods_get_user_data(sos->ods, &sos->meta_sz);
	return 0;
}

void sos_obj_delete(sos_t sos, sos_obj_t obj)
{
	/* free the blobs first, otherwise they will be dangling blobs */
	int i;
	for (i = 0; i < sos->classp->count; i++) {
		sos_attr_t attr = sos_obj_attr_by_id(sos, i);
		if (attr->type != SOS_TYPE_BLOB)
			continue;
		/* blob ods_free routine */
		sos_obj_t blob = sos_obj_attr_get(sos, i, obj);
		ods_free(attr->blob_ods, blob);

	}
	ods_free(sos->ods, obj);
}

sos_obj_t sos_obj_new(sos_t sos)
{
	sos_obj_t obj = ods_alloc(sos->ods, sos->meta->obj_sz);
	if (!obj) {
		if (sos_extend(sos, sos->meta->ods_extend_sz))
			goto err;
		obj = ods_alloc(sos->ods, sos->meta->obj_sz);
		if (!obj) {
			errno = ENOMEM;
			goto err;
		}
	}
	bzero(obj, sos->meta->obj_sz);
	return obj;
 err:
	return NULL;
}

size_t SOS_TYPE_INT32__size_fn(sos_attr_t attr)
{
	return sizeof(int32_t);
}

size_t SOS_TYPE_UINT32__size_fn(sos_attr_t attr)
{
	return sizeof(uint32_t);
}

size_t SOS_TYPE_INT64__size_fn(sos_attr_t attr)
{
	return sizeof(int64_t);
}

size_t SOS_TYPE_UINT64__size_fn(sos_attr_t attr)
{
	return sizeof(uint64_t);
}

size_t SOS_TYPE_DOUBLE__size_fn(sos_attr_t attr)
{
	return sizeof(double);
}

size_t SOS_TYPE_BLOB__size_fn(sos_attr_t attr)
{
	return sizeof(struct sos_blob_obj_s);
}

void SOS_TYPE_INT32__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	SOS_ATTR_GET_BE32(key->key, attr, obj);
	key->keylen = sizeof(int32_t);
}

void SOS_TYPE_UINT32__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	SOS_ATTR_GET_BE32(key->key, attr, obj);
	key->keylen = sizeof(uint32_t);
}

void SOS_TYPE_INT64__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	SOS_ATTR_GET_BE64(key->key, attr, obj);
	key->keylen = sizeof(int64_t);
}

void SOS_TYPE_UINT64__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	SOS_ATTR_GET_BE64(key->key, attr, obj);
	key->keylen = sizeof(uint64_t);
}

void SOS_TYPE_DOUBLE__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	//Not implemented
}

void SOS_TYPE_BLOB__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	//Not implemented
}

void SOS_TYPE_INT32__set_key_fn(sos_attr_t attr, void *value, sos_key_t key)
{
	SOS_KEY_SET_BE32(key, value);
	key->keylen = sizeof(int32_t);
}

void SOS_TYPE_UINT32__set_key_fn(sos_attr_t attr, void *value, sos_key_t key)
{
	SOS_KEY_SET_BE32(key, value);
	key->keylen = sizeof(uint32_t);
}

void SOS_TYPE_INT64__set_key_fn(sos_attr_t attr, void *value, sos_key_t key)
{
	SOS_KEY_SET_BE64(key, value);
	key->keylen = sizeof(int64_t);
}

void SOS_TYPE_UINT64__set_key_fn(sos_attr_t attr, void *value, sos_key_t key)
{
	SOS_KEY_SET_BE64(key, value);
	key->keylen = sizeof(uint64_t);
}

void SOS_TYPE_DOUBLE__set_key_fn(sos_attr_t attr, void *value, sos_key_t key)
{
	//Not implemented
}

void SOS_TYPE_BLOB__set_key_fn(sos_attr_t attr, void *value, sos_key_t key)
{
	//Not implemented
}

int SOS_TYPE_INT32__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	int32_t v;
	SOS_ATTR_GET(v, attr, obj);
	return v - htobe32((*(int32_t *)key->key));
}

int SOS_TYPE_UINT32__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	uint32_t v;
	SOS_ATTR_GET(v, attr, obj);
	return v - htobe32((*(uint32_t *)key->key));
}

int SOS_TYPE_INT64__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	int64_t v;
	SOS_ATTR_GET(v, attr, obj);
	return v - htobe64((*(int64_t *)key->key));
}

int SOS_TYPE_UINT64__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	uint64_t v;
	SOS_ATTR_GET(v, attr, obj);
	return v - htobe64((*(uint64_t *)key->key));
}

int SOS_TYPE_DOUBLE__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	//Not implemented
}

int SOS_TYPE_BLOB__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	//Not implemented
	return 0;
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

void SOS_TYPE_BLOB__set_fn(sos_attr_t attr, sos_obj_t obj, void *value)
{
	ods_t bods = attr->blob_ods;
	sos_blob_obj_t blob = (typeof(blob))&obj->data[attr->data];
	sos_blob_arg_t arg = (typeof(arg))value;
	void *ptr;

	if (blob->off && blob->len < arg->len) {
		/* Cannot reuse space, free it and reset blob */
		ods_free(bods, ods_obj_offset_to_ptr(bods, blob->off));
		blob->off = blob->len = 0;
	}

	if (!blob->len) {
		/* blob not allocated --> allocate it */
		ptr = ods_alloc(bods, arg->len);
		if (!ptr) {
			if (ods_extend(bods, (arg->len | 0xFFFFF)+1))
				goto err1;
			ptr = ods_alloc(bods, arg->len);
			if (!ptr)
				goto err1;
		}
		blob->len = ods_get_alloc_size(bods, arg->len);
		blob->off = ods_obj_ptr_to_offset(bods, ptr);
	}

	memcpy(ptr, arg->data, arg->len);

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

void *SOS_TYPE_BLOB__get_fn(sos_attr_t attr, sos_obj_t obj)
{
	sos_blob_obj_t blob = (typeof(blob)) &obj->data[attr->data];
	return ods_obj_offset_to_ptr(attr->blob_ods, blob->off);
}

void sos_obj_attr_key(sos_t sos, int attr_id, sos_obj_t obj, sos_key_t key)
{
	sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
	if (attr)
		sos_attr_key(attr, obj, key);
}

int sos_obj_attr_key_cmp(sos_t sos, int attr_id,
			 sos_obj_t obj, sos_key_t key)
{
	sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
	if (!attr)
		return -1;
	return sos_attr_key_cmp(attr, obj, key);
}

int sos_attr_key_cmp(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	return attr->cmp_key_fn(attr, obj, key);
}

void sos_attr_key(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	attr->get_key_fn(attr, obj, key);
}

/**
 * Unlink an object from the store, but not yet deleting it.
 */
int sos_obj_remove(sos_t sos, sos_obj_t obj)
{
	int attr_id;
	uint64_t obj_o = ods_obj_ptr_to_offset(sos->ods, obj);
	int rc = 0;

	for (attr_id = 0; attr_id < sos->meta->attr_cnt; attr_id++) {
		struct sos_key_s key;
		sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
		if (!sos_attr_has_index(attr))
			continue;

		sos_attr_key(attr, obj, &key);
		if (rc = oidx_obj_remove(attr->oidx, key.key, key.keylen, obj_o))
			return rc;
	}
	return 0;
}

/**
 * \brief Add (index) object to the store.
 * \note Object has already been Object ODS. This function adds the object to
 * the indices (according to key attributes) so that SOS user can iterate
 * through objects correctly.
 */
int sos_obj_add(sos_t sos, sos_obj_t obj)
{
	int attr_id;
	uint64_t *idx_o;
	uint64_t obj_o = ods_obj_ptr_to_offset(sos->ods, obj);

	for (attr_id = 0; attr_id < sos->meta->attr_cnt; attr_id++) {
		sos_attr_t attr = sos_obj_attr_by_id(sos, attr_id);
		struct sos_key_s key;
		if (!sos_attr_has_index(attr))
			continue;
		sos_attr_key(attr, obj, &key);
		if (oidx_add(attr->oidx, key.key, key.keylen, obj_o))
			goto err;
	}

	return 0;
 err:
	if (idx_o)
		free(idx_o);
	return (ssize_t)-1;
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
	for (i = 0; i < sos->meta->attr_cnt; i++) {
		sos_attr_t attr = sos->classp->attrs + i;
		if (attr->has_idx) {
			sprintf(str, "%s_%s.OBJ", sos->path, attr->name);
			__str_lst_add(&head, str);
			sprintf(str, "%s_%s.PG", sos->path, attr->name);
			__str_lst_add(&head, str);
		}
		if (attr->type == SOS_TYPE_BLOB) {
			/* It is a blob, remove the blob file. */
			sprintf(str, "%s_%s_blob.OBJ", sos->path, attr->name);
			__str_lst_add(&head, str);
			sprintf(str, "%s_%s_blob.PG", sos->path, attr->name);
			__str_lst_add(&head, str);
		}
	}
	free(str);
	sos_close(sos);
	struct __str_lst *sl;
	int rc;
	while (sl = LIST_FIRST(&head)) {
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
	struct sos_key_s key;
	char tbuf[32];
	char kbuf[32];

	sos_obj_attr_key(sos, attr_id, obj, &key);

	t = *(uint32_t *)sos_obj_attr_get(sos, 0, obj);
	ut = *(uint32_t *)sos_obj_attr_get(sos, 1, obj);
	sprintf(tbuf, "%d:%d", t, ut);
	sprintf(kbuf, "%02hhx:%02hhx:%02hhx:%02hhx",
		key.key[0],
		key.key[1],
		key.key[2],
		key.key[3]);
	comp_id = *(uint32_t *)sos_obj_attr_get(sos, 2, obj);
	value = *(uint64_t *)sos_obj_attr_get(sos, 3, obj);
	printf("%11s %16s %8d %12lu\n",
	       kbuf, tbuf, comp_id, value);
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

void sos_key_set_int32(sos_t sos, int attr_id, int32_t value, sos_key_t key)
{
	sos_obj_attr_key_set(sos, attr_id, &value, key);
}

void sos_key_set_int64(sos_t sos, int attr_id, int64_t value, sos_key_t key)
{
	sos_obj_attr_key_set(sos, attr_id, &value, key);
}

void sos_key_set_uint32(sos_t sos, int attr_id, uint32_t value, sos_key_t key)
{
	sos_obj_attr_key_set(sos, attr_id, &value, key);
}

void sos_key_set_uint64(sos_t sos, int attr_id, uint64_t value, sos_key_t key)
{
	sos_obj_attr_key_set(sos, attr_id, &value, key);
}

/*** end helper functions ***/

#ifdef SOS_MAIN
#include <stddef.h>
#include <coll/idx.h>

static void get_value_key(sos_attr_t attr, sos_obj_t obj, sos_key_t key)
{
	uint32_t kv;
	uint32_t limit = 100;
	uint64_t v = *(uint64_t *)sos_attr_get(attr, obj);

	kv = 0;
	key->keylen = 4;
	do {
		if (v < limit)
			break;
		limit += 100;
		kv++;
	} while (1);
	kv = htobe32(kv);
	memcpy(key->key, (unsigned char *)&kv, 4);
}

static void set_key_value(sos_attr_t attr, void *value, sos_key_t key)
{
	uint32_t kv = *(uint32_t *)value;
	kv = htobe32(kv);
	memcpy(key->key, (unsigned char *)&kv, 4);
	key->keylen = 4;
}

SOS_OBJ_BEGIN(ovis_metric_class, "OvisMetric")
	SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR("tv_usec", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_KEY("comp_id", SOS_TYPE_UINT32),
	SOS_OBJ_ATTR_WITH_UKEY("value", SOS_TYPE_UINT64, get_value_key, set_key_value)
SOS_OBJ_END(4);

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
	printf("%11s %16s %8s %12s\n", "Key", "Time", "CompID", "Value");
	printf("----------- ---------------- -------- ------------\n");
}

void dump_metric_store_fwd(struct metric_store_s *m,
			   int attr_id)
{
	sos_obj_t obj;
	sos_iter_t iter = sos_iter_new(m->sos, attr_id);
	print_header(m, sos_iter_name(iter));
	for (obj = sos_iter_next(iter);
	     obj; obj = sos_iter_next(iter)) {
		print_obj(m->sos, obj, attr_id);
	}
	sos_iter_free(iter);
}

void dump_metric_store_bkwd(struct metric_store_s *m,
			    int attr_id)
{
	sos_obj_t obj;
	sos_iter_t iter = sos_iter_new(m->sos, attr_id);
	sos_iter_seek_end(iter);
	print_header(m, sos_iter_name(iter));
	for (obj = sos_iter_prev(iter);
	     obj; obj = sos_iter_prev(iter)) {
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

		sscanf(buf, "%[^,],%[^,],%d,%ld,%d,%d",
		       comp_type, metric_name,
		       &comp_id,
		       &value,
		       &tv_usec,
		       &tv_sec);

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
			m->sos = sos_open(tmp_path, O_CREAT | O_RDWR, 0660,
					  &ovis_metric_class);
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

		sos_obj_attr_set(m->sos, 0, obj, &tv_sec);
		sos_obj_attr_set(m->sos, 1, obj, &tv_usec);
		sos_obj_attr_set(m->sos, 2, obj, &comp_id);
		sos_obj_attr_set(m->sos, 3, obj, &value);

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
	return 0;
 err:
	return 1;
}
#endif
