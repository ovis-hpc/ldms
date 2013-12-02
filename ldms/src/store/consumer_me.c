/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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

#include <inttypes.h>
#include <malloc.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <coll/idx.h>
#include <zap/zap.h>
#include "ldms.h"
#include "ldmsd.h"

static idx_t me_idx;
static ldmsd_msg_log_f msglog;

static char *host;
static uint16_t port;
static char *xprt;
static zap_t zap;
static zap_ep_t zep;

static enum {
	CSM_ME_DISCONNECTED = 0,
	CSM_ME_CONNECTING,
	CSM_ME_CONNECTED
} state;

#pragma pack(4)
struct me_msg {
	enum me_input_type {
		ME_INPUT_DATA = 0,
		ME_NO_DATA
	} tag;
	uint64_t metric_id;
	struct timeval timestamp;
	double value;
};
#pragma pack()

struct me_store_instance {
	struct ldmsd_store *store;
	char *container;
	void *ucontext;
};

pthread_mutex_t cfg_lock;

zap_mem_info_t get_zap_mem_info()
{
	return NULL;
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	pthread_mutex_lock(&cfg_lock);
	char *value;
	value = av_value(avl, "host");
	if (!value)
		goto err;

	host = strdup(value);

	value = av_value(avl, "port");
	if (!value)
		goto err;

	port = atoi(value, NULL, 10);

	value = av_value(avl, "xprt");
	if (!value)
		goto err;
	xprt = strdup(value);

	zap_err_t zerr = 0;
	zerr = zap_get(xprt, &zap, msglog, get_zap_mem_info);
	if (zerr) {
		msglog("me: failed to create a zap. Error '%d'\n",
							zerr);
		free(host);
		free(xprt);
	}
	state = CSM_ME_DISCONNECTED;
	pthread_mutex_unlock(&cfg_lock);
	return zerr;
err:
	if (host)
		free(host);
	pthread_mutex_unlock(&cfg_lock);
	return EINVAL;
}

static void term(void)
{
}

static const char *usage()
{
	return  "	config name=consumer_me host=<host> porot=<port> xprt=<xprt>\n"
		"	   - Set the host and port of the M.E. and choose the transport.\n"
		"	   host     Host name that M.E. runs on.\n"
		"	   port     Listener port of M.E.\n"
		"	   xprt     A Zap transport (sock,rdma,ugni)\n";
}

static ldmsd_store_handle_t
get_store(const char *container)
{
	ldmsd_store_handle_t sh;
	pthread_mutex_lock(&cfg_lock);
	sh = idx_find(me_idx, (void *)container, strlen(container));
	pthread_mutex_unlock(&cfg_lock);
	return sh;
}

static void *get_ucontext(ldmsd_store_handle_t _sh)
{
	struct me_store_instance *si = _sh;
	return si->ucontext;
}


static void me_zap_cb(zap_ep_t zep, zap_event_t ev)
{
	switch (ev->type) {
	case ZAP_EVENT_DISCONNECTED:
	case ZAP_EVENT_CONNECT_ERROR:
	case ZAP_EVENT_REJECTED:
		zap_close(zep);
		state = CSM_ME_DISCONNECTED;
		break;
	case ZAP_EVENT_CONNECTED:
		state = CSM_ME_CONNECTED;
		break;
	default:
		break;
	}
}

static int connect_me()
{
	state = CSM_ME_CONNECTING;
	struct addrinfo *ai;
	int rc;
	char p[16];
	sprintf(p, "%d", port);
	rc = getaddrinfo(host, p, NULL, &ai);
	if (rc) {
		msglog("me: getaddrinfo error %d\n", rc);
		return rc;
	}

	zap_err_t zerr;
	zerr = zap_new(zap, &zep, me_zap_cb);
	if (zerr) {
		msglog("me: failed to create a zap endpoint. "
					"Error '%d'\n", zerr);
		return zerr;
	}

	static int is_failed_before = 0;
	zerr = zap_connect(zep, ai->ai_addr, ai->ai_addrlen);
	if (zerr) {
		if (!is_failed_before) {
			msglog("me: zap_connect error %d: %s\n", zerr,
					zap_err_str(zerr));
			is_failed_before = 1;
		}
		zap_close(zep);
		state = CSM_ME_DISCONNECTED;
		return zerr;
	}
	is_failed_before = 0;
	return 0;
}

