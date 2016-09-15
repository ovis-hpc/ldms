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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#include "baler/bmqueue.h"

bmqueue_t q;

struct my_elm {
	struct bmqueue_elm base;
	uint64_t val;
};

uint64_t dlen = 0;
uint64_t data[4*4096];

void *wrthr_proc(void *arg)
{
	bmqueue_t q = arg;
	struct my_elm elm = {0};
	uint64_t i;
	int rc;
	for (i = 0; i < 4096; i++) {
		elm.val = i;
		rc = bmqueue_enqueue(q, &elm.base);
		assert(rc == 0);
	}
	return NULL;
}

void *rdthr_proc(void *arg)
{
	uint64_t idx;
	bmqueue_t q = arg;
	struct my_elm *e;
	uint64_t i;
	int rc;
	for (i = 0; i < 4096; i++) {
		e = (void*)bmqueue_dequeue(q);
		idx = __sync_fetch_and_add(&dlen, 1);
		data[idx] = e->val;
		bmqueue_elm_put(&e->base);
	}
	return NULL;
}

int u64_cmp(const void *a, const void *b)
{
	uint64_t _a = *(uint64_t*)a;
	uint64_t _b = *(uint64_t*)b;
	if (_a < _b)
		return -1;
	if (_a > _b)
		return 1;
	return 0;
}

int main(int argc, char **argv)
{
	pthread_t wrthrx;
	pthread_t wrthr[4];
	pthread_t rdthr[4];
	int rc;
	uint64_t i;
	struct my_elm elm = {0};

	setenv("BMQUEUE_SEG_ALLOC_LEN", "1024", 1);

	/* single-threaded test */
	q = bmqueue_open("bmqueue", sizeof(struct my_elm), 1);
	assert(q);
	wrthr_proc(q);
	for (i = 0; i < 4096; i++) {
		struct my_elm *e = (void*)bmqueue_dequeue(q);
		assert(e);
		assert(e->val == i);
		bmqueue_elm_put(&e->base);
	}

	/* single writer + single reader parallel test */
	pthread_create(&wrthrx, NULL, wrthr_proc, q);
	for (i = 0; i < 4096; i++) {
		struct my_elm *e = (void*)bmqueue_dequeue(q);
		assert(e);
		assert(e->val == i);
		bmqueue_elm_put(&e->base);
	}
	pthread_join(wrthrx, NULL);

	/* multi-threaded test */
	for (i = 0; i < 4; i++) {
		pthread_create(&rdthr[i], NULL, rdthr_proc, q);
	}
	for (i = 0; i < 4; i++) {
		pthread_create(&wrthr[i], NULL, wrthr_proc, q);
	}

	for (i = 0; i < 4; i++) {
		pthread_join(rdthr[i], NULL);
		pthread_join(wrthr[i], NULL);
	}

	assert(dlen == sizeof(data)/sizeof(data[0]));
	qsort(data, dlen, sizeof(data[0]), u64_cmp);
	for (i = 0; i < dlen; i++) {
		assert(data[i] == i/4);
	}

	return 0;
}
