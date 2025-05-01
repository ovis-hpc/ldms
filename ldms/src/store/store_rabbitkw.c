/**
 * Copyright (c) 2010-2018 National Technology & Engineering Solutions of Sandia,
 * LLC (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the
 * U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2018 Open Grid Computing, Inc. All rights reserved.
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

#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <linux/limits.h>
#include <pthread.h>
#include <errno.h>
#include <coll/idx.h>
#include "ldms.h"
#include "ldmsd.h"
#if OVIS_LDMS_HAVE_AUTH
#include "ovis_auth/auth.h"
#endif /* OVIS_LDMS_HAVE_AUTH */
#include <amqp_tcp_socket.h>
#include <amqp.h>
#include <amqp_framing.h>
#include "rabbit_utils.h"
#include "ovis_util/big_dstring.h"
BIG_DSTRING_TYPE(65536);
#define TIME_MAX INT_MAX /* revisit this in 2037 */

/*
 * This store answers the design case of coarsely routed key/value data
 * via amqp (rabbitmq) to a schema-less backend.
 * It uses a single channel to route all messages.
 * Container and schema name are coded in key/value data.
 * character arrays with embedded nuls are not fully supported
 * (truncated at first nul).
 *
 * The encoding for generic unknown consumers uses the amqp basic properties
 * as follows (note that ldms has no notion of talking back to a sampler, so
 * some are repurposed for our application):
 *   timestamp : timestamp of sample at origin: ***microseconds*** since the epoch.
 *   app_id : ldms
 *
 * See also amqp_framing.h.
 *
 * Requirements met:
 * + Encode data as ASCII tab separated key=type%value pairs.
 * + Data and metadata not distinguished.
 * + Data and header are not separate.
 * + metric data is formatted as tuples repeating metric=x type=t value=v.
 */
/*
 * NOTE: routing key (limit 255 char)
 *   (rabbitkw::path) = <user specified>
 */

#define STOR "store_rabbitkw"
static idx_t store_idx;
static int store_count = 0;
#define dsinit(ds) \
	big_dstring_t ds; \
	bdstr_init(&ds)

#define dscat(ds,char_star_x) \
	bdstrcat(&ds, char_star_x, DSTRING_ALL)

#define dsdone(ds) \
	bdstr_extract(&ds)

static ovis_log_t mylog;

static bool g_extraprops = true; /**< parse value during config */
static char *routing_key_path; /**< store routing_key path */
static char *host_name; /**< store hostname */
static char *vhost; /**< broker vhost name */
static char *pwfile; /**< broker secretword file */
static bool doserver = true; /** bypass server calls if false */
static bool logmsg = false;
static char *rmq_exchange; /**< store queue */
amqp_bytes_t rmq_exchange_bytes; /**< store queue in amqp form */
#define SHORTSTR_MAX 255 /* from amqp 091 spec */

#define PWSIZE 256
#define HEARTBEAT_DEFAULT_SEC 10
#define TIMEOUT_DEFAULT_SEC 10
#define TIMEOUT_DEFAULT_USEC 0
#define RETRY_DEFAULT 60
/* The rabbit amqp implementation is not thread safe. lock around this.
 * for now, the configuration lock is sufficient. */
static struct amqp_connection_state {
	amqp_connection_state_t conn;
	int amqp_port;
	int retry_sec; /** seconds before retrying to connect to amqp server */
	int broker_failed; /** Connecting to broker failed or failure in messaging */
	int heartbeat; /** how often to check server heartbeat, but not checked between messages */
	time_t next_connect; /** Time after which to try again connecting. */
	struct timeval timeout;
	char user[PWSIZE];
	char pw[PWSIZE];
} as = { NULL, 0, RETRY_DEFAULT, 0, HEARTBEAT_DEFAULT_SEC, 0};

/* valid to use only when config completes successfully. */
static
int log_amqp(const char *msg)
{
	if ( !doserver)
		return 0;
	amqp_bytes_t message_bytes = amqp_cstring_bytes(msg);
	return amqp_basic_publish(as.conn, 1,
	                          rmq_exchange_bytes,
	                          amqp_cstring_bytes("ldms:store_rabbitkw"),
	                          0,
	                          0,
	                          NULL,
	                          message_bytes);
}

/* workaround until issue134 is merged. dump this function then in favor of olerr. */
void rabbit_lerror_tmp(const char *fmt, ...)
{
	ovis_log(mylog, OVIS_LERROR, STOR ": %s",fmt);
}

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)
#define NUM_CUSTOM_PROPS 2

