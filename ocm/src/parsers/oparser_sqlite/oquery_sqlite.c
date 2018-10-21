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

#include <errno.h>
#include <sqlite3.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>

#include "oparser_util.h"
#include "oquery_sqlite.h"

void oquery_metric_id(char *metric_name, char *prod_comp_type,
				struct mae_metric_list *list,
				char *coll_comp_names,
				sqlite3 *db)
{
	char stmt[2048], prod_comps[1024];
	int rc;
	int is_coll_comp = 1;
	int is_prod_comp = 1;
	if (!coll_comp_names || strlen(coll_comp_names) == 0)
		is_coll_comp = 0;
	if (!prod_comp_type || strlen(prod_comp_type) == 0)
		is_prod_comp = 0;

	if (!is_prod_comp) {
		if (!is_coll_comp) {
			sprintf(stmt, "SELECT metric_id FROM metrics WHERE "
					"name LIKE '%s';", metric_name);
		} else {
			sprintf(stmt, "SELECT metric_id FROM metrics WHERE "
				"name LIKE '%s' AND coll_comp IN (%s);",
					metric_name, coll_comp_names);
		}
	} else {
		/* Get the comp id of components monitored by the metrics. */
		sprintf(prod_comps, "SELECT comp_id FROM components WHERE "
				"type IN (%s)", prod_comp_type);

		if (!is_coll_comp) {
			sprintf(stmt, "SELECT metric_id FROM metrics WHERE "
				"name LIKE '%s' AND prod_comp_id IN (%s);",
						metric_name, prod_comps);
		} else {
			sprintf(stmt, "SELECT metric_id FROM metrics WHERE "
						"name LIKE '%s' AND "
						"prod_comp_id IN (%s) AND "
						"coll_comp IN (%s);",
						metric_name, prod_comps,
						coll_comp_names);
		}
	}

	sqlite3_stmt *sql_stmt;
	const char *tail;
	rc = sqlite3_prepare_v2(db, stmt, strlen(stmt), &sql_stmt, &tail);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		fprintf(stderr, "sqlite3_prepare_v2 err: %s\n",
							sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_step(sql_stmt);
	while (rc == SQLITE_ROW) {
		int ccount = sqlite3_column_count(sql_stmt);
		if (ccount != 1) {
			fprintf(stderr, "column count (%d) != 1.: '%s'.\n",
								ccount, stmt);
			exit(EINVAL);
		}

		struct mae_metric *metric;

		metric = malloc(sizeof(*metric));
		if (!metric) {
			fprintf(stderr, "%s: Out of Memory.\n", __FUNCTION__);
			exit(ENOMEM);
		}

		const char *metric_id = (char *) sqlite3_column_text(sql_stmt,
									0);
		if (!metric_id) {
			fprintf(stderr, "%s: sqlite_column_text error: ENOMEM\n",
								__FUNCTION__);
			exit(ENOMEM);
		}

		char *end;
		metric->metric_id = strtoll(metric_id, &end, 10);
		if (!*metric_id || *end) {
			fprintf(stderr, "Wrong format '%s'. Expecting "
					"metric_id.\n", metric_id);
			exit(EPERM);
		}

		LIST_INSERT_HEAD(list, metric, entry);
		rc = sqlite3_step(sql_stmt);
	}

}

void oquery_metric_id_by_coll_type(char *metric_name, char *prod_comp_type,
				struct mae_metric_list *list,
				char *coll_type, char *ids,
				sqlite3 *db)
{
	char stmt[2048], prod_comps[1024], coll_names_stmt[1024];
	int rc;
	int is_prod_comp = 1;
	int is_ids = 1;

	if (!prod_comp_type || strlen(prod_comp_type) == 0)
		is_prod_comp = 0;

	if (!ids || strlen(ids) == 0)
		is_ids = 0;

	/* Get the name of collecting components */
	if (!is_ids) {
		sprintf(coll_names_stmt, "SELECT name FROM components WHERE "
						" type = '%s'", coll_type);
	} else {
		sprintf(coll_names_stmt, "SELECT name FROM components WHERE "
				" type = '%s' AND identifier IN (%s)",
				coll_type, ids);
	}

	if (!is_prod_comp) {
		sprintf(stmt, "SELECT metric_id FROM metrics WHERE "
			"name LIKE '%s' AND coll_comp IN (%s);",
				metric_name, coll_names_stmt);
	} else {
		/* Get the comp id of components monitored by the metrics. */
		sprintf(prod_comps, "SELECT comp_id FROM components WHERE "
				"type IN (%s)", prod_comp_type);

		sprintf(stmt, "SELECT metric_id FROM metrics WHERE "
					"name LIKE '%s' AND "
					"prod_comp_id IN (%s) AND "
					"coll_comp IN (%s);",
					metric_name, prod_comps,
					coll_names_stmt);
	}

	sqlite3_stmt *sql_stmt;
	const char *tail;
	rc = sqlite3_prepare_v2(db, stmt, strlen(stmt), &sql_stmt, &tail);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		fprintf(stderr, "sqlite3_prepare_v2 err: %s\n",
							sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_step(sql_stmt);
	while (rc == SQLITE_ROW) {
		int ccount = sqlite3_column_count(sql_stmt);
		if (ccount != 1) {
			fprintf(stderr, "column count (%d) != 1.: '%s'.\n",
								ccount, stmt);
			exit(EINVAL);
		}

		struct mae_metric *metric;

		metric = malloc(sizeof(*metric));
		if (!metric) {
			fprintf(stderr, "%s: Out of Memory.\n", __FUNCTION__);
			exit(ENOMEM);
		}

		const char *metric_id = (char *) sqlite3_column_text(sql_stmt,
									0);
		if (!metric_id) {
			fprintf(stderr, "%s: sqlite_column_text error: ENOMEM\n",
								__FUNCTION__);
			exit(ENOMEM);
		}

		char *end;
		metric->metric_id = strtoll(metric_id, &end, 10);
		if (!*metric_id || *end) {
			fprintf(stderr, "Wrong format '%s'. Expecting "
					"metric_id.\n", metric_id);
			exit(EPERM);
		}

		LIST_INSERT_HEAD(list, metric, entry);
		rc = sqlite3_step(sql_stmt);
	}
}

int query_num_sampling_nodes_cb(void *_num, int argc, char **argv,
							char **col_name)
{
	int *num = (int *)_num;
	*num = atoi(argv[0]);
	return 0;
}

void oquery_num_sampling_nodes(int *num_sampling_nodes, sqlite3 *db)
{
	char *stmt = "SELECT COUNT(DISTINCT(coll_comp_id)) FROM metrics;";
	int rc;
	char *errmsg;

	rc = sqlite3_exec(db, stmt, query_num_sampling_nodes_cb,
					num_sampling_nodes, &errmsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to query the number of components "
				"running ldmsd_sampler: %s\n",
				sqlite3_errmsg(db));
		sqlite3_free(errmsg);
		exit(rc);
	}
	sqlite3_free(errmsg);
}

int query_max_metric_type_id_cb(void *_max, int argc, char **argv,
							char **col_name)
{
	uint32_t *max = (uint32_t *)_max;
	char *end;
	*max = strtol(argv[0], &end, 10);
	if (!*argv[0] || *end) {
		fprintf(stderr, "Wrong format '%s'. Expecting metric_id.\n",
								argv[0]);
		exit(EPERM);
	}
	return 0;
}

void oquery_max_metric_type_id(sqlite3 *db, uint32_t *max_metric_type_id)
{
	char *stmt = "SELECT MAX(metric_type_id) FROM metrics;";
	int rc;
	char *errmsg;

	rc = sqlite3_exec(db, stmt, query_max_metric_type_id_cb,
					max_metric_type_id, &errmsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to query maximum metric type id:"
				" %s\n", sqlite3_errmsg(db));
		sqlite3_free(errmsg);
		exit(rc);
	}
	sqlite3_free(errmsg);
}

