/*
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

/*
 * Author: Tom Tucker tom at ogc dot us
 */

#ifndef __SOS_H
#define __SOS_H

#include <stdint.h>
#include <stddef.h> // for size_t
/**
 * \mainpage Scalable Object Store Documentation
 *
 * \section intro Introduction
 *
 * The Scalable Object Storage Service is a high performance storage
 * engine designed to store structured data to persistent media very
 * efficiently. The design criteria are that objects can be searched
 * for and iterated over for a set of pre-specified object
 * attributes. Iteration can be in both the forward and backward
 * direction and the object attribute key can be formatted by the user
 * or consist of the attribute value. This allows for indexes that
 * automatically bin data, for example, an index that takes a metric
 * value and stores it such that it's key consists of it's standard
 * deviation from the mean. Any number of objects can have the same
 * key.
 *
 * # SOS Storage
 *
 * - sos_open()    Create or re-open an object store.
 * - sos_close()   Close an object store and release it's in-core resources.
 * - sos_flush()   Flush all uncommited changes to the object store to
 *		   persistent storage.
 *
 * # SOS Object Classes
 *
 * - sos_obj_new()	Create a new object
 * - sos_obj_add()	Add the object to the object store and update
 *			the associated indexes.
 * - sos_obj_delete()	Remove an object from the object store.
 *
 * An SOS Object is described by the sos_obj_class_t data type. For
 * convenience SOS objects are defined by a set of macros:
 *
 * - SOS_OBJ_BEGIN(class, name) Begins the defintion of a class
 * - SOS_OBJ_ATTR(name, type) Adds an object attribute of the specified type.
 * - SOS_OBJ_ATTR_WITH_KEY(name,type) Adds an attribute that has an index and
 * can therefore be iterated.
 * - SOS_OBJ_ATTR_WITH_UKEY(name,type,fn) Adds an attribute that has an index
 * and a user-defined function that returns the key for the attribute.
 * - SOS_OBJ_END(count) Ends the object definition.
 *
 * An example of an SOS Object follows:
 *
 *     SOS_OBJ_BEGIN(ovis_metric_class, "OvisMetric")
 *          SOS_OBJ_ATTR_WITH_KEY("tv_sec", SOS_TYPE_INT32),
 *          SOS_OBJ_ATTR("tv_usec", SOS_TYPE_INT32),
 *          SOS_OBJ_ATTR_WITH_KEY("comp_id", SOS_TYPE_INT64),
 *          SOS_OBJ_ATTR_WITH_UKEY("value", SOS_TYPE_INT64, get_value_key)
 *     SOS_OBJ_END(4);
 *
 * # SOS Object Attributes
 *
 * An object attribute is described by a handle of type
 * sos_attr_t. The attribute handle is returned by
 * sos_obj_attr_by_id(). Object attributes can also be referenced by
 * an ordinal index called the attribute Id. In general, API that take
 * an attribute index are named sos_obj_attr_xxx() and functions that
 * an sos_attr_t are named sos_attr_xxx(). For convenience, both
 * object attribute manipulation functions come in both flavors.
 *
 */

typedef struct sos_s *sos_t;
typedef struct sos_attr_s *sos_attr_t;
typedef struct sos_class_s *sos_class_t;
typedef struct sos_obj_s *sos_obj_t;
typedef struct sos_blob_arg_s *sos_blob_arg_t;

/**
 * \brief Argument for ::SOS_TYPE_BLOB__set_fn, representing blob value.
 */
struct sos_blob_arg_s {
	uint64_t len; /**< Length of the data. */
	void *data; /**< Pointer to the data. */
};

#define SOS_KEY_MAX_LEN		sizeof(uint64_t)
typedef struct sos_key_s {
	size_t keylen;
	unsigned char key[SOS_KEY_MAX_LEN];
} *sos_key_t;

typedef void *sos_oidx_t;

typedef size_t (*sos_size_fn_t)(sos_attr_t);
typedef int (*sos_cmp_key_fn_t)(sos_attr_t, sos_obj_t, sos_key_t);
typedef void (*sos_get_key_fn_t)(sos_attr_t, sos_obj_t, sos_key_t);
typedef void (*sos_set_key_fn_t)(sos_attr_t, void *, sos_key_t);
typedef void (*sos_set_fn_t)(sos_attr_t, sos_obj_t, void *);
typedef void *(*sos_get_fn_t)(sos_attr_t, sos_obj_t);

