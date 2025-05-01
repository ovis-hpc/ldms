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
#include "ldmsd_plug_api.h"
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

#define MAXIFACE 256
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
/*
 * Metrics/units references:
 * - linux/net/core/net-procfs.c
 * - linux/include/uapi/linux/if_link.h
 */

#define SAMP "procnetdev2"
typedef struct procnetdev2_s {
        ovis_log_t mylog;

	int rec_def_idx;
	int rec_metric_ids[REC_METRICS_LEN];
	size_t rec_heap_sz;
	size_t heap_sz;

	int netdev_list_mid;

	int ifcount;
	char iface[MAXIFACE][20];
	int excount;
	char exclude[MAXIFACE][20];

	FILE *mf;
	base_data_t base;
} *procnetdev2_t;

static int create_metric_set(procnetdev2_t p, base_data_t base)
{
	ldms_schema_t schema;
	ldms_record_t rec_def;
	int rc;

	p->mf = fopen(procfile, "r");
	if (!p->mf) {
		ovis_log(p->mylog, OVIS_LERROR, "Could not open " SAMP " file "
				"'%s'...exiting\n",
				procfile);
		return ENOENT;
	}

	/* Create a metric set of the required size */
	schema = base_schema_new(p->base);
	if (!schema) {
		ovis_log(p->mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, p->base->schema_name, errno);
		rc = EINVAL;
		goto err1;
	}

	/* Create netdev record definition */
	rec_def = ldms_record_from_template("netdev", rec_metrics, p->rec_metric_ids);
	if (!rec_def) {
		rc = errno;
		goto err2;
	}
	p->rec_heap_sz = ldms_record_heap_size_get(rec_def);
	p->heap_sz = MAXIFACE * ldms_record_heap_size_get(rec_def);

	/* Add record definition into the schema */
	p->rec_def_idx = ldms_schema_record_add(schema, rec_def);
	if (p->rec_def_idx < 0) {
		rc = -p->rec_def_idx;
		goto err3;
	}

	/* Add a list (of records) */
	p->netdev_list_mid = ldms_schema_metric_list_add(schema, "netdev_list", NULL, p->heap_sz);
	if (p->netdev_list_mid < 0) {
		rc = -p->netdev_list_mid;
		goto err2;
	}

	base_set_new(p->base);
	if (!p->base->set) {
		rc = errno;
		goto err2;
	}

	return 0;
err3:
	/* Only manually delete rec_def when it has not yet been added
	 * to the schema */
	ldms_record_delete(rec_def);
err2:
	base_schema_delete(p->base);
	p->base = NULL;
err1:
	if (p->mf)
		fclose(p->mf);
	p->mf = NULL;

	return rc;
}