void oquery_numb_comps(int *num_comps, sqlite3 *db)
{
	struct oparser_comp comp;
	char command[512];
	int rc;
	struct sqlite3_stmt *stmt;
	sprintf(command, "SELECT COUNT(comp_id) FROM components;");

	rc = sqlite3_prepare_v2(db, command, -1, &stmt, 0);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		fprintf(stderr, "sqlite3_prepare_v2 error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		fprintf(stderr, "sqlite3_step error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}
	int num = sqlite3_column_int(stmt, 0);
	*num_comps = num;

	sqlite3_finalize(stmt);
}

void oquery_all_comp_ids(int *num_compids, uint32_t **comp_ids, sqlite3 *db)
{
	struct oparser_comp comp;
	char command[512];
	int rc;
	struct sqlite3_stmt *stmt;
	sprintf(command, "SELECT COUNT(comp_id) FROM components;");

	rc = sqlite3_prepare_v2(db, command, -1, &stmt, 0);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		fprintf(stderr, "sqlite3_prepare_v2 error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		fprintf(stderr, "sqlite3_step error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}
	int num = sqlite3_column_int(stmt, 0);
	*num_compids = num;

	sqlite3_finalize(stmt);

	sprintf(command, "SELECT comp_id FROM components;");

	rc = sqlite3_prepare_v2(db, command, -1, &stmt, 0);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		fprintf(stderr, "sqlite3_prepare_v2 error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}

	uint32_t *compids = *comp_ids;
	compids = malloc(*num_compids * sizeof(uint32_t));

	int i = 0;
	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		compids[i++] = sqlite3_column_int(stmt, 0);
		rc = sqlite3_step(stmt);
	}

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "sqlite3 error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}

	sqlite3_finalize(stmt);
}

