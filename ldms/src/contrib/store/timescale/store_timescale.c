/* -*- c-basic-offset: 8 -*-
 *  * Copyright (c) 2012-2018 Open Grid Computing, Inc. All rights reserved.
 *
 *   * Confidential and proprietary
 *    */
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
#include </usr/include/libpq-fe.h>

struct timescale_store {
	struct ldmsd_store *store;
	void *ucontext;
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
static size_t measurement_limit = MEASUREMENT_LIMIT_DEFAULT;
static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(timescale_store_list, timescale_store) store_list;
static ldmsd_msg_log_f msglog;

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

/**
 *  * \brief Configuration
 *   */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
}

static const char *usage(struct ldmsd_plugin *self)
{
	return 0;
}

static ldmsd_store_handle_t
open_store(struct ldmsd_store *s, const char *container, const char *schema,
	   struct ldmsd_strgp_metric_list *metric_list, void *ucontext)
{
	struct timescale_store *is = NULL;

	is = malloc(sizeof(*is) + measurement_limit);
	if (!is)
		goto out;
	is->measurement_limit = measurement_limit;
	pthread_mutex_init(&is->lock, NULL);
	is->store = s;
	is->ucontext = ucontext;
	is->container = strdup(container);
	if (!is->container)
		goto err1;
	is->schema = strdup(schema);
	if (!is->schema)
		goto err2;
	is->job_mid = -1;
	is->comp_mid = -1;

        msglog(LDMSD_LERROR, "CONNECTION!\n");

        is->conn = PQconnectdb("user=postgres password=postgres hostaddr=172.16.0.190 port=5432 dbname=ldms");
        msglog(LDMSD_LERROR, "CONNECTION!\n");
        if (PQstatus(is->conn) == CONNECTION_BAD) {
		msglog(LDMSD_LERROR, "TimescaleDB connection failed!\n");
                goto err3;
        }


	pthread_mutex_lock(&cfg_lock);
	LIST_INSERT_HEAD(&store_list, is, entry);
	pthread_mutex_unlock(&cfg_lock);
	return is;
        
