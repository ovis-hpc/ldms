/*
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
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
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
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
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <pthread.h>
#include <dlfcn.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <coll/rbt.h>
#include "obj_idx.h"
#include "obj_idx_priv.h"

static pthread_mutex_t obj_idx_lock = PTHREAD_MUTEX_INITIALIZER;

struct obj_idx_type {
	struct obj_idx_provider *provider;
	struct obj_idx_key_provider *key;
	struct rbn rb_node;
};

int dylib_comparator(void *a, void *b)
{
	return strcmp(a, b);
}

static struct rbt dylib_tree = { 0, dylib_comparator };

void *load_library(const char *library, const char *pfx, const char *sym)
{
	char *errstr;
	static char libpath[PATH_MAX];
	void *p = NULL;

	sprintf(libpath, "lib%s_%s.so", pfx, library);
	void *d = dlopen(libpath, RTLD_NOW);
	if (!d) {
		/* The library doesn't exist */
		printf("dlopen: %s\n", dlerror());
		goto err;
	}
	dlerror();
	void *(*get)() = dlsym(d, sym);
	errstr = dlerror();
	if (errstr || !get) {
		printf("dlsym: %s\n", errstr);
		goto err;
	}

	p = get();
 err:
	return p;
}

struct obj_idx_class *get_idx_class(const char *type, const char *key)
{
	struct rbn *rbn;
	struct obj_idx_class *idx_class = NULL;
	struct obj_idx_provider *prv;
	struct obj_idx_comparator *cmp;
	char *idx_classname;

	idx_classname = malloc(strlen(type) + strlen(key) + 1);
	if (!idx_classname)
		return NULL;
	sprintf(idx_classname, "%s:%s", type, key);

	pthread_mutex_lock(&obj_idx_lock);
	rbn = rbt_find(&dylib_tree, (void *)idx_classname);
	if (rbn) {
		idx_class = container_of(rbn, struct obj_idx_class, rb_node);
		goto out;
	}

	/* Attempt to load the provider */
	prv = (struct obj_idx_provider *)
		load_library(type, "idx", "get");
	if (!prv) {
		errno = ENOENT;
		goto out;
	}

	/* Load the comparator */
	cmp = (struct obj_idx_comparator *)
		load_library(key, "key", "get");
	if (!cmp) {
		errno = ENOENT;
		goto out;
	}

	/* Create provider type and add it to the tree */
	idx_class = calloc(1, sizeof *idx_class);
	if (!idx_class) {
		errno = ENOMEM;
		goto out;
	}

	idx_class->prv = prv;
	idx_class->cmp = cmp;
	rbn_init(&idx_class->rb_node, strdup(idx_classname));
	rbt_ins(&dylib_tree, &idx_class->rb_node);
	free(idx_classname);

 out:
	pthread_mutex_unlock(&obj_idx_lock);
	return idx_class;
}

int obj_idx_create_sz(const char *path, int mode,
		   const char *type, const char *key,
		   size_t size,
		   ...)
{
	va_list argp;
	struct obj_idx_class *idx_class;
	struct obj_idx_meta_data *udata;
	size_t udata_sz;
	ods_t ods;

	va_start(argp, key);

	/* Get the class that handles this index type/key combination */
	idx_class = get_idx_class(type, key);
	if (!idx_class)
		return ENOENT;

	/* Create/truncate a new ODS store */
	ods = ods_open_sz(path, O_RDWR | O_CREAT | O_TRUNC, mode, size);
	if (!ods) {
		errno = ENOENT;
		goto out;
	}

	/* Set up the IDX meta data in the ODS store. */
	udata = ods_get_user_data(ods, &udata_sz);
	memset(udata, 0, udata_sz);
	strcpy(udata->signature, OBJ_IDX_SIGNATURE);
	strcpy(udata->type_name, type);
	strcpy(udata->key_name, key);
	errno = idx_class->prv->init(ods, argp);
	ods_close(ods, ODS_COMMIT_ASYNC);
 out:
	return errno;
}

int obj_idx_create(const char *path, int mode,
		   const char *type, const char *key,
		   ...)
{
	va_list argp;
	struct obj_idx_class *idx_class;
	struct obj_idx_meta_data *udata;
	size_t udata_sz;
	ods_t ods;

	va_start(argp, key);

	/* Get the class that handles this index type/key combination */
	idx_class = get_idx_class(type, key);
	if (!idx_class)
		return ENOENT;

	/* Create/truncate a new ODS store */
	ods = ods_open(path, O_RDWR | O_CREAT | O_TRUNC, mode);
	if (!ods) {
		errno = ENOENT;
		goto out;
	}

	/* Set up the IDX meta data in the ODS store. */
	udata = ods_get_user_data(ods, &udata_sz);
	memset(udata, 0, udata_sz);
	strcpy(udata->signature, OBJ_IDX_SIGNATURE);
	strcpy(udata->type_name, type);
	strcpy(udata->key_name, key);
	errno = idx_class->prv->init(ods, argp);
	ods_close(ods, ODS_COMMIT_ASYNC);
 out:
	return errno;
}

