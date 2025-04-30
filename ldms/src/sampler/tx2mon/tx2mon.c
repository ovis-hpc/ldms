/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2021 Open Grid Computing, Inc. All rights reserved.
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
 *	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 *
 *	Redistributions in binary form must reproduce the above
 *	copyright notice, this list of conditions and the following
 *	disclaimer in the documentation and/or other materials provided
 *	with the distribution.
 *
 *	Neither the name of Sandia nor the names of any contributors may
 *	be used to endorse or promote products derived from this software
 *	without specific prior written permission.
 *
 *	Neither the name of Open Grid Computing nor the names of any
 *	contributors may be used to endorse or promote products derived
 *	from this software without specific prior written permission.
 *
 *	Modified source versions must be plainly marked as such, and
 *	must not be misrepresented as being the original software.
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
 * \file tx2mon.c
 */

/*
 * tx2mon.c -	LDMS sampler for basic Marvell TX2 chip telemetry.
 *
 *		Sampler to provide LDMS data available via the tx2mon
 *		CLI utility (https://github.com/jchandra-cavm/tx2mon).
 *		This data exists in structure that is mapped all
 *		the way into sysfs from the memory of the M3 management
 *		processor present on each TX2 die.
 *		This sampler requires the tx2mon kernel module to be loaded.
 *		This module creates sysfs entries that can be opened and
 *		mmapped, then overlaid with a matching structure definition.
 *		Management processor updates the underlying structure at >= 10Hz.
 *		The structure contains a great deal of useful telemetry, including:
 *		 - clock speeds
 *		 - per-core temperatures
 *		 - power data
 *		 - throttling statistics
 */

/*
 *  Copyright [2020] Hewlett Packard Enterprise Development LP
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to:
 *
 *   Free Software Foundation, Inc.
 *   51 Franklin Street, Fifth Floor
 *   Boston, MA 02110-1301, USA.
 */

#define _GNU_SOURCE
#include <inttypes.h>
#include "assert.h"
#include <unistd.h>
#include <sys/errno.h>
#include <sys/syscall.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "sampler_base.h"
#include "tx2mon.h"
#include "stdbool.h"
#include <stdint.h>
#include <string.h>
#include <sys/utsname.h>

#define SAMP "tx2mon"

static ovis_log_t mylog;

static ldms_set_t set[MAX_CPUS_PER_SOC];
static ldms_set_t sc;

static base_data_t base;
static ldms_schema_t schema;
static struct tx2mon_sampler tx2mon_s = {0};

static struct tx2mon_sampler *tx2mon = &tx2mon_s;

#define MCP_STR_WRAP(NAME) #NAME
#define MCP_LISTWRAP(NAME) MCP_ ## NAME
#define META_MCP_STR_WRAP(NAME) #NAME
#define META_MCP_LISTWRAP(NAME) MCP_ ## NAME

static int pidarray = 0;
static int pidextra = 0;
static char *pids = "self";
static int noop = 0;

/*
 * - Define metric list found in /usr/include/tx2mon/mc_oper_region.h.
 * - Create list with the following arguments: name, metric name, type, position in the metric list.
 * - Add schema meta data with ldms_schema_meta_array_add() and ldms_schema_meta_add().
 * - Add schema metric data with ldms_schema_metric_array_add() and ldms_schema_metric_add().
 * - Set the total length of the arrays to the number of cores found in /sys/bus/platform/devices/tx2mon/socinfo.
 * - If "array = true/1/t", add all arrays listed in MCP_LIST and their contents  - set array size to the number
 *	of n_cores found in /socinfo.
 * - If "array = false/0/f", add metrics "min" and "max" for each array listed in MCP_LIST.
 * */
