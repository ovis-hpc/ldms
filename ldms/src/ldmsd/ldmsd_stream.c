#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <semaphore.h>
#include <json/json_util.h>
#include "ldms.h"
#include "ldms_xprt.h"
#include "ldmsd_request.h"
#include "ldmsd_stream.h"

static void msglog(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}

static int s_cmp(void *tree_key, const void *key)
{
	return strcmp((char *)tree_key, (const char *)key);
}

struct ldmsd_stream_client_s {
	ldmsd_stream_recv_cb_t c_cb_fn;
	void *c_ctxt;
	ldmsd_stream_t c_s;
	LIST_ENTRY(ldmsd_stream_client_s) c_ent;
};

struct ldmsd_stream_s {
	const char *s_name;
	struct rbn s_ent;
	pthread_mutex_t s_lock;
	LIST_HEAD(ldmsd_client_list, ldmsd_stream_client_s) s_c_list;
};

static pthread_mutex_t s_tree_lock = PTHREAD_MUTEX_INITIALIZER;
struct rbt s_tree = RBT_INITIALIZER(s_cmp);

void ldmsd_stream_deliver(const char *stream_name, ldmsd_stream_type_t stream_type,
			  const char *data, size_t data_len,
			  json_entity_t entity)
{
	struct rbn *rbn;
	ldmsd_stream_t s;
	ldmsd_stream_client_t c;

	pthread_mutex_lock(&s_tree_lock);
	rbn = rbt_find(&s_tree, stream_name);
	pthread_mutex_unlock(&s_tree_lock);
	if (!rbn)
		return;
	s = container_of(rbn, struct ldmsd_stream_s, s_ent);
	pthread_mutex_lock(&s->s_lock);
	LIST_FOREACH(c, &s->s_c_list, c_ent) {
		c->c_cb_fn(c, c->c_ctxt, stream_type, data, data_len, entity);
	}
	pthread_mutex_unlock(&s->s_lock);
}

ldmsd_stream_client_t
ldmsd_stream_subscribe(const char *stream_name,
		       ldmsd_stream_recv_cb_t cb_fn, void *ctxt)
{
	ldmsd_stream_t s = NULL;
	struct rbn *rbn;
	ldmsd_stream_client_t cc, c = malloc(sizeof *c);
	if (!c)
		goto err_0;

	/* Find the stream */
	pthread_mutex_lock(&s_tree_lock);
	rbn = rbt_find(&s_tree, stream_name);
	pthread_mutex_unlock(&s_tree_lock);
	if (!rbn) {
		s = malloc(sizeof *s);
		if (!s)
			goto err_1;
		s->s_name = strdup(stream_name);
		if (!s->s_name) {
			goto err_2;
		}
		pthread_mutex_init(&s->s_lock, NULL);
		LIST_INIT(&s->s_c_list);
		rbn_init(&s->s_ent, (char *)s->s_name);
		rbt_ins(&s_tree, &s->s_ent);
	} else {
		s = container_of(rbn, struct ldmsd_stream_s, s_ent);
	}
	pthread_mutex_lock(&s->s_lock);
	LIST_FOREACH(cc, &s->s_c_list, c_ent) {
		if (cc->c_cb_fn == cb_fn && cc->c_ctxt == ctxt) {
			msglog("The client %p is already subscribed to "
			       "stream %s", cc, stream_name);
			errno = EEXIST;
			pthread_mutex_unlock(&s->s_lock);
			goto err_1;
		}
	}
	c->c_s = s;
	c->c_cb_fn = cb_fn;
	c->c_ctxt = ctxt;
	LIST_INSERT_HEAD(&s->s_c_list, c, c_ent);
 	pthread_mutex_unlock(&s->s_lock);
 	return c;
 err_2:
	free(s);
 err_1:
	free(c);
 err_0:
	return NULL;
}

const char *ldmsd_stream_name(ldmsd_stream_t s)
{
	return s->s_name;
}

const char *ldmsd_stream_client_name(ldmsd_stream_client_t c)
{
	return ldmsd_stream_name(c->c_s);
}

void ldmsd_stream_close(ldmsd_stream_client_t c)
{
	pthread_mutex_lock(&c->c_s->s_lock);
	LIST_REMOVE(c, c_ent);
	pthread_mutex_unlock(&c->c_s->s_lock);
}

