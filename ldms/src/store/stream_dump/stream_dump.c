/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2023 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2023 Open Grid Computing, Inc. All rights reserved.
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
#define _GNU_SOURCE
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
#include <coll/rbt.h>
#include <ovis_log/ovis_log.h>
#include "ldms.h"
#include "ldmsd.h"

#define LOG_(level, ...) do { \
	ovis_log(mylog, level, __VA_ARGS__); \
} while(0);

#define ERR_LOG(...) LOG_(OVIS_LERROR, __VA_ARGS__)

static ovis_log_t mylog;

static const char *_usage = "\
    config name=stream_dump op=subscribe|close stream=<STREAM>\n\
           [path=<FILE_PATH>]\n\
\n\
    - Configure with `op=subscribe` to subscribe the matching <STREAM> and dump\n\
      the contents to into <FILE_PATH>.\n\
    - Configure with `op=close` to terminate the <STREAM> subscription.\n\
    - The `config` command can be called multiple times.\n\
\n\
    REMARK\n\
    ------\n\
    This plugin is meant for LDMS stream content dumping for debug and\n\
    provisioning purposes. Do not use it with any storage policy.\n\
\n\
    EXAMPLE\n\
    -------\n\
    load name=stream_dump\n\
    config name=stream_dump op=subscribe stream=.*-stream path=/out/all.txt\n\
    config name=stream_dump op=subscribe stream=.* path=/out/all.txt\n\
    config name=stream_dump op=subscribe stream=slurm path=/out/slurm.txt\n\
    config name=stream_dump op=close stream=.*\n\
    \n\
    The `op=close stream=.*` closes only the `stream=.*` stream client,\n\
    leaving the `stream=.*-stream` and `stream=slurm` intact.\n\
\n\
";

static const char *
__usage(ldmsd_plug_handle_t handle)
{
	return _usage;
}

pthread_mutex_t __mutex = PTHREAD_MUTEX_INITIALIZER;

struct __client_s {
	struct rbn rbn;
	ldms_stream_client_t c;
	FILE *f;
	char stream[OVIS_FLEX];
};

struct rbt rbt = RBT_INITIALIZER((void*)strcmp);

/*
 * Guessing if the string s is a regular expression or just a string.
 */
static int __is_regex(const char *s)
{
	const char *c;
	static const char tbl[256] = {
		['$'] = 1,
		['('] = 1,
		[')'] = 1,
		['*'] = 1,
		['+'] = 1,
		['.'] = 1,
		[':'] = 1,
		['?'] = 1,
		['['] = 1,
		['\\'] = 1,
		[']'] = 1,
		['^'] = 1,
		['{'] = 1,
		['|'] = 1,
		['}'] = 1,
	};
	for (c = s; *c; c++) {
		if (tbl[(int)*c])
			return 1;
	}
	return 0;
}

int __stream_cb(ldms_stream_event_t ev, void *cb_arg)
{
	struct __client_s *cli = cb_arg;
	int tid = (pid_t) syscall (SYS_gettid);
	if (ev->type != LDMS_STREAM_EVENT_RECV) /* ignore other events */
		return 0;
	fprintf(cli->f, "\x1%d: %.*s\n", tid, ev->recv.data_len, ev->recv.data);
	return 0;
}

