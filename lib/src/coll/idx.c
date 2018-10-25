/**
 * Copyright (c) 2010-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2012-2013,2016-2018 Open Grid Computing, Inc. All rights reserved.
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
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include "idx.h"
#include "idx_priv.h"

static size_t idx_memory;
static size_t idx_entries;

static inline void *idx_alloc(idx_t idx, size_t sz)
{
	idx_memory += sz;
	return malloc(sz);
}

static void idx_free(idx_t idx, void *p)
{
	if (p)
		free(p);
}

static void idx_zero(void *p, size_t len)
{
	memset(p, 0, len);
}

static struct idx_layer_s *new_idx_layer(idx_t t, size_t sz)
{
	struct idx_layer_s *l;

	l = idx_alloc(t, sizeof(*l) + sz);
	if (!l)
		goto err;
	l->layer_count = 0;
	l->obj_count = 0;
	idx_zero((void *)l, sz + sizeof(*l));
	return l;

 err:
	return NULL;
}

idx_t idx_create()
{
	size_t radix = 256;
	size_t sz = radix * sizeof(struct idx_entry_s);
	idx_t t = malloc(sizeof *t);
	if (!t)
		goto err0;

	t->radix = radix;
	t->layer_sz = sz;
	t->top = new_idx_layer(t, sz);
	if (!t->top)
		goto err1;

	return t;

 err1:
	free(t);
 err0:
	return NULL;
}

static void free_layer(idx_t t, struct idx_layer_s *l)
{
	int i;

	if (!l)
		return;

	for (i = 0; i < t->radix; i++)
		if (l->entries[i].next)
			free_layer(t, l->entries[i].next);

	idx_free(t, l);
}

/**
 * \brief Free a prefix tree.
 *
 * \param d Pointer to the prefix tree
 */
void idx_destroy(idx_t t)
{
	free_layer(t, t->top);
	free(t);
}

/**
 * \brief Find the data object associated with a prefix key
 */
void *idx_find(idx_t t, idx_key_t key, size_t keylen)
{
	unsigned char *pkey = key;
	struct idx_layer_s *pl;

	if (!keylen || !key)
		return NULL;

	keylen--;
	for (pl = t->top; pl && keylen; keylen--)
		pl = pl->entries[*pkey++].next;

	/* If we ran out of key before we ran out of layers --> not found */
	if (keylen)
		return NULL;

	/* ... or we ran out on the last layer */
	if (!pl)
		return NULL;

	return pl->entries[*pkey].obj;
}

static void traverse_layer(idx_t t, struct idx_layer_s *pl, idx_cb_fn_t cb, void *cb_arg)
{
	int e;
	for (e = 0; e < t->radix; e++) {
		void *obj = pl->entries[e].obj;
		struct idx_layer_s *nl = pl->entries[e].next;
		if (obj)
			cb(obj, cb_arg);
		if (nl)
			traverse_layer(t, nl, cb, cb_arg);
	}
}

/**
 * \brief Find the data object associated with a prefix key
 */
void idx_traverse(idx_t t, idx_cb_fn_t cb, void *cb_arg)
{
	struct idx_layer_s *pl = t->top;
	traverse_layer(t, pl, cb, cb_arg);
}

/**
 * \brief Add a prefix key and associated data object to the tree.
 */
int idx_add(idx_t t, idx_key_t key, size_t keylen, void *obj)
{
	struct idx_layer_s *pl;
	unsigned char *pidx = key;

	/* We don't allow adding a NULL pointer as an object */
	if (!obj)
		return EINVAL;

	if (idx_find(t, key, keylen))
		return EEXIST;

	idx_entries++;
	keylen--;
	for (pl = t->top; keylen;
	     keylen--, pl = pl->entries[*pidx++].next) {
		/* If there is no layer at this prefix, add one */
		if (!pl->entries[*pidx].next) {
			pl->entries[*pidx].next = new_idx_layer(t, t->layer_sz);
			if (!pl->entries[*pidx].next)
				goto err0;
			pl->layer_count++;
		}
	}
	pl->obj_count++;
	pl->entries[*pidx].obj = obj;

	return 0;

 err0:
	return ENOMEM;
}

/*
 * Return 1 if layer is deleted, telling the caller to
 * decrement it's layer count and null it's next pointer.
 */
static int purge_layer(struct idx_layer_s *pl, idx_key_t key, size_t keylen)
{
	unsigned char *pkey = key;
	struct idx_layer_s *npl;
	assert(*(unsigned char *)pkey != (unsigned char )255);
	if (keylen < 2) {
		return 0;
	} else if (keylen==2) {
		if (!pl->obj_count && !pl->layer_count) {
			free(pl);
			return 1;
		}
	} else {
		npl = pl->entries[*pkey].next;
		assert(npl);
		if (purge_layer(npl, (idx_key_t)(pkey + 1), keylen-1)) {
			pl->layer_count--;
			pl->entries[*pkey].next = NULL;
			if (!pl->obj_count && !pl->layer_count) {
				free(pl);
				return 1;
			}
		}
	}
	return 0;
}

static void purge_layers(idx_t t, idx_key_t key, size_t keylen)
{
	unsigned char *pkey = key;
	struct idx_layer_s *pl = t->top;

	assert(key && keylen);

	/* The top layer is not purged */
	if (purge_layer(pl->entries[*pkey].next, pkey+1, keylen-1)) {
		pl->layer_count--;
		pl->entries[*pkey].next = NULL;
	}
}