void oquery_comp_id_by_type(const char *type, int *numcomps,
					uint32_t **compids, sqlite3 *db)
{
	char command[512];
	int rc;
	struct sqlite3_stmt *stmt;
	sprintf(command, "SELECT COUNT(*) FROM components where type='%s';",
									type);

	rc = sqlite3_prepare_v2(db, command, 512, &stmt, 0);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		fprintf(stderr, "sqlite3_prepare_v2 error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		fprintf(stderr, "sqlite3_step error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}
	int num = sqlite3_column_int(stmt, 0);
	*numcomps = num;
	sqlite3_finalize(stmt);

	sprintf(command, "SELECT comp_id FROM components where type='%s';",
									type);

	rc = sqlite3_prepare_v2(db, command, 512, &stmt, 0);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		fprintf(stderr, "sqlite3_prepare_v2 error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}

	uint32_t *comp_ids = malloc(num * sizeof(uint32_t));;
	*compids = comp_ids;

	int i = 0;

	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		const char *compid_s;
		compid_s = sqlite3_column_text(stmt, 0);
		comp_ids[i++] = strtoul(compid_s, NULL, 0);
		rc = sqlite3_step(stmt);
	}

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "sqlite3 error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}

	sqlite3_finalize(stmt);
}

int query_comp_id_by_name_cb(void *_comp, int argc, char **argv,
							char **col_name)
{
	struct oparser_comp *comp = (struct oparser_comp *)_comp;
	char *end;
	comp->comp_id = strtol(argv[0], &end, 10);
	if (!*argv[0] || *end) {
		fprintf(stderr, "Wrong format '%s'. Expecting metric_id.\n",
								argv[0]);
		exit(EPERM);
	}
	return 0;
}

void oquery_comp_id_by_name(char *name, uint32_t *comp_id, sqlite3 *db)
{
	struct oparser_comp comp;
	comp.name = strdup(name);
	char stmt[512];
	sprintf(stmt, "SELECT comp_id FROM components WHERE name='%s';",
								name);
	int rc;
	char *errmsg;

	rc = sqlite3_exec(db, stmt, query_comp_id_by_name_cb,
						&comp, &errmsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to query comp_id by name: %s\n",
				sqlite3_errmsg(db));
		sqlite3_free(errmsg);
		exit(rc);
	}
	*comp_id = comp.comp_id;
	free(comp.name);
	sqlite3_free(errmsg);
}

