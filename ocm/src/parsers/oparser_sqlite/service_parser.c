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

#include <coll/str_map.h>
#include <sqlite3.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "oparser_util.h"
#include "service_parser.h"
#include "oquery_sqlite.h"

#define host_map_hash_size 2003

sqlite3 *db;
str_map_t host_scfg;
FILE *conf;

struct oparser_name_queue host_queue;
struct oparser_host_services *hservices;
enum ovis_service service;
struct oparser_cmd_queue *cmd_queue;
struct oparser_cmd *cmd;

struct oparser_name_queue agg_host_list;
struct oparser_name_queue baler_host_list;

void oparser_service_conf_init(sqlite3 *_db)
{
	db = _db;

	int rc;
	host_scfg = str_map_create(host_map_hash_size);
	if (!host_scfg) {
		fprintf(stderr, "%d: Failed to create str_map.\n", errno);
		exit(errno);
	}
	TAILQ_INIT(&host_queue);
	TAILQ_INIT(&agg_host_list);
	TAILQ_INIT(&baler_host_list);
}

static void handle_hostname(char *value)
{
	struct oparser_name *host;
	hservices = (struct oparser_host_services *)
					str_map_get(host_scfg, value);
	if (!hservices) {
		int i;
		hservices = malloc(sizeof(*hservices));
		if (!hservices)
			oom_report(__FUNCTION__);

		for (i = 0; i < OVIS_NUM_SERVICES; i++)
			TAILQ_INIT(&hservices->queue[i]);

		str_map_insert(host_scfg, value,
				(uint64_t)(unsigned char *)hservices);

		host = malloc(sizeof(*host));
		if (!host)
			oom_report(__FUNCTION__);
		host->name = strdup(value);
		if (!host->name)
			oom_report(__FUNCTION__);
		TAILQ_INSERT_TAIL(&host_queue, host, entry);
	}
}

static void handle_service(char *value)
{
	service = str_to_enum_ovis_service(value);
	if (service < 0) {
		fprintf(stderr, "Invalid service '%s'. The choices are "
				"ldmsd_sampler, ldmsd_aggregator, baler, "
				"me and komondor\n", value);
		exit(EINVAL);
	}
	cmd_queue = &hservices->queue[service];

	struct oparser_name *name;
	if (service == OVIS_LDMSD_AGG || service == OVIS_BALER) {
		name = malloc(sizeof(*name));
		if (!name)
			oom_report(__FUNCTION__);
		name->name = strdup(TAILQ_LAST(&host_queue,
					oparser_name_queue)->name);
		if (!name->name)
			oom_report(__FUNCTION__);

		if (service == OVIS_LDMSD_AGG)
			TAILQ_INSERT_TAIL(&agg_host_list, name, entry);
		else
			TAILQ_INSERT_TAIL(&baler_host_list, name, entry);
	}
}

static void handle_commands(char *value)
{
	/* do nothing */
}

void process_agg_add(struct oparser_cmd_queue *queue, char *attrs_values,
					struct oparser_cmd **first_new_cmd)
{
	int num_added_hosts;
	char *attrs, *key, *value;
	char tmp_attr[512];
	char *sets;
	char *hosts;

	tmp_attr[0] = '\0';
	attrs = strdup(cmd->attrs_values);
	if (!attrs)
		oom_report(__FUNCTION__);
	key = strtok(attrs, ":");
	while (key) {
		value = strtok(NULL, ";");
		if (strcmp(key, "host") == 0) {
			hosts = strdup(value);
		} else if (strcmp(key, "sets") == 0) {
			sets = strdup(value);
		} else {
			if (strlen(tmp_attr) == 0)
				sprintf(tmp_attr, "%s:%s", key, value);
			else
				sprintf(tmp_attr, "%s;%s:%s", tmp_attr,
								key, value);
		}
		key = strtok(NULL, ":");
	}

	int i;
	int num_sets;
	struct oparser_cmd *new_cmd;
	struct oparser_name_queue added_hqueue;
	struct oparser_name_queue setqueue;
	struct oparser_name *added_hname, *setname;
	num_added_hosts = process_string_name(hosts, &added_hqueue,
							NULL, NULL);
	num_sets = process_string_name(sets, &setqueue, NULL, NULL);

