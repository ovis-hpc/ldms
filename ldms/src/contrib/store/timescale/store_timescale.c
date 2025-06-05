/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2018 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2020 HPC Center at Shanghai Jiao Tong University. All rights reserved.
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
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include </usr/include/libpq-fe.h>

static char user[100];
static char hostaddr[100];
static char port[100];
static char dbname[100];
static char password[100];
struct timescale_store {
        char *schema;
        char *container;
        pthread_mutex_t lock;
        int job_mid;
        int comp_mid;
        char **metric_name;
        LIST_ENTRY(timescale_store) entry;
        PGconn *conn;
        size_t measurement_limit;
        char measurement[0];
};

#define MEASUREMENT_LIMIT_DEFAULT	8192
static long measurement_limit = MEASUREMENT_LIMIT_DEFAULT;
static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(timescale_store_list, timescale_store) store_list;
static ovis_log_t mylog;

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
	size_t cnt = snprintf(&line[*line_off], line_len, "%hhu", ldms_metric_get_u8(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s8_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%hhd", ldms_metric_get_s8(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_u16_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%hu", ldms_metric_get_u16(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s16_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%hd", ldms_metric_get_s16(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_str_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "\'%s\'", ldms_metric_array_get_str(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_u32_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%u", ldms_metric_get_u32(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s32_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%d", ldms_metric_get_s32(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_u64_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%lu", ldms_metric_get_u64(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s64_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%ld", ldms_metric_get_s64(s, i));
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

typedef int (*timescale_value_set_fn)(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i);
timescale_value_set_fn timescale_value_set[] = {
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
                case '.':
                        *s = '_';
                        break;
                default:
                        break;
                }
                s++;
        }
        int ret = strcmp(name, "create");
        if (ret == 0)
            name[5] = '_';

        ret = strcmp(name, "user");
        if (ret == 0)
            name[3] = '_';

        return name;
}

/**
 *  * \brief Configuration
 *   */
static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl, struct attr_value_list *avl)
{
        char *value, *pwfile = NULL;
        pthread_mutex_lock(&cfg_lock);

        value = av_value(avl, "user");
        if (!value) {
                ovis_log(mylog, OVIS_LERROR, "The 'user' keyword is required.\n");
                return EINVAL;
        }
        strncpy(user, value, sizeof(user));

        pwfile = av_value(avl, "pwfile");
        if (!pwfile) {
                ovis_log(mylog, OVIS_LERROR, "The 'pwfile' keyword is required.\n");
                return EINVAL;
        }
        if (pwfile) {
                if (!pwfile || pwfile[0] != '/') {
                        ovis_log(mylog, OVIS_LERROR, "Invalid password file path! Must start with '/'.\n");
                        return EINVAL;
                }

                FILE *file = fopen(pwfile, "r");
                if (!file) {
                        ovis_log(mylog, OVIS_LERROR, "Unable to open password file!\n");
                        return EINVAL;
                }
                char line[600];
                char *s, *ptr;
                s = NULL;
                while (fgets(line, 600, file)) {
                        if ((line[0] == '#') || (line[0] == '\n'))
                                continue;
                        if (0 == strncmp(line, "secretword=", 11)) {
                                s = strtok_r(&line[11], " \t\n", &ptr);
                                if (!s) {
                                        ovis_log(mylog, OVIS_LERROR, "Auth error: the secret word is an empty srting.\n");
                                        return EINVAL;
                                }
                                break;
                        }
                }
                if (!s) {
                        ovis_log(mylog, OVIS_LERROR, "No secret word in the file!\n");
                        return EINVAL;
                }
                strncpy(password, strdup(s), sizeof(password));

                fclose(file);
        }

        value = av_value(avl, "hostaddr");
        if (!value) {
                ovis_log(mylog, OVIS_LERROR, "The 'hostaddr' keyword is required.\n");
                return EINVAL;
        }
        strncpy(hostaddr, value, sizeof(hostaddr));

        value = av_value(avl, "port");
        if (!value) {
                ovis_log(mylog, OVIS_LERROR, "The 'port' keyword is required.\n");
                return EINVAL;
        }
        strncpy(port, value, sizeof(port));

        value = av_value(avl, "dbname");
        if (!value) {
                ovis_log(mylog, OVIS_LERROR, "The 'dbname' keyword is required.\n");
                return EINVAL;
        }
        strncpy(dbname, value, sizeof(dbname));

        value = av_value(avl, "measurement_limit");
        if (value) {
                measurement_limit = strtol(value, NULL, 0);
                if (measurement_limit <= 0) {
                        ovis_log(mylog, OVIS_LERROR,
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
        return "config name=store_timescale user=<username> pwfile=<full path to password file> "
               "hostaddr=<host ip addr> port=<port no> dbname=<database name> "
               "measurement_limit=<sql statement length>";
}

static ldmsd_store_handle_t
open_store(ldmsd_plug_handle_t handle, const char *container, const char *schema,
	   struct ldmsd_strgp_metric_list *metric_list)
{
        struct timescale_store *is = NULL;
        char *measurement_create;
        size_t cnt_create, off_create;

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
        is->job_mid = -1;
        is->comp_mid = -1;

        char str[256];
        snprintf(str, sizeof(str), "user=%s password=%s hostaddr=%s port=%s dbname=%s",
		 strdup(user), password, strdup(hostaddr), strdup(port), strdup(dbname));

        is->conn = PQconnectdb(str);
        if (PQstatus(is->conn) == CONNECTION_BAD) {
                ovis_log(mylog, OVIS_LERROR, "TimescaleDB connection failed!\n");
                goto err4;
        }

        measurement_create = is->measurement;
        cnt_create = snprintf(measurement_create, is->measurement_limit,
                   "CREATE TABLE IF NOT EXISTS %s(",
                   is->schema);
        off_create = cnt_create;

        ldmsd_strgp_metric_t x;
        char *name;
        int comma = 0;
        TAILQ_FOREACH(x, metric_list, entry) {
                name = x->name;
                enum ldms_value_type metric_type = x->type;
                if (metric_type > LDMS_V_CHAR_ARRAY) {
                        ovis_log(mylog, OVIS_LERROR,
                               "The metric %s:%s of type %s is not supported by "
                               "TimescaleDB and is being ignored.\n",
                               is->schema,
                               name,
                               ldms_metric_type_to_str(metric_type));
                        continue;
                }
                if (comma) {
                        if (off_create > is->measurement_limit - 16)
                                goto err5;
                        measurement_create[off_create++] = ',';
                } else
                        comma = 1;
                cnt_create = snprintf(&measurement_create[off_create], is->measurement_limit - off_create,
                               "%s ", fixup(name));
                off_create += cnt_create;

                if (metric_type < LDMS_V_F32){
                        cnt_create = snprintf(&measurement_create[off_create], is->measurement_limit - off_create, "DECIMAL");
                } else if (metric_type < LDMS_V_CHAR_ARRAY) {
                        cnt_create = snprintf(&measurement_create[off_create], is->measurement_limit - off_create, "DOUBLE PRECISION");
                } else
                        cnt_create = snprintf(&measurement_create[off_create], is->measurement_limit - off_create, "VARCHAR(255)");

                off_create += cnt_create;

        }
        cnt_create = snprintf(&measurement_create[off_create], is->measurement_limit - off_create, ",timestamp TIMESTAMPTZ)");
        off_create += cnt_create;

        PGresult *res = PQexec(is->conn, measurement_create);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                ovis_log(mylog, OVIS_LERROR, "Create table error! with sql %s \n", measurement_create);
                PQclear(res);
                goto err4;
        }

        pthread_mutex_lock(&cfg_lock);
        LIST_INSERT_HEAD(&store_list, is, entry);
        pthread_mutex_unlock(&cfg_lock);
        return is;

 err5:
	ovis_log(mylog, OVIS_LERROR, "Overflow formatting TimescaleDB measurement data.\n");
 err4:
        PQfinish(is->conn);
        free(is->schema);
 err2:
        free(is->container);
 err1:
        free(is);
 out:
        return NULL;
}

static int init_store(struct timescale_store *is, ldms_set_t set, int *mids, int count)
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
        struct timescale_store *is = _sh;
        struct ldms_timestamp timestamp;
        int i;
        int rc = 0;
        size_t cnt_insert, off_insert;
        char *measurement_insert;
        enum ldms_value_type metric_type;
        if (!is)
                return EINVAL;

        pthread_mutex_lock(&is->lock);
        if (is->job_mid < 0) {
                rc = init_store(is, set, metric_arry, metric_count);
                if (rc)
                        goto err;
        }

        measurement_insert = is->measurement;
        cnt_insert = snprintf(measurement_insert, is->measurement_limit,
                   "INSERT INTO %s VALUES(",
                   is->schema);

        off_insert = cnt_insert;

        int comma = 0;
        for (i = 0; i < metric_count; i++) {
                metric_type = ldms_metric_type_get(set, metric_arry[i]);
                if (metric_type > LDMS_V_CHAR_ARRAY) {
                        ovis_log(mylog, OVIS_LERROR,
                               "The metric %s:%s of type %s is not supported by "
                               "TimescaleDB and is being ignored.\n",
                               is->schema,
                               ldms_metric_name_get(set, metric_arry[i]),
                               ldms_metric_type_to_str(metric_type));
                        continue;
                }
                if (comma) {
                        if (off_insert > is->measurement_limit - 16)
                                goto err;
                        measurement_insert[off_insert++] = ',';
                } else
                        comma = 1;

                if (timescale_value_set[metric_type](measurement_insert, &off_insert,
						  is->measurement_limit - off_insert,
						  set, metric_arry[i]))
                        goto err;
        }

        timestamp = ldms_transaction_timestamp_get(set);

        char str[100] = {0};
        sprintf(str, "%d", timestamp.sec);
        char command[150];
        strcpy(command, "date '+%Y-%m-%d %H:%M:%S+08' -d @");
        strcat(command, str);
        FILE *fp;
        char buffer[50];
        fp=popen(command, "r");
	char *strdate = fgets(buffer, sizeof(buffer), fp);
	if (fp)
		pclose(fp);

	if (strdate) {
		cnt_insert = snprintf(&measurement_insert[off_insert], is->measurement_limit - off_insert, ",'%s')", buffer);
		off_insert += cnt_insert;

		PGresult *res = PQexec(is->conn, measurement_insert);
		if (PQresultStatus(res) != PGRES_COMMAND_OK) {
			ovis_log(mylog, OVIS_LERROR, "Insert table error! with sql %s \n", measurement_insert);
			PQclear(res);
			PQfinish(is->conn);
		}
	} else {
		ovis_log(mylog, OVIS_LERROR, "Unable to format date for table insertion!\n");
	}
        pthread_mutex_unlock(&is->lock);
        return 0;
err:
        pthread_mutex_unlock(&is->lock);

        ovis_log(mylog, OVIS_LERROR, "Overflow formatting TimescaleDB measurement data.\n");
        ovis_log(mylog, OVIS_LERROR, "SCHEMA: %s \n", is->schema);
        return ENOMEM;
}

static void close_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	struct timescale_store *is = _sh;

	if (!is)
		return;

	pthread_mutex_lock(&cfg_lock);
	LIST_REMOVE(is, entry);
	pthread_mutex_unlock(&cfg_lock);

	free(is->container);
	free(is->schema);
	PQfinish(is->conn);
	free(is);
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
		.config = config,
		.usage = usage,
		.type = LDMSD_PLUGIN_STORE,
		.constructor = constructor,
		.destructor = destructor,
	},
	.open = open_store,
	.store = store,
	.close = close_store,
};

static void __attribute__ ((constructor)) store_timescale_init();
static void store_timescale_init()
{
	LIST_INIT(&store_list);
}

static void __attribute__ ((destructor)) store_timescale_fini(void);
static void store_timescale_fini()
{
}

