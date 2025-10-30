#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <pthread.h>
#include "ldms.h"

static struct option long_opts[] = {
	{"help",     no_argument,       0,  'H' },
	{"host",     required_argument, 0,  'h' },
	{"port",     required_argument, 0,  'p' },
	{"xprt",     required_argument, 0,  'x' },
	{"auth",     required_argument, 0,  'a' },
	{"auth_opt", required_argument, 0,  'A' },
	{"file",     required_argument, 0,  'f' },
	{"msg_tag",  required_argument, 0,  'm' },
	{"type",     required_argument, 0,  't' },
	{"uid",      required_argument, 0,  'U' },
	{"gid",      required_argument, 0,  'G' },
	{"perm",     required_argument, 0,  'P' },
	{"line",     no_argument,	0,  'l' },
	{"repeat",   required_argument, 0,  'r' },
	{"interval", required_argument, 0,  'i' },
	{"delay",    required_argument, 0,  'D' },
	{"reconnect",no_argument,	0,  'R' },
	{"retry",    required_argument,	0,  'W' },
	{"verbose",  no_argument,       0,  'v' },
	{"giveup",   required_argument, 0,  'g' },
	{0,          0,                 0,  0 }
};

static const char *short_opts = "h:p:f:m:t:x:a:A:U:G:P:lr:i:D:W:g:RvH";

#define AUTH_OPT_MAX 128

#define CREDIT_RETRY_PAUSE 100000000 /* 1/10th sec if credit shortage*/

static int giveup_wait = 0;

static int rc;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static enum app_state {
	CONNECTED,
	DISCONNECTED,
	IO_WAIT,
} state = DISCONNECTED;

static int verbose;
static uint64_t new_quota;
static uint64_t last_quota = 0;

static  void xprt_event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	pthread_mutex_lock(&lock);
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		state = CONNECTED;
		pthread_cond_signal(&cond);
		break;
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_ERROR:
	case LDMS_XPRT_EVENT_DISCONNECTED:
		state = DISCONNECTED;
		pthread_cond_signal(&cond);
		break;
	case LDMS_XPRT_EVENT_SEND_QUOTA_DEPOSITED:
		new_quota = e->quota.quota;
		if (state == IO_WAIT) {
			if (verbose) {
				if (new_quota != last_quota)
					printf("Quota is %ld\n", new_quota);
				last_quota = new_quota;
			}
		}
		state = CONNECTED;
		pthread_cond_signal(&cond);
		break;
	default:
		/* ignore */
		break;
	}
	pthread_mutex_unlock(&lock);
}

/* Create & connect a transport endpoint, or return NULL. When NULL,
 * static global rc will be set and the reasonable action is to exit.
 * If retry is nonzero, waits retry millis between connection attempts
 * unless an authentication error occurs.
 */
static ldms_t get_ldms(const char *xprt, const char *auth,
			struct attr_value_list *auth_opt,
			const char *host, const char *port,
			int retry, int verbose)
{
	ldms_t ldms = NULL;
	if (verbose) {
		printf("get_ldms: retry: %d\n", retry);
	}
	do {
		assert(state == DISCONNECTED);

		ldms = ldms_xprt_new_with_auth(xprt, auth, auth_opt);
		if (!ldms) {
			rc = errno;
			printf("Failed to allocate the local LDMS transport endpoint.\n");
			return NULL;
		}
		rc = ldms_xprt_connect_by_name(ldms, host, port, xprt_event_cb, NULL);
		if (!rc) {
			pthread_mutex_lock(&lock);
			pthread_cond_wait(&cond, &lock);
			pthread_mutex_unlock(&lock);
		}
		switch (state) {
		case CONNECTED:
			if (verbose) {
				printf("Connected to peer %s:%s xprt=%s auth=%s\n",
				       host, port, xprt, auth);
			}
			return ldms;
		default:
			if (verbose)
				printf("Connection ERROR\n");
			ldms_xprt_close(ldms);
			ldms = NULL;
			if (retry) {
				if (verbose) {
					printf("Waiting %d millis to connect to daemon "
					       "at %s:%s xprt=%s auth=%s\n",
					       retry, host, port, xprt, auth);
				}
				usleep(retry * 1000);
			}
			break;
		}
	} while (retry);
	return ldms;
}

