/* -*- c-basic-offset: 8 -*-
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * Copyright 2022 Open Grid Computing, Inc.
 *
 * See the top-level COPYING file for details.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 */
#define _GNU_SOURCE
#include <sys/queue.h>
#include <ctype.h>
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
#include <avro.h>
#include <libserdes/serdes.h>
#include <libserdes/serdes-avro.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"
#include "ovis_log.h"

#define STORE_AVRO_KAFKA "store_avro_kafka"

typedef struct store_kafka_s {
        ovis_log_t log;
	pthread_mutex_t sk_lock;
	rd_kafka_conf_t *g_rd_conf;
	serdes_conf_t *g_serdes_conf;
	int g_serdes_encoding;
	char *g_topic_fmt;
} *store_kafka_t;

typedef struct aks_handle_s
{
	rd_kafka_conf_t *rd_conf;   /* The Kafka configuration */
	rd_kafka_t *rd;		    /* The Kafka handle */
	serdes_conf_t *serdes_conf; /* The serdes configuration */
	serdes_t *serdes;	    /* The serdes handle */
	enum {
		AKS_ENCODING_JSON = 1,
		AKS_ENCODING_AVRO = 2
	} encoding;
	char *topic_fmt;	   /* Format to use to create topic name from row */
	char *topic_name;
	store_kafka_t sf;

	struct rbt schema_tree;
} *aks_handle_t;

static const char *_help_str =
    "   config name=store_avro_kafka [path=JSON_FILE]\n"
    "       encoding=MODE"
    "            MODE is one of JSON or AVRO (default)."
    "       kafka_conf=PATH"
    "            Path to a file in Apache Kafka format containing key/value\n"
    "            pairs defining Kafka configuration properties. See\n"
    "            https://github.com/edenhill/librdkafka/blob/master/CONFIGURATION.md\n"
    "            for a list of supported properties. These configuration\n"
    "            properties will be applied across all connections to\n"
    "            Kafka brokers. These configuration properties can be\n"
    "            overriden by a storage policy (strgp) that is using\n"
    "            this plugin.\n"
    "       serdes_conf=PATH"
    "            Path to a file in Apache Kafka format containing key/value\n"
    "            pairs defining Avro Serdes configuration properties.\n"
    "\n"
    "    Using this plugin with a Storage Policy\n"
    "    ---------------------------------------\n"
    "    The `container` of the `strgp` that uses `store_avro_kafka` is a CSV list of\n"
    "    brokers (host or host:port). For example:\n"
    "\n"
    "    strgp_add name=kp plugin=store_avro_kafka container=localhost,br1.kf:9898 \\\n"
    "              decomposition=decomp.json\n"
    "";
static const char *usage(ldmsd_plug_handle_t handle)
{
	return _help_str;
}

static int schema_cmp(void *a, const void *b)
{
	return strcmp((char *)a, (char *)b);
}
static pthread_mutex_t schema_rbt_lock = PTHREAD_MUTEX_INITIALIZER;
static struct rbt schema_tree = { .comparator = schema_cmp };

struct schema_entry
{
	char *schema_name;
	serdes_schema_t *serdes_schema;	/* The serdes schema */
	ldms_schema_t ldms_schema;	/* The LDMS schema */
	struct rbn rbn;
};

