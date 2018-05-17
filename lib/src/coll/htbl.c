/*
 * Copyright (c) 2017 Open Grid Computing, Inc. All rights reserved.
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
#include <stdlib.h>
#include <string.h>
#include "htbl.h"
#include <assert.h>

#define FNV_64_PRIME 0x100000001b3ULL
#define FNV_64_SEED  0xDEADC0DEBEEFDEADULL

static uint64_t default_hash_fn(const void *key, size_t key_len)
{
	int i;
	uint64_t h = FNV_64_SEED;
	const unsigned char *s = key;
	for (i = 0; i < key_len; i++) {
		h ^= s[i];
		h *= FNV_64_PRIME;
	}
	return h;
}

/**
 * \brief Initialize an Hash Table
 *
 * \param t	Pointer to the Hast Table.
 * \param c	Pointer to the function that compares entries
 *		in the Hash Table
 */
htbl_t htbl_alloc(htbl_cmp_fn_t cmp_fn, size_t depth)
{
	htbl_t t = malloc(sizeof(struct htbl)
			  + (depth * sizeof(struct hent_list_head)));
	if (t) {
		t->table_depth = depth;
		t->entry_count = 0;
		t->cmp_fn = cmp_fn;
		t->hash_fn = default_hash_fn;
		memset(t->table, 0, (depth * sizeof(struct hent_list_head)));
	}
	return t;
}

/**
 * \brief Returns TRUE if the tree is empty.
 *
 * \param t	Pointer to the rbt.
 * \retval 0	The tree is not empty
 * \retval 1	The tree is empty
 */
int hbtl_empty(htbl_t t)
{
	return (0 == t->entry_count);
}

/**
 * \brief Initialize a Hash Table entry.
 *
 * \param e The Entry to initialize
 * \param key Pointer to the key
 */
void hent_init(hent_t e, const void *key, size_t len)
{
	e->key = key;
	e->key_len = len;
}

/**
 * \brief Insert a new entry into the Hash Table
 *
 * Insert a new entry into a Hash Table. The entry is allocated by the
 * user and must be freed by the user when removed from the Hash
 * Table. The 'key' field of the provided entry must have already been
 * set up by calling hent_init()
 *
 * \param t	Pointer to the Hash Table
 * \param x	Pointer to the entry to insert.
 */
void htbl_ins(htbl_t t, hent_t e)
{
	uint64_t h = t->hash_fn(e->key, e->key_len);
	h %= t->table_depth;
	LIST_INSERT_HEAD(&t->table[h], e, hash_link);
	t->entry_count++;
}

/**
 * \brief Delete an entry from the Hash Table
 *
 * \param t	Pointer to the Hash Table
 * \param z	Pointer to the entry to delete.
 */
void htbl_del(htbl_t t, hent_t e)
{
	LIST_REMOVE(e, hash_link);
	t->entry_count--;
}

/**
 * \brief Find an entry in the Hash Table that matches a key
 *
 * \param t	Pointer to the Hash Table
 * \param key	Pointer to the key.
 * \retval !NULL Pointer to the entry with the matching key.
 * \retval NULL  No node in the tree matches the key.
 */
hent_t htbl_find(htbl_t t, const void *key, size_t key_len)
{
	hent_t e;
	uint64_t h = t->hash_fn(key, key_len);
	h %= t->table_depth;
	LIST_FOREACH(e, &t->table[h], hash_link) {
		if (0 == t->cmp_fn(e->key, key, key_len))
			return e;
	}
	return 0;
}

/**
 * \brief Return the first entry the Hash Table.
 *
 * \param t	Pointer to the Hash Table
 * \return	Pointer to the entry or NULL if the table is empty.
 */
hent_t htbl_first(htbl_t t)
{
	return NULL;
}

/**
 * \brief Return the last entry in the Hash Table
 *
 * \param t	Pointer to the RBT.
 * \return	Pointer to the node or NULL if the tree is empty.
 */
hent_t htbl_last(htbl_t t)
{
	return NULL;
}

/**
 * \brief Return the successor entry
 *
 * Given an entry in the table, return it's successor.
 *
 * \param e Pointer to the entry
 * \returns Pointer to the successor entry
 */
hent_t hent_succ(hent_t e)
{
	return NULL;
}

/**
 * \brief Return the predecessor entry
 *
 * Given an entry in the tree, return it's predecessor.
 *
 * \param e Pointer to the entry
 * \returns Pointer to the predecessor entry
 */
hent_t hent_pred(hent_t e)
{
	return NULL;
}

