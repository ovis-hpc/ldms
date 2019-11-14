/**
 * Copyright © 2018-2019 National Technology & Engineering Solutions of Sandia,
 * LLC (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the
 * U.S. Government retains certain rights in this software.
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file opa2.c
 * \OmniPath sampler using OFED-4.8.2 packaged extended MAD performance calls
 * This produces multiple sets named $producer/$interface/$port with schema
 * named by user.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <infiniband/mad.h>
#include <infiniband/umad.h>

#include "ldmsd.h"
#include "ldmsd_sampler.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

static int mgmt_classes[3] = {IB_SMI_CLASS, IB_SA_CLASS, IB_PERFORMANCE_CLASS};

struct hfi_port_comb {
	LIST_ENTRY(hfi_port_comb) entry;
	ldms_set_t set;
	int badport; /* if open error occured, set this and do not retry */
	int port;
	int base_lid;
	struct ibmad_port *srcport;
	char ibd_ca[UMAD_CA_NAME_LEN];
	uint8_t rcv_buf[1024];
};

typedef struct opa2_inst_s *opa2_inst_t;
struct opa2_inst_s {
	struct ldmsd_plugin_inst_s base;

	int ca_name_offset;
	int port_offset;
	int metric_offset;

	char local_hca_names[UMAD_MAX_DEVICES][UMAD_CA_NAME_LEN];
	umad_ca_t all_hfis[UMAD_MAX_DEVICES];
	bool collected_hfi[UMAD_MAX_DEVICES];

	ib_portid_t portid;
	int hfi_quant;

	LIST_HEAD(hfi_port_list, hfi_port_comb) hfi_port_list;
};

#define _stringify(_x) #_x
#define stringify(_x) _stringify(_x)

/******************************* OFED-4.8.2 **********************************/

/* typedef struct umad_ca {
	char ca_name[UMAD_CA_NAME_LEN];
 	unsigned node_type;
	int numports;
	char fw_ver[20];
	char ca_type[40];
	char hw_ver[20];
	uint64_t node_guid;
	uint64_t system_guid;
	umad_port_t *ports[UMAD_CA_MAX_PORTS];
} umad_ca_t;
*/
/*typedef struct umad_port {
	char ca_name[UMAD_CA_NAME_LEN];
	int portnum;
	unsigned base_lid;
	unsigned lmc;
	unsigned sm_lid;
	unsigned sm_sl;
	unsigned state;
	unsigned phys_state;
	unsigned rate;
	uint32_t capmask;
	uint64_t gid_prefix;
	uint64_t port_guid;
	unsigned pkeys_size;
	uint16_t *pkeys;
	char link_layer[UMAD_CA_NAME_LEN];
} umad_port_t;
*/

/*
IB_PC_EXT_FIRST_F,
IB_PC_EXT_PORT_SELECT_F = IB_PC_EXT_FIRST_F,
IB_PC_EXT_COUNTER_SELECT_F,
IB_PC_EXT_XMT_BYTES_F,
IB_PC_EXT_RCV_BYTES_F,
IB_PC_EXT_XMT_PKTS_F,
IB_PC_EXT_RCV_PKTS_F,
IB_PC_EXT_XMT_UPKTS_F,
IB_PC_EXT_RCV_UPKTS_F,
IB_PC_EXT_XMT_MPKTS_F,
IB_PC_EXT_RCV_MPKTS_F
*/
/*****************************************************************************/


/* Init the array all_hfis with data from "hfi*" umad interfaces.
 */
