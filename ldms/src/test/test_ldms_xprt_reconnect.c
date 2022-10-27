/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2019 Open Grid Computing, Inc. All rights reserved.
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
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <pthread.h>
#include "ldms.h"

#define FMT "x:p:h:n:a:sdlmfu"

char *xprt = NULL;
char *host = NULL;
int port = 0;
char *port_s = NULL;
int is_server = 0;

int dir = 0;
int lookup = 0;
int update = 0;

int metric_set = 0;
int num_conn = 1;
int forever = 0;

int exiting = 0;
pthread_mutex_t exit_mutex = PTHREAD_MUTEX_INITIALIZER;

#define SCHEMA_NAME "test"
#define SET_NAME "test_set"
#define METRIC_NAME "A"

struct conn {
	struct sockaddr sa;
	socklen_t sa_len;
	const char *hostname;
	const char *port;
	ldms_t ldms;
	pthread_mutex_t state_lock;
	enum connect_state {
		INIT = 0,
		CONNECTING,
		CONNECTED,
		DISCONNECTED,
	} state;
	enum {
		DIR = 1,
		LOOKEDUP,
		UPDATED,
		NEED_LOOKUP,
		NEED_UPDATE,
	} set_state;
	ldms_set_t set;
};

struct conn *conn_list = 0;

static void client_update_cb(ldms_t ldms, ldms_set_t set, int status, void *arg)
{
	struct conn *conn = (struct conn *)arg;
	const char *set_name = ldms_set_instance_name_get(conn->set);
	if (status) {
		printf("%s: Update failed '%s'\n", conn->port, set_name);
		ldms_set_delete(conn->set);
		conn->set = NULL;
		ldms_xprt_close(ldms);
		return;
	}

	conn->set_state = UPDATED;

	if (!ldms_set_is_consistent(conn->set)) {
		printf("%s: Set %s is inconsistent.\n", conn->port, set_name);
		return;
	}

	printf("%s: '%s' is updated\n", conn->port, set_name);

	pthread_mutex_lock(&exit_mutex);
	if (!forever)
		exiting = 1;
	else
		conn->set_state = NEED_UPDATE;
	pthread_mutex_unlock(&exit_mutex);

	return;
}

static void client_lookup_cb(ldms_t ldms, enum ldms_lookup_status status,
		int more, ldms_set_t set, void *arg)
{
	char inst_name[32];
	int rc;
	struct conn *conn = (struct conn *)arg;

	snprintf(inst_name, 31, "%s/%s", conn->port, SET_NAME);
	if (status != LDMS_LOOKUP_OK) {
		printf("Error doing lookup '%s'\n", inst_name);
		if (forever)
			conn->set_state = NEED_LOOKUP;
		return;
	}

	conn->set = set;
	conn->set_state = LOOKEDUP;

	if (update) {
		sleep(1);
		rc = ldms_xprt_update(set, client_update_cb, (void *)conn);
		if (rc) {
			printf("%s: Error doing ldms_xprt_update '%s'\n", conn->port,
					ldms_set_instance_name_get(set));
			ldms_set_delete(set);
			conn->set = NULL;
			ldms_xprt_close(ldms);
		}
	}

	printf("%s: lookup_cb '%s'\n", conn->port, inst_name);
	pthread_mutex_lock(&exit_mutex);
	if (!forever && !update)
		exiting = 1;
	pthread_mutex_unlock(&exit_mutex);
}

static void client_dir_cb(ldms_t ldms, int status, ldms_dir_t dir, void *arg)
{
	int rc;
	char *set_name = NULL;
	struct conn *conn = (struct conn *)arg;
	switch (dir->type) {
	case LDMS_DIR_LIST:
		printf("%s: dir_cb: DIR_LIST", conn->port);
		conn->set_state = DIR;
		if (dir->set_count > 0) {
			set_name = dir->set_data[0].inst_name;
			printf(": %s\n", set_name);
		} else {
			printf(": No sets\n");
		}
		if (lookup && (dir->set_count > 0)) {
			printf("%s: doing lookup '%s'\n", conn->port, set_name);
			rc = ldms_xprt_lookup(ldms, set_name, LDMS_LOOKUP_BY_INSTANCE,
					client_lookup_cb, (void *)conn);
			if (rc) {
				printf("Error: ldms_xprt_lookup: %d\n", rc);
				ldms_xprt_close(ldms);
			}
		}
		break;
	case LDMS_DIR_ADD:
		printf("%s: dir_cb: DIR_ADD\n", conn->port);
		break;
	case LDMS_DIR_DEL:
		printf("%s: dir_cb: DIR_DEL\n", conn->port);
		break;
	default:
		break;
	}
	ldms_xprt_dir_free(ldms, dir);
	pthread_mutex_lock(&exit_mutex);
	if (!forever && !lookup && !update)
		exiting = 1;
	pthread_mutex_unlock(&exit_mutex);
}

