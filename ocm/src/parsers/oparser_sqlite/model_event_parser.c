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
/*
 * model_event_parser.c
 *
 *  Created on: Oct 22, 2013
 *      Author: nichamon
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/queue.h>
#include <stddef.h>
#include <inttypes.h>
#include <sqlite3.h>
#include <time.h>

#include "oparser_util.h"
#include "model_event_parser.h"
#include "oquery_sqlite.h"

sqlite3 *db;

struct oparser_model_q model_q;
struct oparser_action_q action_q;
struct oparser_event_q event_q;
struct oparser_event_q uevent_q;

struct oparser_model *model;
struct oparser_action *action;
struct oparser_event *event;
int event_id;
struct oparser_sev_level *sevl;

struct oparser_name_queue mnqueue; /* metric name queue */
int num_mnames; /* Number of metric names */
char metric_name[128];
struct metric_id_s *mid_s;
char prod_comp_types[256];
int num_cnames;
struct mae_metric_list metric_list;

int is_user_event;
uint32_t num_user_event;

static char *main_buf;
static char *main_value;
static int mae_line_count;
static FILE *log_fp;

struct mae_metric_comp {
	char metric_name[128];
	char prod_comp_type[512];
	LIST_ENTRY(mae_metric_comp) entry;
};
LIST_HEAD(mae_metric_comp_list, mae_metric_comp) metric_comp_list;

/* mae = model action event */
static enum mae_parser_obj {
	MAE_OBJ_MODEL = 0,
	MAE_OBJ_ACTION,
	MAE_OBJ_EVENT
} objective;

void mae_parser_log(const char *fmt, ...)
{
	va_list ap;
	time_t t;
	struct tm *tm;
	char dtsz[200];

	t = time(NULL);
	tm = localtime(&t);
	if (strftime(dtsz, sizeof(dtsz), "%a %b %d %H:%M:%S %Y", tm))
		fprintf(log_fp, "%s: ", dtsz);
	fprintf(log_fp, "model_events: line %d: ", mae_line_count);
	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	fflush(log_fp);
}

void oparser_mae_parser_init(FILE *log_file, sqlite3 *_db, char *read_buf, char *value_buf)
{
	TAILQ_INIT(&model_q);
	TAILQ_INIT(&action_q);
	TAILQ_INIT(&event_q);
	TAILQ_INIT(&uevent_q);
	LIST_INIT(&metric_list);
	LIST_INIT(&metric_comp_list);
	event_id = 0;
	db = _db;
	is_user_event = 0;
	num_user_event = 0xF0000000;

	if (log_file)
		log_fp = log_file;
	else
		log_fp = stderr;

	main_buf = read_buf;
	main_value = value_buf;
	mae_line_count = 0;
}

static void handle_model(char *value)
{
	objective = MAE_OBJ_MODEL;
	model = calloc(1, sizeof(*model));
	if (!model)
		oom_report(__FUNCTION__);
	TAILQ_INSERT_TAIL(&model_q, model, entry);
}

void handle_metric_name(char *value)
{
	struct mae_metric_comp *m;
	char prod_comps[512], tmp[512];
	char *s;
	char metric_name_full[512];
	int num;

	prod_comps[0] = '\0';
	num = sscanf(value, "%[^{]{%[^}]}", metric_name_full, tmp);

	char core_name[256], variation[256];
	/* at least one comp type is given */
	if (num == 2) {
		s = strtok(tmp, ",");
		sprintf(prod_comps, "'%s'", s);
		while (s = strtok(NULL, ",")) {
			sprintf(prod_comps, "%s,'%s'", prod_comps, s);
		}
	}

	char *wild_card = strstr(metric_name_full, "[*]");
	if (wild_card) {
		int len;
		while (wild_card) {
			len = wild_card - metric_name_full;
			wild_card = wild_card + strlen("[*]");

			sprintf(metric_name_full, "%.*s%%%s", len,
					metric_name_full, wild_card);
			wild_card = strstr(metric_name_full, "[*]");
		}

		m = malloc(sizeof(*m));
		sprintf(m->metric_name, "%s", metric_name_full);
		sprintf(m->prod_comp_type, "%s", prod_comps);
		LIST_INSERT_HEAD(&metric_comp_list, m, entry);
		return;
	}

	char *need_expand = strchr(metric_name_full, '[');
	if (need_expand) {
		num = process_string_name(metric_name_full, &mnqueue,
							NULL, NULL);
		struct oparser_name *name;
		TAILQ_FOREACH(name, &mnqueue, entry) {
			m = malloc(sizeof(*m));
			sprintf(m->metric_name, "%s", name->name);
			sprintf(m->prod_comp_type, "%s", prod_comps);
			LIST_INSERT_HEAD(&metric_comp_list, m, entry);
		}
		empty_name_list(&mnqueue);
	} else {
		/* Nothing to be expanded */
		m = malloc(sizeof(*m));
		sprintf(m->metric_name, "%s", metric_name_full);
		sprintf(m->prod_comp_type, "%s", prod_comps);
		LIST_INSERT_HEAD(&metric_comp_list, m, entry);
	}
}

