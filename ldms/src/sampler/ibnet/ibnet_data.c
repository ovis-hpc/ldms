/*
 * Copyright (c) 2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
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

/*
 * infiniband network port metrics sampler
 */

/* IF compiled with -DMAIN, the ldmsd-dependent code is omitted, leaving
 * a utility which generates the same schema name as the plugin will use
 * if given the same config arguments.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include "ibnet_data.h"
#include "ldmsd_plugattr.h"
#include "sampler_base.h" /* for auth control only */
#include <infiniband/mad.h>
#include <infiniband/umad.h>
#include <infiniband/iba/ib_types.h>
#include <complib/cl_nodenamemap.h>

#define MAX_SCHEMA_BASE 32
#define MAX_STR_NAME 128
#define NNSIZE IB_NODE_DESCRIPTION_SIZE /*< max lid name from node-name-map */
#define REMOTESZ 20 /**< space for lid.port string */
#define PQMAD_SIZE 1024 /**< perfquery.c mad query buffer size. should it be umad_size() + IB_MAD_SIZE? */

#define NOMASK 0
#define NOMASK2 0
#define GSI_CONT 2

/* the metadata for a contiguous group of enum MAD_FIELDS */
struct counter_range {
	unsigned enabled; /* include this range in set. 0 no, 1 yes, 2 continuation s.t. capmask */
	const char *subset; /* name of range */
	enum MAD_FIELDS lo; /* MAD field enum value */
	enum MAD_FIELDS hi; /* MAD field enum value */
	int id; /* query gsi id, or GSI_CONT if continuation of prior query. */
	uint16_t mask_check; /* cap_mask needed for valid set */
	uint32_t mask_check2; /* reserved */
	int lo_index; /* index of first metric in this range. */
};

/* Keep this array 64 elements or less. The true/false flags are combined
 * into a 64 bit hash code and may be appended as a hex string to the schema name.
 */
struct counter_range cr_default[] = {
	{ 0, "base", IB_PC_ERR_SYM_F, IB_PC_ERR_RCVCONSTR_F, IB_GSI_PORT_COUNTERS, NOMASK, NOMASK2, 0 },
	{ 2, "base2", IB_PC_ERR_LOCALINTEG_F, IB_PC_XMT_WAIT_F, GSI_CONT, NOMASK, NOMASK2, 0 },

	{ 1, "extended", IB_PC_EXT_XMT_BYTES_F, IB_PC_EXT_RCV_PKTS_F, IB_GSI_PORT_COUNTERS_EXT, NOMASK, NOMASK2, 0 },
	{ 2, "extended2", IB_PC_EXT_XMT_UPKTS_F, IB_PC_EXT_RCV_MPKTS_F, GSI_CONT, IB_PM_EXT_WIDTH_SUPPORTED, NOMASK2, 0 },
	{ 2, "extended3", IB_PC_EXT_ERR_SYM_F, IB_PC_EXT_QP1_DROP_F, GSI_CONT, NOMASK, NOMASK2, 0 },

	{ 1, "xmtsl", IB_PC_XMT_DATA_SL0_F, IB_PC_XMT_DATA_SL15_F, IB_GSI_PORT_XMIT_DATA_SL, NOMASK, NOMASK2, 0 },
	{ 1, "rcvsl", IB_PC_RCV_DATA_SL0_F, IB_PC_RCV_DATA_SL15_F, IB_GSI_PORT_RCV_DATA_SL, NOMASK, NOMASK2, 0 },
	{ 1, "xmtdisc", IB_PC_XMT_INACT_DISC_F, IB_PC_XMT_SW_HOL_DISC_F, IB_GSI_PORT_XMIT_DISCARD_DETAILS, NOMASK, NOMASK2, 0 },
	{ 1, "rcverr", IB_PC_RCV_LOCAL_PHY_ERR_F, IB_PC_RCV_LOOPING_ERR_F, IB_GSI_PORT_RCV_ERROR_DETAILS, NOMASK, NOMASK2, 0 },
	{ 0, "oprcvcounters", IB_PC_PORT_OP_RCV_PKTS_F, IB_PC_PORT_OP_RCV_DATA_F, IB_GSI_PORT_PORT_OP_RCV_COUNTERS, NOMASK, NOMASK2, 0 },
	{ 1, "flowctlcounters", IB_PC_PORT_XMIT_FLOW_PKTS_F, IB_PC_PORT_RCV_FLOW_PKTS_F, IB_GSI_PORT_PORT_FLOW_CTL_COUNTERS, NOMASK, NOMASK2, 0 },
	{ 0, "vloppackets", IB_PC_PORT_VL_OP_PACKETS0_F, IB_PC_PORT_VL_OP_PACKETS15_F, IB_GSI_PORT_PORT_VL_OP_PACKETS, NOMASK, NOMASK2, 0 },
	{ 0, "vlopdata", IB_PC_PORT_VL_OP_DATA0_F, IB_PC_PORT_VL_OP_DATA15_F, IB_GSI_PORT_PORT_VL_OP_DATA, NOMASK, NOMASK2, 0 },
	{ 1, "vlxmitflowctlerrors", IB_PC_PORT_VL_XMIT_FLOW_CTL_UPDATE_ERRORS0_F, IB_PC_PORT_VL_XMIT_FLOW_CTL_UPDATE_ERRORS15_F, IB_GSI_PORT_PORT_VL_XMIT_FLOW_CTL_UPDATE_ERRORS, NOMASK, NOMASK2, 0 },
	{ 1, "vlxmitcounters", IB_PC_PORT_VL_XMIT_WAIT0_F, IB_PC_PORT_VL_XMIT_WAIT15_F, IB_GSI_PORT_PORT_VL_XMIT_WAIT_COUNTERS, NOMASK, NOMASK2, 0 },
	{ 0, "swportvlcong", IB_PC_SW_PORT_VL_CONGESTION0_F, IB_PC_SW_PORT_VL_CONGESTION15_F, IB_GSI_SW_PORT_VL_CONGESTION, NOMASK, NOMASK2, 0 },
	{ 1, "rcvcc", IB_PC_RCV_CON_CTRL_PKT_RCV_FECN_F, IB_PC_RCV_CON_CTRL_PKT_RCV_BECN_F, IB_GSI_PORT_RCV_CON_CTRL, NOMASK, NOMASK2, 0 },
	{ 0, "slrcvfecn", IB_PC_SL_RCV_FECN0_F, IB_PC_SL_RCV_FECN15_F, IB_GSI_PORT_SL_RCV_FECN, NOMASK, NOMASK2, 0 },
	{ 0, "slrcvbecn", IB_PC_SL_RCV_BECN0_F, IB_PC_SL_RCV_BECN15_F, IB_GSI_PORT_SL_RCV_BECN, NOMASK, NOMASK2, 0 },
	{ 1, "xmitcc", IB_PC_XMIT_CON_CTRL_TIME_CONG_F, IB_PC_XMIT_CON_CTRL_TIME_CONG_F, IB_GSI_PORT_XMIT_CON_CTRL, NOMASK, NOMASK2, 0 },
	{ 1, "vlxmittimecc", IB_PC_VL_XMIT_TIME_CONG0_F, IB_PC_VL_XMIT_TIME_CONG14_F, IB_GSI_PORT_VL_XMIT_TIME_CONG, NOMASK, NOMASK2, 0 },
	{ 1, "smplctl", IB_PSC_OPCODE_F, IB_PSC_SAMPLES_ONLY_OPT_MASK_F, IB_GSI_PORT_SAMPLES_CONTROL, NOMASK, NOMASK2, 0 }
	// not supporting extended_speeds_query
};

