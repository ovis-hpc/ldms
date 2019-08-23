/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2018 Open Grid Computing, Inc. All rights reserved.

 * Confidential and proprietary
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
#include <curl/curl.h>
#include "ldms.h"
#include "ldmsd.h"

static char host_port[64];	/* hostname:port_no for influxdb */
struct influx_store {
	struct ldmsd_store *store;
	void *ucontext;
	char *host_port;
	char *schema;
	char *container;
	pthread_mutex_t lock;
	int job_mid;
	int comp_mid;
	char **metric_name;
	CURL *curl;
	char measurement[4096];
	LIST_ENTRY(influx_store) entry;
};

static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;
LIST_HEAD(influx_store_list, influx_store) store_list;
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
	size_t cnt = snprintf(&line[*line_off], line_len, "%hhui", ldms_metric_get_u8(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s8_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%hhdi", ldms_metric_get_s8(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_u16_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%hui", ldms_metric_get_u16(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s16_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%hdi", ldms_metric_get_s16(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_str_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "\"%s\"", ldms_metric_array_get_str(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_u32_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%ui", ldms_metric_get_u32(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s32_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%di", ldms_metric_get_s32(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_u64_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%lui", ldms_metric_get_u64(s, i));
	return update_offset(cnt, line_off, line_len);
}
static int set_s64_fn(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i)
{
	size_t cnt = snprintf(&line[*line_off], line_len, "%ldi", ldms_metric_get_s64(s, i));
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

typedef int (*influx_value_set_fn)(char *line, size_t *line_off, size_t line_len, ldms_set_t s, int i);
influx_value_set_fn influx_value_set[] = {
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
 * \brief Configuration
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	pthread_mutex_lock(&cfg_lock);

	value = av_value(avl, "host_port");
	if (!value) {
		msglog(LDMSD_LERROR, "The 'host_port' keyword is required.\n");
		return EINVAL;
	}
	strncpy(host_port, value, sizeof(host_port));

	pthread_mutex_unlock(&cfg_lock);
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "    config name=influx host_port=<hostname>':'<port_no>\n";
}

static ldmsd_store_handle_t
open_store(struct ldmsd_store *s, const char *container, const char *schema,
	   struct ldmsd_strgp_metric_list *metric_list, void *ucontext)
{
	struct influx_store *is = NULL;

	is = calloc(1, sizeof(*is));
	if (!is)
		goto out;
	pthread_mutex_init(&is->lock, NULL);
	is->store = s;
	is->ucontext = ucontext;
	is->container = strdup(container);
	if (!is->container)
		goto err1;
	is->schema = strdup(schema);
	if (!is->schema)
		goto err2;
	is->host_port = strdup(host_port);
	if (!is->host_port)
		goto err3;
	is->job_mid = -1;
	is->comp_mid = -1;

	is->curl = curl_easy_init();
	if (!is->curl)
		goto err3;
	pthread_mutex_lock(&cfg_lock);
	LIST_INSERT_HEAD(&store_list, is, entry);
	pthread_mutex_unlock(&cfg_lock);
	return is;
 err3:
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
		default:
			break;
		}
		s++;
	}
	return name;
}

static int init_store(struct influx_store *is, ldms_set_t set, int *mids, int count)
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
/*
meminfo,job_id=12344i,component_id=100000i MemTotal=263518917i,MemFree=97231768i,MemAvailable=245401372i,Buffers=16i,Cached=145691728i,SwapCached=178148i,Active=64993868i,Inactive=94022496i,Active_anon_=12229548i,Inactive_anon_=1872492i,Active_file_=52764320i,Inactive_file_=92150004i,Unevictable=0i,Mlocked=0i,SwapTotal=4194300i,SwapFree=3335952i,Dirty=2680i,Writeback=0i,AnonPages=13141804i,Mapped=22500768i,Shmem=789460i,Slab=5957340i,SReclaimable=5103028i,SUnreclaim=854312i,KernelStack=36096i,PageTables=218840i,NFS_Unstable=0i,Bounce=0i,WritebackTmp=0i,CommitLimit=135953756i,Committed_AS=31471980i,VmallocTotal=34359738367i,VmallocUsed=0i,VmallocChunk=0i,HardwareCorrupted=0i,AnonHugePages=10162176i,ShmemHugePages=0i,ShmemPmdMapped=0i,CmaTotal=0i,CmaFree=0i,HugePages_Total=0i,HugePages_Free=0i,HugePages_Rsvd=0i,HugePages_Surp=0,Hugepagesize=2048i,DirectMap4k=2261260i,DirectMap2M=169596928i,DirectMap1G=98566144i 1566489644000000000
*/
static int
store(ldmsd_store_handle_t _sh, ldms_set_t set, int *metric_arry, size_t metric_count)
{
	struct influx_store *is = _sh;
	struct ldms_timestamp timestamp;
	int i;
	int rc = 0;
	size_t cnt, off;
	char *measurement;
	if (!is)
		return EINVAL;

	pthread_mutex_lock(&is->lock);
	if (is->job_mid < 0) {
		rc = init_store(is, set, metric_arry, metric_count);
		if (rc)
			goto err;
	}

	char url[128];
	snprintf(url, sizeof(url), "http://%s/write?db=%s", is->host_port, is->container);
	curl_easy_setopt(is->curl, CURLOPT_URL, url);

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/influx");

	measurement = is->measurement;
	cnt = snprintf(measurement, 4096,
		       "%s,job_id=%lui,component_id=%lui ",
		       is->schema,
		       ldms_metric_get_u64(set, is->job_mid),
		       ldms_metric_get_u64(set, is->comp_mid));
	off = cnt;

	enum ldms_value_type metric_type;

	int comma = 0;
	for (i = 0; i < metric_count; i++) {
		if (metric_arry[i] == is->job_mid)
			continue;
		if (metric_arry[i] == is->comp_mid)
			continue;
		if (comma) {
			if (off > 4096 - 16)
				goto err;
			measurement[off++] = ',';
		} else
			comma = 1;
		cnt = snprintf(&measurement[off], 4096 - off, "%s=", is->metric_name[i]);
		off += cnt;

		metric_type = ldms_metric_type_get(set, metric_arry[i]);
		if (metric_type < LDMS_V_CHAR_ARRAY || metric_type == LDMS_V_CHAR_ARRAY) {
			if (influx_value_set[metric_type](measurement, &off, 4096, set, metric_arry[i]))
				goto err;
		} else {
			msglog(LDMSD_LINFO, "A metric of type %d is not supported by InfluxDB, ignoring '%s'",
			       metric_type, ldms_metric_name_get(set, metric_arry[i]));
		}
	}
	timestamp = ldms_transaction_timestamp_get(set);
	long long int ts =  ((long long)timestamp.sec * 1000000000L)
		+ ((long long)timestamp.usec * 1000L);
	cnt = snprintf(&measurement[off], 4096 - off, " %lld", ts);
	off += cnt;

	curl_easy_setopt(is->curl, CURLOPT_POSTFIELDS, measurement);
	curl_easy_setopt(is->curl, CURLOPT_POSTFIELDSIZE, off);
	curl_easy_setopt(is->curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_perform(is->curl);
	curl_slist_free_all(headers);
	pthread_mutex_unlock(&is->lock);
	return 0;
err:
	pthread_mutex_unlock(&is->lock);
	msglog(LDMSD_LERROR, "Overflow formatting InfluxDB measurement data.\n");
	return ENOMEM;
}

static int flush_store(ldmsd_store_handle_t _sh)
{
	return 0;
}

static void close_store(ldmsd_store_handle_t _sh)
{
	struct influx_store *is = _sh;

	if (!is)
		return;

	pthread_mutex_lock(&cfg_lock);
	LIST_REMOVE(is, entry);
	pthread_mutex_unlock(&cfg_lock);

	free(is->container);
	free(is->schema);
	free(is);
}

static void *get_ucontext(ldmsd_store_handle_t _sh)
{
	struct influx_store *is = _sh;
	return is->ucontext;
}

static struct ldmsd_store store_influx = {
	.base = {
		.name = "influx",
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
	return &store_influx.base;
}

static void __attribute__ ((constructor)) store_influx_init();
static void store_influx_init()
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	LIST_INIT(&store_list);
}

static void __attribute__ ((destructor)) store_influx_fini(void);
static void store_influx_fini()
{
}
