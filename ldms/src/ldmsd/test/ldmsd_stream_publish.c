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
	{"host",     required_argument, 0,  'h' },
	{"port",     required_argument, 0,  'p' },
	{"file",     required_argument, 0,  'f' },
	{"stream",   required_argument, 0,  's' },
	{"type",     required_argument, 0,  't' },
	{"xprt",     required_argument, 0,  'x' },
	{"auth",     required_argument, 0,  'a' },
	{"auth_arg", required_argument, 0,  'A' },
	{"line",     no_argument,	0,  'l' },
	{"repeat",   required_argument, 0,  'r' },
	{"interval", required_argument, 0,  'i' },
	{"new",      no_argument,       0,  'n' },
	{"new_only", no_argument,       0,  'N' },
	{0,          0,                 0,  0 }
};

void usage(int argc, char **argv) __attribute__((noreturn));
void usage(int argc, char **argv)
{
	printf("usage: %s -x <xprt> -h <host> -p <port> "
	       "-s <stream-name> -t <stream-type> "
	       "-f <file> -a <auth> -A <auth-opt> "
	       "-l -r <count> -i <microsec> -n -N\n",
	       argv[0]);
	exit(1);
}

static const char *short_opts = "h:p:f:s:t:x:a:A:lr:i:nN";

#define AUTH_OPT_MAX 128

int main(int argc, char **argv)
{
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
	ldmsd_stream_type_t typ = LDMSD_STREAM_STRING;
	int line_mode = 0;	/* publish each line separately */
	int repeat = 0;
	unsigned interval = 0;
	enum {
		NEW_FALSE = 0,
		NEW_TRUE,
		NEW_ONLY,
	} stream_new = NEW_FALSE;

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
				typ = LDMSD_STREAM_JSON;
			} else if (0 == strcmp("string", optarg)) {
				stream_type = "string";
				typ = LDMSD_STREAM_STRING;
			} else {
				printf("The type argument must be 'json' or 'string'\n");
				usage(argc, argv);
			}
			break;
		case 'l':
			line_mode = 1;
			break;
		case 'r':
			repeat = atoi(optarg);
			break;
		case 'i':
			interval = (unsigned)atoi(optarg);
			break;
		case 'n':
			stream_new = NEW_TRUE;
			break;
		case 'N':
			stream_new = NEW_ONLY;
			break;
		default:
			usage(argc, argv);
		}
	}
	if (!host || !port || !stream)
		usage(argc, argv);

	if (filename) {
		file = fopen(filename, "r");
		if (!file) {
			perror(filename);
			exit(1);
		}
	} else {
		if (repeat || interval) {
			printf("%s: To use -r or -i, -f FILE must also be used.\n",argv[0]);
			exit(1);
		}
		file = stdin;
	}
	if (!repeat)
		repeat = 1;

	int rc;
	ldms_t ldms = NULL;
	if (stream_new || line_mode) {
		/* Create a transport endpoint */
		ldms = ldms_xprt_new_with_auth(xprt, NULL, auth, NULL);
		if (!ldms) {
			rc = errno;
			printf("Failed to create the LDMS transport endpoint.\n");
			return rc;
		}
		rc = ldms_xprt_connect_by_name(ldms, host, port, NULL, NULL);
		if (rc){
			printf("Error %d connecting to peer\n", rc);
			return rc;
		}
	}
	if (stream_new) {
		/* Create and send a STREAM_NEW message */
		rc = ldmsd_stream_new_publish(stream, ldms);
		if (rc) {
			printf("Error %d creating stream and notifying client\n", rc);
			return rc;
		}
		if (NEW_ONLY == stream_new)
			return 0;
	}

	int k;
	if (!line_mode) {
		for (k = 0; k < repeat; k++) {
			rc = ldmsd_stream_publish_file(stream, stream_type, xprt,
						host, port, auth, auth_opt, file);
			if (repeat == 1 && rc) {
				printf("Error %d publishing file.\n", rc);
				return rc;
			}
			usleep(interval);
			if (k)
				printf("loop: %d returned %d\n", k, rc);
		}
		return 0;
	}

	for (k = 0; k < repeat; k++) {
		char line_buffer[4096];
		char *s;
		if (k)
			rewind(file);
		while (0 != (s = fgets(line_buffer, sizeof(line_buffer)-1, file))) {
			ldmsd_stream_publish(ldms, stream, typ, s, strlen(s)+1);
		}
		if (k)
			printf("loop: %d finished.\n", k);
		usleep(interval);
	}
	ldms_xprt_close(ldms);
	return rc;
}
