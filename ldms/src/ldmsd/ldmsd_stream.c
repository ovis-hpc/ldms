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

struct ldmsd_stream_info_s {
	time_t first_ts; /* Timestamp of the first message */
	time_t last_ts; /* Timestamp of the last message */
	int count; /* The number of received messages */
	size_t total_bytes; /* Total data size of all received messages */
};

typedef struct ldmsd_stream_publisher_s {
	const char *p_name; /* Publisher name */
	ldmsd_stream_type_t p_type;
	struct ldmsd_stream_info_s p_info;
	struct rbn p_ent;

} *ldmsd_stream_publisher_t;

typedef struct ldmsd_stream_s *ldmsd_stream_t;
struct ldmsd_stream_client_s {
	ldmsd_stream_recv_cb_t c_cb_fn;
	void *c_ctxt;
	ldmsd_stream_t c_s;
	int c_flags;
	LIST_ENTRY(ldmsd_stream_client_s) c_ent;
};

static int p_cmp(void *tree_key, const void *key)
{
	return strcmp((char *)tree_key, (const char *)key);
}

struct ldmsd_stream_s {
	const char *s_name;
	struct ldmsd_stream_info_s s_info;
	struct rbn s_ent;
	pthread_mutex_t s_lock;
	LIST_HEAD(ldmsd_client_list, ldmsd_stream_client_s) s_c_list;
	struct rbt s_p_tree;
};

static pthread_mutex_t s_tree_lock = PTHREAD_MUTEX_INITIALIZER;
struct rbt s_tree = RBT_INITIALIZER(s_cmp);

static ldmsd_stream_t __find_stream(const char *stream_name)
{
	struct rbn *rbn;
	ldmsd_stream_t s = NULL;
	pthread_mutex_lock(&s_tree_lock);
	rbn = rbt_find(&s_tree, stream_name);
	if (rbn) {
		s = container_of(rbn, struct ldmsd_stream_s, s_ent);
		pthread_mutex_lock(&s->s_lock);
	}
	pthread_mutex_unlock(&s_tree_lock);
	return s;
}

static struct ldmsd_stream_s *__new_stream(const char *name)
{
	struct ldmsd_stream_s *s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	s->s_name = strdup(name);
	if (!s->s_name)
		goto del_stream;
	pthread_mutex_init(&s->s_lock, NULL);
	LIST_INIT(&s->s_c_list);
	rbt_init(&s->s_p_tree, p_cmp);
	rbn_init(&s->s_ent, (char *)s->s_name);
	pthread_mutex_lock(&s_tree_lock);
	rbt_ins(&s_tree, &s->s_ent);
	pthread_mutex_unlock(&s_tree_lock);
	return s;

 del_stream:
	free(s);
	return NULL;
}

static void __free_publisher(struct ldmsd_stream_publisher_s *p)
{
	free((char*)p->p_name);
	free(p);
}

static struct ldmsd_stream_publisher_s *__new_publisher(const char *name)
{
	struct ldmsd_stream_publisher_s *p;
	p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;

	p->p_name = strdup(name);
	if (!p->p_name)
		goto err;
	rbn_init(&p->p_ent, (char*)name);
	return p;
err:
	__free_publisher(p);
	return NULL;
}

/* The caller must hold the stream lock. */
static struct ldmsd_stream_publisher_s *
__find_publisher(ldmsd_stream_t s, const char *name)
{
	struct rbn *rbn = rbt_find(&s->s_p_tree, name);
	if (!rbn)
		return NULL;
	return container_of(rbn, struct ldmsd_stream_publisher_s, p_ent);
}

struct stream_ctxt {
	ldms_t x;
	int is_done;
};

static int __stream_send(void *xprt, char *data, size_t data_len)
{
	struct stream_ctxt *ctxt = (struct stream_ctxt *)xprt;
	return ldms_xprt_send(ctxt->x, data, data_len);
}