#define MCP_LIST(WRAP) \
	WRAP("counter", counter, LDMS_V_U32, pos_counter) \
	WRAP("freq_cpu", freq_cpu[0], LDMS_V_U32_ARRAY, pos_freq_cpu) \
	WRAP("tmon_cpu", tmon_cpu[0], LDMS_V_F32_ARRAY, pos_tmon_cpu) \
	WRAP("tmon_soc_avg", tmon_soc_avg, LDMS_V_F32, pos_tmon_soc_avg) \
	WRAP("pwr_core", pwr_core, LDMS_V_F32, pos_pwr_core) \
	WRAP("pwr_sram", pwr_sram, LDMS_V_F32, pos_pwr_sram) \
	WRAP("pwr_mem", pwr_mem, LDMS_V_F32, pos_pwr_mem) \
	WRAP("pwr_soc", pwr_soc, LDMS_V_F32, pos_pwr_soc) \
	WRAP("v_core", v_core, LDMS_V_F32, pos_v_core) \
	WRAP("v_sram", v_sram, LDMS_V_F32, pos_v_sram) \
	WRAP("v_mem", v_mem, LDMS_V_F32, pos_v_mem ) \
	WRAP("v_soc", v_soc, LDMS_V_F32, pos_v_soc) \
	WRAP("active_evt", active_evt, LDMS_V_CHAR_ARRAY, pos_active_evt) \
	WRAP("temp_evt_cnt", temp_evt_cnt, LDMS_V_U32, pos_temp_evt_cnt) \
	WRAP("pwr_evt_cnt", pwr_evt_cnt, LDMS_V_U32, pos_pwr_evt_cnt) \
	WRAP("ext_evt_cnt", ext_evt_cnt, LDMS_V_U32, pos_ext_evt_cnt) \
	WRAP("temp_throttle_ms", temp_throttle_ms, LDMS_V_U32, pos_temp_throttle_ms) \
	WRAP("pwr_throttle_ms", pwr_throttle_ms, LDMS_V_U32, pos_pwr_throttle_ms) \
	WRAP("ext_throttle_ms", ext_throttle_ms , LDMS_V_U32, pos_ext_throttle_ms)

/* Define constants in the metric list */
#define META_MCP_LIST(WRAP) \
	WRAP("temp_abs_max", temp_abs_max, LDMS_V_F32, pos_temp_abs_max) \
	WRAP("temp_soft_thresh", temp_soft_thresh, LDMS_V_F32, pos_temp_soft_thresh) \
	WRAP("temp_hard_thresh", temp_hard_thresh, LDMS_V_F32, pos_temp_hard_thresh) \
	WRAP("freq_mem_net", freq_mem_net, LDMS_V_U32, pos_freq_mem_net) \
	WRAP("freq_max", freq_max, LDMS_V_U32, pos_freq_max) \
	WRAP("freq_min", freq_min, LDMS_V_U32, pos_freq_min)\
	WRAP("freq_socs", freq_socs, LDMS_V_U32, pos_freq_socs)\
	WRAP("freq_socn", freq_socn, LDMS_V_U32, pos_freq_socn)\

#define DECLPOS(n, m, t, p) static int p = -1;

MCP_LIST(DECLPOS);
META_MCP_LIST(DECLPOS);
static int pos_cpu_num = -1;

#define META(n, m, t, p) \
	rc = meta_filter(n, t);\
	p = rc; \
	if (p < 0) { rc = -rc; goto err; }

#define METRIC(n, m, t, p) \
	rc = metric_filter(n, t);\
	p = rc; \
	if (p < 0) { rc = -rc; goto err; }
/*
 * meta_filter() checks the stored value in "pidextra" variable and add/removes the following metrics respectively.
 * If "extra" is set to true in the configuration file, the extra metrics will be included.
 * If "extra" is set to false then the extra metrics will not be included.
 * Default is false.
 */
static int meta_filter(char *n, uint32_t t)
{
	int rc = 0;
	int pos = -1;
	if (pidextra)
		rc = ldms_schema_meta_add(schema, n, t);
	else if (!pidextra)
		return rc;

	if (rc > -1)
		pos = rc;
	else {
		rc = -ENOMEM;
		return rc;
	}
	return pos;
}


