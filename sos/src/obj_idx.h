/*
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
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
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
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

#ifndef _OBJ_IDX_H_
#define _OBJ_IDX_H_

#include <stdint.h>
#include <stdio.h>
#include "ods.h"

typedef struct obj_idx *obj_idx_t;
typedef struct obj_iter *obj_iter_t;

#define OBJ_IDX_SIGNATURE	"OBJIDX00"
#define OBJ_IDX_BPTREE		"BPTREE"
#define OBJ_IDX_RADIXTREE	"RADIXTREE"
#define OBJ_IDX_RBTREE		"RBTREE"

/**
 * \brief Create an object index
 *
 * An index implements a persistent key/value store in an ODS data
 * store. The key is a generic obj_key_t and the value is an
 * obj_ref_t in the associated ODS object store. An ODS obj_ref_t can
 * be transformed back and forth between references and memory
 * pointers using the ods_obj_ref_to_ptr() and ods_obj_ptr_to_ref()
 * functions respectively.
 *
 * The 'type' parameter is the name of the type of index,
 * e.g. "BPTREE" or "RADIXTREE". This parameter identifies the
 * organization of the index and indirectly specifies the name of a
 * shared library that is loaded and implements the generic index
 * interface. There are convenience macros defined for the following
 * index types:
 *
 * - OBJ_IDX_BPTREE Implements a B+ Tree. The order of the tree is
 *   specifed in an 'order' parameter that follows the 'type'
 *   parameter. This parameter is only consulted if O_CREAT was
 *   specified and the index does not already exist.
 * - OBJ_IDX_RBTREE Implements a Red-Black Tree.
 * - OBJ_IDX_RADIXTREE Implements a Radix Tree.
 *
 * An index supports a untyped key that is an arbitrarily long
 * array of bytes. The application must specify the name of a 'key'
 * comparison function that compares two keys and returns the relative
 * order or equivalence of the keys. See the obj_key_t documentation
 * for more information. The 'key' parameter indirectly specifies the
 * name of a shared library that implements the key comparator
 * function. There are convenience macros defined for the predefined
 * key types. See the obj_key_t documention for the names and behavior
 * of these comparators.
 *
 * \param path	The path to the ODS store
 * \param mode	The file mode permission flags
 * \param type	The type of the index
 * \param opt,... Optional index type specific parameters
 *
 * \return 0	The index was successfully create. Use obj_idx_open()
 *		to access the index.
 * \return !0	An errno indicating the reason for failure.
 */
int obj_idx_create(const char *path, int mode,
		   const char *type, const char *key,
		   ...);

/**
 * \brief Open an object index
 *
 * An index implements a persistent key/value store in an ODS data
 * store. The key is a generic obj_key_t and the value is an
 * obj_ref_t in the associated ODS object store. An ODS obj_ref_t can
 * be transformed back and forth between references and memory
 * pointers using the ods_obj_ref_to_ptr() and ods_obj_ptr_to_ref()
 * functions respectively.
 *
 * \param path	The path to the ODS store
 * \param type	The name of the index type. See the obj_idx_create()
 *		function for details on this parameter.
 *
 * \return !0	The obj_idx_t handle for the index
 * \return 0	The index file could not be opened. See the
 *		errno for the reason.
 */
obj_idx_t obj_idx_open(const char *path);

/**
 * \brief Close an object index
 *
 * Close the ODS index store. If flags includes ODS_COMMIT_SYNC,
 * the function will not return until the index contents are commited
 * to stable storage. After this function returns, the idx handle is
 * no longer valid.
 *
 * This function protects against closing a NULL idx handle.
 *
 * \param idx	The index handle
 * \param flags	If flags includdes ODS_COMMIT_SYNC flag, the
 *		function will not return until all content is commited
 *		to stable storage.
 */
void obj_idx_close(obj_idx_t idx, int flags);

