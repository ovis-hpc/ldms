/*
 * test_ldms_xprt_reconnect.c
 *
 *  Created on: Aug 6, 2015
 *      Author: nichamon
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

#define FMT "x:p:h:sdmn:"

char *xprt = NULL;
char *host = NULL;
int port = 0;
int is_server = 0;
int dir = 0;
int metric_set = 0;
int update = 0;

int num_conn = 0;

struct conn {
	struct sockaddr_in sin;
	ldms_t ldms;
	pthread_mutex_t state_lock;
	enum connect_state {
		INIT = 0,
		CONNECTING,
		CONNECTED,
		DISCONNECTED,
	} state;
};

struct conn *conn_list = 0;

void _log(const char *fmt, ...) {
	va_list l;
	va_start(l, fmt);
	vprintf(fmt, l);
	va_end(l);
}

void __dir_cb(ldms_t xprt, int status, ldms_dir_t dir, void *arg)
{
	switch (dir->type) {
	case LDMS_DIR_LIST:
		printf("dir_cb: DIR_LIST");
		break;
	case LDMS_DIR_ADD:
		printf("dir_cb: DIR_ADD");
		break;
	case LDMS_DIR_DEL:
		printf("dir_cb: DIR_DEL");
		break;
	default:
		break;
	}
	if (dir->set_count > 0)
		printf(": %s\n", dir->set_names[0]);
	else
		printf(": No sets\n");
	ldms_xprt_dir_free(xprt, dir);
}

void client_cb(ldms_t x, ldms_conn_event_t e, void *cb_arg)
{
	int rc = 0;
	struct sockaddr_in lsin = {0};
	struct sockaddr_in rsin = {0};
	struct conn *conn = (struct conn *)cb_arg;
	socklen_t slen;
	ldms_t ldms = conn->ldms;

	pthread_mutex_lock(&conn->state_lock);
	switch (e) {
	case LDMS_CONN_EVENT_CONNECTED:
		conn->state = CONNECTED;
		printf("connected\n");
		if (dir) {
			rc = ldms_xprt_dir(ldms, __dir_cb, (void *)ldms,
					LDMS_DIR_F_NOTIFY);
			if (rc) {
				printf("Error: ldms_xprt_dir: %d\n", rc);
				ldms_xprt_close(ldms);
			}
		}
		break;
	case LDMS_CONN_EVENT_ERROR:
		conn->state = DISCONNECTED;
		printf("conn_error\n");
		ldms_xprt_put(ldms);
		conn->ldms = NULL;
		break;
	case LDMS_CONN_EVENT_DISCONNECTED:
		conn->state = DISCONNECTED;
		printf("disconnected\n");
		ldms_xprt_put(ldms);
		conn->ldms = NULL;
		break;
	default:
		printf("Unhandled ldms event '%d'\n", e);
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
int __connect(struct conn *conn)
{
	int rc = 0;
	conn->ldms = ldms_xprt_new(xprt, _log);
	if (!conn->ldms) {
		printf("ldms_xprt_new error\n");
		exit(-1);
	}

	printf("connecting to %s:%hu\n", host, ntohs(conn->sin.sin_port));
	conn->state = CONNECTING;

	rc = ldms_xprt_connect(conn->ldms, (void *)&(conn->sin), sizeof(conn->sin),
			client_cb, (void *)conn);
	if (rc)
		printf("ldms_xprt_connect error: %d\n", rc);
	return rc;
}

void *client_routine(void *arg)
{
	struct conn *conn_list = (struct conn *)arg;
	struct sockaddr_in lsin = {0};
	struct sockaddr_in rsin = {0};
	socklen_t slen;
	struct timeval tv;
	int rc = 0;
	struct conn *conn;

	int i;
	while (1) {
		for (i = 0; i < num_conn; i++) {
			conn = &conn_list[i];
			pthread_mutex_lock(&conn->state_lock);
			if (!(conn->state == CONNECTING || conn->state == CONNECTED))
				__connect(&conn_list[i]);
			pthread_mutex_unlock(&conn->state_lock);
		}
		sleep(2);
		continue;
	}
}

void *server_create_sets(void *arg)
{
	if (metric_set) {
		int rc;
		ldms_t ldms = (ldms_t)arg;
		ldms_schema_t schema = ldms_schema_new("test");
		if (!schema) {
			printf("ldms_schema_new error\n");
			pthread_exit(NULL);
		}
		rc = ldms_schema_metric_add(schema, "A", LDMS_V_U64);
		ldms_set_t set = ldms_set_new("test_set", schema);
		if (!set) {
			printf("ldmsd_set_new error\n");
			pthread_exit(NULL);
		}
		ldms_metric_set_u64(set, rc, 11111);
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

void do_server(struct sockaddr_in *sin)
{
	int rc;

	ldms_t ldms = ldms_xprt_new(xprt, _log);
	if (!ldms) {
		printf("ldms_xprt_new error\n");
		exit(-1);
	}

	rc = ldms_xprt_listen(ldms, (void *)sin, sizeof(*sin));
	if (rc) {
		printf("ldms_xprt_listen: %d\n", rc);
		exit(-1);
	}

	printf("Listening on port %hu\n", port);

	pthread_t t;
	rc = pthread_create(&t, NULL, server_create_sets, (void *)ldms);
	if (rc) {
		printf("pthread_create error %d\n", rc);
		exit(-1);
	}
	pthread_join(t, NULL);
}

void do_client(struct sockaddr_in *_sin)
{
	struct addrinfo *ai;
	int rc, i;
	conn_list = calloc(num_conn, sizeof(struct conn));
	if (!conn_list) {
		printf("Out of memory\n");
		exit(-1);
	}

	if (!host) {
		printf("Please give the hostname\n");
		exit(-1);
	}

	rc = getaddrinfo(host, NULL, NULL, &ai);
	if (rc) {
		printf("getaddrinfo error: %d\n", rc);
		exit(-1);
	}

	for (i = 0; i < num_conn; i++) {
		conn_list[i].sin = *(struct sockaddr_in *)ai[0].ai_addr;
		conn_list[i].sin.sin_port = htons(port + i);
		pthread_mutex_init(&conn_list[i].state_lock, 0);
	}

	freeaddrinfo(ai);

	pthread_t t;
	rc = pthread_create(&t, NULL, client_routine, conn_list);
	if (rc) {
		printf("pthread_create error %d\n", rc);
		exit(-1);
	}
	pthread_join(t, NULL);
}

void usage()
{
	printf("	-x xprt		sock, rdma, or ugni\n");
	printf("	-p port		listener port or port to connect\n");
	printf("	-h host		Host name to connect to\n");
	printf("	-s		Server mode\n");
	printf("	-d		Dir request\n");
	printf("	-m		Server with a metric set\n");
	printf("	-n num_connections	Number of servers to connect to\n");
}

void process_arg(int argc, char **argv)
{
	char op;
	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'x':
			xprt = strdup(optarg);
			break;
		case 'p':
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
		case 'm':
			metric_set = 1;
			break;
		case 'n':
			num_conn = atoi(optarg);
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
	return 0;
}
