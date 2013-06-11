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
#ifndef _OIDX_H
#define _OIDX_H
#include "ods.h"

typedef struct oidx_s *oidx_t;
typedef void *oidx_key_t;
#define OIDX_KEY_MAX_LEN 32
typedef struct oidx_iter_s *oidx_iter_t;

extern oidx_t oidx_open(const char *path, int o_flag, ...);
extern void oidx_flush(oidx_t t);
extern void oidx_close(oidx_t t);
extern uint64_t oidx_find(oidx_t t, oidx_key_t k, size_t keylen);
extern uint64_t oidx_delete(oidx_t t, oidx_key_t k, size_t keylen);
extern int oidx_add(oidx_t t, oidx_key_t key, size_t keylen, uint64_t obj);

/**
 * Unlink object from the OIDX.
 */
extern int oidx_obj_remove(oidx_t t, oidx_key_t key, size_t keylen,
		uint64_t obj);

typedef int (*oidx_walk_fn)(oidx_t oidx,
			    oidx_key_t key, size_t keylen,
			    uint64_t obj, void *context);
extern void oidx_walk(oidx_t oidx, oidx_walk_fn walk_fn, void *context);
extern oidx_iter_t oidx_iter_new(oidx_t oidx);
extern void oidx_iter_free(oidx_iter_t);

/**
 * Iterate to the next object list (of the next available key).
 * \param iter The iterator.
 * \return Reference to the next object list.
 * \return 0 if there are not more object lists.
 */
extern uint64_t oidx_iter_next_list(oidx_iter_t iter);

/**
 * Get the reference of the current list pointed by iter.
 * \return reference (ODS offset) of the list.
 * \return 0 if iter refer to no list.
 */
extern uint64_t oidx_iter_current_list(oidx_iter_t iter);

/**
 * Get the reference of the current object pointed by \a iter.
 * \return reference (ODS offset) of the object.
 * \return 0 if \a iter doesn't refer to any object.
 */
extern uint64_t oidx_iter_current_obj(oidx_iter_t iter);

/**
 * \brief Object iterator.
 * This function iterates through objects in the current list first.
 * If the list is end, it will continue on the next object list in the tree.
 * \param iter The iterator.
 * \return Reference to the object.
 * \return 0 if there are no more objects.
 */
extern uint64_t oidx_iter_next_obj(oidx_iter_t iter);

/**
 * \param iter The iterator.
 * \return Reference to the previous object list.
 * \return 0 if there are not more previous object lists.
 */
extern uint64_t oidx_iter_prev_list(oidx_iter_t iter);

/**
 * \brief Object iterator.
 * This function iterates through objects in the current list first.
 * If the list is end, it will continue on the previous object list in the tree.
 * \param iter The iterator.
 * \return Reference to the object.
 * \return 0 if there are no more objects.
 */
extern uint64_t oidx_iter_prev_obj(oidx_iter_t iter);

extern void oidx_iter_seek_start(oidx_iter_t iter);
extern void oidx_iter_seek_end(oidx_iter_t iter);
extern uint64_t oidx_iter_seek(oidx_iter_t iter, oidx_key_t key, size_t keylen);

/**
 * \brief Seek the greatest lowerbound object, relative to the given key.
 * \param iter The iterator.
 * \param key The key.
 * \param keylen The length (in bytes) of the key.
 * \return Reference to the object.
 * \return 0 if there are no such objects.
 */
extern uint64_t oidx_iter_seek_inf(oidx_iter_t iter, oidx_key_t key,
		size_t keylen);

/**
 * \brief Seek the least upperbound object, relative to the given key.
 * \param iter The iterator.
 * \param key The key.
 * \param keylen The length (in bytes) of the key.
 * \return Reference to the object.
 * \return 0 if there are no such objects.
 */
extern uint64_t oidx_iter_seek_sup(oidx_iter_t iter, oidx_key_t key,
		size_t keylen);

#endif