static serdes_schema_t *
serdes_schema_find(aks_handle_t sh, char *schema_name,
		  ldms_schema_t lschema, ldmsd_row_t row)
{
	struct rbn *rbn;
	serdes_schema_t *previous_schema = NULL;
	serdes_schema_t *current_schema = NULL;
	struct schema_entry *entry;
	char *json_buf = NULL;
	size_t json_len;
	char errstr[512];
	int rc;

	pthread_mutex_lock(&schema_rbt_lock);
	/* Check if the schema is already cached in this plugin */
	rbn = rbt_find(&schema_tree, schema_name);
	if (rbn) {
		entry = container_of(rbn, struct schema_entry, rbn);
		current_schema = entry->serdes_schema;
		goto out;
	}
	entry = calloc(1, sizeof(*entry));
	if (!entry)
		goto out;

	/* Look up the schema by name in the registry.
           Name alone does not tell us if the schema matches this row, so we
           will still need to continue on and try pushing an updated schema.
           The registry should handle duplicates or schema evolution testing.
        */
	previous_schema = serdes_schema_get(sh->serdes,
                                            schema_name, -1,
                                            errstr, sizeof(errstr));

	/* Generate a new schema from the row specification and LDMS schema */
	rc = ldmsd_row_to_json_avro_schema(row, &json_buf, &json_len);
	if (rc)
		goto out;

        /* Push the generated schema to the registry */
        current_schema = serdes_schema_add(sh->serdes,
                                             schema_name, -1,
                                             json_buf, json_len,
                                             errstr, sizeof(errstr));
	if (!current_schema) {
		ovis_log(sh->sf->log, OVIS_LERROR, "%s\n", json_buf);
		ovis_log(sh->sf->log, OVIS_LERROR, "Error '%s' creating schema '%s'\n", errstr, schema_name);
		goto out;
	}

        /* Log information about which schema was used */
        if (previous_schema != NULL) {
                if (serdes_schema_id(current_schema)
                    == serdes_schema_id(previous_schema)) {
                        ovis_log(sh->sf->log, OVIS_LINFO,
                                 "Using existing id %d for schema name '%s'\n",
                                 serdes_schema_id(current_schema),
                                 schema_name);
                } else {
                        ovis_log(sh->sf->log, OVIS_LWARN,
                                 "Using replacement id %d for schema name '%s' (previous id %d)\n",
                                 serdes_schema_id(current_schema),
                                 schema_name,
                                 serdes_schema_id(previous_schema));
                        serdes_schema_destroy(previous_schema);
                }
        } else {
                ovis_log(sh->sf->log, OVIS_LINFO,
                         "Using brand new id %d for schema name '%s'\n",
                         serdes_schema_id(current_schema),
                         schema_name);
        }

        /* Cache the schema in this plugin */
	entry->serdes_schema = current_schema;
	entry->ldms_schema = lschema;
	entry->schema_name = strdup(schema_name);
	rbn_init(&entry->rbn, entry->schema_name);
	rbt_ins(&schema_tree, &entry->rbn);
out:
	pthread_mutex_unlock(&schema_rbt_lock);
	if (json_buf)
		free(json_buf);
	return current_schema;
}

static char *strip_whitespace(char *s)
{
	char *p;
	while (*s != '\0' && isspace(*s))
		s++;
	if (*s == '\0')
		return s;
	p = s;
	while (*p != '\0' && !isspace(*p))
		p++;
	if (*p != '\0')
		*p = '\0';
	return s;
}

static int parse_rd_conf_file(char *path, rd_kafka_conf_t *rd_conf, ovis_log_t log)
{
	char err_str[512];
	char line_buf[1024], *line;
	int rc = 0;
	rd_kafka_conf_res_t res;
	FILE *rd_f;

	/* Default broker URL */
	res = rd_kafka_conf_set(rd_conf, "bootstrap.servers", "localhost:9092",
				err_str, sizeof(err_str));
	if (res) {
		ovis_log(log, OVIS_LERROR, "Error '%s' setting the boostrap.servers default value.\n", err_str);
		return EINVAL;
	}
	rd_f = fopen(path, "r");
	if (!rd_f) {
		ovis_log(log, OVIS_LERROR, "Error %d opening '%s'.\n", errno, path);
		return errno;
	}
	while (NULL != (line = fgets(line_buf, sizeof(line_buf), rd_f))) {
		char *key, *value;
		/* Skip leading whitespace */
		while (*line != '\0' && isspace(*line))
			line++;
		if (*line == '\0')
			/* skip blank line */
			continue;
		if (*line == '#')
			/* skip comment line */
			continue;
		/* Tokenize based on '=' */
		char *saveptr;
		key = strtok_r(line, "=", &saveptr);
		if (!key) {
			ovis_log(log, OVIS_LERROR, "Ignoring mal-formed configuration line '%s'\n", line_buf);
			continue;
		}
		value = strtok_r(NULL, "=", &saveptr);
		if (!value) {
			ovis_log(log, OVIS_LERROR, "Ignoring mal-formed configuration line '%s'\n", line_buf);
			/* Ignore malformed line ... missing '=' */
			continue;
		}
		/* Strip whitespace from key and value */
		key = strip_whitespace(key);
		value = strip_whitespace(value);
		ovis_log(log, OVIS_LINFO, "Applying rd_conf key='%s', value='%s'\n", key, value);
		res = rd_kafka_conf_set(rd_conf, key, value, err_str, sizeof(err_str));
		if (res) {
			ovis_log(log, OVIS_LERROR, "Ignoring configuration key '%s'\n", err_str);
			continue;
		}
	}
	return rc;
}

