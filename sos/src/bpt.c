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
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <string.h>
#include <assert.h>
#include "ods.h"
#include "bpt.h"

static int search_leaf_reverse(bpt_t t, bpt_node_t leaf, obj_key_t key);
static int search_leaf(bpt_t t, bpt_node_t leaf, obj_key_t key);
static bpt_node_t leaf_left(bpt_t t, uint64_t node_ref);
static bpt_node_t leaf_right(bpt_t t, uint64_t node_ref);
static int node_neigh(bpt_t t, bpt_node_t node, bpt_node_t *left, bpt_node_t *right);
static bpt_node_t fixup_parents(bpt_t t, bpt_node_t parent, bpt_node_t node);

obj_ref_t bad_ref;

static void print_node(obj_idx_t idx, int ent, bpt_node_t n, int indent)
{
	bpt_t t = idx->priv;
	int i;

	if (!n) {
		printf("<nil>\n");
		return;
	}

	/* Print this node */
	if (n->is_leaf && n->parent)
		indent += 4;
	printf("%p - %*s%s[%d] | %p : ", (void *)n->parent, indent, "",
	       (n->is_leaf?"LEAF":"NODE"),
	       ent, n);
	for (i = 0; i < t->order; i++) {
		obj_key_t key = ods_obj_ref_to_ptr(t->ods, n->entries[i].key);
		printf("%s:%p, ",
		       (key ? obj_key_to_str(idx, key) : "-"),
		       (void *)(unsigned long)n->entries[i].ref);
	}
	printf("\n");
	fflush(stdout);
	if (n->is_leaf)
		return;
	/* Now print all it's children */
	for (i = 0; i < n->count; i++) {
		bpt_node_t node = ods_obj_ref_to_ptr(t->ods, n->entries[i].ref);
		print_node(idx, i, node, indent + 2);
	}
}

static void print_tree(obj_idx_t idx)
{
	bpt_t t = idx->priv;
	bpt_node_t node = ods_obj_ref_to_ptr(idx->ods, t->root_ref);
	print_node(idx, 0, node, 0);
}

static struct bpt_udata *__get_udata(ods_t ods)
{
	size_t udata_sz;
	return ods_get_user_data(ods, &udata_sz);
}

static int bpt_open(obj_idx_t idx)
{
	struct bpt_udata *udata = __get_udata(idx->ods);
	bpt_t t = malloc(sizeof *t);
	if (!t)
		return ENOMEM;
	t->order = udata->order;
	t->root_ref = udata->root;
	t->ods = idx->ods;
	t->comparator = idx->idx_class->cmp->compare_fn;
	idx->priv = t;
	return 0;
}

static int bpt_init(ods_t ods, va_list argp)
{
	struct bpt_udata *udata = __get_udata(ods);
	int order = va_arg(argp, int);
	if (order <= 0) {
		/*
		 * Each entry is 16B + 8B for the parent + 8B for the count.
		 * If each node is a page, 4096 / 16B = 256
		 */
		order = 251;
	}
	udata->order = order;
	udata->root = 0;
	return 0;
}

static void bpt_close(obj_idx_t idx)
{
	struct bpt_udata *udata = __get_udata(idx->ods);
	bpt_t t = idx->priv;
	udata->root = t->root_ref;
}

obj_ref_t leaf_find_ref(bpt_t t, obj_key_t key)
{
	obj_ref_t ref = t->root_ref;
	bpt_node_t n = ods_obj_ref_to_ptr(t->ods, ref);
	obj_key_t entry_key;
	int i;

	if (!n)
		return 0;

	while (!n->is_leaf) {
		int rc;
		for (i = 1; i < n->count; i++) {
			entry_key = ods_obj_ref_to_ptr(t->ods, n->entries[i].key);
			n = ods_obj_ref_to_ptr(t->ods, ref);
			rc = t->comparator(key, entry_key);
			if (rc >= 0)
				continue;
			else
				break;
		}
		ref = n->entries[i-1].ref;
		n = ods_obj_ref_to_ptr(t->ods, ref);
	}
	return ref;
}

static obj_ref_t bpt_find(obj_idx_t idx, obj_key_t key)
{
	int i;
	bpt_t t = idx->priv;
	obj_ref_t leaf_ref = leaf_find_ref(t, key);
	bpt_node_t leaf = ods_obj_ref_to_ptr(t->ods, leaf_ref);
	if (!leaf)
		return 0;
	for (i = 0; i < leaf->count; i++) {
		obj_key_t entry_key = ods_obj_ref_to_ptr(t->ods, leaf->entries[i].key);
		leaf = ods_obj_ref_to_ptr(t->ods, leaf_ref);
		if (!t->comparator(key, entry_key))
			return leaf->entries[i].ref;
	}
	return 0;
}

static void find_first_dup(bpt_t t, obj_key_t key, obj_ref_t *leaf_ref, int *ent)
{
	int j;
	bpt_node_t left;
	while (NULL != (left = leaf_left(t, *leaf_ref))) {
		/* Search the left sibling to see if there is a match */
		j = search_leaf_reverse(t, left, key);
		if (j < 0)
			break;
		*leaf_ref = ods_obj_ptr_to_ref(t->ods, left);
		*ent = j;
		if (j)
			break;
	}
}

static void find_last_dup(bpt_t t, obj_key_t key, obj_ref_t *leaf_ref, int *ent)
{
	int j;
	bpt_node_t right = ods_obj_ref_to_ptr(t->ods, *leaf_ref);
	/* Exhaust the current node */
	for (j = *ent; j < right->count; j++) {
		obj_key_t entry_key = ods_obj_ref_to_ptr(t->ods, right->entries[j].key);
		int rc = t->comparator(key, entry_key);
		if (rc)
			break;
		*ent = j;
	}
	if (*ent < right->count - 1)
		return;
	while (NULL != (right = leaf_right(t, *leaf_ref))) {
		/* Search the right sibling to see if there is a match */
		j = search_leaf(t, right, key);
		if (j < 0)
			break;
		*leaf_ref = ods_obj_ptr_to_ref(t->ods, right);
		*ent = j;
		if (j < right->count - 1)
			break;
	}
}