static const size_t cr_count = sizeof(cr_default) / sizeof(cr_default[0] ) ;
#define CR_COUNT sizeof(cr_default) / sizeof(cr_default[0] )

/**
 * Infiniband port representation and context.
 */
struct ib_port {
	ldms_set_t set;
	int num; /**< port number (remote) */
	uint16_t lid; /**< lid no (remote) */
	uint16_t meta_init;
	char remote[REMOTESZ]; /**< lid.port as a string. Not floating
		point compatible as the maximum format is LLLLL.PPPPPPPPPP
		and many values even small P are not represented in fp bit
		patterns. */
	ib_portid_t port_id; /**< IB port id */
	char lidname[IB_NODE_DESCRIPTION_SIZE]; /**< string name of lid
						from node-name-map file*/
	uint64_t remote_guid; /* guid of lid */
	uint64_t port_guid; /* guid of port */
	LIST_ENTRY(ib_port) entry; /**< List entry */

	/**
	 * Metric handle for the meta lidport;
	 */
	double qtime; /* mad call time in seconds */
	int32_t stime; /* mad call end - group query start, microseconds .*/
	__be16 cap_mask;
	int qerr[CR_COUNT]; /* err found during port_query */
	int err_logged[CR_COUNT];
	uint8_t *rcv_buf[CR_COUNT];
};

struct ibnet_data {
	ldmsd_msg_log_f log;

	/** schema of counter data */
	ldms_schema_t port_schema;

	/** schema of collective timing data */
	ldms_schema_t timing_schema;

	/** gettimeofday can affect performance, so optional.
 	 * flag to control per-port and collective timing:
	 *    0 none,
	 *    1 collective,
	 *    2 perport.
	 *    default 0.
	 */
	unsigned port_timing;
	bool debug;
	/* name of file node-name-map; sometimes
 * 		/etc/opensm/ib-node-name-map or some such */
	char *lidnames;
	nn_map_t *nnmap;
	/* name of file with lid/port locations to sample. */
	char *srclist;
	uint64_t comp_id;
	uid_t uid;
	gid_t gid;
	int perm;
	/* port schema indices */
	int index_remote;
	int index_lid;
	int index_port;
	int index_node_name;
	int index_port_query_time;
	int index_port_query_offset;
	/* timing schema indices */
	int index_compid;
	int index_ib_query_time;
	int index_ib_data_process_time;

	/** optional timing data of all queries, all decodes */
	ldms_set_t timing_set;

	struct counter_range cr_opt[ (sizeof(cr_default) /
					sizeof(cr_default[0] )) ];

	/** list of ports expected from configuration file list */
	LIST_HEAD(ib_port_list, ib_port) ports;

	/* number of local port used to access the fabric. */
	int32_t ca_port; /* default 1 */

	/* local port for contactiung network */
	struct ibmad_port *port;
	

	/** name of local NIC used to access the fabric. */
	char ca_name[UMAD_CA_NAME_LEN];

	/** schema name base, default ibnet */
	char schema_name_base[MAX_SCHEMA_BASE];

};

static const char *ibnet_opts[] = {
	"metric-conf",
	"timing",
	"port-name",
	"port-number",
	"source-list",
	"node-name-map",
	"debug",
	"schema",
	"instance",
	"producer",
	"component_id",
	"uid",
	"gid",
	"perm",
	NULL
};

static const char *ibnet_words[] = {
	"debug",
	NULL
};

static int ibnet_data_cr_opt_parse(struct ibnet_data *d, const char *conf);
static const char *compute_schema_suffix(struct ibnet_data *d);

#ifndef MAIN
static int ibnet_data_schemas_init(struct ibnet_data *d,
	const char *port_schema_name, const char *timing_schema_name);

static int ibnet_data_sets_init(struct ibnet_data *d,
	const char *port_schema_name, const char *timing_producer,
	const char *timing_instance_name);

static int parse_lid_file(struct ibnet_data *d, const char *lidfile);

static int add_lid(struct ibnet_data *d, uint16_t lid_no, uint64_t guid,
	char *devname, int nports, int *ports);
static void ibnet_data_port_destroy(struct ib_port *port);
static void port_query(struct ibnet_data *d, struct ib_port *port, struct timeval *t);
static void port_decode(struct ibnet_data *d, struct ib_port *port);
#else
char *psname[2] = { NULL };
#endif

