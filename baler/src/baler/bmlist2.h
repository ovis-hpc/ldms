/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2016 Sandia Corporation. All rights reserved.
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
 * \file bmlist2.h
 */
#ifndef __BMLIST2_H
#define __BMLIST2_H

#include "bmem.h"

typedef struct bmlist2_entry {
	uint64_t next;
	uint64_t prev;
} *bmlist2_entry_t;

typedef struct bmlist2_head {
	uint64_t first;
	uint64_t last;
} *bmlist2_head_t;

static inline
bmlist2_entry_t BMLIST2_NEXT(bmlist2_entry_t ent, struct bmem *bmem)
{
	return BMPTR(bmem, ent->next);
}

static inline
bmlist2_entry_t BMLIST2_PREV(bmlist2_entry_t ent, struct bmem *bmem)
{
	return BMPTR(bmem, ent->prev);
}

static inline
bmlist2_entry_t BMLIST2_FIRST(bmlist2_head_t head, struct bmem *bmem)
{
	return BMPTR(bmem, head->first);
}

static inline
void BMLIST2_INSERT_HEAD(bmlist2_head_t head, bmlist2_entry_t ent,
							struct bmem *bmem)
{
	uint64_t ent_off = BMOFF(bmem, ent);
	bmlist2_entry_t first = BMPTR(bmem, head->first);
	if (first) {
		first->prev = ent_off;
	}
	ent->prev = 0;
	ent->next = head->first;
	head->first = ent_off;
	if (!head->last)
		head->last = ent_off;
}

static inline
void BMLIST2_INSERT_TAIL(bmlist2_head_t head, bmlist2_entry_t ent,
							struct bmem *bmem)
{
	uint64_t ent_off = BMOFF(bmem, ent);
	bmlist2_entry_t last = BMPTR(bmem, head->last);
	if (last) {
		last->next = ent_off;
	}
	ent->next = 0;
	ent->prev = head->last;
	head->last = ent_off;
	if (!head->first)
		head->first = ent_off;
}

/**
 * Insert \c ent after \c ent0.
 */
static inline
void BMLIST2_INSERT_AFTER(bmlist2_head_t head, bmlist2_entry_t ent0,
					bmlist2_entry_t ent, struct bmem *bmem)
{
	uint64_t ent0_off = BMOFF(bmem, ent0);
	uint64_t ent_off = BMOFF(bmem, ent);
	bmlist2_entry_t next = BMPTR(bmem, ent0->next);
	if (next) {
		next->prev = ent_off;
	}
	ent->prev = ent0_off;
	ent->next = ent0->next;
	ent0->next = ent_off;
	if (ent0_off == head->last)
		head->last = ent_off;
}

static inline
void BMLIST2_INSERT_BEFORE(bmlist2_head_t head, bmlist2_entry_t ent0,
					bmlist2_entry_t ent, struct bmem *bmem)
{
	uint64_t ent0_off = BMOFF(bmem, ent0);
	uint64_t ent_off = BMOFF(bmem, ent);
	bmlist2_entry_t prev = BMPTR(bmem, ent0->prev);
	if (prev) {
		prev->next = ent_off;
	}
	ent->next = ent0_off;
	ent->prev = ent0->prev;
	ent0->prev = ent_off;
	if (ent0_off == head->first)
		head->first = ent_off;
}

static inline
void BMLIST2_REMOVE(bmlist2_head_t head, bmlist2_entry_t ent, struct bmem *bmem)
{
	bmlist2_entry_t prev, next;
	prev = BMPTR(bmem, ent->prev);
	next = BMPTR(bmem, ent->next);

	if (prev) {
		prev->next = ent->next;
	} else {
		/* this is the first entry */
		head->first = ent->next;
	}

	if (next) {
		next->prev = ent->prev;
	} else {
		/* this is the last entry */
		head->last = ent->prev;
	}
}

#endif
