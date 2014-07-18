/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
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
	char *context, *name, saveptr = NULL;
	metrics->context = strtok_r(record, ".", &saveptr);
	metrics->name = strtok_r(NULL, ":", &saveptr);
	if (!metrics->name)
		return NULL;

	struct metric_name *metric_name;

	char *m_name;
	while (m_name = strtok_r(NULL, ",", &saveptr)) {
		metric_name = malloc(sizeof(*metric_name));
		metric_name->name = strdup(m_name);
		LIST_INSERT_HEAD(&metrics->list, metric_name, entry);
		metrics->count++;
	}
	return metrics;
}

struct record_list *parse_given_metrics(char *metrics, int *count)
{
	char *saveptr;
	*count = 0;
	struct record_list *record_list = calloc(1, sizeof(*record_list));
	struct record_metrics *r_metrics;

	char *record = strtok_r(metrics, ";", &saveptr);
	do {
		r_metrics = parse_record(record);
		if (!r_metrics) {
			return NULL;
		}
		LIST_INSERT_HEAD(record_list, r_metrics, entry);
		*count += r_metrics->count;
	} while (record = strtok_r(NULL, ";", &saveptr));

	return record_list;
}

int create_hadoop_set(char *given_metrics, char *fname,
			struct hadoop_set *hdset, uint64_t udata)
{
	ldms_log_fn_t msglog = hdset->msglog;
	FILE *f = fopen(fname, "r");
	if (!f) {
		msglog("hadoop_namenode: Failed to open file '%s'.\n",
				fname);
		return errno;
	}

	char *s;
	char buf[256];
	char metricname[128];
	char type[128];

	int rc;
	size_t meta_sz, data_sz, tot_meta_sz, tot_data_sz;
	tot_meta_sz = tot_data_sz = meta_sz = data_sz = 0;
	int num_metrics = 0;

	fseek(f, 0, SEEK_SET);
	do {
		s = fgets(buf, sizeof(buf), f);
		if (!s)
			break;

		rc = sscanf(buf, "%s\t%s", metricname, type);
		if (rc < 2) {
			break;
		}

		/* Ignore the commented line */
		if (metricname[0] == '#')
			continue;

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
		rc = sscanf(buf, "%s\t%s", metricname, type);
		if (rc < 2)
			break;

		/* Ignore the commented line */
		if (metricname[0] == '#')
			continue;

		m = ldms_add_metric(hdset->set, metricname,
					ldms_str_to_type(type));
		if (!m) {
			rc = ENOMEM;
			goto err_2;
		}

		ldms_set_user_data(m, udata);
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
	msglog("hadoop_namenode: failed to create the set.\n");
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
	char *saveptr = NULL;
	char buf[256];
	char *s, *tmp;
	ldms_metric_t m;
	enum ldms_value_type type;
	union ldms_value value;
	ldms_log_fn_t msglog = hdset->msglog;

	tmp = strdup(data);
	rctxt_name = strtok_r(tmp, ":", &saveptr);
	char *value_s;

	while (metric_name = strtok_r(NULL, "=", &saveptr)) {
		sprintf(buf, "%s:%s", rctxt_name, metric_name);
		m = str_map_get(hdset->map, buf);
		if (!m)
			continue;
		type = ldms_get_metric_type(m);
		value_s = strtok_r(NULL, ",", &saveptr);
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
			hdset->msglog("%s: failed to recieve hadoop "
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
