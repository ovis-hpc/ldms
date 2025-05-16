/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018,2023 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018,2023 Open Grid Computing, Inc. All rights reserved.
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

static htbl_t act_table;
static sos_t sos;
static sos_schema_t app_schema;
static sos_schema_t kernel_schema;
static sos_attr_t kernel_job_id_attr;
static sos_attr_t kernel_start_time_attr;
static sos_attr_t kernel_end_time_attr;
static sos_attr_t kernel_app_id_attr;
static sos_attr_t kernel_comp_id_attr;
static sos_attr_t kernel_mpi_rank_attr;
static sos_attr_t kernel_job_tag_attr;
static sos_schema_t sha256_schema;
static sos_attr_t sha256_string_attr;
static sos_attr_t sha256_digest_attr;

static char path_buff[PATH_MAX];
static char *log_path = "/var/log/ldms/kokkos_kokkos_store.log";
static char *verbosity = "WARN";
static char *stream;

typedef struct kokkos_context_s {
	SHA256_CTX sha_ctxt;

	sos_obj_t app_obj;
	sos_obj_t kernel_obj;

	/* These atttributes are inherited by the kernel objects */
	uint64_t job_id;
	uint64_t app_id;
	uint64_t mpi_rank;
	uint64_t component_id;
	double start_time;
	double end_time;
	uint8_t job_tag[SHA256_DIGEST_LENGTH];

} *kokkos_context_t;

static char *root_path;

static struct ldmsd_plugin kokkos_store;

struct action;
typedef struct action *action_t;
typedef int (*act_process_fn_t)(kokkos_context_t, json_entity_t, sos_obj_t obj, sos_attr_t attr);
struct action {
	act_process_fn_t act_fn;
	sos_schema_t schema;
	sos_attr_t attr;
	int app_n_kernel;
	struct hent hent;
};

static action_t get_act(const char *key, size_t key_len)
{
	hent_t hent = htbl_find(act_table, key, key_len);
	if (hent)
		return container_of(hent, struct action, hent);
	return NULL;
}

static int process_string(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	struct sos_value_s v_;
	assert(obj);
	sos_value_t v = sos_array_new(&v_, attr, obj, e->value.str_->str_len);
	if (v) {
		memcpy(v->data->array.data.char_, e->value.str_->str, e->value.str_->str_len);
		sos_value_put(v);
		return 0;
	}
	return ENOMEM;
}

struct visit_cb_ctxt {
	kokkos_context_t k;
	json_entity_t e;
	int rc;
	char digest[SHA256_DIGEST_LENGTH];
};

static sos_visit_action_t add_digest_cb(sos_index_t index,
					sos_key_t key, sos_idx_data_t *idx_data,
					int found, void *arg)
{
	struct sos_value_s v_, *v;
	sos_value_data_t digest;
	sos_obj_t obj;
	sos_obj_ref_t ref;
	struct visit_cb_ctxt *ctxt = arg;

	if (found) {
		ctxt->rc = EEXIST;
		return SOS_VISIT_NOP;
	}

	/* Allocate a new object */
	obj = sos_obj_new(sha256_schema);
	if (!obj)
		goto err_0;

	digest = sos_obj_attr_data(obj, sha256_digest_attr, NULL);
	if (!digest) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: Error %d getting the data for the sha256 attribute.\n"
		       "Digest key was not added.\n",
		       kokkos_store.name, errno);
		goto err_1;
	}
	memcpy(digest->prim.struc_, ctxt->digest, sos_attr_size(sha256_digest_attr));

	v = sos_array_new(&v_, sha256_string_attr, obj, ctxt->e->value.str_->str_len);
	if (!v) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: Error %d allocating the digest string.\n",
		       kokkos_store.name, errno);
		goto err_1;
	}
	sos_value_memcpy(v, ctxt->e->value.str_->str, ctxt->e->value.str_->str_len);
	sos_value_put(v);

	ref = sos_obj_ref(obj);
	*idx_data = ref.idx_data;
	sos_obj_put(obj);
	ctxt->rc = 0;
	return SOS_VISIT_ADD;

 err_1:
	sos_obj_delete(obj);
	sos_obj_put(obj);

 err_0:
	ctxt->rc = ENOMEM;
	return SOS_VISIT_NOP;
}

