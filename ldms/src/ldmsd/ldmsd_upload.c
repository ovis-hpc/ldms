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
#include <ovis_util/util.h>
#include "ldms.h"
#include "ldmsd_request.h"
#include "ldmsd_stream.h"

static ldms_t ldms;
static char *stream;
static sem_t conn_sem;
static int conn_status;
static sem_t recv_sem;
static pthread_mutex_t ldms_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct option long_opts[] = {
	{"host",     required_argument, 0,  'h' },
	{"port",     required_argument, 0,  'p' },
	{"file",     required_argument, 0,  'f' },
	{"stream",   required_argument, 0,  's' },
	{"xprt",     required_argument, 0,  'x' },
	{"auth",     required_argument, 0,  'a' },
	{"auth_arg", required_argument, 0,  'A' },
	{"verbose",  no_argument,       0,  'v' },
	{0,          0,                 0,  0 }
};

void usage(int argc, char **argv)
{
	printf("usage: %s -x <xprt> -h <host> -p <port> -s <stream-name> "
	       "-f <file> -a <auth> -A <auth-opt>\n",
	       argv[0]);
	exit(1);
}

static const char *short_opts = "h:p:f:s:x:a:A:v";

#define AUTH_OPT_MAX 128

static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		sem_post(&conn_sem);
		conn_status = 0;
		printf("%s:%d\n", __func__, __LINE__);
		break;
	case LDMS_XPRT_EVENT_REJECTED:
		ldms_xprt_put(x);
		conn_status = ECONNREFUSED;
		printf("%s:%d\n", __func__, __LINE__);
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		ldms_xprt_put(x);
		conn_status = ENOTCONN;
		printf("%s:%d\n", __func__, __LINE__);
		break;
	case LDMS_XPRT_EVENT_ERROR:
		conn_status = ECONNREFUSED;
		printf("%s:%d\n", __func__, __LINE__);
		break;
	case LDMS_XPRT_EVENT_RECV:
		printf("%s:%d %s\n", __func__, __LINE__, e->data);
		sem_post(&recv_sem);
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		printf("%s:%d \n", __func__, __LINE__);
		break;
	default:
		printf("Received invalid event type %d\n", e->type);
	}
}

#define SLURM_NOTIFY_TIMEOUT 5

ldms_t setup_connection(const char *xprt, const char *host,
			const char *port, const char *auth)
{
	char hostname[PATH_MAX];
	const char *timeout = "5";
	int rc;
	struct timespec ts;

	if (!host) {
		if (0 == gethostname(hostname, sizeof(hostname)))
			host = hostname;
	}
	if (!timeout) {
		ts.tv_sec = time(NULL) + 5;
		ts.tv_nsec = 0;
	} else {
		int to = atoi(timeout);
		if (to <= 0)
			to = 5;
		ts.tv_sec = time(NULL) + to;
		ts.tv_nsec = 0;
	}
	ldms = ldms_xprt_new_with_auth(xprt, auth, NULL);
	if (!ldms) {
		printf("Error %d creating the '%s' transport\n",
		       errno, xprt);
		return NULL;
	}

	sem_init(&recv_sem, 1, 0);
	sem_init(&conn_sem, 1, 0);

	rc = ldms_xprt_connect_by_name(ldms, host, port, event_cb, NULL);
	if (rc) {
		printf("Error %d connecting to %s:%s\n",
		       rc, host, port);
		return NULL;
	}
	sem_timedwait(&conn_sem, &ts);
	if (conn_status)
		return NULL;
	return ldms;
}

int main(int argc, char **argv)
{
	char *host = NULL;
	char *port = NULL;
	char *xprt = "sock";
	char *filename = NULL;
	char *stream = NULL;
	int opt, opt_idx;
	int verbose = 0;
	char *lval, *rval;
	char *auth = "none";
	struct attr_value_list *auth_opt = NULL;
	const int auth_opt_max = AUTH_OPT_MAX;
	FILE *file;

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
			break;
		case 'p':
			port = strdup(optarg);
			break;
		case 'x':
			xprt = strdup(optarg);
			break;
		case 'a':
			auth = strdup(optarg);
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
			break;
		case 'f':
			filename = strdup(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage(argc, argv);
		}
	}
	if (!host || !port || !filename || !stream)
		usage(argc, argv);

	if (filename) {
		file = fopen(filename, "r");
		if (!file) {
			printf("Error %d opening the file '%s'\n", errno, filename);
			exit(errno);
		}
	} else {
		file = stdin;
	}

	ldms_t ldms = setup_connection(xprt, host, port, auth);
	int rc = ldmsd_stream_publish(ldms, stream, LDMSD_ATTR_STRING, "this is a test", sizeof("this is a test"));
	if (rc)
		printf("Error %d uploading data.\n", rc);
	else
		printf("The data was successfully uploaded.\n");
	return 0;
}
