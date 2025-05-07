/**
 * Copyright (c) 2010-2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2017 Open Grid Computing, Inc. All rights reserved.
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
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <pthread.h>
#include <errno.h>
#include <coll/idx.h>
#include <coll/label-set.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ovis_util/dstring.h"
#if OVIS_LDMS_HAVE_AUTH
#include "ovis_auth/auth.h"
#endif /* OVIS_LDMS_HAVE_AUTH */
#include <amqp_tcp_socket.h>
#include <amqp.h>
#include <amqp_framing.h>
#include "rabbit_utils.h"

/*
 * This store answers the design case of finely routing-keyed metric data
 * direct to rabbitmq.
 * It uses a single channel to route all messages.
 *
 * The encoding for generic unknown consumers uses the amqp basic properties
 * as follows (note that ldms has no notion of talking back to a sampler, so
 * some are repurposed for our application):
 *   timestamp : timestamp of sample: ***microseconds*** since the epoch.
 *   type : data type of metric, as string
 *   app_id : component_id (u64)
 * Suppressible data, mostly redundant with the routing key name:
 *   reply_to : producer name
 *   content_type: text/plain
 *   headers:
 *      metric : metric label
 *      metric_name_amqp : metric label sanitized for topic usage
 *      metric_name_least : metric label as least common denominator
 *      container : instance name
 *      schema : schema name
 *
 * LDMS meta metrics are emitted on a specific frequency, but independent of
 * component_id (or rather with the assumption that they are component
 * independent until we get a store api change).
 *
 * See also amqp_framing.h.
 *
 * Requirements met:
 * + Encode data as ASCII; nothing downstream will parse binary.
 * + Resend metadata periodically, not just at start, but not with every value.
 */
/*
 * NOTE: routing key (limit 255 char)
 *   (rabbitv3::path) = root.container.schema.metric_name_amqp.producer
 * If root is empty string, the leading '.' is omitted.
 * We could easily expand the key to encode all metadata
 * root.container.schema.metric.type.producer.userdata
 * It turns out amqp has no special characters banned from
 * routing keys, though it does parse based on . down stream, so
 * we use metric_name_amqp.
 */

static idx_t store_idx;
static int store_count = 0;
#define dsinit(ds) \
	dstring_t ds; \
	dstr_init(&ds)

#define dscat(ds,char_star_x) \
	dstrcat(&ds, char_star_x, DSTRING_ALL)

#define dsdone(ds) \
	dstr_extract(&ds)

static ovis_log_t mylog;

static bool g_extraprops = true; /**< parse value during config */
static time_t g_metainterval = 0; /** delay between metadata emissions; default no repeat */
static char *root_path; /**< store root path */
static char *host_name; /**< store hostname */
static char *vhost; /**< broker vhost name */
static char *pwfile; /**< broker secretword file */
static int heartbeat = 0; /** broker heartbeat */
#ifdef USEFILTER
static char *filter_path; /**< store filter path */
#endif
static char *rmq_exchange; /**< store queue */
amqp_bytes_t rmq_exchange_bytes; /**< store queue in amqp form */
#define COMPIDSPACE "                     " /* room for u64/nul */
#define COMPIDBUFSZ 22
#define PRODBUFSZ 65
/* 65 spaces for producer/nul */
#define PRODSPACE \
"                                                                 "
#define SHORTSTR_MAX 255 /* from amqp 091 spec */

/* upper bounds for canonicalized metric names. */
#define NAME_MAX_AMQP 150 /* rationale: (255 - room for other bits), roughly */
#define NAME_MAX_ANY 31 /* c99 public symbol min significant. */

/* If daemon dispatches multiple store calls (to distinct si) across threads, */
/* the next 2 must be on a per-si basis since rabbit amqp implementation  */
/* is single threaded per connection. */
static amqp_connection_state_t conn = NULL;
static int amqp_port = 0;

/* valid to use only when config completes successfully. */
static
int log_amqp(const char *msg)
{
	amqp_bytes_t message_bytes = amqp_cstring_bytes(msg);
	return amqp_basic_publish(conn, 1,
	                          rmq_exchange_bytes,
	                          amqp_cstring_bytes("ldms-aggd"),
	                          0,
	                          0,
	                          NULL,
	                          message_bytes);
}

/* workaround until issue134 is merged. dump this function then in favor of olerr. */
void rabbit_lerror_tmp(const char *fmt, ...)
{
	ovis_log(mylog, OVIS_LERROR,"%s",fmt);
}



#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)
#define IDBUFSIZE 22
#define PRODBUFSIZE (LDMS_PRODUCER_NAME_MAX+1)
#define NUM_CUSTOM_PROPS 5
/**
 * \brief Store for individual metric.
 */
