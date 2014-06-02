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

#ifndef ME_H_
#define ME_H_

#include <inttypes.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/time.h>
#include <ovis_util/util.h>

#define ME_PLUGIN_LIBPATH_DEFAULT "/usr/local/lib/me"
#define ME_PATH_MAX 4096
#define ME_MAX_PLUGIN_NAME_LEN 64

#define ME_RESET_MODEL_SIGNAL 0

/* functions */
/**
 * \brief Log the messages from the ME daemon
 */
void me_log(const char *fmt, ...);
typedef void (*me_log_fn)(const char *fmt, ...);

typedef void *me_model_cfg_t;
typedef void *me_input_def_t;

/* mectl command callback function definition */
typedef int (*mectl_cmd_fn)(int fd,
			      struct sockaddr *sa, ssize_t sa_len,
			      char *command);

#define MECTL_LIST_PLUGINS	0    /* List Plugins */
#define MECTL_LOAD_PLUGIN	1    /* Load Plugin */
#define MECTL_TERM_PLUGIN	2    /* Term Plugin */
#define MECTL_CFG_PLUGIN	3    /* Configure Plugin */
#define MECTL_STORE		4    /* Store Metrics */
#define MECTL_CREATE		5    /* Create a model policy */
#define MECTL_MODEL		6    /* Start a model with certain input(s) */
#define MECTL_START_CONSUMER	7    /* Start a consumer */
#define MECTL_EXIT_DAEMON	8    /* Exit the daemon */
#define MECTL_LIST_MODELS	9    /* List model policies */
#define MECTL_LAST_COMMAND	9

#define ME_CONTROL_SOCKNAME "me/control"

/**
 * \enum severity_level
 * \brief the severity levels are NOMINAL, INFO, WARNING and CRITICAL.
 *
 * The ordering starts from nominal which is equal to 0.
 */
enum me_severity_level {
	ME_SEV_NOMINAL = 0,
	ME_SEV_INFO,
	ME_SEV_WARNING,
	ME_SEV_CRITICAL,
	ME_NUM_SEV_LEVELS,
	ME_RESET_STATE,
	ME_ONLY_UPDATE,

};

enum me_input_type {
	ME_INPUT_DATA = 0,
	ME_NO_DATA = 1
};

#pragma pack(4)
/**
 * \struct me_msg
 * \brief Message to send from producers/consumers to ME
 */
struct me_msg {
	enum me_input_type tag;	/**< Tag for the type of message */
	uint64_t metric_id; /**< unique ID for inputs */
	struct timeval timestamp; /**< Timestamp of the input */
	double value;
};
#pragma pack()

/**
 * \struct me_input_data
 * \brief A datum of an input var
 */
struct me_input {
	uint64_t metric_id;
	struct timeval ts; /**< timestamp */
	double value;
	enum me_input_type type;
	TAILQ_ENTRY(me_input) entry;
};
typedef struct me_input *me_input_t;
TAILQ_HEAD(me_input_queue, me_input);

/* API for model plug-ins */

/**
 * \brief Get the input value from an input structure
 * \return the input value in unsigned int 64
 */
double me_get_input_value(me_input_t input, me_log_fn msglog);

/**
 * \brief Get the timestamp
 * \return struct timeval
 */
struct timeval me_get_timestamp(me_input_t input);

/**
 * \brief Get the thresholds
 */
double *me_get_thresholds(me_model_cfg_t cfg);

/**
 * \brief Get parameter from model_cfg
 */
void *me_get_params(me_model_cfg_t cfg);

/**
 * \brief Get the input type
 * \return the input type
 */
enum me_input_type me_get_input_type(me_input_t input);

/**
 * \brief Get the model name
 */
char *me_get_model_name(me_model_cfg_t cfg);

/**
 * \brief set the input definition
 */
void me_set_input_def(me_input_t input, me_input_def_t def);

/**
 * \brief Set the input value type
 */
void me_set_input_type(me_input_t input, enum me_input_type type);

/**
 * \brief Set the input type ID
 */
//void me_set_metric_type_id(me_input_t input, uint64_t id);

/**
 * \brief Set the input component ID
 */
//void me_set_input_comp_id(me_input_t input, uint64_t comp_id);

/**
 * \brief Set the input time stamp
 */
void me_set_input_timestamp(me_input_t input, struct timeval ts);

