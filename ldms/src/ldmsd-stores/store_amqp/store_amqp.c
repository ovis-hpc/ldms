/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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

/**
 * \file store_amqp.c
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

#include "ldmsd.h"
#include "ldmsd_store.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

enum amqp_formatter_type {
	BIN_FMT,
	JSON_FMT,
	CSV_FMT,
};

typedef struct store_amqp_inst_s *store_amqp_inst_t;
typedef int (*amqp_msg_formatter_t)(store_amqp_inst_t, ldms_set_t,
				    ldmsd_strgp_t);

static int json_msg_formatter(store_amqp_inst_t, ldms_set_t, ldmsd_strgp_t);
static int csv_msg_formatter(store_amqp_inst_t, ldms_set_t, ldmsd_strgp_t);
static int bin_msg_formatter(store_amqp_inst_t, ldms_set_t, ldmsd_strgp_t);

static amqp_msg_formatter_t formatters[] = {
	[JSON_FMT] = json_msg_formatter,
	[CSV_FMT] = csv_msg_formatter,
	[BIN_FMT] = bin_msg_formatter,
};

struct store_amqp_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */

	amqp_msg_formatter_t formatter;
	char *container;
	char *exchange;		/* AMQP exchange  */
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

static size_t realloc_msg_buf(store_amqp_inst_t inst, size_t buf_len)
{
	if (inst->msg_buf)
		free(inst->msg_buf);
	inst->msg_buf_len = buf_len;
	inst->msg_buf = malloc(inst->msg_buf_len);
	if (inst->msg_buf)
		return inst->msg_buf_len;
	else
		INST_LOG(inst, LDMSD_LERROR,
			 "OOM error allocating %zd bytes for message buffer.\n",
			 buf_len);
	return 0;
}

static int setup_certs(store_amqp_inst_t inst, const char *cacert, json_entity_t json)
{
	int rc;
	json_entity_t key, cert;

	rc = amqp_ssl_socket_set_cacert(inst->socket, cacert);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR,
			 "Error %d setting the CA cert file.\n", rc);
		goto out;
	}
	key = json_value_find(json, "key");
	cert = json_value_find(json, "cert");
	if ((key && !cert) || (cert && !key)) {
		rc = EINVAL;
		INST_LOG(inst, LDMSD_LERROR,
			 "The key .PEM and cert.PEM files must"
			 "be specified together.\n");
		goto out;
	}
	if (cert && key) {
		if (cert->type != JSON_STRING_VALUE) {
			ldmsd_log(LDMSD_LERROR, "%s: The given 'cert' value is "
					"not a string.\n", inst->base.inst_name);
			goto out;
		}
		if (key->type != JSON_STRING_VALUE) {
			ldmsd_log(LDMSD_LERROR, "%s: The given 'key' value is "
					"not a string.\n", inst->base.inst_name);
			goto out;
		}
		rc = amqp_ssl_socket_set_key(inst->socket,
						json_value_str(cert)->str,
						json_value_str(key)->str);
		if (rc) {
			INST_LOG(inst, LDMSD_LERROR,
				 "Error %d setting key and cert files.\n", rc);
			goto out;
		}
	}

 out:
	return rc;
}

static int check_reply(store_amqp_inst_t inst, amqp_rpc_reply_t r,
		       const char *file, int line)
{
	switch (r.reply_type) {
	case AMQP_RESPONSE_NORMAL:
		return 0;
	case AMQP_RESPONSE_NONE:
		INST_LOG(inst, LDMSD_LERROR,
			 "AMQP protocol error; missing RPC reply type\n");
		break;
	case AMQP_RESPONSE_LIBRARY_EXCEPTION:
		INST_LOG(inst, LDMSD_LERROR, "%s\n",
			 amqp_error_string2(r.library_error));
		break;
	case AMQP_RESPONSE_SERVER_EXCEPTION:
		switch (r.reply.id) {
		case AMQP_CONNECTION_CLOSE_METHOD: {
			amqp_connection_close_t *m =
				(amqp_connection_close_t *) r.reply.decoded;
			INST_LOG(inst, LDMSD_LERROR,
				 "server connection error %d, message: %.*s\n",
				 m->reply_code, (int)m->reply_text.len,
				 m->reply_text.bytes);
			break;
		}
		case AMQP_CHANNEL_CLOSE_METHOD: {
			amqp_channel_close_t *m = (amqp_channel_close_t *) r.reply.decoded;
			INST_LOG(inst, LDMSD_LERROR,
				 "server channel error %d, message: %.*s\n",
				 m->reply_code, (int)m->reply_text.len,
				 m->reply_text.bytes);
			break;
		}
		default:
			INST_LOG(inst, LDMSD_LERROR,
				 "unknown server error, method id 0x%08X\n",
				 r.reply.id);
			break;
		}
		break;
	}
	return -1;
}
#define CHECK_REPLY(inst, _qrc_) check_reply(inst, _qrc_, __FILE__, __LINE__)

