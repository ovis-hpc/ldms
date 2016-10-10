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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "../baler/btypes.h"
#include "../baler/butils.h"
#include "../query/bquery.h"

struct bq_store *store;

struct bdstr *s0, *s1;

void bq_entry_assert_eq(struct bquery *q0, struct bquery *q1)
{
	int rc;
	struct bmsg *m0, *m1;
	bdstr_reset(s0);
	bdstr_reset(s1);
	bq_entry_print(q0, s0);
	bq_entry_print(q1, s1);
	assert(s0->str_len == s1->str_len);
	rc = strncmp(s0->str, s1->str, s0->str_len);
	assert(rc == 0);
	printf("OK: %.*s\n", (int)s0->str_len, s0->str);
}

void test_filter_fwd(const char *host_ids, const char *ptn_ids,
		 const char *ts0, const char *ts1, int count)
{
	int rc, rc0, rc1;
	struct bmsgquery *msgq, *msgq2;
	BQUERY_POS(pos);

	printf("----- %s ------\n", __func__);
	printf("host_ids: %s\n", host_ids);
	printf("ptn_ids: %s\n", ptn_ids);
	printf("ts0: %s\n", ts0);
	printf("ts1: %s\n", ts1);
	printf("count: %d\n", count);
	printf("\n");

	msgq = bmsgquery_create(store, host_ids, ptn_ids, ts0, ts1, 1, 0, &rc);
	assert(msgq);
	rc = bq_first_entry((void*)msgq);
	assert(rc == 0);

	while (count) {
		rc = bq_next_entry((void*)msgq);
		assert(rc == 0);
		count--;
	}

	rc = bq_get_pos((void*)msgq, pos);
	assert(rc == 0);

	msgq2 = bmsgquery_create(store, host_ids, ptn_ids, ts0, ts1, 1, 0, &rc);
	rc = bq_set_pos((void*)msgq2, pos);
	assert(rc == 0);
	do {
		bq_entry_assert_eq((void*)msgq, (void*)msgq2);
		rc0 = bq_next_entry((void*)msgq);
		rc1 = bq_next_entry((void*)msgq2);
	} while (rc0 == 0 && rc1 == 0);

	assert(rc0 == rc1);
	bmsgquery_destroy(msgq);
	bmsgquery_destroy(msgq2);
}

void test_filter_bwd(const char *host_ids, const char *ptn_ids,
		 const char *ts0, const char *ts1, int count)
{
	int rc, rc0, rc1;
	struct bmsgquery *msgq, *msgq2;
	BQUERY_POS(pos);

	printf("----- %s ------\n", __func__);
	printf("host_ids: %s\n", host_ids);
	printf("ptn_ids: %s\n", ptn_ids);
	printf("ts0: %s\n", ts0);
	printf("ts1: %s\n", ts1);
	printf("count: %d\n", count);
	printf("\n");

	msgq = bmsgquery_create(store, host_ids, ptn_ids, ts0, ts1, 1, 0, &rc);
	assert(msgq);
	rc = bq_last_entry((void*)msgq);
	assert(rc == 0);

	while (count) {
		rc = bq_prev_entry((void*)msgq);
		assert(rc == 0);
		count--;
	}

	rc = bq_get_pos((void*)msgq, pos);
	assert(rc == 0);

	msgq2 = bmsgquery_create(store, host_ids, ptn_ids, ts0, ts1, 1, 0, &rc);
	rc = bq_set_pos((void*)msgq2, pos);
	assert(rc == 0);
	do {
		bq_entry_assert_eq((void*)msgq, (void*)msgq2);
		rc0 = bq_prev_entry((void*)msgq);
		rc1 = bq_prev_entry((void*)msgq2);
	} while (rc0 == 0 && rc1 == 0);

	assert(rc0 == rc1);
	bmsgquery_destroy(msgq);
	bmsgquery_destroy(msgq2);
}

