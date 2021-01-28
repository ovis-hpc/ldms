#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <semaphore.h>
#include <ovis_json/ovis_json.h>
#include <execinfo.h> /* for backtrace_symbols() */
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
			       "stream %s\n", cc, stream_name);
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

struct req_buf {
	ldms_t x;
	int sz;
	int off;

	union {
		struct ldmsd_req_hdr_s req[0];
		char buff[0];
	};
};

static void req_buf_init(struct req_buf *b, ldms_t x, int sz)
{
	b->x = x;
	b->sz = sz;
	b->off = sizeof(*b->req);
	b->req->flags = LDMSD_REQ_SOM_F;
	b->req->marker = LDMSD_RECORD_MARKER;
}

static struct req_buf *req_buf_new(ldms_t x, int sz)
{
	struct req_buf *b = calloc(1, sizeof(*b) + sz);
	if (b)
		req_buf_init(b, x, sz);
	return b;
}

int req_buf_send(struct req_buf *b)
{
	int rc;
	b->req->rec_len = b->off;
	ldmsd_hton_req_hdr(b->req);
	rc = ldms_xprt_send(b->x, b->buff, b->off);
	ldmsd_ntoh_req_hdr(b->req);
	if (rc)
		return rc;
	b->off = sizeof(*b->req);
	b->req->flags &= ~LDMSD_REQ_SOM_F;
	return 0;
}

/* Fill the available space of the req with data. Send when the space
 * ran out, and repeat the filling/send. `off` is use to track the offset of the
 * available space. */
static int req_buf_append(struct req_buf *b, const void *data, int data_len, int flags)
{
	const char *d = data;
	int dlen = data_len;
	int cpsz, rc;
	while (dlen) {
		cpsz = b->sz - b->off;
		cpsz = dlen < cpsz ? dlen : cpsz;
		memcpy(&b->buff[b->off], d, cpsz);
		dlen -= cpsz;
		d += cpsz;
		b->off += cpsz;
		if (b->off == b->sz) {
			/* space full, send */
			if ((flags & LDMSD_REQ_EOM_F) && dlen == 0) {
				b->req->flags |= LDMSD_REQ_EOM_F;
				flags &= ~LDMSD_REQ_EOM_F;
				return req_buf_send(b);
			}
			rc = req_buf_send(b);
			if (rc)
				return rc;
		}
	}
	if (b->off > sizeof(*b->req) && (flags & LDMSD_REQ_EOM_F)) {
		/* Got some space left, but this is the last message */
		b->req->flags |= LDMSD_REQ_EOM_F;
		flags &= ~LDMSD_REQ_EOM_F;
		return req_buf_send(b);
	}
	return 0;
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
	struct ldmsd_req_attr_s a;
	int rc;
	int a_len;
	if (!data_len)
		return 0;

	size_t max_msg = ldms_xprt_msg_max(xprt);
	struct req_buf *b = req_buf_new(xprt, max_msg);
	if (!b) {
		msglog("Error allocating %d bytes of memory for buffer\n",
			max_msg);
		return ENOMEM;
	}

	/* stream_req ::= (NAME, STRING_DATA) or (NAME, JSON) */
	/* the msg plainly split into multiple records if it is too long to fit
	 * in a single ldms message. These records must have the same message
	 * number for the receiver (ldmsd) to identify the buffer for
	 * aggregating these records back into the request. */
	b->req->msg_no = __sync_fetch_and_add(&msg_no, 1);
	b->req->type = LDMSD_REQ_TYPE_CONFIG_CMD;
	b->req->req_id = LDMSD_STREAM_PUBLISH_REQ;

	/* NAME */
	a.discrim = htonl(1);
	a.attr_id = htonl(LDMSD_ATTR_NAME);
	a_len = strlen(stream_name) + 1;
	a.attr_len = htonl(a_len);
	rc = req_buf_append(b, &a, sizeof(a), 0);
	if (rc)
		return rc;
	rc = req_buf_append(b, stream_name, a_len, 0);
	if (rc)
		return rc;

	/* STRING or JSON */
	a.discrim = htonl(1);
	switch (stream_type) {
	case LDMSD_STREAM_STRING:
		a.attr_id = htonl(LDMSD_ATTR_STRING);
		break;
	case LDMSD_STREAM_JSON:
		a.attr_id = htonl(LDMSD_ATTR_JSON);
		break;
	default:
		return EINVAL;
	}
	a.attr_len = htonl(data_len);
	rc = req_buf_append(b, &a, sizeof(a), 0);
	if (rc)
		return rc;
	rc = req_buf_append(b, data, data_len, 0);
	if (rc)
		return rc;
	a.discrim = 0;
	a.attr_id = htonl(LDMSD_ATTR_TERM);
	a.attr_len = 0;
	rc = req_buf_append(b, &a, sizeof(a), LDMSD_REQ_EOM_F);
	if (rc)
		return rc;

	return 0;
}

