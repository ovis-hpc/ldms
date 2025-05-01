/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2821 Open Grid Computing, Inc. All rights reserved.
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
 * \file procnet.c
 * \brief /proc/net/dev data provider with set per device
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
#include "sampler_base.h"
#include <pthread.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;

#define PROC_FILE "/proc/net/dev"
static char *procfile = PROC_FILE;
#define NVARS 16
static char *varname[] =
{"rx_bytes", "rx_packets", "rx_errs", "rx_drop", "rx_fifo", "rx_frame",
	"rx_compressed", "rx_multicast", "tx_bytes", "tx_packets", "tx_errs",
	"tx_drop", "tx_fifo", "tx_colls", "tx_carrier", "tx_compressed"};

struct port {
	char name[20]; /* device name */
	ldms_set_t set;
	uint64_t last_sum; /* sum of integer metrics at last sampling. rollover in this sum is ok. */
	int meta_done; /* have we set the device metric */
	uint64_t update;
};
int niface = 0;
//max number of interfaces we can include. TODO: alloc as added

#define MAXIFACE 21
static struct port ports[MAXIFACE];
static int n_ports; /* number of ports in use */
static char ignore_port[MAXIFACE][20];
static size_t n_ignored; /* number of names ignored */
static int configured;
static int termed;

#define SAMP "procnet"
static FILE *mf = NULL;
static int metric_offset;
static base_data_t base;

static ovis_log_t mylog;

struct kw {
	char *token;
	int (*action)(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg);
};

static void procnet_reset()
{
	if (mf)
		fclose(mf);
	mf = NULL;
	int j;
	for (j = 0; j < n_ports; j++) {
		ldms_set_t set = ports[j].set;
		if (set) {
                        const char *tmp = ldms_set_instance_name_get(set);
                        ldmsd_set_deregister(tmp, base->cfg_name);
                        ldms_set_unpublish(set);
                        ldms_set_delete(set);
		}
		memset(&ports[j], 0, sizeof(ports[0]));
	}
	for (j = 0; j < MAXIFACE; j++) {
		memset(ignore_port[j], 0, sizeof(ignore_port[0]));
	}
	if (base) {
		free(base->instance_name);
		base->instance_name = NULL;
		base_del(base);
		base = NULL;
	}
	metric_offset = 0;
	n_ignored = 0;
	n_ports = 0;
	configured = 0;
}

static int add_port(const char *name)
{
	ovis_log(mylog, OVIS_LDEBUG, "adding port %s.\n", name);
	/* temporarily override default instance name behavior */
	char *tmp = base->instance_name;
	size_t len = strlen(tmp);
	int rc;
	base->instance_name = malloc( len + 20);
	if (!base->instance_name) {
		base->instance_name = tmp;
		rc = ENOMEM;
		goto err;
	}
	/* override single set assumed in sampler_base api */
	snprintf(base->instance_name, len + 20, "%s/%s", tmp, name);
	ldms_set_t set = base_set_new(base);
	if (!set) {
		ovis_log(mylog, OVIS_LERROR, "failed to make %s set for %s\n",
			name, SAMP);
		rc = errno;
		base->instance_name = tmp;
		goto err;
	}
	base_auth_set(&base->auth, set);
	ports[n_ports].set = set;
	strncpy(ports[n_ports].name, name, sizeof(ports[n_ports].name));
	base->set = NULL;
	free(base->instance_name);
	base->instance_name = tmp;
	n_ports++;
	rc = 0;
err:
	return rc;
}

static int create_metric_schema(base_data_t base)
{
	int rc;
	ldms_schema_t schema;
	int  j;

	mf = fopen(procfile, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open " SAMP " file "
				"'%s'...exiting\n",
				procfile);
		return ENOENT;
	}

	/* Create a metric set of the required size */
	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = EINVAL;
		goto err;
	}

	/* Location of first metric from proc file */
	metric_offset = ldms_schema_metric_count_get(schema);

	rc = ldms_schema_metric_array_add(schema, "device", LDMS_V_CHAR_ARRAY, sizeof(ports[0].name));
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, "out of memory: device\n");
		rc = ENOMEM;
		goto err;
	}
	rc = ldms_schema_metric_add(schema, "update", LDMS_V_U64);
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, "out of memory: update\n");
		rc = ENOMEM;
		goto err;
	}
	for (j = 0; j < NVARS; j++) {
		rc = ldms_schema_metric_add(schema, varname[j], LDMS_V_U64);
		if (rc < 0) {
			rc = ENOMEM;
			goto err;
		}
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

	for (i = 0; i < ARRAY_SIZE(deprecated); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			ovis_log(mylog, OVIS_LERROR, "config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return "config name=" SAMP " exclude_ports=<devs>\n" \
		BASE_CONFIG_USAGE \
		"    <devs>          A comma-separated list of interfaces to be ignored.\n"
		"                    By default all active interfaces discovered will be reported.\n";
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char* ignorelist = NULL;
	char* pch = NULL;
	char *saveptr = NULL;
	char *ivalue = NULL;
	void *arg = NULL;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}
	pthread_mutex_lock(&cfg_lock);

	if (termed) {
		rc = 0;
		goto err1;
	}
	if (configured) {
		procnet_reset();
		ovis_log(mylog, OVIS_LDEBUG, "reconfiguring.\n");
	}
	n_ignored = 0;
	ivalue = av_value(avl, "exclude_ports");
	if (ivalue == NULL)
		ivalue = "";
	ignorelist = strdup(ivalue);
	if (!ignorelist) {
		ovis_log(mylog, OVIS_LERROR, "out of memory\n");
		goto err;
	}
	pch = strtok_r(ignorelist, ",", &saveptr);
	while (pch != NULL){
		if (n_ignored >= (MAXIFACE-1)) {
			ovis_log(mylog, OVIS_LERROR, "too many devices being ignored: <%s>\n",
				pch);
			goto err;
		}
		snprintf(ignore_port[n_ignored], sizeof(ignore_port[0]), "%s", pch);
		ovis_log(mylog, OVIS_LDEBUG, "ignoring net device <%s>\n", pch);
		n_ignored++;
		pch = strtok_r(NULL, ",", &saveptr);
	}
	free(ignorelist);
	ignorelist = NULL;

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base) {
		rc = EINVAL;
		goto err;
	}

	rc = create_metric_schema(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric schema.\n");
		goto err;
	}
	pthread_mutex_unlock(&cfg_lock);
	configured = 1;
	return 0;

err:
	free(ignorelist);
	base_del(base);
err1:
	pthread_mutex_unlock(&cfg_lock);
	return rc;
}

