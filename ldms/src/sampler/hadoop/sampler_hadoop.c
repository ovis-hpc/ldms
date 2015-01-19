/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Cray Inc. All rights reserved.
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
 * \file sampler_hadoop.c
 * \brief Receive Hadoop Metrics from the LDMS Hadoop sink
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
#include <sys/unistd.h>
#include <limits.h>
#include <netdb.h>
#include <sys/queue.h>

#include <coll/str_map.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_hadoop.h"

#define MAX_BUF_LEN 1024

struct record_metrics *parse_record(char *record){
	struct record_metrics *metrics = calloc(1, sizeof(*metrics));
	metrics->count = 0;
	char *context, *name, *ptr;
	metrics->context = strtok_r(record, ".", &ptr);
	metrics->name = strtok_r(NULL, ":", &ptr);
	if (!metrics->name)
		return NULL;

	struct hadoop_metric *metric_name;

	char *m_name = strtok_r(NULL, ",", &ptr);
	while (m_name) {
		metric_name = malloc(sizeof(*metric_name));
		snprintf(metric_name->name, MAX_LEN_HADOOP_MNAME, "%s", m_name);
		TAILQ_INSERT_HEAD(&metrics->queue, metric_name, entry);
		metrics->count++;
		m_name = strtok_r(NULL, ",", &ptr);
	}
	return metrics;
}

struct record_list *parse_given_metrics(char *metrics, int *count)
{
	*count = 0;
	struct record_list *record_list = calloc(1, sizeof(*record_list));
	struct record_metrics *r_metrics;
	char *ptr;
	char *record = strtok_r(metrics, ";", &ptr);
	do {
		r_metrics = parse_record(record);
		if (!r_metrics) {
			return NULL;
		}
		LIST_INSERT_HEAD(record_list, r_metrics, entry);
		*count += r_metrics->count;
		record = strtok_r(NULL, ";", &ptr);
	} while (record);

	return record_list;
}

void _substitute_char(char *s, char old_c, char new_c)
{
	char *ptr = strchr(s, old_c);
	while (ptr) {
		*ptr = new_c;
		ptr = strchr(ptr + 1, old_c);
	}
}

static struct hadoop_metric *create_hadoop_metric(struct hadoop_set *hdset,
							char *buf)
{
	char *ptr;
	size_t total_mname_len;
	int rc;
	size_t dlen = strlen(hdset->daemon);

	ptr = strrchr(buf, '\t');
	if (!ptr) {
		hdset->msglog("hadoop_%s: Invalid format: %s\n",
					hdset->setname, buf);
		errno = EINVAL;
		return NULL;
	}
	*ptr = '\0';
	total_mname_len = dlen + (ptr - buf);

	if (total_mname_len > MAX_LEN_HADOOP_MNAME) {
		hdset->msglog("hadoop_%s: %s: metric name exceeds %d\n",
				hdset->setname, buf, MAX_LEN_HADOOP_MNAME);
		errno = EPERM;
		return NULL;
	}

	_substitute_char(buf, ':', '.');
	_substitute_char(buf, '\t', '_');

	struct hadoop_metric *hmetric = calloc(1, sizeof(*hmetric));
	if (!hmetric) {
		hdset->msglog("hadoop_%s: Failed to create metric\n",
				hdset->setname);
		errno = ENOMEM;
		return NULL;
	}

	snprintf(hmetric->name, total_mname_len + 2, "%s#%s", buf, hdset->daemon);
	char type[MAX_LEN_HADOOP_TYPE];
	snprintf(type, strlen(ptr + 1), "%s", ptr + 1); /* Don't copy the newline character */
	hmetric->type = ldms_str_to_type(type);
	return hmetric;
}


