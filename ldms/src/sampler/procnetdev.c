/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
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
 * \file procnetdev.c
 * \brief /proc/net/dev data provider
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
#include "ldms.h"
#include "ldmsd.h"


#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

#define PROC_FILE "/proc/net/dev"
static char *procfile = PROC_FILE;
#define NVARS 16
static char varname[][30] =
{"rx_bytes", "rx_packets", "rx_errs", "rx_drop", "rx_fifo", "rx_frame",
	"rx_compressed", "rx_multicast", "tx_bytes", "tx_packets", "tx_errs",
	"tx_drop", "tx_fifo", "tx_colls", "tx_carrier", "tx_compressed"};

int niface = 0;
//max number of interfaces we can include. FIXME: alloc as added
#define MAXIFACE 10
static char iface[MAXIFACE][20];

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
static uint64_t counter;
uint64_t comp_id;

struct kw {
	char *token;
	int (*action)(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg);
};

static int kw_comparator(const void *a, const void *b)
{
	struct kw *_a = (struct kw *)a;
	struct kw *_b = (struct kw *)b;
	return strcmp(_a->token, _b->token);
}


static ldms_set_t get_set()
{
	return set;
}

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	union ldms_value v[NVARS];
	int rc, metric_count, metric_no;
	char *s;
	char lbuf[256];
	char metric_name[128];
	char curriface[20];
	int i,j;


	mf = fopen(procfile, "r");
	if (!mf) {
		msglog("Could not open /proc/net/dev file '%s'...exiting\n",
				procfile);
		return ENOENT;
	}

	/*
	 * Process the file once first to determine the metric set size.
	 */

	fseek(mf, 0, SEEK_SET);

	//first and second lines are header
	//we are currently assuming we know the header names....
	s = fgets(lbuf, sizeof(lbuf), mf);
	if (!s){
		msglog("procnetdev: not reading header line 1\n");
		return ENOENT;
	}
	s = fgets(lbuf, sizeof(lbuf), mf);
	if (!s){
		msglog("procnetdev: not reading header line 2\n");
		return ENOENT;
	}
	int usedifaces = 0;
	tot_meta_sz = 0;
	tot_data_sz = 0;
	metric_count = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		if (usedifaces == niface)
			continue; //must get to EOF for seek to work

		char *pch = strchr(lbuf, ':');
		if (pch != NULL){
			*pch = ' ';
		}

		int rc = sscanf(lbuf, "%s %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", curriface,&v[0].v_u64, &v[1].v_u64, &v[2].v_u64, &v[3].v_u64, &v[4].v_u64, &v[5].v_u64, &v[6].v_u64, &v[7].v_u64, &v[8].v_u64, &v[9].v_u64, &v[10].v_u64, &v[11].v_u64, &v[12].v_u64, &v[13].v_u64, &v[14].v_u64, &v[15].v_u64);
		if (rc != 17){
			msglog("Procnetdev: wrong number of fields in sscanf\n");
			continue;
		}
		for (j = 0; j < niface; j++){
			if (strcmp(iface[j],curriface) == 0){
				for (i = 0; i < NVARS; i++){
					snprintf(metric_name, 128, "%s#%s",
							varname[i], curriface);
					rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
					tot_meta_sz += meta_sz;
					tot_data_sz += data_sz;
					metric_count++;
				}
				usedifaces++;
				break;
			}
		} //for
	} while (s);


	/* Create a metric set of the required size */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;

	/*
	 * Process the file again to define all the metrics.
	 */
	fseek(mf, 0, SEEK_SET);
	s = fgets(lbuf, sizeof(lbuf), mf);
	s = fgets(lbuf, sizeof(lbuf), mf);
	usedifaces = 0;
	metric_no = 0;
	do {

		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		if (usedifaces == niface)
			continue; //must get to EOF for seek to work

		char *pch = strchr(lbuf, ':');
		if (pch != NULL){
			*pch = ' ';
		}

		int rc = sscanf(lbuf, "%s %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", curriface, &v[0].v_u64, &v[1].v_u64, &v[2].v_u64, &v[3].v_u64, &v[4].v_u64, &v[5].v_u64, &v[6].v_u64, &v[7].v_u64, &v[8].v_u64, &v[9].v_u64, &v[10].v_u64, &v[11].v_u64, &v[12].v_u64, &v[13].v_u64, &v[14].v_u64, &v[15].v_u64);
		if (rc != 17){
			msglog("Procnetdev: wrong number of fields in sscanf\n");
			continue;
		}
		for (j = 0; j < niface; j++){
			if (strcmp(iface[j],curriface) == 0){
				for (i = 0; i < NVARS; i++){
					snprintf(metric_name, 128, "%s#%s",
							varname[i], curriface);
					metric_table[metric_no] = ldms_add_metric(set, metric_name, LDMS_V_U64);
					if (!metric_table[metric_no]){
						rc = ENOMEM;
						goto err;
					}
					ldms_set_user_data(metric_table[metric_no],
							comp_id);
					metric_no++;
				}
				usedifaces++;
				break;
			} //if
		} //for
	} while (s);

	return 0;

