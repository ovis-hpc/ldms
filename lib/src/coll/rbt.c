/*
 * Copyright (c) 2008 Open Grid Computing, Inc. All rights reserved.
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
#include "rbt.h"

struct rbn _LEAF = {
	.left = &_LEAF,
	.right = &_LEAF,
	.parent = &_LEAF,
	.color = RBN_BLACK,
};
static struct rbn *LEAF = &_LEAF;

static void rotate_left(struct rbt *t, struct rbn *x)
{
	struct rbn *y = x->right;
	struct rbn *parent = x->parent;

	/* Link y's left to x's right and update parent if not a leaf */
	x->right = y->left;
	if (y->left != LEAF)
		y->left->parent = x;

	/* Attach y to x's parent if x is not the root */
	if (t->root != x) {
		y->parent = parent;
		if (parent->left == x)
			parent->left = y;
		else
			parent->right = y;
	} else
		t->root = y;

	/* Attach x as y's new left */
	y->left = x;
	x->parent = y;
}

static void rotate_right(struct rbt *t, struct rbn *x)
{
	struct rbn *y = x->left;
	struct rbn *parent = x->parent;

	/* Link y's right to x's left and update parent */
	x->left = y->right;
	if (y->right != LEAF)
		y->right->parent = x;

	/* Attach y to x's parent */
	if (t->root != x) {
		y->parent = parent;
		if (parent->right == x)
			parent->right = y;
		else
			parent->left = y;
	} else
		t->root = y;

	/* Attach x as y's new left */
	y->right = x;
	x->parent = y;
}

/**
 * \brief Initialize an RBT.
 *
 * \param t	Pointer to the RBT.
 * \param c	Pointer to the function that compares nodes in the
 *		RBT.
 */
void rbt_init(struct rbt *t, rbn_comparator_t c)
{
	t->root = NULL;
	t->comparator = c;
}

/**
 * \brief Initialize an RBN node.
 *
 * Initialize an RBN node. This is a convenience function to avoid
 * having the application know about the internals of the RBN while
 * still allowing the RBN to be embedded in the applications object
 * and avoiding a second allocation in rbn_ins.
 *
 * \param rbn The RBN to initialie
 * \param key Pointer to the key
 */
void rbn_init(struct rbn *n, void *key)
{
	n->key = key;
}

/**
 * \brief Insert a new node into the RBT.
 *
 * Insert a new node into a RBT. The node is allocated by the user and
 * must be freed by the user when removed from the tree. The 'key'
 * field of the provided node must have already been set up by the
 * caller.
 *
 * \param t	Pointer to the RBT.
 * \param x	Pointer to the node to insert.
 */
void rbt_ins(struct rbt *t, struct rbn *x)
{
	struct rbn *parent = NULL;
	struct rbn *n;
	int c = 0;

	/* Initialize new node */
	x->left = x->right = LEAF;

	/* Trivial root insertion */
	if (!t->root) {
		x->parent = NULL;
		x->color = RBN_BLACK;
		t->root = x;
		return;
	}

	/* Always insert a RED node */
	x->color = RBN_RED;
	for (n = t->root; n != LEAF; ) {
		parent = n;
		c = t->comparator(n->key, x->key);
		if (c > 0)
			n = n->left;
		else
			n = n->right;
		assert(n->left != parent);
		assert(n->right != parent);
	}
	/* Replace leaf with new node */
	x->parent = parent;
	if (c > 0)
		parent->left = x;
	else
		parent->right = x;

	/*
	 * While x is not the root and x's parent is red. Note that if x's
	 * parent is RED, then x's parent is also not the root
	 */
	while (x != t->root && x->parent->color == RBN_RED) {
		struct rbn *uncle;
		if (x->parent == x->parent->parent->left) {
			uncle = x->parent->parent->right;
			if (uncle->color == RBN_RED) {
				x->parent->color = RBN_BLACK;
				uncle->color = RBN_BLACK;
				x->parent->parent->color = RBN_RED;
				x = x->parent->parent;
			} else {
				if (x == x->parent->right) {
					x = x->parent;
					rotate_left(t, x);
				}
				x->parent->color = RBN_BLACK;
				x->parent->parent->color = RBN_RED;
				rotate_right(t, x->parent->parent);
			}
		} else {
			uncle = x->parent->parent->left;
			if (uncle->color == RBN_RED) {
				x->parent->color = RBN_BLACK;
				uncle->color = RBN_BLACK;
				x->parent->parent->color = RBN_RED;
				x = x->parent->parent;
			} else {
				if (x == x->parent->left) {
					x = x->parent;
					rotate_right(t, x);
				}
				x->parent->color = RBN_BLACK;
				x->parent->parent->color = RBN_RED;
				rotate_left(t, x->parent->parent);
			}
		}
	}
	t->root->color = RBN_BLACK;
}