/* group of metric support data.  */
struct rabbitkw_store_instance {
	struct ldmsd_store *store;
	char *container;
	char *schema;
	char *key; /* 'container schema' */
	bool conflict_warned;
	bool extraprops; /* include ldmsd (not sampler) metadata in basic message headers */
	/* was ms */
	char *routingkey; /**< routing_key.container.schema.metric.producer */
	amqp_bytes_t routingkey_bytes; /**< store routingkey in amqp form */

	amqp_basic_properties_t props;
	amqp_table_entry_t myprops[NUM_CUSTOM_PROPS]; /**< names for least, amqp. */
	amqp_table_t mypropstab;
	big_dstring_t message;
};

static pthread_mutex_t cfg_lock;
/*#define DEBUG 1 */
/* Make sure the strings passed in do not disappear before props is */
/* out of use or they are literals. */
/* Take care of specific fields and bit flags. */
static void init_props(bool extraprops, amqp_basic_properties_t *props,	amqp_table_t headers, struct rabbitkw_store_instance *si)
{
	if (props) {
		memset(props,1,sizeof(*props));
	} else {
		return;
	}
	props->delivery_mode = 1;
	props->_flags = 0;
	props->_flags |= AMQP_BASIC_TIMESTAMP_FLAG;
#define APPID "LDMS"
	props->app_id = amqp_cstring_bytes(APPID);
	props->_flags |= AMQP_BASIC_APP_ID_FLAG;
	if (extraprops) {
		props->_flags |= AMQP_BASIC_CONTENT_TYPE_FLAG;
		props->content_type = amqp_cstring_bytes("text/plain");

		props->reply_to = amqp_cstring_bytes("$instance");
		props->_flags |= AMQP_BASIC_REPLY_TO_FLAG;

		props->headers = headers;
		props->_flags |= AMQP_BASIC_HEADERS_FLAG;
	}
}

/* update fields varying per set instanced handled. */
static void update_props(amqp_basic_properties_t *props, const char *instance, struct rabbitkw_store_instance *si)
{
	if (si->extraprops)
		props->reply_to = amqp_cstring_bytes(instance);
}

static int cleaned=0;
void cleanup_amqp()
{
	pthread_mutex_lock(&cfg_lock);
	if (!as.conn)
		goto out;
	if (cleaned)
		goto out;
	cleaned = 1;
	if (doserver) {
		int s;
		log_amqp(STOR ": stopping ldms amqp");
		s = lrmq_die_on_amqp_error(
			amqp_channel_close(as.conn, 1, AMQP_REPLY_SUCCESS),
			"Closing channel");
		if (s) {
			ovis_log(mylog, OVIS_LDEBUG, STOR ": skipping connection close.\n");
			goto out;
		}
		s = lrmq_die_on_amqp_error(
			amqp_connection_close(as.conn, AMQP_REPLY_SUCCESS),
			"Closing connection");
		if (s) {
			ovis_log(mylog, OVIS_LDEBUG, STOR ": skipping connection destroy.\n");
			goto out;
		}
		s = lrmq_die_on_error(amqp_destroy_connection(as.conn), "Ending connection");
	}
out:
	pthread_mutex_unlock(&cfg_lock);

}

static void reset_connection()
{
	if (as.conn)
		amqp_destroy_connection(as.conn);
	as.conn = NULL;
	as.broker_failed++;
	as.next_connect = 0;
	ovis_log(mylog, OVIS_LINFO, STOR ": reset_connection.\n");
}
/*
 * Try to make connection. (log 1st and every 1000th fail ).
 * \return 0, EINVAL, ENOMEM, EKEYREJECTED, EAGAIN. Connection is ready if 0.
 * Retry is possible if EAGAIN is returned.
 */
