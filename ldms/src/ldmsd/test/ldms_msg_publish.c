#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include "ldms.h"

static struct option long_opts[] = {
	{"host",     required_argument, 0,  'h' },
	{"port",     required_argument, 0,  'p' },
	{"file",     required_argument, 0,  'f' },
	{"msg_name", required_argument, 0,  'm' },
	{"type",     required_argument, 0,  't' },
	{"xprt",     required_argument, 0,  'x' },
	{"auth",     required_argument, 0,  'a' },
	{"auth_opt", required_argument, 0,  'A' },
	{"uid",      required_argument, 0,  'U' },
	{"gid",      required_argument, 0,  'G' },
	{"perm",     required_argument, 0,  'P' },
	{0,          0,                 0,  0 }
};

void usage(int argc, char **argv) __attribute__((noreturn));
void usage(int argc, char **argv)
{
	printf("usage: %s -x <xprt> -h <host> -p <port> "
	       "-m <msg-name> -t <msg-type> "
	       "-f <file> -a <auth> -A <auth-opt>"
	       "\n",
	       argv[0]);
	exit(1);
}

static const char *short_opts = "h:p:f:m:t:x:a:A:U:G:P:";

#define AUTH_OPT_MAX 128

int main(int argc, char **argv)
{
	char *host = NULL;
	char *port = NULL;
	char *xprt = "sock";
	char *filename = NULL;
	char *msg = NULL;
	int opt, opt_idx;
	char *lval, *rval;
	char *auth = "none";
	struct attr_value_list *auth_opt = NULL;
	const int auth_opt_max = AUTH_OPT_MAX;
	FILE *file;
	ldms_msg_type_t typ = LDMS_MSG_STRING;
	uid_t uid = geteuid();
	gid_t gid = getegid();
	mode_t perm = 0777;
	struct ldms_cred cred;

	ldms_init(16*1024*1024);
	auth_opt = av_new(auth_opt_max);
	if (!auth_opt) {
		perror("could not allocate auth options");
		exit(1);
	}

	while ((opt=getopt_long(argc, argv, short_opts, long_opts, &opt_idx)) > 0) {
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
		case 't':
			if (0 == strcasecmp("json", optarg)) {
				typ = LDMS_MSG_JSON;
			} else if (0 == strcasecmp("string", optarg)) {
				typ = LDMS_MSG_STRING;
			} else if (0 == strcasecmp("avro_ser", optarg)) {
				typ = LDMS_MSG_AVRO_SER;
			} else {
				printf("The type argument must be 'json', 'string', or 'avro_ser'\n");
				usage(argc, argv);
			}
			break;
		case 'U':
			uid = strtoul(optarg, NULL, 0);
			break;
		case 'G':
			gid = strtoul(optarg, NULL, 0);
			break;
		case 'P':
			perm = strtoul(optarg, NULL, 8);
			break;
		}
	}
	if (!host || !port || !msg)
		usage(argc, argv);

	if (filename) {
		file = fopen(filename, "r");
		if (!file) {
			perror(filename);
			exit(1);
		}
	} else {
		file = stdin;
	}

	int rc;
	ldms_t ldms = NULL;

	/* Create a transport endpoint */
	ldms = ldms_xprt_new_with_auth(xprt, auth, auth_opt);
	if (!ldms) {
		rc = errno;
		printf("Failed to create the LDMS transport endpoint.\n");
		return rc;
	}
	rc = ldms_xprt_connect_by_name(ldms, host, port, NULL, NULL);
	if (rc) {
		printf("Error %d connecting to peer\n", rc);
		return rc;
	}

	cred.uid = uid;
	cred.gid = gid;

	rc = ldms_msg_publish_file(ldms, msg, typ, &cred, perm, file);
	if (rc)
		printf("Error %d creating ldms_msg and notifying client\n", rc);
	ldms_xprt_close(ldms);
	return rc;
}
