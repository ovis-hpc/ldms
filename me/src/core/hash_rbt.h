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