static int __find_lub(obj_idx_t idx, obj_key_t key,
			obj_ref_t *node_ref, int *ent)
{
	int i;
	bpt_t t = idx->priv;
	obj_ref_t leaf_ref = leaf_find_ref(t, key);
	bpt_node_t leaf = ods_obj_ref_to_ptr(t->ods, leaf_ref);
	if (!leaf)
		return 0;
	for (i = 0; i < leaf->count; i++) {
		obj_key_t entry_key = ods_obj_ref_to_ptr(t->ods, leaf->entries[i].key);
		int rc = t->comparator(key, entry_key);
		if (rc > 0) {
			continue;
		} else if (rc == 0 || i < leaf->count) {
			*ent = i;
			*node_ref = leaf_ref;
			if (rc == 0)
				find_last_dup(t, key, node_ref, ent);
			return 1;
		} else {
			break;
		}
	}
	/* LUB is in our right sibling */
	leaf_ref = leaf->entries[t->order - 1].ref;
	leaf = ods_obj_ref_to_ptr(t->ods, leaf_ref);
	if (!leaf)
		return 0;
	*node_ref = leaf_ref;
	*ent = 0;
	return 1;
}

static obj_ref_t bpt_find_lub(obj_idx_t idx, obj_key_t key)
{
	int ent;
	obj_ref_t leaf_ref;
	bpt_node_t leaf;
	if (__find_lub(idx, key, &leaf_ref, &ent)) {
		leaf = ods_obj_ref_to_ptr(idx->ods, leaf_ref);
		return leaf->entries[ent].ref;
	}
	return 0;
}

static int __find_glb(obj_idx_t idx, obj_key_t key, obj_ref_t *node_ref, int *ent)
{
	int i, rc;
	obj_key_t entry_key;
	bpt_t t = idx->priv;
	obj_ref_t leaf_ref = leaf_find_ref(t, key);
	bpt_node_t leaf = ods_obj_ref_to_ptr(t->ods, leaf_ref);
	if (!leaf)
		return 0;
	*node_ref = leaf_ref;
	for (i = 0; i < leaf->count; i++) {
		entry_key = ods_obj_ref_to_ptr(t->ods, leaf->entries[i].key);
		leaf = ods_obj_ref_to_ptr(t->ods, leaf_ref);
		rc = t->comparator(key, entry_key);
		if (rc > 0)
			continue;
		if (rc == 0) {
			*ent = i;
			find_first_dup(t, key, node_ref, ent);
		} else
			*ent = i - 1;
		goto found;
	}
	*ent = leaf->count - 1;
 found:
	return 1;
}

static obj_ref_t bpt_find_glb(obj_idx_t idx, obj_key_t key)
{
	int ent;
	obj_ref_t leaf_ref;
	bpt_node_t leaf;
	if (__find_glb(idx, key, &leaf_ref, &ent)) {
		leaf = ods_obj_ref_to_ptr(idx->ods, leaf_ref);
		return leaf->entries[ent].ref;
	}
	return 0;
}

static obj_key_t key_new(obj_idx_t idx, bpt_t t, size_t sz)
{
	obj_key_t key = obj_idx_alloc(idx, sz);
	return key;
}

static bpt_node_t node_new(obj_idx_t idx, bpt_t t)
{
	size_t sz;
	bpt_node_t n;

	sz = sizeof *n + (t->order * sizeof(struct bpn_entry));
	n = obj_idx_alloc(idx, sz);
	if (n)
		memset(n, 0, sz);
	return n;
}

static struct bpn_entry ENTRY_INITIALIZER = { 0, 0 };

int leaf_insert(bpt_t t, bpt_node_t leaf, obj_key_t key, obj_ref_t obj)
{
	int i, j;

	assert(leaf->is_leaf);

	/* Insert the object */
	for (i = 0; i < leaf->count; i++) {
		obj_key_t ek = ods_obj_ref_to_ptr(t->ods, leaf->entries[i].key);
		if (t->comparator(key, ek) < 0)
			break;
	}
	/* Move up all the entries to make space */
	for (j = leaf->count; j > i; j--)
		leaf->entries[j] = leaf->entries[j-1];

	/* Put in the new entry and update the count */
	leaf->entries[i].key = ods_obj_ptr_to_ref(t->ods, key);
	leaf->entries[i].ref = obj;
	leaf->count++;
	return i;
}

static int find_idx(bpt_t t, bpt_node_t leaf, obj_key_t key)
{
	int i;
	for (i = 0; i < leaf->count; i++) {
		obj_key_t entry_key = ods_obj_ref_to_ptr(t->ods, leaf->entries[i].key);
		if (entry_key && t->comparator(key, entry_key) < 0)
			break;
	}
	return i;
}

static int split_midpoint(int order)
{
	if (order & 1)
		return (order >> 1) + 1;
	return order >> 1;
}

static bpt_node_t leaf_split_insert(obj_idx_t idx, bpt_t t, obj_ref_t left_ref,
				    obj_ref_t new_key_ref, obj_ref_t obj)
{
	bpt_node_t left, right;
	obj_key_t new_key;
	int i, j;
	int ins_idx;
	int ins_left_n_right;
	int midpoint = split_midpoint(t->order);

	right = node_new(idx, t);
	if (!right)
		return NULL;
	left = ods_obj_ref_to_ptr(t->ods, left_ref);
	assert(left->is_leaf);
	new_key = ods_obj_ref_to_ptr(t->ods, new_key_ref);
	right->is_leaf = 1;
	right->parent = left->parent;

	ins_idx = find_idx(t, left, new_key);
	ins_left_n_right = ins_idx < midpoint;
	if (ins_left_n_right) {
		/*
		 * New entry goes in the left node. This means that
		 * the boundary marking which entries moves from left
		 * to right needs to be shifted left one because the
		 * insertion will eventually shift these entries right.
		 */
		for (i = midpoint - 1, j = 0; i < t->order - 1; i++, j++) {
			right->entries[j] = left->entries[i];
			left->count--;
			right->count++;
		}
		/*
		 * Move the objects between the insertion point and
		 * the end one slot to the right.
		 */
		for (i = midpoint - 1; i > ins_idx; i--)
			left->entries[i] = left->entries[i-1];

		/*
		 * Put the new item in the entry list
		 */
		left->entries[ins_idx].ref = obj;
		left->entries[ins_idx].key = new_key_ref;
		left->count++;
		if (!ins_idx)
			fixup_parents(t, ods_obj_ref_to_ptr(t->ods, left->parent), left);
	} else {
		/*
		 * New entry goes in the right node. This means that
		 * as we move the entries from left to right, we need
		 * to leave space for the item that will be added.
		 */
		ins_idx = ins_idx - midpoint;
		for (i = midpoint, j = 0; i < t->order - 1; i++, j++) {
			/*
			 * If this is where the new entry will
			 * go, skip a slot
			 */
			if (ins_idx == j)
				j ++;
			right->entries[j] = left->entries[i];
			left->entries[i] = ENTRY_INITIALIZER;
			left->count--;
			right->count++;
		}
		/*
		 * Put the new item in the entry list
		 */
		right->entries[ins_idx].ref = obj;
		right->entries[ins_idx].key = new_key_ref;
		right->count++;
	}
	/* Link left --> right */
	right->entries[t->order-1].ref =
		left->entries[t->order-1].ref; /* right->next = left->next */
	left->entries[t->order-1].ref =
		ods_obj_ptr_to_ref(t->ods, right); /* left->next = right */

	return right;
}