/**
 * \brief Publish data to stream
 *
 * \param xprt The LDMS transport handle
 * \param stream_name The name of the stream to publish
 * \param attr_id The attribute id for the data (LDMS_ATTR_STRING, LDMS_ATTR_JSON, etc...)
 * \param data A pointer to the buffer to send
 * \param data_len The size of the data buffer in bytes
 *
 * \returns 0 on success, or an errno
 */
int ldmsd_stream_publish(ldms_t xprt,
			 const char *stream_name,
			 ldmsd_stream_type_t stream_type,
			 const char *data, size_t data_len)
{
	static uint64_t msg_no = 1;
	const char * data_ptr;
	ldmsd_req_attr_t first_attr, attr, next_attr;
	int rc;
	size_t this_rec;
	if (!data_len)
		return 0;

	size_t max_msg = ldms_xprt_msg_max(xprt);
	ldmsd_req_hdr_t req = malloc(max_msg);
	if (!req) {
		msglog("Error allocating %d bytes of memory for buffer\n",
			max_msg);
		return ENOMEM;
	}

	/* Put the instance name at the front of the request */
	attr = (ldmsd_req_attr_t)(req + 1);
	attr->discrim = 1;
	attr->attr_id = LDMSD_ATTR_NAME;
	attr->attr_len = strlen(stream_name) + 1;
	size_t meta =
		sizeof(*req) +
		sizeof(*attr) + attr->attr_len + /* plugin name */
		sizeof(*attr) +			 /* plugin data */
		sizeof(attr->discrim);		 /* terminator */
	strcpy((char *)attr->attr_value, stream_name);
	first_attr = ldmsd_next_attr(attr);
	ldmsd_hton_req_attr(attr);
	uint32_t flags = LDMSD_REQ_SOM_F;
	data_ptr = data;
	while (data_len > 0) {
		this_rec = meta + data_len;
		this_rec = this_rec < max_msg ? this_rec : max_msg;

		attr = first_attr;
		attr->discrim = 1;
		switch (stream_type) {
		case LDMSD_STREAM_STRING:
			attr->attr_id = LDMSD_ATTR_STRING;
			break;
		case LDMSD_STREAM_JSON:
			attr->attr_id = LDMSD_ATTR_JSON;
			break;
		}
		attr->attr_len = this_rec - meta;
		memcpy(attr->attr_value, data_ptr, attr->attr_len);
		data_ptr += attr->attr_len;
		data_len -= attr->attr_len;

		next_attr = ldmsd_next_attr(attr);
		ldmsd_hton_req_attr(attr);
		next_attr->discrim = 0;

		req->msg_no = __sync_fetch_and_add(&msg_no, 1);
		req->marker = LDMSD_RECORD_MARKER;
		req->type = LDMSD_REQ_TYPE_CONFIG_CMD;
		req->req_id = LDMSD_STREAM_PUBLISH_REQ;
		req->rec_len = this_rec;
		if (!data_len)
			/* No more data, turn on last message flag */
			flags |= LDMSD_REQ_EOM_F;
		req->flags = flags;
		ldmsd_hton_req_hdr(req);

		rc = ldms_xprt_send(xprt, (char *)req, this_rec);
		if (rc)
			goto err;
		/* Turn off start of message flag, for subsequent sends */
		flags = 0;
	}
 err:
	return 0;
}

static char recv_buf[128];
sem_t conn_sem;
int conn_status;
sem_t recv_sem;
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
	case LDMS_XPRT_EVENT_RECV:
		memcpy(recv_buf, e->data,
		       e->data_len < sizeof(recv_buf) ? e->data_len : sizeof(recv_buf));
		sem_post(&recv_sem);
		break;
	default:
		msglog("Received invalid event type %d\n", e->type);
	}
}

/**
 * \brief Publish file data to a stream.
 *
 * \param stream The name of the stream
 * \param type The stream data type { "raw", "string", or "json" }
 * \param xprt The LDMS transport name (.e.g "sock", "rdma", "ugni")
 * \param host The host name of the \c ldmsd
 * \param port The listening port at \c host
 * \param auth The authentication scheme (e.g. "munge", "ovis", "none")
 * \param auth_opt The authentication schema configuration options (may be NULL)
 * \param file The FILE* containing the JSon data
 *
 * \returns 0 on success, or an errno
 */
