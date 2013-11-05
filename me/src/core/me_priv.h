/*
 * me_priv.h
 *
 *  Created on: Mar 25, 2013
 *      Author: nichamon
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
