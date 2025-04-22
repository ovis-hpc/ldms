/* -*- c-basic-offset: 8 -*- */
/* Copyright 2025 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef __LDMSD_PLUG_API_H__
#define __LDMSD_PLUG_API_H__

#include "ldmsd.h"

/*
 * Record an opaque context pointer in the plugin configuration handle.
 *
 * In order to be multi-configuration capable, a plugin will need to
 * maintain separate state (i.e. context) for each configuration. Typically
 * a plugins will allocate a context structure in the plugin constructor().
 */
void ldmsd_plug_context_set(ldmsd_plug_handle_t handle, void *context);

/*
 * Retrieve an opague context pointer from the plugin configuration handle.
 */
void *ldmsd_plug_context_get(ldmsd_plug_handle_t handle);

/*
 * Retreive a pointer to the plugin's current configuration name. Plugin
 * configuration names will be unique to a give configuration (in
 * contrast to the plugin name).
 */
const char *ldmsd_plug_config_name_get(ldmsd_plug_handle_t handle);

/*
 * Retrive a pointer to the plugin's name. Multiple configurations
 * may be created from the same plugin, and so multiple configurations
 * may have the same plugin name.
 */
const char *ldmsd_plug_plugin_name_get(ldmsd_plug_handle_t handle);

#endif
