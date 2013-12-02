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
#include <inttypes.h>

#include "template_parser.h"
#include "oparser_util.h"

void template_def_to_sqlite(struct template_def *tmpl_def, sqlite3 *db,
							sqlite3_stmt *stmt)
{
	int i;
	struct template *tmpl;
	char metrics[2043];
	struct set *set;
	struct oparser_metric *m;

	for (i = 0; i < tmpl_def->num_tmpls; i++) {
		tmpl = &tmpl_def->templates[i];

		LIST_FOREACH(set, &tmpl->slist, entry) {

			oparser_bind_text(db, stmt, 1, tmpl_def->name,
							__FUNCTION__);

			if (tmpl->comp->name) {
				oparser_bind_text(db, stmt, 2,
						tmpl->comp->name, __FUNCTION__);
			} else {
				oparser_bind_text(db, stmt, 2,
						tmpl->comp->comp_type->type,
						__FUNCTION__);
			}

			oparser_bind_text(db, stmt, 3, set->sampler_pi,
							__FUNCTION__);

			oparser_bind_text(db, stmt, 4, set->cfg, __FUNCTION__);

			m = LIST_FIRST(&set->mlist);
			sprintf(metrics, "%s[%" PRIu64 "]", m->name,
							m->metric_id);

			while (m = LIST_NEXT(m, set_entry)) {
				sprintf(metrics, "%s,%s[%" PRIu64 "]",
						metrics, m->name, m->metric_id);
			}

			oparser_bind_text(db, stmt, 5, metrics, __FUNCTION__);

			oparser_finish_insert(db, stmt, __FUNCTION__);
		}
	}
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
			struct set * set, sqlite3_stmt *stmt, sqlite3 *db)
{
	char path[1024];
	get_parent_path(m->comp->parent, path);

	oparser_bind_text(db, stmt, 1, m->name, __FUNCTION__);
	oparser_bind_int64(db, stmt, 2, m->metric_id, __FUNCTION__);
	oparser_bind_text(db, stmt, 3, set->sampler_pi, __FUNCTION__);
	oparser_bind_int(db, stmt, 4, m->mtype_id, __FUNCTION__);

	if (tmpl->comp->name)
		oparser_bind_text(db, stmt, 5, tmpl->comp->name, __FUNCTION__);
	else
		oparser_bind_text(db, stmt, 5, tmpl->comp->comp_type->type,
								__FUNCTION__);

	oparser_bind_int(db, stmt, 6, m->comp->comp_id, __FUNCTION__);
	oparser_bind_text(db, stmt, 7, path, __FUNCTION__);

	oparser_finish_insert(db, stmt, __FUNCTION__);
}

void oparser_metrics_to_sqlite(struct template_def_list *tmpl_def_list,
								sqlite3 *db)
{
	oparser_drop_table("metrics", db);
	char *stmt_s;
	stmt_s = "CREATE TABLE metrics( " \
		"name	CHAR(128)	NOT NULL, " \
		"metric_id	SQLITE_uint64 PRIMARY KEY NOT NULL, " \
		"sampler	TEXT, " \
		"metric_type_id	SQLITE_uint32	NOT NULL, " \
		"coll_comp	CHAR(64)	NOT NULL, " \
		"prod_comp_id	SQLITE_uint32	NOT NULL, " \
		"path		TEXT );";

	char *index_stmt = "CREATE INDEX metrics_idx ON metrics(name," \
					"coll_comp,metric_type_id);";
	create_table(stmt_s, index_stmt, db);

	stmt_s = "INSERT INTO metrics(name, metric_id, sampler, "
			"metric_type_id, coll_comp, prod_comp_id, path)"
			" VALUES(@name, @metric_id, @sampler, "
			"@mtid, @collc, @pcid, @path)";

	sqlite3_stmt *stmt;
	char *sqlite_err;
	int rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s), &stmt,
						(const char **)&sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, sqlite_err);
		exit(rc);
	}

	rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, sqlite_err);
		exit(rc);
	}

	int i;
	struct template_def *tmpl_def;
	struct template *tmpl;
	struct set *set;
	struct oparser_metric *m;
	LIST_FOREACH(tmpl_def, tmpl_def_list, entry) {
		for (i = 0; i < tmpl_def->num_tmpls; i++) {
			tmpl = &tmpl_def->templates[i];
			LIST_FOREACH(set, &tmpl->slist, entry) {
				LIST_FOREACH(m, &set->mlist, set_entry) {
					create_metric_record(m, tmpl, set,
								stmt, db);
				}
			}
		}
	}

	sqlite3_finalize(stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, sqlite_err);
		exit(rc);
	}
}

void oparser_templates_to_sqlite(struct template_def_list *tmpl_def_list,
								sqlite3 *db)
{
	oparser_drop_table("templates", db);
	int rc;
	char *stmt_s;

	stmt_s =	"CREATE TABLE templates( " \
			"name		CHAR(64)	NOT NULL, " \
			"apply_on	CHAR(64)	NOT NULL, " \
			"ldmsd_set	CHAR(128)	NOT NULL, " \
			"cfg		TEXT, " \
			"metrics	TEXT	NOT NULL );";
	char *index_stmt = "CREATE INDEX templates_idx ON templates(name,apply_on);";

	create_table(stmt_s, index_stmt, db);

	stmt_s = "INSERT INTO templates(name, apply_on, ldmsd_set, "
			"cfg, metrics) VALUES(@name, @apply_on, @ldmsd_set, "
			"@cfg, @metrics)";

	sqlite3_stmt *stmt;
	char *sqlite_err;
	rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s), &stmt,
					(const char **)&sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, sqlite_err);
		exit(rc);
	}

	rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, sqlite_err);
		exit(rc);
	}

	struct template_def *tmpl_def;
	LIST_FOREACH(tmpl_def, tmpl_def_list, entry) {
		template_def_to_sqlite(tmpl_def, db, stmt);
	}

	sqlite3_finalize(stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, sqlite_err);
		exit(rc);
	}
}
