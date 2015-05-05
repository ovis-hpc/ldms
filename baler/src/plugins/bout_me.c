/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2015 Sandia Corporation. All rights reserved.
 *
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
#include <stdio.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <zap/zap.h>
#include <sys/queue.h>

#include <event2/event.h>
#include <event2/thread.h>

#include "baler/butils.h"
#include "baler/bplugin.h"
#include "baler/boutput.h"

extern uint64_t *metric_ids;

#define BALER_METRIC_TYPE_ID 1

#pragma pack(4)
struct me_msg {
	enum me_input_type {
		ME_INPUT_DATA = 0,
		ME_NO_DATA
	} tag;	/**< Tag for the type of message */
	uint64_t metric_id; /**< unique ID for inputs */
	struct timeval timestamp; /**< Timestamp of the input */
	double value;
};
#pragma pack()

struct bout_me_plugin {
	struct boutplugin base; /**< base */
	zap_t zap; /**< zap engine */
	char *host; /**< ME hostname */
	uint16_t port; /**< ME port */
	struct sockaddr_in sa; /**< resolved socket address */
	socklen_t sa_len; /**< length of \c sa, 0 if \c sa is not set */
	zap_ep_t ep; /**< zap end point */
	struct event *ev; /**< Event for interval reconnecting */
	pthread_mutex_t mutex;
	enum me_conn_state {
		BME_DISCONNECTED,
		BME_CONNECTING,
		BME_CONNECTED,
		BME_STOPPING,
		BME_STOPPED
	} conn_state;
	TAILQ_ENTRY(bout_me_plugin) conn_entry; /**< For re-connection */
};

/** Event base */
struct event_base *evbase;
pthread_t evbase_thread;

void bout_me_conn_cb(evutil_socket_t sd, short e, void *arg);

static void bout_me_zap_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

zap_mem_info_t bout_me_zap_mem_info(void)
{
	return NULL;
}

int bout_me_config(struct bplugin *this, struct bpair_str_head *arg_head)
{
	struct bout_me_plugin *m = (void*)this;
	struct bpair_str *bp;

	bp = bpair_str_search(arg_head, "host", NULL);
	if (!bp) {
		berr("bout_me_config: no host specified\n");
		return EINVAL;
	}
	m->host = strdup(bp->s1);
	if (!m->host) {
		return ENOMEM;
	}

	bp = bpair_str_search(arg_head, "port", NULL);
	if (bp) {
		m->port = atoi(bp->s1);
	} else {
		m->port = 55555;
	}

	char *xprt = "sock";
	zap_err_t zerr;
	bp = bpair_str_search(arg_head, "xprt", NULL);
	if (bp) {
		xprt = bp->s1;
	}
	zap_mem_info_fn_t x;
	m->zap = zap_get(xprt, bout_me_zap_log, bout_me_zap_mem_info);
	if (!m->zap) {
		berror("zap_get()");
		return ENOENT; /* consider new return code */
	}

	return 0;
}

void bout_me_reconnect(struct bout_me_plugin *m, time_t delay)
{
	int rc;
	pthread_mutex_lock(&m->mutex);
	if (m->conn_state == BME_STOPPING) {
		m->conn_state = BME_STOPPED;
		pthread_mutex_unlock(&m->mutex);
		return;
	}
	if (m->conn_state == BME_STOPPED) {
		pthread_mutex_unlock(&m->mutex);
		return;
	}
	m->conn_state = BME_DISCONNECTED;
	struct timeval tv = {delay, 0};
	event_add(m->ev, &tv);
	pthread_mutex_unlock(&m->mutex);
}

int bout_me_start(struct bplugin *this)
{
	struct bout_me_plugin *m = (void*)this;
	/* start connecting process */
	bout_me_reconnect(m, 1);
	return 0;
}

int bout_me_stop(struct bplugin *this)
{
	struct bout_me_plugin *m = (void*)this;
	pthread_mutex_lock(&m->mutex);
	m->conn_state = BME_STOPPING;
	pthread_mutex_unlock(&m->mutex);
	zap_close(m->ep);
	return 0;
}

int bout_me_free(struct bplugin *p)
{
	struct bout_me_plugin *m = (void*)p;
	/* Free this plugin-specific content first */
	free(m->host);
#if 0 /* Until zap_free is implemented */
	if (m->ep)
		zap_free(m->ep);
#endif
	if (m->ev)
		event_free(m->ev);
	/* Then free the plugin */
	bplugin_free(p);
	return 0;
}

int bout_me_process_output(struct boutplugin *this, struct boutq_data *odata)
{
	struct bout_me_plugin *m = (void*)this;
	pthread_mutex_lock(&m->mutex);
	if (m->conn_state != BME_CONNECTED) {
		pthread_mutex_unlock(&m->mutex);
		return ENOTCONN;
	}
	pthread_mutex_unlock(&m->mutex);
	struct me_msg msg;
	msg.metric_id = htobe64(metric_ids[odata->comp_id]);
	msg.timestamp.tv_sec = htonl(odata->tv.tv_sec);
	msg.timestamp.tv_usec = htonl(odata->tv.tv_usec);
	msg.tag = 0;
	msg.value = odata->msg->ptn_id;
	return zap_send(m->ep, &msg, sizeof(msg));
}