err:
	ldms_set_release(set);
	return rc;
}

static int add_iface(struct attr_value_list *kwl, struct attr_value_list *avl, void* arg){
	if (niface == (MAXIFACE-1)){
		msglog("Procnetdev too many ifaces -- increase array size\n");
		return EINVAL;
	}

	char *value;

	value = av_value(avl, "iface");
	if (value){
		snprintf(iface[niface], 20, "%s", value);
		niface++;
	}

	return 0;

}


static int init(struct attr_value_list *kwl, struct attr_value_list *avl, void* arg){
	/* Set the compid and create the metric set */

	char *value;

	value = av_value(avl, "component_id");
	if (value)
		comp_id = strtol(value, NULL, 0);

	value = av_value(avl, "set");
	if (!value)
		return EINVAL;

	return create_metric_set(value);
}

struct kw kw_tbl[] = {
	{ "add", add_iface },
	{ "init", init },
};

static const char *usage(void)
{
	return
		"config name=procnetdev action=add iface=<iface>\n"
		"    iface       Interface name (e.g., eth0)\n"
		"config name=procnetdev action=config component_id=<comp_id> set=<setname>\n"
		"    comp_id     The component id value.\n"
		"    setname     The set name.\n";

}


/**
 * \brief Configuration
 *
 * - config procnetdev action=add iface=eth0
 *  (repeat this for each iface)
 * - config procnetdev action=init component_id=<value> set=<setname>
 *  (init must be after all the ifaces are added since it adds the metric set)
 *
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{

	struct kw *kw;
	struct kw key;
	int rc;
	char *action = av_value(avl, "action");

	if (!action)
		goto err0;

	key.token = action;
	kw = bsearch(&key, kw_tbl, ARRAY_SIZE(kw_tbl),
			sizeof(*kw), kw_comparator);
	if (!kw)
		goto err1;

	rc = kw->action(kwl, avl, NULL);
	if (rc)
		goto err2;
	return 0;
err0:
	msglog(usage());
	goto err2;
err1:
	msglog("Invalid configuration keyword '%s'\n", action);
err2:
	return 0;
}

static int sample(void)
{
	char *s;
	char lbuf[256];
	char curriface[20];
	union ldms_value vtemp, v[NVARS];
	struct timespec time1;
	int i, j, metric_no;


	if (!set){
		msglog("procnetdev: plugin not initialized\n");
		return EINVAL;
	}

	metric_no = 0;
	fseek(mf, 0, SEEK_SET); //seek should work if get to EOF

	if (mf) fclose(mf);
	mf = fopen(procfile, "r");
	if (!mf) {
		msglog("Could not open /proc/net/dev file '%s'...exiting\n", procfile);
		return ENOENT;
	}

	int usedifaces = 0;
	s = fgets(lbuf, sizeof(lbuf), mf);
	s = fgets(lbuf, sizeof(lbuf), mf);
	//data
	ldms_begin_transaction(set);
	metric_no = 0;
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;

		if (usedifaces == niface)
			continue; //must get to EOF for seek to work

		char *pch = strchr(lbuf, ':');
		if (pch != NULL){
			*pch = ' ';
		}

		int rc = sscanf(lbuf, "%s %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", curriface, &v[0].v_u64, &v[1].v_u64, &v[2].v_u64, &v[3].v_u64, &v[4].v_u64, &v[5].v_u64, &v[6].v_u64, &v[7].v_u64, &v[8].v_u64, &v[9].v_u64, &v[10].v_u64, &v[11].v_u64, &v[12].v_u64, &v[13].v_u64, &v[14].v_u64, &v[15].v_u64);
		if (rc != 17){
			msglog("Procnetdev: wrong number of fields in sscanf\n");
			continue;
		}

		//note: ifaces will be in the same order each time
		//so we can just include/skip w/o have to keep track of which on we are on
		for (j = 0; j < niface; j++){
			if (strcmp(curriface,iface[j]) == 0){ //NOTE: small number so no conflicts (eg., eth1 and eth10)
				for (i = 0; i < NVARS; i++){
					ldms_set_metric(metric_table[metric_no++], &v[i]);
				}
				usedifaces++;
				break;
			} //if
		} //for
	} while (s);
	ldms_end_transaction(set);

	return 0;
}


static void term(void)
{
	if (mf)
		fclose(mf);
	mf = 0;

	if (set)
		ldms_destroy_set(set);
	set = NULL;
}


static struct ldmsd_sampler procnetdev_plugin = {
	.base = {
		.name = "procnetdev",
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
	return &procnetdev_plugin.base;
}
