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

void jobid_helper_config(struct attr_value_list *avl);
int jobid_helper_schema_add(ldms_schema_t schema);
void jobid_helper_metric_update(ldms_set_t set);

#endif /* __JOBID_HELPER_H */