static void handle_name(char *value)
{
	struct oparser_name *mname;
	struct mae_metric_comp *m;
	char **name;
	int num, i;
	char tmp[64];
	switch (objective) {
	case MAE_OBJ_MODEL:
		name = &model->name;
		goto name;
	case MAE_OBJ_ACTION:
		name = &action->name;
		goto name;
	case MAE_OBJ_EVENT:
		name = &event->name;
		goto name;
	default:
		mae_parser_log("invalid objective '%d'.\n", objective);
		exit(EINVAL);
	}
name:
	*name = strdup(value);
	if (!*name)
		oom_report(__FUNCTION__);
}

static void handle_model_id(char *value)
{
	uint16_t *model_id;
	switch (objective) {
	case MAE_OBJ_MODEL:
		model_id = &model->model_id;
		break;
	case MAE_OBJ_EVENT:
		model_id = &event->model_id;
		break;
	default:
		mae_parser_log("invalid objective '%d'.\n", objective);
		exit(EINVAL);
	}
	char *end;
	*model_id = (unsigned short) strtoul(value, &end, 10);

	if (!*value || *end) {
		mae_parser_log("%dmodel_id '%s' is not "
				"an integer.\n",
				-EINVAL, value);
		exit(EINVAL);
	}

}

static void handle_thresholds(char *value)
{
	model->thresholds = strdup(value);
	if (!model->thresholds)
		oom_report(__FUNCTION__);
}

static void handle_parameters(char *value)
{
	model->params = strdup(value);
	if (!model->params)
		oom_report(__FUNCTION__);
}

static void handle_report_flags(char *value)
{
	if (strlen(value) > 4) {
		mae_parser_log("report_flags need to be 4 binary digits, "
				"e.g. 0011. Received '%s'\n", value);
		exit(EINVAL);
	}
	snprintf(model->report_flags, MAE_NUM_LEVELS + 1, "%s", value);
}

static void handle_action(char *value)
{
	objective = MAE_OBJ_ACTION;
	objective = MAE_OBJ_ACTION;
	action = calloc(1, sizeof(*action));
	if (!action)
		oom_report(__FUNCTION__);
	TAILQ_INSERT_TAIL(&action_q, action, entry);
	return;
}

static void handle_action_name(char *value)
{
	/* Reuse the num_mnames and mnqueue for the action list*/
	num_mnames = process_string_name(value, &mnqueue, NULL, NULL);
	struct oparser_name *name;
	int i = 0;
	TAILQ_FOREACH(name, &mnqueue, entry) {
		if (i == 0) {
			sprintf(sevl->action_name, "%s", name->name);
		} else {
			sprintf(sevl->action_name, "%s,%s",
				sevl->action_name, name->name);
		}
		i++;
	}
	empty_name_list(&mnqueue);
}

static void handle_execute(char *value)
{
	action->execute = strdup(value);
	if (!action->execute)
		oom_report(__FUNCTION__);
}

static void handle_action_type(char *value)
{
	if (0 == strcasecmp(value, "corrective")) {
		action->type = strdup("corrective");
	} else {
		mae_parser_log("Invalid action type '%s': Support only "
			"'corrective' type. If no type is given, the "
			"type is assumed to be 'not-corrective'.\n", value);
		exit(EINVAL);
	}
}