static void dump_cr(struct counter_range *cr, struct ibnet_data *d)
{
	size_t i;
	for (i = 0; i < cr_count; i++)
		d->log(LDMSD_LDEBUG, SAMP 
			" row %zu: enabled %d, lo %d, hi %d, subset '%s',"
			" id %d, check %u, check2 %u, lo_index %d\n", i,
			(int)cr[i].enabled, (int)cr[i].lo, (int)cr[i].hi,
			cr[i].subset, cr[i].id, (unsigned)cr[i].mask_check,
			cr[i].mask_check2, cr[i].lo_index);
}

char *ibnet_data_usage() {
	char *r;

	const char *base = "config name=" SAMP " port-name=<hca> source-list=<lidfile> [port-number=<num>]\n"
	"       [metric-conf=<metricfile>] [node-name-map=<nnmap>] [timing=<timeopt>]\n"
	"       [producer=<name>] [instance=<name>] [component_id=<uint64_t>] [schema=<name_base>]\n"
	"       [debug]\n"
	"       [uid=<user-id>] [gid=<group-id>] [perm=<mode_t permission bits>]\n"
	"    lidfile      full path of list of lids/ports (required).\n"
	"    metricfile   full path of list of metric groups or metrics (optional).\n"
	"    hca	  local interface name (required).\n"
	"    num	  port number on local hca. (default 1)\n"
	"    nnmap	full path of file with guid:description pairs as used (optional)\n"
	"		with --node-name-map in ibnetdiscover.\n"
	"    timeopt      0:no timing data, 1:bulk only, 2:port and bulk, 3: offset timing in ports also.\n"
	"    producer     A unique name for the host providing the timing data (default $HOSTNAME)\n"
	"    instance     A unique name for the timing metric set (default $HOSTNAME/" SAMP ")\n"
	"    component_id A unique number for the component being monitoring, Defaults to zero.\n"
	"    schema       The base name of the port metrics schema, Defaults to " SAMP ".\n"
	"    uid	  The user-id of the set's owner (defaults to geteuid())\n"
	"    gid	  The group id of the set's owner (defaults to getegid())\n"
	"    perm	 The set's access permissions (defaults to 0777)\n"
	"    Currently supported values in metric file are:\n"
	;

	dstring_t ds;
	dstr_init2(&ds, 1024);
	dstr_set(&ds, base);
	size_t i;
	for (i = 0; i < cr_count; i++) {
		if (cr_default[i].id == GSI_CONT) 
			continue;
		dstrcat(&ds, "\t", 1);
		dstrcat(&ds, cr_default[i].subset, DSTRING_ALL);
		if (cr_default[i].enabled == 0)
			dstrcat(&ds, "\t\t(default disabled)", DSTRING_ALL);
		else
			dstrcat(&ds, "\t\t(default enabled)", DSTRING_ALL);
		dstrcat(&ds, "\n", 1);
	}

	const char *epilog =
	"    which are groups that can be found in the\n"
	"    output of the infiniband-diags perfquery utility.\n"
	;

	dstrcat(&ds, epilog, DSTRING_ALL);
	r = dstr_extract(&ds);
	return r;
}

/** create instance using config options */
struct ibnet_data *ibnet_data_new(ldmsd_msg_log_f log, struct attr_value_list *avl, struct attr_value_list *kwl)
{
	if (!log || !avl) {
		errno = EINVAL;
		return NULL;
	}
	int rc;
	rc = ldmsd_plugattr_config_check(ibnet_opts, ibnet_words, avl, kwl,
		NULL, SAMP);
	if (rc)
		return NULL;
	struct ibnet_data *d = calloc(1, sizeof(*d));
	if (!d)
		return NULL;
	d->log = log;
	d->ca_port = 1;
	memcpy(d->cr_opt, (void*)cr_default, sizeof(cr_default));

	/* optional debug */
	if (av_idx_of(kwl, "debug") != -1)
		d->debug = 1;
	d->log(LDMSD_LDEBUG, SAMP " debug=%d\n", d->debug);

#define LENCHECK(v, n) \
	const char *v = av_value(avl, n); \
	if (v && strlen(v) == 0) { \
		errno = EINVAL; \
		d->log(LDMSD_LERROR, SAMP " needs " n "=<something>\n" ); \
		goto err; \
	}
	/* required */
	LENCHECK(cname, "port-name");
	LENCHECK(srclist, "source-list");

	/* optional */
	LENCHECK(conf, "metric-conf");
	LENCHECK(lidnames, "node-name-map");
	LENCHECK(cid, LDMSD_COMPID);
	LENCHECK(cnum, "port-number");
	LENCHECK(sbase, "schema");
	LENCHECK(ctiming, "timing");

	/* optional for timing */
	LENCHECK(itiming, "instance");
	LENCHECK(ptiming, "producer");

#ifndef MAIN

	d->perm = 0777;
	(void)base_auth_parse(avl, &(d->uid), &(d->gid), &(d->perm), d->log);
	if (!srclist || strlen(srclist) == 0) {
		errno = EINVAL;
		d->log(LDMSD_LERROR, SAMP " needs source_list=<file> of lid/port to read.\n");
		goto err;
	}
	d->srclist = strdup(srclist);
	if (!d->srclist) {
		d->log(LDMSD_LERROR, SAMP " config out of memory.\n");
		goto err;
	}
	if (!cname || strlen(cname) >= UMAD_CA_NAME_LEN) {
		errno = EINVAL;
		d->log(LDMSD_LERROR, SAMP " needs port_name of hca/hfi\n");
		if (cname)
			d->log(LDMSD_LDEBUG, SAMP " got too long %s\n", cname);
		goto err;
	} else {
		strcpy(d->ca_name, cname);
	}
#endif
	if (lidnames) {
		d->lidnames = strdup(lidnames);
		if (!d->lidnames) {
			d->log(LDMSD_LERROR, SAMP " config out of memory.\n");
			goto err;
		}
	}

	if (cnum) {
		char *endp;
		endp = NULL;
		errno = 0;
		unsigned long int j = strtoul(cnum, &endp, 0);
		if (endp == cnum || errno) {
			d->log(LDMSD_LERROR, SAMP "Got bad port_number '%s'\n",
				cnum);
			rc = EINVAL;
			goto err;
		}
		if (j > INT_MAX) {
			d->log(LDMSD_LERROR, SAMP "port_number too big %s\n",
				cnum);
			rc = EINVAL;
			goto err;
		}
		d->ca_port = (int)j;
	}

