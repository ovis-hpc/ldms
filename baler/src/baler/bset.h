/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
/**
 * \file bset.h
 * \author Narate Taerat (narate@ogc.us)
 *
 * \defgroup bset Baler's Set Utility
 * \{
 * \brief Baler's Set Utility, implemented using hash + linked list.
 * The objects in this file are all in-memory.
 */
#ifndef __BSET_H
#define __BSET_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/queue.h>

/** Default hash table size (it is a prime number). */
#define BSET_DEFAULT_HSIZE 4999

/** List head */
LIST_HEAD(blist_u32_head, blist_u32);

/**
 * List of uint32_t
 */
struct blist_u32 {
	uint32_t data;
	LIST_ENTRY(blist_u32) link;
};

/**
 * Set of uint32_t.
 */
struct bset_u32 {
	uint32_t hsize; /**< Hash size. */
	uint32_t count; /**< Track the number of elements. */
	struct blist_u32_head *hash; /**< Hash table (to linked list). */
};

/**
 * Iterator for ::bset_u32
 */
struct bset_u32_iter {
	struct bset_u32 *set; /**< Reference to the set */
	int next_idx; /**< next index of set->hash */
	struct blist_u32 *elem; /**< Current element of the current index */
};

/**
 * Create an iterator for the set \c set.
 * \param set The set handle.
 * \return A pointer to ::bset_u32_iter on succes.
 * \return NULL on error.
 */
struct bset_u32_iter* bset_u32_iter_new(struct bset_u32 *set);

/**
 * Set \c *out with value of next element, and step the \c iter forward.
 * \param iter The iterator.
 * \param[out] out The output parameter.
 * \return 0 on success.
 * \return \c ENOENT if no more elements.
 * \return Error code on error
 */
int bset_u32_iter_next(struct bset_u32_iter *iter, uint32_t *out);

/**
 * Reset the iterator \c iter.
 * \param iter The iterator handle.
 * \return 0 on success.
 * \return Error number on error.
 */
int bset_u32_iter_reset(struct bset_u32_iter *iter);

/**
 * Allocation function for ::bset_u32.
 */
struct bset_u32* bset_u32_alloc(int hsize);

/**
 * Initialize \a set. This is used in the case that ::bset_u32 is allocated
 * without calling ::bset_u32_alloc().
 * \return -1 on error.
 * \return 0 on success.
 */
int bset_u32_init(struct bset_u32 *set, int hsize);

/**
 * Clear and free ONLY the contents inside the \a set, but NOT
 * freeing the \a set itself. This function complements ::bset_u32_init().
 * \param set The set to be cleared.
 */
void bset_u32_clear(struct bset_u32 *set);

/**
 * Free for ::bset_u32.
 */
void bset_u32_free(struct bset_u32 *set);

/**
 * Check for existence of \a val in \a set.
 * \param set The pointer to ::bset_u32.
 * \param val The value to check for.
 */
int bset_u32_exist(struct bset_u32 *set, uint32_t val);

/**
 * bset insert return code.
 */
typedef enum {
	BSET_INSERT_ERR = -1,
	BSET_INSERT_OK = 0,
	BSET_INSERT_EXIST
} bset_insert_rc;

/**
 * Insert \c val into \c set.
 * \param set The pointer to ::bset_u32.
 * \param val The value to be inserted.
 * \return 0 on success.
 * \return Error code on error.
 */
int bset_u32_insert(struct bset_u32 *set, uint32_t val);

/**
 * Remove \c val from \c set.
 * \param set The set handle.
 * \param val The value.
 * \return 0 on success.
 * \return ENOENT if \c val is not found.
 */
int bset_u32_remove(struct bset_u32 *set, uint32_t val);

/**
 * Create ::bset_u32 from \c num_list.
 * \param[in] num_list List of numbers (e.g. "1,2,5-7")
 * \param hsize The hash size for the ::bset_u32.
 * \retval NULL on error.
 * \retval ptr Pointer to ::bset_u32 containing numbers in the given \c num_lst.
 */
