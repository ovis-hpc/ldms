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
/*
 * consumer_kmd.c
 *
 *  Created on: Aug 27, 2013
 *      Author: nichamon
 */

#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <netdb.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <ovis_util/util.h>
#include <zap/zap.h>
#include <malloc.h>
#include <stdlib.h>

#include "me.h"
#include "me_interface.h"

static me_log_fn msglog;
static pthread_mutex_t cfg_lock;

static char *host;
static char *xprt;
static int port;
static int kmd_is_connected;

#pragma pack(4)
struct KMD_msg {
	uint16_t model_id;
	uint64_t metric_id;
	uint8_t level;
	uint32_t sec;
	uint32_t usec;
};
#pragma pack()

struct me_consumer_KMD {
	struct me_consumer base;
	zap_t zap;
	zap_ep_t zep;
};

static const char *usage()
{
	return	"	config name=kmd host=<host> xprt=<xprt> port=<port>.\n"
		"	  - Set the host, xprt and port.\n"
		"	  <host>	The host name of Komondor.\n"
		"	  <xprt>	Transport used for the communication.\n"
		"	  <port>	Port number.\n";
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	pthread_mutex_lock(&cfg_lock);
	value = av_value(avl, "host");
	if (!value)
		goto einval;
	host = strdup(value);

	value = av_value(avl, "xprt");
	if (!value)
		goto einval;
	xprt = strdup(value);

	char *port_s = av_value(avl, "port");
	if (!port_s)
		goto einval;
	port = atoi(port_s);
	pthread_mutex_unlock(&cfg_lock);
	return 0;
einval:
	pthread_mutex_unlock(&cfg_lock);
	return EINVAL;
}

static void term()
{
	pthread_mutex_lock(&cfg_lock);
	free(host);
	free(xprt);
	pthread_mutex_unlock(&cfg_lock);
}

zap_mem_info_t get_zap_mem_info()
{
	return NULL;
}

static int connect_kmd(struct me_consumer_KMD *kmd);
static void kmd_send_output(struct me_consumer *csm,
				me_output_t output)
{
	struct me_consumer_KMD *kmd;
	kmd = (struct me_consumer_KMD *) csm;
	zap_err_t zerr;
	struct KMD_msg msg;
	msg.level = output->level;
	msg.model_id = htons(output->model_id);
	msg.sec = htonl(output->ts.tv_sec);
	msg.usec = htonl(output->ts.tv_usec);
	msg.metric_id = htobe64(output->metric_ids[0]);

	pthread_mutex_lock(&cfg_lock);
	if (!kmd_is_connected) {
		connect_kmd(kmd);
		pthread_mutex_unlock(&cfg_lock);
		return;
	}
	pthread_mutex_unlock(&cfg_lock);

	if (zerr = zap_send(kmd->zep, &msg, sizeof(msg))) {
		msglog("kmd: Error '%d' to send output to "
				"Komondor.\n", zerr);
	}
}

static void zap_cb(zap_ep_t zep, zap_event_t ev)
{
	pthread_mutex_lock(&cfg_lock);
	switch (ev->type) {
	case ZAP_EVENT_DISCONNECTED:
		zap_close(zep);
		kmd_is_connected = 0;
		msglog("kmd: Disconnected from ME.\n");
		break;
	case ZAP_EVENT_CONNECT_ERROR:
		zap_close(zep);
		kmd_is_connected = 0;
		msglog("kmd: Connection error.\n");
		break;
	case ZAP_EVENT_CONNECTED:
		kmd_is_connected = 1;
		msglog("kmd: Connected to ME.\n");
		break;
	case ZAP_EVENT_REJECTED:
		zap_close(zep);
		kmd_is_connected = 0;
		msglog("kmd: The connect request was rejected.\n");
		break;
	default:
		assert(0);
		break;
	}
	pthread_mutex_unlock(&cfg_lock);
}

static int connect_kmd(struct me_consumer_KMD *kmdi)
{
	zap_err_t zerr;
	zerr = zap_new(kmdi->zap, &kmdi->zep, zap_cb);
	if (zerr) {
		msglog("kmd: Failed to create Zap endpoint. Error %d.\n", zerr);
		return -1;;
	}

	struct hostent *h;
	struct sockaddr_in sin;
	static int is_failed = 0;

	h = gethostbyname(host);
	if (!h) {
		msglog("kmd: Error '%d' resolving hostname '%s'.\n",
				host, h_errno);
		zap_close(kmdi->zep);
		return -1;
	}

	if (h->h_addrtype != AF_INET) {
		msglog("kmd: Hostname '%s' not supported.\n", host);
		zap_close(kmdi->zep);
		return -1;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_addr.s_addr = *(unsigned int *)(h->h_addr_list[0]);
	sin.sin_family = h->h_addrtype;
	sin.sin_port = htons(port);

	zerr = zap_connect(kmdi->zep, (struct sockaddr *)&sin, sizeof(sin));
	if (zerr) {
		if (!is_failed)
			msglog("kmd: Failed to connect to Komondor. "
					"Error '%d'.\n", zerr);
		is_failed = 1;
		zap_close(kmdi->zep);
		return zerr;
	}
	is_failed = 0;
	kmd_is_connected = 1;
	return 0;
}

static void *get_instance(struct attr_value_list *avlist,
		struct me_interface_plugin *pi)
{
	int rc;
	zap_err_t zerr;
	struct me_consumer_KMD *kmdi = malloc(sizeof(*kmdi));

	kmdi->base.intf_base = pi;
	kmdi->base.process_output_data = kmd_send_output; /* TODO: assign this */

	zerr = zap_get(xprt, &kmdi->zap, msglog, get_zap_mem_info);
	if (zerr) {
		msglog("kmd: Failed to create Zap handle. "
				"Error %d.\n", zerr);
		goto err;
	}

	if (rc = connect_kmd(kmdi))
		goto err2;

	return (void *) kmdi;
err2:
	zap_close(kmdi->zep);
err1:
	free(kmdi->zap);
err:
	free(kmdi);
	return NULL;
}

static struct me_interface_plugin kmd = {
	.base = {
			.name = "consumer_kmd",
			.type = ME_INTERFACE_PLUGIN,
			.usage = usage,
			.config = config,
			.term = term
	},
	.type = ME_CONSUMER,
	.get_instance = get_instance,
};

struct me_plugin *get_plugin(me_log_fn log_fn)
{
	kmd.base.intf_pi = &kmd;
	msglog = log_fn;
	return &kmd.base;
}

static void __attribute__ ((constructor)) consumer_kmd_init();
static void consumer_kmd_init()
{
	pthread_mutex_init(&cfg_lock, NULL);
}

static void __attribute__ ((destructor)) consumer_kmd_fini(void);
static void consumer_kmd_fini()
{
	pthread_mutex_destroy(&cfg_lock);
}