struct rabbitv3_metric_store {
	char *metric; /**< path of the rabbitv3 store */
	struct ovis_name id_amqp; /**< canonical name for routing key use */
	struct ovis_name id_least; /**< canonical name for any use */
	char *routingkey; /**< root.container.schema.metric.producer */
	amqp_bytes_t routingkey_bytes; /**< store routingkey in amqp form */
	char *compidbuf; /**< user data string space */
	amqp_bytes_t compid_bytes; /**< store compidbuf in amqp form */
	pthread_mutex_t lock; /**< lock at metric store level. needed? */
	char *routingkey_producer; /**< compid substring start within routingkey */
	amqp_basic_properties_t props;
	amqp_table_entry_t myprops[NUM_CUSTOM_PROPS]; /**< names for least, amqp. */
	amqp_table_t mypropstab;

	void *message; /**< buffer for formatted message */
	size_t message_cap; /**< allocated size of message */
	enum ldms_value_type type; /* metric type, or none if still pending */
	size_t array_len;
	LIST_ENTRY(rabbitv3_metric_store) entry; /**< Entry for free list. */
};

/* group of metric support data.  */
struct rabbitv3_store_instance {
	struct ldmsd_store *store;
	char *container;
	char *schema;
	char *key; /* container schema */
	idx_t ms_idx; /* data metrics */
	idx_t meta_idx; /* meta metrics */
	LIST_HEAD(ms_list, rabbitv3_metric_store) ms_list;
	struct ms_list meta_list; /* LIST_HEAD meta_list */
	struct ovis_label_set *metric_names_amqp;
	struct ovis_label_set *metric_names_least;
	int metric_count;
	int meta_count; /* # of metrics that are meta */
	int data_count; /* # of data metrics */
	bool extraprops; /* include ldmsd (not sampler) metadata in basic message headers */
	bool write_meta; /* write needed when a set defined or redefined or timeexceeded */
	time_t next_meta; /* clock time (sec) after which rewrite sampler meta. */
	time_t metainterval; /* rewrite meta interval. */
	int conn_errs; /* count of conn err msg output. */
};

static pthread_mutex_t cfg_lock;

/* Make sure the strings passed in do not disappear before props is */
/* out of use or they are literals. */
/* Take care of specific fields and bit flags. */
static void init_props(bool extraprops,
			const char *type,
			const char *routingkey_producer,
			const char *compid,
			amqp_basic_properties_t *props,
			amqp_table_t headers)
{
	if (props) {
		memset(props,1,sizeof(*props));
	} else {
		return;
	}
	if (!type ||  !routingkey_producer) {
		ovis_log(mylog, OVIS_LERROR,
		       "rabbitv3: bad init_props(%p, %p, props)\n",
		       type, routingkey_producer);
		return;
	}
	props->delivery_mode = 1;

	props->_flags = 0;

	props->type = amqp_cstring_bytes(type);
	props->_flags |= AMQP_BASIC_TYPE_FLAG;

	props->app_id = amqp_cstring_bytes(compid);
	props->_flags |= AMQP_BASIC_APP_ID_FLAG;

	props->_flags |= AMQP_BASIC_TIMESTAMP_FLAG;
	/* But if messages are to be directly meaningful once delivered, */
	/* they likely need all this. Let's not overoptimize */
	/* before we have a measurable problem and force parsing on recipients. */
	if (extraprops) {
		props->_flags |= AMQP_BASIC_CONTENT_TYPE_FLAG;
		props->content_type = amqp_cstring_bytes("text/plain");

		props->reply_to = amqp_cstring_bytes(routingkey_producer);
		props->_flags |= AMQP_BASIC_REPLY_TO_FLAG;

		props->headers = headers;
		props->_flags |= AMQP_BASIC_HEADERS_FLAG;

	}
}

static int cleaned=0;
void cleanup_amqp()
{
	pthread_mutex_lock(&cfg_lock);
	if (!conn)
		goto out;
	if (cleaned)
		goto out;
	cleaned = 1;
	int s;
	s = lrmq_die_on_amqp_error(
		amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS),
		"Closing channel");
	if (s) {
		ovis_log(mylog, OVIS_LDEBUG,"rabbitv3: skipping connection close.\n");
		goto out;
	}
	s = lrmq_die_on_amqp_error(
		amqp_connection_close(conn, AMQP_REPLY_SUCCESS),
		"Closing connection");
	if (s) {
		ovis_log(mylog, OVIS_LDEBUG,"rabbitv3: skipping connection destroy.\n");
		goto out;
	}
	s = lrmq_die_on_error(amqp_destroy_connection(conn), "Ending connection");
out:
	pthread_mutex_unlock(&cfg_lock);

}

