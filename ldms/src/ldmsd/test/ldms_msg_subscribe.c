#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <getopt.h>
#include <pthread.h>
#include <netdb.h>
#include <assert.h>
#include "ldms.h"

static ldms_t ldms;
static FILE *file;

static struct option long_opts[] = {
	{"port",     required_argument, 0,  'p' },
	{"file",     required_argument, 0,  'f' },
	{"msg_name", required_argument, 0,  'm' },
	{"xprt",     required_argument, 0,  'x' },
	{"auth",     required_argument, 0,  'a' },
	{"auth_arg", required_argument, 0,  'A' },
	{"host",     required_argument, 0,  'h' },
	{0,          0,                 0,  0 }
};

void usage(int argc, char **argv)
{
	printf("usage: %s -x <xprt> -p <port> -h <host> "
	       "-m <msg-name> "
	       "-f <file> -a <auth> -A <auth-opt>\n",
	       argv[0]);
	exit(1);
}

static const char *short_opts = "p:f:m:x:a:A:h:";

#define AUTH_OPT_MAX 128

static int msg_cb_fn(ldms_msg_event_t ev, void *cb_arg)
{
	/* cb_arg is the pointer supplied to ldms_msg_subscribe() */
	switch (ev->type) {
	case LDMS_MSG_EVENT_RECV:
		if (ev->recv.type == LDMS_MSG_JSON) {
			fputs(ev->recv.data, file);
		} else {
			/* See `struct ldms_msg_event_s` for more information. */
			fprintf(file, "name: %s\n", ev->recv.name);
			fprintf(file, "hop : %d\n", ev->hop_num);
			fprintf(file, "type: %s\n", ldms_msg_type_sym(ev->recv.type));
			fprintf(file, "data: %s\n", ev->recv.data);
		}
		fflush(file);
		break;
	case LDMS_MSG_EVENT_CLIENT_CLOSE:
		/* This is the last event guaranteed be to delivered to this client. The
		 * resources application associate to this client (e.g. cb_arg) could be
		 * safely freed at this point. */
		break;
	default:
		/* ignore other events */;
	}
	return 0;
}

static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		/* You might handle this to filter who you are
		 * allowing connections from */
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_ERROR:
		ldms_xprt_close(x);
		break;
	case LDMS_XPRT_EVENT_RECV:
		/* Ignore .. shouldn't get it anyway */
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		/* Ignore .. shouldn't get it anyway */
		break;
	default:
		break;
	}
}

static int setup_connection(char *xprt, char *host, char *port, char *auth, struct attr_value_list *auth_opt)
{
	int rc;
	struct addrinfo *ai = NULL, hint;
	memset(&hint, 0, sizeof(hint));
	hint.ai_flags = AI_PASSIVE;
	hint.ai_family = AF_INET;

	rc = getaddrinfo(host, port, &hint, &ai);
	if (rc)
		return errno;

	ldms = ldms_xprt_new_with_auth(xprt, auth, auth_opt);
	if (!ldms) {
		fprintf(stderr, "Error %d creating the '%s' transport\n", errno, xprt);
		rc = errno;
		goto out;
	}

	rc = ldms_xprt_listen_by_name(ldms, host, port, event_cb, NULL);
	if (rc)
		fprintf(stderr,
			"Error %d attempting to listen at %s:%s:%s:%s\n",
			rc, xprt, host, port, auth);
 out:
	freeaddrinfo(ai);
	return rc;
}

int main(int argc, char **argv)
{
	char *xprt = "sock";
	char *filename = NULL;
	char *msg = NULL;
	char *host = NULL;
	char *port = NULL;
	int opt, opt_idx;
	char *lval, *rval;
	char *auth = "none";
	struct attr_value_list *auth_opt = NULL;
	const int auth_opt_max = AUTH_OPT_MAX;

	ldms_init(0);

	auth_opt = av_new(auth_opt_max);
	if (!auth_opt) {
		perror("could not allocate auth options");
		exit(1);
	}

	while ((opt = getopt_long(argc, argv, short_opts, long_opts, &opt_idx)) > 0) {
		switch (opt) {
		case 'p':
			port = optarg;
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
		case 'm':
			msg = strdup(optarg);
			if (!msg) {
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
		case 'h':
			host = strdup(optarg);
			break;
		default:
			usage(argc, argv);
		}
	}
	if (!msg)
		msg = ".*";
	if (!port)
		usage(argc, argv);

	if (filename) {
		file = fopen(filename, "w");
		if (!file) {
			perror("The file could not be opened.");
			exit(1);
		}
	} else {
		file = stdout;
	}

	int rc = setup_connection(xprt, host, port, auth, auth_opt);
	if (rc) {
		exit(1);
	}
	ldms_msg_client_t client = ldms_msg_subscribe(".*", 1, msg_cb_fn, NULL, "Subscribe to all messages.");
	if (!client) {
		perror("Could not subscribe to .*\n");
		return 1;
	}
	while (0 == sleep(10)) {
		/* wait for signal or kill */
	}
	return 0;
}
