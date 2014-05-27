/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2014 Sandia Corporation. All rights reserved.
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
/*
 * cable_parser.c
 *
 *  Created on: May 14, 2014
 *      Author: Nichamon Naksinehaboon
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "cable_parser.h"

static char *main_buf;
static char *main_value;
sqlite3 *ovis_db;

static uint32_t comp_id;

struct cable_type *curr_cbtype;
struct cable_type_list cbtype_list;
struct cable_list cb_list;

void handle_cable_type(char *value)
{
	curr_cbtype = malloc(sizeof(*curr_cbtype));
	if (!curr_cbtype) {
		fprintf(stderr, "cable: %s: Out of memory.\n", value);
		exit(ENOMEM);
	}
	LIST_INSERT_HEAD(&cbtype_list, curr_cbtype, entry);
}

void handle_type(char *value)
{
	curr_cbtype->type = strdup(value);
	if (!curr_cbtype->type) {
		fprintf(stderr, "cable: %s: Out of memory.\n", value);
		exit(ENOMEM);
	}
}

void handle_description(char *value)
{
	curr_cbtype->description = strdup(value);
	if (!curr_cbtype) {
		fprintf(stderr, "cable: %s: Out of memory.\n", value);
		exit(ENOMEM);
	}
}

void cable_type_to_sqlite()
{
	oparser_drop_table("cable_types", ovis_db);
	char *stmt_s;
	stmt_s = "CREATE TABLE IF NOT EXISTS cable_types (" \
			"type_id INTEGER PRIMARY KEY AUTOINCREMENT, "
			"type TEXT UNIQUE NOT NULL, " \
			"description TEXT);";

	create_table(stmt_s, ovis_db);

	stmt_s = "INSERT INTO cable_types(" \
			"type, description) VALUES(" \
			"@type, @description);";

	char *insert_stmt[4096];
	sqlite3_stmt *stmt;
	char *errmsg;

	int rc = 0;
	rc = sqlite3_prepare_v2(ovis_db, stmt_s, strlen(stmt_s), &stmt, NULL);
	if (rc) {
		fprintf(stderr, "cable: Failed to prepare insert cable_types: "
				"%s\n", sqlite3_errmsg(ovis_db));
		exit(rc);
	}

	rc = sqlite3_exec(ovis_db, "BEGIN TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		fprintf(stderr, "cable: %s\n", errmsg);
		exit(rc);
	}

	struct cable_type *cbtype;
	LIST_FOREACH(cbtype, &cbtype_list, entry) {
		oparser_bind_text(ovis_db, stmt, 1, cbtype->type,
						__FUNCTION__);
		oparser_bind_text(ovis_db, stmt, 2, cbtype->description,
							__FUNCTION__);
		oparser_finish_insert(ovis_db, stmt, __FUNCTION__);
	}

	rc = sqlite3_exec(ovis_db, "END TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		fprintf(stderr, "cable: %s\n", errmsg);
		exit(rc);
	}
}

void _get_ctype_uidf(char *in_s, char **_ctype, char **_uidf)
{
	char *ctype, *uidf, *tmp;

	ctype = in_s;
	uidf = strchr(in_s, '{');
	*uidf = '\0';
	uidf = uidf + 1;
	tmp = strchr(uidf, '}');
	*tmp ='\0';

	*_ctype = ctype;
	*_uidf = uidf;
}

uint32_t query_comp_id(char *ctype, char *uidf)
{
	int rc = 0;
	int stmt_len = 1024;
	char stmt_s[stmt_len];
	struct sqlite3_stmt *stmt;
	sprintf(stmt_s, "SELECT comp_id FROM components WHERE type = '%s'"
			" and identifier = '%s';", ctype, uidf);

	rc = sqlite3_prepare_v2(ovis_db, stmt_s, strlen(stmt_s), &stmt, NULL);
	if ((rc != SQLITE_OK) && (rc != SQLITE_DONE)) {
		fprintf(stderr, "cable: %s: %s\n", stmt_s, sqlite3_errmsg(ovis_db));
		exit(rc);
	}

	uint32_t comp_id;
	rc = sqlite3_step(stmt);
	if (rc == SQLITE_DONE) {
		fprintf(stderr, "cable: No component '%s{%s}'\n", ctype, uidf);
		exit(EINVAL);
	} else if (rc == SQLITE_ROW) {
		comp_id = sqlite3_column_int(stmt, 0);
		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE)
			goto err;
	} else {
		goto err;
	}

	sqlite3_finalize(stmt);
	return comp_id;
err:
	fprintf(stderr, "cable: Query component failed: %s\n", sqlite3_errmsg(ovis_db));
	exit(rc);
}

int query_cable_type_id(char *type)
{
	int rc = 0;
	int stmt_len = 1024;
	char stmt_s[stmt_len];
	struct sqlite3_stmt *stmt;
	sprintf(stmt_s, "SELECT type_id FROM cable_types WHERE type = '%s';", type);

	rc = sqlite3_prepare_v2(ovis_db, stmt_s, strlen(stmt_s), &stmt, NULL);
	if ((rc != SQLITE_OK) && (rc != SQLITE_DONE)) {
		fprintf(stderr, "cable: %s: %s\n", stmt_s, sqlite3_errmsg(ovis_db));
		exit(rc);
	}

	int type_id;
	char *type_id_s;
	rc = sqlite3_step(stmt);

	if (rc == SQLITE_DONE) {
		fprintf(stderr, "cable: No cable type '%s'\n", type);
		exit(EINVAL);
	} else if (rc == SQLITE_ROW) {
		type_id = sqlite3_column_int(stmt, 0);
		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE)
			goto err;
	} else {
		goto err;
	}

	sqlite3_finalize(stmt);
	return type_id;
err:
	sqlite3_finalize(stmt);
	fprintf(stderr, "cable: Query cable_type failed: %s\n", sqlite3_errmsg(ovis_db));
	exit(rc);
}

void cable_to_components_table(struct cable *cable, sqlite3_stmt *insert_stmt)
{
	if (cable->name) {
		oparser_bind_text(ovis_db, insert_stmt, CNAME_IDX, cable->name,
							__FUNCTION__);
	} else {
		oparser_bind_text(ovis_db, insert_stmt, CNAME_IDX, cable->type,
							__FUNCTION__);
	}

	oparser_bind_text(ovis_db, insert_stmt, CTYPE_IDX, cable->type,
							__FUNCTION__);
	oparser_bind_text(ovis_db, insert_stmt, CIDENTIFIER_IDX, cable->name,
								__FUNCTION__);
	oparser_bind_int(ovis_db, insert_stmt, CCOMP_IDX, cable->comp_id,
								__FUNCTION__);

	char parents[1024];
	sprintf(parents, "%" PRIu32 ",%" PRIu32, cable->src, cable->dest);
	oparser_bind_text(ovis_db, insert_stmt, CPARENT_IDX, parents,
								__FUNCTION__);
	oparser_bind_null(ovis_db, insert_stmt, CGIF_PATH_IDX, __FUNCTION__);

	oparser_bind_int(ovis_db, insert_stmt, CVISIBLE_IDX, 1, __FUNCTION__);
	oparser_finish_insert(ovis_db, insert_stmt, __FUNCTION__);
}

void cable_to_component_relations(struct cable *cable, sqlite3_stmt *insert_stmt)
{
	/* source as parent */
	oparser_bind_int(ovis_db, insert_stmt, PARENT_IDX, cable->src,
							__FUNCTION__);
	oparser_bind_int(ovis_db, insert_stmt, CHILD_IDX, cable->comp_id,
							__FUNCTION__);
	oparser_finish_insert(ovis_db, insert_stmt, __FUNCTION__);

	/* destination as parent */
	oparser_bind_int(ovis_db, insert_stmt, PARENT_IDX, cable->dest,
							__FUNCTION__);
	oparser_bind_int(ovis_db, insert_stmt, CHILD_IDX, cable->comp_id,
							__FUNCTION__);
	oparser_finish_insert(ovis_db, insert_stmt, __FUNCTION__);
}