	added_hname = TAILQ_FIRST(&added_hqueue);
	for (i = 0; i < num_added_hosts; i++) {
		new_cmd = malloc(sizeof(*new_cmd));
		if (!new_cmd)
			oom_report(__FUNCTION__);

		if (i == 0)
			*first_new_cmd = new_cmd;
		TAILQ_INSERT_HEAD(queue, new_cmd, entry);
		sprintf(new_cmd->verb, "add");
		setname = TAILQ_FIRST(&setqueue);

		sprintf(new_cmd->attrs_values, "%s;host:%s;sets:%s/%s",
				tmp_attr, added_hname->name,
				added_hname->name, setname->name);

		while (setname = TAILQ_NEXT(setname, entry)) {
			sprintf(new_cmd->attrs_values, "%s,%s/%s",
					new_cmd->attrs_values, added_hname->name,
					setname->name);
		}
		added_hname = TAILQ_NEXT(added_hname, entry);
	}
}

void postprocess_agg_cmdlist(struct oparser_cmd_queue *queue)
{
	struct oparser_cmd *next_cmd, *new_cmd;
	new_cmd = NULL;
	cmd = TAILQ_FIRST(queue);
	while (cmd && cmd != new_cmd) {
		/* Check cmd != new_cmd to prevent infinite loop */
		next_cmd = TAILQ_NEXT(cmd, entry);
		if (strcmp(cmd->verb, "add") == 0) {
			TAILQ_REMOVE(queue, cmd, entry);
			process_agg_add(queue, cmd->attrs_values, &new_cmd);
			free(cmd);
		}
		cmd = next_cmd;
	}
}

void insert_baler_metric(uint64_t metric_id, uint32_t mtype_id,
				uint32_t comp_id, char *hostname,
				sqlite3 *db, sqlite3_stmt *stmt)
{
	char name[128];
	sprintf(name, "%s#baler_ptn", hostname);
	oparser_bind_text(db, stmt, 1, name, __FUNCTION__);
	oparser_bind_int64(db, stmt, 2, metric_id, __FUNCTION__);
	oparser_bind_text(db, stmt, 3, "Baler", __FUNCTION__);
	oparser_bind_int64(db, stmt, 4, mtype_id, __FUNCTION__);
	oparser_bind_text(db, stmt, 5, hostname, __FUNCTION__);
	oparser_bind_int64(db, stmt, 6, comp_id, __FUNCTION__);
	oparser_finish_insert(db, stmt, __FUNCTION__);
}

void process_baler_hosts(struct oparser_cmd *hostcmd)
{
	char *attrs_values = strdup(hostcmd->attrs_values);
	int num_baler_hosts, num;
	struct oparser_name_queue host_nqueue;
	char key[16], value[512];
	char tmp_attr[512];

	uint32_t max_mtype_id;
	oquery_max_metric_type_id(db, &max_mtype_id);
	max_mtype_id++;

	num = sscanf(attrs_values, "%[^:]:%s", key, value);
	if (num != 2) {
		fprintf(stderr, "Invalid attr:value '%s'. Expecting "
				"'names:[hostnames]'", attrs_values);
		exit(EINVAL);
	}

	num_baler_hosts = process_string_name(value, &host_nqueue, NULL, NULL);
	hostcmd->attrs_values[0] = '\0';

	char *stmt_s = "INSERT INTO metrics(name, metric_id, sampler, "
			"metric_type_id, coll_comp, prod_comp_id, path)"
				" VALUES(@name, @mid, @sampler, @mtid, "
						"@cllc, @pcid, NULL)";

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
	struct oparser_name *nhname;
	uint32_t comp_id;
	uint64_t metric_id;
	nhname = TAILQ_FIRST(&host_nqueue);
	for (i = 0; i < num_baler_hosts; i++) {
		oquery_comp_id_by_name(nhname->name, &comp_id, db);
		metric_id = gen_metric_id(comp_id, max_mtype_id);
		insert_baler_metric(metric_id, max_mtype_id, comp_id,
						nhname->name, db, stmt);
		if (i == 0) {
			sprintf(hostcmd->attrs_values, "%s:%" PRIu64,
					nhname->name, metric_id);
		} else {
			sprintf(hostcmd->attrs_values, "%s;%s:%" PRIu64,
					hostcmd->attrs_values, nhname->name,
					metric_id);
		}
		nhname = TAILQ_NEXT(nhname, entry);
	}
	free(attrs_values);

	sqlite3_finalize(stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &sqlite_err);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, sqlite_err);
		exit(rc);
	}
}

