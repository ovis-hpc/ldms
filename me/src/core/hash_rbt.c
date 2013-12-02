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

#include <errno.h>
#include <stddef.h>
#include <malloc.h>
#include <coll/rbt.h>
#include "hash_rbt.h"

struct hash_rbt *hash_rbt_init(int hash_table_sz)
{
	struct hash_rbt *table = malloc(sizeof(*table));
	if (!table)
		return NULL;

	table->table = calloc(hash_table_sz, sizeof(struct rbt));
	if (!table->table) {
		free(table);
		return NULL;
	}

	table->table_sz = hash_table_sz;
	return table;
}

int hash_rbt_compare_key_64(void *node_key, void *val_key)
{
	return (int)((uint64_t)node_key - ((uint64_t)val_key));
}

int hash_rbt_insert64(struct hash_rbt *tbl, uint64_t key, void *obj)
{
	int hkey = key % tbl->table_sz;
	struct rbt **hash_tbl = tbl->table;
	if (!hash_tbl[hkey]) {
		hash_tbl[hkey] = malloc(sizeof(struct rbt));
		if (!hash_tbl[hkey])
			return ENOMEM;
		rbt_init(hash_tbl[hkey], hash_rbt_compare_key_64);
	}


	struct hash_rbt_node *hnode = malloc(sizeof(*hnode));
	if (!hnode)
		return ENOMEM;
	hnode->obj = obj;
	hnode->node.key = (void*)key;
	rbt_ins(hash_tbl[hkey], &hnode->node);
	return 0;
}

void *hash_rbt_get64(struct hash_rbt *tbl, uint64_t key)
{
	int hkey = key % tbl->table_sz;
	struct rbt **hash_tbl = tbl->table;
	if (!hash_tbl[hkey])
		return NULL;
	struct rbn *rbn = rbt_find(hash_tbl[hkey], (void*)key);
	if (!rbn)
		return NULL;

	struct hash_rbt_node *hnode;
	hnode = container_of(rbn, struct hash_rbt_node, node);
	return hnode->obj;
}

void hash_rbt_del64(struct hash_rbt *tbl, uint64_t key)
{
	int hkey = key % tbl->table_sz;
	struct rbt **hash_tbl = tbl->table;
	if (!hash_tbl[hkey])
		return;

	struct rbn *rbn = rbt_find(hash_tbl[hkey], (void*)key);
	if (!rbn)
		return;

	struct hash_rbt_node *hnode;
	hnode = container_of(rbn, struct hash_rbt_node, node);
	rbt_del(hash_tbl[hkey], &hnode->node);
	free(hnode->obj);
	free(hnode);
}
