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

#ifndef __ODS_H
#define __ODS_H
#include <stdint.h>
#include <stdio.h>
typedef struct ods_s *ods_t;
typedef uint64_t obj_ref_t;

extern ods_t ods_open(const char *path, int o_flag, ...);
extern ods_t ods_open_sz(const char *path, int o_flag, ...);
extern void *ods_get_user_data(ods_t ods, size_t *p_sz);


#define ODS_COMMIT_ASYNC	0
#define ODS_COMMIT_SYNC		1
/**
 * \brief Commit changes to stable storage
 *
 * This function initiates and optionally waits for these changes to
 * be committed to stable storage.
 *
 * The 'flags' parameter determines whether the function returns
 * immediately or waits for changes to be commited to stable
 * storage. If flags is set to ODS_COMMIT_SYNC is set, the function
 * will wait for the commit to complete before returning to the
 * caller.
 *
 * \param ods	The ODS handle
 * \param flags	The commit flags.
 */
extern void ods_commit(ods_t ods, int flags);

/**
 * \brief Close an ODS store
 *
 * Close the ODS store and flush all commits to persistent
 * storage. If the 'flags' parameter is set to ODS_COMMIT_SYNC, the
 * function waits until the changes are commited to stable storage
 * before returning.
 *
 * This function protects against releasing a NULL store.
 *
 * \param ods	The ods handle.
 * \param flags	Set to ODS_COMMIT_SYNC for a synchronous close.
 */
extern void ods_close(ods_t ods, int flags);

/**
 * \brief Allocate an object of the requested size
 *
 * \param ods	The ODS handle
 * \param sz	The desired size
 * \return	Pointer to an object of the requested size or NULL if there
 *		is an error.
 */
extern void *ods_alloc(ods_t ods, size_t sz);

/**
 * \brief Release the resources associated with an object
 *
 * \param ods	The ODS handle
 * \param ptr	Pointer to the object
 */
extern void ods_free(ods_t ods, void *ptr);

/**
 * \brief Release the resources associated with an object
 *
 * \param ods	The ODS handle
 * \param ref	A reference to the object
 */
extern void ods_free_ref(ods_t ods, obj_ref_t ref);

/**
 * \brief Extend the object store by the specified amount
 *
 * This function increases the size of the object store by the
 * specified amount.
 *
 * \param ods	The ODS handle
 * \param sz	The requsted additional size in bytes
 * \return 0	Success
 * \return ENOMEM There was insufficient storage to satisfy the request.
 */
extern int ods_extend(ods_t ods, size_t sz);

/**
 * \brief Dump the meta data for the ODS
 *
 * This function prints information about the object store such as its
 * size and current allocated and free object locations.
 *
 * \param ods	The ODS handle
 * \param fp	The FILE* to which the information should be sent
 */
extern void ods_dump(ods_t ods, FILE *fp);

/**
 * \brief The callback function called by the ods_iter() function
 *
 * \param ods	The ODS handle
 * \param ptr	Pointer to the object
 * \param sz	The size of the object
 * \param arg	The 'arg' passed into ods_iter()
 */
typedef void (*ods_iter_fn_t)(ods_t ods, void *ptr, size_t sz, void *arg);

/**
 * \brief Iterate over all objects in the ODS
 *
 * This function iterates over all objects allocated in the ODS and
 * calls the specified 'iter_fn' for each object. See the
 * ods_iter_fn_t() for the function definition.
 *
 * \param ods		The ODS handle
 * \param iter_fn	Pointer to the function to call
 * \param arg		A void* argument that the user wants passed to
 *			the callback function.
 */
extern void ods_iter(ods_t ods, ods_iter_fn_t iter_fn, void *arg);

/**
 * \brief Return the size of an object
 *
 * This function returns the size of the object pointed to by the
 * 'obj' parameter.
 *
 * \param ods	The ODS handle
 * \param obj	Pointer to the object
 * \return sz	The size of the object in bytes
 * \return -1	The 'obj' parameter does not point to an ODS object.
 */
extern size_t ods_obj_size(ods_t ods, void *obj);

/**
 * Calculate allocation size, given the requested \a size.
 *
 * This is needed by SOS BLOB. SOS BLOB will memorize the allocated size so
 * that the space can be reused right away.
 * \param ods The ODS handle.
 * \param size The request size.
 * \return Allocation size.
 */
extern uint64_t ods_get_alloc_size(ods_t ods, uint64_t size);

/**
 * \brief Convert an object reference to a pointer
 *
 * \param ods	The ODS handle
 * \param ref	The object reference
 * \return Pointer to the object
 */
extern void *ods_obj_ref_to_ptr(ods_t ods, obj_ref_t ref);

/**
 * \brief convert an object pointer to a reference
 *
 * \param ods	The ODS handle
 * \param obj	Pointer to the object
 * \return obj_ref_t for the specified object
 */
extern obj_ref_t ods_obj_ptr_to_ref(ods_t ods, void *obj);

#endif