static int make_connection()
{
	const int warn_every = 1000; /* issue only every nth attempt warning */
	if (as.amqp_port == 0) {
		ovis_log(mylog, OVIS_LERROR, STOR ": config must be done before creating a store.\n");
		return EINVAL;
	}
	if (as.conn)
		return 0;
	int retry = 1;
	time_t now = time(&now);
	if (as.next_connect) {
		/* next_connect != 0 only if there has been a prior uncleared failure */
		if (now == (time_t)-1) {
			/* wacky clock. retry always */
		} else {
			if (now >= as.next_connect) {
				as.next_connect += as.retry_sec;
			} else {
				retry = 0;
			}
		}
	}

	if (!retry)
		return EAGAIN;
	if (as.broker_failed && as.broker_failed % warn_every == 1) {
		ovis_log(mylog, OVIS_LINFO, STOR ": connect amqp retry number %d\n",
			as.broker_failed);
	}
	int rc;
	as.conn = amqp_new_connection();
	if (!as.conn ) {
		ovis_log(mylog, OVIS_LERROR, STOR ": amqp_new_connection failed.\n");
		rc = ENOMEM;
		as.broker_failed++;
		goto fail;
	} else {
		ovis_log(mylog, OVIS_LDEBUG, STOR ": amqp_new_connection ok.\n");
	}
	amqp_socket_t *asocket = NULL;
	asocket = amqp_tcp_socket_new(as.conn); /* stores asocket in conn, fyi. */
	if (!asocket) {
		ovis_log(mylog, OVIS_LERROR,
			STOR ": config: amqp_tcp_socket_new failed.\n");
		as.broker_failed++;
		rc = ENOMEM;
		goto fail;
	} else {
		ovis_log(mylog, OVIS_LDEBUG, STOR ": amqp_tcp_socket_new ok.\n");
	}
	if (doserver) {
		int status = amqp_socket_open_noblock(asocket, host_name, as.amqp_port, &as.timeout);
		if (status) {
			if (!as.broker_failed ||
				as.broker_failed % warn_every == 1) {
				ovis_log(mylog, OVIS_LERROR,
					STOR ": config: amqp_socket_open failed. %s\n",
					amqp_error_string2(status));
			}
			as.broker_failed++;
			rc = EAGAIN;
			goto again;
		}
		status = lrmq_die_on_amqp_error(amqp_login(as.conn,
				vhost,
				AMQP_DEFAULT_MAX_CHANNELS,
				AMQP_DEFAULT_FRAME_SIZE, as.heartbeat,
				AMQP_SASL_METHOD_PLAIN, as.user, as.pw),
				"Logging in");
		if (status < 0 ) {
			ovis_log(mylog, OVIS_LDEBUG, STOR ": amqp_login failed\n");
			rc = EKEYREJECTED;
			goto fail;
		} else {
			ovis_log(mylog, OVIS_LDEBUG, STOR ": amqp_login ok\n");
		}

		void * vstatus = amqp_channel_open(as.conn, 1);
		if (!vstatus) {
			ovis_log(mylog, OVIS_LDEBUG, STOR ": amqp_channel_open failed\n");
			status = lrmq_die_on_amqp_error(
				amqp_get_rpc_reply(as.conn),
				"Opening channel");
			if (status  < 0 ) {
				ovis_log(mylog, OVIS_LDEBUG, STOR ": amqp_get_rpc_reply failed\n");
			}
			rc = ENOMEM;
			goto fail;
		}
		log_amqp(STOR ": started ldms amqp");
		as.broker_failed = 0;
		ovis_log(mylog, OVIS_LINFO, STOR ": started amqp\n");
	} else {
		ovis_log(mylog, OVIS_LINFO, STOR ": started fake amqp\n");
	}

	as.next_connect = 0;
	return 0;
again:
	if (as.conn) {
		ovis_log(mylog, OVIS_LDEBUG, STOR ": destroy conn; try later.\n");
		amqp_destroy_connection(as.conn);
		as.conn = NULL;
	}
	return rc;

fail:
	as.next_connect = TIME_MAX;
	if (as.conn) {
		ovis_log(mylog, OVIS_LDEBUG, STOR ": destroy conn; no retry.\n");
		amqp_destroy_connection(as.conn);
		as.conn = NULL;
	}
	return rc;
}

