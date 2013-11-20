/*
 * OVIS_db_query.h
 *
 *  Created on: Oct 24, 2013
 *      Author: nichamon
 */

#ifndef OVIS_DB_QUERY_H_
#define OVIS_DB_QUERY_H_

#include <sqlite3.h>

#include "oparser_util.h"

void oquery_metric_id(char *metric_name, char *prod_comp_type,
                                struct mae_metric_list *list,
				char *comp_names, sqlite3 *db);

void oquery_num_sampling_nodes( int *num_sampling_nodes, sqlite3 *db);

void oquery_comp_id_by_name(char *name, uint32_t *comp_id, sqlite3 *db);

#endif /* OVIS_DB_QUERY_H_ */