void create_event()
{
	objective = MAE_OBJ_EVENT;
	event = calloc(1, sizeof(*event));
	if (!event)
		oom_report(__FUNCTION__);
	event_id++;
	event->event_id = event_id; /* start from 1 */
	LIST_INIT(&event->mid_list);
	LIST_INIT(&metric_list);
	if (!is_user_event) {
		TAILQ_INSERT_TAIL(&event_q, event, entry);
	} else {
		TAILQ_INSERT_TAIL(&event_q, event, entry);
		TAILQ_INSERT_TAIL(&uevent_q, event, uevent_entry);
	}

}

void mae_print_event();
static void handle_event(char *value)
{
	is_user_event = 0;
	create_event();
}

static void handle_metric(char *value)
{
	mid_s = calloc(1, sizeof(*mid_s));
	if (!mid_s)
		oom_report(__FUNCTION__);
	LIST_INSERT_HEAD(&event->mid_list, mid_s, entry);
	event->num_metric_id_set++;
	LIST_INIT(&metric_comp_list);
}

void create_metric_id_user_event(char *type, struct oparser_name_queue *nqueue)
{
	uint64_t metric_id;
	uint32_t offset_mtype_id = 0xF0000000;
	uint32_t comp_id;
	int i = 0;
	struct oparser_name *uid;
	TAILQ_FOREACH(uid, nqueue, entry) {
		oquery_comp_id_by_uid(type, uid->name, &comp_id, db);
		metric_id = gen_metric_id(comp_id, num_user_event);
		mid_s->metric_ids[i++] = metric_id;
	}
}

void _handle_components_uevent(char *value)
{
	handle_metric(NULL);

	if (strcmp(value, "*") == 0) {
		event->ctype = strdup("ALL");
		int numcomps;
		oquery_numb_comps(&numcomps, db);
		mid_s->num_metric_ids = numcomps;
		mid_s->metric_ids = malloc(numcomps * sizeof(uint64_t));

		int i;
		uint64_t metricid;
		for (i = 0; i < numcomps; i++) {
			/* Assume that the comp ID starts from 1 to numcomps. */
			metricid = gen_metric_id(i + 1, num_user_event);
			mid_s->metric_ids[i] = metricid;
		}
	} else {
		char type[512];
		char *uids = main_buf;
		int rc;
		rc = sscanf(value, " %[^{/\n]{%[^}]/", type, uids);
		if (rc == 1) {
			mae_parser_log("%s: %s: No user id.\n",
					__FUNCTION__, value);
			exit(EINVAL);
		}
		event->ctype = strdup(type);

		int numcomps;
		uint32_t *comp_ids;
		if (strcmp(uids, "*") == 0) {
			oquery_comp_id_by_type(type, &numcomps, &comp_ids, db);
			mid_s->num_metric_ids = numcomps;
			mid_s->metric_ids = malloc(numcomps *
							sizeof(uint64_t));
			uint64_t metricid;
			int i;
			for (i = 0; i < numcomps; i++) {
				metricid = gen_metric_id(comp_ids[i], num_user_event);
				mid_s->metric_ids[i] = metricid;
			}
			return;
		}

		struct oparser_name_queue cnqueue;
		int num_cnames;
		num_cnames = process_string_name(uids, &cnqueue, NULL, NULL);
		mid_s->num_metric_ids = num_cnames;
		mid_s->metric_ids = malloc(num_cnames * sizeof(uint64_t));
		create_metric_id_user_event(type, &cnqueue);
		empty_name_list(&cnqueue);
	}
}

