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
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdbool.h>
#include <unistd.h>
#include "ldms.h"
#include "ldmsd.h"
#include <mad.h>
#include <umad.h>
#include <iba/ib_types.h>

#include "config.h"
#include "jobid_helper.h"
#include "sampler_base.h" /* for auth mix-in */

#define _GNU_SOURCE

#define SAMP "ibmad_sampler"

static ovis_log_t mylog;

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
        char *schema_name;
	char producer_name[LDMS_PRODUCER_NAME_MAX];
	bool use_rate_metrics;
	int port_filter;
	struct port_name ports[MAX_CA_NAMES];
} conf;

struct base_auth auth;

/* red-black tree root for infiniband port metrics */
static struct rbt metrics_tree;

struct metric_data {
	char *instance;
        struct rbn metrics_node;
	ldms_set_t metric_set; /* a pointer */

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

static ldms_schema_t ibmad_schema;
static int metric_port_index;
static int metric_ca_name_index;
static int metric_counter_indices[ARRAY_SIZE(all_metric_names)];
static int metric_rate_indices[ARRAY_SIZE(all_metric_names)];

static int ibmad_schema_create()
{
        ldms_schema_t sch;
	char metric_name[128];
        int rc;
        int i;

        ovis_log(mylog, OVIS_LDEBUG, " ibmad_schema_create()\n");
        sch = ldms_schema_new(conf.schema_name);
        if (sch == NULL)
                goto err1;
        rc = jobid_helper_schema_add(sch);
	if (rc < 0)
		goto err2;
        rc = ldms_schema_meta_array_add(sch, "ca_name", LDMS_V_CHAR_ARRAY, 64);
        if (rc < 0)
                goto err2;
        metric_ca_name_index = rc;
        rc = ldms_schema_meta_add(sch, "port", LDMS_V_U32);
        if (rc < 0)
                goto err2;
        metric_port_index = rc;

	for (i = 0; i < ARRAY_SIZE(all_metric_names); i++) {
		/* add ibmad counter metrics */
		snprintf(metric_name, 128, "%s",
			 all_metric_names[i]);
		metric_counter_indices[i] =
			ldms_schema_metric_add(sch, metric_name, LDMS_V_U64);

		if (conf.use_rate_metrics) {
			/* add ibmad rate metrics */
			snprintf(metric_name, 128, "%s.rate",
				 all_metric_names[i]);
			metric_rate_indices[i] =
				ldms_schema_metric_add(sch, metric_name, LDMS_V_D64);
		}
	}

	ibmad_schema = sch;

        return 0;
err2:
        ldms_schema_delete(sch);
err1:
        ovis_log(mylog, OVIS_LERROR, " schema creation failed\n");
        return -1;
}

static void ibmad_schema_destroy()
{
        ldms_schema_delete(ibmad_schema);
        ibmad_schema = NULL;
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
static int _port_open(struct metric_data *data, const char *ca_name, unsigned base_lid)
{
	int mgmt_classes[3] = {IB_SMI_CLASS, IB_SA_CLASS, IB_PERFORMANCE_CLASS};
	void *p;
	uint16_t cap;
	uint8_t rcvbuf[BUFSIZ];

	/* open source port for sending MAD messages */
	data->srcport = mad_rpc_open_port((char *)ca_name, data->port, mgmt_classes, 3);
	if (!data->srcport) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": ERROR: Cannot open CA:%s port:%d,"
				" ERRNO: %d\n", ca_name, data->port,
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
				"  %d\n", ca_name, data->port, errno);
		mad_rpc_close_port(data->srcport);
		return -1;
	}
	memcpy(&cap, rcvbuf + 2, sizeof(cap));
	data->ext = cap & (IB_PM_EXT_WIDTH_SUPPORTED
			| IB_PM_EXT_WIDTH_NOIETF_SUP);

	if (!data->ext) {
		ovis_log(mylog, OVIS_LDEBUG, SAMP ": WARNING: Extended query not "
			"supported for %s:%d, the sampler will reset "
			"counters every query\n", ca_name, data->port);
	}

	return 0;
}

/**
 * Close the \c port.
 *
 * This function only close IB port.
 */
static void _port_close(struct metric_data *data)
{
	if (data->srcport)
		mad_rpc_close_port(data->srcport);
	data->srcport = NULL;
}