sem_t conn_sem;
int conn_status = ENOTCONN;
static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
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
		/* no-op */
		return;
	default:
		ldms_xprt_put(x);
		conn_status = ECONNABORTED;
		msglog("Received invalid event type %d\n", e->type);
	}
	sem_post(&conn_sem);
}

#define ROUND(len) ((((len)-1)|0xFFF)+1)

char *fread_all(FILE *f, size_t *len_out)
{
	size_t off = 0;
	size_t len = 0x1000; /* 4K */
	char *buff = malloc(len);
	size_t rsz;

	if (!buff)
		return NULL;

	while ((rsz = fread(buff+off, 1, len - off, f))) {
		off += rsz;
		if (off == len) {
			/* expand */
			int new_len = ROUND(len + 0x1000);
			char *new_buff = realloc(buff, ROUND(len + 0x1000));
			if (!new_buff)
				goto err;
			buff = new_buff;
			len = new_len;
		}
	}

	if (!feof(f))
		goto err;
	/* add '\0' */
	if (off == len) {
		int new_len = ROUND(len + 0x1000);
		char *new_buff = realloc(buff, ROUND(len + 0x1000));
		if (!new_buff)
			goto err;
		buff = new_buff;
		len = new_len;
	}
	buff[off] = '\0';
	*len_out = off + 1;
	return buff;

 err:
	free(buff);
	return NULL;
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
#define LDMSD_STREAM_CONNECT_TIMEOUT 5 /* 5 seconds */
int ldmsd_stream_publish_file(const char *stream, const char *type,
			      const char *xprt, const char *host, const char *port,
			      const char *auth, struct attr_value_list *auth_opt,
			      FILE *file)
{
	int rc, stream_type;
	size_t data_len;
	static char buffer[1024*64];
	ldms_t x;
	char *timeout_s = getenv("LDMSD_STREAM_CONN_TIMEOUT");
	int timeout = LDMSD_STREAM_CONNECT_TIMEOUT;
	if (timeout_s)
		timeout = atoi(timeout_s);
	if (0 == strcasecmp("raw", type))
		stream_type = LDMSD_STREAM_STRING;
	else if (0 == strcasecmp("string", type))
		stream_type = LDMSD_STREAM_STRING;
	else if (0 == strcasecmp("json", type))
		stream_type = LDMSD_STREAM_JSON;
	else
		return EINVAL;
	x = ldms_xprt_new_with_auth(xprt, msglog, auth, auth_opt);
	if (!x) {
		msglog("Error %d creating the '%s' transport\n",
			errno, xprt);
		return errno;
	}

	sem_init(&conn_sem, 0, 0);

	rc = ldms_xprt_connect_by_name(x, host, port, event_cb, NULL);
	if (rc) {
		msglog("Error %d connecting to %s:%s\n",
			rc, host, port);
		return rc;
	}
	struct timespec ts;
	ts.tv_sec = time(NULL) + timeout;
	ts.tv_nsec = 0;
	if (sem_timedwait(&conn_sem, &ts)) {
		msglog("Timeout connecting to remote peer\n");
		return errno;
	}
	if (conn_status) {
		msglog("Error %d connecting to remote peer\n", conn_status);
		return conn_status;
	}

	if (stream_type == LDMSD_STREAM_JSON) {
		/* must send entire file at once */
		char *_buf = fread_all(file, &data_len);
		rc = ldmsd_stream_publish(x, stream, LDMSD_STREAM_JSON, _buf, data_len);
		free(_buf);
	} else {
		/* can send stream in chunks */
		while ((data_len = fread(buffer, 1, sizeof(buffer), file)) > 0) {
			rc = ldmsd_stream_publish(x, stream, LDMSD_STREAM_STRING,
					buffer, data_len);
			if (rc)
				goto err;
		}
	}

	ldms_xprt_close(x);
 err:
	return rc;

}

int ldmsd_stream_response(ldms_xprt_event_t e)
{
	struct ldms_reply_hdr *h = (void *)e->data;
	return ntohl(h->rc);
}

struct buf_s {
	size_t sz; /* size of buf */
	size_t pos; /* write position in buf */
	char *buf;
};

/* printf into buf->buf and expand buf->buf as necessary, returns errno on error */
__attribute__((format(printf, 2, 3)))
int buf_printf(struct buf_s *buf, const char *fmt, ...)
{
	size_t len, spc;
	size_t new_sz;
	char *new_buf;
	va_list ap;
 again:
	spc = buf->sz - buf->pos;
	va_start(ap, fmt);
	len = vsnprintf(buf->buf + buf->pos, spc, fmt, ap);
	va_end(ap);
	if (len >= spc) { /* need more space */
		new_sz = ((buf->sz + len)|0xFFF)+1;
		new_buf = realloc(buf->buf, new_sz);
		if (!new_buf)
			return errno;
		buf->sz = new_sz;
		buf->buf = new_buf;
		goto again;
	}
	buf->pos += len;
	return 0;
}

char * ldmsd_stream_client_dump()
{
	struct rbn *rbn;
	ldmsd_stream_t s;
	ldmsd_stream_client_t c;
	int rc;
	int first_stream = 1;
	int first_client;
	struct buf_s buf = {.sz = 4096};

	buf.buf = malloc(buf.sz);
	if (!buf.buf)
		goto err_0;
	rc = buf_printf(&buf, "{\"streams\":[" );
	if (rc)
		goto err_1;
	pthread_mutex_lock(&s_tree_lock);
	RBT_FOREACH(rbn, &s_tree) {
		/* for each stream */
		s = container_of(rbn, struct ldmsd_stream_s, s_ent);
		rc = buf_printf(&buf, "%s{\"name\":\"%s\",\"clients\":[",
				first_stream?"":",", s->s_name);
		if (rc)
			goto err_2;
		first_stream = 0;
		first_client = 1;
		pthread_mutex_lock(&s->s_lock);
		LIST_FOREACH(c, &s->s_c_list, c_ent) {
			/* for each client of the stream */
			void *p = c->c_cb_fn;
			char **sym = backtrace_symbols(&p, 1);
			char _pbuf[32];
			if (!sym) {
				sprintf(_pbuf, "%p", p);
			}
			rc = buf_printf(&buf, "%s{"
					"\"cb_fn\":\"%s\","
					"\"ctxt\":\"%p\""
					"}",
					first_client?"":",",
					sym?sym[0]:_pbuf,
					c->c_ctxt);
			if (rc)
				goto err_3;
			first_client = 0;

		}
		pthread_mutex_unlock(&s->s_lock);
		rc = buf_printf(&buf, "]}");
		if (rc)
			goto err_2;
	}
	pthread_mutex_unlock(&s_tree_lock);
	rc = buf_printf(&buf, "]}" );
	if (rc)
		goto err_1;
	return buf.buf;
 err_3:
	pthread_mutex_unlock(&s->s_lock);
 err_2:
	pthread_mutex_unlock(&s_tree_lock);
 err_1:
	free(buf.buf);
 err_0:
	return NULL;
}