static void handle_components(char *value)
{
	struct oparser_name_queue cnqueue;
	struct mae_metric_comp *m;
	struct mae_metric *metric;
	int i;

	/* handle the user-generated events */
	if (is_user_event) {
		_handle_components_uevent(value);
		return;
	}


	/* handle the regular events */
	if (strcmp(value, "*") == 0) {
		LIST_FOREACH(m, &metric_comp_list, entry) {
			oquery_metric_id(m->metric_name, m->prod_comp_type,
						&metric_list, NULL, db);
		}
		goto assign;
	}

	char type[512];
	char *uids = main_buf;
	int rc;
	rc = sscanf(value, " %[^{/\n]{%[^}]/", type, uids);
	if (rc == 1) {
		mae_parser_log("%s: %s: No user id.\n",
				__FUNCTION__, value);
		exit(EINVAL);
	}

	/* The component type that collects the metrics is given. */
	if (strcmp(uids, "*") == 0) {
		LIST_FOREACH(m, &metric_comp_list, entry) {
			oquery_metric_id_by_coll_type(m->metric_name,
				m->prod_comp_type, &metric_list,
				type, NULL, db);
		}
		goto assign;
	}


	/* The specific component type and identifiers are given. */
	num_cnames = process_string_name(uids, &cnqueue, NULL, NULL);

	struct oparser_name *name;
	char ids[1024];
	name = TAILQ_FIRST(&cnqueue);
	sprintf(ids, "'%s'", name->name);
	while (name = TAILQ_NEXT(name, entry))
		sprintf(ids, "%s,'%s'", ids, name->name);
	sprintf(ids, "%s", ids);
	empty_name_list(&cnqueue);

	LIST_FOREACH(m, &metric_comp_list, entry) {
		oquery_metric_id_by_coll_type(m->metric_name, m->prod_comp_type,
				&metric_list, type, ids, db);
	}

	while (m = LIST_FIRST(&metric_comp_list)) {
		LIST_REMOVE(m, entry);
		free(m);
	}

assign:
	if (LIST_EMPTY(&metric_list)) {
		mae_parser_log("Event %s: Could not find the metrics", event->name);
		exit(ENOENT);
	}
	LIST_FOREACH(metric, &metric_list, entry)
		mid_s->num_metric_ids++;

	mid_s->metric_ids = malloc(mid_s->num_metric_ids * sizeof(uint64_t));
	if (!mid_s->metric_ids)
		oom_report(__FUNCTION__);
	i = 0;
	LIST_FOREACH(metric, &metric_list, entry) {
		mid_s->metric_ids[i] = metric->metric_id;
		i++;
	}

	while (metric = LIST_FIRST(&metric_list)) {
		LIST_REMOVE(metric, entry);
		free(metric);
	}
}

static void handle_severity(char *value)
{
}

enum mae_me_sevl str_to_enum_level(char *value)
{
	if (strcmp(value, "info") == 0)
		return MAE_INFO;
	else if (strcmp(value, "nominal") == 0)
		return MAE_NOMINAL;
	else if (strcmp(value, "warning") == 0)
		return MAE_WARNING;
	else if (strcmp(value, "critical") == 0)
		return MAE_CRITICAL;

	mae_parser_log("Invalid severity level: %s\n", value);
	exit(EINVAL);
}

char *enum_level_to_str(enum mae_me_sevl level)
{
	switch (level) {
	case MAE_INFO:
		return "INFO";
	case MAE_NOMINAL:
		return "NOMINAL";
	case MAE_WARNING:
		return "WARNING";
	case MAE_CRITICAL:
		return "CRITICAL";
	default:
		break;
	}
	return NULL;
}

static void handle_level(char *value)
{
	enum mae_me_sevl level = str_to_enum_level(value);
	sevl = &event->msg_level[level];
}

static void handle_msg(char *value)
{
	char *s = strstr(value, "$(name)");
	if (!s) {
		sevl->msg = strdup(value);
		goto out;
	}

	int len = s - value;
	s = s + strlen("$(name)");
	char _msg[1024];
	sprintf(_msg, "%.*s%s%s", len, value, event->name, s);
	sevl->msg = strdup(_msg);
out:
	if (!sevl->msg)
		oom_report(__FUNCTION__);
}

static void handle_user_event(char *value)
{
	is_user_event = 1;
	create_event();
	event->model_id = 0xFFFF;
	num_user_event++;
	event->fake_mtype_id = num_user_event;
}

static struct kw label_tbl[] = {
	{ "action", handle_action },
	{ "action_name", handle_action_name },
	{ "action_type", handle_action_type },
	{ "components", handle_components },
	{ "event", handle_event },
	{ "execute", handle_execute },
	{ "level", handle_level },
	{ "metric", handle_metric },
	{ "metric_name", handle_metric_name },
	{ "model", handle_model },
	{ "model_id", handle_model_id },
	{ "msg", handle_msg },
	{ "name", handle_name },
	{ "parameters", handle_parameters },
	{ "report_flags", handle_report_flags },
	{ "severity", handle_severity },
	{ "thresholds", handle_thresholds },
	{ "user-event", handle_user_event },
};

