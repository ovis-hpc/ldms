/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2022 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2022 Open Grid Computing, Inc. All rights reserved.
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
#include <librdkafka/rdkafka.h>
#include <ovis_json/ovis_json.h>
#include "ldms.h"
#include "ldmsd.h"

static ovis_log_t mylog;

#define LOG(LVL, FMT, ...) ovis_log(mylog, LVL, "store_kafka: " FMT, ## __VA_ARGS__)

#define LOG_ERROR(FMT, ...) LOG(OVIS_LERROR, FMT, ## __VA_ARGS__)
#define LOG_INFO(FMT, ...) LOG(OVIS_LINFO, FMT, ## __VA_ARGS__)
#define LOG_WARN(FMT, ...) LOG(OVIS_LWARNING, FMT, ## __VA_ARGS__)

static const char *_help_str =
"    config name=store_kafka [path=JSON_FILE]\n"
"        path=JSON_FILE is an optional JSON file containing a dictionary with\n"
"                       KEYS being Kafka configuration properties and\n"
"                       VALUES being their corresponding values.\n"
"                       The properties in the JSON_FILE is applied to all\n"
"                       Kafka connections from store_kafka.\n"
"                       Please see https://github.com/edenhill/librdkafka/blob/master/CONFIGURATION.md\n"
"                       for a list of supported properties.\n"
"\n"
"    STRGP WITH STORE_KAFKA\n"
"    ----------------------\n"
"    The `container` of the `strgp` that uses `store_kafka` is a CSV list of\n"
"    of brokers (host or host:port). For example:\n"
"\n"
"    strgp_add name=kp plugin=store_kafka container=localhost,br1.kf:9898 \\\n"
"              decomposition=decomp.json\n"
"";

static const char *usage(ldmsd_plug_handle_t handle)
{
	return  _help_str;
}

pthread_mutex_t sk_lock = PTHREAD_MUTEX_INITIALIZER;
static rd_kafka_conf_t *common_rconf = NULL;

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl,
		  struct attr_value_list *avl)
{
	int rc = 0;
	const char *path;
	json_entity_t jdoc = NULL, jent, jval;
	json_str_t jkey;
	json_parser_t jp = NULL;
	char *buff = NULL;
	size_t buff_sz, buff_len = 0;
	ssize_t len;
	int fd = -1;
	char num_str[128]; /* plenty to hold just a number */
	const char *val;
	char err_str[4096];
	rd_kafka_conf_res_t conf_res;

	pthread_mutex_lock(&sk_lock);
	if (common_rconf) {
		LOG_ERROR("reconfiguration is not supported\n");
		rc = EINVAL;
		goto out;
	}
	common_rconf = rd_kafka_conf_new();
	if (!common_rconf) {
		rc = errno;
		LOG_ERROR("rd_kafka_conf_new() failed %d\n", rc);
		goto out;
	}

	path = av_value(avl, "path");
	if (!path)
		goto out; /* nothing more to do */

	/* determine json buffer size */
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		rc = errno;
		LOG_ERROR("Cannot open file '%s', errno: %d\n", path, errno);
		goto out;
	}

	buff_sz = lseek(fd, 0, SEEK_END);
	if (buff_sz == (off_t)-1) {
		rc = errno;
		LOG_ERROR("lseek() failed: %d\n", errno);
		goto out;
	}
	buff_sz += 1; /* for the terminating '\0' */

	/* allocate and fill buffer */
	buff = malloc(buff_sz);
	if (!buff) {
		rc = ENOMEM;
		LOG_ERROR("Not enough memory (%s:%s():%d)\n", __FILE__, __func__, __LINE__);
		goto out;
	}
	buff[buff_sz-1] = 0;
	if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
		rc = errno;
		LOG_ERROR("lseek() failed: %d\n", errno);
		goto out;
	}
	while (buff_len < buff_sz - 1) {
		len = read(fd, buff, buff_sz - 1 - buff_len);
		if (len < 0) {
			LOG_ERROR("%s:%s():%d: read() error: %d\n", __FILE__, __func__, __LINE__, errno);
			rc = errno;
			goto out;
		}
		if (len == 0) /* EOF */
			break;
		buff_len += len;
	}
	if (buff_len < buff_sz - 1) {
		LOG_WARN("Expect to read %ld bytes from %s, "
			 "but got only %ld bytes\n",
			 buff_sz-1, path, buff_len);
	}
	buff[buff_len] = 0;

	/* json parse */
	jp = json_parser_new(buff_len + 1);
	if (!jp) {
		rc = errno;
		LOG_ERROR("json_parser_new() failed, errno: %d\n", errno);
		goto out;
	}
	rc = json_parse_buffer(jp, buff, buff_len+1, &jdoc);
	if (rc) {
		LOG_ERROR("json_parse_buffer() failed: %d\n", rc);
		goto out;
	}

	/* we expect a dict of "PROPERTY": "VALUE" */
	if ( jdoc->type != JSON_DICT_VALUE ) {
		LOG_ERROR("Expecting a dictionary in the store_kafka JSON "
			  "configuration file (%s), but got: %s\n",
			  path, json_type_name(jdoc->type));
		goto out;
	}

	/* for each attr */
	for (jent = json_attr_first(jdoc); jent; jent = json_attr_next(jent)) {
		jkey = json_attr_name(jent);
		jval = json_attr_value(jent);
		switch (jval->type) {
		case JSON_STRING_VALUE:
			val = json_value_cstr(jval);
			break;
		case JSON_INT_VALUE:
			val = num_str;
			snprintf(num_str, sizeof(num_str), "%ld", jval->value.int_);
			break;
		case JSON_FLOAT_VALUE:
			val = num_str;
			snprintf(num_str, sizeof(num_str), "%g", jval->value.double_);
			break;
		case JSON_BOOL_VALUE:
			val = num_str;
			snprintf(num_str, sizeof(num_str), "%d", jval->value.bool_);
			break;
		case JSON_NULL_VALUE:
			val = "";
			break;
		default:
			LOG_ERROR("Unsupported value type: %s\n", json_type_name(jval->type));
			goto out;
		}
		conf_res = rd_kafka_conf_set(common_rconf, jkey->str, val, err_str, sizeof(err_str));
		switch (conf_res) {
		case RD_KAFKA_CONF_OK:
			/* no-op */
			break;
		case RD_KAFKA_CONF_UNKNOWN:
			rc = EINVAL;
			LOG_ERROR("Unknown kafka config param: %s\n", jkey->str);
			goto out;
		case RD_KAFKA_CONF_INVALID:
			rc = EINVAL;
			LOG_ERROR("param: %s, invalid value: %s\n", jkey->str, val);
			goto out;
		default:
			rc = EINVAL;
			LOG_ERROR("Unknown kafka config error: %d\n", conf_res);
			goto out;
		}
	}

	rc = 0;
	/* OK; let through */

 out:
	pthread_mutex_unlock(&sk_lock);
	if (fd >= 0)
		close(fd);
	if (buff)
		free(buff);
	if (jp)
		json_parser_free(jp);
	if (jdoc)
		json_entity_free(jdoc);
	return rc;
}

