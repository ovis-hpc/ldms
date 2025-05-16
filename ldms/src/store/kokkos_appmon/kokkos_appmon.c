/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2021,2023 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2021,2023 Open Grid Computing, Inc. All rights reserved.
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
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <coll/htbl.h>
#include <sos/sos.h>
#include <openssl/sha.h>
#include <math.h>
#include <ovis_json/ovis_json.h>
#include "ldms.h"
#include "ldmsd.h"

static ovis_log_t mylog;

static sos_schema_t app_schema;
static char path_buff[PATH_MAX];
static char *log_path = "/var/log/ldms/kokkos_appmon_store.log";
static char *verbosity = "WARN";
static char *stream;
static char *root_path;
static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;
static struct ldmsd_plugin kokkos_store;

static const char *time_job_rank_attrs[] = { "timestamp", "job_id", "rank" };
static const char *job_time_rank_attrs[] = { "job_id", "timestamp", "rank" };
static const char *job_rank_time_attrs[] = { "job_id", "rank", "timestamp" };
static const char *job_name_rank_time_attrs[] = { "job_id", "name", "rank", "timestamp" };

/*
 * Example JSON object:
 *
 * { "job-id" : 10053287,
 *   "node-name" : "nid1234",
 *   "rank" : 1,
 *   "timestamp" : "1626289443.150700",
 *   "kokkos-perf-data" :
 *      [
 *        { "name" : "N9SPARTA_NS14ParticleKokkosE/N9SPARTA_NS28TagParticleCompressReactionsE",
 *          "type" : 0,
 *          "current-kernel-count" : 2,
 *          "total-kernel-count" : 808,
 *          "level" : 0,
 *          "current-kernel-time" : 0.000028,
 *          "total-kernel-time" : 0.000056
 *        }
 *     ]
 *  }
 */
static struct sos_schema_template kokkos_appmon_template = {
	.name = "kokkos_appmon4",
	.uuid = "1db3fbd0-eabb-49e6-8c78-ac67c881b127",
	.attrs = {
		{ .name = "timestamp", .type = SOS_TYPE_DOUBLE },
		{ .name = "node_name", .type = SOS_TYPE_STRING },
		{ .name = "job_id", .type = SOS_TYPE_UINT64 },
		{ .name = "rank", .type = SOS_TYPE_UINT64 },
		{ .name = "name", .type = SOS_TYPE_STRING },
		{ .name = "type", .type = SOS_TYPE_UINT64 },
		{ .name = "current_kernel_count", .type = SOS_TYPE_UINT64 },
		{ .name = "total_kernel_count", .type = SOS_TYPE_UINT64 },
		{ .name = "level", .type = SOS_TYPE_UINT64 },
		{ .name = "current_kernel_time", .type = SOS_TYPE_DOUBLE },
		{ .name = "total_kernel_time", .type = SOS_TYPE_DOUBLE },
		{ .name = "job_name_rank_time", .type = SOS_TYPE_JOIN,
		  .size = 4,
		  .indexed = 1,
		  .join_list = job_name_rank_time_attrs
		},
		{ .name = "time_job_rank", .type = SOS_TYPE_JOIN,
		  .size = 3,
		  .indexed = 1,
		  .join_list = time_job_rank_attrs
		},
		{ .name = "job_rank_time", .type = SOS_TYPE_JOIN,
		  .size = 3,
		  .indexed = 1,
		  .join_list = job_rank_time_attrs
		},
		{ .name = "job_time_rank", .type = SOS_TYPE_JOIN,
		  .size = 3,
		  .indexed = 1,
		  .join_list = job_time_rank_attrs
		},
		{ 0 }
	}
};

enum attr_ids {
       TIMESTAMP_ID = 0,
       NODE_NAME_ID,
       JOB_ID,
       RANK_ID,
       NAME_ID,
       TYPE_ID,
       CURRENT_KERNEL_COUNT_ID,
       TOTAL_KERNEL_COUNT_ID,
       LEVEL_ID,
       CURRENT_KERNEL_TIME_ID,
       TOTAL_KERNEL_TIME_ID,
};

