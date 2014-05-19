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

#define METRIC_S_SIZE (1024 * 1024 * 2)

static char *metric_s;

void template_to_sqlite(struct template *tmpl, sqlite3 *db,
							sqlite3_stmt *stmt)
{
	int i;
	struct set *set;
	struct oparser_metric *m;

	oparser_bind_text(db, stmt, 1, tmpl->tmpl_def->name, __FUNCTION__);
	oparser_bind_text(db, stmt, 2, tmpl->host->name, __FUNCTION__);
	oparser_bind_text(db, stmt, 3, tmpl->tmpl_def->ldms_sampler,
								__FUNCTION__);
	oparser_bind_text(db, stmt, 4, tmpl->tmpl_def->cfg, __FUNCTION__);

	int is_first = 1;
	struct comp_metric_array *cma;
	LIST_FOREACH(cma, &tmpl->cma_list, entry) {
		for (i = 0; i < cma->num_cms; i++) {
			LIST_FOREACH(m, &cma->cms[i].mlist, comp_metric_entry) {
				if (is_first) {
					sprintf(metric_s, "%s[%" PRIu64 "]",
							m->name, m->metric_id);
					is_first = 0;
				} else {
					sprintf(metric_s, "%s,%s[%" PRIu64 "]",
						metric_s, m->name, m->metric_id);
				}
			}
		}
	}

	oparser_bind_text(db, stmt, 5, metric_s, __FUNCTION__);
	oparser_finish_insert(db, stmt, __FUNCTION__);
}

void create_metric_record(struct oparser_metric *m, struct template *tmpl,
			struct comp_metric *cm, sqlite3_stmt *stmt, sqlite3 *db)
{
	char path[1024];

	oparser_bind_text(db, stmt, 1, m->name, __FUNCTION__);
	oparser_bind_int64(db, stmt, 2, m->metric_id, __FUNCTION__);
	oparser_bind_text(db, stmt, 3, tmpl->tmpl_def->ldms_sampler,
							__FUNCTION__);
	oparser_bind_int(db, stmt, 4, m->mtype_id, __FUNCTION__);

	if (tmpl->host->name) {
		oparser_bind_text(db, stmt, 5, tmpl->host->name, __FUNCTION__);
	} else {
		oparser_bind_text(db, stmt, 5, tmpl->host->comp_type->type,
								__FUNCTION__);
	}

	oparser_bind_int(db, stmt, 6, cm->comp->comp_id, __FUNCTION__);
	oparser_finish_insert(db, stmt, __FUNCTION__);
}

void oparser_metrics_to_sqlite(struct tmpl_list *all_tmpl_list, sqlite3 *db)
{
	oparser_drop_table("metrics", db);
	char *stmt_s;
	stmt_s = "CREATE TABLE metrics( " \
		"name	CHAR(128)	NOT NULL, " \
		"metric_id	SQLITE_uint64 PRIMARY KEY NOT NULL, " \
		"sampler	TEXT, " \
		"metric_type_id	SQLITE_uint32	NOT NULL, " \
		"coll_comp	CHAR(64)	NOT NULL, " \
		"prod_comp_id	SQLITE_uint32	NOT NULL);";

	char *index_stmt = "CREATE INDEX metrics_idx ON metrics(" \
					"coll_comp,metric_type_id,metric_id);";
	create_table(stmt_s, db);
	create_index(index_stmt, db);

	stmt_s = "INSERT INTO metrics(name, metric_id, sampler, "
			"metric_type_id, coll_comp, prod_comp_id)"
			" VALUES(@name, @metric_id, @sampler, "
			"@mtid, @collc, @pcid)";

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
	struct template *tmpl;
	struct comp_metric_array *cma;
	struct comp_metric *cm;
	struct oparser_metric *m;
	LIST_FOREACH(tmpl, all_tmpl_list, entry) {
		LIST_FOREACH(cma, &tmpl->cma_list, entry) {
			for (i = 0; i < cma->num_cms; i++) {
				cm = &(cma->cms[i]);
				LIST_FOREACH(m, &cm->mlist, comp_metric_entry) {
					create_metric_record(m, tmpl, cm,
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

void oparser_templates_to_sqlite(struct tmpl_list *all_tmpl_list, sqlite3 *db)
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

	create_table(stmt_s, db);

	char *index_stmt = "CREATE INDEX templates_idx ON templates(apply_on, name);";
	create_index(index_stmt, db);

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

	metric_s = malloc(METRIC_S_SIZE);

	struct template *tmpl;
	LIST_FOREACH(tmpl, all_tmpl_list, entry) {
		template_to_sqlite(tmpl, db, stmt);
	}

	sqlite3_finalize(stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, sqlite_err);
		exit(rc);
	}

	free(metric_s);
}
