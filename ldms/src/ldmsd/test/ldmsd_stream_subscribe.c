#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <getopt.h>
#include <semaphore.h>
#include <pthread.h>
#include <json/json_util.h>
#include <assert.h>
#include "ldms.h"
#include "../ldmsd_request.h"
#include "../ldmsd_stream.h"

static ldms_t ldms;
static sem_t recv_sem;
FILE *file;

void msglog(const char *fmt, ...)
{
	va_list ap;
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&mutex);
	va_start(ap, fmt);
	vfprintf(file, fmt, ap);
	fflush(file);
	pthread_mutex_unlock(&mutex);
}

static struct option long_opts[] = {
	{"port",     required_argument, 0,  'p' },
	{"file",     required_argument, 0,  'f' },
	{"stream",   required_argument, 0,  's' },
	{"xprt",     required_argument, 0,  'x' },
	{"auth",     required_argument, 0,  'a' },
	{"auth_arg", required_argument, 0,  'A' },
	{"daemonize",no_argument,       0,  'D' },
	{0,          0,                 0,  0 }
};

void usage(int argc, char **argv)
{
	printf("usage: %s -x <xprt> -p <port> "
	       "-s <stream-name> "
	       "-f <file> -a <auth> -A <auth-opt> "
	       "-D\n",
	       argv[0]);
	exit(1);
}

static const char *short_opts = "p:f:s:x:a:A:D";

#define AUTH_OPT_MAX 128

static int stream_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity)
{
	if (stream_type == LDMSD_STREAM_STRING)
		msglog("EVENT:{\"type\":\"string\",\"size\":%d,\"event\":", msg_len);
	else
		msglog("EVENT:{\"type\":\"json\",\"size\":%d,\"event\":", msg_len);
	msglog(msg);
	msglog("}");
	return 0;
}

static int stream_publish_handler(ldmsd_req_hdr_t req)
{
	char *stream_name;
	ldmsd_req_attr_t attr;
	json_parser_t parser;
	json_entity_t entity = NULL;

	attr = ldmsd_first_attr(req);
	while (attr->discrim) {
		if (attr->attr_id == LDMSD_ATTR_NAME)
			break;
		attr = ldmsd_next_attr(attr);
	}
	if (!attr->attr_value) {
		msglog("The stream name is missing, malformed stream request.\n");
		exit(5);
	}
	stream_name = strdup((char *)attr->attr_value);
	if (!stream_name) {
		printf("ERROR: out of memory\n");
		exit(1);
	}

	attr = ldmsd_first_attr(req);
	while (attr->discrim) {
		if (attr->attr_id == LDMSD_ATTR_STRING)
			break;
		attr = ldmsd_next_attr(attr);
	}
	if (attr->discrim) {
		ldmsd_stream_deliver(stream_name, LDMSD_STREAM_STRING,
				     (char *)attr->attr_value, attr->attr_len, NULL);
		free(stream_name);
		return 0;
	}

	attr = ldmsd_first_attr(req);
	while (attr->discrim) {
		if (attr->attr_id == LDMSD_ATTR_JSON)
			break;
		attr = ldmsd_next_attr(attr);
	}
	if (!attr->discrim) {
		msglog("The stream payload is missing, malformed stream request.\n");
		exit(6);
	}

	parser = json_parser_new(0);
	if (!parser) {
		msglog("Error creating JSon parser.\n");
		exit(7);
	}
	int rc = json_parse_buffer(parser,
				   (char *)attr->attr_value, attr->attr_len,
				   &entity);
	json_parser_free(parser);
	if (rc) {
		msglog("Syntax error parsing JSon payload.\n");
		msglog("%s\n", attr->attr_value);
		exit(8);
	}
	ldmsd_stream_deliver(stream_name, LDMSD_STREAM_JSON,
			     (char *)attr->attr_value, attr->attr_len, entity);
	free(stream_name);
	json_entity_free(entity);
	return 0;
}