/**
 * \brief Configuration
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
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
	if (!user) {
		user = "guest";
	}

	char *mint;
	mint = av_value(avl, "metainterval");
	if (mint) {
		int tmp = atoi(mint);
		if (mint < 0)  {
			ovis_log(mylog, OVIS_LERROR,
				"rabbitv3: config metainterval=%s invalid.\n",
				mint);
			return EINVAL;
		}
		g_metainterval = tmp;
	}

	char *p;
	p = av_value(avl, "port");
	if (!p) {
		amqp_port = AMQP_PROTOCOL_PORT;
	} else {
		amqp_port = atoi(p);
		if (amqp_port < 1)  {
			ovis_log(mylog, OVIS_LERROR,
				"rabbitv3: config port=%s invalid.\n",p);
			return EINVAL;
		}
	}
	ovis_log(mylog, OVIS_LINFO,"rabbitv3: config host=%s port=%d vhost=%s user=%s metainterval=%d\n",
	       value, amqp_port, vhvalue, user, g_metainterval);

#ifdef USEFILTER
	char *fvalue;
	fvalue = av_value(avl, "filterlist");
	ovis_log(mylog, OVIS_LINFO,"rabbitv3: config filterlist=%s\n",
		fvalue ? fvalue : "<none>");
#endif

#define PWSIZE 256
	char password[PWSIZE];
	sprintf(password,"guest");
	pwfile = av_value(avl, "pwfile");
	if (pwfile) {
		ovis_log(mylog, OVIS_LINFO,"rabbitv3: config pwfile=%s\n", pwfile);
		int pwerr = ovis_get_rabbit_secretword(pwfile,password,PWSIZE,
			rabbit_lerror_tmp);
		if (pwerr) {
			ovis_log(mylog, OVIS_LERROR,"rabbitv3: config pwfile failed\n");
			return EINVAL;
		}
	}

	char *rvalue;
	rvalue = av_value(avl, "root");
	if (!rvalue) {
		rvalue = "";
	}

	char *evalue;
	evalue = av_value(avl, "exchange");
	if (!evalue) {
		evalue = "amq.topic";
	}
	ovis_log(mylog, OVIS_LINFO,"rabbitv3: root=%s exchange=%s\n", rvalue, evalue);


	pthread_mutex_lock(&cfg_lock);

	if (vhost) {
		free(vhost);
	}
	vhost = strdup(vhvalue);

	if (host_name) {
		free(host_name);
	}
	host_name = strdup(value);

	if (root_path) {
		free(root_path);
	}
	root_path = strdup(rvalue);

#ifdef USEFILTER
	if (filter_path) {
		free(filter_path);
	}
	if (fvalue) {
		filter_path = strdup(fvalue);
	} else {
		filter_path = NULL;
	}
#endif

	if (rmq_exchange) {
		free(rmq_exchange);
	}
	rmq_exchange = strdup(evalue);
	rmq_exchange_bytes = amqp_cstring_bytes(rmq_exchange);

	pthread_mutex_unlock(&cfg_lock);

	if (!root_path || !rmq_exchange ||
#ifdef USEFILTER
		(fvalue && !filter_path) ||
#endif
		!host_name) {
		ovis_log(mylog, OVIS_LERROR,"rabbitv3: config strdup failed.\n");
		return ENOMEM;
	}

	int rc = 0;
	pthread_mutex_lock(&cfg_lock);

	conn = amqp_new_connection();
	if (!conn) {
		ovis_log(mylog, OVIS_LERROR,
			"rabbitv3: config: amqp_new_connection failed.\n");
		rc = ENOMEM;
		goto out;
	}
	amqp_socket_t *asocket = NULL;
	asocket = amqp_tcp_socket_new(conn); /* stores asocket in conn, fyi. */
	if (!asocket) {
		ovis_log(mylog, OVIS_LERROR,
			"rabbitv3: config: amqp_tcp_socket_new failed.\n");
		rc = ENOMEM;
		goto err1;
	}
	int status = amqp_socket_open(asocket, host_name, amqp_port);
	if (status) {
		ovis_log(mylog, OVIS_LERROR,
			"rabbitv3: config: amqp_socket_open failed. %s\n", amqp_error_string2(status));
		rc = status;
		goto err2;
	}
	status = lrmq_die_on_amqp_error(amqp_login(conn,
	                                vhost,
	                                AMQP_DEFAULT_MAX_CHANNELS,
	                                AMQP_DEFAULT_FRAME_SIZE, heartbeat,
	                                AMQP_SASL_METHOD_PLAIN, user, password),
	                                "Logging in");
	if (status < 0 ) {
		ovis_log(mylog, OVIS_LDEBUG,"amqp_login failed\n");
		goto err3;
	}

	void * vstatus = amqp_channel_open(conn, 1);
	if (!vstatus) {
		goto err4;
	}

	status = lrmq_die_on_amqp_error(amqp_get_rpc_reply(conn),
			"Opening channel");
	if (status  <0 ) {
		ovis_log(mylog, OVIS_LDEBUG,"amqp_get_rpc_reply failed\n");
		goto err5;
	}

	log_amqp("ldms: started ldms amqp");
	goto out;


	/* we expect fanciness here is coming, so we have distinguished labels */
err5:

err4:

err3:

err2:

err1:
	amqp_destroy_connection(conn); /* deletes socket as side effect. */
	conn = NULL;
out:
	pthread_mutex_unlock(&cfg_lock);
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return
	"    config name=store_rabbitv3 root=<root> host=<host> port=<port> exchange=<exch> \\ \n"
#ifdef USEFILTER
	"           filterlist=<file> vhost=<vhost> user=<user> pwfile=<auth> \\ \n"
#else
	"           vhost=<vhost> user=<user> pwfile=<auth> \\ \n"
