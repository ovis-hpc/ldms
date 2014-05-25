/*
 * cable_parser.h
 *
 *  Created on: May 14, 2014
 *      Author: nichamon
 */

#ifndef CABLE_PARSER_H_
#define CABLE_PARSER_H_

#include <sys/queue.h>
#include "oparser_util.h"
#include "component_parser.h"

struct cable_type {
	char *type;
	char *description;
	LIST_ENTRY(cable_type) entry;
};
LIST_HEAD(cable_type_list, cable_type);

struct cable {
	int type_id;
	uint32_t comp_id;
	char *type;
	uint32_t src;
	uint32_t dest;
	char *state;
	char *name;
	LIST_ENTRY(cable) entry;
};
LIST_HEAD(cable_list, cable);

/**
 * \brief Initialize the cable parser
 */
void oparser_cable_parser_init(FILE *log_file, char *read_buf, char *value_buf);

/**
 * \brief Parse the cable definitions in \c conff
 *
 * \param[in]   conff   The configuration file
 */
void oparser_parse_cable_def(FILE *conff);

#endif /* CABLE_PARSER_H_ */