void enobufs_wait()
{
	if (verbose) {
		printf("Publish lacks send credits needed to "
		       "complete transfer, waiting ... ");
		fflush(stdout);
	}
	pthread_mutex_lock(&lock);
	state = IO_WAIT;
	while (state == IO_WAIT) {
		if (!giveup_wait) {
			pthread_cond_wait(&cond, &lock);
		} else {
			struct timespec to = {0, 0};
			clock_gettime(CLOCK_REALTIME, &to);
			to.tv_sec += giveup_wait;
			rc = pthread_cond_timedwait(&cond, &lock, &to);
			if (rc == ETIMEDOUT) {
				printf("Giving up after waiting %d seconds.\n",
				       giveup_wait);
				fflush(stdout);
				exit(1);
			}
		}
	}
	pthread_mutex_unlock(&lock);
	if (verbose)
		printf("continuing.\n");
}

int main(int argc, char **argv)
{
	char *filename = NULL;
	char *msg = NULL;
	int opt, opt_idx;
	char *lval, *rval;
	struct attr_value_list *auth_opt = NULL;
	const int auth_opt_max = AUTH_OPT_MAX;
	FILE *file;
	ldms_msg_type_t typ = LDMS_MSG_STRING;
	uid_t uid = geteuid();
	gid_t gid = getegid();
	mode_t perm = 0777;
	struct ldms_cred cred;
	int line_mode = 0;
	int repeat = 0;
	int reconnect = 0;
	int interval = 0;
	int delay = 0;
	int retry = 0;
	ldms_t ldms = NULL;
	char *lbuf = NULL;
	size_t lbuf_sz = 0;

	auth_opt = av_new(auth_opt_max);
	if (!auth_opt) {
		perror("could not allocate auth options");
		exit(ENOMEM);
	}
	char *host = strdup("localhost");
	char *port = strdup("411");
	char *xprt = strdup("sock");
	char *auth = strdup("none");
	if (!host || !port || !xprt || !auth) {
		printf("ERROR: out of memory\n");
		rc = ENOMEM;
		goto out;
	}

	while ((opt=getopt_long(argc, argv, short_opts, long_opts, &opt_idx)) > 0) {
		switch (opt) {
		case 'h':
			free(host);
			host = strdup(optarg);
			if (!host) {
				printf("ERROR: out of memory\n");
				rc = ENOMEM;
				goto out;
			}
			break;
		case 'p':
			free(port);
			port = strdup(optarg);
			if (!port) {
				printf("ERROR: out of memory\n");
				rc = ENOMEM;
				goto out;
			}
			break;
		case 'x':
			free(xprt);
			xprt = strdup(optarg);
			if (!xprt) {
				printf("ERROR: out of memory\n");
				rc = ENOMEM;
				goto out;
			}
			break;
		case 'a':
			free(auth);
			auth = strdup(optarg);
			if (!auth) {
				printf("ERROR: out of memory\n");
				rc = ENOMEM;
				goto out;
			}
			break;
		case 'A':
			lval = strtok(optarg, "=");
			rval = strtok(NULL, "");
			if (!lval || !rval) {
				printf("ERROR: Expecting -A name=value");
				goto usage;
			}
			if (auth_opt->count == auth_opt->size) {
				printf("ERROR: Too many auth options");
				goto usage;
			}
			auth_opt->list[auth_opt->count].name = lval;
			auth_opt->list[auth_opt->count].value = rval;
			auth_opt->count++;
			break;
		case 'm':
			msg = strdup(optarg);
			if (!msg) {
				printf("ERROR: out of memory\n");
				rc = ENOMEM;
				goto out;
			}
			break;
		case 'f':
			filename = strdup(optarg);
			if (!filename) {
				printf("ERROR: out of memory\n");
				rc = ENOMEM;
				goto out;
			}
			break;
		case 't':
			if (0 == strcasecmp("json", optarg)) {
				typ = LDMS_MSG_JSON;
			} else if (0 == strcasecmp("string", optarg)) {
				typ = LDMS_MSG_STRING;
			} else if (0 == strcasecmp("avro_ser", optarg)) {
				typ = LDMS_MSG_AVRO_SER;
			} else {
				printf("%s: The type argument must be 'json', "
					"'string', or 'avro_ser', not %s\n",
					argv[0], optarg);
				goto usage;
			}
			break;
		case 'l':
			line_mode = 1;
			break;
		case 'r':
			repeat = atoi(optarg);
			if (repeat <= 0) {
				printf("%s: The repeat argument must be a positive"
				       " number of iterations, not %s.\n",
				       argv[0], optarg);
				goto usage;
			}
			break;
		case 'R':
			reconnect = 1;
			break;
		case 'i':
			interval = atoi(optarg);
			if (interval <= 0) {
				printf("%s: The interval argument must be a positive"
				       " number of microseconds, not %s.\n", argv[0], optarg);
				goto usage;
			}
			break;
		case 'D':
			delay = atoi(optarg);
			if (delay <= 0 || delay > 999999999) {
				printf("%s: The delay argument must be a positive"
				" number of nanoseconds < 1 billion, not %s.\n",
				       argv[0], optarg);
				goto usage;
			}
			break;
		case 'W':
			retry = atoi(optarg);
			if (retry < 0) {
				printf("%s: The retry wait must be a positive"
				       " number of milliseconds, not %s.\n",
				       argv[0], optarg);
				goto usage;
			}
			break;
		case 'H':
			goto usage;
		case 'v':
			verbose = 1;
			break;
		case 'U':
			uid = strtoul(optarg, NULL, 0);
			if (uid > UINT32_MAX) {
				printf("%s: The uid must be smaller than UINT32_MAX. Got %s\n",
					argv[0], optarg);
				goto usage;
			}
			break;
		case 'G':
			gid = strtoul(optarg, NULL, 0);
			if (gid > UINT32_MAX) {
				printf("%s: The gid must be smaller than UINT32_MAX. Got %s\n",
						argv[0], optarg);
				goto usage;
			}
			break;
		case 'P':
			errno = 0;
			perm = strtoul(optarg, NULL, 8);
			if (errno) {
				printf("%s: permissions argument should be an octal"
				       " number, not %s.\n", argv[0], optarg);
				goto usage;
			}
		case 'g':
			giveup_wait = atoi(optarg);
			if (giveup_wait < 0) {
				printf("%s: the specified giveup waitime ('%s') must be > 0.\n",
				      argv[0], optarg);
				goto usage;
			}
			break;
		default:
			printf("Unrecognized option '%c'\n", opt);
			goto usage;
		}
	}

	if (!msg || msg[0] == '\0') {
		printf("%s: message tag name (-m, --msg_tag) is required\n",argv[0]);
		goto usage;
	}
	if (filename) {
		file = fopen(filename, "r");
		if (!file) {
			perror(filename);
			goto out;
		}
	} else {
		if (repeat || interval || reconnect) {
			printf("%s: The (-r, -R, and -i) options can only be used "
			       "with the file (-f) option.\n", argv[0]);
			goto usage;
		}
		file = stdin;
	}
	if (repeat && !interval)
		interval = 10000000;
	if (!repeat)
		repeat = 1;

	if (verbose) {
		printf("sending data to host=%s port=%s xprt=%s"
		       " auth=%s msg_tag=%s from %s\n",
			host, port, xprt, auth, msg,
			(filename ? filename : "pipe") );
	}

	cred.uid = uid;
	cred.gid = gid;
	int k;
	struct timespec line_delay = { 0, delay };
	size_t cnt;
	/* repeat whole file -r times, ignoring errors except if on first try. */
	if (filename) {
		if (!line_mode) {
			for (k = 0; k < repeat; k++) {
				if (!ldms) {
					ldms = get_ldms(xprt, auth, auth_opt,
							host, port, retry, verbose);
					if (!ldms)
						return rc;
				}
				do {
					rewind(file);
					rc = ldms_msg_publish_file(ldms, msg, typ,
								   &cred, perm, file);
					if (!rc)
						break;
					enobufs_wait();
				} while (rc == ENOBUFS);
				if (rc) {
					ldms_xprt_close(ldms);
					ldms = NULL;
					goto out;
				}
				usleep(interval);
				if (reconnect) {
					ldms_xprt_close(ldms);
					ldms = NULL;
				}
			}
			rc = 0;
			goto out;
		}

		/* repeat file line by line -r times, returning first error seen. */
		if (!ldms) {
			ldms = get_ldms(xprt, auth, auth_opt, host, port, retry, verbose);
			if (!ldms)
				goto out;
		}
		for (k = 0; k < repeat; k++) {
			if (k)
				rewind(file);
			while (0 < (int)(cnt = getline(&lbuf, &lbuf_sz, file))) {
				do {
					rc = ldms_msg_publish(ldms, msg, typ,
							      &cred, perm, lbuf, cnt+1);
					if (!rc)
						break;
					enobufs_wait();
				} while (rc == ENOBUFS);
				if (delay)
					nanosleep(&line_delay, NULL);
			}
			usleep(interval);
			if (reconnect) {
				ldms_xprt_close(ldms);
				ldms = NULL;
			}
		}
		goto out;
	}

	/* Process pipe input as file or lines. If this is a pipe, the
	 * application may exit before all pipe data is delivered. Use
	 * the -D option to add delay between message publications to
	 * attempt to ensure delivery.
	 */
	ldms = get_ldms(xprt, auth, auth_opt, host, port, retry, verbose);
	if (!ldms)
		goto out;

	if (!line_mode) {
		do {
			rc = ldms_msg_publish_file(ldms, msg, typ, &cred, perm, file);
			if (!rc)
				break;
			enobufs_wait();
		} while (rc == ENOBUFS);
	} else {
		int line = 0;
		while (0 < (int)(cnt = getline(&lbuf, &lbuf_sz, file))) {
			line++;
			do {
				rc = ldms_msg_publish(ldms, msg, typ, &cred, perm,
						      lbuf, cnt+1);
				if (!rc)
					break;
				enobufs_wait();
			} while (rc == ENOBUFS);
			if (rc) {
				printf("error %d(%s) at line %d publishing: %s\n",
				       rc, strerror(rc), line, lbuf);
				printf("ignore the rest of the input.\n");
				break;
			}
			if (delay)
				nanosleep(&line_delay, NULL);
		}
	}
	goto out;

usage:
	rc = 1;
	/* "Hh:p:f:m:t:x:a:A:U:G:P:lr:i:D:RW:vg" */
	printf("usage: %s -x <xprt> -h <host> -p <port>\n"
	       "\t-x <xprt> the transport name, e.g. 'sock', 'rdma'\n"
	       "\t-h <name> the subscriber host name.\n"
	       "\t-p <port> the subscriber port number.\n"
	       "\t-a <auth> -A <auth-opt>\n"
	       "\t-m <msg-tag> The message tag to use for published messages.\n"
	       "\t-t <msg-type> the message type, 'string', or 'json'\n"
	       "\t-U <uid> -G <gid> -P <perm>\n"
	       "\t-f <file> -l -r <repeat_count> -i <usecs> -R\n"
	       "\t-W <connection_retry_wait_milliseconds>\n"
	       "\t-D <nsecs> The delay between message publish calls\n"
	       "\t-g <secs> wait timeout in seconds for QUOTA credits\n"
	       "\t-H this help message\n"
	       "\t-v verbose messages\n",
	       argv[0]);

out:
	free(lbuf);
	free(host);
	free(port);
	free(xprt);
	free(auth);
	free(filename);
	free(msg);
	av_free(auth_opt);
	exit(rc);
}
