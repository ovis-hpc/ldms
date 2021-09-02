/*
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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
#include <stdlib.h>
#include "rrbt.h"
#include <assert.h>
#include <string.h>

/* A LEAF (NULL) is considered BLACK */
static int is_red(struct rrbn *x)
{
	if (!x)
		return 0;
	return x->color == RRBN_RED;
}

static int is_black(struct rrbn *x)
{
	if (!x)
		return 1;
	return x->color == RRBN_BLACK;
}

static void rotate_left(rrbt_t t, struct rrbn *x)
{
	struct rrbn *y = rrbn_ptr(t, x->right);
	struct rrbn *parent = rrbn_ptr(t, x->parent);

	/* Link y's left to x's right and update parent if not a leaf */
	x->right = y->left;
	if (y->left)
		rrbn_ptr(t, y->left)->parent = rrbt_off(t, x);

	/* Attach y to x's parent if x is not the root.
	 * If x == *t->root, then x->parent == NULL */
	y->parent = x->parent;
	if (*t->root != rrbt_off(t, x)) {
		assert(x->parent);
		if (parent->left == rrbt_off(t, x))
			parent->left = rrbt_off(t, y);
		else
			parent->right = rrbt_off(t, y);
	} else {
		assert(x->parent == 0);
		*t->root = rrbt_off(t, y);
	}

	/* Attach x as y's new left */
	y->left = rrbt_off(t, x);
	x->parent = rrbt_off(t, y);
}

static void rotate_right(rrbt_t t, struct rrbn *x)
{
	struct rrbn *y = rrbn_ptr(t, x->left);
	struct rrbn *parent = rrbn_ptr(t, x->parent);

	/* Link y's right to x's left and update parent */
	x->left = y->right;
	if (y->right)
		rrbn_ptr(t, y->right)->parent = rrbt_off(t, x);

	/* Attach y to x's parent */
	if (*t->root != rrbt_off(t, x)) {
		y->parent = rrbt_off(t, parent);
		if (parent->right == rrbt_off(t, x))
			parent->right = rrbt_off(t, y);
		else
			parent->left = rrbt_off(t, y);
	} else {
		*t->root = rrbt_off(t, y);
		y->parent = 0;
	}

	/* Attach x as y's new left */
	y->right = rrbt_off(t, x);
	x->parent = rrbt_off(t, y);
}

/**
 * \brief Initialize an RRBT.
 *
 * \param t	Pointer to the RRBT.
 * \param c	Pointer to the function that compares nodes in the
 *		RRBT.
 */
void rrbt_init(struct rrbt *t)
{
	t->root = 0;
}

rrbt_t rrbt_get(struct rrbt_instance *inst, uint64_t *root, void *base, rrbn_comparator_t c)
{
	inst->root = root;
	inst->base = base;
	inst->comparator = c;
	return inst;
}

/**
 * \brief Returns TRUE if the tree is empty.
 *
 * \param t	Pointer to the rrbt.
 * \retval 0	The tree is not empty
 * \retval 1	The tree is empty
 */
int rrbt_empty(rrbt_t t)
{
	return (*t->root == 0);
}

/**
 * \brief Initialize an RRBN node.
 *
 * Initialize an RRBN node. This is a convenience function to avoid
 * having the application know about the internals of the RRBN while
 * still allowing the RRBN to be embedded in the applications object
 * and avoiding a second allocation in rrbn_ins.
 *
 * \param n The RRBN to initialize
 * \param key Pointer to the key
 */
void rrbn_init(struct rrbn *n, void *key, size_t key_len)
{
	memcpy(n->key, key, key_len);
}

/**
 * \brief Insert a new node into the RRBT.
 *
 * Insert a new node into a RRBT. The node is allocated by the user and
 * must be freed by the user when removed from the tree. The 'key'
 * field of the provided node must have already been set up by the
 * caller.
 *
 * \param t	Pointer to the RRBT.
 * \param x	Pointer to the node to insert.
 */
