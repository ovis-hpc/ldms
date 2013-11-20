/*
 * template_parser.h
 *
 *  Created on: Oct 17, 2013
 *      Author: nichamon
 */

#ifndef TEMPLATE_PARSER_H_
#define TEMPLATE_PARSER_H_

#include <sys/queue.h>
#include <stdio.h>

#include "oparser_util.h"

struct set {
	char *ldmsd_set_name;
	char cfg[1024];
	struct metric_list mlist;
	uint8_t is_synchronous;
	LIST_ENTRY(set) entry;
};
LIST_HEAD(set_list, set);

struct template {
	struct oparser_component *comp;
	struct set_list slist;
};

struct template_def {
	char *name;
	struct oparser_component_type *comp_type;
	struct template *templates;
	int num_tmpls;
	LIST_ENTRY(template_def) entry;
};
LIST_HEAD(template_def_list, template_def);

struct prod_comps {
	struct oparser_component_type *comp_type;
	struct oparser_component_list clist;
};

void oparser_template_parser_init(FILE *_log_fp);

struct template_def_list *oparser_parse_template(FILE *conf,
				struct oparser_scaffold *scaffold);

void oparser_metrics_to_sqlite(struct template_def_list *tmpl_def_list,
								sqlite3 *db);

void oparser_templates_to_sqlite(struct template_def_list *tmpl_def_list,
								sqlite3 *db);

#endif /* TEMPLATE_PARSER_H_ */