/**
 * \brief Commit the index to stable storage
 *
 * Commit all index updates to stable storage. If flags includes
 * ODS_COMMIT_SYNC, the function will not return until all changes
 * are commited to stable storage.
 *
 * This function protects against commiting a NULL idx handle.
 *
 * \param idx	The index handle
 * \param flags	If flags & ODS_COMMIT_SYNC is not zero, the
 *		function will not return until all changes are commited
 *		to stable storage.
 * \return 0		Changes are commited
 * \return EINPROGRESS	The changes are being written to storage
 * \return EBADF	The specified idx handle is invalid
 */
void obj_idx_commit(obj_idx_t idx, int flags);

/**
 * \brief Implements the structure of an object key
 *
 * An object key is a counted array of bytes. A 'comparator'
 * abstraction is used to define the order and equivalence of two
 * keys. A comparator is defined by a name that indirectly
 * identifies a shared library that implements the comparator
 * function. There are a set of pre-defined comparator functions for
 * which there are convenience macros to specify their names as
 * follows:
 *
 * - OBJ_KEY_STRING The key is a string. The strncmp function is used
 *    to compare the two keys. If the lengths of the two keys is
 *    not equal, but they are lexically equal, the function returns the
 *    difference in length between the two keys.
 * - OBJ_KEY_INT32 The key is a 32b signed integer; the comparator returns
 *    key_a - key_b.
 * - OBJ_KEY_UINT32 The key is a 32b unsigned int; the comparator returns
 *    key_a - key_b.
 * - OBJ_KEY_INT64 The key is a 32b signed long; the comparator returns
 *    key_a - key_b.
 * - OBJ_KEY_UINT64 The key is a 64b unsigned long; the comparator returns
 *    key_a - key_b.
 *
 * These macros are used as parameters to the obj_idx_create()
 * function when the index is created.
 */
typedef struct obj_key {
	uint32_t len;
	unsigned char value[];
} *obj_key_t;

/**
 * \brief Create a key
 *
 * A key has the following public definition:
 *
 * <code>
 * struct obj_key {
 *     uint32_t len;
 *     unsigned char value[];
 * };
 *
 * This is a convenience function to create a key of the specified
 * size. The memory associated with the key is not initialized. The
 * should use the obj_key_set() function to initialize the value of
 * the key.
 *
 * \parm sz	The maximum size in bytes of the key value
 * \return !0	obj_key_t pointer to the key
 * \return 0	Insufficient resources
 */
obj_key_t obj_key_new(size_t sz);

/**
 * \brief Free the memory for a key
 *
 * \param key	The key handle
 */
void obj_key_delete(obj_key_t key);

/**
 * \brief Set the value of a key
 *
 * Sets the value of the key to 'value'. The value is typed to void*
 * to make it convenient to use values of arbitrary types. The 'sz'
 * parameter is not checked with respect to the size specified when
 * the key was created.
 *
 * \param key	The key
 * \param value	The value to set the key to
 * \param sz	The size of value in bytes
 */
void obj_key_set(obj_key_t key, void *value, size_t sz);

/**
 * \brief Return the value of a key
 *
 * \param key	The key
 * \return Pointer to the value of the key
 */
static inline void *obj_key_value(obj_key_t key) { return key->value; }

/**
 * \brief Set the value of a key from a string
 *
 * \param key	The key
 * \param str	Pointer to a string
 * \return 0	if successful
 * \return -1	if there was an error converting the string to a value
 */
int obj_key_from_str(obj_idx_t idx, obj_key_t key, const char *str);

/**
 * \brief Return a string representation of the key value
 *
 * \param idx	The index handle
 * \param key	The key
 * \return A const char * representation of the key value.
 */
const char *obj_key_to_str(obj_idx_t idx, obj_key_t key);

/**
 * \brief Compare two keys using the index's compare function
 *
 * \param idx	The index handle
 * \param a	The first key
 * \param b	The second key
 * \return <0	a < b
 * \return 0	a == b
 * \return >0	a > b
 */
int obj_key_cmp(obj_idx_t idx, obj_key_t a, obj_key_t b);


/**
 * \brief Return the size of a key
 *
 * \param key	The key
 * \return The size of the key in bytes
 */
