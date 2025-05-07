/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2018 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2012-2018 Open Grid Computing, Inc. All rights reserved.
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

typedef enum {
	SYSCLASSIB_METRICS_COUNTER,
	SYSCLASSIB_METRICS_BOTH
} sysclassib_metrics_type_t;


#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "sampler_base.h"

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

	/**
	 * Metric handles for raw metric counters of the port.
	 */
	int handle[ARRAY_SIZE(all_metric_names)];
	/**
	 * Metric handles for rate metrics of the port.
	 */
	int rate[ARRAY_SIZE(all_metric_names)];
};

LIST_HEAD(scib_port_list, scib_port);
struct scib_port_list scib_port_list = {0};
uint8_t rcvbuf[BUFSIZ] = {0};

#define SAMP "sysclassib"
static ldms_set_t set = NULL;
static base_data_t base;

static ovis_log_t mylog;

struct timeval tv[2];
struct timeval *tv_now = &tv[0];
struct timeval *tv_prev = &tv[1];

/*
 * Which metrics - counter or both. default both.
 */
static sysclassib_metrics_type_t sysclassib_metrics_type;
/**
 * \param setname The set name (e.g. nid00001/sysclassib)
 */
static int create_metric_set(base_data_t base)
{
	int rc, i;
	ldms_schema_t schema;
	char metric_name[128];
	struct scib_port *port;


	schema = base_schema_new(base);
	if (!schema)
		return ENOMEM;

	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = EINVAL;
		goto err;
	}


	LIST_FOREACH(port, &scib_port_list, entry) {
		if (port->badport) {
			ovis_log(mylog, OVIS_LINFO,
				SAMP ": Not monitoring port: %s.%d\n",
				port->ca, port->portno);
		} else {
			ovis_log(mylog, OVIS_LINFO,
				SAMP ": Monitoring port: %s.%d\n",
				port->ca, port->portno);
		}
		for (i = 0; i < ARRAY_SIZE(all_metric_names); i++) {
			/* counters */
			snprintf(metric_name, 128, "ib.%s#%s.%d",
					all_metric_names[i],
					port->ca,
					port->portno);
			port->handle[i] = ldms_schema_metric_add(schema, metric_name,
							  LDMS_V_U64);
			/* rates */
			if (sysclassib_metrics_type == SYSCLASSIB_METRICS_BOTH) {
				snprintf(metric_name, 128, "ib.%s.rate#%s.%d",
					all_metric_names[i],
					port->ca,
					port->portno);
				port->rate[i] = ldms_schema_metric_add(schema, metric_name,
							LDMS_V_D64);
			}
		}
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

err:

	return rc;

}

/**
 * Populate all ports (in /sys/class/infiniband) and put into the given \c list.
 *
 * Port population only create port handle and fill in basic port information
 * (CA and port number).
 *
 * \return 0 on success.
 * \return Error code on error.
 */
int populate_ports_wild(struct scib_port_list *list)
{
	wordexp_t p;
	struct scib_port *port;
	int rc = wordexp(SCIB_PATH, &p, 0);
	char ca[64];
	int port_no;
	int i;
	if (rc) {
		if (rc == WRDE_NOSPACE)
			return ENOMEM;
		else
			return ENOENT;
	}
	for (i = 0; i < p.we_wordc; i++) {
		port = calloc(1, sizeof(*port));
		if (!port) {
			rc = ENOMEM;
			goto err;
		}
		LIST_INSERT_HEAD(list, port, entry);
		rc = sscanf(p.we_wordv[i], SCIB_PATH_SCANFMT, ca, &port_no);
		if (rc != 2) {
			rc = EINVAL;
			goto err;
		}
		port->ca = strdup(ca);
		port->portno = port_no;
	}
	wordfree(&p);
	return 0;
err:
	while ((port = LIST_FIRST(list))) {
		LIST_REMOVE(port, entry);
		if (port->ca)
			free(port->ca);
		free(port);
	}
	wordfree(&p);
	return rc;
}

/**
 * Populate ports by the given string \c ports.
 *
 * If \c ports is "*", this function calls populate_port_wild().
 *
 * Port population only create port handle and fill in basic port information
 * (CA and port number).
 *
 * \return 0 if success.
 * \return Error code if error.
 */