static void client_connect_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	int rc = 0;
	struct conn *conn = (struct conn *)cb_arg;
	ldms_t ldms = conn->ldms;
	pthread_mutex_lock(&conn->state_lock);
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		conn->state = CONNECTED;
		printf("%s: connected\n", conn->port);
		if (dir) {
			rc = ldms_xprt_dir(ldms, client_dir_cb, (void *)conn,
					LDMS_DIR_F_NOTIFY);
			if (rc) {
				printf("Error: ldms_xprt_dir: %d\n", rc);
				ldms_xprt_close(ldms);
			}
		} else if (lookup) {
			char inst_name[32];
			snprintf(inst_name, 31, "%s/%s", conn->port, SET_NAME);
			rc = ldms_xprt_lookup(ldms, inst_name, LDMS_LOOKUP_BY_INSTANCE,
					client_lookup_cb, (void *)conn);
			if (rc) {
				printf("Error: ldms_xprt_lookup: %d\n", rc);
				ldms_xprt_close(ldms);
			}
		}
		break;
	case LDMS_XPRT_EVENT_ERROR:
		conn->state = DISCONNECTED;
		printf("%s: conn_error\n", conn->port);
		ldms_xprt_put(ldms);
		conn->ldms = NULL;
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		conn->state = DISCONNECTED;
		printf("%s: disconnected\n", conn->port);
		if (conn->set) {
			ldms_set_delete(conn->set);
			conn->set = NULL;
		}
		ldms_xprt_put(ldms);
		conn->ldms = NULL;
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		printf("%s: send_complete\n", conn->port);
		break;
	default:
		printf("%s: Unhandled ldms event '%d'\n", conn->port, e->type);
		pthread_mutex_unlock(&conn->state_lock);
		exit(-1);
	}
	pthread_mutex_unlock(&conn->state_lock);
}

/*
 * return	0 for success
 * 		1 for busy
 * 		2 for already-connected
 */
static int client_connect(struct conn *conn)
{
	int rc = 0;
	conn->ldms = ldms_xprt_new(xprt);
	if (!conn->ldms) {
		printf("ldms_xprt_new error\n");
		exit(-1);
	}
	printf("connecting to %s:%s\n", conn->hostname, conn->port);
	conn->state = CONNECTING;

	rc = ldms_xprt_connect(conn->ldms, &conn->sa, conn->sa_len,
			client_connect_cb, (void *)conn);
	if (rc)
		printf("ldms_xprt_connect error: %d\n", rc);
	return rc;
}

static void *client_routine(void *arg)
{
	struct conn *conn_list = (struct conn *)arg;
	int rc = 0;
	struct conn *conn;

	int i;
	while (1) {
		for (i = 0; i < num_conn; i++) {
			conn = &conn_list[i];
			pthread_mutex_lock(&conn->state_lock);
			if (!(conn->state == CONNECTING || conn->state == CONNECTED))
				client_connect(&conn_list[i]);
			if (conn->state == CONNECTED && conn->set_state == NEED_LOOKUP) {
				pthread_mutex_unlock(&conn->state_lock);
				char inst_name[32];
				snprintf(inst_name, 31, "%s/%s", conn->port, SET_NAME);
				rc = ldms_xprt_lookup(conn->ldms, inst_name, LDMS_LOOKUP_BY_INSTANCE,
						client_lookup_cb, (void *)conn);
				if (rc) {
					printf("Error: ldms_xprt_lookup: %d\n", rc);
					ldms_xprt_close(conn->ldms);
				}
			} else if (conn->state == CONNECTED && conn->set_state == NEED_UPDATE) {
				pthread_mutex_unlock(&conn->state_lock);
				rc = ldms_xprt_update(conn->set, client_update_cb, (void *)conn);
				if (rc) {
					printf("%d: Error doing ldms_xprt_update '%s'\n", port,
							ldms_set_instance_name_get(conn->set));
					ldms_set_delete(conn->set);
					conn->set = NULL;
					ldms_xprt_close(conn->ldms);
				}
			} else {
				pthread_mutex_unlock(&conn->state_lock);
			}
		}
		sleep(2);
		pthread_mutex_lock(&exit_mutex);
		if (exiting) {
			pthread_mutex_unlock(&exit_mutex);
			return NULL;
		}
		pthread_mutex_unlock(&exit_mutex);
	}
	return NULL;
}

