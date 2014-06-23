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
#include <stddef.h>
#include <sqlite3.h>
#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "template_parser.h"
#include "component_parser.h"
#include "oparser_util.h"

/**
 * \brief Get the comma-separated string of children comp id
 * \param   children_s   A allocated string
 */
void get_children_string(char *children_s, int len,
				struct oparser_comp *comp)
{
	int i = 0;
	struct oparser_comp *child;
	int each_len;
	int remain_len = len;
	struct comp_array *charray;

	LIST_FOREACH(charray, &comp->children, entry) {
		for (i = 0; i < charray->num_comps; i++) {
			child = charray->comps[i];
			if (i == 0) {
				each_len = snprintf(children_s, remain_len,
						"%" PRIu32, child->comp_id);
			} else {
				each_len = snprintf(children_s, remain_len,
						"%" PRIu32, child->comp_id);
			}
			children_s += each_len;
			if (each_len == 0) {
				fprintf(stderr, "children string buffer "
							"is too small. size is %d."
							"the remain is %d."
							"No is %d\n",
							len, remain_len, i);
				exit(ENOMEM);
			}

			remain_len -= each_len;
			if (remain_len < 0) {
				fprintf(stderr, "children string buffer "
							"is too small. size is %d."
							"the remain is %d."
							"No is %d\n",
							len, remain_len, i);
				exit(ENOMEM);
			}

		}

	}
}

void handle_parents_components_table(struct oparser_comp *comp, sqlite3 *db,
						sqlite3_stmt *comp_stmt)
{
	char parents[2043];
	struct oparser_comp *parent;
	struct comp_array *carray;
	int num = 0;
	LIST_FOREACH(carray, &comp->parents, entry) {
		int i;
		for (i = 0; i < carray->num_comps; i++) {
			/* Create the comma-separated list of parent IDs. */
			parent = carray->comps[i];
			if (num == 0) {
				sprintf(parents, "%" PRIu32, parent->comp_id);
			} else {
				sprintf(parents, "%s,%" PRIu32, parents,
							parent->comp_id);
			}
			num++;
		}
	}
	oparser_bind_text(db, comp_stmt, CPARENT_IDX, parents, __FUNCTION__);
}

void component_to_sqlite(struct oparser_comp *comp, sqlite3 *db, sqlite3_stmt *comp_stmt)
{
	if (comp->is_stored == 1)
		return;

	if (comp->name)
		oparser_bind_text(db, comp_stmt, CNAME_IDX, comp->name, __FUNCTION__);
	else
		oparser_bind_text(db, comp_stmt, CNAME_IDX, comp->comp_type->type,
						__FUNCTION__);

	oparser_bind_text(db, comp_stmt, CTYPE_IDX, comp->comp_type->type, __FUNCTION__);
	oparser_bind_text(db, comp_stmt, CIDENTIFIER_IDX, comp->uid, __FUNCTION__);
	oparser_bind_int(db, comp_stmt, CCOMP_IDX, comp->comp_id, __FUNCTION__);

	/* parents */
	if (comp->num_ptypes) {
		handle_parents_components_table(comp, db, comp_stmt);
	} else {
		oparser_bind_null(db, comp_stmt, CPARENT_IDX, __FUNCTION__);
	}

	if (comp->comp_type->gif_path) {
		oparser_bind_text(db, comp_stmt, CGIF_PATH_IDX,
				comp->comp_type->gif_path, __FUNCTION__);
	} else {
		oparser_bind_null(db, comp_stmt, CGIF_PATH_IDX, __FUNCTION__);
	}

	oparser_bind_int(db, comp_stmt, CVISIBLE_IDX, comp->comp_type->visible,
								__FUNCTION__);

	if (comp->comp_type->category == NULL) {
		fprintf(stderr, "No category is given for component type '%s'\n",
							comp->comp_type->type);
		exit(EINVAL);
	} else {
		oparser_bind_text(db, comp_stmt, CCATEGORY_IDX,
				comp->comp_type->category, __FUNCTION__);
	}


	int i;
	struct oparser_comp *child;

	oparser_finish_insert(db, comp_stmt, __FUNCTION__);
	comp->is_stored = 1;

	struct comp_array *carray;
	LIST_FOREACH(carray, &comp->children, entry) {
		for (i = 0; i < carray->num_comps; i++) {
			child = carray->comps[i];
			component_to_sqlite(child, db, comp_stmt);
		}
	}
}

