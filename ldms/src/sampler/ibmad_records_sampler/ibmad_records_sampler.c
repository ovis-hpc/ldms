/* -*- c-basic-offset: 8 -*- */
/* Copyright (c) 2012-2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2012-2018 Open Grid Computing, Inc. All rights reserved.
 * Copyright 2021 Lawrence Livermore National Security, LLC
 *
 * See the top-level COPYRIGHT file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-3-Clause)
 */
#include <limits.h>
#include <string.h>
#include <dirent.h>
#include <coll/rbt.h>
#include <ctype.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdbool.h>
#include <unistd.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include <mad.h>
#include <umad.h>
#include <iba/ib_types.h>

#include "config.h"
#include "sampler_base.h"

#define _GNU_SOURCE

#define SAMP "ibmad_records_sampler"

/* So far I cannot find a header that defines these for us */
#define PORT_STATE_ACTIVE 4

#define MAX_CA_NAMES 32
#define PORT_FILTER_NONE 0
#define PORT_FILTER_INCLUDE 1
#define PORT_FILTER_EXCLUDE 2
struct port_name {
	char ca_name[UMAD_CA_NAME_LEN];
	uint64_t port_bits;
	/**< if n-th bit is 1, port n of ca_name is matched by the filter. */
};

static struct {
	bool use_rate_metrics;
	int port_filter;
	struct port_name ports[MAX_CA_NAMES];
        time_t refresh_interval;
} conf;

static ovis_log_t mylog;
static base_data_t sampler_base; /* contains the schema */

/* red-black tree root for infiniband port metrics */
static struct rbt interfaces_tree;

struct interface_data {
        struct rbn interface_rbn;

        char *ca_name;
	int port;
        struct ibmad_port *srcport;
	ib_portid_t portid;
	int ext; /**< Extended metric indicator */
	int repeat; /**< true if not the first sample */
};

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

/* The first counter that we're intested in IB_PC_*.
 *
 * We ignore IB_PC_PORT_SELECT_F and IB_PC_COUNTER_SELECT_F. */
#define SCIB_PC_FIRST IB_PC_ERR_SYM_F

/* The dummy last counter. */
#define SCIB_PC_LAST IB_PC_LAST_F

/* The first counter that we're interested in IB_PC_EXT*.
 *
 * We ignore  IB_PC_EXT_PORT_SELECT_F and IB_PC_EXT_COUNTER_SELECT_F. */
#define SCIB_PC_EXT_FIRST IB_PC_EXT_XMT_BYTES_F

/* The dummy last counter. */
#define SCIB_PC_EXT_LAST IB_PC_EXT_LAST_F

/* IB_PC_* to scib index map. */
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

static struct {
        int port;
        int ca_name;
        int record_definition;
        int record_list;
        int counters[ARRAY_SIZE(all_metric_names)];
        int rates[ARRAY_SIZE(all_metric_names)];
} mindex;

/* Creates the schema and the metric set */
static int ibmad_initialize()
{
        ldms_record_t rec_def; /* a pointer */
	char metric_name[128];
        int rc;
        int i;

        ovis_log(mylog, OVIS_LDEBUG, "ibmad_initialize()\n");

        /* Create the schema */
        base_schema_new(sampler_base);
        if (sampler_base->schema == NULL)
                goto err1;

        /* Create the record that will be used in the schema's list */
        rec_def = ldms_record_create("ib_nic");
        if (rec_def == NULL)
                goto err2;
        rc = ldms_record_metric_add(rec_def, "ca_name", NULL, LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0)
                goto err3;
        mindex.ca_name = rc;
        rc = ldms_record_metric_add(rec_def, "port", NULL, LDMS_V_U32, 1);
        if (rc < 0)
                goto err3;
        mindex.port = rc;
	for (i = 0; i < ARRAY_SIZE(all_metric_names); i++) {
		/* add ibmad counter metrics */
		snprintf(metric_name, 128, "%s",
			 all_metric_names[i]);
		mindex.counters[i] =
			ldms_record_metric_add(rec_def, metric_name, NULL, LDMS_V_U64, 1);
                if (mindex.rates[i] < 0)
                        goto err3;

		if (conf.use_rate_metrics) {
			/* add ibmad rate metrics */
			snprintf(metric_name, 128, "%s.rate",
				 all_metric_names[i]);
			mindex.rates[i] =
				ldms_record_metric_add(rec_def, metric_name, NULL, LDMS_V_D64, 1);
                        if (mindex.rates[i] < 0)
                                goto err3;
		}
	}
        rc = ldms_schema_record_add(sampler_base->schema, rec_def);
        if (rc < 0) {
                goto err3;
        }
        mindex.record_definition = rc;
        rc = ldms_schema_metric_list_add(sampler_base->schema, "ib_nics", NULL, 1024);
        if (rc < 0) {
                goto err2;
        }
        mindex.record_list = rc;

        /* Create the metric set */
        base_set_new(sampler_base);
        if (sampler_base->set == NULL) {
                goto err2;
        }

        return 0;
err3:
        ldms_record_delete(rec_def);
err2:
        base_schema_delete(sampler_base);
err1:
        ovis_log(mylog, OVIS_LERROR, "schema creation failed\n");
        return -1;
}

