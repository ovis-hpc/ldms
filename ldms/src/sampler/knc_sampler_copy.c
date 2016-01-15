/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014-2015 Sandia Corporation. All rights reserved.
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
 * \file knc_sampler.c
 * \brief provider of data from libmicmgmt
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
#include <pthread.h>
#include <miclib.h>
#include "ldms.h"
#include "ldmsd.h"


//FIXME: can't have two sets in the same sampler yet. Making these two different samplers.
#define NUM_MICS 2
static struct mic_device *mdh_= NULL;
static unsigned int dev_num;
static struct mic_core_util *cutil_ = NULL;
static uint16_t num_cores = 0;
static uint16_t threads_core = 0;
static int core_idx_init = -1;
static struct mic_memory_util_info *mutil_ = NULL;
static int memory_idx_init = -1;
static struct mic_thermal_info *tutil_ = NULL;
static int thermal_idx_init = -1;
static struct mic_power_util_info *putil_ = NULL;
static int power_idx_init = -1;
static uint64_t* counters;

static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;
static char *default_schema_name = "knc";
static ldms_set_t set = NULL;

static int updateCoreUtil(struct mic_device* mdh, struct mic_core_util** cutil){
	int ret;

	ret = mic_update_core_util(mdh, *cutil);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't update core util: %d\n",
			ret);
		return EINVAL;
	}

	return ret;
}

static int updatePowerUtilInfo(struct mic_device* mdh, struct mic_power_util_info** putil){
	int ret;

	if (*putil)
		ret = mic_free_power_utilization_info(*putil);

	ret = mic_get_power_utilization_info(mdh, putil);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't alloc power info: %d\n", ret);
		return EINVAL;
	}

	return 0;
}

static int updateThermalInfo(struct mic_device* mdh, struct mic_thermal_info** tutil){
	int ret;

	if (*tutil)
		ret = mic_free_thermal_info(*tutil);

	ret = mic_get_thermal_info(mdh, tutil);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't alloc thermal info: %d\n", ret);
		return EINVAL;
	}

	return 0;
}

static int updateMemUtilInfo(struct mic_device* mdh, struct mic_memory_util_info **mutil){
	int ret;

	if (*mutil)
		ret = mic_free_memory_utilization_info(*mutil);

	mic_get_memory_utilization_info(mdh, mutil);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't alloc memutil info: %d\n", ret);
		return EINVAL;
	}

	return 0;
}

static int initCoreUtil(struct mic_device* mdh, struct mic_core_util** cutil, int devn){
	int i;
	int ret;

	ret = mic_alloc_core_util(cutil);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't alloc core util: %d\n", ret);
		return EINVAL;
	}

	ret = updateCoreUtil(mdh, cutil);
	if (ret != 0){
		msglog(LDMSD_LERROR, "knc_sampler: Can't update core util: %d\n", ret);
		return EINVAL;
	}
	ret = mic_get_num_cores(*cutil, &num_cores);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't get numcores: %d\n", ret);
		num_cores = 0;
		return EINVAL;
	}
	ret = mic_get_threads_core(*cutil,&threads_core);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't get threads per core: %d\n", ret);
		threads_core = 0;
		return EINVAL;
	}

	msglog(LDMSD_LDEBUG, "knc_sampler: mic %d -  %d cores with %d threads per core\n",
	       devn, (int)num_cores, (int)threads_core);

	counters = (uint64_t*) malloc((int)(num_cores)*sizeof(uint64_t));
	if (counters == NULL)
		return ENOMEM;

	return 0;
}

static void freeCoreUtil(){
	int i, j;
	int ret;

	if (cutil_){
		ret = mic_free_core_util(cutil_);
		if (ret != E_MIC_SUCCESS){
			msglog(LDMSD_LERROR, "knc_sampler: Can't free core util: %d\n", ret);
		}
		cutil_ = NULL;
	}

}

static void freePowerUtilInfo(){
	int i;
	int ret;

	if (putil_){
		ret = mic_free_power_utilization_info(putil_);
		if (ret != E_MIC_SUCCESS){
			msglog(LDMSD_LERROR, "knc_sampler: Can't free power info: %d\n", ret);
		}
		putil_ = NULL;
	}

}

static void freeThermalInfo(){
	int i;
	int ret;

	if (tutil_){
		ret = mic_free_thermal_info(tutil_);
		if (ret != E_MIC_SUCCESS){
			msglog(LDMSD_LERROR, "knc_sampler: Can't free thermal info: %d\n", ret);
		}
		tutil_ = NULL;
	}

}