/* This function checks the value contained in pidarray and adds the corresponding metrics
 * as follows using a switch statement:
 * Include metric arrays if "array" is set to true in the configuration file.
 * Inlcude only max and min metric values if "array" is set to false.
 * Default is false.
 */
static int metric_filter(char *n, uint32_t t)
{
	int rc = 0;
	int pos = -1;
	switch (t) {
	case LDMS_V_U32_ARRAY:
		if (pidarray) {
			rc = ldms_schema_metric_array_add(schema, n, t, tx2mon->n_core);
		} else {
			rc = ldms_schema_metric_add(schema, "freq_cpu_min", LDMS_V_U32);
			rc = ldms_schema_metric_add(schema, "freq_cpu_max", LDMS_V_U32);
		}
		if (rc > -1)
			pos = rc;
		else
			goto err;
		break;
	case LDMS_V_F32_ARRAY:
		if (pidarray) {
			rc = ldms_schema_metric_array_add(schema, n, t, tx2mon->n_core);
		} else {
			rc = ldms_schema_metric_add(schema, "tmon_cpu_min", LDMS_V_F32);
			rc = ldms_schema_metric_add(schema, "tmon_cpu_max", LDMS_V_F32);
		}
		if (rc > -1)
			pos = rc;
		else
			goto err;
		break;
	case LDMS_V_CHAR_ARRAY:
		rc = ldms_schema_metric_array_add(schema, n, t, 50);
		if (rc> -1)
			pos = rc;
		else
			goto err;
		if (!strcmp(n, "active_evt")) {
			rc = ldms_schema_metric_add(schema, "Temperature", LDMS_V_U32);
			rc = ldms_schema_metric_add(schema, "Power", LDMS_V_U32);
			rc = ldms_schema_metric_add(schema, "External", LDMS_V_U32);
			rc = ldms_schema_metric_add(schema, "Unk3", LDMS_V_U32);
			rc = ldms_schema_metric_add(schema, "Unk4", LDMS_V_U32);
			rc = ldms_schema_metric_add(schema, "Unk5", LDMS_V_U32);
			if (rc > -1)
				return pos;
			else
				goto err;
		}
		break;
	default:
		rc = ldms_schema_metric_add(schema, n, t);
		if (rc > -1)
			pos = rc;
		else
			goto err;
		break;
	}

	return pos;

err:
	rc = -ENOMEM;
	return rc;

}

static bool get_bool(const char *val, char *name)
{
	if (!val)
		return false;

	switch (val[0]) {
	case '1':
	case 't':
	case 'T':
	case 'y':
	case 'Y':
		return true;
	case '0':
	case 'f':
	case 'F':
	case 'n':
	case 'N':
		return false;
	default:
		ovis_log(mylog, OVIS_LERROR, "%s: bad bool value %s for %s\n",
			 SAMP, val, name);
		return false;
	}
}
/***************************************************************************/
/*brief parse on soc info and fill provides struct.
 * \return 0 on success, errno from fopen, ENODATA from
 * failed fgets, ENOKEY or ENAMETOOLONG from failed parse.
 */
static int parse_socinfo(void);

/* Read and query cpu data for each node and map to data structure. Can also be used for debugging
 * by displaying the data to the ldmsd log file*/
static int parse_mc_oper_region();

/* Define metric list and call tx2mon_array_conv */
static int tx2mon_set_metrics(int i);

/* Convert metric values to temp, voltage and power units. Output results in float and uint32_t types */
static int tx2mon_array_conv(void *s, int p, int idx, int i, uint32_t t);


/***************************************************************************/