/**
 * \brief Delete a node from the RBT.
 *
 * \param t	Pointer to the RBT.
 * \param z	Pointer to the node to delete.
 */
void rbt_del(struct rbt *t, struct rbn *z)
{
	struct rbn *y = z;
	struct rbn *x;
	int del_color;

	/* If this is the only node, special-case the tree back to empty. */
	if (t->root == y && y->left == LEAF && y->right == LEAF) {
		t->root = NULL;
		return;
	}

	/*
	 * If the node to be deleted has both a left and right child, we
	 * must find a partially empty node in a subtree to replace z with.
	 */
	if (y->left != LEAF && y->right != LEAF)
		for (y = z->right; y->left != LEAF; y = y->left);

	if (y->left != LEAF)
		x = y->left;
	else
		x = y->right;

	/* Replace y with x where it is attached at y's parent */
	x->parent = y->parent;
	if (t->root != y) {
		if (y == y->parent->left)
			y->parent->left = x;
		else
			y->parent->right = x;
	} else
		t->root = x;

	/*
	 * Replace z in the tree with y if z had 2 children
	 */
	del_color = y->color;
	if (y != z) {
		y->left = z->left;
		y->right = z->right;
		y->color = z->color;
		y->parent = z->parent;
		y->left->parent = y;
		y->right->parent = y;
		if (t->root != z) {
			if (z->parent->left == z)
				z->parent->left = y;
			else
				z->parent->right = y;
		} else
			t->root = y;
	}

	/* If we deleted a red node, there is nothing to be done. */
	if (del_color == RBN_RED)
		return;

	/* Now recolor and balance the tree */
	while (x != t->root && x->color == RBN_BLACK) {
		if (x == x->parent->left) {
			y = x->parent->right;
			if (y->color == RBN_RED) {
				y->color = RBN_BLACK;
				x->parent->color = RBN_RED;
				rotate_left(t, x->parent);
				y = x->parent->right;
			}
			if (y->left->color == RBN_BLACK &&
			    y->right->color == RBN_BLACK) {
				y->color = RBN_RED;
				x = x->parent;
			} else {
				if (y->right->color == RBN_BLACK) {
					y->left->color = RBN_BLACK;
					y->color = RBN_RED;
					rotate_right(t, y);
					y = x->parent->right;
				}
				y->color = x->parent->color;
				x->parent->color = RBN_BLACK;
				y->right->color = RBN_BLACK;
				rotate_left(t, x->parent);
				x = t->root;
			}
		} else {                /* x == x->parent->right */
			y = x->parent->left;
			if (y->color == RBN_RED) {
				y->color = RBN_BLACK;
				x->parent->color = RBN_RED;
				rotate_right(t, x->parent);
				y = x->parent->left;
			}
			if (y->right->color == RBN_BLACK &&
			    y->left->color == RBN_BLACK) {
				y->color = RBN_RED;
				x = x->parent;
			} else {
				if (y->left->color == RBN_BLACK) {
					y->right->color = RBN_BLACK;
					y->color = RBN_RED;
					rotate_left(t, y);
					y = x->parent->left;
				}
				y->color = x->parent->color;
				x->parent->color = RBN_BLACK;
				y->left->color = RBN_BLACK;
				rotate_right(t, x->parent);
				x = t->root;
			}
		}
	}
	x->color = RBN_BLACK;
}

/**
 * \brief Return the largest sibling less than or equal
 *
 * \param n	Pointer to the node
 * \return !0	Pointer to the lesser sibling
 * \return NULL	The node specified is the min
 */
struct rbn *rbt_greatest_lt_or_eq(struct rbn *n)
{
	if (n->left)
		return n->left;
	if (n->parent && n->parent->left == n)
		return n->parent;
	return NULL;
}

/**
 * \brief Find the largest node less than or equal to a key
 *
 * \param n	Pointer to the node
 * \return !0	Pointer to the lesser sibling
 * \return NULL	The node specified is the min
 */
struct rbn *rbt_find_greatest_lt_or_eq(struct rbt *t, void *key)
{
	struct rbn *x;