static void term(ldmsd_plug_handle_t handle)
{
	pthread_mutex_lock(&sk_lock);
	if (common_rconf) {
		rd_kafka_conf_destroy(common_rconf);
		common_rconf = NULL;
	}
	pthread_mutex_unlock(&sk_lock);
}

typedef struct store_kafka_handle_s {
	rd_kafka_t *rk; /* The Kafka handle */
	rd_kafka_conf_t *rconf; /* The Kafka configuration */
} *store_kafka_handle_t;

static void close_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	/* NOTE: _sh is strgp->store_handle */

	/* This is called when strgp stopped to clean up resources */
	store_kafka_handle_t sh = _sh;
	if (sh->rk) {
		rd_kafka_destroy(sh->rk);
	}
	if (sh->rconf) {
		rd_kafka_conf_destroy(sh->rconf);
	}
	free(sh);
}

static store_kafka_handle_t __handle_new(ldmsd_strgp_t strgp)
{
	char err_str[512];
	rd_kafka_conf_res_t res;

	store_kafka_handle_t sh = calloc(1, sizeof(*sh));
	if (!sh)
		goto err_0;
	sh->rconf = rd_kafka_conf_dup(common_rconf);
	if (!sh->rconf)
		goto err_1;

	/* strgp->container is the CSV list of brokers */
	const char *param = "bootstrap.servers";
	res = rd_kafka_conf_set(sh->rconf, param,
				strgp->container, err_str, sizeof(err_str));
	if (res != RD_KAFKA_CONF_OK) {
		errno = EINVAL;
		LOG_ERROR("rd_kafka_conf_set() error: %s\n", err_str);
		goto err_2;
	}

	sh->rk = rd_kafka_new(RD_KAFKA_PRODUCER, sh->rconf, err_str, sizeof(err_str));
	if(!sh->rk) {
		LOG_ERROR("rd_kafka_new() error: %s\n", err_str);
		goto err_2;
	}
	sh->rconf = NULL; /* rd_kafka_new consumed and freed the conf */

	return sh;

 err_2:
	rd_kafka_conf_destroy(sh->rconf);
 err_1:
	free(sh);
 err_0:
	return NULL;
}

/* protected by strgp->lock */
static int
commit_rows(ldmsd_plug_handle_t handle, ldmsd_strgp_t strgp, ldms_set_t set, ldmsd_row_list_t row_list,
	    int row_count)
{
	store_kafka_handle_t sh;
	rd_kafka_topic_t *rkt;
	ldmsd_row_t row;
	char *buf;
	int rc, len;

	sh = strgp->store_handle;
	if (!sh) {
		sh = __handle_new(strgp);
		if (!sh)
			return errno;
		strgp->store_handle = sh;
	}

	TAILQ_FOREACH(row, row_list, entry) {
		rc = ldmsd_row_to_json_object(row, &buf, &len);
		if (rc) {
			LOG_ERROR("ldmsd_row_to_json_object() error: %d\n", rc);
			continue;
		}

		/* row schema is the "topic" */
		rkt = rd_kafka_topic_new(sh->rk, row->schema_name, NULL);
		if (!rkt) {
			LOG_ERROR("rd_kafka_topic_new(\"%s\") failed, "
				  "errno: %d\n", row->schema_name, errno);
			free(buf);
			continue;
		}

		rc = rd_kafka_produce(rkt, RD_KAFKA_PARTITION_UA,
				RD_KAFKA_MSG_F_FREE, buf, len, NULL, 0, NULL);
		if (rc) {
			LOG_ERROR("rd_kafka_produce(\"%s\") failed, "
				  "errno: %d\n", row->schema_name, rc);
			free(buf);
		}
		rd_kafka_topic_destroy(rkt);
	}

	return 0;
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
	.base.name = "kafka",
	.base.term = term,
	.base.config = config,
	.base.usage = usage,
	.base.type = LDMSD_PLUGIN_STORE,
        .base.constructor = constructor,
        .base.destructor = destructor,
	.close = close_store,
	.commit = commit_rows,
};
