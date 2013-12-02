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
#ifndef ME_PRIV_H_
#define ME_PRIV_H_

#include <pthread.h>

#include <coll/idx.h>

#include "me.h"
#include "me_interface.h"
#include "model_manager.h"

void me_cleanup(int x);

typedef struct model_policy_list *model_policy_list_t;
typedef struct producer_list *producer_list_t;
typedef struct consumer_list *consumer_list_t;
typedef struct store_list *store_list_t;

model_policy_list_t get_model_policy_list();
producer_list_t get_producer_list();
consumer_list_t get_consumer_list();
store_list_t get_store_list();

/*
 * =============================================================
 * Output buffer
 * =============================================================
 */

/**
 * \struct me_output_buffer
 * \brief buffer for outputs
 *
 * A buffer for each consumer
 */
struct me_output_queue {
	struct me_oqueue oqueue;
	int size;
	pthread_mutex_t lock;
	pthread_cond_t output_avai;
};

/**
 * \brief Add a new output to the output buffer
 *
 * \param[in] output New output to add to the buffer
 * \param[in/out] q_array Array of output queues into which \c output will be added.
 * \param[in] num_queues The number of queues in the \c q_array
 */
void add_output(me_output_t output, struct me_output_queue *q_array,
						int num_queues);

/**
 * \brief Get the oldest output in the buffer
 *
 * \param[in] intf A handle to either me_consumer or me_store
 * \return an output value (NOTE: it is NOT a pointer)
 */
struct me_output *get_output_consumer(struct me_consumer *csm);

struct me_output *get_output_store(struct me_store *store);

void destroy_output(struct me_output *output);

void init_consumer_output_queue(struct me_consumer *csm);

void init_store_output_queue(struct me_store *strore);

/*
 * =============================================================
 * Plugins
 * =============================================================
 */

/**
 * \brief Load a plugin in the ME module
 */
struct me_plugin *me_new_plugin(char *plugin_name);

/**
 * \brief Find a plugin in the existing list
 */
struct me_plugin *me_find_plugin(char *plugin_name);
#endif /* ME_PRIV_H_ */