int create_hadoop_set(char *fname, struct hadoop_set *hdset, uint64_t producer_id)
{
	int rc = 0;
	ldms_log_fn_t msglog = hdset->msglog;
	hdset->schema = ldms_create_schema(hdset->daemon);
	if (!hdset->schema) {
		msglog("%s: Failed to create schema\n", hdset->daemon);
		return ENOMEM;
	}

	FILE *f = fopen(fname, "r");
	if (!f) {
		msglog("hadoop_namenode: Failed to open file '%s'.\n",
				fname);
		rc = errno;
		goto err;
	}

	char *s, *ptr;
	char buf[MAX_LEN_HADOOP_MNAME + MAX_LEN_HADOOP_TYPE];
	char metricname[MAX_LEN_HADOOP_MNAME], short_mname[MAX_LEN_HADOOP_MNAME];
	char type[MAX_LEN_HADOOP_TYPE];
	int num_metrics = 0;

	struct hadoop_metric *hmetric;
	struct hadoop_metric_queue metric_queue;
	TAILQ_INIT(&metric_queue);
	fseek(f, 0, SEEK_SET);
	do {
		s = fgets(buf, sizeof(buf), f);
		if (!s)
			break;

		/* Ignore the commented line */
		if (buf[0] == '#')
			continue;

		hmetric = create_hadoop_metric(hdset, buf);
		if (!hmetric) {
			rc = errno;
			goto err_0;
		}
		TAILQ_INSERT_TAIL(&metric_queue, hmetric, entry);

		rc = ldms_add_metric(hdset->schema, hmetric->name,
							hmetric->type);
		if (rc < 0)
			goto err_0;
		hmetric->metric_idx = rc;
		num_metrics++;
	} while (s);

	hdset->map = str_map_create(num_metrics);
	if (!hdset->map) {
		rc = ENOMEM;
		goto err_0;
	}

	/* Create the metric set */
	rc = ldms_create_set(hdset->setname, hdset->schema, &hdset->set);
	if (rc)
		goto err_1;

	hmetric = TAILQ_FIRST(&metric_queue);
	while (hmetric) {
		rc = str_map_insert(hdset->map, hmetric->name,
					(uint64_t) (void *)hmetric);
		if (rc)
			goto err_2;
		TAILQ_REMOVE(&metric_queue, hmetric, entry);
		hmetric = TAILQ_FIRST(&metric_queue);
	}
	fclose(f);
	ldms_set_producer_id(hdset->set, producer_id);
	return 0;

err_2:
	ldms_destroy_set(hdset->set);
err_1:
	str_map_free(hdset->map);
err_0:
	hmetric = TAILQ_FIRST(&metric_queue);
	while (hmetric) {
		TAILQ_REMOVE(&metric_queue, hmetric, entry);
		free(hmetric);
		hmetric = TAILQ_FIRST(&metric_queue);
	}
	fclose(f);
err:
	ldms_destroy_schema(hdset->schema);
	hdset->schema = NULL;
	msglog("hadoop_%s: failed to create the set.\n", hdset->setname);
	return rc;
}

void destroy_hadoop_set(struct hadoop_set *hdset)
{
	if (hdset->sockfd)
		close(hdset->sockfd);
	if (hdset->map)
		str_map_free(hdset->map);
	if (hdset->schema)
		ldms_destroy_schema(hdset->schema);
	if (hdset->set)
		ldms_destroy_set(hdset->set);
	if (hdset->setname)
		free(hdset->setname);
}

void _recv_metrics(char *data, struct hadoop_set *hdset)
{
	char *daemon_name, *rctxt_name, *metric_name;
	char buf[256];
	char *s, *tmp, *ptr;
	enum ldms_value_type type;
	union ldms_value value;
	ldms_log_fn_t msglog = hdset->msglog;

	tmp = strdup(data);
	rctxt_name = strtok_r(tmp, ":", &ptr);
	char *value_s;

	size_t base_len = strlen(rctxt_name);
	struct hadoop_metric *hmetric;
	metric_name = strtok_r(NULL, "=", &ptr);
	while (metric_name) {
		snprintf(buf, base_len + strlen(metric_name) + 2,  "%s.%s",
						rctxt_name, metric_name);
		_substitute_char(buf, '\t', '_');
		hmetric = (struct hadoop_metric *)(void *)str_map_get(hdset->map, buf);
		if (!hmetric)
			continue;
		type = hmetric->type;
		value_s = strtok_r(NULL, ",", &ptr);
		switch (type) {
		case LDMS_V_S32:
			value.v_s32 = strtol(value_s, NULL, 10);
			break;
		case LDMS_V_S64:
			value.v_s64 = strtoll(value_s, NULL, 10);
			break;
		case LDMS_V_F32:
			value.v_f = strtof(value_s, NULL);
			break;
		case LDMS_V_D64:
			value.v_d = strtod(value_s, NULL);
			break;
		default:
			msglog("hadoop_namenode: Not support type '%s'.\n",
					ldms_type_to_str(type));
			continue;
		}
		ldms_set_midx(hdset->set, hmetric->metric_idx, &value);
	}
}

void *recv_metrics(void *_hdset)
{
	struct hadoop_set *hdset = (struct hadoop_set *)_hdset;

	char buffer[MAX_BUF_LEN];
	char *daemon_name;
	char *context_name;
	char *record_name;
	char *s, *tmp;

	int rc;
	while (1) {
		rc = recvfrom(hdset->sockfd, buffer, (size_t)MAX_BUF_LEN,
							0, NULL, NULL);
		if (rc < 0) {
			hdset->msglog("%s: failed to receive hadoop "
						"metrics. Error %d\n",
						hdset->setname, rc);
			return NULL;
		}


		ldms_begin_transaction(hdset->set);
		_recv_metrics(buffer, hdset);
		ldms_end_transaction(hdset->set);
	}
	return NULL;
}

int setup_datagram(int port, int *sockfd)
{
	struct sockaddr_in sin;
	int rc;

	*sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (*sockfd < 0)
		return errno;

	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = 0;
	rc = bind(*sockfd, (struct sockaddr *)&sin, sizeof(sin));
	if (rc < 0)
		return rc;

	return 0;
}
