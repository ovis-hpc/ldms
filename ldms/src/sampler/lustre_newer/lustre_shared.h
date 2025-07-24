/* -*- c-basic-offset: 8 -*- */
/* Copyright 2025 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_SHARED_H
#define __LUSTRE_SHARED_H

#include "ldms.h"

#define MAXNAMESIZE 64

/*
 * Find the osd directory that should contain simple stats files such
 * as "kbytesfree".
 *
 * Returns strdup'ed string or NULL.  Caller must free.
 */
char *lustre_osd_dir_find(const char * const *paths, const char *component_name, ovis_log_t log);

int lustre_stats_file_sample(const char *stats_path,
			     ldms_set_t metric_set,
			     ovis_log_t log);

#endif
