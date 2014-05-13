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
#define MAX_LEN_HADOOP_MNAME 248
#define MAX_LEN_HADOOP_TYPE 8

struct record_metrics *parse_record(char *record){
	struct record_metrics *metrics = calloc(1, sizeof(*metrics));
	metrics->count = 0;
	char *context, *name;
	metrics->context = strtok(record, ".");
	metrics->name = strtok(NULL, ":");
	if (!metrics->name)
		return NULL;

	struct metric_name *metric_name;

	char *m_name;
	while (m_name = strtok(NULL, ",")) {
		metric_name = malloc(sizeof(*metric_name));
		metric_name->name = strdup(m_name);
		LIST_INSERT_HEAD(&metrics->list, metric_name, entry);
		metrics->count++;
	}
	return metrics;
}

struct record_list *parse_given_metrics(char *metrics, int *count)
{
	*count = 0;
	struct record_list *record_list = calloc(1, sizeof(*record_list));
	struct record_metrics *r_metrics;

	char *record = strtok(metrics, ";");
	do {
		r_metrics = parse_record(record);
		if (!r_metrics) {
			return NULL;
		}
		LIST_INSERT_HEAD(record_list, r_metrics, entry);
		*count += r_metrics->count;
	} while (record = strtok(NULL, ";"));

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

int create_hadoop_set(char *fname, struct hadoop_set *hdset, uint64_t udata)
{
	ldms_log_fn_t msglog = hdset->msglog;
	FILE *f = fopen(fname, "r");
	if (!f) {
		msglog("hadoop_namenode: Failed to open file '%s'.\n",
				fname);
		return errno;
	}

	char *s, *ptr;
	char buf[MAX_LEN_HADOOP_MNAME + MAX_LEN_HADOOP_TYPE];
	char metricname[MAX_LEN_HADOOP_MNAME], short_mname[MAX_LEN_HADOOP_MNAME];
	char type[MAX_LEN_HADOOP_TYPE];
	size_t dlen = strlen(hdset->daemon);

	int rc;
	size_t meta_sz, data_sz, tot_meta_sz, tot_data_sz;
	tot_meta_sz = tot_data_sz = meta_sz = data_sz = 0;
	int num_metrics = 0;

	size_t total_mname_len;

	fseek(f, 0, SEEK_SET);
	do {
		s = fgets(buf, sizeof(buf), f);
		if (!s)
			break;

		/* Ignore the commented line */
		if (buf[0] == '#')
			continue;

		ptr = strrchr(buf, '\t');
		if (!ptr) {
			msglog("hadoop_%s: Invalid format: %s\n",
						hdset->setname, buf);
			return EINVAL;
		}
		*ptr = '\0';
		total_mname_len = dlen + (ptr - buf);

		if (total_mname_len > MAX_LEN_HADOOP_MNAME) {
			msglog("hadoop_%s: %s: metric name exceeds %d\n",
					hdset->setname, buf, MAX_LEN_HADOOP_MNAME);
			return EPERM;
		}

		_substitute_char(buf, ':', '.');
		_substitute_char(buf, '\t', '_');
		snprintf(metricname, total_mname_len + 2, "%s#%s", buf, hdset->daemon);
		snprintf(type, strlen(ptr + 1), "%s", ptr + 1); /* Don't copy the newline character */

		rc = ldms_get_metric_size(metricname,
				ldms_str_to_type(type),
				&meta_sz, &data_sz);
		if (rc)
			return rc;

		tot_meta_sz += meta_sz;
		tot_data_sz += data_sz;
		num_metrics++;
	} while (s);


	/* Create the metric set */
	rc = ldms_create_set(hdset->setname, tot_meta_sz,
				tot_data_sz, &(hdset->set));
	if (rc)
		goto err;

	hdset->map = str_map_create(num_metrics);
	if (!hdset->map) {
		rc = ENOMEM;
		goto err_1;
	}

	ldms_metric_t m;
	fseek(f, 0, SEEK_SET);
	do {
		s = fgets(buf, sizeof(buf), f);
		if (!s)
			break;

		/* Ignore the commented line */
		if (buf[0] == '#')
			continue;

		ptr = strrchr(buf, '\t');
		if (!ptr) {
			msglog("hadoop_%s: Invalid format: %s\n",
						hdset->setname, buf);
			return EINVAL;
		}
		*ptr = '\0';
		total_mname_len = dlen + (ptr - buf);

		if (total_mname_len > MAX_LEN_HADOOP_MNAME) {
			msglog("hadoop_%s: %s: metric name exceeds %d\n",
					hdset->setname, buf, MAX_LEN_HADOOP_MNAME);
			return EPERM;
		}

		_substitute_char(buf, ':', '.');
		_substitute_char(buf, '\t', '_');
		snprintf(metricname, total_mname_len + 2, "%s#%s", buf, hdset->daemon);
		snprintf(type, strlen(ptr + 1), "%s", ptr + 1); /* Don't copy the newline character */

		m = ldms_add_metric(hdset->set, metricname,
					ldms_str_to_type(type));
		if (!m) {
			rc = ENOMEM;
			goto err_2;
		}

		ldms_set_user_data(m, udata);

		/*
		 * Re-use the metricname variable.
		 *
		 * Unlike the metric names in the metric set, the metric names
		 * in the str_map exclude the daemon name. For example,
		 * In metric set: 'namenode.jvm.JvmMetrics: MemNonHeapUsedM'
		 * In str_map: 'jvm.JvmMetrics: MemNonHeapUsedM'
		 */
		snprintf(metricname, ptr - buf + 1, "%s", buf);
		rc = str_map_insert(hdset->map, metricname,
				(uint64_t)(unsigned char *)m);
		if (rc)
			goto err_2;
	} while (s);

	fclose(f);
	return 0;

err_2:
	str_map_free(hdset->map);
err_1:
	ldms_set_release(hdset->set);
err:
	msglog("hadoop_%s: failed to create the set.\n", hdset->setname);
	fclose(f);
	return rc;
}

void destroy_hadoop_set(struct hadoop_set *hdset)
{
	close(hdset->sockfd);
	str_map_free(hdset->map);
	ldms_set_release(hdset->set);
	free(hdset->setname);
}

void _recv_metrics(char *data, struct hadoop_set *hdset)
{
	char *daemon_name, *rctxt_name, *metric_name;
	char buf[256];
	char *s, *tmp, *ptr;
	ldms_metric_t m;
	enum ldms_value_type type;
	union ldms_value value;
	ldms_log_fn_t msglog = hdset->msglog;

	tmp = strdup(data);
	rctxt_name = strtok_r(tmp, ":", &ptr);
	char *value_s;

	size_t base_len = strlen(rctxt_name);

	while (metric_name = strtok_r(NULL, "=", &ptr)) {
		snprintf(buf, base_len + strlen(metric_name) + 2,  "%s.%s",
						rctxt_name, metric_name);
		_substitute_char(buf, '\t', '_');
		m = str_map_get(hdset->map, buf);
		if (!m)
			continue;
		type = ldms_get_metric_type(m);
		value_s = strtok_r(NULL, ",", &ptr);
		switch (type) {
		case LDMS_V_S32:
			value.v_s32 = strtol(value_s, NULL, 10);
			break;
		case LDMS_V_S64:
			value.v_s64 = strtoll(value_s, NULL, 10);
			break;
		case LDMS_V_F:
			value.v_f = strtof(value_s, NULL);
			break;
		case LDMS_V_D:
			value.v_d = strtod(value_s, NULL);
			break;
		default:
			msglog("hadoop_namenode: Not support type '%s'.\n",
					ldms_type_to_str(type));
			continue;
		}
		ldms_set_metric(m, &value);
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
