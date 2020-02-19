/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2018 Open Grid Computing, Inc. All rights reserved.
 *
 * Confidential and proprietary
 */

/**
 * \file store_template.c
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

#include "ldmsd.h"
#include "ldmsd_store.h"

#define INST(x) ((ldmsd_plugin_inst_t)(x))
#define INST_LOG(inst, lvl, fmt, ...) \
		ldmsd_log((lvl), "%s: " fmt, INST(inst)->inst_name, \
								##__VA_ARGS__)

typedef struct store_influx_inst_s *store_influx_inst_t;
struct store_influx_inst_s {
	struct ldmsd_plugin_inst_s base;

	char host_port[256+8];	/* hostname:port_no for influxdb */
				/* 255 FQDN MAX + 8 ':'port */
	char *schema;
	char container[512];
	pthread_mutex_t lock;
	int job_mid;
	int comp_mid;
	char **metric_name;
	int metric_count;
	CURL *curl;
	char measurement[4096];
	char url[4096];
};


/* ============== Internal Functions ================= */

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


/* ============== Store Plugin APIs ================= */

static int
store_influx_close(ldmsd_plugin_inst_t pi);

static int
store_influx_open(ldmsd_plugin_inst_t pi, ldmsd_strgp_t strgp)
{
	/* Perform `open` operation */
	int i, rc;
	store_influx_inst_t inst = (void*)pi;
	ldmsd_strgp_metric_t ent;

	inst->schema = strdup(strgp->schema);
	if (!inst->schema) {
		rc = errno;
		goto err;
	}
	/* metric list in strgp has already been populated */
	inst->metric_count = strgp->metric_count;
	inst->metric_name = calloc(sizeof(char *), strgp->metric_count);
	if (!inst->metric_name) {
		rc = errno;
		goto err;
	}
	i = 0;
	TAILQ_FOREACH(ent, &strgp->metric_list, entry) {
		char *name = strdup(ent->name);
		if (!name) {
			rc = ENOMEM;
			goto err;
		}
		inst->metric_name[i] = fixup(name);
		i++;
	}
	inst->job_mid = -1;
	inst->comp_mid = -1;

	inst->curl = curl_easy_init();
	if (!inst->curl) {
		rc = errno;
		goto err;
	}

	return 0;

 err:
	store_influx_close(pi); /* undo the open */
	return rc;
}

static int
store_influx_close(ldmsd_plugin_inst_t pi)
{
	/* Perform `close` operation -- undo the `open` */
	int i;
	store_influx_inst_t inst = (void*)pi;
	if (inst->curl) {
		curl_easy_cleanup(inst->curl);
		inst->curl = NULL;
	}
	if (inst->schema) {
		free(inst->schema);
		inst->schema = NULL;
	}
	if (inst->metric_name) {
		for (i = 0; i < inst->metric_count; i++) {
			free(inst->metric_name[i]);
		}
		free(inst->metric_name);
		inst->metric_name = NULL;
	}
	return 0;
}

static int
store_influx_flush(ldmsd_plugin_inst_t pi)
{
	/* do nothing */
	return 0;
}