static void freeMemUtilInfo(){
	int i;
	int ret;

	if (mutil_){
		ret = mic_free_memory_utilization_info(mutil_);
		if (ret != E_MIC_SUCCESS){
			msglog(LDMSD_LERROR, "knc_sampler: Can't free mem util info: %d\n", ret);
		}
		mutil_ = NULL;
	}

}

static int addMetrics_PowerUtil(ldms_schema_t schema, int* pidx){
	int rc;

	//	msglog(LDMSD_LDEBUG, "knc_sampler: adding power metrics\n");

	rc = ldms_schema_metric_add(schema, "Power", LDMS_V_U32);
	if (rc < 0)
		return ENOMEM;
	*pidx = rc;

	return 0;
}


static void getPowerUtilInfoVals(struct mic_power_util_info* putil, int metric_no){
	int ret;
	union ldms_value v;

	ret = mic_get_total_power_readings_w0(putil, &v.v_u32);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't get power %d\n", ret);
		v.v_u32 = 0;
	}

	ldms_metric_set(set, metric_no, &v);
	metric_no++;
}

static int addMetrics_Thermal(ldms_schema_t schema, int* tidx){
	int rc;

	//	msglog(LDMSD_LDEBUG, "knc_sampler: adding thermal metrics\n");

	rc = ldms_schema_metric_add(schema, "CPUTemp", LDMS_V_U32);
	if (rc < 0)
		return ENOMEM;
	*tidx = rc;

	rc = ldms_schema_metric_add(schema, "MemTemp", LDMS_V_U16);
	if (rc < 0)
		return ENOMEM;

	rc = ldms_schema_metric_add(schema, "FanInTemp", LDMS_V_U16);
	if (rc < 0)
		return ENOMEM;

	rc = ldms_schema_metric_add(schema, "FanOutTemp", LDMS_V_U16);
	if (rc < 0)
		return ENOMEM;

	rc = ldms_schema_metric_add(schema, "CoreRailTemp", LDMS_V_U16);
	if (rc < 0)
		return ENOMEM;

	rc = ldms_schema_metric_add(schema, "UnCoreRailTemp", LDMS_V_U16);
	if (rc < 0)
		return ENOMEM;

	rc = ldms_schema_metric_add(schema, "MemRailTemp", LDMS_V_U16);
	if (rc < 0)
		return ENOMEM;

	return 0;
}

static void getThermalInfoVals(struct mic_thermal_info* tutil, int metric_no){
	union ldms_value v16;
	union ldms_value v32;
	int ret;

	ret = mic_get_die_temp(tutil, &v32.v_u32);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't get die temp %d\n", ret);
		v32.v_u32 = 0;
	}
	ldms_metric_set(set, metric_no, &v32);
	metric_no++;

	ret = mic_get_gddr_temp(tutil, &v16.v_u16);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't get mem temp %d\n", ret);
		v16.v_u16 = 0;
	}
	ldms_metric_set(set, metric_no, &v16);
	metric_no++;

	ret = mic_get_fanin_temp(tutil, &v16.v_u16);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't get fanin temp %d\n", ret);
		v16.v_u16 = 0;
	}
	ldms_metric_set(set, metric_no, &v16);
	metric_no++;

	ret = mic_get_fanout_temp(tutil, &v16.v_u16);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "knc_sampler: Can't get fanout temp %d\n", ret);
		v16.v_u16 = 0;
	}
	ldms_metric_set(set, metric_no, &v16);
	metric_no++;

	ret = mic_get_vccp_temp(tutil, &v16.v_u16);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get corerail temp %d\n", ret);
		v16.v_u16 = 0;
	}
	ldms_metric_set(set, metric_no, &v16);
	metric_no++;

	ret = mic_get_vddg_temp(tutil, &v16.v_u16);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get uncorerail temp %d\n", ret);
		v16.v_u16 = 0;
	}
	ldms_metric_set(set, metric_no, &v16);
	metric_no++;

	ret = mic_get_vddq_temp(tutil, &v16.v_u16);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get memrail temp %d\n", ret);
		v16.v_u16 = 0;
	}
	ldms_metric_set(set, metric_no, &v16);
	metric_no++;

}


static int addMetrics_MemUtil(ldms_schema_t schema, int* midx){
	int rc;

	//	msglog(LDMSD_LDEBUG, "knc_sampler: adding memory metrics\n");

	rc = ldms_schema_metric_add(schema, "TotMem", LDMS_V_U32);
	if (rc < 0)
		return ENOMEM;
	*midx = rc;

	rc = ldms_schema_metric_add(schema, "AvailMem", LDMS_V_U32);
	if (rc < 0)
		return ENOMEM;

	return 0;
}


