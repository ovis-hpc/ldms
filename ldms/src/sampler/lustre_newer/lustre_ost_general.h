/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_OST_GENERAL_H
#define __LUSTRE_OST_GENERAL_H

#include "ldms.h"
#include "ldmsd.h"
#include "lustre_ost.h"

int ost_general_schema_is_initialized();
int ost_general_schema_init(lo_context_t ctxt);
void ost_general_schema_fini(lo_context_t ctxt);
ldms_set_t ost_general_create(lo_context_t ctxt, const char *fs_name,
                              const char *ost_name);
void ost_general_sample(lo_context_t ctxt,
			const char *ost_name, const char *stats_path,
                        const char *osd_path, ldms_set_t general_metric_set);
void ost_general_destroy(lo_context_t ctxt, ldms_set_t set);

#endif /* __LUSTRE_OST_GENERAL_H */
