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

#include <sys/errno.h>
#include <sys/queue.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <pthread.h>
#include <assert.h>
#include <semaphore.h>

#include "model_manager.h"
#include "hash_rbt.h"

/**
 * \brief The tree of input vars that contains the list of model instances
 * 	  that handle each input variable.
 *
 * There are two elements in the array because there are only two input types;
 * A tree is for an input type.
 *
 * If there are more input types, the array size should be changed accordingly.
 */
struct mp_ref_db {
	struct hash_rbt *hrbt;
} mp_ref_bd;

/*
 * =============================================================
 * Input buffer
 * =============================================================
 */
#define ME_MAX_INPUT_BUFFER 100

uint16_t producer_counts = 0;

struct me_input_queue *me_input_queue;
sem_t input_nq;
sem_t input_dq;
pthread_mutex_t inbuf_lock = PTHREAD_MUTEX_INITIALIZER;

void add_input(struct me_input *input)
{
	sem_wait(&input_nq);
	pthread_mutex_lock(&inbuf_lock);
	TAILQ_INSERT_TAIL(me_input_queue, input, entry);
	sem_post(&input_dq);
	pthread_mutex_unlock(&inbuf_lock);
}

struct me_input *get_input()
{
	struct me_input *input;
	sem_wait(&input_dq);
	pthread_mutex_lock(&inbuf_lock);
	input = TAILQ_FIRST(me_input_queue);
	TAILQ_REMOVE(me_input_queue, input, entry);
	sem_post(&input_nq);
	pthread_mutex_unlock(&inbuf_lock);
	return input;
}

void add_producer_counts()
{
	producer_counts++;
}

void decrease_producer_counts()
{
	producer_counts--;
}

uint16_t get_producer_counts()
{
	return producer_counts;
}

/*
 * =============================================================
 * Output buffer
 * =============================================================
 */

#define ME_MAX_OUTPUT_BUFFER 200

#define ME_MAX_CONSUMER 3
#define ME_MAX_STORE 3

static uint16_t consumer_counts = 0;
static uint16_t store_counts = 0;

static struct me_output_queue csm_outputq_array[ME_MAX_CONSUMER];
static struct me_output_queue store_outputq_array[ME_MAX_STORE];

void output_ref_get(me_output_t output)
{
	pthread_mutex_lock(&output->ref_lock);
	output->ref_count++;
	pthread_mutex_unlock(&output->ref_lock);
}

void output_ref_put(me_output_t output)
{
	pthread_mutex_lock(&output->ref_lock);
	output->ref_count--;
	pthread_mutex_unlock(&output->ref_lock);
	if (output->ref_count == 0) {
		free(output);
	}
}

void add_output(me_output_t _output, struct me_output_queue q_array[],
						int num_queues)
{
	int i;
	struct me_output_queue *oq;
	size_t osize = sizeof(*_output) + (sizeof(uint64_t) *
						_output->num_metrics);
	me_output_t output;
	for (i = 0; i < num_queues; i++) {
		output = malloc(osize);
		oq = &(q_array[i]);
		memcpy(output, _output, osize);
		output->model_name = strdup(_output->model_name);
		pthread_mutex_lock(&oq->lock);
		TAILQ_INSERT_TAIL(&oq->oqueue, output, entry);
		oq->size++;
		pthread_cond_signal(&oq->output_avai);
		pthread_mutex_unlock(&oq->lock);
	}
}

struct me_output *get_output_consumer(struct me_consumer *csm)
{
	struct me_output *output;
	uint16_t id = csm->consumer_id;
	struct me_output_queue *oq = &(csm_outputq_array[id]);
	pthread_mutex_lock(&oq->lock);
	while (oq->size == 0) {
		pthread_cond_wait(&oq->output_avai, &oq->lock);
	}
	output = TAILQ_FIRST(&oq->oqueue);
	TAILQ_REMOVE(&oq->oqueue, output, entry);
	oq->size--;
	pthread_mutex_unlock(&oq->lock);
	return output;
}