	if (conf) {
		rc = ibnet_data_cr_opt_parse(d, conf);
		if (d->debug)
			dump_cr(d->cr_opt, d);
		if (rc)
			goto err;
	}
		
	if (!sbase) {
		strcpy(d->schema_name_base, "ibnet");
	} else {
		if (strlen(sbase) >= MAX_SCHEMA_BASE) {
			d->log(LDMSD_LERROR, SAMP " schema name > %d: %s\n",
				MAX_SCHEMA_BASE, sbase);
			rc = EINVAL;
			goto err;
		}
		strcpy(d->schema_name_base, sbase);
	}

	char timing_instance_name[MAX_STR_NAME] = { '\0' };
	char timing_schema_name[MAX_STR_NAME] = { '\0' };
	char port_schema_name[MAX_STR_NAME] = { '\0' };
	char timing_producer[MAX_STR_NAME] = { '\0' };
	if (ctiming) {
		char *endp;
		endp = NULL;
		errno = 0;
		unsigned int j = strtoul(ctiming, &endp, 3);
		if (endp == ctiming || errno || j > 2 ) {
			d->log(LDMSD_LERROR, SAMP "Got bad timing '%s'\n",
				ctiming);
			rc = EINVAL;
			goto err;
		}
		d->port_timing = j;
		if (itiming) {
			if (strlen(itiming) >= MAX_STR_NAME) {
				d->log(LDMSD_LERROR, SAMP "'%s'"
					" too long for instance.\n",
					itiming);
				rc = EINVAL;
				goto err;
			} else {
				strcpy(timing_instance_name, itiming);
			}
		}
		if (!ptiming) {
			rc = gethostname(timing_producer, MAX_STR_NAME);
			if (rc) {
				rc = errno;
				goto err;
			}
		} else {
			strcpy(timing_producer, ptiming);
		}
		if (!itiming) {
			snprintf(timing_instance_name, MAX_STR_NAME,
				"%s/%s_timing", timing_producer,
				d->schema_name_base);
		}
		const char *schema_suffix = compute_schema_suffix(d);
		snprintf(port_schema_name, MAX_STR_NAME, "%s%s",
				d->schema_name_base, schema_suffix);
		snprintf(timing_schema_name, MAX_STR_NAME, "%s_timing",
			port_schema_name);
#ifndef MAIN
		if (cid) {
			char *endp;
			endp = NULL;
			errno = 0;
			unsigned long int j = strtoul(cid, &endp, 0);
			if (endp == cid || errno) {
				d->log(LDMSD_LERROR, SAMP " Got bad %s '%s'\n",
					LDMSD_COMPID, cid);
				rc = EINVAL;
				goto err;
			}
			d->comp_id = j;
		}
#endif
	}

#ifdef MAIN
	psname[0] = strdup(port_schema_name);
	if (d->port_timing)
		psname[1] = strdup(timing_schema_name);
#else
	// create schemas
	d->log(LDMSD_LDEBUG, SAMP " port schema %s\n", port_schema_name);
	if (d->port_timing)
		d->log(LDMSD_LDEBUG, SAMP " timing schema %s\n", timing_schema_name);
	rc = ibnet_data_schemas_init(d, port_schema_name,
		timing_schema_name);
	if (rc)
		goto err;
	
	int mgmt_classes[3] = {
		IB_SMI_CLASS, IB_SA_CLASS, IB_PERFORMANCE_CLASS
	};
	d->port = mad_rpc_open_port(d->ca_name, d->ca_port, mgmt_classes, 3);
	if (!d->port) {
		d->log(LDMSD_LERROR, SAMP "failed to open port %s:%d\n",
			d->ca_name, d->ca_port);
		goto err;
	}

	if (d->lidnames) {
		errno = 0;
		d->nnmap = open_node_name_map(d->lidnames);
		if (!d->nnmap ) {
			d->log(LDMSD_LERROR, SAMP ": failed to read %s\n",
				d->lidnames);
			if (errno)
				rc = errno;
			else
				rc = EINVAL;
			goto err;
		}
	}

	rc = parse_lid_file(d, srclist);
	if (rc) {
		d->log(LDMSD_LERROR, SAMP " failed parse_lid_file %s\n", srclist);
		goto err;
	}

	// create instances
	rc = ibnet_data_sets_init(d, port_schema_name, timing_producer,
		timing_instance_name);
	if (rc) {
		d->log(LDMSD_LERROR, SAMP " failed ibnet_data_sets_init %s, %s, %s\n",
			port_schema_name, timing_producer, timing_instance_name);
		goto err;
	}
#endif
	return d;
err:
	errno = rc;
	ibnet_data_delete(d);
	return NULL;
}

/** destroy inst. */
void ibnet_data_delete(struct ibnet_data *d)
{
	if (!d)
		return;
#ifndef MAIN
	// timing_set
	if (d->timing_set) {
		ldms_set_unpublish(d->timing_set);
		ldms_set_delete(d->timing_set);
		d->timing_set = NULL;
	}

	// ports
	struct ib_port *port;
	while ((port = LIST_FIRST(&(d->ports)))) {
		LIST_REMOVE(port, entry);
		ibnet_data_port_destroy(port);
	}
	if (d->port_schema) {
		ldms_schema_delete(d->port_schema);
		d->port_schema = NULL;
	}
	if (d->timing_schema) {
		ldms_schema_delete(d->timing_schema);
		d->timing_schema = NULL;
	}
	free(d->srclist);
	if (d->nnmap)
		close_node_name_map(d->nnmap);
#endif
	free(d->lidnames);
	free(d);
}

