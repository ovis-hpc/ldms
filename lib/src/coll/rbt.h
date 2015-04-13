/*
 * Copyright (c) 2008-2015 Open Grid Computing, Inc. All rights reserved.
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
#ifndef _RBT_T
#define _RBT_T

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Red/Black tree implementation. See
 * http://www.nist.gov/dads/HTML/redblack.html for a definition of
 * this algorithm.
 */

#define RBN_RED      0
#define RBN_BLACK    1

/* Red/Black Node */
struct rbn {
	struct rbn       *left;
	struct rbn       *right;
	struct rbn       *parent;
	int               color;
	void             *key;
};

/* Sets key on n.  */
void rbn_init(struct rbn *n, void *key);

/* Comparator callback provided for insert and search operations */
typedef int (*rbn_comparator_t)(void *tree_key, const void *key);

/* Processor for each node during traversal. */
typedef int (*rbn_node_fn)(struct rbn *, void *, int);

struct rbt {
	struct rbn       *root;
	rbn_comparator_t comparator;
};

void rbt_init(struct rbt *t, rbn_comparator_t c);
int rbt_empty(struct rbt *t);
struct rbn *rbt_least_gt_or_eq(struct rbn *n);
struct rbn *rbt_greatest_lt_or_eq(struct rbn *n);
struct rbn *rbt_find_lub(struct rbt *rbt, const void *key);
struct rbn *rbt_find_glb(struct rbt *rbt, const void *key);
struct rbn *rbt_find(struct rbt *t, const void *k);
struct rbn *rbt_min(struct rbt *t);
struct rbn *rbt_max(struct rbt *t);
struct rbn *rbn_succ(struct rbn *n);
struct rbn *rbn_pred(struct rbn *n);
void rbt_ins(struct rbt *t, struct rbn *n);
void rbt_del(struct rbt *t, struct rbn *n);
int rbt_traverse(struct rbt *t, rbn_node_fn f, void *fn_data);
int rbt_is_leaf(struct rbn *n);
#ifndef offsetof
/* C standard since c89 */
#define offsetof(type,member) ((size_t) &((type *)0)->member)
#endif
/* from linux kernel */
#define container_of(ptr, type, member) ({ \
	const __typeof__(((type *)0)->member ) *__mptr = (ptr); \
	(type *)((char *)__mptr - offsetof(type,member));})

#ifdef __cplusplus
}
#endif

#endif