int populate_ports(struct scib_port_list *list, char *ports)
{
	int rc, n, port_no;
	struct scib_port *port;
	char *s;
	char ca[64];
	if (strcmp(ports, "*") == 0)
		return populate_ports_wild(list);
	s = ports;
	while (*s) {
		rc = sscanf(s, "%63[^.].%d%n", ca, &port_no, &n);
		if (rc != 2) {
			ovis_log(mylog, OVIS_LERROR,"sysclassib: Cannot parse ports:%s."
				"Need list of NAME.NUM, e.g. qib0.1,qib.1\n",s);
			rc = EINVAL;  /* invalid format */
			goto err;
		}
		port = calloc(1, sizeof(*port));
		if (!port)
			goto err;
		LIST_INSERT_HEAD(list, port, entry);
		if (*ca == ',')
			port->ca = strdup(ca + 1);
		else
			port->ca = strdup(ca);
		if (!port->ca)
			goto err;
		port->portno = port_no;
		s += n;
	};

	return 0;

err:
	while ((port = LIST_FIRST(list))) {
		LIST_REMOVE(port, entry);
		if (port->ca)
			free(port->ca);
		free(port);
	}
	return rc;
}

/**
 * Open a given IB \c port (using \c port->ca and \c port->portno) and check its
 * capability.
 *
 * \return 0 if success.
 * \return Error number if error.
 */