static int string_comparator(void *a, const void *b)
{
        return strcmp((char *)a, (char *)b);
}

/**
 * Open a given IB \c port (using \c ca and \c port) and check its
 * capability.
 *
 * \return 0 if success.
 * \return Error number if error.
 */
static int _port_open(struct interface_data *data, unsigned base_lid)
{
	int mgmt_classes[3] = {IB_SMI_CLASS, IB_SA_CLASS, IB_PERFORMANCE_CLASS};
	void *p;
	uint16_t cap;
	uint8_t rcvbuf[BUFSIZ];

	/* open source port for sending MAD messages */
	data->srcport = mad_rpc_open_port(data->ca_name, data->port, mgmt_classes, 3);
	if (!data->srcport) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": ERROR: Cannot open CA:%s port:%d,"
				" ERRNO: %d\n", data->ca_name, data->port,
				errno);
		return errno;
	}

	/* assign destination port (it's the same as source port) */
	ib_portid_set(&data->portid, base_lid, 0, 0);

	/* check port capability */
	p = pma_query_via(rcvbuf, &data->portid, data->port, 0,
			  CLASS_PORT_INFO, data->srcport);
	if (!p) {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": pma_query_via() failed: ca_name=%s port=%d"
				"  %d\n", data->ca_name, data->port, errno);
		mad_rpc_close_port(data->srcport);
		return -1;
	}
	memcpy(&cap, rcvbuf + 2, sizeof(cap));
	data->ext = cap & (IB_PM_EXT_WIDTH_SUPPORTED
			| IB_PM_EXT_WIDTH_NOIETF_SUP);

	if (!data->ext) {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": WARNING: Extended query not "
			"supported for %s:%d, the sampler will reset "
			"counters every query\n", data->ca_name, data->port);
	}

	return 0;
}

/**
 * Close the \c port.
 *
 * This function only close IB port.
 */
static void _port_close(struct interface_data *data)
{
	if (data->srcport)
		mad_rpc_close_port(data->srcport);
	data->srcport = NULL;
}


static struct interface_data *interface_create(const char *ca_name,
                                               int port, unsigned base_lid)
{
        struct interface_data *data;
	int rc;

        ovis_log(mylog, OVIS_LDEBUG, "interface_create() %s, base_lid=%u\n",
               ca_name, base_lid);
        data = calloc(1, sizeof(*data));
        if (data == NULL)
                goto out1;
	data->port = port;
        data->ca_name = strdup(ca_name);
        if (data->ca_name == NULL)
                goto out2;

	rc = _port_open(data, base_lid);
	if (rc != 0) {
		goto out3;
	}

        rbn_init(&data->interface_rbn, data->ca_name);

        return data;

out3:
        free(data->ca_name);
out2:
        free(data);
out1:
        return NULL;
}

static void interface_destroy(struct interface_data *data)
{
        ovis_log(mylog, OVIS_LDEBUG, "interface_destroy() %s\n", data->ca_name);
        _port_close(data);
        free(data->ca_name);
        free(data);
}

