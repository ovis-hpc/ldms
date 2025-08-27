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
#include "comp_id_helper.h"

typedef struct {
	ovis_log_t log; /* owned by ldmsd, we do not free the log */
	char *plug_name;
	char *cfg_name;
	struct comp_id_data cid;
	char producer_name[LDMS_PRODUCER_NAME_MAX];
} *lm_context_t;

#endif /* __LUSTRE_MDT_H */
