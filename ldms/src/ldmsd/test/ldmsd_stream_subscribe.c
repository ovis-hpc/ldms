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
#include <semaphore.h>
#include <pthread.h>
#include <netdb.h>
#include <ovis_json/ovis_json.h>
#include <assert.h>
#include <coll/rbt.h>
#include "ldms.h"
#include "../ldmsd_request.h"
#include "../ldmsd_stream.h"

static ldms_t ldms;
static sem_t recv_sem;
static FILE *file;
static int quiet;
static int daemon_io;
static int daemon_noroot;
static int events_raw;

void msglog(const char *fmt, ...)
{
	if (quiet)
		return;
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
	{"daemon-io",no_argument,       0,  'i' },
	{"daemon-noroot",no_argument,   0,  'R' },
	{"events-raw",no_argument,       0,  'E' },
	{"host",     required_argument, 0,  'h' },
	{"quiet",	no_argument,		0,	'q' },
	{0,          0,                 0,  0 }
};

void usage(int argc, char **argv)
{
	printf("usage: %s -x <xprt> -p <port> -h <host> "
	       "-s <stream-name> "
	       "-f <file> -a <auth> -A <auth-opt> "
	       "-D -i -R -E\n",
	       argv[0]);
	exit(1);
}

static const char *short_opts = "p:f:s:x:a:A:DiRqh:E";

#define AUTH_OPT_MAX 128

struct xprt_ctxt {
	struct rbn rbn;
	struct ldmsd_msg_buf *buf;
};
#define XPRT_CTXT_LEN_GRAIN 0xFFF
#define XPRT_CTXT_LEN_ROUND(L) ((((L)-1)|XPRT_CTXT_LEN_GRAIN) + 1)
#define XPRT_CTXT_INIT_LEN (1024*1024)

int xprt_ctxt_cmp(void *tree_key, const void *key)
{
	/* simply compare pointers to ldms xprts */
	return (int64_t)tree_key - (int64_t)key;
}

pthread_mutex_t xprt_ctxt_mutex = PTHREAD_MUTEX_INITIALIZER;
struct rbt xprt_ctxt_rbt = RBT_INITIALIZER(xprt_ctxt_cmp);

struct xprt_ctxt *xprt_ctxt_new(ldms_t ldms)
{
	struct xprt_ctxt *ctxt;
	pthread_mutex_lock(&xprt_ctxt_mutex);
	ctxt = (struct xprt_ctxt *)rbt_find(&xprt_ctxt_rbt, ldms);
	if (ctxt) {
		ctxt = NULL;
		errno = EEXIST;
		goto out;
	}
	ctxt = calloc(1, sizeof(*ctxt));
	if (!ctxt)
		goto out;
	ctxt->buf = ldmsd_msg_buf_new(XPRT_CTXT_INIT_LEN);
	if (!ctxt->buf) {
		free(ctxt);
		goto out;
	}
	rbn_init(&ctxt->rbn, ldms);
	rbt_ins(&xprt_ctxt_rbt, &ctxt->rbn);
 out:
	pthread_mutex_unlock(&xprt_ctxt_mutex);
	return ctxt;
}

struct xprt_ctxt *xprt_ctxt_find(ldms_t ldms)
{
	struct xprt_ctxt *ctxt;
	pthread_mutex_lock(&xprt_ctxt_mutex);
	ctxt = (struct xprt_ctxt *)rbt_find(&xprt_ctxt_rbt, ldms);
	pthread_mutex_unlock(&xprt_ctxt_mutex);
	return ctxt;
}

void xprt_ctxt_free(struct xprt_ctxt *ctxt)
{
	pthread_mutex_lock(&xprt_ctxt_mutex);
	rbt_del(&xprt_ctxt_rbt, &ctxt->rbn);
	pthread_mutex_unlock(&xprt_ctxt_mutex);
	ldmsd_msg_buf_free(ctxt->buf);
	free(ctxt);
}

static int stream_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity)
{
	if (!events_raw) {
		if (stream_type == LDMSD_STREAM_STRING)
			msglog("EVENT:{\"type\":\"string\",\"size\":%d,\"event\":", msg_len);
		else
			msglog("EVENT:{\"type\":\"json\",\"size\":%d,\"event\":", msg_len);
	}
	msglog(msg);
	if (!events_raw)
		msglog("}");
	msglog("\n");
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
				     (char *)attr->attr_value, attr->attr_len, NULL, NULL);
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
			     (char *)attr->attr_value, attr->attr_len, entity, NULL);
	free(stream_name);
	json_entity_free(entity);
	return 0;
}

static int __send(void *xprt, char *data, size_t len)
{
	ldms_t x = (ldms_t)xprt;
	return ldms_xprt_send(x, data, len);
}

