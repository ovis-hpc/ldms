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