static void interfaces_tree_destroy()
{
        struct rbn *rbn;
        struct interface_data *data;

        while (!rbt_empty(&interfaces_tree)) {
                rbn = rbt_min(&interfaces_tree);
                data = container_of(rbn, struct interface_data,
                                   interface_rbn);
                rbt_del(&interfaces_tree, rbn);
                interface_destroy(data);
        }
}

#define NOT_IN_FILTER -1
/* return the index where name appears in conf.ports or NOT_IN_FILTER */
static int in_port_filter(const char *name)
{
	int i;
	for (i = 0 ; i < MAX_CA_NAMES; i++) {
		if (conf.ports[i].ca_name[0] == '\0')
			break;
		if ( !strcmp(conf.ports[i].ca_name, name))
			return i;
	}
	return NOT_IN_FILTER;
}

/* return true if some port on device named might be collectable
 * based on filter. */
static bool collect_ca(const char *name)
{
	if (conf.port_filter == PORT_FILTER_NONE)
		return true;
	int pi = in_port_filter(name);
	if (pi != NOT_IN_FILTER) {
		if (conf.port_filter == PORT_FILTER_INCLUDE)
			return true;
		if (conf.port_filter == PORT_FILTER_EXCLUDE) {
			if (conf.ports[pi].port_bits != 0 )
				return true;
			return false;
		}
	}
	return (conf.port_filter == PORT_FILTER_EXCLUDE);
}

/* return true if both name and port are collectable based on filter. */
static int collect_ca_port(const char *name, int port)
{
	if (conf.port_filter == PORT_FILTER_NONE)
		return true;
	int pi = in_port_filter(name);
	if (pi != NOT_IN_FILTER) {
		if (conf.port_filter == PORT_FILTER_INCLUDE) {
			return ( conf.ports[pi].port_bits & (1 << port)) != 0;
		}
		if (conf.port_filter == PORT_FILTER_EXCLUDE) {
			return ( conf.ports[pi].port_bits & (1 << port)) == 0;
		}
	}
	return (conf.port_filter == PORT_FILTER_EXCLUDE);
}

static void interfaces_tree_refresh()
{
        struct rbt new_interfaces_tree;
        char ca_names[MAX_CA_NAMES][UMAD_CA_NAME_LEN];
        int num_ca_names;
	int i;

	rbt_init(&new_interfaces_tree, string_comparator);

        num_ca_names = umad_get_cas_names(ca_names, MAX_CA_NAMES);
        if (num_ca_names < 0) {
                return ;
	}

        for (i = 0; i < num_ca_names; i++) {
		umad_ca_t ca;
		int j, cnt;

		if (!collect_ca(ca_names[i])) {
			continue;
		}
		umad_get_ca(ca_names[i], &ca);
		for (j = 0, cnt = 0; j < UMAD_CA_MAX_PORTS && cnt < ca.numports; j++) {
			char name_and_port[UMAD_CA_NAME_LEN+128];
			struct rbn *rbn;
			struct interface_data *data;
                        umad_port_t *port = ca.ports[j];

			if (port == NULL)
				continue;
			else
				cnt++;

			if (!collect_ca_port(ca_names[i], port->portnum)) {
				continue;
			}
			if (port->state != PORT_STATE_ACTIVE) {
                                ovis_log(mylog, OVIS_LDEBUG, "metric_tree_refresh() skipping non-active ca %s port %d\n",
                                       port->ca_name, port->portnum);
				continue;
			}
                        /* There are at least two known link_layer types:
                           InfiniBand, Ethernet. In particular, a RoCE implmentation,
                           the "Broadcom NetXtreme-C/E RoCE Driver HCA", reports
                           as having a link_layer of "Ethernet". mdc_rpc_open_port()
                           will fail for those ports. Thus we skip anything that is
                           not using link_layer "InfiniBand". */
                        if (port->link_layer != NULL &&
                            strncmp(port->link_layer, "InfiniBand", 10) != 0) {
                                ovis_log(mylog, OVIS_LDEBUG, "metric_tree_refresh() skipping ca %s port %d link_layer \"%s\" (link_layer is not \"InfiniBand\")\n",
                                       port->ca_name, port->portnum, port->link_layer);
                                continue;
                        }

			snprintf(name_and_port, sizeof(name_and_port), "%s.%d",
				 port->ca_name,
				 port->portnum);
			rbn = rbt_find(&interfaces_tree, name_and_port);
			if (rbn) {
				data = container_of(rbn, struct interface_data,
						    interface_rbn);
				rbt_del(&interfaces_tree, &data->interface_rbn);
			} else {
				data = interface_create(port->ca_name,
							port->portnum,
							port->base_lid);
			}
			if (data == NULL)
				continue;
			rbt_ins(&new_interfaces_tree, &data->interface_rbn);
		}
		umad_release_ca(&ca);
        }

        /* destroy any infiniband data remaining in the global interfaces_tree
	   since we did not see their associated directories this time around */
        interfaces_tree_destroy();

        /* copy the new_interfaces_tree into place over the global interfaces_tree */
        memcpy(&interfaces_tree, &new_interfaces_tree, sizeof(struct rbt));

        return;
}

