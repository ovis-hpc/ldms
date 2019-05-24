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
 * \file store_test.c
 *
 * PlainText store.
 */

#include "ldmsd.h"
#include "ldmsd_store.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

typedef struct store_test_inst_s *store_test_inst_t;
struct store_test_inst_s {
	struct ldmsd_plugin_inst_s base;
	char *path;
	FILE *f;
};


/* ============== Store Plugin APIs ================= */

static int
store_test_open(ldmsd_plugin_inst_t pi, ldmsd_strgp_t strgp)
{
	/* Perform `open` operation */
	store_test_inst_t inst = (void*)pi;
	inst->f = fopen(inst->path, "a");
	if (!inst->f)
		return errno;
	return 0;
}

static int
store_test_close(ldmsd_plugin_inst_t pi)
{
	/* Perform `close` operation */
	store_test_inst_t inst = (void*)pi;
	fclose(inst->f);
	inst->f = NULL;
	return 0;
}

static int
store_test_flush(ldmsd_plugin_inst_t pi)
{
	/* Perform `flush` operation */
	store_test_inst_t inst = (void*)pi;
	fflush(inst->f);
	return 0;
}

static void
fprint_metric_array_val(FILE *f, ldms_set_t set, int i)
{
	enum ldms_value_type type = ldms_metric_type_get(set, i);
	int j, n;
	n = ldms_metric_array_get_len(set, i);
	for (j = 0; j < n; j++) {
		if (j)
			fprintf(f, ",");
		switch (type) {
		case LDMS_V_U8_ARRAY:
			fprintf(f, "%hhu", ldms_metric_array_get_u8(set, i, j));
			break;
		case LDMS_V_S8_ARRAY:
			fprintf(f, "%hhd", ldms_metric_array_get_s8(set, i, j));
			break;
		case LDMS_V_U16_ARRAY:
			fprintf(f, "%hu", ldms_metric_array_get_u16(set, i, j));
			break;
		case LDMS_V_S16_ARRAY:
			fprintf(f, "%hd", ldms_metric_array_get_s16(set, i, j));
			break;
		case LDMS_V_U32_ARRAY:
			fprintf(f, "%u", ldms_metric_array_get_u32(set, i, j));
			break;
		case LDMS_V_S32_ARRAY:
			fprintf(f, "%d", ldms_metric_array_get_s32(set, i, j));
			break;
		case LDMS_V_U64_ARRAY:
			fprintf(f, "%lu", ldms_metric_array_get_u64(set, i, j));
			break;
		case LDMS_V_S64_ARRAY:
			fprintf(f, "%ld", ldms_metric_array_get_s64(set, i, j));
			break;
		case LDMS_V_F32_ARRAY:
			fprintf(f, "%f", ldms_metric_array_get_float(set, i, j));
			break;
		case LDMS_V_D64_ARRAY:
			fprintf(f, "%lf", ldms_metric_array_get_double(set, i, j));
			break;
		default:
			assert(0);
		}
	}
}

static void
fprint_metric_val(FILE *f, ldms_set_t set, int i)
{
	enum ldms_value_type type = ldms_metric_type_get(set, i);
	if (type == LDMS_V_CHAR_ARRAY) {
		fprintf(f, "%s", ldms_metric_array_get_str(set, i));
		return;
	}
	if (ldms_type_is_array(type)) {
		fprint_metric_array_val(f, set, i);
		return ;
	}
	switch (type) {
	case LDMS_V_CHAR:
		fprintf(f, "%c", ldms_metric_get_char(set, i));
		break;
	case LDMS_V_U8:
		fprintf(f, "%hhu", ldms_metric_get_u8(set, i));
		break;
	case LDMS_V_S8:
		fprintf(f, "%hhd", ldms_metric_get_s8(set, i));
		break;
	case LDMS_V_U16:
		fprintf(f, "%hu", ldms_metric_get_u16(set, i));
		break;
	case LDMS_V_S16:
		fprintf(f, "%hd", ldms_metric_get_s16(set, i));
		break;
	case LDMS_V_U32:
		fprintf(f, "%u", ldms_metric_get_u32(set, i));
		break;
	case LDMS_V_S32:
		fprintf(f, "%d", ldms_metric_get_s32(set, i));
		break;
	case LDMS_V_U64:
		fprintf(f, "%lu", ldms_metric_get_u64(set, i));
		break;
	case LDMS_V_S64:
		fprintf(f, "%ld", ldms_metric_get_s64(set, i));
		break;
	case LDMS_V_F32:
		fprintf(f, "%f", ldms_metric_get_float(set, i));
		break;
	case LDMS_V_D64:
		fprintf(f, "%lf", ldms_metric_get_double(set, i));
		break;
	default:
		assert(0);
	}
}

