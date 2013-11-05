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

int component_to_sqlite(struct oparser_component *comp, sqlite3 *db)
{
	int rc;
	char stmt[4086];
	char vary[2043];
	char children[1024];
	char *core = "INSERT INTO components(name, type, comp_id"
			", parent_id, children_ids) VALUES(";
	if (comp->parent) {
		if (comp->name)
			sprintf(vary, "'%s', '%s', %" PRIu32 ", %" PRIu32 ", ",
				comp->name, comp->comp_type->type,
				comp->comp_id, comp->parent->comp_id);
		else
			sprintf(vary, "'%s', '%s', %" PRIu32 ", %" PRIu32 ", ",
				comp->comp_type->type, comp->comp_type->type,
				comp->comp_id, comp->parent->comp_id);
	} else {
		if (comp->name)
			sprintf(vary, "'%s', '%s', %" PRIu32 ", NULL, ",
					comp->name, comp->comp_type->type,
					comp->comp_id);
		else
			sprintf(vary, "'%s', '%s', %" PRIu32 ", NULL, ",
					comp->comp_type->type,
					comp->comp_type->type, comp->comp_id);
	}

	int i;
	struct oparser_component *child;
	for (i = 0; i < comp->num_child_types; i++) {
		LIST_FOREACH(child, &comp->children[i], entry) {
			if (i == 0) {
				sprintf(children, "'%" PRIu32, child->comp_id);
			} else {
				sprintf(children, "%s,%" PRIu32, children,
								child->comp_id);
			}
		}
	}

	if (comp->num_child_types > 0)
		sprintf(children, "%s'", children);
	else
		sprintf(children, "NULL");

	sprintf(stmt, "%s%s%s);", core, vary, children);
	insert_data(stmt, db);

	for (i = 0; i < comp->num_child_types; i++) {
		LIST_FOREACH(child, &comp->children[i], entry) {
			component_to_sqlite(child, db);
		}
	}

	return rc;
}

void scaffold_to_sqlite(struct building_sqlite_table *btable)
{
	struct oparser_scaffold *scaffold = (struct oparser_scaffold *)btable->obj;
	sqlite3 *db = btable->db;

	int rc;
	char *stmt;
	stmt =	"CREATE TABLE components(" \
			"name		CHAR(64)	NOT NULL," \
			"type		CHAR(64)	NOT NULL," \
			"comp_id	SQLITE_uint32 PRIMARY KEY	NOT NULL," \
			"parent_id	SQLITE_uint32," \
			"children_ids	TEXT);";

	char *index_stmt = "CREATE INDEX components_idx ON components(name,type);";
	create_table(stmt, index_stmt, db);

	int i;
	struct oparser_component_list *clist;
	struct oparser_component *comp;
	for (i = 0; i < scaffold->num_child_types; i++) {
		clist = &scaffold->children[i];
		LIST_FOREACH(comp, clist, entry)
			component_to_sqlite(comp, db);
	}
	printf("Complete table 'components'\n");
}

void oparser_scaffold_to_sqlite(struct oparser_scaffold *scaffold, sqlite3 *db)
{
	oparser_drop_table("components", db);
	int rc;
	char *stmt;
	stmt =	"CREATE TABLE components(" \
			"name		CHAR(64)	NOT NULL," \
			"type		CHAR(64)	NOT NULL," \
			"comp_id	SQLITE_uint32 PRIMARY KEY	NOT NULL," \
			"parent_id	SQLITE_uint32," \
			"children_ids	TEXT);";

	char *index_stmt = "CREATE INDEX components_idx ON components(name,type);";
	create_table(stmt, index_stmt, db);

	int i;
	struct oparser_component_list *clist;
	struct oparser_component *comp;
	for (i = 0; i < scaffold->num_child_types; i++) {
		clist = &scaffold->children[i];
		LIST_FOREACH(comp, clist, entry)
			component_to_sqlite(comp, db);
	}
}