void bout_me_zap_cb(zap_ep_t zep, zap_event_t ev)
{
	struct bout_me_plugin *m = zap_get_ucontext(zep);
	switch (ev->type) {
	case ZAP_EVENT_CONNECT_ERROR:
		berr("zap connection error\n");
		zap_close(m->ep);
		bout_me_reconnect(m, 10);
		break;
	case ZAP_EVENT_REJECTED:
		berr("zap connection rejected\n");
		zap_close(m->ep);
		bout_me_reconnect(m, 10);
		break;
	case ZAP_EVENT_DISCONNECTED:
		berr("zap connection disconnected\n");
		zap_close(m->ep);
		bout_me_reconnect(m, 1);
		break;
	case ZAP_EVENT_CONNECTED:
		pthread_mutex_lock(&m->mutex);
		m->conn_state = BME_CONNECTED;
		pthread_mutex_unlock(&m->mutex);
		break;
	default:
		berr("Unexpected zap event: %d\n", ev->type);
	}
}

/**
 * This is an evtimer callback, handling reconnection task.
 */
void bout_me_conn_cb(evutil_socket_t sd, short e, void *arg)
{
	struct bout_me_plugin *m = arg;

	if (m->conn_state != BME_DISCONNECTED)
		return;

	int rc;
	zap_err_t zerr;

	if (!m->sa_len) {
		char cport[16];
		struct addrinfo *ai;
		struct addrinfo *ai_head;

		sprintf(cport, "%hu", m->port);
		rc = getaddrinfo(m->host, cport, NULL, &ai_head);
		if (rc) {
			berr("Cannot resolve host: %s\n", m->host);
			goto err;
		}
		ai = ai_head;
		while (ai && ai->ai_family != AF_INET) {
			ai = ai->ai_next;
		}
		/* now ai is either NULL or of type AF_INET */
		if (!ai) {
			berr("Cannot resolve INET address for host: %s\n",
					m->host);
			goto err;
		}
		m->sa_len = ai->ai_addrlen;
		memcpy(&m->sa, ai->ai_addr, ai->ai_addrlen);
		freeaddrinfo(ai_head);
	}

	m->ep = zap_new(m->zap, bout_me_zap_cb);
	if (!m->ep) {
		berr("zap_new error: %s\n", zap_err_str(zerr));
		goto err;
	}
	/* set ucontext to be ptr to plugin instance */
	zap_set_ucontext(m->ep, m);

	pthread_mutex_lock(&m->mutex);
	if (m->conn_state != BME_DISCONNECTED) {
		berr("bad connection state: %d\n", m->conn_state);
		pthread_mutex_unlock(&m->mutex);
		goto err;
	}
	m->conn_state = BME_CONNECTING;
	pthread_mutex_unlock(&m->mutex);

	zerr = zap_connect(m->ep, (void*)&m->sa, m->sa_len, NULL, 0);
	if (zerr) {
		berr("zap_connect error: %s\n", zap_err_str(zerr));
	}
err:
	return;
}

void *evbase_routine(void* arg)
{
	event_base_dispatch(evbase);
}

struct bplugin *create_plugin_instance()
{
	struct bout_me_plugin *m = calloc(1, sizeof(*m));
	if (!m)
		goto err0;
	m->base.base.name = strdup("bout_me");
	if (!m->base.base.name)
		goto err1;
	m->ev = evtimer_new(evbase, bout_me_conn_cb, m);
	if (!m->ev)
		goto err1;
	/* setting up functions */
	pthread_mutex_init(&m->mutex, NULL);
	m->base.base.config = bout_me_config;
	m->base.base.start = bout_me_start;
	m->base.base.stop = bout_me_stop;
	m->base.base.free = bout_me_free;
	m->base.process_output = bout_me_process_output;
	return &m->base.base;
err1:
	bout_me_free((void*)m);
err0:
	return NULL;
}

void bout_me_lib_constructor() __attribute__((constructor));
void bout_me_lib_constructor()
{
	static int initialized = 0;
	if (initialized)
		return;
	initialized = 1;
	int rc;
	rc = evthread_use_pthreads();
	if (rc) {
		berr("evthread_use_pthreads error %d\n", rc);
		exit(-1);
	}
	evbase = event_base_new();
	if (!evbase) {
		berr("Cannot create evbase.\n");
		exit(-1);
	}
	struct event *ev = event_new(evbase, -1, EV_READ | EV_PERSIST, NULL,
									NULL);
	rc = event_add(ev, NULL);
	if (rc) {
		berr("Cannot add keep alive event.\n");
		exit(-1);
	}
	rc = pthread_create(&evbase_thread, NULL, evbase_routine, NULL);
	if (rc) {
		berr("Cannot create evbase_thread.\n");
		exit(-1);
	}
}

static void bout_me_lib_destructor() __attribute__((destructor));
static void bout_me_lib_destructor()
{
	pthread_cancel(evbase_thread);
	event_base_loopbreak(evbase);
	event_base_free(evbase);
}