int add_digest(kokkos_context_t k, json_entity_t e, char *digest)
{
	struct visit_cb_ctxt ctxt;
	SOS_KEY(key);
	int rc;

	memcpy(ctxt.digest, digest, sizeof(ctxt.digest));
	sos_key_set(key, digest, sos_attr_size(sha256_digest_attr));
	ctxt.k = k;
	ctxt.e = e;
	ctxt.rc = 0;
	rc = sos_index_visit(sos_attr_index(sha256_digest_attr), key, add_digest_cb, &ctxt);
	if (rc != ENOMEM)
		rc = ctxt.rc;
	return ctxt.rc;
}

static int ignore(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	return 0;
}

static int process_job_tag(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	sos_value_data_t data;

	/* Compute the hash and save it in the parser */
	SHA256((unsigned char *)e->value.str_->str, e->value.str_->str_len, k->job_tag);

	/* Add the digest->string map */
	add_digest(k, e, (char*)k->job_tag);

	/* Add the object digest attribute */
	data = sos_obj_attr_data(obj, attr, NULL);
	memcpy(data->prim.struc_, k->job_tag, sos_attr_size(attr));

	return 0;
}

static int process_digest(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	sos_value_data_t data;
	char digest[SHA256_DIGEST_LENGTH];

	/* Compute the hash and save it in the parser */
	SHA256((unsigned char*)e->value.str_->str, e->value.str_->str_len, (uint8_t*)digest);

	/* Add the digest->string map */
	add_digest(k, e, digest);

	/* Add the object digest attribute */
	data = sos_obj_attr_data(obj, attr, NULL);
	memcpy(data->prim.struc_, digest, sos_attr_size(attr));

	return 0;
}

static int process_double(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	sos_value_data_t data;
	assert(obj);
	data = sos_obj_attr_data(obj, attr, NULL);
	if (e->type == JSON_FLOAT_VALUE)
		data->prim.double_ = e->value.double_;
	else if (e->type == JSON_INT_VALUE)
		data->prim.double_ = (double)e->value.int_;
	else
		data->prim.double_ = 0.0;
	return 0;
}

static int process_int(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	sos_value_data_t data;
	assert(obj);
	data = sos_obj_attr_data(obj, attr, NULL);
	data->prim.uint64_ = e->value.int_;
	return 0;
}

static union sos_timestamp_u
to_timestamp(double d)
{
	union sos_timestamp_u u;
	double secs = floor(d);
	u.fine.secs = secs;
	u.fine.usecs = d - secs;
	return u;
}

static int process_start_time(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	sos_value_data_t data;
	assert(obj);
	if (e->value.double_ == 0)
		return EINVAL;
	data = sos_obj_attr_data(obj, attr, NULL);
	data->prim.timestamp_ = to_timestamp(e->value.double_);
	k->start_time = e->value.double_;
	return 0;
}

static int process_end_time(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	sos_value_data_t data;
	assert(obj);
	if (e->value.double_ == 0)
		return EINVAL;
	data = sos_obj_attr_data(obj, attr, NULL);
	data->prim.timestamp_ = to_timestamp(e->value.double_);
	k->end_time = e->value.double_;
	return 0;
}

static int process_job_id(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	sos_value_data_t data;
	assert(obj);
	if (e->value.int_ == 0)
		return EINVAL;
	data = sos_obj_attr_data(obj, attr, NULL);
	k->job_id = e->value.int_;
	data->prim.uint64_ = e->value.int_;
	return 0;
}

static int process_app_id(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	sos_value_data_t data;
	assert(obj);
	data = sos_obj_attr_data(obj, attr, NULL);
	k->app_id = e->value.int_;
	data->prim.uint64_ = e->value.int_;
	return 0;
}

