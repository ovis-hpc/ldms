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
#ifndef _OIDX_PRIV_H
#define _OIDX_PRIV_H
#include <string.h>
#include <sys/queue.h>
#include "ods.h"

struct oidx_layer_s;
typedef struct oidx_layer_s *oidx_layer_t;

#define OIDX_SIGNATURE "OIDXSIGN"
/**
 * OIDX user data definition. Used to construct in-memory version.
 */
typedef struct oidx_udata_s {
	char signature[8];
	uint64_t radix;
	uint64_t layer_sz;
	uint64_t top;
} *oidx_udata_t;

/**
 * In-memory object that refers to an Index
 */
struct oidx_s {
	size_t layer_sz;	/* The size of each layer in bytes */
	ods_t ods;		/* The ods that contains this indexes data */
	size_t udata_sz;
	oidx_udata_t udata;
};

/**
 * Prefix tree layer entry in oidx.
 */
struct oidx_entry_s {
	uint64_t obj;	/**< an object has this key, usually this points to
			    ::oidx_objref_head_s. */
	uint64_t next;	/**< this key is a prefix for a longer key also
			    in the db */
};

/**
 * Object reference list head in oidx.
 *
 * A list comprises this list structure and 0 or more list entries
 * (::oidx_objref_entry_s).
 */
typedef struct oidx_objref_head_s {
	uint64_t begin; /**< Refer to the first entry. 0 if there is no entry.*/
	uint64_t end; /**< Refer to the last entry. 0 if there is no entry. */
} *oidx_objref_head_t;

/**
 * Object reference list entry in oidx.
 */
typedef struct oidx_objref_entry_s {
	uint64_t prev; /**< previous entry. 0 if this is the first entry. */
	uint64_t objref; /**< Object reference (in SOS ODS). */
	uint64_t next; /**< next entry. 0 if this is the last entry. */
} *oidx_objref_entry_t;

/**
 * Similar LIST_FOREACH, but for oidx_objref list.
 * \param oidx The pointer to the working ::oidx_s.
 * \param head_ptr The pointer (not offset reference) to ::oidx_objref_head_s.
 * \param entry_ptr The pointer (not offset reference) to ::oidx_objref_entry_s.
 * 		    This is the variable used to iterate through the list.
 */
#define OIDX_LIST_FOREACH(oidx, head_ptr, entry_ptr) \
	for (entry_ptr = ods_obj_ref_to_ptr(oidx->ods, head_ptr->begin); \
		entry_ptr; \
		entry_ptr = ods_obj_ref_to_ptr(oidx->ods, entry_ptr->next))

/**
 * Similar to LIST_FIRST.
 * \param oidx The pointer to the working ::oidx_s.
 * \param head_ptr The pointer to ::oidx_objref_head_s.
 */
#define OIDX_LIST_FIRST(oidx, head_ptr) \
	ods_obj_ref_to_ptr(oidx->ods, head_ptr->begin)

/**
 * Similar to LIST_INSERT_HEAD.
 * \param oidx The pointer to the working ::oidx_s.
 * \param head_ptr The pointer to ::oidx_objref_head_s.
 * \param entry_ptr The pointer to ::oidx_objref_entry_s, the entry to insert
 * 		    into the list.
 */
#define OIDX_LIST_INSERT_HEAD(oidx, head_ptr, entry_ptr) do { \
	entry_ptr->next = head_ptr->begin; \
	entry_ptr->prev = 0; \
	oidx_objref_entry_t e = ods_obj_ref_to_ptr(oidx->ods, \
							head_ptr->begin); \
	head_ptr->begin = ods_obj_ptr_to_ref(oidx->ods, entry_ptr); \
	if (e) \
		e->prev = head_ptr->begin; \
} while(0)

/**
 * Similar to TAILQ_INSERT_TAIL.
 * \param oidx The pointer to the working ::oidx_s.
 * \param head_ptr The pointer to ::oidx_objref_head_s.
 * \param entry_ptr The pointer to ::oidx_objref_entry_s, the entry to insert
 * 		    into the list.
 */
#define OIDX_LIST_INSERT_TAIL(oidx, head_ptr, entry_ptr) do { \
	entry_ptr->prev = head_ptr->end; \
	entry_ptr->next = 0; \
	oidx_objref_entry_t e = ods_obj_ref_to_ptr(oidx->ods, \
							head_ptr->end); \
	head_ptr->end = ods_obj_ptr_to_ref(oidx->ods, entry_ptr); \
	if (e) \
		e->next = head_ptr->end; \
} while(0)

/**
 * \brief This is similar to LIST_REMOVE.
 *
 * This macro remove (but not delete) entry from the list.
 *
 * \param oidx The pointer to the working ::oidx_s.
 * \param head_ptr The pointer to ::oidx_objref_head_s.
 * \param entry_ptr The pointer to ::oidx_objref_entry_s, the entry to remove
 * 		    from the list.
 */
#define OIDX_LIST_REMOVE(oidx, head_ptr, entry_ptr) do { \
	oidx_objref_entry_t n,p; \
	n = ods_obj_ref_to_ptr(oidx->ods, entry_ptr->next); \
	p = ods_obj_ref_to_ptr(oidx->ods, entry_ptr->prev); \
	if (n) \
		n->prev = entry_ptr->prev; \
	else \
		head_ptr->end = entry_ptr->prev;\
	if (p) \
		p->next = entry_ptr->next; \
	else \
		head_ptr->begin = entry_ptr->next; \
} while(0)

/**
 * Prefix tree layer.
 */
struct oidx_layer_s {
	uint32_t count; /**< Reference counter. */
	struct oidx_entry_s entries[0]; /**< Entries. */
};

/**
 * OIDX iterator.
 */
struct oidx_iter_s {
	unsigned char key[OIDX_KEY_MAX_LEN];
	size_t keylen;
	oidx_layer_t layer[OIDX_KEY_MAX_LEN];
	int layer_idx[OIDX_KEY_MAX_LEN];
	int cur_layer;		/* cur index in layer array */
	oidx_t oidx;		/* the oidx */
	uint64_t cur_list_head; /**< Current list head. */
	uint64_t cur_list_entry; /**< Current object reference entry. */
};

#endif
