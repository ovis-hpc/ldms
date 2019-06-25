/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2018 Sandia Corporation. All rights reserved.
 *
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

#include "kokkos.h"
#include "ldms.h"
#include "ldmsd.h"
#include "json.h"

#include "kvd_svc.h"
ldmsd_msg_log_f msglog;
kvd_svc_t kvc = { 0 };

pthread_t sample_thread;
pthread_t kvd_thread;
struct kvd_svc_s svc;
struct kvd_svc_s cli;

htbl_t act_table;
static struct ldmsd_sampler sampler;
static ldms_schema_t app_schema;
static ldms_set_t app_set;
static ldms_schema_t sample_schema;
static ldms_set_t sample_set;
static uint64_t component_id;

static char path_buff[PATH_MAX];
static char *log_path = "/var/log/ldms/kokkos_sampler.log";
static char *port = "18080";
static char *address= "0.0.0.0";
static char *verbosity = "WARN";

typedef struct kokkos_parser_s {
	struct json_parser_s base;
	uint64_t job_id;
	uint64_t mpi_rank;
} *kokkos_parser_t;

#define SAMPLE_JOB_ID	0
#define SAMPLE_MPI_RANK	1

void print_entity(json_entity_t e);

void print_array(json_entity_t e)
{
	json_item_t i;
	int count = 0;
	printf("[");
	TAILQ_FOREACH(i, &e->value.array_->item_list, item_entry) {
		if (count)
			printf(",");
		print_entity(i->item);
		count++;
	}
	printf("]");
}

void print_dict(json_entity_t e)
{
	json_attr_t i;
	int count = 0;
	printf("{");
	TAILQ_FOREACH(i, &e->value.dict_->attr_list, attr_entry) {
		if (count)
			printf(",");
		printf("\"%s\" : ", i->name);
		print_entity(i->value);
		count++;
	}
	printf("}");
}

void print_entity(json_entity_t e)
{
	switch (e->type) {
	case JSON_INT_VALUE:
		printf("%d", e->value.int_);
		break;
	case JSON_BOOL_VALUE:
		if (e->value.bool_)
			printf("true");
		else
			printf("false");
		break;
	case JSON_FLOAT_VALUE:
		printf("%.2f", e->value.double_);
		break;
	case JSON_STRING_VALUE:
		printf("\"%s\"", e->value.str_);
		break;
	case JSON_ARRAY_VALUE:
		print_array(e);
		break;
	case JSON_DICT_VALUE:
		print_dict(e);
		break;
	case JSON_NULL_VALUE:
		printf("null");
		break;
	default:
		assert(0 == "invalid entity type");
	}
}

struct action;
typedef struct action *action_t;
typedef int (*act_process_fn_t)(json_parser_t, json_entity_t, action_t);
struct action {
	act_process_fn_t act_fn;
	ldms_set_t set;
	int metric_id;
	struct hent hent;
};

action_t get_act(const char *key, size_t key_len)
{
	hent_t hent = htbl_find(act_table, key, key_len);
	if (hent)
		return container_of(hent, struct action, hent);
	return NULL;
}

int process_string(json_parser_t p, json_entity_t e, action_t act)
{
	ldms_metric_array_set_str(act->set, act->metric_id, e->value.str_->str);
	return 0;
}

int process_digest(json_parser_t p, json_entity_t e, action_t act)
{
	int i;
	char cbuf[3];
	size_t len = sizeof(e->value.str_->str_digest) + e->value.str_->str_len;
	static char *fmt = "/add?key=";
	len += strlen(fmt) + /* encoded digest */(32 * 2) + 16;
	char *uri = malloc(len);
	uri[0] = '\0';
	strcat(uri, fmt);

	for (i = 0; i < sizeof(e->value.str_->str_digest); i++) {
		ldms_metric_array_set_u8(act->set,
					 act->metric_id, i,
					 e->value.str_->str_digest[i]);
		sprintf(cbuf, "%02hhx", e->value.str_->str_digest[i]);
		strcat(uri, cbuf);
	}
	if (e->value.str_->str_len)
		kvd_cli_post(&cli, uri, e->value.str_->str, e->value.str_->str_len+1);
	else
		kvd_cli_post(&cli, uri, "", 1);
	free(uri);
	return 0;
}

