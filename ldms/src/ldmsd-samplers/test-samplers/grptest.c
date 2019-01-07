/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018-19 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018-19 Open Grid Computing, Inc. All rights reserved.
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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
#include <assert.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define N 8
#define MASK 0xFF

typedef struct grptest_inst_s *grptest_inst_t;
struct grptest_inst_s {
	struct ldmsd_plugin_inst_s base;
	ldms_set_t grp;
	int members;
};

/* ============== Sampler Plugin APIs ================= */

static
int grptest_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	int rc;
	rc = ldms_schema_metric_add(schema, "counter", LDMS_V_U32, "times");
	if (rc < 0)
		return -rc;
	return 0;
}

static
int grptest_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	uint32_t v;

	v = ldms_metric_get_u32(set, samp->first_idx);
	ldms_metric_set_u32(set, samp->first_idx, v+1);
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *grptest_desc(ldmsd_plugin_inst_t pi)
{
	return "grptest - plugin for testing LDMSD group";
}

static const char *__help = "\
grptest-specific synopsis:\n\
    config name=INST_NAME [COMMON_OPTIONS] members=FLAG\n\
\n\
Option descriptions:\n\
    members    An integer (e.g. 0xF0, 0360) less than 256 which represents\n\
               flags of membership. If the i_th bit is 1, the set{i} is a\n\
	       member of the group. Otherwise, the set{i} is not a member\n\
	       of the group.\n\
";

static
const char *grptest_help(ldmsd_plugin_inst_t pi)
{
	return __help;
}

static
void grptest_del(ldmsd_plugin_inst_t pi)
{
	/* do nothing */
	/* inst->grp cleanup is handled by samp->del() */
}

static
int __init_config(grptest_inst_t inst, struct attr_value_list *avl,
		  struct attr_value_list *kwl, char *ebuf, int ebufsz)
{
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	ldms_schema_t sch = NULL;
	int i;
	int rc = 0;
	char buff[256];
	ldms_set_t set;
	rc = samp->base.config(&inst->base, avl, kwl, ebuf, ebufsz);
	if (rc)
		goto out;
	snprintf(buff, sizeof(buff), "%s/grp", samp->set_inst_name);
	inst->grp = samp->create_set_group(&inst->base, buff, NULL);
	if (!inst->grp) {
		snprintf(ebuf, ebufsz, "%s: group creation failed "
			 "(errno: %d)\n", inst->base.inst_name, errno);
		rc = errno;
		goto out;
	}
	sch = samp->create_schema(&inst->base);
	if (!sch) {
		snprintf(ebuf, ebufsz, "%s: create_schema() failed, errno: %d\n",
			 inst->base.inst_name, errno);
		return errno;
	}
	for (i = 0; i < N; i++) {
		snprintf(buff, sizeof(buff), "%s/set%d", samp->set_inst_name, i);
		set = samp->create_set(&inst->base, buff, sch, NULL);
		if (!set) {
			snprintf(ebuf, ebufsz, "%s: set creation failed "
				 "(errno: %d)\n", inst->base.inst_name, errno);
			rc = errno;
			goto out;
		}
	}
out:
	if (sch)
		ldms_schema_delete(sch);
	return rc;
}

static
int grptest_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	grptest_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int rc;
	int i, m;
	int add, rm;
	char *str;
	char buff[256];

	if (!inst->grp) {
		/* do this only once */
		rc = __init_config(inst, avl, kwl, ebuf, ebufsz);
		if (rc)
			return rc;
		/* let-through */
	}

	/* process `members` flag */
	str = av_value(avl, "members");
	if (!str)
		return 0;
	m = strtoul(str, NULL, 0);
	m &= MASK;

	ldms_transaction_begin(inst->grp);
	rm = (m & inst->members) ^ inst->members;
	add = (m & inst->members) ^ m;
	for (i = 0; add || rm; add>>=1, rm>>=1, i++) {
		if (add & 1) {
			snprintf(buff, sizeof(buff), "%s/set%d",
				 samp->set_inst_name, i);
			rc = ldmsd_group_set_add(inst->grp, buff);
			if (rc) {
				snprintf(ebuf, ebufsz, "%s: group set add "
					 "failed (rc: %d)\n",
					 pi->inst_name, rc);
				goto err;
			}
			inst->members |= (1 << i);
		}
		if (rm & 1) {
			snprintf(buff, sizeof(buff), "%s/set%d",
				 samp->set_inst_name, i);
			rc = ldmsd_group_set_rm(inst->grp, buff);
			if (rc) {
				snprintf(ebuf, ebufsz, "%s: group set remove "
					 "failed (rc: %d)\n",
					 pi->inst_name, rc);
				goto err;
			}
			inst->members &= ~(1 << i);
		}
	}
err:
	return rc;
}

static
int grptest_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = grptest_update_schema;
	samp->update_set = grptest_update_set;

	/* NOTE More initialization code here if needed */
	return 0;
}

static
struct grptest_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "grptest",

                /* Common Plugin APIs */
		.desc   = grptest_desc,
		.help   = grptest_help,
		.init   = grptest_init,
		.del    = grptest_del,
		.config = grptest_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	grptest_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