static int process_component_id(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	sos_value_data_t data;
	assert(obj);
	data = sos_obj_attr_data(obj, attr, NULL);
	if (e->value.int_ < 0)
		k->component_id = 0;
	else
		k->component_id = e->value.int_;
	data->prim.uint64_ = k->component_id;
	return 0;
}

static int process_mpi_rank(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	sos_value_data_t data;
	assert(obj);
	data = sos_obj_attr_data(obj, attr, NULL);

	/* If the kokkos job wasn't MPI, use the component-id for the rank */
	if (e->value.int_ < 0)
		k->mpi_rank = k->component_id;
	else
		k->mpi_rank = e->value.int_;
	data->prim.uint64_ = k->mpi_rank;

	return 0;
}

static int process_sample_entity(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	action_t act;
	sos_value_data_t data;
	json_entity_t a;
	int rc = 0;

	if (e->type != JSON_DICT_VALUE) {
		ovis_log(mylog, OVIS_LERROR, "%s: The sample entity must be a dictionary, not a %s\n",
		       kokkos_store.name, json_type_name(e->type));
		return EINVAL;
	}

	assert(k->kernel_obj == NULL);
	k->kernel_obj = sos_obj_new(kernel_schema);
	if (!k->kernel_obj) {
		ovis_log(mylog, OVIS_LERROR, "%s: Eror %d creating sample object.\n",
		       kokkos_store.name, errno);
		return errno;
	}
	data = sos_obj_attr_data(k->kernel_obj, kernel_job_id_attr, NULL);
	data->prim.uint64_ = k->job_id;

	data = sos_obj_attr_data(k->kernel_obj, kernel_start_time_attr, NULL);
	data->prim.timestamp_ = to_timestamp(k->start_time);

	data = sos_obj_attr_data(k->kernel_obj, kernel_end_time_attr, NULL);
	data->prim.timestamp_ = to_timestamp(k->end_time);

	data = sos_obj_attr_data(k->kernel_obj, kernel_app_id_attr, NULL);
	data->prim.uint64_ = k->app_id;

	data = sos_obj_attr_data(k->kernel_obj, kernel_comp_id_attr, NULL);
	data->prim.uint64_ = k->component_id;

	data = sos_obj_attr_data(k->kernel_obj, kernel_mpi_rank_attr, NULL);
	data->prim.uint64_ = k->mpi_rank;

	data = sos_obj_attr_data(k->kernel_obj, kernel_job_tag_attr, NULL);
	memcpy(data->prim.struc_, k->job_tag, sizeof(k->job_tag));

	for (a = json_attr_first(e); a; a = json_attr_next(a)) {
		json_attr_t attr = a->value.attr_;
		act = get_act(attr->name->value.str_->str,
			      attr->name->value.str_->str_len);
		if (!act) {
			ovis_log(mylog, OVIS_LERROR, "%s: '%s' is not a recognized attribute name.\n",
			       kokkos_store.name, attr->name->value.str_->str);
			continue;
		}
		if (act->app_n_kernel)
			rc = act->act_fn(k, attr->value, k->app_obj, act->attr);
		else
			rc = act->act_fn(k, attr->value, k->kernel_obj, act->attr);
		if (rc)
			goto err;
	}
	sos_obj_index(k->kernel_obj);
	sos_obj_put(k->kernel_obj);
	k->kernel_obj = NULL;
	return 0;
 err:
	sos_obj_delete(k->kernel_obj);
	sos_obj_put(k->kernel_obj);
	k->kernel_obj = NULL;
	return rc;
}

static int process_list_entity(kokkos_context_t p, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	json_entity_t i;
	if (e->type != JSON_LIST_VALUE) {
		ovis_log(mylog, OVIS_LERROR, "%s: The kernel-perf-info entity "
			  "must be a JSon array, i.e. []. Got a %d.\n",
			  kokkos_store.name, e->type);
		return 1;
	}
	for (i = json_item_first(e); i; i = json_item_next(i)) {
		process_sample_entity(p, i, NULL, NULL);
	}
	return 0;
}