void rrbt_ins(rrbt_t t, struct rrbn *x)
{
	struct rrbn *parent = 0;
	struct rrbn *n;
	int c = 0;

	/* Initialize new node */
	x->left = x->right = 0;

	/* Trivial root insertion */
	if (!*t->root) {
		x->parent = 0;
		x->color = RRBN_BLACK;
		*t->root = rrbt_off(t, x);
		return;
	}

	/* Always insert a RED node */
	x->color = RRBN_RED;
	for (n = rrbn_ptr(t, *t->root); n; ) {
		parent = n;
		c = t->comparator(n->key, x->key);
		if (c > 0)
			n = rrbn_ptr(t, n->left);
		else
			n = rrbn_ptr(t, n->right);
	}
	/* Replace leaf with new node */
	assert(parent);
	x->parent = rrbt_off(t, parent);
	if (c > 0)
		parent->left = rrbt_off(t, x);
	else
		parent->right = rrbt_off(t, x);

	/*
	 * While x is not the root and x's parent is red. Note that if x's
	 * parent is RED, then x's parent is also not the root
	 */
	while (x != rrbn_ptr(t, *t->root) && is_red(rrbn_ptr(t, x->parent))) {
		struct rrbn *uncle;
		if (x->parent == rrbn_ptr(t, rrbn_ptr(t, x->parent)->parent)->left) {
			uncle = rrbn_ptr(t, rrbn_ptr(t, rrbn_ptr(t, x->parent)->parent)->right);
			if (is_red(uncle)) {
				rrbn_ptr(t, x->parent)->color = RRBN_BLACK;
				uncle->color = RRBN_BLACK;
				rrbn_ptr(t, rrbn_ptr(t, x->parent)->parent)->color = RRBN_RED;
				x = rrbn_ptr(t, rrbn_ptr(t, x->parent)->parent);
			} else {
				if (x == rrbn_ptr(t, rrbn_ptr(t, x->parent)->right)) {
					x = rrbn_ptr(t, x->parent);
					rotate_left(t, x);
				}
				rrbn_ptr(t, x->parent)->color = RRBN_BLACK;
				rrbn_ptr(t, rrbn_ptr(t, x->parent)->parent)->color = RRBN_RED;
				rotate_right(t, rrbn_ptr(t, rrbn_ptr(t, x->parent)->parent));
			}
		} else {
			uncle = rrbn_ptr(t, rrbn_ptr(t, rrbn_ptr(t, x->parent)->parent)->left);
			if (is_red(uncle)) {
				rrbn_ptr(t, x->parent)->color = RRBN_BLACK;
				uncle->color = RRBN_BLACK;
				rrbn_ptr(t, rrbn_ptr(t, x->parent)->parent)->color = RRBN_RED;
				x = rrbn_ptr(t, rrbn_ptr(t, x->parent)->parent);
			} else {
				if (x == rrbn_ptr(t, rrbn_ptr(t, x->parent)->left)) {
					x = rrbn_ptr(t, x->parent);
					rotate_right(t, x);
				}
				rrbn_ptr(t, x->parent)->color = RRBN_BLACK;
				if (rrbn_ptr(t, x->parent)->parent) {
					rrbn_ptr(t, rrbn_ptr(t, x->parent)->parent)->color = RRBN_RED;
					rotate_left(t, rrbn_ptr(t, rrbn_ptr(t, x->parent)->parent));
				}
			}
		}
	}
	rrbn_ptr(t, *t->root)->color = RRBN_BLACK;
}

/**
 * \brief Delete a node from the RRBT.
 *
 * \param t	Pointer to the RRBT.
 * \param z	Pointer to the node to delete.
 */
