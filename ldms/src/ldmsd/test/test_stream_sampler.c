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
#include "tada/tada.h"

#include "config.h" /* for OVIS_GIT_LONG */

static char tada_user[64]; /* populated in get_plugin */

TEST_BEGIN("LDMSD_Communications", "JSON_Stream_Test", "FVT",
	   tada_user,
	   OVIS_GIT_LONG, /* commit ID */
	   "LDMSD stream test with JSON data format",
	   t_1)
TEST_ASSERTION(t_1, 0, "'schema' STRING Present")
TEST_ASSERTION(t_1, 1, "'schema' STRING Correct")
TEST_ASSERTION(t_1, 2, "'timestamp' INT Present")
TEST_ASSERTION(t_1, 3, "'timestamp' INT Correct")
TEST_ASSERTION(t_1, 4, "'data' DICT Present")
TEST_ASSERTION(t_1, 5, "'id' INT Present")
TEST_ASSERTION(t_1, 6, "'id' INT Value Correct")
TEST_ASSERTION(t_1, 7, "'list' LIST Present")
TEST_ASSERTION(t_1, 8, "'list' LIST Value Correct")
TEST_END(t_1);

TEST_BEGIN("LDMSD_Communications", "STRING_Stream_Test", "FVT",
	   tada_user,
	   OVIS_GIT_LONG, /* commit ID */
	   "LDMSD stream test with plain text data format",
	   t_2)
TEST_ASSERTION(t_2, 0, "Expect file is present")
TEST_ASSERTION(t_2, 1, "All stream data received")
TEST_ASSERTION(t_2, 2, "Stream data is correct")
TEST_END(t_2);

static ldmsd_msg_log_f msglog;
static char *stream;

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=test_stream_sampler path=<path> port=<port_no> log=<path>\n"
		"     stream    The stream name to subscribe to.\n"
		"     expect    The path to a file containing the expected stream text\n";
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

char *expect_file_name;

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

	value = av_value(avl, "expect");
	if (value)
		expect_file_name = strdup(value);

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
	json_str_t schema_name;

	if (stream_type != LDMSD_STREAM_JSON) {
		char expect_text[512];
		FILE *expect_file = fopen(expect_file_name, "r");
		TEST_START(t_2);
		if (TEST_ASSERT(t_2, 0, (expect_file != NULL))) {
			rc = fread(expect_text, 1, sizeof(expect_text), expect_file);
			TEST_ASSERT(t_2, 1, (rc <= msg_len));
			TEST_ASSERT(t_2, 2, (0 == memcmp(msg, expect_text, rc)));
		}
		TEST_FINISH(t_2);
		return 0;
	}
	TEST_START(t_1);
	rc = 0;
	schema = json_attr_find(entity, "schema");
	if (TEST_ASSERT(t_1, 0, (schema != NULL))) {
		schema_name = json_value_str(json_attr_value(schema));
		TEST_ASSERT(t_1, 1, (0 == strcmp(schema_name->str,"stream_test")));
	}

	timestamp = json_attr_find(entity, "timestamp");
	if (TEST_ASSERT(t_1, 2, (timestamp != NULL)))
		TEST_ASSERT(t_1, 3, (1559242264 == json_value_int(json_attr_value(timestamp))));

	data = json_attr_find(entity, "data");
	if (TEST_ASSERT(t_1, 4, (data != NULL))) {
		dict = json_attr_value(data);
		attr = json_attr_find(dict, "id");
		if (TEST_ASSERT(t_1, 5, (attr != NULL)))
			TEST_ASSERT(t_1, 6, (12345 == json_value_int(json_attr_value(attr))));

		attr = json_attr_find(dict, "list");
		if (TEST_ASSERT(t_1, 7, (attr != NULL))) {
			test_list = json_attr_value(attr);
			int i = 1;
			int list_values_match = TADA_TRUE;
			for (test_list = json_item_first(test_list); test_list;
			     test_list = json_item_next(test_list)) {
				if (json_value_int(test_list) != i) {
					list_values_match = TADA_FALSE;
					break;
				}
				i += 1;
			}
			TEST_ASSERT(t_1, 8, (list_values_match == TADA_TRUE));
		}
	}
	TEST_FINISH(t_1);
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
	char *__user = getenv("TADA_USER");
	if (__user)
		snprintf(tada_user, sizeof(tada_user), "%s", __user);
	else
		getlogin_r(tada_user, sizeof(tada_user));
	return &test_stream_sampler.base;
}