static int parse_serdes_conf_file(char *path, serdes_conf_t *s_conf, ovis_log_t log)
{
	char err_str[512];
	char line_buf[1024], *line;
	int rc = 0;
	serdes_err_t res;

	FILE *rd_f = fopen(path, "r");
	if (!rd_f) {
		ovis_log(log, OVIS_LERROR, "Error %d opening '%s'.\n", errno, path);
		return errno;
	}
	while (NULL != (line = fgets(line_buf, sizeof(line_buf), rd_f))) {
		char *key, *value;
		/* Skip leading whitespace */
		while (*line != '\0' && isspace(*line))
			line++;
		if (*line == '\0')
			/* skip blank line */
			continue;
		if (*line == '#')
			/* skip comment line */
			continue;
		/* Tokenize based on '=' */
		char *saveptr;
		key = strtok_r(line, "=", &saveptr);
		if (!key) {
			ovis_log(log, OVIS_LERROR, "Ignoring mal-formed configuration line '%s'\n", line_buf);
			continue;
		}
		value = strtok_r(NULL, "=", &saveptr);
		if (!value) {
			ovis_log(log, OVIS_LERROR, "Ignoring mal-formed configuration line '%s'\n", line_buf);
			/* Ignore malformed line ... missing '=' */
			continue;
		}
		/* Strip whitespace from key and value */
		key = strip_whitespace(key);
		value = strip_whitespace(value);
		ovis_log(log, OVIS_LINFO, "Applying s_conf key='%s', value='%s'\n", key, value);
		res = serdes_conf_set(s_conf, key, value, err_str, sizeof(err_str));
		if (res) {
			ovis_log(log, OVIS_LERROR, "Ignoring serdes configuration key '%s'\n", err_str);
			continue;
		}
	}
	return rc;
}

static int config(ldmsd_plug_handle_t handle, struct attr_value_list *kwl,
		  struct attr_value_list *avl)
{
	int rc = 0;
	store_kafka_t sk = ldmsd_plug_ctxt_get(handle);
	char *path, *encoding, *topic;
	char err_str[512];

	pthread_mutex_lock(&sk->sk_lock);

	if (sk->g_rd_conf) {
		ovis_log(sk->log, OVIS_LERROR, "reconfiguration is not supported\n");
		rc = EINVAL;
		goto out;
	}

	sk->g_rd_conf = rd_kafka_conf_new();
	if (!sk->g_rd_conf) {
		rc = errno;
		ovis_log(sk->log, OVIS_LERROR, "rd_kafka_conf_new() failed %d\n", rc);
		goto out;
	}

	sk->g_serdes_conf = serdes_conf_new(err_str, sizeof(err_str),
			      /* Default URL */
			      "schema.registry.url", "http://localhost:8081");
	if (!sk->g_serdes_conf) {
		rc = EINVAL;
		ovis_log(sk->log, OVIS_LERROR, "serdes_conf_new failed '%s'\n", err_str);
		goto out;
	}

	path = av_value(avl, "kafka_conf");
	if (path) {
		rc = parse_rd_conf_file(path, sk->g_rd_conf, sk->log);
		if (rc) {
			ovis_log(sk->log, OVIS_LERROR, "Error %d parsing the Kafka configuration file '%s'", rc, path);
		}
	}
	path = av_value(avl, "serdes_conf");
	if (path) {
		rc = parse_serdes_conf_file(path, sk->g_serdes_conf, sk->log);
		if (rc) {
			ovis_log(sk->log, OVIS_LERROR, "Error %d parsing the Kafka configuration file '%s'", rc, path);
		}
	}
	encoding = av_value(avl, "encoding");
	if (encoding) {
		if (0 == strcasecmp(encoding, "avro")) {
			sk->g_serdes_encoding = AKS_ENCODING_AVRO;
		} else if (0 == strcasecmp(encoding, "json")) {
			sk->g_serdes_encoding = AKS_ENCODING_JSON;
		} else {
			ovis_log(sk->log, OVIS_LERROR, "Ignoring unrecognized serialization encoding '%s'\n", encoding);
		}
	} else {
		if (0 == sk->g_serdes_encoding)
			sk->g_serdes_encoding = AKS_ENCODING_AVRO;
	}
	topic = av_value(avl, "topic");
	if (topic) {
		char *tmp;
		sk->g_topic_fmt = strdup(topic);
		/* Strip any enclosing \" */
		while (*sk->g_topic_fmt != '\0' && *sk->g_topic_fmt == '\"')
			sk->g_topic_fmt++;
		tmp = sk->g_topic_fmt;
		while (*tmp != '\0' && *tmp != '\"')
			tmp++;
		if (*tmp == '\"')
			*tmp = '\0';
	} else {
		/* The default is the schema name */
		sk->g_topic_fmt = strdup("%S");
	}
out:
	pthread_mutex_unlock(&sk->sk_lock);
	return rc;
}