void test_filter_fwdbwd(const char *host_ids, const char *ptn_ids,
		 const char *ts0, const char *ts1, int count)
{
	int rc, rc0, rc1;
	struct bmsgquery *msgq, *msgq2;
	BQUERY_POS(pos);

	printf("----- %s ------\n", __func__);
	printf("host_ids: %s\n", host_ids);
	printf("ptn_ids: %s\n", ptn_ids);
	printf("ts0: %s\n", ts0);
	printf("ts1: %s\n", ts1);
	printf("count: %d\n", count);
	printf("\n");

	msgq = bmsgquery_create(store, host_ids, ptn_ids, ts0, ts1, 1, 0, &rc);
	assert(msgq);
	rc = bq_first_entry((void*)msgq);
	assert(rc == 0);

	while (count) {
		rc = bq_next_entry((void*)msgq);
		assert(rc == 0);
		count--;
	}

	rc = bq_get_pos((void*)msgq, pos);
	assert(rc == 0);

	msgq2 = bmsgquery_create(store, host_ids, ptn_ids, ts0, ts1, 1, 0, &rc);
	rc = bq_set_pos((void*)msgq2, pos);
	assert(rc == 0);
	do {
		bq_entry_assert_eq((void*)msgq, (void*)msgq2);
		rc0 = bq_prev_entry((void*)msgq);
		rc1 = bq_prev_entry((void*)msgq2);
	} while (rc0 == 0 && rc1 == 0);

	assert(rc0 == rc1);
	bmsgquery_destroy(msgq);
	bmsgquery_destroy(msgq2);
}

void test_filter_bwdfwd(const char *host_ids, const char *ptn_ids,
		 const char *ts0, const char *ts1, int count)
{
	int rc, rc0, rc1;
	struct bmsgquery *msgq, *msgq2;
	BQUERY_POS(pos);

	printf("----- %s ------\n", __func__);
	printf("host_ids: %s\n", host_ids);
	printf("ptn_ids: %s\n", ptn_ids);
	printf("ts0: %s\n", ts0);
	printf("ts1: %s\n", ts1);
	printf("count: %d\n", count);
	printf("\n");

	msgq = bmsgquery_create(store, host_ids, ptn_ids, ts0, ts1, 1, 0, &rc);
	assert(msgq);
	rc = bq_last_entry((void*)msgq);
	assert(rc == 0);

	while (count) {
		rc = bq_prev_entry((void*)msgq);
		assert(rc == 0);
		count--;
	}

	rc = bq_get_pos((void*)msgq, pos);
	assert(rc == 0);

	msgq2 = bmsgquery_create(store, host_ids, ptn_ids, ts0, ts1, 1, 0, &rc);
	rc = bq_set_pos((void*)msgq2, pos);
	assert(rc == 0);
	do {
		bq_entry_assert_eq((void*)msgq, (void*)msgq2);
		rc0 = bq_next_entry((void*)msgq);
		rc1 = bq_next_entry((void*)msgq2);
	} while (rc0 == 0 && rc1 == 0);

	assert(rc0 == rc1);
	bmsgquery_destroy(msgq);
	bmsgquery_destroy(msgq2);
}

/*
 * Verify if iterating backward works correctly.
 */
void test_filter_bwd_verify(const char *host_ids, const char *ptn_ids,
		 const char *ts0, const char *ts1, int count)
{
	char **strs = calloc(count, sizeof(char*));
	assert(strs);
	int len = 0;
	int idx;
	int rc, rc0, rc1;
	struct bmsgquery *msgq, *msgq2;
	BQUERY_POS(pos);

	printf("----- %s ------\n", __func__);
	printf("host_ids: %s\n", host_ids);
	printf("ptn_ids: %s\n", ptn_ids);
	printf("ts0: %s\n", ts0);
	printf("ts1: %s\n", ts1);
	printf("count: %d\n", count);
	printf("\n");

	msgq = bmsgquery_create(store, host_ids, ptn_ids, ts0, ts1, 1, 0, &rc);
	assert(msgq);
	rc = bq_first_entry((void*)msgq);
	assert(rc == 0);

	while (rc==0 && count) {
		strs[len++] = bq_entry_print((void*)msgq, NULL);
		rc = bq_next_entry((void*)msgq);
		count--;
	}

	if (rc) {
		/* depleted */
		rc = bq_last_entry((void*)msgq);
	} else {
		rc = bq_prev_entry((void*)msgq);
	}
	assert(rc == 0);
	idx = len;
	while (rc==0 && idx) {
		idx--;
		char *s = bq_entry_print((void*)msgq, NULL);
		assert(s);
		assert(strcmp(s, strs[idx]) == 0);
		free(s);
		rc = bq_prev_entry((void*)msgq);
	}
	assert(idx == 0);
	bmsgquery_destroy(msgq);
}

