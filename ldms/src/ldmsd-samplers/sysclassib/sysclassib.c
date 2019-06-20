/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2012-2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file sysclassib.c
 * \brief Infiniband metric sampler.
 *
 * For the given LIDs and port numbers, this sampler will check the port
 * capability whether they support extended performance metric counters. For the
 * supported port, this sampler will query the counters and do nothing. For the
 * ports that do not support extended metric counters, the sampler will query
 * and then reset the counters to prevent the counters to stay at MAX value.
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
#include <sys/queue.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <wordexp.h>
#include <fnmatch.h>
#include <sys/time.h>

#include <infiniband/mad.h>
#include <infiniband/umad.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, __VA_ARGS__)

/**
 * IB Extension capability flag (obtained from
 * /usr/include/infiniband/iba/ib_types.h)
 */
#define IB_PM_EXT_WIDTH_SUPPORTED       2
/**
 * Another IB Extension capability flag (obtained from
 * /usr/include/infiniband/iba/ib_types.h)
 */
#define IB_PM_EXT_WIDTH_NOIETF_SUP      4

#define SCIB_PATH "/sys/class/infiniband/*/ports/*"

#define SCIB_PATH_SCANFMT "/sys/class/infiniband/%[^/]/ports/%d"

/**
 * The first counter that we're intested in IB_PC_*.
 *
 * We ignore IB_PC_PORT_SELECT_F and IB_PC_COUNTER_SELECT_F.
 */
#define SCIB_PC_FIRST IB_PC_ERR_SYM_F

/**
 * The dummy last counter.
 */
#define SCIB_PC_LAST IB_PC_LAST_F

/**
 * The first counter that we're interested in IB_PC_EXT*.
 *
 * We ignore  IB_PC_EXT_PORT_SELECT_F and IB_PC_EXT_COUNTER_SELECT_F.
 */
#define SCIB_PC_EXT_FIRST IB_PC_EXT_XMT_BYTES_F

/**
 * The dummy last counter.
 */
#define SCIB_PC_EXT_LAST IB_PC_EXT_LAST_F

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

const char *all_metric_names[] = {
	/* These exist only in IB_PC_* */
	"symbol_error",
	"link_error_recovery",
	"link_downed",
	"port_rcv_errors",
	"port_rcv_remote_physical_errors",
	"port_rcv_switch_relay_errors",
	"port_xmit_discards",
	"port_xmit_constraint_errors",
	"port_rcv_constraint_errors",
	"COUNTER_SELECT2_F",
	"local_link_integrity_errors",
	"excessive_buffer_overrun_errors",
	"VL15_dropped",
	/* These four mutually exist in both IB_PC_* and IB_PC_EXT_* */
	"port_xmit_data",
	"port_rcv_data",
	"port_xmit_packets",
	"port_rcv_packets",
	/* this little guy exists only in IB_PC_* */
	"port_xmit_wait",

	/* these exists only in IB_PC_EXT_* */
	"port_unicast_xmit_packets",
	"port_unicast_rcv_packets",
	"port_multicast_xmit_packets",
	"port_multicast_rcv_packets",
};

/**
 * IB_PC_* to scib index map.
 */
static const int scib_idx[] = {
	/* ignore these two */
	[IB_PC_PORT_SELECT_F]         =  -1,
	[IB_PC_COUNTER_SELECT_F]      =  -1,

	[IB_PC_ERR_SYM_F]             =  0,
	[IB_PC_LINK_RECOVERS_F]       =  1,
	[IB_PC_LINK_DOWNED_F]         =  2,
	[IB_PC_ERR_RCV_F]             =  3,
	[IB_PC_ERR_PHYSRCV_F]         =  4,
	[IB_PC_ERR_SWITCH_REL_F]      =  5,
	[IB_PC_XMT_DISCARDS_F]        =  6,
	[IB_PC_ERR_XMTCONSTR_F]       =  7,
	[IB_PC_ERR_RCVCONSTR_F]       =  8,
	[IB_PC_COUNTER_SELECT2_F]     =  9,
	[IB_PC_ERR_LOCALINTEG_F]      =  10,
	[IB_PC_ERR_EXCESS_OVR_F]      =  11,
	[IB_PC_VL15_DROPPED_F]        =  12,

	/* these four overlaps with IB_PC_EXT_* */
	[IB_PC_XMT_BYTES_F]           =  13,
	[IB_PC_RCV_BYTES_F]           =  14,
	[IB_PC_XMT_PKTS_F]            =  15,
	[IB_PC_RCV_PKTS_F]            =  16,

	[IB_PC_XMT_WAIT_F]            =  17,

	/* ignore these two */
	[IB_PC_EXT_PORT_SELECT_F]     =  -1,
	[IB_PC_EXT_COUNTER_SELECT_F]  =  -1,

	/* these four overlaps with IB_PC_* */
	[IB_PC_EXT_XMT_BYTES_F]       =  13,
	[IB_PC_EXT_RCV_BYTES_F]       =  14,
	[IB_PC_EXT_XMT_PKTS_F]        =  15,
	[IB_PC_EXT_RCV_PKTS_F]        =  16,

	/* these four exist only in IB_PC_EXT* */
	[IB_PC_EXT_XMT_UPKTS_F]       =  18,
	[IB_PC_EXT_RCV_UPKTS_F]       =  19,
	[IB_PC_EXT_XMT_MPKTS_F]       =  20,
	[IB_PC_EXT_RCV_MPKTS_F]       =  21,
};