	for (x = t->root; x && x != LEAF; ) {
		int c;
		c = t->comparator(x->key, key);
		if (!c)
			return x;

		if (c > 0) {
			x = x->left;
		} else {
			/* The node is less than the key. If the
			 * nodes's right sibling is a leaf, then there
			 * are no other nodes in the tree greater than
			 * this node, and still less than the key.
			 * Return this node.
			 */
			if (!x->right
			    || x->right == LEAF
			    || (t->comparator(x->right->key, key) > 0))
				return x;
			else
				x = x->right;
		}
	}
	return NULL;
}

/**
 * \brief Return the smallest sibling greater than or equal
 *
 * \param n	Pointer to the node
 * \return !0	Pointer to the greater sibling
 * \return NULL	The node specified is the max
 */
struct rbn *rbt_least_gt_or_eq(struct rbn *n)
{
	if (n->right)
		return n->right;
	if (n->parent && n->parent->right == n)
		return n->parent;
	return NULL;
}

/**
 * \brief Find the smallest node greater than or equal to a key
 *
 * \param n	Pointer to the node
 * \return !0	Pointer to the greater sibling
 * \return NULL	The node specified is the max
 */
struct rbn *rbt_find_least_gt_or_eq(struct rbt *t, void *key)
{
	struct rbn *x;

	for (x = t->root; x && x != LEAF; ) {
		int c;
		c = t->comparator(x->key, key);
		if (!c)
			return x;

		if (c < 0) {
			x = x->right;
		} else {
			/* This node is greater than the key. If the
			 * node's left sibling is a leaf, then there
			 * are no other nodes in the tree smaller than
			 * this node, and still greater than the key.
			 * Or if there is a left sibling, but this
			 * sibling is smaller than the key, return the
			 * node.
			 */
			if (!x->left
			    || x->left == LEAF
			    || (t->comparator(x->left->key, key) < 0))
				return x;
			else
				x = x->left;
		}
	}
	return NULL;
}

/**
 * \brief Find a node in the RBT that matches a key
 *
 * \param t	Pointer to the RBT.
 * \param key	Pointer to the key.
 * \return	Pointer to the node with the matching key.
 * \return	NULL if no node in the tree matches the key.
 */
struct rbn *rbt_find(struct rbt *t, void *key)
{
	struct rbn *x;

	for (x = t->root; x && x != LEAF; ) {
		int c;
		c = t->comparator(x->key, key);
		if (!c)
			return x;

		if (c > 0)
			x = x->left;
		else
			x = x->right;
	}
	return NULL;
}

/**
 * \brief Return the smallest (i.e leftmost) node in the RBT.
 *
 * \param t	Pointer to the RBT.
 * \return	Pointer to the node or NULL if the tree is empty.
 */
struct rbn *rbt_min(struct rbt *t)
{
	struct rbn *x;
	for (x = t->root; x && x->left != LEAF; x = x->left);
	return x;
}

/**
 * \brief Return the largest (i.e. rightmost) node in the RBT.
 *
 * \param t	Pointer to the RBT.
 * \return	Pointer to the node or NULL if the tree is empty.
 */
struct rbn *rbt_max(struct rbt *t)
{
	struct rbn *x;
	for (x = t->root; x && x->right != LEAF; x = x->right);
	return x;
}

static int rbt_traverse_subtree(struct rbn *n, rbn_node_fn f,
				 void *fn_data, int level)
{
	int rc;
	if (n != LEAF) {
		rc = rbt_traverse_subtree(n->left, f, fn_data, level+1);
		if (rc)
			goto err;
		rc = f(n, fn_data, level);
		if (rc)
			goto err;
		rc = rbt_traverse_subtree(n->right, f, fn_data, level+1);
		if (rc)
			goto err;
	}
	return 0;
 err:
	return rc;
}

/**
 * \brief Traverse an RBT
 *
 * Perform a recursive traversal of an RBT from left to right. For
 * each non-leaf node, a callback function is invoked with a pointer
 * to the node.
 *
 * \param t	A pointer to the RBT.
 * \param f	A pointer to the function to call as each RBT node is
 *		visited.
 * \param p	Pointer to provide as an argument to the callback
 *		function along with the RBT node pointer.
 */
int rbt_traverse(struct rbt *t, rbn_node_fn f, void *p)
{
	if (t->root)
		return rbt_traverse_subtree(t->root, f, p, 0);
	return 0;
}

/**
 * \brief routine to determine if a node is a leaf
 *
 * This function is provided for applications that want to iterate
 * over the RBT themselves. This function returns a non-zero value if
 * the specified node is a leaf.
 *
 * \param n The node to test
 * \return !0 if the node is a leaf
 */
