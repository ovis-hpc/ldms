/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_LLITE_H
#define __LUSTRE_LLITE_H

#include "ldms.h"
#include "ldmsd.h"

typedef struct {
	ovis_log_t log; /* owned by ldmsd, we do not free the log */
	char *plug_name;
	char *cfg_name;
} *lc_context_t;

#endif /* __LUSTRE_LLITE_H */
