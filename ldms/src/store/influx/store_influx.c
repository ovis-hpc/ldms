/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2018 Open Grid Computing, Inc. All rights reserved.
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
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
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
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <sys/syscall.h>
#include <assert.h>
#include <curl/curl.h>
#include "ldms.h"
#include "ldmsd.h"

static char host_port[64];	/* hostname:port_no for influxdb */
struct influx_store {
	char *host_port;
	char *schema;
	char *container;
	pthread_mutex_t lock;
	int job_mid;
	int comp_mid;
	char **metric_name;
	CURL *curl;
	LIST_ENTRY(influx_store) entry;
	size_t measurement_limit;
	char measurement[0];
};

#define MEASUREMENT_LIMIT_DEFAULT	4096
static size_t measurement_limit = MEASUREMENT_LIMIT_DEFAULT;
static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(influx_store_list, influx_store) store_list;

static int set_none_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	assert(0 == "Invalid LDMS metric type");
}
static int update_offset(size_t cnt, size_t *line_off, size_t line_len)
{
	if (cnt + *line_off < line_len) {
		*line_off += cnt;
		return 0;
	}
	return E2BIG;
}
static int set_u8_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%hhui", ldms_metric_get_u8(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s8_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%hhdi", ldms_metric_get_s8(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_u16_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%hui", ldms_metric_get_u16(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s16_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%hdi", ldms_metric_get_s16(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_str_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "\"%s\"", ldms_metric_array_get_str(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_u32_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%ui", ldms_metric_get_u32(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s32_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%di", ldms_metric_get_s32(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_u64_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%lui", ldms_metric_get_u64(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s64_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%ldi", ldms_metric_get_s64(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_float_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%f", ldms_metric_get_float(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_double_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%lf", ldms_metric_get_double(s, i));
	return update_offset(cnt, line_off, line_len);
}

typedef int (*influx_value_set_fn)(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i);
influx_value_set_fn influx_value_set[] = {
	[LDMS_V_NONE] = set_none_fn,
	[LDMS_V_CHAR] = set_s8_fn,
	[LDMS_V_U8] = set_u8_fn,
	[LDMS_V_S8] = set_s8_fn,
	[LDMS_V_U16] = set_u16_fn,
	[LDMS_V_S16] = set_s16_fn,
	[LDMS_V_U32] = set_u32_fn,
	[LDMS_V_S32] = set_s32_fn,
	[LDMS_V_U64] = set_u64_fn,
	[LDMS_V_S64] = set_s64_fn,
	[LDMS_V_F32] = set_float_fn,
	[LDMS_V_D64] = set_double_fn,
	[LDMS_V_CHAR_ARRAY] = set_str_fn
};

/**
 * \brief Configuration
 */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	pthread_mutex_lock(&cfg_lock);

	value = av_value(avl, "host_port");
	if (!value) {
		ovis_log(ldmsd_plug_log_get(handle),
			 OVIS_LERROR, "The 'host_port' keyword is required.\n");
		return EINVAL;
	}
	strncpy(host_port, value, sizeof(host_port));

	value = av_value(avl, "measurement_limit");
	if (value) {
		measurement_limit = strtol(value, NULL, 0);
		if (measurement_limit <= 0) {
			ovis_log(ldmsd_plug_log_get(handle),
				 OVIS_LERROR,
				 "'%s' is not a valid 'measurement_limit' value\n",
				 value);
			measurement_limit = MEASUREMENT_LIMIT_DEFAULT;
		}
	}

	pthread_mutex_unlock(&cfg_lock);
	return 0;
}

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  "    config name=influx host_port=<hostname>':'<port_no>\n";
}

static ldmsd_store_handle_t
open_store(ldmsd_plug_handle_t s, const char *container, const char *schema,
	   struct ldmsd_strgp_metric_list *metric_list)
{
	struct influx_store *is = NULL;

	is = malloc(sizeof(*is) + measurement_limit);
	if (!is)
		goto out;
	is->measurement_limit = measurement_limit;
	pthread_mutex_init(&is->lock, NULL);
	is->container = strdup(container);
	if (!is->container)
		goto err1;
	is->schema = strdup(schema);
	if (!is->schema)
		goto err2;
	is->host_port = strdup(host_port);
	if (!is->host_port)
		goto err3;
	is->job_mid = -1;
	is->comp_mid = -1;

	is->curl = curl_easy_init();
	if (!is->curl)
		goto err3;
	pthread_mutex_lock(&cfg_lock);
	LIST_INSERT_HEAD(&store_list, is, entry);
	pthread_mutex_unlock(&cfg_lock);
	return is;
 err3:
	free(is->schema);
 err2:
	free(is->container);
 err1:
	free(is);
 out:
	return NULL;
}

static char *fixup(char *name)
{
	char *s = name;
	while (*s != '\0') {
		switch (*s) {
		case '(':
		case ')':
		case '#':
		case ':':
			*s = '_';
			break;
		default:
			break;
		}
		s++;
	}
	return name;
}

static int init_store(struct influx_store *is, ldms_set_t set, int *mids, int count)
{
	int i;

	is->job_mid = ldms_metric_by_name(set, "job_id");
	is->comp_mid = ldms_metric_by_name(set, "component_id");

	is->metric_name = calloc(sizeof(char *), count);
	if (!is->metric_name)
		return ENOMEM;

	/* Refactor metric names containing special characters */
	for (i = 0; i < count; i++) {
		char *name = strdup(ldms_metric_name_get(set, i));
		if (!name)
			return ENOMEM;
		is->metric_name[i] = fixup(name);
	}
	return 0;
}

static size_t __element_byte_len_[] = {
	[LDMS_V_NONE] = 0,
	[LDMS_V_CHAR] = 1,
	[LDMS_V_U8] = 1,
	[LDMS_V_S8] = 1,
	[LDMS_V_U16] = 2,
	[LDMS_V_S16] = 2,
	[LDMS_V_U32] = 4,
	[LDMS_V_S32] = 4,
	[LDMS_V_U64] = 8,
	[LDMS_V_S64] = 8,
	[LDMS_V_F32] = 4,
	[LDMS_V_D64] = 8,
	[LDMS_V_CHAR_ARRAY] = 1,
	[LDMS_V_U8_ARRAY] = 1,
	[LDMS_V_S8_ARRAY] = 1,
	[LDMS_V_U16_ARRAY] = 2,
	[LDMS_V_S16_ARRAY] = 2,
	[LDMS_V_U32_ARRAY] = 4,
	[LDMS_V_S32_ARRAY] = 4,
	[LDMS_V_F32_ARRAY] = 4,
	[LDMS_V_U64_ARRAY] = 8,
	[LDMS_V_S64_ARRAY] = 8,
	[LDMS_V_D64_ARRAY] = 8,
};

static inline size_t __element_byte_len(enum ldms_value_type t)
{
	if (t < LDMS_V_FIRST || t > LDMS_V_LAST)
		assert(0 == "Invalid type specified");
	return __element_byte_len_[t];
}

static int
store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh, ldms_set_t set, int *metric_arry, size_t metric_count)
{
	struct influx_store *is = _sh;
	struct ldms_timestamp timestamp;
	int i;
	int rc = 0;
	size_t cnt, off;
	char *measurement;
	if (!is)
		return EINVAL;

	pthread_mutex_lock(&is->lock);
	if (is->job_mid < 0) {
		rc = init_store(is, set, metric_arry, metric_count);
		if (rc)
			goto err;
	}

	char url[128];
	snprintf(url, sizeof(url), "http://%s/write?db=%s", is->host_port, is->container);
	curl_easy_setopt(is->curl, CURLOPT_URL, url);

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/influx");

	measurement = is->measurement;
	cnt = snprintf(measurement, is->measurement_limit,
		       "%s,job_id=%lui,component_id=%lui ",
		       is->schema,
		       ldms_metric_get_u64(set, is->job_mid),
		       ldms_metric_get_u64(set, is->comp_mid));
	off = cnt;

	enum ldms_value_type metric_type;

	int comma = 0;
	for (i = 0; i < metric_count; i++) {
		if (metric_arry[i] == is->job_mid)
			continue;
		if (metric_arry[i] == is->comp_mid)
			continue;
		metric_type = ldms_metric_type_get(set, metric_arry[i]);
		if (metric_type > LDMS_V_CHAR_ARRAY) {
			ovis_log(ldmsd_plug_log_get(handle),
				 OVIS_LERROR,
				 "The metric %s:%s of type %s is not supported by "
				 "InfluxDB and is being ignored.\n",
				 is->schema,
				 ldms_metric_type_to_str(metric_type),
				 ldms_metric_name_get(set, metric_arry[i]));
			continue;
		}
		if (comma) {
			if (off > is->measurement_limit - 16)
				goto err;
			measurement[off++] = ',';
		} else
			comma = 1;
		cnt = snprintf(&measurement[off], is->measurement_limit - off,
			       "%s=", is->metric_name[i]);
		off += cnt;
		if (influx_value_set[metric_type](measurement, &off,
						  is->measurement_limit,
						  set, metric_arry[i]))
			goto err;
	}
	timestamp = ldms_transaction_timestamp_get(set);
	long long int ts =  ((long long)timestamp.sec * 1000000000L)
		+ ((long long)timestamp.usec * 1000L);
	cnt = snprintf(&measurement[off], is->measurement_limit - off, " %lld", ts);
	off += cnt;

	curl_easy_setopt(is->curl, CURLOPT_POSTFIELDS, measurement);
	curl_easy_setopt(is->curl, CURLOPT_POSTFIELDSIZE, off);
	curl_easy_setopt(is->curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_perform(is->curl);
	curl_slist_free_all(headers);
	pthread_mutex_unlock(&is->lock);
	return 0;
err:
	pthread_mutex_unlock(&is->lock);

	ovis_log(ldmsd_plug_log_get(handle),
		 OVIS_LERROR, "Overflow formatting InfluxDB measurement data.\n");
	return ENOMEM;
}

static void close_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	struct influx_store *is = _sh;

	if (!is)
		return;

	pthread_mutex_lock(&cfg_lock);
	LIST_REMOVE(is, entry);
	pthread_mutex_unlock(&cfg_lock);

	free(is->container);
	free(is->schema);
	free(is);
}

struct ldmsd_store ldmsd_plugin_interface = {
	.base = {
		.name = "influx",
		.config = config,
		.usage = usage,
		.type = LDMSD_PLUGIN_STORE,
	},
	.open = open_store,
	.store = store,
	.close = close_store,
};

static void __attribute__ ((constructor)) store_influx_init();
static void store_influx_init()
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	LIST_INIT(&store_list);
}