static int verify_node(obj_idx_t idx, bpt_node_t n)
{
	bpt_t t = idx->priv;
	int i, rc;
	int midpoint = split_midpoint(t->order);

	if (!n)
		return 0;

	/* Make sure each entry is lexically >= previous */
	for (i = 0; i < n->count-1; i++) {
		obj_key_t e1 = ods_obj_ref_to_ptr(t->ods, n->entries[i].key);
		obj_key_t e2 = ods_obj_ref_to_ptr(t->ods, n->entries[i+1].key);
		if (!(t->comparator(e2, e1) >= 0)) {
			return -1;
		}
	}
	if (t->root_ref != ods_obj_ptr_to_ref(t->ods, n)) {
		assert(n->parent);
		if (n->is_leaf)
			midpoint--;
		/* Make sure it has at least midpoint entries */
		if (!n->count >= midpoint) {
			return -1;
		}
	}

	if (n->is_leaf)
		return 0;

	/* Make certain all n's keys refer to the min. of each child entry */
	for (i = 0; i < n->count; i++) {
		obj_key_t parent_key = ods_obj_ref_to_ptr(t->ods, n->entries[i].key);
		bpt_node_t child = ods_obj_ref_to_ptr(t->ods, n->entries[i].ref);
		obj_key_t child_key = ods_obj_ref_to_ptr(t->ods, child->entries[0].key);
		if (parent_key != child_key) {
			return -1;
		}
		if (n->entries[i].key == bad_ref) {
			printf("BAD REF %p\n", (void*)bad_ref);
			return -1;
		}
	}
	/* Now verify each entry */
	for (i = 0; i < n->count; i++) {
		bpt_node_t child = ods_obj_ref_to_ptr(t->ods, n->entries[i].ref);
		rc = verify_node(idx, child);
		if (rc)
			return rc;
	}

	return 0;
}

int verify_leafs(obj_idx_t idx);

int bpt_verify_verbose(obj_idx_t idx, int verbose)
{
	int rc;
	bpt_t t;
	bpt_node_t root;

	t = idx->priv;
	if (!t->root_ref)
		return 0;
	root = ods_obj_ref_to_ptr(idx->ods, t->root_ref);
	if (root->parent != 0) {
		if (verbose) {
			printf("root->parent is not NULL\n");
			fflush(stdout);
		}
		return -1;
	}
	rc = verify_leafs(idx);
	if (rc)
		return rc;

	rc = verify_node(idx, ods_obj_ref_to_ptr(t->ods, t->root_ref));
	if (rc) {
		if (verbose)
			print_tree(idx);
		return -1;
	}
	return 0;
}

static int bpt_verify(obj_idx_t idx)
{
	return bpt_verify_verbose(idx, 0);
}

#ifdef DEBUG_BPT_VERIFY
#define verify_tree(X) bpt_verify_verbose(X, 1)
#else
#define verify_tree(X)
#endif

static void node_insert(bpt_t t, bpt_node_t node, bpt_node_t left,
			obj_ref_t key_ref, bpt_node_t right)
{
	int i, j;
	obj_ref_t left_ref = ods_obj_ptr_to_ref(t->ods, left);

	assert(!node->is_leaf);
	/* Find left's index */
	for (i = 0; i < node->count; i++) {
		if (left_ref == node->entries[i].ref)
			break;
	}
	assert(i < node->count);

	/*
	 * Make room for right after left's current key/ref and the
	 * end of the node
	 */
	for (j = node->count; j > i+1; j--)
		node->entries[j] = node->entries[j-1];

	/* Put in the new entry and update the count */
	node->entries[i+1].key = key_ref;
	node->entries[i+1].ref = ods_obj_ptr_to_ref(t->ods, right);
	node->count++;
	left->parent = right->parent = ods_obj_ptr_to_ref(t->ods, node);
}

