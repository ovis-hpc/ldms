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

#include <netinet/in.h>
#include <sys/errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <endian.h>
#include <arpa/inet.h>
#include <zap/zap.h>
#include <inttypes.h>

#include "me_interface_priv.h"

pthread_mutex_t xprt_list_lock;
LIST_HEAD(xprt_list_h, me_xprt) xprt_list;

static int is_init = 0;

pthread_mutex_t producer_lock = PTHREAD_MUTEX_INITIALIZER;
struct producer_list producer_list;

producer_list_t get_producer_list()
{
	return &producer_list;
}

pthread_mutex_t consumer_lock = PTHREAD_MUTEX_INITIALIZER;
struct consumer_list consumer_list;

consumer_list_t get_consumer_list()
{
	return &consumer_list;
}

pthread_mutex_t store_lock = PTHREAD_MUTEX_INITIALIZER;
struct store_list store_list;

store_list_t get_store_list()
{
	return &store_list;
}

void add_consumer(struct me_consumer *csm)
{
	pthread_mutex_lock(&consumer_lock);
	LIST_INSERT_HEAD(&consumer_list, csm, entry);
	pthread_mutex_unlock(&consumer_lock);
}

void add_store(struct me_store *strg)
{
	pthread_mutex_lock(&store_lock);
	LIST_INSERT_HEAD(&store_list, strg, entry);
	pthread_mutex_unlock(&store_lock);
}

void close_interface_plugin(me_interface_plugin_t intf_pi)
{
	if (intf_pi->zep)
		zap_close(intf_pi->zep);

	switch (intf_pi->type) {
	case ME_PRODUCER:
		LIST_REMOVE((struct me_producer *) intf_pi, entry);
		decrease_producer_counts();
		break;
	case ME_CONSUMER:
		LIST_REMOVE((struct me_consumer *) intf_pi, entry);
		decrease_consumer_counts();
		break;
	case ME_STORE:
		LIST_REMOVE((struct me_store *) intf_pi, entry);
		decrease_store_counts();
		break;
	default:
		me_log("Invalid interface type '%d'.\n", intf_pi->type);
		assert(0);
		break;
	}
	if (intf_pi->base.term)
		intf_pi->base.term();
}

void *evaluate_complete_consumer_cb(void *_csm)
{
	struct me_output *output;
	struct me_consumer *csm;
	while (1) {
		csm = (struct me_consumer *)_csm;
		output = get_output_consumer(csm);
		csm->process_output_data(csm, output);
		free(output);
	}
	return NULL;
}

void *evaluate_complete_store_cb(void *_store)
{
	struct me_store *store;
	struct me_output *output;
	while (1) {
		store = (struct me_store *)_store;
		output = get_output_store(store);
		store->store(store, output);
		free(output);
	}
	return NULL;
}

void process_input_msg(zap_ep_t zep, zap_event_t ev)
{
	struct me_msg *msg = (struct me_msg *)ev->data;
	struct me_input *input = malloc(sizeof(*input));
	input->metric_id = be64toh(msg->metric_id);
	input->ts.tv_sec = ntohl(msg->timestamp.tv_sec);
	input->ts.tv_usec = ntohl(msg->timestamp.tv_usec);
	input->value = msg->value;
	input->type = ntohl(msg->tag);

	/* Add the input to the input queue. */
	add_input(input);
}

void init_core_interface()
{
	LIST_INIT(&producer_list);
	LIST_INIT(&consumer_list);
	LIST_INIT(&store_list);
}

zap_mem_info_t get_zap_mem_info()
{
	return NULL;
}

void listen_cb(zap_ep_t zep, zap_event_t ev)
{
	zap_err_t z_err;
	struct sockaddr_in lsin = {0};
	struct sockaddr_in rsin = {0};
	socklen_t slen;
	zap_get_name(zep, (void *)&lsin, (void *)&rsin, &slen);
	char *ip_s = inet_ntoa(rsin.sin_addr);

	switch (ev->type) {
	case ZAP_EVENT_CONNECT_REQUEST:
		z_err = zap_accept(zep, listen_cb);
		me_log("Accept a connection: %s.\n", ip_s);
		if (z_err)
			me_log("Failed to accept a connection: %s."
				"Error '%d'.\n", ip_s, z_err);
		break;
	case ZAP_EVENT_DISCONNECTED:
		me_log("Disconnected from %s.\n", ip_s);
		zap_close(zep);
		break;
	case ZAP_EVENT_RECV_COMPLETE:
		process_input_msg(zep, ev);
		break;
	case ZAP_EVENT_CONNECTED:
		me_log("Connected to %s.\n", ip_s);
		break;
	default:
		me_log("Invalid Zap Event: %s\n", ip_s);
		assert(0);
		break;
	}
}

int me_listen_on_transport(char *xprt, uint16_t port)
{
	zap_err_t z_err;

	if (is_init) {
		init_core_interface();
		is_init = 1;
	}

	zap_t z_listener;
	z_err = zap_get(xprt, &z_listener, me_log, get_zap_mem_info);
	if (z_err) {
		me_log("Error %d: Could not load the '%s' transport.\n",
				z_err, xprt);
		exit(1);
	}

	zap_ep_t zep_listener;
	z_err = zap_new(z_listener, &zep_listener, listen_cb);
	if (z_err) {
		me_log("Could not create the zap endpoint for the listener.\n");
		exit(1);
	}

	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = 0;
	sin.sin_port = htons(port);

	me_log("Listening on transport %s:%" PRIu16 "\n", xprt, port);

	z_err = zap_listen(zep_listener, (struct sockaddr *)&sin, sizeof(sin));
	if (z_err) {
		me_log("Error %d: zap_listen failed.\n", z_err);
		me_cleanup(7);
	}
	return 0;
}