/**
 * \brief Configuration
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	ovis_log(mylog, OVIS_LDEBUG, STOR ": config start.\n");
	char *value;

	value = av_value(avl, "logmsg");
	if (!value)
		value = "n";
	if (value[0] == 'y') {
		logmsg = true;
	} else {
		logmsg = false;
	}

	value = av_value(avl, "useserver");
	if (!value)
		value = "y";
	if (value[0] == 'y') {
		doserver = true;
	} else {
		doserver = false;
	}

	value = av_value(avl, "extraprops");
	if (!value) {
		value = "y";
	}
	if (value[0] == 'y') {
		g_extraprops = true;
	} else {
		g_extraprops = false;
	}

	value = av_value(avl, "host");
	if (!value) {
		value = "localhost";
	}

	char *vhvalue;
	vhvalue = av_value(avl, "vhost");
	if (!vhvalue) {
		vhvalue = "/";
	}

	char *user;
	user = av_value(avl, "user");
	snprintf(as.user, sizeof(as.user), "guest");
	if (user) {
		snprintf(as.user, sizeof(as.user), "%s", user);
	}

	char *p;
	p = av_value(avl, "port");
	if (!p) {
		as.amqp_port = AMQP_PROTOCOL_PORT;
	} else {
		as.amqp_port = atoi(p);
		if (as.amqp_port < 1)  {
			ovis_log(mylog, OVIS_LERROR, STOR ": config port=%s invalid.\n",
				p);
			return EINVAL;
		}
	}
	ovis_log(mylog, OVIS_LINFO, STOR ": config host=%s port=%d vhost=%s user=%s\n",
	       value, as.amqp_port, vhvalue, user);

	char *rt = av_value(avl, "retry");
	if (!rt) {
		as.retry_sec = RETRY_DEFAULT;
	} else {
		int rts = atoi(rt);
		if (rts < 1) {
			ovis_log(mylog, OVIS_LERROR,
				STOR ": config retry=%s invalid.\n", rts);
			return EINVAL;
		} else {
			as.retry_sec = rts;
		}
	}

	as.timeout.tv_sec = TIMEOUT_DEFAULT_SEC;
	as.timeout.tv_usec = TIMEOUT_DEFAULT_USEC;
	rt = av_value(avl, "timeout");
	if (rt) {
		int rts = atoi(rt);
		if (rts < 1) {
			ovis_log(mylog, OVIS_LERROR,
				STOR ": config timeout=%s invalid.\n", rts);
			return EINVAL;
		} else {
			as.timeout.tv_sec = rts / 1000;
			as.timeout.tv_usec = (rts % 1000) * 1000;
		}
	}

	as.heartbeat = HEARTBEAT_DEFAULT_SEC;
	rt = av_value(avl, "heartbeat");
	if (rt) {
		int rts = atoi(rt);
		if (rts < 0) {
			ovis_log(mylog, OVIS_LERROR,
				STOR ": config heartbeat=%s invalid.\n", rts);
			return EINVAL;
		} else {
			as.heartbeat = rts;
		}
	}

	ovis_log(mylog, OVIS_LINFO, STOR ": config timeout=%d retry=%d heartbeat=%d\n",
		(int)(1000*as.timeout.tv_sec + as.timeout.tv_usec/1000),
		as.retry_sec, as.heartbeat);
	sprintf(as.pw, "guest");
	pwfile = av_value(avl, "pwfile");
	if (pwfile) {
		ovis_log(mylog, OVIS_LINFO, STOR ": config pwfile=%s\n", pwfile);
		int pwerr = ovis_get_rabbit_secretword(pwfile,as.pw,PWSIZE,
			rabbit_lerror_tmp);
		if (pwerr) {
			ovis_log(mylog, OVIS_LERROR, STOR ": config pwfile failed\n");
			return EINVAL;
		}
	}

	char *rvalue;
	rvalue = av_value(avl, "routing_key");
	if (!rvalue) {
		rvalue = "";
	}

	char *evalue;
	evalue = av_value(avl, "exchange");
	if (!evalue) {
		evalue = "amq.topic";
	}
	ovis_log(mylog, OVIS_LINFO, STOR ": routing_key=%s exchange=%s\n",
		rvalue, evalue);
	ovis_log(mylog, OVIS_LINFO, STOR ": logmsg=%d doserver=%d\n",
		logmsg, doserver);


	pthread_mutex_lock(&cfg_lock);

	if (vhost) {
		free(vhost);
	}
	vhost = strdup(vhvalue);

	if (host_name) {
		free(host_name);
	}
	host_name = strdup(value);

	if (routing_key_path) {
		free(routing_key_path);
	}
	routing_key_path = strdup(rvalue);

	if (rmq_exchange) {
		free(rmq_exchange);
	}
	rmq_exchange = strdup(evalue);
	rmq_exchange_bytes = amqp_cstring_bytes(rmq_exchange);

	pthread_mutex_unlock(&cfg_lock);

	if (!routing_key_path || !rmq_exchange ||
		!host_name) {
		ovis_log(mylog, OVIS_LERROR, STOR ": config strdup failed.\n");
		return ENOMEM;
	}

	int rc = 0;

	pthread_mutex_lock(&cfg_lock);

	rc = make_connection();

	pthread_mutex_unlock(&cfg_lock);
	switch (rc) {
	case EAGAIN:
		ovis_log(mylog, OVIS_LERROR, STOR ": config: rabbitmq connection delayed.\n");
		ovis_log(mylog, OVIS_LERROR, STOR ": config: Verify config values or start broker.\n");
		/* fall through */
	case 0:
		ovis_log(mylog, OVIS_LINFO, STOR ": config done.\n");
		return 0;
	default:
		ovis_log(mylog, OVIS_LINFO, STOR ": config failed.\n");
		return rc;
	}
}

