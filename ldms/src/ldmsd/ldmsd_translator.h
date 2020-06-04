/*
 * ldmsd_translator.h
 *
 *  Created on: Jun 2, 2020
 *      Author: nnich
 */

#ifndef LDMS_SRC_LDMSD_LDMSD_TRANSLATOR_H_
#define LDMS_SRC_LDMSD_LDMSD_TRANSLATOR_H_

#include <pthread.h>

#include "ldmsd_plugin.h"

/**
 * \defgroup ldmsd_sampler LDMSD Sampler
 * \{
 * \ingroup ldmsd_plugin
 *
 * \brief LDMSD Sampler Development Documentation.
 *
 * DETAILS HERE.
 *
 * NOTE:
 * - ldms_set from the same instance expect to have the same schema.
 * - schema name and definition must be unique globally (across all ldmsd's).
 */

#define LDMSD_TRANSLATOR_TYPENAME "translator"

typedef struct ldmsd_translator_type_s *ldmsd_translator_type_t;

/**
 * LDMSD Translator Plugin Type structure.
 *
 * This structure extends ::ldmsd_plugin_type_s and contains data common to all
 * LDMSD translator plugins.
 */
struct ldmsd_translator_type_s {
	/** Plugin base structure. */
	struct ldmsd_plugin_type_s base;

	/** [private] Mutex. */
	pthread_mutex_t lock;

	/*
	 * \brief Translate a configuration file into a list of cfgobj request JSON objects
	 */
	json_entity_t (*translate)(ldmsd_plugin_inst_t pi,
					const char *path,
					json_entity_t req_list);

	/*
	 * \brief Convert a list of cfgobj request JSON objects into a configuration file.
	 */
	int (*config_file_export)(ldmsd_plugin_inst_t pi,
				  json_entity_t req_list);

	/*
	 * \brief Handle a reply JSON object
	 *
	 * This function should determine specific locations in
	 * the configuration file that causes the error.
	 */
	int (*error_report)(ldmsd_plugin_inst_t pi,
			    json_entity_t reply);
};

/**
 * Accessing `ldmsd_translator_type_t` given a sampler plugin `inst`.
 */
#define LDMSD_TRANSLATOR(inst) ((ldmsd_translator_type_t)LDMSD_INST(inst)->base)

/**
 * Check if the `pi` is a translator plugin instance.
 */
#define LDMSD_INST_IS_TRANSLATOR(pi) (0 == strcmp((pi)->base->type_name, \
				               LDMSD_TRANSLATOR_TYPENAME))


#endif /* LDMS_SRC_LDMSD_LDMSD_TRANSLATOR_H_ */