#ifndef MAIN
/** update sets in inst. */
void ibnet_data_sample(struct ibnet_data *d)
{
	union ldms_value v;
	double dtquery = 0;
	double dtprocess = 0;
	struct timeval tv[3];
	struct timeval tv_diff;

	if (d->port_timing) {
		gettimeofday(&tv[0], 0);
		ldms_transaction_begin(d->timing_set);
	}
	struct ib_port *port;
	LIST_FOREACH(port, &(d->ports), entry) {
		port_query(d, port, &tv[0]);
	}
	if (d->port_timing) {
		gettimeofday(&tv[1], 0);
	}
	LIST_FOREACH(port, &(d->ports), entry) {
		port_decode(d, port);
	}
	if (d->port_timing) {
		gettimeofday(&tv[2], 0);

		timersub(&tv[1], &tv[0], &tv_diff);
		dtquery = tv_diff.tv_sec + tv_diff.tv_usec *1e-6;

		timersub(&tv[2], &tv[1], &tv_diff);
		dtprocess = tv_diff.tv_sec + tv_diff.tv_usec *1e-6;

		v.v_d = dtquery;
		ldms_metric_set(d->timing_set, d->index_ib_query_time, &v);

		v.v_d = dtprocess;
		ldms_metric_set(d->timing_set, d->index_ib_data_process_time, &v);
		ldms_transaction_end(d->timing_set);
	}

}
#endif

/** Read a file of MAD field groups (1 per line). A group may be disabled by deleting it or starting its
 * name with #, e.g. #port-op-rcv-pkts-group instead of port-op-rcv-pkts-group.
 * If multiple names are on the the same line, the # disables only the name it is part of.
 * '# name' does not disable name; only #name does.
 */
static int ibnet_data_cr_opt_parse(struct ibnet_data *d, const char *conf)
{
	if (!d || !conf)
		return EINVAL;
	FILE *f = fopen(conf,"r");
	size_t sz = 128;
	char s[sz];
	if (!f) {
		char *serr;
		serr = strerror_r(errno, s, sz);
		d->log(LDMSD_LERROR, SAMP " failed to open %s. %s\n", conf, serr);
		return errno;
	}
	int rc = fseek(f, 0, SEEK_END);
	if (rc) {
		rc = errno;
		goto err;
	}
	long flen = ftell(f);
	if (flen < 0) {
		rc = errno;
		goto err;
	}
	rewind(f);
	{
		char buf[flen+1];
		size_t bytes = fread(buf, 1, flen+1, f);
		buf[flen] = '\0';
		if (bytes < flen)
			d->log(LDMSD_LWARNING, SAMP " unexpected short read of %s\n",
				conf);

		int size = 1;
		char *t = buf;
		while (t[0] != '\0') {
			if (isspace(t[0])) size++;
			t++;
		}
		struct attr_value_list *avl = av_new(size);
		struct attr_value_list *kwl = av_new(size);
		rc = tokenize(buf, kwl, avl);
		if (rc) {
			d->log(LDMSD_LERROR, SAMP " failed to parse %s.\n", conf);
			goto err;
		}
		if (av_idx_of(kwl, "#") != -1) {
			d->log(LDMSD_LWARNING, SAMP " '# ' in conf file %s disables "
				"nothing. use #name only.\n", conf);
		}
		size_t k;
		for (k = 0; k < cr_count; k++) {
			if (d->cr_opt[k].enabled < 2) {
				d->cr_opt[k].enabled =
					(av_idx_of(kwl, d->cr_opt[k].subset) != -1);
				if (d->debug)
					d->log(LDMSD_LDEBUG, SAMP " %s %sin %s\n",
						d->cr_opt[k].subset,
						(d->cr_opt[k].enabled ? "" : "not "),
						conf);
			}
		}
		av_free(kwl);
		av_free(avl);
	}

	rc = 0;
err:
	fclose(f);
	if (rc != 0) {
		d->log(LDMSD_LERROR, SAMP " failed to open %s. %s\n", conf, s);
	}
	return rc;
}

#ifndef MAIN
/** Define schemas as needed.
 * Return < 0 if problem, in which case caller should abandon instance.
 * Return 0 if ok.
 *
 * Schema for timing data is:
 * compid
 * ib_query_time
 * ib_data_process_time
 *
 * Schema for port data is
 * remote	s[20]
 * lid		s32
 * port		s32
 * [node_name	c[64] ] if lidnames
 * [port_query_time	double] if port_timing
 * ibfieldlist
 */