void oparser_parse_model_event_conf(FILE *conf)
{
	char key[128];
	char *s;

	struct kw keyword;
	struct kw *kw;

	fseek(conf, 0, SEEK_SET);

	while (s = fgets(main_buf, MAIN_BUF_SIZE, conf)) {
		mae_line_count++;
		if (s[0] == '\n')
			continue;
		sscanf(main_buf, " %[^:]: %[^\n]", key, main_value);
		trim_trailing_space(main_value);
		/* Ignore the comment line */
		if (key[0] == '#')
			continue;

		keyword.token = key;
		kw = bsearch(&keyword, label_tbl, ARRAY_SIZE(label_tbl),
				sizeof(*kw), kw_comparator);

		if (kw) {
			kw->action(main_value);
		} else {
			mae_parser_log("Invalid key '%s'\n", key);
			exit(EINVAL);
		}

	}
}

void mae_print_model(struct oparser_model *model, FILE *out)
{
	fprintf(out, "model:\n");
	fprintf(out, "\tname: %s\n", model->name);
	fprintf(out, "\tmodel_id: %d\n", model->model_id);
	fprintf(out, "\tthresholds: %s\n", model->thresholds);
	fprintf(out, "\tparam: %s\n", model->params);
	fprintf(out, "\treport_flags: %s\n", model->report_flags);
}

void mae_print_action(struct oparser_action *action, FILE *out)
{
	fprintf(out, "action:\n");
	fprintf(out, "\tname: %s\n", action->name);
	fprintf(out, "\texecute: %s\n", action->execute);
}

void mae_print_event(struct oparser_event *event, FILE *out)
{
	fprintf(out, "event:\n");
	fprintf(out, "\tevent_id: %d\n", event->event_id);

	int i, num = 0;
	LIST_FOREACH(mid_s, &event->mid_list, entry) {
		for (i = 0; i < mid_s->num_metric_ids; i++) {
			if (num == 0) {
				fprintf(out, "\tmetric_ids: %" PRIu64 "",
						mid_s->metric_ids[0]);
			} else {
				fprintf(out, ",%" PRIu64 "",
						mid_s->metric_ids[i]);
			}
			num++;
		}
	}

	fprintf(out, "\n");
	for (i = 0; i < MAE_NUM_LEVELS; i++) {
		fprintf(out, "\t\tlevel: %s\n", enum_level_to_str(i));
		fprintf(out, "\t\t\tmsg: %s\n", event->msg_level[i].msg);
		fprintf(out, "\t\t\taction_name: %s\n",
					event->msg_level[i].action_name);
	}
	fprintf(out, "\n");
}

void oparser_print_models_n_rules(FILE *out)
{
	TAILQ_FOREACH(model, &model_q, entry)
		mae_print_model(model, out);

	TAILQ_FOREACH(action, &action_q, entry)
		mae_print_action(action, out);

	TAILQ_FOREACH(event, &event_q, entry)
		mae_print_event(event, out);
}

void model_to_sqlite(struct oparser_model *model, sqlite3 *db,
						sqlite3_stmt *stmt)
{
	oparser_bind_text(db, stmt, 1, model->name, __FUNCTION__);
	oparser_bind_int(db, stmt, 2, model->model_id, __FUNCTION__);

	if (!model->params)
		oparser_bind_null(db, stmt, 3, __FUNCTION__);
	else
		oparser_bind_text(db, stmt, 3, model->params, __FUNCTION__);

	if (!model->thresholds)
		oparser_bind_null(db, stmt, 4, __FUNCTION__);
	else
		oparser_bind_text(db, stmt, 4, model->thresholds, __FUNCTION__);

	if (!model->report_flags)
		oparser_bind_null(db, stmt, 5, __FUNCTION__);
	else
		oparser_bind_text(db, stmt, 5, model->report_flags,
							__FUNCTION__);

	oparser_finish_insert(db, stmt, __FUNCTION__);
}