/**
 * Infiniband port representation and context.
 */
struct scib_port {
	int badport; /**< rc from open_port at start. */
	char *ca; /**< CA name */
	int portno; /**< port number */
	uint64_t comp_id; /**< comp_id */
	ib_portid_t portid; /**< IB port id */

	struct timeval tv; /**< Time of the sample */

	/**
	 * Source port for MAD send.
	 *
	 * Actually, one source port is enough for one IB network. However, our
	 * current implementation trivially open one srcport per target port
	 * (::portid) to avoid managing the mapping between IB networks and
	 * source ports.
	 */
	struct ibmad_port *srcport;
	int ext; /**< Extended metric indicator */
	LIST_ENTRY(scib_port) entry; /**< List entry */

	uint8_t rcvbuf[BUFSIZ];
};

typedef struct sysclassib_inst_s *sysclassib_inst_t;
struct sysclassib_inst_s {
	struct ldmsd_plugin_inst_s base;

	int rate; /* 0 for no rate calculation, 1 for enabling it */

	/* Currently support single port due to component ID assignment issue */
	struct scib_port *port;
};

struct scib_port *new_port(const char *portstr)
{
	char ca[128];
	int n, num;
	struct scib_port *port;

	n = sscanf(portstr, "%63[^.].%d", ca, &num);
	if (n != 2) {
		errno = EINVAL;
		goto err0;
	}

	port = calloc(1, sizeof(*port));
	if (!port)
		goto err0;
	port->ca = strdup(ca);
	if (!port->ca)
		goto err1;
	port->portno = num;
	return port;
err1:
	free(port);
err0:
	return NULL;
}

/**
 * Open a given IB \c port (using \c port->ca and \c port->portno) and check its
 * capability.
 *
 * \return 0 if success.
 * \return Error number if error.
 */
int open_port(sysclassib_inst_t inst, struct scib_port *port)
{
	int mgmt_classes[3] = {IB_SMI_CLASS, IB_SA_CLASS, IB_PERFORMANCE_CLASS};
	int rc;
	void *p;
	uint16_t cap;
	umad_port_t uport;

	/* open source port for sending MAD messages */
	port->srcport = mad_rpc_open_port(port->ca, port->portno,
			mgmt_classes, 3);

	if (!port->srcport) {
		INST_LOG(inst, LDMSD_LERROR,
			 "ERROR: Cannot open CA:%s port:%d, errno: %d\n",
			 port->ca, port->portno, errno);
		return errno;
	}

	/* NOTE: I cannot find a way to get LID from the open srcport, so I have
	 * to use umad_get_port to retreive the LID. If anybody find a way to
	 * obtain LID from the srcport, please tell me so that we won't have to
	 * open another port just to get LID */
	rc = umad_get_port(port->ca, port->portno, &uport);
	if (rc) {
		INST_LOG(inst, LDMSD_LERROR,
			 "umad_get_port('%s', %d) : %d\n",
			 port->ca, port->portno, rc);
		return rc;
	}

	/* assign destination port (it's the same as source port) */
	ib_portid_set(&port->portid, uport.base_lid, 0, 0);
	umad_release_port(&uport);

	/* check port capability */
	p = pma_query_via(port->rcvbuf, &port->portid, port->portno, 0,
			CLASS_PORT_INFO, port->srcport);
	if (!p) {
		INST_LOG(inst, LDMSD_LERROR,
			 "pma_query_via ca: %s port: %d  %d\n",
			 port->ca, port->portno, errno);
		return errno;
	}
	memcpy(&cap, port->rcvbuf + 2, sizeof(cap));
	port->ext = cap & (IB_PM_EXT_WIDTH_SUPPORTED
			| IB_PM_EXT_WIDTH_NOIETF_SUP);

	if (!port->ext) {
		INST_LOG(inst, LDMSD_LINFO, "WARNING: Extended query not "
			 "supported for %s:%d, the sampler will reset "
			 "counters every query\n", port->ca, port->portno);
	}

	return 0;
}