void rrbt_del(rrbt_t t, struct rrbn *z)
{
	struct rrbn *y = z;
	struct rrbn *x;
	int del_color;

	/* If this is the only node, special-case the tree back to empty. */
	if (*t->root == rrbt_off(t, y) && y->left == 0 && y->right == 0) {
		*t->root = 0;
		return;
	}

	/*
	 * If the node to be deleted has both a left and right child, we
	 * must find a partially empty node in a subtree to replace z with.
	 */
	if (y->left && y->right)
		for (y = rrbn_ptr(t, z->right); y->left; y = rrbn_ptr(t, y->left));

	if (y->left)
		x = rrbn_ptr(t, y->left);
	else
		/* Note that y->right may be NULL */
		x = rrbn_ptr(t, y->right);

	/* Replace y with x where it is attached at y's parent. */
	if (x)
		x->parent = y->parent;
	if (*t->root != rrbt_off(t, y)) {
		if (rrbt_off(t, y) == rrbn_ptr(t, y->parent)->left)
			rrbn_ptr(t, y->parent)->left = rrbt_off(t, x);
		else
			rrbn_ptr(t, y->parent)->right = rrbt_off(t, x);
	} else
		*t->root = rrbt_off(t, x);

	/*
	 * Replace z in the tree with y if z had 2 children
	 */
	del_color = y->color;
	if (y != z) {
		y->left = z->left;
		y->right = z->right;
		y->color = z->color;
		y->parent = z->parent;
		if (y->left)
			rrbn_ptr(t, y->left)->parent = rrbt_off(t, y);
		if (y->right)
			rrbn_ptr(t, y->right)->parent = rrbt_off(t, y);
		if (*t->root != rrbt_off(t, z)) {
			if (rrbn_ptr(t, z->parent)->left == rrbt_off(t, z))
				rrbn_ptr(t, z->parent)->left = rrbt_off(t, y);
			else
				rrbn_ptr(t, z->parent)->right = rrbt_off(t, y);
		} else
			*t->root = rrbt_off(t, y);
	}

	/* If we deleted a red node, there is nothing to be done. */
	if (del_color == RRBN_RED)
		return;

	/* Now recolor and balance the tree */
	while (rrbt_off(t, x) != *t->root && x && is_black(x)) {
		assert(x->parent);
		if (rrbt_off(t, x) == rrbn_ptr(t, x->parent)->left) {
			y = rrbn_ptr(t, rrbn_ptr(t, x->parent)->right);
			if (!y) {
				x = rrbn_ptr(t, x->parent);
				continue;
			}
			if (is_red(y)) {
				y->color = RRBN_BLACK;
				rrbn_ptr(t, x->parent)->color = RRBN_RED;
				rotate_left(t, rrbn_ptr(t, x->parent));
				y = rrbn_ptr(t, rrbn_ptr(t, x->parent)->right);
				if (!y) {
					x = rrbn_ptr(t, x->parent);
					continue;
				}
			}
			if (is_black(rrbn_ptr(t, y->left)) && is_black(rrbn_ptr(t, y->right))) {
				y->color = RRBN_RED;
				x = rrbn_ptr(t, x->parent);
			} else {
				if (is_black(rrbn_ptr(t, y->right))) {
					rrbn_ptr(t, y->left)->color = RRBN_BLACK;
					y->color = RRBN_RED;
					rotate_right(t, y);
					y = rrbn_ptr(t, rrbn_ptr(t, x->parent)->right);
					if (!y) {
						x = rrbn_ptr(t, x->parent);
						continue;
					}
				}
				y->color = rrbn_ptr(t, x->parent)->color;
				rrbn_ptr(t, x->parent)->color = RRBN_BLACK;
				rrbn_ptr(t, y->right)->color = RRBN_BLACK;
				rotate_left(t, rrbn_ptr(t, x->parent));
				x = rrbn_ptr(t, *t->root);
			}
		} else {                /* x == x->parent->right */
			y = rrbn_ptr(t, rrbn_ptr(t, x->parent)->left);
			if (!y) {
				x = rrbn_ptr(t, x->parent);
				continue;
			}
			if (is_red(y)) {
				y->color = RRBN_BLACK;
				rrbn_ptr(t, x->parent)->color = RRBN_RED;
				rotate_right(t, rrbn_ptr(t, x->parent));
				y = rrbn_ptr(t, rrbn_ptr(t, x->parent)->left);
				if (!y) {
					x = rrbn_ptr(t, x->parent);
					continue;
				}
			}
			if (is_black(rrbn_ptr(t, y->right)) && is_black(rrbn_ptr(t, y->left))) {
				y->color = RRBN_RED;
				x = rrbn_ptr(t, x->parent);
			} else {
				if (is_black(rrbn_ptr(t, y->left))) {
					rrbn_ptr(t, y->right)->color = RRBN_BLACK;
					y->color = RRBN_RED;
					rotate_left(t, y);
					y = rrbn_ptr(t, rrbn_ptr(t, x->parent)->left);
					if (!y) {
						x = rrbn_ptr(t, x->parent);
						continue;
					}
				}
				y->color = rrbn_ptr(t, x->parent)->color;
				rrbn_ptr(t, x->parent)->color = RRBN_BLACK;
				rrbn_ptr(t, y->left)->color = RRBN_BLACK;
				rotate_right(t, rrbn_ptr(t, x->parent));
				x = rrbn_ptr(t, *t->root);
			}
		}
	}
	if (x)
		x->color = RRBN_BLACK;
}

