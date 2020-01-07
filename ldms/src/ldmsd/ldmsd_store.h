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
#ifndef __LDMSD_STORE_H__
#define __LDMSD_STORE_H__

#include "ldmsd.h"
#include "ldmsd_plugin.h"

/**
 * \defgroup ldmsd_store LDMSD Store
 * \{
 * \ingroup ldmsd_plugin
 *
 * \brief LDMSD Store Plugin Development Documentation.
 *
 * DETAILS HERE.
 *
 */

#define LDMSD_STORE_TYPENAME "store"
typedef struct ldmsd_store_type_s *ldmsd_store_type_t;

/**
 * LDMSD Storage Plugin type structure.
 *
 * This structure extends ::ldmsd_plugin_type_s and contains data and interfaces
 * common to all LDMSD storage plugins.
 */
struct ldmsd_store_type_s {
	/** Plugin base structure. */
	struct ldmsd_plugin_type_s base;

	/** Permission of the storage (for storage creation) */
	mode_t perm;

	/**
	 * Open the underlying storage.
	 *
	 * The storage plugin must override this function. This function is
	 * called when \c ldmsd is ready to start storing data into the store.
	 * However, if \c config of the storage plugin instance failed, this
	 * function won't be called. The storage policy \c strgp is provided so
	 * that the plugin implementation can access storage policy
	 * configuration (e.g. schema name, and list of metrics to be stored).
	 *
	 * \retval 0     If succeeded.
	 * \retval errno If failed.
	 */
	int (*open)(ldmsd_plugin_inst_t i, ldmsd_strgp_t strgp);

	/**
	 * Close the underlying storage.
	 */
	int (*close)(ldmsd_plugin_inst_t i);

	/**
	 * Flush the underlying storage.
	 */
	int (*flush)(ldmsd_plugin_inst_t i);

	/**
	 * Store the data.
	 */
	int (*store)(ldmsd_plugin_inst_t i, ldms_set_t set,
		     ldmsd_strgp_t strgp);
};

int ldmsd_store_open(ldmsd_plugin_inst_t i, ldmsd_strgp_t strgp);
int ldmsd_store_close(ldmsd_plugin_inst_t i);
int ldmsd_store_store(ldmsd_plugin_inst_t i, ldms_set_t set,
		      ldmsd_strgp_t strgp);

/**
 * Provide the string containing the common attributes of the store plugin type
 */
const char *ldmsd_store_help();

/**
 * Default implementation of `query()` interface for store plugin.
 */
json_entity_t ldmsd_store_query(ldmsd_plugin_inst_t i, const char *q);

/** Obtaining store structure from inst. */
#define LDMSD_STORE(inst) ((ldmsd_store_type_t)LDMSD_INST(inst)->base)

/** \} */ /* defgroup ldmsd_store */
#endif
