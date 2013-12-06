/*
 * model_event_parser.h
 *
 *  Created on: Oct 22, 2013
 *      Author: nichamon
 */

#ifndef MODEL_EVENT_PARSER_H_
#define MODEL_EVENT_PARSER_H_

#include <sys/queue.h>

enum mae_me_sevl {
	MAE_NOMINAL = 0,
	MAE_INFO,
	MAE_WARNING,
	MAE_CRITICAL,
	MAE_NUM_LEVELS
};

struct oparser_model {
	char *name;
	int model_id;
	char *thresholds;
	char *params;
	char report_flags[MAE_NUM_LEVELS];
	TAILQ_ENTRY(oparser_model) entry;
};
TAILQ_HEAD(oparser_model_q, oparser_model);

struct oparser_action {
	char *name;
	char *execute;
	TAILQ_ENTRY(oparser_action) entry;
};
TAILQ_HEAD(oparser_action_q, oparser_action);

struct oparser_sev_level {
	char *msg;
	char action_name[512];
};

struct metric_id_s {
	uint64_t *metric_ids;
	int num_metric_ids;
	LIST_ENTRY(metric_id_s) entry;
};
LIST_HEAD(metric_id_list, metric_id_s);

struct oparser_event {
	int event_id;
	char *name;
	int model_id;
	struct metric_id_list mid_list;
	int num_metric_id_set;
	struct oparser_sev_level msg_level[MAE_NUM_LEVELS];
	TAILQ_ENTRY(oparser_event) entry;
};
TAILQ_HEAD(oparser_event_q, oparser_event);

void oparser_mae_parser_init();

void oparser_models_to_sqlite();
void oparser_actions_to_sqlite();
void oparser_events_to_sqlite();

#endif /* MODEL_EVENT_PARSER_H_ */
