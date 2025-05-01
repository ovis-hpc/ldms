/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011,2015-2017 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2011,2015-2017 Sandia Corporation. All rights reserved.
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
 * \file switchx.c
 * \brief Mellanox SwitchX data provider
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
#include "ldms.h"
#include "ldmsd.h"

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include <iba/ib_types.h>

#include "ibdiag_common.h"

static ovis_log_t mylog;

ldms_schema_t		sx_schema;
unsigned int		sx_off;
ib_portid_t		sx_id;
struct ibmad_port 	*sx_src;

#define SX_NUM_PORTS	37

/*
 * There is a set and metric table for each port.
 */
struct sx_ldms_set {
	ldms_set_t	sx_set;
};

/* port numbering starts at 1 */
static struct sx_ldms_set	sx_ldms_sets[SX_NUM_PORTS+1];


struct sx_counters {
	char	*counter_name;
	int	counter_size;
};

/*
 * Port stats
 */
static const struct sx_counters gsi_port_counters[] =
{
	{"SymbolErrorCounter", LDMS_V_U16},
	{"LinkErrorRecoveryCounter", LDMS_V_U8},
	{"LinkDownedCounter", LDMS_V_U8},
	{"PortRcvErrors", LDMS_V_U16},
	{"PortRcvRemotePhysicalErrors", LDMS_V_U16},
	{"PortRcvSwitchRelayErrors", LDMS_V_U16},
	{"PortXmitDiscards", LDMS_V_U16},
	{"PortXmitConstraintErrors", LDMS_V_U8},
	{"PortRcvConstraintErrors", LDMS_V_U8},
	{"CounterSelect2", LDMS_V_U8},
	{"LocalLinkIntegrityErrors", LDMS_V_U8},
	{"ExcessiveBufferOverrunErrors", LDMS_V_U8},
	{"VL15Dropped", LDMS_V_U16},
	{"PortXmitData", LDMS_V_U32},
	{"PortRcvData", LDMS_V_U32},
	{"PortXmitPkts", LDMS_V_U32},
	{"PortRcvPkts", LDMS_V_U32},
	{"PortXmitWait", LDMS_V_U32},
};

/*
 * Extended (64 bit) port stats
 */
static const struct sx_counters gsi_port_counters_ext[] =
{
	{"PortXmitDataExt", LDMS_V_U64},
	{"PortRcvDataExt", LDMS_V_U64},
	{"PortXmitPktsExt", LDMS_V_U64},
	{"PortRcvPktsExt", LDMS_V_U64},
	{"PortUnicastXmitPktsExt", LDMS_V_U64},
	{"PortUnicastRcvPktsExt", LDMS_V_U64},
	{"PortMulticastXmitPktsExt", LDMS_V_U64},
	{"PortMulticastRcvPktsExt", LDMS_V_U64},
};

/*
 * Port state and info counters
 */
static const struct sx_counters portinfo_counters[] =
{
        {"Mkey", LDMS_V_U64},
        {"GidPrefix", LDMS_V_U64},
        {"Lid", LDMS_V_U16},
        {"SMLid", LDMS_V_U16},
        {"CapMask", LDMS_V_U32},
        {"DiagCode", LDMS_V_U16},
        {"MkeyLeasePeriod", LDMS_V_U16},
        {"LocalPort", LDMS_V_U8},
        {"LinkWidthEnabled", LDMS_V_U8},
        {"LinkWidthSupported", LDMS_V_U8},
        {"LinkWidthActive", LDMS_V_U8},
        {"LinkSpeedSupported", LDMS_V_U8},
        {"LinkState", LDMS_V_U8},
        {"PhysLinkState", LDMS_V_U8},
        {"LinkDownDefState", LDMS_V_U8},
        {"ProtectBits", LDMS_V_U8},
        {"LMC", LDMS_V_U8},
        {"LinkSpeedActive", LDMS_V_U8},
        {"LinkSpeedEnabled", LDMS_V_U8},
        {"NeighborMTU", LDMS_V_U8},
        {"SMSL", LDMS_V_U8},
        {"VLCap", LDMS_V_U8},
        {"InitType", LDMS_V_U8},
        {"VLHighLimit", LDMS_V_U8},
        {"VLArbHighCap", LDMS_V_U8},
        {"VLArbLowCap", LDMS_V_U8},
        {"InitReply", LDMS_V_U8},
        {"MtuCap", LDMS_V_U8},
        {"VLStallCount", LDMS_V_U8},
        {"HoqLife", LDMS_V_U8},
        {"OperVLs", LDMS_V_U8},
        {"PartEnforceInb", LDMS_V_U8},
        {"PartEnforceOutb", LDMS_V_U8},
        {"FilterRawInb", LDMS_V_U8},
        {"FilterRawOutb", LDMS_V_U8},
        {"MkeyViolations", LDMS_V_U16},
        {"PkeyViolations", LDMS_V_U16},
        {"QkeyViolations", LDMS_V_U16},
        {"GuidCap", LDMS_V_U8},
        {"ClientReregister", LDMS_V_U8},
        {"McastPkeyTrapSuppressionEnabled", LDMS_V_U8},
        {"SubnetTimeout", LDMS_V_U8},
        {"RespTimeVal", LDMS_V_U8},
        {"LocalPhysErr", LDMS_V_U8},
        {"OverrunErr", LDMS_V_U8},
        {"MaxCreditHint", LDMS_V_U16},
        {"RoundTrip", LDMS_V_U32},
};


