/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 National Technology & Engineering Solutions
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
#include "ldmsd_stream.h"
#include "lpss_common.h"

#define UUID "9651a09c-b12e-41d2-afb0-7109b28cc559"
#define STREAM "linux_proc_sampler_env"
#define LISTNAME "data"

static ldmsd_msg_log_f msglog;
static sos_schema_t app_schema;
static char *stream;
static char *root_path;
static struct ldmsd_plugin stream_store;

static const char *job_component_k_pid_attrs[] = { "job_id", "component_id", "k", "pid" };
static const char *k_job_component_pid_attrs[] = { "k", "job_id", "component_id", "pid" };

/*
 * Example JSON object:
 *
 * { \"job_id\" : 0,
 *   \"component_id\" : 0,
 *   \"ProducerName\" : \"nid00021\",
 *   \"pid\" : 241011,
 *   \"timestamp\" : \"163000348.3312\",
 *   \"task_rank\" : -1,
 *   \"parent\" : 31585,
 *   \"is_thread\" : 1,
 *   \"exe\" : \"/usr/lib/jvm/java-1.8.0-openjdk-1.8.0.222.b10-0.el7_6.aarch64/jre/bin/java\",
 *   \"data\" :
 *	[{
 *	    \"k\" : \"LANG\",
 *	    \"v\" : \"en_US.UTF-8\"
 *	  }
 *     ]
 *  }
 */

static struct sos_schema_template proc_files_template = {
	.name = STREAM,
	.uuid = UUID,
	.attrs = {
		{ .name = "job_id", .type = SOS_TYPE_UINT64 },
		{ .name = "component_id", .type = SOS_TYPE_UINT64 },
		{ .name = "ProducerName", .type = SOS_TYPE_STRING },
		{ .name = "pid", .type = SOS_TYPE_UINT64 },
		{ .name = "timestamp", .type = SOS_TYPE_STRING },
		{ .name = "task_rank", .type = SOS_TYPE_INT64 },
		{ .name = "parent", .type = SOS_TYPE_UINT64 },
		{ .name = "is_thread", .type = SOS_TYPE_UINT64 },
		{ .name = "exe", .type = SOS_TYPE_STRING },
		{ .name = "k", .type = SOS_TYPE_STRING },
		{ .name = "v", .type = SOS_TYPE_STRING },
		{ .name = "job_component_k_pid", .type = SOS_TYPE_JOIN,
		  .size = ARRAY_SIZE(job_component_k_pid_attrs),
		  .indexed = 1,
		  .join_list = job_component_k_pid_attrs
		},
		{ .name = "k_job_component_pid", .type = SOS_TYPE_JOIN,
		  .size = ARRAY_SIZE(k_job_component_pid_attrs),
		  .indexed = 1,
		  .join_list = k_job_component_pid_attrs
		},

		{ 0 }
	}
};


enum attr_ids {
       JOB_ID,
       COMPONENT_ID,
       PRODUCERNAME_ID,
       PID_ID,
       TIMESTAMP_ID,
       TASK_RANK_ID,
       PARENT_ID,
       IS_THREAD_ID,
       EXE_ID,
       K_ID,
       V_ID,
};

static int create_schema(sos_t sos, sos_schema_t *app)
{
	int rc;
	sos_schema_t schema;

	/* Create and add the App schema */
	schema = sos_schema_from_template(&proc_files_template);
	if (!schema) {
		msglog(LDMSD_LERROR, "%s: Error %d creating Darshan data schema.\n",
		       stream_store.name, errno);
		rc = errno;
		goto err;
	}
	rc = sos_schema_add(sos, schema);
	if (rc) {
		msglog(LDMSD_LERROR, "%s: Error %d adding Darshan data schema.\n",
				stream_store.name, rc);
		goto err;
	}
	*app = schema;
	return 0;

err:
	if (schema) {
		free(schema);
	}
	return rc;

}

static int container_mode = 0660;	/* Default container permission bits */
static sos_t sos;
static int reopen_container(char *path)
{
	int rc = 0;

	/* Close the container if it already exists */
	if (sos)
		sos_container_close(sos, SOS_COMMIT_ASYNC);


	/* Creates the container if it doesn't already exist  */
	sos = sos_container_open(path, SOS_PERM_RW|SOS_PERM_CREAT, container_mode);
	if (!sos) {
		return errno;
	}

	app_schema = sos_schema_by_name(sos, proc_files_template.name);
	if (!app_schema) {
		rc = create_schema(sos, &app_schema);
		if (rc)
			return rc;
	}
	return rc;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return	"config name=" STREAM "_store path=<path> port=<port_no> log=<path>\n"
		"     path	The path to the root of the SOS container store (required).\n"
		"     stream	The stream name to subscribe to (defaults to " STREAM ").\n"
		"     mode	The container permission mode for create, (defaults to 0660).\n";
}

static int stream_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity);
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc;
	value = av_value(avl, "mode");
	if (value)
		container_mode = strtol(value, NULL, 0);
	if (!container_mode) {
		msglog(LDMSD_LERROR,
		       "%s: ignoring bogus container permission mode of %s, using 0660.\n",
		       stream_store.name, value);
	}

	value = av_value(avl, "stream");
	if (value)
		stream = strdup(value);
	else
		stream = strdup(STREAM);
	ldmsd_stream_subscribe(stream, stream_recv_cb, self);

	value = av_value(avl, "path");
	if (!value) {
		msglog(LDMSD_LERROR,
		       "%s: the path to the container (path=) must be specified.\n",
		       stream_store.name);
		return ENOENT;
	}

	if (root_path)
		free(root_path);
	root_path = strdup(value);
	if (!root_path) {
		msglog(LDMSD_LERROR,
		       "%s: Error allocating %d bytes for the container path.\n",
		       strlen(value) + 1);
		return ENOMEM;
	}

	rc = reopen_container(root_path);
	if (rc) {
		msglog(LDMSD_LERROR, "%s: Error opening %s.\n",
		       stream_store.name, root_path);
		return ENOENT;
	}
	return 0;
}

