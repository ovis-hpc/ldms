/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010-2016,2018,2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2010-2016,2018,2022 Open Grid Computing, Inc. All rights
 * reserved.
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
 * \file procnetdev2.c
 * \brief /proc/net/dev data provider
 *
 * This is based on \c procnetdev.c. The difference is that \c procnetdev2 uses
 * \c LDMS_V_LIST and \c LDMS_V_RECORD.
 */
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "../sampler_base.h"

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof(a) / sizeof(*a))
#endif

#define PROC_FILE "/proc/net/dev"
static char *procfile = PROC_FILE;

struct rec_metric_info {
	int mid;
	const char *name;
	const char *unit;
	enum ldms_value_type type;
	int array_len;
};

#define MAXIFACE 32
#ifndef IFNAMSIZ
/* from "linux/if.h" */
#define IFNAMSIZ 16
#endif

struct ldms_metric_template_s rec_metrics[] = {
	{ "name"          , 0,    LDMS_V_CHAR_ARRAY , ""        , IFNAMSIZ } ,
	{ "rx_bytes"      , 0,    LDMS_V_U64        , "bytes"   , 1  } ,
	{ "rx_packets"    , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "rx_errs"       , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "rx_drop"       , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "rx_fifo"       , 0,    LDMS_V_U64        , "events"  , 1  } ,
	{ "rx_frame"      , 0,    LDMS_V_U64        , "events"  , 1  } ,
	{ "rx_compressed" , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "rx_multicast"  , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "tx_bytes"      , 0,    LDMS_V_U64        , "bytes"   , 1  } ,
	{ "tx_packets"    , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "tx_errs"       , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "tx_drop"       , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{ "tx_fifo"       , 0,    LDMS_V_U64        , "events"  , 1  } ,
	{ "tx_colls"      , 0,    LDMS_V_U64        , "events"  , 1  } ,
	{ "tx_carrier"    , 0,    LDMS_V_U64        , "events"  , 1  } ,
	{ "tx_compressed" , 0,    LDMS_V_U64        , "packets" , 1  } ,
	{0},
};
#define REC_METRICS_LEN (ARRAY_LEN(rec_metrics) - 1)
int rec_metric_ids[REC_METRICS_LEN];

/*
 * Metrics/units references:
 * - linux/net/core/net-procfs.c
 * - linux/include/uapi/linux/if_link.h
 */


ldms_record_t rec_def;
int rec_def_idx;
int netdev_list_mid;
size_t rec_heap_sz;

int niface = 0;
//max number of interfaces we can include.
static char iface[MAXIFACE][20];

static ldms_set_t set;
#define SAMP "procnetdev2"
static FILE *mf = NULL;
static ldmsd_msg_log_f msglog;
static int metric_offset;
static base_data_t base;

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int create_metric_set(base_data_t base)
{
	int rc;
	ldms_schema_t schema;
	size_t heap_sz;

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, "Could not open " SAMP " file "
				"'%s'...exiting\n",
				procfile);
		return ENOENT;
	}

	/* Create a metric set of the required size */
	schema = base_schema_new(base);
	if (!schema) {
		msglog(LDMSD_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = EINVAL;
		goto err;
	}

	/* Location of first metric from proc file */
	metric_offset = ldms_schema_metric_count_get(schema);

	/* Create netdev record definition */
	rec_def = ldms_record_from_template("netdev", rec_metrics, rec_metric_ids);
	rec_heap_sz = ldms_record_heap_size_get(rec_def);
	heap_sz = MAXIFACE * ldms_record_heap_size_get(rec_def);

	/* Add record definition into the schema */
	rec_def_idx = ldms_schema_record_add(schema, rec_def);
	if (rec_def_idx < 0) {
		rc = -rec_def_idx;
		goto err;
	}

	/* Add a list (of records) */
	netdev_list_mid = ldms_schema_metric_list_add(schema, "netdev_list", NULL, heap_sz);
	if (netdev_list_mid < 0) {
		rc = -netdev_list_mid;
		goto err;
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		goto err;
	}

	return 0;

err:

	if (mf)
		fclose(mf);
	mf = NULL;

	return rc;
}


