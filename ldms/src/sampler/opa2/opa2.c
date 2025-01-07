/**
 * Copyright © 2018 National Technology & Engineering Solutions of Sandia,
 * LLC (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the
 * U.S. Government retains certain rights in this software.
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
#include <mad.h>
#include <umad.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

static int metric_offset;	/* first variable metric */
static int ca_name_offset;
static int port_offset;
static base_data_t cfg_base;	/* global base for all sets */

#define SAMP "opa2" /* string name of sampler */
#define SAPI(x) x ## _opa2 /* C symbol suffix so we can
			      easily set brkpnts in gcc and identify stack
			     in stack dumps/valgrind */

static ovis_log_t mylog;

static char local_hca_names[UMAD_MAX_DEVICES][UMAD_CA_NAME_LEN];
static int mgmt_classes[3] = {IB_SMI_CLASS, IB_SA_CLASS, IB_PERFORMANCE_CLASS};
static umad_ca_t hca, all_hfis[UMAD_MAX_DEVICES];
static bool collected_hfi[UMAD_MAX_DEVICES];
static ib_portid_t portid = { 0 };
static int hfi_quant;

struct hfi_port_comb {
	LIST_ENTRY(hfi_port_comb) entry;
	base_data_t cfg; /* The set and schema data (schema redundant) */
	ldms_set_t set; /* same as cfg->set */
	int badport; /* if open error occured, set this and do not retry */
	int port;
	int base_lid;
	struct ibmad_port *srcport;
	char ibd_ca[UMAD_CA_NAME_LEN];
	uint8_t rcv_buf[1024];
};

LIST_HEAD(hfi_port_list, hfi_port_comb) hfi_port_list =
	LIST_HEAD_INITIALIZER(hfi_port_list);

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
static int find_all_connected_hfis()
{
	int i, ca_quant,rc=0;

	if (umad_init() < 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": cannot initialize the UMAD library \n");
		errno = ENOTSUP;
		return errno;
	}

	ca_quant = umad_get_cas_names(local_hca_names,UMAD_MAX_DEVICES);
	if (ca_quant < 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": can't list connected fabric HCA hardware names \n");
		errno = ENOTSUP;
		return errno;
	}

	hfi_quant = 0;
	for (i = 0; i < ca_quant; i++) {
		rc = umad_get_ca(local_hca_names[i], &hca);
		if (rc != 0) {
			ovis_log(mylog, OVIS_LWARNING, SAMP
				": can't get local HCA data for %s.\n",
				local_hca_names[i]);
			/* but continue on for non-bad cards */
		} else {
			ovis_log(mylog, OVIS_LDEBUG, "HCA connected: %s \n",
				hca.ca_name);
		}

		/* collect hfis in the hca list */
		if (strncmp(hca.ca_name, "hfi", 3) == 0) {
			all_hfis[hfi_quant] = hca;
			ovis_log(mylog, OVIS_LDEBUG, "Detected hfi: %s with %d ports\n",
				hca.ca_name, hca.numports);
			hfi_quant++;
		}
	}

	return 0;
}

