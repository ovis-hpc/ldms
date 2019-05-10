/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2013-2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file sampler_atasmart.c
 * \brief Collect S.M.A.R.T. attribute values.
 *
 * The sampler uses \c libatasmart to collect the S.M.A.R.T metrics.
 *
 * For each attribute, all values are, except the raw value, collected by the
 * sampler. The metric name format is ATTR_FIELD, where 'ATTR' is the atasmart
 * attribute name and 'FIELD' is the field for each atasmart attribute. An
 * example of the metric name is `ECC_Error_Rate_Pretty`, a prettified value of
 * ECC_Error_Rate attribute.
 *
 *
 * NOTE: This sampler currently support only 1 disk per plugin instance. This is
 *       because of the one device per set policy and the fact that we do not
 *       have a conclusion to how component ID will be assigned in the case of
 *       multi-set sampler.
 *
 */
#define _GNU_SOURCE
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include <malloc.h>
#include <atasmart.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"


typedef struct atasmart_field_s {
	const char *name;
	int has_unit;
	enum ldms_value_type type;
} *atasmart_field_t;

/* macro for calculating static array length */
#define ALEN(A) (sizeof(A)/sizeof(A[0]))

/*
 * Fields for each atasmart attribute
 */
struct atasmart_field_s fields[] = {
	{  "Value",       0,  LDMS_V_S16  },
	{  "Worst",       0,  LDMS_V_S16  },
	{  "Threshold",   0,  LDMS_V_S16  },
	{  "Pretty",      1,  LDMS_V_U64  },
	{  "Prefailure",  0,  LDMS_V_U8   },
	{  "Online",      0,  LDMS_V_U8   },
	{  "Good_now",    0,  LDMS_V_S8   },
	{  "Good_past",   0,  LDMS_V_S8   },
	{  "Flag",        0,  LDMS_V_U16  },
};

const char *units[] = {
        [SK_SMART_ATTRIBUTE_UNIT_UNKNOWN] = "",
        [SK_SMART_ATTRIBUTE_UNIT_NONE] = "",
        [SK_SMART_ATTRIBUTE_UNIT_MSECONDS] = "msec",      /* milliseconds */
        [SK_SMART_ATTRIBUTE_UNIT_SECTORS] = "sectors",
        [SK_SMART_ATTRIBUTE_UNIT_MKELVIN] = "mK",       /* millikelvin */
        [SK_SMART_ATTRIBUTE_UNIT_SMALL_PERCENT] = "%", /* percentage with 3
							* decimal points */
        [SK_SMART_ATTRIBUTE_UNIT_PERCENT] = "%",       /* integer percentage */
        [SK_SMART_ATTRIBUTE_UNIT_MB] = "MB",
        [_SK_SMART_ATTRIBUTE_UNIT_MAX] = ""
};

#define NFIELDS (ALEN(fields))
#define NUNITS (ALEN(units))

static inline
const char *unit_str(SkSmartAttributeUnit u)
{
	if (u < NUNITS)
		return units[u];
	return "";
}


typedef struct atasmart_inst_s *atasmart_inst_t;
struct atasmart_inst_s {
	struct ldmsd_plugin_inst_s base;

	char *diskname;

	/* for schema-building routine */
	char buff[256];
	int rc;
	ldms_schema_t schema;

	/* atasmart disk structure */
	SkDisk *d;

	/* for update_set routine */
	int idx;
	ldms_set_t set;
};

/* ============== Sampler Plugin APIs ================= */

void __get_disk_info(SkDisk *d, const SkSmartAttributeParsedData *a,
				void *userdata)
{
	int i, rc;
	atasmart_inst_t inst = userdata;
	if (inst->rc) /* an error has occurred, no need to continue */
		return;
	for (i = 0; i < NFIELDS; i++) {
		snprintf(inst->buff, sizeof(inst->buff), "%s_%s",
			 a->name, fields[i].name);
		rc = ldms_schema_metric_add(inst->schema, inst->buff,
					    fields[i].type,
					    fields[i].has_unit?
						unit_str(a->pretty_unit):"");
		if (rc < 0) {
			inst->rc = -rc;
			return;
		}
	}
}