#define SOS_ATTR_NAME_LEN	32
#define SOS_CLASS_NAME_LEN	32

/** \defgroup class SOS Object Classes
 * @{
 */

enum sos_type_e {
	SOS_TYPE_INT32,
	SOS_TYPE_INT64,
	SOS_TYPE_UINT32,
	SOS_TYPE_UINT64,
	SOS_TYPE_DOUBLE,
	SOS_TYPE_BLOB,
	SOS_TYPE_USER,
	SOS_TYPE_UNKNOWN
	/* SOS_TYPE_STRING,	counted string */
};

char *sos_type_to_str(enum sos_type_e type);

struct sos_attr_s {
	char *name;
	enum sos_type_e type;
	int has_idx;
	sos_get_fn_t get_fn;
	sos_set_fn_t set_fn;
	sos_get_key_fn_t get_key_fn;
	sos_set_key_fn_t set_key_fn;
	sos_cmp_key_fn_t cmp_key_fn;
	sos_size_fn_t size_fn;
	sos_oidx_t oidx;
	uint32_t data;
	sos_t sos;
	int id;
	struct ods_s *blob_ods; /**< Pointer to the ODS that contain blobs. */
};

struct sos_class_s {
	char *name;
	uint32_t count;		/* Number of attrs in the object */
	struct sos_attr_s attrs[];
};

#define SOS_OBJ_BEGIN(_nm, _obj_nm)	\
struct sos_class_s _nm = {		\
	.name = _obj_nm,		\
	.attrs = {

#define SOS_OBJ_ATTR(_nm, _type) \
{ \
	_nm, _type, 0, \
	_type ## __get_fn, \
	_type ## __set_fn, \
	_type ## __get_key_fn, \
	_type ## __set_key_fn, \
	_type ## __cmp_key_fn, \
	_type ## __size_fn \
}

#define SOS_OBJ_ATTR_WITH_KEY(_nm, _type) \
{ \
	_nm, _type, 1,	       \
	_type ## __get_fn, \
	_type ## __set_fn, \
	_type ## __get_key_fn, \
	_type ## __set_key_fn, \
	_type ## __cmp_key_fn, \
	_type ## __size_fn \
}

#define SOS_OBJ_ATTR_WITH_UKEY(_nm, _type, _get_key_fn, _set_key_fn) \
{ \
	_nm, _type, 1, \
	_type ## __get_fn, \
	_type ## __set_fn, \
	_get_key_fn, \
	_set_key_fn, \
	_type ## __cmp_key_fn, \
	_type ## __size_fn \
}

#define SOS_OBJ_END(_cnt)	\
	},			\
	.count = _cnt		\
}

size_t SOS_TYPE_INT32__size_fn(sos_attr_t attr);
size_t SOS_TYPE_UINT32__size_fn(sos_attr_t attr);
size_t SOS_TYPE_INT64__size_fn(sos_attr_t attr);
size_t SOS_TYPE_UINT64__size_fn(sos_attr_t attr);
size_t SOS_TYPE_DOUBLE__size_fn(sos_attr_t attr);
size_t SOS_TYPE_BLOB__size_fn(sos_attr_t attr);

void SOS_TYPE_INT32__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);
void SOS_TYPE_UINT32__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);
void SOS_TYPE_INT64__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);
void SOS_TYPE_UINT64__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);
void SOS_TYPE_DOUBLE__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);
void SOS_TYPE_BLOB__get_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);

void SOS_TYPE_INT32__set_key_fn(sos_attr_t attr, void *value, sos_key_t key);
void SOS_TYPE_UINT32__set_key_fn(sos_attr_t attr, void *value, sos_key_t key);
void SOS_TYPE_INT64__set_key_fn(sos_attr_t attr, void *value, sos_key_t key);
void SOS_TYPE_UINT64__set_key_fn(sos_attr_t attr, void *value, sos_key_t key);
void SOS_TYPE_DOUBLE__set_key_fn(sos_attr_t attr, void *value, sos_key_t key);
void SOS_TYPE_BLOB__set_key_fn(sos_attr_t attr, void *value, sos_key_t key);