/**
 * Close the \c port.
 *
 * This function only close IB port. It does not destroy the port handle. The
 * port handle can be reused in open_port() again.
 */
void close_port(struct scib_port *port)
{
	if (port->srcport)
		mad_rpc_close_port(port->srcport);
	port->srcport = 0;
}

void free_port(struct scib_port *port)
{
	close_port(port);
	free(port->ca);
	free(port);
}

/**
 * Utility function for updating a single metric in a port.
 */
static
inline void update_metric(sysclassib_inst_t inst, ldms_set_t set,
			  struct scib_port *port, int ib_metric,
			  uint64_t new_v, double dt)
{
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int idx = samp->first_idx + (inst->rate?2:1)*scib_idx[ib_metric];
	uint64_t old_v = ldms_metric_get_u64(set, idx);
	if (!port->ext)
		new_v += old_v;
	ldms_metric_set_u64(set, idx, new_v);
	if (inst->rate) {
		ldms_metric_set_double(set, idx+1,
				((double)new_v - (double)old_v) / dt);
	}
}

/**
 * Port query (utility function).
 */
int query_port(sysclassib_inst_t inst, ldms_set_t set,
	       struct scib_port *port, double dt)
{
	void *p;
	int rc;
	uint64_t v;
	int i;
	if (port->badport) {
		/* we will not retry ports missing at sampler start. */
		return 0;
	}
	if (!port->srcport) {
		rc = open_port(inst, port);
		if (rc)
			return rc;
	}
	p = pma_query_via(port->rcvbuf, &port->portid, port->portno, 0,
			IB_GSI_PORT_COUNTERS, port->srcport);
	if (!p) {
		rc = errno;
		INST_LOG(inst, LDMSD_LDEBUG,
			 "Error querying %s.%d, errno: %d\n",
			 port->ca, port->portno, rc);
		close_port(port);
		return rc;
	}

	/* 1st part: the data that only exist in the non-ext */
	for (i = SCIB_PC_FIRST; i < IB_PC_XMT_BYTES_F; i++) {
		v = 0;
		mad_decode_field(port->rcvbuf, i, &v);
		update_metric(inst, set, port, i, v, dt);
	}
	v = 0;
	mad_decode_field(port->rcvbuf, IB_PC_XMT_WAIT_F, &v);
	update_metric(inst, set, port, IB_PC_XMT_WAIT_F, v, dt);

	/* 2nd part: the shared and the ext part */
	if (!port->ext) {
		/* non-ext: update only the shared part */
		for (i = IB_PC_XMT_BYTES_F; i < IB_PC_XMT_WAIT_F; i++) {
			mad_decode_field(port->rcvbuf, i, &v);
			update_metric(inst, set, port, i, v, dt);
		}
		/* and reset the counters */
		performance_reset_via(port->rcvbuf, &port->portid, port->portno,
				0xFFFF, 0, IB_GSI_PORT_COUNTERS, port->srcport);
		return 0;
	}

	/* for ext: update the shared part and the ext-only part */
	p = pma_query_via(port->rcvbuf, &port->portid, port->portno, 0,
			IB_GSI_PORT_COUNTERS_EXT, port->srcport);
	if (!p) {
		rc = errno;
		INST_LOG(inst, LDMSD_LDEBUG,
			 "Error extended querying %s.%d, errno: %d\n",
			 port->ca, port->portno, rc);
		close_port(port);
		return rc;
	}
	for (i = SCIB_PC_EXT_FIRST; i < SCIB_PC_EXT_LAST; i++) {
		v = 0;
		mad_decode_field(port->rcvbuf, i, &v);
		update_metric(inst, set, port, i, v, dt);
	}