/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set"};

	for (i = 0; i < ARRAY_LEN(deprecated); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			msglog(LDMSD_LERROR, SAMP ": config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return "config name=" SAMP " ifaces=<ifs>\n" \
		BASE_CONFIG_USAGE \
		"    <ifs>           A comma-separated list of interface names (e.g. eth0,eth1)\n"
		"                    Order matters. All ifaces will be included\n"
		"                    whether they exist of not up to a total of MAXIFACE\n";
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char* ifacelist = NULL;
	char* pch = NULL;
	char *saveptr = NULL;
	char *ivalue = NULL;
	void *arg = NULL;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	/* process ifaces */
	ivalue = av_value(avl, "ifaces");
	if (!ivalue)
		goto cfg;

	ifacelist = strdup(ivalue);
	if (!ifacelist) {
		msglog(LDMSD_LERROR, SAMP ": out of memory\n");
		goto err;
	}
	pch = strtok_r(ifacelist, ",", &saveptr);
	while (pch != NULL){
		if (niface >= (MAXIFACE-1)) {
			msglog(LDMSD_LERROR, SAMP ": too many ifaces: <%s>\n",
				pch);
			goto err;
		}
		snprintf(iface[niface], 20, "%s", pch);
		msglog(LDMSD_LDEBUG, SAMP ": added iface <%s>\n", iface[niface]);
		niface++;
		pch = strtok_r(NULL, ",", &saveptr);
	}
	free(ifacelist);
	ifacelist = NULL;

	if (niface == 0)
		goto err;

 cfg:
	base = base_config(avl, SAMP, SAMP, msglog);
	if (!base){
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}

	return 0;

 err:
	if (ifacelist)
		free(ifacelist);
	base_del(base);
	return rc;

}

static int sample(struct ldmsd_sampler *self)
{
	int rc;
	char *s;
	char lbuf[256];
	char curriface[IFNAMSIZ];
	union ldms_value v[REC_METRICS_LEN];
	int i;
	ldms_mval_t lh, rec_inst, name_mval;
	size_t heap_sz;

	if (!set){
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	if (!mf)
		mf = fopen(procfile, "r");
	if (!mf) {
		msglog(LDMSD_LERROR, SAMP ": Could not open /proc/net/dev file "
				"'%s'...exiting\n", procfile);
		return ENOENT;
	}
begin:
	base_sample_begin(base);

	lh = ldms_metric_get(set, netdev_list_mid);

	/* reset device data */
	ldms_list_purge(set, lh);

	fseek(mf, 0, SEEK_SET); //seek should work if get to EOF
	s = fgets(lbuf, sizeof(lbuf), mf);
	s = fgets(lbuf, sizeof(lbuf), mf);

	/* data */
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		char *pch = strchr(lbuf, ':');
		if (pch != NULL){
			*pch = ' ';
		}

		int rc = sscanf(lbuf, "%s %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 "\n", curriface, &v[1].v_u64,
				&v[2].v_u64, &v[3].v_u64, &v[4].v_u64,
				&v[5].v_u64, &v[6].v_u64, &v[7].v_u64,
				&v[8].v_u64, &v[9].v_u64, &v[10].v_u64,
				&v[11].v_u64, &v[12].v_u64, &v[13].v_u64,
				&v[14].v_u64, &v[15].v_u64, &v[16].v_u64);
		if (rc != 17){
			msglog(LDMSD_LINFO, SAMP ": wrong number of "
					"fields in sscanf\n");
			continue;
		}

		if (niface) {
			/* ifaces list was given in config */
			for (i = 0; i < niface; i++) {
				if (strcmp(curriface, iface[i]) == 0)
					goto rec;
			}
			/* not in the ifaces list */
			continue;
		}
	rec:
		rec_inst = ldms_record_alloc(set, rec_def_idx);
		if (!rec_inst)
			goto resize;
		/* iface name */
		name_mval = ldms_record_metric_get(rec_inst, rec_metric_ids[0]);
		snprintf(name_mval->a_char, IFNAMSIZ, "%s", curriface);
		/* metrics */
		for (i = 1; i < REC_METRICS_LEN; i++) {
			ldms_record_set_u64(rec_inst, rec_metric_ids[i], v[i].v_u64);
		}
		ldms_list_append_record(set, lh, rec_inst);
	} while (s);

	base_sample_end(base);
	return 0;
resize:
	/*
	 * We intend to leave the set in the inconsistent state so that
	 * the aggregators are aware that some metrics have not been newly sampled.
	 */
	heap_sz = ldms_set_heap_size_get(base->set) + 2*rec_heap_sz;
	base_set_delete(base);
	set = base_set_new_heap(base, heap_sz);
	if (!set) {
		rc = errno;
		ldmsd_log(LDMSD_LCRITICAL, SAMP " : Failed to create a set with "
						"a bigger heap. Error %d\n", rc);
		return rc;
	}
	goto begin;
}


static void term(struct ldmsd_plugin *self)
{
	if (mf)
		fclose(mf);
	mf = NULL;
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}


static struct ldmsd_sampler procnetdev2_plugin = {
	.base = {
		.name = SAMP,
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
	return &procnetdev2_plugin.base;
}