int process_request(ldms_t x, ldmsd_req_hdr_t request)
{
	uint32_t req_id;

	ldmsd_ntoh_req_msg(request);

	if (request->marker != LDMSD_RECORD_MARKER) {
		msglog("Config request is missing record marker");
		exit(3);
	}
	req_id = request->req_id;
	if (req_id != LDMSD_STREAM_PUBLISH_REQ) {
		msglog("Unexpected request id %d\n", req_id);
		exit(4);
	}

	int rc = stream_publish_handler(request);

	request->flags = LDMSD_REQ_SOM_F | LDMSD_REQ_EOM_F;
	request->rsp_err = rc;
	request->rec_len = sizeof(*request);
	ldmsd_hton_req_hdr(request);
	return ldms_xprt_send(x, (char *)request, sizeof(*request));
}

static void recv_msg(ldms_t x, char *data, size_t data_len)
{
	ldmsd_req_hdr_t request = (ldmsd_req_hdr_t)data;

	if (ntohl(request->rec_len) > ldms_xprt_msg_max(x)) {
		msglog("Test command does not support multi-record stream data");
		exit(1);
	}

	switch (ntohl(request->type)) {
	case LDMSD_REQ_TYPE_CONFIG_CMD:
		(void)process_request(x, request);
		break;
	case LDMSD_REQ_TYPE_CONFIG_RESP:
	default:
		msglog("Unexpected request type %d in stream data", ntohl(request->type));
		exit(2);
	}
}

static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_ERROR:
		ldms_xprt_put(x);
		break;
	case LDMS_XPRT_EVENT_RECV:
		recv_msg(x, e->data, e->data_len);
		break;
	default:
		break;
	}
}

static int setup_connection(char *xprt, short port_no, char *auth)
{
	struct sockaddr_in sin;
	int rc;

	ldms = ldms_xprt_new_with_auth(xprt, msglog, auth, NULL);
	if (!ldms) {
		msglog("Error %d creating the '%s' transport\n", errno, xprt);
		return errno;
	}

	sem_init(&recv_sem, 1, 0);

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = 0;
	sin.sin_port = htons(port_no);
	rc = ldms_xprt_listen(ldms, (struct sockaddr *)&sin, sizeof(sin), event_cb, NULL);
	if (rc)
		msglog("Error %d listening on the '%s' transport.\n", rc, xprt);
	return rc;
}

int main(int argc, char **argv)
{
	char *xprt = "sock";
	char *filename = NULL;
	char *stream = NULL;
	int opt, opt_idx;
	char *lval, *rval;
	char *auth = "none";
	struct attr_value_list *auth_opt = NULL;
	const int auth_opt_max = AUTH_OPT_MAX;
	short port_no = 0;
	int daemonize = 0;

	auth_opt = av_new(auth_opt_max);
	if (!auth_opt) {
		perror("could not allocate auth options");
		exit(1);
	}

	while ((opt = getopt_long(argc, argv, short_opts, long_opts, &opt_idx)) > 0) {
		switch (opt) {
		case 'p':
			port_no = atoi(optarg);
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
		case 'D':
			daemonize = 1;
			break;
		default:
			usage(argc, argv);
		}
	}
	if (!port_no || !stream)
		usage(argc, argv);

	if (daemonize) {
		if (daemon(0, 0)) {
			perror("ldmsd_stream_subscribe: ");
			return 2;
		}
	}

	if (filename) {
		file = fopen(filename, "w");
		if (!file) {
			perror("The file could not be opened.");
			exit(1);
		}
	} else {
		file = stdout;
	}

	int rc = setup_connection(xprt, port_no, auth);
	if (rc) {
		errno = rc;
		perror("Could not listen");
	}
	ldmsd_stream_client_t client = ldmsd_stream_subscribe(stream, stream_recv_cb, NULL);
	if (!client)
		return 1;

	while (0 == sleep(10)) {
		/* wait for signal or kill */
	}
	return 0;
}