#endif
	"           extraprops=<y/n> metainterval=<mint>\\ \n"
	"        where:\n"
	"           root      The root of the rabbitmq routing key\n"
	"           host      The rabbitmq server host (default localhost)\n"
	"           port      The port on the rabbitmq host (default 5672)\n"
	"           exch      The exchange destination (default amq.topic)\n"
#ifdef USEFILTER
	"           file      The file listing how to process specific metrics. (disabled)\n"
#endif
	"           vhost     The amqp vhost (default '/').\n"
	"           user      The amqp login user (default 'guest').\n"
	"           auth      The amqp user password location (pw 'guest' if none).\n"
	"           extraprops Choose to encode basic metadata with messages or\n"
	"                     only in the routing key.\n"
	"           mint      The interval (seconds) at which to resend metadata.\n"
	"                     0 means never resend.\n"
	;
}

static size_t value_fmtlen[] = {
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
	[LDMS_V_CHAR_ARRAY] = "%c",
	[LDMS_V_U8_ARRAY] = "%"PRIu8",",
	[LDMS_V_S8_ARRAY] = "%"PRId8",",
	[LDMS_V_U16_ARRAY] = "%"PRIu16",",
	[LDMS_V_S16_ARRAY] = "%"PRId16",",
	[LDMS_V_U32_ARRAY] = "%"PRIu32",",
	[LDMS_V_S32_ARRAY] = "%"PRId32",",
	[LDMS_V_U64_ARRAY] = "%"PRIu64",",
	[LDMS_V_S64_ARRAY] = "%"PRId64",",
	[LDMS_V_F32_ARRAY] = "%.9g,",
	[LDMS_V_D64_ARRAY] = "%.17g,",
};

typedef int(*Formatter)(void *data, size_t len, char *buf, size_t cap, enum ldms_value_type type);

int format_none(void *data, size_t len, char *buf, size_t cap, enum ldms_value_type type)
{
	snprintf(buf,cap,"bogon!");
	return -1;
}

/* This definition works efficiently on littleendian hw;
 bigendian needs more work. */
#define FORMATTER(ptype) \
int format_##ptype(void *data, size_t len, char *buf, size_t cap, enum ldms_value_type type) \
{ \
	int c = 0; \
	ptype * vdata = data; \
	size_t k = 0; \
	char *next = buf; \
	int r; \
	if (type >= LDMS_V_CHAR_ARRAY) { \
		r = snprintf(next, cap, "%zu", len); \
		if (r >= cap) { \
			return -1; \
		} \
		c += r; \
		next += r; \
		cap -= r; \
	} \
	for ( ; k < len; k++) { \
		r = snprintf(next, cap, value_fmt[type], vdata[k]); \
		if (r >= cap) { \
			return -1; \
		} \
		c += r; \
		next += r; \
		cap -= r; \
	} \
	buf[c] = '\0'; \
	return c; \
}

FORMATTER(char)
FORMATTER(uint8_t)
FORMATTER(int8_t)
FORMATTER(uint16_t)
FORMATTER(int16_t)
FORMATTER(uint32_t)
FORMATTER(int32_t)
FORMATTER(uint64_t)
FORMATTER(int64_t)
FORMATTER(float)
FORMATTER(double)

static Formatter write_value[] = {
	[LDMS_V_NONE] = format_none,
	[LDMS_V_CHAR] = format_char,
	[LDMS_V_U8] = format_uint8_t,
	[LDMS_V_S8] = format_int8_t,
	[LDMS_V_U16] = format_uint16_t,
	[LDMS_V_S16] = format_int16_t,
	[LDMS_V_U32] = format_uint32_t,
	[LDMS_V_S32] = format_int32_t,
	[LDMS_V_U64] = format_uint64_t,
	[LDMS_V_S64] = format_int64_t,
	[LDMS_V_F32] = format_float,
	[LDMS_V_D64] = format_double,
	/* format with comma for array elements */
	[LDMS_V_CHAR_ARRAY] = format_char,
	[LDMS_V_U8_ARRAY] = format_uint8_t,
	[LDMS_V_S8_ARRAY] = format_int8_t,
	[LDMS_V_U16_ARRAY] = format_uint16_t,
	[LDMS_V_S16_ARRAY] = format_int16_t,
	[LDMS_V_U32_ARRAY] = format_uint32_t,
	[LDMS_V_S32_ARRAY] = format_int32_t,
	[LDMS_V_U64_ARRAY] = format_uint64_t,
	[LDMS_V_S64_ARRAY] = format_int64_t,
	[LDMS_V_F32_ARRAY] = format_float,
	[LDMS_V_D64_ARRAY] = format_double,
};

