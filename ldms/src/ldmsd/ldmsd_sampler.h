/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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
#ifndef __LDMSD_SAMPLER_H__
#define __LDMSD_SAMPLER_H__

#include <sys/queue.h>
#include <sys/types.h>
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

#define LDMSD_SAMPLER_TYPENAME "sampler"

typedef struct ldmsd_sampler_type_s *ldmsd_sampler_type_t;

typedef struct ldmsd_set_entry_s *ldmsd_set_entry_t;

/**
 * LDMS set entry (for ::ldmsd_sampler_type_s.set_list).
 */
struct ldmsd_set_entry_s {
	/** LDMS set. */
	ldms_set_t set;
	/** Application context from ldmsd_sampler_type_s::create_set(). */
	void *ctxt;
	/** List entry */
	LIST_ENTRY(ldmsd_set_entry_s) entry;
};

/**
 * LDMSD Sampler Plugin Type structure.
 *
 * This structure extends ::ldmsd_plugin_type_s and contains data common to all
 * LDMSD sampler plugins.
 */
struct ldmsd_sampler_type_s {
	/** Plugin base structure. */
	struct ldmsd_plugin_type_s base;
	/** LDMS set instance name (from `config instance=...` parameter). */
	char *set_inst_name;
	/** LDMS set producer name (from `config producer=...` parameter). */
	char *producer_name;
	/** LDMS set schema name (from `config schema=...` parameter). */
	char *schema_name;
	/** The LDMS schema handle. */
	ldms_schema_t schema;
	/** Reference to the set containing job information. */
	ldms_set_t job_set;
	/** List of sets created by this instance. */
	LIST_HEAD(, ldmsd_set_entry_s) set_list;
	/** Set owner user ID. */
	uid_t uid;
	/** Set owner group ID. */
	gid_t gid;
	/** Set permission. */
	int perm;
	/** Number of copies in the set array feature (default: 1). */
	int set_array_card;
	/** Component ID. */
	uint64_t component_id;
	/** Index of the job_id metric in the job_set. */
	int job_id_idx;
	/** Index of the app_id metric in the job_set. */
	int app_id_idx;
	/** Index of the job_start metric in the job_set. */
	int job_start_idx;
	/** Index of the job_end metric in the job_set. */
	int job_end_idx;
	/** [private] Index of the current_slot metric in the job_set. */
	int current_slot_idx;
	/** [private] Mutex. */
	pthread_mutex_t lock;
	/** [private] associated smplr object */
	ldmsd_smplr_t smplr;

	/** First metric index to populate by the sampler instance. */
	int first_idx;

	struct ldmsd_set_ctxt_s set_ctxt;

	/**
	 * Create an LDMS schema.
	 *
	 * The default implementation of this function is to create a schema
	 * with the same name of the plugin, and add a collection of common
	 * metrics of LDMS set created by LDMSD.
	 *
	 * \note The plugin implementation does not need to implement this
	 * function, but can override it if needed.
	 *
	 * \param inst The plugin instance.
	 *
	 * \retval schema The pointer to the schema, if succeed.
	 * \retval NULL   If error. \c errno must also be set to describe the
	 *                error.
	 */
	ldms_schema_t (*create_schema)(ldmsd_plugin_inst_t inst);

	/**
	 * Create an LDMS set, and put it into \c set_list.
	 *
	 * The default implementation of this function is to create a set with
	 * given \c set_name and \c schema, and put it into the \c set_list.
	 *
	 * \note The plugin implementation does not need to implement this
	 * function, but can override it if needed. If overriden, the function
	 * needs to 1) create the set, and 2) put the set into \c set_list.
	 *
	 * \param inst The plugin instance pointer.
	 * \param set_name The name of the set.
	 * \param schema The set schema.
	 * \param ctxt The application context for each LDMS set.
	 *
	 * \retval set  The set handle, if succeed.
	 * \retval NULL If error. \c errno must also be set to describe the
	 *              error.
	 */
	ldms_set_t (*create_set)(ldmsd_plugin_inst_t inst, const char *set_name,
				 ldms_schema_t schema, void *ctxt);

	/**
	 * Create an LDMSD set group and put it into \c set_list.
	 *
	 * \param inst The plugin instance pointer.
	 * \param grp_name The name of the set group.
	 * \param ctxt The application context for each LDMS set.
	 *
	 * \retval grp  The set group handle (a special LDMS set), if succeed.
	 * \retval NULL If error. \c errno must also be set to describe the
	 *              error.
	 */
	ldms_set_t (*create_set_group)(ldmsd_plugin_inst_t inst,
				       const char *grp_name, void *ctxt);

	/**
	 * Delete the set (the undo of create_set()).
	 *
	 * This API is meant to be called by plugin implementation to delete the
	 * set created by `create_set()` API.
	 *
	 * \note The sets created by `create_set()` that are not deleted by
	 *       `delete_set()` API will be automatically deleted when the
	 *       instance is deleted.
	 *
	 * \param inst The plugin instance pointer.
	 * \param name The set name.
	 *
	 * \retval 0      If succeeded.
	 * \retval ENOENT If the set is not found in the set_list of this
	 *                instance.
	 */
	int (*delete_set)(ldmsd_plugin_inst_t inst, const char *name);

