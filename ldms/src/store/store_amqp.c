/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2016,2018 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <assert.h>
#include <inttypes.h>
#include <malloc.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <limits.h>
#include <amqp_tcp_socket.h>
#include <amqp_ssl_socket.h>
#include <amqp_framing.h>
#include <coll/rbt.h>
#include <pthread.h>
#include "ldms.h"
#include "ldmsd.h"

static ovis_log_t mylog;

static struct rbt amqp_rbt;	/* key is amqp_instance.container name */
#define LERR(_fmt_, ...) ovis_log(mylog, OVIS_LERROR, "store_amqp: " _fmt_, ##__VA_ARGS__)
#define LINF(_fmt_, ...) ovis_log(mylog, OVIS_LINFO, "store_amqp: " _fmt_, ##__VA_ARGS__)
/*
 * NOTE:
 *   <amqp::path> = <root_path>/<container>
 */
enum amqp_formatter_type {
	BIN_FMT,
	JSON_FMT,
	CSV_FMT,
};
typedef struct amqp_instance *amqp_inst_t;
typedef int (*amqp_msg_formatter_t)(amqp_inst_t ai, ldms_set_t s,
				    int *metric_array, size_t metric_count);
static amqp_msg_formatter_t formatters[];

struct amqp_instance {
	struct ldmsd_store *store;
	amqp_msg_formatter_t formatter;
	char *container;
	char *exchange;		/* AMQP exchange  */
	char *schema;
	char *vhost;
	char *host;
	unsigned short port;
	char *user;
	char *pwd;
	char *routing_key;
	amqp_socket_t *socket;
	amqp_connection_state_t conn;
	int channel;
	char *ca_pem;		/* CA .PEM */
	char *key;		/* key .PEM */
	char *cert;		/* cert .PEM */
	struct rbn rbn;
	char *value_buf;
	size_t value_buf_len;
	char *msg_buf;
	size_t msg_buf_len;
	pthread_mutex_t lock;
};
static pthread_mutex_t cfg_lock;

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

static size_t realloc_msg_buf(amqp_inst_t ai, size_t buf_len)
{
	if (ai->msg_buf)
		free(ai->msg_buf);
	ai->msg_buf_len = buf_len;
	ai->msg_buf = malloc(ai->msg_buf_len);
	if (ai->msg_buf)
		return ai->msg_buf_len;
	else
		LERR("OOM error allocating %d bytes for message buffer.\n", buf_len);
	return 0;
}

static int setup_certs(amqp_inst_t ai, const char *cacert, struct attr_value_list *avl)
{
	int rc;
	const char *key;
	const char *cert;

	rc = amqp_ssl_socket_set_cacert(ai->socket, cacert);
	if (rc) {
		LERR("Error %d setting the CA cert file.\n", rc);
		goto out;
	}
	key = av_value(avl, "key");
	cert = av_value(avl, "cert");
	if (key) {
		if ((key && !cert) || (cert && !key)) {
			rc = EINVAL;
			LERR("The key .PEM and cert.PEM files must"
			     "be specified together.\n");
			goto out;
		}
		rc = amqp_ssl_socket_set_key(ai->socket, cert, key);
		if (rc) {
			LERR("Error %d setting key and cert files.\n");
			goto out;
		}
	}
 out:
	return rc;
}

static int comparator(void *a, const void *b)
{
	return strcmp((char *)a, (char *)b);
}

static int check_reply(amqp_rpc_reply_t r, const char *file, int line)
{
	switch (r.reply_type) {
	case AMQP_RESPONSE_NORMAL:
		return 0;
	case AMQP_RESPONSE_NONE:
		LERR("AMQP protocol error; missing RPC reply type\n");
		break;
	case AMQP_RESPONSE_LIBRARY_EXCEPTION:
		LERR("%s\n", amqp_error_string2(r.library_error));
		break;
	case AMQP_RESPONSE_SERVER_EXCEPTION:
		switch (r.reply.id) {
		case AMQP_CONNECTION_CLOSE_METHOD: {
			amqp_connection_close_t *m =
				(amqp_connection_close_t *) r.reply.decoded;
			LERR("server connection error %d, message: %.*s\n",
			     m->reply_code,
			     (int)m->reply_text.len, (char *)m->reply_text.bytes);
			break;
		}
		case AMQP_CHANNEL_CLOSE_METHOD: {
			amqp_channel_close_t *m = (amqp_channel_close_t *) r.reply.decoded;
			LERR("server channel error %d, message: %.*s\n",
			     m->reply_code,
			     (int)m->reply_text.len, (char *)m->reply_text.bytes);
			break;
		}
		default:
			LERR("unknown server error, method id 0x%08X\n", r.reply.id);
			break;
		}
		break;
	}
	return -1;
}
#define CHECK_REPLY(_qrc_) check_reply(_qrc_, __FILE__, __LINE__)