static int process_dict_entity(kokkos_context_t k, json_entity_t e, sos_obj_t obj, sos_attr_t attr)
{
	action_t act;
	json_entity_t a;
	int rc = 0;

	if (e->type != JSON_DICT_VALUE) {
		ovis_log(mylog, OVIS_LERROR, "%s: Expected a dictionary object, not a %s.\n",
		       kokkos_store.name, json_type_name(e->type));
		return EINVAL;
	}

	for (a = json_attr_first(e); a; a = json_attr_next(a)) {
		json_attr_t attr = a->value.attr_;
		act = get_act(attr->name->value.str_->str,
			      attr->name->value.str_->str_len);
		if (!act) {
			ovis_log(mylog, OVIS_LERROR, "%s: '%s' is not a recognized attribute name.\n",
			       kokkos_store.name, attr->name->value.str_->str);
			continue;
		}
		if (act->app_n_kernel)
			rc = act->act_fn(k, attr->value, k->app_obj, act->attr);
		else
			rc = act->act_fn(k, attr->value, k->kernel_obj, act->attr);
		if (rc)
			break;
	}
	return rc;
}

static struct sos_schema_template kokkos_sha256_template = {
	.name = "sha256_string",
	.uuid = "4b777639-7ec4-4e10-9747-b1d3a96400a0",
	.attrs = {
		{ .name = "sha256",
		  .type = SOS_TYPE_STRUCT, .size = 32,
		  .indexed = 1, .idx_type = "HTBL" },
		{ .name = "string", .type = SOS_TYPE_CHAR_ARRAY },
		{}
	}
};

static const char *time_job_comp_attrs[] = { "start_time", "job_id", "component_id" };
static const char *job_comp_time_attrs[] = { "job_id", "component_id", "start_time" };
static const char *job_time_comp_attrs[] = { "job_id", "start_time", "component_id" };
static const char *comp_time_job_attrs[] = { "component_id", "start_time", "job_id" };
static const char *comp_job_time_attrs[] = { "component_id", "job_id", "start_time" };
static const char *time_comp_job_attrs[] = { "start_time", "component_id", "job_id" };
static struct sos_schema_template kokkos_app_template = {
	.name = "kokkos_app",
	.uuid = "dece6e5c-16d9-4f7e-8a44-8df117288f06",
	.attrs = {
		{ .name = "job_id", .type = SOS_TYPE_UINT64 },
		{ .name = "job_name", .type = SOS_TYPE_CHAR_ARRAY },
		{ .name = "app_id", .type = SOS_TYPE_UINT64 },
		{ .name = "job_tag", .type = SOS_TYPE_STRUCT,	.size = 32 },
		{ .name = "start_time",	.type = SOS_TYPE_TIMESTAMP },
		{ .name = "end_time",	.type = SOS_TYPE_TIMESTAMP },
		{ .name = "mpi_rank", .type = SOS_TYPE_UINT64 },
		{ .name = "hostname", .type = SOS_TYPE_STRING },
		{ .name = "user_id", .type = SOS_TYPE_UINT32 },
		{ .name = "component_id", .type = SOS_TYPE_UINT64 },
		{ .name = "total_app_time", .type = SOS_TYPE_DOUBLE },
		{ .name = "total_kernel_times", .type = SOS_TYPE_DOUBLE },
		{ .name = "total_non_kernel_times", .type = SOS_TYPE_DOUBLE },
		{ .name = "percent_in_kernels", .type = SOS_TYPE_DOUBLE },
		{ .name = "unique_kernel_calls", .type = SOS_TYPE_DOUBLE },

		{ .name = "time_job_comp", .type = SOS_TYPE_JOIN,
		  .size = sizeof(time_job_comp_attrs) / sizeof(time_job_comp_attrs[0]),
		  .indexed = 1,
		  .join_list = time_job_comp_attrs
		},
		{ .name = "time_comp_job", .type = SOS_TYPE_JOIN,
		  .size = sizeof(time_comp_job_attrs) / sizeof(time_comp_job_attrs[0]),
		  .indexed = 1,
		  .join_list = time_comp_job_attrs
		},
		{ .name = "job_comp_time", .type = SOS_TYPE_JOIN,
		  .size = sizeof(job_comp_time_attrs) / sizeof(job_comp_time_attrs[0]),
		  .indexed = 1,
		  .join_list = job_comp_time_attrs
		},
		{ .name = "job_time_comp", .type = SOS_TYPE_JOIN,
		  .size = sizeof(job_time_comp_attrs) / sizeof(job_time_comp_attrs[0]),
		  .indexed = 1,
		  .join_list = job_time_comp_attrs
		},
		{ .name = "comp_time_job", .type = SOS_TYPE_JOIN,
		  .size = sizeof(comp_time_job_attrs) / sizeof(comp_time_job_attrs[0]),
		  .indexed = 1,
		  .join_list = comp_time_job_attrs
		},
		{ .name = "comp_job_time", .type = SOS_TYPE_JOIN,
		  .size = sizeof(comp_job_time_attrs) / sizeof(comp_job_time_attrs[0]),
		  .indexed = 1,
		  .join_list = comp_job_time_attrs
		},
		{}
	}
};