	/**
	 * Sample function.
	 *
	 * The default implementation of this function follows the pseudo code:
	 * \code
	 * foreach (set_ent in set_list) {
	 *   ldmsd_transaction_begin(set_ent->set);
	 *   ((ldmsd_sampler_type_t)(inst->base))->base_update_set(inst,
	 *                                                    set_ent->set);
	 *   ((ldmsd_sampler_type_t)(inst->base))->update_set(inst,
	 *                                                    set_ent->set,
	 *                                                    set_ent->ctxt);
	 *   ldmsd_transaction_end(set_ent->set);
	 * }
	 * \endcode
	 *
	 * , where \c ctxt is the application context speified in
	 * ldmsd_sampler_type_s::create_set().
	 *
	 * \note The plugin implementation does not need to implement this
	 * function, but can override it if needed.
	 *
	 * \param inst The plugin instance pointer.
	 *
	 * \retval 0     If succeeded.
	 * \retval errno If failed.
	 */
	int (*sample)(ldmsd_plugin_inst_t inst);

	/**
	 * Plugin-specific metric addition to the default schema.
	 *
	 * This function is called after ldmsd_plugin_type_s::create_schema() to
	 * let the plugin add plugin-specific metrics (see
	 * ldms_schema_metric_add(), ldms_schema_metric_array_add(),
	 * ldms_schema_meta_add(), and ldms_schema_meta_array_add()).
	 *
	 * \note The sampler plugin implementation must override this function
	 *       to provide the plugin-specific schema setup.
	 *
	 * \param inst   The plugin instance handle.
	 * \param schema The schema handle.
	 *
	 * \retval 0     If succeed.
	 * \retval errno If failed.
	 */
	int (*update_schema)(ldmsd_plugin_inst_t inst, ldms_schema_t schema);

	/**
	 * Common set update routine.
	 *
	 * This function updates the base (common) metrics in the set. The
	 * caller must call ::ldms_transaction_begin() before calling this
	 * function, and must call ::ldms_transaction_end() afterward.
	 *
	 * The default sample() implementation calls this function to update the
	 * common (base) part of the metrics in the set. If sample() has not
	 * been overridden, the plugin implementation does not need to call this
	 * function. If the sample() has been overridden, the plugin should call
	 * this function for each set to update the common metrics.
	 *
	 * \retval 0     If succeeded.
	 * \retval errno If failed.
	 */
	int (*base_update_set)(ldmsd_plugin_inst_t inst, ldms_set_t set);

	/**
	 * Set data update.
	 *
	 * This function is a subsequence call from
	 * ldmsd_plugin_type_s::sample() default implementation to update the
	 * set data, after ldmsd_plugin_type_s::base_update_set() is called.
	 * ::ldms_transaction_begin() has also been called before update_set(),
	 * and ::ldms_transaction_end() will also be called automatically
	 * afterward.
	 *
	 * The plugin implementation may decide to override
	 * ldmsd_plugin_type_s::sample() instead if the default set update
	 * sequence is not suitable.
	 *
	 * \note With default implementation of ldmsd_plugin_type_s::sample(),
	 *       the plugin implementation must override this function to
	 *       provide the plugin-specific set-data-update logic.
	 *
	 * \param inst The plugin instance handle.
	 * \param set  The LDMS set handle.
	 * \param ctxt The context from ldmsd_sampler_type_s::create_set().
	 *
	 * \retval 0     If succeeded.
	 * \retval errno If failed.
	 */
	int (*update_set)(ldmsd_plugin_inst_t inst, ldms_set_t set,
			   void *ctxt);
};

/**
 * Default implementation of `query()` interface for sampler plugin.
 */
json_entity_t ldmsd_sampler_query(ldmsd_plugin_inst_t i,
					   const char *q);

/**
 * Return the command attribute of the sampler plugin instances.
 */
const char *ldmsd_sampler_help();

/**
 * Accessing `ldmsd_sampler_type_t` given a sampler plugin `inst`.
 */
#define LDMSD_SAMPLER(inst) ((ldmsd_sampler_type_t)LDMSD_INST(inst)->base)

/**
 * Check if the `pi` is a sampler plugin instance.
 */
#define LDMSD_INST_IS_SAMPLER(pi) (0 == strcmp((pi)->base->type_name, \
				               LDMSD_SAMPLER_TYPENAME))

/** component_id metric index (added by `SAMP->create_schema()`) */
#define SAMPLER_COMP_IDX 0
/** job_id metric index (added by `SAMP->create_schema()`) */
#define SAMPLER_JOB_IDX  1
/** app_id metric index (added by `SAMP->create_schema()`) */
#define SAMPLER_APP_IDX  2

/** \} */ /* defgroup ldmsd_sampler */
#endif /* __LDMSD_SAMPLER_H__ */