/**
 * \brief Return the largest sibling less than or equal
 *
 * \param n	Pointer to the node
 * \return !0	Pointer to the lesser sibling
 * \return NULL	The node specified is the min
 */
struct rrbn *rrbt_greatest_lt_or_eq(rrbt_t t, struct rrbn *n)
{
	if (n->left)
		return rrbn_ptr(t, n->left);
	if (n->parent && rrbn_ptr(t, n->parent)->left == rrbt_off(t, n))
		return rrbn_ptr(t, n->parent);
	return NULL;
}

/**
 * \brief Find the largest node less than or equal to a key
 *
 * \param t	Pointer to the tree
 * \param key   Pointer to the key value
 * \return !0	Pointer to the lesser sibling
 * \return NULL	The node specified is the min
 */
struct rrbn *rrbt_find_glb(rrbt_t t, const void *key)
{
	struct rrbn *x;
	struct rrbn *glb = NULL;

	for (x = rrbn_ptr(t, *t->root); x; ) {
		int c;
		c = t->comparator(x->key, key);
		if (!c)
			return x;
		if (c > 0) {
			x = rrbn_ptr(t, x->left);
		} else {
			glb = x;
			x = rrbn_ptr(t, x->right);
		}
	}
	return glb;
}

/**
 * \brief Return the smallest sibling greater than or equal
 *
 * \param n	Pointer to the node
 * \retval !0	Pointer to the greater sibling
 * \retval NULL	The node specified is the max
 */
struct rrbn *rrbt_least_gt_or_eq(rrbt_t t, struct rrbn *n)
{
	if (n->right)
		return rrbn_ptr(t, n->right);
	if (n->parent && rrbn_ptr(t, n->parent)->right == rrbt_off(t, n))
		return rrbn_ptr(t, n->parent);
	return NULL;
}

/**
 * \brief Find the smallest node greater than or equal to a key
 *
 * \param t	Pointer to the tree
 * \param key	Pointer to the key
 * \retval !0	Pointer to the greater sibling
 * \retval NULL	The node specified is the max
 */
struct rrbn *rrbt_find_lub(rrbt_t t, const void *key)
{
	struct rrbn *x;
	struct rrbn *lub = NULL;

	for (x = rrbn_ptr(t, *t->root); x; ) {
		int c;
		c = t->comparator(x->key, key);
		if (!c)
			return x;
		if (c < 0) {
			x = rrbn_ptr(t, x->right);
		} else {
			lub = x;
			x = rrbn_ptr(t, x->left);
		}
	}
	return lub;
}

/**
 * \brief Find a node in the RRBT that matches a key
 *
 * \param t	Pointer to the RRBT.
 * \param key	Pointer to the key.
 * \retval !NULL Pointer to the node with the matching key.
 * \retval NULL  No node in the tree matches the key.
 */
