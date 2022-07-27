/*
 * Copyright (c) 2008-2016 Open Grid Computing, Inc. All rights reserved.
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
#include <inttypes.h>
#include <stdlib.h>
#include "rbt.h"
#include <assert.h>
#include <stdio.h>

/* A LEAF (NULL) is considered BLACK */
static int is_red(struct rbn *x)
{
	if (!x)
		return 0;
	return x->color == RBN_RED;
}

static void rotate_left(struct rbt *t, struct rbn *x)
{
	struct rbn *y = x->right;
	struct rbn *parent = x->parent;

	/* Link y's left to x's right and update parent if not a leaf */
	x->right = y->left;
	if (y->left)
		y->left->parent = x;

	/* Attach y to x's parent if x is not the root.
	 * If x == t->root, then x->parent == NULL */
	y->parent = x->parent;
	if (t->root != x) {
		assert(x->parent);
		if (parent->left == x)
			parent->left = y;
		else
			parent->right = y;
	} else {
		assert(x->parent == NULL);
		t->root = y;
		y->parent = NULL;
	}

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
	if (y->right)
		y->right->parent = x;

	/* Attach y to x's parent */
	if (t->root != x) {
		y->parent = parent;
		if (parent->right == x)
			parent->right = y;
		else
			parent->left = y;
	} else {
		t->root = y;
		y->parent = NULL;
	}

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
	t->card = 0;
}

/**
 * \brief Returns TRUE if the tree is empty.
 *
 * \param t	Pointer to the rbt.
 * \retval 0	The tree is not empty
 * \retval 1	The tree is empty
 */
int rbt_empty(struct rbt *t)
{
	return (t->root == NULL);
}

/**
 * \brief Return the number of entries in the tree
 * \returns The cardinality of the tree
 */
long rbt_card(struct rbt *t)
{
	return t->card;
}

/**
 * \brief Initialize an RBN node.
 *
 * Initialize an RBN node. This is a convenience function to avoid
 * having the application know about the internals of the RBN while
 * still allowing the RBN to be embedded in the applications object
 * and avoiding a second allocation in rbn_ins.
 *
 * \param n The RBN to initialize
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

	t->card += 1;

	/* Initialize new node */
	x->left = x->right = NULL;

	/* Trivial root insertion */
	if (!t->root) {
		x->color = RBN_BLACK;
		t->root = x;
		x->parent = NULL;
		return;
	}

	/* Always insert a RED node */
	x->color = RBN_RED;
	for (n = t->root; n; ) {
		parent = n;
		c = t->comparator(n->key, x->key);
		if (c > 0)
			n = n->left;
		else
			n = n->right;
	}
	/* Replace leaf with new node */
	assert(parent);
	x->parent = parent;
	if (c > 0)
		parent->left = x;
	else
		parent->right = x;

	/*
	 * While x is not the root and x's parent is red. Note that if x's
	 * parent is RED, then x's parent is also not the root
	 */
	while (x != t->root && is_red(x->parent)) {
		struct rbn *uncle;
		if (x->parent == x->parent->parent->left) {
			uncle = x->parent->parent->right;
			if (is_red(uncle)) {
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
			if (is_red(uncle)) {
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
				if (x->parent->parent) {
					x->parent->parent->color = RBN_RED;
					rotate_left(t, x->parent->parent);
				}
			}
		}
	}
	t->root->color = RBN_BLACK;
}

static inline struct rbn *find_pred(struct rbn *n)
{
	struct rbn *x;
	for (x = n->left; x->right; x = x->right);
	return x;
}

static inline struct rbn *find_succ(struct rbn *n)
{
	struct rbn *x;
	for (x = n->right; x->left; x = x->left);
	return x;
}

/**
 * \brief Delete a node from the RBT.
 *
 * \param t	Pointer to the RBT.
 * \param z	Pointer to the node to delete.
 */
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
 * \param t	Pointer to the tree
 * \param key   Pointer to the key value
 * \return !0	Pointer to the lesser sibling
 * \return NULL	The node specified is the min
 */
struct rbn *rbt_find_glb(struct rbt *t, const void *key)
{
	struct rbn *x;
	struct rbn *glb = NULL;