static bpt_node_t node_split_insert(obj_idx_t idx, bpt_t t,
				    obj_ref_t left_node_ref,
				    obj_ref_t right_key_ref,
				    obj_ref_t right_node_ref)
{
	bpt_node_t left_node, right_node, left_parent, right_parent;
	int i, j;
	int ins_idx, ins_left_n_right;
	obj_ref_t right_parent_ref, left_parent_ref;
	int count;
	int midpoint = split_midpoint(t->order);

 split_and_insert:
	right_parent = node_new(idx, t);
	if (!right_parent)
		goto err_0;

	/* And reconstruct after allocation */
	left_node = ods_obj_ref_to_ptr(t->ods, left_node_ref);
	left_parent = ods_obj_ref_to_ptr(t->ods, left_node->parent);
	right_node = ods_obj_ref_to_ptr(t->ods, right_node_ref);

	/*
	 * Find left_node in the parent
	 */
	for (i = 0; i < left_parent->count; i++) {
		if (left_node_ref == left_parent->entries[i].ref)
			break;
	}
	/* Right is the succesor of left */
	ins_idx = i + 1;

	assert(i < left_parent->count);
	right_parent_ref = ods_obj_ptr_to_ref(t->ods, right_parent);
	left_parent_ref = ods_obj_ptr_to_ref(t->ods, left_parent);

	ins_left_n_right = ins_idx < midpoint;
	if (ins_left_n_right) {
		/*
		 * New entry goes in the left parent. This means that
		 * the boundary marking which entries shift to the
		 * right needs to be shifted down one because the
		 * insertion will eventually shift these entries up.
		 */
		count = left_parent->count - midpoint + 1;
		for (i = midpoint - 1, j = 0; j < count; i++, j++) {
			bpt_node_t n =
				ods_obj_ref_to_ptr(t->ods, left_parent->entries[i].ref);
			n->parent = right_parent_ref;
			right_parent->entries[j] = left_parent->entries[i];
			left_parent->entries[i] = ENTRY_INITIALIZER;
		}
		right_parent->count += count;
		left_parent->count -= (count - 1); /* account for the insert below */
		/*
		 * Move the objects between the insertion point and
		 * the end one slot to the right.
		 */
		for (i = midpoint - 1; i > ins_idx; i--)
			left_parent->entries[i] = left_parent->entries[i-1];

		/*
		 * Put the new item in the entry list. Right is the
		 * successor of left, therefore it's insertion index
		 * cannot be zero.
		 */
		assert(ins_idx);
		left_parent->entries[ins_idx].ref = right_node_ref;
		left_parent->entries[ins_idx].key = right_key_ref;
		right_node->parent = left_parent_ref;
	} else {
		/*
		 * New entry goes in the right node. This means that
		 * as we move the entries from left to right, we need
		 * to leave space for the item that will be added.
		 */
		count = left_parent->count;
		ins_idx = ins_idx - midpoint;
		for (i = midpoint, j = 0; i < count; i++, j++) {
			bpt_node_t n =
				ods_obj_ref_to_ptr(t->ods, left_parent->entries[i].ref);
			/*
			 * If this is where the new entry will
			 * go, skip a slot
			 */
			if (ins_idx == j)
				j ++;
			n->parent = right_parent_ref;
			right_parent->entries[j] = left_parent->entries[i];
			left_parent->entries[i] = ENTRY_INITIALIZER;
			right_parent->count++;
			left_parent->count--;
		}
		/*
		 * Put the new item in the entry list
		 */
		right_parent->entries[ins_idx].ref = right_node_ref;
		right_parent->entries[ins_idx].key = right_key_ref;
		right_parent->count++;
		right_node->parent = right_parent_ref;
	}
	assert(right_parent->count > 1);
	assert(left_parent->count > 1);

	/*
	 * Now insert our new right parent at the next level
	 * up the tree.
	 */
	bpt_node_t next_parent = ods_obj_ref_to_ptr(t->ods, left_parent->parent);
	if (!next_parent) {
		/* Split root */
		obj_ref_t left_key_ref = left_parent->entries[0].key;
		obj_ref_t right_key_ref = right_parent->entries[0].key;
		left_parent_ref = ods_obj_ptr_to_ref(t->ods, left_parent);
		right_parent_ref = ods_obj_ptr_to_ref(t->ods, right_parent);
		next_parent = node_new(idx, t);
		obj_ref_t next_parent_ref = ods_obj_ptr_to_ref(t->ods, next_parent);
		next_parent->count = 2;
		next_parent->entries[0].ref = left_parent_ref;
		next_parent->entries[0].key = left_key_ref;
		next_parent->entries[1].ref = right_parent_ref;
		next_parent->entries[1].key = right_key_ref;
		left_parent = ods_obj_ref_to_ptr(t->ods, left_parent_ref);
		left_parent->parent = next_parent_ref;
		right_parent = ods_obj_ref_to_ptr(t->ods, right_parent_ref);
		right_parent->parent = next_parent_ref;
		t->root_ref = next_parent_ref;
		goto out;
	}
	/* If there is room, insert into the parent */
	if (next_parent->count < t->order) {
		node_insert(t, next_parent, left_parent,
			    right_parent->entries[0].key, right_parent);
		goto out;
	}
	/* Go up to the next level and split and insert */
	left_node_ref = left_parent_ref;
	right_node_ref = right_parent_ref;
	right_key_ref = right_parent->entries[0].key;
	goto split_and_insert;
 out:
	return next_parent;
 err_0:
	return NULL;
}

static int bpt_insert(obj_idx_t idx, obj_key_t uk, obj_ref_t obj)
{
	bpt_t t = idx->priv;
	obj_key_t new_key;
	bpt_node_t new_leaf;
	bpt_node_t node;
	bpt_node_t leaf;
	bpt_node_t parent;
	bpt_udata_t udata;
	obj_ref_t new_key_ref, leaf_ref, new_leaf_ref;

	new_key = key_new(idx, t, uk->len + sizeof(*uk));
	if (!new_key)
		return ENOMEM;
	memcpy(new_key, uk, uk->len + sizeof(uk->len));
	new_key_ref = ods_obj_ptr_to_ref(t->ods, new_key);

	if (!t->root_ref) {
		node = node_new(idx, t);
		if (!node)
			goto err_1;
		t->root_ref = ods_obj_ptr_to_ref(idx->ods, node);
		udata = __get_udata(idx->ods);
		udata->root = t->root_ref;
		node->is_leaf = 1;
		leaf_ref = t->root_ref;
	} else
		leaf_ref = leaf_find_ref(t, new_key);

	leaf = ods_obj_ref_to_ptr(t->ods, leaf_ref);

	/* Is there room in the leaf? */
	if (leaf->count < t->order - 1) {
		if (0 == leaf_insert(t, leaf, new_key, obj) && leaf->parent) {
			bpt_node_t parent = ods_obj_ref_to_ptr(t->ods, leaf->parent);
			fixup_parents(t, parent, leaf);
		}
		verify_tree(idx);
		return 0;
	}

	new_leaf = leaf_split_insert(idx, t, leaf_ref, new_key_ref, obj);
	if (!new_leaf)
		goto err_1;
	new_leaf_ref = ods_obj_ptr_to_ref(t->ods, new_leaf);
	leaf = ods_obj_ref_to_ptr(t->ods, leaf_ref);
	new_key = ods_obj_ref_to_ptr(t->ods, new_key_ref);

	parent = ods_obj_ref_to_ptr(t->ods, leaf->parent);
	if (!parent) {
		obj_ref_t leaf_key_ref = leaf->entries[0].key;

		parent = node_new(idx, t);
		if (!parent)
			goto err_2;

		obj_ref_t parent_ref = ods_obj_ptr_to_ref(t->ods, parent);

		/* ods pointers can be stale after ods_alloc */
		new_leaf = ods_obj_ref_to_ptr(t->ods, new_leaf_ref);
		leaf = ods_obj_ref_to_ptr(t->ods, leaf_ref);
		new_key = ods_obj_ref_to_ptr(t->ods, new_key_ref);

		parent->entries[0].key = leaf_key_ref;
		parent->entries[0].ref = leaf_ref;

		parent->entries[1].key = new_leaf->entries[0].key;
		parent->entries[1].ref = new_leaf_ref;
		parent->count = 2;

		leaf->parent = parent_ref;
		new_leaf->parent = parent_ref;
		t->root_ref = parent_ref;
		verify_tree(idx);
		goto out;
	}
	if (parent->count < t->order) {
		node_insert(t, parent, leaf, new_leaf->entries[0].key, new_leaf);
		verify_tree(idx);
		goto out;
	}
	parent = node_split_insert(idx, t, leaf_ref,
				   new_leaf->entries[0].key, new_leaf_ref);
	verify_tree(idx);
	if (!parent)
		goto err_3;
 out:
	udata = __get_udata(idx->ods);
	udata->root = t->root_ref;
	verify_tree(idx);
	return 0;

 err_3:
	/* TODO: Unsplit the leaf and put the tree back in order. */
 err_2:
	ods_free(t->ods, new_leaf);
 err_1:
	ods_free(t->ods, new_key);
	return ENOMEM;
}

