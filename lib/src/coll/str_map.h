/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013,2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013,2016 Sandia Corporation. All rights reserved.
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
 * \file str_map.h
 * \brief String-Object mapping utility.
 *
 * \c str_map is a string-object mapping utility, in which \c string is a
 * null-terminated array of \c char and object is anything that can be encoded
 * in \c uint64_t (intergers and \c void*).
 *
 */
#ifndef __STR_MAP_H
#define __STR_MAP_H
#include <sys/mman.h>
#include <sys/queue.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

LIST_HEAD(obj_list_head, obj_list);
struct obj_list {
	char *key;
	uint64_t obj;
	LIST_ENTRY(obj_list) link;
};

typedef struct str_map* str_map_t;

/**
 * \brief Create a String-Object map.
 * \param sz Hash table size (the unit is number of elements not bytes).
 * \returns NULL on error.
 * \returns Pointer to ::str_map on success.
 */
struct str_map* str_map_create(size_t sz);

/**
 * Free the map. Ignores NULL input.
 */
void str_map_free(str_map_t m);

/**
 * \param map The map.
 * \param key The key.
 * \returns \c uint64_t value assigned to the key by ::str_map_insert().
 * \note In the case of no enty, \c errno will be set to \c ENOENT and the
 * function returns \c 0. In the case that an application give value 0 in
 * ::str_map_insert(), i.e. 0 is a legit data, ::str_map_get() will return 0,
 * but \c errno is 0.
 */
uint64_t str_map_get(str_map_t map, const char *key);

/**
 * \returns 0 on success.
 * \returns Error code on error.
 */
int str_map_insert(str_map_t map, const char *key, uint64_t obj);

/**
 * \brief Remove an object (specified by key) from the map.
 * \returns 0 on success.
 * \returns Error code on error.
 */
int str_map_remove(str_map_t map, const char *key);

/**
 * \param map The empty map to be initialized.
 * \param keys The keys
 * \param nkeys The number of keys
 * \param start_id The starting number of the IDs.
 */
int str_map_id_init(str_map_t map, char **keys, int nkeys,
		    uint64_t start_id);

#endif
