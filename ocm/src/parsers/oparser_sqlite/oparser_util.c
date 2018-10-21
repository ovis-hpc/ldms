/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2014 Sandia Corporation. All rights reserved.
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

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sqlite3.h>
#include <ctype.h>

#include "oparser_util.h"

#define MAX(X,Y) ((X) < (Y) ? (Y) : (X))

int kw_comparator(const void *a, const void *b)
{
	struct kw *_a = (struct kw *)a;
	struct kw *_b = (struct kw *)b;
	return strcmp(_a->token, _b->token);
}

void empty_name_list(struct oparser_name_queue *list)
{
	struct oparser_name *name;
	while (name = TAILQ_FIRST(list)) {
		TAILQ_REMOVE(list, name, entry);
		free(name->name);
		free(name);
	}
}

void oom_report(const char *fn_name)
{
	fprintf(stderr, "%s: Out of memory.\n", fn_name);
	exit(ENOMEM);
}

int count_leading_tabs(char *buf)
{
	int count, i;
	count = 0;
	for (i = 0; i < strlen(buf); i++) {
		if (buf[i] == '\t')
			count++;
		else
			break;
	}
	return count;
}

void trim_trailing_space(char *s)
{
	if (!s)
		return;

	int i = strlen(s) - 1;
	while (i >= 0 && isspace(s[i]))
		i--;

	s[i + 1] = '\0';
}

uint64_t gen_metric_id(uint32_t comp_id, uint32_t metric_type_id)
{
	return ((uint64_t) comp_id) << 32 | metric_type_id;
}

int is_numeric(char *s)
{
	while (*s) {
		if (!isdigit(*s))
			return 0;
		s++;
	}
	return 1;
}

char *get_delimiter(char *s)
{
	char *s1, *s2, *sep;
	s1 = strchr(s, '[');
	if (s1) {
		s2 = strchr(s, ',');
		if (s2) {
			if (s1 - s < s2 - s)
				sep = "]";
			else
				sep = ",";
		} else {
			sep = "]";
		}
	} else {
		sep = ",";
	}
	return sep;
}

char *get_token(char *s, char **res)
{
	char *c = s;
	int in_bracket = 0;
	if (!s)
		return NULL;
	while (*c) {
		switch (*c) {
		case '[':
			in_bracket = 1;
			break;
		case ']':
			in_bracket = 0;
			break;
		case ',':
			if (!in_bracket) {
				*c = '\0';
				*res = c + 1;
				return s;
			}
			break;
		}
		c++;
	}
	*res = NULL;
	return s;
}

int get_expand_token(char *s, char *start, char *end)
{
	char *c = s;
	char *tmp = NULL;
	*start = 0;
	*end = 0;
	while (*c) {
		switch (*c) {
		case ',':
			if (tmp) {
				strncpy(end, tmp, c - tmp);
				end[c - tmp] = '\0';
				return c - s + 1;
			}
			strncpy(start, s, c - s);
			start[c - s] = '\0';
			return c - s + 1;
		case '-':
			strncpy(start, s, c - s);
			start[c - s] = '\0';
			tmp = c + 1;
			break;
		default:
			break;
		}
		c++;
	}
	if (tmp) {
		strcpy(end, tmp);
		return strlen(s);
	} else {
		strcpy(start, s);
		return strlen(s);
	}

	return 0;
}

