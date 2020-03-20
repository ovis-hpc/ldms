/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2015-2017 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2015-2017 Open Grid Computing, Inc. All rights reserved.
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <pthread.h>
#include <linux/limits.h>
#include "gpcd_pub.h"
#include "gpcd_lib.h"
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"

/**
 * \file aries_lbw.c
 * \brief aries latency and bandwidth metric provider (reads gpcd )
 */

/* Aries latency static variables */
static char *event_names[] = {
	"AR_NIC_ORB_PRF_NET_RSP_TRACK_1",
	"AR_NIC_ORB_PRF_NET_RSP_TRACK_2",
	"AR_NIC_NETMON_ORB_EVENT_CNTR_RSP_NET_TRACK",
	"AR_NIC_ORB_CFG_NET_RSP_HIST_OVF",
	"AR_NIC_ORB_PRF_NET_RSP_HIST_BIN01",
	"AR_NIC_ORB_PRF_NET_RSP_HIST_BIN23",
	"AR_NIC_ORB_PRF_NET_RSP_HIST_BIN45",
	"AR_NIC_ORB_PRF_NET_RSP_HIST_BIN67",
	"AR_NIC_ORB_PRF_REQ_BYTES_SENT",
	"AR_NIC_RAT_PRF_REQ_BYTES_RCVD",
	"AR_NIC_ORB_PRF_RSP_BYTES_RCVD"
};
static struct metric_t {
	char *header;
	uint64_t val;
};
static struct metric_t metrics[] = {
	{ "max_rsp_time", 0 },
	{ "min_rsp_time", 0 },
	{ "packet_pairs", 0 },
	{ "sum_latency", 0 },
	{ "mean_latency", 0 },
	{ "bin0-1", 0 },
	{ "bin1-5", 0 },
	{ "bin5-10", 0 },
	{ "bin10-50", 0 },
	{ "bin50-100", 0 },
	{ "bin100-500", 0 },
	{ "bin500-1000", 0 },
	{ "bin1000-", 0 },
	{ "binovf", 0 },
	{ "req_bytes_sent", 0 },
	{ "req_bytes_rcvd", 0 },
	{ "rsp_bytes_rcvd", 0 }
};
#define EVENT_NUM 11
#define METRIC_NUM 17
static int metric_offset;
static uint64_t event_vals[11];
static uint64_t init_vals[4];
static gpcd_context_t* ctx;
static int firstsample = 1;

/* LDMS static variables */
static base_data_t base;
static ldms_set_t set;
static ldmsd_msg_log_f msglog;
static ldms_schema_t schema;
static char *default_schema_name = "aries_lbw";

/* BASIC USE FUNCTIONS */

void split64(uint64_t val, uint64_t* top, uint64_t* bottom)
{
	*top = (val & 0xffffffff00000000) >> 32;
	*bottom = val & 0xffffffff;
}

void split64v2(uint64_t val, uint64_t* top, uint64_t* bottom)
{
	*top = (val & 0xffff0000) >> 16;
	*bottom = val & 0xffff;
}


/* BASIC GPCD FUNCTIONS */

/**
 * Build linked list of tile performance counters we wish to get values for.
 * No aggregation.
 */
int addMetricToContext(char* met)
{
	gpcd_mmr_desc_t *desc;
	int status;
	int i;

	if (ctx == NULL) {
		msglog(LDMSD_LERROR, "aries_lbw: NULL context\n");
		return -1;
	}

	desc = (gpcd_mmr_desc_t *)
	       gpcd_lookup_mmr_byname(met);
	if (!desc) {
		msglog(LDMSD_LINFO, "aries_lbw: Could not lookup <%s>\n", met);
		return -1;
	}

	status = gpcd_context_add_mmr(ctx, desc);
	if (status != 0) {
		msglog(LDMSD_LERROR, "aries_lbw: Could not add mmr for <%s>\n", met);
		gpcd_remove_context(ctx);
		return -1;
	}

	return 0;
}

/**
 * Setup a performance counter descriptor
 */
gpcd_mmr_desc_t* descSetup(char* met)
{
	gpcd_mmr_desc_t* desc = (gpcd_mmr_desc_t *)
	                        gpcd_lookup_mmr_byname(met);
	if (!desc) {
		msglog(LDMSD_LERROR, "aries_lbw: Could not lookup <%s>\n", met);
		return NULL;
	}
	return desc;
}

/**
 * Read a value from a performance counter
 */