/* protected by strgp->lock */
static void close_store(ldmsd_plug_handle_t handle, ldmsd_store_handle_t _sh)
{
	/* This is called when strgp is stopped to clean up resources */
	aks_handle_t sh = _sh;
	if (sh->rd) {
		rd_kafka_destroy(sh->rd);
	}
	if (sh->rd_conf) {
		rd_kafka_conf_destroy(sh->rd_conf);
	}
	free(sh);
}

static aks_handle_t __handle_new(ldmsd_plug_handle_t handle, ldmsd_strgp_t strgp)
{
	store_kafka_t sk = ldmsd_plug_ctxt_get(handle);
	char err_str[512];
	rd_kafka_conf_res_t res;

	aks_handle_t sh = calloc(1, sizeof(*sh));
	if (!sh) {
		ovis_log(sk->log, OVIS_LERROR, "Memory allocation failure @%s:%d\n", __func__, __LINE__);
		goto err_0;
	}
        sh->sf = sk;
	rbt_init(&sh->schema_tree, schema_cmp);
	sh->encoding = sk->g_serdes_encoding;
	sh->topic_fmt = strdup(sk->g_topic_fmt);
	if (!sh->topic_fmt)
		goto err_1;

	sh->rd_conf = rd_kafka_conf_dup(sk->g_rd_conf);
	if (!sh->rd_conf)
		goto err_1;

	sh->serdes_conf = serdes_conf_copy(sk->g_serdes_conf);
	if (!sh->serdes_conf) {
		ovis_log(sk->log, OVIS_LERROR, "%s creating serdes configuration\n", err_str);
		goto err_2;
	}

	sh->serdes = serdes_new(sh->serdes_conf, err_str, sizeof(err_str));
	if (!sh->serdes) {
		ovis_log(sk->log, OVIS_LERROR, "%s creating serdes\n", err_str);
		goto err_3;
	}

	/* strgp->container is the CSV list of brokers */
	if (strgp->container && strgp->container[0] != '\0') {
		const char *param = "bootstrap.servers";
		res = rd_kafka_conf_set(sh->rd_conf, param,
					strgp->container, err_str, sizeof(err_str));
		if (res != RD_KAFKA_CONF_OK) {
			errno = EINVAL;
			ovis_log(sk->log, OVIS_LERROR, "rd_kafka_conf_set() error: %s\n", err_str);
			goto err_2;
		}
	}

	sh->rd = rd_kafka_new(RD_KAFKA_PRODUCER, sh->rd_conf, err_str, sizeof(err_str));
	if (!sh->rd) {
		ovis_log(sk->log, OVIS_LERROR, "rd_kafka_new() error: %s\n", err_str);
		goto err_2;
	}
	sh->rd_conf = NULL; /* rd_kafka_new consumed and freed the conf */

	return sh;

err_3:
	serdes_conf_destroy(sh->serdes_conf);
err_2:
	rd_kafka_conf_destroy(sh->rd_conf);
err_1:
	free(sh->topic_fmt);
	free(sh);
err_0:
	return NULL;
}

