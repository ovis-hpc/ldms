/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2012 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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
#include "ldms_slurmjobid.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

static const char *all_metric_names[] = {
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
	ldms_metric_t handle[ARRAY_SIZE(all_metric_names)];
	/**
	 * Metric handles for rate metrics of the port.
	 */
	ldms_metric_t rate[ARRAY_SIZE(all_metric_names)];
};

LIST_HEAD(scib_port_list, scib_port);
static struct scib_port_list scib_port_list = {0};
static char rcvbuf[BUFSIZ] = {0};

static ldms_set_t set = NULL;
static ldms_metric_t metric_table[1]; /* one for jobid, if configured */
static ldmsd_msg_log_f msglog;
static uint64_t comp_id;

struct timeval tv[2];
struct timeval *tv_now = &tv[0];
struct timeval *tv_prev = &tv[1];

/**
 * Which metrics - counter or both. default both.
 */
static sysclassib_metrics_type_t sysclassib_metrics_type;

LDMS_JOBID_GLOBALS;

/**
 * \param setname The set name (e.g. nid00001/sysclassib)
 */
static int create_metric_set(const char *setname)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, i;
	char metric_name[128];
	struct scib_port *port;

	if (set) {
		msglog(LDMS_LDEBUG,"sysclassib: Double create set: %s\n", setname);
		return EEXIST;
	}

	/* calculate total metric set size */
	tot_meta_sz = 0;
	tot_data_sz = 0;

	int metric_count = 0;
	LDMS_SIZE_JOBID_METRIC(sysclassib,meta_sz,tot_meta_sz,
		data_sz,tot_data_sz,metric_count,rc,msglog);

	LIST_FOREACH(port, &scib_port_list, entry) {
		msglog(LDMS_LINFO,"sysclassib: Monitoring port: %s.%d\n",
			port->ca, port->portno);
		for (i = 0; i < ARRAY_SIZE(all_metric_names); i++) {
			/* counters */
			snprintf(metric_name, 128, "ib.%s#%s.%d",
				 all_metric_names[i],
				 port->ca,
				 port->portno);
			ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz,
					     &data_sz);
			tot_meta_sz += meta_sz;
			tot_data_sz += data_sz;
			/* rates */
			if (sysclassib_metrics_type > SYSCLASSIB_METRICS_BOTH){
				snprintf(metric_name, 128, "ib.%s.rate#%s.%d",
					 all_metric_names[i],
					 port->ca,
					 port->portno);
				ldms_get_metric_size(metric_name, LDMS_V_F, &meta_sz,
						     &data_sz);
				tot_meta_sz += meta_sz;
				tot_data_sz += data_sz;
			}
		}
	}

	/* create set and metrics */
	rc = ldms_create_set(setname, tot_meta_sz, tot_data_sz, &set);
	if (rc) {
		msglog(LDMS_LDEBUG,"sysclassib: ldms_create_set failed, rc: %d\n", rc);
		return rc;
	}

	metric_count = 0;
	LDMS_ADD_JOBID_METRIC(metric_table,metric_count,set,rc,err,comp_id);

	LIST_FOREACH(port, &scib_port_list, entry) {
		for (i = 0; i < ARRAY_SIZE(all_metric_names); i++) {
			/* counters */
			snprintf(metric_name, 128, "ib.%s#%s.%d",
				 all_metric_names[i],
				 port->ca,
				 port->portno);
			port->handle[i] = ldms_add_metric(set, metric_name,
							  LDMS_V_U64);
			ldms_set_user_data(port->handle[i], comp_id);
			/* rates */
			if (sysclassib_metrics_type > SYSCLASSIB_METRICS_BOTH){
				snprintf(metric_name, 128, "ib.%s.rate#%s.%d",
					 all_metric_names[i],
					 port->ca,
					 port->portno);
				port->rate[i] = ldms_add_metric(set, metric_name,
								LDMS_V_F);
				ldms_set_user_data(port->rate[i], comp_id);
			}
		}
	}
	return 0; /* FIXED: missing return that could cause random failures. */
err:
	return 1;
}