int process_string_expand(char *prefix, char *suffix, char *expand,
			struct oparser_name_queue *nlist,
			char *sep_start, char *sep_end)
{
	int count = 0;
	char start[128], end[128], name_s[256];

	struct oparser_name *name;

	char *tmp = expand;
	int len;
	len = get_expand_token(tmp, start, end);
	if (!len) {
		sprintf(name_s, "%s%s%s", prefix, tmp, suffix);
		name = malloc(sizeof(*name));
		name->name = strdup(name_s);
		TAILQ_INSERT_TAIL(nlist, name, entry);
		return 1;
	}

	while (len) {
		tmp += len;
		if (*end) {

			int start_len, end_len, max_len, start_d, end_d;
			start_len = strlen(start);
			end_len = strlen(end);
			max_len = MAX(start_len, end_len);

			start_d = atoi(start);
			end_d = atoi(end);
			int i;
			for (i = start_d; i <= end_d; i++) {
				if (start_len != end_len) {
					sprintf(name_s, "%s%s%d%s%s", prefix,
						sep_start, i, sep_end, suffix);
				} else {
					sprintf(name_s, "%s%s%0*d%s%s", prefix,
						sep_start, max_len, i,
							sep_end, suffix);
				}

				name = malloc(sizeof(*name));
				name->name = strdup(name_s);
				TAILQ_INSERT_TAIL(nlist, name, entry);
				count++;
			}
		} else {
			sprintf(name_s, "%s%s%s%s%s", prefix, sep_start,
						start, sep_end, suffix);
			name = malloc(sizeof(*name));
			name->name = strdup(name_s);
			TAILQ_INSERT_TAIL(nlist, name, entry);
			count++;
		}
		len = get_expand_token(tmp, start, end);
	}

	return count;
}

/**
 * \brief Parse the string name
 *
 * \param[in]	s	The string to be parsed
 * \param[in/out]	nlist	The queue of names to be filled
 * \param[in]	sep_start	The starting delimiter of the expansion part
 * 				if name expansion is performed.
 * \param[out]	sep_end		The end delimiter of the expansion part.
 *
 * \return Number of names
 *
 * Example: s = "nid[0001,0002,0003]"
 * case 1: sep_start = sep_end = NULL
 *   Output: nid0001,nid0002,nid0003
 * case 2: sep_start ="[" and sep_end = "]"
 *   Output: nid[0001],nid[0002],nid[0003].
 *
 */
int process_string_name(char *s, struct oparser_name_queue *nlist,
					char *sep_start, char *sep_end)
{
	if (!s) {
		nlist = NULL;
		return 0;
	}

	if (!sep_start)
		sep_start = "";
	if (!sep_end)
		sep_end = "";

	TAILQ_INIT(nlist);
	struct oparser_name *name;
	char *token, *res;
	int count = 0;

	token = get_token(s, &res);
	while (token) {
		char *open, *close, *suf;
		open = strchr(token, '[');
		if (open) {
			close = strchr(open + 1, ']');
			char expand[256];
			if (!close) {
				strcpy(expand, open + 1);
				suf = "";
			} else {
				strncpy(expand, open + 1,
						close - (open + 1));
				suf = close + 1;
				if ('\0' == suf[0])
					suf = "";
				expand[close - (open + 1)] = '\0';
			}

			*open = '\0';
			count += process_string_expand(token, suf, expand, nlist,
							sep_start, sep_end);
		} else {
			name = malloc(sizeof(*name));
			name->name = strdup(token);
			TAILQ_INSERT_TAIL(nlist, name, entry);
			count++;
		}
		token = get_token(res, &res);
	}
	return count;
}

void destroy_oparser_cmd_queue(struct oparser_cmd_queue *cmd_queue)
{
	struct oparser_cmd *cmd;
	while (cmd = TAILQ_FIRST(cmd_queue)) {
		TAILQ_REMOVE(cmd_queue, cmd, entry);
		free(cmd);
	}
}

void _create_table(char *stmt, sqlite3 *db)
{
	int rc;
	sqlite3_stmt *sql_stmt;
	rc = sqlite3_prepare_v2(db, stmt, strlen(stmt),
							&sql_stmt, NULL);
	if (rc) {
		fprintf(stderr, "Failed to prepare the create table stmt: "
				"%s: %s\n", stmt, sqlite3_errmsg(db));
		exit(rc);
	}
	rc = sqlite3_step(sql_stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "%d: Failed to step the create table stmt: "
				"%s: %s\n", rc, stmt,
						sqlite3_errmsg(db));
		exit(rc);
	}
	sqlite3_finalize(sql_stmt);
}