struct me_output *get_output_store(struct me_store *strg)
{
	struct me_output *output;
	uint16_t id = strg->store_id;
	struct me_output_queue *oq = &(store_outputq_array[id]);
	pthread_mutex_lock(&oq->lock);
	while (oq->size == 0) {
		pthread_cond_wait(&oq->output_avai, &oq->lock);
	}
	output = TAILQ_FIRST(&oq->oqueue);
	TAILQ_REMOVE(&oq->oqueue, output, entry);
	oq->size--;
	pthread_mutex_unlock(&oq->lock);
	return output;
}

void destroy_output(struct me_output *output)
{
	if (output->model_name)
		free(output->model_name);
	pthread_mutex_destroy(&output->ref_lock);
	free(output);
}

void add_consumer_counts()
{
	consumer_counts++;
}

void decrease_consumer_counts()
{
	consumer_counts--;
}

uint16_t get_consumer_counts()
{
	return consumer_counts;
}

void add_store_counts()
{
	store_counts++;
}

void decrease_store_counts()
{
	store_counts--;
}

uint16_t get_store_counts()
{
	return store_counts;
}

void init_consumer_output_queue(struct me_consumer *csm)
{
	uint16_t id = csm->consumer_id;
	struct me_output_queue *oq = &(csm_outputq_array[id]);
	TAILQ_INIT(&oq->oqueue);
	oq->size = 0;
	pthread_cond_init(&oq->output_avai, NULL);
	pthread_mutex_init(&oq->lock, NULL);
}

void init_store_output_queue(struct me_store *strg)
{
	uint16_t id = strg->store_id;
	struct me_output_queue *oq = &(store_outputq_array[id]);
	TAILQ_INIT(&oq->oqueue);
	oq->size = 0;
	pthread_cond_init(&oq->output_avai, NULL);
	pthread_mutex_init(&oq->lock, NULL);
}

/*
 * =============================================================
 */

/**
 * \brief Parse the thresholds string. The omitted thresholds must be -1.
 *
 * Assumption: All thresholds >= 0, except the omitted ones are -1.
 */
int parse_thresholds(char *_thresholds, double thrs[ME_NUM_SEV_LEVELS])
{
	thrs[ME_SEV_NOMINAL] = 0;
	return sscanf(_thresholds, "%lf,%lf,%lf", &thrs[ME_SEV_INFO],
			&thrs[ME_SEV_WARNING], &thrs[ME_SEV_CRITICAL]);
}

int parse_report_flags(char *report_flags,
		char is_report[ME_NUM_SEV_LEVELS])
{
	return sscanf(report_flags, "%[^,],%[^,],%[^,],%[^,]",
			&is_report[ME_SEV_NOMINAL],
			&is_report[ME_SEV_INFO],
			&is_report[ME_SEV_WARNING],
			&is_report[ME_SEV_CRITICAL]);
}

struct model_policy *mp_new(struct me_model_plugin *model_pi)
{
	struct model_policy *mp = calloc(1, sizeof(*mp));
	if (!mp)
		return NULL;

	mp->refcount = 0;
	/*
	 * This will be created when the first input for the
	 * model comes in.
	 */
	mp->m_engine = NULL;
	pthread_mutex_init(&mp->refcount_lock, NULL);
	pthread_mutex_init(&mp->cfg_lock, NULL);
	mp->model_pi = model_pi;
	return mp;
}

