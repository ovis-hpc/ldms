/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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

#include <stdio.h>
#include <sqlite3.h>
#include <string.h>
#include <malloc.h>
#include "ocm.h"
#include "ocmd_plugin.h"

#define LOG(p, FMT, ...) p->log_fn("ocmsqlite3: " FMT, ##__VA_ARGS__);
#define OCMSQL_BUFFER_SIZE (1024 * 1024)
#define OCMSQL_STRING_SIZE (1024 * 4)

struct ocmsqlite3 {
	struct ocmd_plugin base;
	char *path;
	sqlite3 *db;
};

typedef enum osvc {
	OSVC_UNKNOWN,
	OSVC_LDMSD_SAMPLER,
	OSVC_LDMSD_AGGREGATOR,
	OSVC_BALERD,
	OSVC_ME,
	OSVC_KOMONDOR,
	OSVC_LAST
} osvc_t;

const char *__osvc_str[] = {
	[OSVC_UNKNOWN] = "UNKNOWN",
	[OSVC_LDMSD_SAMPLER] = "ldmsd_sampler",
	[OSVC_LDMSD_AGGREGATOR] = "ldmsd_aggregator",
	[OSVC_BALERD] = "balerd",
	[OSVC_ME] = "me",
	[OSVC_KOMONDOR] = "komondor",
};

int handle_OSVC_LDMSD_SAMPLER(ocmd_plugin_t p, const char *host,
			      const char *svc, struct ocm_cfg_buff *buff);
int handle_OSVC_LDMSD_AGGREGATOR(ocmd_plugin_t p, const char *host,
			      const char *svc, struct ocm_cfg_buff *buff);
int handle_OSVC_BALERD(ocmd_plugin_t p, const char *host,
			      const char *svc, struct ocm_cfg_buff *buff);
int handle_OSVC_ME(ocmd_plugin_t p, const char *host,
			      const char *svc, struct ocm_cfg_buff *buff);
int handle_OSVC_KOMONDOR(ocmd_plugin_t p, const char *host,
			      const char *svc, struct ocm_cfg_buff *buff);

typedef int (*handle_osvc_fn)(ocmd_plugin_t p, const char *host,
			      const char *svc, struct ocm_cfg_buff *buff);
handle_osvc_fn handle_osvc_fn_tbl[] = {
	[OSVC_LDMSD_SAMPLER] = handle_OSVC_LDMSD_SAMPLER,
	[OSVC_LDMSD_AGGREGATOR] = handle_OSVC_LDMSD_AGGREGATOR,
	[OSVC_BALERD] = handle_OSVC_BALERD,
	[OSVC_ME] = handle_OSVC_ME,
	[OSVC_KOMONDOR] = handle_OSVC_KOMONDOR
};

osvc_t osvc_from_str(const char *str)
{
	int i;
	for (i = 1; i < OSVC_LAST; i++) {
		if (strcmp(str, __osvc_str[i]) == 0)
			return i;
	}
	return OSVC_UNKNOWN;
}

const char *osvc_to_str(osvc_t o)
{
	if (OSVC_UNKNOWN < o && o < OSVC_LAST)
		return __osvc_str[o];
	return __osvc_str[OSVC_UNKNOWN];
}

/*
 * Check if the input character is valid for environment variables
 */
int is_env_var(char c)
{
	if (isalnum(c) || (c == '_') || (c == '{') || (c == '}')) {
		return 1;
	} else {
		return 0;
	}
}

int _sub_env_var(ocmd_plugin_t p, char *s, char *env_var, int size_s)
{
	char *tmp = getenv(env_var);
	if (!tmp) {
		LOG(p, "$%s isn't set\n", env_var);
		return 0;
	} else {
		sprintf(s + size_s, "%s", tmp);
		return strlen(s);
	}
}

/*
 * Substitute the environment variables in the string with their values.
 *
 * The caller is responsible to free the return char*.
 */