int open_port(struct scib_port *port)
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
		ovis_log(mylog, OVIS_LERROR, SAMP ": ERROR: Cannot open CA:%s port:%d,"
				" ERRNO: %d\n", port->ca, port->portno,
				errno);
		return errno;
	}

	/* NOTE: I cannot find a way to get LID from the open srcport, so I have
	 * to use umad_get_port to retreive the LID. If anybody find a way to
	 * obtain LID from the srcport, please tell me so that we won't have to
	 * open another port just to get LID */
	rc = umad_get_port(port->ca, port->portno, &uport);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": umad_get_port('%s', %d) "
			": %d\n", port->ca, port->portno, rc);
		return rc;
	}

	/* assign destination port (it's the same as source port) */
	ib_portid_set(&port->portid, uport.base_lid, 0, 0);
	umad_release_port(&uport);

	/* check port capability */
	p = pma_query_via(rcvbuf, &port->portid, port->portno, 0,
			CLASS_PORT_INFO, port->srcport);
	if (!p) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": pma_query_via ca: %s port: %d"
				"  %d\n", port->ca, port->portno, errno);
		return errno;
	}
	memcpy(&cap, rcvbuf + 2, sizeof(cap));
	port->ext = cap & (IB_PM_EXT_WIDTH_SUPPORTED
			| IB_PM_EXT_WIDTH_NOIETF_SUP);

	if (!port->ext) {
		ovis_log(mylog, OVIS_LINFO, SAMP ": WARNING: Extended query not "
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

/**
 * Open all port in the \c list at once.
 *
 * \return 0 if no error.
 * \return Error code when encounter an error from a port opening.
 */
int open_ports(struct scib_port_list *list)
{
	struct scib_port *port;
	int rc = 0, err = 0, lastrc = 0;

	LIST_FOREACH(port, list, entry) {
		port->badport = 0;
		rc = open_port(port);
		if (rc) {
			ovis_log(mylog, OVIS_LINFO,"sysclassib: Error querying %s.%d, errno: %d\n",
				port->ca, port->portno, rc);
			port->badport = rc;
			lastrc = rc;
			err++;
		}
	}

	if (err)
		return lastrc;
	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return
		"config name=" SAMP " ports=<ports> [metrics_type=<mtype>] " \
		BASE_CONFIG_USAGE \
		"    <mtype>         0 (raw counters) or 1 (raw and also rates, default)\n"
		"    <ports>         A comma-separated list of ports (e.g. mlx4_0.1,mlx4_0.2) or\n"
		"                    a * for all IB ports. If not given, '*' is assumed.\n";
}

/**
 * \brief Configuration
 *
 * PORTS is a comma-separate list of the form CARD1.PORT1,CARD2.PORT2,...
 * 	or just a single '*' (w/o quote) for all ports
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{

	int rc = 0;
	char *ports;
	char *value;


	if (set) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	value = av_value(avl,"metrics_type");
	if (value) {
		sysclassib_metrics_type = atoi(value);
		if ((sysclassib_metrics_type < SYSCLASSIB_METRICS_COUNTER) ||
		    (sysclassib_metrics_type > SYSCLASSIB_METRICS_BOTH)) {
			return EINVAL;
		}
	} else {
		sysclassib_metrics_type = SYSCLASSIB_METRICS_BOTH;
	}
	ports = av_value(avl, "ports");
	if (!ports)
		ports = "*";

	rc = populate_ports(&scib_port_list, ports);
	if (rc) {
		ovis_log(mylog, OVIS_LINFO, SAMP ": Failed to find ports matching %s.\n",ports);
		return rc;
	}

	rc = open_ports(&scib_port_list);
	if (rc) {
		ovis_log(mylog, OVIS_LINFO, SAMP ": Failed to open ports.\n");
		return rc;
	}

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base){
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}

	return 0;

err:
	base_del(base);
	return rc;
}

/**
 * Utility function for updating a single metric in a port.
 */
static
inline void update_metric(struct scib_port *port, int idx, uint64_t new_v,
			double dt)
{
	uint64_t old_v = ldms_metric_get_u64(set, port->handle[idx]);
	if (!port->ext)
		new_v += old_v;
	ldms_metric_set_u64(set, port->handle[idx], new_v);
	if (sysclassib_metrics_type == SYSCLASSIB_METRICS_BOTH) {
		ldms_metric_set_double(set, port->rate[idx], ((double)new_v - (double)old_v) / dt);
	}
}

/**
 * Port query (utility function).
 */
int query_port(struct scib_port *port, double dt)
{
	void *p;
	int rc;
	uint64_t v;
	int i, j;
	if (port->badport) {
		/* we will not retry ports missing at sampler start. */
		return 0;
	}
	if (!port->srcport) {
		rc = open_port(port);
		if (rc)
			return rc;
	}
	p = pma_query_via(rcvbuf, &port->portid, port->portno, 0,
			IB_GSI_PORT_COUNTERS, port->srcport);
	if (!p) {
		rc = errno;
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": Error querying %s.%d, errno: %d\n",
				port->ca, port->portno, rc);
		close_port(port);
		return rc;
	}

	/* 1st part: the data that only exist in the non-ext */
	for (i = SCIB_PC_FIRST; i < IB_PC_XMT_BYTES_F; i++) {
		v = 0;
		mad_decode_field(rcvbuf, i, &v);
		j = scib_idx[i];
		update_metric(port, j, v, dt);
	}
	v = 0;
	mad_decode_field(rcvbuf, IB_PC_XMT_WAIT_F, &v);
	j = scib_idx[IB_PC_XMT_WAIT_F];
	update_metric(port, j, v, dt);

	/* 2nd part: the shared and the ext part */
	if (!port->ext) {
		/* non-ext: update only the shared part */
		for (i = IB_PC_XMT_BYTES_F; i < IB_PC_XMT_WAIT_F; i++) {
			mad_decode_field(rcvbuf, i, &v);
			j = scib_idx[i];
			update_metric(port, j, v, dt);
		}
		/* and reset the counters */
		performance_reset_via(rcvbuf, &port->portid, port->portno,
				0xFFFF, 0, IB_GSI_PORT_COUNTERS, port->srcport);
		return 0;
	}

	/* for ext: update the shared part and the ext-only part */
	p = pma_query_via(rcvbuf, &port->portid, port->portno, 0,
			IB_GSI_PORT_COUNTERS_EXT, port->srcport);
	if (!p) {
		rc = errno;
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": Error extended querying %s.%d, "
				"errno: %d\n", port->ca, port->portno, rc);
		close_port(port);
		return rc;
	}
	for (i = SCIB_PC_EXT_FIRST; i < SCIB_PC_EXT_LAST; i++) {
		v = 0;
		mad_decode_field(rcvbuf, i, &v);
		j = scib_idx[i];
		update_metric(port, j, v, dt);
	}

	return 0;
}

static int sample(ldmsd_plug_handle_t handle)
{
	struct timeval *tmp;
	struct timeval tv_diff;
	double dt;
	struct scib_port *port;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	gettimeofday(tv_now, 0);
	timersub(tv_now, tv_prev, &tv_diff);
	dt = (double)tv_diff.tv_sec + tv_diff.tv_usec / 1.0e06;

	base_sample_begin(base);
	LIST_FOREACH(port, &scib_port_list, entry) {
		/* query errors are handled in query_port() function */
		query_port(port, dt);
	}
	base_sample_end(base);

	tmp = tv_now;
	tv_now = tv_prev;
	tv_prev = tmp;
	return 0;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
	set = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	struct scib_port *port;
	while ((port = LIST_FIRST(&scib_port_list))) {
		LIST_REMOVE(port, entry);
		close_port(port);
		free(port->ca);
		free(port);
	}
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
