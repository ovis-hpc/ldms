/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_LLITE_GENERAL_H
#define __LUSTRE_LLITE_GENERAL_H

#include "ldms.h"
#include "ldmsd.h"
#include "comp_id_helper.h"
#include "sampler_base.h"


int llite_general_schema_is_initialized();

#define EXTRA215 0x1
#define EXTRATIMES 0x2
/*
 * \param schema_extras is composed of the bit flags EXTRA* above
 * to enable extra items. Fixed names for the schema variants
 * are defined as lustre_client_%d, with the value of schema_extras,
 * unless schema_extras==0.
 */
int llite_general_schema_init(comp_id_t cid, int schema_extras);
void llite_general_schema_fini();
ldms_set_t llite_general_create(const char *producer_name,
                                const char *fs_name,
                                const char *llite_name,
				const comp_id_t cid,
				const struct base_auth *auth);
char *llite_general_osd_path_find(const char *search_path, const char *llite_name);
void llite_general_sample(const char *llite_name, const char *stats_path,
                          ldms_set_t general_metric_set);
void llite_general_destroy(ldms_set_t set);

#endif /* __LUSTRE_LLITE_GENERAL_H */