void oparser_models_to_sqlite()
{
	oparser_drop_table("models", db);
	int rc;
	char *stmt_s;
	stmt_s = "CREATE TABLE IF NOT EXISTS models(" \
			" name		CHAR(64)	NOT NULL," \
			" model_id	INTEGER	PRIMARY KEY	NOT NULL," \
			" params	TEXT," \
			" thresholds	TEXT," \
			" report_flags	CHAR(4));";

	char *index_stmt = "CREATE INDEX IF NOT EXISTS models_idx ON models(name);";
	create_table(stmt_s, db);
	create_index(index_stmt, db);

	stmt_s = "INSERT INTO models(name, model_id, params, thresholds, "
			"report_flags) VALUES(@name, @mid, @param, @th, @rf)";
	sqlite3_stmt *stmt;
	char *errmsg;
	rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s), &stmt, NULL);
	if (rc) {
		mae_parser_log("%s: %s\n", __FUNCTION__, sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		mae_parser_log("%s: %s\n", __FUNCTION__, errmsg);
		exit(rc);
	}

	struct oparser_model *model;
	TAILQ_FOREACH(model, &model_q, entry) {
		model_to_sqlite(model, db, stmt);
	}

	sqlite3_finalize(stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		mae_parser_log("%s: %s\n", __FUNCTION__, errmsg);
		exit(rc);
	}
}

void oparser_actions_to_sqlite()
{
	oparser_drop_table("actions", db);
	int rc;
	char *stmt_s;
	sqlite3_stmt *stmt;
	char *errmsg;
	stmt_s = "CREATE TABLE IF NOT EXISTS actions(" \
			" name		CHAR(128) PRIMARY KEY	NOT NULL," \
			" execute	TEXT	NOT NULL," \
			" type		TEXT	NOT NULL);";

	create_table(stmt_s, db);

	stmt_s = "INSERT INTO actions(name, execute, type) VALUES(@name, @exec, @type)";
	rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s), &stmt, NULL);
	if (rc) {
		mae_parser_log("%s: %s\n", __FUNCTION__, sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		mae_parser_log("%s: %s\n", __FUNCTION__, errmsg);
		exit(rc);
	}

	char vary[512], insert_stmt[1024];
	struct oparser_action *action;
	TAILQ_FOREACH(action, &action_q, entry) {
		oparser_bind_text(db, stmt, 1, action->name, __FUNCTION__);
		oparser_bind_text(db, stmt, 2, action->execute, __FUNCTION__);
		if (action->type == 0) {
			oparser_bind_text(db, stmt, 3, "not-corrective", __FUNCTION__);
		} else {
			oparser_bind_text(db, stmt, 3, action->type, __FUNCTION__);
		}
		oparser_finish_insert(db, stmt, __FUNCTION__);
	}

	sqlite3_finalize(stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		mae_parser_log("%s: %s\n", __FUNCTION__, errmsg);
		exit(rc);
	}
}

void event_to_sqlite(struct oparser_event *event, sqlite3 *db,
		sqlite3_stmt *action_stmt, sqlite3_stmt *metric_stmt)
{
	int i, level;
	for (level = 0; level < MAE_NUM_LEVELS; level++) {
		if (!event->msg_level[level].msg &&
			event->msg_level[level].action_name[0] == '\0') {
			continue;
		}
		if (event->msg_level[level].msg &&
			event->msg_level[level].action_name[0] == '\0') {
			mae_parser_log("event %d: level %d: Need action\n",
				event->event_id, level);
			exit(EINVAL);
		}

		char *actions = strdup(event->msg_level[level].action_name);
		if (!actions) {
			mae_parser_log("Out of memory in event_to_sqlite\n");
			exit(ENOMEM);
		}

		char *action = strtok(actions, ",");
		while (action) {
			oparser_bind_int(db, action_stmt, 1, event->event_id,
								__FUNCTION__);
			oparser_bind_int(db, action_stmt, 2, level, __FUNCTION__);

			if (!event->msg_level[level].msg)
				oparser_bind_null(db, action_stmt, 3, __FUNCTION__);
			else
				oparser_bind_text(db, action_stmt, 3,
					event->msg_level[level].msg, __FUNCTION__);

			oparser_bind_text(db, action_stmt, 4, action, __FUNCTION__);

			oparser_finish_insert(db, action_stmt, __FUNCTION__);
			action = strtok(NULL, ",");
		}
		free(actions);
	}

	struct metric_id_s *mid;
	LIST_FOREACH(mid, &event->mid_list, entry) {
		for (i = 0; i < mid->num_metric_ids; i++) {
			oparser_bind_int(db, metric_stmt, 1, event->event_id,
								__FUNCTION__);
			oparser_bind_int(db, metric_stmt, 2,
					(uint32_t)(mid->metric_ids[i] >> 32),
					__FUNCTION__);
			oparser_bind_int64(db, metric_stmt, 3,
					mid->metric_ids[i], __FUNCTION__);
			oparser_finish_insert(db, metric_stmt, __FUNCTION__);
		}
	}
}

