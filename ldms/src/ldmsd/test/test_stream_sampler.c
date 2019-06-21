/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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
 * \file job.c
 * \brief shared job data provider
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <time.h>
#include <pthread.h>
#include <strings.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <coll/htbl.h>
#include <json/json_util.h>
#include <assert.h>
#include <sched.h>
#include "ldms.h"
#include "../ldmsd.h"
#include "../ldmsd_stream.h"

static ldmsd_msg_log_f msglog;
static char *stream;

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=test_stream_sampler path=<path> port=<port_no> log=<path>\n"
		"     path      The path to the root of the SOS container store.\n"
		"     stream    The stream name to subscribe to.\n";
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static int sample(struct ldmsd_sampler *self)
{
	return 0;
}

static int test_stream_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity);

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	int rc;

	value = av_value(avl, "stream");
	if (value)
		stream = strdup(value);
	else
		stream = strdup("test_stream");
	ldmsd_stream_subscribe(stream, test_stream_recv_cb, self);
	rc = 0;
	return rc;
}

static int test_stream_recv_cb(ldmsd_stream_client_t c, void *ctxt,
			 ldmsd_stream_type_t stream_type,
			 const char *msg, size_t msg_len,
			 json_entity_t entity)
{
	int rc;
	json_entity_t attr, dict, data, schema, timestamp, test_list;
	uint64_t tstamp;
	json_str_t schema_name;
	FILE *fp;

	fp = fopen("/tmp/stream_test_out.txt", "w+");
	fprintf(fp, "Test LDMSD_STREAM_STRING\n");

	if (stream_type != LDMSD_STREAM_JSON) {
		msglog(LDMSD_LDEBUG, "test_stream_sampler: Unexpected stream type data...ignoring\n");
		msglog(LDMSD_LDEBUG, "test_stream_sampler:" "%s\n", msg);
		return EINVAL;
	}
	rc = 0;
	schema = json_attr_find(entity, "schema");
	schema_name = json_value_str(json_attr_value(schema));
	if (strcmp(schema_name->str,"stream_test") == 0)
		fprintf(fp, "\nTest 1: LDMSD_STRING_ATTR PASS\n");
	else
		fprintf(fp, "\nTest 1: LDMSD_STRING_ATTR FAIL\n");

	timestamp = json_attr_find(entity, "timestamp");
	tstamp = json_value_int(json_attr_value(timestamp));
	if (tstamp == 1559242264)
		fprintf(fp, "\nTest 2: json int PASS\n");
	else
		fprintf(fp, "\nTest 2: json int FAIL\n");

	data = json_attr_find(entity, "data");
	dict = json_attr_value(data);
	attr = json_attr_find(dict, "id");
	uint64_t id = json_value_int(json_attr_value(attr));
	if (id == 12345)
		fprintf(fp, "\nTest 3: attr in nested dict PASS\n");
	else
		fprintf(fp, "\n Test 3: attr in nested dict FAIL\n");

	attr = json_attr_find(dict, "test_list");
	test_list = json_attr_value(attr);
	int i = 1;
	int tc = 0;
	for (test_list = json_item_first(test_list); test_list;
	     test_list = json_item_next(test_list)) {
		if (json_value_int(test_list) != i) {
			tc = 1;
			break;
		}
		i += 1;
	}
	if (tc) 
		fprintf(fp, "\nTest 4: List attr FAIL\n");
	else
		fprintf(fp, "\nTest 4: List attr PASS\n");

	fclose(fp);
	return rc;
}

static void term(struct ldmsd_plugin *self)
{
}

static struct ldmsd_sampler test_stream_sampler = {
	.base = {
		.name = "test_stream_sampler",
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &test_stream_sampler.base;
}