static inline int obj_key_size(obj_key_t key) { return key->len; }

/**
 * \brief Compare two keys
 *
 * Compare two keys and return:
 *
 *    < 0 if key a is less than b,
 *      0 if key a == key b
 *    > 0 if key a is greater than key b
 *
 * Note that while a difference in the length means !=, we leave it to
 * the comparator to decide how that affects order.
 *
 * \param a	The first key
 * \param len_a	The length of the key a
 * \param b	The second key
 * \param len_b	The length of the key b
 */
typedef int (*obj_idx_compare_fn_t)(obj_key_t a, obj_key_t b);

/**
 * \brief Insert an object and associated key into the index
 *
 * Insert an object and it's associated key into the index. The 'obj'
 * is the value associated with the key and there is no special
 * processing or consideration given to its value other than the value
 * cannot 0. The 'key' parameter is duplicated on entry and stored in
 * the index in its ODS store. The user can reuse the key or destroy
 * it, as the key in the index has the same value but occupies
 * different storage.
 *
 * \param idx	The index handle
 * \param key	The key
 * \param obj	The object reference
 *
 * \return 0		Success
 * \return ENOMEM	Insuffient resources
 * \return EINVAL	The idx specified is invalid or the obj
 *			specified is 0
 */
int obj_idx_insert(obj_idx_t idx, obj_key_t key, obj_ref_t obj);

/**
 * \brief Delete an object and associated key from the index
 *
 * Delete an object and it's associated key from the index. The
 * resources associated with the 'key' are released. The 'obj' is
 * the return value.
 *
 * \param idx	The index handle
 * \param key	The key
 *
 * \return !0	The obj_ref_t associated with the key.
 * \return 0	The key was not found
 */
obj_ref_t obj_idx_delete(obj_idx_t idx, obj_key_t key);

/**
 * \brief Find the object associated with the specified key
 *
 * Search the index for the specified key and return the associated
 * obj_ref_t.
 *
 * \param idx	The index handle
 * \param key	The key
 *
 * \return !0	The obj_ref_t associated with the key.
 * \return 0	The key was not found
 */
obj_ref_t obj_idx_find(obj_idx_t idx, obj_key_t key);

/**
 * \brief Find the least upper bound of the specified key
 *
 * Search the index for the least upper bound of the specified key.
 *
 * \param idx	The index handle
 * \param key	The key
 *
 * \return !0	The obj_ref_t associated with the key.
 * \return 0	There is no least-upper-bound
 */
obj_ref_t obj_idx_find_lub(obj_idx_t idx, obj_key_t key);

/**
 * \brief Find the greatest lower bound of the specified key
 *
 * Search the index for the greatest lower bound of the specified key.
 *
 * \param idx	The index handle
 * \param key	The key
 *
 * \return !0	The obj_ref_t associated with the key.
 * \return 0	The is not greatest lower bound.
 */
obj_ref_t obj_idx_find_glb(obj_idx_t idx, obj_key_t key);

/**
 * \brief Create an iterator
 *
 * An iterator logically represents a series over an index. It
 * maintains a current position that refers to an object or 0 if the
 * index is empty.
 *
 * Iterators are associated with a single index and cannot be moved
 * from one index to another.
 *
 * The current cursor position can be moved forward (lexically greater
 * keys) with the obj_iter_next() function and back (lexically lesser
 * keys) with the obj_iter_prev() function.
 *
 * \return !0 A new iterator handle
 * \return 0 Insufficient resources
 */
obj_iter_t obj_iter_new(obj_idx_t idx);

/**
 * \brief Destroy an iterator
 *
 * Release the resources associated with the iterator. This function
 * has no impact on the index itself.
 *
 * \param iter	The iterator handle
 */
void obj_iter_delete(obj_iter_t iter);

/**
 * \brief Delete the key at the current iterator position.
 *
 * Delete the key at the current iterator position. After deletion, the iterator
 * is positioned at the next key in the index or the previous key if the tail
 * was deleted.
 *
 * \param iter The iterator handle.
 */
