/*
 * me_interface.h
 *
 *  Created on: Mar 27, 2013
 *      Author: nichamon
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