int SOS_TYPE_INT32__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);
int SOS_TYPE_UINT32__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);
int SOS_TYPE_INT64__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);
int SOS_TYPE_UINT64__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);
int SOS_TYPE_DOUBLE__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);
int SOS_TYPE_BLOB__cmp_key_fn(sos_attr_t attr, sos_obj_t obj, sos_key_t key);

void SOS_TYPE_INT32__set_fn(sos_attr_t attr, sos_obj_t obj, void *value);
void SOS_TYPE_UINT32__set_fn(sos_attr_t attr, sos_obj_t obj, void *value);
void SOS_TYPE_INT64__set_fn(sos_attr_t attr, sos_obj_t obj, void *value);
void SOS_TYPE_UINT64__set_fn(sos_attr_t attr, sos_obj_t obj, void *value);
void SOS_TYPE_DOUBLE__set_fn(sos_attr_t attr, sos_obj_t obj, void *value);
void SOS_TYPE_BLOB__set_fn(sos_attr_t attr, sos_obj_t obj, void *value);

void *SOS_TYPE_INT32__get_fn(sos_attr_t attr, sos_obj_t obj);
void *SOS_TYPE_UINT32__get_fn(sos_attr_t attr, sos_obj_t obj);
void *SOS_TYPE_INT64__get_fn(sos_attr_t attr, sos_obj_t obj);
void *SOS_TYPE_UINT64__get_fn(sos_attr_t attr, sos_obj_t obj);
void *SOS_TYPE_DOUBLE__get_fn(sos_attr_t attr, sos_obj_t obj);
void *SOS_TYPE_BLOB__get_fn(sos_attr_t attr, sos_obj_t obj);

/**
 * @}
 */

/** \defgroup store SOS Storage
 * @{
 */

/**
 * \brief Create/open a new Scalable Object Store
 *
 * Opens and/or creates an object store. The o_flags and o_mode
 * parameters accept the same values and have the same meaning as the
 * corresponding parameters to the open() system call.
 *
 * The o_mode and class_p parameters are only required if creating a
 * new object store and are ignored if the object store already exists.
 *
 * \param path		Pathname for files implementing the SOS
 * \param o_flags	Permission flags, refer to the open system call
 * \param o_mode	If o_flags contains O_CREAT, the mode for the new file
 * \param class_p	Class ptr if creating a new data store
 *
 * \returns sos_t 	The handle for the SOS
 * \returns NULL	If there was an error opening the SOS
 */
sos_t sos_open(const char *path, int o_flags, ...);

/**
 * \brief Delete storage associated with a Scalable Object Store
 *
 * Removes all files associated with the object store. The sos_t
 * handle must be provided (requiring an open) because it is necessary
 * to know the associated indexes in order to be able to know the
 * names of the associated files. sos_destroy will also close \c sos, as the
 * files should be closed before removed.
 *
 * \param sos_t 	The handle for the SOS
 */
sos_t sos_destroy(sos_t sos);

/**
 * \brief Close a SOS
 *
 * The \c sos_close performs an msync flushing all outstanding updates
 * to persistent storage.
 *
 * \param m	Handle for the SOS
 */
void sos_close(sos_t sos);

/**
 * \brief Flush outstanding changes to persistent storage
 *
 * \param sos Handle for the SOS
 */
void sos_flush(sos_t sos);

/**
 * @}
 */

/**
 * \defgroup objects SOS Objects
 * @{
 */

/**
 * Identifies the byte order of the objects
 */
#define SOS_OBJ_BE	1
#define SOS_OBJ_LE	2

/**
 * \brief Allocate an object from the SOS object store.
 *
 * This call will automatically extend the size of the backing store
 * to accomodate the new object. This call will fail if there is
 * insufficient disk space. Use the \c sos_obj_add to add the object
 * to the indices defined by it's object class.
 *
 * \param sos	Handle for the SOS
 * \returns Pointer to the new object
 * \returns NULL if there is an error
 */