/*
 * Create the schema and metric set.
 *
 *  - Read & parse TX2MON_SOCINFO_PATH to learn about the system config, and
 *    test that the kernel module is loaded etc. This file is plain ascii.
 *  - Open & mmap TX2MON_NODE?_PATH files. These are binary data structures
 *    defined in mc_oper_region.h.
 *  - Establish the capabilities reported, based on the version number of
 *    the data structure.
 *  - Set up metrics, update data and sample.
 *
 *    Return 0 iff all good, else report useful error message, clean up,
 *    and return appropriate errno.
 */
static int create_metric_set(base_data_t base)
{
	int rc, i;
	int mcprc = -1;
	size_t instance_len = strlen(base->instance_name) + 12;
	static struct mc_oper_region *s;
	char buf[instance_len];
	char cpu_instance_index[12];

	mcprc = parse_mc_oper_region();
	if (mcprc != 0) {
		ovis_log(mylog, OVIS_LERROR, "unable to read the node file for the sample (%s)\n",
		       STRERROR(mcprc));
		return mcprc;
	}

	schema = base_schema_new(base);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}
	MCP_LIST(METRIC);
	pos_cpu_num = ldms_schema_meta_add(schema, "cpu_num", LDMS_V_U32);
	if (pos_cpu_num < 0) {
		rc = ENOMEM;
		goto err;
	}
	META_MCP_LIST(META);
	for (i = 0; i < 2; i++) {
		s = &tx2mon->cpu[i].mcp;
		snprintf(cpu_instance_index, sizeof(cpu_instance_index), ".%d", i);

		strncpy(buf, base->instance_name, instance_len);
		strncat(buf, cpu_instance_index, 12);

		set[i] = ldms_set_new(buf, schema);

		if (!set[i]) {
			rc = errno;
			ovis_log(mylog, OVIS_LERROR, "ldms_set_new failed %d for %s\n",
			       errno, base->instance_name);
			goto err;
		}

#define META_MCSAMPLE(n, m, t, p)\
		rc = tx2mon_array_conv(&s->m, p, tx2mon->n_core, i, t);\
		if (rc){ \
		rc = EINVAL; \
		ovis_log(mylog, OVIS_LERROR, "sample " n " not correctly defined.\n"); \
		return rc;}\

		META_MCP_LIST(META_MCSAMPLE);
		ldms_metric_set_u32(set[i], pos_cpu_num, (uint32_t)i);

		ldms_set_producer_name_set(set[i], base->producer_name);
		ldms_metric_set_u64(set[i], BASE_COMPONENT_ID, base->component_id);
		ldms_metric_set_u64(set[i], BASE_JOB_ID, 0);
		ldms_metric_set_u64(set[i], BASE_APP_ID, 0);
		base_auth_set(&base->auth, set[i]);

		rc = ldms_set_publish(set[i]);
		if (rc) {
			ldms_set_delete(base->set);
			base->set = NULL;
			errno = rc;
			ovis_log(mylog, OVIS_LERROR, SAMP ": ldms_set_publish failed for %s\n",
			          base->instance_name);
			return EINVAL;
		}
		ldmsd_set_register(set[i], base->pi_name);

		base->set = set[i];
		base_sample_begin(base);

		rc = tx2mon_set_metrics(i);
		if (rc) {
			ovis_log(mylog, OVIS_LERROR, "failed to create metric set.\n");
			rc = EINVAL;
			return rc;
		}
		base_sample_end(base);
		base->set = NULL;

	}


	return 0;
err:
	if(schema)
		ldms_schema_delete(schema);
	return rc;
}

/* compute unique schema name based on options.
 * no options: base = tx2mon
 * schema=xxx: use xxx as the base of the unique name
 * pidarray or pidextra: add schema suffix based on set content.
 * @return name which the caller must arrange to free eventually.
 */