/* return 0 if ok */
static
int write_amqp(struct rabbitv3_metric_store *ms,
               const struct ldms_timestamp *ts, uint64_t comp_id,
               ldms_set_t set, int metric_id)
{
	amqp_bytes_t message_bytes;

	message_bytes.len = 0;
	message_bytes.bytes = ms->message;

	int udlen = snprintf(ms->compidbuf, COMPIDBUFSZ, "%"PRIu64, comp_id);
	ms->props.app_id.len = udlen;
	ms->props.app_id = ms->compid_bytes;

	const char *producer = ldms_set_producer_name_get(set);
	int idlen;
	if (strchr(producer,'.') != NULL) {
		/* mash . qualified producers to _ */
		int plen = strlen(producer);
		char buf[plen+1];
		buf[plen] = '\0';
		int k = 0;
		for (k = 0; k < plen; k++) {
			if (producer[k] != '.') {
				buf[k] = producer[k];
			} else {
				buf[k] = '_';
			}
		}
		idlen = snprintf(ms->routingkey_producer, PRODBUFSZ,
			 "%s", buf);
	} else {
		idlen = snprintf(ms->routingkey_producer, PRODBUFSZ,
			 "%s", producer);
	}
	ms->props.correlation_id.len = idlen;
	ms->routingkey_bytes.len = ms->routingkey_producer - ms->routingkey + idlen;
	ms->props.timestamp = 1000000*(uint64_t)(ts->sec) + ts->usec; /* microseconds utc */

	int len = 0;
	enum ldms_value_type metric_type = ldms_metric_type_get(set,metric_id);
	uint32_t arr_len = ldms_metric_array_get_len(set,metric_id);
	void * data = ldms_metric_get(set,metric_id);
	len = write_value[metric_type](data, arr_len,
			       ms->message, ms->message_cap, metric_type);
	if (len < 0) {
		return E2BIG;
	}
	message_bytes.len = len;

	int rc = lrmq_die_on_error(amqp_basic_publish(conn, 1,
	                           rmq_exchange_bytes,
	                           ms->routingkey_bytes,
	                           0,
	                           0,
	                           &(ms->props),
	                           message_bytes),
	                           "Publishing");

	/* ovis_log(mylog, OVIS_LDEBUG, "%s: %s\n",ms->routingkey, ms->message); */
	return rc;
}

static void destroy_metric_store(struct rabbitv3_metric_store *ms)
{
	if (ms->routingkey) {
		free(ms->routingkey);
	}
	if (ms->metric) {
		free(ms->metric);
	}
	if (ms->message) {
		free(ms->message);
	}
	if (ms->compidbuf) {
		free(ms->compidbuf);
	}
	free(ms);
}

/* reserve string space w/commas for len, type given, or do nothing
if len == 0 */
static int alloc_message(struct rabbitv3_metric_store * ms,
                         enum ldms_value_type metric_type, uint32_t arr_len)
{
	if (ms->message || ms->message_cap) {
		return 2; /* been here before. */
	}
	if (arr_len) {
		size_t s = COMPIDBUFSZ + arr_len * (value_fmtlen[metric_type] +1) + 1;
		ms->message = calloc(s,1);
		if (ms->message == NULL) {
			ovis_log(mylog, OVIS_LERROR,
			       "rabbitv3: OOM ms in alloc_message.\n");
			return 1;
		}
		ms->message_cap = s;
	}
	return 0;
}

static struct rabbitv3_metric_store *new_metric_store(bool extraprops,
                const char *container,
                const char *schema,
                const char *metric_name,
                enum ldms_value_type metric_type,
                uint32_t arr_len,
		struct ovis_label_set *metric_names_amqp,
		struct ovis_label_set *metric_names_least
		)
{
	struct rabbitv3_metric_store * ms = NULL;
	if (metric_type == LDMS_V_NONE) {
		ovis_log(mylog, OVIS_LDEBUG,"rabbitv3: metric type still pending:  %s.\n",metric_name);
		return NULL;
	}
	ms = calloc(1, sizeof(*ms));
	if (!ms) {
		ovis_log(mylog, OVIS_LERROR,"rabbitv3: OOM ms in open_store .\n");
		goto out;
	}
	ms->type = metric_type;
	ms->array_len = arr_len;
	ms->compidbuf = calloc(1,COMPIDBUFSZ);
	if (!ms->compidbuf) {
		ovis_log(mylog, OVIS_LERROR,"rabbitv3: open_store: calloc fail.\n");
		goto err1;
	}
	ms->compidbuf[0] = 'a';
	ms->compid_bytes = amqp_cstring_bytes(ms->compidbuf);
	ms->metric = strdup(metric_name);
	if (!ms->metric) {
		ovis_log(mylog, OVIS_LERROR,"rabbitv3: open_store: strdup fail.\n");
		free(ms->compidbuf);
		goto err1;
	}
	ms->id_amqp = ovis_label_set_insert(metric_names_amqp,
		ovis_name_from_string(ms->metric));
	ms->id_least = ovis_label_set_insert(metric_names_least,
		ovis_name_from_string(ms->metric));
	int err = alloc_message(ms, metric_type, arr_len);
	if (err) {
		goto err1;
	}

	dsinit(ds);
	if (strlen(root_path)) {
		dscat(ds,root_path);
		dscat(ds,".");
	}
	dscat(ds,container);
	dscat(ds,".");
	dscat(ds,schema);
	dscat(ds,".");
	dscat(ds,ms->id_amqp.name);
	dscat(ds,".");
	dscat(ds,PRODSPACE);
	ms->routingkey = dsdone(ds);
	if (!ms->routingkey) {
		ovis_log(mylog, OVIS_LERROR,"rabbitv3: open_store: alloc fail.\n");
		goto err1;
	}
	ms->routingkey_producer = strrchr(ms->routingkey,'.');
	ms->routingkey_producer++;
	ms->routingkey_bytes = amqp_cstring_bytes(ms->routingkey);
	/* routingkey_bytes.len must be reset with each compid change */

	if ( strlen(ms->routingkey) > SHORTSTR_MAX) {
		free(ms->routingkey);
		ms->routingkey = NULL;
		ovis_log(mylog, OVIS_LERROR,"rabbitv3: open_store: amqp routing key too long: %s.\n", ms->routingkey);
		ovis_log(mylog, OVIS_LINFO,"rabbitv3: try shorter names for root, container, schema, or metric.\n");
		goto err1;
	}

#define STRPROP(i,name,val) \
	ms->myprops[i].key = amqp_cstring_bytes(name); \
	ms->myprops[i].value.kind = AMQP_FIELD_KIND_UTF8; \
	ms->myprops[i].value.value.bytes = amqp_cstring_bytes(val)

	STRPROP(0, "metric", ms->metric);
	STRPROP(1, "metric_name_amqp", ms->id_amqp.name);
	STRPROP(2, "metric_name_least", ms->id_least.name);
	STRPROP(3, "container", container);
	STRPROP(4, "schema", schema);
	ms->mypropstab.num_entries = NUM_CUSTOM_PROPS;
	ms->mypropstab.entries = ms->myprops;

	const char * type = ldms_metric_type_to_str(ms->type);
	init_props(extraprops, type,
		ms->routingkey_producer, ms->compidbuf, &(ms->props),
		ms->mypropstab);
	goto out;
err1:
	destroy_metric_store(ms);
	ms = NULL;
out:
	return ms;
}