#define DEF_MSG_BUF_LEN (128 * 1024) /* Should avoid realloc() for most sets */
#define DEF_AMQP_SSL_PORT 5671
#define DEF_AMQP_TCP_PORT 5672

/* ============== Store Plugin APIs ================= */

int store_amqp_open(ldmsd_plugin_inst_t pi, ldmsd_strgp_t strgp)
{
	/* Perform `open` operation */
	store_amqp_inst_t inst = (void*)pi;

	amqp_rpc_reply_t qrc;

	if (!inst->exchange) {
		size_t sz = strlen(strgp->schema) + sizeof("LDMS.set.") + 1;
		inst->exchange = malloc(sz);
		if (!inst->exchange)
			goto err_0;
		sprintf(inst->exchange, "LDMS.set.%s", strgp->schema);
	}
	/* Create the LDMS.json exchange */
	amqp_exchange_declare(inst->conn, inst->channel,
			      amqp_cstring_bytes(inst->exchange),
			      amqp_cstring_bytes("direct"),
			      0, 0,
			      #if AMQP_VERSION >= 0x00060001
			      0, 0,
			      #endif
			      amqp_empty_table);
	/* NOTE1: AMQP_VERSION is uint32_t with the following format:
	 * hibyte: MAJOR(1B) MINOR(1B) PATCH(1B) IS_RELEASE(1B) :lowbyte
	 *
	 * NOTE2: AMQP_VERSION_CODE() was not available until v0.6.1, so we
	 * cannot rely on it.
	 *
	 * NOTE3: librabbitmq changes amqp_exchange_declare() API since v0.6.0
	 * from:
	 *     amqp_exchange_declare(amqp_connection_state_t state,
	 *         amqp_channel_t channel, amqp_bytes_t exchange, amqp_bytes_t
	 *         type, amqp_boolean_t passive, amqp_boolean_t durable,
	 *         amqp_table_t arguments);
	 * to:
	 *     amqp_exchange_declare(amqp_connection_state_t state,
	 *         amqp_channel_t channel, amqp_bytes_t exchange, amqp_bytes_t
	 *         type, amqp_boolean_t passive, amqp_boolean_t durable,
	 *         amqp_boolean_t auto_delete, amqp_boolean_t internal,
	 *         amqp_table_t arguments);
	 * adding `auto_delete` and `internal` parameters right before the last
	 * parameter `arguments`.
	 */
	qrc = amqp_get_rpc_reply(inst->conn);
	if (CHECK_REPLY(inst, qrc) < 0)
		goto err_0;
	return 0;
 err_0:
	return -1;
}

int store_amqp_close(ldmsd_plugin_inst_t pi)
{
	/* Perform `close` operation */
	store_amqp_inst_t inst = (void*)pi;
	amqp_channel_close(inst->conn, inst->channel, AMQP_REPLY_SUCCESS);
	amqp_connection_close(inst->conn, AMQP_REPLY_SUCCESS);
	amqp_destroy_connection(inst->conn);
	return 0;
}

int store_amqp_flush(ldmsd_plugin_inst_t pi)
{
	/* do nothing */
	return 0;
}

int store_amqp_store(ldmsd_plugin_inst_t pi, ldms_set_t set, ldmsd_strgp_t strgp)
{
	/* `store` data from `set` into the store */
	store_amqp_inst_t inst = (void*)pi;
	amqp_basic_properties_t props;
	size_t msg_len;
	int rc;
	amqp_bytes_t msg_bytes;

	props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG |
		AMQP_BASIC_DELIVERY_MODE_FLAG |
		AMQP_BASIC_TIMESTAMP_FLAG;
	props.content_type = amqp_cstring_bytes("text/json");
	props.delivery_mode = 1;
	props.timestamp = (uint64_t)time(NULL);

	pthread_mutex_lock(&inst->lock);
	msg_len = inst->formatter(inst, set, strgp);
	msg_bytes.len = msg_len;
	msg_bytes.bytes = inst->msg_buf;
	rc = amqp_basic_publish(inst->conn, inst->channel,
				amqp_cstring_bytes(inst->exchange),
				amqp_cstring_bytes(inst->routing_key),
				0, 0, &props, msg_bytes);
	pthread_mutex_unlock(&inst->lock);
	return rc;
}