void user_event_to_sqlite(sqlite3 *db, sqlite3_stmt *uevent_stmt)
{
	struct oparser_event *uevent;
	int ueid;
	TAILQ_FOREACH(uevent, &uevent_q, uevent_entry) {
		oparser_bind_int(db, uevent_stmt, 1, uevent->event_id, __FUNCTION__);
		oparser_bind_text(db, uevent_stmt, 2, uevent->name, __FUNCTION__);
		oparser_bind_text(db, uevent_stmt, 3, uevent->ctype, __FUNCTION__);
		oparser_bind_int(db, uevent_stmt, 4, uevent->fake_mtype_id, __FUNCTION__);

		int count_level = 0;

		int i, level;
		for (level = 0; level < MAE_NUM_LEVELS; level++) {
			if (uevent->msg_level[level].action_name[0] == '\0') {
				continue;
			}

			oparser_bind_int(db, uevent_stmt, 5, level, __FUNCTION__);
			count_level++;
		}
		if (count_level != 1) {
			mae_parser_log("%s: Invalid user event definition. "
					"Exactly one severity level must be "
					"defined for a message and/or "
					"actions\n", uevent->name);
			exit(EINVAL);
		}

		oparser_finish_insert(db, uevent_stmt, __FUNCTION__);
	}
}

