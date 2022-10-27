/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016-2018 Open Grid Computing, Inc. All rights reserved.
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
#include <getopt.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/types.h>
#include <assert.h>
#include <semaphore.h>
#include "ldms_private.h"
#include "ldms.h"

#define USER_DATA_LEN 32

static char *host;
static char *xprt;
static char *port;
static int is_server;
static int is_onchange;
static int is_cancel;
static int is_pull;
static int interval = 1;
static int is_verbose;
static int is_closed;
static int push_meta;
static int push_data;
static int is_json;

static ldms_t ldms;
static sem_t exit_sem;

static const int num_sets = 1;

static int META_METRIC_ID;
static int DATA_METRIC_ID;
static int METRIC_TYPE_METRIC_ID;
static int EXPLICIT_PUSH_METRIC_ID;

struct push_set {
	ldms_set_t set;
	int idx;
	char *name;
	enum {
		PUSH_SET_WAIT_PUSH_UPDATE,
		PUSH_SET_TO_CANCEL,
		PUSH_SET_CANCELED,
		PUSH_SET_RECEIVED_LAST_PUSH,
	} state;
};
static struct push_set *set_array[3];
static const char *set_names[] = {
		"test_set_1",
		"test_set_2",
		"test_set_3"
};

enum metric_type {
	META = 1,
	DATA = 2
};

#define FMT "x:p:h:i:svcoudMDj"

static void desc() {
	printf(
"Server:\n"
"	- create a metric set containing one meta metric and one data metric.\n"
"	- meta metric and data metric are alternately incremented.\n"
"	- the set is explicitly push every 4 samples\n"
"Client:\n"
"	- connects to the server, requests dir, do lookup\n"
"	- registers for a push update either 'onchange' (-o)\n"
"	  only for 'explicit push'. The default is 'explicit push'\n"
"	- cancels the push registration after receiving the first push update\n"
"	  if '-c' is given. The program will not close the connection and exit\n"
"	  automatically to catch duplicated last push update.\n"
"	  Testers are encouraged to wait a couple seconds depending on the sampling interval\n"
"	  of the server before exiting the test program\n"
"	- Request for updates if '-u' is given with '-i' interval length.\n"
	);
}

static void usage() {
	printf(
"	-d		Print the description of the test\n"
"	-p port		listener port (server) or port to connect to (client)\n"
"	-x xprt		sock, rdma, or ugni\n"
"	-s		Server mode\n"
"	-v		Verbose\n"
"	-j		Print the set as a json string format\n"
"Server options:\n"
"	-i interval	Interval to make changes\n"
"	-M		Explicitly push the set every other time when the meta metric is changed\n"
"	-D		Explicitly push the set every other time when the data metric is changed\n"
"Client options:\n"
"	-c		Cancel the push registration request\n"
"	-h host		Host name to connect to.\n"
"	-o		Request 'onchange' push\n"
"	-u		Pull set content\n"
"	-i interval	Pull interval\n"
	);
}

static void process_args(int argc, char **argv) {
	char op;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'p':
			port = strdup(optarg);
			break;
		case 'x':
			xprt = strdup(optarg);
			break;
		case 's':
			is_server = 1;
			break;
		case '?':
			usage();
			exit(0);
		case 'i':
			interval = atoi(optarg);
			break;
		case 'v':
			is_verbose = 1;
			break;
		/* Client options */
		case 'h':
			host = strdup(optarg);
			break;
		case 'c':
			is_cancel = 1;
			break;
		case 'o':
			is_onchange = 1;
			break;
		case 'u':
			is_pull = 1;
			break;
		case 'd':
			desc();
			exit(0);
		case 'M':
			push_meta = 1;
			break;
		case 'D':
			push_data = 1;
			break;
		case 'j':
			is_json = 1;
			break;
		default:
			printf("Unrecognized option '%c'\n", op);
			usage();
			exit(1);
		}
	}
}

static void _log(const char *fmt, ...)
{
	va_list l;
	va_start(l, fmt);
	vprintf(fmt, l);
	va_end(l);
}

enum update_type {
	SAMPLE = 0,
	PUSH = 1,
	LAST_PUSH,
	DUP_LAST_PUSH,
	PULL,
};