static int create_schema(sos_t sos, sos_schema_t *app)
{
	int rc;
	sos_schema_t schema;

	/* Create and add the App schema */
	schema = sos_schema_from_template(&kokkos_appmon_template);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR, "%s: Error %d creating Kokkos App schema.\n",
		       kokkos_store.name, errno);
		return errno;
	}
	rc = sos_schema_add(sos, schema);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "%s: Error %d adding Kokkos App schema.\n",
		       kokkos_store.name, rc);
		return rc;
	}
	*app = schema;
	return 0;
}

static int container_mode = 0660;	/* Default container permission bits */
static sos_t sos;
static int reopen_container(char *path)
{
	int rc = 0;

	/* Check if the configuration has changed */
	if (sos && (0 == strcmp(path, sos_container_path(sos)))
	    && (container_mode == sos_container_mode(sos)))
		/* No change, ignore request */
		return 0;

	/* Close the container if it already exists */
	if (sos)
		sos_container_close(sos, SOS_COMMIT_ASYNC);

	/* Check if the container at path is already present */
	sos = sos_container_open(path, SOS_PERM_RW|SOS_PERM_CREAT, container_mode);
	if (!sos)
		return errno;

	app_schema = sos_schema_by_name(sos, kokkos_appmon_template.name);
	if (!app_schema) {
		rc = create_schema(sos, &app_schema);
		if (rc)
			return rc;
	}
	return rc;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=kokkos_appmon_store path=<path> port=<port_no> log=<path>\n"
		"     path      The path to the root of the SOS container store (required).\n"
		"     stream    The stream name to subscribe to (defaults to 'kokkos').\n"
		"     mode      The container permission mode for create, (defaults to 0660).\n";
}

static int stream_recv_cb(ldms_stream_event_t ev, void *ctxt);

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc = 0;

	pthread_mutex_lock(&cfg_lock);
	value = av_value(avl, "mode");
	if (value)
		container_mode = strtol(value, NULL, 0);
	if (!container_mode) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: ignoring container permission mode of %s, using 0660.\n",
		       kokkos_store.name, value);
	}
	value = av_value(avl, "stream");
	if (value)
		stream = strdup(value);
	else
		stream = strdup("kokkos-perf-data");
	ldms_stream_subscribe(stream, 0, stream_recv_cb, handle, "kokkos_appmon");

	value = av_value(avl, "path");
	if (!value) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: the path to the container (path=) must be specified.\n",
		       kokkos_store.name);
		rc = ENOENT;
		goto out;
	}
	if (root_path)
		free(root_path);
	root_path = strdup(value);
	if (!root_path) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: Error allocating %d bytes for the container path.\n",
		       strlen(value) + 1);
		rc = ENOMEM;
		goto out;
	}

	rc = reopen_container(root_path);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "%s: Error opening %s.\n",
		       kokkos_store.name, root_path);
	}
 out:
	pthread_mutex_unlock(&cfg_lock);
	return rc;
}

static int get_json_value(json_entity_t e, char *name, int expected_type, json_entity_t *out)
{
	int v_type;
	json_entity_t a = json_attr_find(e, name);
	json_entity_t v;
	if (!a) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The JSON entity is missing the '%s' attribute.\n",
		       kokkos_store.name,
		       name);
		return EINVAL;
	}
	v = json_attr_value(a);
	v_type = json_entity_type(v);
	if (v_type != expected_type) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: The '%s' JSON entity is the wrong type. "
		       "Expected %d, received %d\n",
		       kokkos_store.name,
		       name, expected_type, v_type);
		return EINVAL;
	}
	*out = v;
	return 0;
}

