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

#ifndef ME_INTERFACE_PRIV_H_
#define ME_INTERFACE_PRIV_H_

#include <pthread.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include "me_priv.h"

#define ME_MAX_TRANSPORT_NAME_LEN 16

/*
 * TODO: document this
 */
struct me_xprt {

	char name[ME_MAX_TRANSPORT_NAME_LEN];
	socklen_t ss_len;
	pthread_mutex_t lock;
	int connected;
	int ref_count;
	LIST_ENTRY(me_xprt) xprt_entry;

	/* Listen for incoming connection requests */
	int (*listen)(struct me_xprt *, struct sockaddr *sa,
						socklen_t sa_len);
	/* Setup a connection after a client requests a connection. */
	int (*setup_connection)(struct me_xprt *x);
	/* Receive inputs. Return the pointer to the input */
	int (*receive_data)(struct me_xprt *, struct me_msg *msg);
	/* Close the connection */
	void (*close)(struct me_xprt *);
	/* Destroy the transport instance */
	void (*destroy)(struct me_xprt *);
	/* Send a request/reply */
	int (*send)(struct me_xprt *, void *, size_t);
	/* Get the interface plug-in */
	me_interface_plugin_t (*get_intf_pi)(const char *pi_name);

	/* Log function*/
	me_log_fn log;
	/* Pointer to the transport's private data */
	void *private;
};

/**
 * \enum conn_status
 */
enum conn_status {
	CONNN_ERROR = -1,
	CONN_IDEL,
	CONN_CONNECTING,
	CONN_CONNECTED,
	CONN_CLOSING,
	CONN_CLOSED
};

/**
 * \brief Setup a listener
 */
int xprt_listen(struct me_xprt *_x, struct sockaddr *sa, socklen_t sa_len);

/**
 * \brief Create a transport of the given type
 *
 * name: Name of the transport type (ONLY 'sock' is supported)
 */
struct me_xprt *me_create_xprt(const char *name);

void me_close_xprt(struct me_xprt *_x);

void me_release_xprt(struct me_xprt *_x);

int me_listen_on_transport(char *xprt, uint16_t port);

/*
 * TODO: implement cleanup interface when exit
 * free all the socket file descriptors for each thread
 * clean up all threads
 */
void me_cleanup_interface();

/**
 * \brief Call the transport-specified receive input function to wait for an input.
 * 	  Upon an input arrival, pass through the input to the producer interface to
 * 	  re-format the input and out it to the input buffer.
 *
 * @param[in]	x	the transport of the producer
 */
void *me_recieve_input(void *_x);

/**
 * \brief Send an output to a consumer when there exists an output in the output buffer
 */
void *send_output_cb(void *_x);


/**
 * \brief Load the specified interface plugin
 *
 * After a connection request from a client, this function is called
 * to load the given interface plugin and create a new thread to handle
 * the communication between ME and the Client.
 *
 * @param[in]	zep	A Zap endpoint
 * @param[in]	intf_name	the given interface name
 * @param[in]	log_fn	the log function
 */
int load_interface_plugin(zap_ep_t zep, char *intf_name);


#endif /* ME_INTERFACE_PRIV_H_ */
