/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file store_sos.c
 * \brief SOS LDMSD storage plugin.
 */

#include "ldmsd.h"
#include "ldmsd_plugin.h"
#include "ldmsd_store.h"

#include "sos/sos.h"

typedef struct store_sos_inst_s *store_sos_inst_t;

struct store_sos_inst_s {
	struct ldmsd_plugin_inst_s base;
	char *path;
	sos_t sos;
	sos_schema_t sos_schema;
	sos_attr_t attr_ts;
	sos_attr_t attr_comp_id;
	sos_attr_t attr_job_id;
	int ldms_comp_id_idx;
	int ldms_job_id_idx;
	int ldms_app_id_idx;
};

static
const char *store_sos_desc(ldmsd_plugin_inst_t i)
{
	return "SOS LDMSD storage plugin";
}

static
const char *store_sos_help(ldmsd_plugin_inst_t i)
{
	return "config path=SOS_ROOT_PATH\n";
}

static
int store_sos_open(ldmsd_plugin_inst_t i, ldmsd_strgp_t strgp);
static
int store_sos_close(ldmsd_plugin_inst_t i);
static
int store_sos_flush(ldmsd_plugin_inst_t i);
static
int store_sos_store(ldmsd_plugin_inst_t i, ldms_set_t set, ldmsd_strgp_t strgp);

int store_sos_init(ldmsd_plugin_inst_t i)
{
	ldmsd_store_type_t store = (void*)i->base;
	store->open = store_sos_open;
	store->close = store_sos_close;
	store->flush = store_sos_flush;
	store->store = store_sos_store;
	return 0;
}

void store_sos_del(ldmsd_plugin_inst_t i)
{
	store_sos_inst_t inst = (void*)i;
	if (inst->path)
		free(inst->path);
	if (inst->sos)
		sos_container_close(inst->sos, SOS_COMMIT_ASYNC);
}

