/**
 * Copyright (c) 2016,2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2016,2018 Open Grid Computing, Inc. All rights reserved.
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
 * \file lnet.c
 * \brief /proc/?/lnet/stats data provider
 *
 * as of Lustre 2.5-2.8:
 * __proc_lnet_stats in
 * lustre-?/lnet/lnet/router_proc.c
 * generates the proc file as:
 *         len = snprintf(tmpstr, tmpsiz,
 *                     "%u %u %u %u %u %u %u "LPU64" "LPU64" "
 *                     LPU64" "LPU64,
 *                     ctrs->msgs_alloc, ctrs->msgs_max,
 *                     ctrs->errors,
 *                     ctrs->send_count, ctrs->recv_count,
 *                     ctrs->route_count, ctrs->drop_count,
 *                     ctrs->send_length, ctrs->recv_length,
 *                     ctrs->route_length, ctrs->drop_length);
 * where LPU64 is equivalent to uint64_t.
 */
#define _GNU_SOURCE
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

#define PROC_FILE_DEFAULT "/proc/sys/lnet/stats"

#define ALEN(X) (sizeof(X)/sizeof((X)[0]))
static const char * stat_name[] = {
	"msgs_alloc",
	"msgs_max",
	"errors",
	"send_count",
	"recv_count",
	"route_count",
	"drop_count",
	"send_length",
	"recv_length",
	"route_length",
	"drop_length",
};
#define N_METRICS ALEN(stat_name)

typedef struct lnet_inst_s *lnet_inst_t;
struct lnet_inst_s {
	struct ldmsd_plugin_inst_s base;
	/* Extend plugin-specific data here */
	char *path;
	char buff[1024];
	uint64_t val[N_METRICS];
};

/* ============== Sampler Plugin APIs ================= */

static
int lnet_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	int i, rc;
	/* Add lnet metrics to the schema */

	for (i = 0; i < N_METRICS; i++) {
		rc = ldms_schema_metric_add(schema,
				stat_name[i], LDMS_V_U64, "");
		if (rc < 0)
			return -rc; /* rc == -errno */
	}
	return 0;
}

static
int lnet_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	lnet_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	FILE *fp = NULL;
	char *s;
	int i, n, rc;

	/* Populate set metrics */
	fp = fopen(inst->path, "r");
	if (!fp)
		return ENOENT;
	s = fgets(inst->buff, sizeof(inst->buff) - 1, fp);
	if (!s) {
		rc = EIO;
		goto err;
	}

	n = sscanf(inst->buff,
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " "
		"%" SCNu64 " " ,
		&(inst->val[0]),
		&(inst->val[1]),
		&(inst->val[2]),
		&(inst->val[3]),
		&(inst->val[4]),
		&(inst->val[5]),
		&(inst->val[6]),
		&(inst->val[7]),
		&(inst->val[8]),
		&(inst->val[9]),
		&(inst->val[10]));

	if (n < N_METRICS) {
		rc = EINVAL;
		goto err;
	}

	for (i = 0; i < n; i++) {
		ldms_metric_set_u64(set, samp->first_idx + i, inst->val[i]);
	}

	rc = 0;
err:
	fclose(fp);
	return rc;
}


/* ============== Common Plugin APIs ================= */

static
const char *lnet_desc(ldmsd_plugin_inst_t pi)
{
	return "lnet - Lustre network statistrics sampler plugin";
}

static
char *_help = "\
lnet config synopsis\n\
    config name=INST [COMMON_OPTIONS] [file=<procfile>]\n\
\n\
Option descriptions:\n\
    file    The path to the procfile for lnet statistics. If not given,\n\
            it is " PROC_FILE_DEFAULT ".\n\
";

static
const char *lnet_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void lnet_del(ldmsd_plugin_inst_t pi)
{
	lnet_inst_t inst = (void*)pi;

	/* The undo of lnet_init and instance cleanup */
	if (inst->path)
		free(inst->path);
}

static
int lnet_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	lnet_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;
	const char *val;

        if (inst->path) {
		snprintf(ebuf, ebufsz, "%s: already configured.\n",
			 pi->inst_name);
		return EALREADY;
        }

	rc = samp->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	/* Plugin-specific config here */
	val = av_value(avl, "file");
	inst->path = strdup(val?val:PROC_FILE_DEFAULT);
	if (!inst->path) {
		snprintf(ebuf, ebufsz, "%s: out of memory\n",
			 pi->inst_name);
		return ENOMEM;
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
int lnet_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = lnet_update_schema;
	samp->update_set = lnet_update_set;

	return 0;
}

static
struct lnet_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "lnet_stats",

                /* Common Plugin APIs */
		.desc   = lnet_desc,
		.help   = lnet_help,
		.init   = lnet_init,
		.del    = lnet_del,
		.config = lnet_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	lnet_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