int main(int argc, char **argv)
{
	struct bmsgquery *msgq, *msgq2;
	BQUERY_POS(pos);

	s0 = bdstr_new(1024);
	s1 = bdstr_new(1024);
	assert(s0);
	assert(s1);

	store = bq_open_store("/home/narate/projects/OVIS/baler/test/indy_test/store.1");
	assert(store);

#if 1
	test_filter_bwd_verify(NULL, NULL, NULL, NULL, 50);
	test_filter_bwd_verify("1,5", NULL, NULL, NULL, 20);
	test_filter_bwd_verify(NULL, "128,130", NULL, NULL, 20);
	test_filter_bwd_verify("1-5", "128,130", NULL, NULL, 21);
	test_filter_bwd_verify("1-5", "128,130", "1435370400", NULL, 3);
	test_filter_bwd_verify("1-5", "128,130", "1435370400", NULL, 2);
	test_filter_bwd_verify("1-5", "128,130", NULL, "1435370400", 3);
	test_filter_bwd_verify("1-5", "128,130", NULL, "1435370400", 2);
	test_filter_bwd_verify("1-5", "128,130", "1435345200", "1435370400", 2);
	test_filter_bwd_verify("1-5", "128,130", "1435345200", "1435370400", 3);
#endif

#if 1
	test_filter_fwd(NULL, NULL, NULL, NULL, 50);
	test_filter_fwd("1-5", NULL, NULL, NULL, 20);
	test_filter_fwd(NULL, "128,130", NULL, NULL, 20);
	test_filter_fwd("1-5", "128,130", NULL, NULL, 21);
	test_filter_fwd("1-5", "128,130", "1435370400", NULL, 3);
	test_filter_fwd("1-5", "128,130", "1435370400", NULL, 2);
	test_filter_fwd("1-5", "128,130", NULL, "1435370400", 3);
	test_filter_fwd("1-5", "128,130", NULL, "1435370400", 2);
	test_filter_fwd("1-5", "128,130", "1435345200", "1435370400", 2);
	test_filter_fwd("1-5", "128,130", "1435345200", "1435370400", 3);
#endif

#if 1
	test_filter_bwd(NULL, NULL, NULL, NULL, 50);
	test_filter_bwd("1-5", NULL, NULL, NULL, 20);
	test_filter_bwd(NULL, "128,130", NULL, NULL, 20);
	test_filter_bwd("1-5", "128,130", NULL, NULL, 21);
	test_filter_bwd("1-5", "128,130", NULL, NULL, 3);
	test_filter_bwd("1-5", "128,130", "1435370400", NULL, 3);
	test_filter_bwd("1-5", "128,130", "1435370400", NULL, 2);
	test_filter_bwd("1-5", "128,130", NULL, "1435370400", 3);
	test_filter_bwd("1-5", "128,130", NULL, "1435370400", 2);
	test_filter_bwd("1-5", "128,130", "1435345200", "1435370400", 2);
	test_filter_bwd("1-5", "128,130", "1435345200", "1435370400", 3);
#endif

#if 1
	test_filter_fwdbwd(NULL, NULL, NULL, NULL, 50);
	test_filter_fwdbwd("1-5", NULL, NULL, NULL, 20);
	test_filter_fwdbwd(NULL, "128,130", NULL, NULL, 20);
	test_filter_fwdbwd("1-5", "128,130", NULL, NULL, 21);
	test_filter_fwdbwd("1-5", "128,130", "1435370400", NULL, 3);
	test_filter_fwdbwd("1-5", "128,130", "1435370400", NULL, 2);
	test_filter_fwdbwd("1-5", "128,130", NULL, "1435370400", 3);
	test_filter_fwdbwd("1-5", "128,130", NULL, "1435370400", 2);
	test_filter_fwdbwd("1-5", "128,130", "1435345200", "1435370400", 2);
	test_filter_fwdbwd("1-5", "128,130", "1435345200", "1435370400", 3);
#endif

	printf("WOO HOO!");
	return 0;
}