 err3:
        PQfinish(is->conn);
	free(is->schema);
 err2:
	free(is->container);
 err1:
	free(is);
 out:
	return NULL;
}

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
store(ldmsd_store_handle_t _sh, ldms_set_t set, int *metric_arry, size_t metric_count)
{
        //msglog(LDMSD_LERROR, "ENTER STORE!\n");
	struct timescale_store *is = _sh;
	struct ldms_timestamp timestamp;
	int i;
	int rc = 0;
	size_t cnt_insert, cnt_create, off_insert, off_create;
	char *measurement_create, *measurement_insert;
        enum ldms_value_type metric_type;
	if (!is)
		return EINVAL;

	pthread_mutex_lock(&is->lock);
	if (is->job_mid < 0) {
		rc = init_store(is, set, metric_arry, metric_count);
		if (rc)
			goto err;
	}

	measurement_create = is->measurement;
       
        //msglog(LDMSD_LERROR, "1111111111111111\n");        
 
        cnt_create = snprintf(measurement_create, is->measurement_limit,
                   "CREATE TABLE IF NOT EXISTS %s(",
                   is->schema);
        off_create = cnt_create; 

        //msglog(LDMSD_LERROR, "%d \n", metric_count);
        //msglog(LDMSD_LERROR, "%s \n", measurement_create);
        int comma = 0;
        for (i = 0; i < metric_count; i++) {
                //msglog(LDMSD_LERROR, "ENTER LOOP!\n");
                //msglog(LDMSD_LERROR, "metric_count %d \n", i);
                metric_type = ldms_metric_type_get(set, metric_arry[i]);
                //msglog(LDMSD_LERROR, "AFTER GETTING METRIC TYPE!\n");
                if (metric_type > LDMS_V_CHAR_ARRAY) {
                        msglog(LDMSD_LERROR,
                               "The metric %s:%s of type %s is not supported by "
                               "TimescaleDB and is being ignored.\n",
                               is->schema,
                               ldms_metric_name_get(set, metric_arry[i]),
                               ldms_metric_type_to_str(metric_type));
                        continue;
                }
                if (comma) {
                        if (off_create > is->measurement_limit - 16)
                                goto err;
                        measurement_create[off_create++] = ',';
                } else
                        comma = 1;
                cnt_create = snprintf(&measurement_create[off_create], is->measurement_limit - off_create,
                               "%s ", is->metric_name[i]);
                off_create += cnt_create;

               if (metric_type < LDMS_V_F32){
                        cnt_create = snprintf(&measurement_create[off_create], is->measurement_limit - off_create, "DECIMAL");
               } else if (metric_type < LDMS_V_CHAR_ARRAY) {
                        cnt_create = snprintf(&measurement_create[off_create], is->measurement_limit - off_create, "DOUBLE PRECISION");
               } else
                        cnt_create = snprintf(&measurement_create[off_create], is->measurement_limit - off_create, "VARCHAR(255)");

               off_create += cnt_create;

        }
        cnt_create = snprintf(&measurement_create[off_create], is->measurement_limit - off_create, ",timestamp TIMESTAMPTZ)\0");
        off_create += cnt_create;        
 
        PGresult *res = PQexec(is->conn, measurement_create);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                msglog(LDMSD_LERROR, "Create table error! with sql %s \n", measurement_create);
                PQclear(res);
                PQfinish(is->conn);
        }

        //msglog(LDMSD_LERROR, "2222222222222222\n");

        measurement_insert = is->measurement;
        cnt_insert = snprintf(measurement_insert, is->measurement_limit,
                   "INSERT INTO %s VALUES(",
                   is->schema);

        off_insert = cnt_insert;

        //msglog(LDMSD_LERROR, "%d \n", metric_count);
        //msglog(LDMSD_LERROR, "%s \n", measurement_insert);
        comma = 0;
	for (i = 0; i < metric_count; i++) {
                //msglog(LDMSD_LERROR, "metric_count %d \n", i);
		metric_type = ldms_metric_type_get(set, metric_arry[i]);
		if (metric_type > LDMS_V_CHAR_ARRAY) {
			msglog(LDMSD_LERROR,
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
	long long int ts =  ((long long)timestamp.sec * 1000000000L)
		+ ((long long)timestamp.usec * 1000L);

        char str[100] = {0};
        //itoa(timestamp.sec, str, 10);
        sprintf(str, "%d", timestamp.sec);
        char command[150];
        strcpy(command, "date '+%Y-%m-%d %H:%M:%S+08' -d @");
        strcat(command, str);
        FILE *fp;
        char buffer[50];
        fp=popen(command, "r");
        fgets(buffer, sizeof(buffer), fp);
        //buffer[23] = '\0';
        pclose(fp);       

	cnt_insert = snprintf(&measurement_insert[off_insert], is->measurement_limit - off_insert, ",'%s')\0", buffer);
	off_insert += cnt_insert;

        res = PQexec(is->conn, measurement_insert);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		msglog(LDMSD_LERROR, "Insert table error! with sql %s \n", measurement_insert);
		PQclear(res);
                PQfinish(is->conn);
        } 
        msglog(LDMSD_LERROR, "Insert succeed!\n");
	pthread_mutex_unlock(&is->lock);
	return 0;
err:
	pthread_mutex_unlock(&is->lock);

	msglog(LDMSD_LERROR, "Overflow formatting TimescaleDB measurement data.\n");
        msglog(LDMSD_LERROR, "SCHEMA: %s \n", is->schema);
	return ENOMEM;
}

static int flush_store(ldmsd_store_handle_t _sh)
{
	return 0;
}

static void close_store(ldmsd_store_handle_t _sh)
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

static void *get_ucontext(ldmsd_store_handle_t _sh)
{
	struct timescale_store *is = _sh;
	return is->ucontext;
}

static struct ldmsd_store store_timescale = {
	.base = {
		.name = "timescale",
		.term = term,
		.config = config,
		.usage = usage,
		.type = LDMSD_PLUGIN_STORE,
	},
	.open = open_store,
	.get_context = get_ucontext,
	.store = store,
	.flush = flush_store,
	.close = close_store,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &store_timescale.base;
}

static void __attribute__ ((constructor)) store_timescale_init();
static void store_timescale_init()
{
	LIST_INIT(&store_list);
}

static void __attribute__ ((destructor)) store_timescale_fini(void);
static void store_timescale_fini()
{
}