void cable_to_sqlite()
{
	oparser_drop_table("cables", ovis_db);
	char *stmt_s;
	stmt_s = "CREATE TABLE IF NOT EXISTS cables (" \
			"type_id	INTEGER REFERENCES cable_types, " \
			"src	INTEGER REFERENCES components ON DELETE CASCADE, " \
			"dest	INTEGER REFERENCES components ON DELETE CASCADE, " \
			"state	TEXT NOT NULL);";

	create_table(stmt_s, ovis_db);

	stmt_s = "CREATE INDEX cables_idx ON cables(type_id, src, dest);";
	create_index(stmt_s, ovis_db);

	sqlite3_stmt *cable_stmt, *comp_stmt, *relation_stmt;
	char *errmsg;
	int rc = 0;

	/* Prepare the insert statement to the cables table */
	stmt_s = "INSERT INTO cables(" \
			"type_id, src, dest, state) VALUES(" \
			"@type_id, @src, @dest, @state);";

	rc = sqlite3_prepare_v2(ovis_db, stmt_s, strlen(stmt_s),
						&cable_stmt, NULL);
	if (rc) {
		fprintf(stderr, "cable: Failed to prepare insert cable: %s\n",
						sqlite3_errmsg(ovis_db));
		exit(rc);
	}

	/* Prepare the insert statement to the components table */
	stmt_s = "INSERT INTO components(name, type, identifier, comp_id," \
			" parent_id, gif_path, visible) " \
			"VALUES(@vname, @vtype, @videntifier, @vcomp_id, " \
			"@vpid, @gifpath, @visible)";

	rc = sqlite3_prepare_v2(ovis_db, stmt_s, strlen(stmt_s),
							&comp_stmt, NULL);
	if (rc) {
		fprintf(stderr, "cable: Failed to prepare insert components"
					": %s\n", sqlite3_errmsg(ovis_db));
		exit(rc);
	}

	stmt_s = "INSERT INTO component_relations(parent, child) " \
				"VALUES(@vparent_id, @vchild_id)";
	rc = sqlite3_prepare_v2(ovis_db, stmt_s, strlen(stmt_s),
						&relation_stmt, NULL);
	if (rc) {
		fprintf(stderr, "cable: Failed to prepare insert "
					"component_relations: %s\n",
					sqlite3_errmsg(ovis_db));
		exit(rc);
	}

	rc = sqlite3_exec(ovis_db, "BEGIN TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		fprintf(stderr, "cable: %s\n", errmsg);
		exit(rc);
	}

	struct cable *cable;
	LIST_FOREACH(cable, &cb_list, entry) {
		oparser_bind_int(ovis_db, cable_stmt, 1, cable->type_id, __FUNCTION__);
		oparser_bind_int(ovis_db, cable_stmt, 2, cable->src, __FUNCTION__);
		oparser_bind_int(ovis_db, cable_stmt, 3, cable->dest, __FUNCTION__);
		oparser_bind_text(ovis_db, cable_stmt, 4, cable->state, __FUNCTION__);
		oparser_finish_insert(ovis_db, cable_stmt, __FUNCTION__);

		cable_to_components_table(cable, comp_stmt);
		cable_to_component_relations(cable, relation_stmt);

		LIST_REMOVE(cable, entry);
		free(cable);
	}

	sqlite3_finalize(cable_stmt);

	rc = sqlite3_exec(ovis_db, "END TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		fprintf(stderr, "cable: %s\n", errmsg);
		exit(rc);
	}
}