	return 0;
}

/* ============== Sampler Plugin APIs ================= */

static
int sysclassib_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	sysclassib_inst_t inst = (void*)pi;
	int i, rc;
	char mname[128];

	for (i = 0; i < ARRAY_SIZE(all_metric_names); i++) {
		/* counters */
		snprintf(mname, sizeof(mname), "ib.%s", all_metric_names[i]);
		rc = ldms_schema_metric_add(schema, mname, LDMS_V_U64, "");
		if (rc < 0)
			return -rc; /* rc == -errno */
		/* rates */
		if (inst->rate) {
			snprintf(mname, sizeof(mname), "ib.%s.rate",
					all_metric_names[i]);
			rc = ldms_schema_metric_add(schema, mname,
						    LDMS_V_D64, "");
			if (rc < 0)
				return -rc; /* rc == -errno */
		}
	}
	return 0;
}

static
int sysclassib_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	sysclassib_inst_t inst = (void*)pi;
	struct scib_port *port = ctxt;
	struct timeval tv, dtv;
	double dt;
	/* Populate set metrics */

	gettimeofday(&tv, NULL);
	timersub(&tv, &port->tv, &dtv);
	dt = dtv.tv_sec + (dtv.tv_usec * 1e-6);

	return query_port(inst, set, port, dt);
}


/* ============== Common Plugin APIs ================= */

static
const char *sysclassib_desc(ldmsd_plugin_inst_t pi)
{
	return "sysclassib - Infiniband metric sampler";
}

static
char *_help = "\
sysclassib configuration synopsis:\n\
    config name=INST [COMMON_OPTIONS] port=<CARD:PORT_NUM> [rate=0|1]\n\
\n\
Option descriptions:\n\
    port    REQUIRED. The `CARD:PORT_NUM` string to identify the device/port.\n\
            (e.g. cxgb4_0:1).\n\
    rate    0 for disabling rate metric calculation. 1 for enabling it.\n\
            The default is 0 (disabled).\n\
";

static
const char *sysclassib_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
void sysclassib_del(ldmsd_plugin_inst_t pi)
{
	sysclassib_inst_t inst = (void*)pi;

	if (inst->port)
		free_port(inst->port);
}

static
int sysclassib_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	sysclassib_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)pi->base;
	ldms_set_t set;
	int rc;
	const char *val;

	if (inst->port) {
		snprintf(ebuf, ebufsz, "%s: already configured", pi->inst_name);
		return EALREADY;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	/* rate */
	val = json_attr_find_str(json, "rate");
	if (val) {
		inst->rate = (atoi(val) != 0);
	}

	/* port */
	val = json_attr_find_str(json, "port");
	if (!val) {
		snprintf(ebuf, ebufsz, "%s: `port` attributed is needed.\n",
			 pi->inst_name);
		return EINVAL;
	}
	inst->port = new_port(val);
	if (!inst->port) {
		if (errno == EINVAL) {
			snprintf(ebuf, ebufsz, "%s: invalid port.\n",
				 pi->inst_name);
			return EINVAL;
		}
		snprintf(ebuf, ebufsz, "%s: cannot create port %s, "
			 "errno: %d.\n", pi->inst_name, val, errno);
		return errno;
	}
	rc = open_port(inst, inst->port);
	if (rc) {
		snprintf(ebuf, ebufsz, "%s: cannot open port %s, errno: %d.\n",
			 pi->inst_name, val, rc);
		return rc;
	}

	/* create schema + set */
	if (!samp->schema) {
		/* create schema only once */
		samp->schema = samp->create_schema(pi);
		if (!samp->schema)
			return errno;
	}
	set = samp->create_set(pi, samp->set_inst_name, samp->schema,
				inst->port);
	if (!set)
		return errno;
	return 0;
}

static
int sysclassib_init(ldmsd_plugin_inst_t pi)
{
	ldmsd_sampler_type_t samp = (void*)pi->base;
	/* override update_schema() and update_set() */
	samp->update_schema = sysclassib_update_schema;
	samp->update_set = sysclassib_update_set;

	return 0;
}

static
struct sysclassib_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "sysclassib",

                /* Common Plugin APIs */
		.desc   = sysclassib_desc,
		.help   = sysclassib_help,
		.init   = sysclassib_init,
		.del    = sysclassib_del,
		.config = sysclassib_config,
	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	sysclassib_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