/* update idx with any elements in set instance not previously seen.
@return 1 if fail on metric or 0 if ok.
 */
static int
update_metrics(struct rabbitv3_store_instance *si, ldms_set_t set,
               int *metric_arry, size_t metric_count )
{
	if (si->metric_count) {
		ovis_log(mylog, OVIS_LINFO,"rabbitv3: unexpected "
		       "reconfigure to size %d from %d.\n",
		       metric_count,si->metric_count);
	}
	struct rabbitv3_metric_store *ms;
	int i = 0;
	char *name;
	const char *mvname;
	int metacount = 0;
	int datacount = 0;
	int flags;
	for (i = 0; i < metric_count; i++) {
		flags = ldms_metric_flags_get(set, i);
		idx_t idx;
		struct ms_list *list;
		if (flags == LDMS_MDESC_F_META) {
			idx = si->meta_idx;
			list = &si->meta_list;
			metacount++;
		} else {
			idx = si->ms_idx;
			list = &si->ms_list;
			datacount++;
		}
		mvname = ldms_metric_name_get(set,metric_arry[i]);
		name = (char *)mvname;
		ms = idx_find(idx, name, strlen(name));
		enum ldms_value_type metric_type =
		        ldms_metric_type_get(set,metric_arry[i]);
		if (ms || metric_type == LDMS_V_NONE) {
			continue;
		}
		uint32_t arr_len =
		        ldms_metric_array_get_len(set,metric_arry[i]);

		ms = new_metric_store(si->extraprops, si->container,
			si->schema, name, metric_type, arr_len,
			si->metric_names_amqp,
			si->metric_names_least);
		if (!ms) {
			ovis_log(mylog, OVIS_LERROR,"rabbitv3: update_metrics fail\n");
			return 1;
		}
		idx_add(idx, name, strlen(name), ms);
		LIST_INSERT_HEAD(list, ms, entry);
		si->metric_count++;
	}
	si->meta_count = metacount;
	si->data_count = datacount;
	return 0;
}

static ldmsd_store_handle_t
open_store(ldmsd_plug_handle_t handle, const char *container, const char *schema,
           struct ldmsd_strgp_metric_list *metric_list)
{
	struct rabbitv3_store_instance *si;
	struct rabbitv3_metric_store *ms;

	pthread_mutex_lock(&cfg_lock);
	dsinit(ds);
	dscat(ds,container);
	dscat(ds," ");
	dscat(ds,schema);
	size_t klen = dstrlen(&ds);
	char *key = dsdone(ds);
	if (!key) {
		ovis_log(mylog, OVIS_LERROR,"rabbitv3: oom ds\n");
	}