static int set_avro_value_from_col(avro_value_t *col_value,
				   ldmsd_col_t col)
{
	int rc, idx;
	struct ldms_timestamp ts;
	avro_value_t val, item_val;
	long ms;
	char char_str[2];
	size_t item_idx;
	enum avro_type_t t = avro_value_get_type(col_value);
	switch (col->type) {
	case LDMS_V_TIMESTAMP:
		switch (t) {
		case AVRO_INT32:
			/* Assume that this is only the seconds portion of
			 * the timestamp
			 */
			ts = ldms_mval_get_ts(col->mval);
			rc = avro_value_set_int(col_value, ts.sec);
			break;
		case AVRO_INT64:
			/* This is assumed to be the Avro logical
			 * type 'timestamp-millis'
			 */
			ts = ldms_mval_get_ts(col->mval);
			ms = ((long)ts.sec * 1000) + (ts.usec / 1000);
			rc = avro_value_set_long(col_value, ms);
			break;
		case AVRO_RECORD:
			/* Expect sec, usec Avro record definition */
			ts = ldms_mval_get_ts(col->mval);
			rc = avro_value_get_by_name(col_value, "sec", &val, NULL);
			assert(!rc);
			rc = avro_value_set_int(&val, (long)ts.sec);
			assert(!rc);
			rc = avro_value_get_by_name(col_value, "usec", &val, NULL);
			assert(!rc);
			rc = avro_value_set_int(&val, (long)ts.usec);
			assert(!rc);
			break;
		default:
			assert(0 == "Unrecognized Avro type to LDMS_V_TIMESTAMP conversion");
			break;
		}
		break;
	case LDMS_V_CHAR:
		char_str[0] = ldms_mval_get_char(col->mval);
		char_str[1] = '\0';
		rc = avro_value_set_string(col_value, char_str);
		break;
	case LDMS_V_U8:
		rc = avro_value_set_int(col_value,
				(int)ldms_mval_get_u8(col->mval));
		break;
	case LDMS_V_S8:
		rc = avro_value_set_int(col_value,
				(int)ldms_mval_get_s8(col->mval));
		break;
	case LDMS_V_U16:
		rc = avro_value_set_int(col_value,
				(int)ldms_mval_get_u16(col->mval));
		break;
	case LDMS_V_S16:
		rc = avro_value_set_int(col_value,
				(int)ldms_mval_get_s16(col->mval));
		break;
	case LDMS_V_U32:
		rc = avro_value_set_int(col_value,
				(int)ldms_mval_get_u32(col->mval));
		break;
	case LDMS_V_S32:
		rc = avro_value_set_int(col_value,
				(int)ldms_mval_get_s32(col->mval));
		break;
	case LDMS_V_U64:
		rc = avro_value_set_long(col_value,
				(long)ldms_mval_get_u64(col->mval));
		break;
	case LDMS_V_S64:
		rc = avro_value_set_long(col_value,
				(long)ldms_mval_get_s64(col->mval));
		break;
	case LDMS_V_F32:
		rc = avro_value_set_float(col_value,
				ldms_mval_get_float(col->mval));
		break;
	case LDMS_V_D64:
		rc = avro_value_set_double(col_value,
				ldms_mval_get_double(col->mval));
		break;
	case LDMS_V_CHAR_ARRAY:
		rc = avro_value_set_string(col_value,
				ldms_mval_array_get_str(col->mval));
		break;
	case LDMS_V_S8_ARRAY:
		for (idx = 0; idx < col->array_len; idx++) {
			rc = avro_value_append(col_value, &item_val, &item_idx);
			if (rc)
				break;
			rc = avro_value_set_int(&item_val,
					(int)ldms_mval_array_get_s8(col->mval, idx));
		}
		break;
	case LDMS_V_U8_ARRAY:
		for (idx = 0; idx < col->array_len; idx++) {
			rc = avro_value_append(col_value, &item_val, &item_idx);
			if (rc)
				break;
			rc = avro_value_set_int(&item_val,
					(unsigned int)ldms_mval_array_get_u8(col->mval, idx));
		}
		break;
	case LDMS_V_U16_ARRAY:
		for (idx = 0; idx < col->array_len; idx++) {
			rc = avro_value_append(col_value, &item_val, &item_idx);
			if (rc)
				break;
			rc = avro_value_set_int(&item_val,
					(unsigned int)ldms_mval_array_get_u16(col->mval, idx));
		}
		break;
	case LDMS_V_S16_ARRAY:
		for (idx = 0; idx < col->array_len; idx++) {
			rc = avro_value_append(col_value, &item_val, &item_idx);
			if (rc)
				break;
			rc = avro_value_set_int(&item_val,
					(int)ldms_mval_array_get_s16(col->mval, idx));
		}
		break;
	case LDMS_V_U32_ARRAY:
		for (idx = 0; idx < col->array_len; idx++) {
			rc = avro_value_append(col_value, &item_val, &item_idx);
			if (rc)
				break;
			rc = avro_value_set_long(&item_val,
					(long)ldms_mval_array_get_u32(col->mval, idx));
		}
		break;
	case LDMS_V_S32_ARRAY:
		for (idx = 0; idx < col->array_len; idx++) {
			rc = avro_value_append(col_value, &item_val, &item_idx);
			if (rc)
				break;
			rc = avro_value_set_int(&item_val,
					ldms_mval_array_get_s64(col->mval, idx));
		}
		break;
	case LDMS_V_U64_ARRAY:
		for (idx = 0; idx < col->array_len; idx++) {
			rc = avro_value_append(col_value, &item_val, &item_idx);
			if (rc)
				break;
			rc = avro_value_set_long(&item_val,
					(long)ldms_mval_array_get_u64(col->mval, idx));
		}
		break;
	case LDMS_V_S64_ARRAY:
		for (idx = 0; idx < col->array_len; idx++) {
			rc = avro_value_append(col_value, &item_val, &item_idx);
			if (rc)
				break;
			rc = avro_value_set_long(&item_val,
					(long)ldms_mval_array_get_u64(col->mval, idx));
		}
		break;
	case LDMS_V_F32_ARRAY:
		for (idx = 0; idx < col->array_len; idx++) {
			rc = avro_value_append(col_value, &item_val, &item_idx);
			if (rc)
				break;
			rc = avro_value_set_float(&item_val,
					ldms_mval_array_get_float(col->mval, idx));
		}
		break;
	case LDMS_V_D64_ARRAY:
		for (idx = 0; idx < col->array_len; idx++) {
			rc = avro_value_append(col_value, &item_val, &item_idx);
			if (rc)
				break;
			rc = avro_value_set_double(&item_val,
					ldms_mval_array_get_double(col->mval, idx));
		}
		break;
	case LDMS_V_LIST:
	case LDMS_V_LIST_ENTRY:
	case LDMS_V_RECORD_TYPE:
	case LDMS_V_RECORD_INST:
	case LDMS_V_RECORD_ARRAY:
	default:
		rc = ENOTSUP;
		break;
	}
	return rc;
}