static int get_json_value(json_entity_t e, char *name, int expected_type, json_entity_t *out)
{
	int v_type;
	json_entity_t a = json_attr_find(e, name);
	json_entity_t v;
	if (!a) {
		msglog(LDMSD_LERROR,
		       "%s: The JSON entity is missing the '%s' attribute.\n",
		       stream_store.name,
		       name);
		return EINVAL;
	}
	v = json_attr_value(a);
	v_type = json_entity_type(v);
	if (v_type != expected_type) {
		msglog(LDMSD_LERROR,
		       "%s: The '%s' JSON entity is the wrong type. "
		       "Expected %d, received %d\n",
		       stream_store.name,
		       name, expected_type, v_type);
		return EINVAL;
	}
	*out = v;
	return 0;
}


static int stream_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			  ldmsd_stream_type_t stream_type,
			  const char *msg, size_t msg_len,
			  json_entity_t entity)
{
	int rc, task_rank;
	json_entity_t v, list, item;
	uint64_t job_id, component_id, pid, parent, is_thread;
	char *producer_name, *timestamp, *exec, *json_k, *json_v;

	if (!entity) {
		msglog(LDMSD_LERROR,
		       "%s: NULL entity received in stream callback.\n",
		       stream_store.name);
		return 0;
	}

	rc = get_json_value(entity, "job_id", JSON_INT_VALUE, &v);
	if (rc)
		goto err;
	job_id = json_value_int(v);

	rc = get_json_value(entity, "component_id", JSON_INT_VALUE, &v);
	if (rc)
		goto err;
	component_id = json_value_int(v);

	rc = get_json_value(entity, "ProducerName", JSON_STRING_VALUE, &v);
	if (rc)
		goto err;
	producer_name = json_value_str(v)->str;

	rc = get_json_value(entity, "pid", JSON_INT_VALUE, &v);
	if (rc)
		goto err;
	pid = json_value_int(v);

	rc = get_json_value(entity, "timestamp", JSON_STRING_VALUE, &v);
	if (rc)
		goto err;
	timestamp = json_value_str(v)->str;

	rc = get_json_value(entity, "task_rank", JSON_INT_VALUE, &v);
	if (rc)
		goto err;
	task_rank = json_value_int(v);

	rc = get_json_value(entity, "parent", JSON_INT_VALUE, &v);
	if (rc)
		goto err;
	parent = json_value_int(v);

	rc = get_json_value(entity, "is_thread", JSON_INT_VALUE, &v);
	if (rc)
		goto err;
	is_thread = json_value_int(v);

	rc = get_json_value(entity, "exe", JSON_STRING_VALUE, &v);
	if (rc)
		goto err;
	exec = json_value_str(v)->str;

	rc = get_json_value(entity, LISTNAME, JSON_LIST_VALUE, &list);
	if (rc)
		goto err;
	for (item = json_item_first(list); item; item = json_item_next(item)) {

		if (json_entity_type(item) != JSON_DICT_VALUE) {
			msglog(LDMSD_LERROR,
			       "%s: Items in segment must all be dictionaries.\n",
			       stream_store.name);
			rc = EINVAL;
			goto err;
		}

		rc = get_json_value(item, "k", JSON_STRING_VALUE, &v);
		if (rc)
			goto err;
		json_k = json_value_str(v)->str;

		rc = get_json_value(item, "v", JSON_STRING_VALUE, &v);
		if (rc)
			goto err;
		json_v = json_value_str(v)->str;

		sos_obj_t obj = sos_obj_new(app_schema);
		if (!obj) {
			rc = errno;
			msglog(LDMSD_LERROR,
			       "%s: Error %d creating Darshan data object.\n",
			       stream_store.name, errno);
			goto err;
		}

		msglog(LDMSD_LDEBUG, "%s: Got a record from stream (%s)",
				stream_store.name, stream);


		sos_obj_attr_by_id_set(obj, JOB_ID, job_id);
		sos_obj_attr_by_id_set(obj, COMPONENT_ID, component_id);
		sos_obj_attr_by_id_set(obj, PRODUCERNAME_ID, strlen(producer_name)+1, producer_name);
		sos_obj_attr_by_id_set(obj, PID_ID, pid);
		sos_obj_attr_by_id_set(obj, TIMESTAMP_ID, strlen(timestamp)+1, timestamp);
		sos_obj_attr_by_id_set(obj, TASK_RANK_ID, task_rank);
		sos_obj_attr_by_id_set(obj, PARENT_ID, parent);
		sos_obj_attr_by_id_set(obj, IS_THREAD_ID, is_thread);
		sos_obj_attr_by_id_set(obj, EXE_ID, strlen(exec)+1,  exec);
		sos_obj_attr_by_id_set(obj, K_ID, strlen(json_k)+1, json_k);
		sos_obj_attr_by_id_set(obj, V_ID, strlen(json_v)+1, json_v);


		sos_obj_index(obj);
		sos_obj_put(obj);
	}
	rc = 0;
 err:
	return rc;
}

static void term(struct ldmsd_plugin *self)
{
	if (sos)
		sos_container_close(sos, SOS_COMMIT_ASYNC);
	free(root_path);
	root_path = NULL;
	free(stream);
	stream = NULL;
}

static struct ldmsd_plugin stream_store = {
	.name = STREAM "_store",
	.term = term,
	.config = config,
	.usage = usage,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &stream_store;
}
