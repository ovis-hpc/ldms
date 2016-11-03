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
 * \file bmlist.h
 * \author Narate Taerat (narate@ogc.us)
 * \date Mar 21, 2013
 *
 * \defgroup bmlist Baler MMapped List for Mapped Memory.
 * \{
 * \brief Baler MMaped List is designed to be a linked list in mmap area.
 * It supports only insertion for now (its current use need no deletion).
 */

#ifndef _BMLIST_H
#define _BMLIST_H

#include "bcommon.h"

/**
 * Link structure for Baler MMapped List.
 */
struct bmlist_link {
	int64_t next; /**< offset to the next list entry. */
};

/**
 * \brief Baler MMapped List node that store uint32_t.
 */
struct __attribute__((packed)) bmlnode_u32 {
	uint32_t data;
	struct bmlist_link link;
};

/**
 * \brief Baler MMapped List node for bmapper hash table.
 */
struct __attribute__((packed)) bmlnode_mapper {
	uint32_t id;
	uint64_t str_off; /* need this due to aliasing */
	struct bmlist_link link;
};

/**
 * \brief A utility macro to get the next element in the list.
 */
#define BMLIST_NEXT(elm, field, bmem) BMPTR(bmem, elm->field.next)

/**
 * \brief Insert head.
 * This macro assumes that \a elm is allocated in the mmapped region.
 * \param head_off_var The offset variable. This variable will also be
 * 		changed to point (in offset sense) to \a elm.
 * \param elm The list element (e.g. node) to be inserted into the list.
 * \param field The field of type ::bmlist_link to access the link of the list.
 * \param bmem The pointer to bmem structure that stores the list.
 */
#define BMLIST_INSERT_HEAD(head_off_var, elm, field, bmem) do { \
	(elm)->field.next = (head_off_var); \
	(head_off_var) = BMOFF(bmem, elm); \
} while (0)

/**
 * \brief For each iteration for Baler Offset List.
 * \param var The variable expression for the iterator.
 * \param head_off The head of the list (being offset relative to \a relm).
 * \param field The field name to access list elements.
 * \param bmem The pointer to bmem structure that stores the list.
 */
#define BMLIST_FOREACH(var, head_off, field, bmem) \
	for ((var) = BMPTR(bmem, head_off);\
		(var); \
		(var) = BMLIST_NEXT(var, field, bmem))

/**\}*/
#endif // _BMLIST_H
