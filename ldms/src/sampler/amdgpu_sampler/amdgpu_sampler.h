/* -*- c-basic-offset: 8 -*- */
/* Copyright 2026 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */

#ifndef AMDGPU_SAMPLER_H
#define AMDGPU_SAMPLER_H

/**
 * amdgpu_sampler - LDMS sampler plugin for AMD GPU monitoring via AMD SMI
 *
 * This plugin uses the AMD System Management Interface (SMI) library to
 * collect GPU metrics. Users configure which fields to monitor, and the
 * plugin creates one LDMS set per detected GPU.
 *
 * Multiple instances can run concurrently with different field configurations
 * and sampling intervals.
 */

/* Plugin interface is exported via ldmsd_plugin_interface */

#endif /* AMDGPU_SAMPLER_H */