static char * compute_schema_name(int pidarray, int pidextra, struct attr_value_list *avl, int arraysize)
{
	char *schema_name = av_value(avl, "schema");
	if (!schema_name) {
		schema_name = SAMP;
	}
	if (pidarray || pidextra) {
		size_t blen = strlen(schema_name) + 14;
		char *buf = malloc(blen);
		char asize[10];
		if (pidarray) {
			snprintf(asize, sizeof(asize), "_%hd", (short) arraysize);
		} else {
			asize[0] = '\0';
		}
		if (buf) {
			snprintf(buf, blen, "%s_%c%c%s", schema_name,
			         (pidarray ?  '1' : '0'),
			         (pidextra ?  '1' : '0'),
			         asize);
		}
		return buf;
	} else {
		return strdup(schema_name);
	}
}

/*
 * Plug-in data structure and access method.
 */

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc = -1;
	char *array, *extra;
	char *sbuf = NULL;
	int i;

	int ret = parse_socinfo();
	if (ret != 0) {
		struct utsname un;
		int ue = uname(&un);
		if (!ue && strcmp(un.machine,"aarch64")) {
			noop = 1;
			ovis_log(mylog, OVIS_LWARNING, SAMP
				": tx2mon does nothing except on ThunderX2/aarch64.\n");
			tx2mon->n_cpu = 0;
			return 0;
		}
		ovis_log(mylog, OVIS_LERROR, "Failed. Check that you loaded tx2mon_kmod module. \n");
		return EINVAL;
	}

	for (i = 0; i < tx2mon->n_cpu; i++) {
		if (set[i]) {
			ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
			return EINVAL;
		}
	}

	array = av_value(avl, "array");
	extra = av_value(avl, "extra");

	if (array && get_bool(array, "array"))
		pidarray = 1;
	if (extra && get_bool(extra, "extra"))
		pidextra = 1;

	sbuf = compute_schema_name(pidarray, pidextra, avl, tx2mon->n_core);
	if (!sbuf) {
		ovis_log(mylog, OVIS_LERROR, "out of memory computing schema name.\n");
		goto err;
	}

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), sbuf, mylog);
	if (!base) {
		goto err;
	}
	char *as = av_value(avl, "auto-schema");
	bool dosuffix = (as && get_bool(as, "auto-schema") );
	if (dosuffix && strcmp(base->schema_name, sbuf) != 0) {
		free(base->schema_name);
		base->schema_name = sbuf;
		sbuf = NULL;
	}

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create metric set.\n");
		goto err;
	}

	ovis_log(mylog, OVIS_LDEBUG, "config done. \n");

	free(sbuf);
	return 0;

err:
	rc = EINVAL;
	base_del(base);
	free(sbuf);
	ovis_log(mylog, OVIS_LDEBUG, "config fail.\n");
	return rc;

}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return
"config name=" SAMP " [port-number=<num>]\n"
"	[producer=<name>] [instance=<name>] [component_id=<uint64_t>] [schema=<name_base>]\n"
"       [array=<bool>] [extra=bool] [auto-schema=bool]\n"
"	[uid=<user-id>] [gid=<group-id>] [perm=<mode_t permission bits>]\n"
"    producer	  A unique name for the host providing the timing data (default $HOSTNAME)\n"
"    instance	  A unique name for the timing metric set (default $HOSTNAME/" SAMP ")\n"
"    component_id A unique number for the component being monitoring, Defaults to zero.\n"
"    schema	  The base name of the port metrics schema, Defaults to " SAMP ".\n"
"    auto-schema  Use schema as base of schema name generated to be unique.\n"
"    array	  Includes only the minimum and maximum metric values of each array in the metric set. \n"
"		  If true, all array values are included. Default is FALSE.\n"
"    extra	  Includes additional frequency metrics of the internal block. \n"
"		  If false, these metrics will be ommitted. Default is FALSE.\n"
"    uid	  The user-id of the set's owner\n"
"    gid	  The group id of the set's owner\n"
"    perm	  The set's access permissions\n"
	        ;
}

static void term(ldmsd_plug_handle_t handle)
{
	int i;
	if (base)
		base_del(base);
	for (i = 0; i < tx2mon->n_cpu; i++) {
		if (set[i])
			ldms_set_delete(set[i]);
		set[i] = NULL;
	}
}