struct me_model_cfg *
cfg_new(struct me_model_plugin *mpi,const char *model_id_s, char *thresholds,
		char *params, char report_flags[ME_NUM_SEV_LEVELS], char *err_str)
{
	int rc = 0;
	struct me_model_cfg *cfg = malloc(sizeof(*cfg));
	if (!cfg) {
		sprintf(err_str, "%dFailed to create a model "
				"configuration.", -ENOMEM);
		return NULL;
	}

	if (parse_thresholds(thresholds, cfg->thresholds) == EOF) {
		sprintf(err_str, "-1%s: failed to parse the thresholds\n",
				mpi->base.name);
		goto err;
	}

	int i;
	if (!report_flags) {
		for (i = 0; i < ME_NUM_SEV_LEVELS; i++) {
			if (i == ME_SEV_NOMINAL)
				cfg->report_flags[i] = '0';
			else
				cfg->report_flags[i] = '1';
		}
	} else {
		rc = parse_report_flags(report_flags, cfg->report_flags);
		if (rc == EOF) {
			sprintf(err_str, "-1%s: failed to parse the report "
					"flags\n", mpi->base.name);
			goto err;
		}
	}
	cfg->params = NULL;
	if (mpi->parse_param) {
		cfg->params = mpi->parse_param(params);
		if (!cfg->params) {
			sprintf(err_str, "-1%s: parsing parameters failed\n",
							mpi->base.name);
			goto err;
		}
	}
	return cfg;
err:
	free(cfg);
	return NULL;
}

int compare_model_policy(struct me_model_plugin *pi,
		struct me_model_cfg *a, struct me_model_cfg *b)
{
	int i;
	if (strcmp(a->report_flags, b->report_flags) == 0) {
		for (i = 1; i < ME_NUM_SEV_LEVELS; i++) {
			if (a->thresholds[i] != b->thresholds[i])
				return 0;
		}
		return pi->compare_param(a->params, b->params);
	}
	return 0;
}

/*
 * Go thru the list of input types of the model_policy
 * For each input type, remove the model_policy_ref that
 * holds a handle to the model_policy.
 *
 */
int destroy_model(struct model *model)
{
	struct model_ref *mref;
	struct model_ref_list *mref_list;
	int i;
	for (i = 0; i < model->num_inputs; i++) {
		mref_list = find_mref_list(model->metric_ids[i]);
		LIST_FOREACH(mref, &(mref_list->list), entry) {
			if (mref->model == model) {
				LIST_REMOVE(mref, entry);
				break;
			}
		}
		free(mref);
	}
	if (!model->policy->model_pi->destroy_model_engine) {
		me_log("Couldn't destroy the model engine '%s'.\n",
				model->policy->model_pi->base.name);
		return -1;
	}
	model->policy->model_pi->destroy_model_engine(model->engine);

	free(model);
	return 0;
}

void model_ref_get(struct model *model)
{
	pthread_mutex_lock(&model->refcount_lock);
	model->refcount++;
	pthread_mutex_unlock(&model->refcount_lock);
}

void model_ref_put(struct model *model)
{
	pthread_mutex_lock(&model->refcount_lock);
	model->refcount--;
	pthread_mutex_unlock(&model->refcount_lock);
}

int add_model(struct model *model, char *err_str)
{
	struct model_ref *mref;
	struct model_ref_list *mref_list;
	int i;
	/*
	 * For each input of the given model_def,
	 * add the model_def to the model_def list of the input.
	 */
	for (i = 0; i < model->num_inputs; i++) {
		mref_list = find_mref_list(model->metric_ids[i]);

		/*
		 * If there is no model_ref_list for the input,
		 * allocate a new list and add to the database.
		 */
		if (!mref_list) {
			mref_list = malloc(sizeof(*mref_list));
			if (!mref_list) {
				sprintf(err_str, "%dCould not allocate a new "
					"model_ref_list for input ID '%lu'.\n",
					-ENOMEM, model->metric_ids[i]);
				return ENOMEM;
			}
			LIST_INIT(&(mref_list->list));
			pthread_mutex_init(&mref_list->list_lock, NULL);
			hash_rbt_insert64(mp_ref_bd.hrbt, model->metric_ids[i],
								mref_list);
		}

		mref = malloc(sizeof(*mref));
		if (!mref) {
			sprintf(err_str, "%dCould not allocate a new "
					"model_ref for input ID '%lu'.\n",
					-ENOMEM, model->metric_ids[i]);
			return ENOMEM;
		}
		mref->model = model;

		/*
		 * Add the model policy reference to the list.
		 */
		pthread_mutex_lock(&(mref_list->list_lock));
		LIST_INSERT_HEAD(&(mref_list->list), mref, entry);
		pthread_mutex_unlock(&(mref_list->list_lock));
	}

	return 0;
}

