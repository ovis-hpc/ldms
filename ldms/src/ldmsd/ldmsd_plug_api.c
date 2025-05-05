/* -*- c-basic-offset: 8 -*-
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * Copyright 2025 Open Grid Computing, Inc.
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#include "ldmsd.h"

ovis_log_t ldmsd_plug_log_get(ldmsd_plug_handle_t handle)
{
	ldmsd_cfgobj_t cfg = (ldmsd_cfgobj_t)handle;

	switch (cfg->type) {
	case LDMSD_CFGOBJ_SAMPLER:
		return ((ldmsd_cfgobj_sampler_t)cfg)->log;
	case LDMSD_CFGOBJ_STORE:
		return ((ldmsd_cfgobj_store_t)cfg)->log;
	default:
		ovis_log(NULL, OVIS_LERROR,
			 "%s : handle is not a plugin cfgobj\n", __func__);
		break;
	}

	return NULL;
}

void ldmsd_plug_ctxt_set(ldmsd_plug_handle_t handle, void *context)
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

void *ldmsd_plug_ctxt_get(ldmsd_plug_handle_t handle)
{
	ldmsd_cfgobj_t cfg = (ldmsd_cfgobj_t)handle;

	switch (cfg->type) {
	case LDMSD_CFGOBJ_SAMPLER:
		return ((ldmsd_cfgobj_sampler_t)cfg)->context;
	case LDMSD_CFGOBJ_STORE:
		return ((ldmsd_cfgobj_store_t)cfg)->context;
	default:
		ovis_log(NULL, OVIS_LERROR,
			 "%s() : handle is not a plugin cfgobj\n", __func__);
		return NULL;
	}
}

const char *ldmsd_plug_cfg_name_get(ldmsd_plug_handle_t handle)
{
	ldmsd_cfgobj_t cfg = (ldmsd_cfgobj_t)handle;
	return cfg->name;
}

const char *ldmsd_plug_name_get(ldmsd_plug_handle_t handle)
{
	ldmsd_cfgobj_t cfg = (ldmsd_cfgobj_t)handle;

	switch (cfg->type) {
	case LDMSD_CFGOBJ_SAMPLER:
		return ((ldmsd_cfgobj_sampler_t)cfg)->plugin->api->name;
	case LDMSD_CFGOBJ_STORE:
		return ((ldmsd_cfgobj_store_t)cfg)->plugin->api->name;
	default:
		ovis_log(NULL, OVIS_LERROR,
			 "%s : handle is not a plugin cfgobj\n", __func__);
		return NULL;
	}
}
