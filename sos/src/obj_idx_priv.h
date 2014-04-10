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

/*
 * Author: Tom Tucker tom at ogc dot us
 */

#ifndef _OBJ_IDX_PRIV_H_
#define _OBJ_IDX_PRIV_H_
#include <coll/rbt.h>
#include "ods.h"

struct obj_idx_provider {
	const char *(*get_type)(void);
	int (*init)(ods_t ods, va_list argp);
	int (*open)(obj_idx_t idx);
	void (*close)(obj_idx_t idx);
	void (*commit)(obj_idx_t idx);
	int (*insert)(obj_idx_t idx, obj_key_t uk, obj_ref_t obj);
	obj_ref_t (*delete)(obj_idx_t idx, obj_key_t key);
	obj_ref_t (*find)(obj_idx_t idx, obj_key_t key);
	obj_ref_t (*find_lub)(obj_idx_t idx, obj_key_t key);
	obj_ref_t (*find_glb)(obj_idx_t idx, obj_key_t key);
	obj_iter_t (*iter_new)(obj_idx_t idx);
	void (*iter_delete)(obj_iter_t i);
	int (*iter_find)(obj_iter_t iter, obj_key_t key);
	int (*iter_find_lub)(obj_iter_t iter, obj_key_t key);
	int (*iter_find_glb)(obj_iter_t iter, obj_key_t key);
	int (*iter_begin)(obj_iter_t iter);
	int (*iter_end)(obj_iter_t iter);
	int (*iter_next)(obj_iter_t iter);
	int (*iter_prev)(obj_iter_t iter);
	obj_key_t (*iter_key)(obj_iter_t iter);
	obj_ref_t (*iter_ref)(obj_iter_t iter);
};

struct obj_idx_comparator {
	/** Return the name of the comparator */
	const char *(*get_type)(void);
	/** Return a description of how the key works  */
	const char *(*get_doc)(void);
	/** Return a string representation of the key value */
	const char *(*to_str)(obj_key_t);
	/** Set the key value from a string */
	int (*from_str)(obj_key_t, const char *);
	/** Compare two keys */
	obj_idx_compare_fn_t compare_fn;
};

#pragma pack(1)
#define OBJ_NAME_MAX	31
struct obj_idx_meta_data {
	char signature[OBJ_NAME_MAX+1];
	char type_name[OBJ_NAME_MAX+1];
	char key_name[OBJ_NAME_MAX+1];
};
#pragma pack()

struct obj_idx_class {
	struct obj_idx_provider *prv;
	struct obj_idx_comparator *cmp;
	struct rbn rb_node;
};

struct obj_idx {
	/** The index and key handler functions */
	struct obj_idx_class *idx_class;
	/** The ODS object store for the index */
	ods_t ods;
	/** The ODS meta data */
	struct obj_idx_meta_data *udata;
	size_t udata_sz;
	/** Place for the index to store its private data */
	void *priv;
};

struct obj_iter {
	struct obj_idx *idx;
};

void *obj_idx_alloc(obj_idx_t idx, size_t sz);

#endif