void oparser_events_to_sqlite()
{
	oparser_drop_table("rule_templates", db);
	oparser_drop_table("rule_actions", db);
	oparser_drop_table("rule_metrics", db);
	oparser_drop_table("user_events", db);
	int rc;
	char *stmt_s;
	stmt_s = "CREATE TABLE IF NOT EXISTS rule_templates (" \
			" event_id	INTEGER PRIMARY KEY	NOT NULL," \
			" event_name	TEXT			NOT NULL," \
			" model_id	INTEGER			NOT NULL);";
	create_table(stmt_s, db);

	stmt_s = "CREATE TABLE IF NOT EXISTS rule_actions (" \
			" event_id	INTEGER		NOT NULL," \
			" level		SMALLINT	NOT NULL," \
			" message	TEXT," \
			" action_name	CHAR(128)	NOT NULL," \
			" PRIMARY KEY(event_id, level, action_name));";
	create_table(stmt_s, db);

	stmt_s = "CREATE TABLE IF NOT EXISTS rule_metrics (" \
			" event_id	INTEGER		NOT NULL," \
			" comp_id	SQLITE_uint32	NOT NULL,"
			" metric_id	SQLITE_uint64	NOT NULL);";

	char *index_stmt = "CREATE INDEX IF NOT EXISTS rule_metrics_idx ON "
					"rule_metrics(event_id,comp_id,metric_id);";
	create_table(stmt_s, db);
	create_index(index_stmt, db);

	stmt_s = "CREATE TABLE IF NOT EXISTS user_events (" \
			" event_id	INTEGER	NOT NULL," \
			" event_name	TEXT		NOT NULL," \
			" comp_type	TEXT		NOT NULL," \
			" fake_mtype_id	INTEGER 	NOT NULL,"
			" level		INTEGER		NOT NULL);";
	index_stmt = "CREATE INDEX IF NOT EXISTS user_events_idx ON " \
			"user_events(event_id,comp_type,fake_mtype_id);";
	create_table(stmt_s, db);
	create_index(index_stmt, db);

	sqlite3_stmt *rule_templates_stmt;
	sqlite3_stmt *rule_actions_stmt;
	sqlite3_stmt *rule_metrics_stmt;
	sqlite3_stmt *user_event_stmt;
	char *errmsg;

	stmt_s = "INSERT INTO rule_templates(event_id, event_name, "
			"model_id) VALUES(@eid, @ename, @model_id)";
	rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s),
					&rule_templates_stmt, NULL);
	if (rc) {
		mae_parser_log("%s[%d]: error prepare[rule_templates]:"
				" %s\n", __FUNCTION__, rc, sqlite3_errmsg(db));
		exit(rc);
	}

	stmt_s = "INSERT INTO rule_actions(event_id, level, message, "
			"action_name) VALUES(@eid, @level, @msg, @aname)";
	rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s),
					&rule_actions_stmt, NULL);
	if (rc) {
		mae_parser_log("%s[%d]: error prepare[rule_actions]:"
				" %s\n", __FUNCTION__, rc, sqlite3_errmsg(db));
		exit(rc);
	}

	stmt_s = "INSERT INTO rule_metrics(event_id, comp_id, metric_id) "
			"VALUES(@eid, @cid, @mid)";
	rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s),
					&rule_metrics_stmt, NULL);
	if (rc) {
		mae_parser_log("%s[%d]: error prepare[rule_metrics]:"
				" %s\n", __FUNCTION__, rc, sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		mae_parser_log("%s: %s\n", __FUNCTION__, errmsg);
		exit(rc);
	}

	struct oparser_event *event;
	TAILQ_FOREACH(event, &event_q, entry) {

		oparser_bind_int(db, rule_templates_stmt, 1, event->event_id, __FUNCTION__);
		oparser_bind_text(db, rule_templates_stmt, 2, event->name, __FUNCTION__);
		oparser_bind_int(db, rule_templates_stmt, 3, event->model_id, __FUNCTION__);
		oparser_finish_insert(db, rule_templates_stmt, __FUNCTION__);

		event_to_sqlite(event, db, rule_actions_stmt,
						rule_metrics_stmt);
	}

	sqlite3_finalize(rule_templates_stmt);
	sqlite3_finalize(rule_actions_stmt);
	sqlite3_finalize(rule_metrics_stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		mae_parser_log("%s: %s\n", __FUNCTION__, errmsg);
		exit(rc);
	}

	stmt_s = "INSERT INTO user_events(event_id, event_name, comp_type, fake_mtype_id, level) "
			"VALUES(@eid, @ename, @ctype, @fake_mtype_id, @level)";
	rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s),
			&user_event_stmt, NULL);
	if (rc) {
		mae_parser_log("%s[%d]: error prepare[user_event]:"
				" %s\n", __FUNCTION__, rc, sqlite3_errmsg(db));
		exit(rc);
	}

	rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		mae_parser_log("%s: %s\n", __FUNCTION__, errmsg);
		exit(rc);
	}

	user_event_to_sqlite(db, user_event_stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		mae_parser_log("%s: %s\n", __FUNCTION__, errmsg);
		exit(rc);
	}

}

#ifdef MAIN
#include <stdarg.h>
#include <getopt.h>

#define FMT "m:d:o:"

int main(int argc, char **argv) {
	int rc, op;
	char *mae_conf, *db_path, *out_path;
	FILE *maef, *out;

	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'm':
			mae_conf = strdup(optarg);
			break;
		case 'd':
			db_path = strdup(optarg);
			break;
		case 'o':
			out_path = strdup(optarg);
			break;
		default:
			mae_parser_log("Invalid argument '%c'\n", op);
			exit(EINVAL);
		}
	}

	maef = fopen(mae_conf, "r");
	if (!maef) {
		mae_parser_log("Could not open the conf file '%s'\n", mae_conf);
		exit(errno);
	}

	out = fopen(out_path, "w");
	if (!maef) {
		mae_parser_log("Could not open the conf file '%s'\n", out_path);
		exit(errno);
	}

	sqlite3 *db;
	char *err_msg = 0;
	rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL);
	if (rc) {
		mae_parser_log("%d: Failed to open sqlite '%s': %s\n",
				rc, db_path, sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_free(err_msg);
		exit(rc);
	}

	oparser_mae_parser_init(db);
	printf("start...\n");
	oparser_parse_model_event_conf(maef);
	printf("finish parsing ... \n");
	oparser_print_models_n_rules(out);
	fflush(out);

	oparser_models_to_sqlite();
	printf("Finish models table\n");
	oparser_actions_to_sqlite();
	printf("Finish actions table\n");
	oparser_events_to_sqlite();
	printf("Finish events table\n");
	return 0;
}
#endif