static const char *update_type_enum2str(enum update_type type)
{
	if (type == PUSH)
		return "PUSH";
	else if (type == PULL)
		return "PULL";
	else if (type == SAMPLE)
		return "SAMPLE";
	else if (type == LAST_PUSH)
		return "LAST_PUSH";
	else if (type == DUP_LAST_PUSH)
		return "DUP_LAST_PUSH";
	_log("unrecognized update type %d\n", type);
	assert(0);
}

static const char *metric_type_enum2str(enum metric_type type)
{
	if (type == META)
		return "META";
	else if (type == DATA)
		return "DATA";
	_log("unrecognized metric type %d\n", type);
	assert(0);
}

static void __print_json_set(ldms_set_t set, enum update_type type)
{
	int i;
	printf("{"
		"\"type\":\"%s\","
		"\"set_name\":\"%s\","
		"\"meta_meta_gn\":%" PRIu64 ","
		"\"data_meta_gn\":%" PRIu64 ","
		"\"data_gn\":%" PRIu64 ","
		"\"cardinality\":%d,"
		"\"metrics\":[",
		update_type_enum2str(type),
		ldms_set_instance_name_get(set),
		set->meta->meta_gn,
		set->data->meta_gn,
		set->data->gn,
		ldms_set_card_get(set));
	for (i = 0; i < ldms_set_card_get(set); i++) {
		if (i > 0)
			printf(",");
		printf("{\"name\":\"%s\",", ldms_metric_name_get(set, i));
		if (i == METRIC_TYPE_METRIC_ID) {
			printf("\"value\":\"%s\"}",
				metric_type_enum2str(ldms_metric_get_u8(set, i)));
		} else if (i == EXPLICIT_PUSH_METRIC_ID) {
			printf("\"value\":\"%s\"}",
				(ldms_metric_get_u8(set, i)?"true":"false"));
		} else {
			printf("\"value\":\"%" PRIu64 "\"}",
					ldms_metric_get_u64(set, i));
		}
	}
	printf("]}\n");
}

static void __print_set(ldms_set_t set)
{
	int card = ldms_set_card_get(set);
	enum ldms_value_type type;
	printf("--------------------------------\n");
	printf("set name: %s\n", ldms_set_instance_name_get(set));
	printf("       meta->meta_gn: %" PRIu64 "\n", set->meta->meta_gn);
	printf("       data->meta_gn: %" PRIu64 "\n", set->data->meta_gn);
	printf("            data->gn: %" PRIu64 "\n", set->data->gn);
	printf("   Number of metrics: %d\n", card);
	printf("	%-10s %16s\n", "MetricName", "Value");
	int i, j, n;
	for (i = 0; i < card; i++) {
		printf("	%-10s", ldms_metric_name_get(set, i));
		if (i == METRIC_TYPE_METRIC_ID) {
			printf("%16s", metric_type_enum2str(ldms_metric_get_u8(set, i)));
			goto out;
		}
		if (i == EXPLICIT_PUSH_METRIC_ID) {
			printf("%16s", (ldms_metric_get_u8(set, i)?"true":"false"));
			goto out;
		}
		type = ldms_metric_type_get(set, i);
		n = ldms_metric_array_get_len(set, i);
		if (n > 10)
			n = 10;
		j = 0;
	loop:
		if (j >= n)
			goto out;
		if (j)
			printf(",");
		switch (type) {
		case LDMS_V_U8:
			printf("%16hhu", ldms_metric_get_u8(set, i));
			break;
		case LDMS_V_S8:
			printf("%16hhd", ldms_metric_get_s8(set, i));
			break;
		case LDMS_V_U16:
			printf("%16hu", ldms_metric_get_u16(set, i));
			break;
		case LDMS_V_S16:
			printf("%16hd", ldms_metric_get_s16(set, i));
			break;
		case LDMS_V_U32:
			printf("%16u", ldms_metric_get_u32(set, i));
			break;
		case LDMS_V_S32:
			printf("%16d", ldms_metric_get_s32(set, i));
			break;
		case LDMS_V_U64:
			printf("%16"PRIu64, ldms_metric_get_u64(set, i));
			break;
		case LDMS_V_S64:
			printf("%16"PRId64, ldms_metric_get_s64(set, i));
			break;
		case LDMS_V_F32:
			printf("%16f", ldms_metric_get_float(set, i));
			break;
		case LDMS_V_D64:
			printf("%16f", ldms_metric_get_double(set, i));
			break;
		case LDMS_V_U8_ARRAY:
			printf("%hhu", ldms_metric_array_get_u8(set, i, j));
			break;
		case LDMS_V_S8_ARRAY:
			printf("%hhd", ldms_metric_array_get_s8(set, i, j));
			break;
		case LDMS_V_U16_ARRAY:
			printf("%hu", ldms_metric_array_get_u16(set, i, j));
			break;
		case LDMS_V_S16_ARRAY:
			printf("%hd", ldms_metric_array_get_s16(set, i, j));
			break;
		case LDMS_V_U32_ARRAY:
			printf("%u", ldms_metric_array_get_u32(set, i, j));
			break;
		case LDMS_V_S32_ARRAY:
			printf("%d", ldms_metric_array_get_s32(set, i, j));
			break;
		case LDMS_V_U64_ARRAY:
			printf("%"PRIu64, ldms_metric_array_get_u64(set, i, j));
			break;
		case LDMS_V_S64_ARRAY:
			printf("%"PRId64, ldms_metric_array_get_s64(set, i, j));
			break;
		case LDMS_V_F32_ARRAY:
			printf("%f", ldms_metric_array_get_float(set, i, j));
			break;
		case LDMS_V_D64_ARRAY:
			printf("%f", ldms_metric_array_get_double(set, i, j));
			break;
		default:
			printf("Unknown metric type\n");
			assert(0);
		}
		j++;
		goto loop;
	out:
		printf("\n");
	}
	printf("--------------------------------\n");
}