/**
 * \struct Model Definition
 * \brief Define an model instance
 *
 * The set of input variables and the thresholds are different
 * for different model definitions.
 */
struct me_model_cfg {
//	int num_inputs; /**< Number of inputs */
//	struct me_input_id_list input_id_h; /**< List of input IDs */
	/**
	 * thresholds[0] = NULL; NOMINAL <60%
	 * thresholds[1] = 0.60; INFO >60% & <75%
	 * thresholds[2] = 0.75; WARNING >75% & <90%
	 * thresholds[3] = 0.90; CRITICAL >90%
	 */
	double thresholds[ME_NUM_SEV_LEVELS];
	void *params; /**< user-given arguments to the model */
	/**
	 * 1 each the severity level should be sent to consumers
	 */
	char report_flags[ME_NUM_SEV_LEVELS];
};

struct me_output_model_info {
	char model_name[ME_MAX_PLUGIN_NAME_LEN];
	char input_type_ids[256];
};

/**
 * \struct event
 * \brief The output of the Model Evaluator
 */
struct me_output {
	struct timeval ts;
	char *model_name;
	uint16_t model_id;
	enum me_severity_level level;
	int ref_count;
	pthread_mutex_t ref_lock;
	TAILQ_ENTRY(me_output) entry;
	uint16_t num_metrics;
	uint64_t metric_ids[0];
};
typedef struct me_output *me_output_t;
TAILQ_HEAD(me_oqueue, me_output);

typedef struct me_model_engine *me_model_engine_t;
struct me_model_engine {
	/**
	 * \brief Evaluate or update the model state according to the input
	 * \param[out] output
	 * \return outputs with timestamp and the severity level.
	 * 	   Otherwise, NULL if it fails to evaluate/update.
	 *
	 * The input has two types: ME_INPUT_DATA and ME_NO_DATA.
	 * ME_INPUT_DATA is the input with current value. On the other hand,
	 * ME_NO_DATA is the input sent from the producer with old value.
	 *
	 * The output with the level value larger than ME_NUM_SEV_LEVELS
	 * won't be send or store.
	 */
	int (*evaluate)(me_model_engine_t m, me_model_cfg_t model_cfg,
				me_input_t input, me_output_t output);
	/**
	 * \brief Reset the model state
	 */
	int (*reset_state)(me_model_engine_t m);

	/**
	 * \brief model context
	 */
	void *mcontext;

	/**
	 * \brief the last level that is added to the output queue
	 */
	enum me_severity_level last_level;
};

struct me_plugin {
	char name[ME_MAX_PLUGIN_NAME_LEN];
	void *handle;
	enum me_plugin_type {
		ME_MODEL_PLUGIN = 0,
		ME_INTERFACE_PLUGIN,
	} type;

	union {
		struct me_interface_plugin *intf_pi;
		struct me_model_plugin *model_pi;
	};

	int refcount;
	pthread_mutex_t lock;
	void (*term)();
	int (*config)(struct attr_value_list *kwl, struct attr_value_list *avl);
	const char *(*usage)();
	void *(*get_spec_plugin)();
	me_log_fn log_fn;
	LIST_ENTRY(me_plugin) entry;
};

typedef struct me_plugin *(*me_plugin_get_f)(me_log_fn log_fn);

/**
 * \struct model_plugin
 * \brief The model_plugin structure is an object of a model plugin.
 *
 * name: the model name
 * term: terminate the instance
 * usage: print the  usage of the model (description of the model configuration)
 * routine: either update the model or evaluate
 * get_params: parse the given parameters
 * get_context: allocate memory for the context of the model-dependent structure
 */
struct me_model_plugin {
	struct me_plugin base;
	me_model_engine_t (*new_model_engine)(me_model_cfg_t model_cfg);
	void (*destroy_model_engine)(struct me_model_engine *engine);
	/**
	 * \brief Parse the parameter string
	 * The function needs to check for NULL pointer of param.
	 */
	void *(*parse_param)(char *param);
	/**
	 * \brief Compare the parameter(s)
	 * \return 1 if there are the same; otherwise, return 0.
	 */
	int (*compare_param)(void *param_1, void *param_2);
	/**
	 * \brief Print parameter(s)
	 * \return string of the parameters
	 */
	char *(*print_param)(void *param);
	void *(*get_context)();
};
typedef struct me_model_plugin *me_model_pi_t;

#endif /* ME_H_ */