static int sample(ldmsd_plug_handle_t handle)
{
	if (noop)
		return 0;
	int rc = 0;
	int i;
	int mcprc = -1;
	for (i = 0; i < tx2mon->n_cpu; i++) {
		if (!set[i]) {
			ovis_log(mylog, OVIS_LERROR, "plugin not initialized\n");
			return EINVAL;
		}
	}
	mcprc = parse_mc_oper_region();

	for (i = 0; i < tx2mon->n_cpu; i ++) {
		base->set = set[i];
		base_sample_begin(base);
		if (!mcprc) {
			rc = tx2mon_set_metrics(i);
			if (rc) {
				ovis_log(mylog, OVIS_LERROR, "failed to create metric set.\n");
				rc = EINVAL;
				return rc;
			}

		}

		base_sample_end(base);
		base->set = NULL;
	}
	return rc;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	int i;

	mylog = ldmsd_plug_log_get(handle);
	for (i = 0; i < MAX_CPUS_PER_SOC; i++)
		set[i] = NULL;

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_sampler ldmsd_plugin_interface = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
		.constructor = constructor,
		.destructor = destructor,
	},
	.sample = sample,
};

/***************************************************************************/

/*
 * tx2mon_get_throttling_events() checks the value of the "active_evt" metric.
 * If the metric is zero, the character array will be set to
 * "None".
 * If "active_evt" is not 0, the function will iterate through the causes
 * and perform a left shift to determine what the cause is.
 * If there is more than one cause, the strings will be concatinated and
 * returned as a single char array.
 * EXAMPLE: If active_evt = 5 (binary:101) and, starting from the far
 * right and iterating by 1 to the left, the initial (zero) and second
 * bits are set to 1 (active). Since "Temperature and External" are
 * indexes 0 and 2 then the returned array will be "Temperature External"
 */

static int  tx2mon_get_throttling_events(uint32_t *active, int i, int p, char *throt_buf, int bufsz)
{
	const char *causes[] = { "Temperature", "Power", "External", "Unk3", "Unk4", "Unk5"};
	const int ncauses = sizeof(causes)/sizeof(causes[0]);
	int sz, incr;
	int rc = 0;
	int events = 0;
	const char *sep = ",";
	int pos = p;
	char total_causes[sizeof(causes)+2];
	strcpy(total_causes, "");

	if (!*active) {
		ldms_metric_array_set_str(set[i], p, "None");
		return rc;
	}

	if (!tx2mon->cpu[i].throttling_available) {
		for (pos = p; p <= ncauses; pos++) {
			ldms_metric_set_u32(set[i], p, 0);
		}
		ovis_log(mylog, OVIS_LDEBUG, "Throttling events not supported. \n");
		return rc;
	}


	for (incr = 0, events = 0; incr < ncauses && bufsz > 0; incr++) {
		pos++;
		if ((*active & (1 << incr)) == 0)
			continue;
		sz = snprintf(throt_buf, bufsz, "%s%s", events ? sep : "", causes[incr]);
		bufsz -= sz;
		throt_buf += sz;
		++events;
		strcat(total_causes, causes[incr]);
		ldms_metric_array_set_str(set[i], p, total_causes);
		ldms_metric_set_u32(set[i], pos, 1);
		strcat(total_causes, ",");
	}

	return rc;
}

/* Converts unsigned 32 and 16 integers to temperature units and returns a float. to_c is the
temperature calulation defined in mc_oper_region.h*/
static float my_to_c_u32(uint32_t t)
{
	return to_c(t);

}
static float my_to_c_u16(uint16_t t)
{
	return to_c(t);
}