int process_double(json_parser_t p, json_entity_t e, action_t act)
{
	ldms_metric_set_double(act->set, act->metric_id, e->value.double_);
	return 0;
}

int process_int(json_parser_t p, json_entity_t e, action_t act)
{
	ldms_metric_set_u64(act->set, act->metric_id, e->value.int_);
	return 0;
}

int process_job_id(json_parser_t p, json_entity_t e, action_t act)
{
	kokkos_parser_t k = (kokkos_parser_t)p;
	k->job_id = e->value.int_;
	ldms_metric_set_u64(act->set, act->metric_id, e->value.int_);
	return 0;
}

int process_mpi_rank(json_parser_t p, json_entity_t e, action_t act)
{
	kokkos_parser_t k = (kokkos_parser_t)p;
	k->mpi_rank = e->value.int_;
	ldms_metric_set_u64(act->set, act->metric_id, e->value.int_);
	return 0;
}

int process_sample_entity(json_parser_t p, json_entity_t e, action_t action)
{
	kokkos_parser_t k = (kokkos_parser_t)p;
	action_t act;
	json_attr_t a;
	if (e->type != JSON_DICT_VALUE) {
		msglog(LDMSD_LERROR, "%s: The sample entity "
			  "must a JSon object, i.e. {}. Got a %d.\n",
			  sampler.base.name, e->type);
		return;
	}
	ldms_transaction_begin(sample_set);
	TAILQ_FOREACH(a, &e->value.dict_->attr_list, attr_entry) {
		act = get_act(a->name->str, a->name->str_len);
		if (!act) {
			msglog(LDMSD_LERROR, "%s: '%s' is not a recognized attribute name.\n",
			       sampler.base.name, a->name->str);
			continue;
		}
		act->act_fn(p, a->value, act);
	}
	ldms_metric_set_u64(sample_set, SAMPLE_JOB_ID, k->job_id);
	ldms_metric_set_u64(sample_set, SAMPLE_MPI_RANK, k->mpi_rank);
	ldms_transaction_end(sample_set);
	return 0;
}

int process_array_entity(json_parser_t p, json_entity_t e, action_t action)
{
	json_item_t i;
	if (e->type != JSON_ARRAY_VALUE) {
		msglog(LDMSD_LERROR, "%s: The kernel-perf-info entity "
			  "must be a JSon array, i.e. []. Got a %d.\n",
			  sampler.base.name, e->type);
		return 1;
	}
	ldms_transaction_begin(action->set);
	TAILQ_FOREACH(i, &e->value.array_->item_list, item_entry) {
		process_sample_entity(p, i->item, NULL);
	}
	ldms_transaction_end(action->set);
	return 0;
}

void process_app_entity(json_parser_t p, json_entity_t e, action_t action)
{
	action_t act;
	json_attr_t a;
	if (e->type != JSON_DICT_VALUE) {
		msglog(LDMSD_LERROR, "%s: The top level entity "
			  "must a JSon object, i.e. {}. Got a %d.\n",
			  sampler.base.name, e->type);
		return;
	}
	ldms_transaction_begin(app_set);
	TAILQ_FOREACH(a, &e->value.dict_->attr_list, attr_entry) {
		act = get_act(a->name->str, a->name->str_len);
		if (!act) {
			msglog(LDMSD_LERROR, "%s: '%s' is not a recognized attribute name.\n",
			       sampler.base.name, a->name->str);
			continue;
		}
		act->act_fn(p, a->value, act);
	}
	ldms_transaction_end(app_set);
}