static int serialize_columns_of_row(avro_schema_t schema,
                                    ldmsd_row_t row, avro_value_t *avro_row, ovis_log_t log)
{
	int rc, i;
	ldmsd_col_t col;
	avro_value_t avro_col;

	for (i = 0; i < row->col_count; i++) {
		char *avro_name;
		col = &row->cols[i];
		avro_name = ldmsd_avro_name_get(col->name);
		rc = avro_value_get_by_name(avro_row, avro_name,
					    &avro_col, NULL);
		free(avro_name);
		if (rc) {
			ovis_log(log, OVIS_LERROR, "Error %d retrieving '%s' from '%s' schema\n",
                                  rc, col->name, avro_schema_name(schema));
			continue;
		}
		rc = set_avro_value_from_col(&avro_col, col);
	}
#ifdef AVRO_KAFKA_DEBUG
	char *json_buf;
	avro_value_to_json(avro_row, 0, &json_buf);
	fprintf(stderr, "%s\n", json_buf);
	free(json_buf);
#endif
	return 0;
}

typedef struct str_s {
	char *buf_ptr;
	char *cur_ptr;
	size_t cur_pos;
	size_t buf_len;
} *str_t;

static char valid_topic_char(char c)
{
	if (isalnum(c))
		return c;
	if (c == '.' || c == '_' || c == '-')
		return c;
	return '.';
}

static char *str_cat_c(str_t str, char c)
{
	if (str->cur_pos >= str->buf_len) {
		str->buf_len += 256;
		str->buf_ptr = realloc(str->buf_ptr, str->buf_len);
		if (!str->buf_ptr)
			return NULL;
		str->cur_ptr = &str->buf_ptr[str->cur_pos];
	}
	*str->cur_ptr = valid_topic_char(c);
	str->cur_ptr++;
	str->cur_pos++;
	str->buf_ptr[str->cur_pos] = '\0';
	return str->buf_ptr;
}

static char *str_cat_s(str_t str, const char* s)
{
	char *p = NULL;
	while (*s != '\0') {
		p = str_cat_c(str, *s++);
	}
	return p;
}

static char *str_cat_fmt(str_t str, const char *fmt, ...)
{
	va_list ap;
	char *s, *p;
	int rc;
	va_start(ap, fmt);
	rc = vasprintf(&s, fmt, ap);
	if (rc < 0) {
		p = NULL;
		rc = errno;
		goto out;
	}
	p = str_cat_s(str, s);
out:
	va_end(ap);
	free(s);
	if (rc)
		errno = rc;
	return p;
}

static char *str_str(str_t str)
{
	str->buf_ptr[str->cur_pos] = '\0';
	return strdup(str->buf_ptr);
}

static void str_free(str_t str)
{
	free(str->buf_ptr);
	free(str);
}

static str_t str_new(size_t len)
{
	str_t str = calloc(1, sizeof *str);
	str->buf_len = len;
	str->buf_ptr = malloc(len);
	if (!str->buf_ptr)
		goto err;
	str->cur_ptr = str->buf_ptr;
	return str;
err:
	if (str)
		free(str);
	return NULL;
}