static ldmsd_store_handle_t
new_store(struct ldmsd_store *s, const char *comp_type, const char *container,
		struct ldmsd_store_metric_index_list *mlist, void *ucontext)
{
	struct me_store_instance *si;
	struct me_metric_store *ms;

	zap_err_t zerr;
	int rc;

	pthread_mutex_lock(&cfg_lock);

	if (state == CSM_ME_DISCONNECTED)
		connect_me();

	si = idx_find(me_idx, (void *)container, strlen(container));
	if (!si) {
		si = calloc(1, sizeof(*si));
		if (!si)
			goto err;

		si->ucontext = ucontext;
		si->store = s;
		si->container = strdup(container);
		if (!si->container)
			goto err1;

		idx_add(me_idx, (void *)container, strlen(container), si);
	}
	pthread_mutex_unlock(&cfg_lock);
	return si;
err1:
	free(si);
err:
	pthread_mutex_unlock(&cfg_lock);
	return NULL;
}

static int me_get_ldsm_metric_value(ldms_metric_t m, double *v)
{
	enum ldms_value_type type = ldms_get_metric_type(m);
	switch (type) {
	case LDMS_V_S8:
		*v = ldms_get_s8(m);
		break;
	case LDMS_V_U8:
		*v = ldms_get_u8(m);
		break;
	case LDMS_V_S16:
		*v = ldms_get_s16(m);
		break;
	case LDMS_V_U16:
		*v = ldms_get_u16(m);
		break;
	case LDMS_V_S32:
		*v = ldms_get_s32(m);
		break;
	case LDMS_V_U32:
		*v = ldms_get_u32(m);
		break;
	case LDMS_V_S64:
		*v = ldms_get_s64(m);
		break;
	case LDMS_V_U64:
		*v = ldms_get_u64(m);
		break;
	case LDMS_V_F:
		*v = ldms_get_float(m);
		break;
	case LDMS_V_D:
		*v = ldms_get_double(m);
		break;
	default:
		msglog("me: not support ldms_value_type '%s'\n", type);
		return -1;
	}
	return 0;

}

static int
send_to_me(ldmsd_store_handle_t _sh, ldms_set_t set, ldms_mvec_t mvec)
{
	int rc = 0;
	zap_err_t zerr;
	struct me_store_instance *si;
	si = _sh;

	const struct ldms_timestamp *ts = ldms_get_timestamp(set);

	if (state == CSM_ME_DISCONNECTED) {
		connect_me();
		return 0;
	}

	if (state != CSM_ME_CONNECTED)
		return 0;

	struct me_msg msg;
	int has_data = ldms_is_set_connected(set);
	if (has_data)
		msg.tag = htonl(ME_INPUT_DATA);
	else
		msg.tag = htonl(ME_NO_DATA);

	msg.timestamp.tv_sec = htonl(ts->sec);
	msg.timestamp.tv_usec = htonl(ts->usec);
	int i;
	for (i = 0; i < mvec->count; i++) {
		msg.metric_id = htobe64(ldms_get_user_data(mvec->v[i]));
		if (has_data) {
			if (me_get_ldsm_metric_value(mvec->v[i], &msg.value))
				continue;
		}
		zerr = zap_send(zep, (void *)&msg, sizeof(msg));
		if (zerr) {
			msglog("me: zap_send error '%d': %s.\n", zerr,
						zap_err_str(zerr));
			return zerr;
		}
	}
	return 0;
}

static int flush_store(ldmsd_store_handle_t _sh)
{
	/* do nothing */
}

static void close_store(ldmsd_store_handle_t _sh)
{
	/* do nothing */
}

static void destroy_store(ldmsd_store_handle_t _sh)
{
	struct me_store_instance *si = _sh;
	idx_delete(me_idx, (void *)si->container, strlen(si->container));
	free(si->container);
	free(si);
}

static struct ldmsd_store consumer_me = {
	.base = {
			.name = "me",
			.term = term,
			.config = config,
			.usage = usage,
	},
	.get = get_store,
	.new = new_store,
	.destroy = destroy_store,
	.get_context = get_ucontext,
	.store = send_to_me,
	.flush = flush_store,
	.close = close_store,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &consumer_me.base;
}

static void __attribute__ ((constructor)) consumer_me_init();
static void consumer_me_init()
{
	me_idx = idx_create();
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) consumer_me_fini(void);
static void consumer_me_fini()
{
	pthread_mutex_destroy(&cfg_lock);
	idx_destroy(me_idx);
}