/* Utility function for updating a single metric in a metric set. */
static
inline void update_metric(struct interface_data *data, ldms_mval_t record_instance,
                          int metric, uint64_t new_v, double dt)
{
	uint64_t old_v = ldms_record_get_u64(record_instance,
					     mindex.counters[metric]);
	if (!data->ext)
		new_v += old_v;
	ldms_record_set_u64(record_instance, mindex.counters[metric], new_v);
	if (conf.use_rate_metrics) {
		ldms_record_set_double(record_instance,
				       mindex.rates[metric],
				       ((dt > 0 && new_v >= old_v && data->repeat) ?
					((double)(new_v - old_v)) / dt : -1.0));
	}
}

static int metrics_sample(struct interface_data *data, ldms_mval_t record_instance,
                         double dt)
{
	void *p;
	int rc;
	uint64_t v;
	int i, j;
	uint8_t rcvbuf[BUFSIZ];

	p = pma_query_via(rcvbuf, &data->portid, data->port, 0,
			IB_GSI_PORT_COUNTERS, data->srcport);
	if (p == NULL) {
		rc = errno;
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": Error querying %s, errno: %d\n",
				data->ca_name, rc);
		return rc;
	}

        ldms_record_array_set_str(record_instance, mindex.ca_name, data->ca_name);
        ldms_record_set_u32(record_instance, mindex.port, data->port);

	/* 1st part: the data that only exist in the non-ext */
	for (i = SCIB_PC_FIRST; i < IB_PC_XMT_BYTES_F; i++) {
		v = 0;
		mad_decode_field(rcvbuf, i, &v);
		j = scib_idx[i];
		update_metric(data, record_instance, j, v, dt);
	}
	v = 0;
	mad_decode_field(rcvbuf, IB_PC_XMT_WAIT_F, &v);
	j = scib_idx[IB_PC_XMT_WAIT_F];
	update_metric(data, record_instance, j, v, dt);

	/* 2nd part: the shared and the ext part */
	if (!data->ext) {
		/* non-ext: update only the shared part */
		for (i = IB_PC_XMT_BYTES_F; i < IB_PC_XMT_WAIT_F; i++) {
			mad_decode_field(rcvbuf, i, &v);
			j = scib_idx[i];
			update_metric(data, record_instance, j, v, dt);
		}
		/* and reset the counters */
		performance_reset_via(rcvbuf, &data->portid, data->port,
				0xFFFF, 0, IB_GSI_PORT_COUNTERS, data->srcport);
		return 0;
	}

	/* for ext: update the shared part and the ext-only part */
	p = pma_query_via(rcvbuf, &data->portid, data->port, 0,
			IB_GSI_PORT_COUNTERS_EXT, data->srcport);
	if (!p) {
		rc = errno;
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": Error extended querying %s, "
				"errno: %d\n", data->ca_name, rc);
		return rc;
	}
	for (i = SCIB_PC_EXT_FIRST; i < SCIB_PC_EXT_LAST; i++) {
		v = 0;
		mad_decode_field(rcvbuf, i, &v);
		j = scib_idx[i];
		update_metric(data, record_instance, j, v, dt);
	}

	data->repeat = 1;
	return 0;
}

