/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2018 Sandia Corporation. All rights reserved.
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
 * \file ibm_occ.c
 * \brief /sys/firmware/opal/exports/occ_inband_sensors data provider
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <endian.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "../sampler_base.h"
#include "ibm_occ.h"

#define OCC_DATA_FILE "/sys/firmware/opal/exports/occ_inband_sensors"

static ovis_log_t mylog;

static ldms_set_t set = NULL;
#define SAMP "ibm_occ"
static int metric_offset;
static base_data_t base;

static char buffer[1024*1024];
struct sid_map_s {
	int sample;
	int sample_min;
	int sample_max;
	int accum;
} *sid_map;

static int add_metric(ldms_schema_t schema, const char *metric_name)
{
	int rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_U64);
	if (rc < 0) {
		ovis_log(mylog, OVIS_LERROR, "Error %d adding the metric '%s' to the schema.",
		       rc, metric_name);
	}
	return rc;
}

static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	int rc, sid, id;
	struct occ_sensor_data_header *hdr = (struct occ_sensor_data_header *)buffer;
	int fd = open(OCC_DATA_FILE, O_RDONLY);
	if (fd < 0) {
		ovis_log(mylog, OVIS_LERROR, "Could not open the '%s' file"
				"...exiting sampler\n", OCC_DATA_FILE);
		return ENOENT;
	}
	rc = read(fd, buffer, sizeof(buffer));

	/* Read the file and add metrics to the schema. */
	hdr->nr_sensors = be16toh(hdr->nr_sensors);
	hdr->names_offset = be32toh(hdr->names_offset);
	hdr->names = (struct occ_sensor_name *)&buffer[hdr->names_offset];

	if (sid_map)
		free(sid_map);
	sid_map = calloc(hdr->nr_sensors, sizeof *sid_map);
	if (!sid_map) {
		ovis_log(mylog, OVIS_LERROR,
		       "Memory exhaustion creating sid_map for %d sensors",
		       hdr->nr_sensors);
		rc = ENOMEM;
		goto err;
	}
	schema = base_schema_new(base);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		goto err;
	}

	/* Location of first metric from proc/ibm_occ file */
	metric_offset = ldms_schema_metric_count_get(schema);

	/* Read the file and add metrics to the schema. */
	for (sid = 0; sid < hdr->nr_sensors; sid++) {
		char metric_name[128];

		id = add_metric(schema, hdr->names[sid].name);
		if (id < 0)
			continue;
		sid_map[sid].sample = id;

		snprintf(metric_name, sizeof(metric_name), "%s_acc", hdr->names[sid].name);
		id = add_metric(schema, metric_name);
		if (id < 0)
			continue;
		sid_map[sid].accum = id;

		switch (hdr->names[sid].format) {
		case OCC_SENSOR_RECORD:
			snprintf(metric_name, sizeof(metric_name), "%s_min", hdr->names[sid].name);
			id = add_metric(schema, metric_name);
			if (id < 0)
				continue;
			sid_map[sid].sample_min = id;
			snprintf(metric_name, sizeof(metric_name), "%s_max", hdr->names[sid].name);
			id = add_metric(schema, metric_name);
			if (id < 0)
				continue;
			sid_map[sid].sample_max = id;
			break;
		case OCC_SENSOR_COUNTER:
			break;
		default:
			printf("Sensor '%s' has an unrecognized data format %d\n",
			       hdr->names[sid].name, hdr->names[sid].format);
		}
	}

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		ovis_log(mylog, OVIS_LERROR, "Error %d creating the metric set '%s'.",
		       rc, base->instance_name);
		goto err;
	}
	close(fd);
	return 0;

 err:
	if (fd >= 0) {
		close(fd);
		fd = -1;
	}
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

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
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
	return  "config name=" SAMP BASE_CONFIG_USAGE;
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	if (set) {
		ovis_log(mylog, OVIS_LERROR, "Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, NULL);
	if (rc != 0){
		return rc;
	}

	base = base_config(avl, ldmsd_plug_cfg_name_get(handle), SAMP, mylog);
	if (!base) {
		rc = errno;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "failed to create a metric set.\n");
		goto err;
	}
	return 0;
 err:
	base_del(base);
	return rc;
}

static int sample(ldmsd_plug_handle_t handle)
{
	int rc, metric_no, fd;
	union ldms_value v;
	struct occ_sensor_data_header *hdr;

	if (!set) {
		ovis_log(mylog, OVIS_LDEBUG, "plugin not initialized\n");
		return EINVAL;
	}

	hdr = (struct occ_sensor_data_header *)buffer;
	fd = open(OCC_DATA_FILE, O_RDONLY);
	if (fd < 0) {
		ovis_log(mylog, OVIS_LERROR, "Error %d opening the '%s' file in sample().",
		       errno, OCC_DATA_FILE);
		return ENOENT;
	}
	rc = read(fd, buffer, sizeof(buffer));
	if (rc <= 0) {
		ovis_log(mylog, OVIS_LERROR, "Error %d reading the '%s' file in sample().",
		       rc, OCC_DATA_FILE);
		return EINVAL;
	}

	base_sample_begin(base);
	metric_no = metric_offset;

	/* Fixup header */
	hdr->nr_sensors = be16toh(hdr->nr_sensors);
	hdr->names_offset = be32toh(hdr->names_offset);
	hdr->ping_offset = be32toh(hdr->ping_offset);
	hdr->pong_offset = be32toh(hdr->pong_offset);
	hdr->names = (struct occ_sensor_name *)&buffer[hdr->names_offset];

	int sid;
	for (sid = 0; sid < hdr->nr_sensors; sid++) {
		off_t off = be32toh(hdr->names[sid].offset);
		struct occ_sensor *ping, *pong, *sens;

		ping = (struct occ_sensor *)&buffer[hdr->ping_offset + off];
		pong = (struct occ_sensor *)&buffer[hdr->pong_offset + off];
		if (be64toh(ping->hdr.timestamp) > be64toh(pong->hdr.timestamp)) {
			sens = ping;
		} else {
			sens = pong;
		}
		switch (hdr->names[sid].format) {
		case OCC_SENSOR_RECORD:
			ldms_metric_set_u64(set, sid_map[sid].accum,
					    (uint64_t)sens->record.accumulator);
			ldms_metric_set_u64(set, sid_map[sid].sample,
					    (uint64_t)sens->record.sample);
			ldms_metric_set_u64(set, sid_map[sid].sample_min,
					    (uint64_t)sens->record.sample_min);
			ldms_metric_set_u64(set, sid_map[sid].sample_max,
					    (uint64_t)sens->record.sample_max);
			break;
		case OCC_SENSOR_COUNTER:
			ldms_metric_set_u64(set, sid_map[sid].accum,
					    (uint64_t)sens->counter.accumulator);
			ldms_metric_set_u64(set, sid_map[sid].sample,
					    (uint64_t)sens->counter.sample);
			break;
		default:
			ovis_log(mylog, OVIS_LERROR,
			       "Sensor '%s' has an unrecognized data format %d\n",
			       hdr->names[sid].name, hdr->names[sid].format);
			continue;
		}
	}
	rc = 0;
	close(fd);
 out:
	base_sample_end(base);
	return rc;
}

static void term(ldmsd_plug_handle_t handle)
{
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);
	set = NULL;

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