static int
store_test_store(ldmsd_plugin_inst_t pi, ldms_set_t set, ldmsd_strgp_t strgp)
{
	/* `store` data from `set` into the store */
	store_test_inst_t inst = (void*)pi;
	ldmsd_strgp_metric_t m;
	const char *setname = ldms_set_instance_name_get(set);
	struct ldms_timestamp ts = ldms_transaction_timestamp_get(set);

	for (m = ldmsd_strgp_metric_first(strgp);
			m;
			m = ldmsd_strgp_metric_next(m)) {
		fprintf(inst->f, "%s:%d.%06d:%s:%s:%s:",
				 setname,
				 ts.sec, ts.usec,
				 m->name,
				 m->flags & LDMS_MDESC_F_DATA ? "DATA" : "META",
				 ldms_metric_type_to_str(m->type));
		fprint_metric_val(inst->f, set, m->idx);
		fprintf(inst->f, "\n");
	}

	return 0;
}

/* ============== Common Plugin APIs ================= */

static const char *
store_test_desc(ldmsd_plugin_inst_t pi)
{
	return "store_test - write data to plain text file";
}

static const char *
store_test_help(ldmsd_plugin_inst_t pi)
{
	return	"store_test config synopsis:\n"
		"    config name=INST [COMMON OPTIONS] path=PATH_TO_FILE\n"
		"\n"
		"Descriptions:\n"
		"    store_test writes plain text data to the file specified\n"
		"    by `path` parameter. If the file does not exist,\n"
		"    it will be created. If the file existed, it will be\n"
		"    appended to.\n";
}

static int
store_test_config(ldmsd_plugin_inst_t pi, json_entity_t json,
		 char *ebuf, int ebufsz)
{
	ldmsd_store_type_t store = LDMSD_STORE(pi);
	store_test_inst_t inst = (void*)pi;
	int rc;
	const char *val;

	rc = store->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	val = json_attr_find_str(json, "path");
	if (!val) {
		snprintf(ebuf, ebufsz, "missing `path` attribute.\n");
		return EINVAL;
	}
	inst->path = strdup(val);
	if (!inst->path) {
		snprintf(ebuf, ebufsz, "Out of memory.\n");
		return errno;
	}
	return 0;
}

static void
store_test_del(ldmsd_plugin_inst_t pi)
{
	store_test_inst_t inst = (void*)pi;
	if (inst->path)
		free(inst->path);
	if (inst->f)
		fclose(inst->f);
}

static int
store_test_init(ldmsd_plugin_inst_t pi)
{
	store_test_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;

	/* override store operations */
	store->open = store_test_open;
	store->close = store_test_close;
	store->flush = store_test_flush;
	store->store = store_test_store;

	return 0;
}

static struct store_test_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = LDMSD_STORE_TYPENAME,
		.plugin_name = "store_test",

                /* Common Plugin APIs */
		.desc   = store_test_desc,
		.help   = store_test_help,
		.init   = store_test_init,
		.del    = store_test_del,
		.config = store_test_config,

	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t
new()
{
	store_test_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