static int __op_subscribe(const char *stream, const char *path)
{
	struct rbn *rbn;
	struct __client_s *cli;
	char desc[8192];
	int rc;
	if (!path) {
		ERR_LOG("'path' is required for op=subscribe\n");
		return EINVAL;
	}

	pthread_mutex_lock(&__mutex);
	rbn = rbt_find(&rbt, stream);
	if (rbn) {
		ERR_LOG("stream client '%s' alread existed\n", stream);
		rc = EEXIST;
		goto err0;
	}
	cli = malloc(sizeof(*cli) + strlen(stream) + 1);
	if (!cli) {
		ERR_LOG("Not enough memory (stream: '%s')\n", stream);
		rc = ENOMEM;
		goto err0;
	}
	memcpy(cli->stream, stream, strlen(stream) + 1);
	rbn_init(&cli->rbn, cli->stream);
	cli->f = fopen(path, "w");
	if (!cli->f) {
		ERR_LOG("Failed to open file '%s', errno: %d\n", path, errno);
		rc = errno;
		goto err1;
	}
	snprintf(desc, sizeof(desc), "stream_dump, path:%s", path);
	setbuf(cli->f, NULL); /* do not buffer */
	cli->c = ldms_stream_subscribe(stream, __is_regex(stream), __stream_cb, cli, desc);
	if (!cli->c) {
		ERR_LOG("ldms_stream_subscribe() failed: %d\n", errno);
		rc = errno;
		goto err2;
	}
	rbt_ins(&rbt, &cli->rbn);
	pthread_mutex_unlock(&__mutex);
	return 0;
 err2:
	fclose(cli->f);
 err1:
	free(cli);
 err0:
	pthread_mutex_unlock(&__mutex);
	return rc;
}

static int __op_close(const char *stream)
{
	struct rbn *rbn;
	struct __client_s *cli;
	int rc;

	pthread_mutex_lock(&__mutex);
	rbn = rbt_find(&rbt, stream);
	if (!rbn) {
		ERR_LOG("stream client '%s' not found\n", stream);
		rc = ENOENT;
		goto err0;
	}
	rbt_del(&rbt, rbn);
	pthread_mutex_unlock(&__mutex);
	cli = container_of(rbn, struct __client_s, rbn);
	ldms_stream_close(cli->c);
	fclose(cli->f);
	free(cli);
	return 0;
 err0:
	pthread_mutex_unlock(&__mutex);
	return rc;
}

static int
__config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl,
		struct attr_value_list *avl)
{
	const char *op = av_value(avl, "op");
	const char *stream = av_value(avl, "stream");
	const char *path = av_value(avl, "path");

	if (0 == strcmp(op, "subscribe")) {
		return __op_subscribe(stream, path);
	} else if (0 == strcmp(op, "close")) {
		return __op_close(stream);
	} else {
		ERR_LOG("Unknown op '%s'\n", op);
		return EINVAL;
	}
}

static void
__term(ldmsd_plug_handle_t handle)
{
	struct rbn *rbn;
	struct __client_s *cli;

	pthread_mutex_lock(&__mutex);
	while ((rbn = rbt_min(&rbt))) {
		rbt_del(&rbt, rbn);
		pthread_mutex_unlock(&__mutex);
		cli = container_of(rbn, struct __client_s, rbn);
		ldms_stream_close(cli->c);
		fclose(cli->f);
		free(cli);
		pthread_mutex_lock(&__mutex);
	}
	pthread_mutex_unlock(&__mutex);
}

static ldmsd_store_handle_t
__open(ldmsd_plug_handle_t handle, const char *container, const char *schema,
       struct ldmsd_strgp_metric_list *metric_list)
{
	errno = ENOTSUP;
	return NULL;
}

static void __close(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	errno = ENOTSUP;
}

static int __flush(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	return ENOTSUP;
}

static int
__store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t sh, ldms_set_t set, int *n, size_t count)
{
	return ENOTSUP;
}

static int
__commit(ldmsd_plug_handle_t handle, ldmsd_strgp_t strgp, ldms_set_t set, ldmsd_row_list_t row_list,
	 int row_count)
{
	return ENOTSUP;
}

static int constructor(ldmsd_plug_handle_t handle)
{
	mylog = ldmsd_plug_log_get(handle);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
}

struct ldmsd_store ldmsd_plugin_interface = {
	.base = {
		.name = "stream_dump",
		.term = __term,
		.config = __config,
		.usage = __usage,
		.type = LDMSD_PLUGIN_STORE,
		.constructor = constructor,
		.destructor = destructor,
	},
	.open = __open,
	.store = __store,
	.flush = __flush,
	.close = __close,
	.commit = __commit,
};