static int ibnet_data_schemas_init(struct ibnet_data *d, const char *port_schema,
const char * timing_schema)
{
	int rc;
	int i;

	if (!d || !port_schema)
		return EINVAL;
	if (d->log)
		d->log(LDMSD_LDEBUG, SAMP " schemas_init(i, %s, %s)\n",
			port_schema, timing_schema );

	/* set up the port schema. */
	d->port_schema = ldms_schema_new(port_schema);
	if (d->port_schema == NULL)
		goto err1;

	if (d->debug)
		dump_cr(d->cr_opt, d);
/* metric add log */
#define MADD(x) if (d->debug) \
			d->log(LDMSD_LDEBUG, SAMP ": add metric %s\n",x)

	// add cluster meta?
	MADD("remote");
	rc = ldms_schema_meta_array_add(d->port_schema, "remote",
		LDMS_V_CHAR_ARRAY, REMOTESZ);
	if (rc < 0)
		goto err2;
	d->index_remote = rc;

	MADD("lid");
	rc = ldms_schema_meta_add(d->port_schema, "lid", LDMS_V_S32);
	if (rc < 0)
		goto err2;
	d->index_lid = rc;

	MADD("port");
	rc = ldms_schema_meta_add(d->port_schema, "port", LDMS_V_S32);
	if (rc < 0)
		goto err2;
	d->index_port = rc;
	
	MADD("node_name");
	if (d->lidnames) {
		rc = ldms_schema_meta_array_add(d->port_schema, "node_name",
			LDMS_V_CHAR_ARRAY, NNSIZE);
		if (rc < 0)
			goto err2;
		d->index_node_name = rc;
	}
	if (d->port_timing >= 2) {
		MADD("port_query_time");
		rc = ldms_schema_metric_add(d->port_schema, "port_query_time", LDMS_V_D64);
		if (rc < 0)
			goto err2;
		d->index_port_query_time = rc;
	}
	if (d->port_timing == 3) {
		MADD("port_query_offset");
		rc = ldms_schema_metric_add(d->port_schema, "port_query_offset", LDMS_V_D64);
		if (rc < 0)
			goto err2;
		d->index_port_query_offset = rc;
	}
	int prev = 0;
	for (i = 0; i < cr_count; i++) {
		if (!d->cr_opt[i].enabled) {
			prev = 0;
			continue;
		}
		if (d->cr_opt[i].enabled == 1|| prev) {
			if (d->debug) 
				d->log(LDMSD_LDEBUG, SAMP ": add subset %s\n", d->cr_opt[i].subset);
			prev = 1;
			enum MAD_FIELDS j = d->cr_opt[i].lo;
			for ( ; j <= d->cr_opt[i].hi; j++) {
				const char *m = mad_field_name(j);
				MADD(m);
				rc = ldms_schema_metric_add(d->port_schema, m,
					LDMS_V_U64);
				if (rc < 0)
					goto err2;
				if (j == d->cr_opt[i].lo)
					d->cr_opt[i].lo_index = rc;
			}
		}
	}

	/* set up the overall timing schema. */
	if (d->port_timing) {
		d->timing_schema = ldms_schema_new(timing_schema);
		if (!d->timing_schema) {
			goto err1;
		}

		rc = ldms_schema_meta_add(d->timing_schema, LDMSD_COMPID, LDMS_V_U64);
		if (rc < 0)
			goto err2;
		d->index_compid = rc;

		MADD("ib_query_time");
		rc = ldms_schema_metric_add(d->timing_schema, "ib_query_time", LDMS_V_D64);
		if (rc < 0)
			goto err2;
		d->index_ib_query_time = rc;

		MADD("ib_data_process_time");
		rc = ldms_schema_metric_add(d->timing_schema, "ib_data_process_time", LDMS_V_D64);
		if (rc < 0)
			goto err2;
		d->index_ib_data_process_time = rc;
	}
	return 0;
err1:
	d->log(LDMSD_LERROR, SAMP " schema creation failed\n");
	return -1;
err2:
	d->log(LDMSD_LERROR, SAMP " metric add failed\n");
	return -1;
}

int ibnet_data_sets_init(struct ibnet_data *d, const char *port_schema_name, const char *timing_producer, const char *timing_instance_name)
{
	int rc;
	if (d->timing_schema) {
		d->timing_set = ldms_set_new(timing_instance_name,
					d->timing_schema);
		if (!d->timing_set) {
			rc = errno;
			d->log(LDMSD_LERROR, SAMP
				" cannot create timing_set\n");
			return errno;
		}
		ldms_set_producer_name_set(d->timing_set, timing_producer);
		ldms_set_config_auth(d->timing_set, d->uid, d->gid, d->perm);
		ldms_metric_set_u64(d->timing_set, d->index_compid,
					d->comp_id);
		ldms_set_publish(d->timing_set);
	}

	char port_instance_name[MAX_STR_NAME];
	struct ib_port *port;
	LIST_FOREACH(port, &(d->ports), entry) {
		snprintf(port_instance_name, MAX_STR_NAME, "%s/%d/%s",
				port->lidname, port->num, port_schema_name);
		port->set = ldms_set_new(port_instance_name, d->port_schema);
		if (!port->set) {
			rc = errno;
			d->log(LDMSD_LERROR, SAMP ": ldms_set_new %s failed, "
				"errno: %d, %s\n", port_instance_name, rc,
				 ovis_errno_abbvr(errno));
			return rc;
		}
		if (d->debug)
			d->log(LDMSD_LDEBUG, SAMP ": ldms_set_new %s\n",
				port_instance_name);
		ldms_set_producer_name_set(port->set, port->lidname);
		ldms_set_config_auth(port->set, d->uid, d->gid, d->perm);
		ldms_set_publish(port->set);
	}
	return 0;
}

/*
 * Build port list from text.
 *
 * \return errno value.
 *
 * parse input is:
 * lid, hexguid, nports, plist
 * where hexguid is 0x....,
 * nports is int,
 * plist is ints nports long or * if range is 1-nports,
 * if not using name map, names will be GUID_hex.
 */
static int parse_lid_file(struct ibnet_data *d, const char *lidfile)
{

	if (!d || !lidfile) {
		d->log(LDMSD_LERROR, SAMP ": parse_lid_file bad args\n");
		return EINVAL;
	}
	FILE *f = fopen(lidfile, "r");
	if (!f) {
		d->log(LDMSD_LERROR, SAMP ": cannot read lid file %s\n", lidfile);
		return errno;
	}

	int rc = 0;
	size_t bsize = 2048;
	char *buf = malloc(bsize);
	if (!buf) {
		d->log(LDMSD_LERROR, SAMP ": out of memory\n");
		rc = ENOMEM;
		goto out;
	}
	ssize_t nchar;
	int ln = 0;
	int sr;
	int lid, nports, offset = 0;

	while ( ( nchar = getline(&buf, &bsize, f) ) != -1) {
		ln++;
		lid = nports = offset = 0;
		/* skip comments */
		sr = sscanf(buf, " #%n", &offset);
		if (sr >= 0 && offset && buf[offset-1] == '#') {
			if (d->debug)
				d->log(LDMSD_LDEBUG, SAMP ": %d comment at %d of %s",
					sr, offset-1, buf);
			continue;
		}
		/* get lid, guid, nports */
		uint64_t guid = 0;
		sr = sscanf(buf, " %d %" SCNx64 " %d %n",
			&lid, &guid, &nports, &offset);
		if (d->debug)
			d->log(LDMSD_LDEBUG, SAMP ": %d %d :line %d: %d %"PRIx64 " %d of %s",
				sr, offset-1, ln, lid, guid, nports, buf);
		if (sr < 3) {
			d->log(LDMSD_LERROR, SAMP ": bad line %d: %s. Expected lid,guid(0xhex),nports,\n",
				ln, buf);
			rc = EINVAL;
			goto out;
		}
		if (nports < 1)
			continue;
		char plbuf[bsize];
		char devname[NNSIZE];
		strcpy(plbuf, buf+offset);
		char *sptr = NULL;
		char *hptr = plbuf;
		char *token = strtok_r(hptr, ",\t\n", &sptr);
		hptr = NULL;
		int pcount = 0;
		int port = 0;
		int portnum[nports];
		sprintf(devname, "GUID_%" PRIx64, guid);
		char *desc = remap_node_name(d->nnmap, guid, devname);
		strcpy(devname, desc);
		free(desc);
		/* get port list */
		while (token) {
			if (strchr(token,'*') != NULL) {
				if (pcount != 0) {
					d->log(LDMSD_LERROR, SAMP ": line %d: cannot use * and numbers for ports.\n", ln);
					rc = EINVAL;
					goto out;
				}

				int i;
				for (i = 0; i < nports; i++) {
					portnum[i] = i + 1;
					pcount++;
				}
				if (d->debug)
					d->log(LDMSD_LDEBUG, SAMP ": ports=*\n");
				break;
			}
			port = -1;
			sr = sscanf(token," %d ", &port);
			if ( sr == 0 || port < 1) {
				d->log(LDMSD_LERROR, SAMP ": line %d: port %s bad\n", ln, token);
				rc = EINVAL;
				goto out;
			}
			if (pcount >= nports) {
				d->log(LDMSD_LERROR, SAMP ": line %d: too many port numbers\n", ln);
				rc = EINVAL;
				goto out;
			}
			portnum[pcount] = port;
			pcount++;
			token = strtok_r(hptr, ",\t\n", &sptr);
		}
		if (pcount < nports) {
			d->log(LDMSD_LERROR, SAMP ": line %d: not enough port numbers\n", ln);
			rc = EINVAL;
			goto out;
		}

		rc = add_lid(d, lid, guid, devname, nports, portnum);
		if (rc) {
			d->log(LDMSD_LINFO, SAMP
				": add_lid failed for line %d\n",ln);
			goto out;
		}
	}

out:
	free(buf);
	fclose(f);
	return rc;
}