static struct sos_schema_template kokkos_kernel_template = {
	.name = "kokkos_kernel",
	.uuid = "a89531ba-18b8-4089-a03e-a834e48f64ad",
	.attrs = {
		{ .name = "job_id", .type = SOS_TYPE_UINT64 },
		{ .name = "app_id", .type = SOS_TYPE_UINT64 },
		{ .name = "component_id", .type = SOS_TYPE_UINT64 },
		{ .name = "job_tag", .type = SOS_TYPE_STRUCT,	.size = 32 },
		{ .name = "start_time",	.type = SOS_TYPE_TIMESTAMP },
		{ .name = "end_time",	.type = SOS_TYPE_TIMESTAMP },
		{ .name = "mpi_rank", .type = SOS_TYPE_UINT64 },
		{ .name = "kernel_name", .type = SOS_TYPE_STRUCT, .size = 32 },
		{ .name = "kernel_type", .type = SOS_TYPE_CHAR_ARRAY },
		{ .name = "region", .type = SOS_TYPE_CHAR_ARRAY },
		{ .name = "call_count", .type = SOS_TYPE_DOUBLE },
		{ .name = "total_time", .type = SOS_TYPE_DOUBLE },
		{ .name = "time_per_call", .type = SOS_TYPE_DOUBLE },

		{ .name = "time_job_comp", .type = SOS_TYPE_JOIN,
		  .size = sizeof(time_job_comp_attrs) / sizeof(time_job_comp_attrs[0]),
		  .indexed = 1,
		  .join_list = time_job_comp_attrs
		},
		{ .name = "time_comp_job", .type = SOS_TYPE_JOIN,
		  .size = sizeof(time_comp_job_attrs) / sizeof(time_comp_job_attrs[0]),
		  .indexed = 1,
		  .join_list = time_comp_job_attrs
		},
		{ .name = "job_comp_time", .type = SOS_TYPE_JOIN,
		  .size = sizeof(job_comp_time_attrs) / sizeof(job_comp_time_attrs[0]),
		  .indexed = 1,
		  .join_list = job_comp_time_attrs
		},
		{ .name = "job_time_comp", .type = SOS_TYPE_JOIN,
		  .size = sizeof(job_time_comp_attrs) / sizeof(job_time_comp_attrs[0]),
		  .indexed = 1,
		  .join_list = job_time_comp_attrs
		},
		{ .name = "comp_time_job", .type = SOS_TYPE_JOIN,
		  .size = sizeof(comp_time_job_attrs) / sizeof(comp_time_job_attrs[0]),
		  .indexed = 1,
		  .join_list = comp_time_job_attrs
		},
		{ .name = "comp_job_time", .type = SOS_TYPE_JOIN,
		  .size = sizeof(comp_job_time_attrs) / sizeof(comp_job_time_attrs[0]),
		  .indexed = 1,
		  .join_list = comp_job_time_attrs
		},
		{ 0 }
	}
};