int readCounter(uint64_t* val, char* met)
{
	int rc;
	gpcd_mmr_desc_t* desc;

	desc = descSetup(met);
	if (desc == NULL) {
		return -1;
	}
	rc = gpcd_read_mmr_val(desc, val, ctx->nic_addr);
	if (rc != 0) {
		msglog(LDMSD_LERROR, "aries_lbw: Could not read <%s>\n", met);
		return -1;
	}
	return 0;
}

/**
 * Write a value from a performance counter
 */
int writeCounter(uint64_t* val, char* met)
{
	int rc;
	gpcd_mmr_desc_t* desc;

	desc = descSetup(met);
	if (desc == NULL) {
		return -1;
	}
	rc = gpcd_write_mmr_val(desc, val, ctx->nic_addr);
	if (rc != 0) {
		msglog(LDMSD_LERROR, "aries_lbw: Could not read <%s>\n", met);
		return -1;
	}
	return 0;
}

/* LATENCY TEST FUNCTIONS */

/**
  * Add all latency / bw events to the context
 */
int addEventsToContext()
{
	int rc;

	for(int i=0; i < EVENT_NUM; i++) {
		rc = addMetricToContext(event_names[i]);
		if (rc != 0) {
			return -1;
		}
	}

	return 0;
}

/**
 * Set histogram bin latency ranges for the experiment
 * Leaving COMP and MASK registers as default
 */
int setHistogram()
{
	int rc;

	uint64_t hist1val = 0x000A000500010000;
	uint64_t hist2val = 0x03E801F400640032;

	rc = writeCounter(&hist1val, event_names[0]);
	if (rc != 0) {
		return -1;
	}
	rc = writeCounter(&hist2val, event_names[1]);
	if (rc != 0) {
		return -1;
	}

	return 0;
}

/**
 * Setup latency test
 * init_vals will contain four initial counter values
 * these values will be used for differential metrics
 */
int initLatency()
{
	int rc;

	/* Default values obtained from s-0045 */
	uint64_t track1val = 65535;
	uint64_t track2val = 0;
	uint64_t histovfval = 0xff;

	rc = writeCounter(&track1val, event_names[0]);
	if (rc != 0) {
		return -1;
	}
	rc = writeCounter(&track2val, event_names[1]);
	if (rc != 0) {
		return -1;
	}
	rc = readCounter(&init_vals[0], event_names[2]);
	if (rc != 0) {
		return -1;
	}
	rc = writeCounter(&histovfval, event_names[3]);
	if (rc != 0) {
		return -1;
	}
	rc = readCounter(&init_vals[1], event_names[8]);
	if (rc != 0) {
		return -1;
	}
	rc = readCounter(&init_vals[2], event_names[9]);
	if (rc != 0) {
		return -1;
	}
	rc = readCounter(&init_vals[3], event_names[10]);
	if (rc != 0) {
		return -1;
	}

	return 0;
}

/**
 * Read the latency counters and return them
 */
int readEvents()
{
	int rc;

	for (int i=0; i < EVENT_NUM; i++) {
		rc = readCounter(&event_vals[i], event_names[i]);
		if (rc != 0) {
			return -1;
		}
	}

	return 0;
}

/**
 * Calculate the Aries metrics from event values
 * Send these metrics to the metric_set
 */
int calcAriesMetrics()
{
	union ldms_value v;
	int metric_no = metric_offset;
	/*metrics is a bunch of uint64, need to store a double as well */
	float metrics4val;

	split64v2(event_vals[0], &metrics[0].val, &metrics[1].val);
	metrics[2].val = event_vals[2] - init_vals[0];
	metrics[3].val = event_vals[1];
	metrics4val = event_vals[1] / (float)metrics[2].val;
	split64(event_vals[4], &metrics[5].val, &metrics[6].val);
	split64(event_vals[5], &metrics[7].val, &metrics[8].val);
	split64(event_vals[6], &metrics[9].val, &metrics[10].val);
	split64(event_vals[7], &metrics[11].val, &metrics[12].val);
	metrics[13].val = event_vals[3];
	metrics[14].val = event_vals[8] - init_vals[1];
	metrics[15].val = event_vals[9] - init_vals[2];
	metrics[16].val = event_vals[10] - init_vals[3];

	for (int i=0; i<METRIC_NUM; i++) {
		if (strcmp(metrics[i].header, "mean_latency") == 0) {
			v.v_f = metrics4val;
			ldms_metric_set(set, metric_no, &v);
			metric_no++;
			msglog(LDMSD_LDEBUG, "aries_lbw: \
				Metric %d is %0.3f\n", i, metrics4val);
		} else {
			v.v_u64 = metrics[i].val;
			ldms_metric_set(set, metric_no, &v);
			metric_no++;
			msglog(LDMSD_LDEBUG, "aries_lbw: \
				Metric %d is %llu\n", i, metrics[i].val);
		}
	}

	return 0;
}