void create_table(char *create_stmt, sqlite3 *db)
{
	_create_table(create_stmt, db);
}

void create_index(char *index_stmt, sqlite3 *db)
{
	_create_table(index_stmt, db);
}


void oparser_drop_table(char *table_name, sqlite3 *db)
{
	char stmt[256];
	sqlite3_stmt *sql_stmt;
	int rc;
	sprintf(stmt, "DROP TABLE IF EXISTS '%s';", table_name);

	rc = sqlite3_prepare_v2(db, stmt, strlen(stmt), &sql_stmt, NULL);
	if (rc) {
		fprintf(stderr, "Failed to prepare: %s: %s\n",
				stmt, sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_step(sql_stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "%d: Failed to step: %s: %s\n",
					rc, stmt, sqlite3_errmsg(db));
		exit(rc);
	}
	sqlite3_finalize(sql_stmt);
}

void oparser_bind_text(sqlite3 *db, sqlite3_stmt *stmt, int idx,
					char *value, const char *fn_name)
{
	int rc = sqlite3_bind_text(stmt, idx, value, -1, SQLITE_TRANSIENT);
	if (rc) {
		fprintf(stderr, "%s[%d]: bind_text[%s]: %s\n", fn_name, rc,
						value, sqlite3_errmsg(db));
		exit(rc);
	}
}

void oparser_bind_int(sqlite3 *db, sqlite3_stmt *stmt, int idx, int value,
							const char *fn_name)
{
	int rc = sqlite3_bind_int(stmt, idx, value);
	if (rc) {
		fprintf(stderr, "%s[%d]: bind_int[%d]: %s\n", fn_name, rc,
						value, sqlite3_errmsg(db));
		exit(rc);
	}
}

void oparser_bind_int64(sqlite3 *db, sqlite3_stmt *stmt, int idx, uint64_t value,
							const char *fn_name)
{
	int rc = sqlite3_bind_int64(stmt, idx, value);
	if (rc) {
		fprintf(stderr, "%s[%d]: bind_int64[%" PRIu64 "]: %s\n",
				fn_name, rc, value, sqlite3_errmsg(db));
		exit(rc);
	}
}

void oparser_bind_null(sqlite3 *db, sqlite3_stmt *stmt, int idx,
						const char *fn_name)
{
	int rc = sqlite3_bind_null(stmt, idx);
	if (rc) {
		fprintf(stderr, "%s[%d]: bind_null: %s\n", fn_name, rc,
							sqlite3_errmsg(db));
		exit(rc);
	}
}

void oparser_finish_insert(sqlite3 *db, sqlite3_stmt *stmt, const char *fn_name)
{
	int rc = sqlite3_step(stmt);
	if (SQLITE_DONE != rc) {
		fprintf(stderr, "%s[%d]: error step: %s\n", fn_name, rc,
							sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_clear_bindings(stmt);
	if (rc) {
		fprintf(stderr, "%s[%d]: error clear_binding: %s\n",
				fn_name, rc, sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_reset(stmt);
	if (rc) {
		fprintf(stderr, "%s[%d]: error reset: %s\n", fn_name, rc,
							sqlite3_errmsg(db));
		exit(rc);
	}
}

#define NUM_ALLOC 10
int oparser_add_comp(struct comp_array *carray, struct oparser_comp *comp)
{
	if (carray->num_alloc == carray->num_comps) {
		carray->num_alloc += NUM_ALLOC;
		carray->comps = realloc(carray->comps, carray->num_alloc *
						sizeof(struct oparser_comp));
		if (!carray->comps)
			return 1;
	}

	carray->comps[(carray->num_comps)++] = comp;
	return 0;
}

void msglog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, fmt, ap);
	va_end(ap);
}