static int send_ack(ldms_t x, ldmsd_req_hdr_t req)
{
	struct ldmsd_msg_buf *buf;
	struct ldmsd_req_attr_s a;
	char *ack_s = "ACK";
	int rc;

	buf = ldmsd_msg_buf_new(ldms_xprt_msg_max(x));
	if (!buf) {
		msglog("Out of memory\n");
		return ENOMEM;
	}

	a.discrim = 1;
	a.attr_id = LDMSD_ATTR_STRING;
	a.attr_len = strlen(ack_s) + 1;
	ldmsd_hton_req_attr(&a);
	rc = ldmsd_msg_buf_send(buf, x, req->msg_no, __send, LDMSD_REQ_SOM_F,
			LDMSD_REQ_TYPE_CONFIG_RESP, 0, (char *)&a, sizeof(a));
	if (rc)
		return rc;
	rc = ldmsd_msg_buf_send(buf, x, req->msg_no, __send, 0,
			LDMSD_REQ_TYPE_CONFIG_RESP, 0, ack_s, strlen(ack_s) + 1);
	if (rc)
		return rc;

	/* Terminating */
	a.discrim = 0;
	rc = ldmsd_msg_buf_send(buf, x, req->msg_no, __send, LDMSD_REQ_EOM_F,
				LDMSD_REQ_TYPE_CONFIG_RESP, 0,
				(char *)&a.discrim, sizeof(a.discrim));
	return rc;
}

int process_request(ldms_t x, ldmsd_req_hdr_t request)
{
	uint32_t req_id;

	if (ntohl(request->marker) != LDMSD_RECORD_MARKER) {
		msglog("Config request is missing record marker");
		exit(3);
	}
	req_id = ntohl(request->req_id);
	if (req_id != LDMSD_STREAM_PUBLISH_REQ) {
		msglog("Unexpected request id %d\n", req_id);
		exit(4);
	}

	struct xprt_ctxt *ctxt = xprt_ctxt_find(x);
	if (!ctxt) {
		msglog("Cannot find ctxt\n");
		exit(5);
	}

	int rc = ldmsd_msg_gather(ctxt->buf, request);
	if (EBUSY == rc)
		return 0;
	if (rc) {
		msglog("ERROR: Failed to receive messages: %d\n", rc);
		return rc;
	}

	/* we got all request data */
	ldmsd_req_hdr_t req = (ldmsd_req_hdr_t)ctxt->buf->buf;
	ldmsd_ntoh_req_msg(req);

	rc = send_ack(x, req);
	if (rc) {
		msglog("ERROR: Failed to send the acknowledgment: %d\n", rc);
		/* Continue to process the stream */
	}

	rc = stream_publish_handler(req);

	/* request processed, reset data buffer */
	ldmsd_msg_buf_init(ctxt->buf);
	return 0;
}

static void recv_msg(ldms_t x, char *data, size_t data_len)
{
	ldmsd_req_hdr_t request = (ldmsd_req_hdr_t)data;

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
	struct xprt_ctxt *ctxt;
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		ctxt = xprt_ctxt_new(x);
		if (!ctxt) {
			msglog("xprt_ctxt_new() failed, errno: %d\n", errno);
			ldms_xprt_close(ldms);
		}
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
	case LDMS_XPRT_EVENT_REJECTED:
	case LDMS_XPRT_EVENT_ERROR:
		ctxt = xprt_ctxt_find(x);
		if (ctxt) {
			xprt_ctxt_free(ctxt);
		}
		ldms_xprt_put(x);
		break;
	case LDMS_XPRT_EVENT_RECV:
		recv_msg(x, e->data, e->data_len);
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		/* Ignore */
		break;
	default:
		break;
	}
}

static int setup_connection(char *xprt, char *host, char *port, char *auth)
{
	int rc;
	struct addrinfo *ai = NULL, hint;
	memset(&hint, 0, sizeof(hint));
	hint.ai_flags = AI_PASSIVE;
	hint.ai_family = AF_INET;

	rc = getaddrinfo(host, port, &hint, &ai);
	if (rc)
		return errno;

	ldms = ldms_xprt_new_with_auth(xprt, msglog, auth, NULL);
	if (!ldms) {
		msglog("Error %d creating the '%s' transport\n", errno, xprt);
		rc = errno;
		goto out;
	}

	sem_init(&recv_sem, 1, 0);

	rc = ldms_xprt_listen(ldms, ai->ai_addr, ai->ai_addrlen, event_cb, NULL);
	if (rc)
		msglog("Error %d listening on the '%s' transport.\n", rc, xprt);
 out:
	freeaddrinfo(ai);
	return rc;
}

int main(int argc, char **argv)
{
	char *xprt = "sock";
	char *filename = NULL;
	char *stream = NULL;
	char *host = NULL;
	char *port = NULL;
	int opt, opt_idx;
	char *lval, *rval;
	char *auth = "none";
	struct attr_value_list *auth_opt = NULL;
	const int auth_opt_max = AUTH_OPT_MAX;
	int daemonize = 0;

	auth_opt = av_new(auth_opt_max);
	if (!auth_opt) {
		perror("could not allocate auth options");
		exit(1);
	}

	while ((opt = getopt_long(argc, argv, short_opts, long_opts, &opt_idx)) > 0) {
		switch (opt) {
		case 'q':
			quiet = 1;
			break;
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
		case 'i':
			daemon_io = 1;
			break;
		case 'R':
			daemon_noroot = 1;
			break;
		case 'E':
			events_raw = 1;
			break;
		case 'h':
			host = optarg;
			break;
		default:
			usage(argc, argv);
		}
	}
	if (!port || !stream)
		usage(argc, argv);

	if (daemonize) {
		uid_t u = geteuid();
		if (u || daemon_noroot) {
			if (daemon(1, daemon_io)) {
				perror("ldmsd_stream_subscribe(pwd): ");
				return 2;
			}
		} else {
			if (daemon(0, daemon_io)) {
				perror("ldmsd_stream_subscribe(/): ");
				return 2;
			}
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

	int rc = setup_connection(xprt, host, port, auth);
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
