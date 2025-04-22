/* -*- c-basic-offset: 8 -*- */
/* Copyright 2025 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#include "ldmsd.h"
#include "ldmsd_plug_api.h"

void ldmsd_plug_context_set(ldmsd_plug_handle_t handle, void *context)
{
        ldmsd_cfgobj_t cfg = (ldmsd_cfgobj_t)handle;

        switch (cfg->type) {
        case LDMSD_CFGOBJ_SAMPLER:
                ((ldmsd_cfgobj_sampler_t)cfg)->context = context;
                break;
        case LDMSD_CFGOBJ_STORE:
                ((ldmsd_cfgobj_store_t)cfg)->context = context;
                break;
        default:
                ovis_log(NULL, OVIS_LERROR,
                         "ldmsd_plug_context_set(): handle is not a plugin cfgobj\n");
                break;
        }
}

void *ldmsd_plug_context_get(ldmsd_plug_handle_t handle)
{
        ldmsd_cfgobj_t cfg = (ldmsd_cfgobj_t)handle;

        switch (cfg->type) {
        case LDMSD_CFGOBJ_SAMPLER:
                return ((ldmsd_cfgobj_sampler_t)cfg)->context;
        case LDMSD_CFGOBJ_STORE:
                return ((ldmsd_cfgobj_store_t)cfg)->context;
        default:
                ovis_log(NULL, OVIS_LERROR,
                         "ldmsd_plug_context_get(): handle is not a plugin cfgobj\n");
                return NULL;
        }
}

const char *ldmsd_plug_config_name_get(ldmsd_plug_handle_t handle)
{
        ldmsd_cfgobj_t cfg = (ldmsd_cfgobj_t)handle;

        return cfg->name;
        switch (cfg->type) {
        case LDMSD_CFGOBJ_SAMPLER:
                return ((ldmsd_cfgobj_sampler_t)cfg)->context;
        case LDMSD_CFGOBJ_STORE:
                return ((ldmsd_cfgobj_store_t)cfg)->context;
        default:
                ovis_log(NULL, OVIS_LERROR,
                         "ldmsd_plug_config_name_get(): handle is not a plugin cfgobj\n");
                return NULL;
        }
}

const char *ldmsd_plug_plugin_name_get(ldmsd_plug_handle_t handle)
{
        ldmsd_cfgobj_t cfg = (ldmsd_cfgobj_t)handle;

        switch (cfg->type) {
        case LDMSD_CFGOBJ_SAMPLER:
                return ((ldmsd_cfgobj_sampler_t)cfg)->plugin_name;
        case LDMSD_CFGOBJ_STORE:
                return ((ldmsd_cfgobj_store_t)cfg)->plugin_name;
        default:
                ovis_log(NULL, OVIS_LERROR,
                         "ldmsd_plug_plugin_name_get(): handle is not a plugin cfgobj\n");
                return NULL;
        }

}
