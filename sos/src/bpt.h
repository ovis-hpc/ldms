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

#ifndef _BPT_H_
#define _BPT_H_

#include "obj_idx.h"
#include "obj_idx_priv.h"
#include "ods.h"

#pragma pack(4)

/**
 * B+ Tree Node Entry
 *
 * Describes a key and the object to which it refers. The key is an
 * obj_ref_t which refers to an arbitrarily sized ODS object that
 * contains an opaque key. The key comparitor is used to compare two
 * keys.
 */
typedef struct bpn_entry {
	/* Refers to the key object */
	obj_ref_t key;

	/* The node or record to which the key refers */
	obj_ref_t ref;
} *bpn_entry_t;

/*
 * B+ Tree Node
 */
typedef struct bpt_node {
	obj_ref_t parent;	/* NULL if root */
	uint32_t count;
	uint32_t is_leaf;
	struct bpn_entry entries[];
} *bpt_node_t;

typedef struct bpt_udata {
	struct obj_idx_meta_data idx_udata;
	uint32_t order;		/* The order or each internal node */
	obj_ref_t root;		/* The root of the tree */
} *bpt_udata_t;

/*
 * In memory object that refers to a B+ Tree
 */
typedef struct bpt_s {
	size_t order;		/* order of the tree */
	ods_t ods;		/* The ods that contains the tree */
	bpt_node_t root;	/* The root of the tree */
	obj_idx_compare_fn_t comparator;
} *bpt_t;

typedef struct bpt_iter {
	obj_idx_t idx;
	int ent;
	bpt_node_t node;
} *bpt_iter_t;

#define BPT_SIGNATURE "BTREE0100"
#pragma pack()

#endif
