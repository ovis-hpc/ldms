/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2016-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016-2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file all_example.c
 * \brief Example of an ldmsd sampler plugin that uses all metric types,
 * useful for testing store formats.
 */
#include <errno.h>
#include <stdlib.h>
#include "ldms.h"
#include "ldmsd.h"

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define array_num_ele_default 3

struct metric_construct {
	const char *name;
	enum ldms_value_type type;
	int n;
};

struct metric_construct metric_construct_entries[] = {
	{"char", LDMS_V_CHAR, 1},
	{"u8", LDMS_V_U8, 1},
	{"s8", LDMS_V_S8, 1},
	{"u16", LDMS_V_U16, 1},
	{"s16", LDMS_V_S16, 1},
	{"u32", LDMS_V_U32, 1},
	{"s32", LDMS_V_S32, 1},
	{"u64", LDMS_V_U64, 1},
	{"s64", LDMS_V_S64, 1},
	{"f32", LDMS_V_F32, 1},
	{"d32", LDMS_V_D64, 1},
	{"char_array", LDMS_V_CHAR_ARRAY, array_num_ele_default},
	{"u8_array", LDMS_V_U8_ARRAY, array_num_ele_default},
	{"s8_array", LDMS_V_S8_ARRAY, array_num_ele_default},
	{"u16_array", LDMS_V_U16_ARRAY, array_num_ele_default},
	{"s16_array", LDMS_V_S16_ARRAY, array_num_ele_default},
	{"u32_array", LDMS_V_U32_ARRAY, array_num_ele_default},
	{"s32_array", LDMS_V_S32_ARRAY, array_num_ele_default},
	{"u64_array", LDMS_V_U64_ARRAY, array_num_ele_default},
	{"s64_array", LDMS_V_S64_ARRAY, array_num_ele_default},
	{"float_array", LDMS_V_F32_ARRAY, array_num_ele_default},
	{"double_array", LDMS_V_D64_ARRAY, array_num_ele_default},
	{"char_end", LDMS_V_CHAR, 1},
	{NULL, LDMS_V_NONE, array_num_ele_default},
};

typedef struct all_example_inst_s *all_example_inst_t;
struct all_example_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
};

/* ============== Sampler Plugin APIs ================= */

static
int all_example_update_schema(ldmsd_plugin_inst_t i, ldms_schema_t schema)
{
	int rc;
	struct metric_construct *ent;
	ent = metric_construct_entries;
	while (ent->name) {
		ldmsd_log(LDMSD_LDEBUG, "%s: adding %s\n",
			  i->inst_name, ent->name);
		if (ent->type >= LDMS_V_CHAR_ARRAY)
			rc = ldms_schema_metric_array_add(schema,
					ent->name, ent->type, "unit", ent->n+1);
		else
			rc = ldms_schema_metric_add(schema, ent->name,
					ent->type, "unit");
		if (rc < 0) {
			/* rc is `-errno` */
			ldmsd_log(LDMSD_LERROR, "%s: adding %s failed\n",
				  i->inst_name, ent->name);
			return -rc;
		}
		ent++;
	}
	return 0;
}

static
int all_example_update_set(ldmsd_plugin_inst_t _i, ldms_set_t set, void *ctxt)
{
	int i, mid;
	ldmsd_sampler_type_t samp = (void*)_i->base;
	static uint8_t off = 76; // ascii L
	struct metric_construct *ent = metric_construct_entries;
	union ldms_value v;

	mid = samp->first_idx;
	while (ent->name) {
		for (i = 0; i < ent->n; i++) {
			switch (ent->type) {
			case LDMS_V_CHAR:
			case LDMS_V_CHAR_ARRAY:
				v.v_s8 = off;
				break;
			case LDMS_V_S8:
			case LDMS_V_S8_ARRAY:
				v.v_s8 = off;
				break;
			case LDMS_V_U8:
			case LDMS_V_U8_ARRAY:
				v.v_u8 = off;
				break;
			case LDMS_V_S16:
			case LDMS_V_S16_ARRAY:
				v.v_s16 = off;
				break;
			case LDMS_V_U16:
			case LDMS_V_U16_ARRAY:
				v.v_u16 = off;
				break;
			case LDMS_V_S32:
			case LDMS_V_S32_ARRAY:
				v.v_s32 = off;
				break;
			case LDMS_V_U32:
			case LDMS_V_U32_ARRAY:
				v.v_u32 = off;
				break;
			case LDMS_V_S64:
			case LDMS_V_S64_ARRAY:
				v.v_s64 = off;
				break;
			case LDMS_V_U64:
			case LDMS_V_U64_ARRAY:
				v.v_u64 = off;
				break;
			case LDMS_V_F32:
			case LDMS_V_F32_ARRAY:
				v.v_f = off;
				break;
			case LDMS_V_D64:
			case LDMS_V_D64_ARRAY:
				v.v_d = off;
				break;
			default:
				v.v_u64 = 0;
			}
			if (ent->type < LDMS_V_CHAR_ARRAY) {
				ldms_metric_set(set, mid, &v);
			} else {
				ldms_metric_array_set_val(set, mid, i, &v);
				if (i == ent->n-1) {
					v.v_u64 = 0;
					ldms_metric_array_set_val(set, mid, i+1, &v);
				}
			}
		}
		mid++;
		ent++;
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *all_example_desc(ldmsd_plugin_inst_t i)
{
	return "all_example - an example sampler utilizing all metric types";
}

static
const char *all_example_help(ldmsd_plugin_inst_t i)
{
	return "all_example does not have any plugin-specific options";
}

static
void all_example_del(ldmsd_plugin_inst_t i)
{
	/* The undo of all_example_init */
}

static
int all_example_config(ldmsd_plugin_inst_t i, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	ldmsd_sampler_type_t samp = (void*)i->base;
	ldms_set_t set;
	int rc;

	rc = samp->base.config(i, json, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */

	/* create schema + set */
	samp->schema = samp->create_schema(i);
	if (!samp->schema)
		return errno;
	set = samp->create_set(i, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;
	return 0;
}

static
int all_example_init(ldmsd_plugin_inst_t i)
{
	ldmsd_sampler_type_t samp = (void*)i->base;
	/* override update_schema() and update_set() */
	samp->update_schema = all_example_update_schema;
	samp->update_set = all_example_update_set;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct all_example_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "all_example",

                /* Common Plugin APIs */
		.desc   = all_example_desc,
		.help   = all_example_help,
		.init   = all_example_init,
		.del    = all_example_del,
		.config = all_example_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	all_example_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