obj_ref_t bpt_min_ref(bpt_t t)
{
	bpt_node_t n;
	obj_ref_t ref = 0;

	if (!t->root_ref)
		return 0;

	/* Walk to the left most leaf and return the 0-th entry  */
	ref = t->root_ref;
	n = ods_obj_ref_to_ptr(t->ods, t->root_ref);
	while (!n->is_leaf) {
		ref = n->entries[0].ref;
		n = ods_obj_ref_to_ptr(t->ods, ref);
	}
	return ref;
}

static obj_ref_t entry_find(bpt_t t, bpt_node_t node, obj_key_t key, int *idx)
{
	int i;
	obj_ref_t obj = 0;
	for (i = 0; i < node->count; i++) {
		obj_key_t entry_key = ods_obj_ref_to_ptr(t->ods, node->entries[i].key);
		if (!entry_key) {
			/* The last key in an interior node is NUL */
			assert(!node->is_leaf);
			break;
		}
		if (!t->comparator(key, entry_key)) {
			break;
		}
	}
	if (i < node->count) {
		obj = node->entries[i].ref;
		*idx = i;
	}
	return obj;
}

static bpt_node_t leaf_right_most(bpt_t t, bpt_node_t node)
{
	while (!node->is_leaf) {
		node = ods_obj_ref_to_ptr(t->ods,
					node->entries[node->count-1].ref);
	}
	return node;
}

/**
 * Find left sibling of the given \c node in the tree \c t.
 *
 * \param t The tree.
 * \param node The reference leaf node.
 *
 * \returns NULL if the left node is not found.
 * \returns A pointer to the left node, if it is found.
 */
static bpt_node_t leaf_left(bpt_t t, uint64_t node_ref)
{
	int idx;
	bpt_node_t node;
	uint64_t parent_ref;
	node = ods_obj_ref_to_ptr(t->ods, node_ref);
	assert(node->is_leaf);
loop:
	if (t->root_ref == node_ref)
		return NULL;
	assert(node->parent);
	parent_ref = node->parent;
	bpt_node_t parent = ods_obj_ref_to_ptr(t->ods, node->parent);
	for (idx = 0; idx < parent->count; idx++)
		if (parent->entries[idx].ref == node_ref)
			break;
	assert(idx < parent->count);
	if (!idx) {
		node_ref = parent_ref;
		node = parent;
		goto loop;
	}

	node = ods_obj_ref_to_ptr(t->ods, parent->entries[idx-1].ref);
	return leaf_right_most(t, node);
}

/**
 * Find left sibling of the given \c node in the tree \c t.
 *
 * \param t The tree.
 * \param node The reference leaf node.
 *
 * \returns NULL if the left node is not found.
 * \returns A pointer to the left node, if it is found.
 */
static bpt_node_t leaf_right(bpt_t t, uint64_t node_ref)
{
	bpt_node_t node;
	uint64_t right_ref;
	node = ods_obj_ref_to_ptr(t->ods, node_ref);
	assert(node->is_leaf);
	right_ref = node->entries[t->order - 1].ref;
	node = ods_obj_ref_to_ptr(t->ods, right_ref);
	return node;
}

static int node_neigh(bpt_t t, bpt_node_t node, bpt_node_t *left, bpt_node_t *right)
{
	int idx;
	obj_ref_t node_ref;
	bpt_node_t parent;

	node_ref = ods_obj_ptr_to_ref(t->ods, node);
	if (t->root_ref == node_ref) {
		*left = *right = NULL;
		return 0;
	}
	assert(node->parent);
	parent = ods_obj_ref_to_ptr(t->ods, node->parent);
	for (idx = 0; idx < parent->count; idx++)
		if (parent->entries[idx].ref == node_ref)
			break;
	assert(idx < parent->count);
	if (idx)
		*left = ods_obj_ref_to_ptr(t->ods, parent->entries[idx-1].ref);
	else
		*left = NULL;

	if (idx < parent->count-1)
		*right = ods_obj_ref_to_ptr(t->ods, parent->entries[idx+1].ref);
	else
		*right = NULL;
	return idx;
}

static int space(bpt_t t, bpt_node_t n)
{
	if (!n)
		return 0;

	if (n->is_leaf)
		return t->order - n->count - 1;

	return t->order - n->count;
}

static int combine_right(bpt_t t, bpt_node_t right, int idx, bpt_node_t node)
{
	int i, j;
	int count = node->count - idx;
	obj_ref_t right_ref;
	bpt_node_t entry;

	if (!right || !count)
		return idx;

	/* Make room to the left */
	for (i = right->count + count - 1; i >= count; i--)
		right->entries[i] = right->entries[i-count];

	right_ref = ods_obj_ptr_to_ref(t->ods, right);
	for (i = 0, j = idx; j < node->count; i++, j++) {
		/* Update the entry's parent */
		if (!node->is_leaf) {
			entry = ods_obj_ref_to_ptr(t->ods, node->entries[j].ref);
			entry->parent = right_ref;
		}
		/* Move the entry to the right sibling */
		right->entries[i] = node->entries[j];
		right->count++;
		idx++;
	}
	return idx;
}

static int combine_left(bpt_t t, bpt_node_t left, int idx, bpt_node_t node)
{
	int i, j;
	int count = node->count - idx;
	bpt_node_t entry;
	obj_ref_t left_ref;

	if (!left)
		return idx;

	left_ref = ods_obj_ptr_to_ref(t->ods, left);
	for (i = left->count, j = idx; j < count && i < t->order - node->is_leaf; i++, j++) {
		/* Update the entry's parent */
		if (!node->is_leaf) {
			entry = ods_obj_ref_to_ptr(t->ods, node->entries[j].ref);
			entry->parent = left_ref;
		}
		/* Move the entry to the left sibling */
		left->entries[i] = node->entries[j];
		left->count++;
		idx++;
	}
	return idx;
}

