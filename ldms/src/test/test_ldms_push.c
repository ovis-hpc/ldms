/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016,2018 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2016,2018 Sandia Corporation. All rights reserved.
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
static int port;
static int is_server;
static int is_onchange;
static int is_cancel;
static int is_pull;
static int interval = 1;
static int is_verbose;
static int is_closed;

static ldms_t ldms;
static sem_t exit_sem;

static const int num_sets = 1;

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

#define FMT "x:p:h:i:svcoud"

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
"Server options:\n"
"	-i interval	Interval to make changes\n"
"Client options:\n"
"	-c		Cancel the pull request\n"
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
			port = atoi(optarg);
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

static void __print_set(ldms_set_t s)
{
	struct ldms_set *set = (struct ldms_set *)s->set;
	int card = ldms_set_card_get(s);
	enum ldms_value_type type;
	printf("--------------------------------\n");
	printf("set name: %s\n", ldms_set_instance_name_get(s));
	printf("       meta->meta_gn: %" PRIu64 "\n", set->meta->meta_gn);
	printf("       data->meta_gn: %" PRIu64 "\n", set->data->meta_gn);
	printf("            data->gn: %" PRIu64 "\n", set->data->gn);
	printf("   Number of metrics: %d\n", card);
	printf("	%-10s %16s %10s\n", "MetricName", "Value", "UserData");
	int i, j, n;
	for (i = 0; i < card; i++) {
		printf("	%-10s", ldms_metric_name_get(s, i));
		type = ldms_metric_type_get(s, i);
		n = ldms_metric_array_get_len(s, i);
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
			printf("%16hhu", ldms_metric_get_u8(s, i));
			break;
		case LDMS_V_S8:
			printf("%16hhd", ldms_metric_get_s8(s, i));
			break;
		case LDMS_V_U16:
			printf("%16hu", ldms_metric_get_u16(s, i));
			break;
		case LDMS_V_S16:
			printf("%16hd", ldms_metric_get_s16(s, i));
			break;
		case LDMS_V_U32:
			printf("%16u", ldms_metric_get_u32(s, i));
			break;
		case LDMS_V_S32:
			printf("%16d", ldms_metric_get_s32(s, i));
			break;
		case LDMS_V_U64:
			printf("%16"PRIu64, ldms_metric_get_u64(s, i));
			break;
		case LDMS_V_S64:
			printf("%16"PRId64, ldms_metric_get_s64(s, i));
			break;
		case LDMS_V_F32:
			printf("%16f", ldms_metric_get_float(s, i));
			break;
		case LDMS_V_D64:
			printf("%16f", ldms_metric_get_double(s, i));
			break;
		case LDMS_V_U8_ARRAY:
			printf("%hhu", ldms_metric_array_get_u8(s, i, j));
			break;
		case LDMS_V_S8_ARRAY:
			printf("%hhd", ldms_metric_array_get_s8(s, i, j));
			break;
		case LDMS_V_U16_ARRAY:
			printf("%hu", ldms_metric_array_get_u16(s, i, j));
			break;
		case LDMS_V_S16_ARRAY:
			printf("%hd", ldms_metric_array_get_s16(s, i, j));
			break;
		case LDMS_V_U32_ARRAY:
			printf("%u", ldms_metric_array_get_u32(s, i, j));
			break;
		case LDMS_V_S32_ARRAY:
			printf("%d", ldms_metric_array_get_s32(s, i, j));
			break;
		case LDMS_V_U64_ARRAY:
			printf("%"PRIu64, ldms_metric_array_get_u64(s, i, j));
			break;
		case LDMS_V_S64_ARRAY:
			printf("%"PRId64, ldms_metric_array_get_s64(s, i, j));
			break;
		case LDMS_V_F32_ARRAY:
			printf("%f", ldms_metric_array_get_float(s, i, j));
			break;
		case LDMS_V_D64_ARRAY:
			printf("%f", ldms_metric_array_get_double(s, i, j));
			break;
		default:
			printf("Unknown metric type\n");
			assert(0);
		}
		j++;
		goto loop;
	out:
		printf("%10" PRIu64 "\n", ldms_metric_user_data_get(s, i));
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
	rc = ldms_schema_meta_add(schema, "FOO_META", LDMS_V_U64);
	if (rc < 0) {
		_log("Failed to add meta metric.\n");
		assert(rc >= 0);
	}

	rc = ldms_schema_metric_add(schema, "FOO", LDMS_V_U64);
	if (rc < 0) {
		_log("Failed to add metric\n");
		assert(rc >= 0);
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

#define USER_EVENT "udata is changed\n"

static void do_server(struct sockaddr_in *sin)
{
	int i;
	for (i = 0; i < num_sets; i++) {
		set_array[i] = __server_create_set(set_names[i]);
		set_array[i]->idx = i;
		if (!set_array[i])
			assert(0);
	}

	int rc;
	rc = ldms_xprt_listen(ldms, (void *)sin, sizeof(*sin), NULL, NULL);
	if (rc) {
		_log("Failed to listen '%d'\n", rc);
		assert(0);
	}

	_log("Listening on port '%d'\n", port);

	ldms_set_t set;
	uint64_t round = 0;
	uint64_t meta_value = 1;
	uint64_t metric_value = 1;
	while (1) {
		for (i = 0; i < num_sets; i++) {
			set = set_array[i]->set;
			ldms_transaction_begin(set);
			if ((round % 2) == 1) {
				_log("%s: Update meta value to %" PRIu64 "\n",
						set_names[i], meta_value);
				ldms_metric_set_u64(set, 0, meta_value);
				meta_value++;
			} else {
				_log("%s: Update metric value to %" PRIu64 "\n",
						set_names[i], metric_value);
				ldms_metric_set_u64(set, 1, metric_value);
				metric_value++;
			}
			_log("%s: End a transaction\n", set_names[i]);
			ldms_transaction_end(set);

			if (is_verbose)
				__print_set(set);

			if ((round % 4) == 1) {
				_log("%s: Push the set\n", set_names[i]);
				ldms_xprt_push(set);
			}

		}

		round++;
		sleep(interval);
	}
}

static void client_update_cb(ldms_t x, ldms_set_t set, int status, void *arg)
{
	char *setname;
	if (status && status != 23) { /* 23 is ZAP_ERR_FLUSH */
		_log("Update_cb error: status '%d'\n", status);
		assert(0);
	}
	setname = (char *)ldms_set_instance_name_get(set);
	_log("%s: update_cb\n", setname);

	__print_set(set);

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
	if (0 == (status & LDMS_UPD_F_PUSH)) {
		_log("%s: Received pull update in push path.\n", setname);
		assert(0);
	}

	if (1 == (status | ~LDMS_UPD_F_PUSH)) {
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
			_log("%s: last push update\n", setname);
			__print_set(set);
			if (push_set->state == PUSH_SET_RECEIVED_LAST_PUSH) {
				_log("%s: Already received the last push update\n",
						setname);
			} else {
				push_set->state = PUSH_SET_RECEIVED_LAST_PUSH;
			}

			goto out;
		}
	}

	_log("%s: push_update_cb\n", setname);

	__print_set(set);

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
		goto out;
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
	struct sockaddr_in lsin = {0};
	struct sockaddr_in rsin = {0};
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		printf("%d: connected\n", port);
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
		printf("%d: conn_error\n", port);
		ldms_xprt_put(x);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		is_closed = 1;
		printf("%d: disconnected\n", port);
		ldms_xprt_put(x);
		sem_post(&exit_sem);
		break;
	default:
		printf("%d: Unhandled ldms event '%d'\n", port, e->type);
		exit(-1);
	}
}

static void do_client(struct sockaddr_in *sin)
{
	int rc;
	sem_init(&exit_sem, 0, 0);
	rc = ldms_xprt_connect(ldms, (void *)sin, sizeof(*sin),
			client_connect_cb, NULL);
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

	struct sockaddr_in sin = {0};
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;

	ldms = ldms_xprt_new(xprt, _log);
	if (!ldms) {
		_log("Failed to create ldms xprt\n");
		assert(ldms);
	}

	if (is_server)
		do_server(&sin);
	else
		do_client(&sin);
	sleep(1);
	printf("DONE\n");
	return 0;

}
