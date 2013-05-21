/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */
#include <stdlib.h>
#include "ogc_rbt.h"

struct ogc_rbn _LEAF = {
	.left = &_LEAF,
	.right = &_LEAF,
	.parent = &_LEAF,
	.color = OGC_RBN_BLACK,
};
static struct ogc_rbn *LEAF = &_LEAF;

static void rotate_left(struct ogc_rbt *t, struct ogc_rbn *x)
{
	struct ogc_rbn *y = x->right;
	struct ogc_rbn *parent = x->parent;

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

static void rotate_right(struct ogc_rbt *t, struct ogc_rbn *x)
{
	struct ogc_rbn *y = x->left;
	struct ogc_rbn *parent = x->parent;

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
void ogc_rbt_init(struct ogc_rbt *t, ogc_rbn_comparator_t c)
{
	t->root = NULL;
	t->comparator = c;
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
void ogc_rbt_ins(struct ogc_rbt *t, struct ogc_rbn *x)
{
	struct ogc_rbn *parent = NULL;
	struct ogc_rbn *n;
	int c = 0;

	/* Initialize new node */
	x->left = x->right = LEAF;

	/* Trivial root insertion */
	if (!t->root) {
		x->parent = NULL;
		x->color = OGC_RBN_BLACK;
		t->root = x;
		return;
	}

	/* Always insert a RED node */
	x->color = OGC_RBN_RED;
	for (n = t->root; n != LEAF; ) {
		parent = n;
		c = t->comparator(x->key, n->key);
		if (c < 0)
			n = n->left;
		else
			n = n->right;
	}
	/* Replace leaf with new node */
	x->parent = parent;
	if (c < 0)
		parent->left = x;
	else
		parent->right = x;

	/*
	 * While x is not the root and x's parent is red. Note that if x's
	 * parent is RED, then x's parent is also not the root
	 */
	while (x != t->root && x->parent->color == OGC_RBN_RED) {
		struct ogc_rbn *uncle;
		if (x->parent == x->parent->parent->left) {
			uncle = x->parent->parent->right;
			if (uncle->color == OGC_RBN_RED) {
				x->parent->color = OGC_RBN_BLACK;
				uncle->color = OGC_RBN_BLACK;
				x->parent->parent->color = OGC_RBN_RED;
				x = x->parent->parent;
			} else {
				if (x == x->parent->right) {
					x = x->parent;
					rotate_left(t, x);
				}
				x->parent->color = OGC_RBN_BLACK;
				x->parent->parent->color = OGC_RBN_RED;
				rotate_right(t, x->parent->parent);
			}
		} else {
			uncle = x->parent->parent->left;
			if (uncle->color == OGC_RBN_RED) {
				x->parent->color = OGC_RBN_BLACK;
				uncle->color = OGC_RBN_BLACK;
				x->parent->parent->color = OGC_RBN_RED;
				x = x->parent->parent;
			} else {
				if (x == x->parent->left) {
					x = x->parent;
					rotate_right(t, x);
				}
				x->parent->color = OGC_RBN_BLACK;
				x->parent->parent->color = OGC_RBN_RED;
				rotate_left(t, x->parent->parent);
			}
		}
	}
	t->root->color = OGC_RBN_BLACK;
}

/**
 * \brief Delete a node from the RBT.
 *
 * \param t	Pointer to the RBT.
 * \param z	Pointer to the node to delete.
 */
void ogc_rbt_del(struct ogc_rbt *t, struct ogc_rbn *z)
{
	struct ogc_rbn *y = z;
	struct ogc_rbn *x;
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
	if (del_color == OGC_RBN_RED)
		return;

	/* Now recolor and balance the tree */
	while (x != t->root && x->color == OGC_RBN_BLACK) {
		if (x == x->parent->left) {
			y = x->parent->right;
			if (y->color == OGC_RBN_RED) {
				y->color = OGC_RBN_BLACK;
				x->parent->color = OGC_RBN_RED;
				rotate_left(t, x->parent);
				y = x->parent->right;
			}
			if (y->left->color == OGC_RBN_BLACK &&
			    y->right->color == OGC_RBN_BLACK) {
				y->color = OGC_RBN_RED;
				x = x->parent;
			} else {
				if (y->right->color == OGC_RBN_BLACK) {
					y->left->color = OGC_RBN_BLACK;
					y->color = OGC_RBN_RED;
					rotate_right(t, y);
					y = x->parent->right;
				}
				y->color = x->parent->color;
				x->parent->color = OGC_RBN_BLACK;
				y->right->color = OGC_RBN_BLACK;
				rotate_left(t, x->parent);
				x = t->root;
			}
		} else {                /* x == x->parent->right */
			y = x->parent->left;
			if (y->color == OGC_RBN_RED) {
				y->color = OGC_RBN_BLACK;
				x->parent->color = OGC_RBN_RED;
				rotate_right(t, x->parent);
				y = x->parent->left;
			}
			if (y->right->color == OGC_RBN_BLACK &&
			    y->left->color == OGC_RBN_BLACK) {
				y->color = OGC_RBN_RED;
				x = x->parent;
			} else {
				if (y->left->color == OGC_RBN_BLACK) {
					y->right->color = OGC_RBN_BLACK;
					y->color = OGC_RBN_RED;
					rotate_left(t, y);
					y = x->parent->left;
				}
				y->color = x->parent->color;
				x->parent->color = OGC_RBN_BLACK;
				y->left->color = OGC_RBN_BLACK;
				rotate_right(t, x->parent);
				x = t->root;
			}
		}
	}
	x->color = OGC_RBN_BLACK;
}

/**
 * \brief Find a node in the RBT that matches a key
 *
 * \param t	Pointer to the RBT.
 * \param key	Pointer to the key.
 * \return	Pointer to the node with the matching key.
 * \return	NULL if no node in the tree matches the key.
 */
struct ogc_rbn *ogc_rbt_find(struct ogc_rbt *t, void *key)
{
	struct ogc_rbn *x;

	for (x = t->root; x && x != LEAF; ) {
		int c;
		c = t->comparator(key, x->key);
		if (!c)
			return x;

		if (c < 0)
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
struct ogc_rbn *ogc_rbt_find_min(struct ogc_rbt *t)
{
	struct ogc_rbn *x;
	for (x = t->root; x && x->left != LEAF; x = x->left);
	return x;
}

/**
 * \brief Return the largest (i.e. rightmost) node in the RBT.
 *
 * \param t	Pointer to the RBT.
 * \return	Pointer to the node or NULL if the tree is empty.
 */
struct ogc_rbn *ogc_rbt_find_max(struct ogc_rbt *t)
{
	struct ogc_rbn *x;
	for (x = t->root; x && x->right != LEAF; x = x->right);
	return x;
}

static void ogc_rbt_traverse_subtree(struct ogc_rbn *n, ogc_rbn_node_fn f, void *fn_data, int level)
{
	if (n != LEAF) {
		ogc_rbt_traverse_subtree(n->right, f, fn_data, level+1);
		f(n, fn_data, level);
		ogc_rbt_traverse_subtree(n->left, f, fn_data, level+1);
	}
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
void ogc_rbt_traverse(struct ogc_rbt *t, ogc_rbn_node_fn f, void *p)
{
	if (t->root)
		ogc_rbt_traverse_subtree(t->root, f, p, 0);
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
int ogc_rbt_is_leaf(struct ogc_rbn *n)
{
	return n == LEAF;
}
