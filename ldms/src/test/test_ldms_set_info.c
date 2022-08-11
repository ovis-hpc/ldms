/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018-2019 Open Grid Computing, Inc. All rights reserved.
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

#define FMT "p:x:h:c:sABE"

#define SECRETWORD "test_lookup"

#define SCHEMA_NAME "schema_test"
#define SET_NAME "set_test"
#define METRIC_NAME "metric_A"

/* server key-value */
#define SET_INFO_INT_KEY "interval"
#define SET_INFO_INT_VALUE "1000000"
#define SET_INFO_OFFSET_KEY "offset"
#define SET_INFO_OFFSET_VALUE "0"
#define SET_INFO_SYNC_KEY "sync"
#define SET_INFO_SYNC_VALUE "1"
#define SET_INFO_RESET_KEY SET_INFO_OFFSET_KEY
#define SET_INFO_RESET_NEW_VALUE "10000"
#define SET_INFO_UNSET_KEY SET_INFO_SYNC_KEY

/* clnt A msg to server key value */
#define CLNT_A_TO_SERVER_RESET_KEY SET_INFO_SYNC_KEY
#define CLNT_A_TO_SERVER_RESET_VALUE "foo"
#define CLNT_A_TO_SERVER_UNSET_KEY SET_INFO_OFFSET_KEY
#define CLNT_A_TO_SERVER_ADD_KEY "who"
#define CLNT_A_TO_SERVER_ADD_VALUE "server"


#define SET_INFO_CLNT1_ADD_KEY "client1"
#define SET_INFO_CLNT1_ADD_VALUE "AGG1"
#define SET_INFO_CLNT1_RESET_KEY SET_INFO_INT_KEY
#define SET_INFO_CLNT1_RESET_VALUE "clntA_interval"
#define FOUND 1

static char *xprt;
static char *host;
static int port;
static int connect_port;
static int is_server;
static int is_A;
static int is_B;
static int no_wait;
static int is_local;
static int IS_DONE;

static sem_t exit_sem;

struct pair {
	char *key;
	char *value;
	LIST_ENTRY(pair) entry;
};
LIST_HEAD(pair_list, pair) removed_pair_list;
struct pair_list replaced_pair_list;

struct clnt_msg {
	enum clnt_msg_type {
		RESET = 0x1,
		UNSET = 0x2,
		ADD   = 0x3,
		CHECK = 0x4,
		FWD   = 0x10,
		DONE  = 0x100,
	} type;
	char key_value[OVIS_FLEX];
};

struct exp_result {
	int count;
	char **keys;
	char **values;
	int *key_marks;
};

struct clnt;
typedef void (*test_fn)(ldms_dir_set_t dset);
typedef struct clnt {
	ldms_t x;
	const char *inst_name;
	const char *schema_name;
	ldms_set_t set;
	test_fn test_fn;
	struct clnt_msg *msg;
	sem_t connect_sem;
	sem_t dir_sem;
	sem_t dir_upd_sem;
	sem_t lookup_sem;
	sem_t set_info_sem;
	sem_t recv_sem;
	int clnt_B_do_lookup;
} *clnt_t;
clnt_t clnt;

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

static struct clnt *clnt_new()
{
	struct clnt *clnt = calloc(1, sizeof(*clnt));
	if (!clnt) {
		printf("Out of memory\n");
		assert(0);
	}
	sem_init(&clnt->connect_sem, 0, 0);
	sem_init(&clnt->dir_sem, 0, 0);
	sem_init(&clnt->dir_upd_sem, 0, 0);
	sem_init(&clnt->lookup_sem, 0, 0);
	sem_init(&clnt->set_info_sem, 0, 0);
	sem_init(&clnt->recv_sem, 0, 0);
	return clnt;
}

static int print_cb(const char *key, const char *value, void *arg)
{
	printf("	%s	%s\n", key, value);
	return 0;
}

