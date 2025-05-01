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

#include <complib/sx_log.h>
#include <sx/sdk/sx_status.h>
#include <sx/sdk/sx_port_id.h>
#include <sx/sdk/sx_dev.h>
#include <sx/sdk/sx_swid.h>
#include <sx/sdk/sx_api.h>
#include <sx/sdk/sx_api_init.h>
#include <sx/sdk/sx_api_port.h>

#define PNAME "switchx_eth"

static ovis_log_t mylog;

ldms_schema_t sx_schema;

sx_api_handle_t sx_handle = 0;

#define SX_NUM_PORTS	37

/*
 * There is a set and metric table for each port.
 */
struct sx_ldms_set {
	ldms_set_t	sx_set;
};

/* port numbering starts at 1 */
static struct sx_ldms_set	sx_ldms_sets[SX_NUM_PORTS+1];

sx_port_attributes_t    port_attributes_list[SX_NUM_PORTS];
u_int			port_count;
sx_device_info_t  	device_info_list[1];

static sx_verbosity_level_t LOG_VAR_NAME(__MODULE__) = SX_VERBOSITY_LEVEL_NOTICE;

void
sx_log_cb(sx_log_severity_t severity, const char *module_name, char *msg)
{
	sx_verbosity_level_t		v = 0;

	SEVERITY_LEVEL_TO_VERBOSITY_LEVEL(severity, v);
	printf("[%-20s][%s] : %s", module_name, SX_VERBOSITY_LEVEL_STR(v), msg);

	return;
}

/*
 * convert logical port number to SDK's representation
 */
int
get_sdk_port(int logport, int *sdk_port)
{
	int		i;

	if (logport > port_count) {
		*sdk_port = -1;
		return EINVAL;
	}

	for (i = 0; i < port_count; i++) {
		if (port_attributes_list[i].port_mapping.module_port == (logport - 1)) {
			*sdk_port = port_attributes_list[i].log_port;
			return 0;
		}
	}

	return EINVAL;

}

/*
 * convert SDK port number to logical
 */
int
get_logical_port(sx_port_log_id_t  sdk_port, int *label_port)
{
	int		port;

	for (port = 0; port < port_count; port++) {
		if (port_attributes_list[port].log_port == sdk_port) {
			*label_port = port_attributes_list[port].port_mapping.module_port+1;
			return 0;
		}
	}
	return EINVAL;
}

/*
 * Initialize the Mellanox SwitchX SDK and gather port information.
 */
static int sx_sdk_init()
{
	int rc;
	u_int dev_count = 1;
	int sw = 0;

	sx_log_init(TRUE, NULL, sx_log_cb);

	/* open api to SDK */
	rc = sx_api_open(sx_log_cb, &sx_handle);
	if (rc != SX_STATUS_SUCCESS) {
		SX_LOG_ERR("Failed to open SX-API (%s)\n", SX_STATUS_MSG(rc));
		return rc;
	}

	/*
	 * Only one device is supported. Get the first one and extract the
	 * number of ports from its attributes.
	 */
	rc = sx_api_port_device_list_get(sx_handle,
		device_info_list,
		&dev_count
	);

	if (SX_CHECK_FAIL(rc)) {
		printf("error getting devices for sw %d\n", sw);
		return EIO;
	}

	port_count = device_info_list[0].num_ports;

	/*
	 * Now get the port attributes.
	 */
	rc = sx_api_port_device_get(
                sx_handle,
                device_info_list[0].dev_id,
                sw,
		port_attributes_list,
                &port_count
                );

	if (SX_CHECK_FAIL(rc)) {
		printf("error getting ports for device %d\n",
			device_info_list[0].dev_id);
		return EIO;
	}

	sx_port_oper_state_t oper_state;
	sx_port_admin_state_t admin_state;
	sx_port_module_state_t module_state;
	int i;
	int port;

	for (i = 0; i < port_count; i++) {
	rc = sx_api_port_state_get(sx_handle, port_attributes_list[i].log_port,
		&oper_state, &admin_state, &module_state);
		if (oper_state == SX_PORT_OPER_STATUS_UP) {
			get_logical_port(port_attributes_list[i].log_port, &port);
			printf("port %d is up\n", port);
		}
	}

	return 0;
}