void
oquery_comp_id_by_uid(char *type, char *uid, uint32_t *comp_id, sqlite3 *db)
{
	struct oparser_comp comp;
	char stmt[512];
	sprintf(stmt, "SELECT comp_id FROM components WHERE type='%s' "
			"and identifier='%s';", type, uid);
	int rc;
	char *errmsg;

	rc = sqlite3_exec(db, stmt, query_comp_id_by_name_cb,
						&comp, &errmsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to query comp_id by name: %s\n",
				sqlite3_errmsg(db));
		sqlite3_free(errmsg);
		exit(rc);
	}
	*comp_id = comp.comp_id;
	sqlite3_free(errmsg);
}

int service_cfg_cb(void *_cmd_queue, int argc, char **argv, char **col_name)
{
	struct oparser_cmd_queue *cmd_queue =
				(struct oparser_cmd_queue *)_cmd_queue;
	if (argc != 2) {
		fprintf(stderr, "Error in '%s': expecting 2 columns "
				"but receive %d.\n", __FUNCTION__, argc);
		return EPERM;
	}

	int idx_verb, idx_av;
	if (strcmp(col_name[0], "verb") == 0) {
		idx_verb = 0;
		idx_av = 1;
	} else {
		idx_verb = 1;
		idx_av = 0;
	}

	struct oparser_cmd *cmd = malloc(sizeof(*cmd));
	if (!cmd)
		oom_report(__FUNCTION__);

	sprintf(cmd->verb, "%s", argv[idx_verb]);
	sprintf(cmd->attrs_values, "%s", argv[idx_av]);
	TAILQ_INSERT_TAIL(cmd_queue, cmd, entry);
	return 0;
}

int oquery_service_cfg(const char *hostname, const char *service,
		struct oparser_cmd_queue *cmd_queue, sqlite3 *db)
{
	char stmt[1024];
	sprintf(stmt, "SELECT verb, attr_value FROM services WHERE "
			"hostname='%s' AND service='%s'", hostname, service);

	int rc;
	char *errmsg;
	rc = sqlite3_exec(db, stmt, service_cfg_cb, cmd_queue, &errmsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to query service cfg: %s\n",
						sqlite3_errmsg(db));
		sqlite3_free(errmsg);
		return rc;
	}
	sqlite3_free(errmsg);
	return 0;
}

void oquery_tree_parent(uint32_t child_comp_id, char *parent, size_t str_len, sqlite3 *db)
{
	int rc;
	char *errmsg;
	char command[512];
	struct sqlite3_stmt *stmt;
	sprintf(command, "SELECT components.type, components.identifier FROM component_trees JOIN "
			"components WHERE "
			"component_trees.parent = components.comp_id AND "
			"component_trees.child =%" PRIu32 ";", child_comp_id);

	rc = sqlite3_prepare_v2(db, command, 512, &stmt, 0);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		fprintf(stderr, "sqlite3_prepare_v2 error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}

	int i = 0;
	size_t avai_len = str_len;
	size_t len = 0;
	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		const char *type, *identifier;
		type = sqlite3_column_text(stmt, 0);
		identifier = sqlite3_column_text(stmt, 1);
		if (i == 0) {
			len = sprintf(parent, "%s{%s}", type, identifier);
		} else {
			len = sprintf(parent, "%s,%s{%s}", parent, type, identifier);
		}

		if (len > avai_len) {
			fprintf(stderr, "%s: Out of memory\n", __FUNCTION__);
			exit(ENOMEM);
		}

		avai_len -= len;
		i++;
		rc = sqlite3_step(stmt);
	}

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "sqlite3 error: %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}

	sqlite3_finalize(stmt);
}
