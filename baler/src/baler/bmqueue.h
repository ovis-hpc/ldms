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
 * \file bmqueue.h
 * \brief Generic queue implementation with mmap.
 */
#ifndef __BMQUEUE_H
#define __BMQUEUE_H

#include <pthread.h>
#include <sys/queue.h>

#include "bmem.h"
#include "bmlist2.h"
#include "butils.h"

#define BMQUEUE_SEG_NAME_MAX 255
#define BMQUEUE_SEG_ALLOC_LEN (1024*1024*1024)

typedef enum {
	BMQUEUE_SEG_STATE_FREE,
	BMQUEUE_SEG_STATE_PENDING,
	BMQUEUE_SEG_STATE_ACTIVE,
} bmqueue_seg_state_t;

typedef struct bmqueue_seg_hdr {
	uint64_t elmsz; /* size of a queue element */
	uint64_t alloc_len; /* in number of queue elements */
	uint64_t elm_count; /* element counter */
	uint64_t ref_count; /* reference counter */
	uint64_t head; /* the next removal position */
	uint64_t tail; /* the next insertion position */
	bmqueue_seg_state_t state; /* state of the segment */
	char data[0];
} *bmqueue_seg_hdr_t;

typedef struct bmqueue_seg {
	TAILQ_ENTRY(bmqueue_seg) entry; /* tailq entry */
	struct bmqueue *queue; /* owner of the segment */
	uint64_t seg_ref; /* reference to segref in bmqueue */
	struct bmem *bmem;
} *bmqueue_seg_t;

static inline
bmqueue_seg_hdr_t BMQUEUE_SEG_HDR(bmqueue_seg_t seg)
{
	return seg->bmem->ptr;
}

/**
 * base structure of bmqueue elements.
 *
 * \note Elements to put into bmqueue must extend this structure.
 */
typedef struct bmqueue_elm {
	bmqueue_seg_t seg;
	char data[0];
} *bmqueue_elm_t;

/**
 * This describe a reference to a bmqueue segment.
 */
typedef struct bmqueue_seg_ref {
	struct bmlist2_entry ent;
	char name[BMQUEUE_SEG_NAME_MAX+1];
} *bmqueue_seg_ref_t;

/**
 * bmqueue header structure at the beginning of the map.
 */
typedef struct bmqueue_hdr {
	uint64_t elmsz; /* queue element size */
	int last_seg_num; /* last seg number */
	struct bmlist2_head free_list; /* list of free segments */
	struct bmlist2_head pending_list; /* list of pending segments (empty
					   * segments, but elements in them
					   * are still in used) */
	struct bmlist2_head active_list; /* list of active segments */
	char data[0]; /* data contains seg references */
} *bmqueue_hdr_t;

TAILQ_HEAD(bmqueue_seg_head, bmqueue_seg);

/**
 * bmqueue handle.
 */
typedef struct bmqueue {
	pthread_mutex_t mutex;
	pthread_cond_t non_empty_cond;
	struct bmqueue_seg_head free_list;
	struct bmqueue_seg_head pending_list;
	struct bmqueue_seg_head active_list;
	struct bmem *bmem;
	char path[PATH_MAX];
} *bmqueue_t;

static inline
bmqueue_hdr_t BMQUEUE_HDR(bmqueue_t q)
{
	return q->bmem->ptr;
}

void bmqueue_elm_put(bmqueue_elm_t elm);

/**
 * Open bmqueue.
 * \param path path to the queue.
 * \param elmsz the size of an element.
 * \param create 0 - do not create if doesn't exist,
 * 		 1 - create if doesn't exist.
 * \retval ptr The ::bmqueue handle.
 */
bmqueue_t bmqueue_open(const char *path, uint64_t elmsz, int create);

/**
 * Close bmqueue.
 */
void bmqueue_close(bmqueue_t q);

/**
 * Copy-add \c elm into the queue.
 *
 * \param q the queue handle.
 * \param elm the element.
 */
int bmqueue_enqueue(bmqueue_t q, const struct bmqueue_elm *elm);

bmqueue_elm_t bmqueue_dequeue(bmqueue_t q);

bmqueue_elm_t bmqueue_dequeue_nonblock(bmqueue_t q);

#endif