/*
 * Retrieve the specified stats
 */
static int sx_get_perf_stats(enum GSI_ATTR_ID attr,struct sx_counters *counters,
			 int port, enum MAD_FIELDS start, enum MAD_FIELDS end)
{
	int rc;
	int i = 0;
	enum MAD_FIELDS fld;
	union ldms_value v;
	struct sx_ldms_set *sx_set = &sx_ldms_sets[port];
	static u_char data[4096];

	bzero(data, sizeof(data));

	/*
	 * get attribute data
	 */
	switch(attr) {
	case IB_GSI_PORT_COUNTERS:
	case IB_GSI_PORT_COUNTERS_EXT:
		rc = pma_query_via(data, &sx_id, port, 0, attr, sx_src);
		break;
	case IB_ATTR_PORT_INFO:
		rc = smp_query_via(data, &sx_id, attr, port, 0, sx_src);
		break;
	default:
		return EINVAL;
	}

	if (rc == 0) {
		return EIO;
	}

	/*
	 * Update the LDMS metric for each field
	 */
	for (fld = start; fld < end; fld++) {
		switch(counters[i].counter_size) {
			case LDMS_V_U8:
				v.v_u8 = (u_char)mad_get_field(data, 0, fld);
				break;
			case LDMS_V_U16:
				v.v_u16 = (u_short)mad_get_field(data, 0, fld);
				break;
			case LDMS_V_U32:
				v.v_u32 = mad_get_field(data, 0, fld);
				break;
			case LDMS_V_U64:
				v.v_u64 = mad_get_field64(data, 0, fld);
				break;
		}
		ldms_metric_set(sx_set->sx_set, sx_off + i, &v);
		i++;
	}

	return 0;
}

/*
 * Initialize MAD
 */
static int sx_mad_init()
{
	int rc;
	int lport = 0;
	u_char info[IB_SMP_DATA_SIZE];
	int classes[4] = { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS,
			   IB_PERFORMANCE_CLASS };

	sx_src = mad_rpc_open_port(ibd_ca, ibd_ca_port, classes, 4);
	if (sx_src == NULL) {
		return EIO;
	}
	smp_mkey_set(sx_src, ibd_mkey);

	rc = resolve_self(ibd_ca, ibd_ca_port, &sx_id, &lport, NULL);
	if (rc < 0) {
		return rc;
	}

	bzero(info, sizeof(info));
	rc = smp_query_via(info, &sx_id, IB_ATTR_PORT_INFO, 0, 0, sx_src);
	if (rc < 0) {
		return rc;
	}
	mad_decode_field(info, IB_PORT_LID_F, &sx_id.lid);

	return 0;
}

/*
 * Add a SX metric to the LDMS schema.
 */
static int sx_add_metric(ldms_schema_t schema,
			 const struct sx_counters counters[], int count)
{
	int i;
	int rc;
	const char *metric;

	/*
	 * iterate through the SDK stats and add each one.
	 */
	for (i = 0; i < count; i++) {
		metric = counters[i].counter_name;

		rc = ldms_schema_metric_add(schema, metric, counters[i].counter_size);
		if (rc < 0) {
			return ENOMEM;
		}
	}
	return 0;
}