static void *server_create_sets(void *arg)
{
	char instance_name[32];
	if (metric_set) {
		int rc;
		ldms_schema_t schema = ldms_schema_new(SCHEMA_NAME);
		if (!schema) {
			printf("ldms_schema_new error\n");
			pthread_exit(NULL);
		}
		rc = ldms_schema_metric_add(schema, METRIC_NAME, LDMS_V_U64);
		snprintf(instance_name, 31, "%d/%s", port, SET_NAME);
		ldms_set_t set = ldms_set_new(instance_name, schema);
		if (!set) {
			printf("ldmsd_set_new error\n");
			pthread_exit(NULL);
		}
		ldms_metric_set_u64(set, rc, 11111);
		rc = ldms_set_publish(set);
		if (rc) {
			printf("ldms_set_publish\n");
			pthread_exit(NULL);
		}
		int i = 1;
		while (1) {
			sleep(2);
			ldms_transaction_begin(set);
			ldms_metric_set_u64(set, rc, i);
			ldms_transaction_end(set);
			i++;
		}
	} else {
		while (1)
			sleep(60);
	}
}

static void do_server(struct sockaddr_in *sin)
{
	int rc;
	ldms_t ldms;
	ldms = ldms_xprt_new(xprt);
	if (!ldms) {
		printf("ldms_xprt_new error\n");
		exit(-1);
	}

	rc = ldms_xprt_listen(ldms, (void *)sin, sizeof(*sin), NULL, NULL);
	if (rc) {
		printf("ldms_xprt_listen: %d\n", rc);
		exit(-1);
	}

	printf("Listening on port %d\n", port);

	pthread_t t;
	rc = pthread_create(&t, NULL, server_create_sets, (void *)ldms);
	if (rc) {
		printf("pthread_create error %d\n", rc);
		exit(-1);
	}
	pthread_join(t, NULL);
}

static void do_client(struct sockaddr_in *_sin)
{
	struct addrinfo *ai;
	int rc, i;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};

	conn_list = calloc(num_conn, sizeof(struct conn));
	if (!conn_list) {
		printf("Out of memory\n");
		exit(-1);
	}

	if (!host) {
		printf("Please give the hostname\n");
		exit(-1);
	}

	char port_s[32];
	for (i = 0; i < num_conn; i++) {
		snprintf(port_s, 31, "%d", port + i);
		rc = getaddrinfo(host, port_s, &hints, &ai);
		if (rc) {
			printf("%s/%s: getaddrinfo failed: %s\n",
					host, port_s, gai_strerror(rc));
			exit(-1);
		}
		conn_list[i].sa = *(ai->ai_addr);
		conn_list[i].sa_len = ai->ai_addrlen;
		conn_list[i].hostname = strdup(host);
		conn_list[i].port = strdup(port_s);
		pthread_mutex_init(&conn_list[i].state_lock, 0);
		freeaddrinfo(ai);
	}

	pthread_t t;
	rc = pthread_create(&t, NULL, client_routine, conn_list);
	if (rc) {
		printf("pthread_create error %d\n", rc);
		exit(-1);
	}
	pthread_join(t, NULL);
}

static void usage()
{
	printf("	-d		Send dir request\n");
	printf("	-f		Client runs forever\n");
	printf("	-h host		Host name to connect to\n");
	printf("	-l		Send lookup request\n");
	printf("	-m		Server with a metric set\n");
	printf("	-n num_connections	Number of servers to connect to\n");
	printf("	-p port		listener port or port to connect\n");
	printf("	-s		Server mode\n");
	printf("	-u		Send lookup and update request\n");
	printf("	-x xprt		sock, rdma, or ugni\n");
}

static void process_arg(int argc, char **argv)
{
	char op;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'x':
			xprt = strdup(optarg);
			break;
		case 'p':
			port_s = strdup(optarg);
			port = atoi(optarg);
			break;
		case 'h':
			host = strdup(optarg);
			break;
		case 's':
			is_server = 1;
			break;
		case 'd':
			dir = 1;
			break;
		case 'l':
			lookup = 1;
			break;
		case 'm':
			metric_set = 1;
			break;
		case 'n':
			num_conn = atoi(optarg);
			break;
		case 'f':
			forever = 1;
			break;
		case 'u':
			update = 1;
			lookup = 1;
			break;
		case '?':
			usage();
			exit(0);
		default:
			printf("Unrecognized argument '%c'\n", op);
			exit(1);
			break;
		}
	}
}

int main(int argc, char **argv) {
	process_arg(argc, argv);
	struct sockaddr_in sin = {0};
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;

	ldms_init(512 * 1024);

	if (is_server)
		do_server(&sin);
	else
		do_client(&sin);
	sleep(1);
	printf("DONE\n");
	return 0;
}