obj_ref_t obj_iter_key_del(obj_iter_t iter);

/**
 * \brief Position an iterator at the specified key.
 *
 * Positions the iterator's cursor to the object with the specified
 * key.
 *
 * Use the \c obj_iter_key() an \c obj_iter_ref() functions to retrieve
 * the object reference and associated key.
 *
 * \param iter	The iterator handle
 * \param key	The key for the search
 * \return ENOENT The specified key was not found in the index
 * \return 0	The iterator position now points to the object associated
 *		with the specified key
 */
int obj_iter_find(obj_iter_t iter, obj_key_t key);

/**
 * \brief Position an iterator at the least-upper-bound of the \c key.
 *
 * Use the \c obj_iter_key() an \c obj_iter_ref() functions to retrieve
 * the object reference and associated key.
 *
 * \return ENOENT if there is no least-upper-bound record.
 * \return 0 if there the iterator successfully positioned at the
 *		least-upper-bound record.
 *
 */
int obj_iter_find_lub(obj_iter_t iter, obj_key_t key);

/**
 * \brief Position an iterator at the greatest-lower-bound of the \c key.
 *
 * Use the \c obj_iter_key() an \c obj_iter_ref() functions to retrieve
 * the object reference and associated key.
 *
 * \return ENOENT if there is no greatest-lower-bound record.
 * \return 0 if there the iterator successfully positioned at the
 *		least-upper-bound record.
 *
 */
int obj_iter_find_glb(obj_iter_t iter, obj_key_t key);

/**
 * \brief Position the iterator cursor at the first object in the index
 *
 * Positions the cursor at the first object in the index. Calling
 * obj_iter_prev() will return ENOENT.
 *
 * Use the obj_iter_key() an obj_iter_ref() functions to retrieve
 * the object reference and associated key.
 *
 * \param iter	The iterator handle.
 * \return ENOENT The index is empty
 * \return 0 The cursor is positioned at the first object in the index
 */
int obj_iter_begin(obj_iter_t iter);

/**
 * \brief Position the iterator cursor at the last object in the index
 *
 * Positions the cursor at the last object in the index. Calling
 * obj_iter_next() will return ENOENT.
 *
 * Use the obj_iter_key() an obj_iter_ref() functions to retrieve
 * the object reference and associated key.
 *
 * \param idx	The index handle.
 * \param iter	The iterator handle.
 * \return ENOENT The index is empty
 * \return 0 The cursor is positioned at the last object in the index
 */
int obj_iter_end(obj_iter_t iter);

/**
 * \brief Move the iterator cursor to the next object in the index
 *
 * Use the obj_iter_key() an obj_iter_ref() functions to retrieve
 * the object reference and associated key.
 *
 * \param iter	The iterator handle
 * \return ENOENT The cursor is at the end of the index
 * \return 0	The cursor is positioned at the next object
 */
int obj_iter_next(obj_iter_t iter);

/**
 * \brief Move the iterator cursor to the previous object in the index
 *
 * Use the obj_iter_key() an obj_iter_ref() functions to retrieve
 * the object reference and associated key.
 *
 * \param iter	The iterator handle
 * \return ENOENT The cursor is at the beginning of the index
 * \return 0	The cursor is positioned at the next object
 */
int obj_iter_prev(obj_iter_t iter);

/**
 * \brief Returns the key associated with current cursor position
 *
 * \param iter	The iterator handle
 * \return !0	Pointer to the key
 * \return 0	The cursor is not positioned at an object
 */
obj_key_t obj_iter_key(obj_iter_t iter);

/**
 * \brief Returns the object reference associated with current cursor position
 *
 * \param iter	The iterator handle
 * \return !0	The object reference
 * \return 0	The cursor is not positioned at an object
 */
obj_ref_t obj_iter_ref(obj_iter_t iter);

/**
 * \brief Returns the object pointer associated with current cursor position
 *
 * \param iter	The iterator handle
 * \return !0	The object pointer
 * \return 0	The cursor is not positioned at an object
 */
void *obj_iter_obj(obj_iter_t iter);

#endif