#define DEF_MSG_BUF_LEN (128 * 1024)	/* Should avoid realloc() for most sets */
#define DEF_AMQP_SSL_PORT 5671
#define DEF_AMQP_TCP_PORT 5672
/**
 * \brief Configuration
 *
 * Required key/values
 *   container=<name>   The unique storage container name.
 *   host=<hostname>    The DNS hostname or IP address of the AMQP server.
 *
 * Optional key/values:
 *   exchange=<name>    Unique name of the AMQP exchange, defaults to LDMS.set.$schema$
 *   port=<port_no>     The port number of the AMQP server, defaults to 5731.
 *   vhost=<host>       Virtual host, defaults to "/"
 *   cacert=<path>      Path to the CA certificate file in PEM format. If specified,
 *                      key and cert must also be specified.
 *   key=<path>         Path to the client key in PEM format. If specified cacert and
 *                      cert must also be specified.
 *   cert=<path>        Path to the client cert in PEM format. If specified, cacert and
 *                      key must also be specified.
 *   user=<name>        The SASL user name, default is "guest"
 *   pwd=<password>     The SASL password, default is "guest"
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	amqp_inst_t ai;
	struct rbn *rbn;
	int rc;
	amqp_rpc_reply_t qrc;

	pthread_mutex_lock(&cfg_lock);
	value = av_value(avl, "container");
	if (!value) {
		LERR("The container name must be specifed.\n");
		rc = EINVAL;
		goto err_0;
	}
	rbn = rbt_find(&amqp_rbt, value);
	if (rbn) {
		LERR("The specified container already exists.\n");
		rc = EINVAL;
		goto err_0;
	}
	ai = calloc(1, sizeof(*ai));
	if (!ai) {
		LERR("OOM error creating AMQP instance.\n");
		rc = ENOMEM;
		goto err_0;
	}
	ai->container = strdup(value);
	if (!ai->container) {
		LERR("OOM error allocating container name.\[n");
		rc = ENOMEM;
		goto err_1;
	}
	ai->formatter = formatters[JSON_FMT];
	ai->routing_key = "JSON";
	value = av_value(avl, "format");
	if (value) {
		if (0 == strcasecmp(value, "csv")) {
			ai->formatter = formatters[CSV_FMT];
			ai->routing_key = "CSV";
		} else if (0 == strcasecmp(value, "binary")) {
			ai->formatter = formatters[BIN_FMT];
			ai->routing_key = "BIN";
		} else if (0 != strcasecmp(value, "json")) {
			LINF("Invalid formatter '%s' specified, defaulting to JSON.\n",
			     value);
		}
	}
	value = av_value(avl, "host");
	if (!value) {
		LERR("The host parameter must be specified.\n");
		rc = EINVAL;
		goto err_1;
	}
	ai->host = strdup(value);
	if (!ai->host) {
		LERR("OOM error copying host name.\n");
		rc = ENOMEM;
		goto err_1;
	}
	value = av_value(avl, "exchange");
	if (value) {
		ai->exchange = strdup(value);
		if (!ai->exchange) {
			LERR("OOM error copying queue name.\n");
			rc = ENOMEM;
			goto err_1;
		}
	}
	value = av_value(avl, "port");
	if (value) {
		long sl;
		sl = strtol(value, NULL, 0);
		if (sl < 1 || sl > USHRT_MAX) {
			LERR("Invalid port number %s.\n", value);
			goto err_1;
		}
		ai->port = sl;
	} else {
		ai->port = DEF_AMQP_TCP_PORT;
	}
	value = av_value(avl, "vhost");
	if (value)
		ai->vhost = strdup(value);
	else
		ai->vhost = strdup("/");
	value = av_value(avl, "user");
	if (value)
		ai->user = strdup(value);
	else
		ai->user = strdup("guest");
	if (!ai->user) {
		LERR("OOM error copying user name.\n");
		rc = ENOMEM;
		goto err_1;
	}
	value = av_value(avl, "pwd");
	if (value)
		ai->pwd = strdup(value);
	else
		ai->pwd = strdup("guest");
	if (!ai->pwd) {
		LERR("OOM error copying password.\n");
		rc = ENOMEM;
		goto err_1;
	}

	/* Create the connection state */
	ai->conn = amqp_new_connection();
	if (!ai->conn) {
		rc = (errno ? errno : ENOMEM);
		ovis_log(mylog, OVIS_LERROR, "%s: Error %d creating the AMQP connection state.\n",
		       __FILE__, rc);
		goto err_1;
	}
	/* Create the socket */
	ai->socket = amqp_tcp_socket_new(ai->conn);
	if (!ai->socket) {
		rc = (errno ? errno : ENOMEM);
		ovis_log(mylog, OVIS_LERROR, "%s: Error %d creating the AMQP connection state.\n",
		       __FILE__, rc);
		goto err_2;
	}
	value = av_value(avl, "cacert");
	if (value) {
		rc = setup_certs(ai, value, avl);
		if (rc)
			goto err_3;
	}
	rc = amqp_socket_open(ai->socket, ai->host, ai->port);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: Error %d opening the AMQP socket.\n", __FILE__, rc);
		goto err_2;
	}
	qrc = amqp_login(ai->conn, ai->vhost, 0, AMQP_DEFAULT_FRAME_SIZE, 0,
			 AMQP_SASL_METHOD_PLAIN, ai->user, ai->pwd);
	if (CHECK_REPLY(qrc) < 0)
		goto err_3;
	ai->channel = 1;
	amqp_channel_open(ai->conn, ai->channel);
	qrc = amqp_get_rpc_reply(ai->conn);
	if (CHECK_REPLY(qrc) < 0)
		goto err_3;

	pthread_mutex_init(&ai->lock, NULL);
	rbn_init(&ai->rbn, ai->container);
	rbt_ins(&amqp_rbt, &ai->rbn);
	pthread_mutex_unlock(&cfg_lock);
	return 0;
 err_3:
	amqp_channel_close(ai->conn, ai->channel, AMQP_REPLY_SUCCESS);
	amqp_connection_close(ai->conn, AMQP_REPLY_SUCCESS);
 err_2:
	amqp_destroy_connection(ai->conn);
 err_1:
	if (ai->container)
		free(ai->container);
	if (ai->msg_buf)
		free(ai->msg_buf);
	if (ai->exchange)
		free(ai->exchange);
	if (ai->host)
		free(ai->host);
	if (ai->vhost)
		free(ai->vhost);
	if (ai->user)
		free(ai->user);
	if (ai->pwd)
		free(ai->pwd);
	free(ai);
 err_0:
	pthread_mutex_unlock(&cfg_lock);
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return "Required key/values\n"
		"   container=<name>   The unique storage container name.\n"
		"   host=<hostname>    The DNS hostname or IP address of the AMQP server.\n"
		"\n"
		" Optional key/values:\n"
		"   exchange=<name>    Unique name of the AMQP exchange, defaults to LDMS.set.$schema$\n"
		"   port=<port_no>     The port number of the AMQP server, defaults to 5731.\n"
		"   vhost=<host>       Virtual host, defaults to '/'\n"
		"   cacert=<path>      Path to the CA certificate file in PEM format. If specified,\n"
		"                      key and cert must also be specified.\n"
		"   key=<path>         Path to the client key in PEM format. If specified cacert and\n"
		"                      cert must also be specified.\n"
		"   cert=<path>        Path to the client cert in PEM format. If specified, cacert and\n"
		"                      key must also be specified.\n"
		"   user=<name>        The SASL user name, default is 'guest'\n"
		"   pwd=<password>     The SASL password, default is 'guest'\n";
}