/* ============== Common Plugin APIs ================= */

static
const char *store_amqp_desc(ldmsd_plugin_inst_t pi)
{
	return "store_amqp - store_amqp store plugin";
}

static
char *_help = "\
Synopsis:\n\
    config name=<INST> [COMMON_OPTIONS] host=<hostname> [exchange=<name>]\n\
                       [port=<port_no>] [vhost=<host>] [cacert=<path>]\n\
                       [key=<path>] [user=<name>] [pwd=<password>]\n\
\n\
Required key/values\n\
    host=<hostname>    The DNS hostname or IP address of the AMQP server.\n\
\n\
 Optional key/values:\n\
    exchange=<name>    Unique name of the AMQP exchange, \n\
                       defaults to LDMS.set.$schema$\n\
    port=<port_no>     The port number of the AMQP server, defaults to 5731.\n\
    vhost=<host>       Virtual host, defaults to '/'\n\
    cacert=<path>      Path to the CA certificate file in PEM format. \n\
                       If specified, key and cert must also be specified.\n\
    key=<path>         Path to the client key in PEM format. If specified \n\
                       cacert and cert must also be specified.\n\
    cert=<path>        Path to the client cert in PEM format. If specified, \n\
                       cacert and key must also be specified.\n\
    user=<name>        The SASL user name, default is 'guest'\n\
    pwd=<password>     The SASL password, default is 'guest'\n\
";

static
const char *store_amqp_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static void store_amqp_cleanup(store_amqp_inst_t inst);