static char *access_string(int perm)
{
#define PERM_BUF_SZ 16
	static char perm_str[PERM_BUF_SZ];
	char *s = perm_str;
	int i;
	*s = '-';
	s++;
	for (i = 6; i >= 0; i -= 3) {
		uint32_t mask = perm >> i;
		if (mask & 4)
			*s = 'r';
		else
			*s = '-';
		s++;
		if (mask & 2)
			*s = 'w';
		else
			*s = '-';
		s++;
		if (mask & 1)
			*s = 'x';
		else
			*s = '-';
		s++;
	}
	*s = '\0';
	return perm_str;
}

static char *get_topic_name(aks_handle_t sh, ldms_set_t set, ldmsd_row_t row)
{
	char *topic_fmt = sh->topic_fmt;
	str_t str = str_new(256);
	char *topic = NULL;
	struct passwd *pwd;
	struct group *grp;

	if (!topic_fmt || topic_fmt[0] == '\0') {
		return strdup(row->schema_name);
	}

	while (*topic_fmt != '\0') {
		while (*topic_fmt != '\0' && *topic_fmt != '%')
			topic = str_cat_c(str, *topic_fmt++);
		if (*topic_fmt == '\0')
			break;
		topic_fmt ++;	/* Skip '%' */
		switch (*topic_fmt) {
		case 'S': /* Set schema name */
			topic = str_cat_s(str, row->schema_name);
			break;
		case 'F': /* Format */
			if (sh->encoding == AKS_ENCODING_JSON)
				topic = str_cat_s(str, "json");
			else
				topic = str_cat_s(str, "avro");
			break;
		case 'u': /* user name */
			pwd = getpwuid(ldms_set_uid_get(set));
			if (pwd) {
				topic = str_cat_fmt(str, "%s", pwd->pw_name);
				break;
			}
			/* Fail safe to uid */
		case 'U': /* UID*/
			topic = str_cat_fmt(str, "%d", ldms_set_uid_get(set));
			break;
		case 'g': /* group name */
			grp = getgrgid(ldms_set_gid_get(set));
			if (grp) {
				topic = str_cat_fmt(str, "%s", grp->gr_name);
				break;
			}
		case 'G': /* GID */
			topic = str_cat_fmt(str, "%d", ldms_set_gid_get(set));
			break;
		case 'a': /* Access mode string */
			topic = str_cat_fmt(str, "%s", access_string(ldms_set_perm_get(set)));
			break;
		case 'A': /* Access mode octal */
			topic = str_cat_fmt(str, "0%o", ldms_set_perm_get(set));
			break;
		case 'I': /* Set instance name */
			topic = str_cat_s(str, ldms_set_instance_name_get(set));
			break;
		case 'P': /* Set producer name */
			topic = str_cat_s(str, ldms_set_producer_name_get(set));
			break;
		default:
			ovis_log(sh->sf->log, OVIS_LERROR,
                                 "Illegal format specifier '%c' in topic format\n", *topic_fmt);
		}
		topic_fmt ++;
	}
	topic = str_str(str);
	str_free(str);
	return topic;
}


static int row_to_avro_payload(aks_handle_t sh, ldmsd_row_t row,
                               void **payload, size_t *sizep)
{
        serdes_schema_t *serdes_schema;
        avro_schema_t schema;
        avro_value_iface_t *class;
        avro_value_t avro_row;

        char errstr[512];
        int rc = 0;

        serdes_schema = serdes_schema_find(sh, (char *)row->schema_name, NULL, row);
        if (!serdes_schema) {
                ovis_log(sh->sf->log, OVIS_LERROR, "A serdes schema for '%s' could not be "
                          "constructed.\n", row->schema_name);
                rc = 1;
                goto out1;
        }
        schema = serdes_schema_avro(serdes_schema);
        class = avro_generic_class_from_schema(schema);
        avro_generic_value_new(class, &avro_row);

        /* Encode ldmsd_row_s as an Avro value */
        rc = serialize_columns_of_row(schema, row, &avro_row, sh->sf->log);
        if (rc) {
                ovis_log(sh->sf->log, OVIS_LERROR, "Failed to format row as Avro value, error: %d\n", rc);
                goto out2;
        }
        /* Serialize an Avro value into a buffer */
        if (serdes_schema_serialize_avro(serdes_schema, &avro_row,
                                         payload, sizep,
                                         errstr, sizeof(errstr))) {
                ovis_log(sh->sf->log, OVIS_LERROR, "Failed to serialize Avro row: '%s'\n", errstr);
                rc = 1;
                goto out2;
        }
out2:
        avro_value_decref(&avro_row);
        avro_value_iface_decref(class);
out1:
        return rc;
}