static void err_enomem(struct kvd_req_ctxt *ctxt, size_t requested)
{
	evbuffer_add_printf(ctxt->evbuffer,
			    "{"
			    "\"status\" : 12,"
			    "\"msg\" : \"Out of memory! Could not allocate %d bytes.\""
			    "}\n", requested);
}

void json_result(struct kvd_req_ctxt *ctxt, int result)
{
	evbuffer_add_printf(ctxt->evbuffer,
			    "{ \"status\" : %d, \"msg\" : \"%s\"  }\n",
			    result, strerror(result));
}

static void kokkos_handle_sample(struct kvd_req_ctxt *ctxt)
{
	json_entity_t e;
	struct evbuffer *in = evhttp_request_get_input_buffer(ctxt->req);
	size_t len = evbuffer_get_length(in);
	const char *key;
	char *data, *decoded;
	int rc;
	json_entity_t entity;

	/* lex needs two end-of-buffer characters ('\0') */
	data = malloc(len+2);
	if (!data) {
		err_enomem(ctxt, len + 1);
		return;
	}

	evbuffer_copyout(in, data, len);
	decoded = evhttp_uridecode(data, 0, &len);
	decoded[len] = '\0';
	decoded[len+1] = '\0';

	json_parser_t parser = json_parser_new(sizeof(struct kokkos_parser_s));
	rc = json_parse_buffer(parser, decoded, len+2, &entity);
	if (rc == 0) {
		process_app_entity(parser, entity, NULL);
		json_entity_free(entity);
		json_result(ctxt, 0);
	} else {
		json_result(ctxt, EINVAL);
	}
	free(parser);
	free(decoded);
	free(data);
}

struct metric_spec {
	const char *metric_name;
	const char *json_name;
	act_process_fn_t act_fn;
	int is_meta;
	enum ldms_value_type type;
	uint32_t count;
};

struct schema_spec {
	const char *name;
	struct metric_spec metrics[10];
};

static struct schema_spec kokkos_app_spec[] = {
	"kokkos_app",
	{
		{ "application",
		  "application",
		  process_string,
		  0, LDMS_V_CHAR_ARRAY, 32 },
		{ "job_id",
		  "job-id",
		  process_job_id,
		  0, LDMS_V_U64 },
		{ "component_id",
		  "mpi-rank",
		  process_mpi_rank,
		  0, LDMS_V_U64 },
		{ "total_app_time",
		  "total-app-time",
		  process_double,
		  0, LDMS_V_D64 },
		{ "total_kernel_times",
		  "total-kernel-times",
		  process_double,
		  0, LDMS_V_D64 },
		{ "total_non_kernel_times",
		  "total-non-kernel-times",
		  process_double,
		  0, LDMS_V_D64 },
		{ "percent_in_kernels",
		  "percent-in-kernels",
		  process_double,
		  0, LDMS_V_D64 },
		{ "unique_kernel_calls",
		  "unique-kernel-calls",
		  process_int,
		  0, LDMS_V_U64 },
		{}
	}
};

static struct schema_spec kokkos_sample_spec[] = {
	"kokkos_sample",
	{
		{ "", "kernel-perf-info", process_array_entity, -1, -1 },
		{ "job_id", "job-id",
		  process_int,
		  0, LDMS_V_U64 },
		{ "component_id",
		  "mpi-rank",
		  process_mpi_rank,
		  0, LDMS_V_U64 },
		{ "call_count", "call-count",
		  process_int,
		  0, LDMS_V_U64 },
		{ "total_time", "total-time",
		  process_double,
		  0, LDMS_V_D64 },
		{ "time_per_call", "time-per-call",
		  process_double,
		  0, LDMS_V_D64 },
		{ "kernel_name", "kernel-name",
		  process_digest,
		  0, LDMS_V_U8_ARRAY, 32 },
		{ "region", "region",
		  process_digest,
		  0, LDMS_V_U8_ARRAY, 32 },
		{ "kernel_type", "kernel-type",
		  process_string,
		  0, LDMS_V_CHAR_ARRAY, 32 },
		{}
	}
};