static int find_all_connected_hfis(opa2_inst_t inst)
{
	int i, ca_quant,rc=0;
	umad_ca_t hca = {""};

	if (umad_init() < 0) {
		INST_LOG(inst, LDMSD_LERROR,
			 "cannot initialize the UMAD library \n");
		errno = ENOTSUP;
		return errno;
	}

	ca_quant = umad_get_cas_names(inst->local_hca_names,UMAD_MAX_DEVICES);
	if (ca_quant < 0) {
		INST_LOG(inst, LDMSD_LERROR,
			 "can't list connected fabric HCA hardware names \n");
		errno = ENOTSUP;
		return errno;
	}

	inst->hfi_quant = 0;
	for (i = 0; i < ca_quant; i++) {
		rc = umad_get_ca(inst->local_hca_names[i], &hca);
		if (rc != 0) {
			INST_LOG(inst, LDMSD_LWARNING,
				 "can't get local HCA data for %s.\n",
				 inst->local_hca_names[i]);
			continue;
			/* but continue on for non-bad cards */
		} else {
			INST_LOG(inst, LDMSD_LDEBUG, "HCA connected: %s \n",
				 hca.ca_name);
		}

		/* collect hfis in the hca list */
		if (strncmp(hca.ca_name, "hfi", 3) == 0) {
			inst->all_hfis[inst->hfi_quant] = hca;
			INST_LOG(inst, LDMSD_LDEBUG,
				 "Detected hfi: %s with %d ports\n",
				 hca.ca_name, hca.numports);
			inst->hfi_quant++;
		}
		umad_release_ca(&hca);
	}

	return 0;
}

static int create_hfi_port(opa2_inst_t inst, umad_ca_t *cap, int port_number)
{
	if (! cap || port_number < 1) {
		return EINVAL;
	}
	if (port_number > cap->numports) {
		INST_LOG(inst, LDMSD_LERROR, "hfi %s has no port %d\n",
			 cap->ca_name, port_number);
		return EINVAL;
	}
	struct hfi_port_comb *newport = calloc(1, sizeof(struct hfi_port_comb));
	if (!newport || ! cap || port_number < 1) {
		return ENOMEM;
	}
	strcpy(newport->ibd_ca, cap->ca_name);
	newport->port = port_number;
	newport->base_lid = cap->ports[port_number]->base_lid;
	LIST_INSERT_HEAD(&inst->hfi_port_list, newport, entry);
	return 0;
}

/* Init hfi_port_list with an object per detected port.
 * This allows that some cards/ports may not exist on some producers,
 * but warns about it.
 */
static int build_port_list(opa2_inst_t inst, char *port_list)
{

	int rc=0,n;
	int port_number, i;
	char *pnter;
	char hfi_name_b[UMAD_CA_NAME_LEN+1];

	if (strcmp(port_list, "*") == 0) {
		/* take every hfi and port */
		for (i=0; i < inst->hfi_quant; i++) {
			for (port_number = 1;
				port_number <= inst->all_hfis[i].numports;
				port_number++) {
				rc = create_hfi_port(inst, &(inst->all_hfis[i]),
						     port_number);
				if (rc) {
					INST_LOG(inst, LDMSD_LERROR,
						 "create_hfi_port error %s\n",
						 strerror(rc));

				}
			}
		}
		goto out;
	}

	pnter = port_list;

	/* We have a user-supplied list */
	while (*pnter) {

		char *hfi_name = hfi_name_b;
		rc = sscanf(pnter, "%" stringify(UMAD_CA_NAME_LEN) "[^.].%d%n",
			hfi_name, &port_number, &n);
		if (rc != 2) {
			INST_LOG(inst, LDMSD_LERROR,
				 "cannot parse 'ports'. Expected "
				 "<hfi_name>.<port number>.  Try utility "
				 "ibstat for valid CAs and ports.\n");
			rc = EINVAL;
			return rc;
		}
		if (*hfi_name == ',') {
			hfi_name++;
		}
		for (i = 0; i < inst->hfi_quant; i++) {
			if (0 == strcmp(hfi_name, inst->all_hfis[i].ca_name)) {
				if (inst->collected_hfi[i]) {
					continue; /* skip dup */
				}
				inst->collected_hfi[i] = true;
				create_hfi_port(inst, &(inst->all_hfis[i]),
						port_number);
				if (rc) {
					INST_LOG(inst, LDMSD_LERROR,
						 "create_hfi_port error %s\n",
						 strerror(rc));

				}
				break;
			}
		}
		if (i == inst->hfi_quant) {
			INST_LOG(inst, LDMSD_LWARNING,
				 "config: unknown hfi %s in %s\n",
				 hfi_name, port_list);
		}
		pnter += n;
		fflush(0);

	}

out: ;
	struct hfi_port_comb *hpc;
	LIST_FOREACH(hpc, &inst->hfi_port_list, entry) {
		INST_LOG(inst, LDMSD_LDEBUG,
			 "%s:%d with LID number: %d\n", hpc->ibd_ca,
			 hpc->port, hpc->base_lid);
		hpc->srcport = mad_rpc_open_port(hpc->ibd_ca, hpc->port,
					mgmt_classes, 3);
		if (!hpc->srcport) {
			INST_LOG(inst, LDMSD_LERROR,
				 "failed to open %s.%d\n",
				 hpc->ibd_ca, hpc->port);
			hpc->badport = 1;
		}
	}

	return rc;
}

