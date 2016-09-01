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

#include <assert.h>
#include "bmqueue.h"

int bmqueue_seg_init(bmqueue_seg_t seg, uint64_t elmsz, uint64_t alloc_len);

bmqueue_seg_t bmqueue_seg_open(bmqueue_t q, struct bmqueue_seg_ref *segref)
{
	int rc;
	int lenq, len_ret, len_lim;
	bmqueue_seg_t seg = calloc(1, sizeof(*seg));
	if (!seg)
		goto err0;
	lenq = strlen(q->path);
	len_lim = sizeof(q->path) - lenq;
	len_ret = snprintf(q->path+lenq, len_lim, "/%s", segref->name);
	if (len_ret >= len_lim) {
		errno = ENAMETOOLONG;
		goto err1;
	}
	seg->bmem = bmem_open(q->path);
	q->path[lenq] = 0; /* recover previous path */
	if (!seg->bmem)
		goto err1;
	seg->queue = q;
	seg->seg_ref = BMOFF(q->bmem, segref);

	return seg;

err2:
	bmem_close(seg->bmem);
err1:
	free(seg);
err0:
	return NULL;
}

void bmqueue_seg_close(bmqueue_seg_t seg)
{
	bmem_close_free(seg->bmem);
	free(seg);
}

int bmqueue_seg_init(bmqueue_seg_t seg, uint64_t elmsz, uint64_t alloc_len)
{
	int rc = 0;
	bmqueue_seg_hdr_t hdr;
	uint64_t off;
	uint64_t sz = alloc_len * elmsz + sizeof(struct bmqueue_seg_hdr);
	bmem_reset(seg->bmem);
	off = bmem_alloc(seg->bmem, sz);
	if (!off)
		return errno;
	hdr = BMQUEUE_SEG_HDR(seg);
	bzero(hdr, sizeof(struct bmqueue_seg_hdr));
	hdr->elmsz = elmsz;
	hdr->alloc_len = alloc_len;
	hdr->state = BMQUEUE_SEG_STATE_FREE;
	return 0;
}

static inline
struct bmqueue_seg_head *__bmqueue_seg_list(bmqueue_t q, bmqueue_seg_state_t s)
{
	switch (s) {
	case BMQUEUE_SEG_STATE_FREE:
		return &q->free_list;
	case BMQUEUE_SEG_STATE_PENDING:
		return &q->pending_list;
	case BMQUEUE_SEG_STATE_ACTIVE:
		return &q->active_list;
	}
	/* should not reach here! */
	bwarn("Unknown bmqueue_seg_state: %d", s);
	assert(0);
}

static inline
struct bmlist2_head *__bmqueue_seg_hdr_list(bmqueue_t q, bmqueue_seg_state_t s)
{
	switch (s) {
	case BMQUEUE_SEG_STATE_FREE:
		return &BMQUEUE_HDR(q)->free_list;
	case BMQUEUE_SEG_STATE_PENDING:
		return &BMQUEUE_HDR(q)->pending_list;
	case BMQUEUE_SEG_STATE_ACTIVE:
		return &BMQUEUE_HDR(q)->active_list;
	}
	/* should not reach here! */
	bwarn("Unknown bmqueue_seg_state: %d", s);
	assert(0);
}

/* caller must held seg->queue->mutex */
static inline
void __bmqueue_seg_state_change(bmqueue_seg_t seg,
				bmqueue_seg_state_t from,
				bmqueue_seg_state_t to)
{
	bmqueue_t q = seg->queue;
	bmqueue_seg_ref_t segref = BMPTR(q->bmem, seg->seg_ref);
	struct bmqueue_seg_head *from_head = __bmqueue_seg_list(q, from);
	struct bmqueue_seg_head *to_head = __bmqueue_seg_list(q, to);
	struct bmlist2_head *from_hdr_head = __bmqueue_seg_hdr_list(q, from);
	struct bmlist2_head *to_hdr_head = __bmqueue_seg_hdr_list(q, to);

	/* remove from pending list */
	assert(BMQUEUE_SEG_HDR(seg)->state == from);
	BMLIST2_REMOVE(from_hdr_head, &segref->ent, q->bmem);
	TAILQ_REMOVE(from_head, seg, entry);

	/* insert into free list */
	BMQUEUE_SEG_HDR(seg)->state = to;
	BMLIST2_INSERT_TAIL(to_hdr_head, &segref->ent, q->bmem);
	TAILQ_INSERT_TAIL(to_head, seg, entry);
}

