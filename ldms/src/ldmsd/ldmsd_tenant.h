/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2025 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2025 Open Grid Computing, Inc. All rights reserved.
 *
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

/**
 * \file ldmsd_tenant.h
 * \brief LDMS Daemon Multi-Tenant Interface
 *
 * This module provides functionality for managing tenant definitions and their
 * associated metrics within the LDMS daemon. Tenants represent distinct entities
 * (such as jobs, users, steps or tasks) that can be tracked and monitored through
 * a list of attributes retrieved from the job manager.
 *
 * # Overview
 *
 * The multi-tenant infrastructure allows:
 * - Definition of tenant types with custom attribute sets
 * - Sampling of tenant information from the job manager
 * - Storage of tenant data in LDMS record lists
 *
 * # Usage Pattern
 *
 * 1. Create a tenant definition with ldmsd_tenant_def_create()
 * 2. Add the tenant schema to an LDMS schema with ldmsd_tenant_schema_list_add()
 * 3. Sample tenant values periodically with ldmsd_tenant_values_sample()
 * 4. Release the definition when done with ldmsd_tenant_def_free()
 *
 * # Thread Safety
 *
 * The tenant definition list is protected by an internal mutex. Individual
 * tenant definitions use reference counting for safe concurrent access.
 */

#ifndef __LDMSD_TENANT_H__
#define __LDMSD_TENANT_H__

#include <pthread.h>

#include "ovis_ref/ref.h"
#include "ovis_log/ovis_log.h"

#include "ldmsd.h"
#include "ldmsd_jobmgr.h"

/** \brief Default initial number of tenants to allocate space for */
#define LDMSD_TENANT_NUM_DEFAULT 10

/** \brief Standard name for tenant record definition in LDMS schemas */
#define LDMSD_TENANT_REC_DEF_NAME "tenant_def"

/** \brief Standard name for tenant list metric in LDMS schemas */
#define LDMSD_TENANT_LIST_NAME "tenants"

/**
 * \brief Tenant metric structure
 *
 * Represents a single attribute (metric) within a tenant definition.
 * Each metric corresponds to a field that will be queried from the job
 * manager and stored in the tenant record.
 *
 * The metric template contains the core metadata (name, type, unit) that
 * defines the metric's schema. The __rent_id field provides an internal
 * reference for accessing the metric within the LDMS record definition.
 *
 * \note This structure is managed internally by the tenant system.
 *       Users typically don't interact with it directly.
 */
typedef struct ldmsd_tenant_metric_s {
	struct ldms_metric_template_s mtempl; /**< Metric template */
	/* Fields internal to tenant core logic */
	int __rent_id; /**< Reference to metric in LDMS record definition */
	TAILQ_ENTRY(ldmsd_tenant_metric_s) ent; /**< List linkage */

} *ldmsd_tenant_metric_t;

/**
 * \brief List type for tenant metrics
 *
 * Maintains an ordered list of metrics that comprise a tenant definition.
 * The order of metrics in this list determines their layout in the
 * tenant record.
 */
TAILQ_HEAD(ldmsd_tenant_metric_list, ldmsd_tenant_metric_s);

/**
 * \brief Tenant definition structure
 *
 * Represents a complete definition of a tenant type, including all its
 * attributes (metrics) and the necessary metadata for querying and storing
 * tenant information.
 *
 * A tenant definition serves as a template that can be reused across
 * multiple LDMS sets. It defines:
 * - The set of attributes to query from the job manager
 * - The structure of the LDMS record for storing tenant data
 * - The memory requirements for tenant storage
 *
 * Tenant definitions are reference-counted and can be safely shared
 * across multiple contexts.
 *
 * \note Once created, a tenant definition is immutable. To change the
 *       definition, create a new one with a different name.
 */
typedef struct ldmsd_tenant_def_s {
	struct ref_s ref;
	char *name; /**< Name of the tenant type, this is a key to reuse tenant definition. */
	int mcount; /* Number of metrics */
	struct ldmsd_tenant_metric_list mlist;
	struct ldms_metric_template_s *rec_def_tmpl;
	size_t rec_def_heap_sz;
	ldmsd_jobmgr_query_t query_handle;
	/**< Entry in the definition list */
	LIST_ENTRY(ldmsd_tenant_def_s)
	ent;
} *ldmsd_tenant_def_t;

/**
 * \brief Create tenant definition from attribute list
 *
 * Creates a new tenant definition that specifies which attributes should be
 * tracked for tenants. Each attribute in the list must be a valid metric name
 * recognized by the job manager.
 *
 * A tenant is defined as a unique combination of attribute values. Two tenants
 * are considered distinct if they differ in any attribute value specified in
 * the definition.
 *
 * \param name      Unique name for this tenant definition (used for lookup and reuse)
 * \param str_list  List of attribute names to include in the tenant definition.
 *                  Each string must match a valid job manager metric name.
 *
 * \return Handle to the newly created tenant definition on success.
 *         Returns NULL on failure with errno set to:
 *         - EEXIST: A definition with this name already exists
 *         - ENOMEM: Memory allocation failed
 *         - EINVAL: One or more attributes are not recognized by the job manager
 *         - Other errors from job manager initialization
 *
 * \note The returned handle has an initial reference count. Call
 *       ldmsd_tenant_def_free() when done using it.
 *
 * \warning If a definition with the same name exists, this function fails.
 *          Use ldmsd_tenant_def_find() to reuse existing definitions.
 */
