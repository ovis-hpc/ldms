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

#ifdef HTBLDEBUG
#include <stdio.h>
#endif

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

void htbl_free(htbl_t t)
{
	free(t);
}

/**
 * \brief Returns TRUE if the tree is empty.
 *
 * \param t	Pointer to the rbt.
 * \retval 0	The tree is not empty
 * \retval 1	The tree is empty
 */
int htbl_empty(htbl_t t)
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
	e->tbl = t;
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
		if ( 0 == t->cmp_fn(e->key, key, key_len)) {
#ifdef HDEBUG
			if ( key_len != e->key_len)
				fprintf(stderr, "htbl_find returning mismatched key_len item: %s for %s\n", (char *)e->key, (char *)key);
#endif
			return e;
		}
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
	uint64_t h;
	hent_t ent;

	for (h = 0; h < t->table_depth; h++) {
		ent = LIST_FIRST(&t->table[h]);
		if (ent)
			return ent;
	}
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
hent_t htbl_next(hent_t e)
{
	uint64_t h;
	hent_t ent = LIST_NEXT(e, hash_link);
	if (ent)
		return ent;
	h = e->tbl->hash_fn(e->key, e->key_len);
	h %= e->tbl->table_depth;
	h += 1;
	for (; h < e->tbl->table_depth; h++) {
		ent = LIST_FIRST(&e->tbl->table[h]);
		if (ent)
			return ent;
	}
	return NULL;
}

#ifdef HTBL_TEST
#include <inttypes.h>
#include <time.h>
#include "ovis-test/test.h"

/* testing inspired by:
const char *data =
"{\"serial\":1156,\"is_thread\":0,\"uid\":4294967295,\"task_pid\":11344,\"exe\":\"/usr/bin/bash\",\"job_id\":0,\"gid\":4294967295,\"start\":\"1624996169.996169\"}\"";
*/

enum valtype {
	t_err,
	t_string,
	t_int,
};
struct tuple {
	const char *name;
	enum valtype t;
	union {
		int64_t i;
		const char *s;
	} v;
};

struct tuple values[] = {
	{"serial", t_int, .v.i = 1156},
	{"is_thread", t_int, .v.i = 0},
	{"uid", t_int, .v.i = 4294967295},
	{"task_pid", t_int, .v.i = 11344},
	{"exe", t_string, .v.s = "/usr/bin/bash"},
	{"job_id", t_int, .v.i = 0},
	{"gid", t_int, .v.i = 4294967295},
	{"start", t_string, .v.s = "1624996169.996169"}
};

struct tuple hard_values[] = {
	{"serial", t_int, .v.i = 1156},
	{"is_thread", t_int, .v.i = 0},
	{"job_id1", t_int, .v.i = 0},
	{"uid", t_int, .v.i = 4294967295},
	{"task_pid", t_int, .v.i = 11344},
	{"exe", t_string, .v.s = "/usr/bin/bash"},
	{"job_id", t_int, .v.i = 0},
	{"gid", t_int, .v.i = 4294967295},
	{"job_id2", t_int, .v.i = 0},
	{"start", t_string, .v.s = "1624996169.996169"}
};

static int attr_cmp(const void *a, const void *b, size_t key_len)
{
        return strncmp(a, b, key_len);
}

#define JSON_HTBL_DEPTH 3

int main(int argc, char *argv[])
{
	const size_t ntv = sizeof(values)/sizeof(values[0]);
	const size_t nthv = sizeof(hard_values)/sizeof(hard_values[0]);
	struct hent tv[ntv];
	struct hent thv[nthv];
	htbl_t tab_tv = htbl_alloc(attr_cmp, JSON_HTBL_DEPTH);
	TEST_ASSERT(tab_tv!=NULL, "create tab_tv\n");
	htbl_t tab_thv = htbl_alloc(attr_cmp, JSON_HTBL_DEPTH);
	TEST_ASSERT(tab_thv!=NULL, "create tab_thv\n");
	size_t k;
	for (k = 0; k < ntv; k++) {
		hent_init(&tv[k], values[k].name, strlen(values[k].name)+1);
		htbl_ins(tab_tv, &tv[k]);
	}
	for (k = 0; k < nthv; k++) {
		hent_init(&thv[k], hard_values[k].name,
				strlen(hard_values[k].name)+1);
		htbl_ins(tab_thv, &thv[k]);
	}
	for (k = ntv; k > 0;  ) {
		k--;
		hent_t e = htbl_find(tab_tv, values[k].name,
					strlen(values[k].name)+1);
		TEST_ASSERT(e != NULL, "found tab_tv element %s\n",
				values[k].name);
	}
	for (k = nthv; k > 0; ) {
		k--;
		hent_t e = htbl_find(tab_thv, hard_values[k].name,
					strlen(hard_values[k].name)+1);
		TEST_ASSERT(e != NULL, "found tab_thv element %s\n",
				hard_values[k].name);
	}
	k = 0;
	hent_t e = htbl_first(tab_tv);
	TEST_ASSERT(e != NULL, "Found tab_tv first element\n");
	while (e) {
		/* printf("list tab_tv includes: %s\n", (char *)e->key); */
		e = htbl_next(e);
		k++;
	}
	TEST_ASSERT(k == ntv, "htbl tab_tv iterator got expected count of elements.\n");
	k = 0;
	e = htbl_first(tab_thv);
	TEST_ASSERT(e != NULL, "Found tab_thv first element.\n");
	while (e) {
		/* printf("list tab_thv includes: %s\n", (char *)e->key); */
		e = htbl_next(e);
		k++;
	}
	TEST_ASSERT(k == nthv, "htbl tab_thv iterator got expected count of elements.\n");
	htbl_free(tab_tv);
	htbl_free(tab_thv);
	return 0;
}
#endif