struct cable_type *find_cable_type(char *type)
{
	struct cable_type *cbtype;
	LIST_FOREACH(cbtype, &cbtype_list, entry) {
		if (0 == strcmp(cbtype->type, type))
			return cbtype;
	}

	fprintf(stderr, "cable: Couldn't find the cable of type '%s'\n", type);
	exit(ENOENT);
}

struct cable *new_cable(char *name)
{
	struct cable *cable = malloc(sizeof(*cable));
	if (!cable) {
		fprintf(stderr, "cable: %s: Out of memory\n", name);
		exit(ENOMEM);
	}
	cable->name = strdup(name);
	if (!cable->name) {
		fprintf(stderr, "cable: %s: Out of memory\n", name);
		exit(ENOMEM);
	}
	cable->comp_id = comp_id;
	comp_id++;
	return cable;

}

void handle_cables(FILE *conff)
{
	cable_type_to_sqlite();

	char *s, *tmp;
	char uid[1024];
	char *ctype, *uidf;

	struct cable *cable;

	while (s = fgets(main_buf, MAIN_BUF_SIZE, conff)) {
		/* Ignore the comment line */
		if (main_buf[0] == '#')
			continue;

		tmp = strtok(main_buf, "\t");
		cable = new_cable(tmp);
		if (!cable) {
			fprintf(stderr, "cable: Out of memory\n");
			exit(ENOMEM);
		}

		tmp = strtok(NULL, "\t");
		cable->type_id = query_cable_type_id(tmp);
		cable->type = strdup(tmp);

		tmp = strtok(NULL, "\t");
		_get_ctype_uidf(tmp, &ctype, &uidf);
		cable->src = query_comp_id(ctype, uidf);

		tmp = strtok(NULL, "\t");
		_get_ctype_uidf(tmp, &ctype, &uidf);
		cable->dest = query_comp_id(ctype, uidf);


		tmp = strtok(NULL, "\t");
		if (!tmp) {
			cable->state = strdup("OK");
		} else {
			cable->state = strdup(tmp);
		}

		LIST_INSERT_HEAD(&cb_list, cable, entry);
	}
}

