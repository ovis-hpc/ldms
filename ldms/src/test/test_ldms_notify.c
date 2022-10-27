/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2018 Sandia Corporation. All rights reserved.
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

static char *host = "localhost";
static char *xprt = "sock";
static int port = 10001;
static int is_server;
static char *setname;
static int want_modified;
static int want_uevent;
static int want_update;
static int is_set_modified;
static int is_uevents;
static int is_cancel;
static int interval = 1;
static int is_recvd_uevent;
static int is_recvd_modified;
static int need_close;

static ldms_t ldms;
static sem_t exit_sem;

#define FMT "x:p:h:S:i:sMuUcd"

static void desc() {
	printf(
"Server:\n"
"	- create the set with the given name. The set contains only one metric.\n"
"	- it periodically creates events of the specified type(s)\n"
"	  The metric value is incremented by 1 if -M is specified\n"
"	  The user data is incremented by 1 if -U is specified\n"
"	- call ldms_notify with the appropriate type after it makes each change\n"
"Client:\n"
"	- connects to the server."
"	- upon receiving 'CONNECTED' event, it requests for the\n"
"	  notification of the specified type(s).\n"
"	- after receive a notification,\n"
"		update the set if -u is specified, and then\n"
"		cancel the notification request if -c is specified.\n"
"	- upon receiving all updates, it closes the transport if -c is specified\n"
"	- client exits when it receives the DISCONNECTED event\n"
	);
}

static void usage() {
	printf(
"	-d		Print the description of the test\n"
"	-p port		listener port (server) or port to connect to (client)\n"
"	-S set_name	Set name\n"
"	-x xprt		sock, rdma, or ugni\n"
"Server options:\n"
"	-i interval	Interval to make changes\n"
"	-M		Generate set modified events\n"
"	-s		Server mode\n"
"	-U		Generate user events\n"
"Client options:\n"
"	-c		Cancel the request after receive the notification of all types\n"
"	-h host		Host name to connect to.\n"
"	-M		Request for set modified events.\n"
"	-U		Request for user events.\n"
"	-u		Update set when receive a notification\n"
	);
}

static void process_args(int argc, char **argv) {
	char op;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'c':
			is_cancel = 1;
			break;
		case 'd':
			desc();
			exit(0);
		case 'h':
			host = strdup(optarg);
			break;
		case 'i':
			interval = atoi(optarg);
			break;
		case 'M':
			want_modified = 1;
			is_set_modified = 1;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'S':
			setname = strdup(optarg);
			break;
		case 's':
			is_server = 1;
			break;
		case 'U':
			want_uevent = 1;
			is_uevents = 1;
			break;
		case 'u':
			want_update = 1;
			break;
		case 'x':
			xprt = strdup(optarg);
			break;
		case '?':
			usage();
			exit(0);
		default:
			printf("Unrecognized option '%c'\n", op);
			usage();
			exit(1);
		}
	}
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
	printf("	%-10s %16s %10s\n", "MetricName", "Value", "UserData");
	int i, j, n;
	for (i = 0; i < card; i++) {
		printf("	%-10s", ldms_metric_name_get(set, i));
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
		printf("%10" PRIu64 "\n", ldms_metric_user_data_get(set, i));
		;
	}
	printf("--------------------------------\n");
}

static ldms_set_t __server_create_set(const char *name)
{
	ldms_schema_t schema = ldms_schema_new("TEST SCHEMA");
	if (!schema) {
		printf("Failed to create the schema\n");
		assert(schema);
	}

	int rc;
	rc = ldms_schema_metric_add(schema, "FOO", LDMS_V_U64);
	if (rc) {
		printf("Failed to add metric\n");
		assert(rc == 0);
	}

	ldms_set_t set = ldms_set_new(name, schema);
	if (!set) {
		printf("Failed to create the set '%s'\n", name);
		assert(set);
	}
	rc = ldms_set_publish(set);
	if (rc) {
		printf("Failed to publish the set '%s'\n", name);
		assert(set);
	}
	ldms_metric_set_u64(set, 0, 0);
	return set;
}

#define USER_EVENT "udata is changed\n"

