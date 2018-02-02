/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2018 Sandia Corporation. All rights reserved.
 *
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

#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <sys/queue.h>
#include "ovis_util/util.h"
#include "ldms_private.h"
#include "ldms.h"

#define STR_INIT_LEN 124
#define MAX_ELE 5
#define REMOVE_ELE 5

struct pair {
	char *key;
	char *value;
	LIST_ENTRY(pair) entry;
};
LIST_HEAD(pair_list, pair) removed_pair_list;
struct pair_list replaced_pair_list;

void __add_pair(const char *key, const char *value)
{
	struct pair_list *l;
	struct pair *pair = malloc(sizeof(*pair));
	if (!pair) {
		printf("TEST PROGRAM ERROR: out of memory\n");
		exit(ENOMEM);
	}

	pair->key = strdup(key);
	if (!pair->key) {
		printf("TEST PROGRAM ERROR: out of memory\n");
		exit(ENOMEM);
	}

	if (value) {
		l = &replaced_pair_list;
		pair->value = strdup(value);
		if (!pair->value) {
			printf("TEST PROGRAM ERROR: out of memory\n");
			exit(ENOMEM);
		}
	} else {
		l = &removed_pair_list;
		pair->value = NULL;
	}
	LIST_INSERT_HEAD(l, pair, entry);
}

struct ldms_set_info_pair *__set_info_get(struct ldms_set_info_pair_list *list,
								const char *key)
{
	int i = 0;
	struct ldms_set_info_pair *pair;

	LIST_FOREACH(pair, list, entry) {
		if (0 == strcmp(pair->key, key))
			return pair;
	}
	return NULL;
}

int __is_missing(int *missing_idx, int len, int idx)
{
	int i;
	for (i = 0; i < len; i++) {
		if (idx == missing_idx[i])
			return 1;
	}
	return 0;
}

int __test_ldms_set_info_all_cb(const char *key, const char *value, void *arg)
{
	struct pair *pair;

	LIST_FOREACH(pair, &removed_pair_list, entry) {
		if (0 == strcmp(key, pair->key)) {
			printf("\n	Removed key '%s' still existed\n", key);
			assert(0);
		}
	}

	LIST_FOREACH(pair, &replaced_pair_list, entry) {
		if (0 == strcmp(key, pair->key)) {
			if (0 != strcmp(value, pair->value)) {
				printf("\n	Key '%s' not contains new value '%s'\n",
						key, pair->value);
				assert(0);
			}
		}
	}
	return 0;
}

int test_ldms_set_info_all(ldms_set_t s)
{
	int rc;
	rc = ldms_set_info_traverse(s, __test_ldms_set_info_all_cb, LDMS_SET_INFO_F_LOCAL, NULL);
	if (rc) {
		printf("\n	Error %d: Failed to traverse the set info local\n", rc);
		assert(0);
	}
	return 0;
}

int test_ldms_set_info_set(ldms_set_t s, const char *key, const char *value)
{
	int rc;
	struct ldms_set_info_pair *pair;
	int count = s->set->local_info.count;
	size_t expecting_len = s->set->local_info.len;
	size_t key_len = strlen(key) + 1;
	size_t value_len = strlen(value) + 1;

	rc = ldms_set_info_set(s, key, value);
	if (rc) {
		printf("\n	Error %d: failed to add '%s:%s'\n", rc, key, value);
		assert(0);
	}

	pair = LIST_FIRST(&s->set->local_info.list);
	if (!pair) {
		printf("\n	Failed to add key '%s' value '%s'\n", key, value);
		assert(0);
	}

	if (0 != strcmp(key, pair->key)) {
		printf("\n	Added key string '%s' is not the given string '%s'\n",
				pair->key, key);
		assert(0);
	}

	if (0 != strcmp(value, pair->value)) {
		printf("\n	Added value string '%s' is not the given string '%s'\n",
				pair->value, value);
		assert(0);
	}

	if ((count + 1) != s->set->local_info.count) {
		printf("\n	s->set->info_avl->count is wrong. expecting %d vs %d\n",
						count + 1, s->set->local_info.count);
		assert(0);
	}

	expecting_len += key_len + value_len;
	if (expecting_len != s->set->local_info.len) {
		printf("\n	s->set->info_avl_len is wrong. expecting %lu vs %lu\n",
					expecting_len, s->set->local_info.len);
		assert(0);
	}

	return 0;
}

