/*
 * component_parser.h
 *
 *  Created on: Oct 14, 2013
 *      Author: nichamon
 */

#ifndef COMPONENT_PARSER_H_
#define COMPONENT_PARSER_H_

#include <sys/queue.h>
#include "oparser_util.h"


struct src_array {
	struct oparser_component **comp_array;
	int level;
	int num_comps;
	LIST_ENTRY(src_array) entry;
};
LIST_HEAD(src_list, src_array);

/**
 * \brief Initialize the Component Parser
 *
 */
void oparser_component_parser_init();

/**
 * \brief Parse the component definitions in \c conf_file
 *
 * \param[in]   conf_file   The configuration file
 *
 */
struct oparser_scaffold *oparser_parse_component_def(FILE *conff);

/**
 * \brief Create a scaffold representing the system
 *
 * \param[in]   type_list   The parsed component definition
 *
 * \return The component type list containing the relationship among the components
 */
struct oparser_scaffold *oparser_create_scaffold();

/**
 * \brief Print the scaffold \c list to the \c output_file
 *
 * \param[in]   list   The component type list obtaining from \c oparser_create_scaffold
 * \param[in]   outputf   the file to print the components
 *
 */
void oparser_print_component_def(struct oparser_component_type_list *list, FILE *outputf);

void oparser_print_scaffold(struct oparser_scaffold *scaffold, FILE *outf);

void oparser_scaffold_to_sqlite(struct oparser_scaffold *scaffold, sqlite3 *db);
#endif /* COMPONENT_PARSER_H_ */