static void term(ldmsd_plug_handle_t handle)
{
	/* What contract is this supposed to meet. Shall close(sh) have
	been called for all existing sh already before this is reached.?
	*/
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return
	"    config name=store_rabbitkw routing_key=<route> host=<host> port=<port> exchange=<exch> \\ \n"
	"           vhost=<vhost> user=<user> pwfile=<auth> \\ \n"
	"           timeout=<msec> retry=<sec> \\ \n"
	"           extraprops=<y/n> useserver=<y/n> logmsg=<y/n>\n"
	"        where:\n"
	"           route     The routing_key to use for all messages\n"
	"           host      The rabbitmq server host (default localhost)\n"
	"           port      The port on the rabbitmq host (default 5672)\n"
	"           exch      The exchange destination (default amq.topic)\n"
	"           vhost     The amqp vhost (default '/').\n"
	"           user      The amqp login user (default 'guest').\n"
	"           auth      The amqp user password location (pw 'guest' if none).\n"
	"           msec      The timeout milliseconds for connect.\n"
	"           sec       The interval (seconds) for connection retry.\n"
	"           useserver Choose send messages to amqp server or not (testing).\n"
	"           logmsg    Choose to log message data (testing).\n"
	"           extraprops Choose to encode basic metadata with messages or\n"
	"                     only in the routing key.\n"
	;
}

static const size_t value_fmtlen[] = {
	[LDMS_V_NONE] = 8,
	[LDMS_V_CHAR] = 1,
	[LDMS_V_U8] = 3,
	[LDMS_V_S8] = 3,
	[LDMS_V_U16] = 5,
	[LDMS_V_S16] = 6,
	[LDMS_V_U32] = 10,
	[LDMS_V_S32] = 11,
	[LDMS_V_U64] = 20,
	[LDMS_V_S64] = 21,
	[LDMS_V_F32] = 15,
	[LDMS_V_D64] = 23,
	/* size of array type in this table is the size of an element */
	[LDMS_V_CHAR_ARRAY] = 1,
	[LDMS_V_U8_ARRAY] = 3,
	[LDMS_V_S8_ARRAY] = 3,
	[LDMS_V_U16_ARRAY] = 5,
	[LDMS_V_S16_ARRAY] = 6,
	[LDMS_V_U32_ARRAY] = 10,
	[LDMS_V_S32_ARRAY] = 11,
	[LDMS_V_U64_ARRAY] = 20,
	[LDMS_V_S64_ARRAY] = 21,
	[LDMS_V_F32_ARRAY] = 15,
	[LDMS_V_D64_ARRAY] = 23,
};

#define MAX_FMTLEN value_fmtlen[LDMS_V_D64]

static const char * value_fmt[] = {
	[LDMS_V_NONE] = "error!",
	[LDMS_V_CHAR] = "%c",
	[LDMS_V_U8] = "%"PRIu8,
	[LDMS_V_S8] = "%"PRId8,
	[LDMS_V_U16] = "%"PRIu16,
	[LDMS_V_S16] = "%"PRId16,
	[LDMS_V_U32] = "%"PRIu32,
	[LDMS_V_S32] = "%"PRId32,
	[LDMS_V_U64] = "%"PRIu64,
	[LDMS_V_S64] = "%"PRId64,
	[LDMS_V_F32] = "%.9g",
	[LDMS_V_D64] = "%.17g",
	/* format with comma for array elements */
	[LDMS_V_CHAR_ARRAY] = "%s",
	[LDMS_V_U8_ARRAY] = "%"PRIu8,
	[LDMS_V_S8_ARRAY] = "%"PRId8,
	[LDMS_V_U16_ARRAY] = "%"PRIu16,
	[LDMS_V_S16_ARRAY] = "%"PRId16,
	[LDMS_V_U32_ARRAY] = "%"PRIu32,
	[LDMS_V_S32_ARRAY] = "%"PRId32,
	[LDMS_V_U64_ARRAY] = "%"PRIu64,
	[LDMS_V_S64_ARRAY] = "%"PRId64,
	[LDMS_V_F32_ARRAY] = "%.9g",
	[LDMS_V_D64_ARRAY] = "%.17g",
};

typedef float f_t;
typedef double d_t;
union dval {
	char c;
	int8_t s8;
	int16_t s16;
	int32_t s32;
	int64_t s64;
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;
	f_t f;
	d_t d;
};

/* This definition works efficiently on littleendian hw;
 * bigendian needs more work. append a scalar on each call,
 * with string exception.
 * \return an errno.h value. */