static void getMemUtilInfoVals(struct mic_memory_util_info* mutil, int metric_no){
	int ret;
	union ldms_value v;

	ret = mic_get_total_memory_size(mutil, &v.v_u32);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get tot mem %d\n", ret);
		v.v_u32 = 0;
	}
	ldms_metric_set(set, metric_no, &v);
	metric_no++;

	ret = mic_get_available_memory_size(mutil, &v.v_u32);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get avail mem %d\n", ret);
	}
	ldms_metric_set(set, metric_no, &v);

}

static int addMetrics_CoreUtil(ldms_schema_t schema, int* cidx){
	int rc;

	rc = ldms_schema_metric_add(schema, "Jiffies", LDMS_V_U64);
	if (rc < 0)
		return ENOMEM;
	*cidx = rc;

	rc = ldms_schema_metric_add(schema, "UserSum", LDMS_V_U64);
	if (rc < 0)
		return ENOMEM;

	rc = ldms_schema_metric_add(schema, "SysSum", LDMS_V_U64);
	if (rc < 0)
		return ENOMEM;

	rc = ldms_schema_metric_add(schema, "IdleSum", LDMS_V_U64);
	if (rc < 0)
		return ENOMEM;

	rc = ldms_schema_metric_array_add(schema, "User", LDMS_V_U64_ARRAY, num_cores);
	if (rc < 0)
		return ENOMEM;

	rc = ldms_schema_metric_array_add(schema, "Sys", LDMS_V_U64_ARRAY, num_cores);
	if (rc < 0)
		return ENOMEM;

	rc = ldms_schema_metric_array_add(schema, "Idle", LDMS_V_U64_ARRAY, num_cores);
	if (rc < 0)
		return ENOMEM;

	return 0;

}

static int getCoreUtilVals(struct mic_core_util* cutil, int metric_no){

	union ldms_value v; //for sum and jif
	int ret;
	int i;

	ret = mic_get_jiffy_counter(cutil, &v.v_u64);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get jiffyA counter %d\n", ret);
		v.v_u64 = 0;
	}
	ldms_metric_set(set, metric_no, &v);
	metric_no++;

	ret = mic_get_user_sum(cutil, &v.v_u64);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get user sum %d\n", ret);
		v.v_u64 = 0;
	}
	ldms_metric_set(set, metric_no, &v);
	metric_no++;

	ret = mic_get_sys_sum(cutil, &v.v_u64);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get sys sum %d\n", ret);
		v.v_u64 = 0;
	}
	ldms_metric_set(set, metric_no, &v);
	metric_no++;

	ret = mic_get_idle_sum(cutil, &v.v_u64);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get idle sum %d\n", ret);
		v.v_u64 = 0;
	}
	ldms_metric_set(set, metric_no, &v);
	metric_no++;

	ret = mic_get_user_counters(cutil, counters);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get user counters %d\n", ret);
		memset(counters, 0, sizeof(counters));
	}
	//FIXME: Why isnt there a whole vector set interface?
	for (i = 0; i < num_cores; i++){
		ldms_metric_array_set_u64(set, metric_no, i, counters[i]);
	}
	metric_no++;

	ret = mic_get_sys_counters(cutil, counters);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get sys counters %d\n", ret);
		memset(counters, 0, sizeof(counters));
	}
	//FIXME: Why isnt there a whole vector set interface?
	for (i = 0; i < num_cores; i++){
		ldms_metric_array_set_u64(set, metric_no, i, counters[i]);
	}
	metric_no++;

	ret = mic_get_idle_counters(cutil, counters);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LDEBUG, "knc_sampler: Can't get idle counters %d\n", ret);
		memset(counters, 0, sizeof(counters));
	}
	//FIXME: Why isnt there a whole vector set interface?
	for (i = 0; i < num_cores; i++){
		ldms_metric_array_set_u64(set, metric_no, i, counters[i]);
	}
	metric_no++;

	return 0;
}


static int init_knc(struct mic_device **mdh, struct mic_core_util** cutil,  int devn){
	int ret;

	msglog(LDMSD_LDEBUG, "knc_sampler: init_knc\n");
	ret = mic_open_device(mdh, devn);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "Cannot open mic device %d: error %d\n",
		       devn, ret);
		return EINVAL;
	}

	ret = initCoreUtil(*mdh, cutil, devn);
	if (ret != E_MIC_SUCCESS){
		msglog(LDMSD_LERROR, "Cannot initCoreUtil for mic device %d: error %d\n",
		       devn, ret);
		return EINVAL;
	}

	return 0;
}