static void do_server(struct sockaddr_in *sin)
{
	ldms_set_t set;
	set = __server_create_set(setname);
	if (!set)
		assert(0);

	int rc;
	rc = ldms_xprt_listen(ldms, (void *)sin, sizeof(*sin), NULL, NULL);
	if (rc) {
		printf("Failed to listen '%d'\n", rc);
		assert(0);
	}

	printf("Listening on port '%d'\n", port);

	char user_data_buf[USER_DATA_LEN];
	sprintf(user_data_buf, USER_EVENT);
	size_t len = strlen(user_data_buf);
	ldms_notify_event_t event;
	size_t sz = sizeof(*event) + len;
	event = calloc(1, sz);
	uint64_t round = 0;
	while (1) {
		memset(event, 0, sz);
		if (is_set_modified) {
			printf("Creating SET_MODIFIED event\n");
			ldms_transaction_begin(set);
			ldms_metric_set_u64(set, 0, round);
			ldms_transaction_end(set);
			ldms_init_notify_modified(event);
		}
		if (is_uevents) {
			printf("Creating USER EVENT event\n");
			ldms_metric_user_data_set(set, 0, round);
			ldms_init_notify_user_data(event, (unsigned char*)user_data_buf, len);
		}

		__print_set(set);

		ldms_notify(set, event);
		round++;
		sleep(interval);
	}

}

static void client_update_cb(ldms_t x, ldms_set_t set, int status, void *arg)
{
	if (status) {
		printf("Update_cb error: status '%d'\n", status);
		assert(0);
	}
	__print_set(set);

	if (need_close) {
		printf("Closing the transport\n");
		ldms_xprt_close(x);
	}
}

static void client_notify_cb(ldms_t x, ldms_set_t set,
		ldms_notify_event_t e, void *arg)
{
	char *uevent = (char *)e->u_data;
	switch (e->type) {
	case LDMS_SET_MODIFIED:
		is_recvd_modified = 1;
		printf("Receive .... notification type: SET MODIFIED\n");
		if (!want_modified)
			assert(0 == "Not requested");
		break;
	case LDMS_USER_DATA:
		is_recvd_uevent = 1;
		printf("Receive .... Notification type: USER DATA\n");
		if (0 != strcmp(uevent, USER_EVENT)) {
			printf("Wrong user data. Expected: %s. Received: %s\n",
					USER_EVENT, uevent);
			assert(0);
		}
		if (!want_uevent)
			assert(0 == "Not requested\n");
		printf("User event: %s\n", e->u_data);
		break;
	default:
		printf("Unsupported notify type '%d'.\n", e->type);
		assert(0);
	}

	int rc;
	if (want_update) {
		printf("Updating ...\n");
		rc = ldms_xprt_update(set, client_update_cb, NULL);
		if (rc) {
			printf("ldms_xprt_update failed '%d'\n", rc);
			assert(0);
		}
	}

	if (is_cancel) {
		if (want_modified + want_uevent == 2)
			/* Requested for both event types */
			if (!is_recvd_modified || !is_recvd_uevent)
				/* Have not received both type yet */
				return;
		printf("Canceling ... notify request\n");
		rc = ldms_cancel_notify(x, set);
		if (rc) {
			printf("ldms_cancel_notify failed '%d'\n", rc);
			assert(0);
		}
		need_close = 1;
	}
}

static void client_lookup_cb(ldms_t x, enum ldms_lookup_status status,
		int more, ldms_set_t set, void *arg)
{
	if (status != LDMS_LOOKUP_OK) {
		printf("Lookup failed '%d'\n", status);
		assert(0);
	}

	int rc;
	uint32_t notify_flags = 0;

	if (want_modified || want_uevent) {
		if (!want_uevent)
			notify_flags |= LDMS_SET_MODIFIED;
		else if (!want_modified)
			notify_flags |= LDMS_USER_DATA;
		else
			notify_flags = 0; /* Need all */
	}

	printf("Requesting ... notification\n");
	rc = ldms_register_notify_cb(x, set, notify_flags,
			client_notify_cb, NULL);
	if (rc) {
		printf("ldms_register_notify_cb SET_MODIFIED "
				"failed '%d'\n", rc);
		assert(0);
	}
}

static void client_connect_cb(ldms_t x, ldms_xprt_event_t e, void *arg)
{
	int rc = 0;
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		printf("%d: connected\n", port);
		rc = ldms_xprt_lookup(x, setname, LDMS_LOOKUP_BY_INSTANCE,
				client_lookup_cb, NULL);
		if (rc) {
			printf("ldms_xprt_lookup failed '%d'\n", rc);
			assert(0);
		}
		break;
	case LDMS_XPRT_EVENT_ERROR:
		printf("%d: conn_error\n", port);
		ldms_xprt_put(x);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		printf("%d: disconnected\n", port);
		ldms_xprt_put(x);
		sem_post(&exit_sem);
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		printf("%d: send_complete\n", port);
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
		printf("ldms_xprt_connect failed '%d'\n", rc);
		assert(0);
	}

	sem_wait(&exit_sem);
	printf("Exiting\n");
	sem_destroy(&exit_sem);
}

int main(int argc, char **argv) {
	process_args(argc, argv);

	ldms_init(1024);

	struct sockaddr_in sin = {0};
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;

	ldms = ldms_xprt_new(xprt);
	if (!ldms) {
		printf("Failed to create ldms xprt\n");
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