static ldmsd_store_handle_t
open_store(ldmsd_plug_handle_t s, const char *container, const char *schema,
	   struct ldmsd_strgp_metric_list *metric_list)
{
	amqp_inst_t ai;
	struct rbn *rbn;
	amqp_rpc_reply_t qrc;

	pthread_mutex_lock(&cfg_lock);
	rbn = rbt_find(&amqp_rbt, container);
	if (!rbn) {
		LERR("The container '%s', has not been configured.\n", container);
		goto err_0;
	}
	ai = container_of(rbn, struct amqp_instance, rbn);
	if (!ai->exchange) {
		size_t sz = strlen(schema) + sizeof("LDMS.set.") + 1;
		ai->exchange = malloc(sz);
		if (!ai->exchange)
			goto err_0;
		sprintf(ai->exchange, "LDMS.set.%s", schema);
	}
	/* Create the LDMS.json exchange */
	amqp_exchange_declare(ai->conn, ai->channel,
			      amqp_cstring_bytes(ai->exchange),
			      amqp_cstring_bytes("direct"),
			      0, 0,
			      #if AMQP_VERSION >= 0x00060001
			      0, 0,
			      #endif
			      amqp_empty_table);
	qrc = amqp_get_rpc_reply(ai->conn);
	if (CHECK_REPLY(qrc) < 0)
		goto err_0;