static struct metric_data *ibmad_metric_create(const char *instance,
					      const char *ca_name, int port, unsigned base_lid)
{
        struct metric_data *data;
	int rc;

        ovis_log(mylog, OVIS_LDEBUG, " ibmad_metric_create() %s, base_lid=%u\n",
               instance, base_lid);
        data = calloc(1, sizeof(*data));
        if (data == NULL)
                goto out1;
	data->port = port;
        data->instance = strdup(instance);
        if (data->instance == NULL)
                goto out2;

        data->metric_set = ldms_set_new(instance, ibmad_schema);
        if (data->metric_set == NULL)
                goto out3;

	ldms_metric_array_set_str(data->metric_set,  metric_ca_name_index, ca_name);
	ldms_metric_set_u32(data->metric_set, metric_port_index, port);

	rc = _port_open(data, ca_name, base_lid);
	if (rc != 0) {
		goto out4;
	}

	base_auth_set(&auth, data->metric_set);
	ldms_set_producer_name_set(data->metric_set, conf.producer_name);
        ldms_set_publish(data->metric_set);
        ldmsd_set_register(data->metric_set, SAMP);
        rbn_init(&data->metrics_node, data->instance);

        return data;

out4:
        ldms_set_delete(data->metric_set);
out3:
        free(data->instance);
out2:
        free(data);
out1:
        return NULL;
}

static void ibmad_metric_destroy(struct metric_data *data)
{
        ovis_log(mylog, OVIS_LDEBUG, " ibmad_destroy() %s\n", data->instance);
	ldmsd_set_deregister(data->instance, SAMP);
        ldms_set_unpublish(data->metric_set);
        ldms_set_delete(data->metric_set);
	_port_close(data);
        free(data->instance);
	free(data);
}

static void metrics_tree_destroy()
{
        struct rbn *rbn;
        struct metric_data *data;

        while (!rbt_empty(&metrics_tree)) {
                rbn = rbt_min(&metrics_tree);
                data = container_of(rbn, struct metric_data,
                                   metrics_node);
                rbt_del(&metrics_tree, rbn);
                ibmad_metric_destroy(data);
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

static void metrics_tree_refresh()
{
        struct rbt new_metrics_tree;
        char ca_names[MAX_CA_NAMES][UMAD_CA_NAME_LEN];
        int num_ca_names;
	int i;

	rbt_init(&new_metrics_tree, string_comparator);

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
			char instance[UMAD_CA_NAME_LEN+128];
			struct rbn *rbn;
			struct metric_data *data;

			if (ca.ports[j] == NULL)
				continue;
			else
				cnt++;

			if (!collect_ca_port(ca_names[i], ca.ports[j]->portnum)) {
				continue;
			}
			if (ca.ports[j]->state != PORT_STATE_ACTIVE) {
				ovis_log(mylog, OVIS_LDEBUG, " metric_tree_refresh() skipping non-active ca %s port %d\n",
				       ca.ports[j]->ca_name, ca.ports[j]->portnum);
				continue;
			}

			snprintf(instance, sizeof(instance), "%s/%s.%d",
				 conf.producer_name,
				 ca.ports[j]->ca_name,
				 ca.ports[j]->portnum);
			rbn = rbt_find(&metrics_tree, instance);
			if (rbn) {
				data = container_of(rbn, struct metric_data,
						    metrics_node);
				rbt_del(&metrics_tree, &data->metrics_node);
			} else {
				data = ibmad_metric_create(instance,
							   ca.ports[j]->ca_name,
							   ca.ports[j]->portnum,
							   ca.ports[j]->base_lid);
			}
			if (data == NULL)
				continue;
			rbt_ins(&new_metrics_tree, &data->metrics_node);
		}
		umad_release_ca(&ca);
        }

        /* destroy any infiniband data remaining in the global metrics_tree
	   since we did not see their associated directories this time around */
        metrics_tree_destroy();

        /* copy the new_metrics_tree into place over the global metrics_tree */
        memcpy(&metrics_tree, &new_metrics_tree, sizeof(struct rbt));

        return;
}

/* Utility function for updating a single metric in a metric set. */
static
inline void update_metric(struct metric_data *data, int metric, uint64_t new_v,
			double dt)
{
	uint64_t old_v = ldms_metric_get_u64(data->metric_set,
					     metric_counter_indices[metric]);
	if (!data->ext)
		new_v += old_v;
	ldms_metric_set_u64(data->metric_set, metric_counter_indices[metric], new_v);
	if (conf.use_rate_metrics) {
		ldms_metric_set_double(data->metric_set,
				       metric_rate_indices[metric],
				       ((dt > 0 && new_v >= old_v && data->repeat) ?
					((double)(new_v - old_v)) / dt : -1.0));
	}
}