char *sub_env_var(ocmd_plugin_t p, const char *in_s)
{
	char *s = malloc(OCMSQL_STRING_SIZE);
	if (!s) {
		LOG(p, "ocmdsqlite3: Out of memory\n");
		return NULL;
	}

	char *env_var = malloc(OCMSQL_STRING_SIZE);
	if (!env_var) {
		LOG(p, "ocmdsqlite3: Out of memory\n");
		return NULL;
	}

	int env_var_len = 0;

	char *c = in_s;
	int env_var_idx, s_idx;
	env_var[0] = s[0] = '\0';
	env_var_idx = s_idx = 0;
	while (*c) {
		if (*c == '$') {
			if (env_var_idx) {
				env_var[env_var_idx - 1] = '\0';
				s_idx = _sub_env_var(p, s, env_var, s_idx);
				if (!s_idx)
					goto err;
			}
			env_var_idx = 1;
		} else {
			if (env_var_idx) {
				if (is_env_var(*c)) {
					if ((*c != '{') && (*c != '}')) {
						env_var[env_var_idx - 1] = *c;
						env_var_idx++;
					}
				} else {
					env_var[env_var_idx - 1] = '\0';
					s_idx = _sub_env_var(p, s,
							env_var, s_idx);
					if (!s_idx)
						goto err;
					s[s_idx++] = *c;
					env_var_idx = 0;
				}
			} else {
				s[s_idx++] = *c;
			}
		}

		if (s_idx >= OCMSQL_STRING_SIZE) {
			LOG(p, "%s: Exceed buffer.\n", in_s);
			goto err;
		}

		if (env_var_idx >= OCMSQL_STRING_SIZE) {
			LOG(p, "%s: Environment name is exceed buffer.\n",
								in_s);
			goto err;
		}

		c++;
	}

	if (env_var_idx) {
		env_var[env_var_idx - 1] = '\0';
		s_idx = _sub_env_var(p, s, env_var, s_idx);
		if (!s_idx)
			goto err;
	}

	s[s_idx] = '\0';
	free(env_var);
	return s;
err:
	free(s);
	free(env_var);
	return NULL;
}

