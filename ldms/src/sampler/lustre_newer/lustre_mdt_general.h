/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_MDT_GENERAL_H
#define __LUSTRE_MDT_GENERAL_H

#include "ldms.h"
#include "ldmsd.h"
#include "lustre_mdt.h"

int mdt_general_schema_is_initialized();
int mdt_general_schema_init(lm_context_t ctxt);
void mdt_general_schema_fini(lm_context_t ctxt);
ldms_set_t mdt_general_create(lm_context_t ctxt, const char *fs_name,
                              const char *mdt_name);
void mdt_general_sample(lm_context_t ctxt, const char *mdt_name, const char *stats_path,
                        const char *osd_path, ldms_set_t general_metric_set);
void mdt_general_destroy(lm_context_t ctxt, ldms_set_t set);

#endif /* __LUSTRE_MDT_GENERAL_H */