sos_obj_t sos_obj_new(sos_t sos);

/**
 * \brief Release the storage consumed by the object in the SOS object store.
 *
 * \param sos	Handle for the SOS
 * \param obj	Pointer to the object
 */
void sos_obj_delete(sos_t sos, sos_obj_t obj);

/**
 * \brief Add an object to the SOS
 *
 * The object is added to all indices defined by it's object class.
 *
 * \param sos	Handle for the SOS
 * \param obj	Handle for the object to add
 *
 * \returns 0	Success
 * \returns -1	An error occurred. Refer to errno for detail.
 */
int sos_obj_add(sos_t s, sos_obj_t obj);

/**
 * \brief Remove an object from the SOS
 *
 * This removes an object from all indexes of which it is a
 * member. The object itself is not destroyed. Use the \c
 * sos_obj_delete function to release the storage consumed by the
 * object itself.
 *
 * \param sos	Handle for the SOS
 * \param obj	Handle for the object to remove
 *
 * \returns 0 on success.
 * \returns Error code on error.
 */
int sos_obj_remove(sos_t s, sos_obj_t obj);

/**
 * \brief Get the sos_attr_t handle for an attribute
 *
 * Returns the sos_attr_t handle for the attribute with the specified
 * name.
 * \param sos	Handle for the SOS
 * \param name	Pointer to the attribute name
 * \returns Pointer to the sos_attr_t handle
 * \returns NULL if the specified attribute does not exist.
 */
sos_attr_t sos_obj_attr_by_name(sos_t sos, const char *name);


/**
 * \brief Get the sos_attr_t handle for an attribute
 *
 * Returns the sos_attr_t handle for the attribute with the specified
 * id.
 *
 * \param sos	Handle for the SOS
 * \param attr_id The Id for the attribute.
 * \returns Pointer to the sos_attr_t handle
 * \returns NULL if the specified attribute does not exist.
 */
sos_attr_t sos_obj_attr_by_id(sos_t sos, int attr_id);

/**
 * \brief Test if an attribute has an index.
 *
 * \param sos		Handle for the SOS
 * \param attr_id	The attribute id

 * \returns !0 if the attribute has an index
 */
int sos_obj_attr_index(sos_t sos, int attr_id);

/**
 * \brief Test if an attribute has an index.
 *
 * \param attr	The sos_attr_t handle

 * \returns !0 if the attribute has an index
 */
int sos_attr_has_index(sos_attr_t attr);

/**
 * \brief Get the key for the specified attribute
 *
 * Populates the key parameter from the value in the object attribute.
 *
 * \param sos		Handle for the SOS
 * \param attr_id	The attribute Id
 * \param obj		The object
 */
void sos_obj_attr_key(sos_t sos, int attr_id, sos_obj_t obj, sos_key_t key);

/**
 * \brief Compare an attribute in an object with a key
 *
 * Compares an object attribute with a key and returns a value < 0 if
 * the attribute is less than the key, 0 if they are identical, and >
 * 0 if the attribute is greater than the key.
 *
 * \param sos		Handle for the SOS
 * \param attr_id	The attribute Id
 * \param obj		The object
 * \param key		The key with which to compare the object attribute
 *
 * \returns <0 if the key is greater than the attribute
 * \returns  0 if the key and attribute are equal
 * \returns >0 if the attribute is greater than the key
 */
int sos_obj_attr_key_cmp(sos_t sos, int attr_id,
			 sos_obj_t obj, sos_key_t key);

/**
 * \brief Get the key for the specified attribute.
 *
 * Populates the provided key with the key-value for the attribute.
 *
 * \param attr	The sos_attr_t handle
 * \param obj	The object
 * \param key	The key to populate
 *
 * \returns Pointer to the key for the attribute in the object.
 */
void sos_attr_key(sos_attr_t attr, sos_obj_t obj, sos_key_t key);

/**
 * \brief Compare an attribute in an object with a key
 *
 * Compares an object attribute with a key and returns a value < 0 if
 * the attribute is less than the key, 0 if they are identical, and >
 * 0 if the attribute is greater than the key.
 *
 * \param attr	The sos_attr_t attribute handle
 * \param obj	The object
 * \param key	The key with which to compare the object attribute
 *
 * \returns <0 if the key is greater than the attribute
 * \returns  0 if the key and attribute are equal
 * \returns >0 if the attribute is greater than the key
 */