void postprocess_baler_cmdlist(struct oparser_cmd_queue *queue)
{
	TAILQ_FOREACH(cmd, queue, entry)
		if (strcmp(cmd->verb, "hosts") == 0)
			process_baler_hosts(cmd);
}

static struct kw label_tbl[] = {
		{ "commands", handle_commands },
		{ "hostname", handle_hostname },
		{ "service", handle_service }
};

void oparser_service_conf_parser(FILE *_conf)
{
	conf = _conf;

	char buf[1024];
	char key[128], value[512], tmp[128];
	char *s;
	int num_leading_tabs;

	struct kw keyword;
	struct kw *kw;

	fseek(conf, 0, SEEK_SET);

	while (s = fgets(buf, sizeof(buf), conf)) {
		sscanf(buf, "%[^:]: %[^#\t\n]", tmp, value);
		num_leading_tabs = count_leading_tabs(tmp);
		trim_trailing_space(value);
		sscanf(tmp, " %s", key);

		/* Ignore the comment line */
		if (key[0] == '#')
			continue;

		keyword.token = key;

		if (num_leading_tabs < 2) {
			/* hostname, service and features */

			keyword.token = key;
			kw = bsearch(&keyword, label_tbl, ARRAY_SIZE(label_tbl),
					sizeof(*kw), kw_comparator);

			if (kw) {
				kw->action(value);
			} else {
				fprintf(stderr, "Invalid key '%s'\n", key);
				exit(EINVAL);
			}
		} else if (num_leading_tabs == 2) {
			cmd = malloc(sizeof(*cmd));
			if (!cmd)
				oom_report(__FUNCTION__);

			sprintf(cmd->verb, "%s", key);
			cmd->attrs_values[0] = '\0';
			TAILQ_INSERT_TAIL(cmd_queue, cmd, entry);
		} else if (num_leading_tabs == 3) {
			if (strlen(cmd->attrs_values) == 0)
				sprintf(cmd->attrs_values, "%s:%s",
						key, value);
			else
				sprintf(cmd->attrs_values, "%s;%s:%s",
						cmd->attrs_values, key, value);
		} else {
			fprintf(stderr, "(%s: %s). Exceed expected number of "
					"leading tabs.\n",
					key, value);
			exit(EPERM);
		}
	}

	struct oparser_name *host = TAILQ_FIRST(&agg_host_list);
	TAILQ_FOREACH(host, &agg_host_list, entry) {
		hservices = (struct oparser_host_services *)
				str_map_get(host_scfg, host->name);
		if (!hservices) {
			fprintf(stderr, "Could not find host '%s'\n",
							host->name);
			exit(ENOENT);
		}
		cmd_queue = &hservices->queue[OVIS_LDMSD_AGG];
		postprocess_agg_cmdlist(cmd_queue);
	}
	destroy_name_list(&agg_host_list);

	host = TAILQ_FIRST(&baler_host_list);
	TAILQ_FOREACH(host, &baler_host_list, entry) {
		hservices = (struct oparser_host_services *)
				str_map_get(host_scfg, host->name);
		if (!hservices) {
			fprintf(stderr, "Could not find host '%s'\n",
							host->name);
			exit(ENOENT);
		}
		cmd_queue = &hservices->queue[OVIS_BALER];
		postprocess_baler_cmdlist(cmd_queue);
	}
	destroy_name_list(&baler_host_list);
}

void print_service(FILE *out, struct oparser_cmd_queue *list)
{
	char *cfg, *tmp;
	TAILQ_FOREACH(cmd, list, entry) {
		fprintf(out, "\t\t%s:\n", cmd->verb);
		cfg = strdup(cmd->attrs_values);
		tmp = strtok(cfg, ";");
		while (tmp) {
			fprintf(out, "\t\t\t%s\n", tmp);
			tmp = strtok(NULL, ";");
		}
		free(cfg);
	}
}

void oparser_print_service_conf(FILE *out)
{
	int i;
	struct oparser_name *host;
	TAILQ_FOREACH(host, &host_queue, entry) {
		fprintf(out, "hostname: %s\n", host->name);
		hservices = (struct oparser_host_services *)
				str_map_get(host_scfg, host->name);
		if (!hservices) {
			fprintf(stderr, "Could not find host '%s'\n",
							host->name);
			exit(ENOENT);
		}

		for (i = 0; i < OVIS_NUM_SERVICES; i++) {
			cmd_queue = &hservices->queue[i];
			if (TAILQ_EMPTY(cmd_queue))
				continue;

			fprintf(out, "\tservice: %s\n",
					enum_ovis_service_to_str(i));
			fprintf(out, "\tfeatures:\n");
			print_service(out, cmd_queue);
		}
	}
}