static int create_hfi_port( umad_ca_t *cap, int port_number) {
	if (! cap || port_number < 1) {
		return EINVAL;
	}
	if (port_number > cap->numports) {
		ovis_log(mylog, OVIS_LERROR, SAMP ": hfi %s has no port %d\n",
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
	LIST_INSERT_HEAD(&hfi_port_list, newport, entry);
	return 0;
}

/* Init hfi_port_list with an object per detected port.
 * This allows that some cards/ports may not exist on some producers,
 * but warns about it.
 */
static int build_port_list(char *port_list)
{

	int rc=0,n;
	int port_number, i;
	char *pnter;
	char hfi_name_b[UMAD_CA_NAME_LEN+1];

	if (strcmp(port_list, "*") == 0) {
		/* take every hfi and port */
		for (i=0; i < hfi_quant; i++) {
			for (port_number = 1;
				port_number <= all_hfis[i].numports;
				port_number++) {
				rc = create_hfi_port(&(all_hfis[i]),
					port_number);
				if (rc) {
					ovis_log(mylog, OVIS_LERROR, SAMP
						": create_hfi_port error %s\n",
						STRERROR(rc));

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
			ovis_log(mylog, OVIS_LERROR,SAMP": cannot parse 'ports'. Expected <hfi_name>.<port number>.  Try utility ibstat for valid CAs and ports.\n");
			rc = EINVAL;
			return rc;
		}
		if (*hfi_name == ',') {
			hfi_name++;
		}
		for (i = 0; i < hfi_quant; i++) {
			if (0 == strcmp(hfi_name, all_hfis[i].ca_name)) {
				if (collected_hfi[i]) {
					continue; /* skip dup */
				}
				collected_hfi[i] = true;
				create_hfi_port(&(all_hfis[i]), port_number);
				if (rc) {
					ovis_log(mylog, OVIS_LERROR, SAMP
						": create_hfi_port error %s\n",
						STRERROR(rc));

				}
				break;
			}
		}
		if (i == hfi_quant) {
			ovis_log(mylog, OVIS_LWARNING, SAMP ": config: unknown hfi %s in %s\n",
				hfi_name, port_list);
		}
		pnter += n;
		fflush(0);

	}

out: ;
	struct hfi_port_comb *hpc;
	LIST_FOREACH(hpc, &hfi_port_list, entry) {
		ovis_log(mylog, OVIS_LDEBUG, "%s:%d with LID number: %d\n", hpc->ibd_ca,
			hpc->port, hpc->base_lid);
		hpc->srcport = mad_rpc_open_port(hpc->ibd_ca, hpc->port,
					mgmt_classes, 3);
		if (!hpc->srcport) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": failed to open %s.%d\n",
				hpc->ibd_ca, hpc->port);
			hpc->badport = 1;
		}
	}

	return rc;
}

/* set null the shared fields of a clone so base_del does no
 * double frees.
 * Side effect: enforces check that cloning is used correctly.
 * This would be cleaner or unneeded if schemas and sets were
 * reference counted. But then everything else would be dirtier.
 */
static void clone_base_del(base_data_t base)
{
	assert(base->set != cfg_base->set);
	assert(base->instance_name != cfg_base->instance_name);

	assert(base->schema == cfg_base->schema);
	base->schema = NULL;

	assert(base->schema_name == cfg_base->schema_name);
	base->schema_name = NULL;

	assert(base->producer_name == cfg_base->producer_name);
	base->producer_name = NULL;

	assert(base->job_set == cfg_base->job_set);
	base->job_set = NULL;

	base_del(base);
}

/* instance_name is $producer/$caname/$caport */
static
base_data_t clone_cfg(base_data_t base, const char *hfi_name, int port)
{
	char inst[PATH_MAX];
	if (!base || !hfi_name) {
		errno = EINVAL;
		return NULL;
	}
	base_data_t newb = calloc(1, sizeof(*base));
	if (!newb) {
		goto err;
	}
	*newb = *base;
	/* now sharing everything but name strings across all bases. */
	sprintf(inst, "%s/%s/%d", newb->producer_name, hfi_name, port);
	newb->instance_name = strdup(inst);

	if (!newb->instance_name) {
		goto err;
	}

	return newb;

err:
	if (newb) {
		if (newb->instance_name) {
			free(newb->instance_name);
		}
		free(newb);
	}
	errno = ENOMEM;
	return NULL;
}


static int create_schema()
{
	ldms_schema_t schema;

	schema = base_schema_new(cfg_base);

	if (!schema) {
		ovis_log(mylog, OVIS_LERROR, SAMP "create_schema: base failed\n");
		return errno;
	}

	metric_offset = ldms_schema_metric_count_get(schema);
	ca_name_offset = metric_offset;
	int rc = ldms_schema_meta_array_add(schema, "ca_name",
			LDMS_V_CHAR_ARRAY, UMAD_CA_NAME_LEN);
	if (rc < 0) {
		return ENOMEM;
	}
	metric_offset++;
	port_offset = metric_offset;
	rc = ldms_schema_meta_add(schema, "port", LDMS_V_U64);
	if (rc < 0) {
		return ENOMEM;
	}
	metric_offset++;

	// Add sampler metrics. always skip portselect and counter select.
	int dec_val;
	for (dec_val = IB_PC_EXT_XMT_BYTES_F;
		dec_val < IB_PC_EXT_LAST_F; dec_val++) {
		rc = ldms_schema_metric_add(schema, mad_field_name(dec_val),
			LDMS_V_U64);
		if (rc < 0) {
			return ENOMEM;
		}
	}

	return 0;
}

static int create_metric_set(struct hfi_port_comb *hpc)
{
	int rc, dec_val;

	hpc->cfg = clone_cfg(cfg_base, hpc->ibd_ca, hpc->port);
	rc = errno;
	if (!hpc->cfg) {
		ovis_log(mylog, OVIS_LERROR, SAMP "create_metric_set: clone failed\n");
		return rc;
	}
	hpc->set = base_set_new(hpc->cfg);
	if (!hpc->set) {
		ovis_log(mylog, OVIS_LERROR, SAMP "create_metric_set: set_new failed\n");
		return ENOMEM;
	}

	ldms_metric_set_u64(hpc->set, port_offset, hpc->port);
	ldms_metric_array_set_str(hpc->set, ca_name_offset, hpc->ibd_ca);

	for (dec_val = IB_PC_EXT_XMT_BYTES_F;
		dec_val < IB_PC_EXT_LAST_F; dec_val++) {
		ldms_metric_set_u64(hpc->set,
			(dec_val-IB_PC_EXT_XMT_BYTES_F + metric_offset), 0);
	}

	return 0;
}

static const char *SAPI(usage)(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP BASE_CONFIG_USAGE
"    <ports>   A comma-separated list of ports (e.g. mlx4_0.1,mlx4_0.2)\n"
"              or just a single * for all omnipath hfi ports. Default '*'.\n"
"    NOTE: $producer/$hfiname/$portnum always overrides instance given.\n"
		;

}

static void init_portid() {
	portid.drpath.cnt = 0;
	memset(portid.drpath.p, 0, 63);
	portid.drpath.drslid = 0;
	portid.drpath.drdlid = 0;
	portid.grh_present = 0;
	memset(portid.gid, 0, 16);
	portid.qp = 1;
	portid.qkey = 0;
	portid.sl = 0;
	portid.pkey_idx = 0;
}

static int SAPI(config)(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;
	if (hfi_quant != 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP "config: cannot be done twice.\n");
		return ENOTSUP;
	}
	ovis_log(mylog, OVIS_LINFO, SAMP "config: started.\n");

	rc = find_all_connected_hfis(); /* get umad interface list */
	if (rc != 0) {
		ovis_log(mylog, OVIS_LERROR, SAMP "config: umad failed\n");
		return rc;
	}


	cfg_base = base_config(avl, self->inst_name, SAMP, mylog);
	if (!cfg_base) {
		rc = errno;
		return rc;
	}

	char *ports = av_value(avl, "ports");
	if (!ports) {
		ports = "*";
	}

	ovis_log(mylog, OVIS_LINFO, SAMP "configured for ports %s \n", ports);

	build_port_list(ports);
	create_schema();

	struct hfi_port_comb *hpc;
	LIST_FOREACH(hpc, &hfi_port_list, entry) {
		rc = create_metric_set(hpc);
		if (rc) {
			return rc;
		}
	}
	init_portid();
	ovis_log(mylog, OVIS_LINFO, SAMP "config: ok.\n");
	return 0;
}

static int SAPI(sample)(struct ldmsd_sampler *self)
{
	int dec_val;
	union ldms_value v;

	struct hfi_port_comb *hpc;
	LIST_FOREACH(hpc, &hfi_port_list, entry) {
		if (hpc->badport || !hpc->srcport) {
			continue;
		}

		base_sample_begin(hpc->cfg);

		portid.lid = hpc->base_lid;
		memset(hpc->rcv_buf, 0, sizeof(hpc->rcv_buf));

		if (!pma_query_via(hpc->rcv_buf, &portid, hpc->port, 0,
				IB_GSI_PORT_COUNTERS_EXT, hpc->srcport)) {
			ovis_log(mylog, OVIS_LERROR, SAMP ": query error on %s.%d\n",
				hpc->ibd_ca, hpc->port);
		}

		int metric_no = metric_offset;
		for (dec_val = IB_PC_EXT_XMT_BYTES_F;
			dec_val <= IB_PC_EXT_RCV_MPKTS_F;
			dec_val++) {
			mad_decode_field(hpc->rcv_buf, dec_val, &v.v_u64);
			ldms_metric_set_u64(hpc->set, metric_no++, v.v_u64);
		}
		base_sample_end(hpc->cfg);
	}

	return 0;
}

static void SAPI(term)(struct ldmsd_plugin *self)
{
	struct hfi_port_comb *hpc;
	ovis_log(mylog, OVIS_LDEBUG, SAMP ": closing plugin.\n");
	LIST_FOREACH(hpc, &hfi_port_list, entry) {
		if (!hpc->badport) {
			hpc->badport = 1;
			mad_rpc_close_port(hpc->srcport);
		}
		clone_base_del(hpc->cfg);
	}

	if (cfg_base) {
		base_del(cfg_base);
		cfg_base = NULL;
	}
	if (mylog)
		ovis_log_destroy(mylog);
}

static struct ldmsd_sampler SAPI(plugin) = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = SAPI(term),
		.config = SAPI(config),
		.usage = SAPI(usage),
	},
	.sample = SAPI(sample),
};

struct ldmsd_plugin *get_plugin() {
	int rc;
	mylog = ovis_log_register("sampler."SAMP, "The log subsystem of the " SAMP " plugin");
	if (!mylog) {
		rc = errno;
		ovis_log(NULL, OVIS_LWARN, "Failed to create the subsystem "
				"of '" SAMP "' plugin. Error %d\n", rc);
	}
	return &SAPI(plugin).base;
}