	ai->schema = strdup(schema);
	pthread_mutex_unlock(&cfg_lock);
	return ai;
 err_0:
	pthread_mutex_unlock(&cfg_lock);
	return NULL;
}

static int
store(ldmsd_store_handle_t _sh, ldms_set_t set,
      int *metric_arry, size_t metric_count)
{
	amqp_basic_properties_t props;
	amqp_inst_t ai = _sh;
	size_t msg_len;
	int rc;
	amqp_bytes_t msg_bytes;
	if (!ai)
		return EINVAL;
	props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG |
		AMQP_BASIC_DELIVERY_MODE_FLAG |
		AMQP_BASIC_TIMESTAMP_FLAG;
	props.content_type = amqp_cstring_bytes("text/json");
	props.delivery_mode = 1;
	props.timestamp = (uint64_t)time(NULL);

	pthread_mutex_lock(&ai->lock);
	msg_len = ai->formatter(ai, set, metric_arry, metric_count);
	msg_bytes.len = msg_len;
	msg_bytes.bytes = ai->msg_buf;
	rc = amqp_basic_publish(ai->conn, ai->channel,
				amqp_cstring_bytes(ai->exchange),
				amqp_cstring_bytes(ai->routing_key),
				0, 0, &props, msg_bytes);
	pthread_mutex_unlock(&ai->lock);
	return rc;
}

static void _close_store(amqp_inst_t ai)
{
	amqp_channel_close(ai->conn, ai->channel, AMQP_REPLY_SUCCESS);
	amqp_connection_close(ai->conn, AMQP_REPLY_SUCCESS);
	amqp_destroy_connection(ai->conn);
	rbt_del(&amqp_rbt, &ai->rbn);
	free(ai->container);
	free(ai->msg_buf);
	free(ai->value_buf);
	free(ai->exchange);
	free(ai->schema);
	free(ai->vhost);
	free(ai->host);
	free(ai->user);
	free(ai->pwd);
}

static void close_store(ldmsd_store_handle_t _sh)
{
	struct amqp_instance *ai = _sh;
	if (!ai)
		return;

	pthread_mutex_lock(&cfg_lock);
	_close_store(ai);
	pthread_mutex_unlock(&cfg_lock);
}

static void term(ldmsd_plug_handle_t handle)
{
	struct rbn *rbn;
	amqp_inst_t ai;

	pthread_mutex_lock(&cfg_lock);
	while ((rbn = rbt_min(&amqp_rbt)) != NULL) {
		ai = container_of(rbn, struct amqp_instance, rbn);
		_close_store(ai);
	}
	pthread_mutex_unlock(&cfg_lock);
}