static int append_value(union dval *uv, big_dstring_t *m, enum ldms_value_type type)
{
	const char *str = NULL;
	char *fin = NULL;
	int r;
	char buf[MAX_FMTLEN + 1];
	union dval v = *uv;
	switch (type) {
	case LDMS_V_CHAR:
		r = snprintf(buf, sizeof(buf), value_fmt[type], v.c);
		break;
	case LDMS_V_CHAR_ARRAY:
		str = (char *)uv;
		r = DSTRING_ALL;
		break;
	case LDMS_V_U8:
	case LDMS_V_U8_ARRAY:
		r = snprintf(buf, sizeof(buf), value_fmt[type], v.u8);
		break;
	case LDMS_V_S8:
	case LDMS_V_S8_ARRAY:
		r = snprintf(buf, sizeof(buf), value_fmt[type], v.s8);
		break;
	case LDMS_V_U16:
	case LDMS_V_U16_ARRAY:
		r = snprintf(buf, sizeof(buf), value_fmt[type], v.u16);
		break;
	case LDMS_V_S16:
	case LDMS_V_S16_ARRAY:
		r = snprintf(buf, sizeof(buf), value_fmt[type], v.s16);
		break;
	case LDMS_V_U32:
	case LDMS_V_U32_ARRAY:
		r = snprintf(buf, sizeof(buf), value_fmt[type], v.u32);
		break;
	case LDMS_V_S32:
	case LDMS_V_S32_ARRAY:
		r = snprintf(buf, sizeof(buf), value_fmt[type], v.s32);
		break;
	case LDMS_V_U64:
	case LDMS_V_U64_ARRAY:
		r = snprintf(buf, sizeof(buf), value_fmt[type], v.u64);
		break;
	case LDMS_V_S64:
	case LDMS_V_S64_ARRAY:
		r = snprintf(buf, sizeof(buf), value_fmt[type], v.s64);
		break;
	case LDMS_V_F32:
	case LDMS_V_F32_ARRAY:
		r = snprintf(buf, sizeof(buf), value_fmt[type], v.f);
		break;
	case LDMS_V_D64:
	case LDMS_V_D64_ARRAY:
		r = snprintf(buf, sizeof(buf), value_fmt[type], v.d);
		break;
	case LDMS_V_NONE:
	default:
		str = value_fmt[LDMS_V_NONE];
		r = DSTRING_ALL;
		break;
	}
	if (str) {
		fin = bdstrcat(m, str, DSTRING_ALL);
	} else {
		fin = bdstrcat(m, buf, r);
	}
	if (!fin) {
		return ENOMEM;
	}
	return 0;
}


/* Requires scope in which m is a bdstring pointer and estr
 * is an error handling label.
 * Appends literal STR to m. */
#define CHKLIT(STR) \
	if (!bdstrcat(m, STR, sizeof(STR))) goto estr

/* Requires scope in which m is a bdstring pointer and estr
 * is an error handling label.
 * Appends pointer STR to m. */
#define CHKSTR(STR) \
	if (!bdstrcat(m, STR, strlen(STR))) goto estr

/* Requires scope in which m is a bdstring pointer and estr
 * is an error handling label.
 * Appends pointer STR to m. */
#define CHKINT(IVAL) \
	if (!bdstrcat(m, IVAL)) goto estr

/* for efficiency we want the next three shorter (M,T,V) but
 * customer requests long for now.
 */
