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

#define FMT "p:x:h:c:sAB"

#define SECRETWORD "test_lookup"

#define SCHEMA_NAME "schema_test"
#define SET_NAME "set_test"
#define METRIC_NAME "metric_A"
#define SET_INFO_INT_KEY "interval"
#define SET_INFO_INT_VALUE "1000000"
#define SET_INFO_OFFSET_KEY "offset"
#define SET_INFO_OFFSET_VALUE "0"
#define SET_INFO_SYNC_KEY "sync"
#define SET_INFO_SYNC_VALUE "1"
#define SET_INFO_RESET_KEY SET_INFO_OFFSET_KEY
#define SET_INFO_RESET_NEW_VALUE "10000"
#define SET_INFO_UNSET_KEY SET_INFO_SYNC_KEY
#define SET_INFO_ORIGN_COUNT 3
#define SET_INFO_LOOKUP_LEN_1 37
#define SET_INFO_CLNT1_ADD_KEY "client1"
#define SET_INFO_CLNT1_ADD_VALUE "AGG1"
#define SET_INFO_CLNT1_RESET_KEY SET_INFO_OFFSET_KEY
#define SET_INFO_CLNT1_RESET_VALUE "20000"
#define SET_INFO_LOOKUP_COUNT_2 4
#define SET_INFO_LOOKUP_LEN_2 50
#define FOUND 1

static char *xprt;
static char *host;
static int port;
static int connect_port;
static int is_server;
static int is_A;
static int is_B;
static int alive_sec = 1800;
static int is_local;

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

typedef void (*test_fn)(ldms_set_t set);
struct clnt_ctxt {
	ldms_t x;
	ldms_set_t set;
	test_fn test_fn;
	sem_t connect_sem;
	sem_t lookup_set_info_sem;
	sem_t set_info_sem;
	sem_t wrong_exact_inst_sem;
};

static struct clnt_ctxt *clnt_ctxt_new()
{
	struct clnt_ctxt *clnt = calloc(1, sizeof(*clnt));
	if (!clnt) {
		printf("Out of memory\n");
		assert(0);
	}
	sem_init(&clnt->connect_sem, 0, 0);
	sem_init(&clnt->lookup_set_info_sem, 0, 0);
	sem_init(&clnt->set_info_sem, 0, 0);
	sem_init(&clnt->wrong_exact_inst_sem, 0, 0);
	return clnt;
}

static void usage() {
	printf(
"   Test the ldms lookup operation. WORK-IN-PROGRESS\n"
"	-p port		listener port\n"
"	-x xprt		sock, rdma, or ugni\n"
"	-s		Server mode\n"
"	-A		1st client. Connect to the server\n"
"	-B		2nd client. Connect to the 1st client\n"
"Client options:\n"
"	-h host		Host name to connect to.\n"
"	-c port		port to connect to\n"
"\n"
	);
}

static void process_args(int argc, char **argv)
{
	char op;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'p':
			port = atoi(optarg);
			break;
		case 'x':
			xprt = strdup(optarg);
			break;
		case 's':
			is_server = 1;
			break;
		case 'A':
			is_A = 1;
			break;
		case 'B':
			is_B = 1;
			break;
		case '?':
			usage();
			exit(0);
		/* Client options */
		case 'h':
			host = strdup(optarg);
			break;
		case 'c':
			connect_port = atoi(optarg);
			break;
		default:
			printf("Unrecognized option '%c'\n", op);
			usage();
			exit(1);
		}
	}
}