obj_idx_t obj_idx_open(const char *path)
{
	obj_idx_t idx;
	struct obj_idx_class *idx_class;
	struct obj_idx_meta_data *udata;
	size_t udata_sz;

	idx = calloc(1, sizeof *idx);
	if (!idx)
		return NULL;

	idx->ods = ods_open(path, O_RDWR);
	if (!idx->ods)
		goto err_0;

	udata = ods_get_user_data(idx->ods, &udata_sz);
	if (strcmp(udata->signature, OBJ_IDX_SIGNATURE)) {
		/* This file doesn't point to an index */
		errno = EBADF;
		goto err_1;
	}

	idx_class = get_idx_class(udata->type_name, udata->key_name);
	if (!idx_class) {
		/* The libraries necessary to handle this index
		   type/key combinationare not present */
		errno = ENOENT;
		goto err_1;
	}
	idx->idx_class = idx_class;
	if (idx_class->prv->open(idx))
		goto err_1;
	return idx;
 err_1:
	ods_close(idx->ods, ODS_COMMIT_ASYNC);
 err_0:
	free(idx);
	return NULL;

}

void obj_idx_close(obj_idx_t idx, int flags)
{
	if (!idx)
		return;
	idx->idx_class->prv->close(idx);
	ods_close(idx->ods, flags);
}

void obj_idx_commit(obj_idx_t idx, int flags)
{
	if (!idx)
		return;
	ods_commit(idx->ods, flags);
}

int obj_idx_insert(obj_idx_t idx, obj_key_t key, obj_ref_t obj)
{
	return idx->idx_class->prv->insert(idx, key, obj);
}

obj_ref_t obj_idx_delete(obj_idx_t idx, obj_key_t key)
{
	return idx->idx_class->prv->delete(idx, key);
}

obj_ref_t obj_idx_find(obj_idx_t idx, obj_key_t key)
{
	return idx->idx_class->prv->find(idx, key);
}

obj_ref_t obj_idx_find_lub(obj_idx_t idx, obj_key_t key)
{
	return idx->idx_class->prv->find_lub(idx, key);
}

obj_ref_t obj_idx_find_glb(obj_idx_t idx, obj_key_t key)
{
	return idx->idx_class->prv->find_glb(idx, key);
}

obj_iter_t obj_iter_new(obj_idx_t idx)
{
	return idx->idx_class->prv->iter_new(idx);
}

void obj_iter_delete(obj_iter_t iter)
{
	return iter->idx->idx_class->prv->iter_delete(iter);
}

obj_ref_t obj_iter_key_del(obj_iter_t iter)
{
	return iter->idx->idx_class->prv->iter_key_del(iter);
}

int obj_iter_find(obj_iter_t iter, obj_key_t key)
{
	return iter->idx->idx_class->prv->iter_find(iter, key);
}

int obj_iter_find_lub(obj_iter_t iter, obj_key_t key)
{
	return iter->idx->idx_class->prv->iter_find_lub(iter, key);
}

int obj_iter_find_glb(obj_iter_t iter, obj_key_t key)
{
	return iter->idx->idx_class->prv->iter_find_glb(iter, key);
}

int obj_iter_begin(obj_iter_t iter)
{
	return iter->idx->idx_class->prv->iter_begin(iter);
}

int obj_iter_end(obj_iter_t iter)
{
	return iter->idx->idx_class->prv->iter_end(iter);
}

int obj_iter_next(obj_iter_t iter)
{
	return iter->idx->idx_class->prv->iter_next(iter);
}

int obj_iter_prev(obj_iter_t iter)
{
	return iter->idx->idx_class->prv->iter_prev(iter);
}

obj_key_t obj_iter_key(obj_iter_t iter)
{
	return iter->idx->idx_class->prv->iter_key(iter);
}

obj_ref_t obj_iter_ref(obj_iter_t iter)
{
	return iter->idx->idx_class->prv->iter_ref(iter);
}

void *obj_iter_obj(obj_iter_t iter)
{
	return ods_obj_ref_to_ptr(iter->idx->ods,
				  iter->idx->idx_class->prv->iter_ref(iter));
}

obj_key_t obj_key_new(size_t sz)
{
	return calloc(sz + sizeof(struct obj_key), sizeof(unsigned char));
}

void obj_key_delete(obj_key_t key)
{
	free(key);
}

void obj_key_set(obj_key_t key, void *value, size_t sz)
{
	memcpy(&key->value[0], value, sz);
	key->len = sz;
}

const char *obj_key_to_str(obj_idx_t idx, obj_key_t key)
{
	return idx->idx_class->cmp->to_str(key);
}

int obj_key_from_str(obj_idx_t idx, obj_key_t key, const char *str)
{
	return idx->idx_class->cmp->from_str(key, str);
}

int obj_key_cmp(obj_idx_t idx, obj_key_t a, obj_key_t b)
{
	return idx->idx_class->cmp->compare_fn(a, b);
}

void *obj_idx_alloc(obj_idx_t idx, size_t sz)
{
	void *p = ods_alloc(idx->ods, sz);
	if (!p) {
		if (!ods_extend(idx->ods, OBJ_IDX_EXTEND_SZ)) {
			p = ods_alloc(idx->ods, sz);
		}
	}
	return p;
}

