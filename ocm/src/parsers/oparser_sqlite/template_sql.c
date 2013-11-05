/*
 * template_sql.c
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
#include <inttypes.h>

#include "template_parser.h"
#include "oparser_util.h"

void template_def_to_sqlite(struct template_def *tmpl_def, sqlite3 *db)
{
	int i;
	char *tname = tmpl_def->name;
	struct template *tmpl;
	char *core = "INSERT INTO templates(name, apply_on, ldmsd_set, "
			"cfg, metrics) VALUES(";
	char stmt[4086];
	char vary[2043];
	struct set *set;
	struct oparser_metric *m;
	sqlite3_stmt *sql_stmt;

	for (i = 0; i < tmpl_def->num_tmpls; i++) {
		tmpl = &tmpl_def->templates[i];

		LIST_FOREACH(set, &tmpl->slist, entry) {
			if (tmpl->comp->name) {
				sprintf(vary, "'%s', '%s'",
						tname, tmpl->comp->name);
			} else {
				sprintf(vary, "'%s', '%s'", tname,
						tmpl->comp->comp_type->type);
			}
			sprintf(vary, "%s, '%s'", vary, set->ldmsd_set_name);
			sprintf(vary, "%s, '%s'", vary, set->cfg);

			m = LIST_FIRST(&set->mlist);
			sprintf(vary, "%s, '%s[%" PRIu64 "]", vary, m->name,
							m->metric_id);

			while (m = LIST_NEXT(m, set_entry)) {
				sprintf(vary, "%s,%s[%" PRIu64 "]",
						vary, m->name, m->metric_id);
			}

			sprintf(stmt, "%s%s');", core, vary);
			insert_data(stmt, db);
		}
	}
}

void template_defs_to_sqlite(struct building_sqlite_table *btable)
{
	struct template_def_list *tmpl_def_list;
	tmpl_def_list = (struct template_def_list *)btable->obj;
	sqlite3 *db = btable->db;
	int rc;
	char *stmt;

	stmt =	"CREATE TABLE templates( " \
			"name		CHAR(64)	NOT NULL, " \
			"apply_on	CHAR(64)	NOT NULL, " \
			"ldmsd_set	CHAR(128)	NOT NULL, " \
			"cfg		TEXT, " \
			"metrics	TEXT	NOT NULL );";
	char *index_stmt = "CREATE INDEX templates_idx ON templates(name,apply_on);";

	create_table(stmt, index_stmt, db);

	struct template_def *tmpl_def;
	LIST_FOREACH(tmpl_def, tmpl_def_list, entry) {
		template_def_to_sqlite(tmpl_def, db);
	}
	printf("Complete table 'templates'\n");
}

int get_parent_path(struct oparser_component *comp, char *path)
{
	int len;
	if (!comp) {
		len = sprintf(path, "/");
		return len;
	}

	int offset = 0;

	if (comp->parent)
		offset = get_parent_path(comp->parent, path);
	len = sprintf(path + offset, "/%" PRIu32, comp->comp_id);
	return offset + len;
}

void create_metric_record(struct oparser_metric *m, struct template *tmpl,
							char *vary)
{
	char path[1024];
	get_parent_path(m->comp->parent, path);
	if (tmpl->comp->name) {
		sprintf(vary, "'%s', %" PRIu64 ", %" PRIu32 ", "
				"'%s', %" PRIu32 ", '%s'",
				m->name, m->metric_id,
				m->mtype_id, tmpl->comp->name,
				m->comp->comp_id, path);
	} else {
		sprintf(vary, "'%s', %" PRIu64 ", %" PRIu32 ", "
				"'%s', %" PRIu32 ", '%s'",
				m->name, m->metric_id,
				m->mtype_id, tmpl->comp->comp_type->type,
				m->comp->comp_id, path);
	}
}

void _metrics_to_sqlite(struct template_def *tmpl_def, sqlite3 *db)
{
	int i;
	char *tname = tmpl_def->name;
	struct template *tmpl;
	char *core = "INSERT INTO metrics(name, metric_id, metric_type_id, "
			"coll_comp, prod_comp_id, path) VALUES(";

	char stmt[4086];
	char vary[2043];
	struct set *set;
	struct oparser_metric *m;
	sqlite3_stmt *sql_stmt;

	for (i = 0; i < tmpl_def->num_tmpls; i++) {

		tmpl = &tmpl_def->templates[i];

		LIST_FOREACH(set, &tmpl->slist, entry) {
			LIST_FOREACH(m, &set->mlist, set_entry) {
				create_metric_record(m, tmpl, vary);
				sprintf(stmt, "%s%s);", core, vary);
				insert_data(stmt, db);
			}


		}
	}
}

void metrics_to_sqlite(struct building_sqlite_table *btable)
{
	struct template_def_list *tmpl_def_list;
	tmpl_def_list = (struct template_def_list *)btable->obj;
	sqlite3 *db = btable->db;
	char *stmt;
	stmt =	"CREATE TABLE metrics( " \
		"name	CHAR(128)	NOT NULL, " \
		"metric_id	SQLITE_uint64 PRIMARY KEY	NOT NULL, " \
		"metric_type_id	SQLITE_uint32	NOT NULL, " \
		"coll_comp	CHAR(64)	NOT NULL, " \
		"prod_comp_id	SQLITE_uint32	NOT NULL, " \
		"path		TEXT );";

	char *index_stmt = "CREATE INDEX metrics_idx ON metrics(name," \
					"coll_comp,metric_type_id);";
	create_table(stmt, index_stmt, db);

	struct template_def *tmpl_def;
	LIST_FOREACH(tmpl_def, tmpl_def_list, entry) {
		_metrics_to_sqlite(tmpl_def, db);
	}
	printf("Complete table 'metrics'\n");
}

void oparser_metrics_to_sqlite(struct template_def_list *tmpl_def_list, sqlite3 *db)
{
	oparser_drop_table("metrics", db);
	char *stmt;
	stmt =	"CREATE TABLE metrics( " \
		"name	CHAR(128)	NOT NULL, " \
		"metric_id	SQLITE_uint64 PRIMARY KEY	NOT NULL, " \
		"metric_type_id	SQLITE_uint32	NOT NULL, " \
		"coll_comp	CHAR(64)	NOT NULL, " \
		"prod_comp_id	SQLITE_uint32	NOT NULL, " \
		"path		TEXT );";

	char *index_stmt = "CREATE INDEX metrics_idx ON metrics(name," \
					"coll_comp,metric_type_id);";
	create_table(stmt, index_stmt, db);

	struct template_def *tmpl_def;
	LIST_FOREACH(tmpl_def, tmpl_def_list, entry) {
		_metrics_to_sqlite(tmpl_def, db);
	}
}

void oparser_templates_to_sqlite(struct template_def_list *tmpl_def_list,
								sqlite3 *db)
{
	oparser_drop_table("templates", db);
	int rc;
	char *stmt;

	stmt =	"CREATE TABLE templates( " \
			"name		CHAR(64)	NOT NULL, " \
			"apply_on	CHAR(64)	NOT NULL, " \
			"ldmsd_set	CHAR(128)	NOT NULL, " \
			"cfg		TEXT, " \
			"metrics	TEXT	NOT NULL );";
	char *index_stmt = "CREATE INDEX templates_idx ON templates(name,apply_on);";

	create_table(stmt, index_stmt, db);

	struct template_def *tmpl_def;
	LIST_FOREACH(tmpl_def, tmpl_def_list, entry) {
		template_def_to_sqlite(tmpl_def, db);
	}
}