static void resize_metric_set()
{
        size_t previous_heap_size;

        previous_heap_size = ldms_set_heap_size_get(sampler_base->set);
        base_set_delete(sampler_base);
        base_set_new_heap(sampler_base, previous_heap_size * 2);
        if (sampler_base->set == NULL) {
                ovis_log(mylog, OVIS_LERROR,
                          "Failed to resize metric set heap: %d\n", errno);
        }
}

static int interfaces_tree_sample()
{
	static struct timeval tv_prev;

        struct rbn *rbn;
	struct timeval tv_now;
	struct timeval tv_diff;
	double dt;
        ldms_mval_t list_handle;
        int rc, rerr = 0;

	gettimeofday(&tv_now, 0);
	timersub(&tv_now, &tv_prev, &tv_diff);
	dt = (double)tv_diff.tv_sec + tv_diff.tv_usec / 1.0e06;

        base_sample_begin(sampler_base);

        list_handle = ldms_metric_get(sampler_base->set, mindex.record_list);
        ldms_list_purge(sampler_base->set, list_handle);

        /* walk tree of known infiniband ports */
        RBT_FOREACH(rbn, &interfaces_tree) {
                struct interface_data *data;
                ldms_mval_t record_instance;

                record_instance = ldms_record_alloc(sampler_base->set, mindex.record_definition);
                if (record_instance == NULL) {
                        ovis_log(mylog, OVIS_LWARNING, "ldms_record_alloc() failed. Continuing with current data.\n");
                        resize_metric_set();
                        break; /* continue with the data size we have now */
                }
                rc = ldms_list_append_record(sampler_base->set, list_handle, record_instance);
		if (rc) {
                        ovis_log(mylog, OVIS_LERROR, "ldms_list_append_record) failed; logic problem. Stopping sampler.\n");
			rerr = rc; /* report last error seen at end */
		}

                data = container_of(rbn, struct interface_data, interface_rbn);
		metrics_sample(data, record_instance, dt);

        }
        base_sample_end(sampler_base);

	memcpy(&tv_prev, &tv_now, sizeof(tv_prev));
	return rerr;
}

static void reinit_ports()
{
	int i;
	conf.port_filter = PORT_FILTER_NONE;
	for (i = 0; i < MAX_CA_NAMES; i++) {
		conf.ports[i].ca_name[0] = '\0';
		conf.ports[i].port_bits = 0;
	}
}

static void dump_port_filters()
{
	int i;
	ovis_log(mylog, OVIS_LDEBUG, SAMP ": dump_port_filters: filt=%s\n",
		(conf.port_filter == PORT_FILTER_NONE ? "NONE" : (
			conf.port_filter == PORT_FILTER_INCLUDE ?
				"INCLUDE" : "EXCLUDE")));
	for (i = 0 ; i < MAX_CA_NAMES; i++) {
		if (conf.ports[i].ca_name[0] == '\0')
			break;
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": dpf: %s : 0x%lx\n",
			conf.ports[i].ca_name, conf.ports[i].port_bits);
	}
}

