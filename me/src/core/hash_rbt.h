/*
 * hash_rbt.h
 *
 *  Created on: Sep 23, 2013
 *      Author: nichamon
 */

/**
 * \mainpage hash_rbt
 *
 * hash_rbt is a data structure that hashes integer keys.
 * and handles the collision by red-black tree. Since
 * keys are integers, the hash function is the modulo operation.
 *
 * The size of the hash table is required when initializes the
 * the data structure.
 *
 * The current implementation supports only the keys of uint64_t.
 */

#ifndef HASH_RBT_H_
#define HASH_RBT_H_

#include <inttypes.h>
#include <coll/rbt.h>

struct hash_rbt
{
	struct rbt **table;
	int table_sz;
};

struct hash_rbt_node {
	struct rbn node;
	void *obj;
};

/**
 * \brief Allocate memory and initialize the hash table
 * \return a hash_rbt
 *
 * The hash function is the modulo operation with \c hash_table_sz.
 */
struct hash_rbt *hash_rbt_init(int hash_table_sz);

/**
 * \brief Insert the given object (\c obj)
 *
 * \param[in] key the key of \c obj
 * \param[in] obj the object
 */
int hash_rbt_insert64(struct hash_rbt *tbl, uint64_t key, void *obj);

/**
 * \brief Find and return the object of the given key
 * \return an object or NULL if the key doesn't exist.
 */
void *hash_rbt_get64(struct hash_rbt *tbl, uint64_t key);

/**
 * \brief Delete the object of the key
 */
void hash_rbt_del64(struct hash_rbt *tbl, uint64_t key);

#endif /* HASH_RBT_H_ */