/*
 * - Define all metrics in the structure list "MCP_LIST". This definition will be used to sample the metric set.
 * - Call tx2mon_array_conv.
 * - tx2mon_array_conv converts each metric and array to the necessary types (float, uint16 and uint32) and sets
 *	the metrics with ldms_metric_set_<type>() and ldms_metric_array_set_<type>().
 * - If "array = true/1/t/T", both arrays in "MCP_LIST" (freq_cpu and tmon_cpu) will be included.
 * - Values found in the array are converted to temp, voltage and power with my_to_c_u16() and my_to_c_u32().
 * - Fail with rc = EINVAL if a metric type does not exist
 * */

static int tx2mon_set_metrics (int i)
{

	struct mc_oper_region *s;
	int rc = 0;
	s = &tx2mon->cpu[i].mcp;

#define MCSAMPLE(n, m, t, p) \
	rc = tx2mon_array_conv(&s->m, p, tx2mon->n_core, i, t);\
	if (rc){ \
		rc = EINVAL; \
		ovis_log(mylog, OVIS_LERROR, "sample " n " not correctly defined.\n"); \
		return rc; }\


	MCP_LIST(MCSAMPLE);
	return rc;

}

static int tx2mon_array_conv(void *s, int p, int idx, int i, uint32_t t)
{
	int rc = 0;
	int c = 0;
	char throt_buf[64];
	int temp_p = 0;
	if (t == LDMS_V_F32_ARRAY) {
		uint16_t *s16 = (uint16_t*)s;
		if (!pidarray) {
			uint16_t *min_max16 = (uint16_t*)s;
			min_max16[0] = s16[0];
			min_max16[1] = s16[0];
			for (c = 0; c < idx; c++) {
				if (my_to_c_u16(s16[c]) < my_to_c_u16(min_max16[0]))
					min_max16[0] = s16[c];
				if (my_to_c_u16(s16[c]) > my_to_c_u16(min_max16[1]))
					min_max16[1] =s16[c];

			}
			for (temp_p = -1; temp_p < 1; temp_p++) {
				ldms_metric_set_float(set[i], p+temp_p, my_to_c_u16(min_max16[temp_p+1]));
			}

		} else {
			for (c = 0; c < idx; c++)
				ldms_metric_array_set_float(set[i], p, c, my_to_c_u16(s16[c]));
		}
	}

	if (t == LDMS_V_U32_ARRAY) {
		uint32_t *s32 = (uint32_t*)s;
		if (!pidarray) {
			uint32_t *min_max32 = (uint32_t*)s;
			min_max32[0] = s32[0];
			min_max32[1] = s32[0];
			for (c = 0; c < idx; c++) {
				if (s32[c] < min_max32[0])
					min_max32[0] = s32[c];
				if (s32[c] > min_max32[1])
					min_max32[1] =s32[c];
			}
			for (temp_p = -1; temp_p < 1; temp_p++) {
				ldms_metric_set_u32(set[i], p+temp_p, min_max32[temp_p+1]);
			}
		} else {
			for (c = 0; c < idx; c++)
				ldms_metric_array_set_u32(set[i], p, c, s32[c]);
		}
	}

	if (t == LDMS_V_F32) {
		uint32_t *f32 = (uint32_t*)s;
		ldms_metric_set_float(set[i], p, my_to_c_u32(*f32));
		if (!pidarray) {
			if (p >= 9 && p <= 16)
				ldms_metric_set_float(set[i], p, (*f32/1000.0));
		} else {
			if (p >= 7 && p <= 14)
				ldms_metric_set_float(set[i], p, (*f32/1000.0));
		}
	}

	if (t == LDMS_V_U32) {
		uint32_t *u32 = (uint32_t*)s;
		ldms_metric_set_u32(set[i], p, *u32);
	}

	if (t == LDMS_V_CHAR_ARRAY) {
		uint32_t *str = (uint32_t*)s;
		rc = tx2mon_get_throttling_events(str, i, p, throt_buf, sizeof(throt_buf));
	}

	return rc;
}