struct rrbn *rrbt_find(rrbt_t t, const void *key)
{
	struct rrbn *x;

	for (x = rrbn_ptr(t, *t->root); x; ) {
		int c;
		c = t->comparator(x->key, key);
		if (!c)
			return x;

		if (c > 0)
			x = rrbn_ptr(t, x->left);
		else
			x = rrbn_ptr(t, x->right);
	}
	return NULL;
}

struct rrbn *__rrbn_min(rrbt_t t, struct rrbn *n)
{
	for (; n && rrbn_ptr(t, n->left); n = rrbn_ptr(t, n->left));
	return n;
}

struct rrbn *__rrbn_max(rrbt_t t, struct rrbn *n)
{
	for (; n && rrbn_ptr(t, n->right); n = rrbn_ptr(t, n->right));
	return n;
}

/**
 * \brief Return the smallest (i.e leftmost) node in the RRBT.
 *
 * \param t	Pointer to the RRBT.
 * \return	Pointer to the node or NULL if the tree is empty.
 */
struct rrbn *rrbt_min(rrbt_t t)
{
	return __rrbn_min(t, rrbn_ptr(t, *t->root));
}

/**
 * \brief Return the largest (i.e. rightmost) node in the RRBT.
 *
 * \param t	Pointer to the RRBT.
 * \return	Pointer to the node or NULL if the tree is empty.
 */
struct rrbn *rrbt_max(rrbt_t t)
{
	return __rrbn_max(t, rrbn_ptr(t, *t->root));
}

static int rrbt_traverse_subtree(rrbt_t t, struct rrbn *n, rrbn_node_fn f,
				 void *fn_data, int level)
{
	int rc;
	if (n) {
		rc = rrbt_traverse_subtree(t, rrbn_ptr(t, n->left), f, fn_data, level+1);
		if (rc)
			goto err;
		rc = f(n, fn_data, level);
		if (rc)
			goto err;
		rc = rrbt_traverse_subtree(t, rrbn_ptr(t, n->right), f, fn_data, level+1);
		if (rc)
			goto err;
	}
	return 0;
 err:
	return rc;
}

/**
 * \brief Traverse an RRBT
 *
 * Perform a recursive traversal of an RRBT from left to right. For
 * each non-leaf node, a callback function is invoked with a pointer
 * to the node.
 *
 * \param t	A pointer to the RRBT.
 * \param f	A pointer to the function to call as each RRBT node is
 *		visited.
 * \param p	Pointer to provide as an argument to the callback
 *		function along with the RRBT node pointer.
 */
int rrbt_traverse(rrbt_t t, rrbn_node_fn f, void *p)
{
	if (*t->root)
		return rrbt_traverse_subtree(t, rrbn_ptr(t, *t->root), f, p, 0);
	return 0;
}

/**
 * \brief Return the successor node
 *
 * Given a node in the tree, return it's successor.
 *
 * \param n	Pointer to the current node
 */
struct rrbn *rrbn_succ(rrbt_t t, struct rrbn *n)
{
	if (n->right)
		return __rrbn_min(t, rrbn_ptr(t, n->right));

	if (n->parent) {
		while (n->parent && n == rrbn_ptr(t, rrbn_ptr(t, n->parent)->right))
			n = rrbn_ptr(t, n->parent);
		return rrbn_ptr(t, n->parent);
	}

	return NULL;
}

/**
 * \brief Return the predecessor node
 *
 * Given a node in the tree, return it's predecessor.
 *
 * \param n	Pointer to the current node
 */
struct rrbn *rrbn_pred(rrbt_t t, struct rrbn *n)
{
	if (n->left)
		return __rrbn_max(t, rrbn_ptr(t, n->left));

	if (n->parent) {
		while (n->parent && n == rrbn_ptr(t, rrbn_ptr(t, n->parent)->left))
			n = rrbn_ptr(t, n->parent);
		return rrbn_ptr(t, n->parent);
	}