static int create_metric_set(const char *instance_name, char* schema_name, int devn)
{
	uint64_t metric_value;
	char *s;
	char lbuf[256];
	char metric_name[128];
	int rc, i;

	rc = init_knc(&mdh_, &cutil_, devn);
	if (rc != 0){
		msglog(LDMSD_LERROR, "Cannot init knc. Exiting\n");
		return EINVAL;
	}

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	//metric 0. only set once.
	rc = ldms_schema_metric_add(schema, "DeviceNum", LDMS_V_U64);
	if (rc < 0)
		return ENOMEM;

	rc = addMetrics_PowerUtil(schema, &power_idx_init);
	if (rc != 0)
		goto err;

	rc = addMetrics_Thermal(schema, &thermal_idx_init);
	if (rc != 0)
		goto err;

	rc = addMetrics_MemUtil(schema, &memory_idx_init);
	if (rc != 0)
		goto err;

	rc = addMetrics_CoreUtil(schema, &core_idx_init);
	if (rc != 0)
		goto err;

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	//do a 1 time set of the device num
	union ldms_value v64;
	v64.v_u64 = devn;
	ldms_metric_set(set, 0, &v64);

	return 0;

 err:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;

	return rc;
}

/**
 * \brief Configuration
 *
 * config name=knc_sampler producer=<producer_name> instance=<instance_name> dev_num=<dev_num> [schema=<sname>]
 *     producer    The producer id value.
 *     instance    The set name. Will be postpended with the dev_num
 *     dev_num     The device num (0/1)
 *     sname       Optional schema name. Defaults to 'knc'
 */
static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *instance;
	char *sname;
	int i;

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "knc_sampler: missing producer\n");
		return ENOENT;
	}

	instance = av_value(avl, "instance");
	if (!instance) {
		msglog(LDMSD_LERROR, "knc_sampler: missing instance.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0){
		msglog(LDMSD_LERROR, "knc_sampler: schema name invalid.\n");
		return EINVAL;
	}

	value = av_value(avl, "dev_num");
	if (!value) {
		msglog(LDMSD_LERROR, "knc_sampler: missing dev_num.\n");
		return EINVAL;
	}
	dev_num = atoi(value);

	if (set) {
		msglog(LDMSD_LERROR, "knc_sampler: Set already created.\n");
		return EINVAL;
	}

	int rc = create_metric_set(instance, sname, dev_num);
	if (rc) {
		msglog(LDMSD_LERROR, "knc_sampler: failed to create a metric set.\n");
		return rc;
	}

	ldms_set_producer_name_set(set, producer_name);

	return 0;
}

static ldms_set_t get_set()
{
	return set;
}

static int sample(void)
{
	int ret;

	if (!set) {
		msglog(LDMSD_LDEBUG, "knc_sampler: plugin not initialized\n");
		return EINVAL;
	}

	ldms_transaction_begin(set);

	//order: Power, Thermal, Mem, Core
	//FIXME: do something with these vals if cannot update
	ret = updatePowerUtilInfo(mdh_, &putil_);
	getPowerUtilInfoVals(putil_, power_idx_init);

	ret = updateThermalInfo(mdh_, &tutil_);
	getThermalInfoVals(tutil_, thermal_idx_init);

	ret = updateMemUtilInfo(mdh_, &mutil_);
	getMemUtilInfoVals(mutil_, memory_idx_init);

	ret = updateCoreUtil(mdh_, &cutil_);
	getCoreUtilVals(cutil_, core_idx_init);

 out:
	ldms_transaction_end(set);
	return 0;
}

static void term(void)
{

	freeCoreUtil();
	freePowerUtilInfo();
	freeThermalInfo();
	freeMemUtilInfo();

	if (counters)
		free(counters);
	counters = NULL;

	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static const char *usage(void)
{
	return  "config name=knc_sampler_copy producer=<prod_name> instance=<inst_name> dev_num=<dev_num> [schema=<sname>]\n"
		"    producer      The producer name\n"
		"    instance      The instance name.\n"
		"    dev_num       The device_num (0/1)\n"
		"    schema        Optional schema name. Defaults to 'knc'\n";
}

static struct ldmsd_sampler knc_sampler_copy_plugin = {
	.base = {
		.name = "knc_sampler_copy",
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
	return &knc_sampler_copy_plugin.base;
}