int rbt_is_leaf(struct rbn *n)
{
	return n == LEAF;
}

#ifdef RBT_TEST
#include <stdio.h>
#include <time.h>
#include "ovis-test/test.h"
struct test_key {
	struct rbn n;
	int key;
};

int test_comparator(void *a, void *b)
{
	return *(int *)a - *(int *)b;
}

int rbt_print(struct rbn *rbn, void *fn_data, int level)
{
	struct test_key *k = (struct test_key *)rbn;
	printf("%*d\n", 200 - (level * 3), k->key);
	return 0;
}

int main(int argc, char *argv[])
{
	struct rbt rbt;
	struct rbt rbtB;
	int key_count = atoi(argv[1]);
	int max = -1;
	int min = 0x7FFFFFFF;
	struct test_key key;
	int x;
	time_t t = time(NULL);

	if (argc > 2)
		t = atoi(argv[2]);
	rbt_init(&rbtB, test_comparator);
	for (key_count = 0; key_count < 100; key_count += 2) {
		struct test_key *k = calloc(1, sizeof *k);
		rbn_init(&k->n, &k->key);
		k->key = key_count;
		rbt_ins(&rbtB, &k->n);
	}
	rbt_traverse(&rbtB, rbt_print, NULL);
	for (x = 1; x < 99; x += 2) {
		struct rbn *rbnGT, *rbnLT;
		int a, b;
		rbnLT = rbt_find_greatest_lt_or_eq(&rbtB, &x);
		rbnGT = rbt_find_least_gt_or_eq(&rbtB, &x);
		a = ((struct test_key *)rbnLT)->key;
		b = ((struct test_key *)rbnGT)->key;
		TEST_ASSERT((a <= x <= b),
			    "%d <= %d <= %d\n",
			    a, b);
	}

	for (x = 0; x < 100; x += 2) {
		struct rbn *rbn = rbt_find(&rbtB, &x);
		TEST_ASSERT((rbn != NULL),
			    "%d found.\n",
			    x);
	}
	srandom(t);
	rbt_init(&rbt, test_comparator);
	key_count = atoi(argv[1]);
	while (key_count--) {
		struct test_key *k = calloc(1, sizeof *k);
		struct rbn *rbn;
		rbn_init(&k->n, &k->key);
		k->key = (int)random();
		rbn = rbt_find(&rbt, &k->key);
		if (rbn) {
			printf("FAIL -- DUPLICATE %d.\n", &k->key);
			continue;
		}
		rbt_ins(&rbt, &k->n);
		if (k->key > max)
			max = k->key;
		else if (k->key < min)
			min = k->key;
	}
	rbt_traverse(&rbt, rbt_print, NULL);
	struct rbn *min_rbn = rbt_min(&rbt);
	struct rbn *max_rbn = rbt_max(&rbt);
	TEST_ASSERT((min_rbn && ((struct test_key *)min_rbn)->key == min),
		    "The min (%d) is in the tree.\n", min);
	TEST_ASSERT((max_rbn && ((struct test_key *)max_rbn)->key == max),
		    "The max (%d) is in the tree.\n", max);
	TEST_ASSERT((min < max),
		    "The min (%d) is less than the max (%d).\n",
		    min, max);
	if (min_rbn)
		rbt_del(&rbt, min_rbn);
	TEST_ASSERT((rbt_find(&rbt, &min) == NULL),
		    "Delete %d and make certain it's not found.\n",
		    min);
	if (max_rbn)
		rbt_del(&rbt, max_rbn);
	TEST_ASSERT((rbt_find(&rbt, &max) == NULL),
		    "Delete %d and make certain it's not found.\n", max);
	srandom(t);
	key_count = atoi(argv[1]);
	while (key_count--) {
		struct rbn *rbn;
		x = (int)random();
		rbn = rbt_find(&rbt, &x);
		if (x == min || x == max) {
			TEST_ASSERT((rbn == NULL),
				    "min/max %14d is not found.\n", x);
			continue;
		}
		TEST_ASSERT((rbn != NULL),
			    "%14d is found.\n", x);
		rbt_del(&rbt, rbn);
		rbn = rbt_find(&rbt, &x);
		TEST_ASSERT((rbn == NULL),
			    "%14d is not found.\n", x);
	}
	rbt_traverse(&rbt, rbt_print, NULL);
	printf("FAIL seed is %d.\n", t);
	return 0;
}

#endif