static
int atasmart_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	atasmart_inst_t inst = (void*)pi;
	int rc;

	/* Add atasmart metrics to the schema */
	inst->rc = 0;
	inst->schema = schema;
	rc = sk_disk_smart_read_data(inst->d);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "%s: Read data SkDisk '%s'. Error %d\n",
			  pi->inst_name, inst->diskname, rc);
		return rc;
	}
	rc = sk_disk_smart_parse_attributes(inst->d, __get_disk_info, inst);
	if (rc) {
		ldmsd_log(LDMSD_LERROR, "%s: SkDisk '%s' parse error: %d\n",
			  pi->inst_name, inst->diskname, rc);
		return rc;
	}
	if (inst->rc) {
		ldmsd_log(LDMSD_LERROR, "%s: metric add error: %d\n",
			  pi->inst_name, inst->rc);
		return inst->rc; /* an error from __get_disk_info() */
	}
	return 0;
}

static
void __set_metric(SkDisk *d, const SkSmartAttributeParsedData *a, void *udata)
{
	atasmart_inst_t inst = udata;

	/* Current */
	ldms_metric_set_s16(inst->set, inst->idx++,
			    a->current_value_valid?a->current_value:-1);
	/* Worst */
	ldms_metric_set_s16(inst->set, inst->idx++,
			    a->worst_value_valid?a->worst_value:-1);
	/* Threshold */
	ldms_metric_set_s16(inst->set, inst->idx++,
			    a->threshold_valid?a->threshold:-1);
	/* Pretty */
	ldms_metric_set_u64(inst->set, inst->idx++, a->pretty_value);

	/* 0 oldage, 1 prefailure */
	ldms_metric_set_u8(inst->set, inst->idx++, a->prefailure);

	/* online */
	ldms_metric_set_u8(inst->set, inst->idx++, a->online);

	/* good now: -1(na), 0(no), 1(yes) */
	ldms_metric_set_s8(inst->set, inst->idx++,
			   a->good_now_valid?a->good_now:-1);
	/* good in the past: -1(na), 0(no), 1(yes) */
	ldms_metric_set_s8(inst->set, inst->idx++,
			   a->good_in_the_past_valid?a->good_in_the_past:-1);
	/* flag */
	ldms_metric_set_u16(inst->set, inst->idx++, a->flags);

}
static
int atasmart_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	atasmart_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int rc;
	inst->set = set;
	inst->idx = samp->first_idx;
	rc = sk_disk_smart_read_data(inst->d);
	if (rc)
		return rc;
	sk_disk_smart_parse_attributes(inst->d, __set_metric, inst);
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *atasmart_desc(ldmsd_plugin_inst_t pi)
{
	return "sampler_atasmart - atasmart sampler plugin";
}

static char *_help = "\
sampler_atasmart synopsis:\n\
    config name=INST [COMMON_OPTIONS] disk=<DEV_NAME>\n\
\n\
Option descriptions:\n\
    disk   The name of the ata device (e.g. sda) to monitor.\n\
";

static
const char *atasmart_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void atasmart_del(ldmsd_plugin_inst_t pi)
{
	atasmart_inst_t inst = (void*)pi;

	/* The undo of atasmart_init and instance cleanup */
	if (inst->d)
		sk_disk_free(inst->d);
	if (inst->diskname)
		free(inst->diskname);
}

static
int atasmart_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	atasmart_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	const char *val;
	int rc;

	if (inst->d) {
		snprintf(ebuf, ebufsz, "%s: already configured.\n",
			 pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	val = json_attr_find_str(json, "disk");
	if (!val) {
		snprintf(ebuf, ebufsz, "%s: `disk` attribute required\n",
			 pi->inst_name);
		return EINVAL;
	}
	inst->diskname = strdup(val);
	if (!inst->diskname) {
		snprintf(ebuf, ebufsz, "%s: out of memory.\n", pi->inst_name);
		return ENOMEM;
	}
	rc = sk_disk_open(inst->diskname, &inst->d);
	if (rc) {
		snprintf(ebuf, ebufsz, "%s: cannot open disk `%s`, "
			 "error: %d.\n", pi->inst_name, inst->diskname, errno);
		return rc;
	}

	/* create schema + set */
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;
	set = samp->create_set(pi, samp->set_inst_name, samp->schema, NULL);
	if (!set)
		return errno;
	return 0;
}

static
int atasmart_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = atasmart_update_schema;
	samp->update_set = atasmart_update_set;

	return 0;
}

static
struct atasmart_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "sampler_atasmart",

                /* Common Plugin APIs */
		.desc   = atasmart_desc,
		.help   = atasmart_help,
		.init   = atasmart_init,
		.del    = atasmart_del,
		.config = atasmart_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	atasmart_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