struct model_ref_list *find_mref_list(uint64_t input_id)
{
	return (struct model_ref_list *)hash_rbt_get64(mp_ref_bd.hrbt,
							input_id);
}

void *evaluate_update()
{
	struct me_input *input;
	struct me_output *output;

	while (1) {
		/*
		 * Get the input value from the input buffer.
		 * This might block the worker thread if
		 * the input buffer is empty or busy.
		 */
		input = get_input();

		/*
		 * Search the input variable tree for
		 * the list of model instances
		 * handling the input datum
		 */
		struct model_ref_list *mref_list;
		mref_list = find_mref_list(input->metric_id);

		/*
		 * There are no models to handle this input.
		 */
		if (!mref_list) {
			free(input);
			continue;
		}

		/* Iterate through the list of model instance */
		struct model_ref *mref;
		struct me_model_cfg *cfg;
		struct model_policy *policy;
		char *mname;
		me_model_engine_t engine;
		pthread_mutex_lock(&(mref_list->list_lock));
		LIST_FOREACH(mref, &(mref_list->list), entry) {
			mname = mref->model->policy->model_pi->base.name;
			cfg = mref->model->policy->cfg;
			engine = mref->model->engine;
			policy = mref->model->policy;
			if (!engine) {
				engine = policy->model_pi->new_model_engine(cfg);
				if (!engine) {
					me_log("Failed to create a model "
						"engine '%s'.\n", mname);
					continue;
				}

				mref->model->engine = engine;
			}
			output = calloc(1, sizeof(*output) +
					(mref->model->num_inputs *
					sizeof(uint64_t)));
			if (!output) {
				me_log("Out of memory to create an output.\n");
				continue;
			}
			if (engine->evaluate(engine, cfg, input, output)) {
				me_log("Failed to evaluate model %s'.\n",
								mname);
				free(output);
				continue;
			}
			memcpy(output->metric_ids, mref->model->metric_ids,
				sizeof(uint64_t) * mref->model->num_inputs);
			output->num_metrics = mref->model->num_inputs;

			/*
			 * Ignore if the output level is not a severity level.
			 */
			if (output->level > ME_NUM_SEV_LEVELS) {
				free(output);
				continue;
			}

			/* Send the input ids with the output too. */
			output->model_name = strdup(mname);
			output->model_id = mref->model->policy->model_id;
			output->ref_count = 0;
			pthread_mutex_init(&output->ref_lock, NULL);

			/*
			 * If the output has only_update, nominal level, or
			 * the level the same as the level of the previous
			 * output, it won't be added to the output queue.
			 */
			if (consumer_counts &&
					engine->last_level != output->level) {
				if (cfg->report_flags[output->level] == '1') {
					add_output(output, csm_outputq_array,
							consumer_counts);
				}
			}
			if (store_counts && output->level > ME_SEV_NOMINAL) {
				add_output(output, store_outputq_array,
							store_counts);
			}

			engine->last_level = output->level;
			destroy_output(output);

		}
		pthread_mutex_unlock(&(mref_list->list_lock));
		free(input);
	}
}

int model_manager_init(int hash_rbt_sz, int max_sem_inq)
{
	mp_ref_bd.hrbt = hash_rbt_init(hash_rbt_sz);
	if (!mp_ref_bd.hrbt) {
		me_log("Could not create the model reference database.\n");
		return ENOMEM;
	}

	if (sem_init(&input_nq, 0, max_sem_inq))
		return errno;

	if (sem_init(&input_dq, 0, 0))
		return errno;

	me_input_queue = malloc(sizeof(*me_input_queue));
	TAILQ_INIT(me_input_queue);
	return 0;
}