	si = idx_find(store_idx, (void *)key, klen);
	if (!si) {
		/*
		 * First, count the metric.
		 */
		int metric_count = 0;
		ldmsd_strgp_metric_t x;

		TAILQ_FOREACH(x, metric_list, entry) {
			metric_count++;
		}

		/*
		 * Open a new store for this container/schema
		 */
		si = calloc(1, sizeof(*si));
		if (!si) {
			ovis_log(mylog, OVIS_LERROR,"rabbitv3: oom si\n");
			goto out;
		}
		si->key = key;
		si->extraprops = g_extraprops;
		si->metainterval = g_metainterval;
		si->metric_count = metric_count;
		si->ms_idx = idx_create();
		if (!si->ms_idx) {
			ovis_log(mylog, OVIS_LERROR,"rabbitv3: oom ms_idx\n");
			goto err1;
		}
		si->meta_idx = idx_create();
		if (!si->meta_idx) {
			ovis_log(mylog, OVIS_LERROR,"rabbitv3: oom meta_idx\n");
			goto err2;
		}
		si->store = s;
		si->container = strdup(container);
		if (!si->container) {
			ovis_log(mylog, OVIS_LERROR,"rabbitv3: oom container\n");
			goto err3;
		}
		si->schema = strdup(schema);
		if (!si->schema) {
			ovis_log(mylog, OVIS_LERROR,"rabbitv3: oom schema\n");
			goto err4;
		}
		si->metric_names_amqp = ovis_label_set_create(il_amqp,
			NAME_MAX_AMQP);
		si->metric_names_least = ovis_label_set_create(il_least,
			NAME_MAX_ANY);

		char *name;
		TAILQ_FOREACH(x, metric_list, entry) {
			/* Create new metric store if not existing. */
			name = x->name;
			enum ldms_value_type metric_type = x->type;
			if (metric_type == LDMS_V_NONE) {
				ovis_log(mylog, OVIS_LERROR,"rabbitv3: open_store without "
				"type for %s\n",x->name);
				continue;
			}
			idx_t idx;
			struct ms_list *list;
			int flags = x->flags;
			if (flags == LDMS_MDESC_F_META) {
				idx = si->meta_idx;
				list = &si->meta_list;
			} else {
				idx = si->ms_idx;
				list = &si->ms_list;
			}
			ms = idx_find(idx, name, strlen(name));
			if (ms) {
				continue;
			}
			uint32_t arr_len = 0; /* defer get_len */
			ms = new_metric_store(si->extraprops,
				si->container, si->schema,
				name, metric_type, arr_len,
				si->metric_names_amqp,
				si->metric_names_least);
			if (!ms) {
				ovis_log(mylog, OVIS_LINFO,"rabbitv3: open_store fails\n");
				goto err5;
			}
			idx_add(idx, name, strlen(name), ms);
			LIST_INSERT_HEAD(list, ms, entry);
		}
		idx_add(store_idx, (void *)key, strlen(key), si);
	}
	store_count++;
	goto out;
err5:
	ovis_label_set_destroy(si->metric_names_amqp);
	ovis_label_set_destroy(si->metric_names_least);
	ms = LIST_FIRST(&si->ms_list);
	while (ms) {
		LIST_REMOVE(ms, entry);
		destroy_metric_store(ms);
		ms = LIST_FIRST(&si->ms_list);
	}
	LIST_FIRST(&si->ms_list) = NULL;
	ms = LIST_FIRST(&si->meta_list);
	while (ms) {
		LIST_REMOVE(ms, entry);
		destroy_metric_store(ms);
		ms = LIST_FIRST(&si->meta_list);
	}
	LIST_FIRST(&si->meta_list) = NULL;
err4:
	free(si->container);
err3:
	idx_destroy(si->meta_idx);
err2:
	idx_destroy(si->ms_idx);
err1:
	free(si->key);
	free(si);
	si = NULL;
out:
	pthread_mutex_unlock(&cfg_lock);
	return si;
}