static int
store_influx_store(ldmsd_plugin_inst_t pi, ldms_set_t set, ldmsd_strgp_t strgp)
{
	/* `store` data from `set` into the store */
	store_influx_inst_t inst = (void*)pi;
	struct ldms_timestamp timestamp;
	int i;
	size_t cnt, off;
	char *measurement;

	/* CONTINUE HERE */
	pthread_mutex_lock(&inst->lock);

	curl_easy_setopt(inst->curl, CURLOPT_URL, inst->url);

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/influx");

	measurement = inst->measurement;
	cnt = snprintf(measurement, 4096,
		       "%s,job_id=%lui,component_id=%lui ",
		       inst->schema,
		       ldms_metric_get_u64(set, inst->job_mid),
		       ldms_metric_get_u64(set, inst->comp_mid));
	off = cnt;

	enum ldms_value_type metric_type;
	influx_value_set_fn fn;

	int comma = 0;
	int mid;
	for (i = 0; i < strgp->metric_count; i++) {
		mid = strgp->metric_arry[i];
		if (mid == inst->job_mid)
			continue;
		if (mid == inst->comp_mid)
			continue;
		if (comma) {
			if (off > 4096 - 16)
				goto err;
			measurement[off++] = ',';
		} else
			comma = 1;
		cnt = snprintf(&measurement[off], 4096 - off,
			       "%s=", inst->metric_name[i]);
		off += cnt;
		if (off >= 4096)
			goto err;

		metric_type = ldms_metric_type_get(set, mid);
		if (metric_type <= LDMS_V_CHAR_ARRAY) {
			fn = influx_value_set[metric_type];
			if (fn(measurement, &off, 4096, set, mid))
				goto err;
		} else {
			INST_LOG(inst, LDMSD_LINFO,
				 "A metric of type %d inst not supported by "
				 "InfluxDB, ignoring '%s'",
				 metric_type, ldms_metric_name_get(set, mid));
		}
	}
	timestamp = ldms_transaction_timestamp_get(set);
	long long int ts =  ((long long)timestamp.sec * 1000000000L)
			    + ((long long)timestamp.usec * 1000L);
	cnt = snprintf(&measurement[off], 4096 - off, " %lld", ts);
	off += cnt;
	if (off >= 4096)
		goto err;

	curl_easy_setopt(inst->curl, CURLOPT_POSTFIELDS, measurement);
	curl_easy_setopt(inst->curl, CURLOPT_POSTFIELDSIZE, off);
	curl_easy_setopt(inst->curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_perform(inst->curl);
	curl_slist_free_all(headers);
	pthread_mutex_unlock(&inst->lock);
	return 0;
err:
	pthread_mutex_unlock(&inst->lock);
	INST_LOG(inst, LDMSD_LERROR,
		 "Overflow formatting InfluxDB measurement data.\n");
	return ENOMEM;
}

/* ============== Common Plugin APIs ================= */

static const char *
store_influx_desc(ldmsd_plugin_inst_t pi)
{
	return "store_influx - store_influx store plugin";
}

static char *_help = "\
store_influx configuration synopsis:\n\
    config name=INST [COMMON_STORE_OPTIONS] host_port=<HOSTNAME>':'<PORT>\n\
           [container=CONTAINER_NAME]\n\
\n\
Option descriptions\n\
    host_post   The <HOSTNAME>:<PORT> (e.g. somehost:123) specifying hostname\n\
                and port to communicate to Influx DB.\n\
    container   The name of the container (default: INST -- the same as the\n\
                plugin instance name).\n\
";

static const char *
store_influx_help(ldmsd_plugin_inst_t pi)
{
	return _help;
}

static const char *
json_attr_find_str(json_entity_t json, char *key)
{
	json_entity_t value = json_value_find(json, key);
	if (!key)
		return NULL;
	return value->value.str_->str;
}

static int
store_influx_config(ldmsd_plugin_inst_t pi, json_entity_t json, char *ebuf, int ebufsz)
{
	store_influx_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;
	const char *value;
	int rc, len;

	pthread_mutex_lock(&inst->lock);
	ebuf[0] = '\0';
	rc = store->base.config(pi, json, ebuf, ebufsz);
	if (rc) /* ebuf has already been populated */
		goto err;

	/* Plugin-specific config here */
	value = json_attr_find_str(json, "host_port");
	if (!value) {
		rc = EINVAL;
		snprintf(ebuf, ebufsz, "The 'host_port' keyword is required.\n");
		goto err;
	}
	len = snprintf(inst->host_port, sizeof(inst->host_port), "%s", value);
	if (len >= sizeof(inst->host_port)) {
		rc = ENAMETOOLONG;
		snprintf(ebuf, ebufsz, "The 'host_port' is too long.\n");
		goto err;
	}
	value = json_attr_find_str(json, "container");
	if (!value) {
		value = pi->inst_name;
	}
	len = snprintf(inst->container, sizeof(inst->container), "%s", value);
	if (len >= sizeof(inst->container)) {
		rc = ENAMETOOLONG;
		snprintf(ebuf, ebufsz, "Container name too long.\n");
		goto err;
	}
	len = snprintf(inst->url, sizeof(inst->url), "http://%s/write?db=%s",
					inst->host_port, inst->container);
	if (len >= sizeof(inst->url)) {
		rc = ENAMETOOLONG;
		snprintf(ebuf, ebufsz, "URL too long\n");
		goto err;
	}
	goto out;
 err:
	if (ebuf[0])
		INST_LOG(inst, LDMSD_LERROR, "%s", ebuf);
 out:
	pthread_mutex_unlock(&inst->lock);
	return rc;
}

static void
store_influx_del(ldmsd_plugin_inst_t pi)
{
	store_influx_inst_t inst = (void*)pi;
	pthread_mutex_destroy(&inst->lock);
}

static int
store_influx_init(ldmsd_plugin_inst_t pi)
{
	store_influx_inst_t inst = (void*)pi;
	ldmsd_store_type_t store = (void*)inst->base.base;

	/* override store operations */
	store->open = store_influx_open;
	store->close = store_influx_close;
	store->flush = store_influx_flush;
	store->store = store_influx_store;

	pthread_mutex_init(&inst->lock, NULL);
	return 0;
}

static
struct store_influx_inst_s __inst = {
	.base = {
		.version     = LDMSD_PLUGIN_VERSION_INITIALIZER,
		.type_name   = LDMSD_STORE_TYPENAME,
		.plugin_name = "store_influx",

                /* Common Plugin APIs */
		.desc   = store_influx_desc,
		.help   = store_influx_help,
		.init   = store_influx_init,
		.del    = store_influx_del,
		.config = store_influx_config,

	},
	/* plugin-specific data initialization (for new()) here */
};

ldmsd_plugin_inst_t new()
{
	store_influx_inst_t inst = malloc(sizeof(*inst));
	if (inst)
		*inst = __inst;
	return &inst->base;
}

__attribute__((constructor))
static void __init__()
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
}
