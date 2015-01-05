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

#ifndef SAMPLER_HADOOP_H_
#define SAMPLER_HADOOP_H_

#include "ldms.h"
#include "ldmsd.h"
#include <coll/str_map.h>

#define MAX_LEN_HADOOP_MNAME 248
#define MAX_LEN_HADOOP_TYPE 8

struct hadoop_metric {
	char name[MAX_LEN_HADOOP_MNAME];
	enum ldms_value_type type;
	uint64_t metric_idx;
	TAILQ_ENTRY(hadoop_metric) entry;
};
TAILQ_HEAD(hadoop_metric_queue, hadoop_metric);

struct hadoop_set {
	ldms_schema_t schema;
	ldms_set_t set;
	/*
	 * Hadoop Daemon: namenode, secondarynamenode, datanode
	 * 	resourcemanager, nodemanager, jobtracker, maptask, reducetask,
	 * 	tasktrack
	 */
	const char *daemon;
	char *setname;
	ldms_log_fn_t msglog;
	int sockfd;
	str_map_t map;
};


struct record_metrics {
	char *context; /* record context */
	char *name; /* record name */
	int count; /* number of metrics to be collected in this record */
	struct hadoop_metric_queue queue; /* metric queue */
	LIST_ENTRY(record_metrics) entry;
};

LIST_HEAD(record_list, record_metrics);

/**
 * \brief Parse the given 'metrics' attribute
 * \param[in] _metrics The given string
 * \return the array of metrics
 */
struct record_list *parse_given_metrics(char *metrics, int *count);

/**
 * \brief Create a Hadoop Metric set
 * \param[in]	fname	    	The name of the file containing the metric names
 * \param[in/out]   hdset	the hadoop set to be created
 * \patam[in]   udata		the udata
 * \return 0 on success.
 */
int create_hadoop_set(char *fname, struct hadoop_set *hdset, uint64_t udata);

/**
 * \brief Destroy a Hadoop Metric set
 * \param[in] hdset The Hadoop set to be destroyed
 */
void destroy_hadoop_set(struct hadoop_set *hdset);

/**
 * \brief Receive Hadoop Metrics from a Hadoop daemon
 *
 * The call-back function after creating a thread to handle Hadoop metrics
 *
 * \param[in]   _hdset   a hadoop set
 */
void *recv_metrics(void *_hdset);

/**
 * \brief Setup a datagram socket
 * \param[in] port Port number
 * \param[out] sockfd Socket file descriptor to setup
 */
int setup_datagram(int port, int *sockfd);

#endif /* SAMPLER_HADOOP_H_ */