static struct push_set *__server_create_set(const char *name)
{
	ldms_schema_t schema = ldms_schema_new("TEST SCHEMA");
	if (!schema) {
		_log("Failed to create the schema\n");
		assert(schema);
	}

	int rc;
	METRIC_TYPE_METRIC_ID = ldms_schema_metric_add(schema, "METRIC_TYPE", LDMS_V_U8);
	if (METRIC_TYPE_METRIC_ID < 0) {
		_log("Failed to add a metric\n");
		assert(METRIC_TYPE_METRIC_ID >= 0);
	}
	EXPLICIT_PUSH_METRIC_ID = ldms_schema_metric_add(schema, "EXPLICIT_PUSH", LDMS_V_U8);
	if (EXPLICIT_PUSH_METRIC_ID < 0) {
		_log("Failed to add a metric\n");
		assert(EXPLICIT_PUSH_METRIC_ID >= 0);
	}
	META_METRIC_ID = ldms_schema_meta_add(schema, "meta_metric", LDMS_V_U64);
	if (META_METRIC_ID < 0) {
		_log("Failed to add meta metric.\n");
		assert(META_METRIC_ID >= 0);
	}

	DATA_METRIC_ID = ldms_schema_metric_add(schema, "data_metric", LDMS_V_U64);
	if (DATA_METRIC_ID < 0) {
		_log("Failed to add metric\n");
		assert(DATA_METRIC_ID >= 0);
	}

	ldms_set_t set = ldms_set_new(name, schema);
	if (!set) {
		_log("Failed to create the set '%s'\n", name);
		assert(set);
	}
	ldms_metric_set_u64(set, 0, 0);
	ldms_metric_set_u64(set, 0, 0);
	rc = ldms_set_publish(set);
	if (rc) {
		_log("Failed to publish the set '%s'\n", name);
		assert(set);
	}
	struct push_set *push_set = calloc(1, sizeof(*push_set));
	if (!push_set) {
		_log("Out of memory\n");
		assert(0);
	}
	push_set->set = set;
	push_set->name = strdup(name);
	return push_set;
}

#define DO_META(round) (((round) % 2) == 1)
#define DO_PUSH(round) ((((round) % 4) == 1) || (((round) % 4) == 2))

