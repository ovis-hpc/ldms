/* -*- c-basic-offset: 8 -*- */
/* Copyright 2025 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LDMSD_PLUG_API_H__
#define __LDMSD_PLUG_API_H__

#include "ldmsd.h"

void ldmsd_plug_context_set(ldmsd_plug_handle_t handle, void *context);
void *ldmsd_plug_context_get(ldmsd_plug_handle_t handle);
const char *ldmsd_plug_config_name_get(ldmsd_plug_handle_t handle);
const char *ldmsd_plug_plugin_name_get(ldmsd_plug_handle_t handle);

ldmsd_plug_handle_t ldmsd_strgp_to_handle(struct ldmsd_strgp *strgp);

/* DEPRECATED
 *
 * This accessor is only for legacy usage. A plugin should never need to
 * request its own api struct. However, before the existance of the context
 * accessor functions, legacy plugins used to wrap the api struct in
 * a larger context plugin.
 */
void *ldmsd_plug_api_get(ldmsd_plug_handle_t handle);

#endif
