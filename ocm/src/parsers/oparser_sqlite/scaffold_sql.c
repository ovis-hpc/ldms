/*
 * scaffold_sql.c
 *
 *  Created on: Oct 21, 2013
 *      Author: nichamon
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
 * \brief Get the comma-separated string of chrildren comp id
 * \param   children_s   A allocated string
 */
void get_children_string(char *children_s, int len,
				struct oparser_component *comp)
{
	int i;
	struct oparser_component *child;
	int each_len;
	int remain_len = len;
	for (i = 0; i < comp->num_child_types; i++) {
		LIST_FOREACH(child, &comp->children[i], entry) {
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
			i++;
		}
	}
}

int component_to_sqlite(struct oparser_component *comp, sqlite3 *db,
							sqlite3_stmt *stmt)
{
	int rc;
	char vary[2043];

	if (comp->name)
		oparser_bind_text(db, stmt, 1, comp->name, __FUNCTION__);
	else
		oparser_bind_text(db, stmt, 1, comp->comp_type->type,
						__FUNCTION__);

	oparser_bind_text(db, stmt, 2, comp->comp_type->type, __FUNCTION__);
	oparser_bind_int(db, stmt, 3, comp->comp_id, __FUNCTION__);

	if (comp->parent)
		oparser_bind_int64(db, stmt, 4, comp->parent->comp_id,
							__FUNCTION__);
	else
		oparser_bind_null(db, stmt, 4, __FUNCTION__);

	int i;
	struct oparser_component *child;

	oparser_finish_insert(db, stmt, __FUNCTION__);

	for (i = 0; i < comp->num_child_types; i++) {
		LIST_FOREACH(child, &comp->children[i], entry) {
			component_to_sqlite(child, db, stmt);
		}
	}

	return rc;
}

void oparser_scaffold_to_sqlite(struct oparser_scaffold *scaffold, sqlite3 *db)
{
	oparser_drop_table("components", db);
	int rc;
	char *stmt_s;
	stmt_s = "CREATE TABLE components(" \
		 "name		CHAR(64)	NOT NULL," \
		 "type		CHAR(64)	NOT NULL," \
		 "comp_id	SQLITE_uint32 PRIMARY KEY	NOT NULL," \
		 "parent_id	SQLITE_uint32);";

	char *index_stmt = "CREATE INDEX components_idx ON components(name,type);";
	create_table(stmt_s, index_stmt, db);

	stmt_s = "INSERT INTO components(name, type, comp_id, parent_id) " \
			"VALUES(@vname, @vtype, @vcomp_id, @vpid)";

	sqlite3_stmt *stmt;
	char *sqlite_err = 0;
	rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s), &stmt,
					(const char **)&sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: %s\n",__FUNCTION__, sqlite_err);
		exit(rc);
	}

	rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, sqlite_err);
		exit(rc);
	}

	int i;
	struct oparser_component_list *clist;
	struct oparser_component *comp;
	for (i = 0; i < scaffold->num_child_types; i++) {
		clist = &scaffold->children[i];
		LIST_FOREACH(comp, clist, entry)
			component_to_sqlite(comp, db, stmt);
	}

	sqlite3_finalize(stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, sqlite_err);
		exit(rc);
	}
}