static ldms_schema_t create_schema(struct schema_spec *spec)
{
	ldms_schema_t schema;
	int rc, i;
	struct metric_spec *metric;

	rc = ENOMEM;
	schema = ldms_schema_new(spec->name);
	if (!schema)
		goto err;

	for (i = 0, metric = &spec->metrics[i]; metric->metric_name;
	     metric = &spec->metrics[++i]) {
		if (metric->metric_name[0] == '\0')
			/* skip json attributes with no associated LDMS metric */
			continue;
		if (metric->is_meta) {
			if (metric->type >= LDMS_V_CHAR_ARRAY) {
				rc = ldms_schema_meta_array_add(schema,
								metric->metric_name,
								metric->type,
								metric->count);
			} else {
				rc = ldms_schema_meta_add(schema,
							  metric->metric_name,
							  metric->type);
			}
		} else {
			if (metric->type >= LDMS_V_CHAR_ARRAY) {
				rc = ldms_schema_metric_array_add(schema,
								  metric->metric_name,
								  metric->type,
								  metric->count);
			} else {
				rc = ldms_schema_metric_add(schema,
							    metric->metric_name,
							    metric->type);
			}
		}
		if (rc < 0)
			goto err;
	}
	return schema;
 err:
	if (schema)
		ldms_schema_delete(schema);
	errno = rc;
	return NULL;
}

static int create_actions(ldms_set_t set, struct schema_spec *spec)
{
	int rc, i;
	struct metric_spec *metric;
	action_t act;

	for (i = 0, metric = &spec->metrics[i]; metric->metric_name;
	     metric = &spec->metrics[++i]) {
		if (metric->json_name[0] == '\0')
			/* skip attrs with no action */
			continue;
		act = malloc(sizeof *act);
		if (!act)
			return ENOMEM;
		act->act_fn = metric->act_fn;
		act->metric_id = ldms_metric_by_name(set, metric->metric_name);
		act->set = set;
		hent_init(&act->hent, metric->json_name, strlen(metric->json_name));
		htbl_ins(act_table, &act->hent);
	}
	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=kokkos [listen_ip=<ip_addr>] port=<port_no> [kvd_ip=<ip_addr>] kvd_port=<port_no> [...]\n"
		"     listen           The IP address to listen on for incoming connections (defaults to 0.0.0.0).\n"
		"     port             The port number to listen on for incoming connections (defaults to 18080).\n"
		"     log	       The log file for sample updates (defaults to /var/log/kokkos.log).\n"
		"     kvd_ip           The IP address of the Key-Value Daemon (defaults to 'localhost').\n"
		"     kvd_port         The port number of the Key-Value Daemon (defaults to 10080).\n"
		"     kvd_log	       The log file for kvd updates (defaults to /var/log/kokkos.log).\n"
		"  The following may be overwritten by the contents of the JSon input data.\n"
		"     producer         The default producer id value.\n"
		"     app_inst         The default application set instance name.\n"
		"     sample_inst      The default sample set instance name.\n"
		"     component_id     The default component id, (0 if not specified).\n";
}

int cmp_json_name(const void *a, const void *b, size_t len)
{
	return strncmp(a, b, len);
}

