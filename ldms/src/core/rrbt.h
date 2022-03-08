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
#ifndef _RRBT_T_
#define _RRBT_T_

#include <stddef.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This implements a relocatable red-black-tree. The entire tree can
 * be copied without affecting internal consistency. All pointers are
 * stored as offsets from a base address.
 *
 * A consistent copy can be performed by memcpy'ing the tree to a new
 * address, and then updating the base address pointer in the rrbt
 * structure to the destination base address.
 *
 * Red/Black tree implementation. See
 * http://www.nist.gov/dads/HTML/redblack.html for a definition of
 * this algorithm.
 */

#define RRBN_RED      0
#define RRBN_BLACK    1

/* Relocatable Red/Black Node */
struct rrbn {
	uint64_t color:1;
	uint64_t left:63;	/* struct rrbn */
	uint64_t right; 	/* struct rrbn */
	uint64_t parent;	/* struct rrbn */
	union {
		uint8_t  key[0];
		uint16_t  key_u16[0];
		uint32_t  key_u32[0];
		uint64_t  key_u64[0];
	};
};
#define RRBN_DEF(_name_, _key_size_)			\
	struct rrbn_ ## _name_ {			\
		struct rrbn base;			\
		union {					\
			uint8_t  key[_key_size_];	\
			uint16_t  key_u16[0];		\
			uint32_t  key_u32[0];		\
			uint64_t  key_u64[0];		\
		};					\
	}

#define RRBN(_r_) &((_r_).base)

void rrbn_init(struct rrbn *n, void *key, size_t key_len);

/* Comparator callback provided for insert and search operations */
typedef int (*rrbn_comparator_t)(void *tree_key, const void *key);

/* Processor for each node during traversal. */
typedef int (*rrbn_node_fn)(struct rrbn *, void *, int);

struct rrbt {
	uint64_t root;		/* struct rbn * */
};

typedef struct rrbt_instance {
	rrbn_comparator_t comparator;
	uint64_t *root;
	uint8_t *base;
} *rrbt_t;

#define rrbt_ptr(_type_, _tree_, _off_) (_off_ ? ((_type_ *)(&_tree_->base[(_off_)])) : NULL)
#define rrbn_ptr(_tree_, _off_) rrbt_ptr(struct rrbn, _tree_, _off_)
#define rrbt_off(_tree_, _ptr_) (_ptr_ ? (((uint8_t *)_ptr_) - _tree_->base) : 0)

void rrbt_init(struct rrbt *t);
rrbt_t rrbt_get(struct rrbt_instance *inst, uint64_t *root, void *base, rrbn_comparator_t c);

#define RRBT_INITIALIZER(_c_, _m_) { .comparator = _c_, .base = _m_ }

void rrbt_print(rrbt_t t);
void rrbt_verify(rrbt_t t);
int rrbt_empty(rrbt_t t);
struct rrbn *rrbt_least_gt_or_eq(rrbt_t t, struct rrbn *n);
struct rrbn *rrbt_greatest_lt_or_eq(rrbt_t t, struct rrbn *n);
struct rrbn *rrbt_find_lub(rrbt_t rrbt, const void *key);
struct rrbn *rrbt_find_glb(rrbt_t rrbt, const void *key);
struct rrbn *rrbt_find(rrbt_t t, const void *k);
struct rrbn *rrbt_min(rrbt_t t);
struct rrbn *rrbt_max(rrbt_t t);
struct rrbn *rrbn_succ(rrbt_t t, struct rrbn *n);
struct rrbn *rrbn_pred(rrbt_t t, struct rrbn *n);
void rrbt_ins(rrbt_t t, struct rrbn *n);
void rrbt_del(rrbt_t t, struct rrbn *n);
int rrbt_traverse(rrbt_t t, rrbn_node_fn f, void *fn_data);
int rrbt_is_leaf(struct rrbn *n);
#ifndef offsetof
/* C standard since c89 */
#define offsetof(type,member) ((size_t) &((type *)0)->member)
#endif
#ifndef container_of
#define container_of(ptr, type, member) ({ \
	const __typeof__(((type *)0)->member ) *__mptr = (void *)(ptr); \
	(type *)((char *)__mptr - offsetof(type,member));})
#endif
#define RRBT_FOREACH(rrbn, rrbt) \
	for ((rrbn) = rrbt_min((rrbt)); (rrbn); (rrbn) = rrbn_succ((rrbn)))

#ifdef __cplusplus
}
#endif

#endif