#define DEF_VALUE_BUF_LEN 1024

size_t u8_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%hhd", val->a_u8[i]);
}
size_t s8_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "0x%02hhx", val->a_s8[i]);
}
size_t u16_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%hd", val->a_u16[i]);
}
size_t s16_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%hd", val->a_s16[i]);
}
size_t u32_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%u", val->a_u32[i]);
}
size_t s32_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%d", val->a_s32[i]);
}
size_t u64_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%"PRIu64, val->a_u64[i]);
}
size_t s64_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%"PRId64, val->a_s64[i]);
}
size_t f32_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%f", val->a_f[i]);
}
size_t d64_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%lf", val->a_d[i]);
}


typedef size_t (*value_printer)(char *buf, size_t rem, ldms_mval_t val, int i);
static value_printer printers[] = {
	[LDMS_V_U8_ARRAY] = u8_printer,
	[LDMS_V_S8_ARRAY] = s8_printer,
	[LDMS_V_U16_ARRAY] = u16_printer,
	[LDMS_V_S16_ARRAY] = s16_printer,
	[LDMS_V_U32_ARRAY] = u32_printer,
	[LDMS_V_S32_ARRAY] = s32_printer,
	[LDMS_V_U64_ARRAY] = u64_printer,
	[LDMS_V_S64_ARRAY] = s64_printer,
	[LDMS_V_F32_ARRAY] = f32_printer,
	[LDMS_V_D64_ARRAY] = d64_printer,
};

char *json_value_printer(amqp_inst_t ai, ldms_set_t s, int idx)
{
	enum ldms_value_type type;
	ldms_mval_t val;
	int n, i;
	size_t cnt, remaining;

	if (!ai->value_buf) {
		ai->value_buf = malloc(DEF_VALUE_BUF_LEN);
		if (!ai->value_buf)
			return NULL;
		ai->value_buf_len = DEF_VALUE_BUF_LEN;
	}
 retry:
	type = ldms_metric_type_get(s, idx);
	n = ldms_metric_array_get_len(s, idx);
	val = ldms_metric_get(s, idx);
	remaining = ai->value_buf_len;

	switch (type) {
	case LDMS_V_CHAR_ARRAY:
		sprintf(ai->value_buf, "\"%s\"", val->a_char);
		break;
	case LDMS_V_CHAR:
		sprintf(ai->value_buf, "'%c'", val->v_char);
		break;
	case LDMS_V_U8:
		sprintf(ai->value_buf, "%hhu", val->v_u8);
		break;
	case LDMS_V_S8:
		sprintf(ai->value_buf, "%hhd", val->v_s8);
		break;
	case LDMS_V_U16:
		sprintf(ai->value_buf, "%hu", val->v_u16);
		break;
	case LDMS_V_S16:
		sprintf(ai->value_buf, "%hd", val->v_s16);
		break;
	case LDMS_V_U32:
		sprintf(ai->value_buf, "%u", val->v_u32);
		break;
	case LDMS_V_S32:
		sprintf(ai->value_buf, "%d", val->v_s32);
		break;
	case LDMS_V_U64:
		sprintf(ai->value_buf, "%"PRIu64, val->v_u64);
		break;
	case LDMS_V_S64:
		sprintf(ai->value_buf, "%"PRId64, val->v_s64);
		break;
	case LDMS_V_F32:
		sprintf(ai->value_buf, "%f", val->v_f);
		break;
	case LDMS_V_D64:
		sprintf(ai->value_buf, "%lf", val->v_d);
		break;
	default: /* arrays */
		remaining = ai->value_buf_len;
		cnt = snprintf(ai->value_buf, remaining, "[");
		remaining -= cnt;
		for (i = 0; i < n; i++) {
			if (i) {
				cnt = snprintf(&ai->value_buf[ai->value_buf_len - remaining], remaining, ",");
				remaining -= cnt;
			}
			assert(printers[type]);
			cnt = printers[type](&ai->value_buf[ai->value_buf_len - remaining], remaining, val, i);
			remaining -= cnt;
			if (remaining <= 0)
				break;
		}
		cnt = snprintf(&ai->value_buf[ai->value_buf_len - remaining], remaining, "]");
		remaining -= cnt;
		break;
	}
	if (remaining <= 0) {
		free(ai->value_buf);
		ai->value_buf = malloc(ai->value_buf_len * 2);
		ai->value_buf_len *= 2;
		if (ai->value_buf)
			goto retry;
	}
	if (ai->value_buf)
		return ai->value_buf;
	return "None";
}