void *sample_proc(void* psvc)
{
	kvd_svc_init(psvc, 1);
	return NULL;
}
void *kvd_proc(void* pcli)
{
	kvd_cli_init(pcli, 1);
	return NULL;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *producer_name;
	int rc;

	svc.log_path = "/var/log/kokkos.log";
	svc.port = "18080";
	svc.address = "0.0.0.0";
	svc.verbosity = "ERROR";

	cli.log_path = "/var/log/kokkos.log";
	cli.port = "10080";
	cli.address = "0.0.0.0";
	cli.verbosity = "ERROR";

	value = av_value(avl, "port");
	if (value)
		svc.port = strdup(value);

	value = av_value(avl, "kvd_port");
	if (value)
		cli.port = strdup(value);

	value = av_value(avl, "listen");
	if (value)
		svc.address = strdup(value);

	value = av_value(avl, "kvd_ip");
	if (value)
		cli.address = strdup(value);

	value = av_value(avl, "log");
	if (value)
		svc.log_path = strdup(value);

	value = av_value(avl, "log_level");
	if (value)
		svc.verbosity = strdup(value);

	value = av_value(avl, "kvd_log");
	if (value)
		cli.log_path = strdup(value);

	value = av_value(avl, "kvd_log_level");
	if (value)
		cli.verbosity = strdup(value);

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, "%s: the producer name must be specified.\n",
		       sampler.base.name);
		return ENOENT;
	}

	value = av_value(avl, "component_id");
	if (value)
		component_id = (uint64_t)(atoi(value));
	else
		component_id = 0;

	value = av_value(avl, "app_inst");
	if (!value) {
		msglog(LDMSD_LERROR,
		       "%s: the application instance name (app_inst=) must be specified.\n",
		       sampler.base.name);
		return ENOENT;
	}

	app_schema = create_schema(kokkos_app_spec);
	if (!app_schema) {
		msglog(LDMSD_LERROR, "%s: failed to create Kokkos Application schema, error %d.\n",
		       sampler.base.name, errno);
		return EEXIST;
	}
	app_set = ldms_set_new(value, app_schema);
	if (!app_set) {
		msglog(LDMSD_LERROR, "%s: error %d creating Kokkos Application set.\n", sampler.base.name,
		       errno);
		rc = EINVAL;
		goto err_0;
	}
	ldms_set_producer_name_set(app_set, producer_name);

	act_table = htbl_alloc(cmp_json_name, 1123);

	if (create_actions(app_set, kokkos_app_spec))
		goto err_1;

	value = av_value(avl, "sample_inst");
	if (!value) {
		msglog(LDMSD_LERROR,
		       "%s: the application instance name (sample_inst=) must be specified.\n",
		       sampler.base.name);
		rc = EINVAL;
		goto err_1;
	}
	sample_schema = create_schema(kokkos_sample_spec);
	if (!sample_schema) {
		msglog(LDMSD_LERROR, "%s: failed to create Kokkos Sample schema, error %d.\n",
		       sampler.base.name, errno);
		rc = EINVAL;
		goto err_1;
	}
	sample_set = ldms_set_new(value, sample_schema);
	if (!sample_set) {
		msglog(LDMSD_LERROR, "%s: error %d creating Kokkos Sample set.\n", sampler.base.name,
		       errno);
		rc = EEXIST;
		goto err_2;
	}
	if (create_actions(sample_set, kokkos_sample_spec))
		goto err_2;

	kvd_set_svc_handler(&svc, "/sample", kokkos_handle_sample);
	pthread_create(&sample_thread, NULL, sample_proc, &svc);
	kvd_cli_init(&cli, 1);

	return 0;
 err_3:
	ldms_set_delete(sample_set);
	sample_set = NULL;
 err_2:
	ldms_schema_delete(sample_schema);
	sample_schema = NULL;
 err_1:
	ldms_set_delete(app_set);
	app_set = NULL;
 err_0:
	ldms_schema_delete(app_schema);
	app_schema = NULL;
	return rc;
}

static int sample(struct ldmsd_sampler *self)
{
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	if (sample_set) {
		ldms_set_delete(sample_set);
		sample_set = NULL;
	}
	if (sample_schema) {
		ldms_schema_delete(sample_schema);
		sample_schema = NULL;
	}
	if (app_set) {
		ldms_set_delete(app_set);
		app_set = NULL;
	}
	if (app_schema) {
		ldms_schema_delete(app_schema);
		app_schema = NULL;
	}
}

static struct ldmsd_sampler sampler = {
	.base = {
		.name = "kokkos",
		.term = term,
		.config = config,
		.usage = usage,
	},
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &sampler.base;
}
