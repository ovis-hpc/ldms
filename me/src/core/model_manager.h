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

#ifndef MODEL_MANAGER_H_
#define MODEL_MANAGER_H_

#include <sys/queue.h>
#include <coll/idx.h>
#include "me_priv.h"

/*
 * =============================================================
 * Input buffer
 * =============================================================
 */

/**
 * \brief Add a new input value to the input buffer
 *
 * @param[in] input the new input to add to the buffer
 */
void add_input(struct me_input *input);

/**
 * \brief Get the oldest input in the buffer
 *
 * \return an input value (NOTE: it is NOT a pointer)
 */
struct me_input *get_input();

void add_producer_counts();

void decrease_producer_counts();

uint16_t get_producer_counts();

void add_consumer_counts();

void decrease_consumer_counts();

uint16_t get_consumer_counts();

void add_store_counts();

void decrease_store_counts();

uint16_t get_store_counts();

/*
 * Model
 */
struct model_policy {
	int model_id; /**< Model ID */
	struct me_model_plugin *model_pi;	/**< model plugin object */
	struct me_model_cfg *cfg;	/**< model configuration */
	pthread_mutex_t cfg_lock;
	struct me_model_engine *m_engine;
	int refcount;
	pthread_mutex_t refcount_lock;
	LIST_ENTRY(model_policy) link;
};
typedef struct model_policy *model_policy_t;
LIST_HEAD(model_policy_list, model_policy);

///* TODO: change to model_ref */
//struct model_policy_ref {
//	struct model_policy *mp;
//	LIST_ENTRY(model_policy_ref) entry;
//};
//LIST_HEAD(model_policy_ref_list, model_policy_ref);
//typedef struct model_policy_ref_list *model_policy_ref_list_t;

struct model {
	struct me_model_engine *engine;
	struct model_policy *policy;
	int refcount;
	pthread_mutex_t refcount_lock;
	uint16_t num_inputs;
	uint64_t metric_ids[0];
};

struct model_ref {
	struct model *model;
	LIST_ENTRY(model_ref) entry;
};

struct model_ref_list {
	LIST_HEAD(mref_list, model_ref) list;
	pthread_mutex_t list_lock;
};

/**
 * \brief Destroy the model policy
 */
int destroy_model(struct model *model);

/**
 * \brief Create a model policy
 * \param[in]	model_pi	model plugin
 * \return a model policy
 */
struct model_policy *mp_new(struct me_model_plugin *model_pi);

/**
 * \brief Create a model configuration
 * \return 0. Otherwise, -1.
 */
struct me_model_cfg *
cfg_new(struct me_model_plugin *mpi,const char *model_id_s, char *thresholds,
		char *params, char *report_flags, char *err_str);

/**
 * \brief Find a model policy
 * \param[in]	model_id	Model ID
 */
struct model_policy *find_model_policy(uint16_t model_id);

/**
 * \brief Verify if the two model definitions are the same
 *
 * \return 1 if the two definitions are the same.
 * 	   0 otherwise.
 *
 * \param[in] pi A model plugin
 * \param[in] a A model definition
 * \param[in] b Another model definition
 */
int compare_model_policy(struct me_model_plugin *pi,
		struct me_model_cfg *a, struct me_model_cfg *b);


/**
 * \brief Add a model to the model database
 *
 * The function adds a reference of the model to each input
 * handled by the model in the model database.
 *
 * \param[in]	mref  the model reference
 */
int add_model(struct model *mref, char *err_str);

/**
 * \brief Search the database for the list of model references
 * 	  that handle the input.
 *
 * \return The pointer to the list of the model references that handle
 * 	   the given input; otherwise, NULL
 *
 * \param[in]  input_id  the input ID to search for
 * 			 the list of model references
 */
struct model_ref_list *find_mref_list(uint64_t input_id);

/**
 * \brief Perform the routine when receive a datum from the Input/Output Interface
 *
 * Search the input variable tree of the input type for
 * the model instances that handle the input variable.
 * Then send the input datum to the model for evaluate or update the model.
 *
 */
void *evaluate_update();

/**
 * \brief Initialize the model manager
 *
 * \param   hash_rbt_sz   The size of the hash rbt
 * \param   max_sem_inq   The initial number of input enqueue semaphore
 * \return 0 on success. Otherwise, return errno.
 */
int model_manager_init(int hash_rbt_sz, int max_sem_inq);

#endif /* MODEL_MANAGER_H_ */