static bpt_node_t fixup_parents(bpt_t t, bpt_node_t parent, bpt_node_t node)
{
	int i;
	while (parent) {
		obj_ref_t node_ref = ods_obj_ptr_to_ref(t->ods, node);
		for (i = 0; i < parent->count; i++)
			if (parent->entries[i].ref == node_ref)
				break;
		assert(i < parent->count);
		parent->entries[i].key = node->entries[0].key;
		node = parent;
		parent = ods_obj_ref_to_ptr(t->ods, parent->parent);
	}
	return ods_obj_ref_to_ptr(t->ods, t->root_ref);
}

static int merge_from_left(bpt_t t, bpt_node_t left, bpt_node_t node, int midpoint)
{
	int count;
	int i, j;
	obj_ref_t node_ref = ods_obj_ptr_to_ref(t->ods, node);

	assert(left->count > midpoint);
	count = left->count - midpoint;

	/* Make room in node */
	for (i = node->count + count - 1, j = 0; j < node->count; j++, i--)
		node->entries[i] = node->entries[i-count];

	for (i = 0, j = left->count - count; i < count; i++, j++) {
		if (!node->is_leaf) {
			bpt_node_t entry =
				ods_obj_ref_to_ptr(t->ods, left->entries[j].ref);
			entry->parent = node_ref;
		}
		node->entries[i] = left->entries[j];
		left->entries[j] = ENTRY_INITIALIZER;
		left->count--;
		node->count++;
	}
	return count;
}

static void merge_from_right(bpt_t t, bpt_node_t right, bpt_node_t node, int midpoint)
{
	int count;
	int i, j;
	bpt_node_t parent;
	obj_ref_t node_ref = ods_obj_ptr_to_ref(t->ods, node);

	assert(right->count > midpoint);
	count = right->count - midpoint;
	for (i = node->count, j = 0; j < count; i++, j++) {
		if (!node->is_leaf) {
			bpt_node_t entry =
				ods_obj_ref_to_ptr(t->ods, right->entries[j].ref);
			entry->parent = node_ref;
		}
		node->entries[i] = right->entries[j];
		right->count--;
		node->count++;
	}
	/* Move right's entries down */
	for (i = 0; i < right->count; i++, j++)
		right->entries[i] = right->entries[j];
	/* Clean up the end of right */
	for (j = right->count; j < right->count + count; j++)
		right->entries[j] = ENTRY_INITIALIZER;

	/* Fixup right's parents */
	parent = ods_obj_ref_to_ptr(t->ods, right->parent);
	fixup_parents(t, parent, right);
}

/**
 * \returns new root of the tree.
 */
static bpt_node_t entry_delete(obj_idx_t oidx, bpt_node_t node, int idx,
				obj_ref_t *leaf_ref, int *ent)
{
	bpt_t t = oidx->priv;
	int i, midpoint;
	bpt_node_t left, right, leaf;
	bpt_node_t parent;
	int node_idx;
	int count;
	int e;
	int left_space, right_space;
	bpt_node_t root;

 next_level:
	parent = ods_obj_ref_to_ptr(t->ods, node->parent);
	/* Remove the key and object from the node */
	for (i = idx; i < node->count - 1; i++)
		node->entries[i] = node->entries[i+1];

	node->entries[node->count-1] = ENTRY_INITIALIZER;
	node->count--;

	if (node->is_leaf) {
		leaf = node;
		e = idx;
	}

	root = ods_obj_ref_to_ptr(t->ods, t->root_ref);
	if (node == root) {
		switch (node->count) {
		case 0:
			/* This is the root and it is empty */
			ods_free(t->ods, node);
			node = NULL;
			leaf = NULL;
			e = 0;
			goto out;
		case 1:
			if (!node->is_leaf) {
				/* Promote my last remaining child to root */
				parent = ods_obj_ref_to_ptr(t->ods, root->entries[0].ref);
				parent->parent = 0;
				ods_free(t->ods, root);
				node = parent;
				goto out;
			}
		default:
			node = root;
			verify_tree(oidx);
			goto out;
		}
	}

	midpoint = split_midpoint(t->order);
	if (node->is_leaf)
		midpoint--;

	if (node->count >= midpoint) {
		if (node->is_leaf) {
			if (e == node->count) {
				if (node->entries[t->order-1].ref) {
					leaf = ods_obj_ref_to_ptr(t->ods, node->entries[t->order-1].ref);
					e = 0;
				} else {
					e--;
				}
			}
		}
		node = fixup_parents(t, parent, node);
		verify_tree(oidx);
		goto out;
	}

	/* left and right are of the same parent */
	node_idx = node_neigh(t, node, &left, &right);
	left_space = space(t, left);
	right_space = space(t, right);
	count = left_space + right_space;
	if (count < node->count) {
		/*
		 * There's not enough room in the left and right
		 * siblings to hold node's remainder, so we need to
		 * collect a few entries from the left and right
		 * siblings to put this node over the min.
		 */
		if (left && left->count > midpoint) {
			count = merge_from_left(t, left, node, midpoint);
			if (node->is_leaf)
				e += count;
		}

		if (right && right->count > midpoint
		    && node->count < midpoint) {
			merge_from_right(t, right, node, midpoint);
		}
		assert(node->count >= midpoint);
		if (node->is_leaf) {
			if (e == node->count) {
				if (node->entries[t->order-1].ref) {
					leaf = ods_obj_ref_to_ptr(t->ods, node->entries[t->order-1].ref);
					e = 0;
				} else {
					e--;
				}
			}
		}
		node = fixup_parents(t, parent, node);
		verify_tree(oidx);
		goto out;
	}

	/* Node is going away, combine as many as possible to the left */
	idx = combine_left(t, left, 0, node);

	/* Then, follow the next entry and fix the leaf chain */
	if (node->is_leaf) {
		if (e < idx) {
			/* the entry will go to the left */
			leaf = left;
			e += t->order - left_space;
		} else {
			/* the entry goes to the right */
			leaf = right;
			e -= idx;
		}

		/* fix leaf chain*/

		if (!left) {
			/* If there is no same-parent left node, get the left
			 * leaf (from different parent). */
			left = leaf_left(t, ods_obj_ptr_to_ref(t->ods, node));
		}
		if (left)
			left->entries[t->order-1].ref = node->entries[t->order-1].ref;
	}

	/* Move the remainder to the right */
	if (idx < node->count) {
		idx = combine_right(t, right, idx, node);
		assert(idx == node->count);
		if (right) {
			parent->entries[node_idx+1].key = right->entries[0].key;
			parent->entries[node_idx+1].ref =
				ods_obj_ptr_to_ref(t->ods, right);
		}
	}

	/* Remove the node(idx) from the parent. */
	idx = node_idx;
	ods_free(t->ods, node);
	node = parent;
	goto next_level;
out:
	if (leaf_ref)
		*leaf_ref = ods_obj_ptr_to_ref(t->ods, leaf);
	if (ent)
		*ent = e;
	return node;
}