static inline
void __seg_get(bmqueue_seg_t seg)
{
	__sync_add_and_fetch(&BMQUEUE_SEG_HDR(seg)->ref_count, 1);
}

static inline
void __seg_put(bmqueue_seg_t seg)
{
	uint64_t ref_count = __sync_sub_and_fetch(&BMQUEUE_SEG_HDR(seg)->ref_count, 1);
	if (ref_count)
		return;
	/* no one refer to this segment anymore, move it
	 * to the free list */
	pthread_mutex_lock(&seg->queue->mutex);
	/* but if it is active, don't free it */
	if (BMQUEUE_SEG_HDR(seg)->state != BMQUEUE_SEG_STATE_ACTIVE)
		__bmqueue_seg_state_change(seg, BMQUEUE_SEG_STATE_PENDING,
							BMQUEUE_SEG_STATE_FREE);
	pthread_mutex_unlock(&seg->queue->mutex);
}

static inline
struct bmqueue_elm *BMQUEUE_SEG_ELM(bmqueue_seg_t seg, int idx)
{
	bmqueue_seg_hdr_t hdr = BMQUEUE_SEG_HDR(seg);
	assert(hdr->elmsz);
	return (void*)hdr->data + idx*hdr->elmsz;
}

/*
 * seg->queue->mutex must be held.
 */
static inline
int __bmqueue_seg_enqueue(bmqueue_seg_t seg, const struct bmqueue_elm *elm)
{
	bmqueue_seg_hdr_t hdr = BMQUEUE_SEG_HDR(seg);
	assert(hdr->state == BMQUEUE_SEG_STATE_ACTIVE);
	if (hdr->elm_count >= hdr->alloc_len)
		return ENOMEM;
	struct bmqueue_elm *_elm;
	_elm = BMQUEUE_SEG_ELM(seg, hdr->tail);
	memcpy(_elm, elm, hdr->elmsz);
	hdr->tail = (hdr->tail + 1) % hdr->alloc_len;
	hdr->elm_count++;
	__seg_get(seg);
	return 0;
}

/*
 * seg->queue->mutex must be held.
 */
static inline
bmqueue_elm_t __bmqueue_seg_dequeue(bmqueue_seg_t seg)
{
	bmqueue_seg_hdr_t hdr = BMQUEUE_SEG_HDR(seg);
	assert(hdr->state == BMQUEUE_SEG_STATE_ACTIVE);
	struct bmqueue_elm *elm;
	if (!hdr->elm_count)
		return NULL;
	elm = BMQUEUE_SEG_ELM(seg, hdr->head);
	hdr->elm_count--;
	hdr->head = (hdr->head + 1) % hdr->alloc_len;
	elm->seg = seg;
	if (!hdr->elm_count &&
			seg != TAILQ_LAST(&seg->queue->active_list, bmqueue_seg_head)) {
		/* pending empty segment, except for the last active segment */
		__bmqueue_seg_state_change(seg, BMQUEUE_SEG_STATE_ACTIVE,
						BMQUEUE_SEG_STATE_PENDING);
	}
	return elm;
}

void bmqueue_elm_put(bmqueue_elm_t elm)
{
	__seg_put(elm->seg);
}

/**
 * Create new segment.
 */