void oparser_services_to_sqlite(sqlite3 *db)
{
	oparser_drop_table("services", db);
	char *stmt_s;
	stmt_s = "CREATE TABLE IF NOT EXISTS services (" \
			" hostname	CHAR(128)	NOT NULL," \
			" service	CHAR(16)	NOT NULL," \
			" cmd_id	INTEGER PRIMARY KEY AUTOINCREMENT," \
			" verb		CHAR(32)	NOT NULL," \
			" attr_value	TEXT);";
	char *index_stmt = "CREATE INDEX IF NOT EXISTS services_idx" \
			" ON services(hostname, service);";

	create_table(stmt_s, index_stmt, db);

	struct oparser_host_services *services;

	struct oparser_name *hostname;
	struct oparser_cmd *cmd;
	stmt_s = "INSERT INTO services(hostname, service, verb, "
			"attr_value) VALUES(@hostname, @service, "
						"@verb, @attr)";
	char *service_s;
	char insert_stmt[4096];
	sqlite3_stmt *stmt;
	char *errmsg;

	int rc;
	rc = sqlite3_prepare_v2(db, stmt_s, strlen(stmt_s), &stmt,
					(const char **)&errmsg);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, errmsg);
		exit(rc);
	}

	rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, errmsg);
		exit(rc);
	}

	int i;
	TAILQ_FOREACH(hostname, &host_queue, entry) {
		services = (struct oparser_host_services *)
				str_map_get(host_scfg, hostname->name);
		if (!services) {
			fprintf(stderr, "Could not find the services "
					"for '%s'\n", hostname->name);
			exit(ENOENT);
		}
		for (i = 0; i < OVIS_NUM_SERVICES; i++) {
			service_s = enum_ovis_service_to_str(i);
			TAILQ_FOREACH(cmd, &services->queue[i], entry) {

				oparser_bind_text(db, stmt, 1, hostname->name,
								__FUNCTION__);
				oparser_bind_text(db, stmt, 2, service_s,
								__FUNCTION__);
				oparser_bind_text(db, stmt, 3, cmd->verb,
								__FUNCTION__);
				oparser_bind_text(db, stmt, 4, cmd->attrs_values,
								__FUNCTION__);
				oparser_finish_insert(db, stmt, __FUNCTION__);
			}
		}
	}

	sqlite3_finalize(stmt);

	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &errmsg);
	if (rc) {
		fprintf(stderr, "%s: %s\n", __FUNCTION__, errmsg);
		exit(rc);
	}
}

#ifdef MAIN
#include <getopt.h>
#include <stdarg.h>

#define FMT "c:o:d:"

int main(int argc, char **argv) {
	int op;
	char *comp_file, *output_file, *db_path;
	FILE *conf, *outf;
	int rc;
	char *errmsg;

	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'c':
			comp_file = strdup(optarg);
			break;
		case 'd':
			db_path = strdup(optarg);
			break;
		case 'o':
			output_file = strdup(optarg);
			break;
		default:
			fprintf(stderr, "Invalid argument '%c'\n", op);
			exit(EINVAL);
			break;
		}
	}
	conf = fopen(comp_file, "r");
	if (!conf) {
		fprintf(stderr, "Cannot open the file '%s'\n", comp_file);
		exit(errno);
	}

	outf = fopen(output_file, "w");
	if (!outf) {
		fprintf(stderr, "Cannot open the file '%s'\n", output_file);
		exit(errno);
	}

	sqlite3 *_db;
	rc = sqlite3_open_v2(db_path, &_db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	if (rc) {
		fprintf(stderr, "Failed to open sqlite '%s': %s\n",
				db_path, sqlite3_errmsg(_db));
		sqlite3_close(_db);
		sqlite3_free(errmsg);
		exit(rc);
	}

	oparser_service_conf_init(_db);
	oparser_service_conf_parser(conf);
	oparser_print_service_conf(outf);

	oparser_services_to_sqlite(db);
	printf("finish creating the services table\n");

	return 0;
}
#endif