int __stream_new_send(ldms_t xprt, ldmsd_stream_t s)
{
	int rc;
	size_t len;
	struct ldmsd_msg_buf *b;
	uint32_t msg_no = ldmsd_msg_no_get();
	struct stream_ctxt ctxt = { .x = xprt };
	struct ldmsd_req_attr_s a;

	b = ldmsd_msg_buf_new(ldms_xprt_msg_max(xprt));
	if (!b)
		return ENOMEM;

	len = strlen(s->s_name) + 1;
	a.discrim = 1;
	a.attr_id = LDMSD_ATTR_NAME;
	a.attr_len = len;
	ldmsd_hton_req_attr(&a);
	rc  = ldmsd_msg_buf_send(b, &ctxt, msg_no,
				__stream_send, LDMSD_REQ_SOM_F,
				LDMSD_REQ_TYPE_CONFIG_CMD,
				LDMSD_STREAM_NEW_REQ,
				(char *)(&a), sizeof(a));
	if (rc)
		goto cleanup;

	rc = ldmsd_msg_buf_send(b, &ctxt, msg_no,
				__stream_send, 0,
				LDMSD_REQ_TYPE_CONFIG_CMD,
				LDMSD_STREAM_NEW_REQ,
				s->s_name, len);
	if (rc)
		goto cleanup;

	a.discrim = 0;
	rc = ldmsd_msg_buf_send(b, &ctxt, msg_no,
				__stream_send, LDMSD_REQ_EOM_F,
				LDMSD_REQ_TYPE_CONFIG_CMD,
				LDMSD_STREAM_NEW_REQ,
				(char*)&(a.discrim), sizeof(a.discrim));
	if (rc)
		goto cleanup;
 cleanup:
	ldmsd_msg_buf_free(b);
	return rc;
}


int ldmsd_stream_new(const char *name)
{
	struct ldmsd_stream_s *s;
	s = __find_stream(name);
	if (s) {
		pthread_mutex_unlock(&s->s_lock);
		return EEXIST;
	}

	s = __new_stream(name);
	if (!s)
		return ENOMEM;
	return 0;
}

int ldmsd_stream_new_publish(const char *name, ldms_t xprt)
{
	int rc = 0;
	struct ldmsd_stream_s *s;

	if (!xprt)
		return EINVAL;

	s = __find_stream(name);
	if (!s) {
		s = __new_stream(name);
		if (!s)
			return ENOMEM;
	}
	rc = __stream_new_send(xprt, s);
	pthread_mutex_unlock(&s->s_lock);
	return rc;
}

void ldmsd_stream_publisher_remove(const char *name)
{
	ldmsd_stream_t s;
	struct rbn *rbn;
	ldmsd_stream_publisher_t p;
	pthread_mutex_lock(&s_tree_lock);
	RBT_FOREACH(rbn, &s_tree) {
		s = container_of(rbn, struct ldmsd_stream_s, s_ent);
		pthread_mutex_lock(&s->s_lock);
		p = __find_publisher(s, name);
		if (p) {
			rbt_del(&s->s_p_tree, &p->p_ent);
			__free_publisher(p);
		}
		pthread_mutex_unlock(&s->s_lock);
	}
	pthread_mutex_unlock(&s_tree_lock);
}

int ldmsd_stream_subscriber_count(const char *stream_name)
{
	int subscriber_count = 0;
	ldmsd_stream_client_t c;
	ldmsd_stream_t s = __find_stream(stream_name);
	if (s) {
		LIST_FOREACH(c, &s->s_c_list, c_ent) {
			subscriber_count += 1;
		}
		pthread_mutex_unlock(&s->s_lock);
	}
	return subscriber_count;
}

