/**
 * \file bhash.h
 * \author Narate Taerat (narate at ogc dot us)
 *
 * \brief Basic hash container implementation for generic usage.
 *
 * \note bhash is not thread-safe.
 */

#ifndef __BHASH_H
#define __BHASH_H

#include "btypes.h"
#include "butils.h"
#include "sys/queue.h"

struct bhash_entry {
	LIST_ENTRY(bhash_entry) link;
	uint64_t value;
	size_t keylen;
	char key[0];
};

LIST_HEAD(bhash_head, bhash_entry);

typedef uint32_t (*bhash_fn_t)(const char *key, int key_len, uint32_t seed);

struct bhash {
	/**
	 * size of hash array.
	 */
	size_t hash_size;

	/**
	 * hash table, elements of which are heads to list of ::hash_entry.
	 */
	struct bhash_head *hash_table;

	/**
	 * Seed of the hash function.
	 */
	uint32_t seed;

	/**
	 * Hash function
	 */
	bhash_fn_t hash_fn;

	/**
	 * Number of elements.
	 */
	size_t count;
};

/**
 * Forward iterator for ::bhash.
 */
struct bhash_iter {
	/**
	 * Current index in hash table.
	 */
	uint32_t idx;
	/**
	 * Current entry in the list corresponding to the idx in the
	 * bhash::hash_table.
	 */
	struct bhash_entry *entry;

	/**
	 * Handle to ::bhash.
	 */
	struct bhash *h;
};

/**
 * Create a new hash container.
 *
 * \param hash_size The size of the hash table for the container.
 * \param seed The seed for the hash function.
 * \param hash_fn Hash function. If NULL, fnv_hash will be used.
 *
 * \retval ptr the handle to the hash container structure, if success.
 * \retval NULL if error.
 */
struct bhash *bhash_new(size_t hash_size, uint32_t seed, bhash_fn_t hash_fn);

void bhash_free(struct bhash *h);

/**
 * Assign \c objref to the given key \c key in the hash container \c h.
 *
 * \param h Hash container handle.
 * \param key Key.
 * \param keylen Length of the key.
 * \param value value assciated with the \c key.
 *
 * \note \c h will not own the given key. The internal key object will be
 * created as a copy instead.
 *
 * \retval ptr reference to the entry in the hash, if success.
 * \retval NULL if failed.
 */
struct bhash_entry *bhash_entry_set(struct bhash *h, const char *key,
						size_t keylen, uint64_t value);

/**
 * Delete \c key (and associated value) from the hash \c h.
 *
 * \retval 0 OK.
 * \retval errno Error.
 */
int bhash_entry_del(struct bhash *h, const char *key, size_t keylen);

/**
 * Remove entry from the hash container, and free the entry altogether.
 *
 * \retval 0 OK.
 * \retval errno Error.
 */
int bhash_entry_remove(struct bhash *h, struct bhash_entry *ent);

/**
 * Get value associated with \c key from the hash \c h.
 *
 * \param h The hash container handle.
 * \param key The key.
 * \param keylen Length of the key.
 *
 * \retval ptr The pointer to the entry (do no modify the entry).
 * \retval NULL if entry not found.
 */
struct bhash_entry *bhash_entry_get(struct bhash *h, const char *key,
								size_t keylen);

/**
 * Create new iterator of the hash \c h.
 *
 * \retval ptr The iterator handle, if success.
 * \retval NULL if failed.
 */
struct bhash_iter *bhash_iter_new(struct bhash *h);

/**
 * Reset the iterator to the beginning entry.
 *
 * \retval 0 OK.
 * \retval ENOENT
 */
int bhash_iter_begin(struct bhash_iter *iter);

/**
 * Move the iterator to the next entry.
 *
 * \param iter The iterator handle.
 *
 * \retval 0 Success.
 * \retval ENOENT if there is no more entry.
 */
int bhash_iter_next(struct bhash_iter *iter);

/**
 * Obtain the current hash entry from the iterator.
 *
 * \param iter The iterator handle.
 *
 * \retval entry if the iterator is still valid.
 * \retval NULL if the iterator end.
 */
struct bhash_entry *bhash_iter_entry(struct bhash_iter *iter);

/**
 * Iterator free function.
 */
void bhash_iter_free(struct bhash_iter *iter);
#endif