static int create_metric_set(base_data_t base)
{
	int mid,rc;

	schema = base_schema_new(base);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	/* Location of first metric after base */
	metric_offset = ldms_schema_metric_count_get(schema);

	/* Add metrics to the schema */
	for (int i = 0; i < METRIC_NUM; i++) {
		if (strcmp(metrics[i].header,"mean_latency") == 0) {
			mid = ldms_schema_metric_add(schema, metrics[i].header, LDMS_V_F32);
			if (mid < 0) {
				return ENOMEM;
			}
		} else {
			mid = ldms_schema_metric_add(schema, metrics[i].header, LDMS_V_U64);
			if (mid < 0) {
				return ENOMEM;
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
	if (schema) {
		ldms_schema_delete(schema);
	}
	schema = NULL;

	return rc;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *rawf;
	void * arg = NULL;
	int i;
	int rc;


	if (set) {
		msglog(LDMSD_LERROR, "aries_lbw: Set already created.\n");
		return EINVAL;
	}

	/* Creating context, disabling perms, setting histogram */
	/* Check perms using histogram, retry if initial histogram set fails */
	/* If two histograms fail, throw error to daemon and exit */
	ctx = gpcd_create_context();
	if (!ctx) {
		msglog(LDMSD_LERROR, "aries_lbw: cannot create context\n");
		return EINVAL;
	}
	/* Trying to disable permissions */
	rc = gpcd_disable_perms();
	if (rc != 0) {
		msglog(LDMSD_LDEBUG, "aries_lbw: cannot run disable_perms\n");
		return EINVAL;
	}
	rc = addEventsToContext();
	if (rc != 0) {
		msglog(LDMSD_LDEBUG, "aries_lbw: cannot add events to context\n");
		return EINVAL;
	}
	/* Trying to set histogram, resetting perms if need be */
	/* FIXME when cray finds a way to return permission status */
	rc = setHistogram(ctx);
	if (rc != 0) {
		msglog(LDMSD_LDEBUG, "aries_lbw: cannot set histogram, retrying\n");
		gpcd_disable_perms();
		rc = setHistogram(ctx);
		if (rc != 0) {
			msglog(LDMSD_LERROR, "aries_lbw: cannot set histogram\n");
			return EINVAL;
		} else {
			msglog(LDMSD_LDEBUG, "aries_lbw: disabling perm again worked, \
						ignore previous errors \n");
		}
	}

	base = base_config(avl, "aries_lbw", default_schema_name, msglog);
	if (!base) {
		return EINVAL;
	}

	rc = create_metric_set(base);
	if (rc) {
		msglog(LDMSD_LERROR, "aries_lbw: failed to create a metric set.\n");
		return rc;
	}
	return 0;
}

static int sample(struct ldmsd_sampler *self)
{

	union ldms_value v;
	int i;
	int rc;

	if (!set) {
		msglog(LDMSD_LERROR, "aries_lbw: plugin not initialized\n");
		return EINVAL;
	}

	base_sample_begin(base);

	if ( firstsample ) {
		/* Need initialization for any sample
		   First sample will be all zeros */
		initLatency(init_vals);
		firstsample = 0;
	} else {
		readEvents();
		calcAriesMetrics();
		initLatency(init_vals);
	}
	
	base_sample_end(base);
	return 0;

}



static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static void term(struct ldmsd_plugin *self)
{

	int i;

	gpcd_remove_context(ctx);

	if (schema) {
		ldms_schema_delete(schema);
	}
	schema = NULL;
	if (set) {
		ldms_set_delete(set);
	}
	set = NULL;
	base_del(base);
	base = NULL;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=aries_lbw" BASE_CONFIG_USAGE " \n";
}

static struct ldmsd_sampler aries_lbw_plugin = {
	.base = {
		.name = "aries_lbw",
		.type = LDMSD_PLUGIN_SAMPLER,
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
	set = NULL;
	return &aries_lbw_plugin.base;
}
