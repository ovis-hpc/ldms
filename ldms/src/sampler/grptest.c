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
#include "sampler_base.h"

#define SAMP "grptest"

#define N 8
#define MASK 0xFF
/* we have only 8 members */

static ldms_set_t grp = NULL;
static ldms_set_t set[N] = {NULL,};
static FILE *mf = 0;
static base_data_t base;
int members = 0; /* flags: bit i being 1 means set[i] is in the group */

static ovis_log_t mylog;

#define DEFAULT_PREFIX "localhost"
const char *prefix = NULL;

static ldms_schema_t sc;

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP BASE_CONFIG_USAGE;
}

static int create_metric_set(base_data_t base)
{
	int i, rc;
	char buff[128];
	const char *name;

	snprintf(buff, sizeof(buff), "%s/grp", prefix);
	grp = ldmsd_group_new(buff);
	if (!grp) {
		rc = errno;
		goto err0;
	}
	rc = ldms_set_producer_name_set(grp, prefix);
	if (rc)
		goto err0;
	rc = ldms_set_publish(grp);
	if (rc)
		goto err0;
	rc = ldmsd_set_register(grp, SAMP);
	if (rc)
		goto err0;

	for (i = 0; i < N; i++) {
		snprintf(buff, sizeof(buff), "%s/set%d", prefix, i);
		set[i] = ldms_set_new(buff, sc);
		if (!set[i]) {
			rc = errno;
			goto err1;
		}
		ldms_transaction_begin(set[i]);
		ldms_metric_set_u32(set[i], 0, 0);
		ldms_transaction_end(set[i]);
		rc = ldms_set_publish(set[i]);
		if (rc)
			goto err1;
		rc = ldms_set_producer_name_set(set[i], prefix);
		if (rc)
			goto err1;
		rc = ldmsd_set_register(set[i], SAMP);
		if (rc)
			goto err1;
	}

	return 0;

err1:
	for (i = 0; i < N; i++) {
		if (set[i]) {
			name = ldms_set_instance_name_get(set[i]);
			ldmsd_set_deregister(name, SAMP);
			ldms_set_delete(set[i]);
			set[i] = NULL;
		}
	}
	name = ldms_set_instance_name_get(grp);
	ldmsd_set_deregister(name, SAMP);
err0:
	return rc;
}

static int config_init(struct ldmsd_plugin *self, struct attr_value_list *kwl,
		       struct attr_value_list *avl)
{
	int rc;
	base = base_config(avl, self->inst_name, SAMP, mylog);
	if (!base) {
		rc = errno;
		goto err0;
	}
	prefix = av_value(avl, "prefix");
	if (!prefix) {
		prefix = DEFAULT_PREFIX;
	}
	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": failed to create a metric set.\n");
		goto err1;
	}
	return 0;

err1:
	base_del(base);
	base = NULL;
err0:
	return rc;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl,
		  struct attr_value_list *avl)
{
	int rc = 0;
	char *str;
	int i, m;
	int add, rm;
	const char *name;

	if (!grp) {
		rc = config_init(self, kwl, avl);
		if (rc)
			return rc;
	}

	/* process `members` flag */
	str = av_value(avl, "members");
	if (!str)
		return 0;
	m = strtoul(str, NULL, 0);
	m &= MASK;

	ldms_transaction_begin(grp);
	rm = (m & members) ^ members;
	add = (m & members) ^ m;
	for (i = 0; add || rm; add>>=1, rm>>=1, i++) {
		if (add & 1) {
			name = ldms_set_instance_name_get(set[i]);
			rc = ldmsd_group_set_add(grp, name);
			if (rc)
				goto err;
			members |= (1 << i);
		}
		if (rm & 1) {
			name = ldms_set_instance_name_get(set[i]);
			rc = ldmsd_group_set_rm(grp, name);
			if (rc)
				goto err;
			members &= ~(1 << i);
		}
	}
err:
	ldms_transaction_end(grp);

	return rc;
}

static void __set_sample(ldms_set_t set)
{
	uint32_t counter;
	ldms_transaction_begin(set);
	counter = ldms_metric_get_u32(set, 0);
	ldms_metric_set_u32(set, 0, counter+1);
	ldms_transaction_end(set);
}

static int sample(struct ldmsd_sampler *self)
{
	int i;
	if (!grp) {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);
	for (i = 0; i < N; i++) {
		__set_sample(set[i]);
	}
	base_sample_end(base);
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	if (mf)
		fclose(mf);
	mf = NULL;
	if (base)
		base_del(base);
	if (grp)
		ldms_set_delete(grp);
	grp = NULL;
	if (mylog)
		ovis_log_destroy(mylog);
}

static struct ldmsd_sampler meminfo_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.sample = sample,
};

struct ldmsd_plugin *get_plugin()
{
	int rc;
	mylog = ovis_log_register("sampler."SAMP, "Message for the " SAMP " plugin");
	if (!mylog) {
		rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the log subsystem "
					"of '" SAMP "' plugin. Error %d\n", rc);
	}
	if (!sc) {
		sc = ldms_schema_new("set_schema");
		if (!sc)
			return NULL;
		rc = ldms_schema_metric_add(sc, "counter", LDMS_V_U32);
		if (rc) {
			ldms_schema_delete(sc);
			sc = NULL;
			return NULL;
		}
	}
	return &meminfo_plugin.base;
}
