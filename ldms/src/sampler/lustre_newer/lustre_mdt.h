/* -*- c-basic-offset: 8 -*- */
/* Copyright 2021 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LUSTRE_MDT_H
#define __LUSTRE_MDT_H

#include "ldms.h"
#include "ldmsd.h"

typedef struct {
	char *plug_name;
	char *cfg_name;
} *lm_context_t;

#endif /* __LUSTRE_MDT_H */