static int
json_msg_formatter(amqp_inst_t ai, ldms_set_t s,
		   int *metric_array, size_t metric_count)
{
	struct ldms_timestamp timestamp;
	size_t cnt, remaining;
	char *msg;
	double t;
	int i;

	if (!ai->msg_buf) {
		if (!realloc_msg_buf(ai, DEF_MSG_BUF_LEN))
			return ENOMEM;
	}
	timestamp = ldms_transaction_timestamp_get(s);
 restart:
	remaining = ai->msg_buf_len;
	msg = ai->msg_buf;
	t = (double)timestamp.sec + (((double)timestamp.usec) / 1.0e6);
	cnt = snprintf(msg, remaining,
		       "{ instance_name : \"%s\", schema_name : \"%s\", "
		       "timestamp : %.6f, metrics : {\n",
		       ldms_set_instance_name_get(s),
		       ldms_set_schema_name_get(s),
		       t);
	remaining -= cnt;
	if (remaining <= 0)
		goto realloc;
	msg += cnt;
	for (i = 0; i < metric_count; i++) {
		cnt = snprintf(msg, remaining, "    \"%s\" : %s,\n",
			       ldms_metric_name_get(s, i),
			       json_value_printer(ai, s, metric_array[i]));
		remaining -= cnt;
		if (remaining <= 0)
			goto realloc;
		msg += cnt;
	}
	cnt = snprintf(msg, remaining, "    }\n}");
	remaining -= cnt;
	if (remaining <= 0)
		goto realloc;
	return ai->msg_buf_len - remaining;
 realloc:
	cnt = realloc_msg_buf(ai, ai->msg_buf_len << 1);
	if (cnt)
		goto restart;

	return 0;
}

static int
csv_msg_formatter(amqp_inst_t ai, ldms_set_t s,
		  int *metric_array, size_t metric_count)
{
	struct ldms_timestamp timestamp;
	size_t cnt, remaining;
	char *msg;
	double t;
	int i;

	if (!ai->msg_buf) {
		if (!realloc_msg_buf(ai, DEF_MSG_BUF_LEN))
			return ENOMEM;
	}
	timestamp = ldms_transaction_timestamp_get(s);
 restart:
	remaining = ai->msg_buf_len;
	msg = ai->msg_buf;
	t = (double)timestamp.sec + (((double)timestamp.usec) / 1.0e6);
	cnt = snprintf(msg, remaining,
		       "\"%s\",\"%s\",%.6f",
		       ldms_set_instance_name_get(s),
		       ldms_set_schema_name_get(s),
		       t);
	remaining -= cnt;
	if (remaining <= 0)
		goto realloc;
	msg += cnt;
	for (i = 0; i < metric_count; i++) {
		cnt = snprintf(msg, remaining, ",%s",
			       json_value_printer(ai, s, metric_array[i]));
		remaining -= cnt;
		if (remaining <= 0)
			goto realloc;
		msg += cnt;
	}
	return ai->msg_buf_len - remaining;
 realloc:
	cnt = realloc_msg_buf(ai, ai->msg_buf_len << 1);
	if (cnt)
		goto restart;

	return 0;
}

/*
 * The binary format is <1B:TYPE><1B:COUNT><xB:VALUE>
 *
 * E.g.
 *   01 01 63               Is the character 'c'
 *   01 05 68 65 6c 6c 6f   Is the character string "hello"
 */