static void do_server(char *host, char *port)
{
	int i;
	for (i = 0; i < num_sets; i++) {
		set_array[i] = __server_create_set(set_names[i]);
		set_array[i]->idx = i;
		if (!set_array[i])
			assert(0);
	}

	int rc;
	rc = ldms_xprt_listen_by_name(ldms, host, port, NULL, NULL);
	if (rc) {
		_log("Failed to listen '%d'\n", rc);
		assert(0);
	}

	_log("Listening on port '%d'\n", port);
	ldms_set_t set;
	uint64_t round = 1;
	uint64_t meta_value = 1;
	uint64_t metric_value = 1;
	while (1) {
		for (i = 0; i < num_sets; i++) {
			set = set_array[i]->set;
			ldms_transaction_begin(set);
			if (DO_META(round)) {
				ldms_metric_set_u8(set, METRIC_TYPE_METRIC_ID, META);
				/* new meta metric value */
				_log("%s:     %s --> %" PRIu64 "\n",
						set_names[i],
						ldms_metric_name_get(set, META_METRIC_ID),
						meta_value);
				ldms_metric_set_u64(set, META_METRIC_ID, meta_value);
				meta_value++;
			} else {
				ldms_metric_set_u8(set, METRIC_TYPE_METRIC_ID, DATA);
				/* new data metric value */
				_log("%s:     %s --> %" PRIu64 "\n",
						set_names[i],
						ldms_metric_name_get(set, DATA_METRIC_ID),
						metric_value);
				ldms_metric_set_u64(set, DATA_METRIC_ID, metric_value);
				metric_value++;
			}
			if (DO_PUSH(round)) {
				if ((push_meta && DO_META(round)) ||
						(push_data && !DO_META(round))) {
					ldms_metric_set_u8(set, EXPLICIT_PUSH_METRIC_ID, 1);
				}
			} else {
				ldms_metric_set_u8(set, EXPLICIT_PUSH_METRIC_ID, 0);
			}
			_log("%s: End a transaction\n", set_names[i]);
			ldms_transaction_end(set);
			if (DO_PUSH(round)) {
				if ((push_meta && DO_META(round)) ||
						(push_data && !DO_META(round))) {
						_log("%s: Push the set\n", set_names[i]);
						ldms_xprt_push(set);
				}
			}
			if (is_verbose) {
				if (is_json)
					__print_json_set(set, SAMPLE);
				else
					__print_set(set);
			}
		}
		round++;
		sleep(interval);
	}
}

static void client_update_cb(ldms_t x, ldms_set_t set, int status, void *arg)
{
	char *setname;
	if (status && status != EPIPE) { /* 23 is ZAP_ERR_FLUSH */
		_log("Update_cb error: status '%d'\n", status);
		assert(0);
	}
	setname = (char *)ldms_set_instance_name_get(set);

	if (is_json) {
		__print_json_set(set, PULL);
	} else {
		_log("%s: update_cb\n", setname);
		__print_set(set);
	}

	if (is_closed)
		return;
	sleep(interval);
	int rc = ldms_xprt_update(set, client_update_cb, arg);
	if (rc) {
		_log("Failed to update set '%s'\n", setname);
	}
}

static void client_push_update_cb(ldms_t x, ldms_set_t set,
					int status, void *arg)
{
	int set_idx = (int)(long unsigned)arg;
	char *setname;
	setname = (char *)ldms_set_instance_name_get(set);
	struct push_set *push_set = set_array[set_idx];
	if (0 == (status & (LDMS_UPD_F_PUSH | LDMS_UPD_F_PUSH_LAST))) {
		_log("%s: Received pull update in push path.\n", setname);
		assert(0);
	}

	if (LDMS_UPD_ERROR(status)) {
		_log("%s: Received push update error\n", setname);
		assert(0);
	}

	if ((!is_cancel || (push_set->state != PUSH_SET_CANCELED)) &&
				(status & LDMS_UPD_F_PUSH_LAST)) {
		_log("%s: Unexpected the last push update.\n", setname);
		assert(0);
	}

	if (push_set->state == PUSH_SET_CANCELED) {
		if (!is_cancel) {
			_log("%s:, Received last push update without "
					"cancellation request\n", setname);
			assert(0);
		}
		if (status & LDMS_UPD_F_PUSH_LAST) {
			if (is_json) {
				__print_json_set(set, LAST_PUSH);
			} else {
				_log("%s: last push update\n", setname);
				__print_set(set);
			}
			if (push_set->state == PUSH_SET_RECEIVED_LAST_PUSH) {
				if (is_json) {
					__print_json_set(set, DUP_LAST_PUSH);
				} else {
					_log("%s: Already received "
						"the last push update\n", setname);
				}
			} else {
				push_set->state = PUSH_SET_RECEIVED_LAST_PUSH;
			}

			goto out;
		}
	}

	if (is_json) {
		__print_json_set(set, PUSH);
	} else {
		_log("%s: push_update_cb\n", setname);
		__print_set(set);
	}

	if (is_cancel) {
		if (push_set->state == PUSH_SET_CANCELED)
			goto out;
		_log("Canceling the push for set '%s'\n", setname);
		int rc = ldms_xprt_cancel_push(set);
		if (rc) {
			_log("Error %d: Failed to cancel push for set '%s'\n",
								rc, setname);
			assert(0);
		}
		push_set->state = PUSH_SET_CANCELED;
	}
out:
	_log("==============================================\n");
	return;
}