int ldmsd_stream_publish_file(const char *stream, const char *type,
			      const char *xprt, const char *host, const char *port,
			      const char *auth, struct attr_value_list *auth_opt,
			      FILE *file)
{
	char *data_ptr;
	ldmsd_req_attr_t first_attr, attr, next_attr;
	int rc, attr_id;
	size_t this_rec, data_len;
	static char buffer[1024*64];
	ldms_t x;

	if (0 == strcasecmp("raw", type))
		attr_id = LDMSD_ATTR_STRING;
	else if (0 == strcasecmp("string", type))
		attr_id = LDMSD_ATTR_STRING;
	else if (0 == strcasecmp("json", type))
		attr_id = LDMSD_ATTR_JSON;
	else
		return EINVAL;
	x = ldms_xprt_new_with_auth(xprt, msglog, auth, auth_opt);
	if (!x) {
		msglog("Error %d creating the '%s' transport\n",
			errno, xprt);
		return errno;
	}

	sem_init(&recv_sem, 0, 0);
	sem_init(&conn_sem, 0, 0);

	rc = ldms_xprt_connect_by_name(x, host, port, event_cb, NULL);
	if (rc) {
		msglog("Error %d connecting to %s:%s\n",
			rc, host, port);
		return rc;
	}
	struct timespec ts;
	ts.tv_sec = time(NULL) + 2;
	ts.tv_nsec = 0;
	sem_timedwait(&conn_sem, &ts);
	if (conn_status)
		return conn_status;
	size_t max_msg = ldms_xprt_msg_max(x);
	ldmsd_req_hdr_t req = malloc(max_msg);
	if (!req) {
		msglog("Error allocating %d bytes of memory for buffer\n",
			max_msg);
		return ENOMEM;
	}

	/* Put the instance name at the front of the request */
	attr = (ldmsd_req_attr_t)(req + 1);
	attr->discrim = 1;
	attr->attr_id = LDMSD_ATTR_NAME;
	attr->attr_len = strlen(stream) + 1;
	size_t meta =
		sizeof(*req) +
		sizeof(*attr) + attr->attr_len + /* plugin name */
		sizeof(*attr) +			 /* plugin data */
		sizeof(attr->discrim);		 /* terminator */
	strcpy((char *)attr->attr_value, stream);
	first_attr = ldmsd_next_attr(attr);
	ldmsd_hton_req_attr(attr);

	uint32_t flags = LDMSD_REQ_SOM_F;
	while ((data_len = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		if (conn_status)
			return conn_status;

		data_ptr = buffer;
		while (data_len) {
			this_rec = meta + data_len;
			this_rec = this_rec < max_msg ? this_rec : max_msg;

			attr = first_attr;
			attr->discrim = 1;
			attr->attr_id = attr_id;
			attr->attr_len = this_rec - meta;
			memcpy(attr->attr_value, data_ptr, attr->attr_len);
			data_ptr += attr->attr_len;
			data_len -= attr->attr_len;

			next_attr = ldmsd_next_attr(attr);
			ldmsd_hton_req_attr(attr);
			next_attr->discrim = 0;

			req->marker = LDMSD_RECORD_MARKER;
			req->type = LDMSD_REQ_TYPE_CONFIG_CMD;
			req->req_id = LDMSD_STREAM_PUBLISH_REQ;
			req->rec_len = this_rec;
			if (!data_len)
				/* No more data, turn on last message flag */
				flags |= LDMSD_REQ_EOM_F;
			req->flags = flags;
			ldmsd_hton_req_hdr(req);

			rc = ldms_xprt_send(x, (char *)req, this_rec);
			if (rc)
				goto err;
			/* Turn off start of message flag, for subsequent sends */
			flags = 0;
		}
	}

	/* Wait for the reply */
	ts.tv_sec = time(NULL) + 10;
	ts.tv_nsec = 0;
	sem_timedwait(&recv_sem, &ts);
	ldmsd_req_hdr_t reply = (ldmsd_req_hdr_t)recv_buf;
	rc = ntohl(reply->rsp_err);
	ldms_xprt_close(x);
 err:
	return rc;

}

int ldmsd_stream_response(ldms_xprt_event_t e)
{
	struct ldms_reply_hdr *h = (void *)e->data;
	return ntohl(h->rc);
}
