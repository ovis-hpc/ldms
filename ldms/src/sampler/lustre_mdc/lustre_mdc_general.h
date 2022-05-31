/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 * Copyright 2022 NTESS.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_MDC_GENERAL_H
#define __LUSTRE_MDC_GENERAL_H

#include "ldms.h"
#include "ldmsd.h"
#include "comp_id_helper.h"
#include "sampler_base.h"

int mdc_general_schema_is_initialized();
int mdc_general_schema_init(const comp_id_t cid, int mdc_timing);
void mdc_general_schema_fini();
ldms_set_t mdc_general_create(const char *producer_name, const char *fs_name,
				const char *mdc_name, const comp_id_t cid,
				const struct base_auth *auth);
void mdc_general_sample(const char *mdc_name, const char *md_stats_path,
			const char *stats_path, ldms_set_t general_metric_set,
			const int mdc_timing);
void mdc_general_destroy(ldms_set_t set);

#endif /* __LUSTRE_MDC_GENERAL_H */
