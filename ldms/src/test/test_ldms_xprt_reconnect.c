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

#define FMT "x:p:h:n:a:sdlmfu"

char *xprt = NULL;
char *host = NULL;
int port = 0;
int is_server = 0;

int dir = 0;
int lookup = 0;
int update = 0;

int metric_set = 0;
char *secretword = 0;
int num_conn = 1;
int forever = 0;

int exiting = 0;
pthread_mutex_t exit_mutex = PTHREAD_MUTEX_INITIALIZER;

#define SCHEMA_NAME "test"
#define SET_NAME "test_set"
#define METRIC_NAME "A"

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

void _log(const char *fmt, ...) {
	va_list l;
	va_start(l, fmt);
	vprintf(fmt, l);
	va_end(l);
}

void client_update_cb(ldms_t ldms, ldms_set_t set, int status, void *arg)
{
	struct conn *conn = (struct conn *)arg;
	int port = ntohs(conn->sin.sin_port);
	const char *set_name = ldms_set_instance_name_get(conn->set);
	if (status) {
		printf("%d: Update failed '%s'\n", port, set_name);
		ldms_set_delete(conn->set);
		conn->set = NULL;
		ldms_xprt_close(ldms);
		return;
	}

	conn->set_state = UPDATED;

	if (!ldms_set_is_consistent(conn->set)) {
		printf("%d: Set %s is inconsistent.\n", port, set_name);
		return;
	}

	printf("%d: '%s' is updated\n", port, set_name);

	pthread_mutex_lock(&exit_mutex);
	if (!forever)
		exiting = 1;
	else
		conn->set_state = NEED_UPDATE;
	pthread_mutex_unlock(&exit_mutex);

	return;
}

void client_lookup_cb(ldms_t ldms, enum ldms_lookup_status status,
		int more, ldms_set_t set, void *arg)
{
	char inst_name[32];
	int rc;
	struct conn *conn = (struct conn *)arg;
	int port = ntohs(conn->sin.sin_port);

	snprintf(inst_name, 31, "%d/%s", port, SET_NAME);
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
			printf("%d: Error doing ldms_xprt_update '%s'\n", port,
					ldms_set_instance_name_get(set));
			ldms_set_delete(set);
			conn->set = NULL;
			ldms_xprt_close(ldms);
		}
	}

	printf("%d: lookup_cb '%s'\n", port, inst_name);
	pthread_mutex_lock(&exit_mutex);
	if (!forever && !update)
		exiting = 1;
	pthread_mutex_unlock(&exit_mutex);
}

void client_dir_cb(ldms_t ldms, int status, ldms_dir_t dir, void *arg)
{
	int rc;
	struct conn *conn = (struct conn *)arg;
	int port = ntohs(conn->sin.sin_port);
	switch (dir->type) {
	case LDMS_DIR_LIST:
		printf("%d: dir_cb: DIR_LIST", port);
		conn->set_state = DIR;
		if (dir->set_count > 0)
			printf(": %s\n", dir->set_names[0]);
		else
			printf(": No sets\n");
		if (lookup && dir->set_count > 0) {
			printf("%d: doing lookup '%s'\n", port, dir->set_names[0]);
			rc = ldms_xprt_lookup(ldms, dir->set_names[0], LDMS_LOOKUP_BY_INSTANCE,
					client_lookup_cb, (void *)conn);
			if (rc) {
				printf("Error: ldms_xprt_lookup: %d\n", rc);
				ldms_xprt_close(ldms);
			}
		}
		break;
	case LDMS_DIR_ADD:
		printf("%d: dir_cb: DIR_ADD\n", port);
		break;
	case LDMS_DIR_DEL:
		printf("%d: dir_cb: DIR_DEL\n", port);
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

void client_connect_cb(ldms_t x, ldms_conn_event_t e, void *cb_arg)
{
	int rc = 0;
	struct sockaddr_in lsin = {0};
	struct sockaddr_in rsin = {0};
	struct conn *conn = (struct conn *)cb_arg;
	socklen_t slen;
	ldms_t ldms = conn->ldms;
	int port = ntohs(conn->sin.sin_port);
	pthread_mutex_lock(&conn->state_lock);
	switch (e) {
	case LDMS_CONN_EVENT_CONNECTED:
		conn->state = CONNECTED;
		printf("%d: connected\n", port);
		if (dir) {
			rc = ldms_xprt_dir(ldms, client_dir_cb, (void *)conn,
					LDMS_DIR_F_NOTIFY);
			if (rc) {
				printf("Error: ldms_xprt_dir: %d\n", rc);
				ldms_xprt_close(ldms);
			}
		} else if (lookup) {
			char inst_name[32];
			snprintf(inst_name, 31, "%d/%s", port, SET_NAME);
			rc = ldms_xprt_lookup(ldms, inst_name, LDMS_LOOKUP_BY_INSTANCE,
					client_lookup_cb, (void *)conn);
			if (rc) {
				printf("Error: ldms_xprt_lookup: %d\n", rc);
				ldms_xprt_close(ldms);
			}
		}
		break;
	case LDMS_CONN_EVENT_ERROR:
		conn->state = DISCONNECTED;
		printf("%d: conn_error\n", port);
		ldms_xprt_put(ldms);
		conn->ldms = NULL;
		break;
	case LDMS_CONN_EVENT_DISCONNECTED:
		conn->state = DISCONNECTED;
		printf("%d: disconnected\n", port);
		if (conn->set) {
			ldms_set_delete(conn->set);
			conn->set = NULL;
		}
		ldms_xprt_put(ldms);
		conn->ldms = NULL;
		break;
	default:
		printf("%d: Unhandled ldms event '%d'\n", port, e);
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
int client_connect(struct conn *conn)
{
	int rc = 0;
#ifdef ENABLE_AUTH
	conn->ldms = ldms_xprt_with_auth_new(xprt, _log, secretword);
#else /* ENABLE_AUTH */
	conn->ldms = ldms_xprt_new(xprt, _log);
#endif /* ENABLE_AUTH */
	if (!conn->ldms) {
		printf("ldms_xprt_new error\n");
		exit(-1);
	}

	printf("connecting to %s:%hu\n", host, ntohs(conn->sin.sin_port));
	conn->state = CONNECTING;

	rc = ldms_xprt_connect(conn->ldms, (void *)&(conn->sin), sizeof(conn->sin),
			client_connect_cb, (void *)conn);
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
				client_connect(&conn_list[i]);
			if (conn->state == CONNECTED && conn->set_state == NEED_LOOKUP) {
				pthread_mutex_unlock(&conn->state_lock);
				char inst_name[32];
				int port = ntohs(conn->sin.sin_port);
				snprintf(inst_name, 31, "%d/%s", port, SET_NAME);
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

void *server_create_sets(void *arg)
{
	char instance_name[32];
	if (metric_set) {
		int rc;
		ldms_t ldms = (ldms_t)arg;
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
	ldms_t ldms;
#ifdef ENABLE_AUTH
	ldms = ldms_xprt_with_auth_new(xprt, _log, secretword);
#else /* ENABLE_AUTH */
	ldms = ldms_xprt_new(xprt, _log);
#endif /* ENABLE_AUTH */
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
	printf("	-a secretword	Test with the authentication\n");
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
		case 'a':
			secretword = strdup(optarg);
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