__attribute__((unused))
static void print_set_info(ldms_set_t set)
{
	printf("local\n");
	ldms_set_info_traverse(set, print_cb, LDMS_SET_INFO_F_LOCAL, NULL);
	printf("\n");
	printf("remote\n");
	ldms_set_info_traverse(set, print_cb, LDMS_SET_INFO_F_REMOTE, NULL);
}

static void usage() {
	printf(
"   Test LDMS set_info APIs and network operations to transport the info\n"
"	-p port		listener port\n"
"	-x xprt		sock, rdma, or ugni\n"
"	-s		Server mode\n"
"	-A		1st client. Connect to the server\n"
"	-B		2nd client. Connect to the 1st client\n"
"	-E		Don't wait for a connection request\n"
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
		case 'E':
			no_wait = 1;
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

struct ldms_set_info_pair *__set_info_get(struct ldms_set_info_list *list,
								const char *key)
{
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

	rc = ldms_set_info_set(s, key, value);
	if (rc) {
		printf("\n	Error %d: failed to add '%s:%s'\n", rc, key, value);
		assert(0);
	}

	pair = LIST_FIRST(&s->local_info);
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

	return 0;
}

int test_ldms_set_info_reset_value(ldms_set_t s, const char *key,
				const char *new_value, const char *old_value)
{
	int rc;
	struct ldms_set_info_pair *pair;

	__add_pair(key, new_value);
	rc = ldms_set_info_set(s, key, new_value);
	if (rc < 0) {
		printf("\n	Error %d: failed to replace the new_value\n", rc);
		assert(0);
	}

	pair = __set_info_get(&s->local_info, key);
	if (!pair) {
		printf("\n	Failed!. The key does not exist.\n");
		assert(0);
	}

	if (0 != strcmp(pair->value, new_value)) {
		printf("\n	Wrong new_value. expecting '%s' vs '%s'\n",
							new_value, pair->value);
		assert(0);
	}
	return 0;
}

int test_ldms_set_info_unset(ldms_set_t s, const char *key, const char *value)
{
	struct ldms_set_info_pair *pair;

	__add_pair(key, NULL);
	ldms_set_info_unset(s, key);

	pair = __set_info_get(&s->local_info, key);
	if (pair) {
		printf("\n	Failed. The pair still exists after it was removed.\n");
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

static void __test_set_info_key_value_pair(const char *key, const char *value,
							struct exp_result *exp)
{
	int i;

	for (i = 0; i < exp->count; i++) {
		if (0 == strcmp(key, exp->keys[i]))
			break;
	}
	if (i == exp->count) {
		printf("\n	Unexpected key '%s'\n", key);
		assert(0);
	}

	if (0 != strcmp(value, exp->values[i])) {
		printf("\n	Wrong value of key '%s'. Expecting '%s' vs '%s'\n",
						key, exp->values[i], value);
		assert(0);
	}

	if (exp->key_marks[i] == FOUND) {
		printf("\n	Found duplicated key '%s' with value '%s'\n", key, value);
		assert(0);
	}
	exp->key_marks[i] = FOUND;
}

static  void __test_dir_set_info(ldms_dir_set_t dset, struct exp_result *exp)
{
	int i;

	for (i = 0; i < dset->info_count; i++)
		__test_set_info_key_value_pair(dset->info[i].key,
						dset->info[i].value, exp);

	for (i = 0; i < exp->count; i++) {
		if (exp->key_marks[i] != FOUND) {
			printf("\n	Failed a key missing from the list. key '%s'\n",
							exp->keys[i]);
			assert(0);
		}
	}
}

static void __test_dir_upd_add_A(ldms_dir_set_t dset)
{
	char *keys[3] = {SET_INFO_INT_KEY, CLNT_A_TO_SERVER_RESET_KEY,
						CLNT_A_TO_SERVER_ADD_KEY};
	char *values[3] = {SET_INFO_INT_VALUE, CLNT_A_TO_SERVER_RESET_VALUE,
						CLNT_A_TO_SERVER_ADD_VALUE};
	int key_marks[3] = {0, 0, 0};
	struct exp_result exp = {
			.count = 3,
			.keys = keys,
			.values = values,
			.key_marks = key_marks,
	};
	__test_dir_set_info(dset, &exp);
}

static void __test_dir_upd_unset_A(ldms_dir_set_t dset)
{
	char *keys[2] = {SET_INFO_INT_KEY, CLNT_A_TO_SERVER_RESET_KEY};
	char *values[2] = {SET_INFO_INT_VALUE, CLNT_A_TO_SERVER_RESET_VALUE};
	int key_marks[2] = {0, 0};
	struct exp_result exp = {
			.count = 2,
			.keys = keys,
			.values = values,
			.key_marks = key_marks,
	};
	__test_dir_set_info(dset, &exp);
}

static void __test_dir_upd_reset_A(ldms_dir_set_t dset)
{
	char *keys[3] = {SET_INFO_INT_KEY, SET_INFO_OFFSET_KEY,
						CLNT_A_TO_SERVER_RESET_KEY};
	char *values[3] = {SET_INFO_INT_VALUE, SET_INFO_RESET_NEW_VALUE,
						CLNT_A_TO_SERVER_RESET_VALUE};
	int key_marks[3] = {0, 0, 0};
	struct exp_result exp = {
			.count = 3,
			.keys = keys,
			.values = values,
			.key_marks = key_marks,
	};
	__test_dir_set_info(dset, &exp);
}

static void __test_dir_set_info_A(ldms_dir_set_t dset)
{
	printf("Verifying the set info at the 1st level");
	char *keys[3] = {SET_INFO_INT_KEY, SET_INFO_OFFSET_KEY, SET_INFO_SYNC_KEY};
	char *values[3] = {SET_INFO_INT_VALUE, SET_INFO_RESET_NEW_VALUE, SET_INFO_SYNC_VALUE};
	int key_marks[3] = {0, 0, 0};
	struct exp_result exp = {
			.count = 3,
			.keys = keys,
			.values = values,
			.key_marks = key_marks
	};
	__test_dir_set_info(dset, &exp);
}

static void __test_lookup_cb(ldms_t ldms, enum ldms_lookup_status status,
				int more, ldms_set_t set, void *arg)
{
	struct clnt *clnt;
	char *inst_name, *schema_name, *metric_name;

	if (status) {
		printf("\n	Lookup complete with an error status '%d'\n", status);
		assert(0);
	}

	clnt = (struct clnt *)arg;
	clnt->set = set;

	inst_name = (char *)ldms_set_instance_name_get(set);
	if (0 != strcmp(inst_name, clnt->inst_name)) {
		printf("\n	Lookup return wrong instance name. Expecting '%s' vs '%s'\n",
								SET_NAME, inst_name);
		assert(0);
	}

	schema_name = (char *)ldms_set_schema_name_get(set);
	if (0 != strcmp(clnt->schema_name, schema_name)) {
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

	sem_post(&clnt->lookup_sem);
}

static void test_lookup(struct clnt *clnt, int flags)
{
	int rc;

	rc = ldms_xprt_lookup(clnt->x, clnt->inst_name, flags,
				__test_lookup_cb, clnt);
	if (rc) {
		printf("\n	Error %d: Lookup failed synchronously\n", rc);
		assert(0);
	}
}

static void client_event_cb(ldms_t x, ldms_xprt_event_t e, void *arg)
{
	struct clnt *clnt = (struct clnt *)arg;
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
		sem_post(&clnt->lookup_sem);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		printf("%d: disconnected\n", port);
		ldms_xprt_put(x);
		sem_post(&clnt->lookup_sem);
		break;
	case LDMS_XPRT_EVENT_RECV:
		if (0 == strcmp(e->data, "done"))
			clnt->clnt_B_do_lookup = 1;
		sem_post(&clnt->recv_sem);
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		break;
	default:
		printf("%d: Unhandled ldms event '%d'\n", port, e->type);
		exit(-1);
	}
}

static void clnt_send_msg(struct clnt *clnt, enum clnt_msg_type type,
					const char *key, const char *value);
static void server_process_recv(ldms_t x, char *data, size_t data_len, ldms_set_t set)
{
	struct clnt_msg *msg = (struct clnt_msg *)data;
	char *key, *value, *endptr;

	key = strtok_r(msg->key_value, ":", &endptr);
	value = strtok_r(NULL, ":", &endptr);

	if (clnt && (msg->type & FWD)) {
		assert(clnt->x);
		msg->type = msg->type & ~FWD;
		clnt_send_msg(clnt, msg->type, key, value);
		return;
	}

	switch (msg->type) {
	case RESET:
		ldms_set_info_set(set, key, value);
		break;
	case UNSET:
		ldms_set_info_unset(set, key);
		break;
	case ADD:
		ldms_set_info_set(set, key, value);
		break;
	case CHECK:
		if (IS_DONE) {
			ldms_xprt_send(x, "done", 5);
		}
		break;
	case DONE:
		if (clnt)
			clnt_send_msg(clnt, DONE, "", "");
		sem_post(&exit_sem);
		break;
	default:
		printf("Unexpected client msg type '%d'\n", msg->type);
		assert(0);
	}
}

static void server_event_cb(ldms_t x, ldms_xprt_event_t e, void *arg)
{
	ldms_set_t set = arg;
	switch (e->type) {
		case LDMS_XPRT_EVENT_DISCONNECTED:
			printf("disconnected ... exiting\n");
			exit(0);
			break;
		case LDMS_XPRT_EVENT_CONNECTED:
			printf("connected\n");
			break;
		case LDMS_XPRT_EVENT_RECV:
			server_process_recv(x, e->data, e->data_len, set);
			break;
		case LDMS_XPRT_EVENT_SEND_COMPLETE:
			break;
		default:
			break;
	}
}

static void client_dir_cb(ldms_t x, int status, ldms_dir_t dir, void *arg)
{
	struct clnt *clnt = arg;
	char *set_name;
	sem_t *sem;

	if (status) {
		printf("Error %d: dir callback failed\n", status);
		assert(0);
	}
	if (dir->set_count != 1) {
		printf("Received unexpected number '%d' of sets "
				"from dir update\n", dir->set_count);
		assert(0);
	}
	set_name = dir->set_data[0].inst_name;
	switch (dir->type) {
	case LDMS_DIR_LIST:
		clnt->inst_name = strdup(set_name);
		if (!clnt->inst_name) {
			printf("Out of memory\n");
			assert(0);
		}
		clnt->schema_name = strdup(dir->set_data[0].schema_name);
		if (!clnt->schema_name) {
			printf("Out of memory\n");
			assert(0);
		}
		sem = &clnt->dir_sem;
		break;
	case LDMS_DIR_UPD:
		if (0 != strcmp(clnt->inst_name, set_name)) {
			printf("Receive DIR_UPD with unknown set '%s'\n", set_name);
			assert(0);
		}
		sem = &clnt->dir_upd_sem;
		break;
	default:
		printf("Received unexpected dir reply type '%d'\n", dir->type);
		assert(0);
		break;
	}
	if (clnt->test_fn)
		clnt->test_fn(&dir->set_data[0]);
	ldms_xprt_dir_free(x, dir);
	sem_post(sem);
}

static void clnt_send_msg(struct clnt *clnt, enum clnt_msg_type type,
					const char *key, const char *value)
{
	int rc;
	size_t sz;
	if (!value)
		value = "";

	sz = sizeof(*(clnt->msg)) + strlen(key) + strlen(value) + 3;
	clnt->msg = malloc(sz);
	if (!clnt->msg) {
		printf("out of memory\n");
		assert(0);
	}
	clnt->msg->type = type;
	sprintf(clnt->msg->key_value, "%s:%s", key, value);
	rc = ldms_xprt_send(clnt->x, (char *)clnt->msg, sz);
	if (rc) {
		printf("Error %d: Failed to send a message\n", rc);
		assert(0);
	}
}

static void do_client_A(struct sockaddr_in *listen_sin, struct sockaddr_in *connect_sin)
{
	int rc;
	ldms_t listen_ldms;
	ldms_t connect_ldms;

	sem_init(&exit_sem, 0, 0);
	clnt = clnt_new();
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

	clnt->test_fn = __test_dir_set_info_A;
	rc = ldms_xprt_dir(clnt->x, client_dir_cb, (void *)clnt, LDMS_DIR_F_NOTIFY);
	if (rc) {
		printf("Error %d: ldms_xprt_dir synchronously failed\n", rc);
		assert(0);
	}
	sem_wait(&clnt->dir_sem);
	printf(" ----- PASSED\n");

	printf("Server resetting a key");
	clnt->test_fn = __test_dir_upd_reset_A;
	clnt_send_msg(clnt, RESET,
		CLNT_A_TO_SERVER_RESET_KEY, CLNT_A_TO_SERVER_RESET_VALUE);
	sem_wait(&clnt->dir_upd_sem);
	free(clnt->msg);
	printf(" ----- PASSED\n");

	printf("Server unset a key");
	clnt->test_fn = __test_dir_upd_unset_A;
	clnt_send_msg(clnt, UNSET, CLNT_A_TO_SERVER_UNSET_KEY, "");
	sem_wait(&clnt->dir_upd_sem);
	free(clnt->msg);
	printf(" ----- PASSED\n");

	printf("Server add a key");
	clnt->test_fn = __test_dir_upd_add_A;
	clnt_send_msg(clnt, ADD, CLNT_A_TO_SERVER_ADD_KEY, CLNT_A_TO_SERVER_ADD_VALUE);
	sem_wait(&clnt->dir_upd_sem);
	free(clnt->msg);
	printf(" ----- PASSED\n");

	clnt->test_fn = NULL;
	test_lookup(clnt, LDMS_LOOKUP_BY_INSTANCE);
	sem_wait(&clnt->lookup_sem);

	char *value;
	printf("Adding a key");
	ldms_set_info_set(clnt->set, SET_INFO_CLNT1_ADD_KEY, SET_INFO_CLNT1_ADD_VALUE);
	value = ldms_set_info_get(clnt->set, SET_INFO_CLNT1_ADD_KEY);
	if (0 != strcmp(value, SET_INFO_CLNT1_ADD_VALUE)) {
		printf("\n	Failed. Expecting '%s' vs '%s'\n",
				SET_INFO_CLNT1_ADD_VALUE, value);
		assert(0);
	}
	free(value);
	printf(" ----- PASSED\n");

	printf("Add a key that is already in the remote list");
	ldms_set_info_set(clnt->set, SET_INFO_CLNT1_RESET_KEY, SET_INFO_CLNT1_RESET_VALUE);
	value = ldms_set_info_get(clnt->set, SET_INFO_CLNT1_RESET_KEY);
	if (0 != strcmp(value, SET_INFO_CLNT1_RESET_VALUE)) {
		printf("\n	Failed. Expecting '%s' vs '%s'\n",
				SET_INFO_CLNT1_RESET_VALUE, value);
		assert(0);
	}
	free(value);
	printf(" ----- PASSED\n");

	printf("Unset a key that appears in both local and remote list");
	ldms_set_info_unset(clnt->set, SET_INFO_CLNT1_RESET_KEY);
	value = ldms_set_info_get(clnt->set, SET_INFO_CLNT1_RESET_KEY);
	if (!value) {
		printf("\n	Failed. Cannot find the value although "
					"it presents in the remote list.\n");
		assert(0);
	}
	if (0 != strcmp(value, SET_INFO_INT_VALUE)) {
		printf("\n	Failed. Expecting '%s' vs '%s'\n",
				SET_INFO_INT_VALUE, value);
		assert(0);
	}
	free(value);
	printf(" ----- PASSED\n");

	IS_DONE = 1;
	printf("DONE\n");
	if (no_wait)
		return;
	printf("Waiting for clients\n");
	sem_wait(&exit_sem);
}

static void __test_set_info_B(ldms_dir_set_t dset)
{
	printf("Verifying the set_info at the 2nd level");
	char *keys[4] = {SET_INFO_CLNT1_RESET_KEY, SET_INFO_CLNT1_ADD_KEY,
				SET_INFO_SYNC_KEY, CLNT_A_TO_SERVER_ADD_KEY};
	char *values[4] = {SET_INFO_INT_VALUE, SET_INFO_CLNT1_ADD_VALUE,
				CLNT_A_TO_SERVER_RESET_VALUE, CLNT_A_TO_SERVER_ADD_VALUE};
	int key_marks[4] = {0, 0, 0, 0};
	struct exp_result exp = {
			.count = 4,
			.keys = keys,
			.values = values,
			.key_marks = key_marks
	};
	__test_dir_set_info(dset, &exp);
}

#define CLNT_B_PROP_UPD_RESET_KEY   "who"
#define CLNT_B_PROP_UPD_RESET_VALUE "set_origin"
static void __test_dir_upd_prop_reset_B(ldms_dir_set_t dset)
{
	char *keys[4] = {SET_INFO_CLNT1_RESET_KEY, SET_INFO_CLNT1_ADD_KEY,
				SET_INFO_SYNC_KEY, CLNT_B_PROP_UPD_RESET_KEY};
	char *values[4] = {SET_INFO_INT_VALUE, SET_INFO_CLNT1_ADD_VALUE,
				CLNT_A_TO_SERVER_RESET_VALUE, CLNT_B_PROP_UPD_RESET_VALUE};
	int key_marks[4] = {0, 0, 0, 0};
	struct exp_result exp = {
			.count = 4,
			.keys = keys,
			.values = values,
			.key_marks = key_marks
	};
	__test_dir_set_info(dset, &exp);
}

#define CLNT_B_PROP_UPD_UNSET_KEY   "sync"
static void __test_dir_upd_prop_unset_B(ldms_dir_set_t dset)
{
	char *keys[3] = {SET_INFO_CLNT1_RESET_KEY, SET_INFO_CLNT1_ADD_KEY,
				CLNT_B_PROP_UPD_RESET_KEY};
	char *values[3] = {SET_INFO_INT_VALUE, SET_INFO_CLNT1_ADD_VALUE,
				CLNT_B_PROP_UPD_RESET_VALUE};
	int key_marks[3] = {0, 0, 0};
	struct exp_result exp = {
			.count = 3,
			.keys = keys,
			.values = values,
			.key_marks = key_marks
	};
	__test_dir_set_info(dset, &exp);
}

#define CLNT_B_PROP_UPD_ADD_KEY    "B_add"
#define CLNT_B_PROP_UPD_ADD_VALUE  "this_value"
static void __test_dir_upd_prop_add_B(ldms_dir_set_t dset)
{
	char *keys[4] = {SET_INFO_CLNT1_RESET_KEY, SET_INFO_CLNT1_ADD_KEY,
				CLNT_B_PROP_UPD_RESET_KEY,
				CLNT_B_PROP_UPD_ADD_KEY};
	char *values[4] = {SET_INFO_INT_VALUE, SET_INFO_CLNT1_ADD_VALUE,
				CLNT_B_PROP_UPD_RESET_VALUE,
				CLNT_B_PROP_UPD_ADD_VALUE};
	int key_marks[4] = {0, 0, 0, 0};
	struct exp_result exp = {
			.count = 4,
			.keys = keys,
			.values = values,
			.key_marks = key_marks
	};
	__test_dir_set_info(dset, &exp);
}

static void do_client_B(struct sockaddr_in *listen_sin, struct sockaddr_in *connect_sin)
{
	int rc;
	ldms_t listen_ldms;
	ldms_t connect_ldms_server;
	clnt = clnt_new();

	listen_ldms = ldms_xprt_new(xprt, NULL);
	if (!listen_ldms) {
		printf("Failed to create ldms xprt\n");
		exit(1);
	}

	connect_ldms_server = ldms_xprt_new(xprt, NULL);
	if (!connect_ldms_server) {
		printf("Failed to create ldms xprt\n");
		exit(1);
	}

	rc = ldms_xprt_listen(listen_ldms, (void *)listen_sin, sizeof(*listen_sin),
							server_event_cb, NULL);
	if (rc) {
		printf("Error %d: Failed to listen\n", rc);
		exit(1);
	}

	rc = ldms_xprt_connect(connect_ldms_server, (void *)connect_sin, sizeof(*connect_sin),
							client_event_cb, clnt);
	if (rc) {
		printf("Error %d: Failed to connect to the server\n", rc);
		assert(0);
	}
	sem_wait(&clnt->connect_sem);

	clnt_send_msg(clnt, CHECK, "", "");
	sem_wait(&clnt->recv_sem);
	free(clnt->msg);
	while (!clnt->clnt_B_do_lookup) {
		sleep(1);
		clnt_send_msg(clnt, CHECK, "", "");
		sem_wait(&clnt->recv_sem);
		free(clnt->msg);
	}

	clnt->test_fn = __test_set_info_B;
	rc = ldms_xprt_dir(clnt->x, client_dir_cb, (void *)clnt, LDMS_DIR_F_NOTIFY);
	if (rc) {
		printf("Error %d: ldms_xprt_dir synchronously failed\n", rc);
		assert(0);
	}
	sem_wait(&clnt->dir_sem);
	printf(" ----- PASSED\n");

	printf("Test set info propagation: resetting a key on the set origin");
	clnt->test_fn = __test_dir_upd_prop_reset_B;
	clnt_send_msg(clnt, FWD | RESET, CLNT_B_PROP_UPD_RESET_KEY, CLNT_B_PROP_UPD_RESET_VALUE);
	sem_wait(&clnt->dir_upd_sem);
	free(clnt->msg);
	printf(" ----- PASSED\n");

	printf("Test set info propagation: unsetting a key on the set origin");
	clnt->test_fn = __test_dir_upd_prop_unset_B;
	clnt_send_msg(clnt, FWD | UNSET, CLNT_B_PROP_UPD_UNSET_KEY, "");
	sem_wait(&clnt->dir_upd_sem);
	free(clnt->msg);
	printf(" ----- PASSED\n");

	printf("Test set info propagation: adding a key on the set origin");
	clnt->test_fn = __test_dir_upd_prop_add_B;
	clnt_send_msg(clnt, FWD | ADD, CLNT_B_PROP_UPD_ADD_KEY, CLNT_B_PROP_UPD_ADD_VALUE);
	sem_wait(&clnt->dir_upd_sem);
	free(clnt->msg);
	printf(" ----- PASSED\n");

	clnt_send_msg(clnt, DONE, "", "");
	printf("DONE\n");
}

ldms_set_t test_local_set_info()
{
	ldms_schema_t schema;
	ldms_set_t set;
	int rc;

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
	rc = ldms_set_publish(set);
	if (rc) {
		printf("Failed to publish the set %s\n", SET_NAME);
		assert(0);
	}
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

	sem_init(&exit_sem, 0, 0);
	set = test_local_set_info();

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
	rc = ldms_xprt_listen(my_ldms, (void *)sin, sizeof(*sin),
					server_event_cb, (void *)set);
	if (rc) {
		printf("Error %d: Failed to listen\n", rc);
		exit(1);
	}

	IS_DONE = 1;
	printf("DONE\n");
	if (no_wait)
		return 0;
	printf("Waiting for clients\n");
	sem_wait(&exit_sem);
	return 0;
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
		s = test_local_set_info();
		ldms_set_delete(s);
		printf("DONE\n");
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
	return 0;
}

