/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013,2016-2017,2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013,2016-2017,2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file str_map.c
 * \brief String-Object mapping utility.
 *
 */

#include "ovis-ldms-config.h"
#include <errno.h>
#include "str_map.h"
#include "fnv_hash.h"

/**
 * \brief str_map definition.
 *
 * ::str_map is a map that maps strings (keys) to objects (void* or uint64_t).
 *
 * To create a str_map, call ::str_map_create(size_t sz).
 * To insert an object into the map, call ::str_map_insert(map, key, obj);
 * To remove an object, call ::str_map_remove(map, key);
 * To get an object, call ::str_map_get(map, key);
 */
struct str_map {
	size_t hash_size; /**< hash size */
	struct obj_list_head lh_table[OVIS_FLEX]; /**< hash table */
};

struct str_map* str_map_create(size_t sz)
{
	struct str_map *m = mmap(NULL, sz*sizeof(uint64_t)+sizeof(*m),
				 PROT_READ|PROT_WRITE,
				 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED)
		return NULL;
	m->hash_size = sz;
	return m;
}

void str_map_free(str_map_t m)
{
	if (!m)
		return;
	int i;
	for (i=0; i<m->hash_size; i++) {
		struct obj_list_head *lh = &m->lh_table[i];
		if (!LIST_FIRST(lh))
			continue;
		struct obj_list *ol;
		while ((ol = LIST_FIRST(lh))) {
			LIST_REMOVE(ol, link);
			free(ol->key);
			free(ol);
		}
	}
	munmap(m, m->hash_size*sizeof(uint64_t) + sizeof(*m));
}

uint64_t str_map_get(str_map_t map, const char *key)
{
	uint32_t h = fnv_hash_a1_32(key, strlen(key), 0);
	struct obj_list *ol;
	struct obj_list_head *lh = &map->lh_table[h%map->hash_size];
	LIST_FOREACH(ol, lh, link) {
		if (strcmp(ol->key, key) == 0) {
			errno = 0;
			return ol->obj;
		}
	}
	errno = ENOENT;
	return 0;
}

int str_map_insert(str_map_t map, const char *key, uint64_t obj)
{
	uint32_t h = fnv_hash_a1_32(key, strlen(key), 0);
	struct obj_list *ol;
	struct obj_list_head *lh = &map->lh_table[h%map->hash_size];
	LIST_FOREACH(ol, lh, link) {
		if (strcmp(ol->key, key) == 0)
			return EEXIST;
	}
	ol = malloc(sizeof(*ol));
	if (!ol)
		return ENOMEM;
	ol->key = strdup(key);
	if (!ol->key) {
		free(ol);
		return ENOMEM;
	}
	ol->obj = obj;
	LIST_INSERT_HEAD(lh, ol, link);
	return 0;
}

int str_map_remove(str_map_t map, const char *key)
{
	uint32_t h = fnv_hash_a1_32(key, strlen(key), 0);
	struct obj_list *ol;
	struct obj_list_head *lh = &map->lh_table[h%map->hash_size];
	LIST_FOREACH(ol, lh, link) {
		if (strcmp(ol->key, key) == 0) {
			LIST_REMOVE(ol, link);
			free(ol->key);
			free(ol);
			return 0;
		}
	}
	return ENOENT;
}

int str_map_id_init(str_map_t map, char **keys, int nkeys,
		    uint64_t start_id)
{
	int i, rc;
	uint64_t id = start_id;
	for (i=0; i<nkeys; i++) {
		rc = str_map_insert(map, keys[i], id);
		if (rc == 0)
			id++;
		else
			return rc;
	}
	return 0;
}