int ocmsqlite3_query_service(ocmd_plugin_t p, const char *host,
			      const char *svc, struct ocm_cfg_buff *buff)
{
	int rc, count;
	count = 0;
	struct ocmsqlite3 *s = (void*)p;
	char sql[1024];
	char *_buff = malloc(OCMSQL_BUFFER_SIZE);
	if (!_buff) {
		LOG(p, "Could not allocate a buffer. Out of memory.\n");
		return ENOMEM;
	}

	struct ocm_value *ov = (void*)_buff;
	const char *tail;
	struct sqlite3_stmt *stmt;
	sprintf(sql, "SELECT * FROM services WHERE hostname='%s' AND"
			" service='%s';", host, svc);
	rc = sqlite3_prepare_v2(s->db, sql, 1024, &stmt, &tail);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		LOG(p, "sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(s->db));
		goto out;
	}

	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		count++;
		int ccount = sqlite3_column_count(stmt);
		if (ccount != 5) {
			/* invalid result */
			LOG(p, "column count (%d) != 5.\n", ccount);
			rc = EINVAL;
			goto err;
		}
		const char *verb = sqlite3_column_text(stmt, 3);
		if (!verb) {
			LOG(p, "sqlite3_column_text error: ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		rc = ocm_cfg_buff_add_verb(buff, verb);
		if (rc) {
			LOG(p, "ocm_cfg_buff_add_av error %d: %s\n",
					rc, strerror(rc));
			goto err;
		}
		char *avs = (char*) sqlite3_column_text(stmt, 4);
		if (!avs) {
			LOG(p, "sqlite3_column_text error: ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		/* avs format >> A1:V1;A2:V2;... */
		char *_ptr;
		char *av = strtok_r(avs, ";", &_ptr);
		while (av) {
			char *tmp, *a, *v;
			tmp = strchr(av, ':');
			*tmp = '\0';
			a = av;
			v = sub_env_var(p, tmp + 1);
			if (!v)
				goto err;

			ocm_value_set_s(ov, v);
			free(v);
			rc = ocm_cfg_buff_add_av(buff, a, ov);
			if (rc) {
				LOG(p, "ocm_cfg_buff_add_av error %d: %s\n",
						rc, strerror(rc));
				goto err;
			}

			av = strtok_r(NULL, ";", &_ptr);
		}
		rc = sqlite3_step(stmt);
	}

	if (rc == SQLITE_DONE) {
		if (count == 0) {
			rc = ENOENT;
			goto err;
		}
	}

out:
	sqlite3_finalize(stmt);
	free(_buff);
	return 0;
err:
	sqlite3_finalize(stmt);
	LOG(p, "%s:%s: Failed to get the service configuration\n", host, svc);
	free(_buff);
	return rc;
}

#define NOT_CORRECTIVE "0"	/* Not corrective action */
#define CORRECTIVE "1"	/* Corrective action */

int ocmsqlite3_query_actions(ocmd_plugin_t p, struct ocm_cfg_buff *buff)
{
	int rc = 0;
	int count = 0;
	struct ocmsqlite3 *s = (void*)p;
	char sql[1024];
	char *_buff = malloc(OCMSQL_BUFFER_SIZE);
	if (!_buff) {
		LOG(p, "Could not allocate a buffer. Out of memory.\n");
		return ENOMEM;
	}

	struct ocm_value *ov = (void*)_buff;
	const char *tail;
	struct sqlite3_stmt *stmt;
	sprintf(sql, "SELECT * FROM actions;");
	rc = sqlite3_prepare_v2(s->db, sql, 1024, &stmt, &tail);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		LOG(p, "sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(s->db));
		goto out;
	}


	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		count++;
		int ccount = sqlite3_column_count(stmt);
		if (ccount != 3) {
			/* invalid result */
			LOG(p, "column count (%d) != 3 (name, execute, type).\n", ccount);
			rc = EINVAL;
			goto err;
		}

		ocm_cfg_buff_add_verb(buff, "action");

		const char *name = sqlite3_column_text(stmt, 0);
		if (!name) {
			LOG(p, "query_actions: sqlite3_column_text error: ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		char *value = sub_env_var(p, name);
		if (!value)
			goto err;

		ocm_value_set_s(ov, value);
		free(value);
		ocm_cfg_buff_add_av(buff, "name", ov);

		const char *execute = (char*) sqlite3_column_text(stmt, 1);
		if (!execute) {
			LOG(p, "query_actions: sqlite3_column_text error: ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		ocm_value_set_s(ov, execute);
		ocm_cfg_buff_add_av(buff, "execute", ov);

		const char *type = (char *) sqlite3_column_text(stmt, 2);
		if (!type) {
			LOG(p, "query_actions: sqlite3_column_text error: ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		if (0 == strcmp(type, "corrective")) {
			ocm_value_set_s(ov, CORRECTIVE);
		} else {
			ocm_value_set_s(ov, NOT_CORRECTIVE);
		}
		ocm_cfg_buff_add_av(buff, "type", ov);

		rc = sqlite3_step(stmt);
	}

	if (rc == SQLITE_DONE) {
		if (count == 0)
			rc = ENOENT;
		else
			rc = 0;
	}
err:
	sqlite3_finalize(stmt);
out:
	free(_buff);
	return rc;
}

int ocmsqlite3_query_rules(ocmd_plugin_t p , struct ocm_cfg_buff *buff)
{
	int rc = 0;
	struct ocmsqlite3 *s = (void*)p;
	char sql[1024];
	char *_buff = malloc(OCMSQL_BUFFER_SIZE);
	if (!_buff) {
		LOG(p, "Could not allocate a buffer. Out of memory.\n");
		return ENOMEM;
	}

	struct ocm_value *ov = (void*)_buff;
	const char *tail;
	struct sqlite3_stmt *stmt;

	sprintf(sql, "SELECT model_id, metric_id, level, action_name "
			"FROM rule_templates NATURAL JOIN rule_actions "
			"NATURAL JOIN rule_metrics;");

	rc = sqlite3_prepare_v2(s->db, sql, 1024, &stmt, &tail);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		LOG(p, "sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(s->db));
		goto out;
	}

	rc = sqlite3_step(stmt);
	int count = 0;
	while (rc == SQLITE_ROW) {
		count++;
		int ccount = sqlite3_column_count(stmt);
		if (ccount != 4) {
			/* invalid result */
			LOG(p, "column count (%d) != 4.\n", ccount);
			rc = EINVAL;
			goto err;
		}

		ocm_cfg_buff_add_verb(buff, "rule");

		ocm_value_set(ov, OCM_VALUE_UINT16,
				sqlite3_column_int(stmt, 0));
		ocm_cfg_buff_add_av(buff, "model_id", ov);

		const char *metric_id = (char*) sqlite3_column_text(stmt, 1);
		if (!metric_id) {
			LOG(p, "sqlite3_column_text error: ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		ocm_value_set(ov, OCM_VALUE_UINT64,
				strtoull(metric_id, NULL, 0));
		ocm_cfg_buff_add_av(buff, "metric_id", ov);

		ocm_value_set(ov, OCM_VALUE_INT16,
			sqlite3_column_int(stmt, 2));
		ocm_cfg_buff_add_av(buff, "severity", ov);

		const char *action_name = (char*) sqlite3_column_text(stmt, 3);
		if (!action_name) {
			LOG(p, "sqlite3_column_text error: ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		char *value = sub_env_var(p, action_name);
		if (!value)
			goto err;

		ocm_value_set_s(ov, value);
		free(value);
		ocm_cfg_buff_add_av(buff, "action", ov);

		rc = sqlite3_step(stmt);
	}

	if (rc == SQLITE_DONE) {
		if (count == 0)
			rc = ENOENT;
		else
			rc = 0;
	}
err:
	sqlite3_finalize(stmt);
out:
	free(_buff);
	return rc;
}

int handle_OSVC_KOMONDOR(ocmd_plugin_t p, const char *host,
				const char *svc, struct ocm_cfg_buff *buff)
{
	int rc = 0;
	struct ocmsqlite3 *s = (void*)p;

	rc = ocmsqlite3_query_service(p, host, svc, buff);
	if (rc) {
		if (rc == ENOENT) {
			p->log_fn("ocmsqlite3: Warning: No service "
					"configuration for '%s/%s'\n",
					host, svc);
		} else {
			p->log_fn("ocmsqlite3: failed to get the service "
					"configuration for '%s/%s'\n",
					host, svc);
		}
		return rc;
	}

	rc = ocmsqlite3_query_actions(p, buff);
	if (rc) {
		p->log_fn("ocmsqlite3: failed to get the action "
				"configuration for '%s/%s'\n",
				host, svc);
		return rc;
	}

	rc = ocmsqlite3_query_rules(p, buff);
	if (rc) {
		p->log_fn("ocmsqlite3: failed to get the event rule "
				"configuration for '%s/%s'\n",
				host, svc);
		return rc;
	}

	return rc;
}

int query_sampler_cfg(ocmd_plugin_t p, struct sqlite3_stmt *stmt,
		struct ocm_cfg_buff *buff)
{
	char *_buff = malloc(OCMSQL_BUFFER_SIZE);
	if (!_buff) {
		LOG(p, "Could not allocate a buffer. Out of memory.\n");
		return ENOMEM;
	}

	struct ocm_value *v = (void *)_buff;

	int idx_apply_on = 1;
	int idx_ldmsd_set = 2;
	int idx_cfg = 3;
	int idx_metrics = 4;

	char *key, *value, *tmp_buf, *interval_s, *offset_s;
	char *ldmsd_set, *cfg_s, *metrics, *host;
	char set_name[128];

	cfg_s = (char*) sqlite3_column_text(stmt, idx_cfg);
	ldmsd_set = (char *) sqlite3_column_text(stmt, idx_ldmsd_set);
	host = (char *) sqlite3_column_text(stmt, idx_apply_on);
	interval_s = offset_s = NULL;

	/* process config string */
	ocm_cfg_buff_add_verb(buff, "config");

	if (';' == cfg_s[0])
		cfg_s++;

	ocm_value_set_s(v, ldmsd_set);
	ocm_cfg_buff_add_av(buff, "name", v);
	/* default set name */
	sprintf(set_name, "%s/%s", host, ldmsd_set);

	key = strtok_r(cfg_s, ":", &tmp_buf);
	while (key) {
		if (strcmp(key, "interval") == 0) {
			interval_s = strtok_r(NULL, ";", &tmp_buf);
		} else if (strcmp(key, "offset_s") == 0) {
			offset_s = strtok_r(NULL, ";", &tmp_buf);
		} else if (strcmp(key, "set_name") == 0) {
			/* rename the setname if needed */
			char *_p = strtok_r(NULL, ";", &tmp_buf);
			char *_s = strstr(_p, "$(apply_on)");
			if (!_s) {
				LOG(p, "set_name invalid, expecting $(apply_on)"
						", skip to next attribute.\n");
				goto skip;
			}
			int len = _s - _p;
			_s = _s + strlen("$(apply_on)");
			sprintf(set_name, "%.*s%s%s", len, _p, host, _s);
		} else {
			value = strtok_r(NULL, ";", &tmp_buf);
			char *tmp = sub_env_var(p, value);
			if (!tmp)
				goto err;

			ocm_value_set_s(v, tmp);
			free(tmp);
			ocm_cfg_buff_add_av(buff, key, v);
		}
skip:
		key = strtok_r(NULL, ":", &tmp_buf);
	}

	ocm_value_set_s(v, set_name);
	ocm_cfg_buff_add_av(buff, "set", v);

	/* prepare the nested attribute-value for attribute name 'metric_id' */
	metrics = (char *) sqlite3_column_text(stmt, idx_metrics);

	uint64_t metric_id;
	struct ocm_cfg_buff *m_buff = ocm_cfg_buff_new(4096, "");
	ocm_cfg_buff_add_verb(m_buff, "");

	key = strtok_r(metrics, "[", &tmp_buf);
	while (key) {
		value = strtok_r(NULL, "]", &tmp_buf);
		metric_id = strtoull(value, NULL, 10);
		ocm_value_set(v, OCM_VALUE_UINT64, metric_id);
		ocm_cfg_buff_add_av(m_buff, key, v);
		tmp_buf += 1; /* skip ',' */
		key = strtok_r(NULL, "[", &tmp_buf);
	}

	ocm_cfg_buff_add_cmd_as_av(buff, "metric_id",
					ocm_cfg_buff_curr_cmd(m_buff));
	ocm_cfg_buff_free(m_buff);

	/* Process start string */
	uint64_t interval;
	int64_t offset;

	ocm_cfg_buff_add_verb(buff, "start");

	ocm_value_set_s(v, ldmsd_set);
	ocm_cfg_buff_add_av(buff, "name", v);

	if (interval_s) {
		interval = strtoull(interval_s, NULL, 10);
		ocm_value_set(v, OCM_VALUE_UINT64, interval);
		ocm_cfg_buff_add_av(buff, "interval", v);
	}

	if (offset_s) {
		offset = strtoull(offset_s, NULL, 10);
		ocm_value_set(v, OCM_VALUE_INT64, offset);
		ocm_cfg_buff_add_av(buff, "offset", v);
	}
	free(_buff);
	return 0;
err:
	free(_buff);
	return -1;
}

int ocmsqlite3_query_sampler_cfg(struct ocm_cfg_buff *buff, sqlite3 *db,
					const char *hostname, ocmd_plugin_t p)
{
	int rc = 0;
	int count = 0;
	struct ocmsqlite3 *s = (void*)p;
	char sql[1024];
	const char *tail;
	struct sqlite3_stmt *stmt;
	sprintf(sql, "SELECT * FROM templates WHERE apply_on='%s';", hostname);
	rc = sqlite3_prepare_v2(s->db, sql, 1024, &stmt, &tail);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		LOG(p, "sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(s->db));
		goto out;
	}

	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		count++;
		int ccount = sqlite3_column_count(stmt);
		if (ccount != 5) {
			/* invalid result */
			LOG(p, "column count (%d) != 5.\n", ccount);
			rc = EINVAL;
			goto err;
		}
		rc = query_sampler_cfg(p, stmt, buff);
		if (rc)
			goto err;
		rc = sqlite3_step(stmt);
	}

	if (rc == SQLITE_DONE) {
		if (count == 0)
			rc = ENOENT;
		else
			rc = 0;
	}
err:
	sqlite3_finalize(stmt);
out:
	return rc;
}

int handle_OSVC_LDMSD_SAMPLER(ocmd_plugin_t p, const char *host,
			      const char *svc, struct ocm_cfg_buff *buff)
{
	struct ocmsqlite3 *s = (struct ocmsqlite3 *)p;
	int rc = ocmsqlite3_query_sampler_cfg(buff, s->db, host, p);
	if (rc == ENOENT) {
		p->log_fn("ocmsqlite3: No configuration for '%s/%s'\n",
				host, svc);
	}
	return rc;
}

int process_ldmsd_aggregator_verb_add(ocmd_plugin_t p,
		struct ocm_cfg_buff *buff, struct sqlite3_stmt *stmt)
{
	int rc;
	char *_buff = malloc(OCMSQL_BUFFER_SIZE);
	if (!_buff) {
		LOG(p, "Could not allocate a buffer. Out of memory.\n");
		return ENOMEM;
	}

	struct ocm_value *ov = (void*)_buff;
	uint64_t interval;
	int64_t offset;
	uint16_t port;
	char *avs = (char*) sqlite3_column_text(stmt, 4);
	if (!avs) {
		LOG(p, "sqlite3_column_text error: ENOMEM\n");
		free(_buff);
		return ENOMEM;
	}
	/* avs format >> A1:V1;A2:V2;... */
	char *_ptr;
	char *av = strtok_r(avs, ";", &_ptr);
	while (av) {
		char *tmp, *a, *v;
		tmp = strchr(av, ':');
		*tmp = '\0';
		a = av;
		v = sub_env_var(p, tmp + 1);
		if (!v)
			goto err;

		if (strcmp(a, "interval") == 0) {
			interval = strtoull(v, NULL, 10);
			free(v);
			ocm_value_set(ov, OCM_VALUE_UINT64, interval);
			rc = ocm_cfg_buff_add_av(buff, "interval", ov);
			if (rc)
				goto err;

		} else if (strcmp(a, "offset") == 0) {
			offset = strtoull(v, NULL, 10);
			free(v);
			ocm_value_set(ov, OCM_VALUE_INT64, offset);
			rc = ocm_cfg_buff_add_av(buff, "offset", ov);
			if (rc)
				goto err;
		} else if (strcmp(a, "port") == 0) {
			port = atoi(v);
			free(v);
			ocm_value_set(ov, OCM_VALUE_UINT16, port);
			rc = ocm_cfg_buff_add_av(buff, "port", ov);
			if (rc)
				goto err;
		} else {
			ocm_value_set_s(ov, v);
			free(v);
			rc = ocm_cfg_buff_add_av(buff, a, ov);
			if (rc)
				goto err;
		}

		av = strtok_r(NULL, ";", &_ptr);
	}
	free(_buff);
	return 0;
err:
	LOG(p, "ocm_cfg_buff_add_av error %d: %s\n", rc, strerror(rc));
	free(_buff);
	return rc;
}

int ocmsqlite3_query_ldmsd_aggregator_service(ocmd_plugin_t p,
		const char *host, const char *svc, struct ocm_cfg_buff *buff)
{
	int rc, count;
	count = 0;
	struct ocmsqlite3 *s = (void*)p;
	char sql[1024];
	char *_buff = malloc(OCMSQL_BUFFER_SIZE);
	if (!_buff) {
		LOG(p, "Could not allocate a buffer. Out of memory.\n");
		return ENOMEM;
	}

	struct ocm_value *ov = (void*)_buff;
	const char *tail;
	struct sqlite3_stmt *stmt;
	sprintf(sql, "SELECT * FROM services WHERE hostname='%s' AND"
			" service='%s';", host, svc);
	rc = sqlite3_prepare_v2(s->db, sql, 1024, &stmt, &tail);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		LOG(p, "sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(s->db));
		goto out;
	}

	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		count++;
		int ccount = sqlite3_column_count(stmt);
		if (ccount != 5) {
			/* invalid result */
			LOG(p, "column count (%d) != 5.\n", ccount);
			rc = EINVAL;
			goto err;
		}
		const char *verb = sqlite3_column_text(stmt, 3);
		if (!verb) {
			LOG(p, "sqlite3_column_text error: ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}

		rc = ocm_cfg_buff_add_verb(buff, verb);
		if (rc) {
			LOG(p, "ocm_cfg_buff_add_av error %d: %s\n",
					rc, strerror(rc));
			goto err;
		}

		if (strcmp(verb, "add") == 0) {
			process_ldmsd_aggregator_verb_add(p, buff, stmt);
			rc = sqlite3_step(stmt);
			continue;
		}

		char *avs = (char*) sqlite3_column_text(stmt, 4);
		if (!avs) {
			LOG(p, "sqlite3_column_text error: ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		/* avs format >> A1:V1;A2:V2;... */
		char *_ptr;
		char *av = strtok_r(avs, ";", &_ptr);
		while (av) {
			char *tmp, *a, *v;
			tmp = strchr(av, ':');
			*tmp = '\0';
			a = av;
			v = sub_env_var(p, tmp + 1);
			if (!v)
				goto err;

			ocm_value_set_s(ov, v);
			free(v);
			rc = ocm_cfg_buff_add_av(buff, a, ov);
			if (rc) {
				LOG(p, "ocm_cfg_buff_add_av error %d: %s\n",
						rc, strerror(rc));
				goto err;
			}

			av = strtok_r(NULL, ";", &_ptr);
		}
		rc = sqlite3_step(stmt);
	}

	if (rc == SQLITE_DONE) {
		if (count == 0)
			rc = ENOENT;
		else
			rc = 0;
	}
err:
	sqlite3_finalize(stmt);
out:
	free(_buff);
	return rc;
}

int handle_OSVC_LDMSD_AGGREGATOR(ocmd_plugin_t p, const char *host,
			      const char *svc, struct ocm_cfg_buff *buff)
{
	int rc = ocmsqlite3_query_ldmsd_aggregator_service(p, host, svc, buff);
	if (rc == ENOENT) {
		p->log_fn("ocmsqlite3: No configuration for '%s/%s'\n",
				host, svc);
	}
	return rc;
}

int handle_OSVC_BALERD(ocmd_plugin_t p, const char *host,
			      const char *svc, struct ocm_cfg_buff *buff)
{
	int rc = ocmsqlite3_query_service(p, host, svc, buff);
	if (rc == ENOENT) {
		p->log_fn("ocmsqlite3: No configuration for '%s/%s'\n",
				host, svc);
	}
	return rc;

}

int ocmsqlite3_query_events(ocmd_plugin_t p, struct ocm_cfg_buff *buff)
{
	struct ocmsqlite3 *s = (void*)p;
	int count = 0;
	int rc = 0;
	char sql[1024];
	char *_buff = malloc(OCMSQL_BUFFER_SIZE);
	if (!_buff) {
		LOG(p, "Could not allocate a buffer. Out of memory.\n");
		return ENOMEM;
	}

	struct ocm_value *ov = (void*)_buff;
	const char *tail;
	struct sqlite3_stmt *stmt;
	/*
	 * model_id = 65535 are for user_event.
	 * Filter the user-generated events out
	 */
	sprintf(sql, "SELECT model_id, metric_id FROM rule_templates "
			"NATURAL JOIN rule_metrics WHERE model_id != 65535;");

	rc = sqlite3_prepare_v2(s->db, sql, 1024, &stmt, &tail);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		LOG(p, "sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(s->db));
		goto out;
	}

	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		count++;
		int ccount = sqlite3_column_count(stmt);
		if (ccount != 2) {
			/* invalid result */
			LOG(p, "column count of query_event (%d) != 2.\n",
								ccount);
			rc = EINVAL;
			goto err;
		}

		ocm_cfg_buff_add_verb(buff, "model");

		const char *model_id = sqlite3_column_text(stmt, 0);
		if (!model_id) {
			LOG(p, "sqlite3_column_text error 'name': ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		ocm_value_set_s(ov, model_id);
		rc = ocm_cfg_buff_add_av(buff, "model_id", ov);
		if (rc) {
			LOG(p, "ocm_cfg_buff_add_av error %d: %s\n",
					rc, strerror(rc));
			goto err;
		}

		const char *metric_id = (char*) sqlite3_column_text(stmt, 1);
		if (!metric_id) {
			LOG(p, "sqlite3_column_text error 'model_id': ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		ocm_value_set_s(ov, metric_id);
		rc = ocm_cfg_buff_add_av(buff, "metric_id", ov);
		if (rc) {
			LOG(p, "ocm_cfg_buff_add_av error %d: %s\n",
					rc, strerror(rc));
			goto err;
		}

		rc = sqlite3_step(stmt);
	}

	if (rc == SQLITE_DONE) {
		if (count == 0)
			rc = ENOENT;
		else
			rc = 0;
	}
err:
	sqlite3_finalize(stmt);
out:
	free(_buff);
	return rc;
}

int ocmsqlite3_query_model_policy(ocmd_plugin_t p, struct ocm_cfg_buff *buff)
{
	struct ocmsqlite3 *s = (void*)p;
	int count = 0;
	int rc = 0;
	char sql[1024];
	char *_buff = malloc(OCMSQL_BUFFER_SIZE);
	if (!_buff) {
		LOG(p, "Could not allocate a buffer. Out of memory.\n");
		return ENOMEM;
	}

	struct ocm_value *ov = (void*)_buff;
	const char *tail;
	struct sqlite3_stmt *stmt;
	sprintf(sql, "SELECT * FROM models;");
	rc = sqlite3_prepare_v2(s->db, sql, 1024, &stmt, &tail);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		LOG(p, "sqlite3_prepare_v2 error: %s\n", sqlite3_errmsg(s->db));
		goto out;
	}

	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		count++;
		int ccount = sqlite3_column_count(stmt);
		if (ccount != 5) {
			/* invalid result */
			LOG(p, "column count (%d) != 5.\n", ccount);
			rc = EINVAL;
			goto err;
		}

		ocm_cfg_buff_add_verb(buff, "create");

		const char *name = sqlite3_column_text(stmt, 0);
		if (!name || '\0' == name[0]) {
			LOG(p, "sqlite3_column_text error 'name': ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		ocm_value_set_s(ov, name);
		rc = ocm_cfg_buff_add_av(buff, "name", ov);
		if (rc) {
			LOG(p, "ocm_cfg_buff_add_av error %d: %s\n",
					rc, strerror(rc));
			goto err;
		}

		const char *model_id = (char*) sqlite3_column_text(stmt, 1);
		if (!model_id || '\0' == model_id[0]) {
			LOG(p, "sqlite3_column_text error 'model_id': ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		ocm_value_set_s(ov, model_id);
		rc = ocm_cfg_buff_add_av(buff, "model_id", ov);
		if (rc) {
			LOG(p, "ocm_cfg_buff_add_av error %d: %s\n",
					rc, strerror(rc));
			goto err;
		}

		const char *param = (char*) sqlite3_column_text(stmt, 2);
		if (param && '\0' != param[0]) {
			ocm_value_set_s(ov, param);
			rc = ocm_cfg_buff_add_av(buff, "param", ov);
			if (rc) {
				LOG(p, "ocm_cfg_buff_add_av error %d: %s\n",
						rc, strerror(rc));
				goto err;
			}
		}

		const char *thresholds = (char*) sqlite3_column_text(stmt, 3);
		if (!thresholds || '\0' == thresholds[0]) {
			LOG(p, "sqlite3_column_text error 'thresholds': ENOMEM\n");
			rc = ENOMEM;
			goto err;
		}
		ocm_value_set_s(ov, thresholds);
		rc = ocm_cfg_buff_add_av(buff, "thresholds", ov);
		if (rc) {
			LOG(p, "ocm_cfg_buff_add_av error %d: %s\n",
					rc, strerror(rc));
			goto err;
		}

		const char *report_flags = (char*) sqlite3_column_text(stmt, 4);
		if (report_flags && '\0' != report_flags[0]) {
			ocm_value_set_s(ov, report_flags);
			rc = ocm_cfg_buff_add_av(buff, "report_flags", ov);
		}

		rc = sqlite3_step(stmt);
	}

	if (rc == SQLITE_DONE) {
		if (count == 0)
			rc = ENOENT;
		else
			rc = 0;
	}
err:
	sqlite3_finalize(stmt);
out:
	free(_buff);
	return rc;
}

int handle_OSVC_ME(ocmd_plugin_t p, const char *host,
			      const char *svc, struct ocm_cfg_buff *buff)
{
	int rc = 0;
	int count = 0;
	struct ocmsqlite3 *s = (struct ocmsqlite3 *)p;
	rc = ocmsqlite3_query_service(p, host, svc, buff);
	if (rc) {
		if (rc == ENOENT) {
			p->log_fn("ocmsqlite3: Warning: No service "
					"configuration for '%s/%s'\n",
					host, svc);
		} else {
			p->log_fn("ocmsqlite3: failed to get the service "
					"configuration for '%s/%s'\n", host, svc);
			return rc;
		}
	}

	/* Get the model policy configuration */
	rc = ocmsqlite3_query_model_policy(p, buff);
	if (rc) {
		p->log_fn("ocmsqlite3: failed to get the model policy "
				"configuration for '%s/%s'\n",
				host, svc);
		return rc;
	}

	/* Get the event configuration */
	rc = ocmsqlite3_query_events(p, buff);
	if (rc) {
		p->log_fn("ocmsqlite3: failed to get the event "
				"configuration for '%s/%s'\n",
				host, svc);
		return rc;
	}
	return 0;
}

int ocmsqlite3_get_config(ocmd_plugin_t p, const char *key,
		struct ocm_cfg_buff *buff)
{
	/* key is "hostname/service_name" */
	int rc;
	struct ocmsqlite3 *s = (void*)p;
	char *host, *service, *ptr;
	char *_k = strdup(key);
	if (!_k) {
		rc = ENOMEM;
		goto err;
	}
	host = strtok_r(_k, "/", &ptr);
	service = strtok_r(NULL, "/", &ptr);
	if (!host || !service) {
		LOG(p, "ocmsqlite3: Invalid key: %s\n", key);
		rc = EINVAL;
		goto err;
	}
	osvc_t osvc = osvc_from_str(service);
	if (osvc != OSVC_UNKNOWN) {
		rc = handle_osvc_fn_tbl[osvc](p, host, service, buff);
	} else {
		rc = EINVAL;
		p->log_fn("ocmsqlite3: Unknown service: %s\n", service);
		goto err;
	}
err:
	return rc;
}

void ocmsqlite3_destroy(ocmd_plugin_t p)
{
	struct ocmsqlite3 *s = (void*)p;
	if (s->db)
		sqlite3_close(s->db);
	if (s->path)
		free(s->path);
	free(s);
}

struct ocmd_plugin* ocmd_plugin_create(void (*log_fn)(const char *fmt, ...),
				       struct attr_value_list *avl)
{
	const char *path = av_value(avl, "path");
	if (!path) {
		errno = EINVAL;
		goto err0;
	}
	struct ocmsqlite3 *s = calloc(1, sizeof(*s));
	if (!s) {
		log_fn("ocmsqlite3: ENOMEM at %s:%d in %s\n", __FILE__,
				__LINE__,  __func__);
		goto err0;
	}
	s->base.get_config = ocmsqlite3_get_config;
	s->base.log_fn = log_fn;
	s->base.destroy = ocmsqlite3_destroy;
	s->path = strdup(path);
	if (!s->path)
		goto err1;
	int rc;
	rc = sqlite3_open_v2(path, &s->db, SQLITE_OPEN_READONLY, NULL);
	if (!s->db) {
		log_fn("sqlite3: sqlite3_open error: ENOMEM\n");
		errno = ENOMEM;
		goto err1;
	}
	if (rc != SQLITE_OK) {
		log_fn("ocmsqlite3: sqlite3_open error: %s\n",
				sqlite3_errmsg(s->db));
		errno = EBUSY;
		goto err1;
	}

	return (ocmd_plugin_t) s;
err1:
	ocmsqlite3_destroy((ocmd_plugin_t)s);
err0:
	return NULL;
}