void check_args()
{
	if (!xprt && !port && !host && !is_server) {
		is_local = 1;
		return;
	}

	if (!xprt) {
		printf("Please specify -x\n");
		exit(1);
	}
	if (!port) {
		printf("Please specify -p\n");
		exit(1);
	}

	if (!is_server) {
		if (!host) {
			printf("please specify -h\n");
			exit(1);
		}
	}
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

int __test_ldms_set_info_traverse_cb(const char *key, const char *value, void *arg)
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

int test_ldms_set_info_traverse(ldms_set_t s)
{
	int rc;
	rc = ldms_set_info_traverse(s, __test_ldms_set_info_traverse_cb,
					LDMS_SET_INFO_F_LOCAL, NULL);
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

int test_ldms_set_info_unset(ldms_set_t s, const char *key, const char *value)
{
	struct ldms_set_info_pair *pair;
	int exp_count = s->set->local_info.count;
	size_t exp_len = s->set->local_info.len;

	__add_pair(key, NULL);
	ldms_set_info_unset(s, key);
	exp_len -= strlen(value) + 1;
	exp_len -= strlen(key) + 1;
	exp_count--;

	pair = __set_info_get(&s->set->local_info.list, key);
	if (pair) {
		printf("\n	Failed. The pair still exists after it was removed.\n");
		assert(0);
	}

	if (s->set->local_info.count != exp_count) {
		printf("\n	Failed. The number of element is not decremented\n");
		assert(0);
	}

	if (exp_len != s->set->local_info.len) {
		printf("\n	Failed. The length of strings is wrong. "
				"Expecting '%lu' vs '%lu\n", exp_len, s->set->local_info.len);
		assert(0);
	}
	return 0;
}

void test_ldms_set_info_get(ldms_set_t s, const char *key, const char *exp_value)
{
	char *value = ldms_set_info_get(s, key);
	if (0 != strcmp(value, exp_value)) {
		printf("\n	Failed. Expecting '%s' vs '%s'\n", exp_value, value);
		assert(0);
	}
	free(value);
}

struct traverse_cb_arg {
	int count;
	char **keys;
	char **values;
	int *key_marks;
};
static int __test_set_info_traverse_cb(const char *key, const char *value, void *arg)
{
	struct traverse_cb_arg *test_arg = (struct traverse_cb_arg *)arg;
	int i;

	for (i = 0; i < test_arg->count; i++) {
		if (0 == strcmp(key, test_arg->keys[i]))
			break;
	}
	if (i == test_arg->count) {
		printf("\n	Unexpected key '%s'\n", key);
		assert(0);
	}

	if (0 != strcmp(value, test_arg->values[i])) {
		printf("\n	Wrong value of key '%s'. Expecting '%s' vs '%s'\n",
						key, test_arg->values[i], value);
		assert(0);
	}

	if (test_arg->key_marks[i] == FOUND) {
		printf("\n	Found duplicated key '%s' with value '%s'\n", key, value);
		assert(0);
	}
	test_arg->key_marks[i] = FOUND;
	return 0;
}

static void __test_set_info_B(ldms_set_t set)
{
	char *value;
	int rc;

	if (SET_INFO_LOOKUP_COUNT_2 != set->set->remote_info.count) {
		printf("\n	Wrong number of key value pairs. Expecting %d vs %d\n",
				SET_INFO_ORIGN_COUNT, set->set->remote_info.count);
		assert(0);
	}

	if (SET_INFO_LOOKUP_LEN_2 != set->set->remote_info.len) {
		printf("\n	Wrong set info len. Expecting %d vs %lu\n",
					SET_INFO_LOOKUP_LEN_1, set->set->remote_info.len);
		assert(0);
	}

	value = ldms_set_info_get(set, SET_INFO_INT_KEY);
	if (!value) {
		printf("\n	Failed to get the value for key '%s'\n", SET_INFO_INT_KEY);
		assert(0);
	}
	if (0 != strcmp(value, SET_INFO_INT_VALUE)) {
		printf("\n	Wrong interval value. expecting '%s' vs '%s'\n",
							SET_INFO_INT_VALUE, value);
		assert(0);
	}
	free(value);

	value = ldms_set_info_get(set, SET_INFO_SYNC_KEY);
	if (!value) {
		printf("\n	Failed to get the value for key '%s'\n", SET_INFO_SYNC_KEY);
		assert(0);
	}
	if (0 != strcmp(value, SET_INFO_SYNC_VALUE)) {
		printf("\n	Wrong interval value. expecting '%s' vs '%s'\n",
							SET_INFO_SYNC_VALUE, value);
		assert(0);
	}
	free(value);

	value = ldms_set_info_get(set, SET_INFO_CLNT1_RESET_KEY);
	if (!value) {
		printf("\n	Failed to get the value for key '%s'\n",
						SET_INFO_CLNT1_RESET_KEY);
		assert(0);
	}
	if (0 != strcmp(value, SET_INFO_CLNT1_RESET_VALUE)) {
		printf("\n	Wrong interval value. expecting '%s' vs '%s'\n",
						SET_INFO_CLNT1_RESET_VALUE, value);
		assert(0);
	}
	free(value);

	value = ldms_set_info_get(set, SET_INFO_CLNT1_ADD_KEY);
	if (!value) {
		printf("\n	Failed to get the value for key '%s'\n",
						SET_INFO_CLNT1_ADD_KEY);
		assert(0);
	}
	if (0 != strcmp(value, SET_INFO_CLNT1_ADD_VALUE)) {
		printf("\n	Wrong interval value. expecting '%s' vs '%s'\n",
						SET_INFO_CLNT1_ADD_VALUE, value);
		assert(0);
	}
	free(value);

	char *keys[SET_INFO_LOOKUP_COUNT_2] = {SET_INFO_INT_KEY, SET_INFO_SYNC_KEY,
				SET_INFO_CLNT1_RESET_KEY, SET_INFO_CLNT1_ADD_KEY};
	char *values[SET_INFO_LOOKUP_COUNT_2] = {SET_INFO_INT_VALUE, SET_INFO_SYNC_VALUE,
				SET_INFO_CLNT1_RESET_VALUE, SET_INFO_CLNT1_ADD_VALUE};
	int key_marks[SET_INFO_LOOKUP_COUNT_2] = {0, 0, 0, 0};
	struct traverse_cb_arg test_arg = {
			.count = SET_INFO_LOOKUP_COUNT_2,
			.keys = keys,
			.values = values,
			.key_marks = key_marks};
	rc = ldms_set_info_traverse(set, __test_set_info_traverse_cb,
					LDMS_SET_INFO_F_REMOTE, (void *)&test_arg);
	if (rc) {
		printf("\n	Error %d: Failed to iterate through the set info list.\n", rc);
		assert(0);
	}
	int i;
	for (i = 0; i < test_arg.count; i++) {
		if (test_arg.key_marks[i] != FOUND) {
			printf("\n	Failed a key missing from the list. key '%s'\n", test_arg.keys[i]);
			assert(0);
		}
	}
}

static void __test_set_info_A(ldms_set_t set)
{
	char *value;
	int rc;

	if (SET_INFO_ORIGN_COUNT != set->set->remote_info.count) {
		printf("\n	Wrong number of key value pairs. Expecting %d vs %d\n",
				SET_INFO_ORIGN_COUNT, set->set->remote_info.count);
		assert(0);
	}

	if (SET_INFO_LOOKUP_LEN_1 != set->set->remote_info.len) {
		printf("\n	Wrong set info len. Expecting %d vs %lu\n",
					SET_INFO_LOOKUP_LEN_1, set->set->remote_info.len);
		assert(0);
	}

	value = ldms_set_info_get(set, SET_INFO_INT_KEY);
	if (!value) {
		printf("\n	Failed to get the value for key '%s'\n", SET_INFO_INT_KEY);
		assert(0);
	}
	if (0 != strcmp(value, SET_INFO_INT_VALUE)) {
		printf("\n	Wrong interval value. expecting '%s' vs '%s'\n",
							SET_INFO_INT_VALUE, value);
		assert(0);
	}
	free(value);

	value = ldms_set_info_get(set, SET_INFO_SYNC_KEY);
	if (!value) {
		printf("\n	Failed to get the value for key '%s'\n", SET_INFO_SYNC_KEY);
		assert(0);
	}
	if (0 != strcmp(value, SET_INFO_SYNC_VALUE)) {
		printf("\n	Wrong interval value. expecting '%s' vs '%s'\n",
							SET_INFO_SYNC_VALUE, value);
		assert(0);
	}
	free(value);
	value = ldms_set_info_get(set, SET_INFO_RESET_KEY);
	if (!value) {
		printf("\n	Failed to get the value for key '%s'\n", SET_INFO_RESET_KEY);
		assert(0);
	}
	if (0 != strcmp(value, SET_INFO_RESET_NEW_VALUE)) {
		printf("\n	Wrong interval value. expecting '%s' vs '%s'\n",
							SET_INFO_RESET_NEW_VALUE, value);
		assert(0);
	}
	free(value);

	char *keys[3] = {SET_INFO_INT_KEY, SET_INFO_OFFSET_KEY, SET_INFO_SYNC_KEY};
	char *values[3] = {SET_INFO_INT_VALUE, SET_INFO_RESET_NEW_VALUE, SET_INFO_SYNC_VALUE};
	int key_marks[3] = {0, 0, 0};
	struct traverse_cb_arg test_arg = {
			.count = 3,
			.keys = keys,
			.values = values,
			.key_marks = key_marks};
	rc = ldms_set_info_traverse(set, __test_set_info_traverse_cb,
					LDMS_SET_INFO_F_REMOTE, (void *)&test_arg);
	if (rc) {
		printf("\n	Error %d: Failed to iterate through the set info list.\n", rc);
		assert(0);
	}
	int i;
	for (i = 0; i < test_arg.count; i++) {
		if (test_arg.key_marks[i] != FOUND) {
			printf("\n	Failed a key missing from the list. key '%s'\n", test_arg.keys[i]);
			assert(0);
		}
	}
}

static void test_lookup_set_info_cb(ldms_t ldms, enum ldms_lookup_status status,
				int more, ldms_set_t set, void *arg)
{
	struct clnt_ctxt *clnt;
	char *inst_name, *schema_name, *metric_name, *str;

	if (status) {
		printf("\n	Lookup complete with an error status '%d'\n", status);
		assert(0);
	}

	clnt = (struct clnt_ctxt *)arg;
	clnt->set = set;

	inst_name = (char *)ldms_set_instance_name_get(set);
	if (0 != strcmp(inst_name, SET_NAME)) {
		printf("\n	Lookup return wrong instance name. Expecting '%s' vs '%s'\n",
								SET_NAME, inst_name);
		assert(0);
	}

	schema_name = (char *)ldms_set_schema_name_get(set);
	if (0 != strcmp(SCHEMA_NAME, schema_name)) {
		printf("\n	Lookup return wrong schema name. Expecting '%s' vs '%s'\n",
							SCHEMA_NAME, schema_name);
		assert(0);
	}

	if (1 != ldms_set_card_get(set)) {
		printf("\n	The number of metrics is wrong. Expecting %d vs %d\n",
							1, ldms_set_card_get(set));
		assert(0);
	}

	metric_name = (char *)ldms_metric_name_get(set, 0);
	if (0 != strcmp(metric_name, METRIC_NAME)) {
		printf("\n	Wrong metric name. Expecting '%s' vs '%s'\n",
						METRIC_NAME, metric_name);
		assert(0);
	}

	clnt->test_fn(set);

	printf(" ----- PASSED\n");
	sem_post(&clnt->lookup_set_info_sem);
}

static void test_lookup_set_info(struct clnt_ctxt *clnt)
{
	int rc;

	printf("Lookup a set and test the set info correctness");
	rc = ldms_xprt_lookup(clnt->x, SET_NAME, LDMS_LOOKUP_BY_INSTANCE,
						test_lookup_set_info_cb, clnt);
	if (rc) {
		printf("Error %d: Lookup failed synchronously\n", rc);
		assert(0);
	}
}

static void client_event_cb(ldms_t x, ldms_xprt_event_t e, void *arg)
{
	int rc = 0;
	struct clnt_ctxt *clnt = (struct clnt_ctxt *)arg;
	assert(clnt);

	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		printf("%d: connected\n", port);
		clnt->x = x;
		sem_post(&clnt->connect_sem);
		break;
	case LDMS_XPRT_EVENT_ERROR:
		printf("%d: conn_error\n", port);
		ldms_xprt_put(x);
		sem_post(&clnt->lookup_set_info_sem);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		printf("%d: disconnected\n", port);
		ldms_xprt_put(x);
		sem_post(&clnt->lookup_set_info_sem);
		break;
	default:
		printf("%d: Unhandled ldms event '%d'\n", port, e->type);
		exit(-1);
	}
}

static void server_event_cb(ldms_t x, ldms_xprt_event_t e, void *arg)
{
	switch (e->type) {
		case LDMS_XPRT_EVENT_DISCONNECTED:
			printf("disconnected ... exiting\n");
			exit(0);
			break;
		case LDMS_XPRT_EVENT_CONNECTED:
			printf("connected\n");
			break;
		default:
			break;
	}
}

static void do_client_A(struct sockaddr_in *listen_sin, struct sockaddr_in *connect_sin)
{
	int rc;
	ldms_t listen_ldms;
	ldms_t connect_ldms;
	struct clnt_ctxt *clnt = clnt_ctxt_new();
	clnt->test_fn = __test_set_info_A;

	listen_ldms = ldms_xprt_new(xprt, NULL);
	if (!listen_ldms) {
		printf("Failed to create ldms xprt\n");
		exit(1);
	}

	connect_ldms = ldms_xprt_new(xprt, NULL);
	if (!connect_ldms) {
		printf("Failed to create ldms xprt\n");
		exit(1);
	}

	rc = ldms_xprt_listen(listen_ldms, (void *)listen_sin, sizeof(*listen_sin),
							server_event_cb, NULL);
	if (rc) {
		printf("Error %d: Failed to listen\n", rc);
		exit(1);
	}

	rc = ldms_xprt_connect(connect_ldms, (void *)connect_sin, sizeof(*connect_sin),
							client_event_cb, clnt);
	if (rc) {
		printf("Error %d: Failed to connect to the server\n", rc);
		assert(0);
	}
	sem_wait(&clnt->connect_sem);

	test_lookup_set_info(clnt);
	sem_wait(&clnt->lookup_set_info_sem);

	printf("Adding %s:%s\n", SET_INFO_CLNT1_ADD_KEY, SET_INFO_CLNT1_ADD_VALUE);
	ldms_set_info_set(clnt->set, SET_INFO_CLNT1_ADD_KEY, SET_INFO_CLNT1_ADD_VALUE);

	printf("Replace the value %s of key %s\n",
			SET_INFO_CLNT1_RESET_VALUE, SET_INFO_CLNT1_RESET_KEY);
	ldms_set_info_set(clnt->set, SET_INFO_CLNT1_RESET_KEY, SET_INFO_CLNT1_RESET_VALUE);

	sleep(alive_sec);
}

static void do_client_B(struct sockaddr_in *listen_sin, struct sockaddr_in *connect_sin)
{
	int rc;
	ldms_t listen_ldms;
	ldms_t connect_ldms;
	struct clnt_ctxt *clnt = clnt_ctxt_new();
	clnt->test_fn = __test_set_info_B;

	listen_ldms = ldms_xprt_new(xprt, NULL);
	if (!listen_ldms) {
		printf("Failed to create ldms xprt\n");
		exit(1);
	}

	connect_ldms = ldms_xprt_new(xprt, NULL);
	if (!connect_ldms) {
		printf("Failed to create ldms xprt\n");
		exit(1);
	}

	rc = ldms_xprt_listen(listen_ldms, (void *)listen_sin, sizeof(*listen_sin),
							server_event_cb, NULL);
	if (rc) {
		printf("Error %d: Failed to listen\n", rc);
		exit(1);
	}

	rc = ldms_xprt_connect(connect_ldms, (void *)connect_sin, sizeof(*connect_sin),
							client_event_cb, clnt);
	if (rc) {
		printf("Error %d: Failed to connect to the server\n", rc);
		assert(0);
	}
	sem_wait(&clnt->connect_sem);

	test_lookup_set_info(clnt);
	sem_wait(&clnt->lookup_set_info_sem);
}

ldms_set_t do_local_info()
{
	ldms_schema_t schema;
	ldms_set_t set;
	int rc, idx, i, total_cnt;
	char key[STR_INIT_LEN];
	char value[STR_INIT_LEN];

	schema = ldms_schema_new("schema_test");
	if (!schema) {
		printf("Failed to create the schema\n");
		assert(0);
	}
	rc = ldms_schema_metric_add(schema, METRIC_NAME, LDMS_V_U64);
	if (rc < 0) {
		printf("Error %d: Failed to add the metric\n", rc);
		assert(0);
	}
	set = ldms_set_new(SET_NAME, schema);
	if (!set) {
		printf("Failed to create the set\n");
		assert(0);
	}

	total_cnt = 0;
	printf("Adding set info key value pairs");
	rc = test_ldms_set_info_set(set, SET_INFO_INT_KEY, SET_INFO_INT_VALUE);
	if (rc)
		assert(0);

	rc = test_ldms_set_info_set(set, SET_INFO_OFFSET_KEY, SET_INFO_OFFSET_VALUE);
	if (rc)
		assert(0);

	rc = test_ldms_set_info_set(set, SET_INFO_SYNC_KEY, SET_INFO_SYNC_VALUE);
	if (rc)
		assert(0);

	printf(" ----- PASSED\n");

	printf("Reset value of an existing pair");
	rc = test_ldms_set_info_reset_value(set, SET_INFO_RESET_KEY,
			SET_INFO_RESET_NEW_VALUE, SET_INFO_OFFSET_VALUE);
	if (rc)
		assert(0);
	printf(" ----- PASSED\n");

	printf("Get a value");
	test_ldms_set_info_get(set, SET_INFO_INT_KEY, SET_INFO_INT_VALUE);
	test_ldms_set_info_get(set, SET_INFO_RESET_KEY, SET_INFO_RESET_NEW_VALUE);
	printf(" ----- PASSED\n");

	printf("Unset a pair");
	rc = test_ldms_set_info_unset(set, SET_INFO_UNSET_KEY, SET_INFO_SYNC_VALUE);
	if (rc)
		assert(0);
	printf(" ----- PASSED\n");

	printf("Traverse the local set info");
	rc = test_ldms_set_info_traverse(set);
	if (rc)
		assert(0);
	printf(" ----- PASSED\n");

	ldms_schema_delete(schema);
	return set;
}

int do_server(struct sockaddr_in *sin)
{
	ldms_set_t set;
	ldms_t my_ldms;
	int rc;

	set = do_local_info();

	rc = ldms_set_info_set(set, SET_INFO_SYNC_KEY, SET_INFO_SYNC_VALUE);
	if (rc) {
		printf("failed to reset the 'sync' value\n");
		exit(1);
	}

	my_ldms = ldms_xprt_new(xprt, NULL);
	if (!my_ldms) {
		printf("Failed to create ldms xprt\n");
		return 1;
	}
	rc = ldms_xprt_listen(my_ldms, (void *)sin, sizeof(*sin), server_event_cb, NULL);
	if (rc) {
		printf("Error %d: Failed to listen\n", rc);
		exit(1);
	}

	sleep(alive_sec);
}

int main(int argc, char **argv)
{
	ldms_set_t s;
	struct sockaddr_in listen_sin = {0};
	struct sockaddr_in connect_sin = {0};

	process_args(argc, argv);
	check_args();

	LIST_INIT(&removed_pair_list);
	LIST_INIT(&replaced_pair_list);

	ldms_init(1024);

	if (is_local) {
		s = do_local_info();
		ldms_set_delete(s);
		goto done;
	} else {
		listen_sin.sin_port = htons(port);
		listen_sin.sin_family = AF_INET;

		if (connect_port) {
			connect_sin.sin_port = htons(connect_port);
			connect_sin.sin_family = AF_INET;
		}

		if (is_server) {
			do_server(&listen_sin);
		} else if (is_A) {
			do_client_A(&listen_sin, &connect_sin);
		} else if (is_B) {
			do_client_B(&listen_sin, &connect_sin);
		}
	}
done:
	printf("DONE\n");
	return 0;
}