void ldmsd_stream_deliver(const char *stream_name, ldmsd_stream_type_t stream_type,
			  const char *data, size_t data_len,
			  json_entity_t entity, const char *p_name)
{
	json_parser_t parser = NULL;
	ldmsd_stream_client_t c;
	ldmsd_stream_publisher_t p;
	ldmsd_stream_t s = __find_stream(stream_name);
	int need_free = 0;
	time_t now;

	now = time(NULL);
	if (!s) {
		s = __new_stream(stream_name);
		if (!s)
			return;
		pthread_mutex_lock(&s->s_lock);
	}
	if (!s->s_info.first_ts)
		s->s_info.first_ts = now;
	s->s_info.count += 1;
	s->s_info.last_ts = now;
	s->s_info.total_bytes += data_len;

	LIST_FOREACH(c, &s->s_c_list, c_ent) {
		if (stream_type == LDMSD_STREAM_JSON
			&& c->c_flags == 0	/* client wants parsed data */
			&& entity == NULL	/* data hasn't been parsed yet */
			&& parser == NULL)	/* we haven't tried and failed already */
		{
			parser = json_parser_new(0);
			if (!parser)
				continue;
			int rc = json_parse_buffer(parser, (char *)data, data_len, &entity);
			if (rc)
				continue;
			need_free = 1;
		}
		c->c_cb_fn(c, c->c_ctxt, stream_type, data, data_len, entity);
	}

	if (p_name) {
		p = __find_publisher(s, p_name);
		if (!p) {
			p = __new_publisher(p_name);
			if (!p)
				return;
			p->p_info.first_ts = now;
			rbt_ins(&s->s_p_tree, &p->p_ent);
		}
		p->p_info.last_ts = now;
		p->p_info.count += 1;
		p->p_info.total_bytes += data_len;
	}

	if (entity && need_free)
		json_entity_free(entity);
	if (parser)
		json_parser_free(parser);
	pthread_mutex_unlock(&s->s_lock);
}

ldmsd_stream_client_t
ldmsd_stream_subscribe(const char *stream_name,
		       ldmsd_stream_recv_cb_t cb_fn, void *ctxt)
{
	ldmsd_stream_t s;
	ldmsd_stream_client_t cc, c;
	c = malloc(sizeof *c);
	if (!c)
		goto err_0;

	/* Find the stream */
	s = __find_stream(stream_name);
	if (!s) {
		s = __new_stream(stream_name);
		if (!s)
			goto err_1;
		pthread_mutex_lock(&s->s_lock);
	}
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
	c->c_flags = 0;
	c->c_cb_fn = cb_fn;
	c->c_ctxt = ctxt;
	LIST_INSERT_HEAD(&s->s_c_list, c, c_ent);
	pthread_mutex_unlock(&s->s_lock);
	return c;
 err_1:
	free(c);
 err_0:
	return NULL;
}

void ldmsd_stream_flags_set(ldmsd_stream_client_t c, uint32_t f)
{
	c->c_flags = f;
}

uint32_t ldmsd_stream_flags_get(ldmsd_stream_client_t c)
{
	return c->c_flags;
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
	free(c);
}

static int stream_send(struct stream_ctxt *ctxt, struct ldmsd_msg_buf *buf,
					uint32_t msg_no, uint32_t flags,
					char *data, size_t data_len)
{
	return ldmsd_msg_buf_send(buf, ctxt, msg_no,
				__stream_send, flags,
				LDMSD_REQ_TYPE_CONFIG_CMD,
				LDMSD_STREAM_PUBLISH_REQ,
				data, data_len);
}