static int update_port(int j, uint64_t msum, union ldms_value *v)
{
	ldms_set_t set = ports[j].set;
	if (!set || j < 0 || j >= MAXIFACE)
		return EINVAL;
	base->set = set;
	int metric_no = metric_offset;
	base_sample_begin(base);
	if (!ports[j].meta_done) {
		ldms_metric_array_set_str( set, metric_no,
			ports[j].name);
		ports[j].meta_done = 1;
	}
	metric_no++;
	ldms_metric_set_u64(set, metric_no, ports[j].update);
	metric_no++;
	int i;
	for (i = 0; i < NVARS; i++) {
		ldms_metric_set(set, metric_no++, &v[i]);
	}
	ports[j].update++;
	base_sample_end(base);
	base->set = NULL;
	return 0;
}

static int sample(ldmsd_plug_handle_t handle)
{
	char *s;
	char lbuf[256];
	char curriface[20];
	union ldms_value v[NVARS];
	int j;
	int rc;

	pthread_mutex_lock(&cfg_lock);

	if (!configured) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		rc = 0;
		goto err;
	}

	if (!mf)
		mf = fopen(procfile, "r");
	if (!mf) {
		ovis_log(mylog, OVIS_LERROR, "Could not open /proc/net/dev file "
				"'%s'...exiting\n", procfile);
		rc = ENOENT;
		goto err;
	}

	fseek(mf, 0, SEEK_SET); //seek should work if get to EOF
	/* consume headers */
	s = fgets(lbuf, sizeof(lbuf), mf);
	s = fgets(lbuf, sizeof(lbuf), mf);

	/* parse all data */
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		char *pch = strchr(lbuf, ':');
		if (pch != NULL){
			*pch = ' ';
		}

		rc = sscanf(lbuf, "%s %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
				" %" PRIu64 "\n", curriface, &v[0].v_u64,
				&v[1].v_u64, &v[2].v_u64, &v[3].v_u64,
				&v[4].v_u64, &v[5].v_u64, &v[6].v_u64,
				&v[7].v_u64, &v[8].v_u64, &v[9].v_u64,
				&v[10].v_u64, &v[11].v_u64, &v[12].v_u64,
				&v[13].v_u64, &v[14].v_u64, &v[15].v_u64);
		if (rc != 17) {
			ovis_log(mylog, OVIS_LINFO, "wrong number of "
					"fields in sscanf. skipping line %s\n", lbuf);
			continue;
		}
		for (j = 0; j < n_ignored; j++) {
			if (!strcmp(curriface, ignore_port[j]))
				goto skip;
		}

		uint64_t msum = 0;
		/* don't care if msum rolls; just want to know if it
		 * changed from prior or is 0. */
		for (j = 0; j < NVARS; j++)
			msum += v[j].v_u64;

		int done = 0;
		for (j = 0; j < n_ports; j++) {
			if (strcmp(curriface, ports[j].name) == 0) {
				if (msum != ports[j].last_sum) {
					rc = update_port(j, msum, v);
					if (rc)
						goto err;
				}
				done = 1;
				break;
			}
		}
		if (!done && msum) {
			if (n_ports >= MAXIFACE) {
				ovis_log(mylog, OVIS_LERROR, "Cannot add %d-th port %s. "
					"Too many ports.\n", MAXIFACE+1, curriface);
				rc = ENOMEM;
				goto err;
			}
			/* add discovered interface if it's active. */
			rc = add_port(curriface);
			if (rc)
				goto err;
			rc = update_port(n_ports-1, msum, v);
			if (rc)
				goto err;
		}
	skip:
		continue;
	} while (s);
	rc = 0;
err:
	pthread_mutex_unlock(&cfg_lock);
	return rc;
}


static void term(ldmsd_plug_handle_t handle)
{
	pthread_mutex_lock(&cfg_lock);
	procnet_reset();
	termed = 1;
	pthread_mutex_unlock(&cfg_lock);
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
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};

static void __attribute__ ((destructor)) procnet_plugin_fini(void);
static void procnet_plugin_fini()
{
	term(NULL);
}
