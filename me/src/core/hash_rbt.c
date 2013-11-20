/*
 * hash_rbt.c
 *
 *  Created on: Sep 23, 2013
 *      Author: nichamon
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