static int stream_recv_cb(ldms_stream_event_t ev, void *ctxt)
{
	int rc;
	json_entity_t v, list, item;
	uint64_t rank, job_id;
	double timestamp, current_kernel_time, total_kernel_time;
	uint64_t level, type, current_kernel_count, total_kernel_count;
	char *name, *node_name;

	if (ev->type != LDMS_STREAM_EVENT_RECV)
		return 0;

	if (!ev->recv.json) {
		ovis_log(mylog, OVIS_LERROR,
		       "NULL entity received in stream callback.\n");
		return 0;
	}
	rc = get_json_value(ev->recv.json, "rank", JSON_INT_VALUE, &v);
	if (rc)
		goto out;
	rank = json_value_int(v);

	rc = get_json_value(ev->recv.json, "job-id", JSON_INT_VALUE, &v);
	if (rc)
		goto out;
	job_id = json_value_int(v);

	rc = get_json_value(ev->recv.json, "node-name", JSON_STRING_VALUE, &v);
	if (rc)
		goto out;
	node_name = json_value_str(v)->str;

	rc = get_json_value(ev->recv.json, "timestamp", JSON_STRING_VALUE, &v);
	if (rc)
		goto out;
	timestamp = strtod(json_value_str(v)->str, NULL);

	rc = get_json_value(ev->recv.json, "kokkos-perf-data", JSON_LIST_VALUE, &list);
	if (rc)
		goto out;
	for (item = json_item_first(list); item; item = json_item_next(item)) {

		if (json_entity_type(item) != JSON_DICT_VALUE) {
			ovis_log(mylog, OVIS_LERROR,
			       "%s: Items in kokkos-perf-data must all be dictionaries.\n",
			       kokkos_store.name);
			rc = EINVAL;
			goto out;
		}

		rc = get_json_value(item, "name", JSON_STRING_VALUE, &v);
		if (rc)
			goto out;
		name = json_value_str(v)->str;

		rc = get_json_value(item, "type", JSON_INT_VALUE, &v);
		if (rc)
			goto out;
		type = json_value_int(v);

		rc = get_json_value(item, "current-kernel-count", JSON_INT_VALUE, &v);
		if (rc)
			goto out;
		current_kernel_count = json_value_int(v);

		rc = get_json_value(item, "total-kernel-count", JSON_INT_VALUE, &v);
		if (rc)
			goto out;
		total_kernel_count = json_value_int(v);

		rc = get_json_value(item, "level", JSON_INT_VALUE, &v);
		if (rc)
			goto out;
		level = json_value_int(v);

		rc = get_json_value(item, "current-kernel-time", JSON_FLOAT_VALUE, &v);
		if (rc)
			goto out;
		current_kernel_time = json_value_float(v);

		rc = get_json_value(item, "total-kernel-time", JSON_FLOAT_VALUE, &v);
		if (rc)
			goto out;
		total_kernel_time = json_value_float(v);

		sos_obj_t obj = sos_obj_new(app_schema);
		if (!obj) {
			rc = errno;
			ovis_log(mylog, OVIS_LERROR,
			       "%s: Error %d creating Kokkos App Mon object.\n",
			       kokkos_store.name, errno);
			goto out;
		}
		sos_obj_attr_by_id_set(obj, TIMESTAMP_ID, timestamp);
		sos_obj_attr_by_id_set(obj, JOB_ID, job_id);
		sos_obj_attr_by_id_set(obj, NODE_NAME_ID, strlen(node_name), node_name);
		sos_obj_attr_by_id_set(obj, RANK_ID, rank);
		sos_obj_attr_by_id_set(obj, NAME_ID, strlen(name), name);
		sos_obj_attr_by_id_set(obj, TYPE_ID, type);
		sos_obj_attr_by_id_set(obj, CURRENT_KERNEL_COUNT_ID, current_kernel_count);
		sos_obj_attr_by_id_set(obj, TOTAL_KERNEL_COUNT_ID, total_kernel_count);
		sos_obj_attr_by_id_set(obj, LEVEL_ID, level);
		sos_obj_attr_by_id_set(obj, CURRENT_KERNEL_TIME_ID, current_kernel_time);
		sos_obj_attr_by_id_set(obj, TOTAL_KERNEL_TIME_ID, total_kernel_time);
		sos_obj_index(obj);
		sos_obj_put(obj);
	}
	rc = 0;
 out:
	return rc;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	if (sos)
		sos_container_close(sos, SOS_COMMIT_ASYNC);
	if (root_path)
		free(root_path);
}

struct ldmsd_plugin ldmsd_plugin_interface = {
	.config = config,
	.usage = usage,
        .constructor = constructor,
        .destructor = destructor,
};