	for (x = t->root; x; ) {
		int c;
		c = t->comparator(x->key, key);
		if (!c)
			return x;
		if (c > 0) {
			x = x->left;
		} else {
			glb = x;
			x = x->right;
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
 * \param t	Pointer to the tree
 * \param key	Pointer to the key
 * \retval !0	Pointer to the greater sibling
 * \retval NULL	The node specified is the max
 */
struct rbn *rbt_find_lub(struct rbt *t, const void *key)
{
	struct rbn *x;
	struct rbn *lub = NULL;

	for (x = t->root; x; ) {
		int c;
		c = t->comparator(x->key, key);
		if (!c)
			return x;
		if (c < 0) {
			x = x->right;
		} else {
			lub = x;
			x = x->left;
		}
	}
	return lub;
}

/**
 * \brief Find a node in the RBT that matches a key
 *
 * \param t	Pointer to the RBT.
 * \param key	Pointer to the key.
 * \retval !NULL Pointer to the node with the matching key.
 * \retval NULL  No node in the tree matches the key.
 */
struct rbn *rbt_find(struct rbt *t, const void *key)
{
	struct rbn *x;

	for (x = t->root; x; ) {
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

static struct rbn *__rbn_min(struct rbn *n)
{
	for (; n && n->left; n = n->left);
	return n;
}

static struct rbn *__rbn_max(struct rbn *n)
{
	for (; n && n->right; n = n->right);
	return n;
}

/**
 * \brief Return the smallest (i.e leftmost) node in the RBT.
 *
 * \param t	Pointer to the RBT.
 * \return	Pointer to the node or NULL if the tree is empty.
 */
struct rbn *rbt_min(struct rbt *t)
{
	return __rbn_min(t->root);
}

/**
 * \brief Return the largest (i.e. rightmost) node in the RBT.
 *
 * \param t	Pointer to the RBT.
 * \return	Pointer to the node or NULL if the tree is empty.
 */
struct rbn *rbt_max(struct rbt *t)
{
	return __rbn_max(t->root);
}

static int rbt_traverse_subtree(struct rbn *n, rbn_node_fn f,
				 void *fn_data, int level)
{
	int rc;
	if (n) {
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
 * \brief Return the successor node
 *
 * Given a node in the tree, return it's successor.
 *
 * \param n	Pointer to the current node
 */
struct rbn *rbn_succ(struct rbn *n)
{
	if (n->right)
		return __rbn_min(n->right);

	if (n->parent) {
		while (n->parent && n == n->parent->right)
			n = n->parent;
		return n->parent;
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
struct rbn *rbn_pred(struct rbn *n)
{
	if (n->left)
		return __rbn_max(n->left);

	if (n->parent) {
		while (n->parent && n == n->parent->left)
			n = n->parent;
		return n->parent;
	}

	return NULL;
}

static struct rbn *sibling(struct rbn *n)
{
	assert (n != NULL);
	assert (n->parent != NULL); /* Root node has no sibling */
	if (n == n->parent->left)
		return n->parent->right;
	else
		return n->parent->left;
}

static void replace_node(struct rbt *t, struct rbn *oldn, struct rbn *newn)
{
	if (oldn->parent == NULL) {
		t->root = newn;
		newn->parent = NULL;
	} else {
		if (oldn == oldn->parent->left)
			oldn->parent->left = newn;
		else
			oldn->parent->right = newn;
	}
	if (newn != NULL) {
		newn->parent = oldn->parent;
	}
}

static int node_color(struct rbn *n) {
	return n == NULL ? RBN_BLACK : n->color;
}

/*
 * 1. Each node is either red or black:
 */
static void verify_property_1(struct rbn *n) {
	assert(node_color(n) == RBN_RED || node_color(n) == RBN_BLACK);
	if (n == NULL) return;
	verify_property_1(n->left);
	verify_property_1(n->right);
}

/*
 * 2. The root node is black.
 */
static void verify_property_2(struct rbn *root) {
	assert(node_color(root) == RBN_BLACK);
}

/*
* 3. All NULL leaves are black and contain no data.
* This property is assured by always treating NULL as black.
*/

/*
 * 4. Every red node has two children, and both are black (or
 * equivalently, the parent of every red node is black).
 */
static void verify_property_4(struct rbn * n)
{
	if (node_color(n) == RBN_RED) {
		assert (node_color(n->left)   == RBN_BLACK);
		assert (node_color(n->right)  == RBN_BLACK);
		assert (node_color(n->parent) == RBN_BLACK);
	}
	if (n == NULL) return;
	verify_property_4(n->left);
	verify_property_4(n->right);
}

/*
 * 5. All paths from any given node to its leaf nodes contain the same
 * number of black nodes. Traverse the tree counting black nodes until
 * a leaf is reached; save this count. Compare this saved count to all
 * other paths to a leaf.
 */
static void verify_property_5_helper(struct rbn * n, int black_count, int* path_black_count)
{
	if (node_color(n) == RBN_BLACK) {
		black_count++;
	}
	if (n == NULL) {
		if (*path_black_count == -1) {
			*path_black_count = black_count;
		} else {
			assert (black_count == *path_black_count);
		}
		return;
	}
	verify_property_5_helper(n->left,  black_count, path_black_count);
	verify_property_5_helper(n->right, black_count, path_black_count);
}

static void verify_property_5(struct rbn *n)
{
	int black_count_path = -1;
	verify_property_5_helper(n, 0, &black_count_path);
}

static void verify_tree(struct rbt *t) {
	verify_property_1(t->root);
	verify_property_2(t->root);
	/* Property 3 is implicit */
	verify_property_4(t->root);
	verify_property_5(t->root);
}

/**
 * \brief verify that the tree is correct
 */
void rbt_verify(struct rbt *t)
{
	verify_tree(t);
}

static int __rbn_print(struct rbn *rbn, void *fn_data, int level)
{
	printf("%p %*c%-2d: %ld\n", rbn, 80 - (level * 6), (rbn->color?'B':'R'),
	       level, *((int64_t *)rbn->key));
	return 0;
}

void rbt_print(struct rbt *t)
{
	if (0 == t->card)
		printf("EMPTY\n");
	rbt_traverse(t, __rbn_print, NULL);
}

static void delete_case1(struct rbt *t, struct rbn *n);
static void delete_case2(struct rbt * t, struct rbn *n);
static void delete_case3(struct rbt *t, struct rbn *n);
static void delete_case4(struct rbt *t, struct rbn *n);
static void delete_case5(struct rbt *t, struct rbn *n);
static void delete_case6(struct rbt *t, struct rbn *n);

void rbt_del(struct rbt *t, struct rbn * n)
{
	struct rbn *pred;
	struct rbn *child;
	assert(n);
	assert(t->card);
	t->card -= 1;
	if (n->left != NULL && n->right != NULL) {
		/*
		 * Swap n with it's predecessor's location in the
		 * tree, and then delete 'n'. The simplifies the
		 * delete to the case where n->right is NULL.
		 */
		pred = find_pred(n);
		struct rbn P = *pred;

		assert(pred->right == NULL);
		assert(pred->parent != NULL);

		/* Move pred to n's location */
		if (pred->parent != n) {
			/* n's predecessor is max of left subtree */
			/*
			 *            R           R
			 *             \           \
			 *		N           Pr
			 *             / \         / \
			 *	      L   R  ==>  L   R
			 *           / \         / \
			 *          X   Pr      X   N
			 *             /           /
			 *            Y           Y
			 */
			pred->left = n->left;
			pred->right = n->right;
			pred->parent = n->parent;
			pred->color = n->color;
			n->color = P.color;
			n->left->parent = n->right->parent = pred;
			if (n->parent) {
				if (n->parent->left == n)
					n->parent->left = pred;
				else
					n->parent->right = pred;
			} else {
				t->root = pred;
			}

			if (node_color(n) == RBN_RED) {
				/*
				 * Removing a red node does not
				 * require rebalancing
				 */
				P.parent->right = P.left;
				return;
			}
			/* Move n to pred's location */
			n->left = P.left;
			n->right = NULL;
			n->parent = P.parent;
			if (P.left)
				P.left->parent = n;
			assert(P.parent->right == pred);
			P.parent->right = n;
		} else {
			/* n's predecessor is its left child */
			/*
			 *            R           R
			 *             \           \
			 *		N           Pr
			 *             / \         / \
			 *	      Pr  R  ==>  N   R
			 *           / \         /
			 *          X   -       X
			 */
			/* pred becomes 'n' */
			pred->color = n->color;
			n->color = P.color;
			pred->parent = n->parent;
			pred->right = n->right;
			n->right->parent = pred;

			/* Attach P to parent */
			if (n->parent) {
				if (n == n->parent->left)
					n->parent->left = pred;
				else
					n->parent->right = pred;
				pred->parent = n->parent;
			} else {
				t->root = pred;
				pred->parent = NULL;
			}

			if (node_color(n) == RBN_RED) {
				/* n->left == pred. pred->left still
				 * poits to it's old left, which is
				 * correct when we're deleting 'n'.
				 * Deleting RED, no rebalance */
				return;
			}

			/* Attach 'n' to pred->left */
			n->left = P.left;
			if (P.left)
				P.left->parent = n;
			pred->left = n;
			n->parent = pred;
			n->right = NULL;
			n->parent = pred;
		}
	}

	assert(n->left == NULL || n->right == NULL);
	child = n->right == NULL ? n->left : n->right;
	struct rbn *delete_me = n;

	/* If 'n' is RED, we don't need to rebalance, just remove it. */
	if (node_color(n) == RBN_RED) {
		replace_node(t, n, child);
		return;
	}

	n->color = node_color(child);

	/*
	 * If n is the root, just remove it since it will change the
	 * number of black nodes on every path to a leaf and not
	 * require rebalancing the tree.
	 */
	if (n->parent == NULL) {
		t->root = child;
		if (child) {
			child->color = RBN_BLACK;
			child->parent = NULL;
		}
		return;
	}

#ifndef __unroll_tail_recursion__
	delete_case2(t, n);
#else
 case_1:
	if (n->parent == NULL)
		return;
	/* Case 2: */
	struct rbn *s = sibling(n);
	struct rbn *p = n->parent;
	if (node_color(s) == RBN_RED) {
		p->color = RBN_RED;
		s->color = RBN_BLACK;
		if (n == p->left)
			rotate_left(t, p);
		else
			rotate_right(t, p);
		s = sibling(n);
		p = n->parent;
	}
	/* Case 3: */
	if (node_color(p) == RBN_BLACK &&
	    node_color(s) == RBN_BLACK &&
	    node_color(s->left) == RBN_BLACK &&
	    node_color(s->right) == RBN_BLACK) {
		s->color = RBN_RED;
		n = p;
		goto case_1;
	}
	/* Case 4: */
	if (node_color(p) == RBN_RED &&
	    node_color(s) == RBN_BLACK &&
	    node_color(s->left) == RBN_BLACK &&
	    node_color(s->right) == RBN_BLACK) {
		s->color = RBN_RED;
		p->color = RBN_BLACK;
		goto delete;
	}
	/* Case 5 */
	if (n == p->left &&
	    node_color(s) == RBN_BLACK &&
	    node_color(s->left) == RBN_RED &&
	    node_color(s->right) == RBN_BLACK) {
		s->color = RBN_RED;
		s->left->color = RBN_BLACK;
		rotate_right(t, s);
	} else if (n == p->right &&
		 node_color(s) == RBN_BLACK &&
		 node_color(s->right) == RBN_RED &&
		 node_color(s->left) == RBN_BLACK) {
		s->color = RBN_RED;
		s->right->color = RBN_BLACK;
		rotate_left(t, s);
	}
	/* Case 6 */
	s->color = p->color;
	p->color = RBN_BLACK;
	if (n == p->left) {
		assert (node_color(s->right) == RBN_RED);
		s->right->color = RBN_BLACK;
		rotate_left(t, p);
	} else {
		assert (node_color(s->left) == RBN_RED);
		s->left->color = RBN_BLACK;
		rotate_right(t, p);
	}
delete:
#endif
	replace_node(t, delete_me, child);
	if (n->parent == NULL && child != NULL) // root should be black
		child->color = RBN_BLACK;
}

static void delete_case1(struct rbt * t, struct rbn *n)
{
	if (n->parent == NULL)
		return;
	delete_case2(t, n);
}

/*
 * Case 2: N's sibling is RED
 *
 * In this case we exchange the colors of the parent and sibling, then
 * rotate about the parent so that the sibling becomes the parent of
 * its former parent. This does not restore the tree properties, but
 * reduces the problem to one of the remaining cases.
 */
static void delete_case2(struct rbt * t, struct rbn *n)
{
	struct rbn *s = sibling(n);
	struct rbn *p = n->parent;
	if (node_color(s) == RBN_RED) {
		p->color = RBN_RED;
		s->color = RBN_BLACK;
		if (n == p->left)
			rotate_left(t, p);
		else
			rotate_right(t, p);
	}
	delete_case3(t, n);
}

/*
 * Case 3: N's parent, sibling, and sibling's children are black.
 *
 * Paint the sibling red. Now all paths passing through N's parent
 * have one less black node than before the deletion, so we must
 * recursively run this procedure from case 1 on N's parent.
 */
static void delete_case3(struct rbt *t, struct rbn *n)
{
	struct rbn *s = sibling(n);
	struct rbn *p = n->parent;
	assert(s);
	if (node_color(p) == RBN_BLACK &&
	    node_color(s) == RBN_BLACK &&
	    node_color(s->left) == RBN_BLACK &&
	    node_color(s->right) == RBN_BLACK) {
		s->color = RBN_RED;
		delete_case1(t, p);
	} else {
		delete_case4(t, n);
	}
}

/*
 * Case 4: N's sibling and sibling's children are black, but its parent is red
 *
 * Exchange the colors of the sibling and parent; this restores the
 * tree properties.
 */
static void delete_case4(struct rbt *t, struct rbn *n)
{
	struct rbn *s = sibling(n);
	struct rbn *p = n->parent;
	assert(s);
	if (node_color(p) == RBN_RED &&
	    node_color(s) == RBN_BLACK &&
	    node_color(s->left) == RBN_BLACK &&
	    node_color(s->right) == RBN_BLACK) {
		s->color = RBN_RED;
		p->color = RBN_BLACK;
	} else {
		delete_case5(t, n);
	}
}

/*
 * Case 5: There are two cases handled here which are mirror images of
 * one another:
 *
 * N's sibling S is black, S's left child is red, S's right child is
 * black, and N is the left child of its parent. We exchange the
 * colors of S and its left sibling and rotate right at S.  N's
 * sibling S is black, S's right child is red, S's left child is
 * black, and N is the right child of its parent. We exchange the
 * colors of S and its right sibling and rotate left at S.
 *
 * Both of these function to reduce us to the situation described in case 6.
 */
static void delete_case5(struct rbt * t, struct rbn *n)
{
	struct rbn *s = sibling(n);
	struct rbn *p = n->parent;
	assert(s);
	if (n == p->left &&
	    node_color(s) == RBN_BLACK &&
	    node_color(s->left) == RBN_RED &&
	    node_color(s->right) == RBN_BLACK) {
		s->color = RBN_RED;
		s->left->color = RBN_BLACK;
		rotate_right(t, s);
	} else if (n == p->right &&
		 node_color(s) == RBN_BLACK &&
		 node_color(s->right) == RBN_RED &&
		 node_color(s->left) == RBN_BLACK) {
		s->color = RBN_RED;
		s->right->color = RBN_BLACK;
		rotate_left(t, s);
	}
	delete_case6(t, n);
}

/*
 * Case 6: There are two cases handled here which are mirror images of
 * one another:
 *
 * N's sibling S is black, S's right child is red, and N is the left
 * child of its parent. We exchange the colors of N's parent and
 * sibling, make S's right child black, then rotate left at N's
 * parent.  N's sibling S is black, S's left child is red, and N is
 * the right child of its parent. We exchange the colors of N's
 * parent and sibling, make S's left child black, then rotate right
 * at N's parent.
 *
 * This accomplishes three things at once:
 *
 * - We add a black node to all paths through N, either by adding a
 * -   black S to those paths or by recoloring N's parent black.  We
 * -   remove a black node from all paths through S's red child,
 * -   either by removing P from those paths or by recoloring S.  We
 * -   recolor S's red child black, adding a black node back to all
 * -   paths through S's red child.
 *
 * S's left child has become a child of N's parent during the rotation and so is unaffected.
 */
static void delete_case6(struct rbt *t, struct rbn *n)
{
	struct rbn *s = sibling(n);
	struct rbn *p = n->parent;
	assert(s);
	s->color = p->color;
	p->color = RBN_BLACK;
	if (n == p->left) {
		assert (node_color(s->right) == RBN_RED);
		s->right->color = RBN_BLACK;
		rotate_left(t, p);
	} else {
		assert (node_color(s->left) == RBN_RED);
		s->left->color = RBN_BLACK;
		rotate_right(t, p);
	}
}

#ifdef RBT_TEST
#include <inttypes.h>
#include <time.h>
#include "ovis-test/test.h"
struct test_key {
	struct rbn n;
	int64_t key;
	int ord;
};

int test_comparator(void *a, const void *b)
{
	int64_t ai = *(int64_t *)a;
	int64_t bi = *(int64_t *)b;
	if (ai < bi)
		return -1;
	if (ai > bi)
		return 1;
	return 0;
}

int rbn_print(struct rbn *rbn, void *fn_data, int level)
{
	struct test_key *k = (struct test_key *)rbn;
	printf("%p %*c%-2d: %12" PRId64" (%d)\n",
		rbn,
		80 - (level * 6),
		(rbn->color?'B':'R'),
		level,
		k->key,
		k->ord);
	return 0;
}

int main(int argc, char *argv[])
{
	struct rbt rbt;
	struct rbt rbtB;
	int key_count, iter_count;
	int max = -1;
	int min = 0x7FFFFFFF;
	int x;
	int64_t kv;
	time_t t = time(NULL);
	struct test_key** keys;

	if (argc != 3) {
		printf("usage: ./rbt {key-count} {iter-count}\n");
		exit(1);
	}
	key_count = atoi(argv[1]);
	iter_count = atoi(argv[2]);
	keys = calloc(key_count, sizeof(struct test_key*));

	rbt_init(&rbtB, test_comparator);

	/*
	 * Test Duplicates
	 */
	for (x = 0; x < key_count; x++) {
		struct test_key *k = calloc(1, sizeof *k);
		k->ord = x;
		k->key = 1000;
		rbn_init(&k->n, &k->key);
		rbt_ins(&rbtB, &k->n);
		keys[x] = k;
	}
	struct rbn *n;
	kv = 1000;
	for (n = rbt_min(&rbtB); n; n = rbn_succ(n)) {
		struct test_key *k = container_of(n, struct test_key, n);
		TEST_ASSERT(k->key == kv, "k->key(%ld) == %ld\n", k->key, kv);
	}
	kv = 1000;
	for (n = rbt_min(&rbtB); n; n = rbn_succ(n)) {
		struct test_key *k = container_of(n, struct test_key, n);
		TEST_ASSERT(k->key == kv, "k->key(%ld) == %ld\n", k->key, kv);
	}

	rbt_verify(&rbtB);
	for (x = 0; x < key_count; x++) {
		struct test_key *k = keys[x];
		rbt_del(&rbtB, &k->n);
	}
	TEST_ASSERT(rbtB.card == 0, "Tree is empty\n");

	/*
	 * Test LUB/GLB
	 */
	int64_t test_keys[] = { 1, 3, 5, 7, 9 };
	int i;
	struct test_key *k;
	for (x = 0; x < 100; ) {
		for (i = 0; i < sizeof(test_keys) / sizeof(test_keys[0]); i++) {
			k = calloc(1, sizeof *k);
			k->ord = x++;
			rbn_init(&k->n, &k->key);
			k->key = test_keys[i];
			rbt_ins(&rbtB, &k->n);
		}
	}

	kv = 0;
	n = rbt_find_glb(&rbtB, &kv);
	TEST_ASSERT(n == NULL, "glb(0) == NULL\n");
	for (i = 0; i < sizeof(test_keys) / sizeof(test_keys[0]); i++) {
		n = rbt_find_glb(&rbtB, &test_keys[i]);
		k = container_of(n, struct test_key, n);
		TEST_ASSERT(k->key == test_keys[i], "glb(%ld) == %ld\n", k->key, test_keys[i]);

		kv = test_keys[i] + 1;
		n = rbt_find_glb(&rbtB, &kv);
		k = container_of(n, struct test_key, n);
		TEST_ASSERT(k->key == test_keys[i], "glb(%ld) == %ld\n", k->key, test_keys[i]);
	}
	kv = 10;
	n = rbt_find_lub(&rbtB, &kv);
	TEST_ASSERT(n == NULL, "lub(10) == NULL\n");

	/* Empty the tree */
	for (n = rbt_min(&rbtB); n; n = rbt_min(&rbtB)) {
		k = container_of(n, struct test_key, n);
		rbt_del(&rbtB, n);
		free(k);
	}
	for (i = 0; i < 100; i++) {
		k = calloc(1, sizeof(*k));
		k->ord = x++;
		k->key = i;
		rbn_init(&k->n, &k->key);
		rbt_ins(&rbtB, &k->n);
	}
	for (x = 0; x < 100; x += 2) {
		kv = x;
		struct rbn *rbn = rbt_find(&rbtB, &kv);
		TEST_ASSERT((rbn != NULL), "%ld found.\n", kv);
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
			printf("FAIL -- DUPLICATE %" PRId64 ".\n", k->key);
			continue;
		}
		rbt_ins(&rbt, &k->n);
		if (k->key > max)
			max = k->key;
		else if (k->key < min)
			min = k->key;
	}
	// rbt_traverse(&rbt, rbn_print, NULL);
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
	int test_iter = 0;
	while (test_iter < iter_count) {
		rbt_init(&rbt, test_comparator);
		/* Generate batch of random keys */
		srandom(time(NULL));
		key_count = atoi(argv[1]);
		for (x = 0; x < key_count; x++) {
			keys[x]->key = (int)random();
			keys[x]->ord = x;
			rbn_init(&keys[x]->n, &keys[x]->key);
			rbt_ins(&rbt, &keys[x]->n);
		}
		verify_tree(&rbt);
		printf("Created %d keys.\n", key_count);

		/* Test that all the inserted keys are present */
		for (x = 0; x < key_count; x++) {
			struct rbn *rbn = rbt_find(&rbt, &keys[x]->key);
			if (!rbn) {
				TEST_ASSERT(rbn != NULL,
					    "Key[%d] ==  %ld is not present in the tree\n",
					    x, keys[x]->key);
			}
		}
		/* Now delete them all */
		for (x = 0; x < key_count; x++) {
			int y;
			struct rbn *rbn;
			/* Ensure that the remaining keys are still present */
			for (y = x; y < key_count; y++) {
				struct rbn *rbn = rbt_find(&rbt, &keys[y]->key);
				if (!rbn) {
					TEST_ASSERT(rbn != NULL,
						    "Key[%d,%d] ==  %ld is not present "
						    "in the tree\n",
						    x, y, keys[y]->key);
				}
			}
			rbn = rbt_find(&rbt, &keys[x]->key);
			if (rbn) {
				rbt_del(&rbt, rbn);
			} else {
				TEST_ASSERT(rbn != NULL,
					    "Key[%d] ==  %ld is not present in the tree\n",
					    x, keys[x]->key);
			}
			verify_tree(&rbt);
			/* Ensure that the remaining keys are still present */
			for (y = x+1; y < key_count; y++) {
				struct rbn *rbn = rbt_find(&rbt, &keys[y]->key);
				if (!rbn) {
					TEST_ASSERT(rbn != NULL,
						    "Key[%d,%d] ==  %ld is not present in the tree\n",
						    x, y, keys[y]->key);
				}
			}
		}
		rbt_traverse(&rbt, rbn_print, NULL);
		test_iter += 1;
		printf("test iteration %d\n", test_iter);
	}
	return 0;
}
#endif