static int create_container(char *path)
{
	int rc = 0;
	sos_t sos;
	time_t t;
	char part_name[16];	/* Unix timestamp as string */
	sos_part_t part;

	rc = sos_container_new(path, 0660);
	if (rc && rc != EEXIST) {
		ovis_log(mylog, OVIS_LERROR, "Error %d creating the container at '%s'\n",
		       rc, path);
		goto err_0;
	}
	sos = sos_container_open(path, SOS_PERM_RW);
	if (!sos) {
		ovis_log(mylog, OVIS_LERROR, "Error %d opening the container at '%s'\n",
		       errno, path);
		goto err_0;
	}
	/*
	 * Create the first partition. All other partitions and
	 * rollover are handled with the SOS partition commands
	 */
	t = time(NULL);
	sprintf(part_name, "%d", (unsigned int)t);
	rc = sos_part_create(sos, part_name, path);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "Error %d creating the partition '%s' in '%s'\n",
		       rc, part_name, path);
		goto err_1;
	}
	part = sos_part_find(sos, part_name);
	if (!part) {
		ovis_log(mylog, OVIS_LERROR, "Newly created partition was not found\n");
		goto err_1;
	}
	rc = sos_part_state_set(part, SOS_PART_STATE_PRIMARY);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "New partition could not be made primary\n");
		goto err_2;
	}
	sos_part_put(part);
	return 0;
 err_2:
	sos_part_put(part);
 err_1:
	sos_container_close(sos, SOS_COMMIT_ASYNC);
	sos = NULL;
 err_0:
	return rc;
}

static int create_schema(sos_t sos,
			 sos_schema_t *app, sos_schema_t *kernel,
			 sos_schema_t *sha256)
{
	int rc;
	sos_schema_t schema;

	/* Create and add the App schema */
	schema = sos_schema_from_template(&kokkos_app_template);
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

	/* Create and add the Kernel schema */
	schema = sos_schema_from_template(&kokkos_kernel_template);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR, "%s: Error %d creating Kokkos Kernel schema.\n",
		       kokkos_store.name, errno);
		return errno;
	}
	rc = sos_schema_add(sos, schema);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "%s: Error %d adding Kokkos Kernel schema.\n",
		       kokkos_store.name, rc);
		return rc;
	}
	*kernel = schema;

	/* Create and add the sha256 schema */
	schema = sos_schema_from_template(&kokkos_sha256_template);
	if (!schema) {
		ovis_log(mylog, OVIS_LERROR, "%s: Error %d creating SHA256 schema.\n",
		       kokkos_store.name, errno);
		return errno;
	}
	rc = sos_schema_add(sos, schema);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "%s: Error %d adding SHA256 schema.\n",
		       kokkos_store.name, rc);
		return rc;
	}
	*sha256 = schema;

	return 0;
}