	return NULL;
}

#ifdef RRBT_TEST
#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#include "ovis-test/test.h"
struct test_key {
	struct rrbn n;
	int key;
	int ord;
};

int test_comparator(void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

int rrbt_print(struct rrbn *rrbn, void *fn_data, int level)
{
	struct test_key *k = (struct test_key *)rrbn;
	printf("%*c-%d(%d)\n", 200 - (level * 3), (rrbn->color?'B':'R'), k->key, k->ord);
	return 0;
}

int main(int argc, char *argv[])
{
	struct rrbt rrbt;
	struct rrbt rrbtB;
	int key_count;
	int max = -1;
	int min = 0x7FFFFFFF;
	struct test_key key;
	int x;
	time_t t = time(NULL);
	struct test_key** keys;

	if (!argv[1]){
		printf("usage: ./rrbt {key-count} [random seed]\n");
		exit(1);
	}
	key_count = atoi(argv[1]);
	keys = calloc(key_count, sizeof(struct test_key*));

	if (argv[2])
		t = atoi(argv[2]);

	rrbt_init(&rrbtB, test_comparator);

	/*
	 * Test Duplicates
	 */
	for (x = 0; x < key_count; x++) {
		struct test_key *k = calloc(1, sizeof *k);
		k->ord = x;
		rrbn_init(&k->n, &k->key);
		keys[x] = k;
		k->key = 1000; // key_count;
		rrbt_ins(&rrbtB, &k->n);
	}
	struct rrbn *n;
	x = 0;
	for (n = rrbt_min(&rrbtB); n; n = rrbn_succ(n)) {
		struct test_key *k = container_of(n, struct test_key, n);
		TEST_ASSERT(k->ord == x, "k->ord(%d) == %d\n", k->ord, x);
		x++;
	}
	x = 9;
	for (n = rrbt_max(&rrbtB); n; n = rrbn_pred(n)) {
		struct test_key *k = container_of(n, struct test_key, n);
		TEST_ASSERT(k->ord == x, "k->ord(%d) == %d\n", k->ord, x);
		x--;
	}
	// rrbt_traverse(&rrbtB, rrbt_print, NULL);
	for (x = 0; x < key_count; x++) {
		struct test_key *k = keys[x];
		rrbt_del(&rrbtB, &k->n);
	}
	for (x = 0; x < key_count; x++) {
		struct test_key *k = calloc(1, sizeof *k);
		k->ord = x;
		rrbn_init(&k->n, &k->key);
		keys[x] = k;
		k->key = 1000; // key_count;
		rrbt_ins(&rrbtB, &k->n);
	}
	// rrbt_traverse(&rrbtB, rrbt_print, NULL);
	for (x = key_count - 1; x >= 0; x--) {
		struct test_key *k = keys[x];
		rrbt_del(&rrbtB, &k->n);
	}
	/*
	 * Test LUB/GLB
	 */
	int test_keys[] = { 1, 3, 5, 7, 9 };
	int i;
	struct test_key *k;
	for (x = 0; x < 100; ) {
		for (i = 0; i < sizeof(test_keys) / sizeof(test_keys[0]); i++) {
			k = calloc(1, sizeof *k);
			k->ord = x++;
			rrbn_init(&k->n, &k->key);
			k->key = test_keys[i];
			rrbt_ins(&rrbtB, &k->n);
		}
	}
	//  rrbt_traverse(&rrbtB, rrbt_print, NULL);
	x = 0;
	n = rrbt_find_glb(&rrbtB, &x);
	TEST_ASSERT(n == NULL, "glb(0) == NULL\n");
	for (i = 0; i < sizeof(test_keys) / sizeof(test_keys[0]); i++) {
		x = test_keys[i];
		n = rrbt_find_glb(&rrbtB, &x);
		k = container_of(n, struct test_key, n);
		TEST_ASSERT(k->key == test_keys[i], "glb(%d) == %d\n", x, k->key);

		x = test_keys[i] + 1;
		n = rrbt_find_glb(&rrbtB, &x);
		k = container_of(n, struct test_key, n);
		TEST_ASSERT(k->key == test_keys[i], "glb(%d) == %d\n", x, k->key);
	}
	x = 10;
	n = rrbt_find_lub(&rrbtB, &x);
	TEST_ASSERT(n == NULL, "lub(10) == NULL\n");

	/* Empty the tree */
	for (n = rrbt_min(&rrbtB); n; n = rrbt_min(&rrbtB)) {
		k = container_of(n, struct test_key, n);
		rrbt_del(&rrbtB, n);
		free(k);
	}
	for (i = 0; i < 100; i++) {
		k = calloc(1, sizeof(*k));
		k->ord = x++;
		k->key = i;
		rrbn_init(&k->n, &k->key);
		rrbt_ins(&rrbtB, &k->n);
	}
	for (x = 0; x < 100; x += 2) {
		struct rrbn *rrbn = rrbt_find(&rrbtB, &x);
		TEST_ASSERT((rrbn != NULL), "%d found.\n", x);
	}
	srandom(t);
	rrbt_init(&rrbt, test_comparator);
	key_count = atoi(argv[1]);
	while (key_count--) {
		struct test_key *k = calloc(1, sizeof *k);
		struct rrbn *rrbn;
		rrbn_init(&k->n, &k->key);
		k->key = (int)random();
		rrbn = rrbt_find(&rrbt, &k->key);
		if (rrbn) {
			printf("FAIL -- DUPLICATE %d.\n", k->key);
			continue;
		}
		rrbt_ins(&rrbt, &k->n);
		if (k->key > max)
			max = k->key;
		else if (k->key < min)
			min = k->key;
	}
	// rrbt_traverse(&rrbt, rrbt_print, NULL);
	struct rrbn *min_rrbn = rrbt_min(&rrbt);
	struct rrbn *max_rrbn = rrbt_max(&rrbt);
	TEST_ASSERT((min_rrbn && ((struct test_key *)min_rrbn)->key == min),
		    "The min (%d) is in the tree.\n", min);
	TEST_ASSERT((max_rrbn && ((struct test_key *)max_rrbn)->key == max),
		    "The max (%d) is in the tree.\n", max);
	TEST_ASSERT((min < max),
		    "The min (%d) is less than the max (%d).\n",
		    min, max);
	if (min_rrbn)
		rrbt_del(&rrbt, min_rrbn);
	TEST_ASSERT((rrbt_find(&rrbt, &min) == NULL),
		    "Delete %d and make certain it's not found.\n",
		    min);
	if (max_rrbn)
		rrbt_del(&rrbt, max_rrbn);
	TEST_ASSERT((rrbt_find(&rrbt, &max) == NULL),
		    "Delete %d and make certain it's not found.\n", max);
	while (0) {
		t = time(NULL);
		printf("seed %jd\n", (intmax_t)t);
		srandom(t);
		key_count = atoi(argv[1]);
		while (key_count--) {
			struct test_key *k = calloc(1, sizeof *k);
			struct rrbn *rrbn;
			rrbn_init(&k->n, &k->key);
			k->key = (int)random();
			rrbn = rrbt_find(&rrbt, &k->key);
			if (rrbn) {
				printf("FAIL -- DUPLICATE %d.\n", k->key);
				continue;
			}
			rrbt_ins(&rrbt, &k->n);
		}
		srandom(t);
		key_count = atoi(argv[1]);
		printf("Created %d keys.\n", key_count);
		while (key_count--) {
			int key;
			struct rrbn *rrbn;
			key = (int)random();
			rrbn = rrbt_find(&rrbt, &key);
			if (rrbn) {
				rrbt_del(&rrbt, rrbn);
				free(rrbn);
				continue;
			} else {
				printf("Doh!!\n");
			}
		}
		printf("Deleted...\n");
	}
	// rrbt_traverse(&rrbt, rrbt_print, NULL);
	return 0;
}

#endif
