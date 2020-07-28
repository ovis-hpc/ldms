#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <getopt.h>
#include <semaphore.h>
#include <pthread.h>
#include <ovis_json/ovis_json.h>
#include <ovis_util/util.h>
#include "ldms.h"
#include "../ldmsd_request.h"
#include "../ldmsd_stream.h"

static struct option long_opts[] = {
	{"host",       required_argument, 0,  'h' },
	{"port",       required_argument, 0,  'p' },
	{"file",       required_argument, 0,  'f' },
	{"stream",     required_argument, 0,  's' },
	{"type",       required_argument, 0,  't' },
	{"xprt",       required_argument, 0,  'x' },
	{"auth",       required_argument, 0,  'a' },
	{"auth_arg",   required_argument, 0,  'A' },
	{"keep_alive", optional_argument, 0,  'k' },
	{0,            0,                 0,  0 }
};

void usage(int argc, char **argv)
{
	printf("usage: %s -x <xprt> -h <host> -p <port> "
	       "-s <stream-name> -t <stream-type> "
	       "-f <file> -a <auth> -A <auth-opt>\n",
	       argv[0]);
	exit(1);
}

static const char *short_opts = "h:p:f:s:t:x:a:A:v";

#define AUTH_OPT_MAX 128
#define KEEP_ALIVE_US 100000

sem_t conn_sem;
int conn_status;
static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		sem_post(&conn_sem);
		conn_status = 0;
		break;
	case LDMS_XPRT_EVENT_REJECTED:
		ldms_xprt_put(x);
		conn_status = ECONNREFUSED;
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		ldms_xprt_put(x);
		conn_status = ENOTCONN;
		break;
	case LDMS_XPRT_EVENT_ERROR:
		conn_status = ECONNREFUSED;
		break;
	default:
		printf("Received invalid event type %d\n", e->type);
	}
}

static int stream_publish_file(const char *stream, const char *type,
				ldms_t x, FILE *file)
{
	int rc;
	ldmsd_stream_type_t stream_type;
	size_t data_len;
	char buffer[1024*64];
	char *buf;
	size_t buf_sz = 1024 * 64;
	size_t buf_off = 0;

	if (0 == strcasecmp("raw", type))
		stream_type = LDMSD_STREAM_STRING;
	else if (0 == strcasecmp("string", type))
		stream_type = LDMSD_STREAM_STRING;
	else if (0 == strcasecmp("json", type))
		stream_type = LDMSD_STREAM_JSON;
	else
		return EINVAL;

	buf = malloc(buf_sz);
	if (!buf) {
		printf("Error allocating %d bytes of memory\n", buf_sz);
		return ENOMEM;
	}

	/* Read the whole file */
	while ((data_len = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		if (data_len > buf_sz - buf_off) {
			buf = realloc(buf, (buf_sz * 2) + data_len);
			if (!buf) {
				printf("Error allocating %d bytes of memory\n",
						(buf_sz * 2) + data_len);
				return ENOMEM;
			}
			buf_sz = (buf_sz * 2) + data_len;
		}
		memcpy(&buf[buf_off], buffer, data_len);
		buf_off += data_len;
	}

	rc = ldmsd_stream_publish(x, stream, stream_type, buf, buf_off);
	return rc;
}

int main(int argc, char **argv)
{
	int rc;
	int keep_alive_us = KEEP_ALIVE_US;
	char *host = NULL;
	char *port = NULL;
	char *xprt = "sock";
	char *filename = NULL;
	char *stream = NULL;
	int opt, opt_idx;
	char *lval, *rval;
	char *auth = "none";
	struct attr_value_list *auth_opt = NULL;
	const int auth_opt_max = AUTH_OPT_MAX;
	FILE *file;
	const char *stream_type = "string";
	ldms_t x;

	auth_opt = av_new(auth_opt_max);
	if (!auth_opt) {
		perror("could not allocate auth options");
		exit(1);
	}

	while ((opt = getopt_long(argc, argv,
				  short_opts, long_opts,
				  &opt_idx)) > 0) {
		switch (opt) {
		case 'h':
			host = strdup(optarg);
			if (!host) {
				printf("ERROR: out of memory\n");
				exit(1);
			}
			break;
		case 'p':
			port = strdup(optarg);
			if (!port) {
				printf("ERROR: out of memory\n");
				exit(1);
			}
			break;
		case 'x':
			xprt = strdup(optarg);
			if (!xprt) {
				printf("ERROR: out of memory\n");
				exit(1);
			}
			break;
		case 'a':
			auth = strdup(optarg);
			if (!auth) {
				printf("ERROR: out of memory\n");
				exit(1);
			}
			break;
		case 'A':
			lval = strtok(optarg, "=");
			rval = strtok(NULL, "");
			if (!lval || !rval) {
				printf("ERROR: Expecting -A name=value");
				exit(1);
			}
			if (auth_opt->count == auth_opt->size) {
				printf("ERROR: Too many auth options");
				exit(1);
			}
			auth_opt->list[auth_opt->count].name = lval;
			auth_opt->list[auth_opt->count].value = rval;
			auth_opt->count++;
			break;
		case 's':
			stream = strdup(optarg);
			if (!stream) {
				printf("ERROR: out of memory\n");
				exit(1);
			}
			break;
		case 'f':
			filename = strdup(optarg);
			if (!filename) {
				printf("ERROR: out of memory\n");
				exit(1);
			}
			break;
		case 't':
			if (0 == strcmp("json", optarg)) {
				stream_type = "json";
			} else if (0 == strcmp("string", optarg)) {
				stream_type = "string";
			} else {
				printf("The type argument must be 'json' or 'string'\n");
				usage(argc, argv);
			}
			break;
		case 'k':
			keep_alive_us = atoi(optarg);
			break;
		default:
			usage(argc, argv);
		}
	}
	if (!host || !port || !stream)
		usage(argc, argv);

	if (filename)
		file = fopen(filename, "r");
	else
		file = stdin;

	x = ldms_xprt_new_with_auth(xprt, NULL, auth, auth_opt);
	if (!x) {
		printf("Error %d creating the '%s' transport\n", errno, xprt);
		exit(1);
	}

	sem_init(&conn_sem, 0, 0);

	rc = ldms_xprt_connect_by_name(x, host, port, event_cb, NULL);
	if (rc) {
		printf("Error %d connecting to %s:%s\n", rc, host, port);
		exit(1);
	}
	struct timespec ts;
	ts.tv_sec = time(NULL) + 2;
	ts.tv_nsec = 0;
	sem_timedwait(&conn_sem, &ts);
	if (conn_status) {
		printf("Error %d connecting to %s:%s\n", rc, host, port);
		exit(1);
	}

	rc = stream_publish_file(stream, stream_type, x, file);
	if (rc) {
		printf("Error %d sending stream\n");
		exit(1);
	}
	usleep(keep_alive_us);
	ldms_xprt_close(x);
	return 0;
}