/**
 * Set up ib_port structures for a lid.
 *
 * \return 0 if success.
 * \return errno value if a problem.
 */
int add_lid(struct ibnet_data *d, uint16_t lid_no, uint64_t guid, char *devname, int nports, int *ports)
{
	int rc, port_no;
	struct ib_port *port;

	int i = 0;
	for ( ; i < nports; i++) {
		port_no = ports[i];
		port = calloc(1, sizeof(*port));
		if (!port) {
			rc = ENOMEM;
			goto err;
		}
		size_t g;
		for (g = 0 ; g < cr_count; g++) {
			if (d->cr_opt[g].enabled == 1) {
				port->rcv_buf[g] = calloc(1, PQMAD_SIZE);
				if (!port->rcv_buf[g]) {
					ibnet_data_port_destroy(port);
					rc = ENOMEM;
					goto err;
				}
			}
		}
		LIST_INSERT_HEAD(&(d->ports), port, entry);

		port->port_id.drpath.cnt = 0;
		memset(port->port_id.drpath.p, 0, IB_SUBNET_PATH_HOPS_MAX);
		port->port_id.drpath.drslid = 0;
		port->port_id.drpath.drdlid = 0;
		port->port_id.grh_present = 0;
		memset(port->port_id.gid, 0, sizeof(ibmad_gid_t));
		port->port_id.qp = 1;
		port->port_id.qkey = 0;
		port->port_id.sl = 0;
		port->port_id.pkey_idx = 0;
		port->port_id.lid = lid_no;

		port->num = port_no;
		port->lid = lid_no;
		port->remote_guid = guid;
		strcpy(port->lidname, devname);
		sprintf(port->remote, "%" PRIu16 ".%d", lid_no, port_no);
		int c;
		for (c = 0; port->remote[c] != '\0'; c++) {
			if (isspace(port->remote[c]) ) {
				port->remote[c]  = '_';
				continue;
			}
			if (port->remote[c] == ';' || port->remote[c] == ',') {
				port->remote[c]  = '_';
				continue;
			}
		}
	}

	return 0;
err:
	return rc;
}

static void ibnet_data_port_destroy(struct ib_port *port)
{
	if (!port)
		return;
	if (port->set) {
		ldms_set_unpublish(port->set);
		ldms_set_delete(port->set);
	}
	port->num = -1;
	port->lid = 0;
	port->remote[0] = '\0';
	port->lidname[0] = '\0';
	port->remote_guid = UINT64_MAX;
	port->qtime = -1;
	port->stime = -1;
	int g;
	for (g = 0 ; g < cr_count; g++) {
		free(port->rcv_buf[g]);
		port->rcv_buf[g] = NULL;
	}
	free(port);
}

static void port_query(struct ibnet_data *d, struct ib_port *port, struct timeval *group_start)
{
	if (!d || !port || !group_start)
		return;
	if (!port)
		return;

	struct timeval qtv_diff, qtv_now, qtv_prev;
	if (d->port_timing == 2) {
		port->qtime = 0;
		port->stime = 0;
		gettimeofday(&qtv_prev, 0);
	}
	if (!port->cap_mask) {
		uint8_t qr[PQMAD_SIZE];
		memset(qr, 0, sizeof(qr));
		if (!pma_query_via(qr, &port->port_id, port->num, 0/*timeout*/,
			CLASS_PORT_INFO, d->port)) {
			d->log(LDMSD_LDEBUG, SAMP ": failed get class port info for lid %d port %d\n",
				port->lid, port->num);
			return;
		}
		memcpy(&(port->cap_mask), qr + 2, sizeof(port->cap_mask));
		if (!port->cap_mask) {
			d->log(LDMSD_LWARNING, SAMP ": 0 class port info for lid %d port %d\n",
				port->lid, port->num);
		}
	}
	size_t g;
	uint8_t *res = NULL;
	for (g = 0 ; g < cr_count; g++) {
		struct counter_range *cr = &(d->cr_opt[g]);
		if (cr->mask_check && !(cr->mask_check & port->cap_mask)) {
			continue;
		}
		switch(cr->enabled) {
		case 0:
			res = NULL;
			continue;
		case 1:
			memset(port->rcv_buf[g], 0, PQMAD_SIZE);
			res = pma_query_via(port->rcv_buf[g], &port->port_id,
				port->num, 0, cr->id, d->port);
			if (!res) {
				port->qerr[g] = errno;
				if (!port->err_logged[g]) {
					d->log(LDMSD_LINFO, SAMP ": Error querying subset %s on %s %d, "
						"errno: %d\n", cr->subset, port->remote,
						port->num, port->qerr[g]);
					port->err_logged[g] = 1;
				}
			} else {
				if (d->debug && false)
					d->log(LDMSD_LDEBUG, SAMP ": read subset %zu %s %d %s\n",
						g, cr->subset, port->lid, port->lidname);
				port->qerr[g] = 0;
				if (port->err_logged[g]) {
					port->err_logged[g] = 0;
					d->log(LDMSD_LINFO, SAMP ": Cleared query error %s.%d\n",
						port->remote, port->num);
				}
			}
			break;
		case 2:
			break;
		}
	}

	if (d->port_timing >= 2) {
		gettimeofday(&qtv_now, 0);
		timersub(&qtv_now, &qtv_prev, &qtv_diff);
		port->qtime = qtv_diff.tv_sec + qtv_diff.tv_usec / 1e6;
		if (d->port_timing == 3) {
			timersub(&qtv_now, group_start, &qtv_diff);
			port->stime = qtv_diff.tv_sec * 1000000 + qtv_diff.tv_usec;
		}
	}

}