static int stream_hdr_send(struct stream_ctxt *ctxt, uint32_t msg_no,
			   const char *stream_name,
			   ldmsd_stream_type_t stream_type,
			   struct ldmsd_msg_buf *buf,
			   size_t data_len)
{
	struct ldmsd_req_attr_s a;
	size_t nlen;
	int rc;

	/* stream_name */
	a.discrim = 1;
	a.attr_id = LDMSD_ATTR_NAME;
	nlen = strlen(stream_name) + 1;
	a.attr_len = nlen;
	ldmsd_hton_req_attr(&a);
	rc = stream_send(ctxt, buf, msg_no, LDMSD_REQ_SOM_F, (char *)&a, sizeof(a));
	if (rc)
		return rc;
	rc = stream_send(ctxt, buf, msg_no, 0, (char *)stream_name, nlen);
	if (rc)
		return rc;

	/* stream_type */
	a.discrim = 1;
	a.attr_len = data_len;
	switch (stream_type) {
	case LDMSD_STREAM_STRING:
		a.attr_id = LDMSD_ATTR_STRING;
		break;
	case LDMSD_STREAM_JSON:
		a.attr_id = LDMSD_ATTR_JSON;
		break;
	default:
		return EINVAL;
	}
	ldmsd_hton_req_attr(&a);
	rc = stream_send(ctxt, buf, msg_no, 0, (char *)&a, sizeof(a));
	return rc;
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
	struct ldmsd_req_attr_s a;
	struct ldmsd_msg_buf *buf;
	uint32_t msg_no = ldmsd_msg_no_get();
	int rc = 0;
	struct stream_ctxt ctxt = {
			.x = xprt,
	};

	if (!data_len)
		return 0;

	size_t max_msg = ldms_xprt_msg_max(xprt);
	buf = ldmsd_msg_buf_new(max_msg);
	if (!buf) {
		msglog("Error allocating %d bytes of memory for buffer\n",
			max_msg);
		return ENOMEM;
	}

	rc = stream_hdr_send(&ctxt, msg_no, stream_name, stream_type, buf, data_len);
	if (rc)
		goto err;

	rc = stream_send(&ctxt, buf, msg_no, 0, (char *)data, data_len);
	if (rc)
		goto err;

	/* TERMINATING */
	a.discrim = 0;
	rc = stream_send(&ctxt, buf, msg_no, LDMSD_REQ_EOM_F,
			(char *)&a.discrim, sizeof(a.discrim));

 err:
	if (buf)
		ldmsd_msg_buf_free(buf);
	return rc;
}

sem_t conn_sem;
sem_t recv_sem;
int conn_status = ENOTCONN;

static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	struct stream_ctxt *ctxt = (struct stream_ctxt *)cb_arg;
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
		/* No need to process the response message. */
		sem_post(&recv_sem);
		break;
	case LDMS_XPRT_EVENT_SEND_COMPLETE:
		if (!ctxt->is_done)
			return;
		break;
	default:
		ldms_xprt_put(x);
		conn_status = ECONNABORTED;
		msglog("Received invalid event type %d\n", e->type);
	}
	sem_post(&conn_sem);
}