static
bmqueue_seg_t __bmqueue_seg_new(bmqueue_t q)
{
	bmqueue_seg_t seg;
	bmqueue_seg_ref_t segref;
	uint64_t off;
	uint64_t alloc_len;
	int rc;
	off = bmem_alloc(q->bmem, sizeof(*segref));
	if (!off)
		return NULL;
	segref = BMPTR(q->bmem, off);
	BMQUEUE_HDR(q)->last_seg_num++;
	snprintf(segref->name, sizeof(segref->name), "%d", BMQUEUE_HDR(q)->last_seg_num);
	seg = bmqueue_seg_open(q, segref);
	if (!seg)
		return NULL;
	alloc_len = bgetenv_u64("BMQUEUE_SEG_ALLOC_LEN", BMQUEUE_SEG_ALLOC_LEN);
	rc = bmqueue_seg_init(seg, BMQUEUE_HDR(q)->elmsz, alloc_len);
	if (rc) {
		/* init failed */
		bmqueue_seg_close(seg);
		return NULL;
	}
	/* put into the free list */
	TAILQ_INSERT_TAIL(&q->free_list, seg, entry);
	BMLIST2_INSERT_TAIL(&BMQUEUE_HDR(q)->free_list, &segref->ent, q->bmem);
	return seg;
}

/**
 * Get a free segment and make it active.
 * \note Caller must hold \c q->mutex.
 */
static
bmqueue_seg_t __bmqueue_new_active_seg(bmqueue_t q)
{
	bmqueue_seg_t seg = TAILQ_FIRST(&q->free_list);
	if (!seg) {
		/* try opening a new one */
		seg = __bmqueue_seg_new(q);
		if (!seg)
			return NULL;
	}
	__bmqueue_seg_state_change(seg, BMQUEUE_SEG_STATE_FREE,
						BMQUEUE_SEG_STATE_ACTIVE);
	return seg;
}

int bmqueue_enqueue(bmqueue_t q, const struct bmqueue_elm *elm)
{
	int rc = 0;
	/* get last active segment */
	pthread_mutex_lock(&q->mutex);
	bmqueue_seg_t seg = TAILQ_LAST(&q->active_list, bmqueue_seg_head);
	rc = __bmqueue_seg_enqueue(seg, elm);
	if (rc) {
		/* get a free segment */
		seg = __bmqueue_new_active_seg(q);
		if (!seg) {
			rc = errno;
			goto out;
		}
		rc = __bmqueue_seg_enqueue(seg, elm);
		assert(rc == 0); /* enqueue to the new segment should always
				  * be successful */
	}
	pthread_cond_broadcast(&q->non_empty_cond);
out:
	pthread_mutex_unlock(&q->mutex);
	return rc;
}

bmqueue_elm_t bmqueue_dequeue_nonblock(bmqueue_t q)
{
	bmqueue_elm_t elm;
	bmqueue_seg_t seg;
	pthread_mutex_lock(&q->mutex);
	seg = TAILQ_FIRST(&q->active_list);
	elm = __bmqueue_seg_dequeue(seg);
	pthread_mutex_unlock(&q->mutex);
	return elm;
}

bmqueue_elm_t bmqueue_dequeue(bmqueue_t q)
{
	bmqueue_elm_t elm;
	bmqueue_seg_t seg;
	pthread_mutex_lock(&q->mutex);
again:
	seg = TAILQ_FIRST(&q->active_list);
	elm = __bmqueue_seg_dequeue(seg);
	if (!elm) {
		pthread_cond_wait(&q->non_empty_cond, &q->mutex);
		goto again;
	}
	pthread_mutex_unlock(&q->mutex);
	return elm;
}