static void port_decode(struct ibnet_data *d, struct ib_port *port)
{
	if (!d || !port)
		return;
	
	ldms_transaction_begin(port->set);
	if (! port->meta_init) {
		port->meta_init = 1;
		ldms_metric_array_set_str(port->set, d->index_remote, port->remote);
		ldms_metric_set_s32(port->set, d->index_lid, port->lid);
		ldms_metric_set_s32(port->set, d->index_port, port->num);
		if (d->lidnames) {
			ldms_metric_array_set_str(port->set, d->index_node_name,
							port->lidname);
		}
	}

	if (d->port_timing >= 2) {
		ldms_metric_set_double(port->set, d->index_port_query_time, port->qtime);
		if (d->port_timing == 3) {
			ldms_metric_set_s32(port->set, d->index_port_query_offset, port->stime);
		}
	}
	size_t g;
	uint8_t *res = NULL;
	enum MAD_FIELDS j;
	int k;
	for (g = 0; g < cr_count; g++) {
		struct counter_range *cr = &(d->cr_opt[g]);
		switch (cr->enabled) {
		case 0:
			res = NULL;
			continue;
		case 1:
			if (port->qerr[g] != 0) {
				continue;
			}
			if (cr->mask_check && !(cr->mask_check & port->cap_mask)) {
				continue;
			}
			j = d->cr_opt[g].lo;
			k = d->cr_opt[g].lo_index;
			res = port->rcv_buf[g];
			for ( ; j <= d->cr_opt[g].hi; j++) {
				uint64_t v = 0;
				mad_decode_field(res, j, &v);
				ldms_metric_set_u64(port->set, k, v);
				k++;
			}
			break;
		case 2:
			if (!res)
				continue;
			if (cr->mask_check && !(cr->mask_check & port->cap_mask)) {
				continue;
			}
			j = d->cr_opt[g].lo;
			k = d->cr_opt[g].lo_index;
			/* res always set by previous case. */
			for ( ; j <= d->cr_opt[g].hi; j++) {
				uint64_t v = 0;
				mad_decode_field(res, j, &v);
				ldms_metric_set_u64(port->set, k, v);
				k++;
			}
			break;
		}
	}
	ldms_transaction_end(port->set);
}
#endif

static char suffix[12];
static const char *compute_schema_suffix(struct ibnet_data *d)
{
	int g;
	int hash = 0;
	int k = 0;
	for (g = 0; g < cr_count; g++) {
		if (d->cr_opt[g].enabled == 1) {
			hash |= 1 << k;
			if (d->debug)
				d->log(LDMSD_LDEBUG, SAMP " hash %d %s\n",
					k, d->cr_opt[g].subset);
		}
		if (d->cr_opt[g].id != GSI_CONT)
			k++;
	}

        const char *tsuffix;
        switch (d->port_timing) {
	case 2:
		tsuffix = "_t";
		break;
	case 3:
		tsuffix = "_T";
		break;
	default:
		tsuffix = "";
		break;
	}
   
	snprintf(suffix, sizeof(suffix), "_%x%s%s", hash, tsuffix,
		(d->lidnames ? "n" : ""));
	return suffix;
}

#ifdef MAIN
static void ibnet_get_schema_name(int argc, char **argv)
{
	int rc = 0;
	dstring_t ds;
	dstr_init2(&ds, 1024);
	int i;
	for (i = 1; i < argc; i++) {
		dstrcat(&ds, argv[i], DSTRING_ALL);
		dstrcat(&ds, " ", 1);
	}
	char *buf = dstr_extract(&ds);
	int size = 1;
	char *t = buf;
	while (t[0] != '\0') {
		if (isspace(t[0])) size++;
		t++;
	}
	struct attr_value_list *avl = av_new(size);
	struct attr_value_list *kwl = av_new(size);
	rc = tokenize(buf, kwl, avl);
	if (rc) {
		fprintf(stderr, SAMP " failed to parse arguments. %s\n", buf);
		rc = EINVAL;
		goto out;
	}
	struct ibnet_data *d = ibnet_data_new(ldmsd_log, avl, kwl);
	if (!d) {
		fprintf(stderr, "could not create schema from options\n");
		rc = EINVAL;
		goto out;
	}
	printf("%s\n", psname[0]);
	free(psname[0]);
	if (d->port_timing) {
		free(psname[1]);
	}
	ibnet_data_delete(d);
out:
	av_free(kwl);
	av_free(avl);
	free(buf);
	exit(rc);
}

#include "ibnet_data.h"
int main(int argc, char **argv) 
{
	ibnet_get_schema_name(argc, argv);
	return 0;
}

void ldmsd_log(enum ldmsd_loglevel level, const char *fmt, ...) { }
#endif