/**
 * Linear search the obj_ref
 */
static int find_obj_ref(bpt_t t, obj_key_t key, obj_ref_t obj_ref,
				obj_ref_t *leaf_ref, int *ent)
{
	bpt_node_t leaf = ods_obj_ref_to_ptr(t->ods, *leaf_ref);
	obj_key_t _key;
	while (1) {
		if (leaf->entries[*ent].ref == obj_ref)
			return 0;
		_key = ods_obj_ref_to_ptr(t->ods, leaf->entries[*ent].key);
		if (t->comparator(_key, key))
			return ENOENT; /* out of bound */

		/* next entry */
		(*ent)++;
		if (*ent == leaf->count) {
			leaf = leaf_right(t, *leaf_ref);
			if (!leaf)
				return ENOENT;
			*leaf_ref = ods_obj_ptr_to_ref(t->ods, leaf);
			*ent = 0;
		}
	}
}

/**
 * \returns 0 OK
 * \return -1 Error
 */
int verify_leafs(obj_idx_t idx)
{
	bpt_t t = idx->priv;
	bpt_node_t node;
	obj_ref_t node_ref;
	obj_ref_t key_ref, prev_key_ref;
	obj_key_t k0, k1;
	int mid = split_midpoint(t->order) - 1;
	int rc, i;
	size_t alloc_sz;
	node_ref = t->root_ref;
	node = ods_obj_ref_to_ptr(t->ods, node_ref);
	while (!node->is_leaf) {
		node_ref = node->entries[0].ref;
		node = ods_obj_ref_to_ptr(t->ods, node_ref);
	}

	key_ref = 0;
	k1 = 0;
	while (node) {
		if (node->parent && node->count < mid) {
			print_node(idx, -1, node, 0);
			return -1;
		}
		prev_key_ref = key_ref;
		k0 = k1;
		key_ref = node->entries[0].key;
		if (ods_verify_ref(idx->ods, key_ref) != 0)
			return -1;
		k1 = ods_obj_ref_to_ptr(t->ods, key_ref);

		/* key size allocation verification */
		alloc_sz = ods_obj_alloc_size(t->ods, k1);
		if ((sizeof(*k1) + k1->len) > alloc_sz)
			return -1;

		/* key verification */
		if (obj_idx_verify_key(idx, k1))
			return -1;

		if (k0) {
			rc = t->comparator(k0, k1);
			if (rc > 0)
				return -1;
		}
		for (i = 1; i < node->count; i++) {
			prev_key_ref = key_ref;
			key_ref = node->entries[i].key;
			if (ods_verify_ref(idx->ods, key_ref) != 0)
				return -1;
			rc = t->comparator(ods_obj_ref_to_ptr(t->ods, key_ref),
				ods_obj_ref_to_ptr(t->ods, prev_key_ref));
			if (rc < 0) {
				return -1;
			}
			node = ods_obj_ref_to_ptr(t->ods, node_ref);
		}
		node = leaf_right(t, node_ref);
		node_ref = ods_obj_ptr_to_ref(t->ods, node);
	}

	return 0;
}

static obj_ref_t bpt_delete(obj_idx_t idx, obj_key_t key)
{
	bpt_t t = idx->priv;
	int ent;
	obj_ref_t obj, key_ref, leaf_ref;
	bpt_node_t leaf;
	bpt_node_t root;
	bpt_udata_t udata;

	leaf_ref = leaf_find_ref(t, key);
	leaf = ods_obj_ref_to_ptr(t->ods, leaf_ref);
	if (!leaf)
		return 0;
	obj = entry_find(t, leaf, key, &ent);
	if (!obj)
		return 0;
	key_ref = leaf->entries[ent].key;
#if DEBUG
	bad_ref = key_ref;
#endif
	root = entry_delete(idx, leaf, ent, NULL, NULL);
	t->root_ref = ods_obj_ptr_to_ref(t->ods, root);
	udata = __get_udata(idx->ods);
	udata->root = t->root_ref;
	ods_free(t->ods, ods_obj_ref_to_ptr(t->ods, key_ref));

	verify_tree(idx);
#if DEBUG
	bad_ref = 0; /* reset bad_ref */
#endif

	return obj;
}

obj_ref_t bpt_max_ref(bpt_t t)
{
	bpt_node_t n;
	obj_ref_t ref = 0;
	if (!t->root_ref)
		return 0;

	/* Walk to the left most leaf and return the 0-th entry  */
	ref = t->root_ref;
	n = ods_obj_ref_to_ptr(t->ods, ref);
	while (!n->is_leaf) {
		ref = n->entries[n->count-1].ref;
		n = ods_obj_ref_to_ptr(t->ods, ref);
	}
	return ref;
}

static obj_iter_t bpt_iter_new(obj_idx_t idx)
{
	bpt_iter_t iter = calloc(1, sizeof *iter);
	iter->idx = idx;
	return (struct obj_iter *)iter;
}

static void bpt_iter_delete(obj_iter_t i)
{
	free(i);
}

static int bpt_iter_begin(obj_iter_t oi)
{
	bpt_node_t node;
	bpt_iter_t i = (bpt_iter_t)oi;
	bpt_t t = i->idx->priv;
	i->ent = 0;
	i->node_ref = bpt_min_ref(t);
	if (!i->node_ref)
		return ENOENT;
	node = ods_obj_ref_to_ptr(t->ods, i->node_ref);
	assert(node->is_leaf);
	return 0;
}

static int bpt_iter_end(obj_iter_t oi)
{
	bpt_node_t node;
	bpt_iter_t i = (bpt_iter_t)oi;
	bpt_t t = i->idx->priv;
	i->ent = 0;
	i->node_ref = bpt_max_ref(t);
	if (!i->node_ref)
		return ENOENT;
	node = ods_obj_ref_to_ptr(t->ods, i->node_ref);
	i->ent = node->count - 1;
	assert(node->is_leaf);
	return 0;
}

static obj_key_t bpt_iter_key(obj_iter_t oi)
{
	bpt_iter_t i = (bpt_iter_t)oi;
	bpt_node_t node;
	obj_key_t k = NULL;
	if (i->node_ref) {
		node = ods_obj_ref_to_ptr(i->idx->ods, i->node_ref);
		k = ods_obj_ref_to_ptr(i->idx->ods, node->entries[i->ent].key);
	}
	return k;
}

