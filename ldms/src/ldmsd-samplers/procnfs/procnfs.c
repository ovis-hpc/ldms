/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file procnfs.c
 * \brief /proc/net/rpc/nfs data provider
 */
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

const char *procfile = "/proc/net/rpc/nfs";

typedef struct metric_def_s {
	const char *name;
	const char *unit;
} *metric_def_t;

/* NOTE: /proc/net/rpc/nfs format:
 * net netcount netudpcount nettcpcount nettcpconn
 * rpc rpccount rpcretrans rpcauthrefresh
 * proc3 LEN NULL GETATTR SETATTR ...
 * pric4 LEN NULL READ WRITE COMMIT ...
 *
 * (from linuxsrc net/sunrpc/stats.c:rpc_proc_show())
 */

struct metric_def_s rpc_mdef[] = {
	/* ignore first field `rpccount` */
	{  "rpc.retrans",       NULL  },
	{  "rpc.authrefresh",   NULL  },
};
#define NUM_RPC (sizeof(rpc_mdef) / sizeof(rpc_mdef[0]))

/* from linux/include/uapi/linux/nfs3.h */
struct metric_def_s proc3_mdef[] = {
	/* ignore the unused `null` field */
	{  "nfs3.getattr",      NULL  },
	{  "nfs3.setattr",      NULL  },
	{  "nfs3.lookup",       NULL  },
	{  "nfs3.access",       NULL  },
	{  "nfs3.readlink",     NULL  },
	{  "nfs3.read",         NULL  },
	{  "nfs3.write",        NULL  },
	{  "nfs3.create",       NULL  },
	{  "nfs3.mkdir",        NULL  },
	{  "nfs3.symlink",      NULL  },
	{  "nfs3.mknod",        NULL  },
	{  "nfs3.remove",       NULL  },
	{  "nfs3.rmdir",        NULL  },
	{  "nfs3.rename",       NULL  },
	{  "nfs3.link",         NULL  },
	{  "nfs3.readdir",      NULL  },
	{  "nfs3.readdirplus",  NULL  },
	{  "nfs3.fsstat",       NULL  },
	{  "nfs3.fsinfo",       NULL  },
	{  "nfs3.pathconf",     NULL  },
	{  "nfs3.commit",       NULL  },
};
#define NUM_PROC3 (sizeof(proc3_mdef) / sizeof(proc3_mdef[0]))

/* from linux/include/linux/nfs4.h */
struct metric_def_s proc4_mdef[] = {
	/* ignore the unused `null` field */
	{  "nfs4.read",                  NULL  },
	{  "nfs4.write",                 NULL  },
	{  "nfs4.commit",                NULL  },
	{  "nfs4.open",                  NULL  },
	{  "nfs4.open_confirm",          NULL  },
	{  "nfs4.open_noattr",           NULL  },
	{  "nfs4.open_downgrade",        NULL  },
	{  "nfs4.close",                 NULL  },
	{  "nfs4.setattr",               NULL  },
	{  "nfs4.fsinfo",                NULL  },
	{  "nfs4.renew",                 NULL  },
	{  "nfs4.setclientid",           NULL  },
	{  "nfs4.setclientid_confirm",   NULL  },
	{  "nfs4.lock",                  NULL  },
	{  "nfs4.lockt",                 NULL  },
	{  "nfs4.locku",                 NULL  },
	{  "nfs4.access",                NULL  },
	{  "nfs4.getattr",               NULL  },
	{  "nfs4.lookup",                NULL  },
	{  "nfs4.lookup_root",           NULL  },
	{  "nfs4.remove",                NULL  },
	{  "nfs4.rename",                NULL  },
	{  "nfs4.link",                  NULL  },
	{  "nfs4.symlink",               NULL  },
	{  "nfs4.create",                NULL  },
	{  "nfs4.pathconf",              NULL  },
	{  "nfs4.statfs",                NULL  },
	{  "nfs4.readlink",              NULL  },
	{  "nfs4.readdir",               NULL  },
	{  "nfs4.server_caps",           NULL  },
	{  "nfs4.delegreturn",           NULL  },
	{  "nfs4.getacl",                NULL  },
	{  "nfs4.setacl",                NULL  },
	{  "nfs4.fs_locations",          NULL  },
	{  "nfs4.release_lockowner",     NULL  },
	{  "nfs4.secinfo",               NULL  },
	{  "nfs4.fsid_present",          NULL  },

	{  "nfs4.exchange_id",           NULL  },
	{  "nfs4.create_session",        NULL  },
	{  "nfs4.destroy_session",       NULL  },
	{  "nfs4.sequence",              NULL  },
	{  "nfs4.get_lease_time",        NULL  },
	{  "nfs4.reclaim_complete",      NULL  },
	{  "nfs4.layoutget",             NULL  },
	{  "nfs4.getdeviceinfo",         NULL  },
	{  "nfs4.layoutcommit",          NULL  },
	{  "nfs4.layoutreturn",          NULL  },
	{  "nfs4.secinfo_no_name",       NULL  },
	{  "nfs4.test_stateid",          NULL  },
	{  "nfs4.free_stateid",          NULL  },
	{  "nfs4.getdevicelist",         NULL  },
	{  "nfs4.bind_conn_to_session",  NULL  },
	{  "nfs4.destroy_clientid",      NULL  },

	{  "nfs4.seek",                  NULL  },
	{  "nfs4.allocate",              NULL  },
	{  "nfs4.deallocate",            NULL  },
	{  "nfs4.layoutstats",           NULL  },
	{  "nfs4.clone",                 NULL  },
	{  "nfs4.copy",                  NULL  },
};
#define NUM_PROC4 (sizeof(proc4_mdef) / sizeof(proc4_mdef[0]))

