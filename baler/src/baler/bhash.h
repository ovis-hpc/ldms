/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
 *
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
int bhash_entry_remove_free(struct bhash *h, struct bhash_entry *ent);

/**
 * Remove entry from the hash container (but not freeing it).
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

void bhash_entry_free(struct bhash_entry *ent);

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