void gen_components_table(struct oparser_scaffold *scaffold, sqlite3 *db)
{
	oparser_drop_table("components", db);
	int rc;
	char *stmt_s;
	stmt_s = "CREATE TABLE components(" \
		 "name		CHAR(64)	NOT NULL," \
		 "type		CHAR(64)	NOT NULL," \
		 "identifier	TEXT		NOT NULL," \
		 "comp_id	INTEGER PRIMARY KEY	NOT NULL," \
		 "parent_id	TEXT," \
		 "gif_path	TEXT," \
		 "visible	INTEGER, " \
		 "category	TEXT		NOT NULL);";

	char *index_stmt = "CREATE INDEX components_idx ON components(type,identifier);";
	create_table(stmt_s, db);
	create_index(index_stmt, db);

	sqlite3_stmt *insert_stmt;

	stmt_s = "INSERT INTO components(name, type, identifier, comp_id," \
			" parent_id, gif_path, visible, category) " \
			"VALUES(@vname, @vtype, @videntifier, @vcomp_id, " \
			"@vpid, @gifpath, @visible, @category)";

	char *sqlite_err = 0;
	rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s),
						&insert_stmt, NULL);
	if (rc) {
		fprintf(stderr, "Failed to prepare 'INSERT INTO "
				"components(...)': %s\n", sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: Failed 'BEGIN TRANSACTION': %s\n",
				__FUNCTION__, sqlite_err);
		exit(rc);
	}

	struct oparser_comp *comp;

	LIST_FOREACH(comp, scaffold->children, root_entry)
		component_to_sqlite(comp, db, insert_stmt);

	sqlite3_finalize(insert_stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: Failed 'END TRANSACTION': %s\n",
						__FUNCTION__, sqlite_err);
		exit(rc);
	}
}

void insert_parent_child(struct oparser_comp *comp, sqlite3 *db,
					sqlite3_stmt *insert_stmt)
{
	if (comp->is_stored == 2)
		return;

	if (comp->is_stored == 0) {
		fprintf(stderr, "%s{%s} wasn't  stored.\n",
			comp->comp_type->type, comp->uid);
		exit(EPERM);
	}

	if (comp->is_stored == 1)
		comp->is_stored++;

	struct oparser_comp *parent, *child;
	struct comp_array *carray;
	int i;
	if (comp->num_ptypes > 0) {
		LIST_FOREACH(carray, &comp->parents, entry) {
			for (i = 0; i < carray->num_comps; i++) {
				parent = carray->comps[i];
				oparser_bind_int(db, insert_stmt, PARENT_IDX,
						parent->comp_id, __FUNCTION__);

				oparser_bind_int(db, insert_stmt, CHILD_IDX,
						comp->comp_id, __FUNCTION__);

				oparser_finish_insert(db, insert_stmt,
								__FUNCTION__);
			}
		}
	}

	LIST_FOREACH(carray, &comp->children, entry) {
		for (i = 0; i < carray->num_comps; i++) {
			child = carray->comps[i];
			insert_parent_child(child, db, insert_stmt);
		}
	}
}

void gen_component_relations_table(struct oparser_scaffold *scaffold, sqlite3 *db)
{
	char *stmt_s, *index_stmt;
	int rc = 0;

	oparser_drop_table("component_relations", db);
	stmt_s = "CREATE TABLE component_relations(" \
			"parent	INTEGER REFERENCES components ON DELETE CASCADE, " \
			"child	INTEGER REFERENCES components ON DELETE CASCADE);";

	create_table(stmt_s, db);

	index_stmt = "CREATE INDEX relations_parent_idx ON " \
			"component_relations(parent, child);";
	create_index(index_stmt, db);

	sqlite3_stmt *insert_stmt;

	stmt_s = "INSERT INTO component_relations(parent, child) " \
			"VALUES(@vparent_id, @vchild_id)";

	rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s),
					&insert_stmt, NULL);
	if (rc) {
		fprintf(stderr, "Failed to prepare 'INSERT INTO " \
				"component_relations()': %s\n",
				sqlite3_errmsg(db));
		exit(rc);
	}

	char *sqlite_err = 0;
	rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: Failed 'BEGIN TRANSACTION': %s\n",
						__FUNCTION__, sqlite_err);
		exit(rc);
	}

	struct oparser_comp *comp;

	LIST_FOREACH(comp, scaffold->children, root_entry) {
		insert_parent_child(comp, db, insert_stmt);
	}

	sqlite3_finalize(insert_stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: Failed 'END TRANSACTION': %s\n",
						__FUNCTION__, sqlite_err);
		exit(rc);
	}
}

void oparser_scaffold_to_sqlite(struct oparser_scaffold *scaffold, sqlite3 *db)
{
	gen_components_table(scaffold, db);
	gen_component_relations_table(scaffold, db);
}