struct ldmsd_tenant_def_s *
ldmsd_tenant_def_create(const char *name, struct ldmsd_str_list *str_list);

/**
 * \brief Release the tenant definition reference from creation
 *
 * This should be called when the consumer no longer needs to access the
 * tenant definition.
 *
 * This function complements ldmsd_tenant_def_create() and should only be
 * called to release the reference obtained from that function. For references
 * obtained via ldmsd_tenant_def_find(), use ldmsd_tenant_def_put() instead.
 *
 * \param tdef  Handle to the tenant definition from ldmsd_tenant_def_create()
 *
 * \note The tenant definition may continue to exist if other references remain.
 *       Do not use the handle after calling this function.
 */
void ldmsd_tenant_def_free(struct ldmsd_tenant_def_s *tdef);

/**
 * \brief Find an existing tenant definition by name
 *
 * \param name  Name of the tenant definition to find
 *
 * \return Handle to the tenant definition if found, NULL otherwise.
 *         The returned handle has an incremented reference count.
 *
 * \note Must call ldmsd_tenant_def_put() to release the reference when done.
 */
struct ldmsd_tenant_def_s *ldmsd_tenant_def_find(const char *name);

/**
 * \brief Release a reference obtained from ldmsd_tenant_def_find()
 *
 * \param tdef  Handle to the tenant definition to release
 */
void ldmsd_tenant_def_put(struct ldmsd_tenant_def_s *tdef);

/**
 * \brief Calculate heap size required for tenant storage
 *
 * Computes the total heap memory needed to store a specified number of
 * tenant records in an LDMS set. This should be used when creating or
 * resizing LDMS sets to ensure adequate heap space.
 *
 * \param tdef         Tenant definition handle
 * \param num_tenants  Expected number of concurrent tenants
 *
 * \return Size in bytes required for heap allocation
 *
 * \note If the actual number of tenants exceeds num_tenants at runtime,
 *       ldmsd_tenant_values_sample() will return ENOMEM and the set
 *       will need to be resized.
 */
size_t ldmsd_tenant_heap_sz_get(struct ldmsd_tenant_def_s *tdef, int num_tenants);

/**
 * \brief Add tenant metrics to an LDMS schema
 *
 * Adds the necessary record definition and list metric to an LDMS schema
 * for storing tenant information. This creates:
 * 1. A record definition describing the structure of individual tenant records
 * 2. A list metric that will contain the actual tenant instances
 *
 * \param tdef                  Tenant definition handle
 * \param schema                LDMS schema to add metrics to
 * \param num_tenants           Estimated maximum number of concurrent tenants
 * \param _tenant_rec_mid       [out] Optional pointer to receive the metric index
 *                              of the tenant record definition
 * \param _tenants_mid          [out] Optional pointer to receive the metric index
 *                              of the tenant list metric
 *
 * \return 0 on success. On failure, returns a positive error code:
 *         - ENOMEM: Memory allocation failed
 *         - Other positive error codes from LDMS schema operations
 *
 * \note The returned metric indices should be saved for use with
 *       ldmsd_tenant_values_sample().
 */
int ldmsd_tenant_schema_list_add(struct ldmsd_tenant_def_s *tdef,
				 ldms_schema_t schema, int num_tenants,
				 int *_tenant_rec_mid, int *_tenants_mid);

/**
 * \brief Sample and update tenant values in an LDMS set
 *
 * \param tdef            Tenant definition handle
 * \param set             LDMS set to update
 * \param tenant_rec_mid  Metric ID of the tenant record definition
 *                        (obtained from ldmsd_tenant_schema_list_add())
 * \param tenants_mid     Metric ID of the tenant list metric
 *                        (obtained from ldmsd_tenant_schema_list_add())
 *
 * \return 0 on success. On failure, returns a positive error code:
 *         - ENOENT: Could not retrieve the tenant list metric from the set
 *         - ENOMEM: Insufficient heap space in the set for the current number
 *                   of tenants. The set should be resized with a larger heap
 *                   and this function called again.
 *
 * \warning When ENOMEM is returned, the tenant list may be partially updated.
 *          The caller should resize the set and retry the sampling operation.
 *
 * \note If no tenants are currently running, the function succeeds and leaves
 *       the tenant list empty (after purging any previous contents).
 */
int ldmsd_tenant_values_sample(struct ldmsd_tenant_def_s *tdef, ldms_set_t set,
			       int tenant_rec_mid, int tenants_mid);

#endif