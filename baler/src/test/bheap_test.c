/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2015 Sandia Corporation. All rights reserved.
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
#include <stdio.h>
#include "baler/bheap.h"
#include "baler/butils.h"

int uint_cmp(void *a, void *b)
{
	if ((uint64_t)a < (uint64_t)b)
		return -1;
	if ((uint64_t)a > (uint64_t)b)
		return 1;
	return 0;
}

int uint_cmp2(void *a, void *b)
{
	if ((uint64_t)a < (uint64_t)b)
		return 1;
	if ((uint64_t)a > (uint64_t)b)
		return -1;
	return 0;
}

void print_heap(struct bheap *h)
{
	int i;
	for (i = 0; i < h->len; i++) {
		printf("%ld, ", (uint64_t)h->array[i]);
	}
	printf("\n");
}

void test_heap(void *fn)
{
	uint64_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	int len = 10;
	int i;
	int rc;
	struct bheap *h = bheap_new(5, fn);
	if (!h) {
		berror("bheap_new()");
		exit(-1);
	}
	for (i = 0; i < len; i++) {
		rc = bheap_insert(h, (void*)data[i]);
		if (rc) {
			berror("bheap_insert()");
			exit(-1);
		}
		rc = bheap_verify(h);
		if (rc) {
			berror("bheap_verify()");
			exit(-1);
		}
		print_heap(h);
	}

	h->array[0] = (void*)11;
	bheap_percolate_top(h);
	rc = bheap_verify(h);
	if (rc) {
		berror("bheap_verify()");
		exit(-1);
	}
	print_heap(h);

	bheap_free(h);
}

int main(int argc, char **argv)
{
	test_heap(uint_cmp);
	test_heap(uint_cmp2);
	return 0;
}
