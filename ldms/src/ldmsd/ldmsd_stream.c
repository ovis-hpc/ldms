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

typedef struct ldmsd_stream_client_s {
	ldmsd_stream_recv_cb_t c_cb_fn;
	void *c_ctxt;
	ldmsd_stream_t c_s;
	LIST_ENTRY(ldmsd_stream_client_s) c_ent;
} *ldmsd_stream_client_t;

typedef struct ldmsd_stream_s {
	const char *s_name;
	struct rbn s_ent;
	pthread_mutex_t s_lock;
	LIST_HEAD(ldmsd_client_list, ldmsd_stream_client_s) s_c_list;
} *ldmsd_stream_t;

static pthread_mutex_t s_tree_lock = PTHREAD_MUTEX_INITIALIZER;
struct rbt s_tree = RBT_INITIALIZER(s_cmp);

enum ldmsd_stream_type_e ldmsd_stream_type_str2enum(const char *type)
{
	if (0 == strncmp(type, "json", 4))
		return LDMSD_STREAM_JSON;
	else
		return LDMSD_STREAM_STRING;
}

const char *ldmsd_stream_type_enum2str(enum ldmsd_stream_type_e type)
{
	if (LDMSD_STREAM_JSON == type)
		return "json";
	else
		return "string";
}

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
	ldmsd_stream_t s;
	struct rbn *rbn;
	ldmsd_stream_client_t c = NULL;

	/* Find the stream */
	pthread_mutex_lock(&s_tree_lock);
	rbn = rbt_find(&s_tree, stream_name);
	pthread_mutex_unlock(&s_tree_lock);
	if (!rbn) {
		s = malloc(sizeof *s);
		s->s_name = strdup(stream_name);
		if (!s->s_name) {
			free(s);
			goto out;
		}
		pthread_mutex_init(&s->s_lock, NULL);
		LIST_INIT(&s->s_c_list);
		rbn_init(&s->s_ent, (char *)s->s_name);
		rbt_ins(&s_tree, &s->s_ent);
	} else {
		s = container_of(rbn, struct ldmsd_stream_s, s_ent);
	}
	c = malloc(sizeof *c);
	if (!c)
		goto out;
	c->c_s = s;
	c->c_cb_fn = cb_fn;
	c->c_ctxt = ctxt;
	pthread_mutex_lock(&s->s_lock);
	LIST_INSERT_HEAD(&s->s_c_list, c, c_ent);
	pthread_mutex_unlock(&s->s_lock);
 out:
	return c;
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

int __ldmsd_stream_send(void *xprt, char *data, size_t data_len)
{
	return ldms_xprt_send((ldms_t)xprt, data, data_len);
}

struct stream_hdr {
	short stream_type;
	short name_len; /* including the terminating character. */
	char data[OVIS_FLEX]; /* stream_name = &data[0], payload = &data[name_len] */
};

void __ldmsd_stream_extract_hdr(const char *s, char **_stream_name,
			enum ldmsd_stream_type_e *_stream_type,
			char **_data, size_t *_offset)
{
	struct stream_hdr *hdr;

	hdr = (struct stream_hdr *)s;
	hdr->stream_type = ntohs(hdr->stream_type);
	hdr->name_len = ntohs(hdr->name_len);
	*_stream_type = hdr->stream_type;
	*_stream_name = hdr->data;
	*_data = &hdr->data[hdr->name_len];
	*_offset = sizeof(*hdr) + hdr->name_len;
}

int ldmsd_stream_append_hdr(ldms_t x, ldmsd_msg_key_t key, ldmsd_req_buf_t buf,
			enum ldmsd_stream_type_e stream_type, const char *name)
{
	int rc;
	struct stream_hdr hdr;
	size_t name_len = strlen(name) + 1;
	hdr.stream_type = htons(stream_type);
	hdr.name_len = htons(name_len);
	rc = ldmsd_append_msg_buffer(x, ldms_xprt_msg_max(x),
			key, __ldmsd_stream_send, buf,
			LDMSD_REC_SOM_F, LDMSD_MSG_TYPE_STREAM,
			(char *)&hdr, sizeof(hdr));
	if (rc)
		return rc;
	return ldmsd_append_msg_buffer(x, ldms_xprt_msg_max(x),
				key, __ldmsd_stream_send, buf, 0,
				LDMSD_MSG_TYPE_STREAM, name, name_len);
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
 * }
 *
 *
 * \returns 0 on success, or an errno
 */
int ldmsd_stream_publish(ldms_t xprt,
			 const char *stream_name,
			 ldmsd_stream_type_t stream_type,
			 const char *data, size_t data_len)
{
	ldmsd_req_buf_t buf;
	struct ldmsd_msg_key key;
	int rc;
	size_t max_msg = ldms_xprt_msg_max(xprt);
	ldms_xprt_get(xprt);
	buf = ldmsd_req_buf_alloc(max_msg);
	if (!buf) {
		msglog("Out of memory\n");
		rc = ENOMEM;
		goto out;
	}

	ldmsd_msg_key_get(xprt, &key);

	rc = ldmsd_stream_append_hdr(xprt, &key, buf, stream_type, stream_name);
	if (rc)
		goto out;

	rc = ldmsd_append_msg_buffer(xprt, max_msg, &key,
					__ldmsd_stream_send,
					buf, LDMSD_REC_EOM_F,
					LDMSD_MSG_TYPE_STREAM,
					data, data_len);
	if (rc)
		goto out;
out:
	if (buf)
		ldmsd_req_buf_free(buf);
	ldms_xprt_put(xprt);
	return rc;
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
	enum ldmsd_stream_type_e stream_type;
	struct ldmsd_msg_key key;
	int rc;
	size_t data_len;
	static char buffer[1024*64];
	size_t max_msg;
	ldms_t x;
	ldmsd_req_buf_t buf = NULL;

	if ((0 == strcasecmp("raw", type)) || (0 == strcasecmp("string", type)))
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

	sem_init(&recv_sem, 0, 0);
	sem_init(&conn_sem, 0, 0);

	rc = ldms_xprt_connect_by_name(x, host, port, event_cb, NULL);
	if (rc) {
		msglog("Error %d connecting to %s:%s\n",
			rc, host, port);
		ldms_xprt_put(x);
		return rc;
	}
	struct timespec ts;
	ts.tv_sec = time(NULL) + 2;
	ts.tv_nsec = 0;
	sem_timedwait(&conn_sem, &ts);
	if (conn_status) {
		rc = conn_status;
		goto out;
	}

	max_msg = ldms_xprt_msg_max(x);
	buf = ldmsd_req_buf_alloc(max_msg);
	if (!buf) {
		msglog("Out of memory\n");
		rc = ENOMEM;
		goto out;
	}

	ldmsd_msg_key_get(x, &key);

	rc = ldmsd_stream_append_hdr(x, &key, buf, stream_type, stream);
	if (rc)
		goto out;

	while ((data_len = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		if (conn_status) {
			rc = conn_status;
			goto out;
		}

		rc = ldmsd_append_msg_buffer_va(x, max_msg, &key,
					__ldmsd_stream_send, buf, 0,
					LDMSD_MSG_TYPE_STREAM, "%s", buffer);

		if (rc)
			goto out;
	}

	rc = ldmsd_append_msg_buffer(x, max_msg, &key, __ldmsd_stream_send,
					buf, LDMSD_REC_EOM_F,
					LDMSD_MSG_TYPE_STREAM, NULL, 0);
out:
	if (buf)
		ldmsd_req_buf_free(buf);
	ldms_xprt_close(x);
	return rc;
}