struct publish_file_ctxt {
	int done; /* 0 means done */
};

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
#define LDMSD_STREAM_ACK_TIMEOUT 20 /* 20 seconds */
int ldmsd_stream_publish_file(const char *stream, const char *type,
			      const char *xprt, const char *host, const char *port,
			      const char *auth, struct attr_value_list *auth_opt,
			      FILE *file)
{
	int rc, stream_type;
	size_t data_len;
	static char *buffer;
	size_t max_msg_len;
	ldms_t x;
	size_t cnt;
	struct ldmsd_msg_buf *buf = NULL;
	uint32_t msg_no;
	struct ldmsd_req_attr_s a;
	struct stream_ctxt ctxt = {0};

	char *timeout_s;
	int timeout = LDMSD_STREAM_CONNECT_TIMEOUT;
	int recv_timeout = LDMSD_STREAM_ACK_TIMEOUT;

	/* conn timeout */
	timeout_s = getenv("LDMSD_STREAM_CONN_TIMEOUT");
	if (timeout_s)
		timeout = atoi(timeout_s);
	/* recv timeout */
	timeout_s = getenv("LDMSD_STREAM_ACK_TIMEOUT");
	if (timeout_s)
		recv_timeout = atoi(timeout_s);

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
	ctxt.x = x;
	max_msg_len = ldms_xprt_msg_max(x);

	buf = ldmsd_msg_buf_new(max_msg_len);
	if (!buf) {
		msglog("Out of memory\n");
		ldms_xprt_put(x);
		return ENOMEM;
	}

	buffer = malloc(max_msg_len);
	if (!buffer) {
		msglog("Out of memory\n");
		ldms_xprt_put(x);
		rc = ENOMEM;
		goto err;
	}

	sem_init(&conn_sem, 0, 0);
	sem_init(&recv_sem, 0, 0);

	rc = ldms_xprt_connect_by_name(x, host, port, event_cb, &ctxt);
	if (rc) {
		msglog("Error %d connecting to %s:%s\n",
			rc, host, port);
		goto err;
	}
	struct timespec ts;
	ts.tv_sec = time(NULL) + timeout;
	ts.tv_nsec = 0;
	if (sem_timedwait(&conn_sem, &ts)) {
		msglog("Timeout connecting to remote peer\n");
		rc = errno;
		goto err;
	}
	if (conn_status) {
		msglog("Error %d connecting to remote peer\n", conn_status);
		rc = conn_status;
		goto err;
	}

	msg_no = ldmsd_msg_no_get();
	rc = fseek(file, 0L, SEEK_END);
	if (!rc) {
		data_len = ftell(file);
		rewind(file);

		rc = stream_hdr_send(&ctxt, msg_no, stream, stream_type,
				     buf, data_len + 1 /* terminating '\0' */);
		if (rc)
			goto close_xprt;

		while ((cnt = fread(buffer, 1, max_msg_len-1, file)) > 0) {
			if (cnt < max_msg_len-1) {
				/* Ensure last buffer is '\0' terminated */
				buffer[cnt] = '\0';
				cnt += 1;
			}
			rc = stream_send(&ctxt, buf, msg_no, 0, buffer, cnt);
			if (rc)
				goto close_xprt;
		}

	} else {
		if (ESPIPE != errno) {
			msglog("The given file is invalid.\n");
			goto close_xprt;
		}

		/* a non-seekable FILE */
		char *s, *b;
		size_t cnt;
		size_t len = max_msg_len;
		data_len = 0;

		b = malloc(max_msg_len);
		if (!b) {
			msglog("Out of memory\n");
			goto close_xprt;
		}
		while (0 != (s = fgets(b, sizeof(b)-1, file))) {
			cnt = strlen(s);
			if (data_len + cnt >= len) {
				/*
				 *  +1 to ensure that we have enough space
				 * to null terminated the buffer
				 */
				char *_b = realloc(buffer, len * 2 + cnt + 1);
				if (!_b) {
					msglog("Out of memory\n");
					goto err;
				}
				buffer = _b;
				len = len * 2 + cnt + 1;
			}
			memcpy(&buffer[data_len], b, cnt);
			data_len += cnt;
		}
		buffer[data_len] = '\0';
		free(b);

		rc = stream_hdr_send(&ctxt, msg_no, stream, stream_type,
				     buf, data_len + 1 /* terminating '\0' */);
		if (rc)
			goto close_xprt;
		rc = stream_send(&ctxt, buf, msg_no, 0, buffer, data_len + 1);
		if (rc)
			goto close_xprt;
	}

	/* Terminating */
	a.discrim = 0;
	ctxt.is_done = 1;
	rc = stream_send(&ctxt, buf, msg_no, LDMSD_REQ_EOM_F,
			(char *)&a.discrim, sizeof(a.discrim));
	if (rc)
		goto close_xprt;

	/*
	 * Wait for the response before closing the transport
	 */
	ts.tv_sec =time(NULL) + recv_timeout;
	ts.tv_nsec = 0;
	sem_timedwait(&recv_sem, &ts);

	ts.tv_sec = time(NULL) + timeout;
	ts.tv_nsec = 0;
	if (sem_timedwait(&conn_sem, &ts)) {
		msglog("Timeout connecting to remote peer\n");
		rc = errno;
		goto err;
	}
close_xprt:
	ldms_xprt_close(x);
err:
	free(buffer);
	if (buf)
		ldmsd_msg_buf_free(buf);
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
			free(sym);
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

int __stream_info_json(struct buf_s *buf, struct ldmsd_stream_info_s *info)
{
	int rc;
	rc = buf_printf(buf, "{");
	if (rc)
		return rc;
	if (0 == info->first_ts)
		goto end;

	rc = buf_printf(buf, "\"first_ts\":%ld,"
			     "\"last_ts\":%ld,"
			     "\"count\":%d,"
			     "\"total_bytes\":%ld",
			     info->first_ts, info->last_ts,
			     info->count, info->total_bytes);
	if (rc)
		return rc;
	if (info->last_ts != info->first_ts) {
		rc = buf_printf(buf, ",\"msg/sec\":%lf,"
				     "\"bytes/sec\":%lf",
				     (info->count*1.0)/(info->last_ts - info->first_ts),
				     info->total_bytes*1.0/(info->last_ts - info->first_ts));
	}
end:
	rc = buf_printf(buf, "}");
	return rc;
}

int __publisher_json(struct buf_s *buf, ldmsd_stream_publisher_t p)
{
	int rc;
	rc = buf_printf(buf, "\"%s\":{"
			      "\"info\":",
			      p->p_name);
	if (rc)
		return rc;
	rc = __stream_info_json(buf, &p->p_info);
	if (rc)
		return rc;
	rc = buf_printf(buf, "}");
	return rc;
}

int __stream_json(struct buf_s *buf, ldmsd_stream_t s)
{
	int rc;
	int first = 1;
	ldmsd_stream_publisher_t p;
	struct rbn *rbn;
	rc = buf_printf(buf, "\"%s\":{", s->s_name);
	if (rc)
		return rc;
	rc = buf_printf(buf, "\"mode\":\"%s\","
			     "\"info\":",
			     (LIST_EMPTY(&s->s_c_list)?"not subscribed":"subscribed"));
	if (rc)
		return rc;
	rc = __stream_info_json(buf, &s->s_info);
	if (rc)
		return rc;
	rc = buf_printf(buf, ",\"publishers\":{");
	if (rc)
		return rc;
	RBT_FOREACH(rbn, &s->s_p_tree) {
		if (!first) {
			rc = buf_printf(buf, ",");
			if (rc)
				return rc;
		} else {
			first = 0;
		}
		p = container_of(rbn, struct ldmsd_stream_publisher_s, p_ent);
		rc = __publisher_json(buf, p);
		if (rc)
			return rc;
	}
	rc = buf_printf(buf, "}}");
	return rc;
}

char *ldmsd_stream_dir_dump()
{
	int rc;
	ldmsd_stream_t s;
	struct rbn *rbn;
	struct buf_s buf = {.sz = 4096};
	int first = 1;
	struct ldmsd_stream_info_s tot_info = {0};

	buf.buf = malloc(buf.sz);
	if (!buf.buf) {
		errno = ENOMEM;
		return NULL;
	}

	rc = buf_printf(&buf, "{");
	if (rc)
		goto free_buf;
	pthread_mutex_lock(&s_tree_lock);
	RBT_FOREACH(rbn, &s_tree) {
		s = container_of(rbn, struct ldmsd_stream_s, s_ent);

		tot_info.total_bytes += s->s_info.total_bytes;
		tot_info.count += s->s_info.count;
		if (tot_info.first_ts == 0)
			tot_info.first_ts = s->s_info.first_ts;
		else if (tot_info.first_ts > s->s_info.first_ts)
			tot_info.first_ts = s->s_info.first_ts;
		if (tot_info.last_ts < s->s_info.last_ts)
			tot_info.last_ts = s->s_info.last_ts;

		rc = buf_printf(&buf, "%s", ((first)?"":","));
		if (rc)
			goto unlock_s;
		first = 0;
		pthread_mutex_lock(&s->s_lock);
		rc = __stream_json(&buf, s);
		if (rc)
			goto unlock_s;
		pthread_mutex_unlock(&s->s_lock);
	}
	pthread_mutex_unlock(&s_tree_lock);

	if (!first) {
		rc = buf_printf(&buf, ",\"_AGGREGATED_\":{"
					   "\"info\":");
		if (rc)
			goto free_buf;
		rc = __stream_info_json(&buf, &tot_info);
		if (rc)
			goto free_buf;
		rc = buf_printf(&buf, "}");
		if (rc)
			goto free_buf;
	}
	rc = buf_printf(&buf, "}");
	if (rc)
		goto free_buf;
	return buf.buf;
unlock_s:
	pthread_mutex_unlock(&s->s_lock);
	pthread_mutex_unlock(&s_tree_lock);
free_buf:
	free(buf.buf);
	errno = rc;
	return NULL;
}