bmqueue_t bmqueue_open(const char *path, uint64_t elmsz, int create)
{
	int len;
	int rc;
	bmqueue_seg_ref_t segref;
	bmlist2_entry_t ent;
	bmqueue_seg_t seg;
	uint64_t off;
	bmqueue_hdr_t hdr;

	if (elmsz < sizeof(struct bmqueue_elm)) {
		rc = EINVAL;
		goto err0;
	}

	bmqueue_t q = calloc(1, sizeof(*q));
	if (!q)
		goto err0;
	if (!bis_dir(path)) {
		if (!create)
			/* bis_dir() already set appropriate errno */
			goto err0;
		/* mkdir */
		rc = bmkdir_p(path, 0755);
		if (rc) {
			errno = rc;
			goto err0;
		}
	}
	len = snprintf(q->path, sizeof(q->path), "%s/hdr", path);
	if (len >= sizeof(q->path)) {
		errno = EINVAL;
		goto err1;
	}
	q->bmem = bmem_open(q->path);
	/* recover the path */
	q->path[strlen(path)] = 0;
	if (!q->bmem)
		goto err1;

	if (q->bmem->hdr->ulen == sizeof(*q->bmem->hdr)) {
		/* need initialization */
		off = bmem_alloc(q->bmem, sizeof(struct bmqueue_hdr));
		if (!off)
			goto err1;
		hdr = BMQUEUE_HDR(q);
		bzero(hdr, sizeof(*hdr));
		hdr->elmsz = elmsz;
	}

	if (BMQUEUE_HDR(q)->elmsz != elmsz) {
		rc = EINVAL;
		goto err1;
	}

	TAILQ_INIT(&q->free_list);
	TAILQ_INIT(&q->pending_list);
	TAILQ_INIT(&q->active_list);

	pthread_mutex_init(&q->mutex, NULL);
	pthread_cond_init(&q->non_empty_cond, NULL);

	/* free segments */
	for (ent = BMLIST2_FIRST(&BMQUEUE_HDR(q)->free_list, q->bmem);
			ent;
			ent = BMLIST2_NEXT(ent, q->bmem)) {
		segref = (void*)ent;
		seg = bmqueue_seg_open(q, segref);
		if (!seg)
			goto err1;
		TAILQ_INSERT_TAIL(&q->free_list, seg, entry);
	}

	/* pending segments */
	for (ent = BMLIST2_FIRST(&BMQUEUE_HDR(q)->pending_list, q->bmem);
			ent;
			ent = BMLIST2_NEXT(ent, q->bmem)) {
		segref = (void*)ent;
		seg = bmqueue_seg_open(q, segref);
		if (!seg)
			goto err1;
		TAILQ_INSERT_TAIL(&q->pending_list, seg, entry);
	}

	/* active segments */
	for (ent = BMLIST2_FIRST(&BMQUEUE_HDR(q)->active_list, q->bmem);
			ent;
			ent = BMLIST2_NEXT(ent, q->bmem)) {
		segref = (void*)ent;
		seg = bmqueue_seg_open(q, segref);
		if (!seg)
			goto err1;
		TAILQ_INSERT_TAIL(&q->active_list, seg, entry);
	}

	if (!TAILQ_FIRST(&q->active_list)) {
		/* active list is empty.
		 * The returned segment is already in active list.*/
		seg = __bmqueue_new_active_seg(q);
		if (!seg)
			goto err1;
	}

	return q;

err1:
	bmqueue_close(q);
err0:
	return NULL;
}

void bmqueue_close(bmqueue_t q)
{
	bmqueue_seg_t seg;
	while ((seg = TAILQ_FIRST(&q->active_list))) {
		TAILQ_REMOVE(&q->active_list, seg, entry);
		bmqueue_seg_close(seg);
	}
	while ((seg = TAILQ_FIRST(&q->pending_list))) {
		TAILQ_REMOVE(&q->pending_list, seg, entry);
		bmqueue_seg_close(seg);
	}
	while ((seg = TAILQ_FIRST(&q->free_list))) {
		TAILQ_REMOVE(&q->free_list, seg, entry);
		bmqueue_seg_close(seg);
	}
	if (q->bmem)
		bmem_close(q->bmem);
	free(q);
}