static int
store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh, ldms_set_t set, int *metric_arry, size_t metric_count)
{
	struct rabbitv3_store_instance *si;
	int i;
	int rc = 0;
	int last_rc = 0;
	if (!_sh) {
		return EINVAL;
	}
	si = _sh;
	if (!conn) {
		if (!si->conn_errs) {
			ovis_log(mylog, OVIS_LDEBUG,
			"Store called before successful broker connection. "
			"%s %s\n",si->container, si->schema);
			si->conn_errs++;
		}
		return EINVAL;
	}
	si->conn_errs = 0;

	const struct ldms_timestamp _ts = ldms_transaction_timestamp_get(set);
	const struct ldms_timestamp *ts = &_ts;
	uint64_t comp_id;
	int err = 0;
	pthread_mutex_lock(&cfg_lock); /* because of async close, need lock here ?*/

	if (metric_count != si->metric_count) {
		/* count check is a heuristic that works for common practice.
		We need to extend the store api to make this exact and
		efficient by having a per set store context pointer.
		A proper map based on producer id might work, however, but
		would be much more efficient kept on the caller side than here.
		(caller scope eliminates the search in this scope).
		*/
		err = update_metrics(si, set, metric_arry, metric_count);
		si->write_meta = true;
	}

	if (si->metainterval) {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		if (si->metainterval && si->next_meta <= tv.tv_sec) {
			si->write_meta = true;
			si->next_meta += si->metainterval;
		}
	}

	if (err != 0 || metric_count != si->metric_count) {
		ovis_log(mylog, OVIS_LERROR,"rabbitv3: Cannot fix count mismatch: "
		       "%d vs store() with metric_count %zu long. \n",
		       si->metric_count, metric_count);
		pthread_mutex_unlock(&cfg_lock);
		return ENOMEM;
	}

	int compid_index = ldms_metric_by_name(set, LDMSD_COMPID);
	if (! compid_index ||
		ldms_metric_type_get(set,compid_index) != LDMS_V_U64) {
	/* ovis_log(mylog, OVIS_LDEBUG,"rabbitv3: no u64 component_id found.\n"); */
		comp_id = 0;
	} else {
		comp_id = ldms_metric_get_u64(set, compid_index);
	}

	int flagstart = 1;
	if (si->write_meta) {
		flagstart = 0;
		/* Note: the time-based repetition check assumes the
		metadata will be the same across all producers. If this is false,
		the store api or this plugin has to keep track of not just
		metrics but individual producers with a bit of context.
		In the case of component_id, it is obviously false, but
		we make a special case of that.
		*/
	}

	int flag[2] = { LDMS_MDESC_F_META, LDMS_MDESC_F_DATA };
	struct rabbitv3_metric_store *ms;
	int last_errno = 0;
	int k;
	for (k = flagstart; k < 2; k++) {
		for (i = 0; i < metric_count; i++) {
			int flags = ldms_metric_flags_get(set, i);
			if (flags != flag[k]) {
				continue;
			}
			int metric_id = metric_arry[i];
			// comp_id = ldms_metric_user_data_get(set, metric_id);
			char *name = (char *)ldms_metric_name_get(set,metric_id);
			if (0 == strcmp(name, LDMSD_COMPID)) {
				continue;
			}
			idx_t idx;
			if (flags == LDMS_MDESC_F_META) {
				idx = si->meta_idx;
			} else {
				idx = si->ms_idx;
			}
			ms = idx_find(idx, name, strlen(name));
			if (!ms) {
				ovis_log(mylog, OVIS_LDEBUG,
				       "Error: received unexpected metric %s\n",
					name);
				continue;
			}
			uint32_t arr_len = ldms_metric_array_get_len(set,
				metric_id);
			if (arr_len > ms->array_len) {
				ms->message_cap = 0;
				free(ms->message);
				ms->message = NULL;
			}
			if (! ms->message_cap) {
				enum ldms_value_type metric_type =
					ldms_metric_type_get(set,metric_id);
				int aerr = alloc_message(ms, metric_type,
					arr_len);
				if (aerr == 1) {
					last_errno = ENOMEM;
					break;
				}
				if (aerr == 2) {
					last_errno = EINVAL;
					break;
				}
			}
			rc = write_amqp(ms, ts, comp_id, set, metric_id);
			if (rc < 0) {
				last_errno = errno;
				last_rc = rc;
				ovis_log(mylog, OVIS_LDEBUG,"Error %d: %s at %s:%d\n",
				       last_errno, STRERROR(last_errno),
					__FILE__, __LINE__);
				// fixme: probably need to handle close/reopen here if server dies.
			}
		}
		if (last_errno) {
			errno = last_errno;
		}
	}

	pthread_mutex_unlock(&cfg_lock);
	return last_rc;
}

static void close_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	pthread_mutex_lock(&cfg_lock);
	struct rabbitv3_store_instance *si = _sh;
	if (!_sh || (si && si->metric_count < 0)) {
		pthread_mutex_unlock(&cfg_lock);
		return;
	}

	ovis_label_set_destroy(si->metric_names_amqp);
	ovis_label_set_destroy(si->metric_names_least);
	struct rabbitv3_metric_store *ms;
	ms = LIST_FIRST(&si->ms_list);
	while (ms) {
		LIST_REMOVE(ms, entry);
		destroy_metric_store(ms);
		ms = LIST_FIRST(&si->ms_list);
	}

	ms = LIST_FIRST(&si->meta_list);
	while (ms) {
		LIST_REMOVE(ms, entry);
		destroy_metric_store(ms);
		ms = LIST_FIRST(&si->meta_list);
	}

	idx_delete(store_idx, (void *)(si->key), strlen(si->key));
	free(si->key);
	free(si->container);
	free(si->schema);
	idx_destroy(si->ms_idx);
	idx_destroy(si->meta_idx);
	si->metric_count = -1;
	si->meta_count = -1;
	si->data_count = -1;
	free(si);
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
	ovis_log(mylog, OVIS_LINFO,"Loading support for rabbitmq amqp version%s\n",
			amqp_version());
	rabbit_store_pi_log_set(mylog);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	/* What contract is this supposed to meet. Shall close(sh) have
	been called for all existing sh already before this is reached.?
	*/
}

struct ldmsd_store ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_STORE,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.open = open_store,
	.store = store,
	.close = close_store,
};

static void __attribute__ ((constructor)) store_rabbitv3_init();
static void store_rabbitv3_init()
{
	store_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) store_rabbitv3_fini(void);
static void store_rabbitv3_fini()
{
	cleanup_amqp();
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(store_idx);
}