/**
 * \brief Delete an entry from a prefix tree based on it's key.
 *
 * \param d Pointer to the prefix tree
 * \param pidx Pointer to the prefix key
 * \returns The object associated with the prefix key
 */
void *idx_delete(idx_t t, idx_key_t key, size_t keylen)
{
	unsigned char *pkey = key;
	struct idx_layer_s *pl;
	void *obj;
	size_t pkeylen = keylen;

	if (!keylen || !key)
		return NULL;

	pkeylen--;
	for (pl = t->top; pl && pkeylen; pkeylen--)
		pl = pl->entries[*pkey++].next;

	/* If we ran out of key before we ran out of layers --> not found */
	if (pkeylen)
		return NULL;

	/* ... or we ran out on the last layer */
	if (!pl)
		return NULL;

	obj = pl->entries[*pkey].obj;
	pl->obj_count--;
	pl->entries[*pkey].obj = NULL;
	purge_layers(t, key, keylen);
	return obj;
}

static void idx_count_internal(void *obj, void *count)
{
	if (count) {
		size_t *c = (size_t *)count;
		(*c)++;
	}
}

size_t idx_count(idx_t t)
{
	size_t result = 0;
	if (t) {
		idx_traverse(t, idx_count_internal, &result);
	}
	return result;
}

#ifdef IDX_TEST
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "ovis-test/test.h"

int entry_count;
void count_cb(void *obj, void *arg)
{
	entry_count++;
}

idx_t idx;
int main(int argc, char *argv[])
{
	size_t keylen;
	static char key[32];
	char *obj;
	idx_t idx;
	int i, rc;
	long seed = time(NULL);
	idx = idx_create();
	/* Add 100 objects */
	srandom(seed);
	for (i = 0; i < 100; i++) {
		long k = random();
		sprintf(key, "%d", k);
		keylen = strlen(key);
		obj = strdup(key);
		rc = idx_add(idx, (idx_key_t)key, keylen, obj);
		TEST_ASSERT(!rc, "Add object '%s'.\n", key);
	}
	entry_count = 0;
	idx_traverse(idx, count_cb, NULL);
	size_t icount = idx_count(idx);
	TEST_ASSERT((entry_count == 100), "There are 100 entries in the index\n");
	TEST_ASSERT(((size_t)entry_count == icount), "icount and entry_count match\n");
	/* Make certain they can be found */
	srandom(seed);
	for (i = 0; i < 100; i++) {
		long k = random();
		sprintf(key, "%d", k);
		keylen = strlen(key);
		obj = idx_find(idx, (idx_key_t)key, keylen);
		TEST_ASSERT(obj != NULL,
			    "Object %p was found with key '%s'.\n", obj, key);
		if (!obj)
			continue;
		TEST_ASSERT(0 == strcmp(key, obj),
			    "Object '%s' matches key '%s'.\n",
			    obj, key);
	}

	/* Delete the objects and make certain they can't be found */
	/* TODO Random can return duplicates which will crash delete */
	srandom(seed);
	for (i = 0; i < 100; i++) {
		long k = random();
		sprintf(key, "%d", k);
		keylen = strlen(key);
		obj = idx_delete(idx, (idx_key_t)key, keylen);
		TEST_ASSERT(obj != NULL,
			    "Object %p was deleted with key '%s'.\n", obj, key);
		if (!obj)
			continue;
		TEST_ASSERT(0 == strcmp(key, obj),
			    "Object deleted '%s' matches key '%s'.\n",
			    obj, key);
		TEST_ASSERT(NULL == idx_find(idx, key, keylen),
			    "Object deleted '%p can no longer be found.\n",
			    obj);
		free(obj);
	}
	idx_destroy(idx);
	idx = idx_create();
	char *strings[] = {"a", "aa", "aaa", "aaaa", "aaaaa", NULL};
	i = 0;
	while (strings[i] != NULL) {
		char *ikey = strings[i];
		int keylen = strlen(ikey);
		rc = idx_add(idx,(idx_key_t)ikey, keylen, ikey);
		TEST_ASSERT(!rc, "Add object '%s'.\n", ikey);
		i++;
	}
	i = 0;
	while (strings[i] != NULL) {
		char *ikey = strings[i];
		int keylen = strlen(ikey);
		obj = idx_find(idx, (idx_key_t)ikey, keylen);
		TEST_ASSERT(obj != NULL,
			    "Object %s was found with key '%s'.\n", obj, ikey);
		TEST_ASSERT(0 == strcmp(ikey, obj),
			    "Object '%s' matches key '%s'.\n",
			    obj, ikey);
		i++;
	}
	size_t ic = idx_count(idx);
	TEST_ASSERT(ic > 0,"idx starting with size %lu\n",(unsigned long)ic);
	i = 0;
	while (strings[i] != NULL) {
		char *ikey = strings[i];
		keylen = strlen(ikey);
		obj = idx_delete(idx, (idx_key_t)ikey, keylen);
		TEST_ASSERT(obj != NULL,
			    "Object %p was deleted with key '%s'.\n", obj, ikey);
		TEST_ASSERT(0 == strcmp(ikey, obj),
			    "Object deleted '%s' matches key '%s'.\n",
			    obj, ikey);
		TEST_ASSERT(NULL == idx_find(idx, ikey, keylen),
			"Object deleted '%p can no longer be found.\n", obj);
		int newic = idx_count(idx);
		ic--;
		TEST_ASSERT(ic == newic, "idx size correct after delete.\n", obj);
		i++;
	}

	idx_destroy(idx);
	return 0;
}

#endif