int sos_attr_key_cmp(sos_attr_t attr, sos_obj_t obj, sos_key_t key);

/**
 * \brief Set an object attribute
 *
 * An attribute has an \c id that is the order of the attribute in the
 * object. Attribute ids start at zero.
 *
 * \param sos	Handle for the SOS
 * \param attr_id The ordinal id of the attribute in the object.
 * \param obj	The object
 * \param value	Pointer to the value
 */
void sos_obj_attr_set(sos_t sos, int attr_id, sos_obj_t obj, void *value);

/**
 * \brief Get the value of an object's attribute
 *
 * \param sos	Handle for the SOS
 * \param attr_id The ordinal id of the attribute in the object.
 * \param obj	The object
 *
 * \returns	Pointer to the attribute's value in the object
 */
void *sos_obj_attr_get(sos_t sos, int attr_id, sos_obj_t obj);
#define SOS_OBJ_ATTR_GET(_v, _s, _i, _o) \
	{ _v = *(typeof(_v) *)sos_obj_attr_get(_s, _i, _o); }

/**
 * \brief Set the value of an object's attribute
 *
 * \param attr	The sos_attr_t handle
 * \param obj	The object
 * \param value	Pointer to the value
 */
void sos_attr_set(sos_attr_t attr, sos_obj_t obj, void *value);

/**
 * \brief Get the value of an object's attribute
 *
 * \param attr	Pointer to the sos_attr_t handle
 * \param obj	The object
 *
 * \returns	Pointer to the attribute's value in the object
 */
void *sos_attr_get(sos_attr_t attr, sos_obj_t obj);
#define SOS_ATTR_GET(_v, _a, _o) \
{ \
	_v = *(typeof(_v) *)sos_attr_get(_a, _o);	\
}

void sos_attr_key_set(sos_attr_t attr, void *value, sos_key_t key);
void sos_obj_attr_key_set(sos_t sos, int attr_id, void *value, sos_key_t key);

/**
 * @}
 */

/**
 * \defgroup iter SOS Iterators
 * @{
 */
typedef struct sos_iter_s *sos_iter_t;

/**
 * \brief Create a new SOS iterator
 *
 * \param sos   Handle for the SOS
 * \param attr_id The attribute id for the index.
 *
 * \returns sos_iter_t For the specified key
 * \returns NULL       If there was an error creating the iterator. Note
 *		       that failure to find a matching object is not an
 *		       error.
 */
sos_iter_t sos_iter_new(sos_t sos, int attr_id);

/**
 * \brief Release the resources associated with a SOS iterator
 *
 * \param iter	The iterator returned by \c sos_new_iter
 */
void sos_iter_free(sos_iter_t iter);

/**
 * \brief Return the iterator name.
 *
 * An iterator inherits it's name from the associated attribute
 *
 * \param iter  Handle for the iterator.
 *
 * \returns Pointer to the attribute name for the iterator.
 */
const char *sos_iter_name(sos_iter_t iter);

/**
 * \brief Position the iterator at the specified key
 *
 * \param iter  Handle for the iterator.
 * \param key   The key for the iterator. The appropriate index will
 *		be searched to find the object that matches the key.
 *
 * \returns !0   Iterator is positioned at matching object.
 * \returns  0   No matching object was found.
 */
int sos_iter_seek(sos_iter_t iter, sos_key_t key);

/**
 * \brief Position the iterator at the infimum of the specified key.
 *
 * \param i Pointer to the iterator
 * \param key The key.
 *
 * \returns 0 if there are no infimum
 * \returns !0 if the infimum exists.
 */
uint64_t sos_iter_seek_inf(sos_iter_t i, sos_key_t key);

/**
 * \brief Position the iterator at the supremum of the specified key.
 *
 * \param i Pointer to the iterator
 * \param key The key.
 *
 * \returns 0 if there are no supremum
 * \returns !0 if the supremum exists
 */
uint64_t sos_iter_seek_sup(sos_iter_t i, sos_key_t key);