typedef struct procnfs_inst_s *procnfs_inst_t;
struct procnfs_inst_s {
	struct ldmsd_plugin_inst_s base;
	FILE *mf;
	int rpc_idx;
	int proc3_idx;
	int proc4_idx;
	char buff[4096];
};

/* ============== Sampler Plugin APIs ================= */

static
int __add_metrics(ldms_schema_t sch, metric_def_t defs, int N)
{
	/* NOTE: return last idx, or -errno */
	int rc = 0;
	int i;
	for (i = 0; i < N; i++) {
		rc = ldms_schema_metric_add(sch, defs[i].name, LDMS_V_U64,
					    defs[i].unit);
		if (rc < 0)
			return rc;
	}
	return rc; /* last index */
}

static
int procnfs_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	procnfs_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	int rc;
	inst->rpc_idx = samp->first_idx;
	rc = __add_metrics(schema, rpc_mdef, NUM_RPC);
	if (rc < 0)
		return -rc;
	inst->proc3_idx = rc + 1;
	rc = __add_metrics(schema, proc3_mdef, NUM_PROC3);
	if (rc < 0)
		return -rc;
	inst->proc4_idx = rc + 1;
	rc = __add_metrics(schema, proc4_mdef, NUM_PROC4);
	if (rc < 0)
		return -rc;
	return 0;
}

static
void __update_set(ldms_set_t set, int from_idx, int len, char *buff)
{
	int i;
	uint64_t val;
	char *ptr, *tok;
	/*
	 * NOTE: Newer NFSPROC4 may have more fields, older NFSPROC4 may have
	 *       fewer fields, but the order will be persistent. When the list
	 *       grows, it will be an appending, not inserting.
	 */
	i = 0;
	tok = strtok_r(buff, " ", &ptr);
	while (tok && i < len) {
		val = atol(tok);
		ldms_metric_set_u64(set, from_idx + i, val);
		tok = strtok_r(NULL, " ", &ptr);
		i++;
	}
}

static
int procnfs_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	int rc;
	procnfs_inst_t inst = (void*)pi;
	char *line;
	char *tok, *ptr;

	if (!inst->mf) {
		inst->mf = fopen(procfile, "r");
		if (!inst->mf) {
			ldmsd_log(LDMSD_LERROR, "%s: cannot open file: %s, "
				  "errno: %d\n", inst->base.inst_name,
				  procfile, errno);
			return errno;
		}
	}

	/* Populate set metrics */
	rc = fseek(inst->mf, 0, SEEK_SET);
	if (rc < 0) {
		ldmsd_log(LDMSD_LERROR, "%s: fseek failed, errno: %d\n",
			  inst->base.inst_name, errno);
		fclose(inst->mf);
		inst->mf = NULL;
		/* will re-open next sample */
		return errno;
	}
	while ((line = fgets(inst->buff, sizeof(inst->buff), inst->mf))) {
		tok = strtok_r(line, " ", &ptr);
		if (0 == strcmp(tok, "rpc")) {
			/* skip first field */
			tok = strtok_r(NULL, " ", &ptr);
			__update_set(set, inst->rpc_idx, NUM_RPC, ptr);
		}
		if (0 == strcmp(tok, "proc3")) {
			/* skip first 2 fields */
			tok = strtok_r(NULL, " ", &ptr);
			tok = strtok_r(NULL, " ", &ptr);
			__update_set(set, inst->proc3_idx, NUM_PROC3, ptr);
		}
		if (0 == strcmp(tok, "proc4")) {
			/* skip first 2 fields */
			tok = strtok_r(NULL, " ", &ptr);
			tok = strtok_r(NULL, " ", &ptr);
			__update_set(set, inst->proc4_idx, NUM_PROC4, ptr);
		}
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *procnfs_desc(ldmsd_plugin_inst_t pi)
{
	return "procnfs - /proc/net/rpc/nfs data sampler";
}

static
const char *procnfs_help(ldmsd_plugin_inst_t pi)
{
	return "procnfs takes no extra options";
}

static
void procnfs_del(ldmsd_plugin_inst_t pi)
{
	procnfs_inst_t inst = (void*)pi;
	if (inst->mf)
		fclose(inst->mf);
}

static
int procnfs_config(ldmsd_plugin_inst_t pi, struct attr_value_list *avl,
				      struct attr_value_list *kwl,
				      char *ebuf, int ebufsz)
{
	procnfs_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;

	if (inst->mf) {
		snprintf(ebuf, ebufsz, "%s: already configured.\n",
			 pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, avl, kwl, ebuf, ebufsz);
	if (rc)
		return rc;

	inst->mf = fopen(procfile, "r");
	if (!inst->mf) {
		snprintf(ebuf, ebufsz, "%s: cannot open file: %s, errno: %d\n",
			 pi->inst_name, procfile, errno);
		return errno;
	}
	setbuf(inst->mf, NULL);

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
int procnfs_init(ldmsd_plugin_inst_t pi)
{
	procnfs_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = procnfs_update_schema;
	samp->update_set = procnfs_update_set;

	inst->mf = NULL;

	return 0;
}

static
struct procnfs_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "procnfs",

                /* Common Plugin APIs */
		.desc   = procnfs_desc,
		.help   = procnfs_help,
		.init   = procnfs_init,
		.del    = procnfs_del,
		.config = procnfs_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	procnfs_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
