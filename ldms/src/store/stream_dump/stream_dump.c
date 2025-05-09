/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2023,2025 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2023,2025 Open Grid Computing, Inc. All rights reserved.
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
#include <ovis_ref/ref.h>

#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "ldmsd_stream.h"

#define LOG_(sd, level, ...) do { \
	ovis_log((sd)->log, level, __VA_ARGS__); \
} while(0);

#define ERR_LOG(sd, ...) LOG_((sd), OVIS_LERROR, __VA_ARGS__)

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

/* stream dump instance */
struct stream_dump_s {
	pthread_mutex_t mutex;
	ovis_log_t log;
	struct rbt rbt; /* tree of __client_s */
};

#define __CLIENT_TYPE_STREAM 's'
#define __CLIENT_TYPE_MSG 'm'

struct __client_s {
	struct rbn rbn;
	struct ref_s ref;
	ldms_msg_client_t mc;
	ldmsd_stream_client_t sc;
	FILE *f;
	char type; /* client type */
	char match[OVIS_FLEX];
};

void __client_free(void *a)
{
	struct __client_s *cli = a;
	fclose(cli->f);
	free(cli);
}

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

int __msg_cb(ldms_msg_event_t ev, void *cb_arg)
{
	struct __client_s *cli = cb_arg;
	int tid = (pid_t) syscall (SYS_gettid);
	if (ev->type == LDMS_MSG_EVENT_CLIENT_CLOSE) {
		/* this is the last event for this client */
		ref_put(&cli->ref, "sub");
		return 0;
	}
	if (ev->type != LDMS_MSG_EVENT_RECV) /* ignore other events */
		return 0;
	fprintf(cli->f, "\x1%d: %.*s\n", tid, ev->recv.data_len, ev->recv.data);
	return 0;
}

int __stream_cb(ldmsd_stream_client_t c, void *cb_arg,
				      ldmsd_stream_type_t stream_type,
				      const char *data, size_t data_len,
				      json_entity_t entity)
{
	struct __client_s *cli = cb_arg;
	int tid = (pid_t) syscall (SYS_gettid);
	fprintf(cli->f, "\x1%d: %.*s\n", tid, (int)data_len, data);
	return 0;
}

static int __op_stream_subscribe(struct stream_dump_s *sd,
				 const char *stream, const char *path)
{
	struct rbn *rbn;
	struct __client_s *cli;
	char desc[8192];
	char key[512];
	int rc;
	if (!path) {
		ERR_LOG(sd, "'path' is required for op=subscribe\n");
		return EINVAL;
	}

	snprintf(key, sizeof(key), "%c%s", __CLIENT_TYPE_STREAM, stream);

	pthread_mutex_lock(&sd->mutex);
	rbn = rbt_find(&sd->rbt, key);
	if (rbn) {
		ERR_LOG(sd, "stream client '%s' already existed\n", stream);
		rc = EEXIST;
		goto err0;
	}
	cli = calloc(1, sizeof(*cli) + strlen(stream) + 1);
	if (!cli) {
		ERR_LOG(sd, "Not enough memory (stream): '%s')\n", stream);
		rc = ENOMEM;
		goto err0;
	}
	cli->type = __CLIENT_TYPE_MSG;
	memcpy(cli->match, stream, strlen(stream) + 1);
	rbn_init(&cli->rbn, &cli->type);
	cli->f = fopen(path, "w");
	if (!cli->f) {
		ERR_LOG(sd, "Failed to open file '%s', errno: %d\n", path, errno);
		rc = errno;
		goto err1;
	}
	snprintf(desc, sizeof(desc), "stream_dump, path:%s", path);
	setbuf(cli->f, NULL); /* do not buffer */
	cli->sc = ldmsd_stream_subscribe(stream, __stream_cb, cli);
	if (!cli->sc) {
		ERR_LOG(sd, "ldms_msg_subscribe() failed: %d\n", errno);
		rc = errno;
		goto err2;
	}
	ref_init(&cli->ref, "sub", __client_free, cli);
	rbt_ins(&sd->rbt, &cli->rbn);
	ref_get(&cli->ref, "rbt");
	pthread_mutex_unlock(&sd->mutex);
	return 0;
 err2:
	fclose(cli->f);
 err1:
	free(cli);
 err0:
	pthread_mutex_unlock(&sd->mutex);
	return rc;
}

static int __op_msg_subscribe(struct stream_dump_s *sd,
			      const char *mch, const char *path)
{
	struct rbn *rbn;
	struct __client_s *cli;
	char desc[8192];
	char key[512];
	int rc;
	if (!path) {
		ERR_LOG(sd, "'path' is required for op=subscribe\n");
		return EINVAL;
	}

	snprintf(key, sizeof(key), "%c%s", __CLIENT_TYPE_MSG, mch);

	pthread_mutex_lock(&sd->mutex);
	rbn = rbt_find(&sd->rbt, key);
	if (rbn) {
		ERR_LOG(sd, "channel client '%s' already existed\n", mch);
		rc = EEXIST;
		goto err0;
	}
	cli = calloc(1, sizeof(*cli) + strlen(mch) + 1);
	if (!cli) {
		ERR_LOG(sd, "Not enough memory (message_channel: '%s')\n", mch);
		rc = ENOMEM;
		goto err0;
	}
	cli->type = __CLIENT_TYPE_MSG;
	memcpy(cli->match, mch, strlen(mch) + 1);
	rbn_init(&cli->rbn, &cli->type);
	cli->f = fopen(path, "w");
	if (!cli->f) {
		ERR_LOG(sd, "Failed to open file '%s', errno: %d\n", path, errno);
		rc = errno;
		goto err1;
	}
	snprintf(desc, sizeof(desc), "stream_dump, path:%s", path);
	setbuf(cli->f, NULL); /* do not buffer */
	cli->mc = ldms_msg_subscribe(mch, __is_regex(mch), __msg_cb, cli, desc);
	if (!cli->mc) {
		ERR_LOG(sd, "ldms_msg_subscribe() failed: %d\n", errno);
		rc = errno;
		goto err2;
	}
	ref_init(&cli->ref, "sub", __client_free, cli);
	rbt_ins(&sd->rbt, &cli->rbn);
	ref_get(&cli->ref, "rbt");
	pthread_mutex_unlock(&sd->mutex);
	return 0;
 err2:
	fclose(cli->f);
 err1:
	free(cli);
 err0:
	pthread_mutex_unlock(&sd->mutex);
	return rc;
}