/**
 * \brief Retrieve the next tuple from the iterator
 *
 * \param i	Iterator handle
 *
 * \returns sos_obj_t  The next object
 * \returns NULL       If no more matching records were found.
 */
sos_obj_t sos_iter_next(sos_iter_t iter);

/**
 * \brief Retrieve the next tuple from the iterator
 *
 * \param i	Iterator handle
 *
 * \returns sos_obj_t  The previous object
 * \returns NULL       If no more matching records were found.
 */
sos_obj_t sos_iter_prev(sos_iter_t i);

/////////////////////////////////////////////////////////////////////
// ADDITIONAL functions implemented in sos.c but were not exported //
// Narate decided to export them as he think they are useful for   //
// programs that use SOS                                           //
/////////////////////////////////////////////////////////////////////

/**
 * Seek the iterator to the first object.
 */
void sos_iter_seek_start(sos_iter_t i);

/**
 * Seek the iterator to the last object.
 */
void sos_iter_seek_end(sos_iter_t i);

/**
 * \param k1 The first key
 * \param k2 The second key
 * \param keylen The length of the keys (bytes)
 * \returns -1 if k1 < k2
 * \returns 0 if k1==k2
 * \returns 1 if k1 > k2
 */
inline
int sos_key_cmp(unsigned char *k1, unsigned char *k2, int keylen)
{
	int i;
	for (i=0; i < keylen; i++) {
		if (k1[i] < k2[i])
			return -1;
		if (k1[i] > k2[i])
			return 1;
	}
	return 0;
}


/**
 * \defgroup helper_functions
 * \{
 * \brief These are helper functions to aid swig-generated sos.
 */

#define SOS_OBJ_ATTR_GET_DEF(_T, _N) \
inline _T sos_obj_attr_get_ ## _N (sos_t sos, int attr_id, sos_obj_t obj) \
{ \
	return *(_T*)sos_obj_attr_get(sos, attr_id, obj); \
}

SOS_OBJ_ATTR_GET_DEF(int8_t, int8)
SOS_OBJ_ATTR_GET_DEF(int16_t, int16)
SOS_OBJ_ATTR_GET_DEF(int32_t, int32)
SOS_OBJ_ATTR_GET_DEF(int64_t, int64)
SOS_OBJ_ATTR_GET_DEF(uint8_t, uint8)
SOS_OBJ_ATTR_GET_DEF(uint16_t, uint16)
SOS_OBJ_ATTR_GET_DEF(uint32_t, uint32)
SOS_OBJ_ATTR_GET_DEF(uint64_t, uint64)


#define SOS_OBJ_ATTR_SET_DEF(_T, _N) \
inline void sos_obj_attr_set_ ## _N (sos_t sos, int attr_id, sos_obj_t obj, _T value) \
{ \
	sos_obj_attr_set(sos, attr_id, obj, &value); \
}

SOS_OBJ_ATTR_SET_DEF(int8_t, int8)
SOS_OBJ_ATTR_SET_DEF(int16_t, int16)
SOS_OBJ_ATTR_SET_DEF(int32_t, int32)
SOS_OBJ_ATTR_SET_DEF(int64_t, int64)
SOS_OBJ_ATTR_SET_DEF(uint8_t, uint8)
SOS_OBJ_ATTR_SET_DEF(uint16_t, uint16)
SOS_OBJ_ATTR_SET_DEF(uint32_t, uint32)
SOS_OBJ_ATTR_SET_DEF(uint64_t, uint64)


int sos_get_attr_count(sos_t sos);
enum sos_type_e sos_get_attr_type(sos_t sos, int attr_id);
const char *sos_get_attr_name(sos_t sos, int attr_id);

void sos_key_set_int32(sos_t sos, int attr_id, int32_t value, sos_key_t key);
void sos_key_set_int64(sos_t sos, int attr_id, int64_t value, sos_key_t key);
void sos_key_set_uint32(sos_t sos, int attr_id, uint32_t value, sos_key_t key);
void sos_key_set_uint64(sos_t sos, int attr_id, uint64_t value, sos_key_t key);

/** \} (end helper_function) */

/**
 * @}
 */

#endif
