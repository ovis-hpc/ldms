#ifndef _KVD_SVC_H_
#define _KVD_SVC_H_

#include <limits.h>

#include <coll/htbl.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/keyvalq_struct.h>

#define KVD_KVC_ERR_LEN 1024
struct kvd_req_ctxt {
	struct evhttp_request *req;
	const struct evhttp_uri *uri;
	const char *query;
	struct evkeyvalq *params;
	int httprc;
	struct evbuffer *evbuffer;
	struct evkeyvalq *hdr;
	char errstr[KVD_KVC_ERR_LEN];
};

typedef void (*kvd_svc_fn_t)(struct kvd_req_ctxt *ctxt);

typedef struct kvd_svc_handler_s {
	kvd_svc_fn_t svc_fn;
	struct hent hent;
} *kvd_svc_handler_t;

typedef struct kvd_svc_s {
	char path_buff[PATH_MAX];
	char *log_path;
	char *port;
	char *address;
	char *verbosity;

	struct event_base *evbase;
	struct evhttp *evhttp;
	struct evhttp_bound_socket *evhttp_socket; /* passive */
	struct evhttp_connection *evhttp_conn;	   /* active */

	htbl_t dispatch_table;
} *kvd_svc_t;

void kvd_set_svc_handler(kvd_svc_t svc, const char *uri, kvd_svc_fn_t fn);
void kvd_svc_init(kvd_svc_t svc, int foreground);
void kvd_cli_init(kvd_svc_t svc, int foreground);
int kvd_cli_get(kvd_svc_t svc, char *kind, char *uri);
int kvd_cli_post(kvd_svc_t svc, char *uri, char *data, size_t data_len);
#endif