#define KEY_METRIC "metric"
#define KEY_TYPE "type"
#define KEY_LEN "length"
#define KEY_VALUE "value"
/* array element separator (. and , are decimals depending on locale) */
#define VAL_SEP ";"
/* return 0 if ok */
static
int write_kw(struct rabbitkw_store_instance *si,
               const struct ldms_timestamp *ts,
               ldms_set_t set, int id, int arr_idx)
{
	big_dstring_t *m = &(si->message);

	/* metric=%n type=%t value=%v */
	int len = 0;
	const char *name = ldms_metric_name_get(set, id);
	enum ldms_value_type t = ldms_metric_type_get(set, id);
	const char *stype = ldms_metric_type_to_str(t);
	if (!name) {
		if (!si->conflict_warned) {
			ovis_log(mylog, OVIS_LERROR, STOR ": write_kw: metric id %d: no name at list index %d.\n", id, arr_idx);
			ovis_log(mylog, OVIS_LERROR, STOR ": reconfigure to resolve schema definition conflict for schema=%s and instance=%s.\n",
				si->schema, ldms_set_instance_name_get(set));
			si->conflict_warned = 1;
		}
		return ERANGE;
	}
	CHKLIT("\t" KEY_METRIC "=");
	CHKSTR(name);
	CHKLIT("\t" KEY_TYPE "=");
	CHKSTR(stype);
	union dval v;
	v.u64 = 0;
	uint32_t arr_len = 1;
	if (ldms_type_is_array(t) && t != LDMS_V_CHAR_ARRAY) {
		arr_len = ldms_metric_array_get_len(set, id);
		CHKLIT("\t" KEY_LEN "=");
		v.u32 = arr_len;
		len = append_value(&v, m, LDMS_V_U64);
	}
	CHKLIT("\t" KEY_VALUE "=");
	const char *str;

	if (!ldms_type_is_array(t) || t == LDMS_V_CHAR_ARRAY) {
		switch (t) {
#define FMT_CASE3(enum_suf, union_mem, fn_suf) \
		case LDMS_V_ ## enum_suf: \
			v. union_mem = ldms_metric_get_ ## fn_suf(set, id); \
			len = append_value(&v, m, t); \
			break

#define FMT_CASE(enum_suf, union_mem) FMT_CASE3(enum_suf, union_mem, union_mem)
		case LDMS_V_CHAR_ARRAY:
			str = ldms_metric_array_get_str(set, id);
			len = append_value((union dval *)str, m, t);
			break;
		FMT_CASE(U8, u8);
		FMT_CASE(U16, u16);
		FMT_CASE(U32, u32);
		FMT_CASE(U64, u64);
		FMT_CASE(S8, s8);
		FMT_CASE(S16, s16);
		FMT_CASE(S32, s32);
		FMT_CASE(S64, s64);
		FMT_CASE3(F32, f, float);
		FMT_CASE3(D64, d, double);
#undef FMT_CASE
#undef FMT_CASE3
		default:
			ovis_log(mylog, OVIS_LERROR, "%s:%d: unexpected metric type %d\n",
				__FILE__,__LINE__,(int)t);
			return EINVAL;
		}
	} else {
		uint32_t arr_len = ldms_metric_array_get_len(set, id);
		uint32_t k = 0;
		for (k = 0; k < arr_len; k++) {
			if (k != 0)
				CHKLIT(VAL_SEP);
			switch (t) {
#define FMT_CASE3(enum_suf, union_mem, fn_suf) \
		case LDMS_V_ ## enum_suf ##_ARRAY: \
			v. union_mem = ldms_metric_array_get_ ## fn_suf(set, id, k); \
			len = append_value(&v, m, t); \
			break
#define FMT_CASE(enum_suf, union_mem) FMT_CASE3(enum_suf, union_mem, union_mem)
			FMT_CASE(U8, u8);
			FMT_CASE(U16, u16);
			FMT_CASE(U32, u32);
			FMT_CASE(U64, u64);
			FMT_CASE(S8, s8);
			FMT_CASE(S16, s16);
			FMT_CASE(S32, s32);
			FMT_CASE(S64, s64);
			FMT_CASE3(F32, f, float);
			FMT_CASE3(D64, d, double);
#undef FMT_CASE
#undef FMT_CASE3
			default:
				ovis_log(mylog, OVIS_LERROR,
					"%s:%d: unexpected array type %d\n",
					__FILE__,__LINE__,(int)t);
				return EINVAL;
			}
		}
	}

	if (len < 0) {
		return E2BIG;
	}
	return 0;
estr:
	ovis_log(mylog, OVIS_LERROR, "%s: formatting data failed.\n", __FILE__);
	return ENOMEM;
}

void destroy_store(struct rabbitkw_store_instance *si)
{
	if (!si)
		return;
	free(si->key);
	free(si->container);
	free(si->schema);
	free(si->routingkey);
	bdstr_free(&(si->message));
	free(si);
}

static ldmsd_store_handle_t
open_store(ldmsd_plug_handle_t s, const char *container, const char *schema,
           struct ldmsd_strgp_metric_list *metric_list)
{
	struct rabbitkw_store_instance *si;

	pthread_mutex_lock(&cfg_lock);
	dsinit(ds);
	dscat(ds,container);
	dscat(ds," ");
	dscat(ds,schema);
	size_t klen = bdstrlen(&ds);
	char *key = dsdone(ds);
	if (!key) {
		ovis_log(mylog, OVIS_LERROR, STOR ": oom ds\n");
	}

	si = idx_find(store_idx, (void *)key, klen);
	if (!si) {
		/*
		 * Open a new store for this container/schema
		 */
		si = calloc(1, sizeof(*si));
		if (!si) {
			ovis_log(mylog, OVIS_LERROR, STOR ": oom si\n");
			goto out;
		}
		si->key = key;
		si->extraprops = g_extraprops;
		si->store = s;
		si->routingkey = strdup(routing_key_path);
		si->container = strdup(container);
		si->schema = strdup(schema);
		if (!si->container || ! si->schema || ! si->routingkey) {
			ovis_log(mylog, OVIS_LERROR, STOR ": out of memory\n");
			goto err3;
		}

#define STRPROP(i,name,val) \
		si->myprops[i].key = amqp_cstring_bytes(name); \
		si->myprops[i].value.kind = AMQP_FIELD_KIND_UTF8; \
		si->myprops[i].value.value.bytes = amqp_cstring_bytes(val)
		init_props(g_extraprops, &(si->props), si->mypropstab, si);
		if (si->extraprops) {
			STRPROP(0, "container", container);
			STRPROP(1, "schema", schema);
			si->mypropstab.num_entries = NUM_CUSTOM_PROPS;
			si->mypropstab.entries = si->myprops;
		}
		idx_add(store_idx, (void *)key, strlen(key), si);
		store_count++;
	}
	goto out;
err3:
	destroy_store(si);
	si = NULL;
out:
	pthread_mutex_unlock(&cfg_lock);
	return si;
}

