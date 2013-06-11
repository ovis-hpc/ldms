/* -*- c-basic-offset: 8 -*-
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

#ifndef _OGC_RBT_T
#define _OGC_RBT_T

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Red/Black tree implementation. See
 * http://www.nist.gov/dads/HTML/redblack.html for a definition of
 * this algorithm.
 */

#define OGC_RBN_RED      0
#define OGC_RBN_BLACK    1

/* Red/Black Node */
struct ogc_rbn {
    struct ogc_rbn       *left;
    struct ogc_rbn       *right;
    struct ogc_rbn       *parent;
    int                 color;
    void                *key;
};

/* Comparator callback provided for insert and search operations */
typedef int (*ogc_rbn_comparator_t)(void *, void *);

/* Processor for each node during traversal. */
typedef void (*ogc_rbn_node_fn)(struct ogc_rbn *, void *, int);

struct ogc_rbt {
    struct ogc_rbn       *root;
    ogc_rbn_comparator_t comparator;
};

void ogc_rbt_init(struct ogc_rbt *t, ogc_rbn_comparator_t c);
struct ogc_rbn *ogc_rbt_find(struct ogc_rbt *t, void *k);
struct ogc_rbn *ogc_rbt_find_min(struct ogc_rbt *t);
struct ogc_rbn *ogc_rbt_find_max(struct ogc_rbt *t);
void ogc_rbt_ins(struct ogc_rbt *t, struct ogc_rbn *n);
void ogc_rbt_del(struct ogc_rbt *t, struct ogc_rbn *n);
void ogc_rbt_traverse(struct ogc_rbt *t, ogc_rbn_node_fn f, void *fn_data);
int ogc_rbt_is_leaf(struct ogc_rbn *n);
#define ogc_offsetof(type,member) ((size_t) &((type *)0)->member)
#define ogc_container_of(ptr, type, member) ({ \
	const __typeof__(((type *)0)->member ) *__mptr = (ptr); \
	(type *)((char *)__mptr - ogc_offsetof(type,member));})

#ifdef __cplusplus
}
#endif

#endif