static
int store_amqp_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	store_amqp_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;
	int rc;

	json_entity_t value;
	amqp_rpc_reply_t qrc;
	char *value_s, *attr_name;

	rc = store->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	inst->formatter = formatters[JSON_FMT];
	inst->routing_key = "JSON";
	value = json_value_find(json, "format");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			attr_name = "format";
			goto einval_not_string;
		}
		value_s = json_value_str(value);
		if (0 == strcasecmp(value_s, "csv")) {
			inst->formatter = formatters[CSV_FMT];
			inst->routing_key = "CSV";
		} else if (0 == strcasecmp(value_s, "binary")) {
			inst->formatter = formatters[BIN_FMT];
			inst->routing_key = "BIN";
		} else if (0 != strcasecmp(value_s, "json")) {
			INST_LOG(inst, LDMSD_LINFO,
				 "Invalid formatter '%s' specified, "
				 "defaulting to JSON.\n", value);
		}
	}
	value = json_value_find(json, "host");
	if (!value) {
		snprintf(ebuf, ebufsz,
			 "The host parameter must be specified.\n");
		rc = EINVAL;
		goto err_1;
	}
	if (value->type != JSON_STRING_VALUE) {
		attr_name = "host";
		goto einval_not_string;
	}
	inst->host = strdup(json_value_str(value)->str);

	if (!inst->host) {
		snprintf(ebuf, ebufsz, "OOM error copying host name.\n");
		rc = ENOMEM;
		goto err_1;
	}
	value = json_value_find(json, "exchange");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			attr_name = "exchange";
			goto einval_not_string;
		}
		inst->exchange = strdup(json_value_str(value)->str);
		if (!inst->exchange) {
			snprintf(ebuf, ebufsz,
				 "OOM error copying queue name.\n");
			rc = ENOMEM;
			goto err_1;
		}
	}
	value = json_value_find(json, "port");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			attr_name = "port";
			goto einval_not_string;
		}
		long sl;
		sl = strtol(json_value_str(value)->str, NULL, 0);
		if (sl < 1 || sl > USHRT_MAX) {
			snprintf(ebuf, ebufsz,
				 "Invalid port number %s.\n",
				 json_value_str(value)->str);
			goto err_1;
		}
		inst->port = sl;
	} else {
		inst->port = DEF_AMQP_TCP_PORT;
	}
	value = json_value_find(json, "vhost");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			attr_name = "vhost";
			goto einval_not_string;
		}
		inst->vhost = strdup(json_value_str(value)->str);
	} else {
		inst->vhost = strdup("/");
	}
	if (!inst->vhost) {
		snprintf(ebuf, ebufsz,
			 "OOM error copying vhost.\n");
		rc = ENOMEM;
		goto err_1;
	}
	value = json_value_find(json, "user");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			attr_name = "user";
			goto einval_not_string;
		}
		inst->user = strdup(json_value_str(value)->str);
	} else {
		inst->user = strdup("guest");
	}
	if (!inst->user) {
		snprintf(ebuf, ebufsz, "OOM error copying user name.\n");
		rc = ENOMEM;
		goto err_1;
	}
	value = json_value_find(json, "pwd");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			attr_name = "pwd";
			goto einval_not_string;
		}
		inst->pwd = strdup(json_value_str(value)->str);
	} else {
		inst->pwd = strdup("guest");
	}
	if (!inst->pwd) {
		snprintf(ebuf, ebufsz, "OOM error copying password.\n");
		rc = ENOMEM;
		goto err_1;
	}

	/* Create the connection state */
	inst->conn = amqp_new_connection();
	if (!inst->conn) {
		rc = (errno ? errno : ENOMEM);
		INST_LOG(inst, LDMSD_LERROR,
			 "%s: Error %d creating the AMQP connection state.\n",
			 __FILE__, rc);
		snprintf(ebuf, ebufsz,
			 "Error %d creating the AMQP connection state.\n", rc);
		goto err_1;
	}
	/* Create the socket */
	inst->socket = amqp_tcp_socket_new(inst->conn);
	if (!inst->socket) {
		rc = (errno ? errno : ENOMEM);
		INST_LOG(inst, LDMSD_LERROR,
			 "%s: Error %d creating the AMQP socket.\n",
			 __FILE__, rc);
		snprintf(ebuf, ebufsz,
			 "Error %d creating the AMQP socket.\n", rc);
		goto err_2;
	}
	value = json_value_find(json, "cacert");
	if (value) {
		if (value->type != JSON_STRING_VALUE) {
			attr_name = "cacert";
			goto einval_not_string;
		}
		rc = setup_certs(inst, json_value_str(value)->str, json);
		if (rc)
			goto err_3;
	}
	rc = amqp_socket_open(inst->socket, inst->host, inst->port);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR,
			 "%s: Error %d opening the AMQP socket.\n",
			 __FILE__, rc);
		snprintf(ebuf, ebufsz,
			 "Error %d opening the AMQP socket.\n", rc);
		goto err_2;
	}
	qrc = amqp_login(inst->conn, inst->vhost, 0, AMQP_DEFAULT_FRAME_SIZE, 0,
			 AMQP_SASL_METHOD_PLAIN, inst->user, inst->pwd);
	if (CHECK_REPLY(inst, qrc) < 0)
		goto err_3;
	inst->channel = 1;
	amqp_channel_open(inst->conn, inst->channel);
	qrc = amqp_get_rpc_reply(inst->conn);
	if (CHECK_REPLY(inst, qrc) < 0)
		goto err_3;

	return 0;
 err_3:
	amqp_channel_close(inst->conn, inst->channel, AMQP_REPLY_SUCCESS);
	amqp_connection_close(inst->conn, AMQP_REPLY_SUCCESS);
 err_2:
	amqp_destroy_connection(inst->conn);
 err_1:
	store_amqp_cleanup(inst);
	return rc;

 einval_not_string:
	ldmsd_log(LDMSD_LERROR, "%s: The given '%s' value is not a string.\n",
 					inst->base.inst_name, attr_name);
 	return EINVAL;
}

#define _FREE(x) do { \
		if (x) { \
			free(x); \
			x = NULL; \
		} \
	} while(0)

static void store_amqp_cleanup(store_amqp_inst_t inst)
{
	_FREE(inst->container);
	_FREE(inst->msg_buf);
	_FREE(inst->exchange);
	_FREE(inst->host);
	_FREE(inst->vhost);
	_FREE(inst->user);
	_FREE(inst->pwd);
}

static
void store_amqp_del(ldmsd_plugin_inst_t pi)
{
	store_amqp_inst_t inst = (void*)pi;

	/* The undo of store_amqp_init and instance cleanup */
	store_amqp_cleanup(inst);
}