static void client_lookup_cb(ldms_t x, enum ldms_lookup_status status,
		int more, ldms_set_t set, void *arg)
{
	if (status != LDMS_LOOKUP_OK) {
		_log("Lookup failed '%d'\n", status);
		assert(0);
	}
	char *setname = (char *)ldms_set_instance_name_get(set);

	int rc, push_flag;
	int idx = (int)(long unsigned)arg;
	struct push_set *push_set = calloc(1, sizeof(*push_set));
	if (!push_set) {
		_log("Out of memory\n");
		assert(0);
	}
	push_set->set = set;
	push_set->idx = idx;
	push_set->name = strdup(set_names[idx]);
	set_array[idx] = push_set;

	push_flag = 0;
	if (is_onchange)
		push_flag = LDMS_XPRT_PUSH_F_CHANGE;

	_log("%s: register push with flag %d\n", setname, push_flag);
	rc = ldms_xprt_register_push(set, push_flag, client_push_update_cb, arg);
	if (rc) {
		_log("%s: Failed to register push %d. Error %d\n",
					setname, push_flag, rc);
		assert(0);
	}
	push_set->state = PUSH_SET_WAIT_PUSH_UPDATE;
	if (is_pull) {
		_log("%s: Update\n", setname);
		rc = ldms_xprt_update(set, client_update_cb, arg);
		if (rc) {
			_log("Failed to update set '%s'\n", setname);
			assert(0);
		}
	}
}

static void client_connect_cb(ldms_t x, ldms_xprt_event_t e, void *arg)
{
	int rc = 0;
	int i;
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		printf("%s: connected\n", port);
		for (i = 0; i < num_sets; i++) {
			rc = ldms_xprt_lookup(x, set_names[i], LDMS_LOOKUP_BY_INSTANCE,
					client_lookup_cb, (void *)(uint64_t)i);
			if (rc) {
				_log("ldms_xprt_lookup for '%s' failed '%d'\n",
							set_names[i], rc);
				assert(0);
			}
		}
		break;
	case LDMS_XPRT_EVENT_ERROR:
		printf("%s: conn_error\n", port);
		ldms_xprt_put(x);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		is_closed = 1;
		printf("%s: disconnected\n", port);
		ldms_xprt_put(x);
		sem_post(&exit_sem);
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		/* ignore */
		break;
	default:
		printf("%s: Unhandled ldms event '%d'\n", port, e->type);
		exit(-1);
	}
}

static void do_client(char *host, char *port)
{
	int rc;
	sem_init(&exit_sem, 0, 0);
	rc = ldms_xprt_connect_by_name(ldms, host, port, client_connect_cb, NULL);
	if (rc) {
		_log("ldms_xprt_connect failed '%d'\n", rc);
		assert(0);
	}

	sem_wait(&exit_sem);
	_log("Exiting\n");
	sem_destroy(&exit_sem);
}

int main(int argc, char **argv) {
	process_args(argc, argv);

	ldms_init(1024);

	setlinebuf(stdout);

	ldms = ldms_xprt_new(xprt);
	if (!ldms) {
		_log("Failed to create ldms xprt\n");
		assert(ldms);
	}

	if (is_server)
		do_server(NULL, port);
	else
		do_client(host, port);
	sleep(1);
	printf("DONE\n");
	return 0;

}