/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg,
                        ovis_log_t log)
{
	char *value;
	int i;

	char* deprecated[] = {"set"};

	for (i = 0; i < ARRAY_LEN(deprecated); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(log, OVIS_LERROR, "config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return "config inst=NAME plugin=" SAMP " ifaces=IFS\n" \
		BASE_CONFIG_USAGE \
		"    <ifs>           A comma-separated list of interface names (e.g. eth0,eth1)\n"
		"                    Order matters. All ifaces will be included\n"
		"                    whether they exist of not up to a total of MAXIFACE\n";
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	procnetdev2_t p = ldmsd_plug_ctxt_get(handle);
	char* ifacelist = NULL;
	char *exclude = NULL;
	char *ivalue = NULL;
	char *saveptr;
	char *iface;
	void *arg = NULL;
	size_t ifcount;
	size_t excount;
	int rc;

	rc = config_check(kwl, avl, arg, p->mylog);
	if (rc != 0)
		return rc;

	if (p->base) {
		ovis_log(p->mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	/* process ifaces */
	ivalue = av_value(avl, "ifaces");
	if (!ivalue)
		goto process_exclude;
	ifacelist = strdup(ivalue);
	if (!ifacelist) {
		ovis_log(p->mylog, OVIS_LCRIT, "Out of memory\n");
		goto err;
	}

	/* Process interface list */
	ifcount = 0;
	iface = strtok_r(ifacelist, ",", &saveptr);
	while (iface) {
		if (ifcount >= (MAXIFACE)) {
			ovis_log(p->mylog, OVIS_LERROR,
				"Too many ifaces: <%s>\n", iface);
			goto err;
		}
		strncpy(p->iface[ifcount++], iface, 20);
		ovis_log(p->mylog, OVIS_LDEBUG,
			"Added iface <%s>\n", iface);
		iface = strtok_r(NULL, ",", &saveptr);
	}
	p->ifcount = ifcount;

 process_exclude:
	/* process excludes */
	excount = 0;
	ivalue = av_value(avl, "exclude");
	if (!ivalue)
		goto cfg;
	exclude = strdup(ivalue);
	if (!exclude) {
		ovis_log(p->mylog, OVIS_LCRIT, "out of memory\n");
		goto err;
	}
	iface = strtok_r(exclude, ",", &saveptr);
	excount = 0;
	while (iface) {
		if (excount >= (MAXIFACE)) {
			ovis_log(p->mylog, OVIS_LERROR,
				"Too many exclude: <%s>\n", ivalue);
			goto err;
		}
		strncpy(p->exclude[excount++], iface, 20);
		ovis_log(p->mylog, OVIS_LDEBUG,
			"Excluded iface <%s>\n", iface);
		iface = strtok_r(NULL, ",", &saveptr);
	}
	p->excount = excount;

 cfg:
	p->base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, p->mylog);
	if (!p->base){
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_set(p, p->base);
	if (rc) {
		ovis_log(p->mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}

	return 0;

 err:
	if (ifacelist)
		free(ifacelist);
	base_del(p->base);
	return rc;

}

static int sample(ldmsd_plug_handle_t handle)
{
	procnetdev2_t p = ldmsd_plug_ctxt_get(handle);
	int rc;
	char *s;
	char lbuf[256];
	char curriface[IFNAMSIZ];
	union ldms_value v[REC_METRICS_LEN];
	int i;
	ldms_mval_t lh, rec_inst, name_mval;

	if (!p->base) {
		ovis_log(p->mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	if (!p->mf)
		p->mf = fopen(procfile, "r");
	if (!p->mf) {
		ovis_log(p->mylog, OVIS_LERROR, "Could not open /proc/net/dev file "
				"'%s'...exiting\n", procfile);
		return ENOENT;
	}
begin:
	base_sample_begin(p->base);

	lh = ldms_metric_get(p->base->set, p->netdev_list_mid);

	/* reset device data */
	ldms_list_purge(p->base->set, lh);

	fseek(p->mf, 0, SEEK_SET); //seek should work if get to EOF
	s = fgets(lbuf, sizeof(lbuf), p->mf);
	s = fgets(lbuf, sizeof(lbuf), p->mf);

	/* data */
	do {
		s = fgets(lbuf, sizeof(lbuf), p->mf);
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
			ovis_log(p->mylog, OVIS_LINFO,
				"wrong number of fields in sscanf\n");
			continue;
		}

		if (p->ifcount) {
			/* ifaces list was given in config */
			for (i = 0; i < p->ifcount; i++) {
				if (strcmp(curriface, p->iface[i]) == 0)
					goto rec;
			}
			/* not in the ifaces list */
			continue;
		}

		if (p->excount) {
			/* exclude list was given in the config */
			int excluded = 0;
			for (i = 0; i < p->excount && !excluded; i++) {
				if (0 == strcmp(curriface, p->exclude[i]))
					excluded = 1;
			}
			if (excluded)
				continue;
		}
	rec:
		rec_inst = ldms_record_alloc(p->base->set, p->rec_def_idx);
		if (!rec_inst)
			goto resize;
		/* iface name */
		name_mval = ldms_record_metric_get(rec_inst, p->rec_metric_ids[0]);
		snprintf(name_mval->a_char, IFNAMSIZ, "%s", curriface);
		/* metrics */
		for (i = 1; i < REC_METRICS_LEN; i++) {
			ldms_record_set_u64(rec_inst, p->rec_metric_ids[i], v[i].v_u64);
		}
		ldms_list_append_record(p->base->set, lh, rec_inst);
	} while (s);

	base_sample_end(p->base);
	return 0;
resize:
	/*
	 * We intend to leave the set in the inconsistent state so that
	 * the aggregators are aware that some metrics have not been newly sampled.
	 */
	base_set_delete(p->base);
	base_set_new_heap(p->base, p->heap_sz);
	if (!p->base->set) {
		rc = errno;
		ovis_log(p->mylog, OVIS_LCRITICAL, SAMP " : Failed to create a set with "
						"a bigger heap. Error %d\n", rc);
		return rc;
	}
	goto begin;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	procnetdev2_t p = calloc(1, sizeof(*p));
	if (!p) {
                return ENOMEM;
	}

        p->mylog = ldmsd_plug_log_get(handle);
        ldmsd_plug_ctxt_set(handle, p);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	procnetdev2_t p = ldmsd_plug_ctxt_get(handle);
	if (p->mf) {
		fclose(p->mf);
		p->mf = NULL;
	}
	base_set_delete(p->base);
	base_del(p->base);
	free(p);
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base.type = LDMSD_PLUGIN_SAMPLER,
	.base.flags = LDMSD_PLUGIN_MULTI_INSTANCE,
	.base.config = config,
	.base.usage = usage,
	.base.constructor = constructor,
	.base.destructor = destructor,

	.sample = sample,
};