/* populate tx2mon struct before any use of it. */
static int parse_socinfo(void)
{
	FILE *socinfo;
	char *path;

	path = realpath(TX2MON_SOCINFO_PATH, NULL);
	if (path == NULL) {
		ovis_log(mylog, OVIS_LERROR, "cannot resolve path for '%s'.\n",
		       TX2MON_SOCINFO_PATH);
		return EBADF;
	}

	socinfo = fopen(path, "r");
	if (!socinfo) {
		ovis_log(mylog, OVIS_LERROR, "cannot open '%s', %s.\n",
		       path, STRERROR(errno));
		free(path);
		return errno;
	}

	/*
	Parse socinfo file, it contains three integers with a single
	space between, any problem => fail.
	*/
	if (fscanf(socinfo, "%d %d %d", &tx2mon->n_cpu,
	           &tx2mon->n_core, &tx2mon->n_thread) != 3) {
		ovis_log(mylog, OVIS_LERROR, "cannot parse '%s'.\n", path);
		fclose(socinfo);
		free(path);
		return EBADF;
	}

	fclose(socinfo);
	free(path);

	ovis_log(mylog, OVIS_LINFO, "n_cpu: %d, n_core: %d, n_thread: %d.\n",
	       tx2mon->n_cpu, tx2mon->n_core, tx2mon->n_thread);

	if (TX2MON_MAX_CPU < tx2mon->n_cpu) {
		ovis_log(mylog, OVIS_LWARNING, "sampler built for max %d CPUs, system reporting %d CPUs, limiting reporting to %d.\n",
		       TX2MON_MAX_CPU, tx2mon->n_cpu, TX2MON_MAX_CPU);
		tx2mon->n_cpu = TX2MON_MAX_CPU;
	}

	if (MAX_CPUS_PER_SOC < tx2mon->n_core) {
		ovis_log(mylog, OVIS_LWARNING, "sampler built for max %d cores, system reporting %d cores, limiting reporting to %d.\n",
		       MAX_CPUS_PER_SOC, tx2mon->n_core, MAX_CPUS_PER_SOC);
		tx2mon->n_core = MAX_CPUS_PER_SOC;
	}

	return 0;

}


/* - Loop through number of cpu structs defined in /sys/bus/platform/devices/tx2mon/socinfo
 *	and set in parse_socinfo().
 * - Check the node file path for each cpu struct exist.
 * - Read cpu information in /sys/bus/platform/devices/tx2mon/node<i>_raw
 * - Close file path.
 *
 * - Define "debug" in tx2mon header file to output all structs and their metric values
 *   in table format (similar to tx2mon program).
 */

static int parse_mc_oper_region()
{
	int ret, ret1;
	int i;
	char filename[sizeof(TX2MON_NODE_PATH) + 2];
	ret = ret1 = 1;
	assert(tx2mon != NULL);
	for(i = 0; i < tx2mon->n_cpu; i++) {

		//Get node path name(s)
		snprintf(filename, sizeof(filename), TX2MON_NODE_PATH, i);
		//set number of nodes for each cpu found
		tx2mon->cpu[i].node = i;
		tx2mon->cpu[i].fd = open(filename, O_RDONLY);
		if (tx2mon->cpu[i].fd < 0) {
			ovis_log(mylog, OVIS_LERROR, "Error reading node%d entry.\n", i);
			ovis_log(mylog, OVIS_LERROR, "Is tx2mon_kmod in the kernel (node%d)?\n", i);
			return errno;
		}
		ret = tx2mon_read_node(&tx2mon->cpu[i]);

		if (ret < 0) {
			ovis_log(mylog, OVIS_LERROR, "Unexpected read error!\n");
			return EINVAL;
		}

#ifdef debug
		if (ret > 0) {
			tx2mon->samples++;
			term_init_save();
			dump_cpu_info(&tx2mon->cpu[i]);
		}
#endif

		close(tx2mon->cpu[i].fd);

	}
	return 0;
}