static int
store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh, ldms_set_t set, int *metric_arry, size_t metric_count)
{
	struct rabbitkw_store_instance *si;
	int i, rc = 0;
	if (!_sh || !set || (metric_count && !metric_arry)) {
		ovis_log(mylog, OVIS_LERROR, STOR ": store() null input received\n");
		return EINVAL;
	}
	si = _sh;
	const struct ldms_timestamp _ts = ldms_transaction_timestamp_get(set);
	const struct ldms_timestamp *ts = &_ts;
	pthread_mutex_lock(&cfg_lock); /* because of async close, need lock here ?*/
	if (doserver ) {
		if (!as.conn) {
			rc = make_connection();
			if (rc == EAGAIN) {
				ovis_log(mylog, OVIS_LDEBUG, STOR ": try later\n");
				rc = 0;
				goto skip;
			}
			if (rc != 0)
				goto skip;
		}
	}

	big_dstring_t *m = &(si->message);
	bdstr_trunc(m, 0);
	amqp_bytes_t message_bytes;


#define CHKWRITE(VPTR, VTYPE) \
	if (append_value(VPTR, m, VTYPE)) goto estr

	if (doserver || logmsg) {
		si->props.timestamp = 1000000*(uint64_t)(ts->sec) + ts->usec; /* microseconds utc */

		CHKLIT("timestamp_us=");
		CHKWRITE((union dval *)&(si->props.timestamp), LDMS_V_U64);
		CHKLIT("\tproducer=");
		const char *ps = ldms_set_producer_name_get(set);
		CHKSTR(ps);
		CHKLIT("\tcontainer=");
		CHKSTR(si->container);
		CHKLIT("\tschema=");
		CHKSTR(si->schema);

		for (i = 0; i < metric_count; i++) {
			int metric_id = metric_arry[i];
			rc = write_kw(si, ts, set, metric_id, i);
			switch(rc) {
			case 0:
			case ERANGE:
				continue;
			default:
				ovis_log(mylog, OVIS_LDEBUG,"Error %d: %s at %s:%d: loop index %d, metric_id %d\n",
				       rc, STRERROR(rc),
					__FILE__, __LINE__, i, metric_id);
				goto estr;
			}
		}
		message_bytes.len = bdstrlen(m);
		message_bytes.bytes = (void *)bdstrval(m);
	}
	if (logmsg) {
		if (message_bytes.len > 4000) {
			ovis_log(mylog, OVIS_LDEBUG, "%s: %.4000s <truncated>\n",
				si->routingkey, message_bytes.bytes);
		} else {
			ovis_log(mylog, OVIS_LDEBUG, "%s: %s\n", si->routingkey,
				message_bytes.bytes);
		}
		ovis_log(mylog, OVIS_LDEBUG, STOR "%s: len=%zu\n", si->routingkey,
			message_bytes.len);
	}

	if (doserver) {
		const char *instance = ldms_set_instance_name_get(set);
		update_props(&(si->props), instance, si);
		rc = lrmq_die_on_error(amqp_basic_publish(as.conn, 1,
			rmq_exchange_bytes,
			amqp_cstring_bytes(si->routingkey),
			0,
			0,
			&(si->props),
			message_bytes),
			STOR ": Publishing");
		if ( rc ) {
			/* we only got here by once opening successfully
			 * so we should retry later. */
			reset_connection();
		}
	} else {
		rc = 0;
	}

skip:
	pthread_mutex_unlock(&cfg_lock);
	return rc;

estr:
	ovis_log(mylog, OVIS_LDEBUG, STOR ": err\n");
	pthread_mutex_unlock(&cfg_lock);
	return ENOMEM;

}

static void close_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	pthread_mutex_lock(&cfg_lock);
	struct rabbitkw_store_instance *si = _sh;
	if (!_sh ) {
		pthread_mutex_unlock(&cfg_lock);
		return;
	}

	idx_delete(store_idx, (void *)(si->key), strlen(si->key));
	destroy_store(si);
	pthread_mutex_unlock(&cfg_lock);
	store_count--;
	if (store_count == 0) {
		cleanup_amqp();
		/* shouldn't have to do this if fini called at exit properly */
	}
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);

	rabbit_store_pi_log_set(mylog);

	ovis_log(mylog, OVIS_LINFO,"Loading support for rabbitmq amqp version%s\n",
			amqp_version());
        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_store ldmsd_plugin_interface = {
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

static void __attribute__ ((constructor)) store_rabbitkw_init();
static void store_rabbitkw_init()
{
	store_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_rabbitkw_fini(void);
static void store_rabbitkw_fini()
{
	cleanup_amqp();
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(store_idx);
}
