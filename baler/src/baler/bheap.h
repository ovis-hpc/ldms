/**
 * \file bheap.h
 * \author Narate Taerat (narate at ogc dot us)
 *
 * \brief Basic min-heap container implementation for generic usage.
 *
 * \note If max-heap is needed, just change the \c cmp function.
 *
 * \note bheap is not thread-safe.
 */
#ifndef __BHEAP_H
#define __BHEAP_H

#include "btypes.h"

struct bheap {
	int (*cmp)(void *a0, void *a1);
	int len;
	int alloc_len;
	void **array;
};

/**
 * Create ::bheap container with \c cmp compare function and \c alloc_len
 * length array.
 *
 * \retval NULL if error. \c errno is also set.
 * \retval ptr the ::bheap handle, if success.
 */
struct bheap *bheap_new(int alloc_len, int (*cmp)(void*,void*));

/**
 * Free the heap and associated resources.
 */
void bheap_free(struct bheap *h);

/**
 * Insert \c elm into the heap \c h.
 *
 * \retval 0 if success.
 * \retval errno if error.
 */
int bheap_insert(struct bheap *h, void *elm);

/**
 * Remove the top element from the heap \c h.
 *
 * \retval NULL if there is no element to remove.
 * \retval ptr the pointer to the top element.
 */
void* bheap_remove_top(struct bheap *h);

/**
 * \retval NULL if there is no element.
 * \retval ptr the pointer to the top element of the heap.
 */
void* bheap_get_top(struct bheap *h);

/**
 * Percolate the top-element to fix the heap.
 */
void bheap_percolate_top(struct bheap *h);

/**
 * Verify the heap \c h.
 *
 * \retval 0 if it is verified.
 * \retval 1 if the heap is not verified.
 */
int bheap_verify(struct bheap *h);

#endif