static int parse_port_filters(const char *val)
{
	int k = 0;
	int num_ca = 0;
	if (!val) {
		return 0;
	}
	unsigned long num;
	size_t len = strlen(val);
	char s[len+1];
	strcpy(s, val);
	char *pch, *saveptr;
	pch = strtok_r(s, ",", &saveptr);
	while (pch != NULL){
		char *dot = strchr(pch, '.');
		num = 0;
		if (dot) {
			char *end = NULL;
			dot[0] = '\0';
			dot++;
			errno = 0;
			num = strtoul(dot, &end, 10);
			if (*end != '\0' || errno == ERANGE) {
				ovis_log(mylog, OVIS_LERROR, SAMP": config: "
					"%s port invalid: %s.\n",
					val, dot);
				return 1;
			}
			if (num > 63) {
				ovis_log(mylog, OVIS_LERROR, SAMP": config: "
					"%s port > 63: %lu.\n",
					val, num);
				return 1;
			}
		}
		for (k = 0; k < num_ca; k++) {
			if (strcmp(conf.ports[k].ca_name, pch) == 0) {
				conf.ports[k].port_bits |= (1 << num);
				break;
			}
		}
		if (k == num_ca) {
			if (k > MAX_CA_NAMES) {
				ovis_log(mylog, OVIS_LERROR, SAMP": config: "
					"too many CA in %s\n", val);
				return 1;
			}
			strcpy(conf.ports[k].ca_name, pch);
			conf.ports[k].port_bits |= (1 << num);
			num_ca++;
		}
		if (num) {
			ovis_log(mylog, OVIS_LDEBUG, SAMP ": parsed %s port %d\n", pch,
				(int)num);
		} else {
			ovis_log(mylog, OVIS_LDEBUG, SAMP ": parsed %s all ports\n",
				pch);
			conf.ports[k].port_bits = UINT64_MAX;
		}
		pch = strtok_r(NULL, ",", &saveptr);
	}
	dump_port_filters();
	return 0;
}

/* strip leading and trailing whitespace */
static void strip_whitespace(char **start)
{
        /* strip leading whitespace */
        while (isspace(**start)) {
                (*start)++;
        }

        /* strip trailing whitespace */
        char * last;
        last = *start + strlen(*start) - 1;
        while (last > *start) {
                if (isspace(last[0])) {
                        last--;
                } else {
                        break;
                }
        }
        last[1] = '\0';
}


static int config(ldmsd_plug_handle_t handle,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        char *value;
        int rc;

        ovis_log(mylog, OVIS_LDEBUG, "config() called\n");

        sampler_base = base_config(avl, ldmsd_plug_cfg_name_get(handle), "ibmad", mylog);
	value = av_value(avl, "rate");
	if (value != NULL && value[0] == '0') {
		conf.use_rate_metrics = false;
	}
	reinit_ports();
	const char *include = av_value(avl, "include");
	const char *exclude = av_value(avl, "exclude");
	if (include && exclude) {
                ovis_log(mylog, OVIS_LERROR, "config: specify either include or exclude option but not both.\n");
		rc = -1;
                goto err;
	}
	const char *val = NULL;
	if (include) {
		val = include;
		conf.port_filter = PORT_FILTER_INCLUDE;
	}
	if (exclude) {
		val = exclude;
		conf.port_filter = PORT_FILTER_EXCLUDE;
	}

        rc = parse_port_filters(val);
        if (rc != 0) {
                goto err;
        }

        value = av_value(avl, "refresh_interval_sec");
        if (value != NULL) {
                char *end;
                long val;

                strip_whitespace(&value);
                val = strtol(value, &end, 10);
                if (*end != '\0') {
                        ovis_log(mylog, OVIS_LERROR, "refresh_interval must be a decimal number\n");
                        rc = EINVAL;
                        return rc;
                }
                conf.refresh_interval = (time_t)val;
        } else {
                conf.refresh_interval = (time_t)600;
        }

        rc = ibmad_initialize();
        if (rc != 0) {
                goto err;
        }

        return 0;
err:
        base_del(sampler_base);
        sampler_base = NULL;
        return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
        static time_t last_refresh = 0;
        time_t current_time;

        ovis_log(mylog, OVIS_LDEBUG, "sample() called\n");

        current_time = time(NULL);
        if (current_time >= last_refresh + conf.refresh_interval) {
                interfaces_tree_refresh();
                last_refresh = current_time;
        }
        return interfaces_tree_sample();
}

static const char *usage(ldmsd_plug_handle_t handle)
{
        ovis_log(mylog, OVIS_LDEBUG, "usage() called\n");
	return  "config name=" SAMP " " BASE_CONFIG_SYNOPSIS
                BASE_CONFIG_DESC
                ;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
        rbt_init(&interfaces_tree, string_comparator);
	conf.use_rate_metrics = true;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
        ovis_log(mylog, OVIS_LDEBUG, "term() called\n");
        interfaces_tree_destroy();
        base_set_delete(sampler_base);
        base_del(sampler_base);
        sampler_base = NULL;
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