static int create_metric_set(opa2_inst_t inst, struct hfi_port_comb *hpc)
{
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int i;
	char name[256];
	int midx;

	/* instance_name is $producer/$caname/$caport */
	snprintf(name, sizeof(name), "%s/%s/%d",
				     samp->producer_name, hpc->ibd_ca,
				     hpc->port);
	hpc->set = samp->create_set(&inst->base, name, samp->schema, hpc);
	if (!hpc->set) {
		INST_LOG(inst, LDMSD_LERROR,
			 "create_metric_set: set_new failed\n");
		return ENOMEM;
	}

	ldms_metric_array_set_str(hpc->set, inst->ca_name_offset, hpc->ibd_ca);
	ldms_metric_set_u64(hpc->set, inst->port_offset, hpc->port);

	midx = inst->metric_offset;
	for (i = IB_PC_EXT_XMT_BYTES_F; i < IB_PC_EXT_LAST_F; i++) {
		ldms_metric_set_u64(hpc->set, midx, 0);
		midx++;
	}

	return 0;
}


/* ============== Sampler Plugin APIs ================= */

static
int opa2_update_schema(ldmsd_plugin_inst_t pi, ldms_schema_t schema)
{
	opa2_inst_t inst = (void*)pi;
	int rc;

	rc = ldms_schema_meta_array_add(schema, "ca_name", LDMS_V_CHAR_ARRAY,
					"", UMAD_CA_NAME_LEN);
	if (rc < 0)
		return -rc; /* rc = -errno */
	inst->ca_name_offset = rc;
	rc = ldms_schema_meta_add(schema, "port", LDMS_V_U64, "");
	if (rc < 0)
		return -rc; /* rc = -errno */
	inst->port_offset = rc;

	// Add sampler metrics. always skip portselect and counter select.
	inst->metric_offset = inst->port_offset + 1;
	int dec_val;
	for (dec_val = IB_PC_EXT_XMT_BYTES_F;
		dec_val < IB_PC_EXT_LAST_F; dec_val++) {
		rc = ldms_schema_metric_add(schema, mad_field_name(dec_val),
					    LDMS_V_U64, "");
		if (rc < 0)
			return -rc; /* rc = -errno */
	}
	return 0;
}

static
int opa2_update_set(ldmsd_plugin_inst_t pi, ldms_set_t set, void *ctxt)
{
	opa2_inst_t inst = (void*)pi;

	int dec_val;
	union ldms_value v;
	struct hfi_port_comb *hpc = ctxt;

	if (hpc->badport || !hpc->srcport) {
		return 0; /* just skip */
	}

	inst->portid.lid = hpc->base_lid;
	memset(hpc->rcv_buf, 0, sizeof(hpc->rcv_buf));

	if (!pma_query_via(hpc->rcv_buf, &inst->portid, hpc->port, 0,
			IB_GSI_PORT_COUNTERS_EXT, hpc->srcport)) {
		INST_LOG(inst, LDMSD_LERROR, "query error on %s.%d\n",
			 hpc->ibd_ca, hpc->port);
	}

	int metric_no = inst->metric_offset;
	for (dec_val = IB_PC_EXT_XMT_BYTES_F;
			dec_val <= IB_PC_EXT_RCV_MPKTS_F;
			dec_val++) {
		mad_decode_field(hpc->rcv_buf, dec_val, &v.v_u64);
		ldms_metric_set_u64(hpc->set, metric_no++, v.v_u64);
	}
	return 0;
}