static const char *usage(void)
{
	return
"config name=sysclassib component_id=<comp_id> set=<setname> metrics_type=<0/1> ports=CA.PRT,...\n"
"    comp_id      The component id value.\n"
"    setname      The set name.\n"
LDMS_JOBID_DESC
"    metrics_type 0=counters only, 1=both (Optional - default=both)\n"
"    ports        A comma-separated list of ports (e.g. mlx4_0.1,mlx4_0.2) or\n"
"                 a * for all IB ports. If not given, '*' is assumed.\n"
;
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
			msglog(LDMS_LERROR,"sysclassib: Cannot parse ports:%s."
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
		msglog(LDMS_LDEBUG,"sysclassib: ERROR: Cannot open CA:%s port:%d,"
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
		msglog(LDMS_LDEBUG,"sysclassib: umad_get_port('%s', %d) error: %d\n",
				port->ca, port->portno, rc);
		return rc;
	}

	/* assign destination port (it's the same as source port) */
	ib_portid_set(&port->portid, uport.base_lid, 0, 0);
	umad_release_port(&uport);

	/* check port capability */
	p = pma_query_via(rcvbuf, &port->portid, port->portno, 0,
			CLASS_PORT_INFO, port->srcport);
	if (!p) {
		msglog(LDMS_LDEBUG,"sysclassib: pma_query_via ca: %s port: %d"
				" error: %d\n", port->ca, port->portno, errno);
		return errno;
	}
	memcpy(&cap, rcvbuf + 2, sizeof(cap));
	port->ext = cap & (IB_PM_EXT_WIDTH_SUPPORTED
			| IB_PM_EXT_WIDTH_NOIETF_SUP);

	if (!port->ext) {
		msglog(LDMS_LDEBUG,"sysclassib: WARNING: Extended query not supported for"
			" %s:%d, the sampler will reset counters every query\n",
			port->ca, port->portno);
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
	int rc;

	LIST_FOREACH(port, list, entry) {
		rc = open_port(port);
		if (rc)
			return rc;
	}

	return 0;
}

/**
 * \brief Configuration
 *
 * config name=sysclassib component_id=NUM set=NAME ports=PORTS
 * NUM is a regular decimal
 * NAME is a set name
 * PORTS is a comma-separate list of the form CARD1.PORT1,CARD2.PORT2,...
 * 	or just a single '*' (w/o quote) for all ports
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{

	int rc = 0;
	char *value;
	char *setstr;
	char *ports;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtoull(value, NULL, 0);
	else
		comp_id = 0;

	setstr = av_value(avl, "set");
	if (!setstr)
		return EINVAL;

	LDMS_CONFIG_JOBID_METRIC(value,avl);

	value = av_value(avl,"metrics_type");
	if (value) {
		sysclassib_metrics_type = atoi(value);
		if ((sysclassib_metrics_type < SYSCLASSIB_METRICS_COUNTER) ||
		    (sysclassib_metrics_type > SYSCLASSIB_METRICS_BOTH)){
			return EINVAL;
		}
	} else {
		/* long standing bug here changed the default behavior to SYSCLASSIB_METRICS_COUNTER
		fixing bug and making default the historical one since noone complained.
		*/
		sysclassib_metrics_type = SYSCLASSIB_METRICS_COUNTER;
	}


	ports = av_value(avl, "ports");
	if (!ports)
		ports = "*";

	rc = populate_ports(&scib_port_list, ports);
	if (rc)
		return rc;

	rc = open_ports(&scib_port_list);
	if (rc)
		return rc;

	return create_metric_set(setstr);
}

static ldms_set_t get_set()
{
	return set;
}

/**
 * Utility function for updating a single metric in a port.
 */
inline void update_metric(struct scib_port *port, int idx, uint64_t new_v,
			float dt)
{
	uint64_t old_v = ldms_get_u64(port->handle[idx]);
	if (!port->ext)
		new_v += old_v;

	ldms_set_u64(port->handle[idx], new_v);
	if (sysclassib_metrics_type > SYSCLASSIB_METRICS_BOTH)
		ldms_set_float(port->rate[idx], (new_v - old_v) / dt);
}

/**
 * Port query (utility function).
 */
int query_port(struct scib_port *port, float dt)
{
	void *p;
	int rc;
	uint64_t v;

	int i, j;
	if (!port->srcport) {
		rc = open_port(port);
		if (rc)
			return rc;
	}
	p = pma_query_via(rcvbuf, &port->portid, port->portno, 0,
			IB_GSI_PORT_COUNTERS, port->srcport);
	if (!p) {
		rc = errno;
		msglog(LDMS_LDEBUG,"sysclassib: Error querying %s.%d, errno: %d\n",
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
		msglog(LDMS_LDEBUG,"sysclassib: Error extended querying %s.%d, errno: %d\n",
				port->ca, port->portno, rc);
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

static int sample(void)
{


	char lbuf[32];


	struct timeval *tmp;
	struct timeval tv_diff;
	
	float dt;
	struct scib_port *port;

	if (!set){
		msglog(LDMS_LDEBUG,"sysclassib: plugin not initialized\n");
		return EINVAL;
	}

	gettimeofday(tv_now, 0);
	timersub(tv_now, tv_prev, &tv_diff);
	dt = tv_diff.tv_sec + tv_diff.tv_usec / 1e06f;

	ldms_begin_transaction(set);
	int metric_no = 0;
        union ldms_value v;

	LDMS_JOBID_SAMPLE(v,metric_table,metric_no);
	LIST_FOREACH(port, &scib_port_list, entry) {
		/* query errors are handled in query_port() function */
		query_port(port, dt);
	}
	ldms_end_transaction(set);

	tmp = tv_now;
	tv_now = tv_prev;
	tv_prev = tmp;
	return 0;
}

static void term(void){

	struct scib_port *port;
	while ((port = LIST_FIRST(&scib_port_list))) {
		LIST_REMOVE(port, entry);
		close_port(port);
		free(port->ca);
		free(port);
	}
	if (set)
		ldms_destroy_set(set);
	set = NULL;
	LDMS_JOBID_TERM;
}

static struct ldmsd_sampler sysclassib_plugin = {
	.base = {
		.name = "sysclassib",
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &sysclassib_plugin.base;
}
