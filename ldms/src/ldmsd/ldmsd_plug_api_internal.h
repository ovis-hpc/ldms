/*
 * Copyright (c) 2025 Lawrence Livermore National Security, LLC
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#ifndef _LDMSD_PLUG_API_INTERNAL_H
#define _LDMSD_PLUG_API_INTERNAL_H

#include "ldmsd.h"

/*
 * struct ldmsd_plug_api_s is currently implemented as a type
 * punning alias for struct ldmsd_cfgobj. This provides
 * a unique compiler-recognizable type upon which to define the
 * ldmsd_plug_handle_t opaque handle. It does this while keeping the
 * real implmentation of the opaque handle (currently a
 * struct ldmsd_cfgobj or one of its type-punned derivatives) opaque
 * to the users of the plugin API.
 *
 * NOTE - The implication of the above is that nothing can be added
 * to struct ldmsd_plug_api_s in its current implmentation!
 */
struct ldmsd_plug_api_s {
	struct ldmsd_cfgobj cfgobj;
};

#endif
