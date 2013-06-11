/*
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

/*
 * Author: Tom Tucker tom at ogc dot us
 */
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
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
	int l = 0;
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

	void *obj = pl->entries[*pkey].obj;
	// Fragment of old codes?
	//  |
	//  V
	/*
	pkey = key;
	for (pl = t->top, l = 0; pl && l < 4; l++) {
		pl = pl->entries[*pkey].next;
		pkey++;
	}*/ // Narate commented this out.
	return obj;
}

/**
 * \brief Add a prefix key and associated data object to the tree.
 */
int idx_add(idx_t t, idx_key_t key, size_t keylen, void *obj)
{
	struct idx_layer_s *pl;
	unsigned char *pidx = key;
	int i;

	/* We don't allow adding a NULL pointer as an object */
	if (!obj)
		return EINVAL;

	if (idx_find(t, key, keylen))
		return EEXIST;

	idx_entries++;
	keylen--;
	for (pl = t->top, i = 0; keylen; keylen--, pl = pl->entries[*pidx++].next) {
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

#if 0
/**
 * \brief Delete an entry from a prefix tree based on it's key.
 *
 * \param d Pointer to the prefix tree
 * \param pidx Pointer to the prefix key
 * \returns The object associated with the prefix key
 */
static struct idx_entry *
_idx_find(struct idx_tree *d, idx_key_t key, size_t keylen)
	  struct idx_layer **ppl)
{
	unsigned char idx;
	unsigned char *pkey = key;
	struct idx_entry *pe;
	struct idx_layer *pl;
	int i;

	if (!keylen || !key)
		return NULL;

	for (pl = t->top, i = 0; i < keylen; i++) {
		if (ppl)
			*ppl = pl;
		if (!pl)
			return NULL;
		pe = &pl->entries[idx];
		pl = pl->next;
	}
	return pe;
}
#endif
void *idx_delete(idx_t t, idx_key_t key, size_t keylen)
{
#if 0
	struct idx_entry *pe;
	struct idx_layer *pl;
	void *obj;
	int i;

	pe = _idx_find(t, key, keylen, &pl);
	if (!pe)
		return NULL;

	obj = pe->obj;
	idx_free(pe);
	pl->entries[((unsigned char *)key)[keylen-1]] = NULL;
	pl->count--;

	return obj;
#endif
	return NULL;
}

#ifdef IDX_MAIN
#include <stdio.h>
#include <stdlib.h>
idx_t idx;
int main(int argc, char *argv[]) {
	char buf[256];
	char *s;
	static char comp_name[12];
	int keylen;
	ssize_t cnt = 0;
	idx_t idx;
	static char comp_type[12];
	static char metric_name[12];
	uint32_t comp_id;
	uint64_t metric_value, secs, msecs;

	/* Create the component type and component tree */
	idx = idx_create();

	while ((s = fgets(buf, sizeof(buf), stdin)) != NULL) {
		char *o;
		sscanf(buf, "%[^,],%[^,],%d,%ld,%ld,%ld", comp_type, metric_name,
		       &comp_id, &metric_value, &secs, &msecs);
		sprintf(comp_name, "%d", comp_id);
		comp_id = ntohl(comp_id);
		void *obj = strdup(comp_name);
		if (!obj) {
			perror("strdup");
			exit(1);
		}
		int rc = idx_add(idx, (idx_key_t)&comp_id, 4, obj);
		if (!rc) {
			o = idx_find(idx, (idx_key_t)&comp_id, 4);
			if ((idx_entries % 100000) == 0)
				printf("idx_memory %zuM entries %zuK ratio %lu found %s\n", idx_memory / 1024 / 1024,
				       idx_entries / 1024, idx_memory / idx_entries, o);
		}
	}
	printf("%zuM\n", idx_entries / 1024 / 1024);
	return 0;
}

#endif
