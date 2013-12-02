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

#ifndef ME_INTERFACE_H_
#define ME_INTERFACE_H_

#include <sys/queue.h>
#include <zap/zap.h>

#define ME_MAX_BUFFER 256

typedef struct me_xprt *me_xprt_t;

/*
 * \struct me_interface_plugin
 *
 * \brief the interface plugin struct
 */
typedef struct me_interface_plugin *me_interface_plugin_t;
struct me_interface_plugin {
	struct me_plugin base;
	zap_ep_t zep; /* Zap endpoint */

	/** interface type */
	enum me_intf_type {
		ME_PRODUCER,
		ME_CONSUMER,
		ME_STORE
	} type;

	/**
	 * \brief Initialize the plugin instance & configure
	 *
	 * If the plugin needs a connection to a peer, it needs to setup
	 * the connection before returning an instance and give the handle of
	 * a Zap endpoint to the instance.
	 */
	void *(*get_instance)(struct attr_value_list *avl,
			struct me_interface_plugin *pi);
};

LIST_HEAD(me_interface_pi_list, me_interface_plugin) me_interface_pi_h;

struct me_producer {
	struct me_interface_plugin *intf_base;
	uint16_t producer_id;

	/**
	 * \brief Format the input data from the producer
	 *
	 * @param[in] buffer the buffer received from the producer
	 * @param[out] input the ME-formated input
	 */
	int (*format_input_data)(char *buffer, 	me_input_t input);
	LIST_ENTRY(me_producer) entry;
};

LIST_HEAD(producer_list, me_producer);

struct me_consumer {
	struct me_interface_plugin *intf_base;

	uint16_t consumer_id;

	/**
	 * \brief Format the output in the consumer requirement
	 *
	 * @param[in] output the ME output to be formated
	 *
	 * The function should log any error.
	 */
	void (*process_output_data)(struct me_consumer *csm,
					me_output_t output);

	LIST_ENTRY(me_consumer) entry;
};

LIST_HEAD(consumer_list, me_consumer);

/**
 * \brief Add consumer \c csm to the consumer list
 */
void add_consumer(struct me_consumer *csm);

struct me_store {
	struct me_interface_plugin *intf_base;
	const char *container;

	/**
	 * The store ID
	 */
	uint16_t store_id;
	int dirty_count;
	/**
	 * \brief Store an output
	 *
	 * \param[in] strg The handle of the store
	 * \param[in] output An me_output to be stored
	 * \return 0 on success.
	 *
	 * Store outputs without flushing the store
	 */
	void (*store)(struct me_store *strg, struct me_output *output);

	/**
	 * \brief Flush the store
	 *
	 * \param strg The handle of the store
	 * \return 0 on success.
	 */
	int (*flush_store)(struct me_store *strg);

	/**
	 * \brief Destroy the store handle
	 *
	 * \param[in] strg The handle of the store to be closed
	 */
	void (*destroy_store)(struct me_store *strg);

	LIST_ENTRY(me_store) entry;
};

LIST_HEAD(store_list, me_store);

/**
 * \brief Add consumer \c strg to the store list
 */
void add_store(struct me_store *strg);

void *evaluate_complete_consumer_cb(void *_consumer);

void *evaluate_complete_store_cb(void *_store);

#endif /* ME_INTERFACE_H_ */
