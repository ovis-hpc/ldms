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

int lustre_stats_file_sample(const char *stats_path,
			     ldms_set_t metric_set,
			     ovis_log_t log);

#endif