static int sx_create_metric_set(const char *path)
{
	int rc;
	ldms_set_t set;
	char *portstr;
	int port;
	char *end;
	int count;

	/*
	 * extract the port number from the path
	 */
	portstr = strrchr(path, '/');
	if (portstr == NULL) {
		printf("sx_create_metric_set: invalid set path\n");
		return EINVAL;
	}

	port = strtol(portstr+1, &end, 0);
	if (*end != '\0') {
		printf("sx_create_metric_set: invalid port number\n");
		return EINVAL;
	}

	/*
	 * if the schemas hasn't been created, create it.
	 */
	if (sx_schema == NULL) {
		sx_schema = ldms_schema_new("switchx");
		if (sx_schema == NULL) {
			return ENOMEM;
		}

		/*
		 * Process the stats to define all the metrics.
		 */

		count = sizeof(gsi_port_counters) / sizeof(struct sx_counters);
		rc = sx_add_metric(sx_schema, gsi_port_counters, count);
		if (rc != 0) {
			goto err;
		}

		count = sizeof(gsi_port_counters_ext) /
				sizeof(struct sx_counters);
		rc = sx_add_metric(sx_schema, gsi_port_counters_ext, count);
		if (rc != 0) {
			goto err;
		}

		count = sizeof(portinfo_counters) / sizeof(struct sx_counters);
		rc = sx_add_metric(sx_schema, portinfo_counters, count);
		if (rc != 0) {
			goto err;
		}
	}


	/* Create the metric set */
	set = ldms_set_new(path, sx_schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	sx_ldms_sets[port].sx_set = set;

	return 0;

 err:
	return rc;
}

/**
 * \brief Configuration
 *
 * sx_config name=switchx set=<setname>
 *     setname     The set name.
 */
static int sx_config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc = EINVAL;
	static int init = 1;

	/*
	 * If the switchx API is not initialized, do it now.
	 */
	if (init == 1) {
		init = 0;
		rc = sx_mad_init();
		if (rc != 0) {
			printf("sx_config: unable to init MAD\n");
			return rc;
		}
	}

	value = av_value(avl, "set");
	if (value)
		sx_create_metric_set(value);
	return 0;
}

static int sx_sample_set(int port)
{
	int rc;
	struct sx_ldms_set *sx_set = &sx_ldms_sets[port];

	/*
	 * check if there is a set for this port. if not, we're done.
	 */
	if (!sx_set->sx_set) {
		return EEXIST;
	}

	ldms_transaction_begin(sx_set->sx_set);

	/*
	 * port stats
	 */
	sx_off = 0;
	rc = sx_get_perf_stats(IB_GSI_PORT_COUNTERS, gsi_port_counters, port,
				IB_PC_ERR_SYM_F, IB_PC_LAST_F);

	/*
	 * port stats ext
	 */
	sx_off += IB_PC_LAST_F - IB_PC_ERR_SYM_F;
	rc = sx_get_perf_stats(IB_GSI_PORT_COUNTERS_EXT, gsi_port_counters_ext,
				port,
				IB_PC_EXT_XMT_BYTES_F, IB_PC_EXT_LAST_F);

	/*
	 * port info
	 */
	sx_off += IB_PC_EXT_LAST_F - IB_PC_EXT_XMT_BYTES_F;
	rc = sx_get_perf_stats(IB_ATTR_PORT_INFO, portinfo_counters,
				port,
				IB_PORT_FIRST_F, IB_PORT_LAST_F);
err:
	ldms_transaction_end(sx_set->sx_set);
	return rc;
}

static int sx_sample(ldmsd_plug_handle_t handle)
{
	int port;
	int rc;

	/*
	 * sample each port that exists
	 */
	for (port = 1; port < SX_NUM_PORTS; port++) {
		rc = sx_sample_set(port);
		if (rc != 0) {
			if (rc != EEXIST) {
				ovis_log(mylog, OVIS_LERROR, "sx_sample: failed sampling"
						" port %d\n", port);
				return rc;
			}
		}
	}
	return 0;
}

static void sx_term(ldmsd_plug_handle_t handle)
{
	int port;

	mad_rpc_close_port(sx_src);

	for (port = 1; port < SX_NUM_PORTS; port++) {
		if (sx_ldms_sets[port].sx_set)
			ldms_set_delete(sx_ldms_sets[port].sx_set);
		sx_ldms_sets[port].sx_set = NULL;
	}

	if (sx_schema != NULL) {
		ldms_schema_delete(sx_schema);
		sx_schema = NULL;
	}
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=switchx set=<setname>\n"
		"    setname     The set name.\n";
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = sx_term,
		.config = sx_config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sx_sample,
};