/* protected by strgp->lock */
static int
commit_rows(ldmsd_plug_handle_t handle, ldmsd_strgp_t strgp, ldms_set_t set, ldmsd_row_list_t row_list,
	    int row_count)
{
	aks_handle_t sh;
	rd_kafka_topic_t *rkt;
	ldmsd_row_t row;
	int rc;

	sh = strgp->store_handle;
	if (!sh)
	{
		sh = __handle_new(handle, strgp);
		if (!sh)
			return errno;
		strgp->store_handle = sh;
	}

	TAILQ_FOREACH(row, row_list, entry)
	{
		void *ser_buf = NULL;
		size_t ser_buf_size;
                int ser_size;

		char *topic_name = get_topic_name(sh, set, row);
		if (!topic_name) {
			ovis_log(sh->sf->log, OVIS_LERROR, "get_topic_name failed for schema '%s'\n", row->schema_name);
			continue;
		}
		ovis_log(sh->sf->log, OVIS_LDEBUG, "topic name %s\n", topic_name);
		rkt = rd_kafka_topic_new(sh->rd, topic_name, NULL);
		if (!rkt)
		{
			ovis_log(sh->sf->log, OVIS_LERROR, "rd_kafka_topic_new(\"%s\") failed, "
				  "errno: %d\n",
				  topic_name, errno);
			goto skip_row_0;
		}
		switch (sh->encoding) {
		case AKS_ENCODING_AVRO:
                        rc = row_to_avro_payload(sh, row, &ser_buf, &ser_buf_size);
			if (rc) {
				ovis_log(sh->sf->log, OVIS_LERROR, "Failed to serialize row as AVRO object, error: %d", rc);
				goto skip_row_1;
			}
			break;
		case AKS_ENCODING_JSON:
			/* Encode row as a JSON text object */
			rc = ldmsd_row_to_json_object(row, (char **)&ser_buf, &ser_size);
			if (rc) {
				ovis_log(sh->sf->log, OVIS_LERROR, "Failed to serialize row as JSON object, error: %d", rc);
				goto skip_row_1;
			}
			ser_buf_size = (size_t)ser_size;
			break;
		default:
			assert(0 == "Invalid/unsupported serialization encoding");
		}

		/* Publish the Avro serialized buffer */
		rc = rd_kafka_produce(rkt, RD_KAFKA_PARTITION_UA,
				      RD_KAFKA_MSG_F_FREE,
				      ser_buf, ser_buf_size, NULL, 0, NULL);
		if (rc) {
			ovis_log(sh->sf->log, OVIS_LERROR,
                                 "rd_kafka_produce(\"%s\") failed, \"%s\"\n",
                                 topic_name, rd_kafka_err2str(rd_kafka_last_error()));
			free(ser_buf);
		}
	skip_row_1:
		rd_kafka_topic_destroy(rkt);
	skip_row_0:
		free(topic_name);
	}

	return 0;
}

static int constructor(ldmsd_plug_handle_t handle) {
	store_kafka_t sk;

        sk = calloc(1, sizeof(*sk));
        if (!sk) {
                return ENOMEM;
        }
        sk->log = ldmsd_plug_log_get(handle);
        pthread_mutex_init(&sk->sk_lock, NULL);
        ldmsd_plug_ctxt_set(handle, sk);

        return 0;
}

static void destructor(ldmsd_plug_handle_t handle) {
	store_kafka_t sk = ldmsd_plug_ctxt_get(handle);

	pthread_mutex_lock(&sk->sk_lock);
	if (sk->g_rd_conf)
	{
		rd_kafka_conf_destroy(sk->g_rd_conf);
		sk->g_rd_conf = NULL;
	}
	pthread_mutex_unlock(&sk->sk_lock);
        pthread_mutex_destroy(&sk->sk_lock);
        free(sk);
}

struct ldmsd_store ldmsd_plugin_interface = {
	.base.type   = LDMSD_PLUGIN_STORE,
	.base.name   = "store_avro_kafka",
	.base.config = config,
	.base.usage  = usage,
        .base.constructor = constructor,
        .base.destructor = destructor,
	.close       = close_store,
	.commit      = commit_rows,
};