int test_ldms_set_info_reset_value(ldms_set_t s, const char *key,
				const char *new_value, const char *old_value)
{
	int rc;
	struct ldms_set_info_pair *pair;
	int count = s->set->local_info.count;
	size_t len = s->set->local_info.len - strlen(old_value) + strlen(new_value);

	__add_pair(key, new_value);
	rc = ldms_set_info_set(s, key, new_value);
	if (rc < 0) {
		printf("\n	Error %d: failed to replace the new_value\n", rc);
		assert(0);
	}

	pair = __set_info_get(&s->set->local_info.list, key);
	if (!pair) {
		printf("\n	Failed!. The key does not exist.\n");
		assert(0);
	}

	if (0 != strcmp(pair->value, new_value)) {
		printf("\n	Wrong new_value. expecting '%s' vs '%s'\n",
							new_value, pair->value);
		assert(0);
	}

	if (count != s->set->local_info.count) {
		printf("\n	Wrong s->set->info_avl->count. Expecting %d vs %d\n",
						count, s->set->local_info.count);
		assert(0);
	}

	if (len != s->set->local_info.len) {
		printf("\n	Wrong s->set->info_avl_len. Expecting %lu vs %lu\n",
							len, s->set->local_info.len);
		assert(0);
	}

	return 0;

}

int test_ldms_set_info_unset(ldms_set_t s, int idx)
{
	char key[STR_INIT_LEN];
	struct ldms_set_info_pair *pair;

	snprintf(key, STR_INIT_LEN-1, "key_%d", idx);
	__add_pair(key, NULL);
	ldms_set_info_unset(s, key);

	pair = __set_info_get(&s->set->local_info.list, key);
	if (pair) {
		printf("\n	Failed. The pair still exists after it was removed.\n");
		assert(0);
	}

	snprintf(key, STR_INIT_LEN-1, "key_%d", idx-1);
	pair = __set_info_get(&s->set->local_info.list, key);
	if (!pair) {
		printf("\n	Failed. The pair added before the intended pair was removed.\n");
		assert(0);
	}

	snprintf(key, STR_INIT_LEN-1, "key_%d", idx+1);
	pair = __set_info_get(&s->set->local_info.list, key);
	if (!pair) {
		printf("\n	Failed. The pair added after the intended pair was removed.\n");
		assert(0);
	}
	return 0;
}

void test_ldms_set_info_get(ldms_set_t s, int idx)
{
	char key[STR_INIT_LEN];
	char exp_value[STR_INIT_LEN];

	sprintf(key, "key_%d", idx);
	char *value = ldms_set_info_get(s, key);
	if (0 != strcmp(value, exp_value)) {
		printf("\n	Failed. Expecting '%s' vs '%s'\n", exp_value, value);
		assert(0);
	}
	free(value);
}

int main(int argc, char **argv) {
	ldms_schema_t schema;
	ldms_set_t set;
	int rc, idx, i, total_cnt;
	char key[STR_INIT_LEN];
	char value[STR_INIT_LEN];

	LIST_INIT(&removed_pair_list);
	LIST_INIT(&replaced_pair_list);

	ldms_init(1024);

	schema = ldms_schema_new("schema_test");
	if (!schema) {
		printf("Failed to create the schema\n");
		assert(0);
	}
	rc = ldms_schema_metric_add(schema, "metric_A", LDMS_V_U64);
	if (rc < 0) {
		printf("Error %d: Failed to add the metric\n", rc);
		assert(0);
	}
	set = ldms_set_new("set_test", schema);
	if (!set) {
		printf("Failed to create the set\n");
		assert(0);
	}

	total_cnt = 0;
	printf("Adding set info key value pairs");
	for (i = 0; i < MAX_ELE; i++) {
		snprintf(key, STR_INIT_LEN - 1, "key_%d", i);
		snprintf(value, STR_INIT_LEN - 1, "value_%d", i);
		printf("--%d", i);
		rc = test_ldms_set_info_set(set, key, value);
		if (rc)
			assert(0);
		total_cnt++;
	}
	printf(" ----- PASSED\n");

	printf("Reset value of an existing pair");
	char tmp[STR_INIT_LEN];
	sprintf(key, "key_%d", 0);
	sprintf(tmp, "value_%d", 0);
	sprintf(value, "new_value_%d", 0);
	rc = test_ldms_set_info_reset_value(set, key, value, tmp);
	if (rc)
		assert(0);
	printf(" ----- PASSED\n");

	printf("Get a value");
	test_ldms_set_info_get(set, 8);
	printf(" ----- PASSED\n");

	printf("Remove a pair");
	rc = test_ldms_set_info_unset(set, REMOVE_ELE);
	if (rc)
		assert(0);
	printf(" ----- PASSED\n");

	printf("Traverse the local set info");
	rc = test_ldms_set_info_all(set);
	if (rc)
		assert(0);
	printf(" ----- PASSED\n");

	ldms_set_delete(set);
	ldms_schema_delete(schema);

	printf("DONE\n");
	return 0;
}

