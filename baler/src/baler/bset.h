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

#include "fnv_hash.h"

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

/********* Range **********/
/**
 * A ::brange_u32 representing a range <tt>[a, b]</tt>
 */
struct brange_u32 {
	uint32_t a;
	uint32_t b;
	LIST_ENTRY(brange_u32) link;
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
 * Create a list of ranges from the given set \c set.
 * \return a list of ranges.
 * \return empty list on error.
 */
void* bset_u32_to_brange_u32(struct bset_u32 *set);

#endif
/**\}*/
