/*
 * util.c
 *
 *  Created on: Oct 15, 2013
 *      Author: nichamon
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

void destroy_name_list(struct oparser_name_queue *list)
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

//int process_string_expand(char *core, char *expand,
//			struct oparser_name_queue *nlist,
//			char *sep_start, char *sep_end)
//{
//	int count;
//	char start[128], end[128], name_s[256];
//	int start_len, end_len, max_len, start_d, end_d;
//	char *tmp;
//	struct oparser_name *name;
//	int i;
//
//	count = sscanf(expand, "%[^-]-%[^]]", start, end);
//
//	if (count == 2) {
//		/* no need to expansion */
//		if (!is_numeric(start) || !is_numeric(end)) {
//			name = malloc(sizeof(*name));
//			name->name = strdup(expand);
//			TAILQ_INSERT_TAIL(nlist, name, entry);
//			return 1;
//		}
//
//		count = 0;
//
//		start_len = strlen(start);
//		end_len = strlen(end);
//		max_len = MAX(start_len, end_len);
//
//		start_d = atoi(start);
//		end_d = atoi(end);
//
//		for (i = start_d; i <= end_d; i++) {
//			if (start_len != end_len) {
//				sprintf(name_s, "%s%s%d%s", core,
//					sep_start, i, sep_end);
//			} else {
//				sprintf(name_s, "%s%s%0*d%s", core, sep_start,
//							max_len, i, sep_end);
//			}
//
//			name = malloc(sizeof(*name));
//			name->name = strdup(name_s);
//			TAILQ_INSERT_TAIL(nlist, name, entry);
//			count++;
//		}
//	} else {
//		count = 0;
//		tmp = strtok(expand, ",");
//		while (tmp) {
//			sprintf(name_s, "%s%s%s%s", core, sep_start,
//							tmp, sep_end);
//			name = malloc(sizeof(*name));
//			name->name = strdup(name_s);
//			TAILQ_INSERT_TAIL(nlist, name, entry);
//			count++;
//			tmp = strtok(NULL, ",");
//		}
//	}
//	return count;
//}

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
//int process_string_name(char *s, struct oparser_name_queue *nlist,
//					char *sep_start, char *sep_end)
//{
//	if (!s) {
//		nlist = NULL;
//		return 0;
//	}
//
//	if (!sep_start)
//		sep_start = "";
//	if (!sep_end)
//		sep_end = "";
//
//	TAILQ_INIT(nlist);
//	struct oparser_name *name;
//	char *tmp, *sep, *saveptr;
//	int count, count_init;
//	count_init = 0;
//	sep = get_delimiter(s);
//	tmp = strtok_r(s, sep, &saveptr);
//	while (tmp) {
//		name = malloc(sizeof(*name));
//		if (tmp[0] == ',')
//			name->name = strdup(tmp + 1);
//		else
//			name->name = strdup(tmp);
//
//		TAILQ_INSERT_TAIL(nlist, name, entry);
//		count_init++;
//
//		sep = get_delimiter(saveptr);
//		tmp = strtok_r(NULL, sep, &saveptr);
//	}
//
//	count = count_init;
//	struct oparser_name *name_next;
//	char core[256], expand[256];
//	int num, i, j;
//
//	name = TAILQ_FIRST(nlist);
//	for (i = 0; i < count_init; i++) {
//		name_next = name->entry.tqe_next;
//		num = sscanf(name->name, "%[^[][%[^]]",
//				core, expand);
//
//		/*
//		 * remove every name and put the new or existing one
//		 * at the end of the queue to preserve order
//		 */
//		TAILQ_REMOVE(nlist, name, entry);
//
//		if (num == 1) {
//			/* Check if we need to expand the core */
//			if (!strchr(core, ',') && !strchr(core, '[') &&
//				!strchr(core, ']' && !strchr(core, '-'))) {
//
//				TAILQ_INSERT_TAIL(nlist, name, entry);
//				continue;
//			}
//		}
//
//		free(name->name);
//		free(name);
//		count--;
//
//		if (num == 1) {
//			count += process_string_expand("", core, nlist,
//							sep_start, sep_end);
//		} else {
//			count += process_string_expand(core, expand, nlist,
//							sep_start, sep_end);
//		}
//
//		name = name_next;
//	}
//
//	return count;
//}

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
				return c - s + 1;
			}
			strncpy(start, s, c - s);
			return c - s + 1;
		case '-':
			strncpy(start, s, c - s);
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
	}

	return 0;
}

int process_string_expand(char *prefix, char *suffix, char *expand,
			struct oparser_name_queue *nlist,
			char *sep_start, char *sep_end)
{
	int count;
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
			count = 0;
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


void insert_data(char *stmt, sqlite3 *db)
{
	int rc;
	sqlite3_stmt *sql_stmt;
	rc = sqlite3_prepare_v2(db, stmt, strlen(stmt), &sql_stmt, NULL);
	if (rc) {
		fprintf(stderr, "Failed to insert: %s: %s\n", stmt,
							sqlite3_errmsg(db));
		exit(rc);
	}
	rc = sqlite3_step(sql_stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "errcode = %d\n", rc);
		fprintf(stderr, "%d: Failed to step: %s: %s\n",
				rc, stmt, sqlite3_errmsg(db));
		exit(rc);
	}
	sqlite3_finalize(sql_stmt);
}

void create_table(char *create_stmt, char *index_stmt, sqlite3 *db)
{
	int rc;
	sqlite3_stmt *sql_stmt;

	rc = sqlite3_prepare_v2(db, create_stmt, strlen(create_stmt), &sql_stmt, NULL);
	if (rc) {
		fprintf(stderr, "Failed to prepare the create table stmt: "
				"%s: %s\n", create_stmt, sqlite3_errmsg(db));
		exit(rc);
	}
	rc = sqlite3_step(sql_stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "%d: Failed to step the create table stmt: "
				"%s: %s\n", rc, create_stmt, sqlite3_errmsg(db));
		exit(rc);
	}
	sqlite3_finalize(sql_stmt);

	if (!index_stmt)
		return;

	rc = sqlite3_prepare_v2(db, index_stmt, strlen(index_stmt), &sql_stmt, NULL);
	if (rc) {
		fprintf(stderr, "Failed to prepare the index_stmt: "
				"%s: %s\n", index_stmt, sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_step(sql_stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "%d: Failed to step the index_stmt: "
				"%s: %s\n", rc, create_stmt, sqlite3_errmsg(db));
		exit(rc);
	}
	sqlite3_finalize(sql_stmt);
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
