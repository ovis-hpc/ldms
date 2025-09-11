/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_OST_JOB_STATS_H
#define __LUSTRE_OST_JOB_STATS_H

#include "ldms.h"
#include "ldmsd.h"
#include "lustre_ost.h"

int ost_job_stats_schema_is_initialized();
int ost_job_stats_schema_init(lo_context_t ctxt);
void ost_job_stats_schema_fini(lo_context_t ctxt);
void ost_job_stats_sample(lo_context_t ctxt, const char *fs_name,
                          const char *ost_name, const char *job_stats_path,
                          struct rbt *job_stats_tree);
void ost_job_stats_destroy(lo_context_t ctxt, struct rbt *job_stats_tree);

#endif /* __LUSTRE_OST_JOB_STATS_H */