int store_sos_config(ldmsd_plugin_inst_t i, json_entity_t json,
					char *ebuf, int ebufsz)
{
	ldmsd_store_type_t store = (void*)i->base;
	store_sos_inst_t inst = (void*)i;
	int rc;
	const char *val;

	rc = store->base.config(i, json, ebuf, ebufsz);
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

static
sos_type_t sos_type_map[] = {
	[LDMS_V_NONE] = SOS_TYPE_UINT32,
	[LDMS_V_U8] = SOS_TYPE_UINT32,
	[LDMS_V_S8] = SOS_TYPE_INT32,
	[LDMS_V_U16] = SOS_TYPE_UINT16,
	[LDMS_V_S16] = SOS_TYPE_UINT16,
	[LDMS_V_U32] = SOS_TYPE_UINT32,
	[LDMS_V_S32] = SOS_TYPE_INT32,
	[LDMS_V_U64] = SOS_TYPE_UINT64,
	[LDMS_V_S64] = SOS_TYPE_INT64,
	[LDMS_V_F32] = SOS_TYPE_FLOAT,
	[LDMS_V_D64] = SOS_TYPE_DOUBLE,
	[LDMS_V_CHAR_ARRAY] = SOS_TYPE_CHAR_ARRAY,
	[LDMS_V_U8_ARRAY] = SOS_TYPE_BYTE_ARRAY,
	[LDMS_V_S8_ARRAY] = SOS_TYPE_BYTE_ARRAY,
	[LDMS_V_U16_ARRAY] = SOS_TYPE_UINT16_ARRAY,
	[LDMS_V_S16_ARRAY] = SOS_TYPE_INT16_ARRAY,
	[LDMS_V_U32_ARRAY] = SOS_TYPE_UINT32_ARRAY,
	[LDMS_V_S32_ARRAY] = SOS_TYPE_INT32_ARRAY,
	[LDMS_V_U64_ARRAY] = SOS_TYPE_UINT64_ARRAY,
	[LDMS_V_S64_ARRAY] = SOS_TYPE_INT64_ARRAY,
	[LDMS_V_F32_ARRAY] = SOS_TYPE_FLOAT_ARRAY,
	[LDMS_V_D64_ARRAY] = SOS_TYPE_DOUBLE_ARRAY,
};

static
int __part_create(store_sos_inst_t inst)
{
	int rc;
	sos_part_t part;
	rc = sos_part_create(inst->sos, "PRIMARY", NULL);
	if (rc)
		return rc;
	part = sos_part_find(inst->sos, "PRIMARY");
	if (!part)
		return errno;
	rc = sos_part_state_set(part, SOS_PART_STATE_PRIMARY);
	sos_part_put(part);
	if (rc)
		return rc;
	return 0;
}

static
sos_attr_t __attr_add_get(sos_schema_t schema, const char *name, sos_type_t type)
{
	int rc;
	sos_attr_t attr;
	attr = sos_schema_attr_by_name(schema, name);
	if (attr)
		return attr;
	rc = sos_schema_attr_add(schema, name, type);
	if (rc)
		return NULL;
	attr = sos_schema_attr_by_name(schema, name);
	return attr;
}

static
int __schema_create(store_sos_inst_t inst, ldmsd_strgp_t strgp)
{
	ldmsd_strgp_metric_t m;
	int rc;
	inst->sos_schema = sos_schema_new(strgp->schema);

	if (!inst->sos_schema) {
		rc = errno;
		goto err0;
	}

	/* timestamp */
	inst->attr_ts = __attr_add_get(inst->sos_schema, "timestamp",
				   SOS_TYPE_TIMESTAMP);
	if (!inst->attr_ts) {
		rc = errno;
		goto err1;
	}
	rc = sos_schema_index_add(inst->sos_schema, "timestamp");
	if (rc)
		goto err1;

	/* component_id  */
	inst->attr_comp_id = __attr_add_get(inst->sos_schema, "component_id",
				   SOS_TYPE_UINT64);
	if (!inst->attr_comp_id) {
		rc = errno;
		goto err1;
	}
	rc = sos_schema_index_add(inst->sos_schema, "component_id");
	if (rc)
		goto err1;

	/* job_id */
	inst->attr_job_id = __attr_add_get(inst->sos_schema, "job_id",
				   SOS_TYPE_UINT64);
	if (!inst->attr_job_id) {
		rc = errno;
		goto err1;
	}
	rc = sos_schema_index_add(inst->sos_schema, "job_id");
	if (rc)
		goto err1;

	for (m = ldmsd_strgp_metric_first(strgp);
			m; m = ldmsd_strgp_metric_next(m)) {
		if (0 == strcmp(m->name, "timestamp") ||
				0 == strcmp(m->name, "component_id") ||
				0 == strcmp(m->name, "job_id")) {
			continue;
		}
		rc = sos_schema_attr_add(inst->sos_schema, m->name,
					 sos_type_map[m->type]);
		if (rc)
			goto err1;
	}

	/*
	 * Component/Time Index
	 */
	char *comp_time_attrs[] = { "component_id", "timestamp" };
	rc = sos_schema_attr_add(inst->sos_schema, "comp_time", SOS_TYPE_JOIN,
				 2, comp_time_attrs);
	if (rc)
		goto err1;
	rc = sos_schema_index_add(inst->sos_schema, "comp_time");
	if (rc)
		goto err1;
	/*
	 * Job/Component/Time Index
	 */
	char *job_comp_time_attrs[] = { "job_id", "component_id", "timestamp" };
	rc = sos_schema_attr_add(inst->sos_schema, "job_comp_time",
				 SOS_TYPE_JOIN, 3, job_comp_time_attrs);
	if (rc)
		goto err1;
	rc = sos_schema_index_add(inst->sos_schema, "job_comp_time");
	if (rc)
		goto err1;

	/*
	 * Job/Time/Component Index
	 */
	char *job_time_comp_attrs[] = { "job_id", "timestamp", "component_id" };
	rc = sos_schema_attr_add(inst->sos_schema, "job_time_comp",
				 SOS_TYPE_JOIN, 3, job_time_comp_attrs);
	if (rc)
		goto err1;
	rc = sos_schema_index_add(inst->sos_schema, "job_time_comp");
	if (rc)
		goto err1;

	rc = sos_schema_add(inst->sos, inst->sos_schema);
	if (rc)
		goto err1;
	return 0;

 err1:
	inst->attr_ts = NULL;
	inst->attr_comp_id = NULL;
	inst->attr_job_id = NULL;
	sos_schema_free(inst->sos_schema);
	inst->sos_schema = NULL;
 err0:
	return rc;
}

static
int store_sos_open(ldmsd_plugin_inst_t i, ldmsd_strgp_t strgp)
{
	ldmsd_store_type_t store = (void*)i->base;
	store_sos_inst_t inst = (void*)i;
	sos_part_iter_t piter;
	sos_part_t part;
	int rc;

	if (inst->sos) {
		ldmsd_lerror("store_sos(%s): already opened\n", i->inst_name);
		rc = EBUSY;
		goto err0;
	}

	inst->ldms_comp_id_idx = -2;
	inst->ldms_job_id_idx = -2;
	inst->ldms_app_id_idx = -2;

 open:
	inst->sos = sos_container_open(inst->path, SOS_PERM_RW);
	if (!inst->sos) {
		/* open failed, try creating */
		rc = sos_container_new(inst->path, store->perm);
		if (rc) {
			ldmsd_lerror("store_sos(%s): sos_container_new() "
				     "failed, rc: %d\n", i->inst_name, rc);
			goto err0;
		}
		goto open;
	}

	/* Check primary partition, create one if not existed */
	piter = sos_part_iter_new(inst->sos);
	if (!piter) {
		rc = errno;
		goto err1;
	}
	rc = ENOENT;
	for (part = sos_part_first(piter); part; part = sos_part_next(piter)) {
		sos_part_state_t st;
		st = sos_part_state(part);
		if (st == SOS_PART_STATE_PRIMARY) {
			rc = 0;
			sos_part_put(part);
			part = NULL;
			break;
		}
	}
	sos_part_iter_free(piter);
	if (rc == ENOENT) {
		/* needs to create PRIMARY partition */
		rc = __part_create(inst);
		if (rc) {
			ldmsd_lerror("store_sos(%s): __part_create() "
				     "failed, rc: %d\n", i->inst_name, rc);
			goto err1;
		}
	}

	/* Check if the schema has been created */
	inst->sos_schema = sos_schema_by_name(inst->sos, strgp->schema);
	if (!inst->sos_schema) {
		rc = __schema_create(inst, strgp);
		if (rc) {
			ldmsd_lerror("store_sos(%s): __schema_create() "
				     "failed, rc: %d\n", i->inst_name, rc);
			goto err1;
		}
	}

	return 0;

 err1:
	sos_container_close(inst->sos, SOS_COMMIT_ASYNC);
	inst->sos = NULL;
 err0:
	return rc;
}

static
int store_sos_close(ldmsd_plugin_inst_t i)
{
	store_sos_inst_t inst = (void*)i;
	if (!inst->sos)
		return EBUSY;
	sos_container_close(inst->sos, SOS_COMMIT_ASYNC);
	inst->sos = NULL;
	return 0;
}

static
int store_sos_flush(ldmsd_plugin_inst_t i)
{
	store_sos_inst_t inst = (void*)i;
	return sos_container_commit(inst->sos, SOS_COMMIT_ASYNC);
}

static
int store_sos_store(ldmsd_plugin_inst_t i, ldms_set_t set, ldmsd_strgp_t strgp)
{
	store_sos_inst_t inst = (void*)i;
	ldmsd_strgp_metric_t m;
	sos_obj_t obj;
	sos_attr_t attr;
	ldms_mval_t mval;
	int alen = 0;
	int sz = 0;
	sos_value_data_t vd;
	sos_value_t sval;
	struct sos_value_s _sval;
	struct ldms_timestamp ldms_ts;
	struct sos_timeval_s sos_ts;
	int rc;

	obj = sos_obj_new(inst->sos_schema);
	if (!obj)
		return errno;

	/* timestamp */
	attr = sos_schema_attr_first(inst->sos_schema);
	vd = sos_obj_attr_data(obj, attr, NULL);
	ldms_ts = ldms_transaction_timestamp_get(set);
	sos_ts.secs = ldms_ts.sec;
	sos_ts.usecs = ldms_ts.usec;
	sos_value_data_set(vd, SOS_TYPE_TIMESTAMP, sos_ts);

	/* component_id */
	attr = sos_schema_attr_next(attr);
	vd = sos_obj_attr_data(obj, attr, NULL);
	if (inst->ldms_comp_id_idx == -2) {
		/* lookup once */
		inst->ldms_comp_id_idx = ldms_metric_by_name(set,
							     "component_id");
	}
	if (inst->ldms_comp_id_idx >= 0) {
		mval = ldms_metric_get(set, inst->ldms_comp_id_idx);
		sos_value_data_set(vd, SOS_TYPE_UINT64, mval->v_u64);
	} else {
		sos_value_data_set(vd, SOS_TYPE_UINT64, 0L);
	}

	/* job_id */
	attr = sos_schema_attr_next(attr);
	vd = sos_obj_attr_data(obj, attr, NULL);
	if (inst->ldms_job_id_idx == -2) {
		/* lookup once */
		inst->ldms_job_id_idx = ldms_metric_by_name(set, "job_id");
	}
	if (inst->ldms_job_id_idx >= 0) {
		mval = ldms_metric_get(set, inst->ldms_job_id_idx);
		sos_value_data_set(vd, SOS_TYPE_UINT64, mval->v_u64);
	} else {
		sos_value_data_set(vd, SOS_TYPE_UINT64, 0L);
	}

	/* app_id */
	attr = sos_schema_attr_next(attr);
	vd = sos_obj_attr_data(obj, attr, NULL);
	if (inst->ldms_app_id_idx == -2) {
		/* lookup once */
		inst->ldms_app_id_idx = ldms_metric_by_name(set, "app_id");
	}
	if (inst->ldms_app_id_idx >= 0) {
		mval = ldms_metric_get(set, inst->ldms_app_id_idx);
		sos_value_data_set(vd, SOS_TYPE_UINT64, mval->v_u64);
	} else {
		sos_value_data_set(vd, SOS_TYPE_UINT64, 0L);
	}


	/* other metrics -- following the same order as we create sos schema */
	m = ldmsd_strgp_metric_first(strgp);
	while (m && attr) {
		if (0 == strcmp(m->name, "component_id") ||
				0 == strcmp(m->name, "job_id")) {
			/* only skip ldms metric like when we
			 * create sos_schema */
			goto ldms_skip;
		}
		mval = ldms_metric_get(set, m->idx);

		if (ldms_type_is_array(m->type)) {
			if (m->type == LDMS_V_CHAR_ARRAY) {
				alen = strlen(mval->a_char) + 1;
				sz = alen;
			} else {
				alen = ldms_metric_array_get_len(set, m->idx);
				sz = sos_attr_size(attr);
			}
			sval = sos_array_new(&_sval, attr, obj, alen);
		} else  {
			sval = sos_value_init(&_sval, obj, attr);
			sz = sos_value_size(sval);
		}
		sos_value_memcpy(sval, mval, sz);
		sos_value_put(sval);

		attr = sos_schema_attr_next(attr);
	ldms_skip:
		m = ldmsd_strgp_metric_next(m);
	}
	rc = sos_obj_index(obj);
	sos_obj_put(obj);
	return rc;
}

struct store_sos_inst_s __inst = {
	.base = {
		.version.version = LDMSD_PLUGIN_VERSION,

		.type_name   = "store",
		.plugin_name = "store_sos",

		.desc   = store_sos_desc,
		.help   = store_sos_help,
		.init   = store_sos_init,
		.del    = store_sos_del,
		.config = store_sos_config,
	},
};

void *new()
{
	store_sos_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return inst;
}