/* ============== Common Plugin APIs ================= */

static
const char *opa2_desc(ldmsd_plugin_inst_t pi)
{
	return "opa2 - opa2 sampler plugin";
}

static
char *_help = "\
opa2 configuration synopsis:\n\
    config name=<INST> [COMMON_OPTIONS] ports=<csv>\n\
\n\
Option descriptions:\n\
    ports   A comma-separated list of ports (e.g. mlx4_0.1,mlx4_0.2) \n\
            or just a single * for all omnipath hfi ports. Default '*'.\n\
\n\
NOTE: $producer/$hfiname/$portnum always overrides instance given.\n\
";

static
const char *opa2_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static
int opa2_config(ldmsd_plugin_inst_t pi, json_entity_t json,
				      char *ebuf, int ebufsz)
{
	opa2_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	int rc;
	char *ports;
	json_entity_t value;

	if (inst->hfi_quant != 0) {
		snprintf(ebuf, ebufsz, "config: cannot be done twice.\n");
		return ENOTSUP;
	}
	INST_LOG(inst, LDMSD_LINFO, "config: started.\n");

	rc = find_all_connected_hfis(inst); /* get umad interface list */
	if (rc != 0) {
		snprintf(ebuf, ebufsz, "config: umad failed\n");
		return rc;
	}

	rc = samp->base.config(pi, json, ebuf, ebufsz);
	if (rc)
		return rc;

	value = json_value_find(json, "ports");
	if (!value) {
		ports = "*";
	} else {
		if (value->type != JSON_STRING_VALUE) {
			snprintf(ebuf, ebufsz, "%s: The given 'ports' value "
							"is not a string.\n",
							pi->inst_name);
			return EINVAL;
		}
		ports = (char *)json_value_str(value)->str;
	}

	INST_LOG(inst, LDMSD_LINFO, "configured for ports %s \n", ports);

	build_port_list(inst, ports);
	samp->schema = samp->create_schema(pi);
	if (!samp->schema)
		return errno;

	struct hfi_port_comb *hpc;
	LIST_FOREACH(hpc, &inst->hfi_port_list, entry) {
		/* create metric set for each port */
		rc = create_metric_set(inst, hpc);
		if (rc) {
			return rc;
		}
	}
	INST_LOG(inst, LDMSD_LINFO, "config: ok.\n");
	return 0;
}

static
void opa2_del(ldmsd_plugin_inst_t pi)
{
	opa2_inst_t inst = (void*)pi;

	struct hfi_port_comb *hpc;
	while ((hpc = LIST_FIRST(&inst->hfi_port_list))) {
		LIST_REMOVE(hpc, entry);
		/* the set is cleaned up by sampler_del() */
		if (hpc->srcport)
			mad_rpc_close_port(hpc->srcport);
		free(hpc);
	}
}

static
int opa2_init(ldmsd_plugin_inst_t pi)
{
	opa2_inst_t inst = (void*)pi;
	ldmsd_sampler_type_t samp = (void*)inst->base.base;
	/* override update_schema() and update_set() */
	samp->update_schema = opa2_update_schema;
	samp->update_set = opa2_update_set;

	return 0;
}

static
struct opa2_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = "sampler",
		.plugin_name = "opa2",

                /* Common Plugin APIs */
		.desc   = opa2_desc,
		.help   = opa2_help,
		.init   = opa2_init,
		.del    = opa2_del,
		.config = opa2_config,
	},
	.portid = { .qp = 1 /* the rest are 0 */ },
};

ldmsd_plugin_inst_t new()
{
	opa2_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}