struct bset_u32 *bset_u32_from_numlist(const char *num_lst, int hsize);

/********* Range **********/
/**
 * A ::brange_u32 representing a range <tt>[a, b]</tt>
 */
struct brange_u32 {
	uint32_t a;
	uint32_t b;
	TAILQ_ENTRY(brange_u32) link;
};

TAILQ_HEAD(brange_u32_head, brange_u32);

struct brange_u32_iter {
	struct brange_u32 *first_range;
	struct brange_u32 *current_range;
	uint32_t current_value;
};

/**
 * Compare \c x to \c range.
 * \return 0 if \c x is in \c range.
 * \return 1 if \c x is greater than \c range.
 * \return -1 if \c x is less than \c range.
 */
static inline
int brange_u32_cmp(struct brange_u32 *range, uint32_t x)
{
	if (x < range->a)
		return -1;
	if (range->b < x)
		return 1;
	return 0;
}

/**
 * Create a list of ranges from the given set \c set, and append them to
 * \c * head.
 *
 * \param set The set handle.
 * \param head The list head to append the ranges to.
 *
 * \retval 0 if OK
 * \retval errno if error.
 */
int bset_u32_to_brange_u32(struct bset_u32 *set, struct brange_u32_head *head);

/**
 * Create a value iterator for ranges.
 *
 * \param first The first range in the range list.
 *
 * \retval NULL if error.
 * \retval ptr ponter to the iterator handle, if success.
 */
struct brange_u32_iter *brange_u32_iter_new(struct brange_u32 *first);

/**
 * Get current value from the iterator.
 *
 * \param itr The iterator.
 * \param[out] v The output value.
 *
 * \retval 0 if OK.
 * \retval ENOENT if there is no more entry.
 */
int brange_u32_iter_get_value(struct brange_u32_iter *itr, uint32_t *v);

/**
 * Move the iterator to the beginning position; also set \c *v to the first
 * value of the ranges.
 *
 * \param itr The iterator handle.
 * \param[out] v The output value.
 *
 * \retval 0 if OK
 * \retval errno if error.
 */
int brange_u32_iter_begin(struct brange_u32_iter *itr, uint32_t *v);

/**
 * Move the iterator to the beginning position; also set \c *v to the last
 * value of the ranges.
 *
 * \param itr The iterator handle.
 * \param[out] v The output value.
 *
 * \retval 0 if OK
 * \retval errno if error.
 */
int brange_u32_iter_end(struct brange_u32_iter *itr, uint32_t *v);

/**
 * Free the iterator.
 */
void brange_u32_iter_free(struct brange_u32_iter *itr);

/**
 * Move iterator to the next entry, and assign the value to \c *v.
 *
 * \param itr The iterator.
 * \param[out] v The output value.
 *
 * \retval 0 if OK.
 * \retval ENOENT if there is no more entry.
 */
int brange_u32_iter_next(struct brange_u32_iter *itr, uint32_t *v);

/**
 * Move the iterator, in a forward direction, to the position greater than or
 * equal to \c *v.
 *
 * \param itr The iterator.
 * \param[in,out] v The value to seek to, also will be set to current position
 *                  as an output (if success).
 *
 * \retval 0 if success.
 * \retval EINVAL if \c v is less than the current position.
 * \retval ENOENT if there is no range that contained or positioned beyond \c v.
 */
int brange_u32_iter_fwd_seek(struct brange_u32_iter *itr, uint32_t *v);

/**
 * Move the iterator, in a backward direction, to the position less than or
 * equal to \c *v.
 *
 * \param itr The iterator.
 * \param[in,out] v The value to seek to, also will be set to current position
 *                  as an output (if success).
 *
 * \retval 0 if success.
 * \retval EINVAL if \c v is greater than the current position.
 * \retval ENOENT if there is no range that contained or positioned before \c v.
 */
int brange_u32_iter_bwd_seek(struct brange_u32_iter *itr, uint32_t *v);

#endif
/**\}*/