static obj_ref_t bpt_iter_ref(obj_iter_t oi)
{
	bpt_iter_t i = (bpt_iter_t)oi;
	obj_ref_t ref = 0;
	bpt_node_t node;
	if (i->node_ref) {
		node = ods_obj_ref_to_ptr(i->idx->ods, i->node_ref);
		ref = node->entries[i->ent].ref;
	}
	return ref;
}

static int search_leaf(bpt_t t, bpt_node_t leaf, obj_key_t key)
{
	int i, rc;
	for (i = 0; i < leaf->count; i++) {
		obj_key_t entry_key = ods_obj_ref_to_ptr(t->ods, leaf->entries[i].key);
		rc = t->comparator(key, entry_key);
		if (!rc)
			return i;
		else if (rc < 0)
			return -1;
	}
	return -1;
}

static int search_leaf_reverse(bpt_t t, bpt_node_t leaf, obj_key_t key)
{
	int i, rc, last_match = -1;
	for (i = leaf->count-1; i >= 0; i--) {
		obj_key_t entry_key = ods_obj_ref_to_ptr(t->ods, leaf->entries[i].key);
		rc = t->comparator(key, entry_key);
		if (0 == rc)
			last_match = i;
		else
			break;
	}
	return last_match;
}

static int bpt_iter_find(obj_iter_t oi, obj_key_t key)
{
	bpt_iter_t iter = (bpt_iter_t)oi;
	bpt_t t = iter->idx->priv;
	obj_ref_t leaf_ref = leaf_find_ref(t, key);
	bpt_node_t leaf = ods_obj_ref_to_ptr(t->ods, leaf_ref);
	int i;

	if (!leaf)
		return ENOENT;

	assert(leaf->is_leaf);
	i = search_leaf(t, leaf, key);
	if (i < 0)
		return ENOENT;

	/* If the key is not the first element in the leaf, then a duplicate key
	 * cannot be a predecessor.
	 */
	if (i)
		goto found;

	find_first_dup(t, key, &leaf_ref, &i);
 found:
	iter->node_ref = leaf_ref;
	iter->ent = i;
	return 0;
}

static int bpt_iter_find_lub(obj_iter_t oi, obj_key_t key)
{
	bpt_iter_t iter = (bpt_iter_t)oi;
	obj_ref_t leaf_ref;
	int ent;

	if (__find_lub(iter->idx, key, &leaf_ref, &ent)) {
		iter->node_ref = leaf_ref;
		iter->ent = ent;
		return 0;
	}
	return ENOENT;
}

static int bpt_iter_find_glb(obj_iter_t oi, obj_key_t key)
{
	bpt_iter_t iter = (bpt_iter_t)oi;
	obj_ref_t leaf_ref;
	int ent;

	if (__find_glb(iter->idx, key, &leaf_ref, &ent)) {
		iter->node_ref = leaf_ref;
		iter->ent = ent;
		return 0;
	}
	return ENOENT;
}

static int bpt_iter_next(obj_iter_t oi)
{
	bpt_node_t node;
	bpt_iter_t i = (bpt_iter_t)oi;
	bpt_t t = i->idx->priv;
	if (!i->node_ref)
		goto not_found;
	node = ods_obj_ref_to_ptr(i->idx->ods, i->node_ref);
	if (i->ent < node->count - 1) {
		i->ent++;
	} else {
		i->node_ref = node->entries[t->order - 1].ref;
		node = ods_obj_ref_to_ptr(i->idx->ods, i->node_ref);
		if (!node)
			goto not_found;
		i->ent = 0;
	}
	return 0;
 not_found:
	return ENOENT;
}

static int bpt_iter_prev(obj_iter_t oi)
{
	bpt_iter_t i = (bpt_iter_t)oi;
	bpt_t t = i->idx->priv;
	if (!i->node_ref)
		goto not_found;
	if (i->ent) {
		i->ent--;
	} else {
		bpt_node_t left;
		left = leaf_left(t, i->node_ref);
		if (!left)
			goto not_found;
		i->node_ref = ods_obj_ptr_to_ref(i->idx->ods, left);
		i->ent = left->count - 1;
	}
	return 0;
 not_found:
	return ENOENT;
}

static obj_ref_t bpt_iter_key_delete(obj_iter_t oi)
{
	bpt_iter_t i = (bpt_iter_t)oi;
	bpt_t t = i->idx->priv;
	int ent = -1;
	obj_ref_t leaf_ref;
	obj_ref_t obj_ref;
	obj_ref_t key_ref;
	bpt_node_t root;
	bpt_node_t leaf;

	if (!i->node_ref)
		return 0;

	leaf = ods_obj_ref_to_ptr(t->ods, i->node_ref);
	obj_ref = leaf->entries[i->ent].ref;
	key_ref = leaf->entries[i->ent].key;

#if DEBUG
	bad_ref = key_ref;
#endif
	root = entry_delete(i->idx, leaf, i->ent, &leaf_ref, &ent);

	assert(ent >= 0);
	assert(ent < t->order);

	/* update root */
	t->root_ref = ods_obj_ptr_to_ref(t->ods, root);
	__get_udata(i->idx->ods)->root = t->root_ref;

	verify_tree(i->idx);
#if DEBUG
	bad_ref = 0;
#endif

	/* Free the key */
	ods_free(t->ods, ods_obj_ref_to_ptr(t->ods, key_ref));

	i->node_ref = leaf_ref;
	i->ent = ent;
	return obj_ref;
}

static const char *bpt_get_type(void)
{
	return "BPTREE";
}

static void bpt_commit(obj_idx_t idx)
{
	bpt_close(idx);
}

static struct obj_idx_provider bpt_provider = {
	.get_type = bpt_get_type,
	.init = bpt_init,
	.open = bpt_open,
	.close = bpt_close,
	.commit = bpt_commit,
	.insert = bpt_insert,
	.delete = bpt_delete,
	.find = bpt_find,
	.find_lub = bpt_find_lub,
	.find_glb = bpt_find_glb,
	.iter_new = bpt_iter_new,
	.iter_delete = bpt_iter_delete,
	.iter_key_del = bpt_iter_key_delete,
	.iter_find = bpt_iter_find,
	.iter_find_lub = bpt_iter_find_lub,
	.iter_find_glb = bpt_iter_find_glb,
	.iter_begin = bpt_iter_begin,
	.iter_end = bpt_iter_end,
	.iter_next = bpt_iter_next,
	.iter_prev = bpt_iter_prev,
	.iter_key = bpt_iter_key,
	.iter_ref = bpt_iter_ref,
	.verify = bpt_verify,
};

struct obj_idx_provider *get(void)
{
	return &bpt_provider;
}