static int __op_msg_close(struct stream_dump_s *sd, const char *mch)
{
	struct rbn *rbn;
	struct __client_s *cli;
	int rc;
	char key[1024];

	snprintf(key, sizeof(key), "%c%s", __CLIENT_TYPE_MSG, mch);

	pthread_mutex_lock(&sd->mutex);
	rbn = rbt_find(&sd->rbt, key);
	if (!rbn) {
		ERR_LOG(sd, "message client '%s' not found\n", mch);
		rc = ENOENT;
		goto err0;
	}
	rbt_del(&sd->rbt, rbn);
	pthread_mutex_unlock(&sd->mutex);
	cli = container_of(rbn, struct __client_s, rbn);
	ref_put(&cli->ref, "rbt");
	assert(cli->mc);
	ldms_msg_client_close(cli->mc);
	return 0;

 err0:
	pthread_mutex_unlock(&sd->mutex);
	return rc;
}

static int __op_stream_close(struct stream_dump_s *sd, const char *stream)
{
	struct rbn *rbn;
	struct __client_s *cli;
	int rc;
	char key[1024];

	snprintf(key, sizeof(key), "%c%s", __CLIENT_TYPE_STREAM, stream);

	pthread_mutex_lock(&sd->mutex);
	rbn = rbt_find(&sd->rbt, stream);
	if (!rbn) {
		ERR_LOG(sd, "stream client '%s' not found\n", stream);
		rc = ENOENT;
		goto err0;
	}
	rbt_del(&sd->rbt, rbn);
	pthread_mutex_unlock(&sd->mutex);
	cli = container_of(rbn, struct __client_s, rbn);
	ref_put(&cli->ref, "rbt");
	ldmsd_stream_close(cli->sc);
	ref_put(&cli->ref, "sub");
	return 0;
 err0:
	pthread_mutex_unlock(&sd->mutex);
	return rc;
}

static int
__config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl,
		struct attr_value_list *avl)
{
	const char *op = av_value(avl, "op");
	const char *stream = av_value(avl, "stream");
	const char *path = av_value(avl, "path");
	const char *mch = av_value(avl, "message_channel");
	struct stream_dump_s *sd = ldmsd_plug_ctxt_get(handle);

	if (0 == strcmp(op, "msg_subscribe")) {
		return __op_msg_subscribe(sd, mch, path);
	} else if (0 == strcmp(op, "stream_subscribe")) {
		return __op_stream_subscribe(sd, stream, path);
	} else if (0 == strcmp(op, "msg_close")) {
		return __op_msg_close(sd, mch);
	} else if (0 == strcmp(op, "stream_close")) {
		return __op_stream_close(sd, stream);
	} else {
		ERR_LOG(sd, "Unknown op '%s'\n", op);
		return EINVAL;
	}
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
	struct stream_dump_s *sd = malloc(sizeof(*sd));
	if (!sd)
		return errno;
	sd->log = ldmsd_plug_log_get(handle);
	rbt_init(&sd->rbt, (void*)strcmp);
	pthread_mutex_init(&sd->mutex, NULL);
	ldmsd_plug_ctxt_set(handle, sd);
	return 0;
}

static void destructor(ldmsd_plug_handle_t handle)
{
	struct rbn *rbn;
	struct __client_s *cli;
	struct stream_dump_s *sd = ldmsd_plug_ctxt_get(handle);
	if (!sd)
		return;

	pthread_mutex_lock(&sd->mutex);
	while ((rbn = rbt_min(&sd->rbt))) {
		rbt_del(&sd->rbt, rbn);
		pthread_mutex_unlock(&sd->mutex);
		cli = container_of(rbn, struct __client_s, rbn);
		ref_put(&cli->ref, "rbt");
		if (cli->mc) {
			ldms_msg_client_close(cli->mc);
		}
		if (cli->sc) {
			ldmsd_stream_close(cli->sc);
			ref_put(&cli->ref, "sub");
		}
		pthread_mutex_lock(&sd->mutex);
	}
	pthread_mutex_unlock(&sd->mutex);
	free(sd);
}

struct ldmsd_store ldmsd_plugin_interface = {
	.base = {
		.config = __config,
		.usage = __usage,
		.type = LDMSD_PLUGIN_STORE,
		.flags = LDMSD_PLUGIN_MULTI_INSTANCE,
		.constructor = constructor,
		.destructor = destructor,
	},
	.open = __open,
	.store = __store,
	.flush = __flush,
	.close = __close,
	.commit = __commit,
};