static int reopen_container(char *path)
{
	int rc = 0;

	/* Close the container if it already exists */
	if (sos)
		sos_container_close(sos, SOS_COMMIT_ASYNC);

	/* Check if the container at path is already present */
	sos = sos_container_open(path, SOS_PERM_RW);
	if (!sos) {
	recreate:
		rc = create_container(path);
		if (rc)
			return rc;
		sos = sos_container_open(path, SOS_PERM_RW);
		if (!sos)
			return errno;
		rc = create_schema(sos, &app_schema, &kernel_schema, &sha256_schema);
		if (rc)
			return rc;
	} else {
		app_schema = sos_schema_by_name(sos, kokkos_app_template.name);
		if (!app_schema)
			/* The container exists, but is missing the Kokkos schema */
			goto recreate;
		kernel_schema = sos_schema_by_name(sos, kokkos_kernel_template.name);
		if (!app_schema)
			return EINVAL;
		sha256_schema = sos_schema_by_name(sos, kokkos_sha256_template.name);
		if (!sha256_schema)
			return EINVAL;
	}
	kernel_job_id_attr = sos_schema_attr_by_name(kernel_schema, "job_id");
	kernel_start_time_attr = sos_schema_attr_by_name(kernel_schema, "start_time");
	kernel_end_time_attr = sos_schema_attr_by_name(kernel_schema, "end_time");
	kernel_app_id_attr = sos_schema_attr_by_name(kernel_schema, "app_id");
	kernel_comp_id_attr = sos_schema_attr_by_name(kernel_schema, "component_id");
	kernel_job_tag_attr = sos_schema_attr_by_name(kernel_schema, "job_tag");
	kernel_mpi_rank_attr = sos_schema_attr_by_name(kernel_schema, "mpi_rank");
	sha256_string_attr = sos_schema_attr_by_name(sha256_schema, "string");
	sha256_digest_attr = sos_schema_attr_by_name(sha256_schema, "sha256");
	return rc;
}

struct metric_spec {
	const char *attr_name;
	const char *json_name;
	act_process_fn_t act_fn;
	int app_n_kernel;
};

struct schema_spec {
	const char *name;
	struct metric_spec metrics[];
};

static struct schema_spec kokkos_app_spec = {
	.name = "kokkos_app",
	{
		{ "", "kokkos-kernel-data", process_dict_entity, 1 },
		{ "job_id", "job-id", process_job_id, 1 },
		{ "job_name", "job-name", process_string, 1 },
		{ "app_id", "app-id", process_app_id, 1 },
		{ "mpi_rank", "mpi-rank", process_mpi_rank, 1 },
		{ "component_id", "component-id", process_component_id, 1 },
		{ "hostname", "hostname", process_string, 1 },
		{ "job_tag", "job-tag", process_job_tag, 1 },
		{ "start_time", "start-time", process_start_time, 1 },
		{ "end_time", "end-time", process_end_time, 1 },
		{ "user_id", "user-id", process_int, 1 },
		{ "total_app_time", "total-app-time", process_double, 1 },
		{ "total_kernel_times", "total-kernel-times", process_double, 1 },
		{ "total_non_kernel_times", "total-non-kernel-times", process_double, 1 },
		{ "percent_in_kernels", "percent-in-kernels", process_double, 1 },
		{ "unique_kernel_calls", "unique-kernel-calls", process_double, 1 },
		{ 0 }
	}
};

static struct schema_spec kokkos_sample_spec = {
	.name = "kokkos_sample",
	{
		{ "", "kernel-perf-info", ignore }, // force this to be processed last process_list_entity },
		{ "call_count", "call-count", process_double },
		{ "total_time", "total-time", process_double },
		{ "time_per_call", "time-per-call", process_double },
		{ "kernel_name", "kernel-name", process_digest },
		{ "region", "region", process_string },
		{ "kernel_type", "kernel-type", process_string },
		{ 0 }
	}
};

static int create_actions(sos_schema_t schema, struct schema_spec *spec)
{
	int i;
	sos_attr_t attr;
	struct metric_spec *metric;
	action_t act;

	for (i = 0, metric = &spec->metrics[i]; metric->attr_name;
	     metric = &spec->metrics[++i]) {
		if (metric->json_name[0] == '\0')
			/* skip attrs with no action */
			continue;

		act = malloc(sizeof *act);
		if (!act)
			return ENOMEM;

		act->act_fn = metric->act_fn;
		act->app_n_kernel = metric->app_n_kernel;
		hent_init(&act->hent, metric->json_name, strlen(metric->json_name));
		if (metric->attr_name && metric->attr_name[0] != '\0') {
			attr = sos_schema_attr_by_name(schema, metric->attr_name);
			if (!attr) {
				ovis_log(mylog, OVIS_LERROR,
				       "%s: The attribute '%s' is not present in '%s'.\n",
				       kokkos_store.name, metric->attr_name, sos_schema_name(schema));
				return ENOENT;
			}
		} else
			attr = NULL;
		act->attr = attr;
		act->schema = schema;
		htbl_ins(act_table, &act->hent);
	}
	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "config name=kokkos_store path=<path> port=<port_no> log=<path>\n"
		"     path      The path to the root of the SOS container store.\n"
		"     port      The port number to listen on for incoming connections (defaults to 18080).\n"
		"     log       The log file for sample updates (defaults to /var/log/kokkos.log).\n";
}