static
int store_amqp_init(ldmsd_plugin_inst_t pi)
{
	store_amqp_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;

	/* override store operations */
	store->open = store_amqp_open;
	store->close = store_amqp_close;
	store->flush = store_amqp_flush;
	store->store = store_amqp_store;

	/* NOTE More initialization code here if needed */
	return 0;
}


/* ======== UTILITIES ======== */

#define DEF_VALUE_BUF_LEN 1024

size_t u8_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%hhu", val->a_u8[i]);
}
size_t s8_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%hhd", val->a_s8[i]);
}
size_t u16_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%hd", __le16_to_cpu(val->a_u16[i]));
}
size_t s16_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%hd", __le16_to_cpu(val->a_s16[i]));
}
size_t u32_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%u", __le32_to_cpu(val->a_u32[i]));
}
size_t s32_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%d", __le32_to_cpu(val->a_s32[i]));
}
size_t u64_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%"PRIu64, (uint64_t)__le64_to_cpu(val->a_u64[i]));
}
size_t s64_printer(char *buf, size_t rem, ldms_mval_t val, int i)
{
	return snprintf(buf, rem, "%"PRId64, (uint64_t)__le64_to_cpu(val->a_s64[i]));
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

char *json_value_printer(store_amqp_inst_t ai, ldms_set_t s, int idx)
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
		sprintf(ai->value_buf, "\"%c\"", val->v_char);
		break;
	case LDMS_V_U8:
		sprintf(ai->value_buf, "%hhu", val->v_u8);
		break;
	case LDMS_V_S8:
		sprintf(ai->value_buf, "%hhd", val->v_s8);
		break;
	case LDMS_V_U16:
		sprintf(ai->value_buf, "%hu", __le16_to_cpu(val->v_u16));
		break;
	case LDMS_V_S16:
		sprintf(ai->value_buf, "%hd", __le16_to_cpu(val->v_s16));
		break;
	case LDMS_V_U32:
		sprintf(ai->value_buf, "%u", __le32_to_cpu(val->v_u32));
		break;
	case LDMS_V_S32:
		sprintf(ai->value_buf, "%d", __le32_to_cpu(val->v_s32));
		break;
	case LDMS_V_U64:
		sprintf(ai->value_buf, "%"PRIu64, (uint64_t)__le64_to_cpu(val->v_u64));
		break;
	case LDMS_V_S64:
		sprintf(ai->value_buf, "%"PRId64, (int64_t)__le64_to_cpu(val->v_s64));
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
json_msg_formatter(store_amqp_inst_t ai, ldms_set_t s, ldmsd_strgp_t strgp)
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
		       "{\n"
		       "  \"instance_name\" : \"%s\",\n"
		       "  \"schema_name\" : \"%s\",\n"
		       "  \"timestamp\" : %.6f,\n"
		       "  \"metrics\" : {\n",
		       ldms_set_instance_name_get(s),
		       ldms_set_schema_name_get(s),
		       t);
	remaining -= cnt;
	if (remaining <= 0)
		goto realloc;
	msg += cnt;
	for (i = 0; i < strgp->metric_count; i++) {
		const char *fmt = i?"   ,\"%s\" : %s\n":
				    "    \"%s\" : %s\n";
		cnt = snprintf(msg, remaining, fmt,
			       ldms_metric_name_get(s, i),
			       json_value_printer(ai, s,
					          strgp->metric_arry[i]));
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
csv_msg_formatter(store_amqp_inst_t ai, ldms_set_t s, ldmsd_strgp_t strgp)
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
	for (i = 0; i < strgp->metric_count; i++) {
		cnt = snprintf(msg, remaining, ",%s",
			       json_value_printer(ai, s,
					          strgp->metric_arry[i]));
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
bin_msg_formatter(store_amqp_inst_t ai, ldms_set_t s, ldmsd_strgp_t strgp)
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

	for (i = 0; i < strgp->metric_count; i++) {
		ldms_mval_t val = ldms_metric_get(s, strgp->metric_arry[i]);
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

/* ======== new() ======== */
static
struct store_amqp_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "store",
		.plugin_name = "store_amqp",

                /* Common Plugin APIs */
		.desc   = store_amqp_desc,
		.help   = store_amqp_help,
		.init   = store_amqp_init,
		.del    = store_amqp_del,
		.config = store_amqp_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	store_amqp_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