static int sx_add_metric(ldms_schema_t schema, const char *names[], int count)
{
	int i;
	int rc;
	const char *metric_name;

	/*
	 * iterate through the SDK stats and add each one.
	 */
	for (i = 0; i < count; i++) {
		metric_name = names[i];

		rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
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
	int sdk_port;

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

	rc = get_sdk_port(port, &sdk_port);
	if (rc != 0) {
		printf("sx_create_metric_set: port number not found\n");
		return EINVAL;
	}

	if (sx_schema == NULL) {
		sx_schema = ldms_create_schema(PNAME);
		if (sx_schema == NULL) {
			return ENOMEM;
		}

	/*
	 * Process the stats to define all the metrics.
	 */

	count = sizeof(sx_port_cntr_ieee_802_dot_3_t) / sizeof(sx_port_cntr_t);
	rc = sx_add_metric(sx_schema, sx_port_cntr_ieee_802_dot_3_str, count);
	if (rc != 0) {
		goto err;
	}

	count = sizeof(sx_port_cntr_rfc_2863_t) / sizeof(sx_port_cntr_t);
	rc = sx_add_metric(sx_schema, sx_port_cntr_rfc_2863_str, count);
	if (rc != 0) {
		goto err;
	}

	count = sizeof(sx_port_cntr_rfc_2819_t) / sizeof(sx_port_cntr_t);
	rc = sx_add_metric(sx_schema, sx_port_cntr_rfc_2819_str, count);
	if (rc != 0) {
		goto err;
	}

	count = sizeof(sx_port_cntr_rfc_3635_t) / sizeof(sx_port_cntr_t);
	rc = sx_add_metric(sx_schema, sx_port_cntr_rfc_3635_str, count);
	if (rc != 0) {
		goto err;
	}
	}

	/* Create the metric set */
	set = ldms_set_new(path, sx_schema);
	if (set == NULL) {
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
		rc = sx_sdk_init();
		if (rc != 0) {
			printf("sx_config: unable to init SDK\n");
			return rc;
		}
	}

	value = av_value(avl, "set");
	if (value)
		rc = sx_create_metric_set(value);

	return rc;
}

static int sx_sample_set(int log_port)
{
	int rc;
	int i;
	int metric_count;
	union ldms_value v;
	sx_port_cntr_ieee_802_dot_3_t sx_stats;
	sx_port_cntr_rfc_2863_t sx_stats_2863;
	sx_port_cntr_rfc_2819_t sx_stats_2819;
	sx_port_cntr_rfc_3635_t sx_stats_3635;
	sx_port_cntr_t *stats;
	int port;
	int off;
	struct sx_ldms_set *sx_set = &sx_ldms_sets[log_port];

	/*
	 * check if there is a set for this port. if not, we're done.
	 */
	if (!sx_set->sx_set) {
		return EEXIST;
	}

	rc = get_sdk_port(log_port, &port);
	if (SX_CHECK_FAIL(rc)) {
		return EINVAL;
	}

	off = 0;
	metric_count = 0;
	ldms_transaction_begin(sx_set->sx_set);

	/*
	 * 802.3 stats
	 */
	rc = sx_api_port_counter_ieee_802_dot_3_get(sx_handle,
					SX_ACCESS_CMD_READ, port, &sx_stats);
	if (SX_CHECK_FAIL(rc)) {
		rc = EIO;
		goto err;
	}

	metric_count = sizeof(sx_stats)/sizeof(sx_port_cntr_t);
	stats = (sx_port_cntr_t *)&sx_stats;

	for (i = 0; i < metric_count; i++) {
		v.v_u64 = stats[i];
		ldms_metric_set(sx_set->sx_set, off + i, &v);
	}

	/*
	 * RFC 2863 stats
	 */
	rc = sx_api_port_counter_rfc_2863_get(sx_handle,
				SX_ACCESS_CMD_READ, port, &sx_stats_2863);
	if (SX_CHECK_FAIL(rc)) {
		rc = EIO;
		goto err;
	}

	off += metric_count;
	metric_count = sizeof(sx_stats_2863)/sizeof(sx_port_cntr_t);
	stats = (sx_port_cntr_t *)&sx_stats_2863;

	for (i = 0; i < metric_count; i++) {
		v.v_u64 = stats[i];
		ldms_metric_set(sx_set->sx_set, off + i, &v);
	}

	/*
	 * RFC 2819 stats
	 */
	rc = sx_api_port_counter_rfc_2819_get(sx_handle,
				SX_ACCESS_CMD_READ, port, &sx_stats_2819);
	if (SX_CHECK_FAIL(rc)) {
		rc = EIO;
		goto err;
	}

	off += metric_count;
	metric_count = sizeof(sx_stats_2819)/sizeof(sx_port_cntr_t);
	stats = (sx_port_cntr_t *)&sx_stats_2819;

	for (i = 0; i < metric_count; i++) {
		v.v_u64 = stats[i];
		ldms_metric_set(sx_set->sx_set, off + i, &v);
	}

	/*
	 * RFC 3635 stats
	 */
	rc = sx_api_port_counter_rfc_3635_get(sx_handle,
				SX_ACCESS_CMD_READ, port, &sx_stats_3635);
	if (SX_CHECK_FAIL(rc)) {
		rc = EIO;
		goto err;
	}

	off += metric_count;
	metric_count = sizeof(sx_stats_3635)/sizeof(sx_port_cntr_t);
	stats = (sx_port_cntr_t *)&sx_stats_3635;

	for (i = 0; i < metric_count; i++) {
		v.v_u64 = stats[i];
		ldms_metric_set(sx_set->sx_set, off + i, &v);
	}

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
				ovis_log(mylog, OVIS_LERROR, "sx_sample: "
					"failed sampling port %d\n", port);
			}
		}
	}
}

static void sx_term(ldmsd_plug_handle_t handle)
{
	int port;

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
	return  "config name=" PNAME " set=<setname>\n"
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