static int cmp_json_name(const void *a, const void *b, size_t len)
{
	return strncmp(a, b, len);
}

static int slurm_recv_cb(ldms_stream_event_t ev, void *ctxt);
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc;

	value = av_value(avl, "stream");
	if (value)
		stream = strdup(value);
	else
		stream = strdup("kokkos");
	ldms_stream_subscribe(stream, 0, slurm_recv_cb, handle, "kokkos_store");

	value = av_value(avl, "path");
	if (!value) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: the path to the container (path=) must be specified.\n",
		       kokkos_store.name);
		return ENOENT;
	}
	if (root_path)
		free(root_path);
	root_path = strdup(value);
	if (!root_path) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: Error allocating %d bytes for the container path.\n",
		       strlen(value) + 1);
		return ENOMEM;
	}

	rc = reopen_container(root_path);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR, "%s: Error opening %s.\n",
		       kokkos_store.name, root_path);
		return ENOENT;
	}

	if (act_table)
		goto out;

	act_table = htbl_alloc(cmp_json_name, 1123);
	if (!act_table) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: Error allocating the action table.\n",
		       kokkos_store.name);
		return ENOENT;
	}

	rc = create_actions(app_schema, &kokkos_app_spec);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: Error %d creating Kokkos App actions.\n",
		       kokkos_store.name, rc);
		goto err_1;
	}
	rc = create_actions(kernel_schema, &kokkos_sample_spec);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR,
		       "%s: Error %d creating Kokkos Kernel actions.\n",
		       kokkos_store.name, rc);
		goto err_1;
	}

 out:
	return 0;

 err_1:
	free(act_table);
	act_table = NULL;
	return rc;
}

static int slurm_recv_cb(ldms_stream_event_t ev, void *ctxt)
{
	json_entity_t list;
	int rc;
	json_entity_t entity;

	if (ev->type != LDMS_STREAM_EVENT_RECV)
		return 0;

	kokkos_context_t k = calloc(1, sizeof *k);

	SHA256_Init(&k->sha_ctxt);
	k->job_id = k->app_id = k->component_id = k->mpi_rank = 0;
	k->app_obj = sos_obj_new(app_schema);
	if (!k->app_obj) {
		rc = errno;
		goto out;
	}
	rc = process_dict_entity(k, ev->recv.json, NULL, NULL);
	if (!rc) {
		ovis_log(mylog, OVIS_LINFO,
		       "Creating Kokkos App record for %d:%d:%d\n",
		       k->job_id, k->component_id, k->mpi_rank);
		sos_obj_index(k->app_obj);
		sos_obj_put(k->app_obj);
	} else {
		if (k->app_obj) {
			sos_obj_delete(k->app_obj);
			sos_obj_put(k->app_obj);
		}
		if (k->kernel_obj) {
			sos_obj_delete(k->kernel_obj);
			sos_obj_put(k->kernel_obj);
		}
	}
	entity = json_value_find(ev->recv.json, "kokkos-kernel-data");
	list = json_value_find(entity, "kernel-perf-info");
	if (!list) {
		rc = ENOENT;
		ovis_log(mylog, OVIS_LERROR,
		       "The kernel-perf-info attribute is missing from the stream data\n");
		goto out;
	}
	rc = process_list_entity(k, list, k->kernel_obj, NULL);
	if (rc) {
		ovis_log(mylog, OVIS_LERROR,
		       "Error %d parsing the kernel-perf-info list attribute\n", rc);
	}
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
