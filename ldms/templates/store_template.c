/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file store_template.c
 *
 * A template for an LDMSD store plugin implementation.
 *
 * Simply find-and-replace XYZ with the name of the store.
 */

#include "ldmsd.h"
#include "ldmsd_store.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

typedef struct XYZ_inst_s *XYZ_inst_t;
struct XYZ_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
};


/* ============== Store Plugin APIs ================= */

static int
XYZ_open(ldmsd_plugin_inst_t pi, ldmsd_strgp_t strgp)
{
	/* Perform `open` operation */
	XYZ_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;
	return 0;
}

static int
XYZ_close(ldmsd_plugin_inst_t pi)
{
	/* Perform `close` operation */
	XYZ_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;
	return 0;
}

static int
XYZ_flush(ldmsd_plugin_inst_t pi)
{
	/* Perform `flush` operation */
	XYZ_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;
	return 0;
}

static int
XYZ_store(ldmsd_plugin_inst_t pi, ldms_set_t set, ldmsd_strgp_t strgp)
{
	/* `store` data from `set` into the store */
	XYZ_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;
	return ENOSYS;
}

/* ============== Common Plugin APIs ================= */

static const char *
XYZ_desc(ldmsd_plugin_inst_t pi)
{
	return "XYZ - XYZ store plugin";
}

static char *_help = "\
XYZ takes no extra options.\n\
";

static const char *
XYZ_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static int
XYZ_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	XYZ_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;
	int rc;

	rc = store->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */

	return 0;
}

static void
XYZ_del(ldmsd_plugin_inst_t pi)
{
	XYZ_inst_t inst = (void*)pi;

	/* The undo of XYZ_init and instance cleanup */
}

static int
XYZ_init(ldmsd_plugin_inst_t pi)
{
	XYZ_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;

	/* override store operations */
	store->open = XYZ_open;
	store->close = XYZ_close;
	store->flush = XYZ_flush;
	store->store = XYZ_store;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct XYZ_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = LDMSD_STORE_TYPENAME,
		.plugin_name = "XYZ",

                /* Common Plugin APIs */
		.desc   = XYZ_desc,
		.help   = XYZ_help,
		.init   = XYZ_init,
		.del    = XYZ_del,
		.config = XYZ_config,

	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	XYZ_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