static int
bin_msg_formatter(amqp_inst_t ai, ldms_set_t s,
		  int *metric_array, size_t metric_count)
{
	struct ldms_timestamp timestamp;
	size_t bytes = ldms_set_data_sz_get(s) + sizeof(double);
	int i;
	double t;
	size_t remaining, cnt;

	if (!ai->msg_buf) {
		if (!realloc_msg_buf(ai, bytes))
			return ENOMEM;
	}
	timestamp = ldms_transaction_timestamp_get(s);
 restart:
	remaining = ai->msg_buf_len;
	t = (double)timestamp.sec + (((double)timestamp.usec) / 1.0e6);
	memcpy(ai->msg_buf, &t, sizeof(double));
	remaining -= sizeof(double);
	if (remaining <= 0)
		goto realloc;

	for (i = 0; i < metric_count; i++) {
		ldms_mval_t val = ldms_metric_get(s, metric_array[i]);
		char *msg = &ai->msg_buf[ai->msg_buf_len - remaining];
		enum ldms_value_type type = ldms_metric_type_get(s, i);
		cnt = 2;
		remaining -= cnt;
		if (remaining <= 0)
			goto realloc;
		msg[0] = type;
		switch (type) {
		case LDMS_V_CHAR:
		case LDMS_V_U8:
		case LDMS_V_S8:
			cnt = sizeof(uint8_t);
			msg[1] = 1;
			memcpy(&msg[2], &val->v_u8, cnt);
			break;
		case LDMS_V_U16:
		case LDMS_V_S16:
			cnt = sizeof(uint16_t);
			msg[1] = 1;
			memcpy(&msg[2], &val->v_u16, cnt);
			break;
		case LDMS_V_U32:
		case LDMS_V_S32:
		case LDMS_V_F32:
			cnt = sizeof(uint32_t);
			msg[1] = 1;
			memcpy(&msg[2], &val->v_u32, cnt);
			break;
		case LDMS_V_U64:
		case LDMS_V_S64:
		case LDMS_V_D64:
			cnt = sizeof(uint64_t);
			msg[1] = 1;
			memcpy(&msg[2], &val->v_u64, cnt);
			break;
		case LDMS_V_CHAR_ARRAY:
		case LDMS_V_U8_ARRAY:
		case LDMS_V_S8_ARRAY:
			cnt = ldms_metric_array_get_len(s, i);
			msg[1] = cnt;
			memcpy(&msg[2], val->a_char, cnt);
			break;
		case LDMS_V_U16_ARRAY:
		case LDMS_V_S16_ARRAY:
			cnt = ldms_metric_array_get_len(s, i);
			msg[1] = cnt;
			cnt *= sizeof(uint16_t);
			memcpy(&msg[2], val->a_u16, cnt);
			break;
		case LDMS_V_U32_ARRAY:
		case LDMS_V_S32_ARRAY:
		case LDMS_V_F32_ARRAY:
			cnt = ldms_metric_array_get_len(s, i);
			msg[1] = cnt;
			cnt *= sizeof(uint32_t);
			memcpy(&msg[2], val->a_u32, cnt);
			break;
		case LDMS_V_U64_ARRAY:
		case LDMS_V_S64_ARRAY:
		case LDMS_V_D64_ARRAY:
			cnt = ldms_metric_array_get_len(s, i);
			msg[1] = cnt;
			cnt *= sizeof(uint64_t);
			memcpy(&msg[2], val->a_u64, cnt);
			break;
		default:
			assert(0 == "Invalid metric type");
		}
		remaining -= cnt;
		if (remaining <= 0)
			goto realloc;
	}
	return ai->msg_buf_len - remaining;
 realloc:
	cnt = realloc_msg_buf(ai, ai->msg_buf_len << 1);
	if (cnt)
		goto restart;

	return 0;
}

static amqp_msg_formatter_t formatters[] = {
	[JSON_FMT] = json_msg_formatter,
	[CSV_FMT] = csv_msg_formatter,
	[BIN_FMT] = bin_msg_formatter,
};

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

static struct ldmsd_store ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_STORE,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.open = open_store,
	.store = store,
	.close = close_store,
};

static void __attribute__ ((constructor)) store_amqp_init();
static void store_amqp_init()
{
	pthread_mutex_init(&cfg_lock, NULL);
	rbt_init(&amqp_rbt, comparator);
}

static void __attribute__ ((destructor)) store_amqp_fini(void);
static void store_amqp_fini()
{
	pthread_mutex_destroy(&cfg_lock);
}
