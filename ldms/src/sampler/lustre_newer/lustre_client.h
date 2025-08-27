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
#include "comp_id_helper.h"
#include "sampler_base.h"

typedef struct {
	ovis_log_t log; /* owned by ldmsd, we do not free the log */
	char *plug_name;
	char *cfg_name;
	struct comp_id_data cid;
	char producer_name[LDMS_PRODUCER_NAME_MAX];
	struct rbt llite_tree; /* red-black tree root for llites */
	char *test_path;
	int schema_extras;
	struct base_auth auth;
} *lc_context_t;
#endif /* __LUSTRE_LLITE_H */
