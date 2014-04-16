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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <stdarg.h>
#include <getopt.h>
#include <semaphore.h>
#include <string.h>
#include <limits.h>

#include "oparser_util.h"
#include "template_parser.h"
#include "component_parser.h"
#include "model_event_parser.h"
#include "service_parser.h"


#define OVIS_DB "ovis_conf.db"

#define COMP_NAME "components"
#define TMPL_NAME "sampler_templates"
#define MAE_NAME "models_n_rules"
#define SVC_NAME "services"

#define OVIS_CONF(X) X ".conf"
#define OVIS_OUTPUT(X) X ".o"

const char *out_path;
const char *comp_path = NULL;
const char *tmpl_path = NULL;
const char *mae_path = NULL;
const char *service_path = NULL;

FILE *comp_conf;
FILE *tmpl_conf;
FILE *mae_conf;
FILE *service_conf;

FILE *comp_o;
FILE *tmpl_o;
FILE *mae_o;
FILE *service_o;

int is_replaced_table = 0;
int is_printed = 0;

sqlite3 *ovis_db;

#define FMT "c:t:m:s:o:p"
void usage(char *argv[])
{
	printf("%s: [%s]\n", argv[0], FMT);
	printf("   -c comp_file		The path to the component definition configuration file.\n");
	printf("   -t tmpl_file		The path to the sampler configuration file.\n");
	printf("   -m mae_file		The path to the model, event and action configuration file.\n");
	printf("   -s service_file	The path to the service configuration file.\n");
	printf("   -o output_path	The path to the directory for the outputs and database\n");
	printf("   -p			Print configuration to output files. The files will be in\n"
	       "			the output directory.\n");
}

void oparser_open_file(const char *path, FILE **f, const char *mode)
{
	*f = fopen(path, mode);
	if (!*f) {
		fprintf(stderr, "Couldn't open file '%s'.\n", path);
		exit(errno);
	}
}

int main(int argc, char **argv) {
	int rc;
	int op;

	while ((op = getopt(argc, argv, FMT)) != -1) {
		switch (op) {
		case 'c':
			comp_path = strdup(optarg);
			break;
		case 't':
			tmpl_path = strdup(optarg);
			break;
		case 'm':
			mae_path = strdup(optarg);
			break;
		case 's':
			service_path = strdup(optarg);
			break;
		case 'o':
			out_path = strdup(optarg);
			break;
		case 'f':
			is_replaced_table = 1;
			break;
		case 'p':
			is_printed = 1;
			break;
		default:
			fprintf(stderr, "Invalid argument '%c'\n", op);
			usage(argv);
			exit(EINVAL);
		}
	}

	if (!out_path) {
		fprintf(stderr, "Required '-o'.\n");
		usage(argv);
		exit(ENAVAIL);
	}

	if (tmpl_path) {
		if (!comp_path) {
			fprintf(stderr, "Need the component configuration "
								"file.\n");
			exit(EINVAL);
		}
	}

	char path[PATH_MAX];
	char *zErrMsg = 0;

	sprintf(path, "%s/%s", out_path, OVIS_DB);
	rc = sqlite3_open_v2(path, &ovis_db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	if (rc) {
		fprintf(stderr, "Failed to open sqlite '%s': %s\n",
				path, sqlite3_errmsg(ovis_db));
		sqlite3_close(ovis_db);
		sqlite3_free(zErrMsg);
		exit(rc);
	}

	struct building_sqlite_table btable;
	char *read_buf = malloc(MAIN_BUF_SIZE);
	char *value_buf = malloc(MAIN_BUF_SIZE);

	if (comp_path) {
		oparser_open_file(comp_path, &comp_conf, "r");
		oparser_component_parser_init(stderr, read_buf, value_buf);

		struct oparser_scaffold *scaffold = NULL;
		scaffold = oparser_parse_component_def(comp_conf);
		if (!scaffold) {
			fprintf(stderr, "Failed to parse the component "
					"configuration file.\n");
			return -1;
		}

		oparser_scaffold_to_sqlite(scaffold, ovis_db);
		printf("Complete table 'components'\n");

		if (is_printed) {
			sprintf(path, "%s/%s", out_path,
					OVIS_OUTPUT(COMP_NAME));
			oparser_open_file(path, &comp_o, "w");
//			oparser_print_scaffold(scaffold, comp_o);
		}

		if (tmpl_path) {
			oparser_open_file(tmpl_path, &tmpl_conf, "r");

			oparser_template_parser_init(stderr, read_buf, value_buf);
			struct tmpl_list *all_tmpl_list = NULL;
			all_tmpl_list = oparser_parse_template(tmpl_conf,
								scaffold);
			if (!all_tmpl_list) {
				fprintf(stderr, "Failed to parse the sampler"
					" template configuration file.\n");
				return -1;
			}

			oparser_templates_to_sqlite(all_tmpl_list, ovis_db);
			printf("Complete table 'templates'\n");
			oparser_metrics_to_sqlite(all_tmpl_list, ovis_db);
			printf("Complete table 'metrics'\n");
			if (is_printed) {
				sprintf(path, "%s/%s", out_path,
						OVIS_OUTPUT(TMPL_NAME));
				oparser_open_file(path, &tmpl_o, "w");
				oparser_print_template_list(
						all_tmpl_list, tmpl_o);
			}
		}
	}

	if (service_path) {
		oparser_open_file(service_path, &service_conf, "r");


		oparser_service_conf_init(ovis_db, read_buf, value_buf);
		oparser_service_conf_parser(service_conf);
		oparser_services_to_sqlite(ovis_db);
		printf("Complete table 'services'\n");
		if (is_printed) {
			sprintf(path, "%s/%s", out_path,
					OVIS_OUTPUT(SVC_NAME));
			oparser_open_file(path, &service_o, "w");
			oparser_print_service_conf(service_o);
		}
	}

	if (mae_path) {
		oparser_open_file(mae_path, &mae_conf, "r");

		oparser_mae_parser_init(ovis_db, read_buf, value_buf);
		oparser_parse_model_event_conf(mae_conf);
		oparser_models_to_sqlite();
		printf("Complete table 'models'\n");
		oparser_actions_to_sqlite();
		printf("Complete table 'actions'\n");
		oparser_events_to_sqlite();
		printf("Complete table 'event_templates'\n");
		printf("Complete table 'rules'\n");

		if (is_printed) {
			sprintf(path, "%s/%s", out_path,
					OVIS_OUTPUT(MAE_NAME));
			oparser_open_file(path, &mae_o, "w");
			oparser_print_models_n_rules(mae_o);
		}
	}

	return 0;
}
