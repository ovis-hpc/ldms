/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __JOBID_HELPER_H
#define __JOBID_HELPER_H

#include "ldms.h"
#include "ldmsd.h"
/* Functions to manage info about a common set instance from
 * which all other sets extract the current job_id value(s).
 * Assumptions:
 * - All samplers extracting a jobid are configured to
 *   extract it from the same common set instance.
 * - The common set instance is not recreated after its
 *   initial appearance, for the life of the daemon.
 * - All samplers using this API will be stopped
 *   before the common set is destroyed.
 *   (Violation of this may lead to reading freed memory).
 * - There is only one job per node, unless the
 *   named job info set includes metric 'job_slot_list'.
 */

#define MAX_JOB_SET_NAME 256

/* Extract the 'job_set' attribute from avl to find
 * the job_id source set name.
 * \return 0 if ok or error if the name length found
 * is > MAX_JOB_SET_NAME.
 */
int jobid_helper_config(struct attr_value_list *avl);

/* Add job_id and app_id to the schema given.
 * \return the failure value of ldms_schema_metric_add
 * or 0 if it succeeds for all added metrics.
 */
int jobid_helper_schema_add(ldms_schema_t schema);

/* Copy the job_id, app_id values from the configured
 * source set into their locations in the given set.
 * If the expected source set is not yet present, this
 * becomes a no-op.
 */
void jobid_helper_metric_update(ldms_set_t set);

#endif /* __JOBID_HELPER_H */