static int metric_sample(struct metric_data *data, double dt)
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
				data->instance, rc);
		return rc;
	}

	/* 1st part: the data that only exist in the non-ext */
	for (i = SCIB_PC_FIRST; i < IB_PC_XMT_BYTES_F; i++) {
		v = 0;
		mad_decode_field(rcvbuf, i, &v);
		j = scib_idx[i];
		update_metric(data, j, v, dt);
	}
	v = 0;
	mad_decode_field(rcvbuf, IB_PC_XMT_WAIT_F, &v);
	j = scib_idx[IB_PC_XMT_WAIT_F];
	update_metric(data, j, v, dt);

	/* 2nd part: the shared and the ext part */
	if (!data->ext) {
		/* non-ext: update only the shared part */
		for (i = IB_PC_XMT_BYTES_F; i < IB_PC_XMT_WAIT_F; i++) {
			mad_decode_field(rcvbuf, i, &v);
			j = scib_idx[i];
			update_metric(data, j, v, dt);
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
				"errno: %d\n", data->instance, rc);
		return rc;
	}
	for (i = SCIB_PC_EXT_FIRST; i < SCIB_PC_EXT_LAST; i++) {
		v = 0;
		mad_decode_field(rcvbuf, i, &v);
		j = scib_idx[i];
		update_metric(data, j, v, dt);
	}

	data->repeat = 1;
	return 0;
}

static void metrics_tree_sample()
{
	static struct timeval tv_prev;

        struct rbn *rbn;
	struct timeval tv_now;
	struct timeval tv_diff;
	double dt;

	gettimeofday(&tv_now, 0);
	timersub(&tv_now, &tv_prev, &tv_diff);
	dt = (double)tv_diff.tv_sec + tv_diff.tv_usec / 1.0e06;

        /* walk tree of known infiniband ports */
        RBT_FOREACH(rbn, &metrics_tree) {
                struct metric_data *data;

                data = container_of(rbn, struct metric_data, metrics_node);
		ldms_transaction_begin(data->metric_set);
		metric_sample(data, dt);
		ldms_transaction_end(data->metric_set);
        }

	memcpy(&tv_prev, &tv_now, sizeof(tv_prev));
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
				ovis_log(mylog, OVIS_LERROR, "config: "
					"%s port invalid: %s.\n",
					val, dot);
				return 1;
			}
			if (num > 63) {
				ovis_log(mylog, OVIS_LERROR, "config: "
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
				ovis_log(mylog, OVIS_LERROR, "config: "
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

static int config(ldmsd_plug_handle_t handle,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        char *value;

        ovis_log(mylog, OVIS_LDEBUG, " config() called\n");

	int jc = jobid_helper_config(avl);
        if (jc) {
		ovis_log(mylog, OVIS_LERROR, "set name for job_set="
			" is too long.\n");
		return jc;
	}
	base_auth_parse(avl, &auth, mylog);

        value = av_value(avl, "schema");
        if (value != NULL) {
		free(conf.schema_name);
                conf.schema_name = strdup(value);
	}
        if (conf.schema_name == NULL) {
                ovis_log(mylog, OVIS_LERROR, "config() strdup schema failed: %d", errno);
		return 1;
        }
        value = av_value(avl, "producer");
        if (value != NULL) {
                strcpy(conf.producer_name, value);
	} else {
		gethostname(conf.producer_name, sizeof(conf.producer_name));
	}
        if (conf.producer_name[0] == '\0') {
                ovis_log(mylog, OVIS_LERROR, "config() producer unset\n");
		return 1;
        }
	value = av_value(avl, "rate");
	if (value != NULL && value[0] == '0') {
		conf.use_rate_metrics = false;
	}
	reinit_ports();
	const char *include = av_value(avl, "include");
	const char *exclude = av_value(avl, "exclude");
	if (include && exclude) {
                ovis_log(mylog, OVIS_LERROR, "config: specify either include or exclude option but not both.\n");
		return 1;
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
	return parse_port_filters(val);
}

static int sample(ldmsd_plug_handle_t handle)
{
        ovis_log(mylog, OVIS_LDEBUG, "sample() called\n");
        if (ibmad_schema == NULL) {
                if (ibmad_schema_create() < 0) {
                        ovis_log(mylog, OVIS_LERROR, "schema create failed\n");
                        return ENOMEM;
                }
        }

        metrics_tree_refresh();
        metrics_tree_sample();

        return 0;
}

static void term(ldmsd_plug_handle_t handle)
{
	ovis_log(mylog, OVIS_LDEBUG, "term() called\n");
	metrics_tree_destroy();
	ibmad_schema_destroy();
	free(conf.schema_name);
}

static const char *usage(ldmsd_plug_handle_t handle)
{
        ovis_log(mylog, OVIS_LDEBUG, "usage() called\n");
	return  "config name=" SAMP;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
	rbt_init(&metrics_tree, string_comparator);
	conf.schema_name = strdup("ibmad");
	conf.use_rate_metrics = true;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};