static struct kw lable_tbl[] = {
	{ "cable_type", handle_cable_type },
	{ "desc", handle_description },
	{ "type", handle_type },
};

void oparser_cable_init(char *read_buf, char *value_buf, sqlite3 *db)
{
	main_buf = read_buf;
	main_value = value_buf;

	ovis_db = db;

	LIST_INIT(&cbtype_list);
	LIST_INIT(&cb_list);
}

int query_max_comp_id()
{
	int rc = 0;
	char *stmt_s;
	struct sqlite3_stmt *stmt;
	stmt_s = "SELECT MAX(comp_id) FROM components;";

	rc = sqlite3_prepare_v2(ovis_db, stmt_s, strlen(stmt_s), &stmt, NULL);
	if ((rc != SQLITE_OK) && (rc != SQLITE_DONE)) {
		fprintf(stderr, "cable: %s: %s\n", stmt_s, sqlite3_errmsg(ovis_db));
		exit(rc);
	}

	int max_comp_id;
	char *type_id_s;
	rc = sqlite3_step(stmt);

	if (rc == SQLITE_DONE) {
		fprintf(stderr, "cable: Could not get the max comp_id from "
						"the components table\n");
		exit(EINVAL);
	} else if (rc == SQLITE_ROW) {
		max_comp_id = sqlite3_column_int(stmt, 0);
		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE)
			goto err;
	} else {
		goto err;
	}

	sqlite3_finalize(stmt);
	return max_comp_id;
err:
	sqlite3_finalize(stmt);
	fprintf(stderr, "cable: Query max comp_id failed: %s\n",
					sqlite3_errmsg(ovis_db));
	exit(rc);
}

void oparser_parse_cable_def(FILE *conff) {
	comp_id = query_max_comp_id();
	comp_id++;

	char key[128];
	char *s;

	struct kw keyword;
	struct kw *kw;

	fseek(conff, 0, SEEK_SET);
	while (s = fgets(main_buf, MAIN_BUF_SIZE, conff)) {
		sscanf(main_buf, " %[^:]: %[^\t\n]", key, main_value);
		trim_trailing_space(main_value);

		/* Ignore the comment line */
		if (key[0] == '#')
			continue;

		if (strcmp(key, "cables") == 0) {
			handle_cables(conff);
			break;
		}

		keyword.token = key;
		kw = bsearch(&keyword, lable_tbl, ARRAY_SIZE(lable_tbl),
				sizeof(*kw), kw_comparator);

		if (kw) {
			kw->action(main_value);
		} else {
			fprintf(stderr, "Invalid key '%s'\n", key);
			exit(EINVAL);
		}
	}

	cable_to_sqlite();
}
